/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/* Request before claim on the test bus: a late joiner sees the taken addresses first. */

#include <stdbool.h>
#include <stdint.h>

#include "unity.h"

#include "j1939/j1939.h"
#include "test_bus.h"

#define TX_LEN    16U
#define MSG_LEN   2U
#define TABLE_LEN 4U
#define STEP_US   10000U

#define ARB         0x8000000000000000U
#define ADDR_FIXED  0x20U
#define ADDR_SELF   0x80U /* first self-configurable address */
#define NAME_FIXED  0x2000U
#define NAME_SELF   (ARB | 0x3000U)
#define NAME_JOINER (ARB | 0x1000U) /* lower than NAME_SELF: would win ADDR_SELF */

typedef struct node {
	j1939_t s;
	j1939_ca_id_t ca;
	j1939_port_frame_t tx[TX_LEN];
	j1939_msg_slot_t msgs[MSG_LEN];
} node_t;

static node_t fixed;
static node_t self;
static node_t joiner;
static test_bus_t bus;
static j1939_names_entry_t entries[TABLE_LEN];

static void node_init(node_t *n, uint8_t address, uint64_t name, bool preclaim) {
	const j1939_cfg_t cfg = {
	        .tx_buf = n->tx,
	        .tx_len = TX_LEN,
	        .msg_buf = n->msgs,
	        .msg_len = MSG_LEN,
	};

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&n->s, &cfg));
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_ca_add(&n->s,
	                               &(j1939_ca_cfg_t){.address = address,
	                                                 .name = name,
	                                                 .request_before_claim = preclaim},
	                               &n->ca));
}

static void steps(uint32_t count) {
	uint32_t i;
	uint8_t k;

	for (i = 0U; i < count; i++) {
		for (k = 0U; k < bus.count; k++) {
			TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(bus.nodes[k], STEP_US));
		}
		(void)test_bus_run(&bus);
	}
}

static void expect_state(const node_t *n, j1939_addr_state_t state, uint8_t address) {
	j1939_addr_state_t st = J1939_ADDR_STATE_UNCLAIMED;
	uint8_t a = 0U;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_addr_get(&n->s, n->ca, &a, &st));
	TEST_ASSERT_EQUAL(state, st);
	TEST_ASSERT_EQUAL_HEX8(address, a);
}

/* Two nodes have claimed their addresses before the joiner comes online. */
void setUp(void) {
	test_bus_init(&bus);
	node_init(&fixed, ADDR_FIXED, NAME_FIXED, false);
	node_init(&self, ADDR_SELF, NAME_SELF, false);
	test_bus_attach(&bus, &fixed.s);
	test_bus_attach(&bus, &self.s);
	steps(30U);
	expect_state(&self, J1939_ADDR_STATE_CLAIMED, ADDR_SELF);
}

void tearDown(void) {
}

static void test_joiner_with_preclaim_avoids_the_taken_address(void) {
	uint8_t address = 0U;

	node_init(&joiner, ADDR_SELF, NAME_JOINER, true);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_names_init(&joiner.s, entries, TABLE_LEN));
	test_bus_attach(&bus, &joiner.s);
	steps(60U);
	/* The joiner claimed the next free address; the running node kept its own. */
	expect_state(&joiner, J1939_ADDR_STATE_CLAIMED, ADDR_SELF + 1U);
	expect_state(&self, J1939_ADDR_STATE_CLAIMED, ADDR_SELF);
	expect_state(&fixed, J1939_ADDR_STATE_CLAIMED, ADDR_FIXED);
	TEST_ASSERT_EQUAL(2U, j1939_names_count(&joiner.s));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_names_address_get(&joiner.s, NAME_SELF, &address));
	TEST_ASSERT_EQUAL_HEX8(ADDR_SELF, address);
}

static void test_joiner_without_preclaim_takes_the_address_over(void) {
	node_init(&joiner, ADDR_SELF, NAME_JOINER, false);
	test_bus_attach(&bus, &joiner.s);
	steps(60U);
	/* Arbitration on the NAME: the joiner wins, the running node has to move. */
	expect_state(&joiner, J1939_ADDR_STATE_CLAIMED, ADDR_SELF);
	expect_state(&self, J1939_ADDR_STATE_CLAIMED, ADDR_SELF + 1U);
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_joiner_with_preclaim_avoids_the_taken_address);
	RUN_TEST(test_joiner_without_preclaim_takes_the_address_over);
	return UNITY_END();
}
