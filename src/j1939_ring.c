/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

#include "j1939_ring_priv.h"

static uint16_t next_index(const j1939_ring_t *r, uint16_t index) {
	uint16_t next = (uint16_t)(index + 1U);

	if (next >= r->len) {
		next = 0U;
	}
	return next;
}

void j1939_ring_init(j1939_ring_t *r, uint16_t len) {
	r->len = len;
	r->head = 0U;
	r->tail = 0U;
	r->count = 0U;
	j1939_port_lock_init(&r->lock);
}

bool j1939_ring_head(j1939_ring_t *r, uint16_t *index) {
	bool ok;

	j1939_port_lock(&r->lock);
	ok = r->count < r->len;
	if (ok) {
		*index = r->head;
	}
	j1939_port_unlock(&r->lock);
	return ok;
}

bool j1939_ring_push(j1939_ring_t *r) {
	bool ok;

	j1939_port_lock(&r->lock);
	ok = r->count < r->len;
	if (ok) {
		r->head = next_index(r, r->head);
		r->count++;
	}
	j1939_port_unlock(&r->lock);
	return ok;
}

bool j1939_ring_tail(j1939_ring_t *r, uint16_t *index) {
	bool ok;

	j1939_port_lock(&r->lock);
	ok = r->count > 0U;
	if (ok) {
		*index = r->tail;
	}
	j1939_port_unlock(&r->lock);
	return ok;
}

bool j1939_ring_pop(j1939_ring_t *r) {
	bool ok;

	j1939_port_lock(&r->lock);
	ok = r->count > 0U;
	if (ok) {
		r->tail = next_index(r, r->tail);
		r->count--;
	}
	j1939_port_unlock(&r->lock);
	return ok;
}

uint16_t j1939_ring_count(j1939_ring_t *r) {
	uint16_t count;

	j1939_port_lock(&r->lock);
	count = r->count;
	j1939_port_unlock(&r->lock);
	return count;
}
