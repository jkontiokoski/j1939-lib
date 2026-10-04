/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_dm_priv.h
 * @brief Diagnostics entry points used by the stack and the Request module.
 */

#ifndef J1939_DM_PRIV_H
#define J1939_DM_PRIV_H

#include <stdbool.h>
#include <stdint.h>

#include "j1939/j1939_stack.h"

/**
 * @brief Disables diagnostics for every CA and clears the diagnostics counters.
 * @param s  Stack.
 */
void j1939_dm_stack_init(j1939_t *s);

/**
 * @brief Handles a Request for a diagnostic message.
 * @param s    Stack.
 * @param id   Identifier of the Request.
 * @param pgn  Requested PGN.
 * @return true if the Request concerns a diagnostic message that a CA
 *         addressed by it supports, false if the Request module handles it
 *         as usual.
 */
bool j1939_dm_request_handle(j1939_t *s, uint32_t id, uint32_t pgn);

/**
 * @brief Sends due DM1s, pending answers and acknowledgements; advances the timers.
 * @param s           Stack.
 * @param elapsed_us  Time since the previous j1939_process(), in microseconds.
 */
void j1939_dm_process(j1939_t *s, uint32_t elapsed_us);

#endif /* J1939_DM_PRIV_H */
