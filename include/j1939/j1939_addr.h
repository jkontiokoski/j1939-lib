/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_addr.h
 * @brief J1939/81 address claiming.
 *
 * Every Controller Application claims an address before it transmits. The
 * stack runs the procedure in j1939_process(); the application only reads
 * the result with j1939_addr_get().
 *
 * - The first j1939_process() after j1939_ca_add() sends Address Claimed
 *   (PGN 60928, to the global address, the NAME as data) from the preferred
 *   address. It is retried by later calls while the tx queue is full.
 * - A CA whose address is outside J1939_ADDR_SELF_CFG_MIN..MAX may transmit
 *   as soon as its Address Claimed is queued. A CA claiming an address in
 *   that range waits J1939_ADDR_CLAIM_WAIT_US first, so that a contending CA
 *   can answer.
 * - An Address Claimed from another node for an address a CA holds is
 *   arbitrated on the NAME: the numerically lower NAME wins. The winner
 *   sends Address Claimed again. The loser, if its NAME is arbitrary address
 *   capable, claims the first free self-configurable address; otherwise it
 *   sends Cannot Claim (Address Claimed from J1939_ADDR_NULL) after a delay
 *   of 0..153 ms derived from its NAME.
 * - A Request for Address Claimed, global or to a CA's address, is answered
 *   by the stack for every CA it concerns: Address Claimed, or Cannot Claim
 *   after the delay for a CA without an address. It is not delivered to the
 *   application.
 * - j1939_send() returns J1939_RET_ERR_NO_ADDRESS until the claim succeeds.
 *   j1939_request_send() for PGN 60928 is always allowed; it uses
 *   J1939_ADDR_NULL while the CA has not claimed an address, and the stack
 *   also answers the Request for its own CAs.
 *
 * Received Address Claimed messages, including Cannot Claim, are also
 * delivered to the application if PGN 60928 is in the rx_pgns list.
 */

#ifndef J1939_ADDR_H
#define J1939_ADDR_H

#include <stdint.h>

#include "j1939/j1939_ret.h"
#include "j1939/j1939_stack.h"

#define J1939_PGN_ADDRESS_CLAIMED 0xEE00U /**< Address Claimed / Cannot Claim, PGN 60928. */

#define J1939_ADDR_CLAIM_WAIT_US 250000U /**< Contention wait for self-configurable addresses. */

/** Cannot Claim delay unit; the delay is 0..255 units, 0..153 ms. */
#define J1939_ADDR_CANNOT_CLAIM_STEP_US 600U

/**
 * @brief Reads the address and claim state of a Controller Application.
 *
 * The address can change at runtime: a CA that loses its address to another
 * node moves to another address or stops transmitting.
 *
 * @param s        Stack.
 * @param ca       Controller Application.
 * @param address  Claimed address, or the address being claimed in state
 *                 J1939_ADDR_STATE_CLAIMING; J1939_ADDR_NULL in the other states.
 * @param state    Claim state.
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG on a NULL pointer or an unknown CA.
 */
j1939_ret_t j1939_addr_get(const j1939_t *s, j1939_ca_id_t ca, uint8_t *address,
                           j1939_addr_state_t *state);

#endif /* J1939_ADDR_H */
