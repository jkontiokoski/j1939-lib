/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/* NAME table: update rules, change counter, startup and directed Requests for Address Claimed. */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "j1939/j1939.h"

#define TX_LEN    8U
#define MSG_LEN   2U
#define TABLE_LEN 3U

#define OWN       0x10U
#define OWN_SELF  0x80U /* self-configurable: 250 ms contention wait */
#define PEER      0x42U
#define OTHER     0x43U
#define THIRD     0x44U
#define NAME_OWN  0x1000U
#define NAME_A    0x5000U
#define NAME_B    0x6000U
#define NAME_C    0x7000U
#define NAME_D    0x8000U
#define NAME_HIGH 0x2000U  /* loses OWN to NAME_OWN */
#define NAME_LOW  0x0800U  /* wins OWN from NAME_OWN */
#define HOLD      1000000U /* J1939_NAMES_REQUEST_HOLD_US */

static j1939_port_frame_t tx_buf[TX_LEN];
static j1939_msg_slot_t msg_buf[MSG_LEN];
static j1939_names_entry_t entries[TABLE_LEN];
static j1939_t s;
static j1939_ca_id_t ca;

static void stack_init(bool with_ca, uint8_t address) {
	const j1939_cfg_t cfg = {
	        .tx_buf = tx_buf,
	        .tx_len = TX_LEN,
	        .msg_buf = msg_buf,
	        .msg_len = MSG_LEN,
	};

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&s, &cfg));
	if (with_ca) {
		TEST_ASSERT_EQUAL(
		        J1939_RET_OK,
		        j1939_ca_add(&s, &(j1939_ca_cfg_t){.address = address, .name = NAME_OWN},
		                     &ca));
	}
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_names_init(&s, entries, TABLE_LEN));
}

static void process(uint32_t us) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(&s, us));
}

static void rx_claim(uint8_t sa, uint64_t name) {
	uint8_t data[J1939_NAME_LEN];
	uint32_t id = 0U;
	j1939_port_frame_t f;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_name_to_bytes(name, data));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_id_build(6U, J1939_PGN_ADDRESS_CLAIMED,
	                                               J1939_ADDR_GLOBAL, sa, &id));
	j1939_port_frame_build(&f, id, data, (uint8_t)J1939_NAME_LEN);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_rx(&s, &f));
}

static uint32_t peek_id(void) {
	const j1939_port_frame_t *f = j1939_tx_peek(&s);

	TEST_ASSERT_NOT_NULL(f);
	return j1939_port_frame_id_get(f);
}

static void expect_claim(uint8_t sa) {
	uint32_t id = peek_id();

	TEST_ASSERT_EQUAL_HEX32(J1939_PGN_ADDRESS_CLAIMED, j1939_id_pgn_get(id));
	TEST_ASSERT_EQUAL_HEX8(sa, j1939_id_sa_get(id));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_tx_pop(&s));
}

static void expect_request(uint8_t da, uint8_t sa) {
	const uint8_t want[3] = {0x00U, 0xEEU, 0x00U};
	uint32_t id = peek_id();
	const j1939_port_frame_t *f = j1939_tx_peek(&s);

	TEST_ASSERT_EQUAL_HEX32(J1939_PGN_REQUEST, j1939_id_pgn_get(id));
	TEST_ASSERT_EQUAL_HEX8(da, j1939_id_da_get(id));
	TEST_ASSERT_EQUAL_HEX8(sa, j1939_id_sa_get(id));
	TEST_ASSERT_EQUAL(3U, j1939_port_frame_len_get(f));
	TEST_ASSERT_EQUAL_HEX8_ARRAY(want, j1939_port_frame_data(f), 3U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_tx_pop(&s));
}

static void expect_empty(void) {
	TEST_ASSERT_NULL(j1939_tx_peek(&s));
}

static void drain(void) {
	while (j1939_tx_pop(&s) == J1939_RET_OK) {
	}
}

/* First process: the CA claims OWN, the startup Request goes out, the CA answers it. */
static void start(void) {
	process(0U);
	expect_claim(OWN);
	expect_request(J1939_ADDR_GLOBAL, OWN);
	expect_claim(OWN);
	expect_empty();
}

static uint8_t address_of(uint64_t name) {
	uint8_t address = 0xA5U;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_names_address_get(&s, name, &address));
	return address;
}

static void fill_tx_queue(void) {
	const uint8_t d[8] = {0U};
	const j1939_msg_t msg = {0xFF00U, 6U, 0U, J1939_ADDR_GLOBAL, 8U, d};

	while (j1939_send(&s, ca, &msg) == J1939_RET_OK) {
	}
}

void setUp(void) {
	(void)memset(entries, 0xA5, sizeof(entries));
	stack_init(true, OWN);
}

void tearDown(void) {
}

static void test_entry_is_12_bytes(void) {
	TEST_ASSERT_EQUAL(12U, sizeof(j1939_names_entry_t));
}

static void test_init_checks_arguments(void) {
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_names_init(NULL, entries, TABLE_LEN));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_names_init(&s, NULL, 1U));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_names_init(&s, NULL, 0U));
}

static void test_without_table_nothing_is_recorded_or_sent(void) {
	uint64_t name = 0U;
	uint8_t address = 0U;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_names_init(&s, NULL, 0U));
	process(0U);
	expect_claim(OWN);
	expect_empty();
	rx_claim(PEER, NAME_A);
	TEST_ASSERT_EQUAL(0U, j1939_names_count(&s));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_STATE, j1939_names_name_get(&s, PEER, &name));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_STATE, j1939_names_address_get(&s, NAME_A, &address));
	process(HOLD);
	expect_empty();
}

static void test_api_checks_arguments(void) {
	uint64_t name = 0U;
	uint8_t address = 0U;

	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_names_name_get(NULL, PEER, &name));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_names_name_get(&s, PEER, NULL));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_names_name_get(&s, J1939_ADDR_NULL, &name));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_names_address_get(NULL, NAME_A, &address));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_names_address_get(&s, NAME_A, NULL));
	TEST_ASSERT_EQUAL(0U, j1939_names_count(NULL));
	TEST_ASSERT_EQUAL(0U, j1939_names_changes(NULL));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_names_at(NULL, 0U, &name, &address));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_names_at(&s, 0U, &name, &address));
	rx_claim(PEER, NAME_A);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_names_at(&s, 0U, NULL, &address));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_names_at(&s, 0U, &name, NULL));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_names_at(&s, 1U, &name, &address));
}

static void test_claim_is_recorded(void) {
	uint16_t changes = j1939_names_changes(&s);
	uint64_t name = 0U;

	rx_claim(PEER, NAME_A);
	TEST_ASSERT_EQUAL(1U, j1939_names_count(&s));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_names_name_get(&s, PEER, &name));
	TEST_ASSERT_EQUAL_UINT64(NAME_A, name);
	TEST_ASSERT_EQUAL_HEX8(PEER, address_of(NAME_A));
	TEST_ASSERT_EQUAL(changes + 1U, j1939_names_changes(&s));
}

static void test_repeated_claim_changes_nothing(void) {
	uint16_t changes;

	rx_claim(PEER, NAME_A);
	changes = j1939_names_changes(&s);
	rx_claim(PEER, NAME_A);
	TEST_ASSERT_EQUAL(1U, j1939_names_count(&s));
	TEST_ASSERT_EQUAL(changes, j1939_names_changes(&s));
}

static void test_claim_of_another_address_moves_the_name(void) {
	uint64_t name = 0U;

	rx_claim(PEER, NAME_A);
	rx_claim(OTHER, NAME_A);
	TEST_ASSERT_EQUAL(1U, j1939_names_count(&s));
	TEST_ASSERT_EQUAL_HEX8(OTHER, address_of(NAME_A));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_EMPTY, j1939_names_name_get(&s, PEER, &name));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_names_name_get(&s, OTHER, &name));
	TEST_ASSERT_EQUAL_UINT64(NAME_A, name);
}

static void test_claim_takes_the_address_of_another_name(void) {
	uint16_t changes;
	uint64_t name = 0U;

	rx_claim(PEER, NAME_A);
	changes = j1939_names_changes(&s);
	rx_claim(PEER, NAME_B);
	TEST_ASSERT_EQUAL(2U, j1939_names_count(&s));
	TEST_ASSERT_EQUAL_HEX8(J1939_ADDR_NULL, address_of(NAME_A));
	TEST_ASSERT_EQUAL_HEX8(PEER, address_of(NAME_B));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_names_name_get(&s, PEER, &name));
	TEST_ASSERT_EQUAL_UINT64(NAME_B, name);
	TEST_ASSERT_EQUAL(changes + 1U, j1939_names_changes(&s));
}

static void test_cannot_claim_lists_the_node_without_address(void) {
	uint64_t name = 0U;

	rx_claim(J1939_ADDR_NULL, NAME_C);
	TEST_ASSERT_EQUAL(1U, j1939_names_count(&s));
	TEST_ASSERT_EQUAL_HEX8(J1939_ADDR_NULL, address_of(NAME_C));
	rx_claim(PEER, NAME_A);
	rx_claim(J1939_ADDR_NULL, NAME_A);
	TEST_ASSERT_EQUAL(2U, j1939_names_count(&s));
	TEST_ASSERT_EQUAL_HEX8(J1939_ADDR_NULL, address_of(NAME_A));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_EMPTY, j1939_names_name_get(&s, PEER, &name));
}

static void test_short_claim_is_ignored(void) {
	uint8_t data[J1939_NAME_LEN] = {0U};
	uint32_t id = 0U;
	j1939_port_frame_t f;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_id_build(6U, J1939_PGN_ADDRESS_CLAIMED,
	                                               J1939_ADDR_GLOBAL, PEER, &id));
	j1939_port_frame_build(&f, id, data, 7U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_rx(&s, &f));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_id_build(6U, J1939_PGN_ADDRESS_CLAIMED,
	                                               J1939_ADDR_GLOBAL, J1939_ADDR_NULL, &id));
	j1939_port_frame_build(&f, id, data, 7U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_rx(&s, &f));
	TEST_ASSERT_EQUAL(0U, j1939_names_count(&s));
}

static void test_own_name_is_not_recorded(void) {
	start();
	rx_claim(OWN, NAME_OWN);             /* own claim looped back */
	rx_claim(J1939_ADDR_NULL, NAME_OWN); /* own Cannot Claim looped back */
	rx_claim(OTHER, NAME_OWN);
	TEST_ASSERT_EQUAL(0U, j1939_names_count(&s));
}

static void test_claim_of_own_address_won_by_the_stack_is_not_recorded(void) {
	uint64_t name = 0U;

	start();
	rx_claim(OWN, NAME_HIGH);
	expect_claim(OWN); /* the CA defends its address */
	TEST_ASSERT_EQUAL(0U, j1939_names_count(&s));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_EMPTY, j1939_names_name_get(&s, OWN, &name));
}

static void test_claim_of_own_address_lost_by_the_stack_is_recorded(void) {
	start();
	rx_claim(OWN, NAME_LOW);
	TEST_ASSERT_EQUAL(1U, j1939_names_count(&s));
	TEST_ASSERT_EQUAL_HEX8(OWN, address_of(NAME_LOW));
}

static void test_new_name_in_full_table_is_counted(void) {
	uint16_t changes;

	rx_claim(PEER, NAME_A);
	rx_claim(OTHER, NAME_B);
	rx_claim(THIRD, NAME_C);
	changes = j1939_names_changes(&s);
	rx_claim(0x45U, NAME_D);
	TEST_ASSERT_EQUAL(TABLE_LEN, j1939_names_count(&s));
	TEST_ASSERT_EQUAL(1U, j1939_stats_get(&s)->names_dropped);
	TEST_ASSERT_EQUAL(changes, j1939_names_changes(&s));
	rx_claim(J1939_ADDR_NULL, NAME_D);
	TEST_ASSERT_EQUAL(2U, j1939_stats_get(&s)->names_dropped);
	/* Listed NAMEs are still updated. */
	rx_claim(0x45U, NAME_A);
	TEST_ASSERT_EQUAL_HEX8(0x45U, address_of(NAME_A));
}

static void test_change_counter_wraps(void) {
	uint16_t start_count = j1939_names_changes(&s);
	uint32_t i;

	for (i = 0U; i < 65536U; i++) {
		rx_claim(((i % 2U) == 0U) ? PEER : OTHER, NAME_A);
	}
	TEST_ASSERT_EQUAL(start_count, j1939_names_changes(&s));
}

static void test_at_lists_entries_in_arrival_order(void) {
	uint64_t name = 0U;
	uint8_t address = 0U;

	rx_claim(PEER, NAME_B);
	rx_claim(J1939_ADDR_NULL, NAME_A);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_names_at(&s, 0U, &name, &address));
	TEST_ASSERT_EQUAL_UINT64(NAME_B, name);
	TEST_ASSERT_EQUAL_HEX8(PEER, address);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_names_at(&s, 1U, &name, &address));
	TEST_ASSERT_EQUAL_UINT64(NAME_A, name);
	TEST_ASSERT_EQUAL_HEX8(J1939_ADDR_NULL, address);
}

static void test_address_get_of_unknown_name_is_empty(void) {
	uint8_t address = 0xA5U;

	TEST_ASSERT_EQUAL(J1939_RET_ERR_EMPTY, j1939_names_address_get(&s, NAME_A, &address));
	TEST_ASSERT_EQUAL_HEX8(0xA5U, address);
}

static void test_init_again_empties_the_table_and_requests_again(void) {
	uint16_t changes;

	start();
	rx_claim(PEER, NAME_A);
	changes = j1939_names_changes(&s);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_names_init(&s, entries, TABLE_LEN));
	TEST_ASSERT_EQUAL(0U, j1939_names_count(&s));
	TEST_ASSERT_EQUAL(changes + 1U, j1939_names_changes(&s));
	process(0U);
	expect_request(J1939_ADDR_GLOBAL, OWN);
	expect_claim(OWN);
	expect_empty();
}

static void test_stack_init_removes_the_table(void) {
	const j1939_cfg_t cfg = {
	        .tx_buf = tx_buf, .tx_len = TX_LEN, .msg_buf = msg_buf, .msg_len = MSG_LEN};
	uint64_t name = 0U;

	rx_claim(PEER, NAME_A);
	rx_claim(OTHER, NAME_B);
	rx_claim(THIRD, NAME_C);
	rx_claim(0x45U, NAME_D);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&s, &cfg));
	TEST_ASSERT_EQUAL(0U, j1939_names_count(&s));
	TEST_ASSERT_EQUAL(0U, j1939_stats_get(&s)->names_dropped);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_STATE, j1939_names_name_get(&s, PEER, &name));
}

static void test_startup_request_is_sent_once(void) {
	start();
	process(HOLD);
	process(HOLD);
	expect_empty();
}

static void test_startup_request_waits_for_the_claim(void) {
	stack_init(true, OWN_SELF);
	process(0U);
	expect_claim(OWN_SELF);
	expect_empty();
	process(J1939_ADDR_CLAIM_WAIT_US - 1U);
	expect_empty();
	process(1U);
	expect_request(J1939_ADDR_GLOBAL, OWN_SELF);
	expect_claim(OWN_SELF);
	expect_empty();
}

static void test_startup_request_is_retried_while_the_tx_queue_is_full(void) {
	start();
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_names_init(&s, entries, TABLE_LEN));
	fill_tx_queue();
	process(0U);
	drain();
	process(0U);
	expect_request(J1939_ADDR_GLOBAL, OWN);
	expect_claim(OWN);
	expect_empty();
}

static void test_startup_request_is_not_sent_without_a_claimed_ca(void) {
	stack_init(false, 0U);
	process(HOLD);
	expect_empty();
}

static void test_unknown_address_is_requested(void) {
	uint64_t name = 0x1234U;

	start();
	TEST_ASSERT_EQUAL(J1939_RET_ERR_EMPTY, j1939_names_name_get(&s, PEER, &name));
	TEST_ASSERT_EQUAL_UINT64(0x1234U, name);
	expect_empty();
	process(0U);
	expect_request(PEER, OWN);
	expect_empty();
	rx_claim(PEER, NAME_A);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_names_name_get(&s, PEER, &name));
	TEST_ASSERT_EQUAL_UINT64(NAME_A, name);
}

static void test_one_request_is_pending_at_a_time(void) {
	uint64_t name = 0U;

	start();
	TEST_ASSERT_EQUAL(J1939_RET_ERR_EMPTY, j1939_names_name_get(&s, PEER, &name));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_EMPTY, j1939_names_name_get(&s, OTHER, &name));
	process(0U);
	expect_request(PEER, OWN);
	expect_empty();
	process(HOLD);
	expect_empty();
}

static void test_requests_for_unknown_addresses_hold_one_second(void) {
	uint64_t name = 0U;

	start();
	TEST_ASSERT_EQUAL(J1939_RET_ERR_EMPTY, j1939_names_name_get(&s, PEER, &name));
	process(0U);
	expect_request(PEER, OWN);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_EMPTY, j1939_names_name_get(&s, OTHER, &name));
	process(HOLD - 1U);
	expect_empty();
	process(1U);
	expect_request(OTHER, OWN);
	expect_empty();
}

static void test_address_held_by_the_stack_is_not_requested(void) {
	uint64_t name = 0U;

	start();
	TEST_ASSERT_EQUAL(J1939_RET_ERR_EMPTY, j1939_names_name_get(&s, OWN, &name));
	process(HOLD);
	expect_empty();
}

static void test_request_without_claimed_ca_comes_from_the_null_address(void) {
	uint64_t name = 0U;

	stack_init(true, OWN_SELF);
	process(0U);
	expect_claim(OWN_SELF);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_EMPTY, j1939_names_name_get(&s, PEER, &name));
	process(0U);
	expect_request(PEER, J1939_ADDR_NULL);
	expect_empty();
}

static void test_request_of_a_stack_without_ca_comes_from_the_null_address(void) {
	uint64_t name = 0U;

	stack_init(false, 0U);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_EMPTY, j1939_names_name_get(&s, PEER, &name));
	process(0U);
	expect_request(PEER, J1939_ADDR_NULL);
	expect_empty();
}

static void test_request_for_unknown_address_is_retried_while_the_tx_queue_is_full(void) {
	uint64_t name = 0U;

	start();
	fill_tx_queue();
	TEST_ASSERT_EQUAL(J1939_RET_ERR_EMPTY, j1939_names_name_get(&s, PEER, &name));
	process(0U);
	drain();
	process(0U);
	expect_request(PEER, OWN);
	expect_empty();
}

static void test_entry_at_an_address_the_stack_holds_is_not_reported(void) {
	uint64_t name = 0U;

	rx_claim(OWN, NAME_A); /* before the CA claims OWN */
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_names_name_get(&s, OWN, &name));
	TEST_ASSERT_EQUAL_UINT64(NAME_A, name);
	start();
	TEST_ASSERT_EQUAL(J1939_RET_ERR_EMPTY, j1939_names_name_get(&s, OWN, &name));
	process(HOLD);
	expect_empty();
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_entry_is_12_bytes);
	RUN_TEST(test_init_checks_arguments);
	RUN_TEST(test_without_table_nothing_is_recorded_or_sent);
	RUN_TEST(test_api_checks_arguments);
	RUN_TEST(test_claim_is_recorded);
	RUN_TEST(test_repeated_claim_changes_nothing);
	RUN_TEST(test_claim_of_another_address_moves_the_name);
	RUN_TEST(test_claim_takes_the_address_of_another_name);
	RUN_TEST(test_cannot_claim_lists_the_node_without_address);
	RUN_TEST(test_short_claim_is_ignored);
	RUN_TEST(test_own_name_is_not_recorded);
	RUN_TEST(test_claim_of_own_address_won_by_the_stack_is_not_recorded);
	RUN_TEST(test_claim_of_own_address_lost_by_the_stack_is_recorded);
	RUN_TEST(test_new_name_in_full_table_is_counted);
	RUN_TEST(test_change_counter_wraps);
	RUN_TEST(test_at_lists_entries_in_arrival_order);
	RUN_TEST(test_address_get_of_unknown_name_is_empty);
	RUN_TEST(test_init_again_empties_the_table_and_requests_again);
	RUN_TEST(test_stack_init_removes_the_table);
	RUN_TEST(test_startup_request_is_sent_once);
	RUN_TEST(test_startup_request_waits_for_the_claim);
	RUN_TEST(test_startup_request_is_retried_while_the_tx_queue_is_full);
	RUN_TEST(test_startup_request_is_not_sent_without_a_claimed_ca);
	RUN_TEST(test_unknown_address_is_requested);
	RUN_TEST(test_one_request_is_pending_at_a_time);
	RUN_TEST(test_requests_for_unknown_addresses_hold_one_second);
	RUN_TEST(test_address_held_by_the_stack_is_not_requested);
	RUN_TEST(test_request_without_claimed_ca_comes_from_the_null_address);
	RUN_TEST(test_request_of_a_stack_without_ca_comes_from_the_null_address);
	RUN_TEST(test_request_for_unknown_address_is_retried_while_the_tx_queue_is_full);
	RUN_TEST(test_entry_at_an_address_the_stack_holds_is_not_reported);
	return UNITY_END();
}
