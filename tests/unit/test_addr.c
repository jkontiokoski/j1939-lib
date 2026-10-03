/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

#include "unity.h"

#include "j1939/j1939.h"

#define TX_LEN  4U
#define MSG_LEN 4U

#define OWN    0x20U
#define OWN_B  0x21U
#define OTHER  0x42U
#define PGN_DS 0xEF00U /* PDU1 proprietary A */

#define ARB       0x8000000000000000U
#define NAME_WIN  0x0000000000001000U /* beats NAME_OWN */
#define NAME_OWN  0x0000000000001500U /* folds to 0x15: Cannot Claim delay 12.6 ms */
#define NAME_LOSE 0x0000000000002000U /* loses to NAME_OWN */
#define DELAY_OWN (0x15U * 600U)

static const uint32_t rx_pgns[] = {PGN_DS, J1939_PGN_ADDRESS_CLAIMED};
static const uint32_t req_pgns[] = {J1939_PGN_ADDRESS_CLAIMED};
static j1939_port_frame_t tx_buf[TX_LEN];
static j1939_msg_slot_t msg_buf[MSG_LEN];
static j1939_t s;

static void init(uint16_t rx_pgns_len) {
	const j1939_cfg_t cfg = {
	        .tx_buf = tx_buf,
	        .tx_len = TX_LEN,
	        .msg_buf = msg_buf,
	        .msg_len = MSG_LEN,
	        .rx_pgns = rx_pgns,
	        .rx_pgns_len = rx_pgns_len,
	        .req_pgns = req_pgns,
	        .req_pgns_len = 1U,
	};

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&s, &cfg));
}

static j1939_ca_id_t ca_add(uint8_t address, uint64_t name) {
	j1939_ca_id_t id = 0xFFU;

	TEST_ASSERT_EQUAL(
	        J1939_RET_OK,
	        j1939_ca_add(&s, &(j1939_ca_cfg_t){.address = address, .name = name}, &id));
	return id;
}

static void rx(uint32_t id, const uint8_t *data, uint8_t len) {
	j1939_port_frame_t f;

	j1939_port_frame_build(&f, id, data, len);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_rx(&s, &f));
}

static uint32_t claim_id(uint8_t da, uint8_t sa) {
	uint32_t id = 0U;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_id_build(6U, J1939_PGN_ADDRESS_CLAIMED, da, sa, &id));
	return id;
}

static void rx_claim_len(uint8_t sa, uint64_t name, uint8_t len) {
	uint8_t data[J1939_NAME_LEN];

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_name_to_bytes(name, data));
	rx(claim_id(J1939_ADDR_GLOBAL, sa), data, len);
}

static void rx_claim(uint8_t sa, uint64_t name) {
	rx_claim_len(sa, name, J1939_NAME_LEN);
}

static void rx_request(uint8_t da) {
	const uint8_t data[3] = {0x00U, 0xEEU, 0x00U};
	uint32_t id = 0U;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_id_build(6U, J1939_PGN_REQUEST, da, OTHER, &id));
	rx(id, data, 3U);
}

/* Pops the next transmitted frame and checks it is Address Claimed from sa with name. */
static void tx_expect_claim(uint8_t sa, uint64_t name) {
	const j1939_port_frame_t *f = j1939_tx_peek(&s);
	uint8_t data[J1939_NAME_LEN];

	TEST_ASSERT_NOT_NULL_MESSAGE(f, "no Address Claimed queued");
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_name_to_bytes(name, data));
	TEST_ASSERT_EQUAL_HEX32(claim_id(J1939_ADDR_GLOBAL, sa), j1939_port_frame_id_get(f));
	TEST_ASSERT_EQUAL_UINT8(J1939_NAME_LEN, j1939_port_frame_len_get(f));
	TEST_ASSERT_EQUAL_HEX8_ARRAY(data, j1939_port_frame_data(f), J1939_NAME_LEN);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_tx_pop(&s));
}

static void tx_expect_empty(void) {
	TEST_ASSERT_EQUAL_UINT16(0U, s.tx.ring.count);
}

static void expect_state(j1939_ca_id_t ca, j1939_addr_state_t state, uint8_t address) {
	j1939_addr_state_t st = J1939_ADDR_STATE_UNCLAIMED;
	uint8_t a = 0U;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_addr_get(&s, ca, &a, &st));
	TEST_ASSERT_EQUAL(state, st);
	TEST_ASSERT_EQUAL_HEX8(address, a);
}

static j1939_ret_t send_bc(j1939_ca_id_t ca) {
	static const uint8_t payload[8] = {0};
	const j1939_msg_t msg = {.pgn = 0xFEF1U,
	                         .prio = 6U,
	                         .sa = 0U,
	                         .da = J1939_ADDR_GLOBAL,
	                         .len = 8U,
	                         .data = payload};

	return j1939_send(&s, ca, &msg);
}

static void process(uint32_t elapsed_us) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(&s, elapsed_us));
}

void setUp(void) {
	init(0U);
}

void tearDown(void) {
}

static void test_claim_is_sent_by_first_process(void) {
	j1939_ca_id_t ca = ca_add(OWN, NAME_OWN);

	expect_state(ca, J1939_ADDR_STATE_UNCLAIMED, J1939_ADDR_NULL);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_NO_ADDRESS, send_bc(ca));
	tx_expect_empty();

	process(0U);
	tx_expect_claim(OWN, NAME_OWN);
	expect_state(ca, J1939_ADDR_STATE_CLAIMED, OWN);
	TEST_ASSERT_EQUAL(J1939_RET_OK, send_bc(ca));

	process(1000000U);
	TEST_ASSERT_EQUAL_UINT16(1U, s.tx.ring.count); /* no new claim */
}

/* J1939/81: only a CA claiming a self-configurable address waits 250 ms. */
static void test_wait_applies_only_to_self_configurable_addresses(void) {
	static const uint8_t immediate[] = {0x00U, 0x7FU, 0xF8U, 0xFDU};
	static const uint8_t waiting[] = {0x80U, 0xF7U};
	uint32_t i;

	for (i = 0U; i < sizeof(immediate); i++) {
		init(0U);
		(void)ca_add(immediate[i], NAME_OWN);
		process(0U);
		tx_expect_claim(immediate[i], NAME_OWN);
		expect_state(0U, J1939_ADDR_STATE_CLAIMED, immediate[i]);
	}
	for (i = 0U; i < sizeof(waiting); i++) {
		init(0U);
		(void)ca_add(waiting[i], NAME_OWN);
		process(0U);
		tx_expect_claim(waiting[i], NAME_OWN);
		expect_state(0U, J1939_ADDR_STATE_CLAIMING, waiting[i]);
		TEST_ASSERT_EQUAL(J1939_RET_ERR_NO_ADDRESS, send_bc(0U));
		process(J1939_ADDR_CLAIM_WAIT_US - 1U);
		expect_state(0U, J1939_ADDR_STATE_CLAIMING, waiting[i]);
		process(1U);
		expect_state(0U, J1939_ADDR_STATE_CLAIMED, waiting[i]);
		TEST_ASSERT_EQUAL(J1939_RET_OK, send_bc(0U));
	}
}

static void test_claiming_ca_receives_its_frames(void) {
	init(1U);
	(void)ca_add(0x80U, NAME_OWN);
	process(0U);
	rx(0x18EF8000U | OTHER, NULL, 0U);
	process(0U);
	expect_state(0U, J1939_ADDR_STATE_CLAIMING, 0x80U);
	TEST_ASSERT_NOT_NULL(j1939_msg_peek(&s));
	TEST_ASSERT_EQUAL_HEX8(0x80U, j1939_msg_peek(&s)->da);
	(void)j1939_msg_pop(&s);
	TEST_ASSERT_NULL(j1939_msg_peek(&s));
}

/* During the claim wait the stack generates no traffic from the address but claims. */
static void test_nack_only_from_claimed_address(void) {
	const uint8_t req[3] = {0x20U, 0xFFU, 0x00U}; /* unsupported PGN 0xFF20 */
	uint32_t id = 0U;
	const j1939_port_frame_t *f;

	(void)ca_add(OWN, NAME_LOSE); /* claimed, at another address */
	(void)ca_add(0x80U, NAME_OWN);
	process(0U);
	tx_expect_claim(OWN, NAME_LOSE);
	tx_expect_claim(0x80U, NAME_OWN);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_id_build(6U, J1939_PGN_REQUEST, 0x80U, OTHER, &id));
	rx(id, req, 3U);
	process(0U);
	expect_state(1U, J1939_ADDR_STATE_CLAIMING, 0x80U);
	tx_expect_empty();

	process(J1939_ADDR_CLAIM_WAIT_US);
	rx(id, req, 3U);
	process(0U);
	f = j1939_tx_peek(&s);
	TEST_ASSERT_NOT_NULL(f);
	TEST_ASSERT_EQUAL_HEX32(0x18E8FF80U, j1939_port_frame_id_get(f));
	TEST_ASSERT_EQUAL_HEX8(J1939_ACK_CTRL_NACK, j1939_port_frame_data(f)[0]);
	TEST_ASSERT_EQUAL_HEX8(OTHER, j1939_port_frame_data(f)[4]);
}

static void test_contention_won_repeats_claim(void) {
	j1939_ca_id_t ca = ca_add(0x80U, NAME_OWN);

	process(0U);
	tx_expect_claim(0x80U, NAME_OWN);
	rx_claim(0x80U, NAME_LOSE);
	/* A claim sent to a single node still claims the address. */
	rx(claim_id(OTHER, 0x80U), (const uint8_t[8]){0x00U, 0x30U, 0U, 0U, 0U, 0U, 0U, 0U}, 8U);
	process(0U);
	tx_expect_claim(0x80U, NAME_OWN);
	tx_expect_claim(0x80U, NAME_OWN);
	tx_expect_empty();
	/* The wait keeps running; the address is not recorded as taken by the loser. */
	expect_state(ca, J1939_ADDR_STATE_CLAIMING, 0x80U);
	process(J1939_ADDR_CLAIM_WAIT_US);
	expect_state(ca, J1939_ADDR_STATE_CLAIMED, 0x80U);
	TEST_ASSERT_EACH_EQUAL_HEX8(0U, s.addr_taken, J1939_ADDR_TAKEN_LEN);
}

static void test_contention_lost_sends_cannot_claim_after_delay(void) {
	j1939_ca_id_t ca;

	init(1U);
	ca = ca_add(OWN, NAME_OWN);
	process(0U);
	tx_expect_claim(OWN, NAME_OWN);

	rx_claim(OWN, NAME_WIN);
	process(0U);
	expect_state(ca, J1939_ADDR_STATE_CANNOT_CLAIM, J1939_ADDR_NULL);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_NO_ADDRESS, send_bc(ca));
	tx_expect_empty();

	process(DELAY_OWN - 1U);
	tx_expect_empty();
	process(1U);
	tx_expect_claim(J1939_ADDR_NULL, NAME_OWN);
	process(1000000U);
	tx_expect_empty();

	/* Frames to the lost address are no longer received. */
	rx(0x18EF0000U | ((uint32_t)OWN << 8) | OTHER, NULL, 0U);
	process(0U);
	TEST_ASSERT_NULL(j1939_msg_peek(&s));
	expect_state(ca, J1939_ADDR_STATE_CANNOT_CLAIM, J1939_ADDR_NULL);
}

static void test_cannot_claim_delay_is_at_most_153_ms(void) {
	j1939_ca_id_t ca = ca_add(OWN, 0xFFU); /* folds to 0xFF */

	process(0U);
	tx_expect_claim(OWN, 0xFFU);
	rx_claim(OWN, 0x01U);
	process(0U);
	process(152999U);
	tx_expect_empty();
	process(1U);
	tx_expect_claim(J1939_ADDR_NULL, 0xFFU);
	expect_state(ca, J1939_ADDR_STATE_CANNOT_CLAIM, J1939_ADDR_NULL);
}

static void test_arbitrary_ca_moves_to_free_address(void) {
	j1939_ca_id_t ca = ca_add(0x80U, ARB | NAME_OWN);

	process(0U);
	tx_expect_claim(0x80U, ARB | NAME_OWN);
	rx_claim(0x81U, NAME_LOSE); /* another node holds 0x81 */
	rx_claim(0x80U, NAME_WIN);
	process(100000U);
	tx_expect_claim(0x82U, ARB | NAME_OWN);
	expect_state(ca, J1939_ADDR_STATE_CLAIMING, 0x82U);

	/* The wait restarts for the new address. */
	process(J1939_ADDR_CLAIM_WAIT_US - 1U);
	expect_state(ca, J1939_ADDR_STATE_CLAIMING, 0x82U);
	process(1U);
	expect_state(ca, J1939_ADDR_STATE_CLAIMED, 0x82U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, send_bc(ca));
}

static void test_arbitrary_ca_skips_addresses_of_its_own_stack(void) {
	j1939_ca_id_t a = ca_add(OWN, ARB | NAME_OWN);
	j1939_ca_id_t b = ca_add(0x80U, NAME_LOSE);
	j1939_ca_id_t c = 0xAAU;

	process(0U);
	tx_expect_claim(OWN, ARB | NAME_OWN);
	tx_expect_claim(0x80U, NAME_LOSE);
	rx_claim(OWN, NAME_WIN);
	process(0U);
	tx_expect_claim(0x81U, ARB | NAME_OWN);
	expect_state(a, J1939_ADDR_STATE_CLAIMING, 0x81U);
	expect_state(b, J1939_ADDR_STATE_CLAIMING, 0x80U);

	/* The new address is in use by the stack. */
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG,
	                  j1939_ca_add(&s, &(j1939_ca_cfg_t){.address = 0x81U}, &c));
	TEST_ASSERT_EQUAL_HEX8(0xAAU, c);
}

static void test_arbitrary_ca_without_free_address_cannot_claim(void) {
	j1939_ca_id_t ca = ca_add(OWN, ARB | NAME_OWN);
	uint32_t a;

	process(0U);
	tx_expect_claim(OWN, ARB | NAME_OWN);
	for (a = J1939_ADDR_SELF_CFG_MIN; a <= J1939_ADDR_SELF_CFG_MAX; a++) {
		rx_claim((uint8_t)a, NAME_LOSE);
	}
	rx_claim(OWN, NAME_WIN);
	process(0U);
	expect_state(ca, J1939_ADDR_STATE_CANNOT_CLAIM, J1939_ADDR_NULL);
	process(0x95U * 600U); /* ARB | NAME_OWN folds to 0x95 */
	tx_expect_claim(J1939_ADDR_NULL, ARB | NAME_OWN);
}

static void test_own_name_and_malformed_claims_are_ignored(void) {
	j1939_ca_id_t ca = ca_add(0x80U, NAME_OWN);

	process(0U);
	tx_expect_claim(0x80U, NAME_OWN);
	rx_claim(0x80U, NAME_OWN);             /* own claim looped back */
	rx_claim_len(0x80U, NAME_WIN, 7U);     /* too short */
	rx_claim(J1939_ADDR_NULL, NAME_WIN);   /* Cannot Claim takes no address */
	rx_claim(J1939_ADDR_GLOBAL, NAME_WIN); /* invalid source */
	rx(claim_id(J1939_ADDR_GLOBAL, 0x80U) | (1U << 25), NULL, 0U); /* EDP set */
	process(0U);
	tx_expect_empty();
	expect_state(ca, J1939_ADDR_STATE_CLAIMING, 0x80U);
	TEST_ASSERT_EACH_EQUAL_HEX8(0U, s.addr_taken, J1939_ADDR_TAKEN_LEN);

	rx_claim(0x81U, NAME_WIN);
	rx_claim(0xF7U, NAME_WIN);
	rx_claim(0x7FU, NAME_WIN);
	process(0U);
	TEST_ASSERT_EQUAL_HEX8(0x02U, s.addr_taken[0]);
	TEST_ASSERT_EQUAL_HEX8(0x80U, s.addr_taken[J1939_ADDR_TAKEN_LEN - 1U]);
}

static void test_claims_are_delivered_when_listed(void) {
	const j1939_msg_t *msg;

	init(2U);
	(void)ca_add(OWN, NAME_OWN);
	process(0U);
	tx_expect_claim(OWN, NAME_OWN);
	rx_claim(0x33U, NAME_WIN);
	rx_claim(J1939_ADDR_NULL, NAME_LOSE);
	rx_request(J1939_ADDR_GLOBAL); /* not delivered even though req_pgns lists it */
	process(0U);

	msg = j1939_msg_peek(&s);
	TEST_ASSERT_NOT_NULL(msg);
	TEST_ASSERT_EQUAL_HEX32(J1939_PGN_ADDRESS_CLAIMED, msg->pgn);
	TEST_ASSERT_EQUAL_HEX8(0x33U, msg->sa);
	TEST_ASSERT_EQUAL_UINT16(8U, msg->len);
	TEST_ASSERT_EQUAL_HEX8(0x10U, msg->data[1]);
	(void)j1939_msg_pop(&s);
	msg = j1939_msg_peek(&s);
	TEST_ASSERT_NOT_NULL(msg);
	TEST_ASSERT_EQUAL_HEX8(J1939_ADDR_NULL, msg->sa);
	(void)j1939_msg_pop(&s);
	TEST_ASSERT_NULL(j1939_msg_peek(&s));
	tx_expect_claim(OWN, NAME_OWN);
}

static void test_request_for_address_claimed_is_answered_per_ca(void) {
	j1939_ca_id_t a = ca_add(OWN, NAME_OWN);
	j1939_ca_id_t b = ca_add(0x80U, NAME_LOSE);

	process(0U);
	tx_expect_claim(OWN, NAME_OWN);
	tx_expect_claim(0x80U, NAME_LOSE);

	rx_request(J1939_ADDR_GLOBAL);
	process(0U);
	tx_expect_claim(OWN, NAME_OWN);
	tx_expect_claim(0x80U, NAME_LOSE); /* also while claiming */
	tx_expect_empty();

	rx_request(0x80U);
	rx_request(0x55U); /* another node */
	process(0U);
	tx_expect_claim(0x80U, NAME_LOSE);
	tx_expect_empty();
	TEST_ASSERT_NULL(j1939_msg_peek(&s));
	expect_state(a, J1939_ADDR_STATE_CLAIMED, OWN);
	expect_state(b, J1939_ADDR_STATE_CLAIMING, 0x80U);
}

static void test_request_is_answered_with_cannot_claim(void) {
	j1939_ca_id_t ca = ca_add(OWN, NAME_OWN);

	process(0U);
	tx_expect_claim(OWN, NAME_OWN);
	rx_claim(OWN, NAME_WIN);
	process(0U);
	process(DELAY_OWN);
	tx_expect_claim(J1939_ADDR_NULL, NAME_OWN);

	/* Two Requests within the delay: one answer. */
	rx_request(J1939_ADDR_GLOBAL);
	process(0U);
	rx_request(J1939_ADDR_GLOBAL);
	rx_request(OWN); /* the lost address */
	process(DELAY_OWN - 1U);
	tx_expect_empty();
	process(1U);
	tx_expect_claim(J1939_ADDR_NULL, NAME_OWN);
	process(1000000U);
	tx_expect_empty();
	expect_state(ca, J1939_ADDR_STATE_CANNOT_CLAIM, J1939_ADDR_NULL);
}

/* A delay started between two calls, by a frame or an API call, counts from the next call. */
static void test_delay_started_between_calls_counts_from_next_call(void) {
	j1939_ca_id_t ca = ca_add(OWN, NAME_OWN);

	process(0U);
	tx_expect_claim(OWN, NAME_OWN);
	rx_claim(OWN, NAME_WIN);
	process(DELAY_OWN); /* the time before the loss does not count */
	tx_expect_empty();
	process(DELAY_OWN - 1U);
	tx_expect_empty();
	process(1U);
	tx_expect_claim(J1939_ADDR_NULL, NAME_OWN);

	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_request_send(&s, ca, J1939_PGN_ADDRESS_CLAIMED, J1939_ADDR_GLOBAL));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_tx_pop(&s)); /* the Request itself */
	process(DELAY_OWN);
	tx_expect_empty();
	process(DELAY_OWN);
	tx_expect_claim(J1939_ADDR_NULL, NAME_OWN);
	expect_state(ca, J1939_ADDR_STATE_CANNOT_CLAIM, J1939_ADDR_NULL);
}

static void test_request_send_uses_null_address_until_claimed(void) {
	j1939_ca_id_t ca = ca_add(OWN, NAME_OWN);
	const j1939_port_frame_t *f;

	/* Only a Request for Address Claimed may be sent without an address. */
	TEST_ASSERT_EQUAL(J1939_RET_ERR_NO_ADDRESS, j1939_request_send(&s, ca, 0xFEF1U, OTHER));
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_request_send(&s, ca, J1939_PGN_ADDRESS_CLAIMED, J1939_ADDR_GLOBAL));
	f = j1939_tx_peek(&s);
	TEST_ASSERT_NOT_NULL(f);
	TEST_ASSERT_EQUAL_HEX32(0x18EAFFFEU, j1939_port_frame_id_get(f));
	TEST_ASSERT_EQUAL_HEX8(0xEEU, j1939_port_frame_data(f)[1]);
	(void)j1939_tx_pop(&s);
	tx_expect_empty(); /* the unclaimed CA answers with its claim in process */

	process(0U);
	tx_expect_claim(OWN, NAME_OWN);

	/* Once claimed: from the own address, and the requester answers too. */
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_request_send(&s, ca, J1939_PGN_ADDRESS_CLAIMED, J1939_ADDR_GLOBAL));
	f = j1939_tx_peek(&s);
	TEST_ASSERT_EQUAL_HEX32(0x18EAFF00U | OWN, j1939_port_frame_id_get(f));
	(void)j1939_tx_pop(&s);
	tx_expect_claim(OWN, NAME_OWN);

	/* A destination specific Request to another node is answered by that node only. */
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_request_send(&s, ca, J1939_PGN_ADDRESS_CLAIMED, OTHER));
	(void)j1939_tx_pop(&s);
	tx_expect_empty();

	/* After losing the address: from NULL again, answered with Cannot Claim. */
	rx_claim(OWN, NAME_WIN);
	process(DELAY_OWN);
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_request_send(&s, ca, J1939_PGN_ADDRESS_CLAIMED, J1939_ADDR_GLOBAL));
	f = j1939_tx_peek(&s);
	TEST_ASSERT_EQUAL_HEX32(0x18EAFFFEU, j1939_port_frame_id_get(f));
	(void)j1939_tx_pop(&s);
	process(DELAY_OWN);
	tx_expect_claim(J1939_ADDR_NULL, NAME_OWN);

	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG,
	                  j1939_request_send(&s, 1U, J1939_PGN_ADDRESS_CLAIMED, OTHER));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG,
	                  j1939_request_send(NULL, ca, J1939_PGN_ADDRESS_CLAIMED, OTHER));
}

static void test_full_tx_queue_delays_claims(void) {
	j1939_ca_id_t a = ca_add(OWN, NAME_OWN);
	j1939_ca_id_t b;
	uint32_t i;

	process(0U);
	for (i = 1U; i < TX_LEN; i++) {
		TEST_ASSERT_EQUAL(J1939_RET_OK, send_bc(a));
	}
	b = ca_add(0x80U, ARB | NAME_LOSE);
	rx_request(J1939_ADDR_GLOBAL); /* a's answer does not fit */
	process(0U);
	expect_state(b, J1939_ADDR_STATE_UNCLAIMED, J1939_ADDR_NULL);
	TEST_ASSERT_EQUAL_UINT32(1U, j1939_stats_get(&s)->tx_overflow);

	for (i = 0U; i < TX_LEN; i++) {
		(void)j1939_tx_pop(&s);
	}
	process(0U);
	tx_expect_claim(0x80U, ARB | NAME_LOSE);
	expect_state(b, J1939_ADDR_STATE_CLAIMING, 0x80U);

	/* b loses while the queue is full: the new claim waits for space. */
	for (i = 0U; i < TX_LEN; i++) {
		TEST_ASSERT_EQUAL(J1939_RET_OK, send_bc(a));
	}
	rx_claim(0x80U, NAME_WIN);
	rx_claim(OWN, NAME_LOSE); /* a wins, but its answer does not fit */
	process(0U);
	expect_state(b, J1939_ADDR_STATE_UNCLAIMED, J1939_ADDR_NULL);
	TEST_ASSERT_EQUAL_UINT32(2U, j1939_stats_get(&s)->tx_overflow);
	for (i = 0U; i < TX_LEN; i++) {
		(void)j1939_tx_pop(&s);
	}
	process(0U);
	tx_expect_claim(0x81U, ARB | NAME_LOSE);
	expect_state(b, J1939_ADDR_STATE_CLAIMING, 0x81U);
	tx_expect_empty();

	/* A Request for Address Claimed that does not fit is not answered locally either. */
	for (i = 0U; i < TX_LEN; i++) {
		TEST_ASSERT_EQUAL(J1939_RET_OK, send_bc(a));
	}
	TEST_ASSERT_EQUAL(J1939_RET_ERR_FULL,
	                  j1939_request_send(&s, b, J1939_PGN_ADDRESS_CLAIMED, J1939_ADDR_GLOBAL));
	TEST_ASSERT_EQUAL_UINT32(2U, j1939_stats_get(&s)->tx_overflow);
}

static void test_cannot_claim_waits_for_tx_space(void) {
	j1939_ca_id_t a = ca_add(OWN, NAME_OWN);
	j1939_ca_id_t b = ca_add(OWN_B, NAME_LOSE);
	uint32_t i;

	process(0U);
	tx_expect_claim(OWN, NAME_OWN);
	tx_expect_claim(OWN_B, NAME_LOSE);
	rx_claim(OWN, NAME_WIN);
	process(0U);
	for (i = 0U; i < TX_LEN; i++) {
		TEST_ASSERT_EQUAL(J1939_RET_OK, send_bc(b));
	}
	process(DELAY_OWN);
	expect_state(a, J1939_ADDR_STATE_CANNOT_CLAIM, J1939_ADDR_NULL);
	for (i = 0U; i < TX_LEN; i++) {
		(void)j1939_tx_pop(&s);
	}
	process(0U);
	tx_expect_claim(J1939_ADDR_NULL, NAME_OWN);
	tx_expect_empty();

	/* A Request to b's address concerns b only. */
	rx_request(OWN_B);
	process(1000000U);
	tx_expect_claim(OWN_B, NAME_LOSE);
	tx_expect_empty();
}

static void test_corrupted_state_stops_transmission(void) {
	j1939_ca_id_t ca = ca_add(OWN, NAME_OWN);

	process(0U);
	tx_expect_claim(OWN, NAME_OWN);
	s.ca[ca].state = (j1939_addr_state_t)42;
	process(0U);
	expect_state(ca, J1939_ADDR_STATE_CANNOT_CLAIM, J1939_ADDR_NULL);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_NO_ADDRESS, send_bc(ca));
	process(DELAY_OWN);
	tx_expect_claim(J1939_ADDR_NULL, NAME_OWN);

	/* The own answer to a Request runs outside process(). */
	s.ca[ca].state = (j1939_addr_state_t)42;
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_request_send(&s, ca, J1939_PGN_ADDRESS_CLAIMED, J1939_ADDR_GLOBAL));
	expect_state(ca, J1939_ADDR_STATE_CANNOT_CLAIM, J1939_ADDR_NULL);
}

static void test_addr_get_rejects_invalid_arguments(void) {
	j1939_addr_state_t st = J1939_ADDR_STATE_CLAIMED;
	uint8_t a = 0x12U;

	(void)ca_add(OWN, NAME_OWN);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_addr_get(NULL, 0U, &a, &st));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_addr_get(&s, 1U, &a, &st));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_addr_get(&s, 0U, NULL, &st));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_addr_get(&s, 0U, &a, NULL));
	TEST_ASSERT_EQUAL_HEX8(0x12U, a);
	TEST_ASSERT_EQUAL(J1939_ADDR_STATE_CLAIMED, st);
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_claim_is_sent_by_first_process);
	RUN_TEST(test_wait_applies_only_to_self_configurable_addresses);
	RUN_TEST(test_claiming_ca_receives_its_frames);
	RUN_TEST(test_nack_only_from_claimed_address);
	RUN_TEST(test_contention_won_repeats_claim);
	RUN_TEST(test_contention_lost_sends_cannot_claim_after_delay);
	RUN_TEST(test_cannot_claim_delay_is_at_most_153_ms);
	RUN_TEST(test_arbitrary_ca_moves_to_free_address);
	RUN_TEST(test_arbitrary_ca_skips_addresses_of_its_own_stack);
	RUN_TEST(test_arbitrary_ca_without_free_address_cannot_claim);
	RUN_TEST(test_own_name_and_malformed_claims_are_ignored);
	RUN_TEST(test_claims_are_delivered_when_listed);
	RUN_TEST(test_request_for_address_claimed_is_answered_per_ca);
	RUN_TEST(test_request_is_answered_with_cannot_claim);
	RUN_TEST(test_delay_started_between_calls_counts_from_next_call);
	RUN_TEST(test_request_send_uses_null_address_until_claimed);
	RUN_TEST(test_full_tx_queue_delays_claims);
	RUN_TEST(test_cannot_claim_waits_for_tx_space);
	RUN_TEST(test_corrupted_state_stops_transmission);
	RUN_TEST(test_addr_get_rejects_invalid_arguments);
	return UNITY_END();
}
