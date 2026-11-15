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
#include "j1939_internal.h"

#define NAMETAB_WORD_BITS 32U          /**< Bits in each NAME half of an entry. */
#define NAMETAB_WORD_MASK 0xFFFFFFFFUL /**< Low half of a NAME. */

/** Reads the NAME of an entry. */
static uint64_t nametab_entry_name(const j1939_nametab_entry_t *e) {
	return ((uint64_t)e->name_hi << NAMETAB_WORD_BITS) | (uint64_t)e->name_lo;
}

/** Tells whether a NAME belongs to a CA of the stack. */
static bool nametab_own(const j1939_t *s, uint64_t name) {
	bool own = false;
	uint8_t i;

	for (i = 0U; (!own) && (i < s->cfg->ca_len); i++) {
		own = s->ca[i].name == name;
	}
	return own;
}

/**
 * Finds the entry of a NAME in a stack with a table. Returns its index, or s->nametab.count if the
 * NAME is not listed.
 */
static uint16_t nametab_find(const j1939_t *s, uint64_t name) {
	const j1939_nametab_t *t = &s->nametab;
	uint16_t found = t->count;
	uint16_t i;

	for (i = 0U; (found == t->count) && (i < t->count); i++) {
		if (nametab_entry_name(&s->cfg->nametab[i]) == name) {
			found = i;
		}
	}
	return found;
}

/**
 * Records the address of the NAME of another node, J1939_ADDR_NULL for Cannot Claim, in a stack
 * with a table.
 *
 * Another NAME holding a claimed address loses it. A new NAME takes the next free entry; with the
 * table full it is counted and not recorded.
 */
static void nametab_record(j1939_t *s, uint64_t name, uint8_t address) {
	j1939_nametab_t *t = &s->nametab;
	bool changed = false;
	uint16_t index;

	if (address != J1939_ADDR_NULL) {
		uint16_t i;

		for (i = 0U; i < t->count; i++) {
			if ((s->cfg->nametab[i].address == address) &&
			    (nametab_entry_name(&s->cfg->nametab[i]) != name)) {
				s->cfg->nametab[i].address = J1939_ADDR_NULL;
				changed = true;
			}
		}
	}
	index = nametab_find(s, name);
	if (index < t->count) {
		if (s->cfg->nametab[index].address != address) {
			s->cfg->nametab[index].address = address;
			changed = true;
		}
	} else if (t->count < s->cfg->nametab_len) {
		s->cfg->nametab[t->count].name_lo = (uint32_t)(name & NAMETAB_WORD_MASK);
		s->cfg->nametab[t->count].name_hi = (uint32_t)(name >> NAMETAB_WORD_BITS);
		s->cfg->nametab[t->count].address = address;
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
			if (s->cfg->nametab[i].address == address) {
				*name = nametab_entry_name(&s->cfg->nametab[i]);
				found = true;
			}
		}
	}
	return found;
}

/**
 * Chooses the CA that sends the global Request after j1939_init(). Returns true once a CA
 * of the stack has claimed its address; ca is written only then.
 *
 * The startup Request is due from the first CA that has claimed, unless a global Request for
 * Address Claimed went out first, see j1939_nametab_global_request().
 */
static bool nametab_startup_sender(const j1939_t *s, j1939_ca_id_t *ca) {
	bool found = false;
	uint8_t i;

	for (i = 0U; (!found) && (i < s->cfg->ca_len); i++) {
		if (j1939_addr_ca_claimed(&s->ca[i])) {
			*ca = i;
			found = true;
		}
	}
	return found;
}

/**
 * Queues a Request for Address Claimed to da, J1939_ADDR_GLOBAL or the address asked. Returns
 * J1939_RET_ERR_FULL if the tx queue is full.
 *
 * Sent from a CA that has claimed its address if there is one, otherwise from J1939_ADDR_NULL.
 */
static j1939_ret_t nametab_request_send(j1939_t *s, uint8_t da) {
	j1939_ca_id_t ca = 0U;
	j1939_ret_t ret;

	if (nametab_startup_sender(s, &ca) || (s->cfg->ca_len > 0U)) {
		/* Uses J1939_ADDR_NULL for a CA without an address. */
		ret = j1939_addr_request_send(s, ca, da);
	} else {
		ret = j1939_request_tx(s, J1939_ADDR_NULL, da, J1939_PGN_ADDRESS_CLAIMED);
	}
	return ret;
}

void j1939_nametab_init(j1939_t *s) {
	s->nametab.request = J1939_ADDR_NULL;
	/* With a table, the first CA that has claimed asks every node for its claim. */
	s->nametab.startup_due = s->cfg->nametab_len > 0U;
}

void j1939_nametab_claimed(j1939_t *s, uint64_t name, uint8_t address) {
	if ((s->cfg->nametab_len > 0U) && !nametab_own(s, name)) {
		nametab_record(s, name, address);
	}
}

void j1939_nametab_cannot_claim(j1939_t *s, uint64_t name) {
	if ((s->cfg->nametab_len > 0U) && !nametab_own(s, name)) {
		nametab_record(s, name, J1939_ADDR_NULL);
	}
}

void j1939_nametab_global_request(j1939_t *s) {
	s->nametab.startup_due = false;
}

void j1939_nametab_process(j1939_t *s, uint32_t elapsed_us) {
	j1939_nametab_t *t = &s->nametab;
	j1939_ca_id_t ca = 0U;

	if (s->cfg->nametab != NULL) {
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

	if ((s != NULL) && (name != NULL) && (address <= J1939_ADDR_MAX)) {
		if (s->cfg->nametab_len == 0U) {
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
		if (s->cfg->nametab_len == 0U) {
			ret = J1939_RET_ERR_STATE;
		} else {
			uint16_t index = nametab_find(s, name);

			if (index < s->nametab.count) {
				*address = s->cfg->nametab[index].address;
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
		*name = nametab_entry_name(&s->cfg->nametab[index]);
		*address = s->cfg->nametab[index].address;
		ret = J1939_RET_OK;
	}
	return ret;
}

uint16_t j1939_nametab_changes(const j1939_t *s) {
	return (s != NULL) ? s->nametab.changes : 0U;
}
