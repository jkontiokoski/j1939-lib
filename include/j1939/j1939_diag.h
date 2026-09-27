/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_diag.h
 * @brief J1939/73 diagnostic message codec: DTCs, lamp status, DM1 and DM2.
 *
 * DM1 (active DTCs) and DM2 (previously active DTCs) share one payload layout:
 *
 * | Bytes    | Content                                                   |
 * | -------- | --------------------------------------------------------- |
 * | 1        | Lamp status: MIL (bits 8-7), red stop (6-5), amber warning (4-3), protect (2-1) |
 * | 2        | Lamp flash, same bit order as byte 1                      |
 * | 3 + 4k.. | DTC k, four bytes each                                    |
 *
 * DTC layout (SPN conversion method 0, "version 4"):
 *
 * | Byte | Bits | Content                                   |
 * | ---- | ---- | ----------------------------------------- |
 * | 1    | 8-1  | SPN bits 8-1                              |
 * | 2    | 8-1  | SPN bits 16-9                             |
 * | 3    | 8-6  | SPN bits 19-17                            |
 * | 3    | 5-1  | FMI                                       |
 * | 4    | 8    | SPN conversion method (CM)                |
 * | 4    | 7-1  | Occurrence count                          |
 *
 * With no DTC to report the payload carries one DTC with every field zero,
 * followed by two 0xFF bytes: eight bytes in all. A payload with one DTC is
 * padded to eight bytes with 0xFF the same way. A payload with two or more
 * DTCs is 2 + 4n bytes long and needs the transport protocol.
 *
 * DM3 and DM11 carry no payload: they are sent as a Request for their PGN.
 *
 * All functions are pure.
 */

#ifndef J1939_DIAG_H
#define J1939_DIAG_H

#include <stdint.h>

#include "j1939/j1939_ret.h"

#define J1939_PGN_DM1  0xFECAU /**< Active diagnostic trouble codes. */
#define J1939_PGN_DM2  0xFECBU /**< Previously active diagnostic trouble codes. */
#define J1939_PGN_DM3  0xFECCU /**< Clear/reset of previously active DTCs. */
#define J1939_PGN_DM11 0xFED3U /**< Clear/reset of active DTCs. */

#define J1939_DIAG_PRIO_DEFAULT 6U /**< Default priority of DM1 and DM2. */

#define J1939_DIAG_DTC_LEN    4U    /**< Encoded length of one DTC. */
#define J1939_DIAG_LAMPS_LEN  2U    /**< Encoded length of the lamp status bytes. */
#define J1939_DIAG_DM_LEN_MIN 8U    /**< Length of a DM1/DM2 payload with 0 or 1 DTC. */
#define J1939_DIAG_DM_LEN_MAX 1785U /**< Largest J1939 message. */
#define J1939_DIAG_DM_DTC_MAX 445U  /**< DTCs that fit in J1939_DIAG_DM_LEN_MAX bytes. */

#define J1939_DIAG_SPN_MAX 0x7FFFFU /**< Largest 19-bit SPN. */
#define J1939_DIAG_FMI_MAX 31U      /**< Largest 5-bit FMI. */
#define J1939_DIAG_OC_MAX  126U     /**< Largest occurrence count; it stays here until cleared. */
#define J1939_DIAG_OC_NA   127U     /**< Occurrence count not available. */

#define J1939_DIAG_CM_V4     0U /**< SPN conversion method 0: version 4 layout. */
#define J1939_DIAG_CM_LEGACY 1U /**< SPN conversion method 1: versions 1-3, not converted. */

/** Lamp status values. 2 is reserved. */
#define J1939_DIAG_LAMP_OFF 0U
#define J1939_DIAG_LAMP_ON  1U
#define J1939_DIAG_LAMP_NA  3U

/** Lamp flash values. */
#define J1939_DIAG_FLASH_SLOW     0U /**< 1 Hz, 50 % duty cycle. */
#define J1939_DIAG_FLASH_FAST     1U /**< 2 Hz or faster, 50 % duty cycle. */
#define J1939_DIAG_FLASH_RESERVED 2U /**< Reserved. */
#define J1939_DIAG_FLASH_OFF      3U /**< Unavailable / do not flash. */

#define J1939_DIAG_LAMP_MAX 3U /**< Largest 2-bit lamp status or flash value. */

/** A diagnostic trouble code. */
typedef struct j1939_diag_dtc {
	uint32_t spn; /**< Suspect parameter number, 0..J1939_DIAG_SPN_MAX. */
	uint8_t fmi;  /**< Failure mode identifier, 0..J1939_DIAG_FMI_MAX. */
	uint8_t oc;   /**< Occurrence count, 0..J1939_DIAG_OC_MAX or J1939_DIAG_OC_NA. */
	uint8_t cm;   /**< SPN conversion method, J1939_DIAG_CM_V4 or J1939_DIAG_CM_LEGACY. */
} j1939_diag_dtc_t;

/** Lamp status (J1939_DIAG_LAMP_*) and flash (J1939_DIAG_FLASH_*) of the four lamps. */
typedef struct j1939_diag_lamps {
	uint8_t mil;                 /**< Malfunction indicator lamp. */
	uint8_t red_stop;            /**< Red stop lamp. */
	uint8_t amber_warning;       /**< Amber warning lamp. */
	uint8_t protect;             /**< Protect lamp. */
	uint8_t mil_flash;           /**< Malfunction indicator lamp flash. */
	uint8_t red_stop_flash;      /**< Red stop lamp flash. */
	uint8_t amber_warning_flash; /**< Amber warning lamp flash. */
	uint8_t protect_flash;       /**< Protect lamp flash. */
} j1939_diag_lamps_t;

/**
 * @brief Encodes a DTC in the version 4 layout.
 *
 * @param dtc  DTC. dtc->cm must be J1939_DIAG_CM_V4.
 * @param buf  J1939_DIAG_DTC_LEN bytes. Written only on success.
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG if a field is out of range or a
 *         pointer is NULL.
 */
j1939_ret_t j1939_diag_dtc_encode(const j1939_diag_dtc_t *dtc, uint8_t *buf);

/**
 * @brief Decodes a DTC.
 *
 * The fields are read at their version 4 positions. When the CM bit is set
 * the sender uses one of the obsolete versions 1-3, which the message does
 * not tell apart: dtc->cm is J1939_DIAG_CM_LEGACY and dtc->spn is not
 * converted; it is the value the version 4 layout gives.
 *
 * @param buf  J1939_DIAG_DTC_LEN bytes.
 * @param dtc  Decoded DTC. Written only on success.
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG if a pointer is NULL.
 */
j1939_ret_t j1939_diag_dtc_decode(const uint8_t *buf, j1939_diag_dtc_t *dtc);

/**
 * @brief Encodes the two lamp status bytes of DM1/DM2.
 *
 * @param lamps  Lamp states, each 0..J1939_DIAG_LAMP_MAX.
 * @param buf    J1939_DIAG_LAMPS_LEN bytes. Written only on success.
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG if a value is out of range or a
 *         pointer is NULL.
 */
j1939_ret_t j1939_diag_lamps_encode(const j1939_diag_lamps_t *lamps, uint8_t *buf);

/**
 * @brief Decodes the two lamp status bytes of DM1/DM2.
 *
 * @param buf    J1939_DIAG_LAMPS_LEN bytes.
 * @param lamps  Decoded lamp states. Written only on success.
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG if a pointer is NULL.
 */
j1939_ret_t j1939_diag_lamps_decode(const uint8_t *buf, j1939_diag_lamps_t *lamps);

/**
 * @brief Builds a DM1 or DM2 payload.
 *
 * With @p dtc_count 0 the payload is the lamp bytes, the all-zero DTC and two
 * 0xFF bytes. With one DTC the payload is padded to J1939_DIAG_DM_LEN_MIN
 * bytes with 0xFF. Otherwise it is 2 + 4 * @p dtc_count bytes.
 *
 * A DTC with SPN 0 and FMI 0 is rejected: it marks a payload without DTCs.
 *
 * @param lamps      Lamp states.
 * @param dtcs       DTCs in the order they are sent; may be NULL when
 *                   @p dtc_count is 0.
 * @param dtc_count  Number of DTCs, 0..J1939_DIAG_DM_DTC_MAX.
 * @param buf        Payload buffer.
 * @param buf_len    Size of @p buf in bytes.
 * @param len        Payload length. Written only on success.
 * @return J1939_RET_OK, J1939_RET_ERR_FULL if @p buf is too small, or
 *         J1939_RET_ERR_ARG if a lamp value or DTC is invalid, @p dtc_count is
 *         too large or a pointer is NULL. @p buf is written only on success.
 */
j1939_ret_t j1939_diag_dm_build(const j1939_diag_lamps_t *lamps, const j1939_diag_dtc_t *dtcs,
                                uint16_t dtc_count, uint8_t *buf, uint16_t buf_len, uint16_t *len);

/**
 * @brief Parses a DM1 or DM2 payload.
 *
 * Accepted lengths are 2 + 4n with n >= 1, and J1939_DIAG_DM_LEN_MIN when
 * the last two bytes are 0xFF (one DTC padded to a full frame), up to
 * J1939_DIAG_DM_LEN_MAX. A single DTC with SPN 0 and FMI 0 means no DTCs;
 * its occurrence count and CM bit are ignored.
 *
 * @param data       Payload.
 * @param len        Payload length.
 * @param lamps      Decoded lamp states. Written only on success.
 * @param dtcs       Decoded DTCs. Written only on success; may be NULL when
 *                   @p dtcs_len is 0.
 * @param dtcs_len   Entries in @p dtcs.
 * @param dtc_count  Number of DTCs in the payload. Written on success and on
 *                   J1939_RET_ERR_FULL.
 * @return J1939_RET_OK, J1939_RET_ERR_FULL if the payload holds more than
 *         @p dtcs_len DTCs, or J1939_RET_ERR_ARG if the payload is malformed
 *         or a pointer is NULL.
 */
j1939_ret_t j1939_diag_dm_parse(const uint8_t *data, uint16_t len, j1939_diag_lamps_t *lamps,
                                j1939_diag_dtc_t *dtcs, uint16_t dtcs_len, uint16_t *dtc_count);

#endif /* J1939_DIAG_H */
