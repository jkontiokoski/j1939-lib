/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/*
 * pgn_listener: receives the PGNs given on the command line and prints every
 * message, whether it came in one frame or was reassembled by the transport
 * protocol (BAM or RTS/CTS). A DM1 is also decoded. The listener answers
 * Requests for Software Identification (PGN 65242) with the library version.
 *
 *   pgn_listener [-i ifname] [-a address] [-n identity] PGN...
 *
 *   -i  CAN interface, default vcan0
 *   -a  preferred address, default 0x90; RTS/CTS senders use it as destination
 *   -n  identity number of the NAME, default 2
 *
 * Example, listening to DM1 and Proprietary A:
 *
 *   ./pgn_listener -a 0x90 0xFECA 0xEF00
 *
 * Ask it for its software identification from another shell:
 *
 *   cansend vcan0 18EA90F9#DAFE00      # Request from 0xF9 to 0x90 for PGN 0xFEDA
 *
 * Stop with Ctrl-C.
 */

#define _POSIX_C_SOURCE 200809L /* getopt, poll */

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "example_common.h"
#include "j1939/j1939.h"
#include "j1939_socketcan.h"

/** Software Identification, PGN 65242. Variable length, so answered with TP if long. */
#define PGN_SOFT 0xFEDAU

#define TX_LEN      16U
#define MSG_LEN     8U
#define TP_RX_LEN   2U /* Two multi-packet receptions at a time, e.g. a BAM and an RTS/CTS. */
#define TP_TX_LEN   1U /* One multi-packet send: the Software Identification answer. */
#define RX_PGNS_MAX 16U
#define DTCS_MAX    16U

/*
 * Integrator-owned memory. A TP buffer holds one message of up to
 * J1939_CFG_TP_BUF_SIZE bytes. A reassembled message stays in its buffer
 * until the application releases it with j1939_msg_pop().
 */
static j1939_port_frame_t tx_buf[TX_LEN];
static j1939_msg_slot_t msg_buf[MSG_LEN];
static j1939_tp_buf_t tp_rx_buf[TP_RX_LEN];
static j1939_tp_buf_t tp_tx_buf[TP_TX_LEN];

/* Filled from the command line before j1939_init(); the stack only reads it. */
static uint32_t rx_pgns[RX_PGNS_MAX];

/* PGNs this node answers Requests for. A Request for any other PGN sent to
 * this node's address is NACKed by the stack. */
static const uint32_t req_pgns[] = {PGN_SOFT};

static j1939_t stack;

static void usage(const char *prog) {
	(void)fprintf(stderr,
	              "usage: %s [-i ifname] [-a address] [-n identity] PGN...\n"
	              "  -i  CAN interface (default " EXAMPLE_IFNAME_DEFAULT ")\n"
	              "  -a  preferred address 0..253 (default 0x90)\n"
	              "  -n  identity number of the NAME (default 2)\n"
	              "  PGN PGNs to receive, e.g. 0xFECA (DM1) 0xEF00 (Proprietary A)\n",
	              prog);
}

/* Decodes a DM1 in place from its message slot. */
static void dm1_print(const j1939_msg_t *msg) {
	j1939_diag_lamps_t lamps;
	j1939_diag_dtc_t dtcs[DTCS_MAX];
	uint16_t count = 0U;
	j1939_ret_t ret = j1939_diag_dm_parse(msg->data, msg->len, &lamps, dtcs, DTCS_MAX, &count);

	if ((ret == J1939_RET_OK) || (ret == J1939_RET_ERR_FULL)) {
		(void)printf("           DM1: MIL %u red %u amber %u protect %u, %u DTC(s)\n",
		             (unsigned)lamps.mil, (unsigned)lamps.red_stop,
		             (unsigned)lamps.amber_warning, (unsigned)lamps.protect,
		             (unsigned)count);
		for (uint16_t i = 0U; (i < count) && (i < DTCS_MAX); i++) {
			(void)printf("           SPN %lu FMI %u OC %u\n",
			             (unsigned long)dtcs[i].spn, (unsigned)dtcs[i].fmi,
			             (unsigned)dtcs[i].oc);
		}
	} else {
		(void)printf("           DM1: malformed\n");
	}
}

/*
 * Answers a Request for Software Identification. J1939/21: the answer goes
 * to the requester when the Request was sent to this node and the PGN is
 * PDU1; to the global address otherwise. PGN 65242 is PDU2, so it is
 * broadcast, and at more than 8 bytes j1939_send() uses BAM.
 */
static void request_answer(j1939_ca_id_t ca, const j1939_msg_t *req) {
	uint32_t pgn = 0U;
	uint32_t v = j1939_version_get();
	uint8_t data[32];
	int n;

	if ((j1939_request_pgn_get(req, &pgn) != J1939_RET_OK) || (pgn != PGN_SOFT)) {
		return;
	}
	/* Byte 1: number of fields; then each field terminated by '*'. */
	data[0] = 1U;
	n = snprintf((char *)&data[1], sizeof(data) - 1U, "j1939-lib %u.%u.%u*",
	             (unsigned)((v >> 16) & 0xFFU), (unsigned)((v >> 8) & 0xFFU),
	             (unsigned)(v & 0xFFU));
	if ((n <= 0) || ((size_t)n >= (sizeof(data) - 1U))) {
		return;
	}

	const j1939_msg_t answer = {
	        .pgn = PGN_SOFT,
	        .prio = J1939_PRIO_DEFAULT,
	        .da = (j1939_pgn_is_pdu1(pgn) && (req->da != J1939_ADDR_GLOBAL))
	                      ? req->sa
	                      : J1939_ADDR_GLOBAL,
	        .len = (uint16_t)(1 + n),
	        .data = data,
	};
	j1939_ret_t ret = j1939_send(&stack, ca, &answer);
	(void)printf("[%8.3f] Request for PGN 0x%05X from 0x%02X: answer %u bytes, %s\n",
	             example_uptime_s(), (unsigned)pgn, (unsigned)req->sa, (unsigned)answer.len,
	             (ret == J1939_RET_OK) ? "queued" : "not sent (busy or no address)");
}

int main(int argc, char **argv) {
	const char *ifname = EXAMPLE_IFNAME_DEFAULT;
	uint32_t address = 0x90U;
	uint32_t identity = 2U;
	uint16_t n_pgns = 0U;
	int fd = -1;
	int opt;

	example_start();
	while ((opt = getopt(argc, argv, "i:a:n:")) != -1) {
		switch (opt) {
		case 'i':
			ifname = optarg;
			break;
		case 'a':
			if (!example_parse_u32(optarg, 253U, &address)) {
				usage(argv[0]);
				return EXIT_FAILURE;
			}
			break;
		case 'n':
			if (!example_parse_u32(optarg, J1939_NAME_IDENTITY_MAX, &identity)) {
				usage(argv[0]);
				return EXIT_FAILURE;
			}
			break;
		default:
			usage(argv[0]);
			return EXIT_FAILURE;
		}
	}
	for (; (optind < argc) && (n_pgns < RX_PGNS_MAX); optind++) {
		if (!example_parse_u32(argv[optind], J1939_PGN_MAX, &rx_pgns[n_pgns])) {
			usage(argv[0]);
			return EXIT_FAILURE;
		}
		n_pgns++;
	}
	if ((n_pgns == 0U) || (optind < argc)) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	const j1939_cfg_t cfg = {
	        .tx_buf = tx_buf,
	        .tx_len = TX_LEN,
	        .msg_buf = msg_buf,
	        .msg_len = MSG_LEN,
	        .rx_pgns = rx_pgns,
	        .rx_pgns_len = n_pgns,
	        .req_pgns = req_pgns,
	        .req_pgns_len = (uint16_t)(sizeof(req_pgns) / sizeof(req_pgns[0])),
	        .tp_tx_buf = tp_tx_buf,
	        .tp_tx_buf_len = TP_TX_LEN,
	        .tp_rx_buf = tp_rx_buf,
	        .tp_rx_buf_len = TP_RX_LEN,
	};
	if (j1939_init(&stack, &cfg) != J1939_RET_OK) {
		/* A PDU1 PGN (0xEF00 and below) must have 0 in its lowest byte. */
		(void)fprintf(stderr, "j1939_init failed: check the PGNs\n");
		return EXIT_FAILURE;
	}

	const j1939_ca_cfg_t ca_cfg = {
	        .address = (uint8_t)address,
	        .name = example_name(identity, false),
	};
	j1939_ca_id_t ca;
	if (j1939_ca_add(&stack, &ca_cfg, &ca) != J1939_RET_OK) {
		(void)fprintf(stderr, "j1939_ca_add failed\n");
		return EXIT_FAILURE;
	}

	if (j1939_socketcan_open(ifname, &fd) != J1939_RET_OK) {
		(void)fprintf(stderr, "cannot open CAN interface %s\n", ifname);
		return EXIT_FAILURE;
	}

	(void)printf("[%8.3f] listening on %s at address 0x%02X for %u PGN(s)\n",
	             example_uptime_s(), ifname, (unsigned)address, (unsigned)n_pgns);

	uint64_t last_us = example_now_us();
	j1939_addr_state_t shown_state = J1939_ADDR_STATE_UNCLAIMED;
	int status = EXIT_SUCCESS;

	while (example_running()) {
		/* a. Sleep until a frame arrives, at most one tick. */
		struct pollfd pfd = {.fd = fd, .events = POLLIN, .revents = 0};
		if (j1939_tx_peek(&stack) != NULL) {
			pfd.events = (short)(POLLIN | POLLOUT);
		}
		(void)poll(&pfd, 1, EXAMPLE_TICK_MS);

		/* b. Socket -> stack. Each frame is handled as it is read. */
		if (j1939_socketcan_rx(fd, &stack, EXAMPLE_RX_PER_TICK, NULL) != J1939_RET_OK) {
			(void)fprintf(stderr, "CAN receive error\n");
			status = EXIT_FAILURE;
			break;
		}

		/* c. Run the stack's timers: claim, transport protocol, diagnostics. */
		(void)j1939_process(&stack, example_elapsed_us(&last_us));

		/* d. Application. Each message is read in place and released with
		 *    j1939_msg_pop(), which also frees a TP reassembly buffer. */
		const j1939_msg_t *msg;
		while ((msg = j1939_msg_peek(&stack)) != NULL) {
			if (msg->pgn == J1939_PGN_REQUEST) {
				request_answer(ca, msg);
			} else {
				example_msg_print(msg);
				if (msg->pgn == J1939_PGN_DM1) {
					dm1_print(msg);
				}
			}
			(void)j1939_msg_pop(&stack);
		}

		uint8_t now_address;
		j1939_addr_state_t now_state;
		(void)j1939_addr_get(&stack, ca, &now_address, &now_state);
		if (now_state != shown_state) {
			(void)printf("[%8.3f] address 0x%02X %s\n", example_uptime_s(),
			             (unsigned)now_address, example_state_name(now_state));
			shown_state = now_state;
		}

		/* e. tx queue -> socket. */
		if (j1939_socketcan_tx(fd, &stack, NULL) != J1939_RET_OK) {
			(void)fprintf(stderr, "CAN transmit error\n");
			status = EXIT_FAILURE;
			break;
		}
	}

	const j1939_stats_t *st = j1939_stats_get(&stack);
	(void)printf("stats: rx_msg_overflow %lu tx_overflow %lu tp_rx_aborted %lu "
	             "tp_rx_refused %lu tp_tx_aborted %lu\n",
	             (unsigned long)st->rx_msg_overflow, (unsigned long)st->tx_overflow,
	             (unsigned long)st->tp_rx_aborted, (unsigned long)st->tp_rx_refused,
	             (unsigned long)st->tp_tx_aborted);
	j1939_socketcan_close(fd);
	return status;
}
