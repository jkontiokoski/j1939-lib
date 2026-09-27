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
#include "j1939/j1939_tp.h"

/** Storage for one received message. Allocated by the integrator, members are private. */
typedef struct j1939_msg_slot {
	j1939_msg_t msg; /**< Message header; data points into this slot. */
	uint8_t data[J1939_MSG_SINGLE_FRAME_MAX]; /**< Payload of single frame messages. */
	j1939_tp_buf_t *tp_buf; /**< Buffer holding a transport protocol message, else NULL. */
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

	j1939_tp_buf_t *tp_tx_buf; /**< Multi-packet transmit buffers. May be NULL if empty. */
	uint16_t tp_tx_buf_len;    /**< Entries in tp_tx_buf. */
	j1939_tp_buf_t *tp_rx_buf; /**< Multi-packet reassembly buffers. May be NULL if empty. */
	uint16_t tp_rx_buf_len;    /**< Entries in tp_rx_buf. */
} j1939_cfg_t;

/** Controller Application configuration. */
typedef struct j1939_ca_cfg {
	uint8_t address; /**< Preferred address, 0..253. */
	uint64_t name;   /**< NAME, see j1939_name.h. Must be unique on the network. */
} j1939_ca_cfg_t;

/** Handle of a Controller Application within its stack. */
typedef uint8_t j1939_ca_id_t;

#define J1939_ADDR_SELF_CFG_MIN 128U /**< First self-configurable address. */
#define J1939_ADDR_SELF_CFG_MAX 247U /**< Last self-configurable address. */
#define J1939_ADDR_TAKEN_LEN    15U  /**< Bytes of the self-configurable address bitmap. */

/** Address claim state of a Controller Application (J1939/81). */
typedef enum j1939_addr_state {
	J1939_ADDR_STATE_UNCLAIMED = 0, /**< Address Claimed not sent yet; no address. */
	J1939_ADDR_STATE_CLAIMING,      /**< Address Claimed sent, contention wait running. */
	J1939_ADDR_STATE_CLAIMED,       /**< Address claimed; the CA may transmit. */
	J1939_ADDR_STATE_CANNOT_CLAIM,  /**< No address; Cannot Claim sent or pending. */
} j1939_addr_state_t;

/** Controller Application state. Members are private. */
typedef struct j1939_ca {
	uint64_t name;             /**< NAME. */
	j1939_addr_state_t state;  /**< Address claim state. */
	uint32_t timer_us;         /**< Contention wait or Cannot Claim delay left. */
	uint8_t address;           /**< Address held or being claimed; J1939_ADDR_NULL if none. */
	bool cannot_claim_pending; /**< A Cannot Claim is sent when timer_us expires. */
} j1939_ca_t;

/** Event counters. */
typedef struct j1939_stats {
	uint32_t rx_msg_overflow; /**< Application messages dropped: all message slots in use. */
	uint32_t tx_overflow;     /**< Stack generated frames dropped: tx queue full. */
	uint32_t tp_tx_aborted;   /**< Multi-packet sends ended by an abort or a timeout. */
	uint32_t tp_rx_aborted;   /**< Multi-packet receptions ended without delivery. */
	uint32_t tp_rx_refused;   /**< RTS or BAM refused: no free session or reassembly buffer. */
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
	/** Self-configurable addresses claimed by other nodes, one bit each. */
	uint8_t addr_taken[J1939_ADDR_TAKEN_LEN];
	j1939_tp_t tp; /**< Transport protocol sessions and buffers. */
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
 * The CA claims its preferred address with the next j1939_process() and may
 * transmit once the claim succeeds, see j1939_addr.h.
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
 * The reassembly buffer of a multi-packet message becomes free for the next
 * transport protocol session.
 *
 * @return J1939_RET_OK, J1939_RET_ERR_EMPTY, or J1939_RET_ERR_ARG if @p s is NULL.
 */
j1939_ret_t j1939_msg_pop(j1939_t *s);

/**
 * @brief Queues a message for transmission from a Controller Application.
 *
 * A payload of up to 8 bytes is sent as one frame, built immediately. A
 * longer payload is copied into a transport protocol transmit buffer and
 * sent with BAM when msg->da is J1939_ADDR_GLOBAL, with RTS/CTS otherwise,
 * also for a PDU2 PGN: the transport protocol frames carry the destination.
 * The BAM or RTS frame is queued immediately, the data packets by
 * j1939_process(). Either way @p msg and its data may be reused after the
 * call. The payload is sent as given; J1939 PGNs of 8 bytes or less
 * normally fill unused bytes with 0xFF.
 *
 * @param s    Stack.
 * @param ca   Sending Controller Application.
 * @param msg  Message. msg->sa is ignored.
 * @return J1939_RET_OK;
 *         J1939_RET_ERR_FULL if the tx queue is full, or for a multi-packet
 *         message if no transport protocol session or transmit buffer is free;
 *         J1939_RET_ERR_BUSY if this CA already sends a multi-packet message
 *         to the same destination;
 *         J1939_RET_ERR_NO_ADDRESS while the CA has not claimed an address;
 *         J1939_RET_ERR_ARG on a NULL pointer, an unknown CA, a payload
 *         longer than J1939_CFG_TP_BUF_SIZE or an invalid identifier.
 */
j1939_ret_t j1939_send(j1939_t *s, j1939_ca_id_t ca, const j1939_msg_t *msg);

/** @return Event counters, or NULL if @p s is NULL. */
const j1939_stats_t *j1939_stats_get(const j1939_t *s);

#endif /* J1939_STACK_H */
