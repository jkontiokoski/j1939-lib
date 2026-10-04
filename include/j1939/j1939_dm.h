/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_dm.h
 * @brief J1939/73 diagnostics of a Controller Application: DM1, DM2, DM3 and DM11.
 */

#ifndef J1939_DM_H
#define J1939_DM_H

#include <stdbool.h>
#include <stdint.h>

#include "j1939/j1939_diag.h"
#include "j1939/j1939_ret.h"
#include "j1939/j1939_stack.h"

/**
 * @addtogroup grp_diag
 *
 * j1939_dm_init() enables diagnostics for a CA. The application owns its
 * fault memory (detection, occurrence counts, moving a DTC from active to
 * previously active) and copies the result into the stack; the stack
 * transmits it:
 *
 * - j1939_dm_active_set(), j1939_dm_prev_set() and j1939_dm_lamps_set() copy
 *   the active DTCs, the previously active DTCs and the lamp status. The
 *   stack keeps the DTCs in the order given.
 * - DM1 (active DTCs) is sent by j1939_process() to the global address once
 *   per J1939_DM1_PERIOD_US, also without DTCs. The first DM1 goes out in the
 *   call in which the CA's claim completes; the period counts from the next
 *   call and keeps its phase across late calls. A DM1 with two or more DTCs
 *   is sent with BAM.
 * - A change of the active DTC set (a DTC becoming active or leaving the set)
 *   sends a DM1 with the next j1939_process(), outside the periodic schedule.
 *   J1939/73 recommends at most one reported state change per DTC per second:
 *   a DTC whose change triggered a DM1 within the last J1939_DM1_PERIOD_US
 *   does not trigger another one; its new state goes out with the next DM1.
 *   The DTCs recently reported are remembered in the hold records of
 *   j1939_dm_cfg_t; while every record is in use, a change triggers no DM1
 *   and waits for the periodic one, so at most hold_len change-triggered DM1s
 *   go out per second. Changes of the lamp status or of an occurrence count
 *   trigger no DM1.
 * - A Request (PGN 59904) for DM1 or DM2, global or to the CA's address, is
 *   answered by the stack and not delivered to the application. Both are PDU2
 *   PGNs: a single frame answer goes to the global address. A multi-packet
 *   answer goes with BAM to the global address for a global Request, and with
 *   RTS/CTS to the requester for a destination specific one. Requests from
 *   several requesters while an answer is pending are answered once, to the
 *   global address. The periodic and change-triggered DM1 always go to the
 *   global address.
 * - A Request for DM3 (clear previously active DTCs) or DM11 (clear active
 *   DTCs) is accepted only if enabled in j1939_dm_cfg_t. The stack does not
 *   clear anything by itself: it reports the request through
 *   j1939_dm_clear_get(), and the application decides with
 *   j1939_dm_clear_confirm(). A request from another requester while one is
 *   in progress is answered with Cannot Respond; a global one is covered by
 *   the request in progress. Without the option the Request is handled as for
 *   any other unsupported PGN: NACK when destination specific, delivered if
 *   the PGN is in req_pgns.
 *
 * Only a CA that has claimed its address transmits. While a CA has not
 * claimed an address, pending answers are discarded and counted.
 *
 * DM1, DM2 and acknowledgements that find the tx queue full or the CA's
 * broadcast busy are retried with every j1939_process() and counted in
 * j1939_stats_t::dm_tx_retry. DM2 answers and acknowledgements not sent
 * within J1939_DM_RESPONSE_US, and periodic DM1s still unsent when the next
 * one is due, are counted in j1939_stats_t::dm_tx_dropped. A BAM of many DTCs
 * can outlast a DM1 period: size the DTC lists for what the bus carries.
 *
 * Not supported: DM1 on several networks, lamp changes as a DM1 trigger, and
 * the OBD rule that DM11 is accepted only globally.
 *
 * @{
 */

#define J1939_DM1_PERIOD_US 1000000U /**< DM1 period, and the per-DTC change hold time. */

/** Response time Tr of J1939/21: for DM2 answers, acknowledgements and clear decisions. */
#define J1939_DM_RESPONSE_US 200000U

/** Bytes of the payload buffer for up to @p dtcs DTCs (see j1939_dm_cfg_t::buf). */
#define J1939_DM_BUF_LEN(dtcs) (((dtcs) <= 1U) ? 8U : (2U + (4U * (dtcs))))

/** Record of a DTC whose change was reported recently. Members are private. */
typedef struct j1939_dm_hold {
	uint32_t spn;     /**< SPN of the DTC. */
	uint32_t left_us; /**< Hold time left; the record is free at 0. */
	uint8_t fmi;      /**< FMI of the DTC. */
	bool fresh;       /**< Started since the last j1939_process(). */
} j1939_dm_hold_t;

/** Diagnostics configuration of a CA. All storage must outlive the stack. */
typedef struct j1939_dm_cfg {
	j1939_diag_dtc_t *active; /**< Active DTC storage. May be NULL if active_len is 0. */
	uint16_t active_len;      /**< Active DTCs the CA can report, 0..J1939_DIAG_DM_DTC_MAX. */
	j1939_diag_dtc_t *prev; /**< Previously active DTC storage. May be NULL if prev_len is 0. */
	uint16_t prev_len; /**< Previously active DTCs the CA can report, 0..J1939_DIAG_DM_DTC_MAX.
	                    */
	j1939_dm_hold_t *hold; /**< Change hold records. May be NULL if hold_len is 0. */
	uint16_t hold_len; /**< Records: DTCs whose changes can trigger a DM1 within one second. */
	uint8_t *buf;      /**< Payload buffer for building DM1 and DM2. */
	uint16_t buf_len; /**< At least J1939_DM_BUF_LEN() of the larger of active_len, prev_len. */
	bool dm3_enable;  /**< Accept DM3 requests and hand them to the application. */
	bool dm11_enable; /**< Accept DM11 requests and hand them to the application. */
} j1939_dm_cfg_t;

/** State of a clear request (DM3 or DM11). Private to the library. */
typedef enum j1939_dm_clear_state {
	J1939_DM_CLEAR_IDLE = 0,  /**< No request. */
	J1939_DM_CLEAR_REQUESTED, /**< Waiting for the application's decision. */
	J1939_DM_CLEAR_ACK,       /**< Accepted; positive acknowledgement to send. */
	J1939_DM_CLEAR_NACK,      /**< Refused or not decided in time; NACK to send. */
} j1939_dm_clear_state_t;

/** Clear request handling. Members are private. */
typedef struct j1939_dm_clear {
	j1939_dm_clear_state_t state; /**< Protocol state. */
	uint32_t timer_us;            /**< Time spent in the state. */
	uint8_t requester;            /**< Address to acknowledge; J1939_ADDR_GLOBAL for none. */
	bool enabled;                 /**< Requests are accepted. */
	bool fresh;                   /**< Timer started since the last j1939_process(). */
} j1939_dm_clear_t;

/** Pending answer to a Request for DM1 or DM2. Members are private. */
typedef struct j1939_dm_answer {
	uint32_t timer_us; /**< Age of the answer. */
	uint8_t da;        /**< Destination of a multi-packet answer; J1939_ADDR_GLOBAL for BAM. */
	bool due;          /**< The answer is to be sent. */
	bool fresh;        /**< Requested since the last j1939_process(). */
} j1939_dm_answer_t;

/** Diagnostic state of a CA. Allocated by the integrator, members are private. */
typedef struct j1939_dm {
	j1939_diag_dtc_t *active;    /**< Active DTCs. */
	j1939_diag_dtc_t *prev;      /**< Previously active DTCs. */
	j1939_dm_hold_t *hold;       /**< Change hold records. */
	uint8_t *buf;                /**< Payload buffer. */
	uint16_t active_len;         /**< Capacity of active. */
	uint16_t active_count;       /**< DTCs in active. */
	uint16_t prev_len;           /**< Capacity of prev. */
	uint16_t prev_count;         /**< DTCs in prev. */
	uint16_t hold_len;           /**< Entries in hold. */
	uint16_t buf_len;            /**< Size of buf. */
	j1939_diag_lamps_t lamps;    /**< Lamp status sent with DM1 and DM2. */
	uint32_t dm1_timer_us;       /**< Time since the periodic DM1 was last due. */
	j1939_dm_answer_t answer[2]; /**< Answers to Requests for DM1 and DM2. */
	j1939_dm_clear_t clear[2];   /**< DM3 and DM11 requests. */
	bool started;                /**< The CA has claimed its address and sent its first DM1. */
	bool dm1_due;                /**< A DM1 is to be sent. */
} j1939_dm_t;

/**
 * @brief Enables diagnostics for a Controller Application.
 *
 * Starts with no DTCs and all lamps off (flash bits "do not flash"). May be
 * called again to reset the state; @p dm must not be used by another CA.
 *
 * @param s    Stack.
 * @param ca   Controller Application.
 * @param dm   State storage for the CA.
 * @param cfg  Configuration.
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG on a NULL pointer, an unknown
 *         CA, a capacity above J1939_DIAG_DM_DTC_MAX, a missing or too small
 *         buffer, a payload larger than J1939_CFG_TP_BUF_SIZE, a payload that
 *         needs the transport protocol while the stack has no transmit
 *         buffer, or a @p dm already used by another CA.
 */
j1939_ret_t j1939_dm_init(j1939_t *s, j1939_ca_id_t ca, j1939_dm_t *dm, const j1939_dm_cfg_t *cfg);

/**
 * @brief Sets the lamp status sent with DM1 and DM2.
 *
 * @return J1939_RET_OK, J1939_RET_ERR_STATE if diagnostics are not enabled
 *         for @p ca, or J1939_RET_ERR_ARG on a NULL pointer, an unknown CA or
 *         a lamp value above J1939_DIAG_LAMP_MAX.
 */
j1939_ret_t j1939_dm_lamps_set(j1939_t *s, j1939_ca_id_t ca, const j1939_diag_lamps_t *lamps);

/**
 * @brief Replaces the active DTCs.
 *
 * DTCs are identified by SPN and FMI. A DTC that is new, or that is missing
 * from @p dtcs, has changed state and may trigger a DM1, see the file
 * description.
 *
 * @param s      Stack.
 * @param ca     Controller Application.
 * @param dtcs   Active DTCs, in the order DM1 lists them. May be NULL if
 *               @p count is 0.
 * @param count  Number of DTCs.
 * @return J1939_RET_OK;
 *         J1939_RET_ERR_FULL if @p count exceeds j1939_dm_cfg_t::active_len;
 *         J1939_RET_ERR_STATE if diagnostics are not enabled for @p ca;
 *         J1939_RET_ERR_ARG on a NULL pointer, an unknown CA, a DTC that
 *         j1939_diag_dm_build() rejects, or two DTCs with the same SPN and
 *         FMI. Nothing changes on error.
 */
j1939_ret_t j1939_dm_active_set(j1939_t *s, j1939_ca_id_t ca, const j1939_diag_dtc_t *dtcs,
                                uint16_t count);

/**
 * @brief Replaces the previously active DTCs, sent with DM2.
 *
 * @return As j1939_dm_active_set(), with j1939_dm_cfg_t::prev_len as the
 *         capacity. A change triggers no transmission.
 */
j1939_ret_t j1939_dm_prev_set(j1939_t *s, j1939_ca_id_t ca, const j1939_diag_dtc_t *dtcs,
                              uint16_t count);

/**
 * @brief Reads a clear request waiting for the application's decision.
 *
 * A request waits at most J1939_DM_RESPONSE_US after the first
 * j1939_process() following its reception. When both are waiting, DM3 is
 * reported first.
 *
 * @param s    Stack.
 * @param ca   Controller Application.
 * @param pgn  J1939_PGN_DM3 (clear previously active DTCs) or J1939_PGN_DM11
 *             (clear active DTCs). Written only on success.
 * @return J1939_RET_OK, J1939_RET_ERR_EMPTY if no request is waiting,
 *         J1939_RET_ERR_STATE if diagnostics are not enabled for @p ca, or
 *         J1939_RET_ERR_ARG on a NULL pointer or an unknown CA.
 */
j1939_ret_t j1939_dm_clear_get(const j1939_t *s, j1939_ca_id_t ca, uint32_t *pgn);

/**
 * @brief Accepts or refuses a waiting clear request.
 *
 * On acceptance the stack empties its copy of the concerned list (the
 * previously active DTCs for DM3, the active DTCs for DM11; the other list
 * is kept) and the application clears its own diagnostic records of those
 * DTCs before the next j1939_process(), which sends the positive
 * acknowledgement. Faults still present are reported again with the next
 * j1939_dm_active_set(). On refusal a negative acknowledgement is sent. Only
 * a destination specific request is acknowledged.
 *
 * The application clears its records only when this function returns
 * J1939_RET_OK with @p accept true.
 *
 * @param s       Stack.
 * @param ca      Controller Application.
 * @param pgn     J1939_PGN_DM3 or J1939_PGN_DM11.
 * @param accept  true to clear, false to refuse.
 * @return J1939_RET_OK, J1939_RET_ERR_STATE if no such request is waiting
 *         (never received, already decided or timed out) or diagnostics are
 *         not enabled for @p ca, or J1939_RET_ERR_ARG on a NULL pointer, an
 *         unknown CA or another PGN.
 */
j1939_ret_t j1939_dm_clear_confirm(j1939_t *s, j1939_ca_id_t ca, uint32_t pgn, bool accept);

/** @} */

#endif /* J1939_DM_H */
