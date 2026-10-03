/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/* Transport protocol between three nodes on the test bus. */

#include <stdbool.h>
#include <stdint.h>

#include "unity.h"

#include "j1939/j1939.h"
#include "test_bus.h"

#define TX_LEN  16U
#define MSG_LEN 4U
#define BUF_LEN 2U

#define ADDR_A 0x01U
#define ADDR_B 0x02U
#define ADDR_C 0x03U
#define NAME_A 0x10U
#define NAME_B 0x20U
#define NAME_C 0x30U
#define NAME_D 0x05U   /* wins against NAME_B */
#define PGN_BC 0xFECAU /* PDU2, received by every node */
#define PGN_DS 0xEF00U /* PDU1, received by every node */
#define PGN_NO 0xE100U /* PDU1, received by nobody */

#define STEP_US 10000U /* process period */

typedef struct node {
	j1939_t s;
	j1939_ca_id_t ca;
	j1939_port_frame_t tx[TX_LEN];
	j1939_msg_slot_t msgs[MSG_LEN];
	j1939_tp_buf_t tp_tx[BUF_LEN];
	j1939_tp_buf_t tp_rx[BUF_LEN];
} node_t;

static const uint32_t rx_pgns[] = {PGN_BC, PGN_DS};

static node_t a;
static node_t b;
static node_t c;
static node_t d;
static test_bus_t bus;
static uint8_t data[J1939_TP_MSG_MAX];

static uint8_t pat(uint32_t i, uint8_t seed) {
	return (uint8_t)((i * 31U) + seed);
}

static void node_init(node_t *n, uint8_t address, uint64_t name) {
	const j1939_cfg_t cfg = {
	        .tx_buf = n->tx,
	        .tx_len = TX_LEN,
	        .msg_buf = n->msgs,
	        .msg_len = MSG_LEN,
	        .rx_pgns = rx_pgns,
	        .rx_pgns_len = 2U,
	        .tp_tx_buf = n->tp_tx,
	        .tp_tx_buf_len = BUF_LEN,
	        .tp_rx_buf = n->tp_rx,
	        .tp_rx_buf_len = BUF_LEN,
	};

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&n->s, &cfg));
	TEST_ASSERT_EQUAL(
	        J1939_RET_OK,
	        j1939_ca_add(&n->s, &(j1939_ca_cfg_t){.address = address, .name = name}, &n->ca));
	test_bus_attach(&bus, &n->s);
}

static j1939_ret_t send(node_t *n, uint8_t da, uint32_t pgn, uint16_t len, uint8_t seed) {
	j1939_msg_t msg = {.pgn = pgn, .prio = 6U, .sa = 0U, .da = da, .len = len, .data = data};
	uint32_t i;

	for (i = 0U; i < len; i++) {
		data[i] = pat(i, seed);
	}
	return j1939_send(&n->s, n->ca, &msg);
}

static void process_all(uint32_t us) {
	uint8_t i;

	for (i = 0U; i < bus.count; i++) {
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(bus.nodes[i], us));
	}
}

static void steps(uint32_t count) {
	uint32_t i;

	for (i = 0U; i < count; i++) {
		process_all(STEP_US);
		(void)test_bus_run(&bus);
	}
}

static uint8_t sessions_open(const node_t *n) {
	uint8_t count = 0U;
	uint32_t i;

	for (i = 0U; i < J1939_CFG_TP_SESSIONS; i++) {
		if (n->s.tp.sessions[i].state != J1939_TP_IDLE) {
			count++;
		}
	}
	return count;
}

static void expect_msg(const j1939_msg_t *msg, uint32_t pgn, uint8_t sa, uint8_t da, uint16_t len,
                       uint8_t seed) {
	uint32_t i;

	TEST_ASSERT_NOT_NULL_MESSAGE(msg, "no message");
	TEST_ASSERT_EQUAL_HEX32(pgn, msg->pgn);
	TEST_ASSERT_EQUAL_HEX8(sa, msg->sa);
	TEST_ASSERT_EQUAL_HEX8(da, msg->da);
	TEST_ASSERT_EQUAL_UINT16(len, msg->len);
	for (i = 0U; i < len; i++) {
		TEST_ASSERT_EQUAL_HEX8(pat(i, seed), msg->data[i]);
	}
}

static void expect_pop(node_t *n, uint32_t pgn, uint8_t sa, uint8_t da, uint16_t len,
                       uint8_t seed) {
	expect_msg(j1939_msg_peek(&n->s), pgn, sa, da, len, seed);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_msg_pop(&n->s));
}

/* Carries the frames of node from, dropping data packet seq; then the rest of the bus. */
static void carry_dropping(node_t *from, uint8_t seq) {
	const j1939_port_frame_t *f;
	uint8_t i;

	while ((f = j1939_tx_peek(&from->s)) != NULL) {
		bool drop = (j1939_id_pgn_get(j1939_port_frame_id_get(f)) == J1939_PGN_TP_DT) &&
		            (j1939_port_frame_data(f)[0] == seq);

		for (i = 0U; (!drop) && (i < bus.count); i++) {
			if (bus.nodes[i] != &from->s) {
				TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_rx(bus.nodes[i], f));
			}
		}
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_tx_pop(&from->s));
	}
	(void)test_bus_run(&bus);
}

void setUp(void) {
	test_bus_init(&bus);
	node_init(&a, ADDR_A, NAME_A);
	node_init(&b, ADDR_B, NAME_B);
	node_init(&c, ADDR_C, NAME_C);
	/* Every node claims its address; the claims are not counted. */
	process_all(0U);
	(void)test_bus_run(&bus);
	process_all(0U);
	bus.frames = 0U;
}

void tearDown(void) {
}

static void test_bam_of_maximum_size_reaches_every_node(void) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, send(&a, J1939_ADDR_GLOBAL, PGN_BC, J1939_TP_MSG_MAX, 1U));
	/* 255 packets, one every 50 ms: 5 process periods each. */
	steps(255U * 5U + 3U);
	expect_pop(&b, PGN_BC, ADDR_A, J1939_ADDR_GLOBAL, J1939_TP_MSG_MAX, 1U);
	expect_pop(&c, PGN_BC, ADDR_A, J1939_ADDR_GLOBAL, J1939_TP_MSG_MAX, 1U);
	TEST_ASSERT_EQUAL_UINT32(1U + 255U, bus.frames);
	TEST_ASSERT_EQUAL_UINT8(0U, sessions_open(&a));
}

static void test_bam_packets_keep_the_gap(void) {
	uint32_t i;
	uint32_t carried = 0U;
	uint32_t since = 0U;

	TEST_ASSERT_EQUAL(J1939_RET_OK, send(&a, J1939_ADDR_GLOBAL, PGN_BC, 30U, 2U));
	TEST_ASSERT_EQUAL_UINT32(1U, test_bus_run(&bus));
	for (i = 0U; i < 40U; i++) {
		process_all(STEP_US);
		since += STEP_US;
		if (test_bus_run(&bus) > 0U) {
			TEST_ASSERT_GREATER_OR_EQUAL_UINT32(J1939_CFG_TP_BAM_GAP_US, since);
			TEST_ASSERT_LESS_OR_EQUAL_UINT32(J1939_CFG_TP_BAM_GAP_US + STEP_US, since);
			since = 0U;
			carried++;
		}
	}
	TEST_ASSERT_EQUAL_UINT32(5U, carried);
	expect_pop(&b, PGN_BC, ADDR_A, J1939_ADDR_GLOBAL, 30U, 2U);
}

static void test_rts_cts_of_maximum_size(void) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, send(&a, ADDR_B, PGN_DS, J1939_TP_MSG_MAX, 3U));
	steps(40U);
	expect_pop(&b, PGN_DS, ADDR_A, ADDR_B, J1939_TP_MSG_MAX, 3U);
	TEST_ASSERT_NULL(j1939_msg_peek(&c.s)); /* not addressed to C */
	/* RTS, CTS, 255 packets, EndOfMsgAck. */
	TEST_ASSERT_EQUAL_UINT32(258U, bus.frames);
	TEST_ASSERT_EQUAL_UINT8(0U, sessions_open(&a));
	TEST_ASSERT_EQUAL_UINT8(0U, sessions_open(&b));
	TEST_ASSERT_EQUAL_UINT32(0U, a.s.stats.tp_tx_aborted);
	TEST_ASSERT_EQUAL_UINT32(0U, b.s.stats.tp_rx_aborted);
}

static void test_lost_packet_times_out_and_aborts(void) {
	uint32_t i;

	TEST_ASSERT_EQUAL(J1939_RET_OK, send(&a, ADDR_B, PGN_DS, 20U, 4U));
	for (i = 0U; i < 4U; i++) {
		process_all(STEP_US);
		carry_dropping(&a, 3U);
	}
	TEST_ASSERT_EQUAL_UINT8(1U, sessions_open(&b));
	/* B waits T1 for the last packet, then aborts; A closes on the abort. */
	steps(J1939_TP_T1_US / STEP_US + 3U);
	TEST_ASSERT_EQUAL_UINT8(0U, sessions_open(&b));
	TEST_ASSERT_EQUAL_UINT8(0U, sessions_open(&a));
	TEST_ASSERT_EQUAL_UINT32(1U, b.s.stats.tp_rx_aborted);
	TEST_ASSERT_EQUAL_UINT32(1U, a.s.stats.tp_tx_aborted);
	TEST_ASSERT_NULL(j1939_msg_peek(&b.s));
	TEST_ASSERT_EQUAL(J1939_RET_OK, send(&a, ADDR_B, PGN_DS, 20U, 4U));
}

static void test_responder_abort_reaches_the_originator(void) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, send(&a, ADDR_B, PGN_NO, 20U, 5U));
	steps(3U);
	TEST_ASSERT_EQUAL_UINT8(0U, sessions_open(&a));
	TEST_ASSERT_EQUAL_UINT32(1U, a.s.stats.tp_tx_aborted);
	TEST_ASSERT_EQUAL_UINT32(2U, bus.frames); /* RTS, Connection Abort */
	TEST_ASSERT_NULL(j1939_msg_peek(&b.s));
}

static void test_concurrent_broadcast_and_connection(void) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, send(&a, J1939_ADDR_GLOBAL, PGN_BC, 100U, 6U));
	TEST_ASSERT_EQUAL(J1939_RET_OK, send(&c, ADDR_B, PGN_DS, 300U, 7U));
	steps(15U * 5U + 3U);
	/* The connection completes first; the broadcast needs 15 packet gaps. */
	expect_pop(&b, PGN_DS, ADDR_C, ADDR_B, 300U, 7U);
	expect_pop(&b, PGN_BC, ADDR_A, J1939_ADDR_GLOBAL, 100U, 6U);
	expect_pop(&c, PGN_BC, ADDR_A, J1939_ADDR_GLOBAL, 100U, 6U);
	TEST_ASSERT_NULL(j1939_msg_peek(&a.s));
}

static void test_broadcasts_from_two_senders(void) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, send(&a, J1939_ADDR_GLOBAL, PGN_BC, 50U, 8U));
	TEST_ASSERT_EQUAL(J1939_RET_OK, send(&c, J1939_ADDR_GLOBAL, PGN_BC, 60U, 8U));
	steps(9U * 5U + 3U);
	expect_pop(&b, PGN_BC, ADDR_A, J1939_ADDR_GLOBAL, 50U, 8U);
	expect_pop(&b, PGN_BC, ADDR_C, J1939_ADDR_GLOBAL, 60U, 8U);
	expect_pop(&a, PGN_BC, ADDR_C, J1939_ADDR_GLOBAL, 60U, 8U);
	expect_pop(&c, PGN_BC, ADDR_A, J1939_ADDR_GLOBAL, 50U, 8U);
}

static void test_out_of_reassembly_memory_aborts_rts(void) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, send(&a, ADDR_B, PGN_DS, 40U, 9U));
	steps(5U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, send(&c, ADDR_B, PGN_DS, 40U, 10U));
	steps(5U);
	/* Both reassembly buffers of B are held by unreleased messages. */
	TEST_ASSERT_EQUAL(J1939_RET_OK, send(&a, ADDR_B, PGN_DS, 40U, 11U));
	steps(5U);
	TEST_ASSERT_EQUAL_UINT32(1U, b.s.stats.tp_rx_refused);
	TEST_ASSERT_EQUAL_UINT32(1U, a.s.stats.tp_tx_aborted);
	TEST_ASSERT_EQUAL_UINT8(0U, sessions_open(&a));

	expect_pop(&b, PGN_DS, ADDR_A, ADDR_B, 40U, 9U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, send(&a, ADDR_B, PGN_DS, 40U, 11U));
	steps(5U);
	expect_pop(&b, PGN_DS, ADDR_C, ADDR_B, 40U, 10U);
	expect_pop(&b, PGN_DS, ADDR_A, ADDR_B, 40U, 11U);
}

static void test_message_slot_holds_buffer_until_pop(void) {
	const j1939_msg_t *held;
	const uint8_t *held_data;

	TEST_ASSERT_EQUAL(J1939_RET_OK, send(&a, ADDR_B, PGN_DS, 70U, 12U));
	steps(5U);
	held = j1939_msg_peek(&b.s);
	expect_msg(held, PGN_DS, ADDR_A, ADDR_B, 70U, 12U);
	held_data = held->data;

	/* More messages arrive while the application still reads the first one. */
	TEST_ASSERT_EQUAL(J1939_RET_OK, send(&c, ADDR_B, PGN_DS, 70U, 13U));
	steps(5U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, send(&a, J1939_ADDR_GLOBAL, PGN_BC, 70U, 14U));
	steps(11U * 5U + 3U);
	TEST_ASSERT_EQUAL_UINT32(1U, b.s.stats.tp_rx_refused);
	TEST_ASSERT_EQUAL_PTR(held, j1939_msg_peek(&b.s));
	TEST_ASSERT_EQUAL_PTR(held_data, held->data);
	expect_msg(held, PGN_DS, ADDR_A, ADDR_B, 70U, 12U);

	/* After the pop the buffer takes the next message. */
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_msg_pop(&b.s));
	TEST_ASSERT_EQUAL(J1939_RET_OK, send(&a, J1939_ADDR_GLOBAL, PGN_BC, 70U, 14U));
	steps(11U * 5U + 3U);
	expect_pop(&b, PGN_DS, ADDR_C, ADDR_B, 70U, 13U);
	expect_msg(j1939_msg_peek(&b.s), PGN_BC, ADDR_A, J1939_ADDR_GLOBAL, 70U, 14U);
	TEST_ASSERT_EQUAL_PTR(held_data, j1939_msg_peek(&b.s)->data);
}

static void test_responder_losing_its_address_ends_the_transfer_silently(void) {
	uint8_t address = 0U;
	j1939_addr_state_t state = J1939_ADDR_STATE_UNCLAIMED;

	TEST_ASSERT_EQUAL(J1939_RET_OK, send(&a, ADDR_B, PGN_DS, J1939_TP_MSG_MAX, 15U));
	steps(4U);
	TEST_ASSERT_EQUAL_UINT8(1U, sessions_open(&b));

	/* D claims B's address with a NAME of higher priority. */
	node_init(&d, ADDR_B, NAME_D);
	steps(3U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_addr_get(&b.s, b.ca, &address, &state));
	TEST_ASSERT_EQUAL(J1939_ADDR_STATE_CANNOT_CLAIM, state);
	TEST_ASSERT_EQUAL_UINT8(0U, sessions_open(&b));
	TEST_ASSERT_EQUAL_UINT32(1U, b.s.stats.tp_rx_aborted);
	/* B sent no abort from the lost address: A goes on until T3 expires. */
	TEST_ASSERT_EQUAL_UINT8(1U, sessions_open(&a));
	TEST_ASSERT_EQUAL_UINT32(0U, a.s.stats.tp_tx_aborted);

	steps(J1939_TP_T3_US / STEP_US + 20U);
	TEST_ASSERT_EQUAL_UINT8(0U, sessions_open(&a));
	TEST_ASSERT_EQUAL_UINT32(1U, a.s.stats.tp_tx_aborted);
	TEST_ASSERT_EQUAL_UINT8(0U, sessions_open(&d));
	TEST_ASSERT_NULL(j1939_msg_peek(&b.s));
	TEST_ASSERT_NULL(j1939_msg_peek(&d.s));
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_bam_of_maximum_size_reaches_every_node);
	RUN_TEST(test_bam_packets_keep_the_gap);
	RUN_TEST(test_rts_cts_of_maximum_size);
	RUN_TEST(test_lost_packet_times_out_and_aborts);
	RUN_TEST(test_responder_abort_reaches_the_originator);
	RUN_TEST(test_concurrent_broadcast_and_connection);
	RUN_TEST(test_broadcasts_from_two_senders);
	RUN_TEST(test_out_of_reassembly_memory_aborts_rts);
	RUN_TEST(test_message_slot_holds_buffer_until_pop);
	RUN_TEST(test_responder_losing_its_address_ends_the_transfer_silently);
	return UNITY_END();
}
