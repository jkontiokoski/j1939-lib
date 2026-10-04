/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_rxobj.h
 * @brief Receive objects: the latest payload of one PGN from one sender, with timeout supervision.
 */

#ifndef J1939_RXOBJ_H
#define J1939_RXOBJ_H

#include <stdbool.h>
#include <stdint.h>

#include "j1939/j1939_ret.h"
#include "j1939/j1939_stack.h"

/**
 * @addtogroup grp_rxobj
 *
 * j1939_rxobj_init() gives the stack a table of receive objects. Each object
 * names a PGN and the source address it is received from:
 *
 * - j1939_rx() copies every matching message into the object's buffer, single
 *   frame or completed transport protocol message, overwriting the previous
 *   one. The message is still delivered through the message slots if its PGN
 *   is in j1939_cfg_t::rx_pgns; the object is updated even when no message
 *   slot is free.
 * - A payload shorter than min_len or longer than buf_len is rejected and
 *   counted in j1939_stats_t::rxobj_rejected; the buffer keeps the previous
 *   payload and the age keeps running, so a malformed message never hides a
 *   timeout.
 * - j1939_process() advances the age of each received payload. A reception
 *   counts from the next j1939_process(). An object whose age reaches
 *   timeout_us is timed out, counted once per timeout in
 *   j1939_stats_t::rxobj_timeout. The next reception makes it valid again.
 * - The application reads the state with j1939_rxobj_get() and decodes the
 *   payload in place from its buffer, e.g. with j1939_signal_decode(). The
 *   buffer changes only during j1939_rx() and j1939_process().
 *
 * Only messages that pass the stack's destination filter reach an object:
 * global ones and those addressed to an address a CA of the stack holds.
 * Senders are identified by source address: when a node loses its address,
 * the node that claims it next reaches the same object.
 *
 * @{
 */

/** Configuration of a receive object. Constant integrator data, e.g. generated. */
typedef struct j1939_rxobj_cfg {
	uint8_t *buf;        /**< Payload buffer of buf_len bytes. */
	uint32_t pgn;        /**< PGN received. */
	uint32_t timeout_us; /**< Age at which the payload is timed out; 0: no supervision. */
	uint16_t buf_len;    /**< Largest accepted payload, min_len..J1939_CFG_TP_BUF_SIZE. */
	uint16_t min_len;    /**< Smallest accepted payload, at least 1. */
	uint8_t sa;          /**< Source address received from, 0..253. */
} j1939_rxobj_cfg_t;

/** State of a receive object. */
typedef enum j1939_rxobj_state {
	J1939_RXOBJ_NO_DATA = 0, /**< Nothing received yet. */
	J1939_RXOBJ_VALID,       /**< Received within timeout_us. */
	J1939_RXOBJ_TIMEOUT,     /**< Last reception older than timeout_us; buf holds it. */
} j1939_rxobj_state_t;

/** State of a receive object as reported to the application. */
typedef struct j1939_rxobj_status {
	uint32_t age_us; /**< Time since the last reception; 0 with J1939_RXOBJ_NO_DATA. */
	uint16_t len;    /**< Length of the payload in buf; 0 with J1939_RXOBJ_NO_DATA. */
	j1939_rxobj_state_t state; /**< State. */
	bool updated;              /**< Received since the previous j1939_rxobj_get(). */
} j1939_rxobj_status_t;

/** Runtime state of a receive object. Allocated by the integrator, members are private. */
typedef struct j1939_rxobj {
	uint32_t age_us; /**< Time since the last reception, saturating. */
	uint16_t len;    /**< Length of the payload in buf. */
	bool received;   /**< A payload has been received. */
	bool timed_out;  /**< The age reached timeout_us. */
	bool updated;    /**< Received since the last j1939_rxobj_get(). */
	bool fresh;      /**< Received since the last j1939_process(). */
} j1939_rxobj_t;

/**
 * @brief Gives the stack its receive objects.
 *
 * Every object starts in J1939_RXOBJ_NO_DATA. May be called again to replace
 * or reset the table; @p len 0 removes it, and so does j1939_init(). The
 * configuration and state arrays must outlive the stack.
 *
 * @param s    Stack.
 * @param cfg  Configuration of each object. May be NULL if @p len is 0.
 * @param obj  State storage, @p len entries. May be NULL if @p len is 0.
 * @param len  Number of objects.
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG on a NULL pointer, an invalid
 *         PGN, a PGN the stack handles itself (Request, Address Claimed,
 *         transport protocol), a source address above 253, invalid lengths
 *         or two objects with the same PGN and source address. Nothing
 *         changes on error.
 */
j1939_ret_t j1939_rxobj_init(j1939_t *s, const j1939_rxobj_cfg_t *cfg, j1939_rxobj_t *obj,
                             uint16_t len);

/**
 * @brief Reads the state of a receive object and clears its updated flag.
 *
 * The payload is in j1939_rxobj_cfg_t::buf, status->len bytes long, while
 * the state is J1939_RXOBJ_VALID or J1939_RXOBJ_TIMEOUT.
 *
 * @param s       Stack.
 * @param index   Index of the object in the table given to j1939_rxobj_init().
 * @param status  Written on success.
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG on a NULL pointer or an unknown index.
 */
j1939_ret_t j1939_rxobj_get(j1939_t *s, uint16_t index, j1939_rxobj_status_t *status);

/** @} */

#endif /* J1939_RXOBJ_H */
