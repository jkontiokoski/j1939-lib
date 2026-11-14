/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_internal.h
 * @brief What the stack modules offer each other: not part of the public API.
 *
 * Each module has a section with its entry points: handling of its received messages, its
 * share of j1939_process() and the queries the other modules need. A feature that is switched
 * off in j1939_config.h defines its entry points as empty inline functions here, so that the
 * callers are the same code in every build.
 */

#ifndef J1939_INTERNAL_H
#define J1939_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "j1939/j1939_msg.h"
#include "j1939/j1939_ret.h"
#include "j1939/j1939_stack.h"

/* ---- Stack: frames out, messages to the application, PGN lists ---- */

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

/* ---- Requests ---- */

/**
 * @brief Handles a received Request addressed to this stack or to all nodes.
 * @param s     Stack.
 * @param id    Identifier of the Request.
 * @param data  Payload: the requested PGN in the first three bytes.
 * @param len   Payload length in bytes.
 */
void j1939_request_handle(j1939_t *s, uint32_t id, const uint8_t *data, uint8_t len);

/* ---- Address claiming ---- */

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
 * addresses other nodes claim, updates the NAME table, and delivers the
 * message if it is in rx_pgns.
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

/* ---- NAME table ---- */

/**
 * @brief Removes the NAME table and clears its counter.
 * @param s  Stack.
 */
void j1939_nametab_stack_init(j1939_t *s);

/**
 * @brief Records an Address Claimed of another node.
 *
 * Called after the stack's own arbitration, only if no CA of the stack holds
 * the address. A NAME of the stack's own CAs is not recorded.
 *
 * @param s        Stack.
 * @param name     NAME from the message.
 * @param address  Claimed address, 0..253.
 */
void j1939_nametab_claimed(j1939_t *s, uint64_t name, uint8_t address);

/**
 * @brief Records a Cannot Claim of another node: present, without an address.
 * @param s     Stack.
 * @param name  NAME from the message.
 */
void j1939_nametab_cannot_claim(j1939_t *s, uint64_t name);

/**
 * @brief Notes that the stack sent a global Request for Address Claimed.
 *
 * Its answers fill the table as the startup Request would, so the startup
 * Request is no longer due: after a CA's request before claim, after a
 * Request of the application, and after the startup Request itself.
 *
 * @param s  Stack.
 */
void j1939_nametab_global_request(j1939_t *s);

/**
 * @brief Looks up the NAME at an address without sending a Request.
 * @param s        Stack with a table (s->nametab.buf not NULL).
 * @param address  Source address, 0..253.
 * @param name     NAME. Written only if found.
 * @return true if another node of the table holds @p address and no CA of
 *         the stack does.
 */
bool j1939_nametab_lookup(const j1939_t *s, uint8_t address, uint64_t *name);

/**
 * @brief Sends the pending Requests for Address Claimed and advances the hold timer.
 * @param s           Stack.
 * @param elapsed_us  Time since the previous j1939_process(), in microseconds.
 */
void j1939_nametab_process(j1939_t *s, uint32_t elapsed_us);

/* ---- Transport protocol ---- */

#if J1939_TP_ENABLE
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

/**
 * @brief Checks if a TP buffer has been allocated to the stack.
 * @param s    Stack.
 * @return     true if the buffer has been allocated.
 */
static inline bool j1939_tp_tx_buf_allocated(const j1939_t *s) {
	return (s->tp.tx_buf != NULL) && (s->tp.tx_buf_len > 0U);
}

#else
static inline bool j1939_tp_cfg_valid(const j1939_cfg_t *cfg) {
	(void)cfg;
	return true;
}

static inline void j1939_tp_init(j1939_t *s, const j1939_cfg_t *cfg) {
	(void)s;
	(void)cfg;
}

static inline void j1939_tp_address_lost(j1939_t *s, uint8_t address) {
	(void)s;
	(void)address;
}

static inline void j1939_tp_process(j1939_t *s, uint32_t elapsed_us) {
	(void)s;
	(void)elapsed_us;
}

static inline bool j1939_tp_tx_buf_allocated(const j1939_t *s) {
	(void)s;
	return false;
}
#endif

/* ---- Receive objects ---- */

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

/* ---- Transmit objects ---- */

/**
 * @brief Removes the transmit objects and clears their counters.
 * @param s  Stack.
 */
void j1939_txobj_stack_init(j1939_t *s);

/**
 * @brief Handles a Request for the PGN of a transmit object.
 * @param s    Stack.
 * @param id   Identifier of the Request.
 * @param pgn  Requested PGN.
 * @return true if a transmit object of a CA addressed by the Request has the
 *         PGN, false if the Request module handles it as usual.
 */
bool j1939_txobj_request_handle(j1939_t *s, uint32_t id, uint32_t pgn);

/**
 * @brief Sends due objects and pending answers; advances the timers.
 * @param s           Stack.
 * @param elapsed_us  Time since the previous j1939_process(), in microseconds.
 */
void j1939_txobj_process(j1939_t *s, uint32_t elapsed_us);

/* ---- Diagnostics ---- */

/**
 * @brief Disables diagnostics for every CA and clears the diagnostics counters.
 * @param s  Stack.
 */
void j1939_dm_stack_init(j1939_t *s);

/**
 * @brief Handles a Request for a diagnostic message.
 * @param s    Stack.
 * @param id   Identifier of the Request.
 * @param pgn  Requested PGN.
 * @return true if the Request concerns a diagnostic message that a CA
 *         addressed by it supports, false if the Request module handles it
 *         as usual.
 */
bool j1939_dm_request_handle(j1939_t *s, uint32_t id, uint32_t pgn);

/**
 * @brief Sends due DM1s, pending answers and acknowledgements; advances the timers.
 * @param s           Stack.
 * @param elapsed_us  Time since the previous j1939_process(), in microseconds.
 */
void j1939_dm_process(j1939_t *s, uint32_t elapsed_us);

#endif /* J1939_INTERNAL_H */
