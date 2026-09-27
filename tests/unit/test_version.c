/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

#include "unity.h"

#include "j1939/j1939.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_version_matches_header(void) {
	TEST_ASSERT_EQUAL_HEX32(J1939_VERSION, j1939_version_get());
}

static void test_version_packing(void) {
	TEST_ASSERT_EQUAL_UINT32(J1939_VERSION_MAJOR, (j1939_version_get() >> 16) & 0xFFU);
	TEST_ASSERT_EQUAL_UINT32(J1939_VERSION_MINOR, (j1939_version_get() >> 8) & 0xFFU);
	TEST_ASSERT_EQUAL_UINT32(J1939_VERSION_PATCH, j1939_version_get() & 0xFFU);
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_version_matches_header);
	RUN_TEST(test_version_packing);
	return UNITY_END();
}
