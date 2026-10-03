/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/*
 * SocketCAN loopback test. Needs a virtual CAN interface:
 *
 *   sudo ip link add dev vcan0 type vcan && sudo ip link set up vcan0
 *
 * The interface name can be overridden with J1939_TEST_CANIF. Without the
 * interface the test reports "skipped".
 */

#define _DEFAULT_SOURCE

#include "unity.h"

#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "j1939/j1939.h"
#include "j1939_socketcan.h"

#define SKIP_RETURN_CODE 77
#define N_MSGS           6U
#define TX_LEN           (N_MSGS + 1U) /* the messages and the Address Claimed */
#define SENDER           0x42U
#define PGN_BASE         0xFF00U
#define POLL_TIMEOUT_MS  1000

static const char *ifname;
static int fd_tx = -1;
static int fd_rx = -1;

static j1939_port_frame_t tx_buf[TX_LEN];
static j1939_msg_slot_t sender_msgs[1];
static j1939_t sender;
static j1939_ca_id_t sender_ca;

static uint32_t rx_pgns[N_MSGS];
static j1939_port_frame_t receiver_tx_buf[1];
static j1939_msg_slot_t receiver_msgs[N_MSGS];
static j1939_t receiver;
static uint16_t received;

static uint8_t msg_len(uint32_t i) {
	return (uint8_t)((i + 3U > 8U) ? 8U : (i + 3U));
}

static void payload(uint32_t i, uint8_t *data) {
	uint8_t b;

	for (b = 0U; b < 8U; b++) {
		data[b] = (uint8_t)(i * 16U + b);
	}
}

/* Queues N_MSGS broadcasts of different lengths in the sender's tx queue. */
static void send_all(void) {
	uint8_t data[8];
	uint32_t i;

	for (i = 0U; i < N_MSGS; i++) {
		const j1939_msg_t msg = {.pgn = PGN_BASE + i,
		                         .prio = 6U,
		                         .sa = 0U,
		                         .da = J1939_ADDR_GLOBAL,
		                         .len = msg_len(i),
		                         .data = data};

		payload(i, data);
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_send(&sender, sender_ca, &msg));
	}
}

/* Reads at most max frames per call until @p want frames arrived or the socket stays quiet. */
static void receive(uint16_t want, uint16_t max) {
	struct pollfd pfd = {.fd = fd_rx, .events = POLLIN, .revents = 0};
	uint16_t n = 0U;

	while (received < want) {
		if (poll(&pfd, 1U, POLL_TIMEOUT_MS) <= 0) {
			break;
		}
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_socketcan_rx(fd_rx, &receiver, max, &n));
		TEST_ASSERT_LESS_OR_EQUAL_UINT16(max, n);
		received = (uint16_t)(received + n);
	}
}

void setUp(void) {
	uint32_t i;

	for (i = 0U; i < N_MSGS; i++) {
		rx_pgns[i] = PGN_BASE + i;
	}
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&sender, &(j1939_cfg_t){.tx_buf = tx_buf,
	                                                                   .tx_len = TX_LEN,
	                                                                   .msg_buf = sender_msgs,
	                                                                   .msg_len = 1U}));
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_ca_add(&sender, &(j1939_ca_cfg_t){.address = SENDER}, &sender_ca));
	/* An address outside 128-247 is claimed at once. */
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(&sender, 0U));
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_init(&receiver, &(j1939_cfg_t){.tx_buf = receiver_tx_buf,
	                                                       .tx_len = 1U,
	                                                       .msg_buf = receiver_msgs,
	                                                       .msg_len = N_MSGS,
	                                                       .rx_pgns = rx_pgns,
	                                                       .rx_pgns_len = N_MSGS}));
	received = 0U;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_socketcan_open(ifname, &fd_tx));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_socketcan_open(ifname, &fd_rx));
}

void tearDown(void) {
	j1939_socketcan_close(fd_tx);
	j1939_socketcan_close(fd_rx);
}

static void test_messages_pass_unchanged(void) {
	uint16_t n_tx = 0U;
	uint8_t data[8];
	uint32_t i;

	send_all();
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_socketcan_tx(fd_tx, &sender, &n_tx));
	TEST_ASSERT_EQUAL_UINT16(TX_LEN, n_tx);
	TEST_ASSERT_NULL(j1939_tx_peek(&sender));

	receive(TX_LEN, TX_LEN);
	TEST_ASSERT_EQUAL_UINT16(TX_LEN, received);
	for (i = 0U; i < N_MSGS; i++) {
		const j1939_msg_t *msg = j1939_msg_peek(&receiver);

		TEST_ASSERT_NOT_NULL_MESSAGE(msg, "message not received");
		payload(i, data);
		TEST_ASSERT_EQUAL_HEX32(PGN_BASE + i, msg->pgn);
		TEST_ASSERT_EQUAL_HEX8(SENDER, msg->sa);
		TEST_ASSERT_EQUAL_UINT16(msg_len(i), msg->len);
		TEST_ASSERT_EQUAL_HEX8_ARRAY(data, msg->data, msg_len(i));
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_msg_pop(&receiver));
	}
}

static void test_rx_reads_at_most_max_frames(void) {
	struct pollfd pfd = {.fd = fd_rx, .events = POLLIN, .revents = 0};
	uint16_t n_rx = 0U;

	send_all();
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_socketcan_tx(fd_tx, &sender, NULL));
	TEST_ASSERT_GREATER_THAN(0, poll(&pfd, 1U, POLL_TIMEOUT_MS));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_socketcan_rx(fd_rx, &receiver, 2U, &n_rx));
	TEST_ASSERT_LESS_OR_EQUAL_UINT16(2U, n_rx);
	received = n_rx;

	/* The other frames waited in the socket. */
	receive(TX_LEN, TX_LEN);
	TEST_ASSERT_EQUAL_UINT16(TX_LEN, received);
}

static void test_standard_frames_are_filtered(void) {
	j1939_port_frame_t f;
	uint16_t n_rx = 0U;
	int fd_raw;

	/* An 11-bit frame written directly; the port's builder makes only extended frames. */
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_socketcan_open(ifname, &fd_raw));
	(void)memset(&f, 0, sizeof(f));
	f.can_id = 0x123U;
	TEST_ASSERT_EQUAL((ssize_t)sizeof(f), write(fd_raw, &f, sizeof(f)));
	j1939_socketcan_close(fd_raw);

	receive(1U, 1U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_socketcan_rx(fd_rx, &receiver, 1U, &n_rx));
	TEST_ASSERT_EQUAL_UINT16(0U, received);
	TEST_ASSERT_EQUAL_UINT16(0U, n_rx);
}

static void test_open_rejects_unknown_interface(void) {
	int fd = -1;

	TEST_ASSERT_EQUAL(J1939_RET_ERR_IO, j1939_socketcan_open("nosuchcan0", &fd));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_socketcan_open(NULL, &fd));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_socketcan_rx(fd_rx, NULL, 1U, NULL));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_socketcan_tx(fd_tx, NULL, NULL));
}

int main(void) {
	int probe = -1;

	ifname = getenv("J1939_TEST_CANIF");
	if (ifname == NULL) {
		ifname = "vcan0";
	}
	if (j1939_socketcan_open(ifname, &probe) != J1939_RET_OK) {
		return SKIP_RETURN_CODE;
	}
	j1939_socketcan_close(probe);

	UNITY_BEGIN();
	RUN_TEST(test_messages_pass_unchanged);
	RUN_TEST(test_rx_reads_at_most_max_frames);
	RUN_TEST(test_standard_frames_are_filtered);
	RUN_TEST(test_open_rejects_unknown_interface);
	return UNITY_END();
}
