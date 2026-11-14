/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_nametab.c
 * @brief NAME table: NAMEs and addresses of the other nodes, filled from their
 *        claims and from Requests for Address Claimed.
 */

#include "j1939/j1939_nametab.h"

#include <stddef.h>

#include "j1939/j1939_addr.h"
#include "j1939/j1939_id.h"
#include "j1939/j1939_request.h"
#include "j1939_addr_priv.h"
#include "j1939_nametab_priv.h"
#include "j1939_stack_priv.h"

#define NAMETAB_ADDRESS_MAX 0xFDU        /**< Highest address a node can claim (253). */
#define NAMETAB_WORD_BITS   32U          /**< Bits in each NAME half of an entry. */
#define NAMETAB_WORD_MASK   0xFFFFFFFFUL /**< Low half of a NAME. */
#define NAMETAB_BYTE_MASK   0xFFU        /**< One byte of the requested PGN. */
#define NAMETAB_BYTE_SHIFT  8U           /**< Bits per byte of the requested PGN. */

/**
 * @brief Reads the NAME of an entry.
 * @param e  Entry.
 * @return The 64-bit NAME.
 */
static uint64_t nametab_entry_name(const j1939_nametab_entry_t *e) {
	return ((uint64_t)e->name_hi << NAMETAB_WORD_BITS) | (uint64_t)e->name_lo;
}

/**
 * @brief Tells whether a NAME belongs to a CA of the stack.
 * @param s     Stack.
 * @param name  NAME.
 * @return true if a CA of the stack has @p name.
 */
static bool nametab_own(const j1939_t *s, uint64_t name) {
	bool own = false;
	uint8_t i;

	for (i = 0U; (!own) && (i < s->ca_count); i++) {
		own = s->ca[i].name == name;
	}
	return own;
}

/**
 * @brief Finds the entry of a NAME.
 * @param s     Stack with a table.
 * @param name  NAME.
 * @return Its index, or s->nametab.count if the NAME is not listed.
 */
static uint16_t nametab_find(const j1939_t *s, uint64_t name) {
	const j1939_nametab_t *t = &s->nametab;
	uint16_t found = t->count;
	uint16_t i;

	for (i = 0U; (found == t->count) && (i < t->count); i++) {
		if (nametab_entry_name(&t->buf[i]) == name) {
			found = i;
		}
	}
	return found;
}

/**
 * @brief Records the address of a NAME.
 *
 * Another NAME holding a claimed address loses it. A new NAME takes the next
 * free entry; with the table full it is counted and not recorded.
 *
 * @param s        Stack with a table.
 * @param name     NAME of another node.
 * @param address  Its address, or J1939_ADDR_NULL for Cannot Claim.
 */
static void nametab_record(j1939_t *s, uint64_t name, uint8_t address) {
	j1939_nametab_t *t = &s->nametab;
	bool changed = false;
	uint16_t index;

	if (address != J1939_ADDR_NULL) {
		uint16_t i;

		for (i = 0U; i < t->count; i++) {
			if ((t->buf[i].address == address) &&
			    (nametab_entry_name(&t->buf[i]) != name)) {
				t->buf[i].address = J1939_ADDR_NULL;
				changed = true;
			}
		}
	}
	index = nametab_find(s, name);
	if (index < t->count) {
		if (t->buf[index].address != address) {
			t->buf[index].address = address;
			changed = true;
		}
	} else if (t->count < t->len) {
		t->buf[t->count].name_lo = (uint32_t)(name & NAMETAB_WORD_MASK);
		t->buf[t->count].name_hi = (uint32_t)(name >> NAMETAB_WORD_BITS);
		t->buf[t->count].address = address;
		t->count++;
		changed = true;
	} else {
		s->stats.nametab_dropped++;
	}
	if (changed) {
		t->changes++;
	}
}

bool j1939_nametab_lookup(const j1939_t *s, uint8_t address, uint64_t *name) {
	const j1939_nametab_t *t = &s->nametab;
	bool found = false;

	if (!j1939_addr_held(s, address)) {
		uint16_t i;

		for (i = 0U; (!found) && (i < t->count); i++) {
			if (t->buf[i].address == address) {
				*name = nametab_entry_name(&t->buf[i]);
				found = true;
			}
		}
	}
	return found;
}

/**
 * @brief Chooses the CA that sends the global Request after j1939_nametab_init().
 *
 * The startup Request is due from the first CA that has claimed, unless a
 * global Request for Address Claimed went out first, see
 * j1939_nametab_global_request().
 *
 * @param s   Stack.
 * @param ca  The sending CA. Written only when the Request is due.
 * @return true once a CA of the stack has claimed its address.
 */
static bool nametab_startup_sender(const j1939_t *s, j1939_ca_id_t *ca) {
	bool found = false;
	uint8_t i;

	for (i = 0U; (!found) && (i < s->ca_count); i++) {
		if (j1939_addr_tx_allowed(&s->ca[i])) {
			*ca = i;
			found = true;
		}
	}
	return found;
}

/**
 * @brief Queues a Request for Address Claimed.
 *
 * Sent from a CA that has claimed its address if there is one, otherwise from
 * J1939_ADDR_NULL.
 *
 * @param s   Stack.
 * @param da  J1939_ADDR_GLOBAL or the address asked.
 * @return J1939_RET_OK, or J1939_RET_ERR_FULL if the tx queue is full.
 */
static j1939_ret_t nametab_request_send(j1939_t *s, uint8_t da) {
	const uint32_t pgn = J1939_PGN_ADDRESS_CLAIMED;
	const uint8_t data[J1939_REQUEST_LEN] = {
	        (uint8_t)(pgn & NAMETAB_BYTE_MASK),
	        (uint8_t)((pgn >> NAMETAB_BYTE_SHIFT) & NAMETAB_BYTE_MASK),
	        (uint8_t)((pgn >> (2U * NAMETAB_BYTE_SHIFT)) & NAMETAB_BYTE_MASK)};
	const j1939_msg_t msg = {
	        J1939_PGN_REQUEST, J1939_PRIO_DEFAULT, 0U, da, (uint16_t)J1939_REQUEST_LEN, data};
	j1939_ca_id_t ca = 0U;
	j1939_ret_t ret;

	if (nametab_startup_sender(s, &ca) || (s->ca_count > 0U)) {
		/* Uses J1939_ADDR_NULL for a CA without an address. */
		ret = j1939_addr_request_send(s, ca, &msg);
	} else {
		ret = j1939_stack_send(s, &msg, J1939_ADDR_NULL);
	}
	return ret;
}

void j1939_nametab_stack_init(j1939_t *s) {
	s->nametab.buf = NULL;
	s->nametab.hold_us = 0U;
	s->nametab.len = 0U;
	s->nametab.count = 0U;
	s->nametab.changes = 0U;
	s->nametab.request = J1939_ADDR_NULL;
	s->nametab.startup_due = false;
	s->stats.nametab_dropped = 0U;
}

j1939_ret_t j1939_nametab_init(j1939_t *s, j1939_nametab_entry_t *buf, uint16_t len) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((s != NULL) && ((buf != NULL) || (len == 0U))) {
		s->nametab.buf = (len > 0U) ? buf : NULL;
		s->nametab.hold_us = 0U;
		s->nametab.len = len;
		s->nametab.count = 0U;
		s->nametab.changes++;
		s->nametab.request = J1939_ADDR_NULL;
		s->nametab.startup_due = len > 0U;
		ret = J1939_RET_OK;
	}
	return ret;
}

void j1939_nametab_claimed(j1939_t *s, uint64_t name, uint8_t address) {
	if ((s->nametab.buf != NULL) && !nametab_own(s, name)) {
		nametab_record(s, name, address);
	}
}

void j1939_nametab_cannot_claim(j1939_t *s, uint64_t name) {
	if ((s->nametab.buf != NULL) && !nametab_own(s, name)) {
		nametab_record(s, name, J1939_ADDR_NULL);
	}
}

void j1939_nametab_global_request(j1939_t *s) {
	s->nametab.startup_due = false;
}

void j1939_nametab_process(j1939_t *s, uint32_t elapsed_us) {
	j1939_nametab_t *t = &s->nametab;
	j1939_ca_id_t ca = 0U;

	if (t->buf != NULL) {
		t->hold_us = (t->hold_us > elapsed_us) ? (t->hold_us - elapsed_us) : 0U;
		if (t->startup_due && nametab_startup_sender(s, &ca) &&
		    (nametab_request_send(s, J1939_ADDR_GLOBAL) == J1939_RET_OK)) {
			t->startup_due = false;
		}
		if ((t->request != J1939_ADDR_NULL) && (t->hold_us == 0U) &&
		    (nametab_request_send(s, t->request) == J1939_RET_OK)) {
			t->request = J1939_ADDR_NULL;
			t->hold_us = J1939_NAMETAB_REQUEST_HOLD_US;
		}
	}
}

j1939_ret_t j1939_nametab_name_get(j1939_t *s, uint8_t address, uint64_t *name) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((s != NULL) && (name != NULL) && (address <= NAMETAB_ADDRESS_MAX)) {
		if (s->nametab.buf == NULL) {
			ret = J1939_RET_ERR_STATE;
		} else if (j1939_nametab_lookup(s, address, name)) {
			ret = J1939_RET_OK;
		} else {
			if ((s->nametab.request == J1939_ADDR_NULL) &&
			    !j1939_addr_held(s, address)) {
				s->nametab.request = address;
			}
			ret = J1939_RET_ERR_EMPTY;
		}
	}
	return ret;
}

j1939_ret_t j1939_nametab_address_get(const j1939_t *s, uint64_t name, uint8_t *address) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((s != NULL) && (address != NULL)) {
		if (s->nametab.buf == NULL) {
			ret = J1939_RET_ERR_STATE;
		} else {
			uint16_t index = nametab_find(s, name);

			if (index < s->nametab.count) {
				*address = s->nametab.buf[index].address;
				ret = J1939_RET_OK;
			} else {
				ret = J1939_RET_ERR_EMPTY;
			}
		}
	}
	return ret;
}

uint16_t j1939_nametab_count(const j1939_t *s) {
	return (s != NULL) ? s->nametab.count : 0U;
}

j1939_ret_t j1939_nametab_at(const j1939_t *s, uint16_t index, uint64_t *name, uint8_t *address) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((s != NULL) && (name != NULL) && (address != NULL) && (index < s->nametab.count)) {
		*name = nametab_entry_name(&s->nametab.buf[index]);
		*address = s->nametab.buf[index].address;
		ret = J1939_RET_OK;
	}
	return ret;
}

uint16_t j1939_nametab_changes(const j1939_t *s) {
	return (s != NULL) ? s->nametab.changes : 0U;
}
