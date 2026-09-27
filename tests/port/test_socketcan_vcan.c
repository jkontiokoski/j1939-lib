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

#include "j1939_socketcan.h"

#define SKIP_RETURN_CODE 77
#define N_FRAMES         6U
#define RX_LEN           4U
#define POLL_TIMEOUT_MS  1000

static const char *ifname;
static int fd_tx = -1;
static int fd_rx = -1;
static j1939_port_frame_t tx_buf[N_FRAMES];
static j1939_port_frame_t rx_buf[RX_LEN];
static j1939_queue_t tx_q;
static j1939_queue_t rx_q;

static uint32_t test_id(uint32_t i) {
	return 0x18FF0000U | (i << 8) | 0x42U;
}

static void fill_tx(void) {
	j1939_port_frame_t f;
	uint8_t payload[8];
	uint32_t i;
	uint8_t b;

	for (i = 0U; i < N_FRAMES; i++) {
		for (b = 0U; b < 8U; b++) {
			payload[b] = (uint8_t)(i * 16U + b);
		}
		j1939_port_frame_build(&f, test_id(i), payload,
		                       (uint8_t)(i + 3U > 8U ? 8U : i + 3U));
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_queue_put(&tx_q, &f));
	}
}

/* Receives until @p want frames are in the rx queue or the socket stays quiet. */
static void receive(uint16_t want) {
	struct pollfd pfd = {.fd = fd_rx, .events = POLLIN, .revents = 0};
	uint16_t n;

	while (j1939_queue_count(&rx_q) < want) {
		if (poll(&pfd, 1U, POLL_TIMEOUT_MS) <= 0) {
			break;
		}
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_socketcan_rx(fd_rx, &rx_q, &n));
	}
}

void setUp(void) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_queue_init(&tx_q, tx_buf, N_FRAMES));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_queue_init(&rx_q, rx_buf, RX_LEN));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_socketcan_open(ifname, &fd_tx));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_socketcan_open(ifname, &fd_rx));
}

void tearDown(void) {
	j1939_socketcan_close(fd_tx);
	j1939_socketcan_close(fd_rx);
}

static void test_frames_pass_unchanged_with_backpressure(void) {
	uint16_t n_tx = 0U;
	uint32_t i;
	uint32_t expected_len;

	fill_tx();
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_socketcan_tx(fd_tx, &tx_q, &n_tx));
	TEST_ASSERT_EQUAL_UINT16(N_FRAMES, n_tx);
	TEST_ASSERT_EQUAL_UINT16(0U, j1939_queue_count(&tx_q));

	/* The rx queue holds fewer frames than were sent: the rest wait in the socket. */
	for (i = 0U; i < N_FRAMES; i++) {
		receive(1U);
		TEST_ASSERT_NOT_NULL_MESSAGE(j1939_queue_peek(&rx_q), "frame not received");
		expected_len = (i + 3U > 8U) ? 8U : i + 3U;
		TEST_ASSERT_TRUE(j1939_port_frame_is_ext(j1939_queue_peek(&rx_q)));
		TEST_ASSERT_EQUAL_HEX32(test_id(i),
		                        j1939_port_frame_id_get(j1939_queue_peek(&rx_q)));
		TEST_ASSERT_EQUAL_UINT8(expected_len,
		                        j1939_port_frame_len_get(j1939_queue_peek(&rx_q)));
		TEST_ASSERT_EQUAL_HEX8_ARRAY(tx_buf[i].data,
		                             j1939_port_frame_data(j1939_queue_peek(&rx_q)),
		                             expected_len);
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_queue_pop(&rx_q));
	}
}

static void test_rx_stops_when_queue_is_full(void) {
	uint16_t n_tx = 0U;
	uint16_t n_rx = 0U;

	fill_tx();
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_socketcan_tx(fd_tx, &tx_q, &n_tx));
	receive(RX_LEN);
	TEST_ASSERT_EQUAL_UINT16(RX_LEN, j1939_queue_count(&rx_q));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_socketcan_rx(fd_rx, &rx_q, &n_rx));
	TEST_ASSERT_EQUAL_UINT16(0U, n_rx);
}

static void test_standard_frames_are_filtered(void) {
	j1939_port_frame_t f;
	uint16_t n_rx = 0U;
	int fd_raw;

	/* The raw fixture frame bypasses the port's builder: 11-bit identifier. */
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_socketcan_open(ifname, &fd_raw));
	(void)memset(&f, 0, sizeof(f));
	f.can_id = 0x123U;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_queue_put(&tx_q, &f));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_socketcan_tx(fd_raw, &tx_q, NULL));
	TEST_ASSERT_EQUAL_UINT16_MESSAGE(0U, j1939_queue_count(&tx_q), "standard frame not sent");
	j1939_socketcan_close(fd_raw);

	receive(1U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_socketcan_rx(fd_rx, &rx_q, &n_rx));
	TEST_ASSERT_EQUAL_UINT16(0U, j1939_queue_count(&rx_q));
}

static void test_open_rejects_unknown_interface(void) {
	int fd = -1;

	TEST_ASSERT_EQUAL(J1939_RET_ERR_IO, j1939_socketcan_open("nosuchcan0", &fd));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_socketcan_open(NULL, &fd));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_socketcan_rx(fd_rx, NULL, NULL));
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
	RUN_TEST(test_frames_pass_unchanged_with_backpressure);
	RUN_TEST(test_rx_stops_when_queue_is_full);
	RUN_TEST(test_standard_frames_are_filtered);
	RUN_TEST(test_open_rejects_unknown_interface);
	return UNITY_END();
}
