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
}

bool j1939_ring_head(const j1939_ring_t *r, uint16_t *index) {
	bool ok = r->count < r->len;

	if (ok) {
		*index = r->head;
	}
	return ok;
}

bool j1939_ring_push(j1939_ring_t *r) {
	bool ok = r->count < r->len;

	if (ok) {
		r->head = next_index(r, r->head);
		r->count++;
	}
	return ok;
}

bool j1939_ring_tail(const j1939_ring_t *r, uint16_t *index) {
	bool ok = r->count > 0U;

	if (ok) {
		*index = r->tail;
	}
	return ok;
}

bool j1939_ring_pop(j1939_ring_t *r) {
	bool ok = r->count > 0U;

	if (ok) {
		r->tail = next_index(r, r->tail);
		r->count--;
	}
	return ok;
}

uint16_t j1939_ring_count(const j1939_ring_t *r) {
	return r->count;
}

bool j1939_ring_holds(const j1939_ring_t *r, uint16_t index) {
	bool held;

	if (index >= r->tail) {
		held = (uint16_t)(index - r->tail) < r->count;
	} else {
		held = (uint16_t)((index + r->len) - r->tail) < r->count;
	}
	return held;
}
