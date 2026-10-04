/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_diag.c
 * @brief J1939/73 codec: DTCs, lamp status and DM1/DM2 payloads.
 */

#include "j1939/j1939_diag.h"

#include <stdbool.h>
#include <stddef.h>

#define BYTE_MASK      0xFFU /**< Mask of one byte. */
#define BYTE_SHIFT     8U    /**< Bits per byte. */
#define SPN_HIGH_SHIFT 16U   /**< Position of SPN bits 19-17 in the SPN. */
#define SPN_HIGH_MASK  0x7U  /**< Mask of SPN bits 19-17, after shifting. */
#define SPN_HIGH_POS   5U    /**< Position of SPN bits 19-17 in DTC byte 3. */
#define FMI_MASK       0x1FU /**< FMI field mask in DTC byte 3. */
#define OC_MASK        0x7FU /**< Occurrence count field mask in DTC byte 4. */
#define CM_POS         7U    /**< Position of the conversion method bit in DTC byte 4. */
#define LAMP_MASK      0x3U  /**< Mask of one 2-bit lamp field. */
#define PAD            0xFFU /**< Padding byte of a single DTC payload. */

/** @name Bit positions of the four lamps in both lamp bytes
 * @{ */
#define MIL_POS 6U /**< Malfunction indicator lamp. */
#define RSL_POS 4U /**< Red stop lamp. */
#define AWL_POS 2U /**< Amber warning lamp. */
#define PL_POS  0U /**< Protect lamp. */
/** @} */

/**
 * @brief Checks the field ranges of a DTC for encoding.
 * @param dtc  DTC.
 * @return true if SPN, FMI and occurrence count fit and the conversion method is 0.
 */
static bool dtc_valid(const j1939_diag_dtc_t *dtc) {
	return (dtc->spn <= J1939_DIAG_SPN_MAX) && (dtc->fmi <= J1939_DIAG_FMI_MAX) &&
	       (dtc->oc <= J1939_DIAG_OC_NA) && (dtc->cm == J1939_DIAG_CM_V4);
}

/**
 * @brief Tells whether a DTC is the marker of a DM payload without DTCs.
 * @param dtc  DTC.
 * @return true for SPN 0 with FMI 0.
 */
static bool dtc_is_none(const j1939_diag_dtc_t *dtc) {
	return (dtc->spn == 0U) && (dtc->fmi == 0U);
}

/**
 * @brief Writes a DTC in its four-byte layout (conversion method 0).
 * @param dtc  DTC with valid fields.
 * @param buf  Four bytes.
 */
static void dtc_write(const j1939_diag_dtc_t *dtc, uint8_t *buf) {
	buf[0] = (uint8_t)(dtc->spn & BYTE_MASK);
	buf[1] = (uint8_t)((dtc->spn >> BYTE_SHIFT) & BYTE_MASK);
	buf[2] = (uint8_t)((((dtc->spn >> SPN_HIGH_SHIFT) & SPN_HIGH_MASK) << SPN_HIGH_POS) |
	                   ((uint32_t)dtc->fmi & FMI_MASK));
	buf[3] = (uint8_t)((((uint32_t)dtc->cm & 1U) << CM_POS) | ((uint32_t)dtc->oc & OC_MASK));
}

/**
 * @brief Reads a DTC from its four-byte layout; the SPN is read at its version 4 position.
 * @param buf  Four bytes.
 * @param dtc  Decoded DTC.
 */
static void dtc_read(const uint8_t *buf, j1939_diag_dtc_t *dtc) {
	dtc->spn = (uint32_t)buf[0] | ((uint32_t)buf[1] << BYTE_SHIFT) |
	           ((((uint32_t)buf[2] >> SPN_HIGH_POS) & SPN_HIGH_MASK) << SPN_HIGH_SHIFT);
	dtc->fmi = (uint8_t)((uint32_t)buf[2] & FMI_MASK);
	dtc->oc = (uint8_t)((uint32_t)buf[3] & OC_MASK);
	dtc->cm = (uint8_t)(((uint32_t)buf[3] >> CM_POS) & 1U);
}

/**
 * @brief Checks every lamp and flash field against J1939_DIAG_LAMP_MAX.
 * @param l  Lamp status.
 * @return true if all fields fit two bits.
 */
static bool lamps_valid(const j1939_diag_lamps_t *l) {
	return (l->mil <= J1939_DIAG_LAMP_MAX) && (l->red_stop <= J1939_DIAG_LAMP_MAX) &&
	       (l->amber_warning <= J1939_DIAG_LAMP_MAX) && (l->protect <= J1939_DIAG_LAMP_MAX) &&
	       (l->mil_flash <= J1939_DIAG_LAMP_MAX) &&
	       (l->red_stop_flash <= J1939_DIAG_LAMP_MAX) &&
	       (l->amber_warning_flash <= J1939_DIAG_LAMP_MAX) &&
	       (l->protect_flash <= J1939_DIAG_LAMP_MAX);
}

/**
 * @brief Packs the four 2-bit lamp fields of one lamp byte.
 * @param mil  Malfunction indicator lamp field.
 * @param rsl  Red stop lamp field.
 * @param awl  Amber warning lamp field.
 * @param pl   Protect lamp field.
 * @return The lamp byte.
 */
static uint8_t lamp_byte(uint8_t mil, uint8_t rsl, uint8_t awl, uint8_t pl) {
	return (uint8_t)((((uint32_t)mil & LAMP_MASK) << MIL_POS) |
	                 (((uint32_t)rsl & LAMP_MASK) << RSL_POS) |
	                 (((uint32_t)awl & LAMP_MASK) << AWL_POS) |
	                 (((uint32_t)pl & LAMP_MASK) << PL_POS));
}

/**
 * @brief Extracts one 2-bit lamp field from a lamp byte.
 * @param byte  Lamp byte.
 * @param pos   Bit position of the field.
 * @return The field value.
 */
static uint8_t lamp_get(uint8_t byte, uint32_t pos) {
	return (uint8_t)(((uint32_t)byte >> pos) & LAMP_MASK);
}

/**
 * @brief Writes the two lamp bytes: status, then flash.
 * @param l    Lamp status with valid fields.
 * @param buf  Two bytes.
 */
static void lamps_write(const j1939_diag_lamps_t *l, uint8_t *buf) {
	buf[0] = lamp_byte(l->mil, l->red_stop, l->amber_warning, l->protect);
	buf[1] = lamp_byte(l->mil_flash, l->red_stop_flash, l->amber_warning_flash,
	                   l->protect_flash);
}

/**
 * @brief Reads the two lamp bytes.
 * @param buf  Two bytes: status, then flash.
 * @param l    Decoded lamp status.
 */
static void lamps_read(const uint8_t *buf, j1939_diag_lamps_t *l) {
	l->mil = lamp_get(buf[0], MIL_POS);
	l->red_stop = lamp_get(buf[0], RSL_POS);
	l->amber_warning = lamp_get(buf[0], AWL_POS);
	l->protect = lamp_get(buf[0], PL_POS);
	l->mil_flash = lamp_get(buf[1], MIL_POS);
	l->red_stop_flash = lamp_get(buf[1], RSL_POS);
	l->amber_warning_flash = lamp_get(buf[1], AWL_POS);
	l->protect_flash = lamp_get(buf[1], PL_POS);
}

/**
 * @brief Validates every DTC of a list to be built.
 * @param dtcs   DTCs.
 * @param count  Number of DTCs.
 * @return true if every DTC is valid and none is the no-DTC marker.
 */
static bool dtc_list_valid(const j1939_diag_dtc_t *dtcs, uint16_t count) {
	bool valid = true;
	uint16_t i;

	for (i = 0U; valid && (i < count); i++) {
		valid = dtc_valid(&dtcs[i]) && !dtc_is_none(&dtcs[i]);
	}
	return valid;
}

/**
 * @brief Computes the payload length of a DM.
 * @param count  Number of DTCs, at most J1939_DIAG_DM_DTC_MAX.
 * @return 2 + 4 * count bytes, at least J1939_DIAG_DM_LEN_MIN.
 */
static uint16_t dm_len(uint16_t count) {
	uint16_t len = (uint16_t)(J1939_DIAG_LAMPS_LEN + ((uint32_t)count * J1939_DIAG_DTC_LEN));

	if (len < J1939_DIAG_DM_LEN_MIN) {
		len = (uint16_t)J1939_DIAG_DM_LEN_MIN;
	}
	return len;
}

/**
 * @brief Counts the four-byte DTC fields of a received DM payload.
 * @param data  Payload.
 * @param len   Payload length in bytes.
 * @return The number of DTC fields: 1 for the padded eight-byte form, or 0
 *         if the length is malformed.
 */
static uint16_t dm_entries(const uint8_t *data, uint16_t len) {
	uint16_t entries = 0U;

	if ((len == J1939_DIAG_DM_LEN_MIN) && (data[J1939_DIAG_DM_LEN_MIN - 2U] == PAD) &&
	    (data[J1939_DIAG_DM_LEN_MIN - 1U] == PAD)) {
		entries = 1U;
	} else if ((len > J1939_DIAG_LAMPS_LEN) && (len <= J1939_DIAG_DM_LEN_MAX) &&
	           (((uint32_t)len - J1939_DIAG_LAMPS_LEN) % J1939_DIAG_DTC_LEN) == 0U) {
		entries = (uint16_t)(((uint32_t)len - J1939_DIAG_LAMPS_LEN) / J1939_DIAG_DTC_LEN);
	} else {
		/* Malformed length. */
	}
	return entries;
}

j1939_ret_t j1939_diag_dtc_encode(const j1939_diag_dtc_t *dtc, uint8_t *buf) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((dtc != NULL) && (buf != NULL) && dtc_valid(dtc)) {
		dtc_write(dtc, buf);
		ret = J1939_RET_OK;
	}
	return ret;
}

j1939_ret_t j1939_diag_dtc_decode(const uint8_t *buf, j1939_diag_dtc_t *dtc) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((buf != NULL) && (dtc != NULL)) {
		dtc_read(buf, dtc);
		ret = J1939_RET_OK;
	}
	return ret;
}

j1939_ret_t j1939_diag_lamps_encode(const j1939_diag_lamps_t *lamps, uint8_t *buf) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((lamps != NULL) && (buf != NULL) && lamps_valid(lamps)) {
		lamps_write(lamps, buf);
		ret = J1939_RET_OK;
	}
	return ret;
}

j1939_ret_t j1939_diag_lamps_decode(const uint8_t *buf, j1939_diag_lamps_t *lamps) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((buf != NULL) && (lamps != NULL)) {
		lamps_read(buf, lamps);
		ret = J1939_RET_OK;
	}
	return ret;
}

j1939_ret_t j1939_diag_dm_build(const j1939_diag_lamps_t *lamps, const j1939_diag_dtc_t *dtcs,
                                uint16_t dtc_count, uint8_t *buf, uint16_t buf_len, uint16_t *len) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((lamps == NULL) || (buf == NULL) || (len == NULL) ||
	    (dtc_count > J1939_DIAG_DM_DTC_MAX) || ((dtcs == NULL) && (dtc_count > 0U))) {
		/* Invalid arguments. */
	} else if (!lamps_valid(lamps) || !dtc_list_valid(dtcs, dtc_count)) {
		/* Invalid content. */
	} else if (buf_len < dm_len(dtc_count)) {
		ret = J1939_RET_ERR_FULL;
	} else {
		const uint16_t n = dm_len(dtc_count);
		uint16_t pos = (uint16_t)J1939_DIAG_LAMPS_LEN;
		uint16_t i;

		lamps_write(lamps, buf);
		if (dtc_count == 0U) {
			const j1939_diag_dtc_t none = {0U, 0U, 0U, J1939_DIAG_CM_V4};

			dtc_write(&none, &buf[pos]);
			pos += (uint16_t)J1939_DIAG_DTC_LEN;
		}
		for (i = 0U; i < dtc_count; i++) {
			dtc_write(&dtcs[i], &buf[pos]);
			pos += (uint16_t)J1939_DIAG_DTC_LEN;
		}
		for (i = pos; i < n; i++) {
			buf[i] = (uint8_t)PAD;
		}
		*len = n;
		ret = J1939_RET_OK;
	}
	return ret;
}

j1939_ret_t j1939_diag_dm_parse(const uint8_t *data, uint16_t len, j1939_diag_lamps_t *lamps,
                                j1939_diag_dtc_t *dtcs, uint16_t dtcs_len, uint16_t *dtc_count) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((data != NULL) && (lamps != NULL) && (dtc_count != NULL) &&
	    ((dtcs != NULL) || (dtcs_len == 0U))) {
		const uint16_t entries = dm_entries(data, len);
		uint16_t count = entries;

		if (entries == 1U) {
			j1939_diag_dtc_t first;

			dtc_read(&data[J1939_DIAG_LAMPS_LEN], &first);
			if (dtc_is_none(&first)) {
				count = 0U;
			}
		}

		if (entries == 0U) {
			/* Malformed payload. */
		} else if (count > dtcs_len) {
			*dtc_count = count;
			ret = J1939_RET_ERR_FULL;
		} else {
			uint16_t i;

			lamps_read(data, lamps);
			for (i = 0U; i < count; i++) {
				dtc_read(&data[J1939_DIAG_LAMPS_LEN +
				               ((uint32_t)i * J1939_DIAG_DTC_LEN)],
				         &dtcs[i]);
			}
			*dtc_count = count;
			ret = J1939_RET_OK;
		}
	}
	return ret;
}
