/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_request.c
 * @brief Request (PGN 59904) and Acknowledgement (PGN 59392), J1939/21.
 */

#include "j1939/j1939_request.h"

#include <stddef.h>

#include "j1939/j1939_addr.h"
#include "j1939/j1939_id.h"
#include "j1939_internal.h"

#define REQUEST_PRIO      6U    /**< Priority of Requests and Acknowledgements. */
#define ACK_RESERVED      0xFFU /**< Reserved bytes of an Acknowledgement. */
#define ACK_GROUP_FN_NONE 0xFFU /**< Group function value when none applies. */
#define ACK_REQUESTER     4U    /**< Position of the acknowledged address. */
#define ACK_PGN           5U    /**< Position of the acknowledged PGN. */

void j1939_request_handle(j1939_t *s, const j1939_msg_t *req) {
	if (req->len >= J1939_REQUEST_LEN) {
		uint32_t pgn = j1939_stack_pgn_get(req->data);

		if (pgn > J1939_PGN_MAX) {
			/* Malformed Request. */
		} else if (pgn == J1939_PGN_ADDRESS_CLAIMED) {
			j1939_addr_request_handle(s, req->da);
		} else if (req->sa >= J1939_ADDR_NULL) {
			/* A node without an address may only ask for the claims; it cannot be
			 * answered or acknowledged. */
		} else if (j1939_dm_request_handle(s, req, pgn)) {
			/* Diagnostic message answered by the stack. */
		} else if (j1939_txobj_request_handle(s, req, pgn)) {
			/* Transmit object answered by the stack. */
		} else if (j1939_stack_pgn_listed(s->cfg->req_pgns, s->cfg->req_pgns_len, pgn)) {
			(void)j1939_stack_deliver(s, req);
		} else if ((req->da != J1939_ADDR_GLOBAL) && j1939_addr_claimed(s, req->da)) {
			/* Nobody answers it: NACK from the CA it was sent to. */
			if (j1939_request_ack_tx(s, req->da, J1939_ACK_CTRL_NACK, req->sa, pgn) !=
			    J1939_RET_OK) {
				s->stats.tx_overflow++;
			}
		} else {
			/* Global Requests for unsupported PGNs are not answered, nor are
			 * Requests to a CA whose claim is not complete. */
		}
	}
}

uint8_t j1939_request_requester(const j1939_msg_t *req) {
	return (req->da == J1939_ADDR_GLOBAL) ? J1939_ADDR_GLOBAL : req->sa;
}

j1939_ret_t j1939_request_tx(j1939_t *s, uint8_t sa, uint8_t da, uint32_t pgn) {
	uint8_t data[J1939_REQUEST_LEN];

	j1939_stack_pgn_put(pgn, data);
	return j1939_stack_tx(s, REQUEST_PRIO, J1939_PGN_REQUEST, da, sa, data,
	                      (uint8_t)J1939_REQUEST_LEN);
}

j1939_ret_t j1939_request_ack_tx(j1939_t *s, uint8_t sa, uint8_t ctrl, uint8_t requester,
                                 uint32_t pgn) {
	uint8_t data[J1939_ACK_LEN] = {
	        ctrl, ACK_GROUP_FN_NONE, ACK_RESERVED, ACK_RESERVED, 0U, 0U, 0U, 0U};

	/* J1939/21 sends it to the global address and names the requester in the data. */
	data[ACK_REQUESTER] = requester;
	j1939_stack_pgn_put(pgn, &data[ACK_PGN]);
	return j1939_stack_tx(s, REQUEST_PRIO, J1939_PGN_ACK, J1939_ADDR_GLOBAL, sa, data,
	                      (uint8_t)J1939_ACK_LEN);
}

j1939_ret_t j1939_request_send(j1939_t *s, j1939_ca_id_t ca, uint32_t pgn, uint8_t da) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if (pgn > J1939_PGN_MAX) {
		/* Not a PGN. */
	} else if (pgn == J1939_PGN_ADDRESS_CLAIMED) {
		/* Allowed without an address. */
		ret = j1939_addr_request_send(s, ca, da);
	} else if ((s != NULL) && (ca < s->cfg->ca_len)) {
		const j1939_ca_t *c = &s->ca[ca];

		ret = j1939_addr_ca_claimed(c) ? j1939_request_tx(s, c->address, da, pgn)
		                               : J1939_RET_ERR_NO_ADDRESS;
	} else {
		/* NULL stack or unknown CA. */
	}
	return ret;
}

j1939_ret_t j1939_request_pgn_get(const j1939_msg_t *msg, uint32_t *pgn) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((msg != NULL) && (pgn != NULL) && (msg->pgn == J1939_PGN_REQUEST) &&
	    (msg->len >= J1939_REQUEST_LEN) && (msg->data != NULL)) {
		uint32_t requested = j1939_stack_pgn_get(msg->data);

		if (requested <= J1939_PGN_MAX) {
			*pgn = requested;
			ret = J1939_RET_OK;
		}
	}
	return ret;
}
