/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

#include "j1939/j1939_addr.h"

#include <stddef.h>
#include <string.h>

#include "j1939/j1939_id.h"
#include "j1939/j1939_name.h"
#include "j1939_addr_priv.h"
#include "j1939_stack_priv.h"

#define CLAIM_PRIO     6U
#define CA_ADDRESS_MAX 0xFDU
#define BITS_PER_BYTE  8U

static bool self_cfg(uint8_t address) {
	return (address >= J1939_ADDR_SELF_CFG_MIN) && (address <= J1939_ADDR_SELF_CFG_MAX);
}

static bool held(const j1939_ca_t *ca) {
	return (ca->state == J1939_ADDR_STATE_CLAIMING) || (ca->state == J1939_ADDR_STATE_CLAIMED);
}

/* Records a self-configurable address claimed by another node. */
static void taken_set(j1939_t *s, uint8_t address) {
	if (self_cfg(address)) {
		uint32_t bit = (uint32_t)address - J1939_ADDR_SELF_CFG_MIN;

		s->addr_taken[bit / BITS_PER_BYTE] |= (uint8_t)(1U << (bit % BITS_PER_BYTE));
	}
}

static bool taken_get(const j1939_t *s, uint8_t address) {
	uint32_t bit = (uint32_t)address - J1939_ADDR_SELF_CFG_MIN;

	return (s->addr_taken[bit / BITS_PER_BYTE] & (uint8_t)(1U << (bit % BITS_PER_BYTE))) != 0U;
}

/* Returns true if a CA of the stack holds or is about to claim address. */
static bool local_in_use(const j1939_t *s, uint8_t address) {
	bool found = false;
	uint8_t i;

	for (i = 0U; (!found) && (i < s->ca_count); i++) {
		found = s->ca[i].address == address;
	}
	return found;
}

/* Returns the first self-configurable address neither taken nor used locally, or NULL. */
static uint8_t address_select(const j1939_t *s) {
	uint8_t found = J1939_ADDR_NULL;
	uint32_t a;

	for (a = J1939_ADDR_SELF_CFG_MIN;
	     (found == J1939_ADDR_NULL) && (a <= J1939_ADDR_SELF_CFG_MAX); a++) {
		if (!taken_get(s, (uint8_t)a) && !local_in_use(s, (uint8_t)a)) {
			found = (uint8_t)a;
		}
	}
	return found;
}

/* Cannot Claim delay: 0..255 steps, from the NAME so that CAs spread without rand(). */
static uint32_t cannot_claim_delay(uint64_t name) {
	uint8_t data[J1939_NAME_LEN];
	uint8_t fold = 0U;
	uint32_t i;

	(void)j1939_name_to_bytes(name, data);
	for (i = 0U; i < J1939_NAME_LEN; i++) {
		fold ^= data[i];
	}
	return (uint32_t)fold * J1939_ADDR_CANNOT_CLAIM_STEP_US;
}

/* Queues Address Claimed from sa with the CA's NAME; sa J1939_ADDR_NULL makes it Cannot Claim. */
static j1939_ret_t claim_tx(j1939_t *s, const j1939_ca_t *ca, uint8_t sa) {
	uint8_t data[J1939_NAME_LEN];
	uint32_t id = 0U;

	(void)j1939_name_to_bytes(ca->name, data);
	/* Constant arguments: the identifier is always valid. */
	(void)j1939_id_build(CLAIM_PRIO, J1939_PGN_ADDRESS_CLAIMED, J1939_ADDR_GLOBAL, sa, &id);
	return j1939_stack_tx(s, id, data, (uint8_t)J1939_NAME_LEN);
}

/* Answers with the CA's Address Claimed; a full tx queue is counted. */
static void claim_repeat(j1939_t *s, const j1939_ca_t *ca) {
	if (claim_tx(s, ca, ca->address) != J1939_RET_OK) {
		s->stats.tx_overflow++;
	}
}

/* Sends the first Address Claimed for ca->address. Stays unclaimed if the tx queue is full. */
static void claim_start(j1939_t *s, j1939_ca_t *ca) {
	if (claim_tx(s, ca, ca->address) == J1939_RET_OK) {
		if (self_cfg(ca->address)) {
			ca->state = J1939_ADDR_STATE_CLAIMING;
			ca->timer_us = J1939_ADDR_CLAIM_WAIT_US;
		} else {
			ca->state = J1939_ADDR_STATE_CLAIMED;
		}
	}
}

/* Gives up the address and schedules a Cannot Claim. */
static void cannot_claim_enter(j1939_ca_t *ca) {
	ca->state = J1939_ADDR_STATE_CANNOT_CLAIM;
	ca->address = J1939_ADDR_NULL;
	ca->cannot_claim_pending = true;
	ca->timer_us = cannot_claim_delay(ca->name);
}

/* The CA lost its address to a NAME of higher priority. */
static void address_lost(j1939_t *s, j1939_ca_t *ca) {
	uint8_t next = J1939_ADDR_NULL;

	if (j1939_name_arbitrary_address(ca->name)) {
		next = address_select(s);
	}
	if (next != J1939_ADDR_NULL) {
		ca->address = next;
		ca->state = J1939_ADDR_STATE_UNCLAIMED;
		claim_start(s, ca);
	} else {
		cannot_claim_enter(ca);
	}
}

/* Returns true once timer_us has run out, and counts it down otherwise. */
static bool timer_expired(j1939_ca_t *ca, uint32_t elapsed_us) {
	bool expired = ca->timer_us <= elapsed_us;

	ca->timer_us = expired ? 0U : (ca->timer_us - elapsed_us);
	return expired;
}

void j1939_addr_init(j1939_t *s) {
	(void)memset(s->addr_taken, 0, sizeof(s->addr_taken));
}

void j1939_addr_ca_init(j1939_ca_t *ca, const j1939_ca_cfg_t *cfg) {
	ca->name = cfg->name;
	ca->state = J1939_ADDR_STATE_UNCLAIMED;
	ca->timer_us = 0U;
	ca->address = cfg->address;
	ca->cannot_claim_pending = false;
}

void j1939_addr_process(j1939_t *s, uint32_t elapsed_us) {
	uint8_t i;

	for (i = 0U; i < s->ca_count; i++) {
		j1939_ca_t *ca = &s->ca[i];

		switch (ca->state) {
		case J1939_ADDR_STATE_UNCLAIMED:
			claim_start(s, ca);
			break;
		case J1939_ADDR_STATE_CLAIMING:
			if (timer_expired(ca, elapsed_us)) {
				ca->state = J1939_ADDR_STATE_CLAIMED;
			}
			break;
		case J1939_ADDR_STATE_CLAIMED:
			break;
		case J1939_ADDR_STATE_CANNOT_CLAIM:
			if (ca->cannot_claim_pending && timer_expired(ca, elapsed_us) &&
			    (claim_tx(s, ca, J1939_ADDR_NULL) == J1939_RET_OK)) {
				ca->cannot_claim_pending = false;
			}
			break;
		default:
			/* Corrupted state: stop transmitting and announce it. */
			cannot_claim_enter(ca);
			break;
		}
	}
}

bool j1939_addr_held(const j1939_t *s, uint8_t address) {
	bool found = false;
	uint8_t i;

	for (i = 0U; (!found) && (i < s->ca_count); i++) {
		found = held(&s->ca[i]) && (s->ca[i].address == address);
	}
	return found;
}

bool j1939_addr_claimed(const j1939_t *s, uint8_t address) {
	bool found = false;
	uint8_t i;

	for (i = 0U; (!found) && (i < s->ca_count); i++) {
		found = j1939_addr_tx_allowed(&s->ca[i]) && (s->ca[i].address == address);
	}
	return found;
}

bool j1939_addr_tx_allowed(const j1939_ca_t *ca) {
	return ca->state == J1939_ADDR_STATE_CLAIMED;
}

void j1939_addr_claim_handle(j1939_t *s, uint32_t id, const uint8_t *data, uint8_t len) {
	uint8_t sa = j1939_id_sa_get(id);

	/* Cannot Claim (source NULL) takes no address. */
	if ((len >= J1939_NAME_LEN) && (sa <= CA_ADDRESS_MAX)) {
		uint64_t name = 0U;
		uint8_t i;

		(void)j1939_name_from_bytes(data, &name);
		for (i = 0U; i < s->ca_count; i++) {
			j1939_ca_t *ca = &s->ca[i];

			/* The CA's own NAME is its own claim, e.g. from a driver with loopback. */
			if (held(ca) && (ca->address == sa) && (ca->name != name)) {
				if (j1939_name_compare(ca->name, name) < 0) {
					claim_repeat(s, ca);
				} else {
					taken_set(s, sa);
					address_lost(s, ca);
				}
			}
		}
		if (!j1939_addr_held(s, sa)) {
			taken_set(s, sa);
		}
	}
	if (j1939_stack_pgn_listed(s->rx_pgns, s->rx_pgns_len, J1939_PGN_ADDRESS_CLAIMED)) {
		j1939_stack_deliver(s, id, data, len);
	}
}

void j1939_addr_request_handle(j1939_t *s, uint8_t da) {
	uint8_t i;

	for (i = 0U; i < s->ca_count; i++) {
		j1939_ca_t *ca = &s->ca[i];

		if ((da == J1939_ADDR_GLOBAL) || (held(ca) && (ca->address == da))) {
			switch (ca->state) {
			case J1939_ADDR_STATE_UNCLAIMED:
				/* The claim goes out with the next j1939_process(). */
				break;
			case J1939_ADDR_STATE_CLAIMING:
			case J1939_ADDR_STATE_CLAIMED:
				claim_repeat(s, ca);
				break;
			case J1939_ADDR_STATE_CANNOT_CLAIM:
				if (!ca->cannot_claim_pending) {
					ca->cannot_claim_pending = true;
					ca->timer_us = cannot_claim_delay(ca->name);
				}
				break;
			default:
				cannot_claim_enter(ca);
				break;
			}
		}
	}
}

j1939_ret_t j1939_addr_request_send(j1939_t *s, j1939_ca_id_t ca, const j1939_msg_t *msg) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((s != NULL) && (ca < s->ca_count)) {
		const j1939_ca_t *c = &s->ca[ca];
		uint8_t sa = j1939_addr_tx_allowed(c) ? c->address : J1939_ADDR_NULL;

		ret = j1939_stack_send(s, msg, sa);
		if (ret == J1939_RET_OK) {
			/* The requester answers its own Request, as every other node does. */
			j1939_addr_request_handle(s, msg->da);
		}
	}
	return ret;
}

j1939_ret_t j1939_addr_get(const j1939_t *s, j1939_ca_id_t ca, uint8_t *address,
                           j1939_addr_state_t *state) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((s != NULL) && (address != NULL) && (state != NULL) && (ca < s->ca_count)) {
		const j1939_ca_t *c = &s->ca[ca];

		*address = held(c) ? c->address : J1939_ADDR_NULL;
		*state = c->state;
		ret = J1939_RET_OK;
	}
	return ret;
}
