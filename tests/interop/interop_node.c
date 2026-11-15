/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/*
 * interop_node: one library node on SocketCAN, driven by tools/interop.sh against the J1939
 * stack of the Linux kernel and its tools.
 *
 *   interop_node [-i ifname] [-a address] [-n name] [-t ms] claim
 *   interop_node [-i ifname] [-a address] [-n name] [-t ms] send <da> <pgn> <file>
 *   interop_node [-i ifname] [-a address] [-n name] [-t ms] recv <pgn> <file>
 *
 *   -i  CAN interface, default vcan0
 *   -a  preferred address, default 0x80
 *   -n  NAME as a 64-bit number, default 0x00008000FFE00010
 *   -t  run time in ms, default 2000
 *
 *   claim  claims the address and runs until the time is up
 *   send   sends the content of <file> as one message of <pgn> to <da>, once the address is
 *          claimed; fails if the message was not queued or the transfer was aborted
 *   recv   writes the first received message of <pgn> to <file> and ends; fails if none came
 *
 * The last line printed is the state at the end:
 *
 *   address 0x80 CLAIMED tp_tx_aborted 0 tp_rx_aborted 0 tx_overflow 0
 *
 * Exit status: 0 passed, 1 failed, 2 wrong usage or no CAN interface.
 */

#define _POSIX_C_SOURCE 200809L /* getopt, poll, clock_gettime */

#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "j1939/j1939.h"
#include "j1939_socketcan.h"

#if !J1939_TP_ENABLE
#error "interop_node needs the transport protocol"
#endif

#define TICK_MS     5
#define RX_PER_TICK 512U
#define TX_LEN      32U
#define MSG_LEN     4U
#define LINGER_US   100000U /* Time to send the last answers after a reception. */

#define EXIT_USAGE 2

typedef enum node_mode {
	NODE_CLAIM,
	NODE_SEND,
	NODE_RECV,
} node_mode_t;

static j1939_port_frame_t tx_buf[TX_LEN];
static j1939_msg_slot_t msg_buf[MSG_LEN];
static j1939_tp_buf_t tp_rx_buf[2];
static j1939_tp_buf_t tp_tx_buf[1];
static uint32_t rx_pgns[1];
static uint8_t payload[J1939_TP_MSG_MAX];
static j1939_t stack;

static void usage(const char *prog) {
	(void)fprintf(stderr,
	              "usage: %s [-i ifname] [-a address] [-n name] [-t ms] claim\n"
	              "       %s [...] send <da> <pgn> <file>\n"
	              "       %s [...] recv <pgn> <file>\n",
	              prog, prog, prog);
}

static uint64_t now_us(void) {
	struct timespec ts = {0, 0};

	(void)clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((uint64_t)ts.tv_sec * 1000000U) + ((uint64_t)ts.tv_nsec / 1000U);
}

static bool parse_u64(const char *s, uint64_t max, uint64_t *value) {
	char *end = NULL;
	unsigned long long v = strtoull(s, &end, 0);

	if ((end == s) || (*end != '\0') || ((uint64_t)v > max)) {
		return false;
	}
	*value = (uint64_t)v;
	return true;
}

static const char *state_name(j1939_addr_state_t state) {
	switch (state) {
	case J1939_ADDR_STATE_UNCLAIMED:
		return "UNCLAIMED";
	case J1939_ADDR_STATE_CLAIMING:
		return "CLAIMING";
	case J1939_ADDR_STATE_CLAIMED:
		return "CLAIMED";
	case J1939_ADDR_STATE_CANNOT_CLAIM:
		return "CANNOT_CLAIM";
	case J1939_ADDR_STATE_REQUESTING:
		return "REQUESTING";
	default:
		return "?";
	}
}

static bool file_read(const char *path, uint8_t *buf, size_t max, uint16_t *len) {
	FILE *f = fopen(path, "rb");
	size_t n;

	if (f == NULL) {
		return false;
	}
	n = fread(buf, 1U, max, f);
	(void)fclose(f);
	*len = (uint16_t)n;
	return n > 0U;
}

static bool file_write(const char *path, const uint8_t *buf, size_t len) {
	FILE *f = fopen(path, "wb");
	size_t n;

	if (f == NULL) {
		return false;
	}
	n = fwrite(buf, 1U, len, f);
	return (fclose(f) == 0) && (n == len);
}

int main(int argc, char **argv) {
	const char *ifname = "vcan0";
	const char *file = NULL;
	uint64_t address = 0x80U;
	uint64_t name = 0x00008000FFE00010ULL;
	uint64_t run_ms = 2000U;
	uint64_t da = J1939_ADDR_GLOBAL;
	uint64_t pgn = 0U;
	uint16_t len = 0U;
	node_mode_t mode;
	bool args_ok = true;
	int fd = -1;
	int opt;

	while ((opt = getopt(argc, argv, "i:a:n:t:")) != -1) {
		switch (opt) {
		case 'i':
			ifname = optarg;
			break;
		case 'a':
			args_ok = args_ok && parse_u64(optarg, 253U, &address);
			break;
		case 'n':
			args_ok = args_ok && parse_u64(optarg, UINT64_MAX, &name);
			break;
		case 't':
			args_ok = args_ok && parse_u64(optarg, 600000U, &run_ms);
			break;
		default:
			args_ok = false;
			break;
		}
	}
	argc -= optind;
	argv += optind;
	if (args_ok && (argc == 1) && (strcmp(argv[0], "claim") == 0)) {
		mode = NODE_CLAIM;
	} else if (args_ok && (argc == 4) && (strcmp(argv[0], "send") == 0) &&
	           parse_u64(argv[1], 255U, &da) && parse_u64(argv[2], J1939_PGN_MAX, &pgn)) {
		mode = NODE_SEND;
		file = argv[3];
		if (!file_read(file, payload, sizeof(payload), &len)) {
			(void)fprintf(stderr, "cannot read %s\n", file);
			return EXIT_USAGE;
		}
	} else if (args_ok && (argc == 3) && (strcmp(argv[0], "recv") == 0) &&
	           parse_u64(argv[1], J1939_PGN_MAX, &pgn)) {
		mode = NODE_RECV;
		file = argv[2];
	} else {
		usage("interop_node");
		return EXIT_USAGE;
	}

	rx_pgns[0] = (uint32_t)pgn;
	/* Locals of main(): they outlive the stack, which keeps a pointer to them. */
	const j1939_ca_cfg_t ca_cfg = {.address = (uint8_t)address, .name = name};
	const j1939_cfg_t cfg = {
	        .tx_buf = tx_buf,
	        .tx_len = TX_LEN,
	        .msg_buf = msg_buf,
	        .msg_len = MSG_LEN,
	        .rx_pgns = rx_pgns,
	        .rx_pgns_len = (mode == NODE_RECV) ? 1U : 0U,
	        .ca = &ca_cfg,
	        .ca_len = 1U,
	        .tp_tx_buf = tp_tx_buf,
	        .tp_tx_buf_len = 1U,
	        .tp_rx_buf = tp_rx_buf,
	        .tp_rx_buf_len = 2U,
	};
	const j1939_ca_id_t ca = 0U;

	if (j1939_init(&stack, &cfg) != J1939_RET_OK) {
		(void)fprintf(stderr, "stack setup failed: check the PGN\n");
		return EXIT_USAGE;
	}
	if (j1939_socketcan_open(ifname, &fd) != J1939_RET_OK) {
		(void)fprintf(stderr, "cannot open CAN interface %s\n", ifname);
		return EXIT_USAGE;
	}

	const j1939_msg_t out = {
	        .pgn = (uint32_t)pgn,
	        .prio = J1939_PRIO_DEFAULT,
	        .da = (uint8_t)da,
	        .len = len,
	        .data = payload,
	};
	uint64_t last = now_us();
	uint64_t end = last + (run_ms * 1000U);
	bool sent = false;
	bool send_failed = false;
	bool received = false;
	bool io_ok = true;

	while (io_ok && (last < end)) {
		struct pollfd pfd = {.fd = fd, .events = POLLIN, .revents = 0};
		const j1939_msg_t *msg;
		uint64_t now;

		(void)poll(&pfd, 1, TICK_MS);
		io_ok = j1939_socketcan_rx(fd, &stack, RX_PER_TICK, NULL) == J1939_RET_OK;
		now = now_us();
		(void)j1939_process(&stack, (uint32_t)(now - last));
		last = now;

		if ((mode == NODE_SEND) && !sent && !send_failed) {
			j1939_ret_t ret = j1939_send(&stack, ca, &out);

			sent = ret == J1939_RET_OK;
			/* Before the claim, or while the queue is full: try again. */
			send_failed = !sent && (ret != J1939_RET_ERR_NO_ADDRESS) &&
			              (ret != J1939_RET_ERR_FULL) && (ret != J1939_RET_ERR_BUSY);
		}
		while ((msg = j1939_msg_peek(&stack)) != NULL) {
			if ((mode == NODE_RECV) && !received && (msg->pgn == (uint32_t)pgn)) {
				received = file_write(file, msg->data, msg->len);
				end = now + LINGER_US;
			}
			(void)j1939_msg_pop(&stack);
		}
		io_ok = io_ok && (j1939_socketcan_tx(fd, &stack, NULL) == J1939_RET_OK);
	}

	const j1939_stats_t *st = j1939_stats_get(&stack);
	j1939_addr_state_t state = J1939_ADDR_STATE_UNCLAIMED;
	uint8_t now_address = J1939_ADDR_NULL;
	bool pass = io_ok;

	(void)j1939_addr_get(&stack, ca, &now_address, &state);
	(void)printf("address 0x%02X %s tp_tx_aborted %lu tp_rx_aborted %lu tx_overflow %lu\n",
	             (unsigned)now_address, state_name(state), (unsigned long)st->tp_tx_aborted,
	             (unsigned long)st->tp_rx_aborted, (unsigned long)st->tx_overflow);
	if (mode == NODE_SEND) {
		pass = pass && sent && (st->tp_tx_aborted == 0U) && (st->tx_overflow == 0U);
	} else if (mode == NODE_RECV) {
		pass = pass && received;
	} else {
		/* Claim: the caller reads the state line. */
	}
	j1939_socketcan_close(fd);
	return pass ? EXIT_SUCCESS : EXIT_FAILURE;
}
