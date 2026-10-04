/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/* NAME table on the test bus: a late joiner learns three running nodes and follows a move. */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "j1939/j1939.h"
#include "test_bus.h"

#define TX_LEN    16U
#define MSG_LEN   2U
#define TABLE_LEN 4U
#define STEP_US   10000U

#define ADDR_A      0x20U
#define ADDR_B      0x21U
#define ADDR_C      0x22U
#define ADDR_B_NEW  0x30U
#define ADDR_JOINER 0x40U
#define ADDR_SELF   0x90U /* self-configurable: the joiner waits 250 ms before transmitting */
#define NAME_A      0x2000U
#define NAME_B      0x2100U
#define NAME_C      0x2200U
#define NAME_JOINER 0x4000U

typedef struct node {
	j1939_t s;
	j1939_ca_id_t ca;
	j1939_port_frame_t tx[TX_LEN];
	j1939_msg_slot_t msgs[MSG_LEN];
	j1939_tp_buf_t tp_tx[1];
	j1939_tp_buf_t tp_rx[1];
} node_t;

static node_t a;
static node_t b;
static node_t c;
static node_t joiner;
static test_bus_t bus;
static j1939_names_entry_t entries[TABLE_LEN];

static void node_init(node_t *n, uint8_t address, uint64_t name, bool accept_commanded) {
	const j1939_cfg_t cfg = {
	        .tx_buf = n->tx,
	        .tx_len = TX_LEN,
	        .msg_buf = n->msgs,
	        .msg_len = MSG_LEN,
	        .tp_tx_buf = n->tp_tx,
	        .tp_tx_buf_len = 1U,
	        .tp_rx_buf = n->tp_rx,
	        .tp_rx_buf_len = 1U,
	};

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&n->s, &cfg));
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_ca_add(&n->s,
	                               &(j1939_ca_cfg_t){.address = address,
	                                                 .name = name,
	                                                 .accept_commanded = accept_commanded},
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

static uint8_t joiner_address_of(uint64_t name) {
	uint8_t address = 0xA5U;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_names_address_get(&joiner.s, name, &address));
	return address;
}

/* Three nodes have claimed their addresses before the joiner comes online. */
static void running_nodes(void) {
	test_bus_init(&bus);
	node_init(&a, ADDR_A, NAME_A, false);
	node_init(&b, ADDR_B, NAME_B, true);
	node_init(&c, ADDR_C, NAME_C, false);
	test_bus_attach(&bus, &a.s);
	test_bus_attach(&bus, &b.s);
	test_bus_attach(&bus, &c.s);
	test_bus_settle(&bus, 4U);
}

static void joiner_online(uint8_t address) {
	node_init(&joiner, address, NAME_JOINER, false);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_names_init(&joiner.s, entries, TABLE_LEN));
	test_bus_attach(&bus, &joiner.s);
}

void setUp(void) {
	running_nodes();
}

void tearDown(void) {
}

static void test_late_joiner_learns_the_running_nodes(void) {
	uint64_t name = 0U;

	joiner_online(ADDR_JOINER);
	TEST_ASSERT_EQUAL(0U, j1939_names_count(&joiner.s));
	steps(2U);
	TEST_ASSERT_EQUAL(3U, j1939_names_count(&joiner.s));
	TEST_ASSERT_EQUAL_HEX8(ADDR_A, joiner_address_of(NAME_A));
	TEST_ASSERT_EQUAL_HEX8(ADDR_B, joiner_address_of(NAME_B));
	TEST_ASSERT_EQUAL_HEX8(ADDR_C, joiner_address_of(NAME_C));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_names_name_get(&joiner.s, ADDR_B, &name));
	TEST_ASSERT_EQUAL_UINT64(NAME_B, name);
	TEST_ASSERT_EQUAL(0U, j1939_stats_get(&joiner.s)->names_dropped);
}

static void test_joiner_follows_a_node_moved_by_commanded_address(void) {
	uint64_t name = 0U;
	uint16_t changes;

	joiner_online(ADDR_JOINER);
	steps(2U);
	changes = j1939_names_changes(&joiner.s);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_addr_command_send(&joiner.s, joiner.ca, NAME_B,
	                                                        ADDR_B_NEW, J1939_ADDR_GLOBAL));
	steps(30U); /* BAM: two packets at least 50 ms apart, then the move */
	TEST_ASSERT_EQUAL_HEX8(ADDR_B_NEW, joiner_address_of(NAME_B));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_EMPTY, j1939_names_name_get(&joiner.s, ADDR_B, &name));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_names_name_get(&joiner.s, ADDR_B_NEW, &name));
	TEST_ASSERT_EQUAL_UINT64(NAME_B, name);
	TEST_ASSERT_EQUAL(3U, j1939_names_count(&joiner.s));
	TEST_ASSERT_NOT_EQUAL(changes, j1939_names_changes(&joiner.s));
}

static void test_unknown_address_is_answered_before_the_joiner_claims(void) {
	uint64_t name = 0U;

	joiner_online(ADDR_SELF);
	steps(1U); /* the joiner claims ADDR_SELF and waits 250 ms before transmitting */
	TEST_ASSERT_EQUAL(J1939_RET_ERR_EMPTY, j1939_names_name_get(&joiner.s, ADDR_C, &name));
	steps(2U); /* Request from the NULL address, answer from C */
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_names_name_get(&joiner.s, ADDR_C, &name));
	TEST_ASSERT_EQUAL_UINT64(NAME_C, name);
	TEST_ASSERT_EQUAL(1U, j1939_names_count(&joiner.s));
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_late_joiner_learns_the_running_nodes);
	RUN_TEST(test_joiner_follows_a_node_moved_by_commanded_address);
	RUN_TEST(test_unknown_address_is_answered_before_the_joiner_claims);
	return UNITY_END();
}
