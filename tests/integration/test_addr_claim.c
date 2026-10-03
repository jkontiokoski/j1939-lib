/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/* Address claiming between nodes on the test bus. */

#include "unity.h"

#include "j1939/j1939.h"
#include "test_bus.h"

#define LEN 16U

#define ARB 0x8000000000000000U

typedef struct node {
	j1939_t s;
	j1939_ca_id_t ca;
	j1939_port_frame_t tx[LEN];
	j1939_msg_slot_t msgs[LEN];
} node_t;

static const uint32_t claim_pgns[] = {J1939_PGN_ADDRESS_CLAIMED};

static node_t a;
static node_t b;
static node_t c;
static node_t m; /* monitor: no CA, receives every Address Claimed */
static test_bus_t bus;

static void node_init(node_t *n, bool monitor) {
	const j1939_cfg_t cfg = {
	        .tx_buf = n->tx,
	        .tx_len = LEN,
	        .msg_buf = n->msgs,
	        .msg_len = LEN,
	        .rx_pgns = monitor ? claim_pgns : NULL,
	        .rx_pgns_len = monitor ? 1U : 0U,
	        .req_pgns = NULL,
	        .req_pgns_len = 0U,
	};

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&n->s, &cfg));
	test_bus_attach(&bus, &n->s);
}

static void ca_add(node_t *n, uint8_t address, uint64_t name) {
	TEST_ASSERT_EQUAL(
	        J1939_RET_OK,
	        j1939_ca_add(&n->s, &(j1939_ca_cfg_t){.address = address, .name = name}, &n->ca));
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

static void expect(const node_t *n, j1939_addr_state_t state, uint8_t address) {
	j1939_addr_state_t st = J1939_ADDR_STATE_UNCLAIMED;
	uint8_t addr = 0U;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_addr_get(&n->s, n->ca, &addr, &st));
	TEST_ASSERT_EQUAL(state, st);
	TEST_ASSERT_EQUAL_HEX8(address, addr);
}

/* Pops the monitor's next message and checks it is an Address Claimed from sa with name. */
static void monitor_expect(uint8_t sa, uint64_t name) {
	const j1939_msg_t *msg = j1939_msg_peek(&m.s);
	uint64_t got = 0U;

	TEST_ASSERT_NOT_NULL_MESSAGE(msg, "monitor saw no Address Claimed");
	TEST_ASSERT_EQUAL_HEX32(J1939_PGN_ADDRESS_CLAIMED, msg->pgn);
	TEST_ASSERT_EQUAL_HEX8(sa, msg->sa);
	TEST_ASSERT_EQUAL_UINT16(J1939_NAME_LEN, msg->len);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_name_from_bytes(msg->data, &got));
	TEST_ASSERT_EQUAL_HEX64(name, got);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_msg_pop(&m.s));
}

static j1939_ret_t send_bc(node_t *n) {
	static const uint8_t payload[8] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U};
	const j1939_msg_t msg = {.pgn = 0xFEF1U,
	                         .prio = 6U,
	                         .sa = 0U,
	                         .da = J1939_ADDR_GLOBAL,
	                         .len = 8U,
	                         .data = payload};

	return j1939_send(&n->s, n->ca, &msg);
}

void setUp(void) {
	test_bus_init(&bus);
	node_init(&a, false);
	node_init(&b, false);
	node_init(&c, false);
	node_init(&m, true);
}

void tearDown(void) {
}

static void test_distinct_addresses_are_claimed(void) {
	ca_add(&a, 0x10U, 0x100U);
	ca_add(&b, 0x11U, 0x200U);
	ca_add(&c, 0x80U, ARB | 0x300U);

	advance(0U);
	expect(&a, J1939_ADDR_STATE_CLAIMED, 0x10U);
	expect(&b, J1939_ADDR_STATE_CLAIMED, 0x11U);
	expect(&c, J1939_ADDR_STATE_CLAIMING, 0x80U);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_NO_ADDRESS, send_bc(&c));
	TEST_ASSERT_EQUAL_UINT32(3U, bus.frames);

	advance(J1939_ADDR_CLAIM_WAIT_US);
	expect(&c, J1939_ADDR_STATE_CLAIMED, 0x80U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, send_bc(&c));

	monitor_expect(0x10U, 0x100U);
	monitor_expect(0x11U, 0x200U);
	monitor_expect(0x80U, ARB | 0x300U);
	TEST_ASSERT_NULL(j1939_msg_peek(&m.s));
}

static void test_non_arbitrary_loser_sends_cannot_claim(void) {
	ca_add(&a, 0x20U, 0x100U);
	ca_add(&b, 0x20U, 0x2FFU); /* Cannot Claim delay: 0xFD steps */

	advance(0U);
	expect(&a, J1939_ADDR_STATE_CLAIMED, 0x20U);
	expect(&b, J1939_ADDR_STATE_CANNOT_CLAIM, J1939_ADDR_NULL);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_NO_ADDRESS, send_bc(&b));
	monitor_expect(0x20U, 0x100U);
	monitor_expect(0x20U, 0x2FFU);
	monitor_expect(0x20U, 0x100U); /* a defends its address */
	TEST_ASSERT_NULL(j1939_msg_peek(&m.s));

	advance((0xFDU * J1939_ADDR_CANNOT_CLAIM_STEP_US) - 1U);
	TEST_ASSERT_NULL(j1939_msg_peek(&m.s));
	advance(1U);
	monitor_expect(J1939_ADDR_NULL, 0x2FFU);
	TEST_ASSERT_NULL(j1939_msg_peek(&m.s));
	expect(&a, J1939_ADDR_STATE_CLAIMED, 0x20U);
}

static void test_arbitrary_loser_moves_to_another_address(void) {
	ca_add(&a, 0x80U, 0x100U);
	ca_add(&b, 0x80U, ARB | 0x200U);

	advance(0U);
	expect(&a, J1939_ADDR_STATE_CLAIMING, 0x80U);
	expect(&b, J1939_ADDR_STATE_CLAIMING, 0x81U);
	advance(J1939_ADDR_CLAIM_WAIT_US);
	expect(&a, J1939_ADDR_STATE_CLAIMED, 0x80U);
	expect(&b, J1939_ADDR_STATE_CLAIMED, 0x81U);

	monitor_expect(0x80U, 0x100U);
	monitor_expect(0x80U, ARB | 0x200U);
	monitor_expect(0x80U, 0x100U); /* a defends its address */
	monitor_expect(0x81U, ARB | 0x200U);
	TEST_ASSERT_NULL(j1939_msg_peek(&m.s));
}

static void test_three_nodes_contend_for_one_address(void) {
	ca_add(&a, 0x80U, ARB | 0x300U);
	ca_add(&b, 0x80U, 0x200U);
	ca_add(&c, 0x80U, 0x100U);

	advance(0U);
	advance(J1939_ADDR_CLAIM_WAIT_US);
	expect(&c, J1939_ADDR_STATE_CLAIMED, 0x80U);
	expect(&a, J1939_ADDR_STATE_CLAIMED, 0x81U);
	expect(&b, J1939_ADDR_STATE_CANNOT_CLAIM, J1939_ADDR_NULL);
	TEST_ASSERT_EQUAL(J1939_RET_OK, send_bc(&c));
	TEST_ASSERT_EQUAL(J1939_RET_OK, send_bc(&a));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_NO_ADDRESS, send_bc(&b));
}

/* A node joining a running network asks for the claimed addresses from NULL. */
static void test_request_for_address_claimed_from_joining_node(void) {
	ca_add(&a, 0x10U, 0x100U);
	ca_add(&b, 0x80U, 0x200U);
	advance(0U);
	advance(J1939_ADDR_CLAIM_WAIT_US);
	while (j1939_msg_peek(&m.s) != NULL) {
		(void)j1939_msg_pop(&m.s);
	}
	bus.frames = 0U;

	ca_add(&c, 0x80U, ARB | 0x300U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_request_send(&c.s, c.ca, J1939_PGN_ADDRESS_CLAIMED,
	                                                   J1939_ADDR_GLOBAL));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_NO_ADDRESS, send_bc(&c));
	(void)test_bus_run(&bus);
	advance(0U);

	/* a and b answer; c claims its preferred address, loses it to b and moves. */
	monitor_expect(0x10U, 0x100U);
	monitor_expect(0x80U, 0x200U);
	monitor_expect(0x80U, ARB | 0x300U);
	monitor_expect(0x80U, 0x200U);
	monitor_expect(0x81U, ARB | 0x300U);
	TEST_ASSERT_NULL(j1939_msg_peek(&m.s));
	expect(&c, J1939_ADDR_STATE_CLAIMING, 0x81U);
	advance(J1939_ADDR_CLAIM_WAIT_US);
	expect(&c, J1939_ADDR_STATE_CLAIMED, 0x81U);
	expect(&a, J1939_ADDR_STATE_CLAIMED, 0x10U);
	expect(&b, J1939_ADDR_STATE_CLAIMED, 0x80U);
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_distinct_addresses_are_claimed);
	RUN_TEST(test_non_arbitrary_loser_sends_cannot_claim);
	RUN_TEST(test_arbitrary_loser_moves_to_another_address);
	RUN_TEST(test_three_nodes_contend_for_one_address);
	RUN_TEST(test_request_for_address_claimed_from_joining_node);
	return UNITY_END();
}
