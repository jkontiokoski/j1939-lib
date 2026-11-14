/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_addr.c
 * @brief Address claiming (J1939/81): claim state of each CA, arbitration on
 *        the NAME, Cannot Claim and Commanded Address.
 */

#include "j1939/j1939_addr.h"

#include <stddef.h>
#include <string.h>

#include "j1939/j1939_id.h"
#include "j1939/j1939_name.h"
#include "j1939/j1939_request.h"
#include "j1939_internal.h"

#define CLAIM_PRIO    6U /**< Priority of Address Claimed and Cannot Claim. */
#define BITS_PER_BYTE 8U /**< Bits per byte of the taken-address bitmap. */

/** Tells whether an address is in the self-configurable range, 128..247. */
static bool self_cfg(uint8_t address) {
	return (address >= J1939_ADDR_SELF_CFG_MIN) && (address <= J1939_ADDR_SELF_CFG_MAX);
}

/** Tells whether a CA holds its address: CLAIMING or CLAIMED. */
static bool held(const j1939_ca_t *ca) {
	return (ca->state == J1939_ADDR_STATE_CLAIMING) || (ca->state == J1939_ADDR_STATE_CLAIMED);
}

/** Records a self-configurable address claimed by another node; other ranges are not recorded. */
static void taken_set(j1939_t *s, uint8_t address) {
	if (self_cfg(address)) {
		uint32_t bit = (uint32_t)address - J1939_ADDR_SELF_CFG_MIN;

		s->addr_taken[bit / BITS_PER_BYTE] |= (uint8_t)(1U << (bit % BITS_PER_BYTE));
	}
}

/**
 * Tells whether another node has claimed a self-configurable address, as recorded with taken_set().
 * The address must be in the self-configurable range.
 */
static bool taken_get(const j1939_t *s, uint8_t address) {
	uint32_t bit = (uint32_t)address - J1939_ADDR_SELF_CFG_MIN;

	return (s->addr_taken[bit / BITS_PER_BYTE] & (uint8_t)(1U << (bit % BITS_PER_BYTE))) != 0U;
}

/**
 * Chooses an address for an arbitrary address capable CA that lost its own: the first
 * self-configurable address neither taken by another node nor used by a CA of the stack, or
 * J1939_ADDR_NULL if none is free.
 */
static uint8_t address_select(const j1939_t *s) {
	uint8_t found = J1939_ADDR_NULL;
	uint32_t a;

	for (a = J1939_ADDR_SELF_CFG_MIN;
	     (found == J1939_ADDR_NULL) && (a <= J1939_ADDR_SELF_CFG_MAX); a++) {
		if (!taken_get(s, (uint8_t)a) && !j1939_addr_in_use(s, (uint8_t)a)) {
			found = (uint8_t)a;
		}
	}
	return found;
}

/**
 * Computes the pseudo-random Cannot Claim delay of a CA, in microseconds: 0..255 steps of
 * J1939_ADDR_CANNOT_CLAIM_STEP_US.
 *
 * The step count is the XOR of the NAME's bytes, so that CAs with different NAMEs usually spread
 * without a random source.
 */
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

/**
 * Starts the contention wait or the Cannot Claim delay.
 *
 * The timer counts from the next j1939_process(), also when a received frame or an API call starts
 * it between two calls.
 */
static void timer_start(j1939_ca_t *ca, uint32_t timeout_us) {
	ca->timer_us = timeout_us;
	ca->timer_fresh = true;
}

/**
 * Queues Address Claimed with the CA's NAME from source address sa; J1939_ADDR_NULL makes it Cannot
 * Claim. Returns J1939_RET_ERR_FULL if the tx queue is full.
 */
static j1939_ret_t claim_tx(j1939_t *s, const j1939_ca_t *ca, uint8_t sa) {
	uint8_t data[J1939_NAME_LEN];

	(void)j1939_name_to_bytes(ca->name, data);
	return j1939_stack_tx(s, CLAIM_PRIO, J1939_PGN_ADDRESS_CLAIMED, J1939_ADDR_GLOBAL, sa, data,
	                      (uint8_t)J1939_NAME_LEN);
}

/**
 * Sends the Address Claimed of a CA that holds its address again: its defence in a contention, or
 * its answer to a Request for Address Claimed.
 *
 * With the tx queue full it stays pending and j1939_addr_process() sends it, so that the other node
 * always learns that the address is held.
 */
static void claim_repeat(j1939_t *s, j1939_ca_t *ca) {
	ca->claim_pending = claim_tx(s, ca, ca->address) != J1939_RET_OK;
}

/** Sends an Address Claimed that found the tx queue full when it was due. */
static void claim_retry(j1939_t *s, j1939_ca_t *ca) {
	if (ca->claim_pending) {
		claim_repeat(s, ca);
	}
}

/**
 * Sends the first Address Claimed of an UNCLAIMED CA for ca->address.
 *
 * Outside the self-configurable range the CA is claimed at once; inside it the contention wait
 * starts. The CA stays unclaimed if the tx queue is full.
 */
static void claim_start(j1939_t *s, j1939_ca_t *ca) {
	ca->claim_pending = false;
	if (claim_tx(s, ca, ca->address) == J1939_RET_OK) {
		if (self_cfg(ca->address)) {
			ca->state = J1939_ADDR_STATE_CLAIMING;
			timer_start(ca, J1939_ADDR_CLAIM_WAIT_US);
		} else {
			ca->state = J1939_ADDR_STATE_CLAIMED;
		}
	}
}

/** Gives up the CA's address: the transport protocol sessions of that address end. */
static void address_release(j1939_t *s, j1939_ca_t *ca) {
	if (ca->address != J1939_ADDR_NULL) {
		j1939_tp_address_lost(s, ca->address);
	}
	ca->address = J1939_ADDR_NULL;
	ca->claim_pending = false;
}

/** Gives up the address and schedules a Cannot Claim. */
static void cannot_claim_enter(j1939_t *s, j1939_ca_t *ca) {
	address_release(s, ca);
	ca->state = J1939_ADDR_STATE_CANNOT_CLAIM;
	ca->cannot_claim_pending = true;
	timer_start(ca, cannot_claim_delay(ca->name));
}

/**
 * Handles the loss of the CA's address to a NAME of higher priority.
 *
 * An arbitrary address capable CA claims a free self-configurable address; otherwise, or without a
 * free one, it goes to CANNOT_CLAIM.
 */
static void address_lost(j1939_t *s, j1939_ca_t *ca) {
	uint8_t next = J1939_ADDR_NULL;

	if (j1939_name_arbitrary_address(ca->name)) {
		next = address_select(s);
	}
	if (next != J1939_ADDR_NULL) {
		address_release(s, ca);
		ca->address = next;
		ca->state = J1939_ADDR_STATE_UNCLAIMED;
		claim_start(s, ca);
	} else {
		cannot_claim_enter(s, ca);
	}
}

/**
 * Handles a corrupted claim state: stop transmitting, drop a pending command and announce Cannot
 * Claim.
 */
static void fail_safe(j1939_t *s, j1939_ca_t *ca) {
	ca->commanded = J1939_ADDR_NULL;
	cannot_claim_enter(s, ca);
}

/**
 * Applies a pending Commanded Address.
 *
 * A command to the address the CA holds repeats its Address Claimed; a command to an address
 * another CA of the stack uses is refused. Returns true if the CA gave up its address for the
 * commanded one; its claim then starts as after a loss.
 */
static bool command_apply(j1939_t *s, j1939_ca_t *ca) {
	uint8_t next = ca->commanded;
	bool moved = false;

	ca->commanded = J1939_ADDR_NULL;
	if (next > J1939_ADDR_MAX) {
		/* No command pending. */
	} else if (ca->address == next) {
		/* Already there: announce it again; an unclaimed CA claims it anyway. */
		if (held(ca)) {
			claim_repeat(s, ca);
		}
	} else if (j1939_addr_in_use(s, next)) {
		/* Another CA of the stack uses the address: refused. */
	} else {
		address_release(s, ca);
		ca->address = next;
		ca->state = J1939_ADDR_STATE_UNCLAIMED;
		ca->cannot_claim_pending = false;
		claim_start(s, ca);
		moved = true;
	}
	return moved;
}

/**
 * Counts the CA's timer down and returns true once it has run out. A timer started since the
 * previous call waits.
 */
static bool timer_expired(j1939_ca_t *ca, uint32_t elapsed_us) {
	bool expired = false;

	if (!ca->timer_fresh) {
		expired = ca->timer_us <= elapsed_us;
		ca->timer_us = expired ? 0U : (ca->timer_us - elapsed_us);
	}
	return expired;
}

/**
 * Ends the wait of a CA in REQUESTING and starts its claim.
 *
 * An accepted Commanded Address is claimed as commanded. Otherwise the CA claims its preferred
 * address, or a free self-configurable address instead if another node claimed the preferred one
 * and the NAME is arbitrary address capable.
 */
static void preclaim_end(j1939_t *s, j1939_ca_t *ca) {
	ca->state = J1939_ADDR_STATE_UNCLAIMED;
	if (!command_apply(s, ca)) {
		if (ca->preferred_taken && j1939_name_arbitrary_address(ca->name)) {
			uint8_t next = address_select(s);

			if (next != J1939_ADDR_NULL) {
				ca->address = next;
			}
		}
		claim_start(s, ca);
	}
}

/** Runs the REQUESTING state of a CA: sends its Request, then waits. */
static void preclaim_tick(j1939_t *s, j1939_ca_id_t id, uint32_t elapsed_us) {
	j1939_ca_t *ca = &s->ca[id];

	if (!ca->preclaim_sent) {
		/* The CA has no address: the Request goes out from J1939_ADDR_NULL, and the stack's
		 * own CAs answer it as every other node does. */
		if (j1939_addr_request_send(s, id, J1939_ADDR_GLOBAL) == J1939_RET_OK) {
			ca->preclaim_sent = true;
			timer_start(ca, J1939_ADDR_PRECLAIM_WAIT_US);
		}
	} else if (timer_expired(ca, elapsed_us)) {
		preclaim_end(s, ca);
	} else {
		/* Collecting the answers. */
	}
}

void j1939_addr_init(j1939_t *s) {
	(void)memset(s->addr_taken, 0, sizeof(s->addr_taken));
}

void j1939_addr_ca_init(j1939_ca_t *ca, const j1939_ca_cfg_t *cfg) {
	ca->name = cfg->name;
	ca->state = cfg->request_before_claim ? J1939_ADDR_STATE_REQUESTING
	                                      : J1939_ADDR_STATE_UNCLAIMED;
	ca->timer_us = 0U;
	ca->timer_fresh = false;
	ca->address = cfg->address;
	ca->cannot_claim_pending = false;
	ca->claim_pending = false;
	ca->accept_commanded = cfg->accept_commanded;
	ca->commanded = J1939_ADDR_NULL;
	ca->preclaim_sent = false;
	ca->preferred_taken = false;
}

void j1939_addr_process(j1939_t *s, uint32_t elapsed_us) {
	uint8_t i;

	for (i = 0U; i < s->ca_count; i++) {
		j1939_ca_t *ca = &s->ca[i];

		switch (ca->state) {
		case J1939_ADDR_STATE_UNCLAIMED:
			if (!command_apply(s, ca)) {
				claim_start(s, ca);
			}
			break;
		case J1939_ADDR_STATE_CLAIMING:
			if (!command_apply(s, ca)) {
				claim_retry(s, ca);
				if (timer_expired(ca, elapsed_us)) {
					ca->state = J1939_ADDR_STATE_CLAIMED;
				}
			}
			break;
		case J1939_ADDR_STATE_CLAIMED:
			if (!command_apply(s, ca)) {
				claim_retry(s, ca);
			}
			break;
		case J1939_ADDR_STATE_REQUESTING:
			preclaim_tick(s, i, elapsed_us);
			break;
		case J1939_ADDR_STATE_CANNOT_CLAIM:
			if (!command_apply(s, ca) && ca->cannot_claim_pending &&
			    timer_expired(ca, elapsed_us) &&
			    (claim_tx(s, ca, J1939_ADDR_NULL) == J1939_RET_OK)) {
				ca->cannot_claim_pending = false;
			}
			break;
		default:
			/* Corrupted state: stop transmitting and announce it. */
			fail_safe(s, ca);
			break;
		}
		/* A timer started before or during this call counts from the next one. */
		ca->timer_fresh = false;
	}
}

bool j1939_addr_in_use(const j1939_t *s, uint8_t address) {
	bool found = false;
	uint8_t i;

	for (i = 0U; (!found) && (i < s->ca_count); i++) {
		found = s->ca[i].address == address;
	}
	return found;
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
		found = j1939_addr_ca_claimed(&s->ca[i]) && (s->ca[i].address == address);
	}
	return found;
}

bool j1939_addr_ca_claimed(const j1939_ca_t *ca) {
	return ca->state == J1939_ADDR_STATE_CLAIMED;
}

bool j1939_addr_ca_addressed(const j1939_ca_t *ca, uint8_t da) {
	return (da == J1939_ADDR_GLOBAL) || (held(ca) && (ca->address == da));
}

void j1939_addr_claim_handle(j1939_t *s, const j1939_msg_t *msg) {
	uint8_t sa = msg->sa;

	if ((msg->len >= J1939_NAME_LEN) && (sa == J1939_ADDR_NULL)) {
		uint64_t name = 0U;

		/* Cannot Claim takes no address; the NAME table records the node. */
		(void)j1939_name_from_bytes(msg->data, &name);
		j1939_nametab_cannot_claim(s, name);
	} else if ((msg->len >= J1939_NAME_LEN) && (sa <= J1939_ADDR_MAX)) {
		uint64_t name = 0U;
		uint8_t i;

		(void)j1939_name_from_bytes(msg->data, &name);
		for (i = 0U; i < s->ca_count; i++) {
			j1939_ca_t *ca = &s->ca[i];

			/* The CA's own NAME is its own claim, e.g. from a driver with loopback. */
			if ((ca->state == J1939_ADDR_STATE_REQUESTING) && (ca->address == sa) &&
			    (ca->name != name)) {
				ca->preferred_taken = true;
			} else if (held(ca) && (ca->address == sa) && (ca->name != name)) {
				if (j1939_name_compare(ca->name, name) < 0) {
					claim_repeat(s, ca);
				} else {
					taken_set(s, sa);
					address_lost(s, ca);
				}
			} else {
				/* Another address, or the CA's own claim. */
			}
		}
		if (!j1939_addr_held(s, sa)) {
			taken_set(s, sa);
			j1939_nametab_claimed(s, name, sa);
		}
	} else {
		/* Too short, or from the global address: no claim. */
	}
	if (j1939_stack_pgn_listed(s->rx_pgns, s->rx_pgns_len, J1939_PGN_ADDRESS_CLAIMED)) {
		(void)j1939_stack_deliver(s, msg);
	}
}

bool j1939_addr_command_accepted(const j1939_t *s) {
	bool found = false;
	uint8_t i;

	for (i = 0U; (!found) && (i < s->ca_count); i++) {
		found = s->ca[i].accept_commanded;
	}
	return found;
}

void j1939_addr_command_handle(j1939_t *s, const j1939_msg_t *msg) {
	if (msg->len >= J1939_ADDR_COMMAND_LEN) {
		uint64_t name = 0U;
		uint8_t next = msg->data[J1939_NAME_LEN];
		uint8_t i;

		(void)j1939_name_from_bytes(msg->data, &name);
		for (i = 0U; i < s->ca_count; i++) {
			j1939_ca_t *ca = &s->ca[i];

			/* 254 and 255 are not addresses a CA can claim. */
			if (ca->accept_commanded && (ca->name == name) &&
			    (next <= J1939_ADDR_MAX)) {
				ca->commanded = next;
			}
		}
	}
}

void j1939_addr_request_handle(j1939_t *s, uint8_t da) {
	uint8_t i;

	for (i = 0U; i < s->ca_count; i++) {
		j1939_ca_t *ca = &s->ca[i];

		if (j1939_addr_ca_addressed(ca, da)) {
			switch (ca->state) {
			case J1939_ADDR_STATE_UNCLAIMED:
				/* The claim goes out with the next j1939_process(). */
				break;
			case J1939_ADDR_STATE_REQUESTING:
				/* No address yet: nothing to answer with. */
				break;
			case J1939_ADDR_STATE_CLAIMING:
			case J1939_ADDR_STATE_CLAIMED:
				claim_repeat(s, ca);
				break;
			case J1939_ADDR_STATE_CANNOT_CLAIM:
				if (!ca->cannot_claim_pending) {
					ca->cannot_claim_pending = true;
					timer_start(ca, cannot_claim_delay(ca->name));
				}
				break;
			default:
				fail_safe(s, ca);
				break;
			}
		}
	}
}

j1939_ret_t j1939_addr_request_send(j1939_t *s, j1939_ca_id_t ca, uint8_t da) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((s != NULL) && (ca < s->ca_count)) {
		const j1939_ca_t *c = &s->ca[ca];
		uint8_t sa = j1939_addr_ca_claimed(c) ? c->address : J1939_ADDR_NULL;

		ret = j1939_request_tx(s, sa, da, J1939_PGN_ADDRESS_CLAIMED);
		if (ret == J1939_RET_OK) {
			/* The requester answers its own Request, as every other node does. */
			j1939_addr_request_handle(s, da);
			if (da == J1939_ADDR_GLOBAL) {
				j1939_nametab_global_request(s);
			}
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

j1939_ret_t j1939_addr_command_send(j1939_t *s, j1939_ca_id_t ca, uint64_t name, uint8_t address,
                                    uint8_t da) {
	j1939_ret_t ret = J1939_RET_ERR_ARG;

	if ((address <= J1939_ADDR_MAX) && (da != J1939_ADDR_NULL)) {
		uint8_t data[J1939_ADDR_COMMAND_LEN];
		j1939_msg_t msg;

		(void)j1939_name_to_bytes(name, data);
		data[J1939_NAME_LEN] = address;
		msg.pgn = J1939_PGN_COMMANDED_ADDRESS;
		msg.prio = J1939_PRIO_DEFAULT;
		msg.sa = 0U;
		msg.da = da;
		msg.len = (uint16_t)J1939_ADDR_COMMAND_LEN;
		msg.data = data;
		/* The transport protocol copies the payload; it needs a claimed sender. */
		ret = j1939_send(s, ca, &msg);
	}
	return ret;
}
