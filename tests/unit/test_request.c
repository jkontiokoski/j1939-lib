/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

#include "unity.h"

#include "j1939/j1939.h"

#define RX_LEN  8U
#define TX_LEN  2U
#define MSG_LEN 4U

#define OWN     0x10U
#define OTHER   0x42U
#define PGN_SUP 0xFF10U /* answered by the application */
#define PGN_UNS 0xFF20U /* not answered */

static const uint32_t req_pgns[] = {PGN_SUP};
static j1939_port_frame_t rx_buf[RX_LEN];
static j1939_port_frame_t tx_buf[TX_LEN];
static j1939_msg_slot_t msg_buf[MSG_LEN];
static j1939_t s;
static j1939_ca_id_t ca;

static void rx_request(uint8_t da, uint32_t pgn, uint8_t len) {
	/* Requests are often padded to 8 bytes with 0xFF. */
	uint8_t data[8] = {
	        (uint8_t)pgn, (uint8_t)(pgn >> 8), (uint8_t)(pgn >> 16), 0xFFU, 0xFFU, 0xFFU, 0xFFU,
	        0xFFU};
	j1939_port_frame_t *slot = j1939_queue_acquire(j1939_rx_queue(&s));
	uint32_t id = 0U;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_id_build(6U, J1939_PGN_REQUEST, da, OTHER, &id));
	TEST_ASSERT_NOT_NULL(slot);
	j1939_port_frame_build(slot, id, data, len);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_queue_commit(j1939_rx_queue(&s)));
}

void setUp(void) {
	const j1939_cfg_t cfg = {
	        .rx_buf = rx_buf,
	        .rx_len = RX_LEN,
	        .tx_buf = tx_buf,
	        .tx_len = TX_LEN,
	        .msg_buf = msg_buf,
	        .msg_len = MSG_LEN,
	        .rx_pgns = NULL,
	        .rx_pgns_len = 0U,
	        .req_pgns = req_pgns,
	        .req_pgns_len = 1U,
	};

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&s, &cfg));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_ca_add(&s, &(j1939_ca_cfg_t){.address = OWN}, &ca));
	/* Claim the address; the CA may transmit right after its Address Claimed. */
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(&s, 0U));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_queue_pop(j1939_tx_queue(&s)));
}

void tearDown(void) {
}

static void test_request_for_supported_pgn_is_delivered(void) {
	const j1939_msg_t *msg;
	uint32_t pgn = 0U;

	rx_request(OWN, PGN_SUP, 3U);
	rx_request(J1939_ADDR_GLOBAL, PGN_SUP, 8U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(&s, 0U));

	msg = j1939_msg_peek(&s);
	TEST_ASSERT_NOT_NULL(msg);
	TEST_ASSERT_EQUAL_HEX32(J1939_PGN_REQUEST, msg->pgn);
	TEST_ASSERT_EQUAL_HEX8(OWN, msg->da);
	TEST_ASSERT_EQUAL_HEX8(OTHER, msg->sa);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_request_pgn_get(msg, &pgn));
	TEST_ASSERT_EQUAL_HEX32(PGN_SUP, pgn);
	(void)j1939_msg_pop(&s);

	msg = j1939_msg_peek(&s);
	TEST_ASSERT_NOT_NULL(msg);
	TEST_ASSERT_EQUAL_HEX8(J1939_ADDR_GLOBAL, msg->da);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_request_pgn_get(msg, &pgn));
	TEST_ASSERT_EQUAL_HEX32(PGN_SUP, pgn);
	(void)j1939_msg_pop(&s);

	TEST_ASSERT_EQUAL_UINT16(0U, j1939_queue_count(j1939_tx_queue(&s)));
}

static void test_specific_request_for_unsupported_pgn_is_nacked(void) {
	const uint8_t expected[8] = {
	        J1939_ACK_CTRL_NACK, 0xFFU, 0xFFU, 0xFFU, OTHER, 0x20U, 0xFFU, 0x00U};
	const j1939_port_frame_t *f;
	uint32_t id = 0U;

	rx_request(OWN, PGN_UNS, 3U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(&s, 0U));
	TEST_ASSERT_NULL(j1939_msg_peek(&s));

	f = j1939_queue_peek(j1939_tx_queue(&s));
	TEST_ASSERT_NOT_NULL(f);
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_id_build(6U, J1939_PGN_ACK, J1939_ADDR_GLOBAL, OWN, &id));
	TEST_ASSERT_EQUAL_HEX32(id, j1939_port_frame_id_get(f));
	TEST_ASSERT_EQUAL_UINT8(8U, j1939_port_frame_len_get(f));
	TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, j1939_port_frame_data(f), 8U);
}

static void test_requests_without_answer_are_ignored(void) {
	rx_request(J1939_ADDR_GLOBAL, PGN_UNS, 3U); /* global, unsupported */
	rx_request(0x20U, PGN_UNS, 3U);             /* for another node */
	rx_request(OWN, PGN_UNS, 2U);               /* too short */
	rx_request(OWN, 0xFFFFFFU, 3U);             /* not a PGN */
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(&s, 0U));
	TEST_ASSERT_NULL(j1939_msg_peek(&s));
	TEST_ASSERT_EQUAL_UINT16(0U, j1939_queue_count(j1939_tx_queue(&s)));
}

static void test_nack_overflow_is_counted(void) {
	uint32_t i;

	for (i = 0U; i < (TX_LEN + 1U); i++) {
		rx_request(OWN, PGN_UNS, 3U);
	}
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(&s, 0U));
	TEST_ASSERT_EQUAL_UINT16(TX_LEN, j1939_queue_count(j1939_tx_queue(&s)));
	TEST_ASSERT_EQUAL_UINT32(1U, j1939_stats_get(&s)->tx_overflow);
}

static void test_request_send_builds_request(void) {
	const uint8_t expected[3] = {0x10U, 0xFFU, 0x00U};
	const j1939_port_frame_t *f;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_request_send(&s, ca, PGN_SUP, OTHER));
	f = j1939_queue_peek(j1939_tx_queue(&s));
	TEST_ASSERT_NOT_NULL(f);
	TEST_ASSERT_EQUAL_HEX32(0x18EA0000U | ((uint32_t)OTHER << 8) | OWN,
	                        j1939_port_frame_id_get(f));
	TEST_ASSERT_EQUAL_UINT8(3U, j1939_port_frame_len_get(f));
	TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, j1939_port_frame_data(f), 3U);
	(void)j1939_queue_pop(j1939_tx_queue(&s));

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_request_send(&s, ca, 0x3FF00U, J1939_ADDR_GLOBAL));
	f = j1939_queue_peek(j1939_tx_queue(&s));
	TEST_ASSERT_EQUAL_HEX32(0x18EAFF00U | OWN, j1939_port_frame_id_get(f));
	TEST_ASSERT_EQUAL_HEX8(0x03U, j1939_port_frame_data(f)[2]);

	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_request_send(&s, ca, 0x40000U, OTHER));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_request_send(&s, 5U, PGN_SUP, OTHER));
}

static void test_request_pgn_get_rejects_invalid_messages(void) {
	const uint8_t data[3] = {0xFFU, 0xFFU, 0xFFU};
	j1939_msg_t msg = {
	        .pgn = J1939_PGN_REQUEST, .prio = 6U, .sa = 1U, .da = 2U, .len = 3U, .data = data};
	uint32_t pgn = 0x1234U;

	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_request_pgn_get(&msg, &pgn)); /* 0xFFFFFF */
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_request_pgn_get(NULL, &pgn));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_request_pgn_get(&msg, NULL));
	msg.len = 2U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_request_pgn_get(&msg, &pgn));
	msg.len = 3U;
	msg.pgn = J1939_PGN_ACK;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_request_pgn_get(&msg, &pgn));
	msg.pgn = J1939_PGN_REQUEST;
	msg.data = NULL;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_request_pgn_get(&msg, &pgn));
	TEST_ASSERT_EQUAL_HEX32(0x1234U, pgn);
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_request_for_supported_pgn_is_delivered);
	RUN_TEST(test_specific_request_for_unsupported_pgn_is_nacked);
	RUN_TEST(test_requests_without_answer_are_ignored);
	RUN_TEST(test_nack_overflow_is_counted);
	RUN_TEST(test_request_send_builds_request);
	RUN_TEST(test_request_pgn_get_rejects_invalid_messages);
	return UNITY_END();
}
