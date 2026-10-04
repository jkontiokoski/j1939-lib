/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_request.h
 * @brief J1939/21 Request (PGN 59904) and Acknowledgement (PGN 59392).
 */

#ifndef J1939_REQUEST_H
#define J1939_REQUEST_H

#include <stdint.h>

#include "j1939/j1939_msg.h"
#include "j1939/j1939_ret.h"
#include "j1939/j1939_stack.h"

/**
 * @addtogroup grp_request
 *
 * The stack handles a Request when j1939_rx() receives it, in this order:
 *
 * 1. A Request for Address Claimed is answered by address claiming for every
 *    CA it concerns, see @ref grp_addr.
 * 2. A Request for DM1, DM2, DM3 or DM11 to a CA with diagnostics enabled is
 *    handled by the diagnostics, see @ref grp_diag.
 * 3. A Request for the PGN of a transmit object of a CA it addresses is
 *    answered by the stack, see @ref grp_txobj.
 * 4. A Request for a PGN in the stack's req_pgns list is delivered to the
 *    application as a message with pgn J1939_PGN_REQUEST. The application
 *    reads the requested PGN with j1939_request_pgn_get() and answers with
 *    j1939_send(): to the requester for a destination specific Request of a
 *    PDU1 PGN, to the global address otherwise.
 * 5. A destination specific Request for any other PGN is answered by the
 *    stack with a NACK, sent to the global address with the requester in byte
 *    5, as J1939/21 specifies. A CA still in its contention wait sends no
 *    NACK.
 * 6. A global Request for any other PGN is ignored.
 *
 * A Request shorter than J1939_REQUEST_LEN bytes is ignored. Requests handled
 * in steps 1 to 3 are not delivered to the application.
 *
 * @{
 */

#define J1939_PGN_REQUEST 0xEA00U /**< Request, PGN 59904. */
#define J1939_PGN_ACK     0xE800U /**< Acknowledgement, PGN 59392. */

#define J1939_REQUEST_LEN 3U /**< Payload length of a Request. */
#define J1939_ACK_LEN     8U /**< Payload length of an Acknowledgement. */

/** @name Acknowledgement control byte values
 * @{ */
#define J1939_ACK_CTRL_ACK            0U /**< Positive acknowledgement. */
#define J1939_ACK_CTRL_NACK           1U /**< Negative acknowledgement. */
#define J1939_ACK_CTRL_ACCESS_DENIED  2U /**< Access denied. */
#define J1939_ACK_CTRL_CANNOT_RESPOND 3U /**< Cannot respond. */
/** @} */

/**
 * @brief Queues a Request for @p pgn.
 *
 * @param s    Stack.
 * @param ca   Requesting Controller Application.
 * @param pgn  Requested PGN.
 * @param da   Node to ask, or J1939_ADDR_GLOBAL to ask all nodes.
 * @return As j1939_send(), or J1939_RET_ERR_ARG if @p pgn is above J1939_PGN_MAX.
 */
j1939_ret_t j1939_request_send(j1939_t *s, j1939_ca_id_t ca, uint32_t pgn, uint8_t da);

/**
 * @brief Reads the requested PGN from a received Request.
 *
 * @param msg  Message with pgn J1939_PGN_REQUEST.
 * @param pgn  Requested PGN. Written only on success.
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG if @p msg is not a Request of
 *         at least J1939_REQUEST_LEN bytes or a pointer is NULL.
 */
j1939_ret_t j1939_request_pgn_get(const j1939_msg_t *msg, uint32_t *pgn);

/** @} */

#endif /* J1939_REQUEST_H */
