/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_queue.h
 * @brief CAN frame queue over integrator-supplied storage.
 *
 * A first-in first-out queue of native frames for exactly one producer and
 * one consumer, which may run in different execution contexts (ISR, thread).
 * Index updates are protected by the port lock.
 *
 * The producer either writes a frame directly into the next free slot
 * (j1939_queue_acquire(), then j1939_queue_commit()), or copies one in with
 * j1939_queue_put(). The consumer reads the oldest frame in place with
 * j1939_queue_peek() and releases it with j1939_queue_pop().
 */

#ifndef J1939_QUEUE_H
#define J1939_QUEUE_H

#include <stdint.h>

#include "j1939/j1939_port_contract.h"
#include "j1939/j1939_ret.h"

/** Frame queue. Members are private to the library. */
typedef struct j1939_queue {
	j1939_port_frame_t *buf; /**< Integrator storage. */
	uint16_t len;            /**< Number of slots in buf. */
	uint16_t head;           /**< Next slot to be written. */
	uint16_t tail;           /**< Oldest written slot. */
	uint16_t count;          /**< Number of written slots. */
	j1939_port_lock_t lock;  /**< Protects head, tail and count. */
} j1939_queue_t;

/**
 * @brief Initialises an empty queue over integrator storage.
 *
 * @param q    Queue to initialise.
 * @param buf  Storage for @p len frames. Owned by the integrator, must outlive the queue.
 * @param len  Number of frames in @p buf, at least 1. All slots are usable.
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG on a NULL pointer or zero length.
 */
j1939_ret_t j1939_queue_init(j1939_queue_t *q, j1939_port_frame_t *buf, uint16_t len);

/**
 * @brief Producer: returns the next free slot without claiming it.
 *
 * The producer fills the slot and then calls j1939_queue_commit(). Calling
 * this again before committing returns the same slot.
 *
 * @return Free slot, or NULL if the queue is full or @p q is NULL.
 */
j1939_port_frame_t *j1939_queue_acquire(j1939_queue_t *q);

/**
 * @brief Producer: publishes the slot returned by j1939_queue_acquire().
 *
 * @return J1939_RET_OK, J1939_RET_ERR_FULL if there is no free slot, or
 *         J1939_RET_ERR_ARG if @p q is NULL.
 */
j1939_ret_t j1939_queue_commit(j1939_queue_t *q);

/**
 * @brief Producer: copies @p frame into the queue.
 *
 * @return J1939_RET_OK, J1939_RET_ERR_FULL if there is no free slot, or
 *         J1939_RET_ERR_ARG on a NULL pointer.
 */
j1939_ret_t j1939_queue_put(j1939_queue_t *q, const j1939_port_frame_t *frame);

/**
 * @brief Consumer: returns the oldest frame without removing it.
 *
 * @return Oldest frame, or NULL if the queue is empty or @p q is NULL.
 */
const j1939_port_frame_t *j1939_queue_peek(j1939_queue_t *q);

/**
 * @brief Consumer: removes the oldest frame.
 *
 * @return J1939_RET_OK, J1939_RET_ERR_EMPTY if the queue is empty, or
 *         J1939_RET_ERR_ARG if @p q is NULL.
 */
j1939_ret_t j1939_queue_pop(j1939_queue_t *q);

/**
 * @brief Number of frames in the queue.
 *
 * @return Frame count, 0 if @p q is NULL.
 */
uint16_t j1939_queue_count(j1939_queue_t *q);

#endif /* J1939_QUEUE_H */
