/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_txobj.c
 * @brief Transmit objects: PGNs the stack sends periodically, on change and on
 *        Request.
 */

#include "j1939/j1939_txobj.h"

#include <stddef.h>
#include <string.h>

#include "j1939/j1939_addr.h"
#include "j1939/j1939_dtc.h"
#include "j1939/j1939_id.h"
#include "j1939/j1939_request.h"
#include "j1939/j1939_tp.h"
#include "j1939_internal.h"

#define FLAG_STARTED 0x01U /**< The CA has claimed its address; the schedule runs. */
#define FLAG_DUE     0x02U /**< Periodic send due. */
#define FLAG_CHANGED 0x04U /**< Change waiting for its send. */
#define FLAG_ANSWER  0x08U /**< Answer to a Request pending. */
#define FLAG_FRESH   0x10U /**< Answer recorded since the last j1939_process(). */
#define PAD          0xFFU /**< Initial payload byte: "not available". */
#define MSG_LEN_MAX  ((uint32_t)J1939_MAX_MSG_LEN) /**< Largest object payload. */

/** Reads a state flag of an object, one of the FLAG_ bits. */
static bool flag_get(const j1939_txobj_t *o, uint8_t flag) {
	return (o->flags & flag) != 0U;
}

/** Sets or clears a state flag of an object, one of the FLAG_ bits. */
static void flag_put(j1939_txobj_t *o, uint8_t flag, bool on) {
	if (on) {
		o->flags = (uint8_t)(o->flags | flag);
	} else {
		o->flags = (uint8_t)(o->flags & (uint8_t)~flag);
	}
}

/**
 * Tells whether the stack builds or answers the messages of a PGN itself: what the receive path
 * consumes, Acknowledgement, Commanded Address, DM1, DM2, DM3 and DM11.
 */
static bool pgn_stack_owned(uint32_t pgn) {
	return j1939_stack_pgn_owned(pgn) || (pgn == J1939_PGN_ACK) ||
	       (pgn == J1939_PGN_COMMANDED_ADDRESS) || (pgn == J1939_PGN_DM1) ||
	       (pgn == J1939_PGN_DM2) || (pgn == J1939_PGN_DM3) || (pgn == J1939_PGN_DM11);
}

/**
 * Checks one entry of a transmit object table: it has a buffer, a known CA, a valid PGN and
 * priority not owned by the stack nor in req_pgns, a length of 1 to J1939_MAX_MSG_LEN (above 8 only
 * with a TP transmit buffer) and a destination other than J1939_ADDR_NULL, global for a single
 * frame PDU2 PGN.
 */
static bool tx_entry_valid(const j1939_t *s, const j1939_txobj_cfg_t *c) {
	bool single = c->len <= J1939_MSG_SINGLE_FRAME_MAX;

	return (c->buf != NULL) && (c->ca < s->cfg->ca_len) && j1939_pgn_valid(c->pgn) &&
	       (c->prio <= J1939_PRIO_MAX) && (c->len > 0U) && ((uint32_t)c->len <= MSG_LEN_MAX) &&
	       (single || j1939_tp_tx_buf_allocated(s)) && (c->da != J1939_ADDR_NULL) &&
	       (j1939_pgn_is_pdu1(c->pgn) || !single || (c->da == J1939_ADDR_GLOBAL)) &&
	       !pgn_stack_owned(c->pgn) &&
	       !j1939_stack_pgn_listed(s->cfg->req_pgns, s->cfg->req_pgns_len, c->pgn);
}

/**
 * Checks a whole transmit object table: every entry is valid and no two have the same CA, PGN and
 * destination.
 */
static bool tx_table_valid(const j1939_t *s, const j1939_txobj_cfg_t *cfg, uint16_t len) {
	bool valid = true;
	uint16_t i;
	uint16_t j;

	for (i = 0U; valid && (i < len); i++) {
		valid = tx_entry_valid(s, &cfg[i]);
		for (j = 0U; valid && (j < i); j++) {
			valid = (cfg[j].ca != cfg[i].ca) || (cfg[j].pgn != cfg[i].pgn) ||
			        (cfg[j].da != cfg[i].da);
		}
	}
	return valid;
}

/** Sends the payload of an object to da. Returns as j1939_send(). */
static j1939_ret_t obj_send(j1939_t *s, const j1939_txobj_cfg_t *c, uint8_t da) {
	const j1939_msg_t msg = {c->pgn, c->prio, 0U, da, c->len, c->buf};

	return j1939_send(s, c->ca, &msg);
}

/**
 * Chooses the destination of an answer to a Request: J1939_ADDR_GLOBAL for a single frame of a PDU2
 * PGN, else the requester, which is J1939_ADDR_GLOBAL for a global Request.
 */
static uint8_t answer_da(const j1939_txobj_cfg_t *c, uint8_t requester) {
	return ((c->len <= J1939_MSG_SINGLE_FRAME_MAX) && !j1939_pgn_is_pdu1(c->pgn))
	               ? J1939_ADDR_GLOBAL
	               : requester;
}

/**
 * Handles the result of a send that is retried while the tx queue is full or the transport protocol
 * busy. Returns true when the send is over, sent or given up.
 *
 * Retries are counted in txobj_tx_retry; a send given up, at the latest when age_us reaches
 * J1939_TR_US, in txobj_tx_dropped. With J1939_STACK_NO_EXPIRY as its age a send is retried until
 * it succeeds.
 */
static bool obj_send_done(j1939_t *s, j1939_ret_t ret, uint32_t age_us) {
	return j1939_stack_retry_done(ret, age_us, &s->stats.txobj_tx_retry,
	                              &s->stats.txobj_tx_dropped);
}

/**
 * Stops an object whose CA may not transmit.
 *
 * A pending answer is dropped and counted; the schedule restarts with the next claim.
 */
static void obj_stop(j1939_t *s, j1939_txobj_t *o) {
	if (flag_get(o, (uint8_t)FLAG_ANSWER)) {
		s->stats.txobj_tx_dropped++;
	}
	o->flags = 0U;
}

/**
 * Starts the schedule when the claim completes: periodic and change objects are sent in this call.
 */
static void obj_start(const j1939_txobj_cfg_t *c, j1939_txobj_t *o) {
	o->period_us = 0U;
	o->since_us = UINT32_MAX;
	flag_put(o, (uint8_t)FLAG_STARTED, true);
	flag_put(o, (uint8_t)FLAG_DUE, c->period_us > 0U);
	flag_put(o, (uint8_t)FLAG_CHANGED, c->inhibit_us > 0U);
}

/**
 * Advances the object's timers and marks a periodic send due.
 *
 * A periodic send still due when the next one falls due is counted in txobj_tx_dropped. The period
 * keeps its phase across late calls.
 */
static void schedule_tick(j1939_t *s, const j1939_txobj_cfg_t *c, j1939_txobj_t *o,
                          uint32_t elapsed_us) {
	j1939_stack_timer_add(&o->since_us, elapsed_us);
	if (c->period_us > 0U) {
		j1939_stack_timer_add(&o->period_us, elapsed_us);
		if (o->period_us >= c->period_us) {
			if (flag_get(o, (uint8_t)FLAG_DUE)) {
				/* The previous periodic send never went out. */
				s->stats.txobj_tx_dropped++;
			}
			flag_put(o, (uint8_t)FLAG_DUE, true);
			o->period_us %= c->period_us;
		}
	}
}

/**
 * Sends a due periodic or change-triggered message to the object's destination.
 *
 * A change is sent once inhibit_us has passed since the object's last send. A send to the global
 * address also serves a pending answer that would go to the global address.
 */
static void schedule_send(j1939_t *s, const j1939_txobj_cfg_t *c, j1939_txobj_t *o) {
	if (flag_get(o, (uint8_t)FLAG_DUE) ||
	    (flag_get(o, (uint8_t)FLAG_CHANGED) && (o->since_us >= c->inhibit_us))) {
		j1939_ret_t ret = obj_send(s, c, c->da);

		if (obj_send_done(s, ret, J1939_STACK_NO_EXPIRY)) {
			flag_put(o, (uint8_t)FLAG_DUE, false);
			flag_put(o, (uint8_t)FLAG_CHANGED, false);
			if (ret == J1939_RET_OK) {
				o->since_us = 0U;
				/* A broadcast answers a pending Request whose answer is global too.
				 */
				if ((c->da == J1939_ADDR_GLOBAL) &&
				    (answer_da(c, o->requester) == J1939_ADDR_GLOBAL)) {
					flag_put(o, (uint8_t)FLAG_ANSWER, false);
				}
			}
		}
	}
}

/** Sends a pending answer to a Request, retried at most J1939_TR_US. */
static void obj_answer_tick(j1939_t *s, const j1939_txobj_cfg_t *c, j1939_txobj_t *o,
                            uint32_t elapsed_us) {
	if (flag_get(o, (uint8_t)FLAG_ANSWER)) {
		if (flag_get(o, (uint8_t)FLAG_FRESH)) {
			flag_put(o, (uint8_t)FLAG_FRESH, false);
		} else {
			j1939_stack_timer_add(&o->answer_us, elapsed_us);
		}
		if (obj_send_done(s, obj_send(s, c, answer_da(c, o->requester)), o->answer_us)) {
			flag_put(o, (uint8_t)FLAG_ANSWER, false);
		}
	}
}

/**
 * Records a Request for the object; requests of several nodes are answered once, globally. The
 * requester is J1939_ADDR_GLOBAL for a global Request.
 */
static void obj_answer_request(j1939_txobj_t *o, uint8_t requester) {
	if (!flag_get(o, (uint8_t)FLAG_ANSWER)) {
		flag_put(o, (uint8_t)FLAG_ANSWER, true);
		flag_put(o, (uint8_t)FLAG_FRESH, true);
		o->answer_us = 0U;
		o->requester = requester;
	} else if (o->requester != requester) {
		o->requester = J1939_ADDR_GLOBAL;
	} else {
		/* Same requester again. */
	}
}

/**
 * Tells whether an object is the first of its CA with its PGN: that one answers a Request, the
 * later ones differ only in their destination.
 */
static bool obj_first_of_pgn(const j1939_t *s, uint16_t index) {
	const j1939_txobj_cfg_t *c = &s->cfg->txobj_cfg[index];
	bool first = true;
	uint16_t i;

	for (i = 0U; first && (i < index); i++) {
		first = (s->cfg->txobj_cfg[i].ca != c->ca) || (s->cfg->txobj_cfg[i].pgn != c->pgn);
	}
	return first;
}

bool j1939_txobj_init(j1939_t *s) {
	const j1939_txobj_cfg_t *cfg = s->cfg->txobj_cfg;
	uint16_t len = s->cfg->txobj_len;
	bool valid = tx_table_valid(s, cfg, len);
	uint16_t i;

	for (i = 0U; valid && (i < len); i++) {
		j1939_txobj_t *o = &s->cfg->txobj[i];
		/* A local keeps cppcheck's MISRA 11.8 check from treating the storage as const. */
		uint8_t *buf = cfg[i].buf;

		/* "Not available" until the application sets a payload. */
		(void)memset(buf, (int)PAD, (size_t)cfg[i].len);
		(void)memset(o, 0, sizeof(*o));
		o->requester = J1939_ADDR_GLOBAL;
	}
	return valid;
}

bool j1939_txobj_request_handle(j1939_t *s, const j1939_msg_t *req, uint32_t pgn) {
	bool handled = false;
	uint16_t i;

	for (i = 0U; i < s->cfg->txobj_len; i++) {
		const j1939_txobj_cfg_t *c = &s->cfg->txobj_cfg[i];
		const j1939_ca_t *ca = &s->ca[c->ca];

		if ((c->pgn == pgn) && j1939_addr_ca_addressed(ca, req->da) &&
		    obj_first_of_pgn(s, i)) {
			handled = true;
			/* Nothing is sent before the claim completes. */
			if (j1939_addr_ca_claimed(ca)) {
				j1939_txobj_t *o = &s->cfg->txobj[i];

				obj_answer_request(o, j1939_request_requester(req));
			}
		}
	}
	return handled;
}

void j1939_txobj_process(j1939_t *s, uint32_t elapsed_us) {
	uint16_t i;

	for (i = 0U; i < s->cfg->txobj_len; i++) {
		const j1939_txobj_cfg_t *c = &s->cfg->txobj_cfg[i];
		j1939_txobj_t *o = &s->cfg->txobj[i];

		if (!j1939_addr_ca_claimed(&s->ca[c->ca])) {
			obj_stop(s, o);
		} else {
			if (!flag_get(o, (uint8_t)FLAG_STARTED)) {
				obj_start(c, o);
			} else {
				schedule_tick(s, c, o, elapsed_us);
			}
			schedule_send(s, c, o);
			obj_answer_tick(s, c, o, elapsed_us);
		}
	}
}

j1939_ret_t j1939_txobj_set(j1939_t *s, uint16_t index, const uint8_t *data, uint16_t len) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((s != NULL) && (data != NULL) && (index < s->cfg->txobj_len) &&
	    (len == s->cfg->txobj_cfg[index].len)) {
		const j1939_txobj_cfg_t *c = &s->cfg->txobj_cfg[index];
		uint8_t *buf = c->buf;

		if ((c->inhibit_us > 0U) && (memcmp(buf, data, (size_t)len) != 0)) {
			j1939_txobj_t *o = &s->cfg->txobj[index];

			flag_put(o, (uint8_t)FLAG_CHANGED, true);
		}
		(void)memcpy(buf, data, (size_t)len);
		ret = J1939_RET_OK;
	}
	return ret;
}
