/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/* Stack internals shared between the stack modules. */

#ifndef J1939_STACK_PRIV_H
#define J1939_STACK_PRIV_H

#include <stdbool.h>
#include <stdint.h>

#include "j1939/j1939_stack.h"

/* Returns true if pgn is in the list. */
bool j1939_stack_pgn_listed(const uint32_t *list, uint16_t len, uint32_t pgn);

/* Stores a received frame as an application message. Counts an overflow if no slot is free. */
void j1939_stack_deliver(j1939_t *s, uint32_t id, const uint8_t *data, uint8_t len);

/* Queues a frame. Returns J1939_RET_ERR_FULL if the tx queue is full. */
j1939_ret_t j1939_stack_tx(j1939_t *s, uint32_t id, const uint8_t *data, uint8_t len);

/* Handles a received Request addressed to this stack or to all nodes. */
void j1939_request_handle(j1939_t *s, uint32_t id, const uint8_t *data, uint8_t len);

#endif /* J1939_STACK_PRIV_H */
