/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/*
 * The SocketCAN port header compiled as C++ against the C library. It is included first, so that
 * it, not the umbrella header, brings in j1939_stack.h.
 */

#include "unity.h"

#include "j1939_socketcan.h"

#include "j1939/j1939.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_socketcan_functions_check_arguments(void) {
	int fd = -1;

	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_socketcan_open(NULL, &fd));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_socketcan_rx(fd, NULL, 1U, NULL));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_socketcan_tx(fd, NULL, NULL));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_process(NULL, 0U));
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_socketcan_functions_check_arguments);
	return UNITY_END();
}
