/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/* Receive object entry points used by the stack and the transport protocol. */

#ifndef J1939_RXOBJ_PRIV_H
#define J1939_RXOBJ_PRIV_H

#include <stdbool.h>
#include <stdint.h>

#include "j1939/j1939_stack.h"

/* Removes the receive objects and clears their counters. */
void j1939_rxobj_stack_init(j1939_t *s);

/* Returns true if a receive object takes pgn from sa. */
bool j1939_rxobj_wanted(const j1939_t *s, uint32_t pgn, uint8_t sa);

/* Stores a received message in the object for pgn and sa, if there is one. */
void j1939_rxobj_handle(j1939_t *s, uint32_t pgn, uint8_t sa, const uint8_t *data, uint16_t len);

/* Advances the age of the received payloads and detects timeouts. */
void j1939_rxobj_process(j1939_t *s, uint32_t elapsed_us);

#endif /* J1939_RXOBJ_PRIV_H */
