/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

#include "j1939/j1939_txobj.h"

#include <stddef.h>
#include <string.h>

#include "j1939/j1939_addr.h"
#include "j1939/j1939_diag.h"
#include "j1939/j1939_id.h"
#include "j1939/j1939_request.h"
#include "j1939/j1939_tp.h"
#include "j1939_addr_priv.h"
#include "j1939_stack_priv.h"
#include "j1939_txobj_priv.h"

#define FLAG_STARTED 0x01U /* the CA has claimed its address; the schedule runs */
#define FLAG_DUE     0x02U /* periodic send due */
#define FLAG_CHANGED 0x04U /* change waiting for its send */
#define FLAG_ANSWER  0x08U /* answer to a Request pending */
#define FLAG_FRESH   0x10U /* answer recorded since the last j1939_process() */
#define PAD          0xFFU /* "not available" */
#define FRAME_LEN    8U
#define TP_BUF_SIZE  ((uint32_t)J1939_CFG_TP_BUF_SIZE)
#define NO_EXPIRY    0U /* age of a send that is retried until it succeeds */

static bool flag_get(const j1939_txobj_t *o, uint8_t flag) {
	return (o->flags & flag) != 0U;
}

static void flag_put(j1939_txobj_t *o, uint8_t flag, bool on) {
	if (on) {
		o->flags = (uint8_t)(o->flags | flag);
	} else {
		o->flags = (uint8_t)(o->flags & (uint8_t)~flag);
	}
}

static void timer_add(uint32_t *timer_us, uint32_t elapsed_us) {
	if (elapsed_us > (UINT32_MAX - *timer_us)) {
		*timer_us = UINT32_MAX;
	} else {
		*timer_us += elapsed_us;
	}
}

/* PGNs whose messages the stack builds or answers itself. */
static bool pgn_stack_owned(uint32_t pgn) {
	return (pgn == J1939_PGN_REQUEST) || (pgn == J1939_PGN_ACK) || (pgn == J1939_PGN_TP_CM) ||
	       (pgn == J1939_PGN_TP_DT) || (pgn == J1939_PGN_ADDRESS_CLAIMED) ||
	       (pgn == J1939_PGN_COMMANDED_ADDRESS) || (pgn == J1939_PGN_DM1) ||
	       (pgn == J1939_PGN_DM2) || (pgn == J1939_PGN_DM3) || (pgn == J1939_PGN_DM11);
}

static bool entry_valid(const j1939_t *s, const j1939_txobj_cfg_t *c) {
	bool single = c->len <= FRAME_LEN;

	return (c->buf != NULL) && (c->ca < s->ca_count) && j1939_stack_pgn_valid(c->pgn) &&
	       (c->prio <= J1939_PRIO_MAX) && (c->len > 0U) && ((uint32_t)c->len <= TP_BUF_SIZE) &&
	       (single || (s->tp.tx_buf_len > 0U)) && (c->da != J1939_ADDR_NULL) &&
	       (j1939_pgn_is_pdu1(c->pgn) || !single || (c->da == J1939_ADDR_GLOBAL)) &&
	       !pgn_stack_owned(c->pgn) &&
	       !j1939_stack_pgn_listed(s->req_pgns, s->req_pgns_len, c->pgn);
}

static bool table_valid(const j1939_t *s, const j1939_txobj_cfg_t *cfg, uint16_t len) {
	bool valid = true;
	uint16_t i;
	uint16_t j;

	for (i = 0U; valid && (i < len); i++) {
		valid = entry_valid(s, &cfg[i]);
		for (j = 0U; valid && (j < i); j++) {
			valid = (cfg[j].ca != cfg[i].ca) || (cfg[j].pgn != cfg[i].pgn) ||
			        (cfg[j].da != cfg[i].da);
		}
	}
	return valid;
}

/* Sends the payload of an object to da. */
static j1939_ret_t obj_send(j1939_t *s, const j1939_txobj_cfg_t *c, uint8_t da) {
	const j1939_msg_t msg = {c->pgn, c->prio, 0U, da, c->len, c->buf};

	return j1939_send(s, c->ca, &msg);
}

/* Destination of an answer: a single frame of a PDU2 PGN always goes to the global address. */
static uint8_t answer_da(const j1939_txobj_cfg_t *c, uint8_t requester) {
	return ((c->len <= FRAME_LEN) && !j1939_pgn_is_pdu1(c->pgn)) ? J1939_ADDR_GLOBAL
	                                                             : requester;
}

/*
 * Handles the result of a send that is retried while the tx queue is full or
 * the transport protocol busy, at most until age_us reaches
 * J1939_TXOBJ_RESPONSE_US. Returns true when the send is over, sent or given up.
 */
static bool obj_send_done(j1939_t *s, j1939_ret_t ret, uint32_t age_us) {
	bool done = true;

	if (ret == J1939_RET_OK) {
		/* Sent. */
	} else if ((ret == J1939_RET_ERR_FULL) || (ret == J1939_RET_ERR_BUSY)) {
		if (age_us >= J1939_TXOBJ_RESPONSE_US) {
			s->stats.txobj_tx_dropped++;
		} else {
			s->stats.txobj_tx_retry++;
			done = false;
		}
	} else {
		s->stats.txobj_tx_dropped++;
	}
	return done;
}

/* The CA may not transmit: a pending answer is dropped, the schedule restarts with the claim. */
static void obj_stop(j1939_t *s, j1939_txobj_t *o) {
	if (flag_get(o, (uint8_t)FLAG_ANSWER)) {
		s->stats.txobj_tx_dropped++;
	}
	o->flags = 0U;
}

/* The claim completed: periodic and change objects are sent in this call. */
static void obj_start(const j1939_txobj_cfg_t *c, j1939_txobj_t *o) {
	o->period_us = 0U;
	o->since_us = UINT32_MAX;
	flag_put(o, (uint8_t)FLAG_STARTED, true);
	flag_put(o, (uint8_t)FLAG_DUE, c->period_us > 0U);
	flag_put(o, (uint8_t)FLAG_CHANGED, c->inhibit_us > 0U);
}

static void schedule_tick(j1939_t *s, const j1939_txobj_cfg_t *c, j1939_txobj_t *o,
                          uint32_t elapsed_us) {
	timer_add(&o->since_us, elapsed_us);
	if (c->period_us > 0U) {
		timer_add(&o->period_us, elapsed_us);
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

/* Sends a due periodic or change-triggered message to the object's destination. */
static void schedule_send(j1939_t *s, const j1939_txobj_cfg_t *c, j1939_txobj_t *o) {
	if (flag_get(o, (uint8_t)FLAG_DUE) ||
	    (flag_get(o, (uint8_t)FLAG_CHANGED) && (o->since_us >= c->inhibit_us))) {
		j1939_ret_t ret = obj_send(s, c, c->da);

		if (obj_send_done(s, ret, NO_EXPIRY)) {
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

/* Sends a pending answer to a Request, retried at most J1939_TXOBJ_RESPONSE_US. */
static void obj_answer_tick(j1939_t *s, const j1939_txobj_cfg_t *c, j1939_txobj_t *o,
                            uint32_t elapsed_us) {
	if (flag_get(o, (uint8_t)FLAG_ANSWER)) {
		if (flag_get(o, (uint8_t)FLAG_FRESH)) {
			flag_put(o, (uint8_t)FLAG_FRESH, false);
		} else {
			timer_add(&o->answer_us, elapsed_us);
		}
		if (obj_send_done(s, obj_send(s, c, answer_da(c, o->requester)), o->answer_us)) {
			flag_put(o, (uint8_t)FLAG_ANSWER, false);
		}
	}
}

/* Records a Request; requests of several nodes are answered once, globally. */
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

/* The Request addresses the CA: global, or to the address the CA holds. */
static bool obj_ca_addressed(const j1939_ca_t *ca, uint8_t da) {
	return (da == J1939_ADDR_GLOBAL) ||
	       ((ca->address == da) && ((ca->state == J1939_ADDR_STATE_CLAIMING) ||
	                                (ca->state == J1939_ADDR_STATE_CLAIMED)));
}

void j1939_txobj_stack_init(j1939_t *s) {
	s->txobj_cfg = NULL;
	s->txobj = NULL;
	s->txobj_len = 0U;
	s->stats.txobj_tx_retry = 0U;
	s->stats.txobj_tx_dropped = 0U;
}

bool j1939_txobj_request_handle(j1939_t *s, uint32_t id, uint32_t pgn) {
	bool answered[J1939_CFG_CA_MAX];
	bool handled = false;
	uint8_t da = j1939_id_da_get(id);
	uint8_t requester = (da == J1939_ADDR_GLOBAL) ? J1939_ADDR_GLOBAL : j1939_id_sa_get(id);
	uint16_t i;

	(void)memset(answered, 0, sizeof(answered));
	for (i = 0U; i < s->txobj_len; i++) {
		const j1939_txobj_cfg_t *c = &s->txobj_cfg[i];
		const j1939_ca_t *ca = &s->ca[c->ca];

		/* The first object of a CA with the PGN answers. */
		if ((c->pgn == pgn) && !answered[c->ca] && obj_ca_addressed(ca, da)) {
			answered[c->ca] = true;
			handled = true;
			/* Nothing is sent before the claim completes. */
			if (j1939_addr_tx_allowed(ca)) {
				obj_answer_request(&s->txobj[i], requester);
			}
		}
	}
	return handled;
}

void j1939_txobj_process(j1939_t *s, uint32_t elapsed_us) {
	uint16_t i;

	for (i = 0U; i < s->txobj_len; i++) {
		const j1939_txobj_cfg_t *c = &s->txobj_cfg[i];
		j1939_txobj_t *o = &s->txobj[i];

		if (!j1939_addr_tx_allowed(&s->ca[c->ca])) {
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

j1939_ret_t j1939_txobj_init(j1939_t *s, const j1939_txobj_cfg_t *cfg, j1939_txobj_t *obj,
                             uint16_t len) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((s != NULL) && (((cfg != NULL) && (obj != NULL)) || (len == 0U)) &&
	    table_valid(s, cfg, len)) {
		uint16_t i;

		for (i = 0U; i < len; i++) {
			/* A local keeps cppcheck's MISRA 11.8 check from treating the storage as
			 * const. */
			uint8_t *buf = cfg[i].buf;

			(void)memset(buf, (int)PAD, (size_t)cfg[i].len);
			(void)memset(&obj[i], 0, sizeof(obj[i]));
			obj[i].requester = J1939_ADDR_GLOBAL;
		}
		s->txobj_cfg = cfg;
		s->txobj = obj;
		s->txobj_len = len;
		ret = J1939_RET_OK;
	}
	return ret;
}

j1939_ret_t j1939_txobj_set(j1939_t *s, uint16_t index, const uint8_t *data, uint16_t len) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((s != NULL) && (data != NULL) && (index < s->txobj_len) &&
	    (len == s->txobj_cfg[index].len)) {
		const j1939_txobj_cfg_t *c = &s->txobj_cfg[index];
		uint8_t *buf = c->buf;

		if ((c->inhibit_us > 0U) && (memcmp(buf, data, (size_t)len) != 0)) {
			flag_put(&s->txobj[index], (uint8_t)FLAG_CHANGED, true);
		}
		(void)memcpy(buf, data, (size_t)len);
		ret = J1939_RET_OK;
	}
	return ret;
}
