/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/*
 * addr_claim_demo: one Controller Application claims an address (J1939/81)
 * and prints every change of its claim state, and every Address Claimed it
 * sees on the bus.
 *
 *   addr_claim_demo [-i ifname] [-a address] [-n identity] [-A] [-r]
 *
 *   -i  CAN interface, default vcan0
 *   -a  preferred address, default 0x80
 *   -n  identity number of the NAME, default 1
 *   -A  the NAME is arbitrary address capable
 *   -r  first ask the network for its claims (global Request for Address Claimed)
 *
 * Arbitration: start two instances with the same address and different
 * identities. The lower NAME keeps the address. The loser moves to a free
 * address in 128..247 if it was started with -A, or sends Cannot Claim.
 *
 *   ./addr_claim_demo -n 1 &
 *   ./addr_claim_demo -n 2 -A     # moves to 0x81
 *   ./addr_claim_demo -n 3        # CANNOT_CLAIM
 *
 * Stop with Ctrl-C.
 */

#define _POSIX_C_SOURCE 200809L /* getopt, poll */

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "example_common.h"
#include "j1939/j1939.h"
#include "j1939_socketcan.h"

/*
 * Integrator-owned memory. The library allocates nothing: every queue and
 * buffer is a static array handed to j1939_init(). This demo sends and
 * receives only single frames, so it gives the stack no TP buffers.
 */
#define RX_LEN  32U
#define TX_LEN  16U
#define MSG_LEN 8U

static j1939_port_frame_t rx_buf[RX_LEN];
static j1939_port_frame_t tx_buf[TX_LEN];
static j1939_msg_slot_t msg_buf[MSG_LEN];

/* Address Claimed messages are delivered to the application only if their
 * PGN is listed; the stack handles them either way. */
static const uint32_t rx_pgns[] = {J1939_PGN_ADDRESS_CLAIMED};

static j1939_t stack;

static void usage(const char *prog) {
	(void)fprintf(stderr,
	              "usage: %s [-i ifname] [-a address] [-n identity] [-A] [-r]\n"
	              "  -i  CAN interface (default " EXAMPLE_IFNAME_DEFAULT ")\n"
	              "  -a  preferred address 0..253 (default 0x80)\n"
	              "  -n  identity number of the NAME (default 1); the lower NAME wins\n"
	              "  -A  arbitrary address capable NAME\n"
	              "  -r  send a global Request for Address Claimed at startup\n",
	              prog);
}

/* Prints an Address Claimed or Cannot Claim seen on the bus. */
static void claim_print(const j1939_msg_t *msg) {
	uint64_t name = 0U;

	if ((msg->len >= J1939_NAME_LEN) &&
	    (j1939_name_from_bytes(msg->data, &name) == J1939_RET_OK)) {
		(void)printf("[%8.3f] %s from 0x%02X, NAME 0x%016llX\n", example_uptime_s(),
		             (msg->sa == J1939_ADDR_NULL) ? "Cannot Claim  " : "Address Claimed",
		             (unsigned)msg->sa, (unsigned long long)name);
	}
}

int main(int argc, char **argv) {
	const char *ifname = EXAMPLE_IFNAME_DEFAULT;
	uint32_t address = 0x80U;
	uint32_t identity = 1U;
	bool arbitrary = false;
	bool request = false;
	int fd = -1;
	int opt;

	example_start();
	while ((opt = getopt(argc, argv, "i:a:n:Ar")) != -1) {
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
		case 'A':
			arbitrary = true;
			break;
		case 'r':
			request = true;
			break;
		default:
			usage(argv[0]);
			return EXIT_FAILURE;
		}
	}

	/* 1. Stack instance over the static buffers. */
	const j1939_cfg_t cfg = {
	        .rx_buf = rx_buf,
	        .rx_len = RX_LEN,
	        .tx_buf = tx_buf,
	        .tx_len = TX_LEN,
	        .msg_buf = msg_buf,
	        .msg_len = MSG_LEN,
	        .rx_pgns = rx_pgns,
	        .rx_pgns_len = (uint16_t)(sizeof(rx_pgns) / sizeof(rx_pgns[0])),
	};
	if (j1939_init(&stack, &cfg) != J1939_RET_OK) {
		(void)fprintf(stderr, "j1939_init failed\n");
		return EXIT_FAILURE;
	}

	/* 2. One Controller Application. It claims its address with the first
	 *    j1939_process() and may transmit once the claim succeeds. */
	const j1939_ca_cfg_t ca_cfg = {
	        .address = (uint8_t)address,
	        .name = example_name(identity, arbitrary),
	};
	j1939_ca_id_t ca;
	if (j1939_ca_add(&stack, &ca_cfg, &ca) != J1939_RET_OK) {
		(void)fprintf(stderr, "j1939_ca_add failed\n");
		return EXIT_FAILURE;
	}

	/* 3. The CAN socket: non-blocking, extended data frames only. */
	if (j1939_socketcan_open(ifname, &fd) != J1939_RET_OK) {
		(void)fprintf(stderr, "cannot open CAN interface %s\n", ifname);
		return EXIT_FAILURE;
	}

	(void)printf("[%8.3f] NAME 0x%016llX, preferred address 0x%02X%s on %s\n",
	             example_uptime_s(), (unsigned long long)ca_cfg.name, (unsigned)address,
	             arbitrary ? ", arbitrary address capable" : "", ifname);

	/* Optional: learn which addresses are in use before claiming. A Request
	 * for Address Claimed may be sent in any state; before the claim it
	 * goes out from the NULL address. Every node answers with its claim,
	 * which the stack records, so an arbitrary address capable CA that
	 * loses arbitration picks an address nobody holds. */
	if (request) {
		(void)j1939_request_send(&stack, ca, J1939_PGN_ADDRESS_CLAIMED, J1939_ADDR_GLOBAL);
	}

	j1939_queue_t *rx_q = j1939_rx_queue(&stack);
	j1939_queue_t *tx_q = j1939_tx_queue(&stack);
	uint64_t last_us = example_now_us();
	j1939_addr_state_t shown_state = J1939_ADDR_STATE_UNCLAIMED;
	uint8_t shown_address = J1939_ADDR_NULL;
	int status = EXIT_SUCCESS;

	while (example_running()) {
		/* a. Sleep until a frame arrives, at most one tick. Ask for
		 *    POLLOUT too while frames wait for room in the socket. */
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

		/* c. Run the stack with the time that really passed. */
		(void)j1939_process(&stack, example_elapsed_us(&last_us));

		/* d. Application: received messages, then the claim state. */
		const j1939_msg_t *msg;
		while ((msg = j1939_msg_peek(&stack)) != NULL) {
			if (msg->pgn == J1939_PGN_ADDRESS_CLAIMED) {
				claim_print(msg);
			}
			(void)j1939_msg_pop(&stack);
		}

		uint8_t now_address;
		j1939_addr_state_t now_state;
		(void)j1939_addr_get(&stack, ca, &now_address, &now_state);
		if ((now_state != shown_state) || (now_address != shown_address)) {
			(void)printf("[%8.3f] state %-12s -> %-12s address 0x%02X\n",
			             example_uptime_s(), example_state_name(shown_state),
			             example_state_name(now_state), (unsigned)now_address);
			shown_state = now_state;
			shown_address = now_address;
		}

		/* e. tx queue -> socket. Frames the socket cannot take stay queued. */
		if (j1939_socketcan_tx(fd, tx_q, NULL) != J1939_RET_OK) {
			(void)fprintf(stderr, "CAN transmit error\n");
			status = EXIT_FAILURE;
			break;
		}
	}

	j1939_socketcan_close(fd);
	return status;
}
