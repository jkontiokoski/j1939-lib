/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/* A receive object follows its sender by NAME when the sender moves to another address. */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "j1939/j1939.h"
#include "test_bus.h"

#define TX_LEN    16U
#define MSG_LEN   2U
#define NAMES_LEN 4U

#define ADDR_PUMP    0x80U /* self-configurable */
#define ADDR_MONITOR 0x30U
#define NAME_PUMP    0x8000000000002000ULL /* arbitrary address capable */
#define NAME_RIVAL   0x1000U               /* lower NAME: wins ADDR_PUMP */
#define NAME_MONITOR 0x3000U

#define PGN_STATUS 0xFF10U /* 8 bytes every 100 ms */

#define PERIOD_US  100000U
#define TIMEOUT_US 300000U
#define STEP_US    10000U

typedef struct node {
	j1939_t s;
	j1939_ca_id_t ca;
	j1939_port_frame_t tx[TX_LEN];
	j1939_msg_slot_t msgs[MSG_LEN];
} node_t;

static node_t pump;
static node_t monitor;
static node_t rival;
static test_bus_t bus;
static j1939_names_entry_t names[NAMES_LEN];
static uint8_t status_buf[8];
static const j1939_rxobj_cfg_t obj_cfg = {status_buf, PGN_STATUS, TIMEOUT_US, 8U,
                                          8U,         0U,         NAME_PUMP};
static j1939_rxobj_t obj;
static uint32_t elapsed;
static uint8_t counter;

static void node_init(node_t *n, uint8_t address, uint64_t name) {
	const j1939_cfg_t cfg = {
	        .tx_buf = n->tx,
	        .tx_len = TX_LEN,
	        .msg_buf = n->msgs,
	        .msg_len = MSG_LEN,
	};

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&n->s, &cfg));
	TEST_ASSERT_EQUAL(
	        J1939_RET_OK,
	        j1939_ca_add(&n->s, &(j1939_ca_cfg_t){.address = address, .name = name}, &n->ca));
	test_bus_attach(&bus, &n->s);
}

/* The pump application: sends PGN_STATUS every PERIOD_US while it holds an address. */
static void pump_app(void) {
	if (elapsed >= PERIOD_US) {
		uint8_t d[8] = {counter, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU};
		const j1939_msg_t msg = {PGN_STATUS, 6U, 0U, J1939_ADDR_GLOBAL, 8U, d};

		elapsed = 0U;
		if (j1939_send(&pump.s, pump.ca, &msg) == J1939_RET_OK) {
			counter++;
		}
	}
}

static void steps(uint32_t count) {
	uint32_t i;
	uint8_t k;

	for (i = 0U; i < count; i++) {
		elapsed += STEP_US;
		pump_app();
		for (k = 0U; k < bus.count; k++) {
			TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(bus.nodes[k], STEP_US));
		}
		(void)test_bus_run(&bus);
	}
}

static j1939_rxobj_status_t status_of(void) {
	j1939_rxobj_status_t st;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_rxobj_get(&monitor.s, 0U, &st));
	return st;
}

static uint8_t pump_address(void) {
	j1939_addr_state_t state;
	uint8_t address;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_addr_get(&pump.s, pump.ca, &address, &state));
	TEST_ASSERT_EQUAL(J1939_ADDR_STATE_CLAIMED, state);
	return address;
}

void setUp(void) {
	test_bus_init(&bus);
	node_init(&pump, ADDR_PUMP, NAME_PUMP);
	node_init(&monitor, ADDR_MONITOR, NAME_MONITOR);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_names_init(&monitor.s, names, NAMES_LEN));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_rxobj_init(&monitor.s, &obj_cfg, &obj, 1U));
	elapsed = 0U;
	counter = 0U;
	(void)memset(status_buf, 0, sizeof(status_buf));
	steps(30U); /* claims, including the pump's 250 ms contention wait */
}

void tearDown(void) {
}

static void test_follows_sender_across_move(void) {
	uint8_t before;
	uint64_t name = 0U;

	steps(5U * PERIOD_US / STEP_US);
	TEST_ASSERT_EQUAL_HEX8(ADDR_PUMP, pump_address());
	TEST_ASSERT_EQUAL(J1939_RXOBJ_VALID, status_of().state);

	/* A node with a lower NAME takes the pump's address; the pump moves to another one. */
	node_init(&rival, ADDR_PUMP, NAME_RIVAL);
	steps(40U);
	TEST_ASSERT_NOT_EQUAL(ADDR_PUMP, pump_address());
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_names_name_get(&monitor.s, pump_address(), &name));
	TEST_ASSERT_EQUAL_HEX64(NAME_PUMP, name);

	/* The object keeps receiving the pump at its new address and never times out. */
	before = status_buf[0];
	steps(10U * PERIOD_US / STEP_US);
	TEST_ASSERT_EQUAL(J1939_RXOBJ_VALID, status_of().state);
	TEST_ASSERT_NOT_EQUAL(before, status_buf[0]);
	TEST_ASSERT_EQUAL_UINT8((uint8_t)(counter - 1U), status_buf[0]);
	TEST_ASSERT_EQUAL_UINT32(0U, j1939_stats_get(&monitor.s)->rxobj_timeout);
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_follows_sender_across_move);
	return UNITY_END();
}
