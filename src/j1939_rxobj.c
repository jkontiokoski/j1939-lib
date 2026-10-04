/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_rxobj.c
 * @brief Receive objects: latest payload of a PGN from one sender, with timeout
 *        supervision.
 */

#include "j1939/j1939_rxobj.h"

#include <stddef.h>
#include <string.h>

#include "j1939/j1939_addr.h"
#include "j1939/j1939_request.h"
#include "j1939/j1939_tp.h"
#include "j1939_rxobj_priv.h"
#include "j1939_stack_priv.h"

#define SA_MAX      0xFDU /**< Highest source address a node can claim. */
#define BUF_LEN_MAX ((uint16_t)J1939_CFG_TP_BUF_SIZE) /**< Largest payload an object can hold. */

/**
 * @brief Tells whether the stack handles a PGN itself, so that it never reaches a receive object.
 * @param pgn  PGN.
 * @return true for Request, Address Claimed, TP.CM and TP.DT.
 */
static bool pgn_owned(uint32_t pgn) {
	return (pgn == J1939_PGN_REQUEST) || (pgn == J1939_PGN_ADDRESS_CLAIMED) ||
	       (pgn == J1939_PGN_TP_CM) || (pgn == J1939_PGN_TP_DT);
}

/**
 * @brief Checks one entry of a receive object table.
 * @param c  Entry.
 * @return true if it has a buffer, a valid PGN the stack does not own, a
 *         source address of at most 253 and 1 <= min_len <= buf_len <=
 *         J1939_CFG_TP_BUF_SIZE.
 */
static bool rx_entry_valid(const j1939_rxobj_cfg_t *c) {
	return (c->buf != NULL) && j1939_stack_pgn_valid(c->pgn) && !pgn_owned(c->pgn) &&
	       (c->sa <= SA_MAX) && (c->min_len >= 1U) && (c->min_len <= c->buf_len) &&
	       (c->buf_len <= BUF_LEN_MAX);
}

/**
 * @brief Checks a whole receive object table.
 * @param cfg  Configuration entries; may be NULL if @p len is 0.
 * @param obj  State entries; may be NULL if @p len is 0.
 * @param len  Number of entries.
 * @return true if every entry is valid and no two have the same PGN and source address.
 */
static bool rx_table_valid(const j1939_rxobj_cfg_t *cfg, const j1939_rxobj_t *obj, uint16_t len) {
	bool valid = ((cfg != NULL) && (obj != NULL)) || (len == 0U);
	uint16_t i;
	uint16_t k;

	for (i = 0U; valid && (i < len); i++) {
		valid = rx_entry_valid(&cfg[i]);
		for (k = 0U; valid && (k < i); k++) {
			valid = (cfg[k].pgn != cfg[i].pgn) || (cfg[k].sa != cfg[i].sa);
		}
	}
	return valid;
}

/**
 * @brief Finds the receive object for a PGN and sender.
 * @param s    Stack.
 * @param pgn  PGN.
 * @param sa   Source address.
 * @return Its index, or s->rxobj_len if there is none.
 */
static uint16_t obj_find(const j1939_t *s, uint32_t pgn, uint8_t sa) {
	uint16_t found = s->rxobj_len;
	uint16_t i;

	for (i = 0U; (found == s->rxobj_len) && (i < s->rxobj_len); i++) {
		if ((s->rxobj_cfg[i].pgn == pgn) && (s->rxobj_cfg[i].sa == sa)) {
			found = i;
		}
	}
	return found;
}

void j1939_rxobj_stack_init(j1939_t *s) {
	s->rxobj_cfg = NULL;
	s->rxobj = NULL;
	s->rxobj_len = 0U;
	s->stats.rxobj_rejected = 0U;
	s->stats.rxobj_timeout = 0U;
}

j1939_ret_t j1939_rxobj_init(j1939_t *s, const j1939_rxobj_cfg_t *cfg, j1939_rxobj_t *obj,
                             uint16_t len) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((s != NULL) && rx_table_valid(cfg, obj, len)) {
		uint16_t i;

		for (i = 0U; i < len; i++) {
			obj[i].age_us = 0U;
			obj[i].len = 0U;
			obj[i].received = false;
			obj[i].timed_out = false;
			obj[i].updated = false;
			obj[i].fresh = false;
		}
		s->rxobj_cfg = (len > 0U) ? cfg : NULL;
		s->rxobj = (len > 0U) ? obj : NULL;
		s->rxobj_len = len;
		ret = J1939_RET_OK;
	}
	return ret;
}

j1939_ret_t j1939_rxobj_get(j1939_t *s, uint16_t index, j1939_rxobj_status_t *status) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((s != NULL) && (status != NULL) && (index < s->rxobj_len)) {
		j1939_rxobj_t *o = &s->rxobj[index];

		if (!o->received) {
			status->state = J1939_RXOBJ_NO_DATA;
		} else if (o->timed_out) {
			status->state = J1939_RXOBJ_TIMEOUT;
		} else {
			status->state = J1939_RXOBJ_VALID;
		}
		status->age_us = o->age_us;
		status->len = o->len;
		status->updated = o->updated;
		o->updated = false;
		ret = J1939_RET_OK;
	}
	return ret;
}

bool j1939_rxobj_wanted(const j1939_t *s, uint32_t pgn, uint8_t sa) {
	return obj_find(s, pgn, sa) < s->rxobj_len;
}

void j1939_rxobj_handle(j1939_t *s, uint32_t pgn, uint8_t sa, const uint8_t *data, uint16_t len) {
	uint16_t i = obj_find(s, pgn, sa);

	if (i < s->rxobj_len) {
		const j1939_rxobj_cfg_t *c = &s->rxobj_cfg[i];
		j1939_rxobj_t *o = &s->rxobj[i];

		if ((len < c->min_len) || (len > c->buf_len)) {
			s->stats.rxobj_rejected++;
		} else {
			(void)memcpy(c->buf, data, len);
			o->len = len;
			o->age_us = 0U;
			o->received = true;
			o->timed_out = false;
			o->updated = true;
			o->fresh = true;
		}
	}
}

void j1939_rxobj_process(j1939_t *s, uint32_t elapsed_us) {
	uint16_t i;

	for (i = 0U; i < s->rxobj_len; i++) {
		const j1939_rxobj_cfg_t *c = &s->rxobj_cfg[i];
		j1939_rxobj_t *o = &s->rxobj[i];

		if (!o->received) {
			/* Nothing to supervise yet. */
		} else if (o->fresh) {
			/* A reception counts from the next call. */
			o->fresh = false;
		} else {
			if (elapsed_us > (UINT32_MAX - o->age_us)) {
				o->age_us = UINT32_MAX;
			} else {
				o->age_us += elapsed_us;
			}
			if ((c->timeout_us > 0U) && !o->timed_out && (o->age_us >= c->timeout_us)) {
				o->timed_out = true;
				s->stats.rxobj_timeout++;
			}
		}
	}
}
