/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

#include "j1939/j1939_queue.h"

#include <stddef.h>

static uint16_t next_index(const j1939_queue_t *q, uint16_t index) {
	uint16_t next = (uint16_t)(index + 1U);

	if (next >= q->len) {
		next = 0U;
	}
	return next;
}

j1939_ret_t j1939_queue_init(j1939_queue_t *q, j1939_port_frame_t *buf, uint16_t len) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((q != NULL) && (buf != NULL) && (len > 0U)) {
		q->buf = buf;
		q->len = len;
		q->head = 0U;
		q->tail = 0U;
		q->count = 0U;
		j1939_port_lock_init(&q->lock);
		ret = J1939_RET_OK;
	}
	return ret;
}

j1939_port_frame_t *j1939_queue_acquire(j1939_queue_t *q) {
	j1939_port_frame_t *slot = NULL;

	if (q != NULL) {
		j1939_port_lock(&q->lock);
		if (q->count < q->len) {
			slot = &q->buf[q->head];
		}
		j1939_port_unlock(&q->lock);
	}
	return slot;
}

j1939_ret_t j1939_queue_commit(j1939_queue_t *q) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if (q != NULL) {
		j1939_port_lock(&q->lock);
		if (q->count < q->len) {
			q->head = next_index(q, q->head);
			q->count++;
			ret = J1939_RET_OK;
		} else {
			ret = J1939_RET_ERR_FULL;
		}
		j1939_port_unlock(&q->lock);
	}
	return ret;
}

j1939_ret_t j1939_queue_put(j1939_queue_t *q, const j1939_port_frame_t *frame) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((q != NULL) && (frame != NULL)) {
		j1939_port_frame_t *slot = j1939_queue_acquire(q);

		if (slot == NULL) {
			ret = J1939_RET_ERR_FULL;
		} else {
			*slot = *frame;
			ret = j1939_queue_commit(q);
		}
	}
	return ret;
}

const j1939_port_frame_t *j1939_queue_peek(j1939_queue_t *q) {
	const j1939_port_frame_t *slot = NULL;

	if (q != NULL) {
		j1939_port_lock(&q->lock);
		if (q->count > 0U) {
			slot = &q->buf[q->tail];
		}
		j1939_port_unlock(&q->lock);
	}
	return slot;
}

j1939_ret_t j1939_queue_pop(j1939_queue_t *q) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if (q != NULL) {
		j1939_port_lock(&q->lock);
		if (q->count > 0U) {
			q->tail = next_index(q, q->tail);
			q->count--;
			ret = J1939_RET_OK;
		} else {
			ret = J1939_RET_ERR_EMPTY;
		}
		j1939_port_unlock(&q->lock);
	}
	return ret;
}

uint16_t j1939_queue_count(j1939_queue_t *q) {
	uint16_t count = 0U;

	if (q != NULL) {
		j1939_port_lock(&q->lock);
		count = q->count;
		j1939_port_unlock(&q->lock);
	}
	return count;
}
