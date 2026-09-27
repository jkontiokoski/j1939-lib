/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/* Transport protocol on one stack: frames of the peers are injected, sent frames inspected. */

#include <stdint.h>

#include "unity.h"

#include "j1939/j1939.h"

#define RX_LEN  32U
#define TX_LEN  8U
#define MSG_LEN 2U
#define BUF_LEN 2U

#define OWN    0x10U
#define PEER   0x42U
#define PEER2  0x43U
#define PEER3  0x44U
#define PGN_A  0xFECAU /* PDU2 */
#define PGN_B  0xEF00U /* PDU1 */
#define PGN_X  0xFF55U /* not received */
#define LEN_20 20U     /* 3 packets, the last one with 6 bytes */

#define RTS   0x10U
#define CTS   0x11U
#define EOMA  0x13U
#define BAM   0x20U
#define ABORT 0xFFU
#define NA    0xFFU

#define GAP J1939_CFG_TP_BAM_GAP_US

static const uint32_t rx_pgns[] = {PGN_A, PGN_B};
static j1939_port_frame_t rx_buf[RX_LEN];
static j1939_port_frame_t tx_buf[TX_LEN];
static j1939_msg_slot_t msg_buf[MSG_LEN];
static j1939_tp_buf_t tp_tx[BUF_LEN];
static j1939_tp_buf_t tp_rx[BUF_LEN];
static j1939_cfg_t cfg;
static j1939_t s;
static j1939_ca_id_t ca;
static uint8_t data[J1939_TP_MSG_MAX];
static uint8_t sent[8];

static uint8_t pat(uint32_t i) {
	return (uint8_t)((i * 13U) + 1U);
}

static uint32_t make_id(uint8_t prio, uint32_t pgn, uint8_t da, uint8_t sa) {
	uint32_t id = 0U;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_id_build(prio, pgn, da, sa, &id));
	return id;
}

static void rx_raw(uint32_t id, const uint8_t *d, uint8_t len) {
	j1939_port_frame_t *slot = j1939_queue_acquire(j1939_rx_queue(&s));

	TEST_ASSERT_NOT_NULL(slot);
	j1939_port_frame_build(slot, id, d, len);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_queue_commit(j1939_rx_queue(&s)));
}

static void rx_cm_to(uint8_t sa, uint8_t da, uint8_t ctrl, uint8_t b1, uint8_t b2, uint8_t b3,
                     uint8_t b4, uint32_t pgn) {
	const uint8_t d[8] = {
	        ctrl, b1, b2, b3, b4, (uint8_t)pgn, (uint8_t)(pgn >> 8), (uint8_t)(pgn >> 16)};

	rx_raw(make_id(5U, J1939_PGN_TP_CM, da, sa), d, 8U);
}

static void rx_rts(uint8_t sa, uint16_t len, uint8_t packets, uint8_t limit, uint32_t pgn) {
	rx_cm_to(sa, OWN, RTS, (uint8_t)len, (uint8_t)(len >> 8), packets, limit, pgn);
}

static void rx_bam(uint8_t sa, uint16_t len, uint8_t packets, uint32_t pgn) {
	rx_cm_to(sa, J1939_ADDR_GLOBAL, BAM, (uint8_t)len, (uint8_t)(len >> 8), packets, NA, pgn);
}

static void rx_cts(uint8_t sa, uint8_t n, uint8_t next, uint32_t pgn) {
	rx_cm_to(sa, OWN, CTS, n, next, NA, NA, pgn);
}

static void rx_abort(uint8_t sa, uint8_t da, uint8_t reason, uint32_t pgn) {
	rx_cm_to(sa, da, ABORT, reason, NA, NA, NA, pgn);
}

/* Data packet seq of a message with the test pattern. */
static void rx_dt(uint8_t sa, uint8_t da, uint8_t seq) {
	uint8_t d[8];
	uint32_t i;

	d[0] = seq;
	for (i = 0U; i < 7U; i++) {
		d[1U + i] = pat(((uint32_t)(seq - 1U) * 7U) + i);
	}
	rx_raw(make_id(7U, J1939_PGN_TP_DT, da, sa), d, 8U);
}

static void run(uint32_t us) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(&s, us));
}

/* Pops the next sent frame, checks its identifier and keeps its data in sent[]. */
static void expect_tx(uint8_t prio, uint32_t pgn, uint8_t da) {
	const j1939_port_frame_t *f = j1939_queue_peek(j1939_tx_queue(&s));
	uint32_t i;

	TEST_ASSERT_NOT_NULL_MESSAGE(f, "no frame sent");
	TEST_ASSERT_EQUAL_HEX32(make_id(prio, pgn, da, OWN), j1939_port_frame_id_get(f));
	TEST_ASSERT_EQUAL_UINT8(8U, j1939_port_frame_len_get(f));
	for (i = 0U; i < 8U; i++) {
		sent[i] = j1939_port_frame_data(f)[i];
	}
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_queue_pop(j1939_tx_queue(&s)));
}

static void expect_cm(uint8_t da, uint8_t ctrl, uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4,
                      uint32_t pgn) {
	const uint8_t exp[8] = {
	        ctrl, b1, b2, b3, b4, (uint8_t)pgn, (uint8_t)(pgn >> 8), (uint8_t)(pgn >> 16)};

	expect_tx(7U, J1939_PGN_TP_CM, da);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(exp, sent, 8U);
}

static void expect_abort(uint8_t da, uint8_t reason, uint32_t pgn) {
	expect_cm(da, ABORT, reason, NA, NA, NA, pgn);
}

static void expect_none(void) {
	TEST_ASSERT_EQUAL_UINT16(0U, j1939_queue_count(j1939_tx_queue(&s)));
}

static uint8_t sessions_open(void) {
	uint8_t n = 0U;
	uint32_t i;

	for (i = 0U; i < J1939_CFG_TP_SESSIONS; i++) {
		if (s.tp.sessions[i].state != J1939_TP_IDLE) {
			n++;
		}
	}
	return n;
}

static void expect_msg(uint32_t pgn, uint8_t sa, uint8_t da, uint16_t len) {
	const j1939_msg_t *msg = j1939_msg_peek(&s);
	uint32_t i;

	TEST_ASSERT_NOT_NULL_MESSAGE(msg, "no message");
	TEST_ASSERT_EQUAL_HEX32(pgn, msg->pgn);
	TEST_ASSERT_EQUAL_HEX8(sa, msg->sa);
	TEST_ASSERT_EQUAL_HEX8(da, msg->da);
	TEST_ASSERT_EQUAL_UINT16(len, msg->len);
	for (i = 0U; i < len; i++) {
		TEST_ASSERT_EQUAL_HEX8(pat(i), msg->data[i]);
	}
}

/* Fills the message slots with single frame messages. */
static void fill_msg_slots(void) {
	const uint8_t d[8] = {0};
	uint32_t i;

	for (i = 0U; i < MSG_LEN; i++) {
		rx_raw(make_id(6U, PGN_A, J1939_ADDR_GLOBAL, PEER3), d, 8U);
	}
	run(0U);
	TEST_ASSERT_EQUAL_UINT32(0U, s.stats.rx_msg_overflow);
}

static j1939_ret_t send(uint8_t da, uint32_t pgn, uint16_t len) {
	const j1939_msg_t msg = {
	        .pgn = pgn, .prio = 6U, .sa = 0U, .da = da, .len = len, .data = data};

	return j1939_send(&s, ca, &msg);
}

static void init(uint16_t tx_bufs, uint16_t rx_bufs) {
	cfg.tp_tx_buf_len = tx_bufs;
	cfg.tp_rx_buf_len = rx_bufs;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&s, &cfg));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_ca_add(&s, &(j1939_ca_cfg_t){.address = OWN}, &ca));
	/* Claim the address; the CA may transmit right after its Address Claimed. */
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(&s, 0U));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_queue_pop(j1939_tx_queue(&s)));
}

void setUp(void) {
	uint32_t i;

	for (i = 0U; i < J1939_TP_MSG_MAX; i++) {
		data[i] = pat(i);
	}
	cfg = (j1939_cfg_t){
	        .rx_buf = rx_buf,
	        .rx_len = RX_LEN,
	        .tx_buf = tx_buf,
	        .tx_len = TX_LEN,
	        .msg_buf = msg_buf,
	        .msg_len = MSG_LEN,
	        .rx_pgns = rx_pgns,
	        .rx_pgns_len = 2U,
	        .tp_tx_buf = tp_tx,
	        .tp_rx_buf = tp_rx,
	};
	init(BUF_LEN, BUF_LEN);
}

void tearDown(void) {
	TEST_ASSERT_EQUAL_UINT32(0U, s.rx.ring.lock.depth);
	TEST_ASSERT_EQUAL_UINT32(0U, s.tx.ring.lock.depth);
	TEST_ASSERT_EQUAL_UINT32(0U, s.msgs.ring.lock.depth);
	TEST_ASSERT_LESS_OR_EQUAL_UINT32(1U, s.msgs.ring.lock.max_depth);
}

/* ---- Configuration ---- */

static void test_init_validates_tp_buffers(void) {
	j1939_cfg_t c = cfg;
	j1939_t other;

	c.tp_tx_buf = NULL;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_init(&other, &c));
	c.tp_tx_buf_len = 0U;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&other, &c));
	c = cfg;
	c.tp_rx_buf = NULL;
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_init(&other, &c));
	c.tp_rx_buf_len = 0U;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&other, &c));
	TEST_ASSERT_EQUAL_UINT32(0U, j1939_stats_get(&other)->tp_rx_refused);
}

/* ---- BAM receive ---- */

static void test_bam_is_reassembled_and_delivered(void) {
	const j1939_msg_t *msg;

	rx_bam(PEER, LEN_20, 3U, PGN_A);
	rx_dt(PEER, J1939_ADDR_GLOBAL, 1U);
	rx_dt(PEER, J1939_ADDR_GLOBAL, 1U); /* duplicate, ignored */
	rx_dt(PEER, J1939_ADDR_GLOBAL, 2U);
	run(0U);
	TEST_ASSERT_NULL(j1939_msg_peek(&s));
	TEST_ASSERT_EQUAL_UINT8(1U, sessions_open());
	rx_dt(PEER, J1939_ADDR_GLOBAL, 3U);
	run(0U);

	expect_msg(PGN_A, PEER, J1939_ADDR_GLOBAL, LEN_20);
	msg = j1939_msg_peek(&s);
	TEST_ASSERT_EQUAL_UINT8(5U, msg->prio); /* priority of the BAM frame */
	TEST_ASSERT_EQUAL_PTR(tp_rx[0].data, msg->data);
	TEST_ASSERT_EQUAL_UINT8(0U, sessions_open());
	expect_none(); /* receivers of a broadcast stay silent */
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_msg_pop(&s));
}

static void test_bam_announcements_that_are_ignored(void) {
	uint8_t short_frame[8] = {BAM, LEN_20, 0U, 3U, NA, 0xCAU, 0xFEU, 0U};

	rx_bam(PEER, LEN_20, 3U, PGN_X);                                 /* not in rx_pgns */
	rx_bam(PEER, 8U, 2U, PGN_A);                                     /* too short for TP */
	rx_bam(PEER, LEN_20, 4U, PGN_A);                                 /* packet count mismatch */
	rx_bam(PEER, J1939_TP_MSG_MAX + 1U, 255U, PGN_A);                /* too long */
	rx_cm_to(PEER, OWN, BAM, LEN_20, 0U, 3U, NA, PGN_A);             /* BAM to one node */
	rx_cm_to(PEER, J1939_ADDR_GLOBAL, 0x55U, 0U, 0U, 0U, 0U, PGN_A); /* reserved control */
	rx_raw(make_id(7U, J1939_PGN_TP_CM, J1939_ADDR_GLOBAL, PEER), short_frame, 7U);
	rx_raw(make_id(7U, J1939_PGN_TP_CM, J1939_ADDR_GLOBAL, J1939_ADDR_NULL), short_frame, 8U);
	rx_raw(make_id(7U, J1939_PGN_TP_CM, J1939_ADDR_GLOBAL, J1939_ADDR_GLOBAL), short_frame, 8U);
	rx_raw(make_id(7U, J1939_PGN_TP_CM, 0x20U, PEER), short_frame, 8U); /* another node */
	rx_dt(PEER, J1939_ADDR_GLOBAL, 1U);                                 /* no session */
	run(0U);
	TEST_ASSERT_EQUAL_UINT8(0U, sessions_open());
	TEST_ASSERT_NULL(j1939_msg_peek(&s));
	expect_none();
	TEST_ASSERT_EQUAL_UINT32(0U, s.stats.tp_rx_refused);
	TEST_ASSERT_EQUAL_UINT32(0U, s.stats.tp_rx_aborted);
}

static void test_bam_lost_packet_drops_the_message(void) {
	rx_bam(PEER, LEN_20, 3U, PGN_A);
	rx_dt(PEER, J1939_ADDR_GLOBAL, 1U);
	rx_dt(PEER, J1939_ADDR_GLOBAL, 3U);
	rx_dt(PEER, J1939_ADDR_GLOBAL, 2U); /* too late, session is gone */
	run(0U);
	TEST_ASSERT_EQUAL_UINT8(0U, sessions_open());
	TEST_ASSERT_NULL(j1939_msg_peek(&s));
	TEST_ASSERT_EQUAL_UINT32(1U, s.stats.tp_rx_aborted);
	TEST_ASSERT_EQUAL_UINT8(0U, tp_rx[0].state); /* buffer free again */
}

static void test_bam_times_out_after_t1(void) {
	rx_bam(PEER, LEN_20, 3U, PGN_A);
	run(0U);
	run(J1939_TP_T1_US - 1U);
	rx_dt(PEER, J1939_ADDR_GLOBAL, 1U);
	run(J1939_TP_T1_US); /* the packet restarts T1, which counts from the next call */
	run(J1939_TP_T1_US - 1U);
	TEST_ASSERT_EQUAL_UINT8(1U, sessions_open());
	run(1U);
	TEST_ASSERT_EQUAL_UINT8(0U, sessions_open());
	TEST_ASSERT_EQUAL_UINT32(1U, s.stats.tp_rx_aborted);
	expect_none();
}

static void test_new_bam_replaces_unfinished_one(void) {
	rx_bam(PEER, LEN_20, 3U, PGN_A);
	rx_dt(PEER, J1939_ADDR_GLOBAL, 1U);
	rx_bam(PEER, 9U, 2U, PGN_B);
	rx_dt(PEER, J1939_ADDR_GLOBAL, 1U);
	rx_dt(PEER, J1939_ADDR_GLOBAL, 2U);
	run(0U);
	TEST_ASSERT_EQUAL_UINT32(1U, s.stats.tp_rx_aborted);
	expect_msg(PGN_B, PEER, J1939_ADDR_GLOBAL, 9U);
}

static void test_bam_from_two_senders_interleaved(void) {
	rx_bam(PEER, LEN_20, 3U, PGN_A);
	rx_bam(PEER2, 9U, 2U, PGN_B);
	rx_dt(PEER, J1939_ADDR_GLOBAL, 1U);
	rx_dt(PEER2, J1939_ADDR_GLOBAL, 1U);
	rx_dt(PEER, J1939_ADDR_GLOBAL, 2U);
	rx_dt(PEER2, J1939_ADDR_GLOBAL, 2U);
	rx_dt(PEER, J1939_ADDR_GLOBAL, 3U);
	run(0U);
	expect_msg(PGN_B, PEER2, J1939_ADDR_GLOBAL, 9U);
	(void)j1939_msg_pop(&s);
	expect_msg(PGN_A, PEER, J1939_ADDR_GLOBAL, LEN_20);
}

static void test_bam_without_session_or_buffer_is_counted(void) {
	/* Sessions: both in use, a third sender is refused. */
	rx_bam(PEER, LEN_20, 3U, PGN_A);
	rx_bam(PEER2, LEN_20, 3U, PGN_A);
	rx_bam(PEER3, LEN_20, 3U, PGN_A);
	run(0U);
	TEST_ASSERT_EQUAL_UINT32(1U, s.stats.tp_rx_refused);

	/* Buffers: both completed messages hold theirs until popped. */
	rx_dt(PEER, J1939_ADDR_GLOBAL, 1U);
	rx_dt(PEER, J1939_ADDR_GLOBAL, 2U);
	rx_dt(PEER, J1939_ADDR_GLOBAL, 3U);
	rx_dt(PEER2, J1939_ADDR_GLOBAL, 1U);
	rx_dt(PEER2, J1939_ADDR_GLOBAL, 2U);
	rx_dt(PEER2, J1939_ADDR_GLOBAL, 3U);
	rx_bam(PEER3, LEN_20, 3U, PGN_A);
	run(0U);
	TEST_ASSERT_EQUAL_UINT8(0U, sessions_open());
	TEST_ASSERT_EQUAL_UINT32(2U, s.stats.tp_rx_refused);

	/* Popping a message frees its buffer for the next broadcast. */
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_msg_pop(&s));
	rx_bam(PEER3, LEN_20, 3U, PGN_A);
	run(0U);
	TEST_ASSERT_EQUAL_UINT8(1U, sessions_open());
	TEST_ASSERT_EQUAL_PTR(&tp_rx[0], s.tp.sessions[0].buf);
	TEST_ASSERT_EQUAL_UINT32(2U, s.stats.tp_rx_refused);
}

static void test_bam_without_message_slot_is_dropped(void) {
	fill_msg_slots();
	rx_bam(PEER, LEN_20, 3U, PGN_A);
	rx_dt(PEER, J1939_ADDR_GLOBAL, 1U);
	rx_dt(PEER, J1939_ADDR_GLOBAL, 2U);
	rx_dt(PEER, J1939_ADDR_GLOBAL, 3U);
	run(0U);
	TEST_ASSERT_EQUAL_UINT32(1U, s.stats.rx_msg_overflow);
	TEST_ASSERT_EQUAL_UINT32(1U, s.stats.tp_rx_aborted);
	TEST_ASSERT_EQUAL_UINT8(0U, tp_rx[0].state);
}

static void test_abort_to_global_ends_broadcast_of_its_sender(void) {
	rx_bam(PEER, LEN_20, 3U, PGN_A);
	rx_abort(PEER, J1939_ADDR_GLOBAL, J1939_TP_ABORT_OTHER, PGN_B); /* other PGN */
	run(0U);
	TEST_ASSERT_EQUAL_UINT8(1U, sessions_open());
	rx_abort(PEER, J1939_ADDR_GLOBAL, J1939_TP_ABORT_OTHER, PGN_A);
	run(0U);
	TEST_ASSERT_EQUAL_UINT8(0U, sessions_open());
	TEST_ASSERT_EQUAL_UINT32(1U, s.stats.tp_rx_aborted);
}

static void test_buffer_is_reclaimed_when_its_slot_is_reused(void) {
	const uint8_t d[8] = {0};

	init(BUF_LEN, 1U);
	rx_bam(PEER, LEN_20, 3U, PGN_A);
	rx_dt(PEER, J1939_ADDR_GLOBAL, 1U);
	rx_dt(PEER, J1939_ADDR_GLOBAL, 2U);
	rx_dt(PEER, J1939_ADDR_GLOBAL, 3U);
	run(0U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_msg_pop(&s));
	/* Two single frame messages: the second one reuses the slot of the popped message. */
	rx_raw(make_id(6U, PGN_A, J1939_ADDR_GLOBAL, PEER3), d, 8U);
	rx_raw(make_id(6U, PGN_A, J1939_ADDR_GLOBAL, PEER3), d, 8U);
	rx_bam(PEER, LEN_20, 3U, PGN_A);
	run(0U);
	TEST_ASSERT_EQUAL_UINT16(0U, tp_rx[0].slot);
	TEST_ASSERT_EQUAL_UINT8(1U, sessions_open());
	TEST_ASSERT_EQUAL_UINT32(0U, s.stats.tp_rx_refused);
}

/* ---- RTS/CTS receive ---- */

static void test_rts_cts_is_received_and_acknowledged(void) {
	rx_rts(PEER, LEN_20, 3U, NA, PGN_B);
	run(0U);
	expect_cm(PEER, CTS, 3U, 1U, NA, NA, PGN_B);
	rx_dt(PEER, OWN, 1U);
	rx_dt(PEER, OWN, 2U);
	rx_dt(PEER, OWN, 2U); /* duplicate, ignored */
	rx_dt(PEER, OWN, 3U);
	run(0U);
	expect_cm(PEER, EOMA, LEN_20, 0U, 3U, NA, PGN_B);
	expect_none();
	expect_msg(PGN_B, PEER, OWN, LEN_20);
	TEST_ASSERT_EQUAL_UINT8(5U, j1939_msg_peek(&s)->prio);
	TEST_ASSERT_EQUAL_UINT8(0U, sessions_open());
}

static void test_rts_limit_splits_the_transfer_into_windows(void) {
	rx_rts(PEER, LEN_20, 3U, 2U, PGN_A);
	run(0U);
	expect_cm(PEER, CTS, 2U, 1U, NA, NA, PGN_A);
	rx_dt(PEER, OWN, 1U);
	run(0U);
	expect_none();
	rx_dt(PEER, OWN, 2U);
	run(0U);
	expect_cm(PEER, CTS, 1U, 3U, NA, NA, PGN_A);
	rx_dt(PEER, OWN, 3U);
	run(0U);
	expect_cm(PEER, EOMA, LEN_20, 0U, 3U, NA, PGN_A);
	expect_msg(PGN_A, PEER, OWN, LEN_20);
	(void)j1939_msg_pop(&s);

	/* A limit of 0 is treated as one packet per CTS. */
	rx_rts(PEER, 9U, 2U, 0U, PGN_A);
	run(0U);
	expect_cm(PEER, CTS, 1U, 1U, NA, NA, PGN_A);
}

static void test_rts_that_cannot_be_received_is_aborted(void) {
	rx_rts(PEER, LEN_20, 3U, NA, PGN_X);
	rx_rts(PEER, J1939_TP_MSG_MAX + 1U, 255U, NA, PGN_A);
	rx_rts(PEER, 8U, 2U, NA, PGN_A);
	rx_rts(PEER, LEN_20, 2U, NA, PGN_A);
	rx_cm_to(PEER, J1939_ADDR_GLOBAL, RTS, LEN_20, 0U, 3U, NA, PGN_A); /* RTS to all: ignored */
	run(0U);
	expect_abort(PEER, J1939_TP_ABORT_OTHER, PGN_X);
	expect_abort(PEER, J1939_TP_ABORT_TOO_LARGE, PGN_A);
	expect_abort(PEER, J1939_TP_ABORT_OTHER, PGN_A);
	expect_abort(PEER, J1939_TP_ABORT_OTHER, PGN_A);
	expect_none();
	TEST_ASSERT_EQUAL_UINT8(0U, sessions_open());
	TEST_ASSERT_EQUAL_UINT32(0U, s.stats.tp_rx_refused);
}

static void test_rts_without_session_or_buffer_is_aborted(void) {
	rx_bam(PEER2, LEN_20, 3U, PGN_A);
	rx_bam(PEER3, LEN_20, 3U, PGN_A);
	rx_rts(PEER, LEN_20, 3U, NA, PGN_A);
	run(0U);
	expect_abort(PEER, J1939_TP_ABORT_BUSY, PGN_A);
	TEST_ASSERT_EQUAL_UINT32(1U, s.stats.tp_rx_refused);

	init(BUF_LEN, 0U);
	rx_rts(PEER, LEN_20, 3U, NA, PGN_A);
	run(0U);
	expect_abort(PEER, J1939_TP_ABORT_RESOURCES, PGN_A);
	TEST_ASSERT_EQUAL_UINT32(1U, s.stats.tp_rx_refused);
}

static void test_rts_for_other_pgn_from_open_peer_is_refused(void) {
	rx_rts(PEER, LEN_20, 3U, NA, PGN_B);
	run(0U);
	expect_cm(PEER, CTS, 3U, 1U, NA, NA, PGN_B);
	rx_rts(PEER, LEN_20, 3U, NA, PGN_A);
	run(0U);
	expect_abort(PEER, J1939_TP_ABORT_BUSY, PGN_A);
	rx_dt(PEER, OWN, 1U);
	rx_dt(PEER, OWN, 2U);
	rx_dt(PEER, OWN, 3U);
	run(0U);
	expect_cm(PEER, EOMA, LEN_20, 0U, 3U, NA, PGN_B);
	expect_msg(PGN_B, PEER, OWN, LEN_20);
}

static void test_repeated_rts_restarts_the_connection(void) {
	rx_rts(PEER, LEN_20, 3U, NA, PGN_B);
	rx_dt(PEER, OWN, 1U);
	rx_rts(PEER, 9U, 2U, NA, PGN_B);
	run(0U);
	expect_cm(PEER, CTS, 3U, 1U, NA, NA, PGN_B);
	expect_cm(PEER, CTS, 2U, 1U, NA, NA, PGN_B); /* no abort for the replaced one */
	expect_none();
	TEST_ASSERT_EQUAL_UINT32(1U, s.stats.tp_rx_aborted);
	rx_dt(PEER, OWN, 1U);
	rx_dt(PEER, OWN, 2U);
	run(0U);
	expect_cm(PEER, EOMA, 9U, 0U, 2U, NA, PGN_B);
	expect_msg(PGN_B, PEER, OWN, 9U);
}

static void test_out_of_sequence_packet_aborts(void) {
	rx_rts(PEER, LEN_20, 3U, NA, PGN_B);
	rx_dt(PEER, OWN, 1U);
	rx_dt(PEER, OWN, 3U);
	run(0U);
	expect_cm(PEER, CTS, 3U, 1U, NA, NA, PGN_B);
	expect_abort(PEER, J1939_TP_ABORT_BAD_SEQ, PGN_B);
	TEST_ASSERT_EQUAL_UINT32(1U, s.stats.tp_rx_aborted);
	TEST_ASSERT_EQUAL_UINT8(0U, sessions_open());
}

static void test_responder_times_out_with_t2_and_t1(void) {
	rx_rts(PEER, LEN_20, 3U, NA, PGN_B);
	run(0U);
	expect_cm(PEER, CTS, 3U, 1U, NA, NA, PGN_B);
	run(J1939_TP_T2_US - 1U);
	expect_none();
	rx_dt(PEER, OWN, 1U);
	run(J1939_TP_T2_US);
	run(J1939_TP_T1_US - 1U);
	expect_none();
	run(1U);
	expect_abort(PEER, J1939_TP_ABORT_TIMEOUT, PGN_B);
	TEST_ASSERT_EQUAL_UINT32(1U, s.stats.tp_rx_aborted);

	rx_rts(PEER, LEN_20, 3U, NA, PGN_B);
	run(0U);
	expect_cm(PEER, CTS, 3U, 1U, NA, NA, PGN_B);
	run(J1939_TP_T2_US);
	expect_abort(PEER, J1939_TP_ABORT_TIMEOUT, PGN_B);
}

static void test_peer_abort_ends_the_reception(void) {
	rx_rts(PEER, LEN_20, 3U, NA, PGN_B);
	rx_abort(PEER, OWN, J1939_TP_ABORT_RESOURCES, PGN_A); /* other PGN: ignored */
	run(0U);
	TEST_ASSERT_EQUAL_UINT8(1U, sessions_open());
	rx_abort(PEER, OWN, J1939_TP_ABORT_RESOURCES, PGN_B);
	run(0U);
	expect_cm(PEER, CTS, 3U, 1U, NA, NA, PGN_B);
	expect_none();
	TEST_ASSERT_EQUAL_UINT8(0U, sessions_open());
	TEST_ASSERT_EQUAL_UINT32(1U, s.stats.tp_rx_aborted);
	TEST_ASSERT_EQUAL_UINT32(0U, s.stats.tp_tx_aborted);
}

static void test_responder_holds_while_message_slots_are_full(void) {
	fill_msg_slots();
	rx_rts(PEER, LEN_20, 3U, NA, PGN_B);
	run(0U);
	expect_cm(PEER, CTS, 0U, NA, NA, NA, PGN_B);
	run(J1939_TP_TH_US - 1U);
	expect_none();
	run(1U);
	expect_cm(PEER, CTS, 0U, NA, NA, NA, PGN_B);
	rx_dt(PEER, OWN, 0U); /* invalid sequence number: ignored */
	run(0U);
	expect_none();

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_msg_pop(&s));
	run(0U);
	expect_cm(PEER, CTS, 3U, 1U, NA, NA, PGN_B);
	rx_dt(PEER, OWN, 1U);
	rx_dt(PEER, OWN, 2U);
	rx_dt(PEER, OWN, 3U);
	run(0U);
	expect_cm(PEER, EOMA, LEN_20, 0U, 3U, NA, PGN_B);
}

static void test_responder_gives_up_holding(void) {
	uint32_t i;

	fill_msg_slots();
	rx_rts(PEER, LEN_20, 3U, NA, PGN_B);
	run(0U);
	for (i = 1U; i < J1939_TP_HOLD_MAX; i++) {
		expect_cm(PEER, CTS, 0U, NA, NA, NA, PGN_B);
		run(J1939_TP_TH_US);
	}
	expect_cm(PEER, CTS, 0U, NA, NA, NA, PGN_B);
	run(J1939_TP_TH_US);
	expect_abort(PEER, J1939_TP_ABORT_RESOURCES, PGN_B);
	TEST_ASSERT_EQUAL_UINT8(0U, sessions_open());
	TEST_ASSERT_EQUAL_UINT32(1U, s.stats.tp_rx_aborted);
}

static void test_data_while_holding_aborts(void) {
	fill_msg_slots();
	rx_rts(PEER, LEN_20, 3U, NA, PGN_B);
	rx_dt(PEER, OWN, 1U);
	run(0U);
	expect_cm(PEER, CTS, 0U, NA, NA, NA, PGN_B);
	expect_abort(PEER, J1939_TP_ABORT_UNEXPECTED_DT, PGN_B);
}

static void test_message_slot_taken_before_completion_aborts(void) {
	const uint8_t d[8] = {0};

	rx_rts(PEER, LEN_20, 3U, NA, PGN_B);
	rx_raw(make_id(6U, PGN_A, J1939_ADDR_GLOBAL, PEER3), d, 8U);
	rx_raw(make_id(6U, PGN_A, J1939_ADDR_GLOBAL, PEER3), d, 8U);
	rx_dt(PEER, OWN, 1U);
	rx_dt(PEER, OWN, 2U);
	rx_dt(PEER, OWN, 3U);
	run(0U);
	expect_cm(PEER, CTS, 3U, 1U, NA, NA, PGN_B);
	expect_abort(PEER, J1939_TP_ABORT_RESOURCES, PGN_B);
	TEST_ASSERT_EQUAL_UINT32(1U, s.stats.rx_msg_overflow);
	TEST_ASSERT_EQUAL_UINT32(1U, s.stats.tp_rx_aborted);
}

static void test_responder_frames_dropped_on_full_tx_queue_are_counted(void) {
	uint32_t i;

	for (i = 0U; i < TX_LEN; i++) {
		TEST_ASSERT_EQUAL(J1939_RET_OK, send(J1939_ADDR_GLOBAL, PGN_A, 8U));
	}
	rx_rts(PEER, LEN_20, 3U, NA, PGN_B);
	run(0U);
	TEST_ASSERT_EQUAL_UINT32(1U, s.stats.tx_overflow);
	TEST_ASSERT_EQUAL_UINT8(1U, sessions_open());
}

/* ---- BAM transmit ---- */

static void test_bam_send_paces_packets(void) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, send(J1939_ADDR_GLOBAL, PGN_A, LEN_20));
	data[0] = 0U; /* the caller may reuse its data */
	expect_tx(6U, J1939_PGN_TP_CM, J1939_ADDR_GLOBAL);
	TEST_ASSERT_EQUAL_HEX8(BAM, sent[0]);
	TEST_ASSERT_EQUAL_UINT8(LEN_20, sent[1]);
	TEST_ASSERT_EQUAL_UINT8(0U, sent[2]);
	TEST_ASSERT_EQUAL_UINT8(3U, sent[3]);
	TEST_ASSERT_EQUAL_HEX8(NA, sent[4]);
	TEST_ASSERT_EQUAL_HEX8(0xCAU, sent[5]);
	TEST_ASSERT_EQUAL_HEX8(0xFEU, sent[6]);
	TEST_ASSERT_EQUAL_HEX8(0x00U, sent[7]);

	run(GAP); /* the gap counts from the first call after the send */
	run(GAP - 1U);
	expect_none();
	run(1U);
	expect_tx(6U, J1939_PGN_TP_DT, J1939_ADDR_GLOBAL);
	TEST_ASSERT_EQUAL_UINT8(1U, sent[0]);
	TEST_ASSERT_EQUAL_HEX8(pat(0U), sent[1]);
	TEST_ASSERT_EQUAL_HEX8(pat(6U), sent[7]);
	run(10U * GAP); /* at most one packet per call */
	expect_tx(6U, J1939_PGN_TP_DT, J1939_ADDR_GLOBAL);
	TEST_ASSERT_EQUAL_UINT8(2U, sent[0]);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_BUSY, send(J1939_ADDR_GLOBAL, PGN_A, LEN_20));
	run(GAP);
	expect_tx(6U, J1939_PGN_TP_DT, J1939_ADDR_GLOBAL);
	TEST_ASSERT_EQUAL_UINT8(3U, sent[0]);
	TEST_ASSERT_EQUAL_HEX8(pat(19U), sent[6]);
	TEST_ASSERT_EQUAL_HEX8(0xFFU, sent[7]);
	TEST_ASSERT_EQUAL_UINT8(0U, sessions_open());
	run(GAP);
	expect_none();
	TEST_ASSERT_EQUAL(J1939_RET_OK, send(J1939_ADDR_GLOBAL, PGN_A, LEN_20));
	TEST_ASSERT_EQUAL_UINT32(0U, s.stats.tp_tx_aborted);
}

static void test_bam_send_waits_for_tx_queue_then_gives_up(void) {
	uint32_t i;

	TEST_ASSERT_EQUAL(J1939_RET_OK, send(J1939_ADDR_GLOBAL, PGN_A, LEN_20));
	for (i = 1U; i < TX_LEN; i++) {
		TEST_ASSERT_EQUAL(J1939_RET_OK, send(J1939_ADDR_GLOBAL, PGN_B, 0U));
	}
	run(0U);
	run(GAP);
	TEST_ASSERT_EQUAL_UINT16(TX_LEN, j1939_queue_count(j1939_tx_queue(&s)));
	(void)j1939_queue_pop(j1939_tx_queue(&s)); /* room for one packet */
	run(J1939_TP_TR_US);
	TEST_ASSERT_EQUAL_UINT16(TX_LEN, j1939_queue_count(j1939_tx_queue(&s)));
	run(GAP + J1939_TP_TR_US - 1U);
	TEST_ASSERT_EQUAL_UINT8(1U, sessions_open());
	run(1U);
	TEST_ASSERT_EQUAL_UINT8(0U, sessions_open());
	TEST_ASSERT_EQUAL_UINT32(1U, s.stats.tp_tx_aborted);
}

/* ---- RTS/CTS transmit ---- */

static void test_rts_cts_send(void) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, send(PEER, PGN_B, LEN_20));
	expect_tx(6U, J1939_PGN_TP_CM, PEER);
	TEST_ASSERT_EQUAL_HEX8(RTS, sent[0]);
	TEST_ASSERT_EQUAL_UINT8(LEN_20, sent[1]);
	TEST_ASSERT_EQUAL_UINT8(3U, sent[3]);
	TEST_ASSERT_EQUAL_HEX8(NA, sent[4]); /* no packet limit */
	TEST_ASSERT_EQUAL_HEX8(0xEFU, sent[6]);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_BUSY, send(PEER, PGN_B, LEN_20));

	rx_cts(PEER, 2U, 1U, PGN_A); /* other PGN: ignored */
	rx_cm_to(PEER, OWN, EOMA, LEN_20, 0U, 3U, NA, PGN_A);
	run(0U);
	expect_none();
	rx_cts(PEER, 2U, 1U, PGN_B);
	run(0U);
	expect_tx(6U, J1939_PGN_TP_DT, PEER);
	TEST_ASSERT_EQUAL_UINT8(1U, sent[0]);
	expect_tx(6U, J1939_PGN_TP_DT, PEER);
	TEST_ASSERT_EQUAL_UINT8(2U, sent[0]);
	expect_none();
	rx_cts(PEER, 1U, 3U, PGN_B);
	run(0U);
	expect_tx(6U, J1939_PGN_TP_DT, PEER);
	TEST_ASSERT_EQUAL_UINT8(3U, sent[0]);
	TEST_ASSERT_EQUAL_HEX8(0xFFU, sent[7]);
	rx_cts(PEER, 1U, 2U, PGN_B); /* retransmission request */
	run(0U);
	expect_tx(6U, J1939_PGN_TP_DT, PEER);
	TEST_ASSERT_EQUAL_UINT8(2U, sent[0]);
	TEST_ASSERT_EQUAL_HEX8(pat(7U), sent[1]);
	rx_cm_to(PEER, OWN, EOMA, LEN_20, 0U, 3U, NA, PGN_B);
	run(0U);
	TEST_ASSERT_EQUAL_UINT8(0U, sessions_open());
	TEST_ASSERT_EQUAL_UINT32(0U, s.stats.tp_tx_aborted);
	TEST_ASSERT_EQUAL(J1939_RET_OK, send(PEER, PGN_B, LEN_20));
}

static void test_originator_times_out_with_t3(void) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, send(PEER, PGN_B, LEN_20));
	expect_tx(6U, J1939_PGN_TP_CM, PEER);
	run(0U);
	run(J1939_TP_T3_US - 1U);
	expect_none();
	run(1U);
	expect_abort(PEER, J1939_TP_ABORT_TIMEOUT, PGN_B);
	TEST_ASSERT_EQUAL_UINT32(1U, s.stats.tp_tx_aborted);

	/* T3 also runs after the last packet of a window. */
	TEST_ASSERT_EQUAL(J1939_RET_OK, send(PEER, PGN_B, LEN_20));
	expect_tx(6U, J1939_PGN_TP_CM, PEER);
	rx_cts(PEER, 3U, 1U, PGN_B);
	run(0U);
	expect_tx(6U, J1939_PGN_TP_DT, PEER);
	expect_tx(6U, J1939_PGN_TP_DT, PEER);
	expect_tx(6U, J1939_PGN_TP_DT, PEER);
	run(J1939_TP_T3_US - 1U);
	expect_none();
	run(1U);
	expect_abort(PEER, J1939_TP_ABORT_TIMEOUT, PGN_B);
}

static void test_originator_hold_and_t4(void) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, send(PEER, PGN_B, LEN_20));
	expect_tx(6U, J1939_PGN_TP_CM, PEER);
	rx_cts(PEER, 0U, NA, PGN_B);
	run(0U);
	run(J1939_TP_T4_US - 1U);
	rx_cts(PEER, 0U, NA, PGN_B); /* repeated hold restarts T4 */
	run(0U);
	run(J1939_TP_T4_US - 1U);
	expect_none();
	rx_cts(PEER, 3U, 1U, PGN_B);
	run(0U);
	expect_tx(6U, J1939_PGN_TP_DT, PEER);
	expect_tx(6U, J1939_PGN_TP_DT, PEER);
	expect_tx(6U, J1939_PGN_TP_DT, PEER);

	rx_cts(PEER, 0U, NA, PGN_B);
	run(0U);
	run(J1939_TP_T4_US);
	expect_abort(PEER, J1939_TP_ABORT_TIMEOUT, PGN_B);
	TEST_ASSERT_EQUAL_UINT32(1U, s.stats.tp_tx_aborted);
}

static void test_invalid_cts_aborts(void) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, send(PEER, PGN_B, LEN_20));
	expect_tx(6U, J1939_PGN_TP_CM, PEER);
	rx_cts(PEER, 1U, 0U, PGN_B);
	run(0U);
	expect_abort(PEER, J1939_TP_ABORT_OTHER, PGN_B);

	TEST_ASSERT_EQUAL(J1939_RET_OK, send(PEER, PGN_B, LEN_20));
	expect_tx(6U, J1939_PGN_TP_CM, PEER);
	rx_cts(PEER, 2U, 2U, PGN_B);
	rx_cts(PEER, 2U, 2U, PGN_B); /* second CTS while the packets are being sent */
	run(0U);
	expect_abort(PEER, J1939_TP_ABORT_CTS_IN_DATA, PGN_B);

	TEST_ASSERT_EQUAL(J1939_RET_OK, send(PEER, PGN_B, LEN_20));
	expect_tx(6U, J1939_PGN_TP_CM, PEER);
	rx_cts(PEER, 3U, 2U, PGN_B); /* beyond the last packet */
	run(0U);
	expect_abort(PEER, J1939_TP_ABORT_OTHER, PGN_B);
	expect_none();
	TEST_ASSERT_EQUAL_UINT32(3U, s.stats.tp_tx_aborted);
}

static void test_peer_abort_ends_the_send(void) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, send(PEER, PGN_B, LEN_20));
	expect_tx(6U, J1939_PGN_TP_CM, PEER);
	rx_abort(PEER, OWN, J1939_TP_ABORT_BUSY, PGN_B);
	run(0U);
	expect_none();
	TEST_ASSERT_EQUAL_UINT8(0U, sessions_open());
	TEST_ASSERT_EQUAL_UINT32(1U, s.stats.tp_tx_aborted);
	TEST_ASSERT_EQUAL_UINT8(0U, tp_tx[0].state);
}

static void test_data_waits_for_tx_queue_then_aborts(void) {
	uint32_t i;

	TEST_ASSERT_EQUAL(J1939_RET_OK, send(PEER, PGN_B, LEN_20));
	for (i = 1U; i < TX_LEN; i++) {
		TEST_ASSERT_EQUAL(J1939_RET_OK, send(J1939_ADDR_GLOBAL, PGN_A, 0U));
	}
	(void)j1939_queue_pop(j1939_tx_queue(&s)); /* the RTS */
	rx_cts(PEER, 3U, 1U, PGN_B);
	run(0U);
	TEST_ASSERT_EQUAL_UINT16(TX_LEN, j1939_queue_count(j1939_tx_queue(&s)));
	(void)j1939_queue_pop(j1939_tx_queue(&s));
	run(J1939_TP_TR_US - 1U); /* one packet goes out and restarts Tr */
	TEST_ASSERT_EQUAL_UINT16(TX_LEN, j1939_queue_count(j1939_tx_queue(&s)));
	run(J1939_TP_TR_US - 1U);
	TEST_ASSERT_EQUAL_UINT8(1U, sessions_open());
	run(1U);
	TEST_ASSERT_EQUAL_UINT8(0U, sessions_open());
	TEST_ASSERT_EQUAL_UINT32(1U, s.stats.tp_tx_aborted);
	TEST_ASSERT_EQUAL_UINT32(1U, s.stats.tx_overflow); /* the abort did not fit */
}

static void test_send_resource_errors(void) {
	uint32_t i;

	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, send(PEER, PGN_B, J1939_CFG_TP_BUF_SIZE + 1U));
	TEST_ASSERT_EQUAL(J1939_RET_OK, send(PEER, PGN_B, LEN_20));
	TEST_ASSERT_EQUAL(J1939_RET_OK, send(PEER2, PGN_B, J1939_TP_MSG_MAX));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_FULL, send(PEER3, PGN_B, LEN_20)); /* no session */

	init(1U, BUF_LEN);
	TEST_ASSERT_EQUAL(J1939_RET_OK, send(PEER, PGN_B, LEN_20));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_FULL, send(PEER2, PGN_B, LEN_20)); /* no buffer */

	init(BUF_LEN, BUF_LEN);
	for (i = 0U; i < TX_LEN; i++) {
		TEST_ASSERT_EQUAL(J1939_RET_OK, send(J1939_ADDR_GLOBAL, PGN_A, 8U));
	}
	TEST_ASSERT_EQUAL(J1939_RET_ERR_FULL, send(PEER, PGN_B, LEN_20)); /* no room for RTS */
	TEST_ASSERT_EQUAL_UINT8(0U, sessions_open());
	TEST_ASSERT_EQUAL_UINT8(0U, tp_tx[0].state);
}

static void test_timer_saturates(void) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, send(PEER, PGN_B, LEN_20));
	expect_tx(6U, J1939_PGN_TP_CM, PEER);
	run(0U);
	run(1U);
	run(UINT32_MAX);
	TEST_ASSERT_EQUAL_UINT8(0U, sessions_open());
	expect_abort(PEER, J1939_TP_ABORT_TIMEOUT, PGN_B);
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_init_validates_tp_buffers);
	RUN_TEST(test_bam_is_reassembled_and_delivered);
	RUN_TEST(test_bam_announcements_that_are_ignored);
	RUN_TEST(test_bam_lost_packet_drops_the_message);
	RUN_TEST(test_bam_times_out_after_t1);
	RUN_TEST(test_new_bam_replaces_unfinished_one);
	RUN_TEST(test_bam_from_two_senders_interleaved);
	RUN_TEST(test_bam_without_session_or_buffer_is_counted);
	RUN_TEST(test_bam_without_message_slot_is_dropped);
	RUN_TEST(test_abort_to_global_ends_broadcast_of_its_sender);
	RUN_TEST(test_buffer_is_reclaimed_when_its_slot_is_reused);
	RUN_TEST(test_rts_cts_is_received_and_acknowledged);
	RUN_TEST(test_rts_limit_splits_the_transfer_into_windows);
	RUN_TEST(test_rts_that_cannot_be_received_is_aborted);
	RUN_TEST(test_rts_without_session_or_buffer_is_aborted);
	RUN_TEST(test_rts_for_other_pgn_from_open_peer_is_refused);
	RUN_TEST(test_repeated_rts_restarts_the_connection);
	RUN_TEST(test_out_of_sequence_packet_aborts);
	RUN_TEST(test_responder_times_out_with_t2_and_t1);
	RUN_TEST(test_peer_abort_ends_the_reception);
	RUN_TEST(test_responder_holds_while_message_slots_are_full);
	RUN_TEST(test_responder_gives_up_holding);
	RUN_TEST(test_data_while_holding_aborts);
	RUN_TEST(test_message_slot_taken_before_completion_aborts);
	RUN_TEST(test_responder_frames_dropped_on_full_tx_queue_are_counted);
	RUN_TEST(test_bam_send_paces_packets);
	RUN_TEST(test_bam_send_waits_for_tx_queue_then_gives_up);
	RUN_TEST(test_rts_cts_send);
	RUN_TEST(test_originator_times_out_with_t3);
	RUN_TEST(test_originator_hold_and_t4);
	RUN_TEST(test_invalid_cts_aborts);
	RUN_TEST(test_peer_abort_ends_the_send);
	RUN_TEST(test_data_waits_for_tx_queue_then_aborts);
	RUN_TEST(test_send_resource_errors);
	RUN_TEST(test_timer_saturates);
	return UNITY_END();
}
