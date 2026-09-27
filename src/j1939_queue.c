/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

#include "j1939/j1939_queue.h"

#include <stddef.h>

#include "j1939_ring_priv.h"

j1939_ret_t j1939_queue_init(j1939_queue_t *q, j1939_port_frame_t *buf, uint16_t len) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((q != NULL) && (buf != NULL) && (len > 0U)) {
		q->buf = buf;
		j1939_ring_init(&q->ring, len);
		ret = J1939_RET_OK;
	}
	return ret;
}

j1939_port_frame_t *j1939_queue_acquire(j1939_queue_t *q) {
	j1939_port_frame_t *slot = NULL;
	uint16_t index;

	if ((q != NULL) && j1939_ring_head(&q->ring, &index)) {
		slot = &q->buf[index];
	}
	return slot;
}

j1939_ret_t j1939_queue_commit(j1939_queue_t *q) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if (q != NULL) {
		ret = j1939_ring_push(&q->ring) ? J1939_RET_OK : J1939_RET_ERR_FULL;
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
	uint16_t index;

	if ((q != NULL) && j1939_ring_tail(&q->ring, &index)) {
		slot = &q->buf[index];
	}
	return slot;
}

j1939_ret_t j1939_queue_pop(j1939_queue_t *q) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if (q != NULL) {
		ret = j1939_ring_pop(&q->ring) ? J1939_RET_OK : J1939_RET_ERR_EMPTY;
	}
	return ret;
}

uint16_t j1939_queue_count(j1939_queue_t *q) {
	uint16_t count = 0U;

	if (q != NULL) {
		count = j1939_ring_count(&q->ring);
	}
	return count;
}
