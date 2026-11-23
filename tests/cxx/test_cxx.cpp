/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/*
 * The public headers compiled as C++ against the C library. Each header is included on its own,
 * before the umbrella header, and one of its functions is called: a header whose declarations
 * lack C linkage fails to link.
 */

#include "unity.h"

#include "j1939/j1939_addr.h"
#include "j1939/j1939_dm.h"
#include "j1939/j1939_dtc.h"
#include "j1939/j1939_id.h"
#include "j1939/j1939_name.h"
#include "j1939/j1939_nametab.h"
#include "j1939/j1939_queue.h"
#include "j1939/j1939_request.h"
#include "j1939/j1939_rxobj.h"
#include "j1939/j1939_signal.h"
#include "j1939/j1939_stack.h"
#include "j1939/j1939_txobj.h"

/* Last, so that it cannot supply what a module header lacks. */
#include "j1939/j1939.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_codecs_round_trip(void) {
	uint32_t id = 0U;
	uint64_t name = 0U;
	uint8_t bytes[8] = {0U};

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_id_build(6U, 0xFF00U, J1939_ADDR_GLOBAL, 0x80U, &id));
	TEST_ASSERT_EQUAL_HEX32(0xFF00U, j1939_id_pgn_get(id));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_name_to_bytes(0x0123456789ABCDEFULL, bytes));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_name_from_bytes(bytes, &name));
	TEST_ASSERT_TRUE(name == 0x0123456789ABCDEFULL);
}

static void test_stack_functions_check_arguments(void) {
	uint32_t pgn = 0U;

	TEST_ASSERT_NULL(j1939_stats_get(NULL));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_addr_command_send(NULL, 0U, 0U, 0x80U, 0x00U));
	TEST_ASSERT_EQUAL_UINT16(0U, j1939_nametab_changes(NULL));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_request_pgn_get(NULL, &pgn));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_rxobj_get(NULL, 0U, NULL));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_txobj_set(NULL, 0U, NULL, 0U));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_dm_clear_confirm(NULL, 0U, 0U, true));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_dtc_dm_parse(NULL, 0U, NULL, NULL, 0U, NULL));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_check(NULL));
}

static void test_queue_and_version(void) {
	j1939_port_frame_t storage[2];
	j1939_queue_t q;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_queue_init(&q, storage, 2U));
	TEST_ASSERT_EQUAL_UINT16(0U, j1939_queue_count(&q));
	TEST_ASSERT_EQUAL_HEX32(J1939_VERSION, j1939_version_get());
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_codecs_round_trip);
	RUN_TEST(test_stack_functions_check_arguments);
	RUN_TEST(test_queue_and_version);
	return UNITY_END();
}
