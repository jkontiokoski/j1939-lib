/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/* Transmit objects: schedule, change trigger, Request answers, claim gating, configuration. */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "j1939/j1939.h"

#define TX_LEN  8U
#define MSG_LEN 2U
#define OBJ_MAX 4U
#define LONG    20U /* multi-packet payload */

#define OWN    0x10U
#define OWN2   0x11U
#define OTHER  0x42U
#define OTHER2 0x43U
#define SELF   0x80U /* self-configurable: claimed after the contention wait */
#define NAME   0x1000U

#define PGN_A  0xFF10U /* PDU2 */
#define PGN_B  0xFF11U /* PDU2 */
#define PGN_P1 0xEF00U /* PDU1, Proprietary A */

#define PERIOD  100000U
#define INHIBIT 30000U
#define TR      J1939_TXOBJ_RESPONSE_US
#define GAP     J1939_CFG_TP_BAM_GAP_US

#define BAM 0x20U
#define RTS 0x10U

static j1939_port_frame_t tx_buf[TX_LEN];
static j1939_msg_slot_t msg_buf[MSG_LEN];
static j1939_tp_buf_t tp_tx[1];
static uint8_t bufs[OBJ_MAX][LONG];
static j1939_txobj_cfg_t ocfg[OBJ_MAX];
static j1939_txobj_t obj[OBJ_MAX];
static j1939_cfg_t cfg;
static j1939_t s;
static j1939_ca_id_t ca;
static uint8_t own = OWN;

static const uint8_t ff[LONG] = {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
                                 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
                                 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU};
static const uint8_t data_a[LONG] = {1U,  2U,  3U,  4U,  5U,  6U,  7U,  8U,  9U,  10U,
                                     11U, 12U, 13U, 14U, 15U, 16U, 17U, 18U, 19U, 20U};
static const uint8_t data_b[LONG] = {21U, 22U, 23U, 24U, 25U, 26U, 27U, 28U, 29U, 30U,
                                     31U, 32U, 33U, 34U, 35U, 36U, 37U, 38U, 39U, 40U};

static uint32_t make_id(uint8_t prio, uint32_t pgn, uint8_t da, uint8_t sa) {
	uint32_t id = 0U;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_id_build(prio, pgn, da, sa, &id));
	return id;
}

static void rx_raw(uint32_t id, const uint8_t *d, uint8_t len) {
	j1939_port_frame_t f;

	j1939_port_frame_build(&f, id, d, len);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_rx(&s, &f));
}

static void rx_request(uint8_t sa, uint8_t da, uint32_t pgn) {
	const uint8_t d[3] = {(uint8_t)pgn, (uint8_t)(pgn >> 8), (uint8_t)(pgn >> 16)};

	rx_raw(make_id(6U, J1939_PGN_REQUEST, da, sa), d, 3U);
}

static void process(uint32_t us) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(&s, us));
}

static void expect_none(void) {
	TEST_ASSERT_EQUAL_UINT16(0U, s.tx.ring.count);
}

static void expect_frame(uint32_t id, const uint8_t *d, uint8_t len) {
	const j1939_port_frame_t *f = j1939_tx_peek(&s);

	TEST_ASSERT_NOT_NULL(f);
	TEST_ASSERT_EQUAL_HEX32(id, j1939_port_frame_id_get(f));
	TEST_ASSERT_EQUAL_UINT8(len, j1939_port_frame_len_get(f));
	TEST_ASSERT_EQUAL_HEX8_ARRAY(d, j1939_port_frame_data(f), len);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_tx_pop(&s));
}

/* Expects a single frame of 8 bytes of pgn to da. */
static void expect_obj(uint32_t pgn, uint8_t da, const uint8_t *d) {
	expect_frame(make_id(6U, pgn, da, own), d, 8U);
}

/* Collects the data packets of a BAM of LONG bytes whose TP.CM was already taken. */
static void expect_bam_packets(const uint8_t *d) {
	const uint8_t packets = (uint8_t)((LONG + 6U) / 7U);
	uint8_t got[LONG + 7U];
	uint8_t next = 1U;
	uint32_t calls;

	for (calls = 0U; (next <= packets) && (calls < 10U); calls++) {
		const j1939_port_frame_t *f;

		process(GAP);
		while ((next <= packets) && ((f = j1939_tx_peek(&s)) != NULL)) {
			TEST_ASSERT_EQUAL_HEX32(
			        make_id(6U, J1939_PGN_TP_DT, J1939_ADDR_GLOBAL, own),
			        j1939_port_frame_id_get(f));
			TEST_ASSERT_EQUAL_UINT8(next, j1939_port_frame_data(f)[0]);
			(void)memcpy(&got[(next - 1U) * 7U], &j1939_port_frame_data(f)[1], 7U);
			next++;
			(void)j1939_tx_pop(&s);
		}
	}
	TEST_ASSERT_EQUAL_UINT8(packets + 1U, next);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(d, got, LONG);
}

/* Expects the TP.CM BAM of LONG bytes of pgn. */
static void expect_bam_cm(uint32_t pgn) {
	const uint8_t cm[8] = {BAM,
	                       (uint8_t)LONG,
	                       0U,
	                       (uint8_t)((LONG + 6U) / 7U),
	                       0xFFU,
	                       (uint8_t)pgn,
	                       (uint8_t)(pgn >> 8),
	                       (uint8_t)(pgn >> 16)};

	expect_frame(make_id(6U, J1939_PGN_TP_CM, J1939_ADDR_GLOBAL, own), cm, 8U);
}

/* Expects a BAM of LONG bytes; the packets are collected with further calls of GAP each. */
static void expect_bam(uint32_t pgn, const uint8_t *d) {
	expect_bam_cm(pgn);
	expect_bam_packets(d);
}

/* Expects LONG bytes to da with RTS/CTS; the peer asks for all packets at once. */
static void expect_rts(uint32_t pgn, uint8_t da, const uint8_t *d) {
	const uint8_t packets = (uint8_t)((LONG + 6U) / 7U);
	const uint8_t rts[8] = {RTS,
	                        (uint8_t)LONG,
	                        0U,
	                        packets,
	                        0xFFU,
	                        (uint8_t)pgn,
	                        (uint8_t)(pgn >> 8),
	                        (uint8_t)(pgn >> 16)};
	const uint8_t cts[8] = {0x11U,
	                        packets,
	                        1U,
	                        0xFFU,
	                        0xFFU,
	                        (uint8_t)pgn,
	                        (uint8_t)(pgn >> 8),
	                        (uint8_t)(pgn >> 16)};
	const uint8_t eoma[8] = {0x13U,
	                         (uint8_t)LONG,
	                         0U,
	                         packets,
	                         0xFFU,
	                         (uint8_t)pgn,
	                         (uint8_t)(pgn >> 8),
	                         (uint8_t)(pgn >> 16)};
	uint8_t got[LONG + 7U];
	uint8_t i;

	expect_frame(make_id(6U, J1939_PGN_TP_CM, da, own), rts, 8U);
	rx_raw(make_id(7U, J1939_PGN_TP_CM, own, da), cts, 8U);
	process(0U);
	for (i = 1U; i <= packets; i++) {
		const j1939_port_frame_t *f = j1939_tx_peek(&s);

		TEST_ASSERT_NOT_NULL(f);
		TEST_ASSERT_EQUAL_HEX32(make_id(6U, J1939_PGN_TP_DT, da, own),
		                        j1939_port_frame_id_get(f));
		TEST_ASSERT_EQUAL_UINT8(i, j1939_port_frame_data(f)[0]);
		(void)memcpy(&got[(i - 1U) * 7U], &j1939_port_frame_data(f)[1], 7U);
		(void)j1939_tx_pop(&s);
	}
	TEST_ASSERT_EQUAL_HEX8_ARRAY(d, got, LONG);
	rx_raw(make_id(7U, J1939_PGN_TP_CM, own, da), eoma, 8U);
}

static void tx_fill(void) {
	const j1939_msg_t msg = {.pgn = 0xFF00U,
	                         .prio = 6U,
	                         .sa = 0U,
	                         .da = J1939_ADDR_GLOBAL,
	                         .len = 0U,
	                         .data = NULL};

	while (s.tx.ring.count < TX_LEN) {
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_send(&s, ca, &msg));
	}
}

static void tx_drain(void) {
	while (j1939_tx_pop(&s) == J1939_RET_OK) {
	}
}

/* Object i: 8 bytes of pgn to the global address from ca. */
static void obj_def(uint16_t i, uint32_t pgn, uint32_t period, uint32_t inhibit) {
	ocfg[i] = (j1939_txobj_cfg_t){.buf = bufs[i],
	                              .pgn = pgn,
	                              .period_us = period,
	                              .inhibit_us = inhibit,
	                              .len = 8U,
	                              .prio = 6U,
	                              .da = J1939_ADDR_GLOBAL,
	                              .ca = ca};
}

static void objs_init(uint16_t n) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_txobj_init(&s, ocfg, obj, n));
}

static void set(uint16_t i, const uint8_t *d) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_txobj_set(&s, i, d, ocfg[i].len));
}

static void stack_setup(uint8_t address, uint64_t name) {
	cfg = (j1939_cfg_t){
	        .tx_buf = tx_buf,
	        .tx_len = TX_LEN,
	        .msg_buf = msg_buf,
	        .msg_len = MSG_LEN,
	        .tp_tx_buf = tp_tx,
	        .tp_tx_buf_len = 1U,
	};
	own = address;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&s, &cfg));
	TEST_ASSERT_EQUAL(
	        J1939_RET_OK,
	        j1939_ca_add(&s, &(j1939_ca_cfg_t){.address = address, .name = name}, &ca));
	(void)memset(bufs, 0, sizeof(bufs));
}

/* Claims OWN: Address Claimed goes first; objects due on the claim follow in the queue. */
static void start(void) {
	process(0U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_tx_pop(&s));
}

void setUp(void) {
	stack_setup(OWN, NAME);
}

void tearDown(void) {
}

static void test_state_is_small(void) {
	TEST_ASSERT_LESS_OR_EQUAL_UINT32(16U, (uint32_t)sizeof(j1939_txobj_t));
}

static void test_nothing_before_the_claim_then_not_available(void) {
	stack_setup(SELF, NAME);
	obj_def(0U, PGN_A, PERIOD, 0U);
	objs_init(1U);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(ff, bufs[0], 8U);
	process(0U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_tx_pop(&s)); /* Address Claimed */
	expect_none();
	process(J1939_ADDR_CLAIM_WAIT_US - 1U);
	expect_none();
	/* Claimed: the first send in the same call, all "not available". */
	process(1U);
	expect_obj(PGN_A, J1939_ADDR_GLOBAL, ff);
	expect_none();
	process(PERIOD - 1U);
	expect_none();
	process(1U);
	expect_obj(PGN_A, J1939_ADDR_GLOBAL, ff);
}

static void test_period_boundaries_and_phase(void) {
	obj_def(0U, PGN_A, PERIOD, 0U);
	objs_init(1U);
	start();
	expect_obj(PGN_A, J1939_ADDR_GLOBAL, ff);
	set(0U, data_a);
	process(PERIOD - 1U);
	expect_none();
	process(1U);
	expect_obj(PGN_A, J1939_ADDR_GLOBAL, data_a);
	process(1U);
	expect_none();
	/* A late call sends once and keeps the phase. */
	process((2U * PERIOD) + (PERIOD / 2U) - 1U);
	expect_obj(PGN_A, J1939_ADDR_GLOBAL, data_a);
	expect_none();
	process((PERIOD / 2U) - 1U);
	expect_none();
	process(1U);
	expect_obj(PGN_A, J1939_ADDR_GLOBAL, data_a);
	TEST_ASSERT_EQUAL_UINT32(0U, j1939_stats_get(&s)->txobj_tx_retry);
	TEST_ASSERT_EQUAL_UINT32(0U, j1939_stats_get(&s)->txobj_tx_dropped);
}

static void test_change_trigger_honours_the_inhibit_time(void) {
	obj_def(0U, PGN_A, 0U, INHIBIT);
	objs_init(1U);
	start();
	/* A change object is sent once on the claim. */
	expect_obj(PGN_A, J1939_ADDR_GLOBAL, ff);
	set(0U, data_a);
	process(INHIBIT - 1U);
	expect_none();
	process(1U);
	expect_obj(PGN_A, J1939_ADDR_GLOBAL, data_a);
	/* The same payload again is no change. */
	set(0U, data_a);
	process(10U * INHIBIT);
	expect_none();
	/* Inhibit time long over: sent with the next call. */
	set(0U, data_b);
	process(0U);
	expect_obj(PGN_A, J1939_ADDR_GLOBAL, data_b);
	set(0U, data_a);
	set(0U, data_b);
	process(INHIBIT - 1U);
	expect_none();
	process(1U);
	expect_obj(PGN_A, J1939_ADDR_GLOBAL, data_b);
	expect_none();
}

static void test_periodic_send_serves_a_change(void) {
	obj_def(0U, PGN_A, 200000U, 150000U);
	objs_init(1U);
	start();
	expect_obj(PGN_A, J1939_ADDR_GLOBAL, ff); /* one send for both triggers */
	expect_none();
	set(0U, data_a);
	process(150000U);
	expect_obj(PGN_A, J1939_ADDR_GLOBAL, data_a);
	/* The change send does not move the periodic phase; the periodic send serves the next
	 * change. */
	set(0U, data_b);
	process(49999U);
	expect_none();
	process(1U);
	expect_obj(PGN_A, J1939_ADDR_GLOBAL, data_b);
	process(100000U);
	expect_none();
	process(99999U);
	expect_none();
	process(1U);
	expect_obj(PGN_A, J1939_ADDR_GLOBAL, data_b);
}

static void test_inhibit_zero_has_no_change_trigger(void) {
	obj_def(0U, PGN_A, PERIOD, 0U);
	obj_def(1U, PGN_B, 0U, 0U); /* Request only */
	objs_init(2U);
	start();
	expect_obj(PGN_A, J1939_ADDR_GLOBAL, ff);
	expect_none();
	set(0U, data_a);
	set(1U, data_a);
	process(PERIOD - 1U);
	expect_none();
	process(1U);
	expect_obj(PGN_A, J1939_ADDR_GLOBAL, data_a);
	expect_none();
}

static void test_full_tx_queue_is_retried_and_counted(void) {
	obj_def(0U, PGN_A, PERIOD, 0U);
	objs_init(1U);
	start();
	expect_obj(PGN_A, J1939_ADDR_GLOBAL, ff);
	tx_fill();
	process(PERIOD);
	TEST_ASSERT_EQUAL_UINT32(1U, j1939_stats_get(&s)->txobj_tx_retry);
	process(PERIOD - 1U);
	TEST_ASSERT_EQUAL_UINT32(2U, j1939_stats_get(&s)->txobj_tx_retry);
	TEST_ASSERT_EQUAL_UINT32(0U, j1939_stats_get(&s)->txobj_tx_dropped);
	process(1U); /* the next deadline finds the send still due */
	TEST_ASSERT_EQUAL_UINT32(1U, j1939_stats_get(&s)->txobj_tx_dropped);
	TEST_ASSERT_EQUAL_UINT32(3U, j1939_stats_get(&s)->txobj_tx_retry);
	tx_drain();
	process(0U);
	expect_obj(PGN_A, J1939_ADDR_GLOBAL, ff);
	expect_none();
	/* Long call periods saturate the timers. */
	tx_fill();
	process(UINT32_MAX);
	process(UINT32_MAX);
	TEST_ASSERT_EQUAL_UINT32(2U, j1939_stats_get(&s)->txobj_tx_dropped);
}

static void test_multi_packet_objects_use_bam_and_wait_when_busy(void) {
	obj_def(0U, PGN_A, 1000000U, 0U);
	obj_def(1U, PGN_B, 1000000U, 0U);
	ocfg[0].len = LONG;
	ocfg[1].len = LONG;
	objs_init(2U);
	set(0U, data_a);
	set(1U, data_b);
	start();
	/* The CA has one BAM at a time: the second object waits for it. */
	TEST_ASSERT_EQUAL_UINT32(1U, j1939_stats_get(&s)->txobj_tx_retry);
	expect_bam(PGN_A, data_a);
	process(GAP);
	expect_bam(PGN_B, data_b);
	expect_none();
	TEST_ASSERT_EQUAL_UINT32(0U, j1939_stats_get(&s)->txobj_tx_dropped);
}

static void test_lost_address_stops_and_the_next_claim_restarts(void) {
	const uint8_t winner[8] = {0x01U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};

	stack_setup(OWN, NAME | (1ULL << 63)); /* arbitrary address capable */
	obj_def(0U, PGN_A, PERIOD, 0U);
	obj_def(1U, PGN_B, 0U, 0U);
	objs_init(2U);
	start();
	expect_obj(PGN_A, J1939_ADDR_GLOBAL, ff);
	tx_fill();
	rx_request(OTHER, OWN, PGN_B);
	process(0U); /* the answer waits for room */
	rx_raw(make_id(6U, J1939_PGN_ADDRESS_CLAIMED, J1939_ADDR_GLOBAL, OWN), winner, 8U);
	tx_drain();
	process(PERIOD);
	/* The pending answer is dropped; the CA claims 0x80 and waits. */
	TEST_ASSERT_EQUAL_UINT32(1U, j1939_stats_get(&s)->txobj_tx_dropped);
	TEST_ASSERT_EQUAL_UINT16(1U, s.tx.ring.count);
	tx_drain();
	process(J1939_ADDR_CLAIM_WAIT_US - 1U);
	expect_none();
	own = SELF;
	process(1U);
	expect_obj(PGN_A, J1939_ADDR_GLOBAL, ff);
	expect_none();
}

static void test_request_is_answered_from_the_object(void) {
	obj_def(0U, PGN_A, 0U, 0U);
	objs_init(1U);
	start();
	expect_none();
	set(0U, data_a);
	rx_request(OTHER, J1939_ADDR_GLOBAL, PGN_A);
	process(0U);
	expect_obj(PGN_A, J1939_ADDR_GLOBAL, data_a);
	/* A single frame of a PDU2 PGN goes to the global address. */
	rx_request(OTHER, OWN, PGN_A);
	rx_request(OTHER2, OWN, PGN_A);
	process(0U);
	expect_obj(PGN_A, J1939_ADDR_GLOBAL, data_a);
	expect_none();
	TEST_ASSERT_NULL(j1939_msg_peek(&s));
}

static void test_broadcast_serves_a_global_answer(void) {
	obj_def(0U, PGN_A, PERIOD, 0U);
	objs_init(1U);
	start();
	expect_obj(PGN_A, J1939_ADDR_GLOBAL, ff);
	rx_request(OTHER, OWN, PGN_A);
	process(PERIOD);
	expect_obj(PGN_A, J1939_ADDR_GLOBAL, ff);
	expect_none();
	process(0U);
	expect_none();
}

static void test_multi_packet_answers(void) {
	obj_def(0U, PGN_A, 0U, 0U);
	ocfg[0].len = LONG;
	objs_init(1U);
	start();
	set(0U, data_a);
	/* Destination specific: RTS/CTS to the requester. */
	rx_request(OTHER, OWN, PGN_A);
	process(0U);
	expect_rts(PGN_A, OTHER, data_a);
	process(0U);
	expect_none();
	/* Global: BAM. */
	rx_request(OTHER, J1939_ADDR_GLOBAL, PGN_A);
	process(0U);
	expect_bam(PGN_A, data_a);
	/* Two requesters while the answer is pending: one BAM. */
	rx_request(OTHER, OWN, PGN_A);
	rx_request(OTHER2, OWN, PGN_A);
	rx_request(OTHER2, OWN, PGN_A);
	process(0U);
	expect_bam(PGN_A, data_a);
	expect_none();
	TEST_ASSERT_EQUAL_UINT32(0U, j1939_stats_get(&s)->tp_tx_aborted);
}

static void test_pdu1_object_and_answers(void) {
	obj_def(0U, PGN_P1, PERIOD, 0U);
	ocfg[0].da = 0x30U;
	obj_def(1U, PGN_P1, 0U, 0U); /* same PGN to everybody: the first object answers */
	objs_init(2U);
	start();
	expect_obj(PGN_P1, 0x30U, ff);
	expect_none();
	set(0U, data_a);
	set(1U, data_b);
	rx_request(OTHER, OWN, PGN_P1);
	process(0U);
	expect_obj(PGN_P1, OTHER, data_a);
	rx_request(OTHER, J1939_ADDR_GLOBAL, PGN_P1);
	process(0U);
	expect_obj(PGN_P1, J1939_ADDR_GLOBAL, data_a);
	expect_none();
}

static void test_answer_not_sent_in_time_is_dropped(void) {
	obj_def(0U, PGN_A, 0U, 0U);
	objs_init(1U);
	start();
	tx_fill();
	rx_request(OTHER, OWN, PGN_A);
	process(0U);
	process(TR - 1U);
	TEST_ASSERT_EQUAL_UINT32(0U, j1939_stats_get(&s)->txobj_tx_dropped);
	TEST_ASSERT_EQUAL_UINT32(2U, j1939_stats_get(&s)->txobj_tx_retry);
	process(1U);
	TEST_ASSERT_EQUAL_UINT32(1U, j1939_stats_get(&s)->txobj_tx_dropped);
	tx_drain();
	process(0U);
	expect_none();
}

static void test_requests_for_other_cas_and_during_the_claim(void) {
	j1939_ca_id_t ca2 = 0U;

	stack_setup(SELF, NAME);
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_ca_add(&s, &(j1939_ca_cfg_t){.address = OWN2, .name = 2U}, &ca2));
	obj_def(0U, PGN_A, 0U, 0U);
	objs_init(1U);
	process(0U);
	tx_drain(); /* both Address Claimed */
	/* The object's CA is still claiming: neither answer nor NACK. */
	rx_request(OTHER, SELF, PGN_A);
	process(0U);
	expect_none();
	/* The other CA has no such object: NACK as for any unsupported PGN. */
	rx_request(OTHER, OWN2, PGN_A);
	{
		const uint8_t nack[8] = {
		        J1939_ACK_CTRL_NACK,   0xFFU, 0xFFU, 0xFFU, OTHER, (uint8_t)PGN_A,
		        (uint8_t)(PGN_A >> 8), 0U};

		expect_frame(make_id(6U, J1939_PGN_ACK, J1939_ADDR_GLOBAL, OWN2), nack, 8U);
	}
	process(J1939_ADDR_CLAIM_WAIT_US);
	own = SELF;
	expect_none();
	/* A Request to another node is not answered. */
	rx_request(OTHER, 0x20U, PGN_A);
	process(0U);
	expect_none();
	rx_request(OTHER, SELF, PGN_A);
	process(0U);
	expect_obj(PGN_A, J1939_ADDR_GLOBAL, ff);
	expect_none();
}

static void test_objects_of_two_cas(void) {
	j1939_ca_id_t ca2 = 0U;

	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_ca_add(&s, &(j1939_ca_cfg_t){.address = OWN2, .name = 2U}, &ca2));
	obj_def(0U, PGN_A, 0U, 0U);
	obj_def(1U, PGN_A, 0U, 0U);
	ocfg[1].ca = ca2;
	objs_init(2U);
	process(0U);
	tx_drain();
	set(0U, data_a);
	set(1U, data_b);
	rx_request(OTHER, J1939_ADDR_GLOBAL, PGN_A);
	process(0U);
	expect_obj(PGN_A, J1939_ADDR_GLOBAL, data_a);
	own = OWN2;
	expect_obj(PGN_A, J1939_ADDR_GLOBAL, data_b);
	expect_none();
}

static void test_init_validates_every_entry(void) {
	j1939_t s2;
	const uint32_t req_pgns[] = {PGN_B};
	const uint32_t owned[] = {J1939_PGN_REQUEST,
	                          J1939_PGN_ACK,
	                          J1939_PGN_TP_CM,
	                          J1939_PGN_TP_DT,
	                          J1939_PGN_ADDRESS_CLAIMED,
	                          J1939_PGN_COMMANDED_ADDRESS,
	                          J1939_PGN_DM1,
	                          J1939_PGN_DM2,
	                          J1939_PGN_DM3,
	                          J1939_PGN_DM11};
	uint32_t i;

	obj_def(0U, PGN_A, PERIOD, 0U);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_txobj_init(NULL, ocfg, obj, 1U));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_txobj_init(&s, NULL, obj, 1U));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_txobj_init(&s, ocfg, NULL, 1U));
	ocfg[0].buf = NULL;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_txobj_init(&s, ocfg, obj, 1U));
	obj_def(0U, PGN_A, PERIOD, 0U);
	ocfg[0].ca = 1U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_txobj_init(&s, ocfg, obj, 1U));
	obj_def(0U, 0xEF01U, PERIOD, 0U);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_txobj_init(&s, ocfg, obj, 1U));
	obj_def(0U, 0x40000U, PERIOD, 0U);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_txobj_init(&s, ocfg, obj, 1U));
	obj_def(0U, PGN_A, PERIOD, 0U);
	ocfg[0].prio = 8U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_txobj_init(&s, ocfg, obj, 1U));
	obj_def(0U, PGN_A, PERIOD, 0U);
	ocfg[0].len = 0U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_txobj_init(&s, ocfg, obj, 1U));
	ocfg[0].len = (uint16_t)(J1939_CFG_TP_BUF_SIZE + 1U);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_txobj_init(&s, ocfg, obj, 1U));
	/* Single frame PDU2 needs the global address; multi-packet goes with RTS/CTS. */
	obj_def(0U, PGN_A, PERIOD, 0U);
	ocfg[0].da = OTHER;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_txobj_init(&s, ocfg, obj, 1U));
	ocfg[0].len = LONG;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_txobj_init(&s, ocfg, obj, 1U));
	obj_def(0U, PGN_P1, PERIOD, 0U);
	ocfg[0].da = J1939_ADDR_NULL;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_txobj_init(&s, ocfg, obj, 1U));
	for (i = 0U; i < (sizeof(owned) / sizeof(owned[0])); i++) {
		obj_def(0U, owned[i], PERIOD, 0U);
		TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_txobj_init(&s, ocfg, obj, 1U));
	}
	/* Duplicate CA, PGN and destination. */
	obj_def(0U, PGN_A, PERIOD, 0U);
	obj_def(1U, PGN_A, 0U, INHIBIT);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_txobj_init(&s, ocfg, obj, 2U));
	/* A rejected table leaves the objects in use. */
	TEST_ASSERT_EQUAL_UINT16(1U, s.txobj_len);
	/* len 0 removes the objects. */
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_txobj_init(&s, NULL, NULL, 0U));
	TEST_ASSERT_EQUAL_UINT16(0U, s.txobj_len);
	/* A PGN the application answers; multi-packet without a transport protocol buffer. */
	cfg.req_pgns = req_pgns;
	cfg.req_pgns_len = 1U;
	cfg.tp_tx_buf = NULL;
	cfg.tp_tx_buf_len = 0U;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&s2, &cfg));
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_ca_add(&s2, &(j1939_ca_cfg_t){.address = OWN, .name = NAME}, &ca));
	obj_def(0U, PGN_B, PERIOD, 0U);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_txobj_init(&s2, ocfg, obj, 1U));
	obj_def(0U, PGN_A, PERIOD, 0U);
	ocfg[0].len = 9U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_txobj_init(&s2, ocfg, obj, 1U));
	ocfg[0].len = 8U;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_txobj_init(&s2, ocfg, obj, 1U));
}

static void test_set_rejects_invalid_input(void) {
	obj_def(0U, PGN_A, PERIOD, 0U);
	objs_init(1U);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_txobj_set(NULL, 0U, data_a, 8U));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_txobj_set(&s, 0U, NULL, 8U));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_txobj_set(&s, 1U, data_a, 8U));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_txobj_set(&s, 0U, data_a, 7U));
	TEST_ASSERT_EQUAL_HEX8_ARRAY(ff, bufs[0], 8U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_txobj_set(&s, 0U, data_a, 8U));
	TEST_ASSERT_EQUAL_HEX8_ARRAY(data_a, bufs[0], 8U);
}

static void test_broadcast_keeps_a_directed_multi_packet_answer(void) {
	obj_def(0U, PGN_A, 1000000U, 0U);
	ocfg[0].len = LONG;
	objs_init(1U);
	start();
	expect_bam(PGN_A, ff); /* on the claim; takes 3 gaps */
	set(0U, data_a);
	rx_request(OTHER, OWN, PGN_A);
	/* The periodic BAM falls due before the answer goes out. The answer would go to the
	 * requester with RTS/CTS, so the broadcast does not serve it: it waits for the only TP
	 * transmit buffer and follows the BAM. */
	process(1000000U - (3U * GAP));
	expect_bam_cm(PGN_A);
	expect_none();
	expect_bam_packets(data_a);
	expect_rts(PGN_A, OTHER, data_a);
	process(0U);
	expect_none();
	TEST_ASSERT_GREATER_THAN_UINT32(0U, j1939_stats_get(&s)->txobj_tx_retry);
	TEST_ASSERT_EQUAL_UINT32(0U, j1939_stats_get(&s)->tp_tx_aborted);
	TEST_ASSERT_EQUAL_UINT32(0U, j1939_stats_get(&s)->txobj_tx_dropped);
}

static void test_repeated_request_of_one_requester_is_answered_to_it(void) {
	obj_def(0U, PGN_A, 0U, 0U);
	ocfg[0].len = LONG;
	objs_init(1U);
	start();
	set(0U, data_a);
	rx_request(OTHER, OWN, PGN_A);
	rx_request(OTHER, OWN, PGN_A);
	process(0U);
	expect_rts(PGN_A, OTHER, data_a);
	process(0U);
	expect_none();
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_state_is_small);
	RUN_TEST(test_nothing_before_the_claim_then_not_available);
	RUN_TEST(test_period_boundaries_and_phase);
	RUN_TEST(test_change_trigger_honours_the_inhibit_time);
	RUN_TEST(test_periodic_send_serves_a_change);
	RUN_TEST(test_inhibit_zero_has_no_change_trigger);
	RUN_TEST(test_full_tx_queue_is_retried_and_counted);
	RUN_TEST(test_multi_packet_objects_use_bam_and_wait_when_busy);
	RUN_TEST(test_lost_address_stops_and_the_next_claim_restarts);
	RUN_TEST(test_request_is_answered_from_the_object);
	RUN_TEST(test_broadcast_serves_a_global_answer);
	RUN_TEST(test_multi_packet_answers);
	RUN_TEST(test_pdu1_object_and_answers);
	RUN_TEST(test_answer_not_sent_in_time_is_dropped);
	RUN_TEST(test_requests_for_other_cas_and_during_the_claim);
	RUN_TEST(test_objects_of_two_cas);
	RUN_TEST(test_init_validates_every_entry);
	RUN_TEST(test_set_rejects_invalid_input);
	RUN_TEST(test_broadcast_keeps_a_directed_multi_packet_answer);
	RUN_TEST(test_repeated_request_of_one_requester_is_answered_to_it);
	return UNITY_END();
}
