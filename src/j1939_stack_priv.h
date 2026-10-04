/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_stack_priv.h
 * @brief Stack internals shared between the stack modules.
 */

#ifndef J1939_STACK_PRIV_H
#define J1939_STACK_PRIV_H

#include <stdbool.h>
#include <stdint.h>

#include "j1939/j1939_stack.h"

/**
 * @brief Tells whether a PGN is valid.
 * @param pgn  PGN.
 * @return true if @p pgn is at most J1939_PGN_MAX and, for PDU1, its lowest byte is 0.
 */
bool j1939_stack_pgn_valid(uint32_t pgn);

/**
 * @brief Tells whether a PGN is in a list.
 * @param list  PGN list; may be NULL if @p len is 0.
 * @param len   Entries in @p list.
 * @param pgn   PGN.
 * @return true if found.
 */
bool j1939_stack_pgn_listed(const uint32_t *list, uint16_t len, uint32_t pgn);

/**
 * @brief Stores a received frame as an application message.
 *
 * Counts an overflow in rx_msg_overflow if no message slot is free.
 *
 * @param s     Stack.
 * @param id    Identifier of the frame.
 * @param data  Payload.
 * @param len   Payload length in bytes, at most 8.
 */
void j1939_stack_deliver(j1939_t *s, uint32_t id, const uint8_t *data, uint8_t len);

/**
 * @brief Queues a single frame message.
 * @param s    Stack.
 * @param msg  Message with a valid payload of at most 8 bytes; msg->sa is ignored.
 * @param sa   Source address.
 * @return J1939_RET_OK, J1939_RET_ERR_ARG for an invalid identifier, or
 *         J1939_RET_ERR_FULL if the tx queue is full.
 */
j1939_ret_t j1939_stack_send(j1939_t *s, const j1939_msg_t *msg, uint8_t sa);

/**
 * @brief Queues a frame.
 * @param s     Stack.
 * @param id    29-bit identifier.
 * @param data  Payload.
 * @param len   Payload length in bytes, at most 8.
 * @return J1939_RET_OK, or J1939_RET_ERR_FULL if the tx queue is full.
 */
j1939_ret_t j1939_stack_tx(j1939_t *s, uint32_t id, const uint8_t *data, uint8_t len);

/**
 * @brief Handles a received Request addressed to this stack or to all nodes.
 * @param s     Stack.
 * @param id    Identifier of the Request.
 * @param data  Payload: the requested PGN in the first three bytes.
 * @param len   Payload length in bytes.
 */
void j1939_request_handle(j1939_t *s, uint32_t id, const uint8_t *data, uint8_t len);

#endif /* J1939_STACK_PRIV_H */
