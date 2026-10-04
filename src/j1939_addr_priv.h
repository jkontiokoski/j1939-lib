/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_addr_priv.h
 * @brief Address claiming internals used by the stack and the Request module.
 */

#ifndef J1939_ADDR_PRIV_H
#define J1939_ADDR_PRIV_H

#include <stdbool.h>
#include <stdint.h>

#include "j1939/j1939_msg.h"
#include "j1939/j1939_stack.h"

/**
 * @brief Forgets the addresses claimed by other nodes.
 * @param s  Stack.
 */
void j1939_addr_init(j1939_t *s);

/**
 * @brief Sets up a new CA: its claim starts with the next j1939_addr_process().
 * @param ca   CA state to initialise.
 * @param cfg  Its configuration, already validated by j1939_ca_add().
 */
void j1939_addr_ca_init(j1939_ca_t *ca, const j1939_ca_cfg_t *cfg);

/**
 * @brief Sends pending claims and advances the claim timers.
 * @param s           Stack.
 * @param elapsed_us  Time since the previous j1939_process(), in microseconds.
 */
void j1939_addr_process(j1939_t *s, uint32_t elapsed_us);

/**
 * @brief Tells whether a CA of the stack holds an address, so frames to it are received.
 * @param s        Stack.
 * @param address  Address.
 * @return true if a CA in CLAIMING or CLAIMED state uses @p address.
 */
bool j1939_addr_held(const j1939_t *s, uint8_t address);

/**
 * @brief Tells whether a CA of the stack has claimed an address and may transmit from it.
 * @param s        Stack.
 * @param address  Address.
 * @return true if a CA in CLAIMED state uses @p address.
 */
bool j1939_addr_claimed(const j1939_t *s, uint8_t address);

/**
 * @brief Tells whether a CA has claimed its address and may transmit.
 * @param ca  CA.
 * @return true in CLAIMED state.
 */
bool j1939_addr_tx_allowed(const j1939_ca_t *ca);

/**
 * @brief Handles a received Address Claimed or Cannot Claim, whatever its destination.
 *
 * Arbitrates the addresses the stack's CAs hold, records the self-configurable
 * addresses other nodes claim, and delivers the message if it is in rx_pgns.
 *
 * @param s     Stack.
 * @param id    Identifier of the frame.
 * @param data  Payload.
 * @param len   Payload length in bytes.
 */
void j1939_addr_claim_handle(j1939_t *s, uint32_t id, const uint8_t *data, uint8_t len);

/**
 * @brief Tells whether a CA of the stack accepts Commanded Address.
 * @param s  Stack.
 * @return true if at least one CA was configured with accept_commanded.
 */
bool j1939_addr_command_accepted(const j1939_t *s);

/**
 * @brief Handles a received Commanded Address.
 *
 * Records the new address for the CA it concerns, if that CA accepts
 * commands. Applied by the next j1939_addr_process(), so the transport
 * protocol session that carried it ends first.
 *
 * @param s     Stack.
 * @param data  Payload: the target's NAME, then the new address.
 * @param len   Payload length in bytes.
 */
void j1939_addr_command_handle(j1939_t *s, const uint8_t *data, uint16_t len);

/**
 * @brief Answers a Request for Address Claimed for every CA it concerns.
 * @param s   Stack.
 * @param da  Destination of the Request: J1939_ADDR_GLOBAL or an address.
 */
void j1939_addr_request_handle(j1939_t *s, uint8_t da);

/**
 * @brief Sends a Request for Address Claimed from a CA.
 *
 * Uses J1939_ADDR_NULL as the source while the CA has not claimed an
 * address, and answers the Request for the stack's own CAs.
 *
 * @param s    Stack.
 * @param ca   Sending CA.
 * @param msg  The Request message; msg->sa is ignored.
 * @return J1939_RET_OK, J1939_RET_ERR_FULL if the tx queue is full, or
 *         J1939_RET_ERR_ARG on a NULL stack, an unknown CA or an invalid
 *         identifier.
 */
j1939_ret_t j1939_addr_request_send(j1939_t *s, j1939_ca_id_t ca, const j1939_msg_t *msg);

#endif /* J1939_ADDR_PRIV_H */
