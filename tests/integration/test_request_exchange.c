/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/* Two nodes on the test bus: a requester and a responder. */

#include "unity.h"

#include "j1939/j1939.h"
#include "test_bus.h"

#define LEN 8U

#define ADDR_A  0x01U
#define ADDR_B  0x02U
#define PGN_SUP 0xFF10U
#define PGN_UNS 0xFF20U

typedef struct node {
	j1939_t s;
	j1939_ca_id_t ca;
	j1939_port_frame_t rx[LEN];
	j1939_port_frame_t tx[LEN];
	j1939_msg_slot_t msgs[LEN];
} node_t;

static const uint32_t a_rx_pgns[] = {PGN_SUP, J1939_PGN_ACK};
static const uint32_t b_req_pgns[] = {PGN_SUP};
static const uint8_t b_data[8] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U};

static node_t a;
static node_t b;
static test_bus_t bus;

static void node_init(node_t *n, uint8_t address, const uint32_t *rx_pgns, uint16_t rx_pgns_len,
                      const uint32_t *req_pgns, uint16_t req_pgns_len) {
	const j1939_cfg_t cfg = {
	        .rx_buf = n->rx,
	        .rx_len = LEN,
	        .tx_buf = n->tx,
	        .tx_len = LEN,
	        .msg_buf = n->msgs,
	        .msg_len = LEN,
	        .rx_pgns = rx_pgns,
	        .rx_pgns_len = rx_pgns_len,
	        .req_pgns = req_pgns,
	        .req_pgns_len = req_pgns_len,
	};

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&n->s, &cfg));
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_ca_add(&n->s, &(j1939_ca_cfg_t){.address = address}, &n->ca));
	test_bus_attach(&bus, &n->s);
}

/* Application of node B: answers the Requests the stack delivers. */
static void b_application(void) {
	const j1939_msg_t *msg;

	while ((msg = j1939_msg_peek(&b.s)) != NULL) {
		uint32_t pgn = 0U;

		TEST_ASSERT_EQUAL_HEX32(J1939_PGN_REQUEST, msg->pgn);
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_request_pgn_get(msg, &pgn));
		if (pgn == PGN_SUP) {
			const j1939_msg_t answer = {.pgn = PGN_SUP,
			                            .prio = 6U,
			                            .sa = 0U,
			                            .da = J1939_ADDR_GLOBAL,
			                            .len = 8U,
			                            .data = b_data};

			TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_send(&b.s, b.ca, &answer));
		}
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_msg_pop(&b.s));
	}
}

void setUp(void) {
	test_bus_init(&bus);
	node_init(&a, ADDR_A, a_rx_pgns, 2U, NULL, 0U);
	node_init(&b, ADDR_B, NULL, 0U, b_req_pgns, 1U);
	/* Both nodes claim their addresses before the exchange. */
	test_bus_settle(&bus, 4U);
	bus.frames = 0U;
}

void tearDown(void) {
}

static void test_supported_request_is_answered(void) {
	const j1939_msg_t *msg;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_request_send(&a.s, a.ca, PGN_SUP, ADDR_B));
	test_bus_settle(&bus, 4U);
	b_application();
	test_bus_settle(&bus, 4U);

	msg = j1939_msg_peek(&a.s);
	TEST_ASSERT_NOT_NULL(msg);
	TEST_ASSERT_EQUAL_HEX32(PGN_SUP, msg->pgn);
	TEST_ASSERT_EQUAL_HEX8(ADDR_B, msg->sa);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(b_data, msg->data, 8U);
	(void)j1939_msg_pop(&a.s);
	TEST_ASSERT_NULL(j1939_msg_peek(&a.s));
	TEST_ASSERT_EQUAL_UINT32(2U, bus.frames);
}

static void test_unsupported_request_is_nacked_by_the_stack(void) {
	const j1939_msg_t *msg;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_request_send(&a.s, a.ca, PGN_UNS, ADDR_B));
	test_bus_settle(&bus, 4U);

	TEST_ASSERT_NULL(j1939_msg_peek(&b.s));
	msg = j1939_msg_peek(&a.s);
	TEST_ASSERT_NOT_NULL(msg);
	TEST_ASSERT_EQUAL_HEX32(J1939_PGN_ACK, msg->pgn);
	TEST_ASSERT_EQUAL_HEX8(ADDR_B, msg->sa);
	TEST_ASSERT_EQUAL_HEX8(J1939_ACK_CTRL_NACK, msg->data[0]);
	TEST_ASSERT_EQUAL_HEX8(ADDR_A, msg->data[4]);
	TEST_ASSERT_EQUAL_HEX8(0x20U, msg->data[5]);
	TEST_ASSERT_EQUAL_HEX8(0xFFU, msg->data[6]);
	TEST_ASSERT_EQUAL_HEX8(0x00U, msg->data[7]);
}

static void test_global_unsupported_request_gets_no_answer(void) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_request_send(&a.s, a.ca, PGN_UNS, J1939_ADDR_GLOBAL));
	test_bus_settle(&bus, 4U);
	TEST_ASSERT_NULL(j1939_msg_peek(&a.s));
	TEST_ASSERT_NULL(j1939_msg_peek(&b.s));
	TEST_ASSERT_EQUAL_UINT32(1U, bus.frames);
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_supported_request_is_answered);
	RUN_TEST(test_unsupported_request_is_nacked_by_the_stack);
	RUN_TEST(test_global_unsupported_request_gets_no_answer);
	return UNITY_END();
}
