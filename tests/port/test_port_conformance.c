/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/* Port conformance tests. Built once per port against its j1939_target.h. */

#include "unity.h"

#include <string.h>

#include "j1939_port_fixture.h"

void setUp(void) {
}

void tearDown(void) {
}

static const uint32_t ids[] = {
        0x00000000U, 0x00000001U, 0x000000FFU, 0x0CF00400U, 0x18FEF100U,
        0x18EAFFFEU, 0x1CECFF00U, 0x15555555U, 0x0AAAAAAAU, 0x1FFFFFFFU,
};

static void test_build_sets_extended_data_frame(void) {
	j1939_port_frame_t f;
	size_t i;

	for (i = 0U; i < (sizeof(ids) / sizeof(ids[0])); i++) {
		j1939_port_frame_build(&f, ids[i], NULL, 0U);
		TEST_ASSERT_TRUE(j1939_port_frame_is_ext(&f));
		TEST_ASSERT_FALSE(j1939_port_frame_is_rtr(&f));
		TEST_ASSERT_EQUAL_HEX32(ids[i], j1939_port_frame_id_get(&f));
	}
}

static void test_build_ignores_bits_above_28(void) {
	j1939_port_frame_t f;

	j1939_port_frame_build(&f, 0xE0000000U | 0x18FEF100U, NULL, 0U);
	TEST_ASSERT_TRUE(j1939_port_frame_is_ext(&f));
	TEST_ASSERT_FALSE(j1939_port_frame_is_rtr(&f));
	TEST_ASSERT_EQUAL_HEX32(0x18FEF100U, j1939_port_frame_id_get(&f));
}

static void test_build_payload_round_trip(void) {
	const uint8_t payload[8] = {0x01U, 0x23U, 0x45U, 0x67U, 0x89U, 0xABU, 0xCDU, 0xEFU};
	j1939_port_frame_t f;
	uint8_t len;

	for (len = 0U; len <= 8U; len++) {
		j1939_port_frame_build(&f, 0x18FEF100U, payload, len);
		TEST_ASSERT_EQUAL_UINT8(len, j1939_port_frame_len_get(&f));
		if (len > 0U) {
			TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, j1939_port_frame_data(&f), len);
		}
	}
}

static void test_build_clamps_length(void) {
	const uint8_t payload[8] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U};
	j1939_port_frame_t f;

	j1939_port_frame_build(&f, 0x18FEF100U, payload, 200U);
	TEST_ASSERT_EQUAL_UINT8(8U, j1939_port_frame_len_get(&f));
	TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, j1939_port_frame_data(&f), 8U);
}

static void test_build_overwrites_previous_content(void) {
	j1939_port_frame_t f;

	(void)memset(&f, 0xFF, sizeof(f));
	j1939_port_frame_build(&f, 0x0CF00400U, NULL, 0U);
	TEST_ASSERT_TRUE(j1939_port_frame_is_ext(&f));
	TEST_ASSERT_FALSE(j1939_port_frame_is_rtr(&f));
	TEST_ASSERT_EQUAL_HEX32(0x0CF00400U, j1939_port_frame_id_get(&f));
	TEST_ASSERT_EQUAL_UINT8(0U, j1939_port_frame_len_get(&f));

	j1939_port_fixture_rtr(&f, 0x18EAFF00U);
	j1939_port_frame_build(&f, 0x18EAFF00U, NULL, 0U);
	TEST_ASSERT_FALSE(j1939_port_frame_is_rtr(&f));
}

static void test_standard_frame_is_not_extended(void) {
	j1939_port_frame_t f;

	j1939_port_fixture_std(&f, 0x7FFU);
	TEST_ASSERT_FALSE(j1939_port_frame_is_ext(&f));
	TEST_ASSERT_FALSE(j1939_port_frame_is_rtr(&f));
	TEST_ASSERT_EQUAL_HEX32(0x7FFU, j1939_port_frame_id_get(&f));
}

static void test_remote_frame_is_detected(void) {
	j1939_port_frame_t f;

	j1939_port_fixture_rtr(&f, 0x18EAFF00U);
	TEST_ASSERT_TRUE(j1939_port_frame_is_ext(&f));
	TEST_ASSERT_TRUE(j1939_port_frame_is_rtr(&f));
	TEST_ASSERT_EQUAL_HEX32(0x18EAFF00U, j1939_port_frame_id_get(&f));
}

static void test_raw_dlc_above_8_reads_as_8(void) {
	j1939_port_frame_t f;
	uint8_t dlc;

	for (dlc = 9U; dlc <= 15U; dlc++) {
		j1939_port_frame_build(&f, 0x18FEF100U, NULL, 0U);
		j1939_port_fixture_raw_dlc(&f, dlc);
		TEST_ASSERT_EQUAL_UINT8(8U, j1939_port_frame_len_get(&f));
	}
}

static void test_frame_copies_by_assignment(void) {
	const uint8_t payload[3] = {0xAAU, 0xBBU, 0xCCU};
	j1939_port_frame_t a;
	j1939_port_frame_t b;

	j1939_port_frame_build(&a, 0x1CECFF00U, payload, 3U);
	b = a;
	TEST_ASSERT_TRUE(j1939_port_frame_is_ext(&b));
	TEST_ASSERT_EQUAL_HEX32(0x1CECFF00U, j1939_port_frame_id_get(&b));
	TEST_ASSERT_EQUAL_UINT8(3U, j1939_port_frame_len_get(&b));
	TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, j1939_port_frame_data(&b), 3U);
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_build_sets_extended_data_frame);
	RUN_TEST(test_build_ignores_bits_above_28);
	RUN_TEST(test_build_payload_round_trip);
	RUN_TEST(test_build_clamps_length);
	RUN_TEST(test_build_overwrites_previous_content);
	RUN_TEST(test_standard_frame_is_not_extended);
	RUN_TEST(test_remote_frame_is_detected);
	RUN_TEST(test_raw_dlc_above_8_reads_as_8);
	RUN_TEST(test_frame_copies_by_assignment);
	return UNITY_END();
}
