/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_signal.c
 * @brief Signal (SPN) engine: bit extraction and insertion, scaling and the
 *        J1939/71 value ranges.
 */

#include "j1939/j1939_signal.h"

#include <stdbool.h>
#include <stddef.h>

#include "j1939/j1939_id.h"

#define BITS_PER_BYTE 8U    /**< Bits per byte. */
#define BYTE_MASK     0xFFU /**< Mask of one byte. */
/** A field of J1939_SIGNAL_BITS_MAX bits touches at most this many bytes. */
#define CHUNKS_MAX 5U
/** Bits of the largest payload. */
#define PAYLOAD_BITS_MAX (J1939_SIGNAL_LEN_MAX * BITS_PER_BYTE)

/** @name J1939/71 most significant byte ranges of continuous parameters
 * @{ */
#define CONT_VALID_MAX    0xFAU /**< Last valid value. */
#define CONT_PARAM_SPEC   0xFBU /**< Parameter specific indicator. */
#define CONT_RESERVED_MAX 0xFDU /**< Last reserved value. */
#define CONT_ERROR        0xFEU /**< Error indicator; 0xFF is not available. */
/** @} */

/** @name J1939/71 2-bit discrete parameter values
 * @{ */
#define DISCRETE_BITS      2U /**< Length of a discrete parameter. */
#define DISCRETE_VALID_MAX 1U /**< Last valid value. */
#define DISCRETE_ERROR     2U /**< Error indicator; 3 is not available. */
/** @} */

/**
 * @brief Computes the largest value of a field.
 * @param bits  Field length, 1..32.
 * @return 2^bits - 1.
 */
static uint32_t field_max(uint8_t bits) {
	uint32_t max = UINT32_MAX;

	if (bits < J1939_SIGNAL_BITS_MAX) {
		max = ((uint32_t)1U << bits) - 1U;
	}
	return max;
}

/**
 * @brief Divides, rounding to nearest with halves up.
 * @param n  Dividend.
 * @param d  Divisor, not 0.
 * @return n / d rounded.
 */
static uint64_t div_round(uint64_t n, uint64_t d) {
	uint64_t q = n / d;
	uint64_t r = n % d;

	if (r >= (d - r)) {
		q++;
	}
	return q;
}

/**
 * @brief Checks that a bit field lies within a payload.
 * @param len    Payload length in bytes.
 * @param start  Bit offset of the field.
 * @param bits   Field length.
 * @return true if 1 <= bits <= J1939_SIGNAL_BITS_MAX and the field ends within @p len bytes.
 */
static bool field_ok(uint16_t len, uint16_t start, uint8_t bits) {
	return (bits >= 1U) && (bits <= J1939_SIGNAL_BITS_MAX) &&
	       (((uint32_t)start + (uint32_t)bits) <= ((uint32_t)len * BITS_PER_BYTE));
}

/**
 * @brief Checks the length of a signal against its range type.
 * @param sig  Descriptor.
 * @return true for 1..32 bits (plain), 8, 16 or 32 bits (continuous) or 2 bits (discrete).
 */
static bool bits_ok(const j1939_signal_t *sig) {
	bool ok;

	switch (sig->type) {
	case J1939_SIGNAL_TYPE_PLAIN:
		ok = (sig->bits >= 1U) && (sig->bits <= J1939_SIGNAL_BITS_MAX);
		break;
	case J1939_SIGNAL_TYPE_CONTINUOUS:
		ok = (sig->bits == 8U) || (sig->bits == 16U) || (sig->bits == 32U);
		break;
	case J1939_SIGNAL_TYPE_DISCRETE:
		ok = (sig->bits == DISCRETE_BITS);
		break;
	default:
		ok = false;
		break;
	}
	return ok;
}

/**
 * @brief Checks that no raw value of a signal can overflow when scaled.
 *
 * The largest raw value must scale to at most INT64_MAX, before and after
 * the offset is added. Then no raw value of the field can overflow: the
 * scaled magnitude is at most (2^32 - 1)^2 + 2^31 and fits in 64 bits, and
 * a negative offset only moves the result towards zero.
 *
 * @param sig  Descriptor with a valid length and a non-zero denominator.
 * @return true if scaling is safe.
 */
static bool scale_ok(const j1939_signal_t *sig) {
	uint64_t q = div_round((uint64_t)field_max(sig->bits) * sig->res_num, sig->res_den);
	bool ok = (q <= (uint64_t)INT64_MAX);

	if (ok && (sig->offset > 0)) {
		int64_t room = INT64_MAX - sig->offset;

		ok = (q <= (uint64_t)room);
	}
	return ok;
}

/**
 * @brief Validates a descriptor.
 * @param sig  Descriptor; may be NULL.
 * @return true if not NULL, SPN and PGN are valid, the resolution has no
 *         zero term, the length fits the range type, the field fits the
 *         largest payload and scaling cannot overflow.
 */
static bool sig_ok(const j1939_signal_t *sig) {
	bool ok = false;

	if (sig != NULL) {
		ok = (sig->spn <= J1939_SPN_MAX) && (sig->pgn <= J1939_PGN_MAX) &&
		     ((!j1939_pgn_is_pdu1(sig->pgn)) || ((sig->pgn & BYTE_MASK) == 0U)) &&
		     (sig->res_num != 0U) && (sig->res_den != 0U) && bits_ok(sig) &&
		     (((uint32_t)sig->start + (uint32_t)sig->bits) <= PAYLOAD_BITS_MAX);
		if (ok) {
			ok = scale_ok(sig);
		}
	}
	return ok;
}

/**
 * @brief Reads a little endian bit field.
 * @param data   Payload.
 * @param start  Bit offset of the field; the field must lie within the payload.
 * @param bits   Field length, 1..32.
 * @return The field value.
 */
static uint32_t bits_read(const uint8_t *data, uint16_t start, uint8_t bits) {
	uint32_t raw = 0U;
	uint32_t pos = start;
	uint32_t done = 0U;
	uint32_t i;

	for (i = 0U; i < CHUNKS_MAX; i++) {
		if (done < bits) {
			uint32_t shift = pos % BITS_PER_BYTE;
			uint32_t n = BITS_PER_BYTE - shift;
			uint32_t chunk;

			if (n > ((uint32_t)bits - done)) {
				n = (uint32_t)bits - done;
			}
			chunk = ((uint32_t)data[pos / BITS_PER_BYTE] >> shift) &
			        (((uint32_t)1U << n) - 1U);
			raw |= chunk << done;
			done += n;
			pos += n;
		}
	}
	return raw;
}

/**
 * @brief Writes a little endian bit field, leaving all other bits unchanged.
 * @param data   Payload.
 * @param start  Bit offset of the field; the field must lie within the payload.
 * @param bits   Field length, 1..32.
 * @param raw    Value; must fit in @p bits.
 */
static void bits_write(uint8_t *data, uint16_t start, uint8_t bits, uint32_t raw) {
	uint32_t pos = start;
	uint32_t done = 0U;
	uint32_t i;

	for (i = 0U; i < CHUNKS_MAX; i++) {
		if (done < bits) {
			uint32_t shift = pos % BITS_PER_BYTE;
			uint32_t n = BITS_PER_BYTE - shift;
			uint32_t mask;
			uint32_t idx = pos / BITS_PER_BYTE;

			if (n > ((uint32_t)bits - done)) {
				n = (uint32_t)bits - done;
			}
			mask = (((uint32_t)1U << n) - 1U) << shift;
			data[idx] = (uint8_t)(((uint32_t)data[idx] & ~mask) |
			                      (((raw >> done) << shift) & mask));
			done += n;
			pos += n;
		}
	}
}

/**
 * @brief Classifies a raw value by the J1939/71 range of its signal type.
 * @param sig  Valid descriptor.
 * @param raw  Raw value; must fit in the signal's bits.
 * @return The range class; plain signals are always valid.
 */
static j1939_signal_class_t classify(const j1939_signal_t *sig, uint32_t raw) {
	j1939_signal_class_t cls;
	uint32_t msb;

	switch (sig->type) {
	case J1939_SIGNAL_TYPE_PLAIN:
		cls = J1939_SIGNAL_VALID;
		break;
	case J1939_SIGNAL_TYPE_CONTINUOUS:
		msb = raw >> ((uint32_t)sig->bits - BITS_PER_BYTE);
		if (msb <= CONT_VALID_MAX) {
			cls = J1939_SIGNAL_VALID;
		} else if (msb == CONT_PARAM_SPEC) {
			cls = J1939_SIGNAL_PARAM_SPECIFIC;
		} else if (msb <= CONT_RESERVED_MAX) {
			cls = J1939_SIGNAL_RESERVED;
		} else if (msb == CONT_ERROR) {
			cls = J1939_SIGNAL_ERROR;
		} else {
			cls = J1939_SIGNAL_NOT_AVAILABLE;
		}
		break;
	case J1939_SIGNAL_TYPE_DISCRETE:
		if (raw <= DISCRETE_VALID_MAX) {
			cls = J1939_SIGNAL_VALID;
		} else if (raw == DISCRETE_ERROR) {
			cls = J1939_SIGNAL_ERROR;
		} else {
			cls = J1939_SIGNAL_NOT_AVAILABLE;
		}
		break;
	default:
		/* Ruled out by sig_ok(). */
		cls = J1939_SIGNAL_ERROR;
		break;
	}
	return cls;
}

/**
 * @brief Scales a raw value to engineering units: raw * res_num / res_den + offset.
 * @param sig  Valid descriptor, see scale_ok().
 * @param raw  Raw value; must fit in the signal's bits.
 * @return The engineering value, rounded to nearest with halves up.
 */
static int64_t to_eng(const j1939_signal_t *sig, uint32_t raw) {
	uint64_t q = div_round((uint64_t)raw * sig->res_num, sig->res_den);

	return (int64_t)q + sig->offset;
}

/**
 * @brief Converts an engineering value to a raw value.
 * @param sig    Valid descriptor.
 * @param value  Engineering value.
 * @param raw    Raw value. Written only on success.
 * @return true if the value rounds to a raw value that fits the field and
 *         lies in the valid range.
 */
static bool from_eng(const j1939_signal_t *sig, int64_t value, uint32_t *raw) {
	bool ok = false;
	bool neg = (value < sig->offset);
	uint64_t mag;

	/* |value - offset| is below 2^64; unsigned arithmetic computes it exactly. */
	if (neg) {
		mag = (uint64_t)sig->offset - (uint64_t)value;
	} else {
		mag = (uint64_t)value - (uint64_t)sig->offset;
	}

	/* A product above 64 bits gives at least 2^64 / (2^32 - 1) > 2^32 raw. */
	if (mag <= (UINT64_MAX / sig->res_den)) {
		uint64_t p = mag * sig->res_den;
		uint64_t q = 0U;

		if (neg) {
			/* -p / res_num rounds half up to raw 0 when p / res_num <= 1/2. */
			uint64_t rem = p % sig->res_num;

			ok = ((p / sig->res_num) == 0U) && (rem <= (sig->res_num - rem));
		} else {
			q = div_round(p, sig->res_num);
			ok = (q <= field_max(sig->bits));
		}
		if (ok) {
			uint32_t r = (uint32_t)q;

			ok = (classify(sig, r) == J1939_SIGNAL_VALID);
			if (ok) {
				*raw = r;
			}
		}
	}
	return ok;
}

/**
 * @brief Computes the error or not available raw value of a signal.
 * @param sig  Valid descriptor.
 * @param cls  J1939_SIGNAL_ERROR or J1939_SIGNAL_NOT_AVAILABLE.
 * @param raw  The indicator value. Written only on success.
 * @return false for a plain signal or another class.
 */
static bool indicator_raw(const j1939_signal_t *sig, j1939_signal_class_t cls, uint32_t *raw) {
	bool ok = (sig->type != J1939_SIGNAL_TYPE_PLAIN);
	uint32_t max = field_max(sig->bits);

	if (ok) {
		if (cls == J1939_SIGNAL_NOT_AVAILABLE) {
			*raw = max;
		} else if (cls == J1939_SIGNAL_ERROR) {
			/* Clears the lowest bit of the most significant byte: 0xFE.., or 10. */
			if (sig->type == J1939_SIGNAL_TYPE_DISCRETE) {
				*raw = DISCRETE_ERROR;
			} else {
				*raw = max &
				       ~((uint32_t)1U << ((uint32_t)sig->bits - BITS_PER_BYTE));
			}
		} else {
			ok = false;
		}
	}
	return ok;
}

/**
 * @brief Checks the arguments and reads a bit field.
 * @param data   Payload.
 * @param len    Payload length in bytes.
 * @param start  Bit offset of the field.
 * @param bits   Field length.
 * @param raw    Field value. Written only on success.
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG on a NULL pointer or a field outside the payload.
 */
static j1939_ret_t bits_get(const uint8_t *data, uint16_t len, uint16_t start, uint8_t bits,
                            uint32_t *raw) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((data != NULL) && (raw != NULL) && field_ok(len, start, bits)) {
		*raw = bits_read(data, start, bits);
		ret = J1939_RET_OK;
	}
	return ret;
}

/**
 * @brief Checks the arguments and writes a bit field.
 * @param data   Payload.
 * @param len    Payload length in bytes.
 * @param start  Bit offset of the field.
 * @param bits   Field length.
 * @param raw    Value.
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG on a NULL pointer, a field
 *         outside the payload or a value that does not fit.
 */
static j1939_ret_t bits_set(uint8_t *data, uint16_t len, uint16_t start, uint8_t bits,
                            uint32_t raw) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((data != NULL) && field_ok(len, start, bits) && (raw <= field_max(bits))) {
		bits_write(data, start, bits, raw);
		ret = J1939_RET_OK;
	}
	return ret;
}

/**
 * @brief Reads, classifies and scales a signal.
 * @param sig    Valid descriptor.
 * @param data   Payload.
 * @param len    Payload length in bytes.
 * @param value  Engineering value. Written only for a valid raw value.
 * @param cls    Range class of the raw value.
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG on a NULL pointer or a field outside the payload.
 */
static j1939_ret_t decode(const j1939_signal_t *sig, const uint8_t *data, uint16_t len,
                          int64_t *value, j1939_signal_class_t *cls) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((data != NULL) && (value != NULL) && (cls != NULL) &&
	    field_ok(len, sig->start, sig->bits)) {
		uint32_t raw = bits_read(data, sig->start, sig->bits);
		j1939_signal_class_t c = classify(sig, raw);

		if (c == J1939_SIGNAL_VALID) {
			*value = to_eng(sig, raw);
		}
		*cls = c;
		ret = J1939_RET_OK;
	}
	return ret;
}

j1939_ret_t j1939_signal_check(const j1939_signal_t *sig) {
	return sig_ok(sig) ? J1939_RET_OK : J1939_RET_ERR_ARG;
}

j1939_ret_t j1939_signal_bits_get(const uint8_t *data, uint16_t len, uint16_t start, uint8_t bits,
                                  uint32_t *raw) {
	return bits_get(data, len, start, bits, raw);
}

j1939_ret_t j1939_signal_bits_set(uint8_t *data, uint16_t len, uint16_t start, uint8_t bits,
                                  uint32_t raw) {
	return bits_set(data, len, start, bits, raw);
}

j1939_ret_t j1939_signal_raw_get(const j1939_signal_t *sig, const uint8_t *data, uint16_t len,
                                 uint32_t *raw) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if (sig_ok(sig)) {
		ret = bits_get(data, len, sig->start, sig->bits, raw);
	}
	return ret;
}

j1939_ret_t j1939_signal_raw_set(const j1939_signal_t *sig, uint8_t *data, uint16_t len,
                                 uint32_t raw) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if (sig_ok(sig)) {
		ret = bits_set(data, len, sig->start, sig->bits, raw);
	}
	return ret;
}

j1939_ret_t j1939_signal_classify(const j1939_signal_t *sig, uint32_t raw,
                                  j1939_signal_class_t *cls) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if (sig_ok(sig) && (cls != NULL) && (raw <= field_max(sig->bits))) {
		*cls = classify(sig, raw);
		ret = J1939_RET_OK;
	}
	return ret;
}

j1939_ret_t j1939_signal_to_eng(const j1939_signal_t *sig, uint32_t raw, int64_t *value) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if (sig_ok(sig) && (value != NULL) && (raw <= field_max(sig->bits))) {
		*value = to_eng(sig, raw);
		ret = J1939_RET_OK;
	}
	return ret;
}

j1939_ret_t j1939_signal_from_eng(const j1939_signal_t *sig, int64_t value, uint32_t *raw) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if (sig_ok(sig) && (raw != NULL)) {
		if (from_eng(sig, value, raw)) {
			ret = J1939_RET_OK;
		}
	}
	return ret;
}

j1939_ret_t j1939_signal_decode(const j1939_signal_t *sig, const uint8_t *data, uint16_t len,
                                int64_t *value, j1939_signal_class_t *cls) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if (sig_ok(sig)) {
		ret = decode(sig, data, len, value, cls);
	}
	return ret;
}

j1939_ret_t j1939_signal_encode(const j1939_signal_t *sig, uint8_t *data, uint16_t len,
                                int64_t value) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;
	uint32_t raw;

	if (sig_ok(sig) && (data != NULL) && field_ok(len, sig->start, sig->bits)) {
		if (from_eng(sig, value, &raw)) {
			bits_write(data, sig->start, sig->bits, raw);
			ret = J1939_RET_OK;
		}
	}
	return ret;
}

j1939_ret_t j1939_signal_indicator_set(const j1939_signal_t *sig, uint8_t *data, uint16_t len,
                                       j1939_signal_class_t cls) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;
	uint32_t raw;

	if (sig_ok(sig) && (data != NULL) && field_ok(len, sig->start, sig->bits)) {
		if (indicator_raw(sig, cls, &raw)) {
			bits_write(data, sig->start, sig->bits, raw);
			ret = J1939_RET_OK;
		}
	}
	return ret;
}

j1939_ret_t j1939_signal_msg_decode(const j1939_signal_t *sig, const j1939_msg_t *msg,
                                    int64_t *value, j1939_signal_class_t *cls) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if (sig_ok(sig) && (msg != NULL)) {
		if (msg->pgn == sig->pgn) {
			ret = decode(sig, msg->data, msg->len, value, cls);
		}
	}
	return ret;
}
