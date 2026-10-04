/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/* Receive objects that identify their sender by NAME through the NAME table. */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "j1939/j1939.h"

#define TX_LEN    16U
#define MSG_LEN   1U
#define NAMES_LEN 4U

#define OWN        0x10U
#define PEER       0x42U
#define OTHER      0x43U
#define OWN_NAME   0x1000U
#define PEER_NAME  0x2000U
#define OTHER_NAME 0x3000U
#define TP_BAM     0x20U

#define PGN_A  0xFF10U /* 8 bytes, a NAME object and an address object */
#define PGN_TP 0xFF20U /* multi-packet, 9..20 bytes, a NAME object */

#define TIMEOUT 300000U

enum { OBJ_NAME = 0, OBJ_SA, OBJ_TP, OBJ_COUNT };

static j1939_port_frame_t tx_buf[TX_LEN];
static j1939_msg_slot_t msg_buf[MSG_LEN];
static j1939_tp_buf_t tp_rx[1];
static j1939_names_entry_t names[NAMES_LEN];
static uint8_t buf_name[8];
static uint8_t buf_sa[8];
static uint8_t buf_tp[20];
static const j1939_rxobj_cfg_t objs_cfg[OBJ_COUNT] = {
        {buf_name, PGN_A, TIMEOUT, 8U, 8U, 0U, PEER_NAME},
        {buf_sa, PGN_A, TIMEOUT, 8U, 8U, PEER, 0U},
        {buf_tp, PGN_TP, 0U, 20U, 9U, 0U, PEER_NAME},
};
static j1939_rxobj_t objs[OBJ_COUNT];
static j1939_t s;
static j1939_ca_id_t ca;

static const uint8_t pay1[8] = {1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U};
static const uint8_t pay2[8] = {2U, 2U, 2U, 2U, 2U, 2U, 2U, 2U};
static const uint8_t pay3[8] = {3U, 3U, 3U, 3U, 3U, 3U, 3U, 3U};

static void rx_msg(uint32_t pgn, uint8_t da, uint8_t sa, const uint8_t *d, uint8_t len) {
	j1939_port_frame_t f;
	uint32_t id = 0U;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_id_build(6U, pgn, da, sa, &id));
	j1939_port_frame_build(&f, id, d, len);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_rx(&s, &f));
}

/* Another node's Address Claimed from sa, or its Cannot Claim with sa J1939_ADDR_NULL. */
static void rx_claim(uint8_t sa, uint64_t name) {
	uint8_t d[8];

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_name_to_bytes(name, d));
	rx_msg(J1939_PGN_ADDRESS_CLAIMED, J1939_ADDR_GLOBAL, sa, d, 8U);
}

static void process(uint32_t us) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(&s, us));
}

static void drain(void) {
	while (j1939_tx_peek(&s) != NULL) {
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_tx_pop(&s));
	}
}

static j1939_rxobj_status_t status_of(uint16_t index) {
	j1939_rxobj_status_t st;

	(void)memset(&st, 0xA5, sizeof(st));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_rxobj_get(&s, index, &st));
	return st;
}

static void stack_init(bool with_names) {
	const j1939_cfg_t cfg = {
	        .tx_buf = tx_buf,
	        .tx_len = TX_LEN,
	        .msg_buf = msg_buf,
	        .msg_len = MSG_LEN,
	        .tp_rx_buf = tp_rx,
	        .tp_rx_buf_len = 1U,
	};

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&s, &cfg));
	TEST_ASSERT_EQUAL(
	        J1939_RET_OK,
	        j1939_ca_add(&s, &(j1939_ca_cfg_t){.address = OWN, .name = OWN_NAME}, &ca));
	if (with_names) {
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_names_init(&s, names, NAMES_LEN));
	}
}

void setUp(void) {
	(void)memset(buf_name, 0, sizeof(buf_name));
	(void)memset(buf_sa, 0, sizeof(buf_sa));
	(void)memset(buf_tp, 0, sizeof(buf_tp));
	stack_init(true);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_rxobj_init(&s, objs_cfg, objs, OBJ_COUNT));
	process(0U); /* claims OWN, then the startup Request of the NAME table */
	process(0U);
	drain();
}

void tearDown(void) {
}

static void test_cfg_size(void) {
	/* The NAME adds 8 bytes; the state is unchanged. */
	TEST_ASSERT_LESS_OR_EQUAL_UINT32(32U, (uint32_t)sizeof(j1939_rxobj_cfg_t));
	TEST_ASSERT_LESS_OR_EQUAL_UINT32(12U, (uint32_t)sizeof(j1939_rxobj_t));
}

static void test_init_rejects_name_without_table(void) {
	stack_init(false);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_rxobj_init(&s, objs_cfg, objs, OBJ_COUNT));
	/* An address-only table is accepted without a NAME table. */
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_rxobj_init(&s, &objs_cfg[OBJ_SA], objs, 1U));
}

static void test_init_rejects_duplicate_name(void) {
	static uint8_t b[8];
	j1939_rxobj_t o[2];
	const j1939_rxobj_cfg_t dup[2] = {{b, PGN_A, 0U, 8U, 8U, 0U, PEER_NAME},
	                                  {b, PGN_A, 0U, 8U, 8U, PEER, PEER_NAME}};
	const j1939_rxobj_cfg_t mixed[2] = {{b, PGN_A, 0U, 8U, 8U, PEER, 0U},
	                                    {b, PGN_A, 0U, 8U, 8U, PEER, PEER_NAME}};
	const j1939_rxobj_cfg_t other_pgn[2] = {{b, PGN_A, 0U, 8U, 8U, 0U, PEER_NAME},
	                                        {b, PGN_TP, 0U, 8U, 8U, 0U, PEER_NAME}};
	/* With a NAME, sa is ignored: even 254 is accepted. */
	const j1939_rxobj_cfg_t sa_ignored = {b, PGN_A, 0U, 8U, 8U, J1939_ADDR_NULL, PEER_NAME};

	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_rxobj_init(&s, dup, o, 2U));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_rxobj_init(&s, mixed, o, 2U));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_rxobj_init(&s, other_pgn, o, 2U));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_rxobj_init(&s, &sa_ignored, o, 1U));
}

static void test_unknown_name_receives_nothing(void) {
	rx_msg(PGN_A, J1939_ADDR_GLOBAL, PEER, pay1, 8U);
	TEST_ASSERT_EQUAL(J1939_RXOBJ_NO_DATA, status_of(OBJ_NAME).state);
	/* The address object takes it. */
	TEST_ASSERT_EQUAL(J1939_RXOBJ_VALID, status_of(OBJ_SA).state);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(pay1, buf_sa, 8U);
}

static void test_both_kinds_updated(void) {
	rx_claim(PEER, PEER_NAME);
	rx_msg(PGN_A, J1939_ADDR_GLOBAL, PEER, pay1, 8U);
	TEST_ASSERT_EQUAL(J1939_RXOBJ_VALID, status_of(OBJ_NAME).state);
	TEST_ASSERT_EQUAL(J1939_RXOBJ_VALID, status_of(OBJ_SA).state);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(pay1, buf_name, 8U);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(pay1, buf_sa, 8U);
}

static void test_follows_address_change(void) {
	rx_claim(PEER, PEER_NAME);
	rx_msg(PGN_A, J1939_ADDR_GLOBAL, PEER, pay1, 8U);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(pay1, buf_name, 8U);

	/* The node moves: its new address reaches the NAME object, its old one no longer. */
	rx_claim(OTHER, PEER_NAME);
	rx_msg(PGN_A, J1939_ADDR_GLOBAL, OTHER, pay2, 8U);
	TEST_ASSERT_TRUE(status_of(OBJ_NAME).updated);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(pay2, buf_name, 8U);
	rx_msg(PGN_A, J1939_ADDR_GLOBAL, PEER, pay3, 8U);
	TEST_ASSERT_FALSE(status_of(OBJ_NAME).updated);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(pay2, buf_name, 8U);
	/* The address object still takes whatever comes from PEER. */
	TEST_ASSERT_EQUAL_HEX8_ARRAY(pay3, buf_sa, 8U);
}

static void test_address_taken_by_other_node(void) {
	rx_claim(PEER, PEER_NAME);
	rx_claim(PEER, OTHER_NAME);
	rx_msg(PGN_A, J1939_ADDR_GLOBAL, PEER, pay1, 8U);
	TEST_ASSERT_EQUAL(J1939_RXOBJ_NO_DATA, status_of(OBJ_NAME).state);
}

static void test_no_address_times_out(void) {
	j1939_rxobj_status_t st;

	rx_claim(PEER, PEER_NAME);
	rx_msg(PGN_A, J1939_ADDR_GLOBAL, PEER, pay1, 8U);
	process(0U); /* the reception counts from here */

	/* Cannot Claim: the NAME has no address, nothing reaches the object. */
	rx_claim(J1939_ADDR_NULL, PEER_NAME);
	rx_msg(PGN_A, J1939_ADDR_GLOBAL, PEER, pay2, 8U);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(pay1, buf_name, 8U);
	process(TIMEOUT - 1U);
	TEST_ASSERT_EQUAL(J1939_RXOBJ_VALID, status_of(OBJ_NAME).state);
	process(1U);
	st = status_of(OBJ_NAME);
	TEST_ASSERT_EQUAL(J1939_RXOBJ_TIMEOUT, st.state);
	TEST_ASSERT_EQUAL_UINT32(TIMEOUT, st.age_us);
}

static void test_bam_into_name_object(void) {
	static const uint8_t data[12] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U};
	const uint8_t bam[8] = {TP_BAM,
	                        12U,
	                        0U,
	                        2U,
	                        0xFFU,
	                        (uint8_t)PGN_TP,
	                        (uint8_t)(PGN_TP >> 8),
	                        (uint8_t)(PGN_TP >> 16)};
	uint8_t dt1[8] = {1U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
	uint8_t dt2[8] = {2U, 0U, 0U, 0U, 0U, 0U, 0xFFU, 0xFFU};

	(void)memcpy(&dt1[1], &data[0], 7U);
	(void)memcpy(&dt2[1], &data[7], 5U);

	/* Unknown sender: the BAM is not taken. */
	rx_msg(J1939_PGN_TP_CM, J1939_ADDR_GLOBAL, PEER, bam, 8U);
	rx_msg(J1939_PGN_TP_DT, J1939_ADDR_GLOBAL, PEER, dt1, 8U);
	rx_msg(J1939_PGN_TP_DT, J1939_ADDR_GLOBAL, PEER, dt2, 8U);
	TEST_ASSERT_EQUAL(J1939_RXOBJ_NO_DATA, status_of(OBJ_TP).state);

	rx_claim(PEER, PEER_NAME);
	rx_msg(J1939_PGN_TP_CM, J1939_ADDR_GLOBAL, PEER, bam, 8U);
	rx_msg(J1939_PGN_TP_DT, J1939_ADDR_GLOBAL, PEER, dt1, 8U);
	rx_msg(J1939_PGN_TP_DT, J1939_ADDR_GLOBAL, PEER, dt2, 8U);
	TEST_ASSERT_EQUAL(J1939_RXOBJ_VALID, status_of(OBJ_TP).state);
	TEST_ASSERT_EQUAL_UINT16(12U, status_of(OBJ_TP).len);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(data, buf_tp, 12U);
	/* No message slot was needed. */
	TEST_ASSERT_NULL(j1939_msg_peek(&s));
}

static void test_two_names_same_pgn(void) {
	static uint8_t b_peer[8];
	static uint8_t b_other[8];
	j1939_rxobj_t o[2];
	j1939_rxobj_status_t st;
	const j1939_rxobj_cfg_t two[2] = {{b_peer, PGN_A, 0U, 8U, 8U, 0U, PEER_NAME},
	                                  {b_other, PGN_A, 0U, 8U, 8U, 0U, OTHER_NAME}};

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_rxobj_init(&s, two, o, 2U));
	rx_claim(PEER, PEER_NAME);
	rx_claim(OTHER, OTHER_NAME);
	rx_msg(PGN_A, J1939_ADDR_GLOBAL, OTHER, pay2, 8U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_rxobj_get(&s, 0U, &st));
	TEST_ASSERT_EQUAL(J1939_RXOBJ_NO_DATA, st.state);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_rxobj_get(&s, 1U, &st));
	TEST_ASSERT_EQUAL(J1939_RXOBJ_VALID, st.state);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(pay2, b_other, 8U);
}

static void test_table_removed_stops_matching(void) {
	rx_claim(PEER, PEER_NAME);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_names_init(&s, NULL, 0U));
	rx_msg(PGN_A, J1939_ADDR_GLOBAL, PEER, pay1, 8U);
	TEST_ASSERT_EQUAL(J1939_RXOBJ_NO_DATA, status_of(OBJ_NAME).state);
	TEST_ASSERT_EQUAL(J1939_RXOBJ_VALID, status_of(OBJ_SA).state);
}

static void test_no_match_from_null_address(void) {
	/* A message from 254 matches no NAME object, even if a NAME has no address. */
	rx_claim(J1939_ADDR_NULL, PEER_NAME);
	rx_msg(PGN_A, J1939_ADDR_GLOBAL, J1939_ADDR_NULL, pay1, 8U);
	TEST_ASSERT_EQUAL(J1939_RXOBJ_NO_DATA, status_of(OBJ_NAME).state);
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_cfg_size);
	RUN_TEST(test_init_rejects_name_without_table);
	RUN_TEST(test_init_rejects_duplicate_name);
	RUN_TEST(test_unknown_name_receives_nothing);
	RUN_TEST(test_both_kinds_updated);
	RUN_TEST(test_follows_address_change);
	RUN_TEST(test_address_taken_by_other_node);
	RUN_TEST(test_no_address_times_out);
	RUN_TEST(test_bam_into_name_object);
	RUN_TEST(test_two_names_same_pgn);
	RUN_TEST(test_table_removed_stops_matching);
	RUN_TEST(test_no_match_from_null_address);
	return UNITY_END();
}
