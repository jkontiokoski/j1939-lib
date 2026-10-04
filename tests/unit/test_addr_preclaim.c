/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/* Request before claim (J1939/81 address claiming) on one stack: frames of other nodes are
 * injected, sent frames inspected. */

#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "j1939/j1939.h"

#define TX_LEN    4U
#define MSG_LEN   2U
#define BUF_LEN   1U
#define TABLE_LEN 4U

#define OWN   0x20U
#define OWN_B 0x21U
#define NEW   0x30U
#define OTHER 0x42U
#define TOOL  0x43U
#define SELF  0x80U /* first self-configurable address */

#define ARB         0x8000000000000000U
#define NAME_OWN    0x0000000000001500U
#define NAME_OWN_B  0x0000000000001600U
#define NAME_WIN    0x0000000000001000U /* beats NAME_OWN */
#define NAME_LOSE   0x0000000000002000U /* loses to NAME_OWN */
#define NAME_OTHER2 0x0000000000003000U
#define WAIT        J1939_ADDR_PRECLAIM_WAIT_US

/* TP.CM control byte of a broadcast announcement. */
#define BAM 0x20U

static const uint32_t rx_pgns[] = {J1939_PGN_COMMANDED_ADDRESS};
static j1939_port_frame_t tx_buf[TX_LEN];
static j1939_msg_slot_t msg_buf[MSG_LEN];
static j1939_tp_buf_t tp_rx[BUF_LEN];
static j1939_names_entry_t entries[TABLE_LEN];
static j1939_t s;

static void init(void) {
	const j1939_cfg_t cfg = {
	        .tx_buf = tx_buf,
	        .tx_len = TX_LEN,
	        .msg_buf = msg_buf,
	        .msg_len = MSG_LEN,
	        .rx_pgns = rx_pgns,
	        .rx_pgns_len = 0U,
	        .tp_rx_buf = tp_rx,
	        .tp_rx_buf_len = BUF_LEN,
	};

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&s, &cfg));
}

static j1939_ca_id_t ca_add(uint8_t address, uint64_t name, bool preclaim, bool accept) {
	j1939_ca_id_t id = 0xFFU;

	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_ca_add(&s,
	                               &(j1939_ca_cfg_t){.address = address,
	                                                 .name = name,
	                                                 .accept_commanded = accept,
	                                                 .request_before_claim = preclaim},
	                               &id));
	return id;
}

static uint32_t make_id(uint8_t prio, uint32_t pgn, uint8_t da, uint8_t sa) {
	uint32_t id = 0U;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_id_build(prio, pgn, da, sa, &id));
	return id;
}

static void rx(uint32_t id, const uint8_t *data, uint8_t len) {
	j1939_port_frame_t f;

	j1939_port_frame_build(&f, id, data, len);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_rx(&s, &f));
}

static void rx_claim(uint8_t sa, uint64_t name) {
	uint8_t data[J1939_NAME_LEN];

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_name_to_bytes(name, data));
	rx(make_id(6U, J1939_PGN_ADDRESS_CLAIMED, J1939_ADDR_GLOBAL, sa), data, J1939_NAME_LEN);
}

static void rx_request_claims(uint8_t da) {
	const uint8_t data[3] = {0x00U, 0xEEU, 0x00U};

	rx(make_id(6U, J1939_PGN_REQUEST, da, OTHER), data, 3U);
}

/* A Commanded Address for name, broadcast by TOOL with BAM. */
static void rx_command(uint64_t name, uint8_t address) {
	const uint32_t pgn = J1939_PGN_COMMANDED_ADDRESS;
	const uint8_t cm[8] = {
	        BAM, 9U, 0U, 2U, 0xFFU, (uint8_t)pgn, (uint8_t)(pgn >> 8), (uint8_t)(pgn >> 16)};
	uint8_t d[14];
	uint8_t p[8];
	uint32_t i;

	(void)memset(d, 0xFF, sizeof(d));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_name_to_bytes(name, d));
	d[8] = address;
	rx(make_id(7U, J1939_PGN_TP_CM, J1939_ADDR_GLOBAL, TOOL), cm, 8U);
	for (i = 0U; i < 2U; i++) {
		p[0] = (uint8_t)(i + 1U);
		(void)memcpy(&p[1], &d[i * 7U], 7U);
		rx(make_id(7U, J1939_PGN_TP_DT, J1939_ADDR_GLOBAL, TOOL), p, 8U);
	}
}

/* Pops the next transmitted frame and checks it is a global Request for Address Claimed. */
static void tx_expect_request(uint8_t sa) {
	const uint8_t data[3] = {0x00U, 0xEEU, 0x00U};
	const j1939_port_frame_t *f = j1939_tx_peek(&s);

	TEST_ASSERT_NOT_NULL_MESSAGE(f, "no Request queued");
	TEST_ASSERT_EQUAL_HEX32(make_id(6U, J1939_PGN_REQUEST, J1939_ADDR_GLOBAL, sa),
	                        j1939_port_frame_id_get(f));
	TEST_ASSERT_EQUAL_UINT8(3U, j1939_port_frame_len_get(f));
	TEST_ASSERT_EQUAL_HEX8_ARRAY(data, j1939_port_frame_data(f), 3U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_tx_pop(&s));
}

/* Pops the next transmitted frame and checks it is Address Claimed from sa with name. */
static void tx_expect_claim(uint8_t sa, uint64_t name) {
	const j1939_port_frame_t *f = j1939_tx_peek(&s);
	uint8_t data[J1939_NAME_LEN];

	TEST_ASSERT_NOT_NULL_MESSAGE(f, "no Address Claimed queued");
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_name_to_bytes(name, data));
	TEST_ASSERT_EQUAL_HEX32(make_id(6U, J1939_PGN_ADDRESS_CLAIMED, J1939_ADDR_GLOBAL, sa),
	                        j1939_port_frame_id_get(f));
	TEST_ASSERT_EQUAL_HEX8_ARRAY(data, j1939_port_frame_data(f), J1939_NAME_LEN);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_tx_pop(&s));
}

static void tx_expect_empty(void) {
	TEST_ASSERT_NULL(j1939_tx_peek(&s));
}

static void expect_state(j1939_ca_id_t ca, j1939_addr_state_t state, uint8_t address) {
	j1939_addr_state_t st = J1939_ADDR_STATE_UNCLAIMED;
	uint8_t a = 0U;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_addr_get(&s, ca, &a, &st));
	TEST_ASSERT_EQUAL(state, st);
	TEST_ASSERT_EQUAL_HEX8(address, a);
}

static void process(uint32_t elapsed_us) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(&s, elapsed_us));
}

/* Sends the Request and lets the whole wait pass. */
static void request_and_wait(void) {
	process(0U);
	tx_expect_request(J1939_ADDR_NULL);
	process(WAIT);
}

void setUp(void) {
	init();
}

void tearDown(void) {
}

static void test_ca_starts_requesting_without_address(void) {
	static const uint8_t payload[8] = {0};
	const j1939_msg_t msg = {0xFF10U, 6U, 0U, J1939_ADDR_GLOBAL, 8U, payload};
	j1939_ca_id_t ca = ca_add(OWN, NAME_OWN, true, false);

	expect_state(ca, J1939_ADDR_STATE_REQUESTING, J1939_ADDR_NULL);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_NO_ADDRESS, j1939_send(&s, ca, &msg));
	tx_expect_empty();
}

static void test_request_goes_out_from_null_first(void) {
	j1939_ca_id_t ca = ca_add(OWN, NAME_OWN, true, false);

	process(0U);
	tx_expect_request(J1939_ADDR_NULL);
	tx_expect_empty();
	expect_state(ca, J1939_ADDR_STATE_REQUESTING, J1939_ADDR_NULL);
}

static void test_request_is_retried_while_tx_queue_full(void) {
	j1939_ca_id_t ca = ca_add(OWN, NAME_OWN, true, false);
	uint32_t i;

	/* Directed Requests fill the queue; they do not count as the global one. */
	for (i = 0U; i < TX_LEN; i++) {
		TEST_ASSERT_EQUAL(J1939_RET_OK,
		                  j1939_request_send(&s, ca, J1939_PGN_ADDRESS_CLAIMED, OTHER));
	}
	process(0U);
	process(WAIT);
	for (i = 0U; i < TX_LEN; i++) {
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_tx_pop(&s));
	}
	tx_expect_empty();
	expect_state(ca, J1939_ADDR_STATE_REQUESTING, J1939_ADDR_NULL);

	process(0U);
	tx_expect_request(J1939_ADDR_NULL);
	/* The wait starts with the Request. */
	process(WAIT - 1U);
	expect_state(ca, J1939_ADDR_STATE_REQUESTING, J1939_ADDR_NULL);
	process(1U);
	tx_expect_claim(OWN, NAME_OWN);
}

static void test_wait_ends_at_its_boundary(void) {
	j1939_ca_id_t ca = ca_add(OWN, NAME_OWN, true, false);

	process(0U);
	tx_expect_request(J1939_ADDR_NULL);
	/* The wait counts from the call after the Request. */
	process(WAIT - 1U);
	expect_state(ca, J1939_ADDR_STATE_REQUESTING, J1939_ADDR_NULL);
	tx_expect_empty();
	process(1U);
	tx_expect_claim(OWN, NAME_OWN);
	expect_state(ca, J1939_ADDR_STATE_CLAIMED, OWN);
}

static void test_free_preferred_address_is_claimed(void) {
	j1939_ca_id_t ca = ca_add(SELF, NAME_OWN | ARB, true, false);

	request_and_wait();
	tx_expect_claim(SELF, NAME_OWN | ARB);
	expect_state(ca, J1939_ADDR_STATE_CLAIMING, SELF);
}

static void test_taken_preferred_address_moves_capable_ca_without_contention(void) {
	j1939_ca_id_t ca = ca_add(OWN, NAME_WIN | ARB, true, false);

	process(0U);
	tx_expect_request(J1939_ADDR_NULL);
	/* The answers: OWN and the first self-configurable address are in use. NAME_WIN would
	 * win OWN, but the CA does not contend for it. */
	rx_claim(OWN, NAME_LOSE);
	rx_claim(SELF, NAME_OTHER2);
	process(WAIT);
	tx_expect_claim(SELF + 1U, NAME_WIN | ARB);
	tx_expect_empty();
	expect_state(ca, J1939_ADDR_STATE_CLAIMING, SELF + 1U);
}

static void test_taken_preferred_address_is_contended_by_fixed_ca(void) {
	j1939_ca_id_t ca = ca_add(OWN, NAME_WIN, true, false);

	process(0U);
	tx_expect_request(J1939_ADDR_NULL);
	rx_claim(OWN, NAME_LOSE);
	process(WAIT);
	tx_expect_claim(OWN, NAME_WIN);
	expect_state(ca, J1939_ADDR_STATE_CLAIMED, OWN);
	/* The other node defends; the lower NAME of the CA wins and it claims again. */
	rx_claim(OWN, NAME_LOSE);
	tx_expect_claim(OWN, NAME_WIN);
	expect_state(ca, J1939_ADDR_STATE_CLAIMED, OWN);
}

static void test_capable_ca_without_free_address_claims_preferred(void) {
	j1939_ca_id_t ca = ca_add(OWN, NAME_WIN | ARB, true, false);
	uint32_t a;

	process(0U);
	tx_expect_request(J1939_ADDR_NULL);
	rx_claim(OWN, NAME_LOSE);
	for (a = J1939_ADDR_SELF_CFG_MIN; a <= J1939_ADDR_SELF_CFG_MAX; a++) {
		rx_claim((uint8_t)a, NAME_OTHER2 + a);
	}
	process(WAIT);
	tx_expect_claim(OWN, NAME_WIN | ARB);
	expect_state(ca, J1939_ADDR_STATE_CLAIMED, OWN);
}

static void test_own_name_does_not_mark_preferred_address_taken(void) {
	j1939_ca_id_t ca = ca_add(OWN, NAME_OWN | ARB, true, false);

	process(0U);
	tx_expect_request(J1939_ADDR_NULL);
	/* A driver that loops back frames, or a node with a copied NAME. */
	rx_claim(OWN, NAME_OWN | ARB);
	process(WAIT);
	tx_expect_claim(OWN, NAME_OWN | ARB);
	expect_state(ca, J1939_ADDR_STATE_CLAIMED, OWN);
}

static void test_no_answer_while_requesting(void) {
	j1939_ca_id_t ca = ca_add(OWN, NAME_OWN, true, false);

	process(0U);
	tx_expect_request(J1939_ADDR_NULL);
	rx_request_claims(J1939_ADDR_GLOBAL);
	rx_request_claims(OWN);
	process(WAIT - 1U);
	tx_expect_empty();
	expect_state(ca, J1939_ADDR_STATE_REQUESTING, J1939_ADDR_NULL);
}

static void test_other_ca_answers_the_request(void) {
	j1939_ca_id_t a = ca_add(OWN, NAME_OWN, false, false);
	j1939_ca_id_t b = ca_add(OWN_B, NAME_OWN_B, true, false);

	/* a claims at once; b's Request is answered by a, as by every node. */
	process(0U);
	tx_expect_claim(OWN, NAME_OWN);
	tx_expect_request(J1939_ADDR_NULL);
	tx_expect_claim(OWN, NAME_OWN);
	tx_expect_empty();
	expect_state(a, J1939_ADDR_STATE_CLAIMED, OWN);
	expect_state(b, J1939_ADDR_STATE_REQUESTING, J1939_ADDR_NULL);
}

static void test_command_during_wait_is_claimed_when_wait_ends(void) {
	j1939_ca_id_t ca = ca_add(OWN, NAME_OWN, true, true);

	process(0U);
	tx_expect_request(J1939_ADDR_NULL);
	rx_command(NAME_OWN, NEW);
	process(WAIT - 1U);
	expect_state(ca, J1939_ADDR_STATE_REQUESTING, J1939_ADDR_NULL);
	tx_expect_empty();
	process(1U);
	tx_expect_claim(NEW, NAME_OWN);
	expect_state(ca, J1939_ADDR_STATE_CLAIMED, NEW);
}

static void test_startup_request_skipped_after_preclaim(void) {
	j1939_ca_id_t ca;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_names_init(&s, entries, TABLE_LEN));
	ca = ca_add(OWN, NAME_OWN, true, false);
	process(0U);
	tx_expect_request(J1939_ADDR_NULL);
	rx_claim(OTHER, NAME_LOSE);
	process(WAIT);
	tx_expect_claim(OWN, NAME_OWN);
	expect_state(ca, J1939_ADDR_STATE_CLAIMED, OWN);
	process(0U);
	process(WAIT);
	tx_expect_empty();
	TEST_ASSERT_EQUAL_UINT16(1U, j1939_names_count(&s));
}

static void test_startup_request_skipped_with_preclaim_of_another_ca(void) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_names_init(&s, entries, TABLE_LEN));
	(void)ca_add(OWN, NAME_OWN, false, false);
	(void)ca_add(OWN_B, NAME_OWN_B, true, false);
	process(0U);
	tx_expect_claim(OWN, NAME_OWN);
	tx_expect_request(J1939_ADDR_NULL);
	tx_expect_claim(OWN, NAME_OWN);
	tx_expect_empty();
	process(WAIT);
	tx_expect_claim(OWN_B, NAME_OWN_B);
	process(0U);
	tx_expect_empty();
}

static void test_startup_request_sent_for_normal_ca(void) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_names_init(&s, entries, TABLE_LEN));
	(void)ca_add(OWN, NAME_OWN, false, false);
	process(0U);
	tx_expect_claim(OWN, NAME_OWN);
	tx_expect_request(OWN);
	/* The stack answers its own Request too. */
	tx_expect_claim(OWN, NAME_OWN);
	tx_expect_empty();
}

static void test_startup_request_skipped_after_application_request(void) {
	j1939_ca_id_t ca;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_names_init(&s, entries, TABLE_LEN));
	ca = ca_add(OWN, NAME_OWN, false, false);
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_request_send(&s, ca, J1939_PGN_ADDRESS_CLAIMED, J1939_ADDR_GLOBAL));
	tx_expect_request(J1939_ADDR_NULL);
	process(0U);
	tx_expect_claim(OWN, NAME_OWN);
	tx_expect_empty();
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_ca_starts_requesting_without_address);
	RUN_TEST(test_request_goes_out_from_null_first);
	RUN_TEST(test_request_is_retried_while_tx_queue_full);
	RUN_TEST(test_wait_ends_at_its_boundary);
	RUN_TEST(test_free_preferred_address_is_claimed);
	RUN_TEST(test_taken_preferred_address_moves_capable_ca_without_contention);
	RUN_TEST(test_taken_preferred_address_is_contended_by_fixed_ca);
	RUN_TEST(test_capable_ca_without_free_address_claims_preferred);
	RUN_TEST(test_own_name_does_not_mark_preferred_address_taken);
	RUN_TEST(test_no_answer_while_requesting);
	RUN_TEST(test_other_ca_answers_the_request);
	RUN_TEST(test_command_during_wait_is_claimed_when_wait_ends);
	RUN_TEST(test_startup_request_skipped_after_preclaim);
	RUN_TEST(test_startup_request_skipped_with_preclaim_of_another_ca);
	RUN_TEST(test_startup_request_sent_for_normal_ca);
	RUN_TEST(test_startup_request_skipped_after_application_request);
	return UNITY_END();
}
