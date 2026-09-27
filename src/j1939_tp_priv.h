/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/* Transport protocol entry points used by the stack. */

#ifndef J1939_TP_PRIV_H
#define J1939_TP_PRIV_H

#include <stdbool.h>
#include <stdint.h>

#include "j1939/j1939_msg.h"
#include "j1939/j1939_ret.h"
#include "j1939/j1939_stack.h"

/* Returns true if the transport protocol members of cfg are valid. */
bool j1939_tp_cfg_valid(const j1939_cfg_t *cfg);

/* Resets the sessions, takes over the buffers of cfg and clears the transport protocol counters. */
void j1939_tp_init(j1939_t *s, const j1939_cfg_t *cfg);

/* Handles a received TP.CM or TP.DT frame addressed to this stack or to all nodes. */
void j1939_tp_handle(j1939_t *s, uint32_t id, const uint8_t *data, uint8_t len);

/* Advances the session timers and sends due data packets. */
void j1939_tp_process(j1939_t *s, uint32_t elapsed_us);

/*
 * Starts sending msg (payload checked by j1939_send(), len above 8) from address sa.
 * Returns J1939_RET_OK, J1939_RET_ERR_ARG, J1939_RET_ERR_BUSY or J1939_RET_ERR_FULL.
 */
j1939_ret_t j1939_tp_send(j1939_t *s, uint8_t sa, const j1939_msg_t *msg);

#endif /* J1939_TP_PRIV_H */
