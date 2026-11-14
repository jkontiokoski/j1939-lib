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
 *
 * A received frame is decoded once, in j1939_rx(), into a j1939_msg_t whose data points into
 * the frame. The handlers below take that message; it is valid only during the call.
 */

#ifndef J1939_INTERNAL_H
#define J1939_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "j1939/j1939_msg.h"
#include "j1939/j1939_ret.h"
#include "j1939/j1939_stack.h"

/* ---- Stack: frames out, messages to the application, shared helpers ---- */

/** Age of a send that is retried until it succeeds, see j1939_stack_retry_done(). */
#define J1939_STACK_NO_EXPIRY 0U

/**
 * @brief Queues a frame.
 * @param s     Stack.
 * @param prio  Priority, 0..7.
 * @param pgn   PGN.
 * @param da    Destination address; J1939_ADDR_GLOBAL for a PDU2 PGN.
 * @param sa    Source address.
 * @param data  Payload.
 * @param len   Payload length in bytes, at most 8.
 * @return J1939_RET_OK, J1939_RET_ERR_ARG for an invalid identifier, or
 *         J1939_RET_ERR_FULL if the tx queue is full.
 */
j1939_ret_t j1939_stack_tx(j1939_t *s, uint8_t prio, uint32_t pgn, uint8_t da, uint8_t sa,
                           const uint8_t *data, uint8_t len);

/**
 * @brief Puts a received message into a message slot for the application.
 *
 * A payload of up to 8 bytes is copied into the slot. A longer one stays in its transport
 * protocol buffer, which the slot then refers to until j1939_msg_pop(). Without a free slot the
 * message is counted in rx_msg_overflow.
 *
 * @param s    Stack.
 * @param msg  Message.
 * @return true if delivered, false if no message slot is free.
 */
bool j1939_stack_deliver(j1939_t *s, const j1939_msg_t *msg);

/**
 * @brief Tells whether a PGN is in a list.
 * @param list  PGN list; may be NULL if @p len is 0.
 * @param len   Entries in @p list.
 * @param pgn   PGN.
 * @return true if found.
 */
bool j1939_stack_pgn_listed(const uint32_t *list, uint16_t len, uint32_t pgn);

/**
 * @brief Tells whether the receive path consumes a PGN itself.
 * @param pgn  PGN.
 * @return true for Request, Address Claimed, TP.CM and TP.DT.
 */
bool j1939_stack_pgn_owned(uint32_t pgn);

/**
 * @brief Writes a PGN as the three little endian bytes it has in a Request, an
 *        Acknowledgement and a TP.CM frame.
 * @param pgn   PGN.
 * @param data  Three bytes.
 */
void j1939_stack_pgn_put(uint32_t pgn, uint8_t *data);

/**
 * @brief Reads a PGN from three little endian bytes.
 * @param data  Three bytes.
 * @return The value; above J1939_PGN_MAX for malformed input.
 */
uint32_t j1939_stack_pgn_get(const uint8_t *data);

/**
 * @brief Advances a timer that counts up, saturating at UINT32_MAX.
 * @param timer_us    Timer, in microseconds.
 * @param elapsed_us  Time to add, in microseconds.
 */
void j1939_stack_timer_add(uint32_t *timer_us, uint32_t elapsed_us);

/**
 * @brief Advances a timer that may have been started since the previous j1939_process().
 *
 * A timer started by a received frame or an API call must not count the time that passed
 * before it was started: in the call after its start only the flag is cleared.
 *
 * @param timer_us    Timer, in microseconds; saturates at UINT32_MAX.
 * @param fresh       Set when the timer was started since the previous call; cleared here.
 * @param elapsed_us  Time since the previous j1939_process(), in microseconds.
 */
void j1939_stack_timer_advance(uint32_t *timer_us, bool *fresh, uint32_t elapsed_us);

/**
 * @brief Classifies the result of a send that is retried while the tx queue is full or the
 *        transport protocol busy.
 * @param ret      Result of the send.
 * @param age_us   Age of the send: it is given up at J1939_TR_US. J1939_STACK_NO_EXPIRY for a
 *                 send that is retried until it succeeds.
 * @param retries  Counter of deferred sends.
 * @param dropped  Counter of sends given up.
 * @return true when the send is over, sent or given up; false if it is to be tried again.
 */
bool j1939_stack_retry_done(j1939_ret_t ret, uint32_t age_us, uint32_t *retries, uint32_t *dropped);

/* ---- Requests ---- */

/**
 * @brief Handles a received Request addressed to this stack or to all nodes.
 * @param s    Stack.
 * @param req  The Request: the requested PGN in the first three bytes.
 */
void j1939_request_handle(j1939_t *s, const j1939_msg_t *req);

/**
 * @brief Tells where the answer to a Request goes.
 * @param req  The Request.
 * @return Its sender, or J1939_ADDR_GLOBAL for a Request to all nodes.
 */
uint8_t j1939_request_requester(const j1939_msg_t *req);

/**
 * @brief Queues a Request.
 * @param s    Stack.
 * @param sa   Source address.
 * @param da   Node asked, or J1939_ADDR_GLOBAL.
 * @param pgn  Requested PGN, at most J1939_PGN_MAX.
 * @return J1939_RET_OK, or J1939_RET_ERR_FULL if the tx queue is full.
 */
j1939_ret_t j1939_request_tx(j1939_t *s, uint8_t sa, uint8_t da, uint32_t pgn);

/**
 * @brief Queues an Acknowledgement: to the global address, with the requester in its data.
 * @param s          Stack.
 * @param sa         Source address.
 * @param ctrl       Control byte, see J1939_ACK_CTRL_ACK and the following.
 * @param requester  Address the acknowledgement is meant for.
 * @param pgn        Acknowledged PGN.
 * @return J1939_RET_OK, or J1939_RET_ERR_FULL if the tx queue is full.
 */
j1939_ret_t j1939_request_ack_tx(j1939_t *s, uint8_t sa, uint8_t ctrl, uint8_t requester,
                                 uint32_t pgn);

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
 * @brief Tells whether a CA of the stack uses an address, whatever its claim state.
 * @param s        Stack.
 * @param address  Address.
 * @return true if a CA has it as the address it holds or is about to claim.
 */
bool j1939_addr_in_use(const j1939_t *s, uint8_t address);

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
bool j1939_addr_ca_claimed(const j1939_ca_t *ca);

/**
 * @brief Tells whether a message to a destination concerns a CA.
 * @param ca  CA.
 * @param da  Destination address.
 * @return true for J1939_ADDR_GLOBAL or the address the CA holds, in CLAIMING or CLAIMED state.
 */
bool j1939_addr_ca_addressed(const j1939_ca_t *ca, uint8_t da);

/**
 * @brief Handles a received Address Claimed or Cannot Claim, whatever its destination.
 *
 * Arbitrates the addresses the stack's CAs hold, records the self-configurable
 * addresses other nodes claim, updates the NAME table, and delivers the
 * message if it is in rx_pgns.
 *
 * @param s    Stack.
 * @param msg  The message.
 */
void j1939_addr_claim_handle(j1939_t *s, const j1939_msg_t *msg);

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
 * @param s    Stack.
 * @param msg  The message: the target's NAME, then the new address.
 */
void j1939_addr_command_handle(j1939_t *s, const j1939_msg_t *msg);

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
 * @param s   Stack; may be NULL.
 * @param ca  Sending CA.
 * @param da  Node asked, or J1939_ADDR_GLOBAL.
 * @return J1939_RET_OK, J1939_RET_ERR_FULL if the tx queue is full, or
 *         J1939_RET_ERR_ARG on a NULL stack or an unknown CA.
 */
j1939_ret_t j1939_addr_request_send(j1939_t *s, j1939_ca_id_t ca, uint8_t da);

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
 * @param s    Stack.
 * @param msg  The frame.
 */
void j1939_tp_handle(j1939_t *s, const j1939_msg_t *msg);

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
 * @brief Frees the reassembly buffer of a multi-packet message the application has released.
 * @param s     Stack.
 * @param data  Payload of the message, in a reassembly buffer.
 */
void j1939_tp_msg_release(j1939_t *s, const uint8_t *data);

/**
 * @brief Tells whether the stack has a transmit buffer, so that it can send multi-packet
 *        messages.
 * @param s  Stack.
 * @return true with at least one transmit buffer.
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

static inline void j1939_tp_msg_release(j1939_t *s, const uint8_t *data) {
	(void)s;
	(void)data;
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
 * @param s    Stack.
 * @param msg  The message, single frame or reassembled.
 */
void j1939_rxobj_handle(j1939_t *s, const j1939_msg_t *msg);

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
 * @param req  The Request.
 * @param pgn  Requested PGN.
 * @return true if a transmit object of a CA addressed by the Request has the
 *         PGN, false if the Request module handles it as usual.
 */
bool j1939_txobj_request_handle(j1939_t *s, const j1939_msg_t *req, uint32_t pgn);

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
 * @param req  The Request.
 * @param pgn  Requested PGN.
 * @return true if the Request concerns a diagnostic message that a CA
 *         addressed by it supports, false if the Request module handles it
 *         as usual.
 */
bool j1939_dm_request_handle(j1939_t *s, const j1939_msg_t *req, uint32_t pgn);

/**
 * @brief Sends due DM1s, pending answers and acknowledgements; advances the timers.
 * @param s           Stack.
 * @param elapsed_us  Time since the previous j1939_process(), in microseconds.
 */
void j1939_dm_process(j1939_t *s, uint32_t elapsed_us);

#endif /* J1939_INTERNAL_H */
