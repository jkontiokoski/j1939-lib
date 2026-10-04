/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/*
 * Transmit objects on the test bus: an ECU sends periodic, change-triggered and
 * request-only objects; a monitor and a tool receive them, the tool requests.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "j1939/j1939.h"
#include "test_bus.h"

#define TX_LEN  16U
#define MSG_LEN 8U
#define LONG    20U

#define ADDR_ECU     0x00U
#define ADDR_MONITOR 0x30U
#define ADDR_TOOL    0xF9U
#define NAME_ECU     0x500U
#define NAME_MONITOR 0x600U
#define NAME_TOOL    0x700U
#define NAME_WINNER  0x100U /* lower than NAME_ECU */

#define PGN_PERIODIC 0xFF20U
#define PGN_CHANGE   0xFF21U
#define PGN_REQUEST  0xFF22U

#define OBJ_PERIODIC 0U
#define OBJ_CHANGE   1U
#define OBJ_REQUEST  2U

#define STEP_US    10000U  /* process period */
#define PERIOD_US  100000U /* 10 steps */
#define INHIBIT_US 50000U  /* 5 steps */

typedef struct node {
	j1939_t s;
	j1939_ca_id_t ca;
	j1939_port_frame_t tx[TX_LEN];
	j1939_msg_slot_t msgs[MSG_LEN];
	j1939_tp_buf_t tp_tx[1];
	j1939_tp_buf_t tp_rx[1];
	uint32_t count[3]; /* messages received per object PGN */
	uint8_t last[3][LONG];
	uint8_t last_da[3];
} node_t;

static const uint32_t pgns[] = {PGN_PERIODIC, PGN_CHANGE, PGN_REQUEST};

static node_t ecu;
static node_t monitor;
static node_t tool;
static node_t winner;
static test_bus_t bus;
static uint8_t obj_buf[3][LONG];
static j1939_txobj_t obj[3];
static const j1939_txobj_cfg_t obj_cfg[3] = {
        {.buf = obj_buf[0],
         .pgn = PGN_PERIODIC,
         .period_us = PERIOD_US,
         .inhibit_us = 0U,
         .len = 8U,
         .prio = 3U,
         .da = J1939_ADDR_GLOBAL,
         .ca = 0U},
        {.buf = obj_buf[1],
         .pgn = PGN_CHANGE,
         .period_us = 0U,
         .inhibit_us = INHIBIT_US,
         .len = 8U,
         .prio = 6U,
         .da = J1939_ADDR_GLOBAL,
         .ca = 0U},
        {.buf = obj_buf[2],
         .pgn = PGN_REQUEST,
         .period_us = 0U,
         .inhibit_us = 0U,
         .len = LONG,
         .prio = 6U,
         .da = J1939_ADDR_GLOBAL,
         .ca = 0U},
};

static void node_init(node_t *n, uint8_t address, uint64_t name) {
	const j1939_cfg_t cfg = {
	        .tx_buf = n->tx,
	        .tx_len = TX_LEN,
	        .msg_buf = n->msgs,
	        .msg_len = MSG_LEN,
	        .rx_pgns = pgns,
	        .rx_pgns_len = 3U,
	        .tp_tx_buf = n->tp_tx,
	        .tp_tx_buf_len = 1U,
	        .tp_rx_buf = n->tp_rx,
	        .tp_rx_buf_len = 1U,
	};

	(void)memset(n->count, 0, sizeof(n->count));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&n->s, &cfg));
	TEST_ASSERT_EQUAL(
	        J1939_RET_OK,
	        j1939_ca_add(&n->s, &(j1939_ca_cfg_t){.address = address, .name = name}, &n->ca));
}

/* The receiving application: records each object PGN from the ECU. */
static void poll(node_t *n) {
	const j1939_msg_t *msg;

	while ((msg = j1939_msg_peek(&n->s)) != NULL) {
		uint32_t k;

		TEST_ASSERT_EQUAL_HEX8(ADDR_ECU, msg->sa);
		for (k = 0U; k < 3U; k++) {
			if (msg->pgn == pgns[k]) {
				TEST_ASSERT_EQUAL_UINT16(obj_cfg[k].len, msg->len);
				n->count[k]++;
				(void)memcpy(n->last[k], msg->data, msg->len);
				n->last_da[k] = msg->da;
			}
		}
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_msg_pop(&n->s));
	}
}

static void steps(uint32_t count) {
	uint32_t i;

	for (i = 0U; i < count; i++) {
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(&ecu.s, STEP_US));
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(&monitor.s, STEP_US));
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(&tool.s, STEP_US));
		(void)test_bus_run(&bus);
		poll(&monitor);
		poll(&tool);
	}
}

void setUp(void) {
	test_bus_init(&bus);
	node_init(&ecu, ADDR_ECU, NAME_ECU);
	node_init(&monitor, ADDR_MONITOR, NAME_MONITOR);
	node_init(&tool, ADDR_TOOL, NAME_TOOL);
	test_bus_attach(&bus, &ecu.s);
	test_bus_attach(&bus, &monitor.s);
	test_bus_attach(&bus, &tool.s);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_txobj_init(&ecu.s, obj_cfg, obj, 3U));
}

void tearDown(void) {
}

static void test_periodic_and_change_objects_reach_the_receivers(void) {
	const uint8_t speed[8] = {0x11U, 0x22U, 0x33U, 0x44U, 0xFFU, 0xFFU, 0xFFU, 0xFFU};
	uint8_t state[8] = {0U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU};
	uint32_t i;

	/* One second: the periodic object on the claim and every 100 ms, the change object once. */
	steps(100U);
	TEST_ASSERT_EQUAL_UINT32(10U, monitor.count[OBJ_PERIODIC]);
	TEST_ASSERT_EQUAL_UINT32(1U, monitor.count[OBJ_CHANGE]);
	TEST_ASSERT_EQUAL_UINT32(0U, monitor.count[OBJ_REQUEST]);
	TEST_ASSERT_EQUAL_HEX8(0xFFU, monitor.last[OBJ_PERIODIC][0]);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_txobj_set(&ecu.s, OBJ_PERIODIC, speed, 8U));
	steps(10U);
	TEST_ASSERT_EQUAL_UINT32(11U, monitor.count[OBJ_PERIODIC]);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(speed, monitor.last[OBJ_PERIODIC], 8U);
	/* A change every step for 100 ms: at most one send per 50 ms. */
	for (i = 0U; i < 10U; i++) {
		state[0] = (uint8_t)(i + 1U);
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_txobj_set(&ecu.s, OBJ_CHANGE, state, 8U));
		steps(1U);
	}
	steps(10U);
	TEST_ASSERT_EQUAL_UINT32(4U, monitor.count[OBJ_CHANGE]);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(state, monitor.last[OBJ_CHANGE], 8U);
	TEST_ASSERT_EQUAL_UINT32(monitor.count[OBJ_CHANGE], tool.count[OBJ_CHANGE]);
	TEST_ASSERT_EQUAL_UINT32(0U, j1939_stats_get(&ecu.s)->txobj_tx_dropped);
}

static void test_tool_requests_a_request_only_object(void) {
	uint8_t ident[LONG];
	uint32_t i;

	for (i = 0U; i < LONG; i++) {
		ident[i] = (uint8_t)('A' + i);
	}
	steps(1U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_txobj_set(&ecu.s, OBJ_REQUEST, ident, LONG));
	/* Destination specific: RTS/CTS to the tool only. */
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_request_send(&tool.s, tool.ca, PGN_REQUEST, ADDR_ECU));
	steps(10U);
	TEST_ASSERT_EQUAL_UINT32(1U, tool.count[OBJ_REQUEST]);
	TEST_ASSERT_EQUAL_HEX8(ADDR_TOOL, tool.last_da[OBJ_REQUEST]);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(ident, tool.last[OBJ_REQUEST], LONG);
	TEST_ASSERT_EQUAL_UINT32(0U, monitor.count[OBJ_REQUEST]);
	/* Global: BAM to everybody. */
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_request_send(&tool.s, tool.ca, PGN_REQUEST, J1939_ADDR_GLOBAL));
	steps(30U);
	TEST_ASSERT_EQUAL_UINT32(2U, tool.count[OBJ_REQUEST]);
	TEST_ASSERT_EQUAL_HEX8(J1939_ADDR_GLOBAL, tool.last_da[OBJ_REQUEST]);
	TEST_ASSERT_EQUAL_UINT32(1U, monitor.count[OBJ_REQUEST]);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(ident, monitor.last[OBJ_REQUEST], LONG);
}

static void test_objects_stop_when_the_ecu_loses_its_address(void) {
	uint32_t before;

	steps(50U);
	before = monitor.count[OBJ_PERIODIC];
	TEST_ASSERT_EQUAL_UINT32(5U, before);
	/* A node with a lower NAME claims the ECU's address; the ECU cannot claim another. */
	node_init(&winner, ADDR_ECU, NAME_WINNER);
	test_bus_attach(&bus, &winner.s);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(&winner.s, 0U));
	steps(100U);
	TEST_ASSERT_LESS_OR_EQUAL_UINT32(before + 1U, monitor.count[OBJ_PERIODIC]);
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_request_send(&tool.s, tool.ca, PGN_REQUEST, J1939_ADDR_GLOBAL));
	steps(30U);
	TEST_ASSERT_EQUAL_UINT32(0U, tool.count[OBJ_REQUEST]);
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_periodic_and_change_objects_reach_the_receivers);
	RUN_TEST(test_tool_requests_a_request_only_object);
	RUN_TEST(test_objects_stop_when_the_ecu_loses_its_address);
	return UNITY_END();
}
