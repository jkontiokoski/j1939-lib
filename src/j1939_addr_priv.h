/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/* Address claiming internals used by the stack and the Request module. */

#ifndef J1939_ADDR_PRIV_H
#define J1939_ADDR_PRIV_H

#include <stdbool.h>
#include <stdint.h>

#include "j1939/j1939_msg.h"
#include "j1939/j1939_stack.h"

/* Forgets the addresses claimed by other nodes. */
void j1939_addr_init(j1939_t *s);

/* Sets up a new CA: its claim starts with the next j1939_addr_process(). */
void j1939_addr_ca_init(j1939_ca_t *ca, const j1939_ca_cfg_t *cfg);

/* Sends pending claims and advances the claim timers. */
void j1939_addr_process(j1939_t *s, uint32_t elapsed_us);

/* Returns true if a CA of the stack holds address, so frames to it are received. */
bool j1939_addr_held(const j1939_t *s, uint8_t address);

/* Returns true if the CA has claimed its address and may transmit. */
bool j1939_addr_tx_allowed(const j1939_ca_t *ca);

/* Handles a received Address Claimed or Cannot Claim, whatever its destination. */
void j1939_addr_claim_handle(j1939_t *s, uint32_t id, const uint8_t *data, uint8_t len);

/* Answers a Request for Address Claimed sent to da for every CA it concerns. */
void j1939_addr_request_handle(j1939_t *s, uint8_t da);

/*
 * Sends a Request for Address Claimed from a CA, from J1939_ADDR_NULL while
 * the CA has not claimed an address, and answers it for the stack's own CAs.
 */
j1939_ret_t j1939_addr_request_send(j1939_t *s, j1939_ca_id_t ca, const j1939_msg_t *msg);

#endif /* J1939_ADDR_PRIV_H */
