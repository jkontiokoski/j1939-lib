/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/* Transmit object entry points used by the stack and the Request module. */

#ifndef J1939_TXOBJ_PRIV_H
#define J1939_TXOBJ_PRIV_H

#include <stdbool.h>
#include <stdint.h>

#include "j1939/j1939_stack.h"

/* Removes the transmit objects and clears their counters. */
void j1939_txobj_stack_init(j1939_t *s);

/*
 * Handles a Request for pgn (id is the Request's identifier). Returns true if
 * a transmit object of a CA addressed by it has the PGN, false if the Request
 * module handles it as usual.
 */
bool j1939_txobj_request_handle(j1939_t *s, uint32_t id, uint32_t pgn);

/* Sends due objects and pending answers; advances the timers. */
void j1939_txobj_process(j1939_t *s, uint32_t elapsed_us);

#endif /* J1939_TXOBJ_PRIV_H */
