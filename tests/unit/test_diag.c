/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

#include <string.h>

#include "unity.h"

#include "j1939/j1939.h"

#define GUARD 0xA5U

static uint8_t buf[J1939_DIAG_DM_LEN_MAX];
static j1939_diag_dtc_t dtcs[J1939_DIAG_DM_DTC_MAX];
static j1939_diag_dtc_t parsed[J1939_DIAG_DM_DTC_MAX];

static const j1939_diag_lamps_t lamps_off = {
        .mil = J1939_DIAG_LAMP_OFF,
        .red_stop = J1939_DIAG_LAMP_OFF,
        .amber_warning = J1939_DIAG_LAMP_OFF,
        .protect = J1939_DIAG_LAMP_OFF,
        .mil_flash = J1939_DIAG_FLASH_OFF,
        .red_stop_flash = J1939_DIAG_FLASH_OFF,
        .amber_warning_flash = J1939_DIAG_FLASH_OFF,
        .protect_flash = J1939_DIAG_FLASH_OFF,
};

/* SPN 1208, FMI 3, OC 10: the worked example of J1939/73 5.7.1.11. */
static const j1939_diag_dtc_t dtc_1208 = {1208U, 3U, 10U, J1939_DIAG_CM_V4};
static const uint8_t dtc_1208_bytes[J1939_DIAG_DTC_LEN] = {0xB8U, 0x04U, 0x03U, 0x0AU};

static void assert_dtc_equal(const j1939_diag_dtc_t *expected, const j1939_diag_dtc_t *actual) {
	TEST_ASSERT_EQUAL_HEX32(expected->spn, actual->spn);
	TEST_ASSERT_EQUAL_UINT8(expected->fmi, actual->fmi);
	TEST_ASSERT_EQUAL_UINT8(expected->oc, actual->oc);
	TEST_ASSERT_EQUAL_UINT8(expected->cm, actual->cm);
}

static void assert_lamps_equal(const j1939_diag_lamps_t *expected,
                               const j1939_diag_lamps_t *actual) {
	TEST_ASSERT_EQUAL_MEMORY(expected, actual, sizeof(*expected));
}

static void assert_guard(const uint8_t *p, size_t len) {
	size_t i;

	for (i = 0U; i < len; i++) {
		TEST_ASSERT_EQUAL_HEX8(GUARD, p[i]);
	}
}

/* A distinct valid DTC for index i; never the no-DTC marker. */
static j1939_diag_dtc_t dtc_for(uint32_t i) {
	j1939_diag_dtc_t dtc = {(i * 1177U + 1U) & J1939_DIAG_SPN_MAX, (uint8_t)(i % 32U),
	                        (uint8_t)(i % 128U), J1939_DIAG_CM_V4};
	return dtc;
}

void setUp(void) {
	memset(buf, GUARD, sizeof(buf));
	memset(parsed, GUARD, sizeof(parsed));
}

void tearDown(void) {
}

static void test_pgn_constants(void) {
	TEST_ASSERT_EQUAL_HEX32(0xFECAU, J1939_PGN_DM1);
	TEST_ASSERT_EQUAL_HEX32(0xFECBU, J1939_PGN_DM2);
	TEST_ASSERT_EQUAL_HEX32(0xFECCU, J1939_PGN_DM3);
	TEST_ASSERT_EQUAL_HEX32(0xFED3U, J1939_PGN_DM11);
	/* 2 + 4 * 445 fits in the largest message, 2 + 4 * 446 does not. */
	TEST_ASSERT_TRUE(2U + (4U * J1939_DIAG_DM_DTC_MAX) <= J1939_DIAG_DM_LEN_MAX);
	TEST_ASSERT_TRUE(2U + (4U * (J1939_DIAG_DM_DTC_MAX + 1U)) > J1939_DIAG_DM_LEN_MAX);
}

static void test_dtc_known_vectors(void) {
	static const struct {
		j1939_diag_dtc_t dtc;
		uint8_t bytes[J1939_DIAG_DTC_LEN];
	} vectors[] = {
	        {{1208U, 3U, 10U, J1939_DIAG_CM_V4}, {0xB8U, 0x04U, 0x03U, 0x0AU}},
	        {{0U, 0U, 0U, J1939_DIAG_CM_V4}, {0x00U, 0x00U, 0x00U, 0x00U}},
	        {{0x7FFFFU, 31U, 127U, J1939_DIAG_CM_V4}, {0xFFU, 0xFFU, 0xFFU, 0x7FU}},
	        /* Each field alone at its maximum lands in its own bits only. */
	        {{0x000FFU, 0U, 0U, J1939_DIAG_CM_V4}, {0xFFU, 0x00U, 0x00U, 0x00U}},
	        {{0x0FF00U, 0U, 0U, J1939_DIAG_CM_V4}, {0x00U, 0xFFU, 0x00U, 0x00U}},
	        {{0x70000U, 0U, 0U, J1939_DIAG_CM_V4}, {0x00U, 0x00U, 0xE0U, 0x00U}},
	        {{0U, 31U, 0U, J1939_DIAG_CM_V4}, {0x00U, 0x00U, 0x1FU, 0x00U}},
	        {{0U, 0U, 127U, J1939_DIAG_CM_V4}, {0x00U, 0x00U, 0x00U, 0x7FU}},
	        /* Bits either side of the byte and field boundaries. */
	        {{0x00100U, 0U, 0U, J1939_DIAG_CM_V4}, {0x00U, 0x01U, 0x00U, 0x00U}},
	        {{0x10000U, 0U, 0U, J1939_DIAG_CM_V4}, {0x00U, 0x00U, 0x20U, 0x00U}},
	        {{0x40000U, 0U, 0U, J1939_DIAG_CM_V4}, {0x00U, 0x00U, 0x80U, 0x00U}},
	        {{0x10000U, 16U, 64U, J1939_DIAG_CM_V4}, {0x00U, 0x00U, 0x30U, 0x40U}},
	        /* SPN 520192 (0x7F000), FMI 1, OC 1. */
	        {{0x7F000U, 1U, 1U, J1939_DIAG_CM_V4}, {0x00U, 0xF0U, 0xE1U, 0x01U}},
	};
	uint8_t out[J1939_DIAG_DTC_LEN];
	j1939_diag_dtc_t dtc;
	size_t i;

	for (i = 0U; i < (sizeof(vectors) / sizeof(vectors[0])); i++) {
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_diag_dtc_encode(&vectors[i].dtc, out));
		TEST_ASSERT_EQUAL_HEX8_ARRAY(vectors[i].bytes, out, J1939_DIAG_DTC_LEN);
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_diag_dtc_decode(vectors[i].bytes, &dtc));
		assert_dtc_equal(&vectors[i].dtc, &dtc);
	}
}

/* Every SPN, FMI and OC value near a field or byte boundary survives encode and decode. */
static void test_dtc_round_trip_boundaries(void) {
	static const uint32_t spns[] = {0U,       1U,       0xFFU,    0x100U,   0xFFFFU,  0x10000U,
	                                0x1FFFFU, 0x20000U, 0x3FFFFU, 0x40000U, 0x7FFFEU, 0x7FFFFU};
	static const uint8_t fmis[] = {0U, 1U, 15U, 16U, 30U, 31U};
	static const uint8_t ocs[] = {0U, 1U, 63U, 64U, 126U, 127U};
	uint8_t out[J1939_DIAG_DTC_LEN];
	j1939_diag_dtc_t in;
	j1939_diag_dtc_t dtc;
	size_t s;
	size_t f;
	size_t o;

	for (s = 0U; s < (sizeof(spns) / sizeof(spns[0])); s++) {
		for (f = 0U; f < sizeof(fmis); f++) {
			for (o = 0U; o < sizeof(ocs); o++) {
				in.spn = spns[s];
				in.fmi = fmis[f];
				in.oc = ocs[o];
				in.cm = J1939_DIAG_CM_V4;
				TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_diag_dtc_encode(&in, out));
				TEST_ASSERT_EQUAL_HEX8(0U, out[3] & 0x80U);
				TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_diag_dtc_decode(out, &dtc));
				assert_dtc_equal(&in, &dtc);
			}
		}
	}
}

/* Every 4-byte pattern of the upper SPN bits, FMI and CM/OC decodes and re-encodes unchanged. */
static void test_dtc_decode_encode_all_high_bytes(void) {
	uint8_t in[J1939_DIAG_DTC_LEN] = {0x5AU, 0xC3U, 0U, 0U};
	uint8_t out[J1939_DIAG_DTC_LEN];
	j1939_diag_dtc_t dtc;
	uint32_t b2;
	uint32_t b3;

	for (b2 = 0U; b2 <= 0xFFU; b2++) {
		for (b3 = 0U; b3 <= 0x7FU; b3++) {
			in[2] = (uint8_t)b2;
			in[3] = (uint8_t)b3;
			TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_diag_dtc_decode(in, &dtc));
			TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_diag_dtc_encode(&dtc, out));
			TEST_ASSERT_EQUAL_HEX8_ARRAY(in, out, J1939_DIAG_DTC_LEN);
		}
	}
}

/* CM = 1: fields are read at their version 4 positions and the CM bit is reported. */
static void test_dtc_decode_legacy_conversion_method(void) {
	const uint8_t in[J1939_DIAG_DTC_LEN] = {0xB8U, 0x04U, 0x03U, 0x8AU};
	const j1939_diag_dtc_t expected = {1208U, 3U, 10U, J1939_DIAG_CM_LEGACY};
	uint8_t out[J1939_DIAG_DTC_LEN] = {GUARD, GUARD, GUARD, GUARD};
	j1939_diag_dtc_t dtc;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_diag_dtc_decode(in, &dtc));
	assert_dtc_equal(&expected, &dtc);
	/* Only the version 4 layout is encoded. */
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_diag_dtc_encode(&dtc, out));
	assert_guard(out, sizeof(out));
}

static void test_dtc_encode_rejects_out_of_range(void) {
	static const j1939_diag_dtc_t invalid[] = {
	        {0x80000U, 0U, 0U, J1939_DIAG_CM_V4}, {0xFFFFFFFFU, 0U, 0U, J1939_DIAG_CM_V4},
	        {0U, 32U, 0U, J1939_DIAG_CM_V4},      {0U, 0xFFU, 0U, J1939_DIAG_CM_V4},
	        {0U, 0U, 128U, J1939_DIAG_CM_V4},     {0U, 0U, 0xFFU, J1939_DIAG_CM_V4},
	        {0U, 0U, 0U, J1939_DIAG_CM_LEGACY},   {0U, 0U, 0U, 2U},
	};
	uint8_t out[J1939_DIAG_DTC_LEN] = {GUARD, GUARD, GUARD, GUARD};
	j1939_diag_dtc_t dtc = dtc_1208;
	size_t i;

	for (i = 0U; i < (sizeof(invalid) / sizeof(invalid[0])); i++) {
		TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_diag_dtc_encode(&invalid[i], out));
		assert_guard(out, sizeof(out));
	}
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_diag_dtc_encode(NULL, out));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_diag_dtc_encode(&dtc_1208, NULL));
	assert_guard(out, sizeof(out));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_diag_dtc_decode(NULL, &dtc));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_diag_dtc_decode(dtc_1208_bytes, NULL));
	assert_dtc_equal(&dtc_1208, &dtc);
}

static void test_lamps_known_vector(void) {
	const j1939_diag_lamps_t lamps = {
	        .mil = J1939_DIAG_LAMP_ON,
	        .red_stop = J1939_DIAG_LAMP_OFF,
	        .amber_warning = J1939_DIAG_LAMP_ON,
	        .protect = J1939_DIAG_LAMP_NA,
	        .mil_flash = J1939_DIAG_FLASH_FAST,
	        .red_stop_flash = J1939_DIAG_FLASH_OFF,
	        .amber_warning_flash = J1939_DIAG_FLASH_SLOW,
	        .protect_flash = J1939_DIAG_FLASH_RESERVED,
	};
	/* 01 00 01 11, 01 11 00 10 */
	const uint8_t bytes[J1939_DIAG_LAMPS_LEN] = {0x47U, 0x72U};
	uint8_t out[J1939_DIAG_LAMPS_LEN];
	j1939_diag_lamps_t decoded;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_diag_lamps_encode(&lamps, out));
	TEST_ASSERT_EQUAL_HEX8_ARRAY(bytes, out, J1939_DIAG_LAMPS_LEN);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_diag_lamps_decode(bytes, &decoded));
	assert_lamps_equal(&lamps, &decoded);

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_diag_lamps_encode(&lamps_off, out));
	TEST_ASSERT_EQUAL_HEX8(0x00U, out[0]);
	TEST_ASSERT_EQUAL_HEX8(0xFFU, out[1]);
}

/* Each lamp field maps to its own two bits. */
static void test_lamps_field_positions(void) {
	j1939_diag_lamps_t lamps;
	uint8_t out[J1939_DIAG_LAMPS_LEN];
	uint8_t *fields[] = {&lamps.mil,
	                     &lamps.red_stop,
	                     &lamps.amber_warning,
	                     &lamps.protect,
	                     &lamps.mil_flash,
	                     &lamps.red_stop_flash,
	                     &lamps.amber_warning_flash,
	                     &lamps.protect_flash};
	size_t i;

	for (i = 0U; i < 8U; i++) {
		memset(&lamps, 0, sizeof(lamps));
		*fields[i] = J1939_DIAG_LAMP_ON;
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_diag_lamps_encode(&lamps, out));
		TEST_ASSERT_EQUAL_HEX8((i < 4U) ? (uint8_t)(0x40U >> (2U * i)) : 0U, out[0]);
		TEST_ASSERT_EQUAL_HEX8((i >= 4U) ? (uint8_t)(0x40U >> (2U * (i - 4U))) : 0U,
		                       out[1]);
	}
}

/* All 65536 lamp byte pairs decode and re-encode unchanged. */
static void test_lamps_round_trip_all(void) {
	uint8_t in[J1939_DIAG_LAMPS_LEN];
	uint8_t out[J1939_DIAG_LAMPS_LEN];
	j1939_diag_lamps_t lamps;
	uint32_t v;

	for (v = 0U; v <= 0xFFFFU; v++) {
		in[0] = (uint8_t)(v >> 8);
		in[1] = (uint8_t)v;
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_diag_lamps_decode(in, &lamps));
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_diag_lamps_encode(&lamps, out));
		TEST_ASSERT_EQUAL_HEX8_ARRAY(in, out, J1939_DIAG_LAMPS_LEN);
	}
}

static void test_lamps_reject_invalid(void) {
	uint8_t out[J1939_DIAG_LAMPS_LEN] = {GUARD, GUARD};
	j1939_diag_lamps_t lamps;
	uint8_t *fields[] = {&lamps.mil,
	                     &lamps.red_stop,
	                     &lamps.amber_warning,
	                     &lamps.protect,
	                     &lamps.mil_flash,
	                     &lamps.red_stop_flash,
	                     &lamps.amber_warning_flash,
	                     &lamps.protect_flash};
	size_t i;

	for (i = 0U; i < 8U; i++) {
		lamps = lamps_off;
		*fields[i] = 4U;
		TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_diag_lamps_encode(&lamps, out));
		assert_guard(out, sizeof(out));
	}
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_diag_lamps_encode(NULL, out));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_diag_lamps_encode(&lamps_off, NULL));
	assert_guard(out, sizeof(out));

	lamps = lamps_off;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_diag_lamps_decode(NULL, &lamps));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_diag_lamps_decode(out, NULL));
	assert_lamps_equal(&lamps_off, &lamps);
}

/* No DTCs: lamps, the all-zero DTC, then 0xFF 0xFF. */
static void test_dm_build_no_dtc(void) {
	const uint8_t expected[] = {0x00U, 0xFFU, 0x00U, 0x00U, 0x00U, 0x00U, 0xFFU, 0xFFU};
	uint16_t len = 0U;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_diag_dm_build(&lamps_off, NULL, 0U, buf, 8U, &len));
	TEST_ASSERT_EQUAL_UINT16(8U, len);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, buf, sizeof(expected));
	assert_guard(&buf[8], 8U);

	/* A DTC array is ignored when the count is 0. */
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_diag_dm_build(&lamps_off, &dtc_1208, 0U, buf, 8U, &len));
	TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, buf, sizeof(expected));
}

/* One DTC: padded to a full frame with 0xFF. */
static void test_dm_build_one_dtc(void) {
	const j1939_diag_lamps_t lamps = {J1939_DIAG_LAMP_OFF,  J1939_DIAG_LAMP_OFF,
	                                  J1939_DIAG_LAMP_ON,   J1939_DIAG_LAMP_OFF,
	                                  J1939_DIAG_FLASH_OFF, J1939_DIAG_FLASH_OFF,
	                                  J1939_DIAG_FLASH_OFF, J1939_DIAG_FLASH_OFF};
	const uint8_t expected[] = {0x04U, 0xFFU, 0xB8U, 0x04U, 0x03U, 0x0AU, 0xFFU, 0xFFU};
	uint16_t len = 0U;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_diag_dm_build(&lamps, &dtc_1208, 1U, buf, 8U, &len));
	TEST_ASSERT_EQUAL_UINT16(8U, len);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, buf, sizeof(expected));
	assert_guard(&buf[8], 8U);
}

static void test_dm_build_two_dtcs(void) {
	const j1939_diag_dtc_t list[] = {dtc_1208, {0x7FFFFU, 31U, 127U, J1939_DIAG_CM_V4}};
	const uint8_t expected[] = {0x00U, 0xFFU, 0xB8U, 0x04U, 0x03U,
	                            0x0AU, 0xFFU, 0xFFU, 0xFFU, 0x7FU};
	uint16_t len = 0U;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_diag_dm_build(&lamps_off, list, 2U, buf, 10U, &len));
	TEST_ASSERT_EQUAL_UINT16(10U, len);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, buf, sizeof(expected));
	assert_guard(&buf[10], 8U);
}

static void test_dm_build_parse_many(void) {
	static const uint16_t counts[] = {2U, 3U, 17U, 255U, 256U, J1939_DIAG_DM_DTC_MAX};
	j1939_diag_lamps_t lamps;
	uint16_t len;
	uint16_t count;
	size_t c;
	uint16_t i;

	for (i = 0U; i < J1939_DIAG_DM_DTC_MAX; i++) {
		dtcs[i] = dtc_for(i);
	}
	for (c = 0U; c < (sizeof(counts) / sizeof(counts[0])); c++) {
		const uint16_t n = counts[c];
		const uint16_t size = (uint16_t)(2U + (4U * n));

		memset(buf, GUARD, sizeof(buf));
		len = 0U;
		TEST_ASSERT_EQUAL(J1939_RET_OK,
		                  j1939_diag_dm_build(&lamps_off, dtcs, n, buf, size, &len));
		TEST_ASSERT_EQUAL_UINT16(size, len);
		if (size < sizeof(buf)) {
			TEST_ASSERT_EQUAL_HEX8(GUARD, buf[size]);
		}

		count = 0U;
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_diag_dm_parse(buf, len, &lamps, parsed,
		                                                    J1939_DIAG_DM_DTC_MAX, &count));
		TEST_ASSERT_EQUAL_UINT16(n, count);
		assert_lamps_equal(&lamps_off, &lamps);
		for (i = 0U; i < n; i++) {
			assert_dtc_equal(&dtcs[i], &parsed[i]);
		}
	}
}

static void test_dm_build_buffer_too_small(void) {
	const j1939_diag_dtc_t list[] = {dtc_1208, dtc_1208};
	uint16_t len = 0xBEEFU;

	TEST_ASSERT_EQUAL(J1939_RET_ERR_FULL,
	                  j1939_diag_dm_build(&lamps_off, NULL, 0U, buf, 7U, &len));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_FULL,
	                  j1939_diag_dm_build(&lamps_off, list, 1U, buf, 7U, &len));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_FULL,
	                  j1939_diag_dm_build(&lamps_off, list, 2U, buf, 9U, &len));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_FULL,
	                  j1939_diag_dm_build(&lamps_off, list, 2U, buf, 0U, &len));
	TEST_ASSERT_EQUAL_HEX16(0xBEEFU, len);
	assert_guard(buf, 16U);

	/* A larger buffer is fine. */
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_diag_dm_build(&lamps_off, list, 2U, buf, sizeof(buf), &len));
	TEST_ASSERT_EQUAL_UINT16(10U, len);
}

static void test_dm_build_rejects_invalid(void) {
	j1939_diag_dtc_t list[] = {dtc_1208, dtc_1208, dtc_1208};
	j1939_diag_lamps_t lamps = lamps_off;
	uint16_t len = 0xBEEFU;

	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_diag_dm_build(NULL, list, 1U, buf, 8U, &len));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_diag_dm_build(&lamps, NULL, 1U, buf, 8U, &len));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_diag_dm_build(&lamps, list, 1U, NULL, 8U, &len));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_diag_dm_build(&lamps, list, 1U, buf, 8U, NULL));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG,
	                  j1939_diag_dm_build(&lamps, dtcs, J1939_DIAG_DM_DTC_MAX + 1U, buf,
	                                      sizeof(buf), &len));

	/* Invalid lamp value. */
	lamps.protect_flash = 4U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_diag_dm_build(&lamps, list, 1U, buf, 8U, &len));

	/* Invalid DTC anywhere in the list, including the last one. */
	list[2].fmi = 32U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG,
	                  j1939_diag_dm_build(&lamps_off, list, 3U, buf, sizeof(buf), &len));
	list[2] = dtc_1208;
	list[1].cm = J1939_DIAG_CM_LEGACY;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG,
	                  j1939_diag_dm_build(&lamps_off, list, 3U, buf, sizeof(buf), &len));

	/* The zero DTC is reserved for "no DTCs", whatever its occurrence count. */
	list[1] = (j1939_diag_dtc_t){0U, 0U, 5U, J1939_DIAG_CM_V4};
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG,
	                  j1939_diag_dm_build(&lamps_off, list, 3U, buf, sizeof(buf), &len));
	list[0] = (j1939_diag_dtc_t){0U, 0U, 0U, J1939_DIAG_CM_V4};
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG,
	                  j1939_diag_dm_build(&lamps_off, list, 1U, buf, sizeof(buf), &len));

	TEST_ASSERT_EQUAL_HEX16(0xBEEFU, len);
	assert_guard(buf, 16U);

	/* SPN 0 with a non-zero FMI, and FMI 0 with a non-zero SPN, are ordinary DTCs. */
	list[0] = (j1939_diag_dtc_t){0U, 1U, 0U, J1939_DIAG_CM_V4};
	list[1] = (j1939_diag_dtc_t){1U, 0U, 0U, J1939_DIAG_CM_V4};
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_diag_dm_build(&lamps_off, list, 2U, buf, sizeof(buf), &len));
}

static void test_dm_parse_no_dtc(void) {
	const uint8_t data[] = {0x00U, 0xFFU, 0x00U, 0x00U, 0x00U, 0x00U, 0xFFU, 0xFFU};
	j1939_diag_lamps_t lamps;
	uint16_t count = 0xBEEFU;

	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_diag_dm_parse(data, sizeof(data), &lamps, parsed, 4U, &count));
	TEST_ASSERT_EQUAL_UINT16(0U, count);
	assert_lamps_equal(&lamps_off, &lamps);
	assert_guard((const uint8_t *)parsed, sizeof(parsed[0]));

	/* No DTC storage is needed. */
	count = 0xBEEFU;
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_diag_dm_parse(data, sizeof(data), &lamps, NULL, 0U, &count));
	TEST_ASSERT_EQUAL_UINT16(0U, count);

	/* The unpadded form, as carried by the transport protocol, is the same. */
	count = 0xBEEFU;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_diag_dm_parse(data, 6U, &lamps, NULL, 0U, &count));
	TEST_ASSERT_EQUAL_UINT16(0U, count);
}

/* SPN 0 and FMI 0 mark "no DTCs"; the occurrence count and CM bit do not matter. */
static void test_dm_parse_no_dtc_marker_ignores_oc_and_cm(void) {
	const uint8_t data[] = {0x00U, 0xFFU, 0x00U, 0x00U, 0x00U, 0xFFU, 0xFFU, 0xFFU};
	j1939_diag_lamps_t lamps;
	uint16_t count = 0xBEEFU;

	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_diag_dm_parse(data, sizeof(data), &lamps, NULL, 0U, &count));
	TEST_ASSERT_EQUAL_UINT16(0U, count);
}

static void test_dm_parse_one_dtc(void) {
	const uint8_t data[] = {0x47U, 0x72U, 0xB8U, 0x04U, 0x03U, 0x0AU, 0xFFU, 0xFFU};
	j1939_diag_lamps_t lamps;
	uint16_t count = 0U;

	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_diag_dm_parse(data, sizeof(data), &lamps, parsed, 1U, &count));
	TEST_ASSERT_EQUAL_UINT16(1U, count);
	assert_dtc_equal(&dtc_1208, &parsed[0]);
	TEST_ASSERT_EQUAL_UINT8(J1939_DIAG_LAMP_ON, lamps.mil);
	TEST_ASSERT_EQUAL_UINT8(J1939_DIAG_LAMP_NA, lamps.protect);
	TEST_ASSERT_EQUAL_UINT8(J1939_DIAG_FLASH_FAST, lamps.mil_flash);
	TEST_ASSERT_EQUAL_UINT8(J1939_DIAG_FLASH_RESERVED, lamps.protect_flash);
	assert_guard((const uint8_t *)&parsed[1], sizeof(parsed[1]));

	/* Without the padding. */
	count = 0U;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_diag_dm_parse(data, 6U, &lamps, parsed, 1U, &count));
	TEST_ASSERT_EQUAL_UINT16(1U, count);
	assert_dtc_equal(&dtc_1208, &parsed[0]);
}

/* In a list of several DTCs the zero DTC is not the marker; a legacy CM is reported. */
static void test_dm_parse_list_content(void) {
	const uint8_t data[] = {0x00U, 0xFFU, 0x00U, 0x00U, 0x00U,
	                        0x00U, 0xB8U, 0x04U, 0x03U, 0x8AU};
	const j1939_diag_dtc_t zero = {0U, 0U, 0U, J1939_DIAG_CM_V4};
	const j1939_diag_dtc_t legacy = {1208U, 3U, 10U, J1939_DIAG_CM_LEGACY};
	j1939_diag_lamps_t lamps;
	uint16_t count = 0U;

	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_diag_dm_parse(data, sizeof(data), &lamps, parsed, 2U, &count));
	TEST_ASSERT_EQUAL_UINT16(2U, count);
	assert_dtc_equal(&zero, &parsed[0]);
	assert_dtc_equal(&legacy, &parsed[1]);
}

static void test_dm_parse_rejects_malformed_length(void) {
	static const uint16_t lengths[] = {0U,    1U,    2U,    3U,    4U,     5U,  7U,
	                                   9U,    11U,   12U,   13U,   16U,    17U, 1783U,
	                                   1784U, 1785U, 1786U, 1790U, 0xFFFEU};
	j1939_diag_lamps_t lamps = lamps_off;
	uint16_t count = 0xBEEFU;
	size_t i;

	memset(buf, 0x11U, sizeof(buf));
	for (i = 0U; i < (sizeof(lengths) / sizeof(lengths[0])); i++) {
		TEST_ASSERT_EQUAL_MESSAGE(J1939_RET_ERR_ARG,
		                          j1939_diag_dm_parse(buf, lengths[i], &lamps, parsed,
		                                              J1939_DIAG_DM_DTC_MAX, &count),
		                          "length");
	}
	TEST_ASSERT_EQUAL_HEX16(0xBEEFU, count);
	assert_lamps_equal(&lamps_off, &lamps);
	assert_guard((const uint8_t *)parsed, sizeof(parsed));
}

/* Eight bytes are one padded DTC only if both padding bytes are 0xFF. */
static void test_dm_parse_rejects_bad_padding(void) {
	uint8_t data[] = {0x00U, 0xFFU, 0xB8U, 0x04U, 0x03U, 0x0AU, 0xFFU, 0xFFU};
	j1939_diag_lamps_t lamps;
	uint16_t count = 0xBEEFU;

	data[6] = 0x00U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG,
	                  j1939_diag_dm_parse(data, sizeof(data), &lamps, parsed, 2U, &count));
	data[6] = 0xFFU;
	data[7] = 0xFEU;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG,
	                  j1939_diag_dm_parse(data, sizeof(data), &lamps, parsed, 2U, &count));
	data[6] = 0x00U;
	data[7] = 0x00U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG,
	                  j1939_diag_dm_parse(data, sizeof(data), &lamps, parsed, 2U, &count));
	TEST_ASSERT_EQUAL_HEX16(0xBEEFU, count);
}

static void test_dm_parse_storage_too_small(void) {
	const uint8_t data[] = {0x00U, 0xFFU, 0xB8U, 0x04U, 0x03U, 0x0AU, 0xB8U,
	                        0x04U, 0x03U, 0x0BU, 0xB8U, 0x04U, 0x03U, 0x0CU};
	j1939_diag_lamps_t lamps = lamps_off;
	uint16_t count = 0U;

	lamps.mil = J1939_DIAG_LAMP_ON;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_FULL,
	                  j1939_diag_dm_parse(data, sizeof(data), &lamps, parsed, 2U, &count));
	TEST_ASSERT_EQUAL_UINT16(3U, count);
	TEST_ASSERT_EQUAL_UINT8(J1939_DIAG_LAMP_ON, lamps.mil);
	assert_guard((const uint8_t *)parsed, 3U * sizeof(parsed[0]));

	count = 0U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_FULL,
	                  j1939_diag_dm_parse(data, sizeof(data), &lamps, NULL, 0U, &count));
	TEST_ASSERT_EQUAL_UINT16(3U, count);

	/* One DTC with no storage. */
	count = 0U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_FULL,
	                  j1939_diag_dm_parse(data, 6U, &lamps, NULL, 0U, &count));
	TEST_ASSERT_EQUAL_UINT16(1U, count);

	count = 0U;
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_diag_dm_parse(data, sizeof(data), &lamps, parsed, 3U, &count));
	TEST_ASSERT_EQUAL_UINT16(3U, count);
	TEST_ASSERT_EQUAL_UINT8(12U, parsed[2].oc);
}

static void test_dm_parse_rejects_null(void) {
	const uint8_t data[] = {0x00U, 0xFFU, 0xB8U, 0x04U, 0x03U, 0x0AU, 0xFFU, 0xFFU};
	j1939_diag_lamps_t lamps;
	uint16_t count = 0xBEEFU;

	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG,
	                  j1939_diag_dm_parse(NULL, 8U, &lamps, parsed, 1U, &count));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG,
	                  j1939_diag_dm_parse(data, 8U, NULL, parsed, 1U, &count));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG,
	                  j1939_diag_dm_parse(data, 8U, &lamps, NULL, 1U, &count));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG,
	                  j1939_diag_dm_parse(data, 8U, &lamps, parsed, 1U, NULL));
	TEST_ASSERT_EQUAL_HEX16(0xBEEFU, count);
}

/* What dm_build produces for 0 and 1 DTC, dm_parse reads back. */
static void test_dm_build_parse_zero_and_one(void) {
	j1939_diag_lamps_t lamps;
	uint16_t len = 0U;
	uint16_t count = 0xBEEFU;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_diag_dm_build(&lamps_off, NULL, 0U, buf, 8U, &len));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_diag_dm_parse(buf, len, &lamps, parsed, 1U, &count));
	TEST_ASSERT_EQUAL_UINT16(0U, count);

	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_diag_dm_build(&lamps_off, &dtc_1208, 1U, buf, 8U, &len));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_diag_dm_parse(buf, len, &lamps, parsed, 1U, &count));
	TEST_ASSERT_EQUAL_UINT16(1U, count);
	assert_dtc_equal(&dtc_1208, &parsed[0]);
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_pgn_constants);
	RUN_TEST(test_dtc_known_vectors);
	RUN_TEST(test_dtc_round_trip_boundaries);
	RUN_TEST(test_dtc_decode_encode_all_high_bytes);
	RUN_TEST(test_dtc_decode_legacy_conversion_method);
	RUN_TEST(test_dtc_encode_rejects_out_of_range);
	RUN_TEST(test_lamps_known_vector);
	RUN_TEST(test_lamps_field_positions);
	RUN_TEST(test_lamps_round_trip_all);
	RUN_TEST(test_lamps_reject_invalid);
	RUN_TEST(test_dm_build_no_dtc);
	RUN_TEST(test_dm_build_one_dtc);
	RUN_TEST(test_dm_build_two_dtcs);
	RUN_TEST(test_dm_build_parse_many);
	RUN_TEST(test_dm_build_buffer_too_small);
	RUN_TEST(test_dm_build_rejects_invalid);
	RUN_TEST(test_dm_parse_no_dtc);
	RUN_TEST(test_dm_parse_no_dtc_marker_ignores_oc_and_cm);
	RUN_TEST(test_dm_parse_one_dtc);
	RUN_TEST(test_dm_parse_list_content);
	RUN_TEST(test_dm_parse_rejects_malformed_length);
	RUN_TEST(test_dm_parse_rejects_bad_padding);
	RUN_TEST(test_dm_parse_storage_too_small);
	RUN_TEST(test_dm_parse_rejects_null);
	RUN_TEST(test_dm_build_parse_zero_and_one);
	return UNITY_END();
}
