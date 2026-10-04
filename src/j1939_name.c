/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_name.c
 * @brief NAME codec (J1939/81): fields, byte order and arbitration order.
 */

#include "j1939/j1939_name.h"

#include <stddef.h>

/** @name Bit positions of the NAME fields
 * @{ */
#define IDENTITY_SHIFT                0U  /**< Identity number. */
#define MANUFACTURER_SHIFT            21U /**< Manufacturer code. */
#define ECU_INSTANCE_SHIFT            32U /**< ECU instance. */
#define FUNCTION_INSTANCE_SHIFT       35U /**< Function instance. */
#define FUNCTION_SHIFT                40U /**< Function. */
#define RESERVED_SHIFT                48U /**< Reserved bit. */
#define VEHICLE_SYSTEM_SHIFT          49U /**< Vehicle system. */
#define VEHICLE_SYSTEM_INSTANCE_SHIFT 56U /**< Vehicle system instance. */
#define INDUSTRY_GROUP_SHIFT          60U /**< Industry group. */
#define ARBITRARY_ADDRESS_SHIFT       63U /**< Arbitrary address capable bit. */
/** @} */

#define BYTE_MASK  0xFFU /**< Mask of one byte. */
#define BYTE_SHIFT 8U    /**< Bits per byte. */

/**
 * @brief Places a field value at its position in the NAME.
 * @param value  Field value, already range checked.
 * @param shift  Bit position of the field.
 * @return The value shifted into place.
 */
static uint64_t field_put(uint32_t value, uint32_t shift) {
	return (uint64_t)value << shift;
}

/**
 * @brief Extracts a field from a NAME.
 * @param name   NAME.
 * @param shift  Bit position of the field.
 * @param max    Largest field value, an all-ones mask.
 * @return The field value.
 */
static uint32_t field_get(uint64_t name, uint32_t shift, uint32_t max) {
	return (uint32_t)((name >> shift) & (uint64_t)max);
}

/**
 * @brief Checks every NAME field against its range.
 * @param f  Fields.
 * @return true if all fields fit their bit widths.
 */
static bool fields_valid(const j1939_name_fields_t *f) {
	return (f->industry_group <= J1939_NAME_INDUSTRY_GROUP_MAX) &&
	       (f->vehicle_system_instance <= J1939_NAME_VEHICLE_SYSTEM_INSTANCE_MAX) &&
	       (f->vehicle_system <= J1939_NAME_VEHICLE_SYSTEM_MAX) &&
	       (f->reserved <= J1939_NAME_RESERVED_MAX) &&
	       (f->function_instance <= J1939_NAME_FUNCTION_INSTANCE_MAX) &&
	       (f->ecu_instance <= J1939_NAME_ECU_INSTANCE_MAX) &&
	       (f->manufacturer <= J1939_NAME_MANUFACTURER_MAX) &&
	       (f->identity <= J1939_NAME_IDENTITY_MAX);
}

j1939_ret_t j1939_name_encode(const j1939_name_fields_t *fields, uint64_t *name) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((fields != NULL) && (name != NULL) && fields_valid(fields)) {
		uint32_t arbitrary = fields->arbitrary_address ? 1U : 0U;

		*name = field_put(arbitrary, ARBITRARY_ADDRESS_SHIFT) |
		        field_put(fields->industry_group, INDUSTRY_GROUP_SHIFT) |
		        field_put(fields->vehicle_system_instance, VEHICLE_SYSTEM_INSTANCE_SHIFT) |
		        field_put(fields->vehicle_system, VEHICLE_SYSTEM_SHIFT) |
		        field_put(fields->reserved, RESERVED_SHIFT) |
		        field_put(fields->function, FUNCTION_SHIFT) |
		        field_put(fields->function_instance, FUNCTION_INSTANCE_SHIFT) |
		        field_put(fields->ecu_instance, ECU_INSTANCE_SHIFT) |
		        field_put(fields->manufacturer, MANUFACTURER_SHIFT) |
		        field_put(fields->identity, IDENTITY_SHIFT);
		ret = J1939_RET_OK;
	}
	return ret;
}

j1939_ret_t j1939_name_decode(uint64_t name, j1939_name_fields_t *fields) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if (fields != NULL) {
		fields->arbitrary_address = field_get(name, ARBITRARY_ADDRESS_SHIFT, 1U) != 0U;
		fields->industry_group = (uint8_t)field_get(name, INDUSTRY_GROUP_SHIFT,
		                                            J1939_NAME_INDUSTRY_GROUP_MAX);
		fields->vehicle_system_instance =
		        (uint8_t)field_get(name, VEHICLE_SYSTEM_INSTANCE_SHIFT,
		                           J1939_NAME_VEHICLE_SYSTEM_INSTANCE_MAX);
		fields->vehicle_system = (uint8_t)field_get(name, VEHICLE_SYSTEM_SHIFT,
		                                            J1939_NAME_VEHICLE_SYSTEM_MAX);
		fields->reserved =
		        (uint8_t)field_get(name, RESERVED_SHIFT, J1939_NAME_RESERVED_MAX);
		fields->function =
		        (uint8_t)field_get(name, FUNCTION_SHIFT, J1939_NAME_FUNCTION_MAX);
		fields->function_instance = (uint8_t)field_get(name, FUNCTION_INSTANCE_SHIFT,
		                                               J1939_NAME_FUNCTION_INSTANCE_MAX);
		fields->ecu_instance =
		        (uint8_t)field_get(name, ECU_INSTANCE_SHIFT, J1939_NAME_ECU_INSTANCE_MAX);
		fields->manufacturer =
		        (uint16_t)field_get(name, MANUFACTURER_SHIFT, J1939_NAME_MANUFACTURER_MAX);
		fields->identity = field_get(name, IDENTITY_SHIFT, J1939_NAME_IDENTITY_MAX);
		ret = J1939_RET_OK;
	}
	return ret;
}

bool j1939_name_arbitrary_address(uint64_t name) {
	return field_get(name, ARBITRARY_ADDRESS_SHIFT, 1U) != 0U;
}

int32_t j1939_name_compare(uint64_t a, uint64_t b) {
	int32_t result = 0;

	if (a < b) {
		result = -1;
	} else if (a > b) {
		result = 1;
	} else {
		/* Equal NAMEs. */
	}
	return result;
}

j1939_ret_t j1939_name_to_bytes(uint64_t name, uint8_t *data) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if (data != NULL) {
		uint64_t rest = name;
		uint32_t i;

		for (i = 0U; i < J1939_NAME_LEN; i++) {
			data[i] = (uint8_t)(rest & BYTE_MASK);
			rest >>= BYTE_SHIFT;
		}
		ret = J1939_RET_OK;
	}
	return ret;
}

j1939_ret_t j1939_name_from_bytes(const uint8_t *data, uint64_t *name) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((data != NULL) && (name != NULL)) {
		uint64_t value = 0U;
		uint32_t i;

		/* Most significant byte first. */
		for (i = J1939_NAME_LEN; i > 0U; i--) {
			value = (value << BYTE_SHIFT) | (uint64_t)data[i - 1U];
		}
		*name = value;
		ret = J1939_RET_OK;
	}
	return ret;
}
