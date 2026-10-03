/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/* Commanded Address between nodes on the test bus: a tool moves a target, a monitor watches. */

#include <stdbool.h>
#include <stdint.h>

#include "unity.h"

#include "j1939/j1939.h"
#include "test_bus.h"

#define LEN     32U
#define BUF_LEN 1U
#define STEP_US 10000U /* process period */

#define ADDR_TOOL   0x10U
#define ADDR_TARGET 0x20U
#define NAME_TOOL   0x100U
#define NAME_TARGET 0x200U

typedef struct node {
	j1939_t s;
	j1939_ca_id_t ca;
	j1939_port_frame_t tx[LEN];
	j1939_msg_slot_t msgs[LEN];
	j1939_tp_buf_t tp_tx[BUF_LEN];
	j1939_tp_buf_t tp_rx[BUF_LEN];
} node_t;

static const uint32_t claim_pgns[] = {J1939_PGN_ADDRESS_CLAIMED};

static node_t tool;
static node_t target;
static node_t monitor; /* no CA: receives every Address Claimed */
static test_bus_t bus;

static void node_init(node_t *n, bool is_monitor) {
	const j1939_cfg_t cfg = {
	        .tx_buf = n->tx,
	        .tx_len = LEN,
	        .msg_buf = n->msgs,
	        .msg_len = LEN,
	        .rx_pgns = is_monitor ? claim_pgns : NULL,
	        .rx_pgns_len = is_monitor ? 1U : 0U,
	        .tp_tx_buf = n->tp_tx,
	        .tp_tx_buf_len = BUF_LEN,
	        .tp_rx_buf = n->tp_rx,
	        .tp_rx_buf_len = BUF_LEN,
	};

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&n->s, &cfg));
	test_bus_attach(&bus, &n->s);
}

static void ca_add(node_t *n, uint8_t address, uint64_t name, bool accept) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_ca_add(&n->s,
	                                             &(j1939_ca_cfg_t){.address = address,
	                                                               .name = name,
	                                                               .accept_commanded = accept},
	                                             &n->ca));
}

/* Advances every node's time by elapsed_us, then lets the bus settle. */
static void advance(uint32_t elapsed_us) {
	uint8_t i;

	for (i = 0U; i < bus.count; i++) {
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(bus.nodes[i], elapsed_us));
	}
	(void)test_bus_run(&bus);
	test_bus_settle(&bus, 16U);
	TEST_ASSERT_EQUAL_UINT32(0U, test_bus_run(&bus));
}

static void steps(uint32_t count) {
	uint32_t i;

	for (i = 0U; i < count; i++) {
		advance(STEP_US);
	}
}

static void expect(const node_t *n, j1939_addr_state_t state, uint8_t address) {
	j1939_addr_state_t st = J1939_ADDR_STATE_UNCLAIMED;
	uint8_t addr = 0U;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_addr_get(&n->s, n->ca, &addr, &st));
	TEST_ASSERT_EQUAL(state, st);
	TEST_ASSERT_EQUAL_HEX8(address, addr);
}

/* Pops the monitor's next message and checks it is an Address Claimed from sa with name. */
static void monitor_expect(uint8_t sa, uint64_t name) {
	const j1939_msg_t *msg = j1939_msg_peek(&monitor.s);
	uint64_t got = 0U;

	TEST_ASSERT_NOT_NULL_MESSAGE(msg, "monitor saw no Address Claimed");
	TEST_ASSERT_EQUAL_HEX32(J1939_PGN_ADDRESS_CLAIMED, msg->pgn);
	TEST_ASSERT_EQUAL_HEX8(sa, msg->sa);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_name_from_bytes(msg->data, &got));
	TEST_ASSERT_EQUAL_HEX64(name, got);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_msg_pop(&monitor.s));
}

static void monitor_expect_none(void) {
	TEST_ASSERT_NULL(j1939_msg_peek(&monitor.s));
}

static j1939_ret_t command(uint64_t name, uint8_t address, uint8_t da) {
	return j1939_addr_command_send(&tool.s, tool.ca, name, address, da);
}

static void expect_no_tp_errors(void) {
	uint8_t i;

	for (i = 0U; i < bus.count; i++) {
		const j1939_stats_t *st = j1939_stats_get(bus.nodes[i]);

		TEST_ASSERT_EQUAL_UINT32(0U, st->tp_tx_aborted);
		TEST_ASSERT_EQUAL_UINT32(0U, st->tp_rx_aborted);
		TEST_ASSERT_EQUAL_UINT32(0U, st->tp_rx_refused);
		TEST_ASSERT_EQUAL_UINT32(0U, st->tx_overflow);
	}
}

void setUp(void) {
	test_bus_init(&bus);
	node_init(&tool, false);
	node_init(&target, false);
	node_init(&monitor, true);
	ca_add(&tool, ADDR_TOOL, NAME_TOOL, false);
	ca_add(&target, ADDR_TARGET, NAME_TARGET, true);
	advance(0U);
	expect(&tool, J1939_ADDR_STATE_CLAIMED, ADDR_TOOL);
	expect(&target, J1939_ADDR_STATE_CLAIMED, ADDR_TARGET);
	monitor_expect(ADDR_TOOL, NAME_TOOL);
	monitor_expect(ADDR_TARGET, NAME_TARGET);
	monitor_expect_none();
}

void tearDown(void) {
}

static void test_broadcast_command_moves_target(void) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, command(NAME_TARGET, 0x30U, J1939_ADDR_GLOBAL));
	steps(15U); /* BAM: two packets 50 ms apart */
	expect(&target, J1939_ADDR_STATE_CLAIMED, 0x30U);
	monitor_expect(0x30U, NAME_TARGET);
	monitor_expect_none();
	expect(&tool, J1939_ADDR_STATE_CLAIMED, ADDR_TOOL);
	expect_no_tp_errors();

	/* The target transmits from its new address only. */
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_request_send(&tool.s, tool.ca,
	                                                   J1939_PGN_ADDRESS_CLAIMED, ADDR_TARGET));
	steps(1U);
	monitor_expect_none();
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_request_send(&tool.s, tool.ca, J1939_PGN_ADDRESS_CLAIMED, 0x30U));
	steps(1U);
	monitor_expect(0x30U, NAME_TARGET);
	monitor_expect_none();
}

static void test_connection_mode_command_moves_target(void) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, command(NAME_TARGET, 0x81U, ADDR_TARGET));
	steps(2U); /* RTS, CTS, data, EndOfMsgAck, then the claim */
	expect(&target, J1939_ADDR_STATE_CLAIMING, 0x81U);
	monitor_expect(0x81U, NAME_TARGET);
	monitor_expect_none();
	TEST_ASSERT_EQUAL_UINT8(0U, tool.s.tp.sessions[0].state);

	advance(J1939_ADDR_CLAIM_WAIT_US);
	expect(&target, J1939_ADDR_STATE_CLAIMED, 0x81U);
	monitor_expect_none();
	expect_no_tp_errors();

	/* A second command to the new address moves it back. */
	TEST_ASSERT_EQUAL(J1939_RET_OK, command(NAME_TARGET, ADDR_TARGET, 0x81U));
	steps(2U);
	expect(&target, J1939_ADDR_STATE_CLAIMED, ADDR_TARGET);
	monitor_expect(ADDR_TARGET, NAME_TARGET);
	monitor_expect_none();
	expect_no_tp_errors();
}

/* Commanded onto the tool's address, the target loses to the lower NAME and cannot claim;
 * a second command gives it an address again. */
static void test_contention_after_command(void) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, command(NAME_TARGET, ADDR_TOOL, J1939_ADDR_GLOBAL));
	steps(15U);
	expect(&target, J1939_ADDR_STATE_CANNOT_CLAIM, J1939_ADDR_NULL);
	expect(&tool, J1939_ADDR_STATE_CLAIMED, ADDR_TOOL);
	monitor_expect(ADDR_TOOL, NAME_TARGET);
	monitor_expect(ADDR_TOOL, NAME_TOOL);         /* the tool defends its address */
	monitor_expect(J1939_ADDR_NULL, NAME_TARGET); /* after a delay of 1.2 ms */
	monitor_expect_none();

	TEST_ASSERT_EQUAL(J1939_RET_OK, command(NAME_TARGET, 0x31U, J1939_ADDR_GLOBAL));
	steps(15U);
	expect(&target, J1939_ADDR_STATE_CLAIMED, 0x31U);
	monitor_expect(0x31U, NAME_TARGET);
	monitor_expect_none();
}

/* A node that does not accept commands keeps its address. */
static void test_refusing_node_keeps_address(void) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, command(NAME_TOOL, 0x30U, J1939_ADDR_GLOBAL));
	steps(15U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, command(NAME_TARGET + 1U, 0x30U, J1939_ADDR_GLOBAL));
	steps(15U);
	expect(&target, J1939_ADDR_STATE_CLAIMED, ADDR_TARGET);
	monitor_expect_none();
	expect_no_tp_errors();
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_broadcast_command_moves_target);
	RUN_TEST(test_connection_mode_command_moves_target);
	RUN_TEST(test_contention_after_command);
	RUN_TEST(test_refusing_node_keeps_address);
	return UNITY_END();
}
