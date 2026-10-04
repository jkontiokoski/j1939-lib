/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/* Limits of transport protocol buffers smaller than the 1785-byte protocol maximum, and
 * multi-packet send rejections. */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "j1939/j1939.h"

#define TX_LEN  8U
#define MSG_LEN 2U
#define OWN     0x10U
#define PEER    0x42U
#define PGN_A   0xFF10U /* PDU2 */
#define BUF_MAX ((uint16_t)J1939_CFG_TP_BUF_SIZE)

static j1939_port_frame_t tx_buf[TX_LEN];
static j1939_msg_slot_t msg_buf[MSG_LEN];
static j1939_tp_buf_t tp_tx[1];
static j1939_tp_buf_t tp_rx[1];
static const uint32_t rx_pgns[] = {PGN_A};
static j1939_t s;
static j1939_ca_id_t ca;

static uint32_t make_id(uint8_t prio, uint32_t pgn, uint8_t da, uint8_t sa) {
	uint32_t id = 0U;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_id_build(prio, pgn, da, sa, &id));
	return id;
}

static void rx_cm(uint8_t ctrl, uint8_t da, uint16_t len) {
	const uint8_t d[8] = {
	        ctrl,  (uint8_t)len,   (uint8_t)(len >> 8),   (uint8_t)((len + 6U) / 7U),
	        0xFFU, (uint8_t)PGN_A, (uint8_t)(PGN_A >> 8), (uint8_t)(PGN_A >> 16)};
	j1939_port_frame_t f;

	j1939_port_frame_build(&f, make_id(7U, J1939_PGN_TP_CM, da, PEER), d, 8U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_rx(&s, &f));
}

static void expect_frame(uint32_t id, const uint8_t *d) {
	const j1939_port_frame_t *f = j1939_tx_peek(&s);

	TEST_ASSERT_NOT_NULL(f);
	TEST_ASSERT_EQUAL_HEX32(id, j1939_port_frame_id_get(f));
	TEST_ASSERT_EQUAL_HEX8_ARRAY(d, j1939_port_frame_data(f), 8U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_tx_pop(&s));
}

void setUp(void) {
	const j1939_cfg_t cfg = {
	        .tx_buf = tx_buf,
	        .tx_len = TX_LEN,
	        .msg_buf = msg_buf,
	        .msg_len = MSG_LEN,
	        .rx_pgns = rx_pgns,
	        .rx_pgns_len = 1U,
	        .tp_tx_buf = tp_tx,
	        .tp_tx_buf_len = 1U,
	        .tp_rx_buf = tp_rx,
	        .tp_rx_buf_len = 1U,
	};

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&s, &cfg));
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_ca_add(&s, &(j1939_ca_cfg_t){.address = OWN, .name = 1U}, &ca));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(&s, 0U));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_tx_pop(&s)); /* Address Claimed */
}

void tearDown(void) {
}

static void test_announcements_beyond_the_buffer_are_refused(void) {
	const uint8_t abort[8] = {0xFFU,
	                          J1939_TP_ABORT_RESOURCES,
	                          0xFFU,
	                          0xFFU,
	                          0xFFU,
	                          (uint8_t)PGN_A,
	                          (uint8_t)(PGN_A >> 8),
	                          (uint8_t)(PGN_A >> 16)};
	const j1939_msg_t *msg;
	uint8_t seq;

	/* RTS: Connection Abort, reason 2. */
	rx_cm(0x10U, OWN, BUF_MAX + 1U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(&s, 0U));
	expect_frame(make_id(7U, J1939_PGN_TP_CM, PEER, OWN), abort);
	TEST_ASSERT_EQUAL_UINT32(1U, j1939_stats_get(&s)->tp_rx_refused);
	/* BAM: ignored. */
	rx_cm(0x20U, J1939_ADDR_GLOBAL, J1939_TP_MSG_MAX);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(&s, 0U));
	TEST_ASSERT_NULL(j1939_tx_peek(&s));
	TEST_ASSERT_EQUAL_UINT32(2U, j1939_stats_get(&s)->tp_rx_refused);
	/* A broadcast of the buffer size is received. */
	rx_cm(0x20U, J1939_ADDR_GLOBAL, BUF_MAX);
	for (seq = 1U; seq <= (uint8_t)((BUF_MAX + 6U) / 7U); seq++) {
		const uint8_t d[8] = {seq, seq, seq, seq, seq, seq, seq, seq};
		j1939_port_frame_t f;

		j1939_port_frame_build(&f, make_id(7U, J1939_PGN_TP_DT, J1939_ADDR_GLOBAL, PEER), d,
		                       8U);
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_rx(&s, &f));
	}
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(&s, 0U));
	msg = j1939_msg_peek(&s);
	TEST_ASSERT_NOT_NULL(msg);
	TEST_ASSERT_EQUAL_UINT16(BUF_MAX, msg->len);
	TEST_ASSERT_EQUAL_UINT32(2U, j1939_stats_get(&s)->tp_rx_refused);
}

static void test_multi_packet_send_beyond_the_buffer_or_invalid_is_rejected(void) {
	static uint8_t data[BUF_MAX + 1U];
	j1939_msg_t msg = {.pgn = PGN_A,
	                   .prio = 6U,
	                   .sa = 0U,
	                   .da = J1939_ADDR_GLOBAL,
	                   .len = BUF_MAX + 1U,
	                   .data = data};

	(void)memset(data, 0x5A, sizeof(data));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_send(&s, ca, &msg));
	TEST_ASSERT_NULL(j1939_tx_peek(&s));
	/* Within the buffer, but with an invalid priority. */
	msg.len = BUF_MAX;
	msg.prio = 8U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_send(&s, ca, &msg));
	TEST_ASSERT_NULL(j1939_tx_peek(&s));
	msg.prio = 6U;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_send(&s, ca, &msg));
}

static void test_dm_payload_beyond_the_buffer_is_rejected(void) {
	/* 24 DTCs need 98 bytes, 25 need 102. */
	static j1939_diag_dtc_t dtcs[25];
	static uint8_t buf[J1939_DM_BUF_LEN(25U)];
	static j1939_dm_t dm;
	j1939_dm_cfg_t c = {.prev = dtcs, .prev_len = 25U, .buf = buf, .buf_len = sizeof(buf)};

	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_dm_init(&s, ca, &dm, &c));
	c.prev_len = 24U;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_dm_init(&s, ca, &dm, &c));
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_announcements_beyond_the_buffer_are_refused);
	RUN_TEST(test_multi_packet_send_beyond_the_buffer_or_invalid_is_rejected);
	RUN_TEST(test_dm_payload_beyond_the_buffer_is_rejected);
	return UNITY_END();
}
