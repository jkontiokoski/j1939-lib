/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

#include <stddef.h>
#include <string.h>

#include "unity.h"

#include "example_signals.h"
#include "j1939/j1939_signal.h"

#define PAYLOAD_LEN 8U

void setUp(void) {
}

void tearDown(void) {
}

static const j1939_signal_t *sig(example_signal_id_t id) {
	return &example_signals[id];
}

static void test_table_is_valid_and_proprietary_b(void) {
	size_t i;
	size_t j;

	for (i = 0U; i < (size_t)EXAMPLE_SIGNAL_COUNT; i++) {
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_check(&example_signals[i]));
		TEST_ASSERT_TRUE((example_signals[i].pgn >= 0xFF00U) &&
		                 (example_signals[i].pgn <= 0xFFFFU));
		TEST_ASSERT_NOT_NULL(example_signals[i].name);
		TEST_ASSERT_TRUE(((uint32_t)example_signals[i].start + example_signals[i].bits) <=
		                 (PAYLOAD_LEN * 8U));
		/* Signals of one PGN do not overlap. */
		for (j = 0U; j < i; j++) {
			if (example_signals[j].pgn == example_signals[i].pgn) {
				TEST_ASSERT_TRUE(((uint32_t)example_signals[j].start +
				                  example_signals[j].bits) <=
				                 example_signals[i].start);
			}
		}
	}
}

/* Build a status message, then decode it as a receiver would. */
static void test_status_message(void) {
	uint8_t data[PAYLOAD_LEN];
	const uint8_t expect[PAYLOAD_LEN] = {0x40U, 0x1FU, 0x82U, 0x40U,
	                                     0x06U, 0x71U, 0x72U, 0xFDU};
	j1939_msg_t msg = {.pgn = EXAMPLE_PGN_PUMP_STATUS,
	                   .prio = 6U,
	                   .sa = 0x80U,
	                   .da = 0xFFU,
	                   .len = PAYLOAD_LEN};
	j1939_signal_class_t cls = J1939_SIGNAL_RESERVED;
	int64_t value = 0;

	/* Unused bits are sent as ones. */
	memset(data, 0xFF, sizeof(data));
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_signal_encode(sig(EXAMPLE_PUMP_SPEED), data, PAYLOAD_LEN, 1000));
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_signal_encode(sig(EXAMPLE_OIL_TEMP), data, PAYLOAD_LEN, 90));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_encode(sig(EXAMPLE_SUPPLY_PRESSURE), data,
	                                                    PAYLOAD_LEN, 12500));
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_signal_encode(sig(EXAMPLE_PUMP_ENABLED), data, PAYLOAD_LEN, 1));
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_signal_encode(sig(EXAMPLE_FILTER_CLOGGED), data, PAYLOAD_LEN, 0));
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_signal_encode(sig(EXAMPLE_PUMP_MODE), data, PAYLOAD_LEN, 7));
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_signal_encode(sig(EXAMPLE_VALVE_POSITION), data, PAYLOAD_LEN, 37));
	TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, data, PAYLOAD_LEN);

	msg.data = data;
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_signal_msg_decode(sig(EXAMPLE_PUMP_SPEED), &msg, &value, &cls));
	TEST_ASSERT_EQUAL(J1939_SIGNAL_VALID, cls);
	TEST_ASSERT_EQUAL_INT64(1000, value);
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_signal_msg_decode(sig(EXAMPLE_OIL_TEMP), &msg, &value, &cls));
	TEST_ASSERT_EQUAL_INT64(90, value);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_msg_decode(sig(EXAMPLE_SUPPLY_PRESSURE), &msg,
	                                                        &value, &cls));
	TEST_ASSERT_EQUAL_INT64(12500, value);
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_signal_msg_decode(sig(EXAMPLE_PUMP_ENABLED), &msg, &value, &cls));
	TEST_ASSERT_EQUAL_INT64(1, value);
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_signal_msg_decode(sig(EXAMPLE_FILTER_CLOGGED), &msg, &value, &cls));
	TEST_ASSERT_EQUAL_INT64(0, value);
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_signal_msg_decode(sig(EXAMPLE_PUMP_MODE), &msg, &value, &cls));
	TEST_ASSERT_EQUAL_INT64(7, value);
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_signal_msg_decode(sig(EXAMPLE_VALVE_POSITION), &msg, &value, &cls));
	TEST_ASSERT_EQUAL_INT64(37, value);

	/* A counters signal does not decode from a status message. */
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG,
	                  j1939_signal_msg_decode(sig(EXAMPLE_FAULT_COUNT), &msg, &value, &cls));

	/* The sender reports a failed temperature sensor. */
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_signal_indicator_set(sig(EXAMPLE_OIL_TEMP), data, PAYLOAD_LEN,
	                                             J1939_SIGNAL_ERROR));
	value = 0;
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_signal_msg_decode(sig(EXAMPLE_OIL_TEMP), &msg, &value, &cls));
	TEST_ASSERT_EQUAL(J1939_SIGNAL_ERROR, cls);
	TEST_ASSERT_EQUAL_INT64(0, value);
}

static void test_counters_message(void) {
	const uint8_t data[PAYLOAD_LEN] = {0x10U, 0x27U, 0x00U, 0x00U, 0x00U, 0x7DU, 0x03U, 0xFFU};
	const j1939_msg_t msg = {.pgn = EXAMPLE_PGN_PUMP_COUNTERS,
	                         .prio = 6U,
	                         .sa = 0x80U,
	                         .da = 0xFFU,
	                         .len = PAYLOAD_LEN,
	                         .data = data};
	j1939_signal_class_t cls = J1939_SIGNAL_RESERVED;
	int64_t value = 0;

	/* 10000 * 0.05 h = 500 h = 30000 min */
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_signal_msg_decode(sig(EXAMPLE_OPERATING_TIME), &msg, &value, &cls));
	TEST_ASSERT_EQUAL(J1939_SIGNAL_VALID, cls);
	TEST_ASSERT_EQUAL_INT64(30000, value);
	/* 0x7D00 = 32000: 250 deg - 250 deg */
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_signal_msg_decode(sig(EXAMPLE_TILT_ANGLE), &msg, &value, &cls));
	TEST_ASSERT_EQUAL_INT64(0, value);
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_signal_msg_decode(sig(EXAMPLE_FAULT_COUNT), &msg, &value, &cls));
	TEST_ASSERT_EQUAL_INT64(3, value);
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_table_is_valid_and_proprietary_b);
	RUN_TEST(test_status_message);
	RUN_TEST(test_counters_message);
	return UNITY_END();
}
