/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_queue.c
 * @brief Optional frame queue between two execution contexts, built as the
 *        j1939::queue target. See j1939_queue.h.
 */

#include "j1939/j1939_queue.h"

#include <stdbool.h>
#include <stddef.h>

#include "j1939_ring_priv.h"

j1939_ret_t j1939_queue_init(j1939_queue_t *q, j1939_port_frame_t *buf, uint16_t len) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((q != NULL) && (buf != NULL) && (len > 0U)) {
		q->buf = buf;
		j1939_ring_init(&q->ring, len);
		j1939_port_lock_init(&q->lock);
		ret = J1939_RET_OK;
	}
	return ret;
}

/**
 * @brief Producer side: finds the next free slot, with the index read in the lock.
 * @param q  Queue.
 * @return The free slot, or NULL if the queue is full.
 */
static j1939_port_frame_t *slot_free(j1939_queue_t *q) {
	j1939_port_frame_t *slot = NULL;
	uint16_t index = 0U;
	bool ok;

	j1939_port_lock(&q->lock);
	ok = j1939_ring_head(&q->ring, &index);
	j1939_port_unlock(&q->lock);
	if (ok) {
		slot = &q->buf[index];
	}
	return slot;
}

/**
 * @brief Producer side: publishes the free slot, in the lock.
 * @param q  Queue.
 * @return J1939_RET_OK, or J1939_RET_ERR_FULL if the queue is full.
 */
static j1939_ret_t slot_publish(j1939_queue_t *q) {
	bool ok;

	j1939_port_lock(&q->lock);
	ok = j1939_ring_push(&q->ring);
	j1939_port_unlock(&q->lock);
	return ok ? J1939_RET_OK : J1939_RET_ERR_FULL;
}

j1939_port_frame_t *j1939_queue_acquire(j1939_queue_t *q) {
	return (q != NULL) ? slot_free(q) : NULL;
}

j1939_ret_t j1939_queue_commit(j1939_queue_t *q) {
	return (q != NULL) ? slot_publish(q) : J1939_RET_ERR_ARG;
}

j1939_ret_t j1939_queue_put(j1939_queue_t *q, const j1939_port_frame_t *frame) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((q != NULL) && (frame != NULL)) {
		j1939_port_frame_t *slot = slot_free(q);

		if (slot == NULL) {
			ret = J1939_RET_ERR_FULL;
		} else {
			*slot = *frame;
			ret = slot_publish(q);
		}
	}
	return ret;
}

const j1939_port_frame_t *j1939_queue_peek(j1939_queue_t *q) {
	const j1939_port_frame_t *slot = NULL;

	if (q != NULL) {
		uint16_t index = 0U;
		bool ok;

		j1939_port_lock(&q->lock);
		ok = j1939_ring_tail(&q->ring, &index);
		j1939_port_unlock(&q->lock);
		if (ok) {
			slot = &q->buf[index];
		}
	}
	return slot;
}

j1939_ret_t j1939_queue_pop(j1939_queue_t *q) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if (q != NULL) {
		bool ok;

		j1939_port_lock(&q->lock);
		ok = j1939_ring_pop(&q->ring);
		j1939_port_unlock(&q->lock);
		ret = ok ? J1939_RET_OK : J1939_RET_ERR_EMPTY;
	}
	return ret;
}

uint16_t j1939_queue_count(j1939_queue_t *q) {
	uint16_t count = 0U;

	if (q != NULL) {
		j1939_port_lock(&q->lock);
		count = j1939_ring_count(&q->ring);
		j1939_port_unlock(&q->lock);
	}
	return count;
}
