/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

#include "j1939/j1939_stack.h"

#include <stddef.h>
#include <string.h>

#include "j1939/j1939_addr.h"
#include "j1939/j1939_id.h"
#include "j1939/j1939_request.h"
#include "j1939_addr_priv.h"
#include "j1939_dm_priv.h"
#include "j1939_ring_priv.h"
#include "j1939_stack_priv.h"
#include "j1939_tp_priv.h"

#define PDU1_DA_MASK   0xFFU
#define CA_ADDRESS_MAX 0xFDU

static bool pgn_valid(uint32_t pgn) {
	return (pgn <= J1939_PGN_MAX) && (!j1939_pgn_is_pdu1(pgn) || ((pgn & PDU1_DA_MASK) == 0U));
}

static bool pgn_list_valid(const uint32_t *list, uint16_t len) {
	bool valid = (list != NULL) || (len == 0U);
	uint16_t i;

	for (i = 0U; valid && (i < len); i++) {
		valid = pgn_valid(list[i]);
	}
	return valid;
}

static bool ca_find(const j1939_t *s, uint8_t address) {
	bool found = false;
	uint8_t i;

	for (i = 0U; (!found) && (i < s->ca_count); i++) {
		found = s->ca[i].address == address;
	}
	return found;
}

static void frame_handle(j1939_t *s, const j1939_port_frame_t *f) {
	if (j1939_port_frame_is_ext(f) && !j1939_port_frame_is_rtr(f)) {
		uint32_t id = j1939_port_frame_id_get(f);
		uint32_t pgn = j1939_id_pgn_get(id);
		uint8_t da = j1939_id_da_get(id);
		const uint8_t *data = j1939_port_frame_data(f);
		uint8_t len = j1939_port_frame_len_get(f);

		if ((pgn & J1939_PGN_EDP) != 0U) {
			/* Not a J1939 message. */
		} else if (pgn == J1939_PGN_ADDRESS_CLAIMED) {
			j1939_addr_claim_handle(s, id, data, len);
		} else if ((da == J1939_ADDR_GLOBAL) || j1939_addr_held(s, da)) {
			if (pgn == J1939_PGN_REQUEST) {
				j1939_request_handle(s, id, data, len);
			} else if ((pgn == J1939_PGN_TP_CM) || (pgn == J1939_PGN_TP_DT)) {
				j1939_tp_handle(s, id, data, len);
			} else if (j1939_stack_pgn_listed(s->rx_pgns, s->rx_pgns_len, pgn)) {
				j1939_stack_deliver(s, id, data, len);
			} else {
				/* Not of interest to this node. */
			}
		} else {
			/* Addressed to another node. */
		}
	}
}

bool j1939_stack_pgn_listed(const uint32_t *list, uint16_t len, uint32_t pgn) {
	bool found = false;
	uint16_t i;

	for (i = 0U; (!found) && (i < len); i++) {
		found = list[i] == pgn;
	}
	return found;
}

void j1939_stack_deliver(j1939_t *s, uint32_t id, const uint8_t *data, uint8_t len) {
	uint16_t index;

	if (j1939_ring_head(&s->msgs.ring, &index)) {
		j1939_msg_slot_t *slot = &s->msgs.buf[index];

		slot->msg.pgn = j1939_id_pgn_get(id);
		slot->msg.prio = j1939_id_prio_get(id);
		slot->msg.sa = j1939_id_sa_get(id);
		slot->msg.da = j1939_id_da_get(id);
		slot->msg.len = len;
		if (len > 0U) {
			(void)memcpy(slot->data, data, len);
		}
		slot->msg.data = slot->data;
		slot->tp_buf = NULL;
		(void)j1939_ring_push(&s->msgs.ring);
	} else {
		s->stats.rx_msg_overflow++;
	}
}

j1939_ret_t j1939_stack_send(j1939_t *s, const j1939_msg_t *msg, uint8_t sa) {
	uint32_t id;
	j1939_ret_t ret = j1939_id_build(msg->prio, msg->pgn, msg->da, sa, &id);

	if (ret == J1939_RET_OK) {
		ret = j1939_stack_tx(s, id, msg->data, (uint8_t)msg->len);
	}
	return ret;
}

j1939_ret_t j1939_stack_tx(j1939_t *s, uint32_t id, const uint8_t *data, uint8_t len) {
	j1939_ret_t ret = J1939_RET_ERR_FULL;
	j1939_port_frame_t *slot = j1939_queue_acquire(&s->tx);

	if (slot != NULL) {
		j1939_port_frame_build(slot, id, data, len);
		ret = j1939_queue_commit(&s->tx);
	}
	return ret;
}

static bool cfg_valid(const j1939_cfg_t *cfg) {
	return (cfg->rx_buf != NULL) && (cfg->rx_len > 0U) && (cfg->tx_buf != NULL) &&
	       (cfg->tx_len > 0U) && (cfg->msg_buf != NULL) && (cfg->msg_len > 0U) &&
	       pgn_list_valid(cfg->rx_pgns, cfg->rx_pgns_len) &&
	       pgn_list_valid(cfg->req_pgns, cfg->req_pgns_len) && j1939_tp_cfg_valid(cfg);
}

j1939_ret_t j1939_init(j1939_t *s, const j1939_cfg_t *cfg) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((s != NULL) && (cfg != NULL) && cfg_valid(cfg)) {
		/* Locals keep cppcheck's MISRA 11.8 check from treating the frames as const. */
		j1939_port_frame_t *rx_buf = cfg->rx_buf;
		j1939_port_frame_t *tx_buf = cfg->tx_buf;

		(void)j1939_queue_init(&s->rx, rx_buf, cfg->rx_len);
		(void)j1939_queue_init(&s->tx, tx_buf, cfg->tx_len);
		s->msgs.buf = cfg->msg_buf;
		j1939_ring_init(&s->msgs.ring, cfg->msg_len);
		s->rx_pgns = cfg->rx_pgns;
		s->rx_pgns_len = cfg->rx_pgns_len;
		s->req_pgns = cfg->req_pgns;
		s->req_pgns_len = cfg->req_pgns_len;
		s->ca_count = 0U;
		s->stats.rx_msg_overflow = 0U;
		s->stats.tx_overflow = 0U;
		j1939_addr_init(s);
		j1939_tp_init(s, cfg);
		j1939_dm_stack_init(s);
		ret = J1939_RET_OK;
	}
	return ret;
}

j1939_ret_t j1939_ca_add(j1939_t *s, const j1939_ca_cfg_t *cfg, j1939_ca_id_t *id) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((s != NULL) && (cfg != NULL) && (id != NULL) && (cfg->address <= CA_ADDRESS_MAX) &&
	    !ca_find(s, cfg->address)) {
		if (s->ca_count < (uint8_t)J1939_CFG_CA_MAX) {
			j1939_addr_ca_init(&s->ca[s->ca_count], cfg);
			*id = s->ca_count;
			s->ca_count++;
			ret = J1939_RET_OK;
		} else {
			ret = J1939_RET_ERR_FULL;
		}
	}
	return ret;
}

j1939_queue_t *j1939_rx_queue(j1939_t *s) {
	return (s != NULL) ? &s->rx : NULL;
}

j1939_queue_t *j1939_tx_queue(j1939_t *s) {
	return (s != NULL) ? &s->tx : NULL;
}

j1939_ret_t j1939_process(j1939_t *s, uint32_t elapsed_us) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if (s != NULL) {
		/* Bounded by the rx queue length. */
		uint16_t n = j1939_queue_count(&s->rx);
		uint16_t i;

		j1939_addr_process(s, elapsed_us);
		for (i = 0U; i < n; i++) {
			const j1939_port_frame_t *f = j1939_queue_peek(&s->rx);

			if (f != NULL) {
				frame_handle(s, f);
				(void)j1939_queue_pop(&s->rx);
			}
		}
		j1939_tp_process(s, elapsed_us);
		j1939_dm_process(s, elapsed_us);
		ret = J1939_RET_OK;
	}
	return ret;
}

const j1939_msg_t *j1939_msg_peek(j1939_t *s) {
	const j1939_msg_t *msg = NULL;
	uint16_t index;

	if ((s != NULL) && j1939_ring_tail(&s->msgs.ring, &index)) {
		msg = &s->msgs.buf[index].msg;
	}
	return msg;
}

j1939_ret_t j1939_msg_pop(j1939_t *s) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if (s != NULL) {
		ret = j1939_ring_pop(&s->msgs.ring) ? J1939_RET_OK : J1939_RET_ERR_EMPTY;
	}
	return ret;
}

j1939_ret_t j1939_send(j1939_t *s, j1939_ca_id_t ca, const j1939_msg_t *msg) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((s != NULL) && (msg != NULL) && (ca < s->ca_count) &&
	    ((msg->data != NULL) || (msg->len == 0U))) {
		if (!j1939_addr_tx_allowed(&s->ca[ca])) {
			ret = J1939_RET_ERR_NO_ADDRESS;
		} else if (msg->len <= J1939_MSG_SINGLE_FRAME_MAX) {
			ret = j1939_stack_send(s, msg, s->ca[ca].address);
		} else {
			ret = j1939_tp_send(s, s->ca[ca].address, msg);
		}
	}
	return ret;
}

const j1939_stats_t *j1939_stats_get(const j1939_t *s) {
	return (s != NULL) ? &s->stats : NULL;
}
