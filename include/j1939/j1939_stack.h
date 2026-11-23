/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_stack.h
 * @brief J1939 stack instance: one per CAN bus.
 */

#ifndef J1939_STACK_H
#define J1939_STACK_H

#include <stdbool.h>
#include <stdint.h>

#include "j1939/j1939_addr.h"
#include "j1939/j1939_config.h"
#include "j1939/j1939_dm.h"
#include "j1939/j1939_nametab.h"
#include "j1939/j1939_port_contract.h"
#include "j1939/j1939_request.h"
#include "j1939/j1939_rxobj.h"
#include "j1939/j1939_tp.h"
#include "j1939/j1939_txobj.h"
#include "j1939/j1939_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup grp_stack
 *
 * One stack instance runs one CAN bus. Data flows through it in four steps:
 *
 * 1. The integrator passes each received frame to j1939_rx(). Protocol frames
 *    are handled by the stack; application messages are stored in the message
 *    slots and the receive objects.
 * 2. j1939_process() advances the stack's timers and queues what is due.
 * 3. The application reads messages with j1939_msg_peek() and releases them
 *    with j1939_msg_pop().
 * 4. j1939_send() and the stack itself put frames into the tx queue, which
 *    the integrator drains with j1939_tx_peek() and j1939_tx_pop().
 *
 * j1939_rx() sorts each frame in this order:
 *
 * 1. Standard (11-bit) frames, remote frames and frames with the extended
 *    data page bit set are dropped.
 * 2. Address Claimed goes to address claiming whatever its destination, see
 *    @ref grp_addr, then to the NAME table (@ref grp_nametab), and to the
 *    application too if its PGN is in rx_pgns.
 * 3. Frames to an address that no CA of the stack holds are dropped; frames
 *    to the global address pass.
 * 4. A Request goes to the Request handling, see @ref grp_request.
 * 5. TP.CM and TP.DT frames go to the transport protocol, see @ref grp_tp.
 * 6. Any other frame updates the receive object for its PGN and source
 *    address, if there is one (@ref grp_rxobj), and is delivered to the
 *    application if its PGN is in rx_pgns. The rest is dropped.
 *
 * All functions of a stack instance run in one execution context. All memory
 * is supplied by the integrator through j1939_cfg_t, in one call to j1939_init(). When the message
 * slots or the tx queue are full, the stack drops the message or frame it generated and counts it
 * in j1939_stats_t; application sends report J1939_RET_ERR_FULL instead.
 *
 * @{
 */

/** Storage for one received message. Allocated by the integrator, members are private. */
typedef struct j1939_msg_slot {
	j1939_msg_t msg; /**< Message header; data points into this slot. */
	uint8_t data[J1939_MSG_SINGLE_FRAME_MAX]; /**< Payload of single frame messages. */
} j1939_msg_slot_t;

/**
 * Stack configuration: everything a stack instance uses, given to j1939_init() in one piece.
 *
 * The stack keeps a pointer to the configuration, so the struct, and every buffer, list and
 * table it names, must outlive the stack instance. A part that is not used is left out: a
 * NULL pointer with a length of 0. j1939_init() says what makes a configuration valid.
 */
typedef struct j1939_cfg {
	j1939_port_frame_t *tx_buf; /**< Transmit frame queue storage. */
	uint16_t tx_len;            /**< Frames in tx_buf, at least 1. */
	j1939_msg_slot_t *msg_buf;  /**< Received message storage. */
	uint16_t msg_len;           /**< Slots in msg_buf, at least 1. */
	const uint32_t *rx_pgns;    /**< PGNs delivered to the application. */
	uint16_t rx_pgns_len;       /**< Entries in rx_pgns. */
	const uint32_t *req_pgns;   /**< PGNs the application answers Requests for. */
	uint16_t req_pgns_len;      /**< Entries in req_pgns. */

	/** Controller Applications, see @ref grp_addr. The index of a CA is its j1939_ca_id_t. */
	const j1939_ca_cfg_t *ca;
	uint8_t ca_len; /**< Entries in ca, at most J1939_CFG_CA_MAX. */

	/** NAME table storage, one entry per other node expected, see @ref grp_nametab. */
	j1939_nametab_entry_t *nametab;
	uint16_t nametab_len; /**< Entries in nametab. */

	const j1939_rxobj_cfg_t *rxobj_cfg; /**< Receive object table, see @ref grp_rxobj. */
	j1939_rxobj_t *rxobj;               /**< State storage, one entry per receive object. */
	uint16_t rxobj_len;                 /**< Entries in rxobj_cfg and in rxobj. */

	const j1939_txobj_cfg_t *txobj_cfg; /**< Transmit object table, see @ref grp_txobj. */
	j1939_txobj_t *txobj;               /**< State storage, one entry per transmit object. */
	uint16_t txobj_len;                 /**< Entries in txobj_cfg and in txobj. */

#if J1939_TP_ENABLE
	j1939_tp_buf_t *tp_tx_buf; /**< Multi-packet transmit buffers, see @ref grp_tp. */
	uint16_t tp_tx_buf_len;    /**< Entries in tp_tx_buf. */
	j1939_tp_buf_t *tp_rx_buf; /**< Multi-packet reassembly buffers. */
	uint16_t tp_rx_buf_len;    /**< Entries in tp_rx_buf. */
#endif
} j1939_cfg_t;

/** Event counters. */
typedef struct j1939_stats {
	uint32_t rx_msg_overflow; /**< Application messages dropped: all message slots in use. */
	uint32_t tx_overflow;     /**< Stack generated frames dropped: tx queue full. */
#if J1939_TP_ENABLE
	uint32_t tp_tx_aborted; /**< Multi-packet sends ended by an abort or a timeout. */
	uint32_t tp_rx_aborted; /**< Multi-packet receptions ended without delivery. */
	uint32_t tp_rx_refused; /**< RTS or BAM refused: no free session or reassembly buffer. */
#endif
#if J1939_DM_ENABLE
	uint32_t dm_tx_retry;   /**< DM1, DM2 or acknowledgement sends deferred, see j1939_dm.h. */
	uint32_t dm_tx_dropped; /**< DM1, DM2 or acknowledgement sends given up, see j1939_dm.h. */
#endif
	uint32_t rxobj_rejected;   /**< Payloads a receive object refused for their length. */
	uint32_t rxobj_timeout;    /**< Receive objects that timed out, see j1939_rxobj.h. */
	uint32_t txobj_tx_retry;   /**< Transmit object sends deferred, see j1939_txobj.h. */
	uint32_t txobj_tx_dropped; /**< Transmit object sends given up, see j1939_txobj.h. */
	uint32_t nametab_dropped;  /**< New NAMEs not recorded: NAME table full. */
} j1939_stats_t;

/** Stack instance. Allocated by the integrator, members are private. */
struct j1939 {
	const j1939_cfg_t *cfg; /**< Configuration given to j1939_init(). */
	j1939_ring_t tx;        /**< Frames to transmit: indices into cfg->tx_buf. */
	j1939_ring_t msgs;      /**< Messages for the application: indices into cfg->msg_buf. */
	j1939_ca_t ca[J1939_CFG_CA_MAX]; /**< State of the Controller Applications. */
	/** Self-configurable addresses claimed by other nodes, one bit each. */
	uint8_t addr_taken[J1939_ADDR_TAKEN_LEN];
	j1939_nametab_t nametab; /**< NAME table state. */
#if J1939_TP_ENABLE
	j1939_tp_t tp; /**< Transport protocol sessions. */
#endif
	j1939_stats_t stats; /**< Event counters. */
};

/**
 * @brief Sets up a stack instance from its configuration.
 *
 * This is the only setup call. The stack keeps @p cfg, which must outlive it. Everything
 * starts from the beginning: the queues are empty, the counters 0, every CA starts its claim
 * with the next j1939_process(), the NAME table is empty, the receive objects have no data and
 * the payload buffers of the transmit objects are filled with 0xFF.
 *
 * A configuration is valid if:
 *
 * - tx_buf and msg_buf hold at least one entry each;
 * - every other pointer is set where its length is above 0;
 * - the PGNs in rx_pgns and req_pgns are valid: at most J1939_PGN_MAX, lowest byte 0 for PDU1
 *   formats;
 * - there are at most J1939_CFG_CA_MAX CAs, each with an address of at most 253 that no other
 *   CA of the stack has;
 * - the receive objects, the transmit objects and the diagnostics of each CA are valid, see
 *   j1939_rxobj_cfg_t, j1939_txobj_cfg_t and j1939_dm_cfg_t.
 *
 * With an invalid configuration the stack is left without CAs, queues and tables: every later
 * call on it does nothing or reports an error, until j1939_init() succeeds.
 *
 * @param s    Stack.
 * @param cfg  Configuration.
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG on a NULL pointer or an invalid configuration.
 */
j1939_ret_t j1939_init(j1939_t *s, const j1939_cfg_t *cfg);

/**
 * @brief Handles a received frame.
 *
 * The frame is read in place during the call and may be reused afterwards.
 * Standard, remote and non-J1939 frames are ignored, so every frame of the
 * bus may be passed. Answers the stack generates go into the tx queue,
 * application messages into the message slots. Timers started by the frame
 * count from the next j1939_process().
 *
 * @param s      Stack.
 * @param frame  Received frame.
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG on a NULL pointer.
 */
j1939_ret_t j1939_rx(j1939_t *s, const j1939_port_frame_t *frame);

/**
 * @brief Returns the oldest frame of the tx queue without removing it.
 *
 * The integrator hands the frame to the CAN driver and removes it with
 * j1939_tx_pop() once the driver has accepted it. A frame the driver
 * refuses stays in the queue for the next attempt.
 *
 * @return Frame, or NULL if the tx queue is empty or @p s is NULL.
 */
const j1939_port_frame_t *j1939_tx_peek(j1939_t *s);

/**
 * @brief Removes the frame returned by j1939_tx_peek().
 *
 * @return J1939_RET_OK, J1939_RET_ERR_EMPTY, or J1939_RET_ERR_ARG if @p s is NULL.
 */
j1939_ret_t j1939_tx_pop(j1939_t *s);

/**
 * @brief Advances the stack's timers and queues the frames that are due.
 *
 * Runs address claiming, the Requests of the NAME table, the transport
 * protocol, diagnostics, the supervision of the receive objects and the
 * transmit objects with the time passed since the previous call.
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
 *         longer than J1939_MAX_MSG_LEN or an invalid identifier.
 */
j1939_ret_t j1939_send(j1939_t *s, j1939_ca_id_t ca, const j1939_msg_t *msg);

/** @return Event counters, or NULL if @p s is NULL. */
const j1939_stats_t *j1939_stats_get(const j1939_t *s);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* J1939_STACK_H */
