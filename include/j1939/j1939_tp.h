/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_tp.h
 * @brief J1939/21 transport protocol: broadcast (BAM) and connection mode (RTS/CTS).
 */

#ifndef J1939_TP_H
#define J1939_TP_H

#include <stdbool.h>
#include <stdint.h>

#include "j1939/j1939_config.h"

/**
 * @addtogroup grp_tp
 *
 * The transport protocol carries messages of 9 to 1785 bytes. It runs inside
 * the stack and has no API of its own:
 *
 * - j1939_send() hands a message longer than 8 bytes to the transport
 *   protocol. It is copied into a transmit buffer (j1939_cfg_t::tp_tx_buf),
 *   so the caller may reuse its data after the call. A message to
 *   J1939_ADDR_GLOBAL is broadcast with BAM, a message to a node is sent with
 *   RTS/CTS. The CA must have claimed its address, as for a single frame.
 * - j1939_rx() reassembles multi-packet messages in reassembly buffers
 *   (j1939_cfg_t::tp_rx_buf) and delivers complete messages through the
 *   message slots, where msg->data points into the buffer. The buffer stays
 *   reserved until the message is released with j1939_msg_pop().
 * - Only PGNs in rx_pgns are received, PGNs a receive object takes from the
 *   sender (@ref grp_rxobj), and Commanded Address while a CA accepts it
 *   (@ref grp_addr). An RTS for another PGN is answered with Connection Abort
 *   (J1939_TP_ABORT_OTHER), a BAM for it is ignored. A message only for a
 *   receive object or for address claiming needs no message slot, so the
 *   responder never holds for it.
 * - TP.CM and TP.DT frames that are not 8 bytes long, or come from address
 *   254 or 255, are dropped.
 * - A received message's msg->prio is the priority of its RTS or BAM, msg->da
 *   the responder's address for RTS/CTS and J1939_ADDR_GLOBAL for BAM. The
 *   originator sends RTS, BAM and data packets with the message priority;
 *   CTS, EndOfMsgAck and Connection Abort use priority 7.
 * - Timers advance only through j1939_process(). A timer started by an event
 *   counts from the next j1939_process() call, so a timeout expires between
 *   its nominal value and one call period later.
 *
 * Sessions come from a pool of J1939_CFG_TP_SESSIONS per stack, shared by
 * both directions. A stack runs at most one session per direction and pair of
 * addresses: one BAM per sender and one RTS/CTS connection per originator and
 * responder.
 *
 * - Address claiming: CTS, EndOfMsgAck and Connection Abort go out only from
 *   a claimed address, so an RTS to a CA still in its contention wait is not
 *   answered and the originator times out. BAM reception sends nothing and
 *   does not depend on the claim. When a CA loses its address or moves to
 *   another one, the sessions of its old address end without Connection Abort
 *   and are counted in j1939_stats_t::tp_tx_aborted and tp_rx_aborted.
 * - BAM data packets go out one per j1939_process() call, never less than
 *   J1939_CFG_TP_BAM_GAP_US apart. Call j1939_process() often enough to keep
 *   the gap below 200 ms.
 * - The originator sends the packets a CTS requests as far as the tx queue
 *   has room, and waits for room at most J1939_TP_TR_US. It honours CTS with
 *   0 packets (hold, J1939_TP_T4_US) and retransmission requests.
 * - The responder asks for all remaining packets in one CTS, limited by the
 *   RTS's packets-per-CTS value. While all message slots are in use it holds
 *   the connection before requesting the packets that complete the message:
 *   CTS with 0 packets every J1939_TP_TH_US, at most J1939_TP_HOLD_MAX times,
 *   then Connection Abort (J1939_TP_ABORT_RESOURCES). A completed RTS/CTS
 *   message that still finds no free message slot is answered with Connection
 *   Abort (J1939_TP_ABORT_RESOURCES) instead of EndOfMsgAck, so the
 *   originator learns that it was lost.
 * - An RTS is refused with J1939_TP_ABORT_BUSY without a free session, with
 *   J1939_TP_ABORT_RESOURCES without a free buffer or when larger than
 *   J1939_CFG_TP_BUF_SIZE, with J1939_TP_ABORT_TOO_LARGE when larger than
 *   1785 bytes, and with J1939_TP_ABORT_OTHER when its size and packet count
 *   do not match.
 * - On reception a packet already received is ignored; a skipped sequence
 *   number aborts a connection (J1939_TP_ABORT_BAD_SEQ) and drops a BAM; a
 *   data packet while holding aborts (J1939_TP_ABORT_UNEXPECTED_DT). A
 *   repeated RTS for the same PGN from an open originator restarts the
 *   connection without an abort; an RTS for another PGN is refused with
 *   J1939_TP_ABORT_BUSY and the open connection continues. A new BAM from a
 *   sender replaces its unfinished one. A Connection Abort ends the matching
 *   session (same peer and PGN).
 *
 * Frames the stack generates (CTS, EndOfMsgAck, Connection Abort) are dropped
 * and counted in j1939_stats_t::tx_overflow when the tx queue is full; the
 * protocol timers then end the session.
 *
 * @{
 */

#define J1939_PGN_TP_CM 0xEC00U /**< TP.CM connection management, PGN 60416. */
#define J1939_PGN_TP_DT 0xEB00U /**< TP.DT data transfer, PGN 60160. */

#define J1939_TP_MSG_MIN 9U    /**< Smallest message sent with the transport protocol. */
#define J1939_TP_MSG_MAX 1785U /**< Largest message: 255 packets of 7 bytes. */

/** @name Connection Abort reasons (J1939/21)
 * @{ */
#define J1939_TP_ABORT_BUSY          1U   /**< Already in a session, cannot support another. */
#define J1939_TP_ABORT_RESOURCES     2U   /**< System resources were needed for another task. */
#define J1939_TP_ABORT_TIMEOUT       3U   /**< A timeout occurred. */
#define J1939_TP_ABORT_CTS_IN_DATA   4U   /**< CTS received while data transfer is in progress. */
#define J1939_TP_ABORT_RETRANSMIT    5U   /**< Maximum retransmit request limit reached. */
#define J1939_TP_ABORT_UNEXPECTED_DT 6U   /**< Unexpected data transfer packet. */
#define J1939_TP_ABORT_BAD_SEQ       7U   /**< Bad sequence number. */
#define J1939_TP_ABORT_DUP_SEQ       8U   /**< Duplicate sequence number. */
#define J1939_TP_ABORT_TOO_LARGE     9U   /**< Message size is greater than 1785 bytes. */
#define J1939_TP_ABORT_OTHER         250U /**< Any other reason. */
/** @} */

/** @name Protocol timers (J1939/21), in microseconds
 * @{ */
#define J1939_TP_T1_US 750000U  /**< Responder: time between data packets. */
#define J1939_TP_T2_US 1250000U /**< Responder: time from CTS to the first data packet. */
#define J1939_TP_T3_US                                                                           \
	1250000U                /**< Originator: time from the last data packet (or RTS) to CTS. \
	                         */
#define J1939_TP_T4_US 1050000U /**< Originator: time from a hold CTS to the next CTS. */
#define J1939_TP_TR_US 200000U  /**< Time to respond, and to send a due data packet. */
#define J1939_TP_TH_US                                                            \
	500000U /**< Responder: interval of the hold CTSs that keep a connection. \
	         */
/** @} */

/** Hold CTSs a responder sends before it aborts the connection (J1939_TP_ABORT_RESOURCES). */
#define J1939_TP_HOLD_MAX 4U

/**
 * Buffer for one transport protocol message. Allocated by the integrator,
 * members are private.
 */
typedef struct j1939_tp_buf {
	uint8_t data[J1939_CFG_TP_BUF_SIZE]; /**< Message payload. */
	uint16_t slot;                       /**< Message slot the message was delivered to. */
	uint8_t state;                       /**< Free, in a session, or delivered. */
} j1939_tp_buf_t;

/** Session states. Private to the library. */
typedef enum j1939_tp_state {
	J1939_TP_IDLE = 0, /**< Session unused. */
	J1939_TP_TX_BAM,   /**< Broadcasting data packets. */
	J1939_TP_TX_WAIT,  /**< Originator waiting for CTS or EndOfMsgAck (T3). */
	J1939_TP_TX_HOLD,  /**< Originator held by the responder (T4). */
	J1939_TP_TX_DATA,  /**< Originator sending the packets a CTS requested (Tr). */
	J1939_TP_RX_BAM,   /**< Receiving a broadcast (T1). */
	J1939_TP_RX_DATA,  /**< Responder waiting for requested packets (T2, T1). */
	J1939_TP_RX_HOLD,  /**< Responder holding the connection until a message slot is free (Th).
	                    */
} j1939_tp_state_t;

/** Transport protocol session. Members are private. */
typedef struct j1939_tp_session {
	j1939_tp_state_t state; /**< Protocol state. */
	j1939_tp_buf_t *buf;    /**< Message buffer. */
	uint32_t pgn;           /**< PGN of the message. */
	uint32_t timer_us;      /**< Time since the timer was started. */
	uint32_t timeout_us;    /**< Expiry of the running timer. */
	uint16_t len;           /**< Message length. */
	uint16_t packets;       /**< Number of packets. */
	uint16_t next;          /**< Next packet to send or receive, 1-based. */
	uint16_t last;          /**< Last packet of the current CTS window. */
	uint8_t limit;          /**< Responder: packets per CTS the originator allows. */
	uint8_t holds;          /**< Responder: hold CTSs sent. */
	uint8_t prio;   /**< Priority of the message and of the frames the originator sends. */
	uint8_t local;  /**< Own address; J1939_ADDR_GLOBAL for a received broadcast. */
	uint8_t remote; /**< Peer address; J1939_ADDR_GLOBAL for a sent broadcast. */
	bool tx;        /**< true if this stack is the originator. */
	bool fresh;     /**< Timer started since the last j1939_process(). */
} j1939_tp_session_t;

/** Transport protocol state of a stack. Members are private. */
typedef struct j1939_tp {
	j1939_tp_session_t sessions[J1939_CFG_TP_SESSIONS]; /**< Session pool. */
	j1939_tp_buf_t *tx_buf;                             /**< Transmit buffers. */
	j1939_tp_buf_t *rx_buf;                             /**< Reassembly buffers. */
	uint16_t tx_buf_len;                                /**< Entries in tx_buf. */
	uint16_t rx_buf_len;                                /**< Entries in rx_buf. */
} j1939_tp_t;

/** @} */

#endif /* J1939_TP_H */
