/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_names.c
 * @brief NAME table: NAMEs and addresses of the other nodes, filled from their
 *        claims and from Requests for Address Claimed.
 */

#include "j1939/j1939_names.h"

#include <stddef.h>

#include "j1939/j1939_addr.h"
#include "j1939/j1939_id.h"
#include "j1939/j1939_request.h"
#include "j1939_addr_priv.h"
#include "j1939_names_priv.h"
#include "j1939_stack_priv.h"

#define NAMES_ADDRESS_MAX 0xFDU        /**< Highest address a node can claim (253). */
#define NAMES_WORD_BITS   32U          /**< Bits in each NAME half of an entry. */
#define NAMES_WORD_MASK   0xFFFFFFFFUL /**< Low half of a NAME. */
#define NAMES_BYTE_MASK   0xFFU        /**< One byte of the requested PGN. */
#define NAMES_BYTE_SHIFT  8U           /**< Bits per byte of the requested PGN. */

/**
 * @brief Reads the NAME of an entry.
 * @param e  Entry.
 * @return The 64-bit NAME.
 */
static uint64_t names_entry_name(const j1939_names_entry_t *e) {
	return ((uint64_t)e->name_hi << NAMES_WORD_BITS) | (uint64_t)e->name_lo;
}

/**
 * @brief Tells whether a NAME belongs to a CA of the stack.
 * @param s     Stack.
 * @param name  NAME.
 * @return true if a CA of the stack has @p name.
 */
static bool names_own(const j1939_t *s, uint64_t name) {
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
 * @return Its index, or s->names.count if the NAME is not listed.
 */
static uint16_t names_find(const j1939_t *s, uint64_t name) {
	const j1939_names_tab_t *t = &s->names;
	uint16_t found = t->count;
	uint16_t i;

	for (i = 0U; (found == t->count) && (i < t->count); i++) {
		if (names_entry_name(&t->buf[i]) == name) {
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
static void names_record(j1939_t *s, uint64_t name, uint8_t address) {
	j1939_names_tab_t *t = &s->names;
	bool changed = false;
	uint16_t index;

	if (address != J1939_ADDR_NULL) {
		uint16_t i;

		for (i = 0U; i < t->count; i++) {
			if ((t->buf[i].address == address) &&
			    (names_entry_name(&t->buf[i]) != name)) {
				t->buf[i].address = J1939_ADDR_NULL;
				changed = true;
			}
		}
	}
	index = names_find(s, name);
	if (index < t->count) {
		if (t->buf[index].address != address) {
			t->buf[index].address = address;
			changed = true;
		}
	} else if (t->count < t->len) {
		t->buf[t->count].name_lo = (uint32_t)(name & NAMES_WORD_MASK);
		t->buf[t->count].name_hi = (uint32_t)(name >> NAMES_WORD_BITS);
		t->buf[t->count].address = address;
		t->count++;
		changed = true;
	} else {
		s->stats.names_dropped++;
	}
	if (changed) {
		t->changes++;
	}
}

/**
 * @brief Looks up the NAME at an address without sending a Request.
 * @param s        Stack with a table.
 * @param address  Source address, 0..253.
 * @param name     NAME. Written only if found.
 * @return true if another node of the table holds @p address and no CA of
 *         the stack does.
 */
static bool names_lookup(const j1939_t *s, uint8_t address, uint64_t *name) {
	const j1939_names_tab_t *t = &s->names;
	bool found = false;

	if (!j1939_addr_held(s, address)) {
		uint16_t i;

		for (i = 0U; (!found) && (i < t->count); i++) {
			if (t->buf[i].address == address) {
				*name = names_entry_name(&t->buf[i]);
				found = true;
			}
		}
	}
	return found;
}

/**
 * @brief Chooses the CA that sends the global Request after j1939_names_init().
 *
 * The startup Request is due from the first CA that has claimed, unless a
 * global Request for Address Claimed went out first, see
 * j1939_names_global_request().
 *
 * @param s   Stack.
 * @param ca  The sending CA. Written only when the Request is due.
 * @return true once a CA of the stack has claimed its address.
 */
static bool names_startup_sender(const j1939_t *s, j1939_ca_id_t *ca) {
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
static j1939_ret_t names_request_send(j1939_t *s, uint8_t da) {
	const uint32_t pgn = J1939_PGN_ADDRESS_CLAIMED;
	const uint8_t data[J1939_REQUEST_LEN] = {
	        (uint8_t)(pgn & NAMES_BYTE_MASK),
	        (uint8_t)((pgn >> NAMES_BYTE_SHIFT) & NAMES_BYTE_MASK),
	        (uint8_t)((pgn >> (2U * NAMES_BYTE_SHIFT)) & NAMES_BYTE_MASK)};
	const j1939_msg_t msg = {
	        J1939_PGN_REQUEST, J1939_PRIO_DEFAULT, 0U, da, (uint16_t)J1939_REQUEST_LEN, data};
	j1939_ca_id_t ca = 0U;
	j1939_ret_t ret;

	if (names_startup_sender(s, &ca) || (s->ca_count > 0U)) {
		/* Uses J1939_ADDR_NULL for a CA without an address. */
		ret = j1939_addr_request_send(s, ca, &msg);
	} else {
		ret = j1939_stack_send(s, &msg, J1939_ADDR_NULL);
	}
	return ret;
}

void j1939_names_stack_init(j1939_t *s) {
	s->names.buf = NULL;
	s->names.hold_us = 0U;
	s->names.len = 0U;
	s->names.count = 0U;
	s->names.changes = 0U;
	s->names.request = J1939_ADDR_NULL;
	s->names.startup_due = false;
	s->stats.names_dropped = 0U;
}

j1939_ret_t j1939_names_init(j1939_t *s, j1939_names_entry_t *buf, uint16_t len) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((s != NULL) && ((buf != NULL) || (len == 0U))) {
		s->names.buf = (len > 0U) ? buf : NULL;
		s->names.hold_us = 0U;
		s->names.len = len;
		s->names.count = 0U;
		s->names.changes++;
		s->names.request = J1939_ADDR_NULL;
		s->names.startup_due = len > 0U;
		ret = J1939_RET_OK;
	}
	return ret;
}

void j1939_names_claimed(j1939_t *s, uint64_t name, uint8_t address) {
	if ((s->names.buf != NULL) && !names_own(s, name)) {
		names_record(s, name, address);
	}
}

void j1939_names_cannot_claim(j1939_t *s, uint64_t name) {
	if ((s->names.buf != NULL) && !names_own(s, name)) {
		names_record(s, name, J1939_ADDR_NULL);
	}
}

void j1939_names_global_request(j1939_t *s) {
	s->names.startup_due = false;
}

void j1939_names_process(j1939_t *s, uint32_t elapsed_us) {
	j1939_names_tab_t *t = &s->names;
	j1939_ca_id_t ca = 0U;

	if (t->buf != NULL) {
		t->hold_us = (t->hold_us > elapsed_us) ? (t->hold_us - elapsed_us) : 0U;
		if (t->startup_due && names_startup_sender(s, &ca) &&
		    (names_request_send(s, J1939_ADDR_GLOBAL) == J1939_RET_OK)) {
			t->startup_due = false;
		}
		if ((t->request != J1939_ADDR_NULL) && (t->hold_us == 0U) &&
		    (names_request_send(s, t->request) == J1939_RET_OK)) {
			t->request = J1939_ADDR_NULL;
			t->hold_us = J1939_NAMES_REQUEST_HOLD_US;
		}
	}
}

j1939_ret_t j1939_names_name_get(j1939_t *s, uint8_t address, uint64_t *name) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((s != NULL) && (name != NULL) && (address <= NAMES_ADDRESS_MAX)) {
		if (s->names.buf == NULL) {
			ret = J1939_RET_ERR_STATE;
		} else if (names_lookup(s, address, name)) {
			ret = J1939_RET_OK;
		} else {
			if ((s->names.request == J1939_ADDR_NULL) && !j1939_addr_held(s, address)) {
				s->names.request = address;
			}
			ret = J1939_RET_ERR_EMPTY;
		}
	}
	return ret;
}

j1939_ret_t j1939_names_address_get(const j1939_t *s, uint64_t name, uint8_t *address) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((s != NULL) && (address != NULL)) {
		if (s->names.buf == NULL) {
			ret = J1939_RET_ERR_STATE;
		} else {
			uint16_t index = names_find(s, name);

			if (index < s->names.count) {
				*address = s->names.buf[index].address;
				ret = J1939_RET_OK;
			} else {
				ret = J1939_RET_ERR_EMPTY;
			}
		}
	}
	return ret;
}

uint16_t j1939_names_count(const j1939_t *s) {
	return (s != NULL) ? s->names.count : 0U;
}

j1939_ret_t j1939_names_at(const j1939_t *s, uint16_t index, uint64_t *name, uint8_t *address) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((s != NULL) && (name != NULL) && (address != NULL) && (index < s->names.count)) {
		*name = names_entry_name(&s->names.buf[index]);
		*address = s->names.buf[index].address;
		ret = J1939_RET_OK;
	}
	return ret;
}

uint16_t j1939_names_changes(const j1939_t *s) {
	return (s != NULL) ? s->names.changes : 0U;
}
