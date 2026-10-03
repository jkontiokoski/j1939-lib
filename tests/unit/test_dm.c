/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/* Diagnostics in the stack: DM1 timing and triggers, DM1/DM2 Requests, DM3/DM11 clearing. */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "j1939/j1939.h"

#define TX_LEN   8U
#define MSG_LEN  2U
#define DTC_LEN  4U
#define HOLD_LEN 2U

#define OWN    0x10U
#define OTHER  0x42U
#define OTHER2 0x43U
#define SELF   0x80U /* self-configurable: claimed after the contention wait */
#define NAME   0x1000U

#define PERIOD J1939_DM1_PERIOD_US
#define TR     J1939_DM_RESPONSE_US
#define GAP    J1939_CFG_TP_BAM_GAP_US

#define BAM 0x20U

static const j1939_diag_dtc_t dtc_a = {100U, 3U, 1U, J1939_DIAG_CM_V4};
static const j1939_diag_dtc_t dtc_b = {0x7FFFFU, 31U, 126U, J1939_DIAG_CM_V4};
static const j1939_diag_dtc_t dtc_c = {5000U, 1U, 127U, J1939_DIAG_CM_V4};
static const j1939_diag_dtc_t dtc_d = {6000U, 2U, 2U, J1939_DIAG_CM_V4};

static j1939_port_frame_t tx_buf[TX_LEN];
static j1939_msg_slot_t msg_buf[MSG_LEN];
static j1939_tp_buf_t tp_tx[1];
static j1939_diag_dtc_t active[DTC_LEN];
static j1939_diag_dtc_t prev[DTC_LEN];
static j1939_dm_hold_t hold[HOLD_LEN];
static uint8_t dm_buf[J1939_DM_BUF_LEN(DTC_LEN)];
static j1939_dm_t dm;
static j1939_dm_cfg_t dm_cfg;
static j1939_cfg_t cfg;
static j1939_t s;
static j1939_ca_id_t ca;
static j1939_diag_lamps_t lamps;
static uint8_t own = OWN;

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

static void expect_ack(uint8_t ctrl, uint8_t requester, uint32_t pgn) {
	const uint8_t d[8] = {ctrl,
	                      0xFFU,
	                      0xFFU,
	                      0xFFU,
	                      requester,
	                      (uint8_t)pgn,
	                      (uint8_t)(pgn >> 8),
	                      (uint8_t)(pgn >> 16)};

	expect_frame(make_id(6U, J1939_PGN_ACK, J1939_ADDR_GLOBAL, own), d, 8U);
}

/*
 * Expects a DM1 or DM2 with the current lamps and dtcs. A payload of more
 * than 8 bytes must come as BAM: its packets are collected with further
 * j1939_process() calls of GAP each.
 */
static void expect_dm(uint32_t pgn, const j1939_diag_dtc_t *dtcs, uint16_t n) {
	uint8_t ref[J1939_DM_BUF_LEN(DTC_LEN)];
	uint16_t len = 0U;

	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_diag_dm_build(&lamps, dtcs, n, ref, sizeof(ref), &len));
	if (len <= 8U) {
		expect_frame(make_id(6U, pgn, J1939_ADDR_GLOBAL, own), ref, (uint8_t)len);
	} else {
		const uint8_t packets = (uint8_t)((len + 6U) / 7U);
		const uint8_t cm[8] = {
		        BAM,   (uint8_t)len, (uint8_t)(len >> 8), packets,
		        0xFFU, (uint8_t)pgn, (uint8_t)(pgn >> 8), (uint8_t)(pgn >> 16)};
		uint8_t got[J1939_DM_BUF_LEN(DTC_LEN) + 7U];
		uint8_t next = 1U;
		uint32_t calls;

		expect_frame(make_id(6U, J1939_PGN_TP_CM, J1939_ADDR_GLOBAL, own), cm, 8U);
		for (calls = 0U; (next <= packets) && (calls < 10U); calls++) {
			const j1939_port_frame_t *f;

			process(GAP);
			while ((next <= packets) && ((f = j1939_tx_peek(&s)) != NULL)) {
				TEST_ASSERT_EQUAL_HEX32(
				        make_id(6U, J1939_PGN_TP_DT, J1939_ADDR_GLOBAL, own),
				        j1939_port_frame_id_get(f));
				TEST_ASSERT_EQUAL_UINT8(next, j1939_port_frame_data(f)[0]);
				(void)memcpy(&got[(next - 1U) * 7U], &j1939_port_frame_data(f)[1],
				             7U);
				next++;
				(void)j1939_tx_pop(&s);
			}
		}
		TEST_ASSERT_EQUAL_UINT8(packets + 1U, next);
		TEST_ASSERT_EQUAL_HEX8_ARRAY(ref, got, len);
	}
}

/* Expects a multi-packet DM to da with RTS/CTS; the peer asks for all packets at once. */
static void expect_dm_rts(uint32_t pgn, const j1939_diag_dtc_t *dtcs, uint16_t n, uint8_t da) {
	uint8_t ref[J1939_DM_BUF_LEN(DTC_LEN)];
	uint8_t got[J1939_DM_BUF_LEN(DTC_LEN) + 7U];
	uint16_t len = 0U;
	uint8_t packets;
	uint8_t i;

	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_diag_dm_build(&lamps, dtcs, n, ref, sizeof(ref), &len));
	TEST_ASSERT_GREATER_THAN_UINT16(8U, len);
	packets = (uint8_t)((len + 6U) / 7U);
	{
		const uint8_t rts[8] = {
		        0x10U, (uint8_t)len, (uint8_t)(len >> 8), packets,
		        0xFFU, (uint8_t)pgn, (uint8_t)(pgn >> 8), (uint8_t)(pgn >> 16)};
		const uint8_t cts[8] = {0x11U,
		                        packets,
		                        1U,
		                        0xFFU,
		                        0xFFU,
		                        (uint8_t)pgn,
		                        (uint8_t)(pgn >> 8),
		                        (uint8_t)(pgn >> 16)};

		expect_frame(make_id(6U, J1939_PGN_TP_CM, da, own), rts, 8U);
		rx_raw(make_id(7U, J1939_PGN_TP_CM, own, da), cts, 8U);
	}
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
	TEST_ASSERT_EQUAL_HEX8_ARRAY(ref, got, len);
	{
		const uint8_t eoma[8] = {
		        0x13U, (uint8_t)len, (uint8_t)(len >> 8), packets,
		        0xFFU, (uint8_t)pgn, (uint8_t)(pgn >> 8), (uint8_t)(pgn >> 16)};

		rx_raw(make_id(7U, J1939_PGN_TP_CM, own, da), eoma, 8U);
	}
}

static void expect_dm1(const j1939_diag_dtc_t *dtcs, uint16_t n) {
	expect_dm(J1939_PGN_DM1, dtcs, n);
}

static void set_active(const j1939_diag_dtc_t *dtcs, uint16_t n) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_dm_active_set(&s, ca, dtcs, n));
}

static void set_prev(const j1939_diag_dtc_t *dtcs, uint16_t n) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_dm_prev_set(&s, ca, dtcs, n));
}

/* Fills the tx queue with frames that are not DMs. */
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

static void stack_setup(uint8_t address, bool dm3, bool dm11) {
	cfg = (j1939_cfg_t){
	        .tx_buf = tx_buf,
	        .tx_len = TX_LEN,
	        .msg_buf = msg_buf,
	        .msg_len = MSG_LEN,
	        .tp_tx_buf = tp_tx,
	        .tp_tx_buf_len = 1U,
	};
	dm_cfg = (j1939_dm_cfg_t){
	        .active = active,
	        .active_len = DTC_LEN,
	        .prev = prev,
	        .prev_len = DTC_LEN,
	        .hold = hold,
	        .hold_len = HOLD_LEN,
	        .buf = dm_buf,
	        .buf_len = sizeof(dm_buf),
	        .dm3_enable = dm3,
	        .dm11_enable = dm11,
	};
	own = address;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&s, &cfg));
	TEST_ASSERT_EQUAL(
	        J1939_RET_OK,
	        j1939_ca_add(&s, &(j1939_ca_cfg_t){.address = address, .name = NAME}, &ca));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_dm_init(&s, ca, &dm, &dm_cfg));
	lamps = (j1939_diag_lamps_t){0U, 0U, 0U, 0U, 3U, 3U, 3U, 3U};
}

/* Claims OWN: Address Claimed, then the first DM1 in the same call. */
static void start(void) {
	process(0U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_tx_pop(&s));
	expect_dm1(NULL, 0U);
	expect_none();
}

void setUp(void) {
	stack_setup(OWN, true, true);
}

void tearDown(void) {
}

static void test_first_dm1_waits_for_the_claim(void) {
	stack_setup(SELF, true, true);
	set_active(&dtc_a, 1U);
	process(0U);
	/* Only Address Claimed while claiming. */
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_tx_pop(&s));
	expect_none();
	rx_request(OTHER, SELF, J1939_PGN_DM1);
	rx_request(OTHER, J1939_ADDR_GLOBAL, J1939_PGN_DM2);
	rx_request(OTHER, SELF, J1939_PGN_DM3);
	process(J1939_ADDR_CLAIM_WAIT_US - 1U);
	expect_none();
	TEST_ASSERT_EQUAL(J1939_RET_ERR_EMPTY, j1939_dm_clear_get(&s, ca, &(uint32_t){0U}));
	/* Claimed: the first DM1 goes out at once. */
	process(1U);
	expect_dm1(&dtc_a, 1U);
	expect_none();
	TEST_ASSERT_EQUAL_UINT32(0U, j1939_stats_get(&s)->dm_tx_dropped);
}

static void test_dm1_period_boundaries(void) {
	start();
	process(PERIOD - 1U);
	expect_none();
	process(1U);
	expect_dm1(NULL, 0U);
	process(PERIOD - 1U);
	expect_none();
	process(1U);
	expect_dm1(NULL, 0U);
	process(1U);
	expect_none();
	/* A late call sends one DM1 and keeps the phase. */
	process((2U * PERIOD) + (PERIOD / 2U) - 1U);
	expect_dm1(NULL, 0U);
	process((PERIOD / 2U) - 1U);
	expect_none();
	process(1U);
	expect_dm1(NULL, 0U);
	TEST_ASSERT_EQUAL_UINT32(0U, j1939_stats_get(&s)->dm_tx_retry);
}

static void test_dm1_with_one_dtc_and_lamps(void) {
	const uint8_t expected[8] = {0x04U, 0xF3U, 100U, 0U, 3U, 1U, 0xFFU, 0xFFU};

	start();
	lamps.amber_warning = J1939_DIAG_LAMP_ON;
	lamps.amber_warning_flash = J1939_DIAG_FLASH_SLOW;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_dm_lamps_set(&s, ca, &lamps));
	set_active(&dtc_a, 1U);
	process(0U);
	expect_frame(0x18FECA00U | OWN, expected, 8U);
	expect_none();
}

static void test_dm1_with_many_dtcs_uses_bam(void) {
	const j1939_diag_dtc_t list[3] = {dtc_c, dtc_a, dtc_b};

	start();
	set_active(list, 3U);
	process(0U);
	expect_dm1(list, 3U);
	expect_none();
}

static void test_change_triggers_dm1_once_per_dtc_per_second(void) {
	const j1939_diag_dtc_t ab[2] = {dtc_a, dtc_b};

	start();
	process(100000U); /* t = 0.1 s */
	expect_none();

	/* A becomes active: DM1 at once. */
	set_active(&dtc_a, 1U);
	process(0U);
	expect_dm1(&dtc_a, 1U);

	/* A leaves within its hold second: no DM1. */
	set_active(NULL, 0U);
	process(100000U); /* t = 0.2 s */
	expect_none();

	/* Another DTC changes: DM1 at once. */
	set_active(&dtc_b, 1U);
	process(100000U); /* t = 0.3 s */
	expect_dm1(&dtc_b, 1U);

	/* Both held: A returns, B leaves. */
	set_active(&dtc_a, 1U);
	process(100000U); /* t = 0.4 s */
	expect_none();

	/* The periodic DM1 carries the held changes. */
	process(599999U);
	expect_none();
	process(1U); /* t = 1.0 s */
	expect_dm1(&dtc_a, 1U);

	/* A's hold (from t = 0.1 s) has run out: its change triggers again. */
	process(100000U); /* t = 1.1 s */
	set_active(ab, 2U);
	process(0U);
	expect_none();    /* B is still held until t = 1.3 s */
	process(200000U); /* t = 1.3 s */
	expect_none();
	set_active(NULL, 0U);
	process(0U);
	expect_dm1(NULL, 0U);
	expect_none();
}

static void test_change_without_free_hold_record_waits_for_period(void) {
	start();
	set_active(&dtc_a, 1U);
	process(0U);
	expect_dm1(&dtc_a, 1U);
	set_active(&dtc_b, 1U); /* A held, B takes the second record */
	process(0U);
	expect_dm1(&dtc_b, 1U);
	set_active(&dtc_c, 1U); /* B held, no record for C */
	process(0U);
	expect_none();
	process(PERIOD - 1U);
	expect_none();
	process(1U);
	expect_dm1(&dtc_c, 1U);
}

static void test_occurrence_count_and_lamps_trigger_nothing(void) {
	j1939_diag_dtc_t a2 = dtc_a;

	start();
	set_active(&dtc_a, 1U);
	process(0U);
	expect_dm1(&dtc_a, 1U);
	process(PERIOD - 1U); /* the hold of A runs out */
	expect_none();
	a2.oc = 9U;
	set_active(&a2, 1U);
	lamps.red_stop = J1939_DIAG_LAMP_ON;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_dm_lamps_set(&s, ca, &lamps));
	process(0U);
	expect_none();
	process(1U);
	expect_dm1(&a2, 1U);
}

static void test_request_for_dm1_is_answered_globally(void) {
	start();
	set_active(&dtc_a, 1U);
	process(PERIOD - 1U); /* change DM1 */
	expect_dm1(&dtc_a, 1U);
	rx_request(OTHER, OWN, J1939_PGN_DM1);
	process(0U);
	expect_dm1(&dtc_a, 1U);
	expect_none();
	TEST_ASSERT_NULL(j1939_msg_peek(&s));
	rx_request(OTHER, J1939_ADDR_GLOBAL, J1939_PGN_DM1);
	process(0U);
	expect_dm1(&dtc_a, 1U);
	expect_none();
	/* One DM1 serves the Request and the periodic one due in the same call. */
	rx_request(OTHER, OWN, J1939_PGN_DM1);
	process(1U);
	expect_dm1(&dtc_a, 1U);
	expect_none();
}

static void test_request_for_dm2_is_answered_globally(void) {
	const j1939_diag_dtc_t list[2] = {dtc_b, dtc_d};

	start();
	rx_request(OTHER, OWN, J1939_PGN_DM2);
	process(0U);
	expect_dm(J1939_PGN_DM2, NULL, 0U);
	set_prev(&dtc_c, 1U);
	rx_request(OTHER, J1939_ADDR_GLOBAL, J1939_PGN_DM2);
	rx_request(OTHER2, OWN, J1939_PGN_DM2); /* merged with the pending answer */
	process(0U);
	expect_dm(J1939_PGN_DM2, &dtc_c, 1U);
	expect_none();
	set_prev(list, 2U);
	rx_request(OTHER, J1939_ADDR_GLOBAL, J1939_PGN_DM2);
	process(0U);
	expect_dm(J1939_PGN_DM2, list, 2U);
	expect_none();
	TEST_ASSERT_NULL(j1939_msg_peek(&s));
}

static void test_multi_packet_answer_goes_to_the_requester(void) {
	const j1939_diag_dtc_t list[2] = {dtc_b, dtc_d};

	start();
	set_prev(list, 2U);
	set_active(list, 2U);
	process(0U);
	expect_dm1(list, 2U); /* the change DM1, with BAM */
	/* Destination specific: RTS/CTS to the requester. */
	rx_request(OTHER, OWN, J1939_PGN_DM2);
	process(0U);
	expect_dm_rts(J1939_PGN_DM2, list, 2U, OTHER);
	rx_request(OTHER2, OWN, J1939_PGN_DM1);
	process(0U);
	expect_dm_rts(J1939_PGN_DM1, list, 2U, OTHER2);
	process(0U);
	expect_none();
	/* Requests of two nodes while the answer is pending: one BAM. */
	rx_request(OTHER, OWN, J1939_PGN_DM2);
	rx_request(OTHER2, OWN, J1939_PGN_DM2);
	rx_request(OTHER2, OWN, J1939_PGN_DM2);
	process(0U);
	expect_dm(J1939_PGN_DM2, list, 2U);
	expect_none();
	/* One DTC is a single frame, which a PDU2 PGN sends globally. */
	set_prev(&dtc_b, 1U);
	rx_request(OTHER, OWN, J1939_PGN_DM2);
	process(0U);
	expect_dm(J1939_PGN_DM2, &dtc_b, 1U);
	expect_none();
	TEST_ASSERT_EQUAL_UINT32(0U, j1939_stats_get(&s)->tp_tx_aborted);
}

static void test_requests_for_other_nodes_or_cas_without_diagnostics(void) {
	j1939_ca_id_t ca2 = 0U;

	start();
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_ca_add(&s, &(j1939_ca_cfg_t){.address = 0x11U, .name = 2U}, &ca2));
	process(0U);
	(void)j1939_tx_pop(&s);                  /* Address Claimed of ca2 */
	rx_request(OTHER, 0x20U, J1939_PGN_DM1); /* another node */
	process(0U);
	expect_none();
	/* The CA without diagnostics refuses as for any unsupported PGN. */
	rx_request(OTHER, 0x11U, J1939_PGN_DM2);
	process(0U);
	own = 0x11U;
	expect_ack(J1939_ACK_CTRL_NACK, OTHER, J1939_PGN_DM2);
	expect_none();
	TEST_ASSERT_EQUAL(J1939_RET_ERR_STATE, j1939_dm_active_set(&s, ca2, NULL, 0U));
}

static void test_dm3_accepted_clears_previously_active(void) {
	uint32_t pgn = 0U;

	start();
	set_active(&dtc_a, 1U);
	set_prev(&dtc_b, 1U);
	process(PERIOD - 1U);
	expect_dm1(&dtc_a, 1U);
	rx_request(OTHER, OWN, J1939_PGN_DM3);
	process(0U);
	expect_none(); /* nothing cleared, nothing sent before the decision */
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_dm_clear_get(&s, ca, &pgn));
	TEST_ASSERT_EQUAL_HEX32(J1939_PGN_DM3, pgn);
	TEST_ASSERT_NULL(j1939_msg_peek(&s));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_dm_clear_confirm(&s, ca, J1939_PGN_DM3, true));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_EMPTY, j1939_dm_clear_get(&s, ca, &pgn));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_STATE, j1939_dm_clear_confirm(&s, ca, J1939_PGN_DM3, true));
	process(0U);
	expect_ack(J1939_ACK_CTRL_ACK, OTHER, J1939_PGN_DM3);
	expect_none();
	rx_request(OTHER, OWN, J1939_PGN_DM2);
	process(0U);
	expect_dm(J1939_PGN_DM2, NULL, 0U);
	process(1U); /* periodic: the active DTC stays */
	expect_dm1(&dtc_a, 1U);
}

static void test_dm11_global_accepted_clears_active_without_ack(void) {
	uint32_t pgn = 0U;

	start();
	set_active(&dtc_a, 1U);
	set_prev(&dtc_b, 1U);
	process(PERIOD - 1U);
	expect_dm1(&dtc_a, 1U);
	process(1U);
	expect_dm1(&dtc_a, 1U);
	process(PERIOD - 1U); /* A's hold has run out */
	rx_request(OTHER, J1939_ADDR_GLOBAL, J1939_PGN_DM11);
	process(0U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_dm_clear_get(&s, ca, &pgn));
	TEST_ASSERT_EQUAL_HEX32(J1939_PGN_DM11, pgn);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_dm_clear_confirm(&s, ca, J1939_PGN_DM11, true));
	/* The cleared set is a change: DM1 at once, no acknowledgement of a global request. */
	process(0U);
	expect_dm1(NULL, 0U);
	expect_none();
	rx_request(OTHER, OWN, J1939_PGN_DM2);
	process(0U);
	expect_dm(J1939_PGN_DM2, &dtc_b, 1U);
}

static void test_clear_refused_is_nacked_and_keeps_data(void) {
	start();
	set_active(&dtc_a, 1U);
	process(0U);
	expect_dm1(&dtc_a, 1U);
	rx_request(OTHER, OWN, J1939_PGN_DM11);
	process(0U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_dm_clear_confirm(&s, ca, J1939_PGN_DM11, false));
	process(0U);
	expect_ack(J1939_ACK_CTRL_NACK, OTHER, J1939_PGN_DM11);
	expect_none();
	rx_request(OTHER, OWN, J1939_PGN_DM1);
	process(0U);
	expect_dm1(&dtc_a, 1U);
	/* A refused global request sends nothing. */
	rx_request(OTHER, J1939_ADDR_GLOBAL, J1939_PGN_DM11);
	process(0U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_dm_clear_confirm(&s, ca, J1939_PGN_DM11, false));
	process(0U);
	expect_none();
}

static void test_clear_without_decision_times_out(void) {
	uint32_t pgn = 0U;

	start();
	set_prev(&dtc_b, 1U);
	rx_request(OTHER, OWN, J1939_PGN_DM3);
	process(PERIOD / 2U); /* the call receiving the request does not count */
	process(TR - 1U);
	expect_none();
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_dm_clear_get(&s, ca, &pgn));
	process(1U);
	expect_ack(J1939_ACK_CTRL_NACK, OTHER, J1939_PGN_DM3);
	expect_none();
	TEST_ASSERT_EQUAL(J1939_RET_ERR_EMPTY, j1939_dm_clear_get(&s, ca, &pgn));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_STATE, j1939_dm_clear_confirm(&s, ca, J1939_PGN_DM3, true));
	rx_request(OTHER, OWN, J1939_PGN_DM2);
	process(0U);
	expect_dm(J1939_PGN_DM2, &dtc_b, 1U);
	/* A global request times out silently. */
	rx_request(OTHER, J1939_ADDR_GLOBAL, J1939_PGN_DM3);
	process(0U);
	process(TR);
	expect_none();
	TEST_ASSERT_EQUAL(J1939_RET_ERR_EMPTY, j1939_dm_clear_get(&s, ca, &pgn));
}

static void test_clear_not_enabled_is_handled_as_unsupported(void) {
	uint32_t pgn = 0U;

	stack_setup(OWN, false, false);
	start();
	rx_request(OTHER, OWN, J1939_PGN_DM3);
	rx_request(OTHER, OWN, J1939_PGN_DM11);
	rx_request(OTHER, J1939_ADDR_GLOBAL, J1939_PGN_DM11);
	process(0U);
	expect_ack(J1939_ACK_CTRL_NACK, OTHER, J1939_PGN_DM3);
	expect_ack(J1939_ACK_CTRL_NACK, OTHER, J1939_PGN_DM11);
	expect_none();
	TEST_ASSERT_EQUAL(J1939_RET_ERR_EMPTY, j1939_dm_clear_get(&s, ca, &pgn));
}

static void test_concurrent_clear_requests(void) {
	uint32_t pgn = 0U;

	start();
	/* A global request followed by a destination specific one: the latter is acknowledged. */
	rx_request(OTHER, J1939_ADDR_GLOBAL, J1939_PGN_DM3);
	rx_request(OTHER, OWN, J1939_PGN_DM3);
	/* Another requester while one is waiting: Cannot Respond. */
	rx_request(OTHER2, OWN, J1939_PGN_DM3);
	rx_request(OTHER2, J1939_ADDR_GLOBAL, J1939_PGN_DM3); /* covered */
	rx_request(OTHER2, OWN, J1939_PGN_DM11);
	process(0U);
	expect_ack(J1939_ACK_CTRL_CANNOT_RESPOND, OTHER2, J1939_PGN_DM3);
	expect_none();
	/* DM3 is reported first. */
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_dm_clear_get(&s, ca, &pgn));
	TEST_ASSERT_EQUAL_HEX32(J1939_PGN_DM3, pgn);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_dm_clear_confirm(&s, ca, J1939_PGN_DM3, true));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_dm_clear_get(&s, ca, &pgn));
	TEST_ASSERT_EQUAL_HEX32(J1939_PGN_DM11, pgn);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_dm_clear_confirm(&s, ca, J1939_PGN_DM11, true));
	process(0U);
	expect_ack(J1939_ACK_CTRL_ACK, OTHER, J1939_PGN_DM3);
	expect_ack(J1939_ACK_CTRL_ACK, OTHER2, J1939_PGN_DM11);
	expect_none();
}

static void test_full_tx_queue_is_retried_and_counted(void) {
	start();
	tx_fill();
	process(PERIOD);
	TEST_ASSERT_EQUAL_UINT32(1U, j1939_stats_get(&s)->dm_tx_retry);
	process(PERIOD - 1U);
	TEST_ASSERT_EQUAL_UINT32(2U, j1939_stats_get(&s)->dm_tx_retry);
	TEST_ASSERT_EQUAL_UINT32(0U, j1939_stats_get(&s)->dm_tx_dropped);
	/* The next period finds the DM1 still unsent. */
	process(1U);
	TEST_ASSERT_EQUAL_UINT32(1U, j1939_stats_get(&s)->dm_tx_dropped);
	TEST_ASSERT_EQUAL_UINT32(3U, j1939_stats_get(&s)->dm_tx_retry);
	tx_drain();
	process(0U);
	expect_dm1(NULL, 0U);
	expect_none();
}

static void test_full_tx_queue_drops_late_answers(void) {
	start();
	rx_request(OTHER, OWN, J1939_PGN_DM2);
	rx_request(OTHER, OWN, J1939_PGN_DM11);
	tx_fill();
	process(0U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_dm_clear_confirm(&s, ca, J1939_PGN_DM11, true));
	process(TR - 1U);
	TEST_ASSERT_EQUAL_UINT32(0U, j1939_stats_get(&s)->dm_tx_dropped);
	process(1U); /* DM2 answer too late; the acknowledgement has time left */
	TEST_ASSERT_EQUAL_UINT32(1U, j1939_stats_get(&s)->dm_tx_dropped);
	process(TR - 2U);
	TEST_ASSERT_EQUAL_UINT32(1U, j1939_stats_get(&s)->dm_tx_dropped);
	process(1U);
	TEST_ASSERT_EQUAL_UINT32(2U, j1939_stats_get(&s)->dm_tx_dropped);
	tx_drain();
	process(0U);
	expect_none();
}

static void test_long_call_periods_saturate_timers(void) {
	start();
	rx_request(OTHER, OWN, J1939_PGN_DM2);
	rx_request(OTHER, OWN, J1939_PGN_DM3);
	tx_fill();
	/* Cannot Respond to a second requester finds no room either. */
	rx_request(OTHER2, OWN, J1939_PGN_DM3);
	process(0U);
	TEST_ASSERT_EQUAL_UINT32(1U, j1939_stats_get(&s)->dm_tx_dropped);
	process(TR - 1U);
	process(UINT32_MAX);
	/* The DM2 answer is given up; the undecided clear request turns into a pending NACK. */
	TEST_ASSERT_EQUAL_UINT32(2U, j1939_stats_get(&s)->dm_tx_dropped);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_EMPTY, j1939_dm_clear_get(&s, ca, &(uint32_t){0U}));
	/* The NACK is given up, and the unsent DM1 of the previous period counted. */
	process(UINT32_MAX);
	TEST_ASSERT_EQUAL_UINT32(4U, j1939_stats_get(&s)->dm_tx_dropped);
	tx_drain();
	process(0U);
	expect_dm1(NULL, 0U);
	expect_none();
}

static void test_busy_broadcast_is_retried(void) {
	const j1939_diag_dtc_t list[2] = {dtc_a, dtc_b};

	start();
	set_prev(list, 2U);
	set_active(list, 2U);
	process(0U); /* DM1 BAM starts */
	rx_request(OTHER, J1939_ADDR_GLOBAL, J1939_PGN_DM2);
	process(0U);
	TEST_ASSERT_EQUAL_UINT32(1U, j1939_stats_get(&s)->dm_tx_retry);
	expect_dm1(list, 2U); /* collects the DM1 packets; the DM2 waits */
	process(GAP);
	expect_dm(J1939_PGN_DM2, list, 2U);
	expect_none();
	TEST_ASSERT_EQUAL_UINT32(0U, j1939_stats_get(&s)->dm_tx_dropped);
}

static void test_lost_address_ends_diagnostics(void) {
	const uint8_t name[8] = {0x01U, 0U, 0U, 0U, 0U, 0U, 0U, 0U}; /* wins against NAME */

	start();
	rx_request(OTHER, OWN, J1939_PGN_DM2);
	rx_request(OTHER, OWN, J1939_PGN_DM3);
	process(0U);
	expect_dm(J1939_PGN_DM2, NULL, 0U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_dm_clear_confirm(&s, ca, J1939_PGN_DM3, false));
	rx_request(OTHER, OWN, J1939_PGN_DM2);
	rx_raw(make_id(6U, J1939_PGN_ADDRESS_CLAIMED, J1939_ADDR_GLOBAL, OWN), name, 8U);
	process(0U);
	/* The pending DM2 answer and NACK are dropped. */
	TEST_ASSERT_EQUAL_UINT32(2U, j1939_stats_get(&s)->dm_tx_dropped);
	process(10U * PERIOD);
	process(PERIOD);
	/* Only the Cannot Claim. */
	TEST_ASSERT_EQUAL_UINT16(1U, s.tx.ring.count);
	TEST_ASSERT_EQUAL_HEX32(
	        make_id(6U, J1939_PGN_ADDRESS_CLAIMED, J1939_ADDR_GLOBAL, J1939_ADDR_NULL),
	        j1939_port_frame_id_get(j1939_tx_peek(&s)));
}

static void test_corrupted_clear_state_fails_safe(void) {
	start();
	set_prev(&dtc_b, 1U);
	dm.clear[0].state = (j1939_dm_clear_state_t)77;
	dm.clear[1].state = (j1939_dm_clear_state_t)77;
	process(0U);
	expect_none();
	TEST_ASSERT_EQUAL(J1939_RET_ERR_EMPTY, j1939_dm_clear_get(&s, ca, &(uint32_t){0U}));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_STATE, j1939_dm_clear_confirm(&s, ca, J1939_PGN_DM3, true));
	rx_request(OTHER, OWN, J1939_PGN_DM2);
	process(0U);
	expect_dm(J1939_PGN_DM2, &dtc_b, 1U);
}

static void test_init_rejects_invalid_configuration(void) {
	j1939_dm_t dm2;
	j1939_dm_cfg_t c = dm_cfg;
	j1939_ca_id_t ca2 = 0U;

	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_dm_init(NULL, ca, &dm2, &dm_cfg));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_dm_init(&s, ca, NULL, &dm_cfg));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_dm_init(&s, ca, &dm2, NULL));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_dm_init(&s, 1U, &dm2, &dm_cfg));
	c.active_len = J1939_DIAG_DM_DTC_MAX + 1U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_dm_init(&s, ca, &dm2, &c));
	c = dm_cfg;
	c.prev_len = J1939_DIAG_DM_DTC_MAX + 1U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_dm_init(&s, ca, &dm2, &c));
	c = dm_cfg;
	c.active = NULL;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_dm_init(&s, ca, &dm2, &c));
	c = dm_cfg;
	c.prev = NULL;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_dm_init(&s, ca, &dm2, &c));
	c = dm_cfg;
	c.hold = NULL;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_dm_init(&s, ca, &dm2, &c));
	c = dm_cfg;
	c.buf = NULL;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_dm_init(&s, ca, &dm2, &c));
	c = dm_cfg;
	c.buf_len = J1939_DM_BUF_LEN(DTC_LEN) - 1U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_dm_init(&s, ca, &dm2, &c));
	c.prev_len = 1U;
	c.active_len = 1U;
	c.buf_len = 8U;
	/* A single frame DM needs no transport protocol. */
	cfg.tp_tx_buf = NULL;
	cfg.tp_tx_buf_len = 0U;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&s, &cfg));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_ca_add(&s, &(j1939_ca_cfg_t){.address = OWN}, &ca));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_dm_init(&s, ca, &dm2, &c));
	/* Multi-packet DMs without a transport protocol transmit buffer. */
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_dm_init(&s, ca, &dm2, &dm_cfg));
	/* One state per CA. */
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_ca_add(&s, &(j1939_ca_cfg_t){.address = 0x11U}, &ca2));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_dm_init(&s, ca2, &dm2, &c));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_dm_init(&s, ca, &dm2, &c)); /* again, same CA */
	/* No storage at all: DMs without DTCs. */
	c = (j1939_dm_cfg_t){.buf = dm_buf, .buf_len = 8U};
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_dm_init(&s, ca2, &dm, &c));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_FULL, j1939_dm_active_set(&s, ca2, &dtc_a, 1U));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_dm_active_set(&s, ca2, NULL, 0U));
}

static void test_setters_reject_invalid_input(void) {
	j1939_diag_dtc_t bad = dtc_a;
	j1939_diag_dtc_t dup[2] = {dtc_a, dtc_a};
	const j1939_diag_dtc_t five[5] = {dtc_a, dtc_b, dtc_c, dtc_d, {7000U, 4U, 0U, 0U}};
	j1939_diag_lamps_t l = lamps;
	uint32_t pgn = 0U;

	dup[1].oc = 5U; /* same SPN and FMI */
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_dm_active_set(NULL, ca, &dtc_a, 1U));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_dm_active_set(&s, 1U, &dtc_a, 1U));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_dm_active_set(&s, ca, NULL, 1U));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_FULL, j1939_dm_active_set(&s, ca, five, 5U));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_dm_active_set(&s, ca, dup, 2U));
	bad.cm = J1939_DIAG_CM_LEGACY;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_dm_active_set(&s, ca, &bad, 1U));
	bad = (j1939_diag_dtc_t){0U, 0U, 1U, J1939_DIAG_CM_V4};
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_dm_prev_set(&s, ca, &bad, 1U));
	bad.fmi = 32U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_dm_prev_set(&s, ca, &bad, 1U));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_FULL, j1939_dm_prev_set(&s, ca, five, 5U));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_dm_prev_set(NULL, ca, NULL, 0U));
	l.protect = 4U;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_dm_lamps_set(&s, ca, &l));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_dm_lamps_set(&s, ca, NULL));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_dm_lamps_set(NULL, ca, &lamps));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_dm_clear_get(&s, ca, NULL));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_dm_clear_get(NULL, ca, &pgn));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_dm_clear_confirm(&s, ca, J1939_PGN_DM1, true));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_dm_clear_confirm(NULL, ca, J1939_PGN_DM3, true));
	/* A second CA without diagnostics. */
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_ca_add(&s, &(j1939_ca_cfg_t){.address = 0x11U},
	                                             &(j1939_ca_id_t){0U}));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_STATE, j1939_dm_lamps_set(&s, 1U, &lamps));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_STATE, j1939_dm_prev_set(&s, 1U, NULL, 0U));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_STATE, j1939_dm_clear_get(&s, 1U, &pgn));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_STATE,
	                  j1939_dm_clear_confirm(&s, 1U, J1939_PGN_DM11, true));
	/* Nothing changed. */
	TEST_ASSERT_EQUAL_UINT16(0U, dm.active_count);
	TEST_ASSERT_EQUAL_UINT16(0U, dm.prev_count);
	TEST_ASSERT_FALSE(dm.dm1_due);
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_first_dm1_waits_for_the_claim);
	RUN_TEST(test_dm1_period_boundaries);
	RUN_TEST(test_dm1_with_one_dtc_and_lamps);
	RUN_TEST(test_dm1_with_many_dtcs_uses_bam);
	RUN_TEST(test_change_triggers_dm1_once_per_dtc_per_second);
	RUN_TEST(test_change_without_free_hold_record_waits_for_period);
	RUN_TEST(test_occurrence_count_and_lamps_trigger_nothing);
	RUN_TEST(test_request_for_dm1_is_answered_globally);
	RUN_TEST(test_request_for_dm2_is_answered_globally);
	RUN_TEST(test_multi_packet_answer_goes_to_the_requester);
	RUN_TEST(test_requests_for_other_nodes_or_cas_without_diagnostics);
	RUN_TEST(test_dm3_accepted_clears_previously_active);
	RUN_TEST(test_dm11_global_accepted_clears_active_without_ack);
	RUN_TEST(test_clear_refused_is_nacked_and_keeps_data);
	RUN_TEST(test_clear_without_decision_times_out);
	RUN_TEST(test_clear_not_enabled_is_handled_as_unsupported);
	RUN_TEST(test_concurrent_clear_requests);
	RUN_TEST(test_full_tx_queue_is_retried_and_counted);
	RUN_TEST(test_full_tx_queue_drops_late_answers);
	RUN_TEST(test_long_call_periods_saturate_timers);
	RUN_TEST(test_busy_broadcast_is_retried);
	RUN_TEST(test_lost_address_ends_diagnostics);
	RUN_TEST(test_corrupted_clear_state_fails_safe);
	RUN_TEST(test_init_rejects_invalid_configuration);
	RUN_TEST(test_setters_reject_invalid_input);
	return UNITY_END();
}
