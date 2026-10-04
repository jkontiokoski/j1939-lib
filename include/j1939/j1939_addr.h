/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_addr.h
 * @brief J1939/81 address claiming.
 */

#ifndef J1939_ADDR_H
#define J1939_ADDR_H

#include <stdint.h>

#include "j1939/j1939_ret.h"
#include "j1939/j1939_stack.h"

/**
 * @addtogroup grp_addr
 *
 * Every Controller Application claims an address before it transmits. The
 * stack runs the procedure in j1939_process(); the application only reads the
 * result with j1939_addr_get(). The claim timers advance only with the
 * elapsed time passed to j1939_process(); a wait started by a received frame
 * or an API call counts from the next call.
 *
 * | State (j1939_addr_state_t) | Address held | May transmit                   |
 * | -------------------------- | ------------ | ------------------------------ |
 * | UNCLAIMED                  | No           | Request for Address Claimed    |
 * | CLAIMING                   | Yes          | Request for Address Claimed    |
 * | CLAIMED                    | Yes          | Everything                     |
 * | CANNOT_CLAIM               | No           | Request for Address Claimed    |
 *
 * - The first j1939_process() after j1939_ca_add() sends Address Claimed (PGN
 *   60928, to the global address, the NAME as data) from the preferred
 *   address. It is retried by later calls while the tx queue is full.
 * - A CA whose address is outside J1939_ADDR_SELF_CFG_MIN..MAX is CLAIMED as
 *   soon as its Address Claimed is queued. A CA claiming an address in that
 *   range stays CLAIMING for J1939_ADDR_CLAIM_WAIT_US first, so that a
 *   contending CA can answer.
 * - An Address Claimed from another node for an address a CA holds is
 *   arbitrated on the NAME: the numerically lower NAME wins. The winner sends
 *   Address Claimed again. The loser, if its NAME is arbitrary address
 *   capable, claims the lowest self-configurable address that no other node
 *   has claimed and no CA of the stack uses. Otherwise, or if none is free,
 *   it becomes CANNOT_CLAIM and sends Cannot Claim (Address Claimed from
 *   J1939_ADDR_NULL) after a delay of 0..153 ms:
 *   J1939_ADDR_CANNOT_CLAIM_STEP_US times the XOR of the NAME's eight bytes,
 *   so CAs with different NAMEs usually differ.
 * - A Request for Address Claimed, global or to a CA's address, is answered
 *   by the stack for every CA it concerns: Address Claimed from CLAIMING or
 *   CLAIMED, Cannot Claim after the delay from CANNOT_CLAIM. A CA in
 *   UNCLAIMED sends its claim with the next j1939_process() anyway. The
 *   Request is not delivered to the application.
 * - j1939_send() returns J1939_RET_ERR_NO_ADDRESS until the claim succeeds.
 *   j1939_request_send() for PGN 60928 is always allowed; it uses
 *   J1939_ADDR_NULL while the CA has not claimed an address, and the stack
 *   also answers the Request for its own CAs.
 * - The stack records the self-configurable addresses that other nodes have
 *   claimed (J1939_ADDR_TAKEN_LEN bytes in j1939_t) to choose a free address.
 *   Entries are never cleared: J1939/81 has no message that releases an
 *   address.
 * - An Address Claimed carrying a CA's own NAME is ignored, so a driver that
 *   loops back transmitted frames does no harm.
 * - Losing an address, to arbitration, to a Commanded Address or through a
 *   corrupted claim state, ends the transport protocol sessions of that
 *   address without Connection Abort. A corrupted claim state makes the CA
 *   CANNOT_CLAIM and drops a pending command.
 * - A CA in CANNOT_CLAIM claims again only after an accepted Commanded
 *   Address.
 *
 * Received Address Claimed messages, including Cannot Claim, are also
 * delivered to the application if PGN 60928 is in the rx_pgns list.
 *
 * Commanded Address (PGN 65240) tells the CA with a given NAME to move to a
 * new address. It is 9 bytes long, so it travels with the transport protocol:
 * BAM to the global address, or RTS/CTS to the CA's address, which then needs
 * a CLAIMED CA. Receiving it needs a transport protocol reassembly buffer
 * (j1939_cfg_t::tp_rx_buf).
 *
 * - A CA acts on it only if j1939_ca_cfg_t::accept_commanded is set. Commands
 *   for a CA that refuses them, for a NAME the stack does not have, or with
 *   the new address 254 or 255 are ignored and nothing is sent. The stack
 *   receives Commanded Address only while one of its CAs accepts commands.
 * - An accepted command takes effect with the next j1939_process(), so that
 *   the transport protocol session carrying it ends first: an RTS/CTS command
 *   is acknowledged with EndOfMsgAck from the old address, then Address
 *   Claimed goes out from the new one.
 * - The CA gives up its old address, from any claim state, and claims the new
 *   one as described above. A pending Cannot Claim is dropped; a command
 *   during the contention wait abandons the address being claimed.
 * - A command to the address the CA already holds repeats its Address Claimed
 *   without restarting the contention wait. A command to an address another
 *   CA of the stack uses is ignored.
 * - The new address is not written back to j1939_ca_cfg_t. The application
 *   reads it with j1939_addr_get() and may keep it as the preferred address,
 *   e.g. in non-volatile memory.
 * - Received Commanded Address messages are also delivered to the application
 *   if PGN 65240 is in the rx_pgns list; one that finds no free message slot
 *   is then not acted on either.
 *
 * @{
 */

#define J1939_PGN_ADDRESS_CLAIMED 0xEE00U /**< Address Claimed / Cannot Claim, PGN 60928. */

#define J1939_PGN_COMMANDED_ADDRESS 0xFED8U /**< Commanded Address, PGN 65240. */
#define J1939_ADDR_COMMAND_LEN      9U      /**< Commanded Address payload: NAME, address. */

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

/**
 * @brief Commands the CA with NAME @p name to move to @p address.
 *
 * Queues a Commanded Address from a Controller Application, typically of a
 * service tool, with the transport protocol: BAM when @p da is
 * J1939_ADDR_GLOBAL, RTS/CTS to the target's current address otherwise. The
 * target reports its new address with Address Claimed.
 *
 * @param s        Stack.
 * @param ca       Sending Controller Application.
 * @param name     NAME of the CA to move.
 * @param address  New address, 0..253.
 * @param da       J1939_ADDR_GLOBAL, or the target's current address.
 * @return As j1939_send(), which needs a transport protocol transmit buffer;
 *         J1939_RET_ERR_ARG also if @p address is above 253 or @p da is
 *         J1939_ADDR_NULL.
 */
j1939_ret_t j1939_addr_command_send(j1939_t *s, j1939_ca_id_t ca, uint64_t name, uint8_t address,
                                    uint8_t da);

/** @} */

#endif /* J1939_ADDR_H */
