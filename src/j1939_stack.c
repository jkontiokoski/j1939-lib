/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_stack.c
 * @brief Stack instance: configuration, frame reception and filtering, tx queue
 *        and message slots, and the helpers the stack modules share.
 */

#include "j1939/j1939_stack.h"

#include <stddef.h>
#include <string.h>

#include "j1939/j1939_addr.h"
#include "j1939/j1939_id.h"
#include "j1939/j1939_request.h"
#include "j1939_internal.h"
#include "j1939_ring_priv.h"

#define BYTE_MASK  0xFFU /**< Mask of one byte. */
#define BYTE_SHIFT 8U    /**< Bits per byte. */

/**
 * Checks a PGN list of the configuration: every entry is a valid PGN. The list may be NULL if len
 * is 0.
 */
static bool pgn_list_valid(const uint32_t *list, uint16_t len) {
	bool valid = (list != NULL) || (len == 0U);
	uint16_t i;

	for (i = 0U; valid && (i < len); i++) {
		valid = j1939_pgn_valid(list[i]);
	}
	return valid;
}

/** A list of the configuration is present where its length says it has entries. */
#define LIST_OK(ptr, len) (((ptr) != NULL) || ((len) == 0U))

/**
 * Checks what the modules rely on before they look at their part of a configuration: the queues
 * hold an entry each, every list is present, the PGN lists are valid and the CAs fit.
 */
static bool cfg_valid(const j1939_cfg_t *cfg) {
	bool valid = (cfg->tx_buf != NULL) && (cfg->tx_len > 0U) && (cfg->msg_buf != NULL) &&
	             (cfg->msg_len > 0U) && pgn_list_valid(cfg->rx_pgns, cfg->rx_pgns_len) &&
	             pgn_list_valid(cfg->req_pgns, cfg->req_pgns_len) &&
	             LIST_OK(cfg->ca, cfg->ca_len) && (cfg->ca_len <= (uint8_t)J1939_CFG_CA_MAX) &&
	             LIST_OK(cfg->nametab, cfg->nametab_len) &&
	             LIST_OK(cfg->rxobj_cfg, cfg->rxobj_len) &&
	             LIST_OK(cfg->rxobj, cfg->rxobj_len) &&
	             LIST_OK(cfg->txobj_cfg, cfg->txobj_len) && LIST_OK(cfg->txobj, cfg->txobj_len);

#if J1939_TP_ENABLE
	valid = valid && LIST_OK(cfg->tp_tx_buf, cfg->tp_tx_buf_len) &&
	        LIST_OK(cfg->tp_rx_buf, cfg->tp_rx_buf_len);
#endif
	return valid;
}

/** Puts a stack into its starting state for a configuration: everything empty and zero. */
static void stack_reset(j1939_t *s, const j1939_cfg_t *cfg) {
	(void)memset(s, 0, sizeof(*s));
	s->cfg = cfg;
	j1939_ring_init(&s->tx, cfg->tx_len);
	j1939_ring_init(&s->msgs, cfg->msg_len);
}

/**
 * Passes a received message to the module that handles it.
 *
 * Messages with the extended data page bit are not J1939. Address Claimed goes to address
 * claiming whatever its destination; other messages pass only when global or addressed to a held
 * address: Requests to the Request module, TP.CM and TP.DT to the transport protocol, the rest to
 * the receive objects and, if listed in rx_pgns, to the message slots.
 */
static void rx_dispatch(j1939_t *s, const j1939_msg_t *msg) {
	if ((msg->pgn & J1939_PGN_EDP) != 0U) {
		/* Not a J1939 message. */
	} else if (msg->pgn == J1939_PGN_ADDRESS_CLAIMED) {
		j1939_addr_claim_handle(s, msg);
	} else if ((msg->da != J1939_ADDR_GLOBAL) && !j1939_addr_held(s, msg->da)) {
		/* Addressed to another node. */
	} else if (msg->pgn == J1939_PGN_REQUEST) {
		j1939_request_handle(s, msg);
	} else if ((msg->pgn == J1939_PGN_TP_CM) || (msg->pgn == J1939_PGN_TP_DT)) {
#if J1939_TP_ENABLE
		j1939_tp_handle(s, msg);
#else
		/* Built without the transport protocol: dropped. */
#endif
	} else {
		j1939_rxobj_handle(s, msg);
		if (j1939_stack_pgn_listed(s->cfg->rx_pgns, s->cfg->rx_pgns_len, msg->pgn)) {
			(void)j1939_stack_deliver(s, msg);
		}
	}
}

j1939_ret_t j1939_stack_tx(j1939_t *s, uint8_t prio, uint32_t pgn, uint8_t da, uint8_t sa,
                           const uint8_t *data, uint8_t len) {
	uint32_t id = 0U;
	j1939_ret_t ret = j1939_id_build(prio, pgn, da, sa, &id);
	uint16_t index;

	if (ret != J1939_RET_OK) {
		/* Invalid identifier. */
	} else if (j1939_ring_head(&s->tx, &index)) {
		j1939_port_frame_t *frame = &s->cfg->tx_buf[index];

		j1939_port_frame_build(frame, id, data, len);
		(void)j1939_ring_push(&s->tx);
	} else {
		ret = J1939_RET_ERR_FULL;
	}
	return ret;
}

bool j1939_stack_deliver(j1939_t *s, const j1939_msg_t *msg) {
	uint16_t index;
	bool delivered = j1939_ring_head(&s->msgs, &index);

	if (delivered) {
		j1939_msg_slot_t *slot = &s->cfg->msg_buf[index];

		slot->msg = *msg;
		if (msg->len <= J1939_MSG_SINGLE_FRAME_MAX) {
			/* A frame is gone when j1939_rx() returns: the slot keeps its payload. */
			if (msg->len > 0U) {
				(void)memcpy(slot->data, msg->data, msg->len);
			}
			slot->msg.data = slot->data;
		}
		(void)j1939_ring_push(&s->msgs);
	} else {
		s->stats.rx_msg_overflow++;
	}
	return delivered;
}

bool j1939_stack_pgn_listed(const uint32_t *list, uint16_t len, uint32_t pgn) {
	bool found = false;
	uint16_t i;

	for (i = 0U; (!found) && (i < len); i++) {
		found = list[i] == pgn;
	}
	return found;
}

bool j1939_stack_pgn_owned(uint32_t pgn) {
	return (pgn == J1939_PGN_REQUEST) || (pgn == J1939_PGN_ADDRESS_CLAIMED) ||
	       (pgn == J1939_PGN_TP_CM) || (pgn == J1939_PGN_TP_DT);
}

void j1939_stack_pgn_put(uint32_t pgn, uint8_t *data) {
	data[0] = (uint8_t)(pgn & BYTE_MASK);
	data[1] = (uint8_t)((pgn >> BYTE_SHIFT) & BYTE_MASK);
	data[2] = (uint8_t)((pgn >> (2U * BYTE_SHIFT)) & BYTE_MASK);
}

uint32_t j1939_stack_pgn_get(const uint8_t *data) {
	return (uint32_t)data[0] | ((uint32_t)data[1] << BYTE_SHIFT) |
	       ((uint32_t)data[2] << (2U * BYTE_SHIFT));
}

void j1939_stack_timer_add(uint32_t *timer_us, uint32_t elapsed_us) {
	if (elapsed_us > (UINT32_MAX - *timer_us)) {
		*timer_us = UINT32_MAX;
	} else {
		*timer_us += elapsed_us;
	}
}

void j1939_stack_timer_advance(uint32_t *timer_us, bool *fresh, uint32_t elapsed_us) {
	if (*fresh) {
		*fresh = false;
	} else {
		j1939_stack_timer_add(timer_us, elapsed_us);
	}
}

bool j1939_stack_retry_done(j1939_ret_t ret, uint32_t age_us, uint32_t *retries,
                            uint32_t *dropped) {
	bool done = true;

	if (ret == J1939_RET_OK) {
		/* Sent. */
	} else if (((ret == J1939_RET_ERR_FULL) || (ret == J1939_RET_ERR_BUSY)) &&
	           (age_us < J1939_TR_US)) {
		(*retries)++;
		done = false;
	} else {
		/* Too late to be of use, or an error that a retry does not cure. */
		(*dropped)++;
	}
	return done;
}

j1939_ret_t j1939_init(j1939_t *s, const j1939_cfg_t *cfg) {
	/* A stack without CAs, queues and tables: what an invalid configuration leaves. */
	static const j1939_cfg_t cfg_none = {.tx_buf = NULL};
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if (s != NULL) {
		stack_reset(s, &cfg_none);
		if ((cfg != NULL) && cfg_valid(cfg)) {
			bool valid;

			stack_reset(s, cfg);
			/* Every module looks at its part; one invalid part makes the whole invalid.
			 */
			valid = j1939_addr_init(s);
			j1939_nametab_init(s);
			j1939_tp_init(s);
			valid = j1939_rxobj_init(s) && valid;
			valid = j1939_txobj_init(s) && valid;
			valid = j1939_dm_init(s) && valid;
			if (valid) {
				ret = J1939_RET_OK;
			} else {
				stack_reset(s, &cfg_none);
			}
		}
	}
	return ret;
}

j1939_ret_t j1939_rx(j1939_t *s, const j1939_port_frame_t *frame) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((s != NULL) && (frame != NULL)) {
		/* Standard and remote frames are not J1939. */
		if (j1939_port_frame_is_ext(frame) && !j1939_port_frame_is_rtr(frame)) {
			/* The frame is decoded once; the handlers work on the message. */
			uint32_t id = j1939_port_frame_id_get(frame);
			const j1939_msg_t msg = {
			        .pgn = j1939_id_pgn_get(id),
			        .prio = j1939_id_prio_get(id),
			        .sa = j1939_id_sa_get(id),
			        .da = j1939_id_da_get(id),
			        .len = j1939_port_frame_len_get(frame),
			        .data = j1939_port_frame_data(frame),
			};

			rx_dispatch(s, &msg);
		}
		ret = J1939_RET_OK;
	}
	return ret;
}

const j1939_port_frame_t *j1939_tx_peek(j1939_t *s) {
	const j1939_port_frame_t *frame = NULL;
	uint16_t index;

	if ((s != NULL) && j1939_ring_tail(&s->tx, &index)) {
		frame = &s->cfg->tx_buf[index];
	}
	return frame;
}

j1939_ret_t j1939_tx_pop(j1939_t *s) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if (s != NULL) {
		ret = j1939_ring_pop(&s->tx) ? J1939_RET_OK : J1939_RET_ERR_EMPTY;
	}
	return ret;
}

j1939_ret_t j1939_process(j1939_t *s, uint32_t elapsed_us) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if (s != NULL) {
		j1939_addr_process(s, elapsed_us);
		j1939_nametab_process(s, elapsed_us);
		j1939_tp_process(s, elapsed_us);
		j1939_dm_process(s, elapsed_us);
		j1939_rxobj_process(s, elapsed_us);
		j1939_txobj_process(s, elapsed_us);
		ret = J1939_RET_OK;
	}
	return ret;
}

const j1939_msg_t *j1939_msg_peek(j1939_t *s) {
	const j1939_msg_t *msg = NULL;
	uint16_t index;

	if ((s != NULL) && j1939_ring_tail(&s->msgs, &index)) {
		msg = &s->cfg->msg_buf[index].msg;
	}
	return msg;
}

j1939_ret_t j1939_msg_pop(j1939_t *s) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;
	uint16_t index;

	if (s == NULL) {
		/* Invalid argument. */
	} else if (j1939_ring_tail(&s->msgs, &index)) {
		const j1939_msg_t *msg = &s->cfg->msg_buf[index].msg;

		if (msg->len > J1939_MSG_SINGLE_FRAME_MAX) {
			/* A multi-packet message: its reassembly buffer is free again. */
			j1939_tp_msg_release(s, msg->data);
		}
		(void)j1939_ring_pop(&s->msgs);
		ret = J1939_RET_OK;
	} else {
		ret = J1939_RET_ERR_EMPTY;
	}
	return ret;
}

j1939_ret_t j1939_send(j1939_t *s, j1939_ca_id_t ca, const j1939_msg_t *msg) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((s != NULL) && (msg != NULL) && (ca < s->cfg->ca_len) &&
	    ((msg->data != NULL) || (msg->len == 0U))) {
		const j1939_ca_t *c = &s->ca[ca];

		if (!j1939_addr_ca_claimed(c)) {
			ret = J1939_RET_ERR_NO_ADDRESS;
		} else if (msg->len <= J1939_MSG_SINGLE_FRAME_MAX) {
			ret = j1939_stack_tx(s, msg->prio, msg->pgn, msg->da, c->address, msg->data,
			                     (uint8_t)msg->len);
		} else {
#if J1939_TP_ENABLE
			ret = j1939_tp_send(s, c->address, msg);
#else
			/* Built without the transport protocol: one frame at most (ERR_ARG). */
#endif
		}
	}
	return ret;
}

const j1939_stats_t *j1939_stats_get(const j1939_t *s) {
	return (s != NULL) ? &s->stats : NULL;
}
