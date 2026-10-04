/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_queue.h
 * @brief Optional helper: CAN frame queue between two execution contexts.
 *
 * The stack does not use this queue. It is for integrators whose CAN driver
 * has no frame FIFO of its own, typically bare metal, where a receive
 * interrupt has to hand frames to the task that runs the stack:
 *
 * @code
 * static j1939_port_frame_t rx_storage[32];
 * static j1939_queue_t rx_q;
 *
 * j1939_queue_init(&rx_q, rx_storage, 32);
 *
 * void can_rx_isr(void) {                         // producer
 *     j1939_port_frame_t *slot = j1939_queue_acquire(&rx_q);
 *     if (slot != NULL) {
 *         driver_read(slot);
 *         j1939_queue_commit(&rx_q);
 *     }
 * }
 *
 * const j1939_port_frame_t *f;                    // consumer: the stack's task
 * while ((f = j1939_queue_peek(&rx_q)) != NULL) {
 *     j1939_rx(&stack, f);
 *     j1939_queue_pop(&rx_q);
 * }
 * @endcode
 *
 * The queue is a first-in first-out queue of native frames over integrator
 * storage, for exactly one producer and one consumer. Index updates run
 * inside the port lock; frame contents are written and read outside it.
 *
 * It is built as its own library target, j1939::queue, and needs the lock
 * declared below in the port's j1939_target.h. A port that does not use the
 * queue does not define the lock.
 */

#ifndef J1939_QUEUE_H
#define J1939_QUEUE_H

#include <stdint.h>

#include "j1939/j1939_port_contract.h"
#include "j1939/j1939_ret.h"
#include "j1939/j1939_ring.h"

/* j1939_port_lock_t: lock object embedded in each queue, typedef by the port. */

/**
 * @brief Initialises a lock object. Called once, before any other use.
 *
 * @param lock  Lock embedded in a frame queue.
 */
static inline void j1939_port_lock_init(j1939_port_lock_t *lock);

/**
 * @brief Enters a critical section.
 *
 * Lock and unlock must also act as compiler and memory barriers, so that
 * frame contents written before a queue update are visible to the other
 * execution context. Critical sections are short and never nested.
 *
 * @param lock  Lock embedded in a frame queue.
 */
static inline void j1939_port_lock(j1939_port_lock_t *lock);

/**
 * @brief Leaves a critical section.
 *
 * @param lock  Lock embedded in a frame queue.
 */
static inline void j1939_port_unlock(j1939_port_lock_t *lock);

/** Frame queue. Members are private. */
typedef struct j1939_queue {
	j1939_port_frame_t *buf; /**< Integrator storage. */
	j1939_ring_t ring;       /**< Indices into buf. */
	j1939_port_lock_t lock;  /**< Protects ring. */
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
