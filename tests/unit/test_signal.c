/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

#include <stddef.h>
#include <string.h>

#include "unity.h"

#include "j1939/j1939_signal.h"

#define PAYLOAD_LEN 8U

void setUp(void) {
}

void tearDown(void) {
}

/* Descriptor with the common fields filled in. */
static j1939_signal_t sig_make(j1939_signal_type_t type, uint16_t start, uint8_t bits, uint32_t num,
                               uint32_t den, int64_t offset) {
	j1939_signal_t sig;

	memset(&sig, 0, sizeof(sig));
	sig.spn = 520192U;
	sig.pgn = 0xFF10U;
	sig.start = start;
	sig.bits = bits;
	sig.type = type;
	sig.res_num = num;
	sig.res_den = den;
	sig.offset = offset;
	sig.unit = "u";
	sig.name = "test";
	return sig;
}

/* Reference model: one payload bit at a time. */
static uint32_t ref_bit(const uint8_t *data, uint32_t n) {
	return ((uint32_t)data[n / 8U] >> (n % 8U)) & 1U;
}

static uint32_t ref_get(const uint8_t *data, uint32_t start, uint32_t bits) {
	uint32_t raw = 0U;
	uint32_t i;

	for (i = 0U; i < bits; i++) {
		raw |= ref_bit(data, start + i) << i;
	}
	return raw;
}

static void ref_set(uint8_t *data, uint32_t start, uint32_t bits, uint32_t raw) {
	uint32_t i;

	for (i = 0U; i < bits; i++) {
		uint32_t n = start + i;
		uint8_t m = (uint8_t)(1U << (n % 8U));

		if (((raw >> i) & 1U) != 0U) {
			data[n / 8U] = (uint8_t)(data[n / 8U] | m);
		} else {
			data[n / 8U] = (uint8_t)(data[n / 8U] & (uint8_t)~m);
		}
	}
}

static uint32_t max_of(uint32_t bits) {
	return (bits >= 32U) ? 0xFFFFFFFFU : ((1U << bits) - 1U);
}

/* Deterministic pseudo-random numbers (xorshift32). */
static uint32_t rng_state = 0x12345678U;

static uint32_t rng(void) {
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return rng_state;
}

static void test_pos_macro(void) {
	TEST_ASSERT_EQUAL_UINT16(0U, J1939_SIGNAL_POS(1U, 1U));
	TEST_ASSERT_EQUAL_UINT16(7U, J1939_SIGNAL_POS(1U, 8U));
	TEST_ASSERT_EQUAL_UINT16(10U, J1939_SIGNAL_POS(2U, 3U));
	TEST_ASSERT_EQUAL_UINT16(63U, J1939_SIGNAL_POS(8U, 8U));
}

static void test_bits_known_values(void) {
	const uint8_t data[PAYLOAD_LEN] = {0x12U, 0x34U, 0x56U, 0x78U, 0x9AU, 0xBCU, 0xDEU, 0xF0U};
	uint32_t raw = 0U;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_bits_get(data, PAYLOAD_LEN, 0U, 8U, &raw));
	TEST_ASSERT_EQUAL_HEX32(0x12U, raw);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_bits_get(data, PAYLOAD_LEN, 0U, 16U, &raw));
	TEST_ASSERT_EQUAL_HEX32(0x3412U, raw);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_bits_get(data, PAYLOAD_LEN, 0U, 32U, &raw));
	TEST_ASSERT_EQUAL_HEX32(0x78563412U, raw);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_bits_get(data, PAYLOAD_LEN, 32U, 32U, &raw));
	TEST_ASSERT_EQUAL_HEX32(0xF0DEBC9AU, raw);
	/* Nibbles and a field across five bytes. */
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_bits_get(data, PAYLOAD_LEN, 4U, 4U, &raw));
	TEST_ASSERT_EQUAL_HEX32(0x1U, raw);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_bits_get(data, PAYLOAD_LEN, 4U, 12U, &raw));
	TEST_ASSERT_EQUAL_HEX32(0x341U, raw);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_bits_get(data, PAYLOAD_LEN, 4U, 32U, &raw));
	TEST_ASSERT_EQUAL_HEX32(0xA7856341U, raw);
	/* Single bits: bit 1 of byte 0 is set, bit 0 is not. */
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_bits_get(data, PAYLOAD_LEN, 1U, 1U, &raw));
	TEST_ASSERT_EQUAL_HEX32(1U, raw);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_bits_get(data, PAYLOAD_LEN, 0U, 1U, &raw));
	TEST_ASSERT_EQUAL_HEX32(0U, raw);
	/* The last bit of the payload. */
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_bits_get(data, PAYLOAD_LEN, 63U, 1U, &raw));
	TEST_ASSERT_EQUAL_HEX32(1U, raw);
}

static void test_bits_set_known_values(void) {
	uint8_t data[PAYLOAD_LEN];
	const uint8_t expect[PAYLOAD_LEN] = {0x0FU, 0xFFU, 0xA5U, 0x5AU,
	                                     0x3FU, 0xF0U, 0xFFU, 0x7FU};

	memset(data, 0xFF, sizeof(data));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_bits_set(data, PAYLOAD_LEN, 4U, 4U, 0x0U));
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_signal_bits_set(data, PAYLOAD_LEN, 16U, 16U, 0x5AA5U));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_bits_set(data, PAYLOAD_LEN, 38U, 6U, 0x0U));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_bits_set(data, PAYLOAD_LEN, 63U, 1U, 0x0U));
	TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, data, PAYLOAD_LEN);
}

/*
 * Every offset and length in 1, 2, 3, 5 and 8 byte payloads, on several
 * backgrounds and values: the read matches the reference model and a write
 * changes exactly the field's bits.
 */
static void test_bits_round_trip_exhaustive(void) {
	static const uint16_t lens[] = {1U, 2U, 3U, 5U, 8U};
	static const uint8_t backgrounds[] = {0x00U, 0xFFU, 0xA5U};
	uint8_t data[PAYLOAD_LEN];
	uint8_t expect[PAYLOAD_LEN];
	size_t l;
	size_t b;
	uint32_t bits;
	uint32_t start;
	uint32_t v;

	for (l = 0U; l < (sizeof(lens) / sizeof(lens[0])); l++) {
		uint32_t total = (uint32_t)lens[l] * 8U;

		for (bits = 1U; (bits <= 32U) && (bits <= total); bits++) {
			for (start = 0U; (start + bits) <= total; start++) {
				const uint32_t values[] = {0U,
				                           max_of(bits),
				                           0x55555555U & max_of(bits),
				                           0xAAAAAAAAU & max_of(bits),
				                           1U,
				                           rng() & max_of(bits)};

				for (b = 0U; b < (sizeof(backgrounds) + 1U); b++) {
					uint32_t raw = 0U;
					size_t i;

					for (i = 0U; i < PAYLOAD_LEN; i++) {
						data[i] = (b < sizeof(backgrounds))
						                  ? backgrounds[b]
						                  : (uint8_t)rng();
					}
					TEST_ASSERT_EQUAL(J1939_RET_OK,
					                  j1939_signal_bits_get(
					                          data, lens[l], (uint16_t)start,
					                          (uint8_t)bits, &raw));
					TEST_ASSERT_EQUAL_HEX32(ref_get(data, start, bits), raw);

					for (v = 0U; v < (sizeof(values) / sizeof(values[0]));
					     v++) {
						memcpy(expect, data, sizeof(data));
						ref_set(expect, start, bits, values[v]);
						TEST_ASSERT_EQUAL(
						        J1939_RET_OK,
						        j1939_signal_bits_set(
						                data, lens[l], (uint16_t)start,
						                (uint8_t)bits, values[v]));
						TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, data,
						                             PAYLOAD_LEN);
						TEST_ASSERT_EQUAL(
						        J1939_RET_OK,
						        j1939_signal_bits_get(data, lens[l],
						                              (uint16_t)start,
						                              (uint8_t)bits, &raw));
						TEST_ASSERT_EQUAL_HEX32(values[v], raw);
					}
				}
			}
		}
	}
}

static void test_bits_rejects_invalid_arguments(void) {
	uint8_t data[PAYLOAD_LEN];
	uint8_t before[PAYLOAD_LEN];
	uint32_t raw = 0xDEADBEEFU;

	memset(data, 0x5A, sizeof(data));
	memcpy(before, data, sizeof(data));

	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_bits_get(NULL, 8U, 0U, 8U, &raw));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_bits_get(data, 8U, 0U, 8U, NULL));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_bits_get(data, 8U, 0U, 0U, &raw));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_bits_get(data, 8U, 0U, 33U, &raw));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_bits_get(data, 8U, 57U, 8U, &raw));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_bits_get(data, 0U, 0U, 1U, &raw));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_bits_get(data, 2U, 0xFFFFU, 32U, &raw));
	TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFU, raw);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_bits_get(data, 8U, 56U, 8U, &raw));

	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_bits_set(NULL, 8U, 0U, 8U, 0U));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_bits_set(data, 8U, 0U, 0U, 0U));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_bits_set(data, 8U, 0U, 33U, 0U));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_bits_set(data, 8U, 57U, 8U, 0U));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_bits_set(data, 8U, 0U, 7U, 0x80U));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_bits_set(data, 8U, 0U, 1U, 2U));
	TEST_ASSERT_EQUAL_HEX8_ARRAY(before, data, PAYLOAD_LEN);
}

static void test_check_accepts_valid_descriptors(void) {
	j1939_signal_t sig = sig_make(J1939_SIGNAL_TYPE_PLAIN, 0U, 1U, 1U, 1U, 0);
	uint8_t bits;

	for (bits = 1U; bits <= 32U; bits++) {
		sig.bits = bits;
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_check(&sig));
	}
	sig = sig_make(J1939_SIGNAL_TYPE_CONTINUOUS, 0U, 8U, 1U, 1U, 0);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_check(&sig));
	sig.bits = 16U;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_check(&sig));
	sig.bits = 32U;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_check(&sig));
	sig = sig_make(J1939_SIGNAL_TYPE_DISCRETE, 6U, 2U, 1U, 1U, 0);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_check(&sig));

	/* Limits of the SPN, the PGN and the position. */
	sig = sig_make(J1939_SIGNAL_TYPE_PLAIN, 0U, 32U, 1U, 1U, 0);
	sig.spn = J1939_SPN_MAX;
	sig.pgn = 0x3FFFFU;
	sig.start = (uint16_t)((J1939_SIGNAL_LEN_MAX * 8U) - 32U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_check(&sig));
	sig.pgn = 0xEF00U; /* PDU1 */
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_check(&sig));

	/* Largest scaling: (2^32 - 1) * 0x7FFFFFFF stays below INT64_MAX. */
	sig = sig_make(J1939_SIGNAL_TYPE_PLAIN, 0U, 32U, 0x7FFFFFFFU, 1U, INT64_MIN);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_check(&sig));
	sig = sig_make(J1939_SIGNAL_TYPE_PLAIN, 0U, 8U, 1U, 1U, INT64_MAX - 255);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_check(&sig));
	sig = sig_make(J1939_SIGNAL_TYPE_PLAIN, 0U, 32U, 0xFFFFFFFFU, 0xFFFFFFFFU, 0);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_check(&sig));
}

static void test_check_rejects_invalid_descriptors(void) {
	const j1939_signal_t good = sig_make(J1939_SIGNAL_TYPE_CONTINUOUS, 8U, 16U, 1U, 8U, -10);
	j1939_signal_t sig;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_check(&good));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_check(NULL));

	sig = good;
	sig.spn = J1939_SPN_MAX + 1U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_check(&sig));
	sig = good;
	sig.pgn = 0x40000U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_check(&sig));
	sig = good;
	sig.pgn = 0xEA01U; /* PDU1 with a destination address */
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_check(&sig));
	sig = good;
	sig.res_num = 0U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_check(&sig));
	sig = good;
	sig.res_den = 0U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_check(&sig));
	sig = good;
	sig.start = (uint16_t)((J1939_SIGNAL_LEN_MAX * 8U) - 15U);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_check(&sig));
	sig = good;
	sig.start = 0xFFFFU;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_check(&sig));
	sig = good;
	sig.type = (j1939_signal_type_t)3;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_check(&sig));

	/* Lengths that do not match the type. */
	sig = good;
	sig.type = J1939_SIGNAL_TYPE_PLAIN;
	sig.bits = 0U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_check(&sig));
	sig.bits = 33U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_check(&sig));
	sig = good;
	sig.bits = 12U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_check(&sig));
	sig.bits = 24U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_check(&sig));
	sig.bits = 2U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_check(&sig));
	sig = good;
	sig.type = J1939_SIGNAL_TYPE_DISCRETE;
	sig.bits = 1U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_check(&sig));
	sig.bits = 3U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_check(&sig));
	sig.bits = 8U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_check(&sig));

	/* Scaling beyond INT64_MAX. */
	sig = sig_make(J1939_SIGNAL_TYPE_PLAIN, 0U, 32U, 0xFFFFFFFFU, 1U, INT64_MIN);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_check(&sig));
	sig = sig_make(J1939_SIGNAL_TYPE_PLAIN, 0U, 32U, 0x80000001U, 1U, 0);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_check(&sig));
	sig = sig_make(J1939_SIGNAL_TYPE_PLAIN, 0U, 8U, 1U, 1U, INT64_MAX - 254);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_check(&sig));
	/* 255 / 2 rounds up to 128. */
	sig = sig_make(J1939_SIGNAL_TYPE_PLAIN, 0U, 8U, 1U, 2U, INT64_MAX - 127);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_check(&sig));
	sig.offset = INT64_MAX - 128;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_check(&sig));
}

/* Every descriptor-taking function refuses an invalid descriptor. */
static void test_functions_reject_invalid_descriptor(void) {
	j1939_signal_t bad = sig_make(J1939_SIGNAL_TYPE_CONTINUOUS, 0U, 8U, 0U, 1U, 0);
	const j1939_msg_t msg = {.pgn = 0xFF10U, .prio = 6U, .sa = 0U, .da = 0xFFU, .len = 8U};
	uint8_t data[PAYLOAD_LEN] = {0};
	uint32_t raw = 7U;
	int64_t value = 7;
	j1939_signal_class_t cls = J1939_SIGNAL_RESERVED;
	size_t i;

	for (i = 0U; i < 2U; i++) {
		const j1939_signal_t *sig = (i == 0U) ? NULL : &bad;
		j1939_msg_t m = msg;

		m.data = data;
		TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_raw_get(sig, data, 8U, &raw));
		TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_raw_set(sig, data, 8U, 1U));
		TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_classify(sig, 1U, &cls));
		TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_to_eng(sig, 1U, &value));
		TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_from_eng(sig, 1, &raw));
		TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG,
		                  j1939_signal_decode(sig, data, 8U, &value, &cls));
		TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_encode(sig, data, 8U, 1));
		TEST_ASSERT_EQUAL(
		        J1939_RET_ERR_ARG,
		        j1939_signal_indicator_set(sig, data, 8U, J1939_SIGNAL_NOT_AVAILABLE));
		TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG,
		                  j1939_signal_msg_decode(sig, &m, &value, &cls));
	}
	TEST_ASSERT_EQUAL_HEX32(7U, raw);
	TEST_ASSERT_EQUAL_INT64(7, value);
	TEST_ASSERT_EQUAL(J1939_SIGNAL_RESERVED, cls);
	TEST_ASSERT_EACH_EQUAL_HEX8(0U, data, PAYLOAD_LEN);
}

static void test_raw_get_set(void) {
	const j1939_signal_t sig =
	        sig_make(J1939_SIGNAL_TYPE_PLAIN, J1939_SIGNAL_POS(2U, 5U), 10U, 1U, 1U, 0);
	uint8_t data[PAYLOAD_LEN];
	uint32_t raw = 0U;

	memset(data, 0xFF, sizeof(data));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_raw_set(&sig, data, PAYLOAD_LEN, 0x2A5U));
	TEST_ASSERT_EQUAL_HEX8(0xFFU, data[0]);
	TEST_ASSERT_EQUAL_HEX8(0x5FU, data[1]);
	TEST_ASSERT_EQUAL_HEX8(0xEAU, data[2]);
	TEST_ASSERT_EQUAL_HEX8(0xFFU, data[3]);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_raw_get(&sig, data, PAYLOAD_LEN, &raw));
	TEST_ASSERT_EQUAL_HEX32(0x2A5U, raw);

	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_raw_set(&sig, data, PAYLOAD_LEN, 0x400U));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_raw_set(&sig, data, 2U, 0U));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_raw_get(&sig, data, 2U, &raw));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_raw_get(&sig, data, 3U, &raw));
}

static void check_class(const j1939_signal_t *sig, uint32_t raw, j1939_signal_class_t expect) {
	j1939_signal_class_t cls = (j1939_signal_class_t)99;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_classify(sig, raw, &cls));
	TEST_ASSERT_EQUAL_MESSAGE(expect, cls, "class");
}

static void test_classify_continuous(void) {
	j1939_signal_t sig = sig_make(J1939_SIGNAL_TYPE_CONTINUOUS, 0U, 8U, 1U, 1U, 0);
	uint32_t raw;

	for (raw = 0U; raw <= 0xFFU; raw++) {
		j1939_signal_class_t expect = J1939_SIGNAL_VALID;

		if (raw == 0xFBU) {
			expect = J1939_SIGNAL_PARAM_SPECIFIC;
		} else if ((raw == 0xFCU) || (raw == 0xFDU)) {
			expect = J1939_SIGNAL_RESERVED;
		} else if (raw == 0xFEU) {
			expect = J1939_SIGNAL_ERROR;
		} else if (raw == 0xFFU) {
			expect = J1939_SIGNAL_NOT_AVAILABLE;
		} else {
			/* valid */
		}
		check_class(&sig, raw, expect);
	}

	sig.bits = 16U;
	check_class(&sig, 0x0000U, J1939_SIGNAL_VALID);
	check_class(&sig, 0xFAFFU, J1939_SIGNAL_VALID);
	check_class(&sig, 0xFB00U, J1939_SIGNAL_PARAM_SPECIFIC);
	check_class(&sig, 0xFBFFU, J1939_SIGNAL_PARAM_SPECIFIC);
	check_class(&sig, 0xFC00U, J1939_SIGNAL_RESERVED);
	check_class(&sig, 0xFDFFU, J1939_SIGNAL_RESERVED);
	check_class(&sig, 0xFE00U, J1939_SIGNAL_ERROR);
	check_class(&sig, 0xFEFFU, J1939_SIGNAL_ERROR);
	check_class(&sig, 0xFF00U, J1939_SIGNAL_NOT_AVAILABLE);
	check_class(&sig, 0xFFFFU, J1939_SIGNAL_NOT_AVAILABLE);

	sig.bits = 32U;
	check_class(&sig, 0x00000000U, J1939_SIGNAL_VALID);
	check_class(&sig, 0xFAFFFFFFU, J1939_SIGNAL_VALID);
	check_class(&sig, 0xFB000000U, J1939_SIGNAL_PARAM_SPECIFIC);
	check_class(&sig, 0xFBFFFFFFU, J1939_SIGNAL_PARAM_SPECIFIC);
	check_class(&sig, 0xFC000000U, J1939_SIGNAL_RESERVED);
	check_class(&sig, 0xFDFFFFFFU, J1939_SIGNAL_RESERVED);
	check_class(&sig, 0xFE000000U, J1939_SIGNAL_ERROR);
	check_class(&sig, 0xFEFFFFFFU, J1939_SIGNAL_ERROR);
	check_class(&sig, 0xFF000000U, J1939_SIGNAL_NOT_AVAILABLE);
	check_class(&sig, 0xFFFFFFFFU, J1939_SIGNAL_NOT_AVAILABLE);
}

static void test_classify_discrete_and_plain(void) {
	j1939_signal_t sig = sig_make(J1939_SIGNAL_TYPE_DISCRETE, 0U, 2U, 1U, 1U, 0);
	j1939_signal_class_t cls = J1939_SIGNAL_RESERVED;

	check_class(&sig, 0U, J1939_SIGNAL_VALID);
	check_class(&sig, 1U, J1939_SIGNAL_VALID);
	check_class(&sig, 2U, J1939_SIGNAL_ERROR);
	check_class(&sig, 3U, J1939_SIGNAL_NOT_AVAILABLE);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_classify(&sig, 4U, &cls));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_classify(&sig, 0U, NULL));
	TEST_ASSERT_EQUAL(J1939_SIGNAL_RESERVED, cls);

	sig = sig_make(J1939_SIGNAL_TYPE_PLAIN, 0U, 8U, 1U, 1U, 0);
	check_class(&sig, 0U, J1939_SIGNAL_VALID);
	check_class(&sig, 0xFEU, J1939_SIGNAL_VALID);
	check_class(&sig, 0xFFU, J1939_SIGNAL_VALID);
	sig.bits = 32U;
	check_class(&sig, 0xFFFFFFFFU, J1939_SIGNAL_VALID);
}

static int64_t eng(const j1939_signal_t *sig, uint32_t raw) {
	int64_t value = 0x5A5A5A5A;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_to_eng(sig, raw, &value));
	return value;
}

static uint32_t raw_of(const j1939_signal_t *sig, int64_t value) {
	uint32_t raw = 0xDEADBEEFU;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_from_eng(sig, value, &raw));
	return raw;
}

static void from_eng_fails(const j1939_signal_t *sig, int64_t value) {
	uint32_t raw = 0xDEADBEEFU;

	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_from_eng(sig, value, &raw));
	TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFU, raw);
}

static void test_scaling_rounding(void) {
	/* 0.125 per bit decoded to whole units: halves round up. */
	j1939_signal_t sig = sig_make(J1939_SIGNAL_TYPE_CONTINUOUS, 0U, 16U, 1U, 8U, 0);

	TEST_ASSERT_EQUAL_INT64(0, eng(&sig, 0U));
	TEST_ASSERT_EQUAL_INT64(0, eng(&sig, 3U));  /* 0.375 */
	TEST_ASSERT_EQUAL_INT64(1, eng(&sig, 4U));  /* 0.5 */
	TEST_ASSERT_EQUAL_INT64(1, eng(&sig, 11U)); /* 1.375 */
	TEST_ASSERT_EQUAL_INT64(2, eng(&sig, 12U)); /* 1.5 */
	TEST_ASSERT_EQUAL_INT64(1000, eng(&sig, 8000U));
	TEST_ASSERT_EQUAL_INT64(8192, eng(&sig, 0xFFFFU)); /* 8191.875 */
	TEST_ASSERT_EQUAL_HEX32(8000U, raw_of(&sig, 1000));
	TEST_ASSERT_EQUAL_HEX32(0U, raw_of(&sig, 0));
	TEST_ASSERT_EQUAL_HEX32(0xFAF8U, raw_of(&sig, 8031)); /* largest valid whole value */
	from_eng_fails(&sig, 8032);                           /* 0xFB00: indicator range */
	from_eng_fails(&sig, -1);

	/* 2 units per bit: engineering halves between raw steps round up. */
	sig = sig_make(J1939_SIGNAL_TYPE_CONTINUOUS, 0U, 8U, 2U, 1U, 0);
	TEST_ASSERT_EQUAL_HEX32(1U, raw_of(&sig, 1));      /* 0.5 */
	TEST_ASSERT_EQUAL_HEX32(2U, raw_of(&sig, 3));      /* 1.5 */
	TEST_ASSERT_EQUAL_HEX32(0U, raw_of(&sig, -1));     /* -0.5 */
	from_eng_fails(&sig, -2);                          /* -1 */
	TEST_ASSERT_EQUAL_HEX32(0xFAU, raw_of(&sig, 499)); /* 249.5 */
	TEST_ASSERT_EQUAL_HEX32(0xFAU, raw_of(&sig, 500));
	from_eng_fails(&sig, 501); /* 250.5 rounds to 0xFB, an indicator */

	/* 3 units per bit: thirds below the offset. */
	sig = sig_make(J1939_SIGNAL_TYPE_PLAIN, 0U, 8U, 3U, 1U, 10);
	TEST_ASSERT_EQUAL_HEX32(0U, raw_of(&sig, 9));  /* -1/3 */
	from_eng_fails(&sig, 8);                       /* -2/3 */
	TEST_ASSERT_EQUAL_HEX32(1U, raw_of(&sig, 12)); /* 2/3 */
	TEST_ASSERT_EQUAL_HEX32(0U, raw_of(&sig, 11)); /* 1/3 */
}

static void test_scaling_negative_offset(void) {
	/* 1 per bit, offset -40 (a temperature). */
	j1939_signal_t sig = sig_make(J1939_SIGNAL_TYPE_CONTINUOUS, 0U, 8U, 1U, 1U, -40);

	TEST_ASSERT_EQUAL_INT64(-40, eng(&sig, 0U));
	TEST_ASSERT_EQUAL_INT64(0, eng(&sig, 40U));
	TEST_ASSERT_EQUAL_INT64(210, eng(&sig, 0xFAU));
	TEST_ASSERT_EQUAL_HEX32(0U, raw_of(&sig, -40));
	TEST_ASSERT_EQUAL_HEX32(0xFAU, raw_of(&sig, 210));
	from_eng_fails(&sig, -41);
	from_eng_fails(&sig, 211);

	/* 0.03125 per bit, offset -273: rounding is half up on the true value. */
	sig = sig_make(J1939_SIGNAL_TYPE_CONTINUOUS, 0U, 16U, 1U, 32U, -273);
	TEST_ASSERT_EQUAL_INT64(-273, eng(&sig, 0U));
	TEST_ASSERT_EQUAL_INT64(0, eng(&sig, 8736U));
	TEST_ASSERT_EQUAL_INT64(0, eng(&sig, 8720U));      /* -0.5 */
	TEST_ASSERT_EQUAL_INT64(-1, eng(&sig, 8719U));     /* -0.53125 */
	TEST_ASSERT_EQUAL_INT64(1, eng(&sig, 8752U));      /* 0.5 */
	TEST_ASSERT_EQUAL_INT64(1735, eng(&sig, 0xFAFFU)); /* 1734.96875 */
	TEST_ASSERT_EQUAL_HEX32(8736U, raw_of(&sig, 0));
	TEST_ASSERT_EQUAL_HEX32(0xFAE0U, raw_of(&sig, 1734));
	from_eng_fails(&sig, 1735);
	from_eng_fails(&sig, -274);

	/* Fine engineering unit: 1/128 per bit to millis, offset -250000. */
	sig = sig_make(J1939_SIGNAL_TYPE_CONTINUOUS, 0U, 16U, 125U, 16U, -250000);
	TEST_ASSERT_EQUAL_INT64(-250000, eng(&sig, 0U));
	TEST_ASSERT_EQUAL_INT64(-249992, eng(&sig, 1U)); /* -249992.1875 */
	TEST_ASSERT_EQUAL_INT64(0, eng(&sig, 32000U));
	TEST_ASSERT_EQUAL_INT64(-8, eng(&sig, 31999U)); /* -7.8125 */
	TEST_ASSERT_EQUAL_HEX32(32000U, raw_of(&sig, 0));
	TEST_ASSERT_EQUAL_HEX32(31999U, raw_of(&sig, -8));
	TEST_ASSERT_EQUAL_HEX32(31999U, raw_of(&sig, -4));  /* 31999.488 */
	TEST_ASSERT_EQUAL_HEX32(31998U, raw_of(&sig, -12)); /* 31998.464 */
}

static void test_scaling_round_trips(void) {
	/* Integer resolution: every raw value survives decode and encode. */
	j1939_signal_t sig = sig_make(J1939_SIGNAL_TYPE_CONTINUOUS, 0U, 16U, 5U, 1U, -1000);
	uint32_t raw;
	int64_t v;

	for (raw = 0U; raw <= 0xFAFFU; raw++) {
		TEST_ASSERT_EQUAL_HEX32(raw, raw_of(&sig, eng(&sig, raw)));
	}

	/* Resolution finer than the unit: every value survives encode and decode. */
	sig = sig_make(J1939_SIGNAL_TYPE_CONTINUOUS, 0U, 16U, 1U, 32U, -273);
	for (v = -273; v <= 1734; v++) {
		TEST_ASSERT_EQUAL_INT64(v, eng(&sig, raw_of(&sig, v)));
	}

	/* Resolution coarser than the unit: raw values survive, values land within half a step. */
	sig = sig_make(J1939_SIGNAL_TYPE_PLAIN, 0U, 10U, 125U, 16U, -3000);
	for (raw = 0U; raw <= 0x3FFU; raw++) {
		TEST_ASSERT_EQUAL_HEX32(raw, raw_of(&sig, eng(&sig, raw)));
	}
	for (v = -3000; v <= 4990; v++) {
		TEST_ASSERT_INT64_WITHIN(4, v, eng(&sig, raw_of(&sig, v)));
	}
}

static void test_scaling_extremes(void) {
	/* The full 32-bit range with a large resolution. */
	j1939_signal_t sig = sig_make(J1939_SIGNAL_TYPE_PLAIN, 0U, 32U, 1000U, 1U, 0);

	TEST_ASSERT_EQUAL_INT64(4294967295000LL, eng(&sig, 0xFFFFFFFFU));
	TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFU, raw_of(&sig, 4294967295000LL));
	from_eng_fails(&sig, 4294967295500LL); /* rounds to 2^32 */
	TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFU, raw_of(&sig, 4294967295499LL));
	from_eng_fails(&sig, INT64_MAX);
	from_eng_fails(&sig, INT64_MIN);

	/* Largest valid scaling reaches INT64_MAX exactly. */
	sig = sig_make(J1939_SIGNAL_TYPE_PLAIN, 0U, 8U, 1U, 1U, INT64_MAX - 255);
	TEST_ASSERT_EQUAL_INT64(INT64_MAX, eng(&sig, 0xFFU));
	TEST_ASSERT_EQUAL_HEX32(0xFFU, raw_of(&sig, INT64_MAX));
	from_eng_fails(&sig, INT64_MIN);
	from_eng_fails(&sig, 0);

	/* Offset INT64_MIN: the difference to INT64_MAX is 2^64 - 1. */
	sig = sig_make(J1939_SIGNAL_TYPE_PLAIN, 0U, 32U, 0x7FFFFFFFU, 1U, INT64_MIN);
	TEST_ASSERT_EQUAL_INT64(INT64_MIN, eng(&sig, 0U));
	TEST_ASSERT_EQUAL_INT64(INT64_MIN + (int64_t)0x7FFFFFFE80000001LL, eng(&sig, 0xFFFFFFFFU));
	TEST_ASSERT_EQUAL_HEX32(0U, raw_of(&sig, INT64_MIN));
	TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFU,
	                        raw_of(&sig, INT64_MIN + (int64_t)0x7FFFFFFE80000001LL));
	from_eng_fails(&sig, INT64_MAX);
	/* The product with res_den overflows 64 bits. */
	sig.res_den = 2U;
	from_eng_fails(&sig, INT64_MAX);
	sig.offset = INT64_MAX - 0x80000000LL;
	sig.res_num = 1U;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_check(&sig));
	from_eng_fails(&sig, INT64_MIN);

	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_from_eng(&sig, 0, NULL));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_to_eng(&sig, 0U, NULL));
	sig.bits = 8U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_to_eng(&sig, 0x100U, &(int64_t){0}));
}

static void test_decode(void) {
	const j1939_signal_t sig =
	        sig_make(J1939_SIGNAL_TYPE_CONTINUOUS, J1939_SIGNAL_POS(3U, 1U), 16U, 1U, 8U, 0);
	uint8_t data[PAYLOAD_LEN] = {0xFFU, 0xFFU, 0x40U, 0x1FU, 0xFFU, 0xFFU, 0xFFU, 0xFFU};
	int64_t value = 0;
	j1939_signal_class_t cls = J1939_SIGNAL_RESERVED;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_decode(&sig, data, PAYLOAD_LEN, &value, &cls));
	TEST_ASSERT_EQUAL(J1939_SIGNAL_VALID, cls);
	TEST_ASSERT_EQUAL_INT64(1000, value);

	/* Indicators leave the value unchanged. */
	data[3] = 0xFEU;
	value = 42;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_decode(&sig, data, PAYLOAD_LEN, &value, &cls));
	TEST_ASSERT_EQUAL(J1939_SIGNAL_ERROR, cls);
	TEST_ASSERT_EQUAL_INT64(42, value);
	data[3] = 0xFFU;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_decode(&sig, data, PAYLOAD_LEN, &value, &cls));
	TEST_ASSERT_EQUAL(J1939_SIGNAL_NOT_AVAILABLE, cls);
	TEST_ASSERT_EQUAL_INT64(42, value);

	cls = J1939_SIGNAL_RESERVED;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_decode(&sig, NULL, 8U, &value, &cls));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_decode(&sig, data, 8U, NULL, &cls));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_decode(&sig, data, 8U, &value, NULL));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_decode(&sig, data, 3U, &value, &cls));
	TEST_ASSERT_EQUAL(J1939_SIGNAL_RESERVED, cls);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_decode(&sig, data, 4U, &value, &cls));
}

static void test_encode(void) {
	const j1939_signal_t sig =
	        sig_make(J1939_SIGNAL_TYPE_CONTINUOUS, J1939_SIGNAL_POS(2U, 5U), 16U, 1U, 1U, -40);
	uint8_t data[PAYLOAD_LEN];
	uint8_t before[PAYLOAD_LEN];
	uint32_t raw = 0U;

	memset(data, 0xFF, sizeof(data));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_encode(&sig, data, PAYLOAD_LEN, 0x1234 - 40));
	TEST_ASSERT_EQUAL_HEX8(0xFFU, data[0]);
	TEST_ASSERT_EQUAL_HEX8(0x4FU, data[1]);
	TEST_ASSERT_EQUAL_HEX8(0x23U, data[2]);
	TEST_ASSERT_EQUAL_HEX8(0xF1U, data[3]);
	TEST_ASSERT_EQUAL_HEX8(0xFFU, data[4]);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_raw_get(&sig, data, PAYLOAD_LEN, &raw));
	TEST_ASSERT_EQUAL_HEX32(0x1234U, raw);

	memcpy(before, data, sizeof(data));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_encode(&sig, data, PAYLOAD_LEN, -41));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG,
	                  j1939_signal_encode(&sig, data, PAYLOAD_LEN, 0xFB00 - 40));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_encode(&sig, data, 3U, 0));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_encode(&sig, NULL, PAYLOAD_LEN, 0));
	TEST_ASSERT_EQUAL_HEX8_ARRAY(before, data, PAYLOAD_LEN);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_encode(&sig, data, 4U, 0xFAFF - 40));
	TEST_ASSERT_EQUAL_HEX8(0xFFU, data[1]);
	TEST_ASSERT_EQUAL_HEX8(0xAFU, data[2]);
	TEST_ASSERT_EQUAL_HEX8(0xFFU, data[3]);
}

static void test_indicator_set(void) {
	static const uint8_t lens[] = {8U, 16U, 32U};
	static const uint32_t errors[] = {0xFEU, 0xFEFFU, 0xFEFFFFFFU};
	uint8_t data[PAYLOAD_LEN];
	uint8_t before[PAYLOAD_LEN];
	uint8_t expect[PAYLOAD_LEN];
	uint32_t raw = 0U;
	size_t i;

	for (i = 0U; i < sizeof(lens); i++) {
		const j1939_signal_t sig =
		        sig_make(J1939_SIGNAL_TYPE_CONTINUOUS, 12U, lens[i], 1U, 1U, 0);

		memset(data, 0, sizeof(data));
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_indicator_set(&sig, data, PAYLOAD_LEN,
		                                                           J1939_SIGNAL_ERROR));
		TEST_ASSERT_EQUAL(J1939_RET_OK,
		                  j1939_signal_raw_get(&sig, data, PAYLOAD_LEN, &raw));
		TEST_ASSERT_EQUAL_HEX32(errors[i], raw);
		check_class(&sig, raw, J1939_SIGNAL_ERROR);
		TEST_ASSERT_EQUAL_HEX8(0U, data[0]);
		TEST_ASSERT_EQUAL(J1939_RET_OK,
		                  j1939_signal_indicator_set(&sig, data, PAYLOAD_LEN,
		                                             J1939_SIGNAL_NOT_AVAILABLE));
		TEST_ASSERT_EQUAL(J1939_RET_OK,
		                  j1939_signal_raw_get(&sig, data, PAYLOAD_LEN, &raw));
		TEST_ASSERT_EQUAL_HEX32(max_of(lens[i]), raw);
		check_class(&sig, raw, J1939_SIGNAL_NOT_AVAILABLE);
		memset(expect, 0, sizeof(expect));
		ref_set(expect, 12U, lens[i], max_of(lens[i]));
		TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, data, PAYLOAD_LEN);
	}

	{
		const j1939_signal_t sig = sig_make(J1939_SIGNAL_TYPE_DISCRETE, 2U, 2U, 1U, 1U, 0);

		memset(data, 0, sizeof(data));
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_indicator_set(&sig, data, PAYLOAD_LEN,
		                                                           J1939_SIGNAL_ERROR));
		TEST_ASSERT_EQUAL_HEX8(0x08U, data[0]);
		TEST_ASSERT_EQUAL(J1939_RET_OK,
		                  j1939_signal_indicator_set(&sig, data, PAYLOAD_LEN,
		                                             J1939_SIGNAL_NOT_AVAILABLE));
		TEST_ASSERT_EQUAL_HEX8(0x0CU, data[0]);

		memcpy(before, data, sizeof(data));
		TEST_ASSERT_EQUAL(
		        J1939_RET_ERR_ARG,
		        j1939_signal_indicator_set(&sig, data, PAYLOAD_LEN, J1939_SIGNAL_VALID));
		TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG,
		                  j1939_signal_indicator_set(&sig, data, PAYLOAD_LEN,
		                                             J1939_SIGNAL_PARAM_SPECIFIC));
		TEST_ASSERT_EQUAL(
		        J1939_RET_ERR_ARG,
		        j1939_signal_indicator_set(&sig, data, PAYLOAD_LEN, J1939_SIGNAL_RESERVED));
		TEST_ASSERT_EQUAL(
		        J1939_RET_ERR_ARG,
		        j1939_signal_indicator_set(&sig, NULL, PAYLOAD_LEN, J1939_SIGNAL_ERROR));
		TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG,
		                  j1939_signal_indicator_set(&sig, data, 0U, J1939_SIGNAL_ERROR));
		TEST_ASSERT_EQUAL_HEX8_ARRAY(before, data, PAYLOAD_LEN);
	}

	{
		const j1939_signal_t sig = sig_make(J1939_SIGNAL_TYPE_PLAIN, 0U, 8U, 1U, 1U, 0);

		memset(data, 0, sizeof(data));
		TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG,
		                  j1939_signal_indicator_set(&sig, data, PAYLOAD_LEN,
		                                             J1939_SIGNAL_NOT_AVAILABLE));
		TEST_ASSERT_EACH_EQUAL_HEX8(0U, data, PAYLOAD_LEN);
	}
}

static void test_msg_decode(void) {
	const j1939_signal_t sig =
	        sig_make(J1939_SIGNAL_TYPE_CONTINUOUS, J1939_SIGNAL_POS(8U, 1U), 8U, 1U, 1U, -40);
	const uint8_t data[PAYLOAD_LEN] = {0U, 0U, 0U, 0U, 0U, 0U, 0U, 100U};
	j1939_msg_t msg = {.pgn = 0xFF10U, .prio = 6U, .sa = 0x21U, .da = 0xFFU, .len = 8U};
	int64_t value = 0;
	j1939_signal_class_t cls = J1939_SIGNAL_RESERVED;

	msg.data = data;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_msg_decode(&sig, &msg, &value, &cls));
	TEST_ASSERT_EQUAL(J1939_SIGNAL_VALID, cls);
	TEST_ASSERT_EQUAL_INT64(60, value);

	value = 0;
	cls = J1939_SIGNAL_RESERVED;
	msg.pgn = 0xFF11U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_msg_decode(&sig, &msg, &value, &cls));
	msg.pgn = 0xFF10U;
	msg.len = 7U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_msg_decode(&sig, &msg, &value, &cls));
	msg.len = 8U;
	msg.data = NULL;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_msg_decode(&sig, &msg, &value, &cls));
	msg.data = data;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_msg_decode(&sig, NULL, &value, &cls));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_msg_decode(&sig, &msg, NULL, &cls));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_signal_msg_decode(&sig, &msg, &value, NULL));
	TEST_ASSERT_EQUAL_INT64(0, value);
	TEST_ASSERT_EQUAL(J1939_SIGNAL_RESERVED, cls);

	/* Longer messages are accepted. */
	msg.len = 9U;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_signal_msg_decode(&sig, &msg, &value, &cls));
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_pos_macro);
	RUN_TEST(test_bits_known_values);
	RUN_TEST(test_bits_set_known_values);
	RUN_TEST(test_bits_round_trip_exhaustive);
	RUN_TEST(test_bits_rejects_invalid_arguments);
	RUN_TEST(test_check_accepts_valid_descriptors);
	RUN_TEST(test_check_rejects_invalid_descriptors);
	RUN_TEST(test_functions_reject_invalid_descriptor);
	RUN_TEST(test_raw_get_set);
	RUN_TEST(test_classify_continuous);
	RUN_TEST(test_classify_discrete_and_plain);
	RUN_TEST(test_scaling_rounding);
	RUN_TEST(test_scaling_negative_offset);
	RUN_TEST(test_scaling_round_trips);
	RUN_TEST(test_scaling_extremes);
	RUN_TEST(test_decode);
	RUN_TEST(test_encode);
	RUN_TEST(test_indicator_set);
	RUN_TEST(test_msg_decode);
	return UNITY_END();
}
