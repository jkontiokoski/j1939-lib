/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

#include "unity.h"

#include "j1939/j1939_id.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_decode_pdu2(void) {
	/* EEC1-style broadcast: prio 3, PGN 0xF004, SA 0x00 */
	const uint32_t id = 0x0CF00400U;

	TEST_ASSERT_EQUAL_UINT8(3U, j1939_id_prio_get(id));
	TEST_ASSERT_EQUAL_HEX32(0xF004U, j1939_id_pgn_get(id));
	TEST_ASSERT_EQUAL_HEX8(J1939_ADDR_GLOBAL, j1939_id_da_get(id));
	TEST_ASSERT_EQUAL_HEX8(0x00U, j1939_id_sa_get(id));
}

static void test_decode_pdu1(void) {
	/* Request: prio 6, PGN 0xEA00, DA 0x12, SA 0x34 */
	const uint32_t id = 0x18EA1234U;

	TEST_ASSERT_EQUAL_UINT8(6U, j1939_id_prio_get(id));
	TEST_ASSERT_EQUAL_HEX32(0xEA00U, j1939_id_pgn_get(id));
	TEST_ASSERT_EQUAL_HEX8(0x12U, j1939_id_da_get(id));
	TEST_ASSERT_EQUAL_HEX8(0x34U, j1939_id_sa_get(id));
}

static void test_decode_data_pages(void) {
	/* DP = 1 */
	TEST_ASSERT_EQUAL_HEX32(0x1FEF1U, j1939_id_pgn_get(0x19FEF100U));
	/* EDP = 1 */
	TEST_ASSERT_EQUAL_HEX32(0x2EA00U, j1939_id_pgn_get(0x1AEA1234U));
	/* EDP = 1, DP = 1 */
	TEST_ASSERT_EQUAL_HEX32(0x3FF00U, j1939_id_pgn_get(0x1BFF0000U));
}

static void test_decode_ignores_bits_above_28(void) {
	const uint32_t id = 0xE0000000U | 0x18EA1234U;

	TEST_ASSERT_EQUAL_UINT8(6U, j1939_id_prio_get(id));
	TEST_ASSERT_EQUAL_HEX32(0xEA00U, j1939_id_pgn_get(id));
}

static void test_pdu_format_boundary(void) {
	TEST_ASSERT_TRUE(j1939_pgn_is_pdu1(0x0000U));
	TEST_ASSERT_TRUE(j1939_pgn_is_pdu1(0xEF00U));
	TEST_ASSERT_FALSE(j1939_pgn_is_pdu1(0xF000U));
	TEST_ASSERT_FALSE(j1939_pgn_is_pdu1(0xFFFFU));
	TEST_ASSERT_TRUE(j1939_pgn_is_pdu1(0x1EF00U));
	TEST_ASSERT_FALSE(j1939_pgn_is_pdu1(0x1F000U));

	/* PF 239 carries a destination address, PF 240 does not. */
	TEST_ASSERT_EQUAL_HEX32(0xEF00U, j1939_id_pgn_get(0x18EF1234U));
	TEST_ASSERT_EQUAL_HEX8(0x12U, j1939_id_da_get(0x18EF1234U));
	TEST_ASSERT_EQUAL_HEX32(0xF012U, j1939_id_pgn_get(0x18F01234U));
	TEST_ASSERT_EQUAL_HEX8(J1939_ADDR_GLOBAL, j1939_id_da_get(0x18F01234U));
}

static void test_build_known_ids(void) {
	uint32_t id = 0U;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_id_build(3U, 0xF004U, J1939_ADDR_GLOBAL, 0x00U, &id));
	TEST_ASSERT_EQUAL_HEX32(0x0CF00400U, id);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_id_build(6U, 0xEA00U, 0x12U, 0x34U, &id));
	TEST_ASSERT_EQUAL_HEX32(0x18EA1234U, id);
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_id_build(7U, 0x3FFFFU, J1939_ADDR_GLOBAL, 0xFFU, &id));
	TEST_ASSERT_EQUAL_HEX32(J1939_ID_MASK, id);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_id_build(0U, 0x0000U, 0x00U, 0x00U, &id));
	TEST_ASSERT_EQUAL_HEX32(0x00000000U, id);
}

static void test_build_rejects_invalid_arguments(void) {
	uint32_t id = 0xDEADBEEFU;

	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_id_build(8U, 0xF004U, 0xFFU, 0x00U, &id));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_id_build(6U, 0x40000U, 0xFFU, 0x00U, &id));
	/* PDU1 PGN with a non-zero lowest byte */
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_id_build(6U, 0xEA01U, 0x12U, 0x00U, &id));
	/* PDU2 PGN with a specific destination */
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_id_build(6U, 0xF004U, 0x12U, 0x00U, &id));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_id_build(6U, 0xF004U, 0xFFU, 0x00U, NULL));
	TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFU, id);
}

/* Every PGN, with every priority and a spread of addresses, survives build and decode. */
static void test_round_trip_all_pgns(void) {
	static const uint8_t addrs[] = {0x00U, 0x01U, 0x7FU, 0x80U, 0xFEU, 0xFFU};
	uint32_t pgn;
	uint32_t id;
	uint8_t prio;
	size_t a;
	uint8_t da;
	uint8_t sa;

	for (pgn = 0U; pgn <= J1939_PGN_MAX; pgn++) {
		if (j1939_pgn_is_pdu1(pgn) && ((pgn & 0xFFU) != 0U)) {
			continue;
		}
		for (a = 0U; a < sizeof(addrs); a++) {
			prio = (uint8_t)((pgn + a) % 8U);
			sa = addrs[a];
			da = j1939_pgn_is_pdu1(pgn) ? addrs[sizeof(addrs) - 1U - a]
			                            : J1939_ADDR_GLOBAL;
			TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_id_build(prio, pgn, da, sa, &id));
			TEST_ASSERT_EQUAL_HEX32(0U, id & ~J1939_ID_MASK);
			TEST_ASSERT_EQUAL_UINT8(prio, j1939_id_prio_get(id));
			TEST_ASSERT_EQUAL_HEX32(pgn, j1939_id_pgn_get(id));
			TEST_ASSERT_EQUAL_HEX8(da, j1939_id_da_get(id));
			TEST_ASSERT_EQUAL_HEX8(sa, j1939_id_sa_get(id));
		}
	}
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_decode_pdu2);
	RUN_TEST(test_decode_pdu1);
	RUN_TEST(test_decode_data_pages);
	RUN_TEST(test_decode_ignores_bits_above_28);
	RUN_TEST(test_pdu_format_boundary);
	RUN_TEST(test_build_known_ids);
	RUN_TEST(test_build_rejects_invalid_arguments);
	RUN_TEST(test_round_trip_all_pgns);
	return UNITY_END();
}
