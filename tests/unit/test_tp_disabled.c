/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/* A stack built without the transport protocol: single frame messages only. */

#include <stdint.h>

#include "unity.h"

#include "j1939/j1939.h"

#if J1939_TP_ENABLE
#error "test_tp_disabled needs a library built with J1939_TP_ENABLE 0"
#endif

#define TX_LEN  8U
#define MSG_LEN 2U
#define LONG    20U /* multi-packet payload */

#define OWN   0x10U
#define PEER  0x42U
#define NAME  0x100000U
#define PGN_A 0xFF10U /* PDU2 */
#define PGN_B 0xEF00U /* PDU1 */

#define RTS 0x10U
#define BAM 0x20U
#define NA  0xFFU

static const uint32_t rx_pgns[] = {PGN_A, PGN_B};
static const j1939_dtc_t dtc_a = {520210U, 3U, 1U, J1939_DTC_CM_V4};
static j1939_port_frame_t tx_buf[TX_LEN];
static j1939_msg_slot_t msg_buf[MSG_LEN];
static j1939_ca_cfg_t ca_cfg;
static j1939_cfg_t cfg;
static j1939_t s;
static j1939_ca_id_t ca;
static uint8_t data[J1939_TP_MSG_MAX];

static uint32_t make_id(uint8_t prio, uint32_t pgn, uint8_t da, uint8_t sa) {
	uint32_t id = 0U;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_id_build(prio, pgn, da, sa, &id));
	return id;
}

static void rx_raw(uint32_t id, const uint8_t *d, uint8_t len) {
	j1939_port_frame_t f;

	j1939_port_frame_build(&f, id, d, len);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_rx(&s, &f));
}

static void rx_cm(uint8_t da, uint8_t ctrl, uint16_t len, uint8_t packets, uint8_t limit,
                  uint32_t pgn) {
	const uint8_t d[8] = {ctrl,  (uint8_t)len, (uint8_t)(len >> 8), packets,
	                      limit, (uint8_t)pgn, (uint8_t)(pgn >> 8), (uint8_t)(pgn >> 16)};

	rx_raw(make_id(7U, J1939_PGN_TP_CM, da, PEER), d, 8U);
}

static void rx_dt(uint8_t da, uint8_t seq) {
	const uint8_t d[8] = {seq, 1U, 2U, 3U, 4U, 5U, 6U, 7U};

	rx_raw(make_id(7U, J1939_PGN_TP_DT, da, PEER), d, 8U);
}

static void process(uint32_t us) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(&s, us));
}

static void expect_none(void) {
	TEST_ASSERT_EQUAL_UINT16(0U, s.tx.count);
}

/* Pops the next sent frame and checks its PGN; returns its payload. */
static const uint8_t *expect_pgn(uint32_t pgn, uint8_t len) {
	static uint8_t sent[8];
	const j1939_port_frame_t *f = j1939_tx_peek(&s);
	uint32_t i;

	TEST_ASSERT_NOT_NULL_MESSAGE(f, "no frame sent");
	TEST_ASSERT_EQUAL_HEX32(pgn, j1939_id_pgn_get(j1939_port_frame_id_get(f)));
	TEST_ASSERT_EQUAL_UINT8(len, j1939_port_frame_len_get(f));
	for (i = 0U; i < len; i++) {
		sent[i] = j1939_port_frame_data(f)[i];
	}
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_tx_pop(&s));
	return sent;
}

static j1939_ret_t send(uint8_t da, uint32_t pgn, uint16_t len) {
	const j1939_msg_t msg = {
	        .pgn = pgn, .prio = 6U, .sa = 0U, .da = da, .len = len, .data = data};

	return j1939_send(&s, ca, &msg);
}

void setUp(void) {
	cfg = (j1939_cfg_t){
	        .tx_buf = tx_buf,
	        .tx_len = TX_LEN,
	        .msg_buf = msg_buf,
	        .msg_len = MSG_LEN,
	        .rx_pgns = rx_pgns,
	        .rx_pgns_len = 2U,
	        .ca = &ca_cfg,
	        .ca_len = 1U,
	};
	ca_cfg = (j1939_ca_cfg_t){.address = OWN, .name = NAME};
	ca = 0U;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&s, &cfg));
}

void tearDown(void) {
}

/* Claims OWN: the CA may transmit right after its Address Claimed. */
static void claim(void) {
	process(0U);
	(void)expect_pgn(J1939_PGN_ADDRESS_CLAIMED, 8U);
}

static void test_stack_runs_without_tp_buffers(void) {
	claim();
	expect_none();
	process(1000000U);
	expect_none();
}

static void test_send_takes_single_frames_only(void) {
	claim();
	TEST_ASSERT_EQUAL(J1939_RET_OK, send(J1939_ADDR_GLOBAL, PGN_A, 8U));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, send(J1939_ADDR_GLOBAL, PGN_A, 9U));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, send(PEER, PGN_B, LONG));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, send(PEER, PGN_B, (uint16_t)J1939_TP_MSG_MAX));
	(void)expect_pgn(PGN_A, 8U);
	expect_none();
}

static void test_received_bam_is_dropped(void) {
	claim();
	rx_cm(J1939_ADDR_GLOBAL, BAM, LONG, 3U, NA, PGN_A);
	rx_dt(J1939_ADDR_GLOBAL, 1U);
	rx_dt(J1939_ADDR_GLOBAL, 2U);
	rx_dt(J1939_ADDR_GLOBAL, 3U);
	process(0U);
	TEST_ASSERT_NULL(j1939_msg_peek(&s));
	expect_none();
}

static void test_received_rts_is_not_answered(void) {
	claim();
	rx_cm(OWN, RTS, LONG, 3U, NA, PGN_B);
	process(0U);
	expect_none();
	rx_dt(OWN, 1U);
	process(1000000U);
	TEST_ASSERT_NULL(j1939_msg_peek(&s));
	expect_none();
}

static void test_single_frames_are_still_delivered(void) {
	const uint8_t d[8] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U};
	const j1939_msg_t *msg;

	claim();
	rx_raw(make_id(6U, PGN_A, J1939_ADDR_GLOBAL, PEER), d, 8U);
	process(0U);
	msg = j1939_msg_peek(&s);
	TEST_ASSERT_NOT_NULL(msg);
	TEST_ASSERT_EQUAL_HEX32(PGN_A, msg->pgn);
	TEST_ASSERT_EQUAL_UINT16(8U, msg->len);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(d, msg->data, 8U);
}

static void test_txobj_takes_single_frames_only(void) {
	static uint8_t buf[LONG];
	j1939_txobj_cfg_t ocfg = {.buf = buf,
	                          .pgn = PGN_B,
	                          .period_us = 100000U,
	                          .len = LONG,
	                          .prio = 6U,
	                          .da = PEER,
	                          .ca = ca};
	j1939_txobj_t obj;

	cfg.txobj_cfg = &ocfg;
	cfg.txobj = &obj;
	cfg.txobj_len = 1U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_init(&s, &cfg));
	ocfg.len = 8U;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&s, &cfg));
}

static void test_rxobj_takes_single_frames_only(void) {
	static uint8_t buf[LONG];
	j1939_rxobj_cfg_t ocfg = {
	        .buf = buf, .pgn = PGN_A, .buf_len = 9U, .min_len = 1U, .sa = PEER};
	j1939_rxobj_t obj;

	TEST_ASSERT_EQUAL_UINT32(8U, J1939_MAX_MSG_LEN);
	cfg.rxobj_cfg = &ocfg;
	cfg.rxobj = &obj;
	cfg.rxobj_len = 1U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_init(&s, &cfg));
	ocfg.buf_len = 8U;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&s, &cfg));
}

static void test_dm_takes_single_frame_configurations_only(void) {
	static j1939_dtc_t active[2];
	static j1939_dtc_t prev[2];
	static j1939_dm_hold_t hold[1];
	static uint8_t dm_buf[J1939_DM_BUF_LEN(2U)];
	j1939_dm_t dm;
	j1939_dm_cfg_t dm_cfg = {.state = &dm,
	                         .active = active,
	                         .active_len = 2U,
	                         .prev = prev,
	                         .prev_len = 2U,
	                         .hold = hold,
	                         .hold_len = 1U,
	                         .buf = dm_buf,
	                         .buf_len = sizeof(dm_buf)};
	const uint8_t *sent;

	/* Two DTCs need a multi-packet DM1. */
	ca_cfg.dm = &dm_cfg;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_init(&s, &cfg));
	dm_cfg.active_len = 1U;
	dm_cfg.prev_len = 1U;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&s, &cfg));

	claim();
	(void)expect_pgn(J1939_PGN_DM1, 8U); /* first DM1 right after the claim */
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_dm_active_set(&s, ca, &dtc_a, 1U));
	process(0U);
	sent = expect_pgn(J1939_PGN_DM1, 8U);
	/* SPN 520210, FMI 3, conversion method 0, occurrence count 1. */
	TEST_ASSERT_EQUAL_HEX8(0x12U, sent[2]);
	TEST_ASSERT_EQUAL_HEX8(0xF0U, sent[3]);
	TEST_ASSERT_EQUAL_HEX8(0xE3U, sent[4]);
	TEST_ASSERT_EQUAL_HEX8(0x01U, sent[5]);
	expect_none();
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_stack_runs_without_tp_buffers);
	RUN_TEST(test_send_takes_single_frames_only);
	RUN_TEST(test_received_bam_is_dropped);
	RUN_TEST(test_received_rts_is_not_answered);
	RUN_TEST(test_single_frames_are_still_delivered);
	RUN_TEST(test_txobj_takes_single_frames_only);
	RUN_TEST(test_rxobj_takes_single_frames_only);
	RUN_TEST(test_dm_takes_single_frame_configurations_only);
	return UNITY_END();
}
