/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/* Diagnostics entry points used by the stack and the Request module. */

#ifndef J1939_DM_PRIV_H
#define J1939_DM_PRIV_H

#include <stdbool.h>
#include <stdint.h>

#include "j1939/j1939_stack.h"

/* Disables diagnostics for every CA and clears the diagnostics counters. */
void j1939_dm_stack_init(j1939_t *s);

/*
 * Handles a Request for pgn (id is the Request's identifier). Returns true if
 * the Request concerns a diagnostic message that a CA addressed by it
 * supports, false if the Request module handles it as usual.
 */
bool j1939_dm_request_handle(j1939_t *s, uint32_t id, uint32_t pgn);

/* Sends due DM1s, pending answers and acknowledgements; advances the timers. */
void j1939_dm_process(j1939_t *s, uint32_t elapsed_us);

#endif /* J1939_DM_PRIV_H */
