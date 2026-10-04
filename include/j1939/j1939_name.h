/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_name.h
 * @brief J1939/81 NAME codec.
 */

#ifndef J1939_NAME_H
#define J1939_NAME_H

#include <stdbool.h>
#include <stdint.h>

#include "j1939/j1939_ret.h"

/**
 * @addtogroup grp_name
 *
 * The NAME is a 64-bit value that identifies a Controller Application.
 * Field layout (bit 63 is the most significant):
 *
 * | Bits  | Field                          | Width |
 * | ----- | ------------------------------ | ----- |
 * | 63    | Arbitrary address capable      | 1     |
 * | 62-60 | Industry group                 | 3     |
 * | 59-56 | Vehicle system instance        | 4     |
 * | 55-49 | Vehicle system                 | 7     |
 * | 48    | Reserved                       | 1     |
 * | 47-40 | Function                       | 8     |
 * | 39-35 | Function instance              | 5     |
 * | 34-32 | ECU instance                   | 3     |
 * | 31-21 | Manufacturer code              | 11    |
 * | 20-0  | Identity number                | 21    |
 *
 * On the bus the NAME is sent as 8 bytes, least significant byte first.
 * In address arbitration the numerically lower NAME has the higher priority.
 *
 * All functions are pure.
 *
 * @{
 */

#define J1939_NAME_LEN 8U /**< Bytes of a NAME on the bus. */

#define J1939_NAME_INDUSTRY_GROUP_MAX          7U        /**< Largest industry group. */
#define J1939_NAME_VEHICLE_SYSTEM_INSTANCE_MAX 15U       /**< Largest vehicle system instance. */
#define J1939_NAME_VEHICLE_SYSTEM_MAX          127U      /**< Largest vehicle system. */
#define J1939_NAME_RESERVED_MAX                1U        /**< Largest reserved bit value. */
#define J1939_NAME_FUNCTION_MAX                255U      /**< Largest function. */
#define J1939_NAME_FUNCTION_INSTANCE_MAX       31U       /**< Largest function instance. */
#define J1939_NAME_ECU_INSTANCE_MAX            7U        /**< Largest ECU instance. */
#define J1939_NAME_MANUFACTURER_MAX            0x7FFU    /**< Largest manufacturer code. */
#define J1939_NAME_IDENTITY_MAX                0x1FFFFFU /**< Largest identity number. */

/** NAME fields. */
typedef struct j1939_name_fields {
	bool arbitrary_address;          /**< The CA can select another address itself. */
	uint8_t industry_group;          /**< 0..J1939_NAME_INDUSTRY_GROUP_MAX. */
	uint8_t vehicle_system_instance; /**< 0..J1939_NAME_VEHICLE_SYSTEM_INSTANCE_MAX. */
	uint8_t vehicle_system;          /**< 0..J1939_NAME_VEHICLE_SYSTEM_MAX. */
	uint8_t reserved;                /**< 0..J1939_NAME_RESERVED_MAX; J1939/81 sends 0. */
	uint8_t function;                /**< 0..J1939_NAME_FUNCTION_MAX. */
	uint8_t function_instance;       /**< 0..J1939_NAME_FUNCTION_INSTANCE_MAX. */
	uint8_t ecu_instance;            /**< 0..J1939_NAME_ECU_INSTANCE_MAX. */
	uint16_t manufacturer;           /**< 0..J1939_NAME_MANUFACTURER_MAX. */
	uint32_t identity;               /**< 0..J1939_NAME_IDENTITY_MAX. */
} j1939_name_fields_t;

/**
 * @brief Builds a NAME from its fields.
 *
 * @param fields  Field values.
 * @param name    Built NAME. Written only on success.
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG if a field is out of range or a
 *         pointer is NULL.
 */
j1939_ret_t j1939_name_encode(const j1939_name_fields_t *fields, uint64_t *name);

/**
 * @brief Splits a NAME into its fields.
 *
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG if @p fields is NULL.
 */
j1939_ret_t j1939_name_decode(uint64_t name, j1939_name_fields_t *fields);

/** @return true if the arbitrary address capable bit of @p name is set. */
bool j1939_name_arbitrary_address(uint64_t name);

/**
 * @brief Compares the arbitration priority of two NAMEs.
 *
 * @return Negative if @p a has the higher priority (is numerically lower),
 *         positive if @p b has, 0 if the NAMEs are equal.
 */
int32_t j1939_name_compare(uint64_t a, uint64_t b);

/**
 * @brief Writes the bus form of a NAME: J1939_NAME_LEN bytes, least significant first.
 *
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG if @p data is NULL.
 */
j1939_ret_t j1939_name_to_bytes(uint64_t name, uint8_t *data);

/**
 * @brief Reads a NAME from its bus form.
 *
 * @param data  J1939_NAME_LEN bytes, least significant first.
 * @param name  NAME. Written only on success.
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG if a pointer is NULL.
 */
j1939_ret_t j1939_name_from_bytes(const uint8_t *data, uint64_t *name);

/** @} */

#endif /* J1939_NAME_H */
