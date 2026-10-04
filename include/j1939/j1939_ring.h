/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_ring.h
 * @brief Ring buffer index bookkeeping shared by the library's queues.
 *
 * The type is public only so that integrators can allocate the objects that
 * embed it. Its members and functions are private to the library.
 */

#ifndef J1939_RING_H
#define J1939_RING_H

#include <stdint.h>

/**
 * @addtogroup grp_stack
 * @{
 */

/** Ring indices of a queue with one producer and one consumer. */
typedef struct j1939_ring {
	uint16_t len;   /**< Number of slots. */
	uint16_t head;  /**< Next slot to be written. */
	uint16_t tail;  /**< Oldest written slot. */
	uint16_t count; /**< Number of written slots. */
} j1939_ring_t;

/** @} */

#endif /* J1939_RING_H */
