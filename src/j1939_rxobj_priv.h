/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_rxobj_priv.h
 * @brief Receive object entry points used by the stack and the transport protocol.
 */

#ifndef J1939_RXOBJ_PRIV_H
#define J1939_RXOBJ_PRIV_H

#include <stdbool.h>
#include <stdint.h>

#include "j1939/j1939_stack.h"

/**
 * @brief Removes the receive objects and clears their counters.
 * @param s  Stack.
 */
void j1939_rxobj_stack_init(j1939_t *s);

/**
 * @brief Tells whether a receive object takes a PGN from a sender.
 * @param s    Stack.
 * @param pgn  PGN.
 * @param sa   Source address.
 * @return true if an object is configured for @p pgn and @p sa, or for
 *         @p pgn and the NAME the NAME table records at @p sa.
 */
bool j1939_rxobj_wanted(const j1939_t *s, uint32_t pgn, uint8_t sa);

/**
 * @brief Stores a received message in every object for its PGN and sender.
 * @param s     Stack.
 * @param pgn   PGN of the message.
 * @param sa    Source address of the message.
 * @param data  Payload.
 * @param len   Payload length in bytes.
 */
void j1939_rxobj_handle(j1939_t *s, uint32_t pgn, uint8_t sa, const uint8_t *data, uint16_t len);

/**
 * @brief Advances the age of the received payloads and detects timeouts.
 * @param s           Stack.
 * @param elapsed_us  Time since the previous j1939_process(), in microseconds.
 */
void j1939_rxobj_process(j1939_t *s, uint32_t elapsed_us);

#endif /* J1939_RXOBJ_PRIV_H */
