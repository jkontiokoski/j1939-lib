/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_ring_priv.h
 * @brief Ring buffer index bookkeeping of the library's FIFOs.
 *
 * The functions do no locking: the stack uses its rings from one execution
 * context, and the optional frame queue wraps every call in the port lock.
 * One producer and one consumer: the producer uses j1939_ring_head() and
 * j1939_ring_push(), the consumer j1939_ring_tail() and j1939_ring_pop().
 */

#ifndef J1939_RING_PRIV_H
#define J1939_RING_PRIV_H

#include <stdbool.h>
#include <stdint.h>

#include "j1939/j1939_ring.h"

/**
 * @brief Initialises an empty ring.
 * @param r    Ring.
 * @param len  Number of slots, above 0.
 */
void j1939_ring_init(j1939_ring_t *r, uint16_t len);

/**
 * @brief Producer: finds the next free slot.
 * @param r      Ring.
 * @param index  Index of the free slot. Written only on success.
 * @return false if the ring is full.
 */
bool j1939_ring_head(const j1939_ring_t *r, uint16_t *index);

/**
 * @brief Producer: publishes the free slot found with j1939_ring_head().
 * @param r  Ring.
 * @return false if the ring is full.
 */
bool j1939_ring_push(j1939_ring_t *r);

/**
 * @brief Consumer: finds the oldest written slot.
 * @param r      Ring.
 * @param index  Index of the oldest slot. Written only on success.
 * @return false if the ring is empty.
 */
bool j1939_ring_tail(const j1939_ring_t *r, uint16_t *index);

/**
 * @brief Consumer: releases the oldest slot.
 * @param r  Ring.
 * @return false if the ring is empty.
 */
bool j1939_ring_pop(j1939_ring_t *r);

/**
 * @brief Counts the written slots.
 * @param r  Ring.
 * @return Slots written and not yet released.
 */
uint16_t j1939_ring_count(const j1939_ring_t *r);

/**
 * @brief Tells whether a slot is written and not yet released.
 * @param r      Ring.
 * @param index  Slot index, below the ring length.
 * @return true if the slot lies between tail and head.
 */
bool j1939_ring_holds(const j1939_ring_t *r, uint16_t index);

#endif /* J1939_RING_PRIV_H */
