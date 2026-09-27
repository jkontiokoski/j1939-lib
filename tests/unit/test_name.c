/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

#include "unity.h"

#include "j1939/j1939.h"

void setUp(void) {
}

void tearDown(void) {
}

static j1939_name_fields_t fields_max(void) {
	return (j1939_name_fields_t){
	        .arbitrary_address = true,
	        .industry_group = J1939_NAME_INDUSTRY_GROUP_MAX,
	        .vehicle_system_instance = J1939_NAME_VEHICLE_SYSTEM_INSTANCE_MAX,
	        .vehicle_system = J1939_NAME_VEHICLE_SYSTEM_MAX,
	        .reserved = J1939_NAME_RESERVED_MAX,
	        .function = J1939_NAME_FUNCTION_MAX,
	        .function_instance = J1939_NAME_FUNCTION_INSTANCE_MAX,
	        .ecu_instance = J1939_NAME_ECU_INSTANCE_MAX,
	        .manufacturer = J1939_NAME_MANUFACTURER_MAX,
	        .identity = J1939_NAME_IDENTITY_MAX,
	};
}

static void assert_fields_equal(const j1939_name_fields_t *a, const j1939_name_fields_t *b) {
	TEST_ASSERT_EQUAL(a->arbitrary_address, b->arbitrary_address);
	TEST_ASSERT_EQUAL_UINT8(a->industry_group, b->industry_group);
	TEST_ASSERT_EQUAL_UINT8(a->vehicle_system_instance, b->vehicle_system_instance);
	TEST_ASSERT_EQUAL_UINT8(a->vehicle_system, b->vehicle_system);
	TEST_ASSERT_EQUAL_UINT8(a->reserved, b->reserved);
	TEST_ASSERT_EQUAL_UINT8(a->function, b->function);
	TEST_ASSERT_EQUAL_UINT8(a->function_instance, b->function_instance);
	TEST_ASSERT_EQUAL_UINT8(a->ecu_instance, b->ecu_instance);
	TEST_ASSERT_EQUAL_UINT16(a->manufacturer, b->manufacturer);
	TEST_ASSERT_EQUAL_UINT32(a->identity, b->identity);
}

/* Each field alone at its maximum lands on its documented bits. */
static void test_each_field_has_its_own_bits(void) {
	const j1939_name_fields_t zero = {0};
	j1939_name_fields_t f;
	uint64_t name = 0U;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_name_encode(&zero, &name));
	TEST_ASSERT_EQUAL_HEX64(0U, name);

	f = zero;
	f.arbitrary_address = true;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_name_encode(&f, &name));
	TEST_ASSERT_EQUAL_HEX64(0x8000000000000000U, name);
	f = zero;
	f.industry_group = J1939_NAME_INDUSTRY_GROUP_MAX;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_name_encode(&f, &name));
	TEST_ASSERT_EQUAL_HEX64(0x7000000000000000U, name);
	f = zero;
	f.vehicle_system_instance = J1939_NAME_VEHICLE_SYSTEM_INSTANCE_MAX;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_name_encode(&f, &name));
	TEST_ASSERT_EQUAL_HEX64(0x0F00000000000000U, name);
	f = zero;
	f.vehicle_system = J1939_NAME_VEHICLE_SYSTEM_MAX;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_name_encode(&f, &name));
	TEST_ASSERT_EQUAL_HEX64(0x00FE000000000000U, name);
	f = zero;
	f.reserved = J1939_NAME_RESERVED_MAX;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_name_encode(&f, &name));
	TEST_ASSERT_EQUAL_HEX64(0x0001000000000000U, name);
	f = zero;
	f.function = J1939_NAME_FUNCTION_MAX;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_name_encode(&f, &name));
	TEST_ASSERT_EQUAL_HEX64(0x0000FF0000000000U, name);
	f = zero;
	f.function_instance = J1939_NAME_FUNCTION_INSTANCE_MAX;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_name_encode(&f, &name));
	TEST_ASSERT_EQUAL_HEX64(0x000000F800000000U, name);
	f = zero;
	f.ecu_instance = J1939_NAME_ECU_INSTANCE_MAX;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_name_encode(&f, &name));
	TEST_ASSERT_EQUAL_HEX64(0x0000000700000000U, name);
	f = zero;
	f.manufacturer = J1939_NAME_MANUFACTURER_MAX;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_name_encode(&f, &name));
	TEST_ASSERT_EQUAL_HEX64(0x00000000FFE00000U, name);
	f = zero;
	f.identity = J1939_NAME_IDENTITY_MAX;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_name_encode(&f, &name));
	TEST_ASSERT_EQUAL_HEX64(0x00000000001FFFFFU, name);

	f = fields_max();
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_name_encode(&f, &name));
	TEST_ASSERT_EQUAL_HEX64(0xFFFFFFFFFFFFFFFFU, name);
}

static void test_fields_round_trip(void) {
	const j1939_name_fields_t in = {
	        .arbitrary_address = true,
	        .industry_group = 2U,
	        .vehicle_system_instance = 9U,
	        .vehicle_system = 0x55U,
	        .reserved = 0U,
	        .function = 0x81U,
	        .function_instance = 0x13U,
	        .ecu_instance = 5U,
	        .manufacturer = 0x2A5U,
	        .identity = 0x12345U,
	};
	const j1939_name_fields_t max = fields_max();
	j1939_name_fields_t out;
	uint64_t name = 0U;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_name_encode(&in, &name));
	TEST_ASSERT_EQUAL_HEX64(0xA9AA819D54A12345U, name);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_name_decode(name, &out));
	assert_fields_equal(&in, &out);

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_name_decode(0xFFFFFFFFFFFFFFFFU, &out));
	assert_fields_equal(&max, &out);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_name_decode(0x0123456789ABCDEFU, &out));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_name_encode(&out, &name));
	TEST_ASSERT_EQUAL_HEX64(0x0123456789ABCDEFU, name);
}

static void test_encode_rejects_out_of_range_fields(void) {
	j1939_name_fields_t f;
	uint64_t name = 0x1234U;

	f = fields_max();
	f.industry_group++;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_name_encode(&f, &name));
	f = fields_max();
	f.vehicle_system_instance++;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_name_encode(&f, &name));
	f = fields_max();
	f.vehicle_system++;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_name_encode(&f, &name));
	f = fields_max();
	f.reserved++;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_name_encode(&f, &name));
	f = fields_max();
	f.function_instance++;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_name_encode(&f, &name));
	f = fields_max();
	f.ecu_instance++;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_name_encode(&f, &name));
	f = fields_max();
	f.manufacturer++;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_name_encode(&f, &name));
	f = fields_max();
	f.identity++;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_name_encode(&f, &name));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_name_encode(NULL, &name));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_name_encode(&f, NULL));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_name_decode(0U, NULL));
	TEST_ASSERT_EQUAL_HEX64(0x1234U, name);
}

static void test_arbitrary_address_bit(void) {
	TEST_ASSERT_TRUE(j1939_name_arbitrary_address(0x8000000000000000U));
	TEST_ASSERT_FALSE(j1939_name_arbitrary_address(0x7FFFFFFFFFFFFFFFU));
}

static void test_lower_name_has_priority(void) {
	TEST_ASSERT_LESS_THAN_INT32(0, j1939_name_compare(1U, 2U));
	TEST_ASSERT_GREATER_THAN_INT32(0, j1939_name_compare(2U, 1U));
	TEST_ASSERT_EQUAL_INT32(0, j1939_name_compare(7U, 7U));
	/* Unsigned: a set arbitrary address bit lowers the priority. */
	TEST_ASSERT_LESS_THAN_INT32(0,
	                            j1939_name_compare(0x7FFFFFFFFFFFFFFFU, 0x8000000000000000U));
	TEST_ASSERT_GREATER_THAN_INT32(
	        0, j1939_name_compare(0xFFFFFFFFFFFFFFFFU, 0x0000000000000000U));
}

static void test_bus_form_is_little_endian(void) {
	const uint8_t expected[J1939_NAME_LEN] = {0xEFU, 0xCDU, 0xABU, 0x89U,
	                                          0x67U, 0x45U, 0x23U, 0x81U};
	uint8_t data[J1939_NAME_LEN] = {0};
	uint64_t name = 0U;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_name_to_bytes(0x8123456789ABCDEFU, data));
	TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, data, J1939_NAME_LEN);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_name_from_bytes(data, &name));
	TEST_ASSERT_EQUAL_HEX64(0x8123456789ABCDEFU, name);

	name = 0x55U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_name_to_bytes(0U, NULL));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_name_from_bytes(NULL, &name));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_name_from_bytes(data, NULL));
	TEST_ASSERT_EQUAL_HEX64(0x55U, name);
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_each_field_has_its_own_bits);
	RUN_TEST(test_fields_round_trip);
	RUN_TEST(test_encode_rejects_out_of_range_fields);
	RUN_TEST(test_arbitrary_address_bit);
	RUN_TEST(test_lower_name_has_priority);
	RUN_TEST(test_bus_form_is_little_endian);
	return UNITY_END();
}
