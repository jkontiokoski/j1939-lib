/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_signal.h
 * @brief Signal (SPN) access: J1939/71 conventions and the J1939DA schema.
 *
 * A signal is described by a @ref j1939_signal_t descriptor: plain `const`
 * data supplied by the integrator, typically one table per project built
 * from the licensed J1939DA. The functions below extract, insert, scale and
 * classify signals within a PGN payload. They are pure: no state, no I/O,
 * no floating point.
 *
 * @par Bit position
 * A signal occupies @c bits consecutive bits starting at bit offset
 * @c start. Payload bit @c n is bit <tt>n % 8</tt> (0 = least significant)
 * of byte <tt>n / 8</tt> (0 = first byte). Multi-byte signals are little
 * endian, as J1939 requires: the least significant bits of the value are in
 * the lowest payload bits. The J1939DA start position "B.b" (byte B and bit
 * b, both counted from 1) converts with J1939_SIGNAL_POS(B, b).
 *
 * @par Scaling
 * The engineering value of a raw value is
 * <tt>raw * res_num / res_den + offset</tt>, in the integrator's chosen
 * integer engineering unit. The unit sets the precision: a 0.125 rpm/bit
 * signal decodes to whole rpm with res_num = 1, res_den = 8, or exactly to
 * millirpm with res_num = 125, res_den = 1. Arithmetic is 64-bit integer.
 * Results are rounded to the nearest integer, halves up (towards positive
 * infinity), in both directions.
 *
 * @par Validity (J1939/71)
 * Continuous parameters of 1, 2 and 4 bytes are classified by their most
 * significant byte:
 *
 * | Range                        | 1 byte    | 2 bytes         | 4 bytes                 |
 * | ---------------------------- | --------- | --------------- | ----------------------- |
 * | Valid signal                 | 0x00-0xFA | 0x0000-0xFAFF   | 0x00000000-0xFAFFFFFF   |
 * | Parameter specific indicator | 0xFB      | 0xFB00-0xFBFF   | 0xFB000000-0xFBFFFFFF   |
 * | Reserved (future indicators) | 0xFC-0xFD | 0xFC00-0xFDFF   | 0xFC000000-0xFDFFFFFF   |
 * | Error indicator              | 0xFE      | 0xFE00-0xFEFF   | 0xFE000000-0xFEFFFFFF   |
 * | Not available                | 0xFF      | 0xFF00-0xFFFF   | 0xFF000000-0xFFFFFFFF   |
 *
 * Discrete (2-bit) status parameters: 00 and 01 are valid states,
 * 10 is the error indicator, 11 is not available.
 *
 * Signals without J1939/71 ranges (other lengths, counters, bit masks,
 * proprietary fields) use J1939_SIGNAL_TYPE_PLAIN: every raw value is valid.
 *
 * @par Descriptor validation
 * Every function taking a descriptor validates it first, as
 * j1939_signal_check() does, and fails with J1939_RET_ERR_ARG if it is
 * invalid. A valid descriptor guarantees that scaling cannot overflow.
 */

#ifndef J1939_SIGNAL_H
#define J1939_SIGNAL_H

#include <stdint.h>

#include "j1939/j1939_msg.h"
#include "j1939/j1939_ret.h"

#define J1939_SPN_MAX         0x7FFFFU /**< Largest 19-bit SPN. */
#define J1939_SIGNAL_BITS_MAX 32U      /**< Longest signal, in bits. */
#define J1939_SIGNAL_LEN_MAX  1785U    /**< Longest payload a signal may lie in, in bytes. */

/**
 * Bit offset of the J1939DA start position "@p byte . @p bit", both counted
 * from 1. Example: J1939_SIGNAL_POS(2, 3) is 10.
 */
#define J1939_SIGNAL_POS(byte, bit) ((((byte) - 1U) * 8U) + ((bit) - 1U))

/** Value range convention of a signal. */
typedef enum j1939_signal_type {
	J1939_SIGNAL_TYPE_PLAIN = 0,  /**< No reserved values; 1..32 bits. */
	J1939_SIGNAL_TYPE_CONTINUOUS, /**< J1939/71 parameter ranges; 8, 16 or 32 bits. */
	J1939_SIGNAL_TYPE_DISCRETE,   /**< J1939/71 2-bit discrete status; 2 bits. */
} j1939_signal_type_t;

/** J1939/71 classification of a raw value. */
typedef enum j1939_signal_class {
	J1939_SIGNAL_VALID = 0,      /**< Valid signal. */
	J1939_SIGNAL_PARAM_SPECIFIC, /**< Parameter specific indicator. */
	J1939_SIGNAL_RESERVED,       /**< Reserved for future indicators. */
	J1939_SIGNAL_ERROR,          /**< Error indicator. */
	J1939_SIGNAL_NOT_AVAILABLE,  /**< Not available or not requested. */
} j1939_signal_class_t;

/**
 * Signal descriptor. Integrator data, normally in a `const` table.
 *
 * Valid when:
 * - spn is at most J1939_SPN_MAX,
 * - pgn is at most 0x3FFFF, with the lowest byte 0 for PDU1 formats,
 * - bits matches the type (see @ref j1939_signal_type_t),
 * - start + bits is at most 8 * J1939_SIGNAL_LEN_MAX,
 * - res_num and res_den are not 0,
 * - the largest raw value of the field scales to at most INT64_MAX.
 */
typedef struct j1939_signal {
	uint32_t spn;             /**< Suspect parameter number, 0..J1939_SPN_MAX. */
	uint32_t pgn;             /**< Parameter group the signal is transmitted in. */
	uint16_t start;           /**< Bit offset in the payload, see J1939_SIGNAL_POS(). */
	uint8_t bits;             /**< Length in bits. */
	j1939_signal_type_t type; /**< Value range convention. */
	uint32_t res_num;         /**< Resolution numerator: engineering units per bit. */
	uint32_t res_den;         /**< Resolution denominator. */
	int64_t offset;           /**< Offset, in engineering units. */
	const char *unit;         /**< Engineering unit for display. May be NULL. */
	const char *name;         /**< Signal name for display. May be NULL. */
} j1939_signal_t;

/**
 * @brief Validates a descriptor.
 *
 * @param sig  Descriptor.
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG if @p sig is NULL or invalid.
 */
j1939_ret_t j1939_signal_check(const j1939_signal_t *sig);

/**
 * @brief Reads a bit field from a payload.
 *
 * @param data   Payload.
 * @param len    Payload length in bytes.
 * @param start  Bit offset of the field, see J1939_SIGNAL_POS().
 * @param bits   Field length, 1..J1939_SIGNAL_BITS_MAX.
 * @param raw    Field value. Written only on success.
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG if a pointer is NULL, @p bits is
 *         out of range or the field does not lie within @p len bytes.
 */
j1939_ret_t j1939_signal_bits_get(const uint8_t *data, uint16_t len, uint16_t start, uint8_t bits,
                                  uint32_t *raw);

/**
 * @brief Writes a bit field into a payload. All other payload bits are left
 *        unchanged.
 *
 * @param data   Payload.
 * @param len    Payload length in bytes.
 * @param start  Bit offset of the field, see J1939_SIGNAL_POS().
 * @param bits   Field length, 1..J1939_SIGNAL_BITS_MAX.
 * @param raw    Field value; must fit in @p bits.
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG if @p data is NULL, @p bits or
 *         @p raw is out of range or the field does not lie within @p len
 *         bytes. The payload is changed only on success.
 */
j1939_ret_t j1939_signal_bits_set(uint8_t *data, uint16_t len, uint16_t start, uint8_t bits,
                                  uint32_t raw);

/**
 * @brief Reads the raw value of a signal from a payload.
 *
 * @param sig   Descriptor.
 * @param data  Payload.
 * @param len   Payload length in bytes.
 * @param raw   Raw value. Written only on success.
 * @return As j1939_signal_bits_get(), or J1939_RET_ERR_ARG if @p sig is invalid.
 */
j1939_ret_t j1939_signal_raw_get(const j1939_signal_t *sig, const uint8_t *data, uint16_t len,
                                 uint32_t *raw);

/**
 * @brief Writes the raw value of a signal into a payload. All other payload
 *        bits are left unchanged.
 *
 * @param sig   Descriptor.
 * @param data  Payload.
 * @param len   Payload length in bytes.
 * @param raw   Raw value; must fit in the signal's bits. It is not classified,
 *              so indicator values can be written too.
 * @return As j1939_signal_bits_set(), or J1939_RET_ERR_ARG if @p sig is invalid.
 */
j1939_ret_t j1939_signal_raw_set(const j1939_signal_t *sig, uint8_t *data, uint16_t len,
                                 uint32_t raw);

/**
 * @brief Classifies a raw value by the J1939/71 ranges of the signal's type.
 *
 * @param sig  Descriptor.
 * @param raw  Raw value; must fit in the signal's bits.
 * @param cls  Classification. Written only on success.
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG if @p sig is invalid, @p cls is
 *         NULL or @p raw does not fit.
 */
j1939_ret_t j1939_signal_classify(const j1939_signal_t *sig, uint32_t raw,
                                  j1939_signal_class_t *cls);

/**
 * @brief Scales a raw value to engineering units: raw * res_num / res_den +
 *        offset, rounded to nearest, halves up.
 *
 * The raw value is not classified; use j1939_signal_decode() to skip
 * indicator values.
 *
 * @param sig    Descriptor.
 * @param raw    Raw value; must fit in the signal's bits.
 * @param value  Engineering value. Written only on success.
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG if @p sig is invalid, @p value
 *         is NULL or @p raw does not fit.
 */
j1939_ret_t j1939_signal_to_eng(const j1939_signal_t *sig, uint32_t raw, int64_t *value);

/**
 * @brief Converts an engineering value to a raw value: (value - offset) *
 *        res_den / res_num, rounded to nearest, halves up.
 *
 * @param sig    Descriptor.
 * @param value  Engineering value.
 * @param raw    Raw value. Written only on success.
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG if @p sig is invalid, @p raw is
 *         NULL, or the rounded result is negative, does not fit in the
 *         signal's bits or is not a valid signal by J1939/71 (it would be
 *         transmitted as an indicator).
 */
j1939_ret_t j1939_signal_from_eng(const j1939_signal_t *sig, int64_t value, uint32_t *raw);

/**
 * @brief Reads, classifies and scales a signal.
 *
 * @param sig    Descriptor.
 * @param data   Payload.
 * @param len    Payload length in bytes.
 * @param value  Engineering value. Written only on success and only when the
 *               class is J1939_SIGNAL_VALID.
 * @param cls    Classification of the raw value. Written on success.
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG if @p sig is invalid, a pointer
 *         is NULL or the signal does not lie within @p len bytes.
 */
j1939_ret_t j1939_signal_decode(const j1939_signal_t *sig, const uint8_t *data, uint16_t len,
                                int64_t *value, j1939_signal_class_t *cls);

/**
 * @brief Scales an engineering value and writes it into a payload. All other
 *        payload bits are left unchanged.
 *
 * @param sig    Descriptor.
 * @param data   Payload.
 * @param len    Payload length in bytes.
 * @param value  Engineering value.
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG as j1939_signal_from_eng() and
 *         j1939_signal_raw_set(). The payload is changed only on success.
 */
j1939_ret_t j1939_signal_encode(const j1939_signal_t *sig, uint8_t *data, uint16_t len,
                                int64_t value);

/**
 * @brief Writes an indicator value into a payload. All other payload bits
 *        are left unchanged.
 *
 * Not available is written as all ones. The error indicator is written as
 * 0xFE in the most significant byte with the lower bytes all ones for
 * continuous signals (0xFE, 0xFEFF, 0xFEFFFFFF), and as 10 for discrete
 * signals.
 *
 * @param sig   Descriptor of a continuous or discrete signal.
 * @param data  Payload.
 * @param len   Payload length in bytes.
 * @param cls   J1939_SIGNAL_ERROR or J1939_SIGNAL_NOT_AVAILABLE.
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG if @p sig is invalid or a plain
 *         signal, @p cls is another class, @p data is NULL or the signal does
 *         not lie within @p len bytes. The payload is changed only on success.
 */
j1939_ret_t j1939_signal_indicator_set(const j1939_signal_t *sig, uint8_t *data, uint16_t len,
                                       j1939_signal_class_t cls);

/**
 * @brief Decodes a signal from a received message, as j1939_signal_decode().
 *
 * @param sig    Descriptor.
 * @param msg    Received message; its PGN must be the descriptor's PGN.
 * @param value  Engineering value. Written only on success and only when the
 *               class is J1939_SIGNAL_VALID.
 * @param cls    Classification of the raw value. Written on success.
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG if @p sig is invalid, a pointer
 *         is NULL, the PGNs differ or the message is too short to contain
 *         the signal.
 */
j1939_ret_t j1939_signal_msg_decode(const j1939_signal_t *sig, const j1939_msg_t *msg,
                                    int64_t *value, j1939_signal_class_t *cls);

#endif /* J1939_SIGNAL_H */
