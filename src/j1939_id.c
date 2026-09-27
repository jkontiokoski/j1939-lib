/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

#include "j1939/j1939_id.h"

#include <stddef.h>

#define PRIO_SHIFT 26U
#define PRIO_MASK  0x7U
#define PGN_SHIFT  8U
#define PF_SHIFT   8U
#define BYTE_MASK  0xFFU

static uint8_t pgn_pf(uint32_t pgn) {
	return (uint8_t)((pgn >> PF_SHIFT) & BYTE_MASK);
}

uint8_t j1939_id_prio_get(uint32_t id) {
	return (uint8_t)((id >> PRIO_SHIFT) & PRIO_MASK);
}

uint32_t j1939_id_pgn_get(uint32_t id) {
	uint32_t pgn = (id >> PGN_SHIFT) & J1939_PGN_MAX;

	if (j1939_pgn_is_pdu1(pgn)) {
		pgn &= ~BYTE_MASK;
	}
	return pgn;
}

uint8_t j1939_id_da_get(uint32_t id) {
	uint8_t da = J1939_ADDR_GLOBAL;

	if (j1939_pgn_is_pdu1((id >> PGN_SHIFT) & J1939_PGN_MAX)) {
		da = (uint8_t)((id >> PGN_SHIFT) & BYTE_MASK);
	}
	return da;
}

uint8_t j1939_id_sa_get(uint32_t id) {
	return (uint8_t)(id & BYTE_MASK);
}

bool j1939_pgn_is_pdu1(uint32_t pgn) {
	return pgn_pf(pgn) < J1939_PF_PDU2_MIN;
}

j1939_ret_t j1939_id_build(uint8_t prio, uint32_t pgn, uint8_t da, uint8_t sa, uint32_t *id) {
	j1939_ret_t ret = J1939_RET_OK;

	if ((id == NULL) || (prio > J1939_PRIO_MAX) || (pgn > J1939_PGN_MAX)) {
		ret = J1939_RET_ERR_ARG;
	} else if (j1939_pgn_is_pdu1(pgn)) {
		if ((pgn & BYTE_MASK) != 0U) {
			ret = J1939_RET_ERR_ARG;
		}
	} else if (da != J1939_ADDR_GLOBAL) {
		ret = J1939_RET_ERR_ARG;
	} else {
		/* PDU2 with global destination: nothing further to check. */
	}

	if (ret == J1939_RET_OK) {
		uint32_t ps = j1939_pgn_is_pdu1(pgn) ? (uint32_t)da : 0U;

		*id = ((uint32_t)prio << PRIO_SHIFT) | ((pgn | ps) << PGN_SHIFT) | (uint32_t)sa;
	}
	return ret;
}
