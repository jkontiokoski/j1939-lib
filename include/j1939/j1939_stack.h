/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_stack.h
 * @brief J1939 stack instance: one per CAN bus.
 *
 * Data flow:
 *
 * 1. The driver puts received frames into the rx queue (j1939_rx_queue()).
 * 2. j1939_process() consumes the rx queue. Protocol frames are handled by
 *    the stack; application messages are stored in the message slots.
 * 3. The application reads messages with j1939_msg_peek() and releases them
 *    with j1939_msg_pop().
 * 4. j1939_send() and the stack itself put frames into the tx queue
 *    (j1939_tx_queue()), which the driver drains.
 *
 * All memory is supplied by the integrator through j1939_cfg_t.
 */

#ifndef J1939_STACK_H
#define J1939_STACK_H

#include <stdbool.h>
#include <stdint.h>

#include "j1939/j1939_config.h"
#include "j1939/j1939_msg.h"
#include "j1939/j1939_queue.h"
#include "j1939/j1939_ret.h"
#include "j1939/j1939_ring.h"

/** Storage for one received message. Allocated by the integrator, members are private. */
typedef struct j1939_msg_slot {
	j1939_msg_t msg; /**< Message header; data points into this slot. */
	uint8_t data[J1939_MSG_SINGLE_FRAME_MAX]; /**< Payload of single frame messages. */
} j1939_msg_slot_t;

/** Stack configuration. All buffers and lists must outlive the stack instance. */
typedef struct j1939_cfg {
	j1939_port_frame_t *rx_buf; /**< Receive frame queue storage. */
	uint16_t rx_len;            /**< Frames in rx_buf, at least 1. */
	j1939_port_frame_t *tx_buf; /**< Transmit frame queue storage. */
	uint16_t tx_len;            /**< Frames in tx_buf, at least 1. */
	j1939_msg_slot_t *msg_buf;  /**< Received message storage. */
	uint16_t msg_len;           /**< Slots in msg_buf, at least 1. */
	const uint32_t *rx_pgns;    /**< PGNs delivered to the application. May be NULL if empty. */
	uint16_t rx_pgns_len;       /**< Entries in rx_pgns. */
	const uint32_t
	        *req_pgns; /**< PGNs the application answers Requests for. May be NULL if empty. */
	uint16_t req_pgns_len; /**< Entries in req_pgns. */
} j1939_cfg_t;

/** Controller Application configuration. */
typedef struct j1939_ca_cfg {
	uint8_t address; /**< Source address, 0..253. */
} j1939_ca_cfg_t;

/** Handle of a Controller Application within its stack. */
typedef uint8_t j1939_ca_id_t;

/** Controller Application state. Members are private. */
typedef struct j1939_ca {
	uint8_t address; /**< Current source address. */
} j1939_ca_t;

/** Event counters. */
typedef struct j1939_stats {
	uint32_t rx_msg_overflow; /**< Application messages dropped: all message slots in use. */
	uint32_t tx_overflow;     /**< Stack generated frames dropped: tx queue full. */
} j1939_stats_t;

/** Message slot queue. Members are private. */
typedef struct j1939_msg_queue {
	j1939_msg_slot_t *buf; /**< Integrator storage. */
	j1939_ring_t ring;     /**< Indices into buf. */
} j1939_msg_queue_t;

/** Stack instance. Allocated by the integrator, members are private. */
typedef struct j1939 {
	j1939_queue_t rx;                /**< Received frames. */
	j1939_queue_t tx;                /**< Frames to transmit. */
	j1939_msg_queue_t msgs;          /**< Messages for the application. */
	const uint32_t *rx_pgns;         /**< See j1939_cfg_t. */
	uint16_t rx_pgns_len;            /**< See j1939_cfg_t. */
	const uint32_t *req_pgns;        /**< See j1939_cfg_t. */
	uint16_t req_pgns_len;           /**< See j1939_cfg_t. */
	j1939_ca_t ca[J1939_CFG_CA_MAX]; /**< Controller Applications. */
	uint8_t ca_count;                /**< Controller Applications in use. */
	j1939_stats_t stats;             /**< Event counters. */
} j1939_t;

/**
 * @brief Initialises a stack instance.
 *
 * PGNs in the lists must be valid: at most J1939_PGN_MAX, lowest byte 0 for
 * PDU1 formats.
 *
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG if a pointer, length or PGN is invalid.
 */
j1939_ret_t j1939_init(j1939_t *s, const j1939_cfg_t *cfg);

/**
 * @brief Adds a Controller Application.
 *
 * The CA uses the configured address immediately.
 *
 * @param s    Stack.
 * @param cfg  CA configuration.
 * @param id   Handle of the new CA. Written only on success.
 * @return J1939_RET_OK, J1939_RET_ERR_FULL if J1939_CFG_CA_MAX CAs exist, or
 *         J1939_RET_ERR_ARG on a NULL pointer, an address above 253 or an
 *         address already used by another CA of this stack.
 */
j1939_ret_t j1939_ca_add(j1939_t *s, const j1939_ca_cfg_t *cfg, j1939_ca_id_t *id);

/** @return Receive frame queue; the driver is its producer. NULL if @p s is NULL. */
j1939_queue_t *j1939_rx_queue(j1939_t *s);

/** @return Transmit frame queue; the driver is its consumer. NULL if @p s is NULL. */
j1939_queue_t *j1939_tx_queue(j1939_t *s);

/**
 * @brief Runs the stack.
 *
 * Handles the frames that are in the rx queue when the call starts. Frames
 * arriving meanwhile are handled by the next call.
 *
 * @param s           Stack.
 * @param elapsed_us  Time since the previous call, in microseconds.
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG if @p s is NULL.
 */
j1939_ret_t j1939_process(j1939_t *s, uint32_t elapsed_us);

/**
 * @brief Returns the oldest received application message.
 *
 * The message and its data remain valid until j1939_msg_pop().
 *
 * @return Message, or NULL if there is none or @p s is NULL.
 */
const j1939_msg_t *j1939_msg_peek(j1939_t *s);

/**
 * @brief Releases the message returned by j1939_msg_peek().
 *
 * @return J1939_RET_OK, J1939_RET_ERR_EMPTY, or J1939_RET_ERR_ARG if @p s is NULL.
 */
j1939_ret_t j1939_msg_pop(j1939_t *s);

/**
 * @brief Queues a message for transmission from a Controller Application.
 *
 * The frame is built immediately; @p msg and its data may be reused after
 * the call. The payload is sent as given; J1939 PGNs of 8 bytes or less
 * normally fill unused bytes with 0xFF.
 *
 * @param s    Stack.
 * @param ca   Sending Controller Application.
 * @param msg  Message. msg->sa is ignored.
 * @return J1939_RET_OK, J1939_RET_ERR_FULL if the tx queue is full, or
 *         J1939_RET_ERR_ARG on a NULL pointer, an unknown CA, a payload
 *         longer than J1939_MSG_SINGLE_FRAME_MAX or an invalid identifier.
 */
j1939_ret_t j1939_send(j1939_t *s, j1939_ca_id_t ca, const j1939_msg_t *msg);

/** @return Event counters, or NULL if @p s is NULL. */
const j1939_stats_t *j1939_stats_get(const j1939_t *s);

#endif /* J1939_STACK_H */
