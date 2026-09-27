/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_target.h
 * @brief Mock port used by the library's own tests.
 *
 * The frame layout follows a bxCAN style mailbox register: the identifier is
 * left-aligned in a 32-bit word with the IDE and RTR flags in the low bits,
 * and the DLC is kept raw (0..15). The layout is deliberately different
 * from SocketCAN so that tests catch any assumption about frame layout.
 *
 * The lock records its use so that tests can check that critical sections
 * are balanced and never nested.
 */

#ifndef J1939_TARGET_H
#define J1939_TARGET_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define J1939_MOCK_IR_EXID_SHIFT 3U  /**< Extended identifier position. */
#define J1939_MOCK_IR_STID_SHIFT 21U /**< Standard identifier position. */
#define J1939_MOCK_IR_IDE        0x4U
#define J1939_MOCK_IR_RTR        0x2U
#define J1939_MOCK_EXID_MASK     0x1FFFFFFFU
#define J1939_MOCK_STID_MASK     0x7FFU
#define J1939_MOCK_DLC_MASK      0xFU
#define J1939_MOCK_DATA_MAX      8U

/** Mock mailbox frame. */
typedef struct j1939_mock_frame {
	uint32_t ir;      /**< Identifier register: id, IDE, RTR. */
	uint8_t data[8];  /**< Payload. */
	uint8_t dlc;      /**< Raw data length code, 0..15. */
	uint8_t reserved; /**< Unused. */
} j1939_port_frame_t;

/** Lock with usage bookkeeping for tests. */
typedef struct j1939_mock_lock {
	uint32_t depth;     /**< Current nesting depth. */
	uint32_t max_depth; /**< Deepest nesting seen. */
	uint32_t count;     /**< Number of lock calls. */
} j1939_port_lock_t;

static inline bool j1939_port_frame_is_ext(const j1939_port_frame_t *f) {
	return (f->ir & J1939_MOCK_IR_IDE) != 0U;
}

static inline bool j1939_port_frame_is_rtr(const j1939_port_frame_t *f) {
	return (f->ir & J1939_MOCK_IR_RTR) != 0U;
}

static inline uint32_t j1939_port_frame_id_get(const j1939_port_frame_t *f) {
	uint32_t id;

	if (j1939_port_frame_is_ext(f)) {
		id = (f->ir >> J1939_MOCK_IR_EXID_SHIFT) & J1939_MOCK_EXID_MASK;
	} else {
		id = (f->ir >> J1939_MOCK_IR_STID_SHIFT) & J1939_MOCK_STID_MASK;
	}
	return id;
}

static inline uint8_t j1939_port_frame_len_get(const j1939_port_frame_t *f) {
	uint8_t len = (uint8_t)(f->dlc & J1939_MOCK_DLC_MASK);

	if (len > J1939_MOCK_DATA_MAX) {
		len = J1939_MOCK_DATA_MAX;
	}
	return len;
}

static inline const uint8_t *j1939_port_frame_data(const j1939_port_frame_t *f) {
	return f->data;
}

static inline void j1939_port_frame_build(j1939_port_frame_t *f, uint32_t id29, const uint8_t *data,
                                          uint8_t len) {
	uint8_t n = (len > J1939_MOCK_DATA_MAX) ? (uint8_t)J1939_MOCK_DATA_MAX : len;

	(void)memset(f, 0, sizeof(*f));
	f->ir = ((id29 & J1939_MOCK_EXID_MASK) << J1939_MOCK_IR_EXID_SHIFT) | J1939_MOCK_IR_IDE;
	f->dlc = n;
	if ((data != NULL) && (n > 0U)) {
		(void)memcpy(f->data, data, n);
	}
}

static inline void j1939_port_lock_init(j1939_port_lock_t *lock) {
	lock->depth = 0U;
	lock->max_depth = 0U;
	lock->count = 0U;
}

static inline void j1939_port_lock(j1939_port_lock_t *lock) {
	lock->depth++;
	lock->count++;
	if (lock->depth > lock->max_depth) {
		lock->max_depth = lock->depth;
	}
}

static inline void j1939_port_unlock(j1939_port_lock_t *lock) {
	lock->depth--;
}

#endif /* J1939_TARGET_H */
