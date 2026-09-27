/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

#include "j1939/j1939_request.h"

#include <stddef.h>

#include "j1939/j1939_addr.h"
#include "j1939/j1939_id.h"
#include "j1939_addr_priv.h"
#include "j1939_stack_priv.h"

#define BYTE_MASK         0xFFU
#define BYTE_SHIFT        8U
#define ACK_PRIO          6U
#define ACK_RESERVED      0xFFU
#define ACK_GROUP_FN_NONE 0xFFU

static uint32_t pgn_decode(const uint8_t *data) {
	return (uint32_t)data[0] | ((uint32_t)data[1] << BYTE_SHIFT) |
	       ((uint32_t)data[2] << (2U * BYTE_SHIFT));
}

static void pgn_encode(uint32_t pgn, uint8_t *data) {
	data[0] = (uint8_t)(pgn & BYTE_MASK);
	data[1] = (uint8_t)((pgn >> BYTE_SHIFT) & BYTE_MASK);
	data[2] = (uint8_t)((pgn >> (2U * BYTE_SHIFT)) & BYTE_MASK);
}

/* NACK a destination specific Request. J1939/21 sends it to the global address. */
static void nack_send(j1939_t *s, uint8_t own_address, uint8_t requester, uint32_t pgn) {
	uint8_t data[J1939_ACK_LEN] = {J1939_ACK_CTRL_NACK,
	                               ACK_GROUP_FN_NONE,
	                               ACK_RESERVED,
	                               ACK_RESERVED,
	                               requester,
	                               0U,
	                               0U,
	                               0U};
	uint32_t id;

	pgn_encode(pgn, &data[5]);
	if (j1939_id_build(ACK_PRIO, J1939_PGN_ACK, J1939_ADDR_GLOBAL, own_address, &id) ==
	    J1939_RET_OK) {
		if (j1939_stack_tx(s, id, data, (uint8_t)J1939_ACK_LEN) != J1939_RET_OK) {
			s->stats.tx_overflow++;
		}
	}
}

void j1939_request_handle(j1939_t *s, uint32_t id, const uint8_t *data, uint8_t len) {
	if (len >= J1939_REQUEST_LEN) {
		uint32_t pgn = pgn_decode(data);
		uint8_t da = j1939_id_da_get(id);

		if (pgn > J1939_PGN_MAX) {
			/* Malformed Request. */
		} else if (pgn == J1939_PGN_ADDRESS_CLAIMED) {
			j1939_addr_request_handle(s, da);
		} else if (j1939_stack_pgn_listed(s->req_pgns, s->req_pgns_len, pgn)) {
			j1939_stack_deliver(s, id, data, len);
		} else if ((da != J1939_ADDR_GLOBAL) && j1939_addr_claimed(s, da)) {
			nack_send(s, da, j1939_id_sa_get(id), pgn);
		} else {
			/* Global Requests for unsupported PGNs are not answered, nor are
			 * Requests to a CA whose claim is not complete. */
		}
	}
}

j1939_ret_t j1939_request_send(j1939_t *s, j1939_ca_id_t ca, uint32_t pgn, uint8_t da) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;
	uint8_t data[J1939_REQUEST_LEN];
	j1939_msg_t msg;

	if (pgn <= J1939_PGN_MAX) {
		pgn_encode(pgn, data);
		msg.pgn = J1939_PGN_REQUEST;
		msg.prio = ACK_PRIO;
		msg.sa = 0U;
		msg.da = da;
		msg.len = (uint16_t)J1939_REQUEST_LEN;
		msg.data = data;
		if (pgn == J1939_PGN_ADDRESS_CLAIMED) {
			ret = j1939_addr_request_send(s, ca, &msg);
		} else {
			ret = j1939_send(s, ca, &msg);
		}
	}
	return ret;
}

j1939_ret_t j1939_request_pgn_get(const j1939_msg_t *msg, uint32_t *pgn) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((msg != NULL) && (pgn != NULL) && (msg->pgn == J1939_PGN_REQUEST) &&
	    (msg->len >= J1939_REQUEST_LEN) && (msg->data != NULL)) {
		uint32_t requested = pgn_decode(msg->data);

		if (requested <= J1939_PGN_MAX) {
			*pgn = requested;
			ret = J1939_RET_OK;
		}
	}
	return ret;
}
