/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

#include "j1939/j1939_dm.h"

#include <stddef.h>
#include <string.h>

#include "j1939/j1939_addr.h"
#include "j1939/j1939_id.h"
#include "j1939/j1939_request.h"
#include "j1939_addr_priv.h"
#include "j1939_dm_priv.h"

#define CLEAR_DM3      0U /* index of the DM3 request in j1939_dm_t::clear */
#define CLEAR_DM11     1U /* index of the DM11 request */
#define CLEAR_KINDS    2U
#define ANSWER_DM1     0U /* index of the DM1 answer in j1939_dm_t::answer */
#define ANSWER_DM2     1U /* index of the DM2 answer */
#define ANSWER_KINDS   2U
#define DM_PRIO        J1939_DIAG_PRIO_DEFAULT
#define ACK_PRIO       6U
#define ACK_PAD        0xFFU
#define BYTE_MASK      0xFFU
#define BYTE_SHIFT     8U
#define FRAME_LEN      8U
#define TP_BUF_SIZE    ((uint32_t)J1939_CFG_TP_BUF_SIZE)
#define NO_EXPIRY      0U /* age of a send that is retried until it succeeds */
#define CLEAR_PGN(k)   (((k) == CLEAR_DM3) ? J1939_PGN_DM3 : J1939_PGN_DM11)
#define DM_DTC_MAX     ((uint16_t)J1939_DIAG_DM_DTC_MAX)
#define HOLD_TIME_US   J1939_DM1_PERIOD_US
#define DM_BUF_NEED(n) ((uint32_t)J1939_DM_BUF_LEN((uint32_t)(n)))

/* Lookup of the diagnostic state of a CA: J1939_RET_ERR_STATE when not enabled. */
static j1939_ret_t dm_lookup(const j1939_t *s, j1939_ca_id_t ca, const j1939_dm_t **dm) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((s != NULL) && (ca < s->ca_count)) {
		*dm = s->dm[ca];
		ret = (*dm != NULL) ? J1939_RET_OK : J1939_RET_ERR_STATE;
	}
	return ret;
}

/* As dm_lookup(), for changing the stack's diagnostic state. */
static j1939_ret_t dm_lookup_mut(j1939_t *s, j1939_ca_id_t ca, j1939_dm_t **dm) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((s != NULL) && (ca < s->ca_count)) {
		struct j1939_dm **slot = &s->dm[ca];

		*dm = *slot;
		ret = (*dm != NULL) ? J1939_RET_OK : J1939_RET_ERR_STATE;
	}
	return ret;
}

/* Advances a timer, except in the call after it was started. */
static void timer_advance(uint32_t *timer_us, bool *fresh, uint32_t elapsed_us) {
	if (*fresh) {
		*fresh = false;
	} else if (elapsed_us > (UINT32_MAX - *timer_us)) {
		*timer_us = UINT32_MAX;
	} else {
		*timer_us += elapsed_us;
	}
}

/* DTCs are identified by SPN and FMI. */
static bool dtc_same(const j1939_diag_dtc_t *a, const j1939_diag_dtc_t *b) {
	return (a->spn == b->spn) && (a->fmi == b->fmi);
}

static bool dtc_listed(const j1939_diag_dtc_t *list, uint16_t count, const j1939_diag_dtc_t *dtc) {
	bool found = false;
	uint16_t i;

	for (i = 0U; (!found) && (i < count); i++) {
		found = dtc_same(&list[i], dtc);
	}
	return found;
}

/* A DTC that j1939_diag_dm_build() accepts. */
static bool dm_dtc_valid(const j1939_diag_dtc_t *dtc) {
	uint8_t tmp[J1939_DIAG_DTC_LEN];

	return (j1939_diag_dtc_encode(dtc, tmp) == J1939_RET_OK) &&
	       ((dtc->spn != 0U) || (dtc->fmi != 0U));
}

static j1939_ret_t dtc_list_check(const j1939_diag_dtc_t *dtcs, uint16_t count, uint16_t cap) {
	j1939_ret_t ret = J1939_RET_OK;
	uint16_t i;

	if ((dtcs == NULL) && (count > 0U)) {
		ret = J1939_RET_ERR_ARG;
	} else if (count > cap) {
		ret = J1939_RET_ERR_FULL;
	} else {
		for (i = 0U; (ret == J1939_RET_OK) && (i < count); i++) {
			if (!dm_dtc_valid(&dtcs[i]) || dtc_listed(dtcs, i, &dtcs[i])) {
				ret = J1939_RET_ERR_ARG;
			}
		}
	}
	return ret;
}

static void dm_hold_tick(j1939_dm_t *dm, uint32_t elapsed_us) {
	uint16_t i;

	for (i = 0U; i < dm->hold_len; i++) {
		j1939_dm_hold_t *h = &dm->hold[i];

		if (h->fresh) {
			h->fresh = false;
		} else if (h->left_us > elapsed_us) {
			h->left_us -= elapsed_us;
		} else {
			h->left_us = 0U;
		}
	}
}

/*
 * A DTC changed state. Returns true if the change triggers a DM1: the DTC
 * has no running hold and a free record takes it.
 */
static bool dtc_changed(j1939_dm_t *dm, const j1939_diag_dtc_t *dtc) {
	j1939_dm_hold_t *free_rec = NULL;
	bool held = false;
	uint16_t i;

	for (i = 0U; (!held) && (i < dm->hold_len); i++) {
		j1939_dm_hold_t *h = &dm->hold[i];

		if (h->left_us == 0U) {
			if (free_rec == NULL) {
				free_rec = h;
			}
		} else {
			held = (h->spn == dtc->spn) && (h->fmi == dtc->fmi);
		}
	}
	if (held) {
		free_rec = NULL;
	} else if (free_rec != NULL) {
		free_rec->spn = dtc->spn;
		free_rec->fmi = dtc->fmi;
		free_rec->left_us = HOLD_TIME_US;
		free_rec->fresh = true;
	} else {
		/* Every record in use: the change waits for the periodic DM1. */
	}
	return free_rec != NULL;
}

/* Replaces the active DTCs with a checked list, scheduling a DM1 on a reportable change. */
static void active_update(j1939_dm_t *dm, const j1939_diag_dtc_t *dtcs, uint16_t count) {
	bool trigger = false;
	uint16_t i;

	for (i = 0U; i < dm->active_count; i++) {
		if (!dtc_listed(dtcs, count, &dm->active[i]) && dtc_changed(dm, &dm->active[i])) {
			trigger = true;
		}
	}
	for (i = 0U; i < count; i++) {
		if (!dtc_listed(dm->active, dm->active_count, &dtcs[i]) &&
		    dtc_changed(dm, &dtcs[i])) {
			trigger = true;
		}
	}
	if (count > 0U) {
		(void)memcpy(dm->active, dtcs, (size_t)count * sizeof(dtcs[0]));
	}
	dm->active_count = count;
	if (trigger) {
		dm->dm1_due = true;
	}
}

/*
 * Handles the result of a DM send that is retried while the tx queue is full
 * or the broadcast busy, at most until age_us reaches J1939_DM_RESPONSE_US.
 * Returns true when the send is over, sent or given up.
 */
static bool send_done(j1939_t *s, j1939_ret_t ret, uint32_t age_us) {
	bool done = true;

	if (ret == J1939_RET_OK) {
		/* Sent. */
	} else if ((ret == J1939_RET_ERR_FULL) || (ret == J1939_RET_ERR_BUSY)) {
		if (age_us >= J1939_DM_RESPONSE_US) {
			s->stats.dm_tx_dropped++;
		} else {
			s->stats.dm_tx_retry++;
			done = false;
		}
	} else {
		s->stats.dm_tx_dropped++;
	}
	return done;
}

/* Sends a DM1 or DM2; a single frame always to the global address, a PDU2 PGN. */
static j1939_ret_t dm_send(j1939_t *s, j1939_ca_id_t ca, uint32_t pgn, const j1939_diag_dtc_t *dtcs,
                           uint16_t count, uint8_t da) {
	j1939_dm_t *dm = s->dm[ca];
	uint16_t len = 0U;
	j1939_ret_t ret = j1939_diag_dm_build(&dm->lamps, dtcs, count, dm->buf, dm->buf_len, &len);

	if (ret == J1939_RET_OK) {
		const j1939_msg_t msg = {
		        pgn, DM_PRIO, 0U, (len > FRAME_LEN) ? da : J1939_ADDR_GLOBAL, len, dm->buf};

		ret = j1939_send(s, ca, &msg);
	}
	return ret;
}

/* Sends an Acknowledgement to the global address, addressed to requester in its data. */
static j1939_ret_t ack_send(j1939_t *s, j1939_ca_id_t ca, uint8_t ctrl, uint8_t requester,
                            uint32_t pgn) {
	const uint8_t data[FRAME_LEN] = {ctrl,
	                                 ACK_PAD,
	                                 ACK_PAD,
	                                 ACK_PAD,
	                                 requester,
	                                 (uint8_t)(pgn & BYTE_MASK),
	                                 (uint8_t)((pgn >> BYTE_SHIFT) & BYTE_MASK),
	                                 (uint8_t)((pgn >> (2U * BYTE_SHIFT)) & BYTE_MASK)};
	const j1939_msg_t msg = {J1939_PGN_ACK,       ACK_PRIO, 0U, J1939_ADDR_GLOBAL,
	                         (uint16_t)FRAME_LEN, data};

	return j1939_send(s, ca, &msg);
}

/* Ends the application's decision on a clear request: acknowledge it if it was destination
 * specific. */
static void clear_decide(j1939_dm_clear_t *c, bool accept, bool fresh) {
	if (c->requester == J1939_ADDR_GLOBAL) {
		c->state = J1939_DM_CLEAR_IDLE;
	} else {
		c->state = accept ? J1939_DM_CLEAR_ACK : J1939_DM_CLEAR_NACK;
	}
	c->timer_us = 0U;
	c->fresh = fresh;
}

static void ack_try(j1939_t *s, j1939_ca_id_t ca, j1939_dm_clear_t *c, uint32_t pgn) {
	uint8_t ctrl = (uint8_t)((c->state == J1939_DM_CLEAR_ACK) ? J1939_ACK_CTRL_ACK
	                                                          : J1939_ACK_CTRL_NACK);

	if (send_done(s, ack_send(s, ca, ctrl, c->requester, pgn), c->timer_us)) {
		c->state = J1939_DM_CLEAR_IDLE;
	}
}

static void clear_tick(j1939_t *s, j1939_ca_id_t ca, j1939_dm_clear_t *c, uint32_t pgn,
                       uint32_t elapsed_us) {
	timer_advance(&c->timer_us, &c->fresh, elapsed_us);
	switch (c->state) {
	case J1939_DM_CLEAR_IDLE:
		break;
	case J1939_DM_CLEAR_REQUESTED:
		if (c->timer_us >= J1939_DM_RESPONSE_US) {
			/* No decision in time: nothing is cleared. */
			clear_decide(c, false, false);
			if (c->state == J1939_DM_CLEAR_NACK) {
				ack_try(s, ca, c, pgn);
			}
		}
		break;
	case J1939_DM_CLEAR_ACK:
	case J1939_DM_CLEAR_NACK:
		ack_try(s, ca, c, pgn);
		break;
	default:
		/* Corrupted state: clear nothing, send nothing. */
		c->state = J1939_DM_CLEAR_IDLE;
		break;
	}
}

static void clear_request(j1939_t *s, j1939_ca_id_t ca, j1939_dm_clear_t *c, uint32_t pgn,
                          uint8_t requester) {
	if (c->state == J1939_DM_CLEAR_IDLE) {
		c->state = J1939_DM_CLEAR_REQUESTED;
		c->requester = requester;
		c->timer_us = 0U;
		c->fresh = true;
	} else if ((requester == J1939_ADDR_GLOBAL) || (requester == c->requester)) {
		/* Covered by the request in progress. */
	} else if ((c->state == J1939_DM_CLEAR_REQUESTED) && (c->requester == J1939_ADDR_GLOBAL)) {
		/* The destination specific request is acknowledged instead. */
		c->requester = requester;
	} else {
		/* Busy with another requester's clear. */
		if (ack_send(s, ca, J1939_ACK_CTRL_CANNOT_RESPOND, requester, pgn) !=
		    J1939_RET_OK) {
			s->stats.dm_tx_dropped++;
		}
	}
}

/* The CA may not transmit: pending sends end and the first DM1 follows the next claim. */
static void dm_stop(j1939_t *s, j1939_dm_t *dm) {
	uint32_t k;

	dm->started = false;
	dm->dm1_due = false;
	for (k = 0U; k < ANSWER_KINDS; k++) {
		if (dm->answer[k].due) {
			dm->answer[k].due = false;
			s->stats.dm_tx_dropped++;
		}
	}
	for (k = 0U; k < CLEAR_KINDS; k++) {
		j1939_dm_clear_t *c = &dm->clear[k];

		if ((c->state == J1939_DM_CLEAR_ACK) || (c->state == J1939_DM_CLEAR_NACK)) {
			s->stats.dm_tx_dropped++;
		}
		c->state = J1939_DM_CLEAR_IDLE;
	}
}

static void dm1_tick(j1939_t *s, j1939_dm_t *dm, uint32_t elapsed_us) {
	if (!dm->started) {
		/* First DM1 as soon as the CA may transmit; the period counts from the next call.
		 */
		dm->started = true;
		dm->dm1_due = true;
		dm->dm1_timer_us = 0U;
	} else {
		bool fresh = false;

		timer_advance(&dm->dm1_timer_us, &fresh, elapsed_us);
		if (dm->dm1_timer_us >= J1939_DM1_PERIOD_US) {
			if (dm->dm1_due) {
				/* The previous DM1 never went out. */
				s->stats.dm_tx_dropped++;
			}
			dm->dm1_due = true;
			dm->dm1_timer_us %= J1939_DM1_PERIOD_US;
		}
	}
}

/* Sends a pending answer to a Request, retried at most J1939_DM_RESPONSE_US. */
static void answer_tick(j1939_t *s, j1939_ca_id_t ca, j1939_dm_answer_t *a, uint32_t pgn,
                        const j1939_diag_dtc_t *dtcs, uint16_t count, uint32_t elapsed_us) {
	if (a->due) {
		timer_advance(&a->timer_us, &a->fresh, elapsed_us);
		if (send_done(s, dm_send(s, ca, pgn, dtcs, count, a->da), a->timer_us)) {
			a->due = false;
		}
	}
}

/* Records a Request for an answer; requests of several nodes are answered once, globally. */
static void answer_request(j1939_dm_answer_t *a, uint8_t requester) {
	if (!a->due) {
		a->due = true;
		a->timer_us = 0U;
		a->fresh = true;
		a->da = requester;
	} else if (a->da != requester) {
		a->da = J1939_ADDR_GLOBAL;
	} else {
		/* Same requester again. */
	}
}

static void ca_process(j1939_t *s, j1939_ca_id_t ca, j1939_dm_t *dm, uint32_t elapsed_us) {
	dm_hold_tick(dm, elapsed_us);
	if (!j1939_addr_tx_allowed(&s->ca[ca])) {
		dm_stop(s, dm);
	} else {
		uint32_t k;

		dm1_tick(s, dm, elapsed_us);
		for (k = 0U; k < CLEAR_KINDS; k++) {
			clear_tick(s, ca, &dm->clear[k], CLEAR_PGN(k), elapsed_us);
		}
		if (dm->dm1_due && send_done(s,
		                             dm_send(s, ca, J1939_PGN_DM1, dm->active,
		                                     dm->active_count, J1939_ADDR_GLOBAL),
		                             NO_EXPIRY)) {
			dm->dm1_due = false;
		}
		answer_tick(s, ca, &dm->answer[ANSWER_DM1], J1939_PGN_DM1, dm->active,
		            dm->active_count, elapsed_us);
		answer_tick(s, ca, &dm->answer[ANSWER_DM2], J1939_PGN_DM2, dm->prev, dm->prev_count,
		            elapsed_us);
	}
}

/* The Request addresses the CA: global, or to the address the CA holds. */
static bool ca_addressed(const j1939_ca_t *ca, uint8_t da) {
	return (da == J1939_ADDR_GLOBAL) ||
	       ((ca->address == da) && ((ca->state == J1939_ADDR_STATE_CLAIMING) ||
	                                (ca->state == J1939_ADDR_STATE_CLAIMED)));
}

static bool clear_index(uint32_t pgn, uint32_t *k) {
	bool found = true;

	if (pgn == J1939_PGN_DM3) {
		*k = CLEAR_DM3;
	} else if (pgn == J1939_PGN_DM11) {
		*k = CLEAR_DM11;
	} else {
		found = false;
	}
	return found;
}

static bool dm_supported(const j1939_dm_t *dm, uint32_t pgn) {
	uint32_t k = 0U;
	bool supported = (pgn == J1939_PGN_DM1) || (pgn == J1939_PGN_DM2);

	if (clear_index(pgn, &k)) {
		supported = dm->clear[k].enabled;
	}
	return supported;
}

static void request_accept(j1939_t *s, j1939_ca_id_t ca, j1939_dm_t *dm, uint32_t pgn,
                           uint8_t requester) {
	uint32_t k = 0U;

	if (pgn == J1939_PGN_DM1) {
		if ((requester == J1939_ADDR_GLOBAL) || (dm->active_count <= 1U)) {
			/* A broadcast DM1 answers it. */
			dm->dm1_due = true;
		} else {
			answer_request(&dm->answer[ANSWER_DM1], requester);
		}
	} else if (pgn == J1939_PGN_DM2) {
		answer_request(&dm->answer[ANSWER_DM2], requester);
	} else if (clear_index(pgn, &k)) {
		clear_request(s, ca, &dm->clear[k], pgn, requester);
	} else {
		/* Not a diagnostic message; excluded by the caller. */
	}
}

static bool dm_cfg_valid(const j1939_t *s, const j1939_dm_cfg_t *cfg) {
	uint16_t max = (cfg->active_len > cfg->prev_len) ? cfg->active_len : cfg->prev_len;
	uint32_t need = DM_BUF_NEED(max);

	return (cfg->active_len <= DM_DTC_MAX) && (cfg->prev_len <= DM_DTC_MAX) &&
	       ((cfg->active != NULL) || (cfg->active_len == 0U)) &&
	       ((cfg->prev != NULL) || (cfg->prev_len == 0U)) &&
	       ((cfg->hold != NULL) || (cfg->hold_len == 0U)) && (cfg->buf != NULL) &&
	       ((uint32_t)cfg->buf_len >= need) && (need <= TP_BUF_SIZE) &&
	       ((need <= FRAME_LEN) || (s->tp.tx_buf_len > 0U));
}

/* Returns true if dm is registered for a CA other than ca. */
static bool dm_used(const j1939_t *s, j1939_ca_id_t ca, const j1939_dm_t *dm) {
	bool used = false;
	uint8_t i;

	for (i = 0U; (!used) && (i < s->ca_count); i++) {
		used = (i != ca) && (s->dm[i] == dm);
	}
	return used;
}

void j1939_dm_stack_init(j1939_t *s) {
	uint32_t i;

	for (i = 0U; i < (uint32_t)J1939_CFG_CA_MAX; i++) {
		s->dm[i] = NULL;
	}
	s->stats.dm_tx_retry = 0U;
	s->stats.dm_tx_dropped = 0U;
}

bool j1939_dm_request_handle(j1939_t *s, uint32_t id, uint32_t pgn) {
	bool handled = false;
	uint8_t da = j1939_id_da_get(id);
	uint8_t requester = (da == J1939_ADDR_GLOBAL) ? J1939_ADDR_GLOBAL : j1939_id_sa_get(id);
	uint8_t i;

	for (i = 0U; i < s->ca_count; i++) {
		j1939_dm_t *dm = s->dm[i];

		if ((dm != NULL) && ca_addressed(&s->ca[i], da) && dm_supported(dm, pgn)) {
			handled = true;
			/* Nothing is sent before the claim completes. */
			if (j1939_addr_tx_allowed(&s->ca[i])) {
				request_accept(s, i, dm, pgn, requester);
			}
		}
	}
	return handled;
}

void j1939_dm_process(j1939_t *s, uint32_t elapsed_us) {
	uint8_t i;

	for (i = 0U; i < s->ca_count; i++) {
		j1939_dm_t *dm = s->dm[i];

		if (dm != NULL) {
			ca_process(s, i, dm, elapsed_us);
		}
	}
}

j1939_ret_t j1939_dm_init(j1939_t *s, j1939_ca_id_t ca, j1939_dm_t *dm, const j1939_dm_cfg_t *cfg) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((s != NULL) && (dm != NULL) && (cfg != NULL) && (ca < s->ca_count) &&
	    dm_cfg_valid(s, cfg) && !dm_used(s, ca, dm)) {
		/* Locals keep cppcheck's MISRA 11.8 check from treating the storage as const. */
		j1939_diag_dtc_t *active = cfg->active;
		j1939_diag_dtc_t *prev = cfg->prev;
		j1939_dm_hold_t *hold = cfg->hold;
		uint8_t *buf = cfg->buf;
		uint16_t i;
		uint32_t k;

		(void)memset(dm, 0, sizeof(*dm));
		dm->active = active;
		dm->active_len = cfg->active_len;
		dm->prev = prev;
		dm->prev_len = cfg->prev_len;
		dm->hold = hold;
		dm->hold_len = cfg->hold_len;
		dm->buf = buf;
		dm->buf_len = cfg->buf_len;
		dm->lamps.mil_flash = J1939_DIAG_FLASH_OFF;
		dm->lamps.red_stop_flash = J1939_DIAG_FLASH_OFF;
		dm->lamps.amber_warning_flash = J1939_DIAG_FLASH_OFF;
		dm->lamps.protect_flash = J1939_DIAG_FLASH_OFF;
		for (i = 0U; i < cfg->hold_len; i++) {
			hold[i].left_us = 0U;
			hold[i].fresh = false;
		}
		for (k = 0U; k < CLEAR_KINDS; k++) {
			dm->clear[k].state = J1939_DM_CLEAR_IDLE;
			dm->clear[k].requester = J1939_ADDR_GLOBAL;
		}
		dm->clear[CLEAR_DM3].enabled = cfg->dm3_enable;
		dm->clear[CLEAR_DM11].enabled = cfg->dm11_enable;
		s->dm[ca] = dm;
		ret = J1939_RET_OK;
	}
	return ret;
}

j1939_ret_t j1939_dm_lamps_set(j1939_t *s, j1939_ca_id_t ca, const j1939_diag_lamps_t *lamps) {
	j1939_dm_t *dm = NULL;
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if (lamps != NULL) {
		ret = dm_lookup_mut(s, ca, &dm);
	}
	if (ret == J1939_RET_OK) {
		uint8_t tmp[J1939_DIAG_LAMPS_LEN];

		ret = j1939_diag_lamps_encode(lamps, tmp);
		if (ret == J1939_RET_OK) {
			dm->lamps = *lamps;
		}
	}
	return ret;
}

j1939_ret_t j1939_dm_active_set(j1939_t *s, j1939_ca_id_t ca, const j1939_diag_dtc_t *dtcs,
                                uint16_t count) {
	j1939_dm_t *dm = NULL;
	j1939_ret_t ret = dm_lookup_mut(s, ca, &dm);

	if (ret == J1939_RET_OK) {
		ret = dtc_list_check(dtcs, count, dm->active_len);
	}
	if (ret == J1939_RET_OK) {
		active_update(dm, dtcs, count);
	}
	return ret;
}

j1939_ret_t j1939_dm_prev_set(j1939_t *s, j1939_ca_id_t ca, const j1939_diag_dtc_t *dtcs,
                              uint16_t count) {
	j1939_dm_t *dm = NULL;
	j1939_ret_t ret = dm_lookup_mut(s, ca, &dm);

	if (ret == J1939_RET_OK) {
		ret = dtc_list_check(dtcs, count, dm->prev_len);
	}
	if (ret == J1939_RET_OK) {
		if (count > 0U) {
			(void)memcpy(dm->prev, dtcs, (size_t)count * sizeof(dtcs[0]));
		}
		dm->prev_count = count;
	}
	return ret;
}

j1939_ret_t j1939_dm_clear_get(const j1939_t *s, j1939_ca_id_t ca, uint32_t *pgn) {
	const j1939_dm_t *dm = NULL;
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if (pgn != NULL) {
		ret = dm_lookup(s, ca, &dm);
	}
	if (ret == J1939_RET_OK) {
		if (dm->clear[CLEAR_DM3].state == J1939_DM_CLEAR_REQUESTED) {
			*pgn = J1939_PGN_DM3;
		} else if (dm->clear[CLEAR_DM11].state == J1939_DM_CLEAR_REQUESTED) {
			*pgn = J1939_PGN_DM11;
		} else {
			ret = J1939_RET_ERR_EMPTY;
		}
	}
	return ret;
}

j1939_ret_t j1939_dm_clear_confirm(j1939_t *s, j1939_ca_id_t ca, uint32_t pgn, bool accept) {
	j1939_dm_t *dm = NULL;
	uint32_t k = 0U;
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if (clear_index(pgn, &k)) {
		ret = dm_lookup_mut(s, ca, &dm);
	}
	if (ret == J1939_RET_OK) {
		j1939_dm_clear_t *c = &dm->clear[k];

		if (c->state != J1939_DM_CLEAR_REQUESTED) {
			ret = J1939_RET_ERR_STATE;
		} else {
			if (accept) {
				if (k == CLEAR_DM3) {
					dm->prev_count = 0U;
				} else {
					active_update(dm, NULL, 0U);
				}
			}
			clear_decide(c, accept, true);
		}
	}
	return ret;
}
