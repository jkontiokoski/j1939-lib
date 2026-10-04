/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/* Receive objects: storage, sender and length filtering, timeout supervision, TP reception. */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "j1939/j1939.h"

#define TX_LEN  8U
#define MSG_LEN 1U

#define OWN    0x10U
#define PEER   0x42U
#define OTHER  0x43U
#define NAME   0x1000U
#define TP_BAM 0x20U
#define TP_RTS 0x10U
#define TP_CTS 0x11U
#define EOMA   0x13U

#define PGN_A   0xFF10U /* PDU2, 8 bytes */
#define PGN_B   0xEF00U /* PDU1 (Proprietary A), 4..8 bytes */
#define PGN_TP  0xFF20U /* multi-packet, 9..20 bytes */
#define PGN_LST 0xFF30U /* also in rx_pgns */

#define TIMEOUT 300000U

enum { OBJ_A = 0, OBJ_B, OBJ_TP, OBJ_LST, OBJ_COUNT };

static j1939_port_frame_t tx_buf[TX_LEN];
static j1939_msg_slot_t msg_buf[MSG_LEN];
static j1939_tp_buf_t tp_rx[1];
static const uint32_t rx_pgns[] = {PGN_LST};
static uint8_t buf_a[8];
static uint8_t buf_b[8];
static uint8_t buf_tp[20];
static uint8_t buf_lst[8];
static const j1939_rxobj_cfg_t objs_cfg[OBJ_COUNT] = {
        {buf_a, PGN_A, TIMEOUT, 8U, 8U, PEER, 0U},
        {buf_b, PGN_B, 0U, 8U, 4U, PEER, 0U},
        {buf_tp, PGN_TP, TIMEOUT, 20U, 9U, PEER, 0U},
        {buf_lst, PGN_LST, TIMEOUT, 8U, 1U, PEER, 0U},
};
static j1939_rxobj_t objs[OBJ_COUNT];
static j1939_t s;
static j1939_ca_id_t ca;

static const uint8_t pay[20] = {1U,  2U,  3U,  4U,  5U,  6U,  7U,  8U,  9U,  10U,
                                11U, 12U, 13U, 14U, 15U, 16U, 17U, 18U, 19U, 20U};

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

static void rx_msg(uint32_t pgn, uint8_t da, uint8_t sa, const uint8_t *d, uint8_t len) {
	rx_raw(make_id(6U, pgn, da, sa), d, len);
}

static void process(uint32_t us) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(&s, us));
}

static j1939_rxobj_status_t status_of(uint16_t index) {
	j1939_rxobj_status_t st;

	(void)memset(&st, 0xA5, sizeof(st));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_rxobj_get(&s, index, &st));
	return st;
}

static void expect_state(uint16_t index, j1939_rxobj_state_t state) {
	TEST_ASSERT_EQUAL(state, status_of(index).state);
}

/* Sends TP.CM frames of a BAM or RTS from sa for len bytes of pgn. */
static void rx_announce(uint8_t ctrl, uint8_t da, uint8_t sa, uint32_t pgn, uint16_t len) {
	const uint8_t d[8] = {ctrl,  (uint8_t)len, (uint8_t)(len >> 8), (uint8_t)((len + 6U) / 7U),
	                      0xFFU, (uint8_t)pgn, (uint8_t)(pgn >> 8), (uint8_t)(pgn >> 16)};

	rx_msg(J1939_PGN_TP_CM, da, sa, d, 8U);
}

static void rx_packets(uint8_t da, uint8_t sa, const uint8_t *data, uint16_t len) {
	uint8_t seq;
	uint16_t off;

	for (seq = 1U, off = 0U; off < len; seq++, off += 7U) {
		uint8_t d[8] = {seq, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU};
		uint16_t n = (uint16_t)(len - off);

		(void)memcpy(&d[1], &data[off], (n > 7U) ? 7U : n);
		rx_msg(J1939_PGN_TP_DT, da, sa, d, 8U);
	}
}

static uint8_t pop_ctrl(void) {
	const j1939_port_frame_t *f = j1939_tx_peek(&s);
	uint8_t ctrl;

	TEST_ASSERT_NOT_NULL(f);
	TEST_ASSERT_EQUAL_HEX32(J1939_PGN_TP_CM, j1939_id_pgn_get(j1939_port_frame_id_get(f)));
	ctrl = j1939_port_frame_data(f)[0];
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_tx_pop(&s));
	return ctrl;
}

void setUp(void) {
	const j1939_cfg_t cfg = {
	        .tx_buf = tx_buf,
	        .tx_len = TX_LEN,
	        .msg_buf = msg_buf,
	        .msg_len = MSG_LEN,
	        .rx_pgns = rx_pgns,
	        .rx_pgns_len = 1U,
	        .tp_rx_buf = tp_rx,
	        .tp_rx_buf_len = 1U,
	};

	(void)memset(buf_a, 0, sizeof(buf_a));
	(void)memset(buf_b, 0, sizeof(buf_b));
	(void)memset(buf_tp, 0, sizeof(buf_tp));
	(void)memset(buf_lst, 0, sizeof(buf_lst));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&s, &cfg));
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_ca_add(&s, &(j1939_ca_cfg_t){.address = OWN, .name = NAME}, &ca));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_rxobj_init(&s, objs_cfg, objs, OBJ_COUNT));
	process(0U); /* claims OWN */
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_tx_pop(&s));
}

void tearDown(void) {
}

static void test_state_small(void) {
	TEST_ASSERT_LESS_OR_EQUAL_UINT32(12U, (uint32_t)sizeof(j1939_rxobj_t));
	/* docs/configuration.md states the configuration entry size. */
	TEST_ASSERT_LESS_OR_EQUAL_UINT32(32U, (uint32_t)sizeof(j1939_rxobj_cfg_t));
}

static void test_init_rejects_invalid(void) {
	static uint8_t b[8];
	j1939_rxobj_t o[2];
	const j1939_rxobj_cfg_t bad[] = {
	        {NULL, PGN_A, 0U, 8U, 8U, PEER, 0U},                      /* no buffer */
	        {b, 0xEF01U, 0U, 8U, 8U, PEER, 0U},                       /* PDU1 with DA */
	        {b, 0x40000U, 0U, 8U, 8U, PEER, 0U},                      /* above J1939_PGN_MAX */
	        {b, J1939_PGN_REQUEST, 0U, 8U, 3U, PEER, 0U},             /* stack owned */
	        {b, J1939_PGN_ADDRESS_CLAIMED, 0U, 8U, 8U, PEER, 0U},     /* stack owned */
	        {b, J1939_PGN_TP_CM, 0U, 8U, 8U, PEER, 0U},               /* stack owned */
	        {b, J1939_PGN_TP_DT, 0U, 8U, 8U, PEER, 0U},               /* stack owned */
	        {b, PGN_A, 0U, 8U, 8U, J1939_ADDR_NULL, 0U},              /* sa 254 */
	        {b, PGN_A, 0U, 8U, 0U, PEER, 0U},                         /* min_len 0 */
	        {b, PGN_A, 0U, 4U, 5U, PEER, 0U},                         /* min_len > buf_len */
	        {b, PGN_A, 0U, J1939_CFG_TP_BUF_SIZE + 1U, 8U, PEER, 0U}, /* too large */
	};
	const j1939_rxobj_cfg_t dup[2] = {{b, PGN_A, 0U, 8U, 8U, PEER, 0U},
	                                  {b, PGN_A, 0U, 8U, 8U, PEER, 0U}};
	const j1939_rxobj_cfg_t two[2] = {{b, PGN_A, 0U, 8U, 8U, PEER, 0U},
	                                  {b, PGN_A, 0U, 8U, 8U, OTHER, 0U}};
	uint32_t i;

	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_rxobj_init(NULL, objs_cfg, objs, OBJ_COUNT));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_rxobj_init(&s, NULL, objs, OBJ_COUNT));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_rxobj_init(&s, objs_cfg, NULL, OBJ_COUNT));
	for (i = 0U; i < (sizeof(bad) / sizeof(bad[0])); i++) {
		TEST_ASSERT_EQUAL_MESSAGE(J1939_RET_ERR_ARG, j1939_rxobj_init(&s, &bad[i], o, 1U),
		                          "entry rejected");
	}
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_rxobj_init(&s, dup, o, 2U));

	/* Nothing changed: the original table still works. */
	rx_msg(PGN_A, J1939_ADDR_GLOBAL, PEER, pay, 8U);
	expect_state(OBJ_A, J1939_RXOBJ_VALID);

	/* The same PGN from two senders is two objects. */
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_rxobj_init(&s, two, o, 2U));
	/* An empty table removes the objects. */
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_rxobj_init(&s, NULL, NULL, 0U));
	{
		j1939_rxobj_status_t st;

		TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_rxobj_get(&s, 0U, &st));
	}
}

static void test_get_args(void) {
	j1939_rxobj_status_t st;

	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_rxobj_get(NULL, OBJ_A, &st));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_rxobj_get(&s, OBJ_A, NULL));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_rxobj_get(&s, OBJ_COUNT, &st));
}

static void test_no_data_initially(void) {
	j1939_rxobj_status_t st = status_of(OBJ_A);

	TEST_ASSERT_EQUAL(J1939_RXOBJ_NO_DATA, st.state);
	TEST_ASSERT_EQUAL_UINT16(0U, st.len);
	TEST_ASSERT_EQUAL_UINT32(0U, st.age_us);
	TEST_ASSERT_FALSE(st.updated);
	process(TIMEOUT * 2U);
	expect_state(OBJ_A, J1939_RXOBJ_NO_DATA);
	TEST_ASSERT_EQUAL_UINT32(0U, j1939_stats_get(&s)->rxobj_timeout);
}

static void test_stores_latest(void) {
	j1939_rxobj_status_t st;

	rx_msg(PGN_A, J1939_ADDR_GLOBAL, PEER, pay, 8U);
	st = status_of(OBJ_A);
	TEST_ASSERT_EQUAL(J1939_RXOBJ_VALID, st.state);
	TEST_ASSERT_EQUAL_UINT16(8U, st.len);
	TEST_ASSERT_EQUAL_UINT32(0U, st.age_us);
	TEST_ASSERT_TRUE(st.updated);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(pay, buf_a, 8U);
	TEST_ASSERT_FALSE(status_of(OBJ_A).updated);

	rx_msg(PGN_A, J1939_ADDR_GLOBAL, PEER, &pay[8], 8U);
	TEST_ASSERT_TRUE(status_of(OBJ_A).updated);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(&pay[8], buf_a, 8U);
	/* Not in rx_pgns: no message for the application. */
	TEST_ASSERT_NULL(j1939_msg_peek(&s));
}

static void test_other_sender_or_pgn_ignored(void) {
	rx_msg(PGN_A, J1939_ADDR_GLOBAL, OTHER, pay, 8U);
	rx_msg(0xFF11U, J1939_ADDR_GLOBAL, PEER, pay, 8U);
	expect_state(OBJ_A, J1939_RXOBJ_NO_DATA);
	TEST_ASSERT_EQUAL_UINT32(0U, j1939_stats_get(&s)->rxobj_rejected);
}

static void test_destination_filter(void) {
	/* PDU1 to another node: not for this stack. */
	rx_msg(PGN_B, 0x55U, PEER, pay, 8U);
	expect_state(OBJ_B, J1939_RXOBJ_NO_DATA);
	/* To the CA's address, and to the global address. */
	rx_msg(PGN_B, OWN, PEER, pay, 8U);
	expect_state(OBJ_B, J1939_RXOBJ_VALID);
	rx_msg(PGN_B, J1939_ADDR_GLOBAL, PEER, &pay[4], 4U);
	TEST_ASSERT_EQUAL_UINT16(4U, status_of(OBJ_B).len);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(&pay[4], buf_b, 4U);
}

static void test_length_rejected(void) {
	rx_msg(PGN_B, OWN, PEER, pay, 8U);
	(void)status_of(OBJ_B);
	/* Shorter than min_len 4. */
	rx_msg(PGN_B, OWN, PEER, &pay[10], 3U);
	TEST_ASSERT_EQUAL_UINT32(1U, j1939_stats_get(&s)->rxobj_rejected);
	TEST_ASSERT_FALSE(status_of(OBJ_B).updated);
	TEST_ASSERT_EQUAL_UINT16(8U, status_of(OBJ_B).len);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(pay, buf_b, 8U);
}

static void test_rejection_keeps_age(void) {
	/* buf_len 8 and min_len 8: a 7-byte frame is rejected and does not refresh. */
	rx_msg(PGN_A, J1939_ADDR_GLOBAL, PEER, pay, 8U);
	process(1000U);           /* reception counts from here */
	process(TIMEOUT - 1000U); /* age TIMEOUT - 1000 */
	rx_msg(PGN_A, J1939_ADDR_GLOBAL, PEER, pay, 7U);
	TEST_ASSERT_EQUAL_UINT32(1U, j1939_stats_get(&s)->rxobj_rejected);
	process(1000U);
	expect_state(OBJ_A, J1939_RXOBJ_TIMEOUT);
}

static void test_timeout_boundaries(void) {
	j1939_rxobj_status_t st;

	rx_msg(PGN_A, J1939_ADDR_GLOBAL, PEER, pay, 8U);
	process(50000U); /* the call after the reception does not age it */
	TEST_ASSERT_EQUAL_UINT32(0U, status_of(OBJ_A).age_us);
	process(TIMEOUT - 1U);
	st = status_of(OBJ_A);
	TEST_ASSERT_EQUAL(J1939_RXOBJ_VALID, st.state);
	TEST_ASSERT_EQUAL_UINT32(TIMEOUT - 1U, st.age_us);
	process(1U);
	st = status_of(OBJ_A);
	TEST_ASSERT_EQUAL(J1939_RXOBJ_TIMEOUT, st.state);
	TEST_ASSERT_EQUAL_UINT32(TIMEOUT, st.age_us);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(pay, buf_a, 8U);
	TEST_ASSERT_EQUAL_UINT32(1U, j1939_stats_get(&s)->rxobj_timeout);
	process(TIMEOUT);
	TEST_ASSERT_EQUAL_UINT32(1U, j1939_stats_get(&s)->rxobj_timeout);

	/* The next reception makes it valid again, and the next timeout counts again. */
	rx_msg(PGN_A, J1939_ADDR_GLOBAL, PEER, pay, 8U);
	st = status_of(OBJ_A);
	TEST_ASSERT_EQUAL(J1939_RXOBJ_VALID, st.state);
	TEST_ASSERT_EQUAL_UINT32(0U, st.age_us);
	process(0U);
	process(TIMEOUT);
	expect_state(OBJ_A, J1939_RXOBJ_TIMEOUT);
	TEST_ASSERT_EQUAL_UINT32(2U, j1939_stats_get(&s)->rxobj_timeout);
}

static void test_no_supervision(void) {
	rx_msg(PGN_B, OWN, PEER, pay, 8U);
	process(0U);
	process(UINT32_MAX);
	process(UINT32_MAX);
	TEST_ASSERT_EQUAL(J1939_RXOBJ_VALID, status_of(OBJ_B).state);
	TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, status_of(OBJ_B).age_us);
	TEST_ASSERT_EQUAL_UINT32(0U, j1939_stats_get(&s)->rxobj_timeout);
}

static void test_listed_also_delivered(void) {
	const j1939_msg_t *msg;

	rx_msg(PGN_LST, J1939_ADDR_GLOBAL, PEER, pay, 2U);
	expect_state(OBJ_LST, J1939_RXOBJ_VALID);
	msg = j1939_msg_peek(&s);
	TEST_ASSERT_NOT_NULL(msg);
	TEST_ASSERT_EQUAL_HEX32(PGN_LST, msg->pgn);

	/* The only slot is in use: the object is still updated. */
	rx_msg(PGN_LST, J1939_ADDR_GLOBAL, PEER, &pay[2], 2U);
	TEST_ASSERT_EQUAL_UINT32(1U, j1939_stats_get(&s)->rx_msg_overflow);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(&pay[2], buf_lst, 2U);

	/* A listed PGN from a sender without object still reaches the slots. */
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_msg_pop(&s));
	rx_msg(PGN_LST, J1939_ADDR_GLOBAL, OTHER, pay, 2U);
	TEST_ASSERT_NOT_NULL(j1939_msg_peek(&s));
}

static void test_bam_into_object(void) {
	j1939_rxobj_status_t st;

	rx_announce(TP_BAM, J1939_ADDR_GLOBAL, PEER, PGN_TP, 20U);
	rx_packets(J1939_ADDR_GLOBAL, PEER, pay, 20U);
	st = status_of(OBJ_TP);
	TEST_ASSERT_EQUAL(J1939_RXOBJ_VALID, st.state);
	TEST_ASSERT_EQUAL_UINT16(20U, st.len);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(pay, buf_tp, 20U);
	/* No message slot used, and the reassembly buffer is free again. */
	TEST_ASSERT_NULL(j1939_msg_peek(&s));
	rx_announce(TP_BAM, J1939_ADDR_GLOBAL, PEER, PGN_TP, 10U);
	rx_packets(J1939_ADDR_GLOBAL, PEER, &pay[5], 10U);
	TEST_ASSERT_EQUAL_UINT16(10U, status_of(OBJ_TP).len);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(&pay[5], buf_tp, 10U);
	TEST_ASSERT_EQUAL_UINT32(0U, j1939_stats_get(&s)->tp_rx_refused);
}

static void test_bam_other_sender_ignored(void) {
	rx_announce(TP_BAM, J1939_ADDR_GLOBAL, OTHER, PGN_TP, 20U);
	rx_packets(J1939_ADDR_GLOBAL, OTHER, pay, 20U);
	expect_state(OBJ_TP, J1939_RXOBJ_NO_DATA);
	TEST_ASSERT_EQUAL_UINT16(0U, s.tx.ring.count);
}

static void test_bam_too_long_rejected(void) {
	/* buf_len of OBJ_A is 8: a multi-packet PGN_A is received and refused by the object. */
	rx_announce(TP_BAM, J1939_ADDR_GLOBAL, PEER, PGN_A, 9U);
	rx_packets(J1939_ADDR_GLOBAL, PEER, pay, 9U);
	expect_state(OBJ_A, J1939_RXOBJ_NO_DATA);
	TEST_ASSERT_EQUAL_UINT32(1U, j1939_stats_get(&s)->rxobj_rejected);
}

static void test_rts_cts_into_object(void) {
	rx_announce(TP_RTS, OWN, PEER, PGN_TP, 16U);
	TEST_ASSERT_EQUAL_HEX8(TP_CTS, pop_ctrl());
	rx_packets(OWN, PEER, pay, 16U);
	TEST_ASSERT_EQUAL_HEX8(EOMA, pop_ctrl());
	TEST_ASSERT_EQUAL_UINT16(16U, status_of(OBJ_TP).len);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(pay, buf_tp, 16U);
	TEST_ASSERT_NULL(j1939_msg_peek(&s));
}

static void test_rts_unwanted_aborted(void) {
	/* PGN_TP from another sender: no object, not listed. */
	rx_announce(TP_RTS, OWN, OTHER, PGN_TP, 16U);
	TEST_ASSERT_EQUAL_HEX8(0xFFU, pop_ctrl());
}

static void test_reinit_resets(void) {
	rx_msg(PGN_A, J1939_ADDR_GLOBAL, PEER, pay, 8U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_rxobj_init(&s, objs_cfg, objs, OBJ_COUNT));
	expect_state(OBJ_A, J1939_RXOBJ_NO_DATA);
	TEST_ASSERT_FALSE(status_of(OBJ_A).updated);
}

static void test_stack_init_removes(void) {
	const j1939_cfg_t cfg = {
	        .tx_buf = tx_buf, .tx_len = TX_LEN, .msg_buf = msg_buf, .msg_len = MSG_LEN};
	j1939_rxobj_status_t st;

	rx_msg(PGN_A, J1939_ADDR_GLOBAL, PEER, pay, 7U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&s, &cfg));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_rxobj_get(&s, OBJ_A, &st));
	TEST_ASSERT_EQUAL_UINT32(0U, j1939_stats_get(&s)->rxobj_rejected);
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_state_small);
	RUN_TEST(test_init_rejects_invalid);
	RUN_TEST(test_get_args);
	RUN_TEST(test_no_data_initially);
	RUN_TEST(test_stores_latest);
	RUN_TEST(test_other_sender_or_pgn_ignored);
	RUN_TEST(test_destination_filter);
	RUN_TEST(test_length_rejected);
	RUN_TEST(test_rejection_keeps_age);
	RUN_TEST(test_timeout_boundaries);
	RUN_TEST(test_no_supervision);
	RUN_TEST(test_listed_also_delivered);
	RUN_TEST(test_bam_into_object);
	RUN_TEST(test_bam_other_sender_ignored);
	RUN_TEST(test_bam_too_long_rejected);
	RUN_TEST(test_rts_cts_into_object);
	RUN_TEST(test_rts_unwanted_aborted);
	RUN_TEST(test_reinit_resets);
	RUN_TEST(test_stack_init_removes);
	return UNITY_END();
}
