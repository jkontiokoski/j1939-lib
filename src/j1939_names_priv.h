/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_names_priv.h
 * @brief NAME table entry points used by the stack and address claiming.
 */

#ifndef J1939_NAMES_PRIV_H
#define J1939_NAMES_PRIV_H

#include <stdbool.h>
#include <stdint.h>

#include "j1939/j1939_stack.h"

/**
 * @brief Removes the NAME table and clears its counter.
 * @param s  Stack.
 */
void j1939_names_stack_init(j1939_t *s);

/**
 * @brief Records an Address Claimed of another node.
 *
 * Called after the stack's own arbitration, only if no CA of the stack holds
 * the address. A NAME of the stack's own CAs is not recorded.
 *
 * @param s        Stack.
 * @param name     NAME from the message.
 * @param address  Claimed address, 0..253.
 */
void j1939_names_claimed(j1939_t *s, uint64_t name, uint8_t address);

/**
 * @brief Records a Cannot Claim of another node: present, without an address.
 * @param s     Stack.
 * @param name  NAME from the message.
 */
void j1939_names_cannot_claim(j1939_t *s, uint64_t name);

/**
 * @brief Notes that the stack sent a global Request for Address Claimed.
 *
 * Its answers fill the table as the startup Request would, so the startup
 * Request is no longer due: after a CA's request before claim, after a
 * Request of the application, and after the startup Request itself.
 *
 * @param s  Stack.
 */
void j1939_names_global_request(j1939_t *s);

/**
 * @brief Sends the pending Requests for Address Claimed and advances the hold timer.
 * @param s           Stack.
 * @param elapsed_us  Time since the previous j1939_process(), in microseconds.
 */
void j1939_names_process(j1939_t *s, uint32_t elapsed_us);

#endif /* J1939_NAMES_PRIV_H */
