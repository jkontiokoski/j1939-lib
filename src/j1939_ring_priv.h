/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/*
 * Ring buffer index bookkeeping. The functions do no locking: the stack uses
 * its rings from one execution context, and the optional frame queue wraps
 * every call in the port lock.
 */

#ifndef J1939_RING_PRIV_H
#define J1939_RING_PRIV_H

#include <stdbool.h>
#include <stdint.h>

#include "j1939/j1939_ring.h"

/* Initialises an empty ring of len slots (len > 0). */
void j1939_ring_init(j1939_ring_t *r, uint16_t len);

/* Producer: index of the next free slot. Returns false if the ring is full. */
bool j1939_ring_head(const j1939_ring_t *r, uint16_t *index);

/* Producer: publishes the free slot. Returns false if the ring is full. */
bool j1939_ring_push(j1939_ring_t *r);

/* Consumer: index of the oldest written slot. Returns false if the ring is empty. */
bool j1939_ring_tail(const j1939_ring_t *r, uint16_t *index);

/* Consumer: releases the oldest slot. Returns false if the ring is empty. */
bool j1939_ring_pop(j1939_ring_t *r);

/* Number of written slots. */
uint16_t j1939_ring_count(const j1939_ring_t *r);

/* Returns true if slot index (< len) is written and not yet released. */
bool j1939_ring_holds(const j1939_ring_t *r, uint16_t index);

#endif /* J1939_RING_PRIV_H */
