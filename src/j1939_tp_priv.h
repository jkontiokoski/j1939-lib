/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_tp_priv.h
 * @brief Transport protocol entry points used by the stack.
 */

#ifndef J1939_TP_PRIV_H
#define J1939_TP_PRIV_H

#include <stdbool.h>
#include <stdint.h>

#include "j1939/j1939_msg.h"
#include "j1939/j1939_ret.h"
#include "j1939/j1939_stack.h"

/**
 * @brief Checks the transport protocol members of a stack configuration.
 * @param cfg  Stack configuration.
 * @return true if the buffer pointers and lengths are consistent.
 */
bool j1939_tp_cfg_valid(const j1939_cfg_t *cfg);

/**
 * @brief Resets the sessions, takes over the buffers of the configuration and clears the
 *        transport protocol counters.
 * @param s    Stack.
 * @param cfg  Stack configuration, already validated.
 */
void j1939_tp_init(j1939_t *s, const j1939_cfg_t *cfg);

/**
 * @brief Handles a received TP.CM or TP.DT frame addressed to this stack or to all nodes.
 * @param s     Stack.
 * @param id    Identifier of the frame.
 * @param data  Payload.
 * @param len   Payload length in bytes.
 */
void j1939_tp_handle(j1939_t *s, uint32_t id, const uint8_t *data, uint8_t len);

/**
 * @brief Ends the sessions of an address a CA lost, without a Connection Abort.
 *
 * The address is no longer the stack's. The sessions are counted as aborted.
 *
 * @param s        Stack.
 * @param address  The lost address.
 */
void j1939_tp_address_lost(j1939_t *s, uint8_t address);

/**
 * @brief Advances the session timers and sends due data packets.
 * @param s           Stack.
 * @param elapsed_us  Time since the previous j1939_process(), in microseconds.
 */
void j1939_tp_process(j1939_t *s, uint32_t elapsed_us);

/**
 * @brief Starts sending a multi-packet message.
 * @param s    Stack.
 * @param sa   Source address.
 * @param msg  Message; payload checked by j1939_send(), longer than 8 bytes.
 * @return J1939_RET_OK, J1939_RET_ERR_ARG, J1939_RET_ERR_BUSY or J1939_RET_ERR_FULL,
 *         as described for j1939_send().
 */
j1939_ret_t j1939_tp_send(j1939_t *s, uint8_t sa, const j1939_msg_t *msg);

#endif /* J1939_TP_PRIV_H */
