/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_txobj.h
 * @brief Transmit objects: PGNs that the stack sends periodically, on change and on Request.
 */

#ifndef J1939_TXOBJ_H
#define J1939_TXOBJ_H

#include <stdint.h>

#include "j1939/j1939_ret.h"
#include "j1939/j1939_stack.h"

/**
 * @addtogroup grp_txobj
 *
 * The integrator describes each transmitted PGN in a `const` table of
 * j1939_txobj_cfg_t and supplies a state array of the same length;
 * j1939_txobj_init() hands both to the stack. An object is referred to by its
 * index in the table. The application writes the payload only with
 * j1939_txobj_set(), so change detection sees every update; j1939_process()
 * decides when to send it:
 *
 * - Nothing is sent before the object's CA has claimed its address. Until the
 *   first j1939_txobj_set() the payload is all 0xFF, "not available" for
 *   every parameter.
 * - Periodic objects (period_us > 0) are sent in the j1939_process() call in
 *   which the claim completes and then once per period, counted from the next
 *   call. A late call sends once and keeps the phase.
 * - Objects with inhibit_us > 0 are also sent when j1939_txobj_set() changes
 *   the payload: with the next j1939_process() once inhibit_us has passed
 *   since the object's last send, otherwise as soon as it has. They are sent
 *   once when the claim completes too. A periodic send carries the latest
 *   payload and serves a pending change; change-triggered sends do not move
 *   the periodic phase.
 * - A Request (PGN 59904) for an object's PGN, global or to the address of
 *   its CA, is answered by the stack with the current payload and not
 *   delivered to the application. A single frame of a PDU2 PGN goes to the
 *   global address, other answers to the requester, multi-packet answers to a
 *   global Request with BAM. Requests from several nodes while an answer is
 *   pending are answered once, to the global address. A periodic or
 *   change-triggered send to the global address also serves a pending answer
 *   that would go to the global address. When a CA has several objects of the
 *   PGN, the first in the table answers. A Request to a CA still in its
 *   contention wait is neither answered nor refused; a destination specific
 *   Request to a CA without such an object is answered with NACK.
 * - Objects with period_us and inhibit_us both 0 are sent only on Request.
 * - Payloads longer than 8 bytes go with the transport protocol, as with
 *   j1939_send(): BAM to the global address, RTS/CTS to a node. They share
 *   the CA's sessions and the transport protocol transmit buffers with other
 *   sends.
 *
 * Sends that find the tx queue full or the CA's transport protocol session
 * busy are retried with every j1939_process() and counted in
 * j1939_stats_t::txobj_tx_retry. A periodic send still unsent when the next
 * one is due, an answer not sent within J1939_TXOBJ_RESPONSE_US, and answers
 * pending when the CA loses its address are counted in
 * j1939_stats_t::txobj_tx_dropped. After losing its address a CA's objects
 * start again as above with its next claim.
 *
 * @{
 */

/** Response time Tr of J1939/21: answers to Requests not sent within it are given up. */
#define J1939_TXOBJ_RESPONSE_US 200000U

/** Transmit object configuration. Usually `const` data; the buffer must outlive the stack. */
typedef struct j1939_txobj_cfg {
	uint8_t *buf;        /**< Payload storage of len bytes. Written only by the stack. */
	uint32_t pgn;        /**< PGN; for PDU1 formats with the lowest byte 0. */
	uint32_t period_us;  /**< Transmission period; 0: not periodic. */
	uint32_t inhibit_us; /**< 0: no change trigger; else least time from a send to a change
	                        send. */
	uint16_t len;        /**< Payload length, 1..J1939_CFG_TP_BUF_SIZE. */
	uint8_t prio;        /**< Priority, 0..7. */
	/** J1939_ADDR_GLOBAL, or a node address. A single frame PDU2 PGN needs the global address.
	 */
	uint8_t da;
	j1939_ca_id_t ca; /**< Sending Controller Application. */
} j1939_txobj_cfg_t;

/** Transmit object state. Allocated by the integrator, members are private. */
typedef struct j1939_txobj {
	uint32_t period_us; /**< Time since the periodic send was last due. */
	uint32_t since_us;  /**< Time since the last send; saturates. */
	uint32_t answer_us; /**< Age of the pending answer. */
	uint8_t requester;  /**< Requester of the pending answer; J1939_ADDR_GLOBAL for all. */
	uint8_t flags;      /**< State bits. */
} j1939_txobj_t;

/**
 * @brief Hands the transmit objects to the stack.
 *
 * Called after the CAs are added. Every entry is validated before the stack
 * changes. Fills every payload buffer with 0xFF. May be called again to
 * replace or reset the objects; @p len 0 removes them, and so does
 * j1939_init().
 *
 * @param s    Stack.
 * @param cfg  Configuration table of @p len entries. May be NULL if @p len is 0.
 * @param obj  State array of @p len entries. May be NULL if @p len is 0.
 * @param len  Number of objects.
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG on a NULL pointer, an unknown
 *         CA, an invalid PGN, priority or destination, a NULL buffer, a
 *         length of 0, above J1939_CFG_TP_BUF_SIZE or above 8 while the stack
 *         has no transport protocol transmit buffer, a PGN the stack handles
 *         itself (Request, Acknowledgement, transport protocol, Address
 *         Claimed, Commanded Address, DM1, DM2, DM3, DM11) or answered by the
 *         application (in j1939_cfg_t::req_pgns), or two entries with the same
 *         CA, PGN and destination.
 */
j1939_ret_t j1939_txobj_init(j1939_t *s, const j1939_txobj_cfg_t *cfg, j1939_txobj_t *obj,
                             uint16_t len);

/**
 * @brief Replaces the payload of a transmit object.
 *
 * The payload is copied. With j1939_txobj_cfg_t::inhibit_us > 0 a payload
 * that differs from the current one triggers a send, see @ref grp_txobj.
 *
 * @param s      Stack.
 * @param index  Object index in the configuration table.
 * @param data   Payload.
 * @param len    Payload length; must equal j1939_txobj_cfg_t::len.
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG on a NULL pointer, an unknown
 *         index or a wrong length.
 */
j1939_ret_t j1939_txobj_set(j1939_t *s, uint16_t index, const uint8_t *data, uint16_t len);

/** @} */

#endif /* J1939_TXOBJ_H */
