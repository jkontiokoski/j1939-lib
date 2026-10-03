/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

#include "unity.h"

#include "j1939/j1939.h"
#include "j1939_port_fixture.h"

#define TX_LEN  4U
#define MSG_LEN 3U

#define OWN_A   0x10U
#define OWN_B   0x11U
#define OTHER   0x42U
#define PGN_BC  0xFEF1U /* PDU2 broadcast */
#define PGN_DS  0xEF00U /* PDU1 proprietary A */
#define PGN_OFF 0xFEEEU /* not in the rx list */

static const uint32_t rx_pgns[] = {PGN_BC, PGN_DS};
static j1939_port_frame_t tx_buf[TX_LEN];
static j1939_msg_slot_t msg_buf[MSG_LEN];
static j1939_cfg_t cfg;
static j1939_t s;
static j1939_ca_id_t ca_a;

static const uint8_t payload[8] = {0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U, 0x88U};

static uint32_t make_id(uint8_t prio, uint32_t pgn, uint8_t da, uint8_t sa) {
	uint32_t id = 0U;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_id_build(prio, pgn, da, sa, &id));
	return id;
}

static void rx_frame(uint32_t id, uint8_t len) {
	j1939_port_frame_t f;

	j1939_port_frame_build(&f, id, payload, len);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_rx(&s, &f));
}

void setUp(void) {
	cfg = (j1939_cfg_t){
	        .tx_buf = tx_buf,
	        .tx_len = TX_LEN,
	        .msg_buf = msg_buf,
	        .msg_len = MSG_LEN,
	        .rx_pgns = rx_pgns,
	        .rx_pgns_len = 2U,
	        .req_pgns = NULL,
	        .req_pgns_len = 0U,
	};
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&s, &cfg));
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_ca_add(&s, &(j1939_ca_cfg_t){.address = OWN_A}, &ca_a));
	/* Claim the address; the CA may transmit right after its Address Claimed. */
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(&s, 0U));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_tx_pop(&s));
}

void tearDown(void) {
}

static void test_init_rejects_invalid_configuration(void) {
	static const uint32_t bad_pdu1[] = {0xEF12U};
	static const uint32_t bad_range[] = {0x40000U};
	j1939_cfg_t c;
	j1939_t other;

	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_init(NULL, &cfg));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_init(&other, NULL));
	c = cfg;
	c.tx_buf = NULL;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_init(&other, &c));
	c = cfg;
	c.tx_len = 0U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_init(&other, &c));
	c = cfg;
	c.msg_buf = NULL;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_init(&other, &c));
	c = cfg;
	c.msg_len = 0U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_init(&other, &c));
	c = cfg;
	c.rx_pgns = NULL;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_init(&other, &c));
	c = cfg;
	c.rx_pgns = bad_pdu1;
	c.rx_pgns_len = 1U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_init(&other, &c));
	c = cfg;
	c.req_pgns = bad_range;
	c.req_pgns_len = 1U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_init(&other, &c));
}

static void test_ca_add_validates_address_and_capacity(void) {
	j1939_ca_id_t id = 0xAAU;

	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG,
	                  j1939_ca_add(&s, &(j1939_ca_cfg_t){.address = J1939_ADDR_NULL}, &id));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG,
	                  j1939_ca_add(&s, &(j1939_ca_cfg_t){.address = J1939_ADDR_GLOBAL}, &id));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG,
	                  j1939_ca_add(&s, &(j1939_ca_cfg_t){.address = OWN_A}, &id));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG,
	                  j1939_ca_add(NULL, &(j1939_ca_cfg_t){.address = 1U}, &id));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_ca_add(&s, NULL, &id));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG,
	                  j1939_ca_add(&s, &(j1939_ca_cfg_t){.address = 1U}, NULL));
	TEST_ASSERT_EQUAL_HEX8(0xAAU, id);

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_ca_add(&s, &(j1939_ca_cfg_t){.address = 0xFDU}, &id));
	TEST_ASSERT_EQUAL_UINT8(1U, id);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_FULL,
	                  j1939_ca_add(&s, &(j1939_ca_cfg_t){.address = 2U}, &id));
}

static void test_broadcast_in_list_is_delivered(void) {
	const j1939_msg_t *msg;

	TEST_ASSERT_NULL(j1939_msg_peek(&s));
	rx_frame(make_id(3U, PGN_BC, J1939_ADDR_GLOBAL, OTHER), 8U);

	/* Delivered by j1939_rx() itself, without j1939_process(). */
	msg = j1939_msg_peek(&s);
	TEST_ASSERT_NOT_NULL(msg);
	TEST_ASSERT_EQUAL_HEX32(PGN_BC, msg->pgn);
	TEST_ASSERT_EQUAL_UINT8(3U, msg->prio);
	TEST_ASSERT_EQUAL_HEX8(OTHER, msg->sa);
	TEST_ASSERT_EQUAL_HEX8(J1939_ADDR_GLOBAL, msg->da);
	TEST_ASSERT_EQUAL_UINT16(8U, msg->len);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, msg->data, 8U);

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_msg_pop(&s));
	TEST_ASSERT_NULL(j1939_msg_peek(&s));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_EMPTY, j1939_msg_pop(&s));
}

static void test_empty_message_is_delivered(void) {
	rx_frame(make_id(6U, PGN_BC, J1939_ADDR_GLOBAL, OTHER), 0U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(&s, 0U));
	TEST_ASSERT_NOT_NULL(j1939_msg_peek(&s));
	TEST_ASSERT_EQUAL_UINT16(0U, j1939_msg_peek(&s)->len);
	TEST_ASSERT_NOT_NULL(j1939_msg_peek(&s)->data);
}

static void test_unlisted_and_foreign_frames_are_dropped(void) {
	j1939_port_frame_t f;

	rx_frame(make_id(6U, PGN_OFF, J1939_ADDR_GLOBAL, OTHER), 8U);
	rx_frame(make_id(6U, PGN_DS, 0x20U, OTHER), 8U); /* another node's address */
	rx_frame(make_id(6U, PGN_BC, J1939_ADDR_GLOBAL, OTHER) | (1U << 25), 8U); /* EDP set */
	j1939_port_fixture_std(&f, 0x123U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_rx(&s, &f));
	j1939_port_fixture_rtr(&f, make_id(6U, PGN_BC, J1939_ADDR_GLOBAL, OTHER));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_rx(&s, &f));

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(&s, 0U));
	TEST_ASSERT_NULL(j1939_msg_peek(&s));
	TEST_ASSERT_EQUAL_UINT16(0U, s.tx.ring.count);
}

static void test_destination_specific_to_each_ca_is_delivered(void) {
	j1939_ca_id_t ca_b;

	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_ca_add(&s, &(j1939_ca_cfg_t){.address = OWN_B}, &ca_b));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(&s, 0U)); /* OWN_B is claimed */
	rx_frame(make_id(6U, PGN_DS, OWN_A, OTHER), 2U);
	rx_frame(make_id(6U, PGN_DS, OWN_B, OTHER), 2U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(&s, 0U));

	TEST_ASSERT_EQUAL_HEX8(OWN_A, j1939_msg_peek(&s)->da);
	TEST_ASSERT_EQUAL_HEX32(PGN_DS, j1939_msg_peek(&s)->pgn);
	TEST_ASSERT_EQUAL_UINT16(2U, j1939_msg_peek(&s)->len);
	(void)j1939_msg_pop(&s);
	TEST_ASSERT_EQUAL_HEX8(OWN_B, j1939_msg_peek(&s)->da);
	(void)j1939_msg_pop(&s);
	TEST_ASSERT_NULL(j1939_msg_peek(&s));
}

static void test_message_overflow_is_counted(void) {
	uint32_t i;

	for (i = 0U; i < (MSG_LEN + 2U); i++) {
		rx_frame(make_id(6U, PGN_BC, J1939_ADDR_GLOBAL, (uint8_t)i), 8U);
	}
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(&s, 0U));
	TEST_ASSERT_EQUAL_UINT32(2U, j1939_stats_get(&s)->rx_msg_overflow);
	/* The oldest messages are kept. */
	for (i = 0U; i < MSG_LEN; i++) {
		TEST_ASSERT_EQUAL_HEX8((uint8_t)i, j1939_msg_peek(&s)->sa);
		(void)j1939_msg_pop(&s);
	}
}

static void test_rx_reads_the_frame_only_during_the_call(void) {
	j1939_port_frame_t f;

	j1939_port_frame_build(&f, make_id(6U, PGN_BC, J1939_ADDR_GLOBAL, OTHER), payload, 8U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_rx(&s, &f));
	j1939_port_frame_build(&f, make_id(6U, PGN_OFF, J1939_ADDR_GLOBAL, OTHER), NULL, 0U);
	TEST_ASSERT_EQUAL_HEX32(PGN_BC, j1939_msg_peek(&s)->pgn);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, j1939_msg_peek(&s)->data, 8U);
}

static void test_tx_queue_is_drained_in_order(void) {
	const j1939_msg_t msg = {.pgn = PGN_BC,
	                         .prio = 6U,
	                         .sa = 0U,
	                         .da = J1939_ADDR_GLOBAL,
	                         .len = 1U,
	                         .data = payload};
	const j1939_port_frame_t *f;

	TEST_ASSERT_NULL(j1939_tx_peek(&s));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_EMPTY, j1939_tx_pop(&s));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_send(&s, ca_a, &msg));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_send(&s, ca_a,
	                                           &(j1939_msg_t){.pgn = PGN_DS,
	                                                          .prio = 6U,
	                                                          .sa = 0U,
	                                                          .da = OTHER,
	                                                          .len = 0U,
	                                                          .data = NULL}));
	/* A frame the driver refuses stays: peek returns it again until it is popped. */
	f = j1939_tx_peek(&s);
	TEST_ASSERT_EQUAL_PTR(f, j1939_tx_peek(&s));
	TEST_ASSERT_EQUAL_HEX32(make_id(6U, PGN_BC, J1939_ADDR_GLOBAL, OWN_A),
	                        j1939_port_frame_id_get(f));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_tx_pop(&s));
	TEST_ASSERT_EQUAL_HEX32(make_id(6U, PGN_DS, OTHER, OWN_A),
	                        j1939_port_frame_id_get(j1939_tx_peek(&s)));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_tx_pop(&s));
	TEST_ASSERT_NULL(j1939_tx_peek(&s));
}

static void test_send_queues_single_frame(void) {
	const j1939_msg_t msg = {
	        .pgn = PGN_DS, .prio = 3U, .sa = 0x99U, .da = OTHER, .len = 5U, .data = payload};
	const j1939_port_frame_t *f;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_send(&s, ca_a, &msg));
	f = j1939_tx_peek(&s);
	TEST_ASSERT_NOT_NULL(f);
	TEST_ASSERT_TRUE(j1939_port_frame_is_ext(f));
	TEST_ASSERT_EQUAL_HEX32(make_id(3U, PGN_DS, OTHER, OWN_A), j1939_port_frame_id_get(f));
	TEST_ASSERT_EQUAL_UINT8(5U, j1939_port_frame_len_get(f));
	TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, j1939_port_frame_data(f), 5U);
}

static void test_send_rejects_invalid_messages(void) {
	j1939_msg_t msg = {.pgn = PGN_BC,
	                   .prio = 6U,
	                   .sa = 0U,
	                   .da = J1939_ADDR_GLOBAL,
	                   .len = 8U,
	                   .data = payload};
	uint32_t i;

	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_send(NULL, ca_a, &msg));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_send(&s, ca_a, NULL));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_send(&s, 1U, &msg));
	msg.len = J1939_TP_MSG_MAX + 1U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_send(&s, ca_a, &msg));
	msg.len = 9U; /* multi-packet, but no transport protocol transmit buffer is configured */
	TEST_ASSERT_EQUAL(J1939_RET_ERR_FULL, j1939_send(&s, ca_a, &msg));
	msg.len = 1U;
	msg.data = NULL;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_send(&s, ca_a, &msg));
	msg.len = 0U;
	msg.da = OTHER; /* PDU2 PGN with a destination */
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_send(&s, ca_a, &msg));
	TEST_ASSERT_EQUAL_UINT16(0U, s.tx.ring.count);

	msg.da = J1939_ADDR_GLOBAL;
	for (i = 0U; i < TX_LEN; i++) {
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_send(&s, ca_a, &msg));
	}
	TEST_ASSERT_EQUAL(J1939_RET_ERR_FULL, j1939_send(&s, ca_a, &msg));
}

static void test_null_stack_accessors(void) {
	j1939_port_frame_t f;

	j1939_port_frame_build(&f, make_id(6U, PGN_BC, J1939_ADDR_GLOBAL, OTHER), payload, 8U);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_rx(NULL, &f));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_rx(&s, NULL));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_process(NULL, 0U));
	TEST_ASSERT_NULL(j1939_tx_peek(NULL));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_tx_pop(NULL));
	TEST_ASSERT_NULL(j1939_msg_peek(NULL));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_msg_pop(NULL));
	TEST_ASSERT_NULL(j1939_stats_get(NULL));
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_init_rejects_invalid_configuration);
	RUN_TEST(test_ca_add_validates_address_and_capacity);
	RUN_TEST(test_broadcast_in_list_is_delivered);
	RUN_TEST(test_empty_message_is_delivered);
	RUN_TEST(test_unlisted_and_foreign_frames_are_dropped);
	RUN_TEST(test_destination_specific_to_each_ca_is_delivered);
	RUN_TEST(test_message_overflow_is_counted);
	RUN_TEST(test_rx_reads_the_frame_only_during_the_call);
	RUN_TEST(test_tx_queue_is_drained_in_order);
	RUN_TEST(test_send_queues_single_frame);
	RUN_TEST(test_send_rejects_invalid_messages);
	RUN_TEST(test_null_stack_accessors);
	return UNITY_END();
}
