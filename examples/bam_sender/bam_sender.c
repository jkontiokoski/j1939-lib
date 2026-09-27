/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/*
 * bam_sender: sends multi-packet messages with the transport protocol.
 *
 * - A DM1 with four DTCs (18 bytes, 3 packets), built with
 *   j1939_diag_dm_build(), broadcast with BAM to the global address.
 * - With -d, a 100 byte Proprietary A message (PGN 0xEF00, 15 packets) sent
 *   with RTS/CTS to the given destination.
 *
 *   bam_sender [-i ifname] [-a address] [-n identity] [-d dest] [-c count] [-p period_ms]
 *
 *   -i  CAN interface, default vcan0
 *   -a  preferred address, default 0x80
 *   -n  identity number of the NAME, default 3
 *   -d  destination of the RTS/CTS message; without it only the DM1 is sent
 *   -c  number of rounds, default 0: until Ctrl-C
 *   -p  period of the rounds in ms, default 1000 (the DM1 rate)
 *
 * Run pgn_listener first so that the destination exists:
 *
 *   ./pgn_listener -a 0x90 0xFECA 0xEF00 &
 *   ./bam_sender -a 0x80 -d 0x90 -c 3
 *
 * j1939_send() hands the message to the transport protocol and returns; the
 * data packets are sent by later j1939_process() calls. The stack does not
 * report when a transfer has finished. A transfer that fails (Connection
 * Abort or timeout) is counted in j1939_stats_t::tp_tx_aborted, which this
 * example prints when it changes.
 */

#define _POSIX_C_SOURCE 200809L /* getopt, poll */

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "example_common.h"
#include "j1939/j1939.h"
#include "j1939_socketcan.h"

#define PGN_PROP_A 0xEF00U /* Proprietary A: PDU1, destination specific. */
#define PROP_A_LEN 100U

#define RX_LEN    32U
#define TX_LEN    32U /* Room for a whole CTS window of data packets. */
#define MSG_LEN   4U
#define TP_TX_LEN 2U /* The BAM and the RTS/CTS transfer run at the same time. */

/*
 * Linger after the last round so that a running RTS/CTS transfer can end:
 * longer than T3, the time the originator waits for CTS or EndOfMsgAck.
 */
#define LINGER_US (J1939_TP_T3_US + 500000U)

static j1939_port_frame_t rx_buf[RX_LEN];
static j1939_port_frame_t tx_buf[TX_LEN];
static j1939_msg_slot_t msg_buf[MSG_LEN];
static j1939_tp_buf_t tp_tx_buf[TP_TX_LEN];

static j1939_t stack;

/* Example DTCs: invented faults with plausible SPN/FMI pairs. */
static const j1939_diag_dtc_t dtcs[] = {
        {.spn = 100U, .fmi = 1U, .oc = 3U, .cm = J1939_DIAG_CM_V4},     /* Oil pressure low */
        {.spn = 110U, .fmi = 0U, .oc = 1U, .cm = J1939_DIAG_CM_V4},     /* Coolant temp high */
        {.spn = 190U, .fmi = 2U, .oc = 7U, .cm = J1939_DIAG_CM_V4},     /* Speed erratic */
        {.spn = 520192U, .fmi = 31U, .oc = 1U, .cm = J1939_DIAG_CM_V4}, /* Proprietary SPN */
};

static void usage(const char *prog) {
	(void)fprintf(stderr,
	              "usage: %s [-i ifname] [-a address] [-n identity] [-d dest] [-c count] "
	              "[-p period_ms]\n"
	              "  -i  CAN interface (default " EXAMPLE_IFNAME_DEFAULT ")\n"
	              "  -a  preferred address 0..253 (default 0x80)\n"
	              "  -n  identity number of the NAME (default 3)\n"
	              "  -d  destination address of the RTS/CTS message 0..253\n"
	              "  -c  rounds to send, 0 = until Ctrl-C (default 0)\n"
	              "  -p  period in ms (default 1000)\n",
	              prog);
}

static const char *ret_name(j1939_ret_t ret) {
	const char *name = "error";

	switch (ret) {
	case J1939_RET_OK:
		name = "queued";
		break;
	case J1939_RET_ERR_BUSY:
		name = "busy: previous transfer to this destination still running";
		break;
	case J1939_RET_ERR_FULL:
		name = "full: no free TP session, TP buffer or tx queue slot";
		break;
	case J1939_RET_ERR_NO_ADDRESS:
		name = "no address claimed";
		break;
	default:
		break;
	}
	return name;
}

/* One round: the DM1 with BAM, the proprietary message with RTS/CTS. */
static void round_send(j1939_ca_id_t ca, bool have_dest, uint8_t dest, uint32_t round) {
	static const j1939_diag_lamps_t lamps = {
	        .mil = J1939_DIAG_LAMP_OFF,
	        .red_stop = J1939_DIAG_LAMP_OFF,
	        .amber_warning = J1939_DIAG_LAMP_ON,
	        .protect = J1939_DIAG_LAMP_OFF,
	        .mil_flash = J1939_DIAG_FLASH_OFF,
	        .red_stop_flash = J1939_DIAG_FLASH_OFF,
	        .amber_warning_flash = J1939_DIAG_FLASH_OFF,
	        .protect_flash = J1939_DIAG_FLASH_OFF,
	};
	uint8_t dm1[J1939_DIAG_LAMPS_LEN + (sizeof(dtcs) / sizeof(dtcs[0])) * J1939_DIAG_DTC_LEN];
	uint16_t dm1_len = 0U;
	j1939_ret_t ret;

	/* DM1: lamp bytes and four DTCs, 2 + 4 * 4 = 18 bytes. Sent to the
	 * global address, so j1939_send() announces it with BAM. */
	ret = j1939_diag_dm_build(&lamps, dtcs, (uint16_t)(sizeof(dtcs) / sizeof(dtcs[0])), dm1,
	                          (uint16_t)sizeof(dm1), &dm1_len);
	if (ret == J1939_RET_OK) {
		const j1939_msg_t msg = {
		        .pgn = J1939_PGN_DM1,
		        .prio = J1939_DIAG_PRIO_DEFAULT,
		        .da = J1939_ADDR_GLOBAL,
		        .len = dm1_len,
		        .data = dm1,
		};
		ret = j1939_send(&stack, ca, &msg);
		(void)printf("[%8.3f] round %lu: DM1 %u bytes with BAM: %s\n", example_uptime_s(),
		             (unsigned long)round, (unsigned)dm1_len, ret_name(ret));
	}

	/* Proprietary A to one node: j1939_send() opens an RTS/CTS connection.
	 * The payload is copied into a TP buffer, so prop may go out of scope. */
	if (have_dest) {
		uint8_t prop[PROP_A_LEN];

		for (uint16_t i = 0U; i < PROP_A_LEN; i++) {
			prop[i] = (uint8_t)(round + i);
		}
		const j1939_msg_t msg = {
		        .pgn = PGN_PROP_A,
		        .prio = J1939_PRIO_DEFAULT,
		        .da = dest,
		        .len = PROP_A_LEN,
		        .data = prop,
		};
		ret = j1939_send(&stack, ca, &msg);
		(void)printf("[%8.3f] round %lu: PGN 0x%05X %u bytes with RTS/CTS to 0x%02X: %s\n",
		             example_uptime_s(), (unsigned long)round, (unsigned)PGN_PROP_A,
		             (unsigned)PROP_A_LEN, (unsigned)dest, ret_name(ret));
	}
}

int main(int argc, char **argv) {
	const char *ifname = EXAMPLE_IFNAME_DEFAULT;
	uint32_t address = 0x80U;
	uint32_t identity = 3U;
	uint32_t dest = J1939_ADDR_NULL;
	bool have_dest = false;
	uint32_t count = 0U;
	uint32_t period_ms = 1000U;
	int fd = -1;
	int opt;

	example_start();
	while ((opt = getopt(argc, argv, "i:a:n:d:c:p:")) != -1) {
		bool ok = true;

		switch (opt) {
		case 'i':
			ifname = optarg;
			break;
		case 'a':
			ok = example_parse_u32(optarg, 253U, &address);
			break;
		case 'n':
			ok = example_parse_u32(optarg, J1939_NAME_IDENTITY_MAX, &identity);
			break;
		case 'd':
			ok = example_parse_u32(optarg, 253U, &dest);
			have_dest = true;
			break;
		case 'c':
			ok = example_parse_u32(optarg, UINT32_MAX, &count);
			break;
		case 'p':
			ok = example_parse_u32(optarg, 3600000U, &period_ms) && (period_ms > 0U);
			break;
		default:
			ok = false;
			break;
		}
		if (!ok) {
			usage(argv[0]);
			return EXIT_FAILURE;
		}
	}

	/* No rx_pgns: this node receives nothing for the application. The
	 * stack still handles address claiming and the CTS / EndOfMsgAck /
	 * Connection Abort frames of its own RTS/CTS transfers. */
	const j1939_cfg_t cfg = {
	        .rx_buf = rx_buf,
	        .rx_len = RX_LEN,
	        .tx_buf = tx_buf,
	        .tx_len = TX_LEN,
	        .msg_buf = msg_buf,
	        .msg_len = MSG_LEN,
	        .tp_tx_buf = tp_tx_buf,
	        .tp_tx_buf_len = TP_TX_LEN,
	};
	if (j1939_init(&stack, &cfg) != J1939_RET_OK) {
		(void)fprintf(stderr, "j1939_init failed\n");
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

	j1939_queue_t *rx_q = j1939_rx_queue(&stack);
	j1939_queue_t *tx_q = j1939_tx_queue(&stack);
	const j1939_stats_t *stats = j1939_stats_get(&stack);
	uint64_t last_us = example_now_us();
	uint64_t next_round_us = 0U; /* 0: first round as soon as the address is claimed. */
	uint64_t done_us = 0U;       /* Set after the last round. */
	uint32_t rounds = 0U;
	uint32_t shown_aborts = 0U;
	bool claimed = false;
	int status = EXIT_SUCCESS;

	while (example_running()) {
		/* a. Sleep until a frame arrives, at most one tick. The tick also
		 *    paces the BAM data packets, see EXAMPLE_TICK_MS. */
		struct pollfd pfd = {.fd = fd, .events = POLLIN, .revents = 0};
		if (j1939_queue_count(tx_q) > 0U) {
			pfd.events = (short)(POLLIN | POLLOUT);
		}
		(void)poll(&pfd, 1, EXAMPLE_TICK_MS);

		/* b. Socket -> rx queue. */
		if (j1939_socketcan_rx(fd, rx_q, NULL) != J1939_RET_OK) {
			(void)fprintf(stderr, "CAN receive error\n");
			status = EXIT_FAILURE;
			break;
		}

		/* c. Run the stack. It sends the next BAM packet once the gap has
		 *    passed and answers CTS with the requested data packets. */
		(void)j1939_process(&stack, example_elapsed_us(&last_us));

		/* d. Application. Nothing is delivered (no rx_pgns), but the
		 *    queue is drained anyway as a habit. */
		while (j1939_msg_peek(&stack) != NULL) {
			(void)j1939_msg_pop(&stack);
		}

		uint8_t now_address;
		j1939_addr_state_t state;
		(void)j1939_addr_get(&stack, ca, &now_address, &state);
		if (!claimed && (state == J1939_ADDR_STATE_CLAIMED)) {
			(void)printf("[%8.3f] address 0x%02X claimed\n", example_uptime_s(),
			             (unsigned)now_address);
			claimed = true;
		}

		uint64_t now = example_now_us();
		if (claimed && (done_us == 0U) && (now >= next_round_us)) {
			rounds++;
			round_send(ca, have_dest, (uint8_t)dest, rounds);
			next_round_us = now + ((uint64_t)period_ms * 1000U);
			if ((count != 0U) && (rounds >= count)) {
				done_us = now;
			}
		}
		if (stats->tp_tx_aborted != shown_aborts) {
			(void)printf("[%8.3f] transfers aborted so far: %lu\n", example_uptime_s(),
			             (unsigned long)stats->tp_tx_aborted);
			shown_aborts = stats->tp_tx_aborted;
		}

		/* e. tx queue -> socket. */
		if (j1939_socketcan_tx(fd, tx_q, NULL) != J1939_RET_OK) {
			(void)fprintf(stderr, "CAN transmit error\n");
			status = EXIT_FAILURE;
			break;
		}

		if ((done_us != 0U) && ((now - done_us) >= LINGER_US)) {
			break;
		}
	}

	(void)printf("stats: tp_tx_aborted %lu tx_overflow %lu\n",
	             (unsigned long)stats->tp_tx_aborted, (unsigned long)stats->tx_overflow);
	j1939_socketcan_close(fd);
	return status;
}
