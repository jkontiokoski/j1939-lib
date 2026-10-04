/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_txobj_priv.h
 * @brief Transmit object entry points used by the stack and the Request module.
 */

#ifndef J1939_TXOBJ_PRIV_H
#define J1939_TXOBJ_PRIV_H

#include <stdbool.h>
#include <stdint.h>

#include "j1939/j1939_stack.h"

/**
 * @brief Removes the transmit objects and clears their counters.
 * @param s  Stack.
 */
void j1939_txobj_stack_init(j1939_t *s);

/**
 * @brief Handles a Request for the PGN of a transmit object.
 * @param s    Stack.
 * @param id   Identifier of the Request.
 * @param pgn  Requested PGN.
 * @return true if a transmit object of a CA addressed by the Request has the
 *         PGN, false if the Request module handles it as usual.
 */
bool j1939_txobj_request_handle(j1939_t *s, uint32_t id, uint32_t pgn);

/**
 * @brief Sends due objects and pending answers; advances the timers.
 * @param s           Stack.
 * @param elapsed_us  Time since the previous j1939_process(), in microseconds.
 */
void j1939_txobj_process(j1939_t *s, uint32_t elapsed_us);

#endif /* J1939_TXOBJ_PRIV_H */
