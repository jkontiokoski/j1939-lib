/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/* Commanded Address (J1939/81) on one stack: frames of the peers are injected, sent frames
 * inspected. */

#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "j1939/j1939.h"

#define TX_LEN  8U
#define MSG_LEN 2U
#define BUF_LEN 2U

#define OWN    0x20U
#define OWN_B  0x21U
#define NEW    0x30U
#define SELF   0x80U /* self-configurable: claimed after the contention wait */
#define TOOL   0x42U
#define PEER   0x43U
#define PGN_DS 0xEF00U /* PDU1 proprietary A */
#define CMD    J1939_PGN_COMMANDED_ADDRESS

#define ARB       0x8000000000000000U
#define NAME_WIN  0x0000000000001000U /* beats NAME_OWN */
#define NAME_OWN  0x0000000000001500U /* folds to 0x15: Cannot Claim delay 12.6 ms */
#define NAME_LOSE 0x0000000000002000U /* loses to NAME_OWN */
#define DELAY_OWN (0x15U * 600U)

/* TP.CM control bytes. */
#define RTS   0x10U
#define CTS   0x11U
#define EOMA  0x13U
#define BAM   0x20U
#define ABORT 0xFFU
#define NA    0xFFU

static const uint32_t pgns_ds[] = {PGN_DS};
static const uint32_t pgns_cmd[] = {CMD, J1939_PGN_ADDRESS_CLAIMED};
static j1939_port_frame_t tx_buf[TX_LEN];
static j1939_msg_slot_t msg_buf[MSG_LEN];
static j1939_tp_buf_t tp_tx[BUF_LEN];
static j1939_tp_buf_t tp_rx[BUF_LEN];
static j1939_t s;

static void init(const uint32_t *rx_pgns, uint16_t rx_pgns_len, uint16_t tp_tx_len) {
	const j1939_cfg_t cfg = {
	        .tx_buf = tx_buf,
	        .tx_len = TX_LEN,
	        .msg_buf = msg_buf,
	        .msg_len = MSG_LEN,
	        .rx_pgns = rx_pgns,
	        .rx_pgns_len = rx_pgns_len,
	        .tp_tx_buf = tp_tx,
	        .tp_tx_buf_len = tp_tx_len,
	        .tp_rx_buf = tp_rx,
	        .tp_rx_buf_len = BUF_LEN,
	};

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&s, &cfg));
}

static j1939_ca_id_t ca_add(uint8_t address, uint64_t name, bool accept) {
	j1939_ca_id_t id = 0xFFU;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_ca_add(&s,
	                                             &(j1939_ca_cfg_t){.address = address,
	                                                               .name = name,
	                                                               .accept_commanded = accept},
	                                             &id));
	return id;
}

static uint32_t make_id(uint8_t prio, uint32_t pgn, uint8_t da, uint8_t sa) {
	uint32_t id = 0U;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_id_build(prio, pgn, da, sa, &id));
	return id;
}

static void rx(uint32_t id, const uint8_t *data, uint8_t len) {
	j1939_port_frame_t f;

	j1939_port_frame_build(&f, id, data, len);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_rx(&s, &f));
}

static void rx_cm(uint8_t sa, uint8_t da, uint8_t ctrl, uint8_t b1, uint8_t b2, uint8_t b3,
                  uint8_t b4, uint32_t pgn) {
	const uint8_t d[8] = {
	        ctrl, b1, b2, b3, b4, (uint8_t)pgn, (uint8_t)(pgn >> 8), (uint8_t)(pgn >> 16)};

	rx(make_id(7U, J1939_PGN_TP_CM, da, sa), d, 8U);
}

static void rx_claim(uint8_t sa, uint64_t name) {
	uint8_t data[J1939_NAME_LEN];

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_name_to_bytes(name, data));
	rx(make_id(6U, J1939_PGN_ADDRESS_CLAIMED, J1939_ADDR_GLOBAL, sa), data, J1939_NAME_LEN);
}

/* The two data packets of a Commanded Address from TOOL to da. */
static void rx_command_packets(uint8_t da, uint64_t name, uint8_t address) {
	uint8_t d[16];
	uint8_t p[8];
	uint32_t i;

	(void)memset(d, 0xFF, sizeof(d));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_name_to_bytes(name, d));
	d[8] = address;
	for (i = 0U; i < 2U; i++) {
		p[0] = (uint8_t)(i + 1U);
		(void)memcpy(&p[1], &d[i * 7U], 7U);
		rx(make_id(7U, J1939_PGN_TP_DT, da, TOOL), p, 8U);
	}
}

/* A complete Commanded Address broadcast from TOOL. */
static void rx_command_bam(uint64_t name, uint8_t address) {
	rx_cm(TOOL, J1939_ADDR_GLOBAL, BAM, 9U, 0U, 2U, NA, CMD);
	rx_command_packets(J1939_ADDR_GLOBAL, name, address);
}

static void process(uint32_t elapsed_us) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(&s, elapsed_us));
}

/* Pops the next sent frame, checks its identifier and returns its data in d. */
static void tx_expect(uint32_t id, uint8_t len, uint8_t *d) {
	const j1939_port_frame_t *f = j1939_tx_peek(&s);

	TEST_ASSERT_NOT_NULL_MESSAGE(f, "no frame sent");
	TEST_ASSERT_EQUAL_HEX32(id, j1939_port_frame_id_get(f));
	TEST_ASSERT_EQUAL_UINT8(len, j1939_port_frame_len_get(f));
	(void)memcpy(d, j1939_port_frame_data(f), len);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_tx_pop(&s));
}

static void tx_expect_claim(uint8_t sa, uint64_t name) {
	uint8_t want[J1939_NAME_LEN];
	uint8_t got[J1939_NAME_LEN];

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_name_to_bytes(name, want));
	tx_expect(make_id(6U, J1939_PGN_ADDRESS_CLAIMED, J1939_ADDR_GLOBAL, sa), J1939_NAME_LEN,
	          got);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, J1939_NAME_LEN);
}

static void tx_expect_cm(uint8_t sa, uint8_t da, uint8_t prio, uint8_t ctrl, uint8_t b1,
                         uint8_t b2) {
	uint8_t d[8];

	tx_expect(make_id(prio, J1939_PGN_TP_CM, da, sa), 8U, d);
	TEST_ASSERT_EQUAL_HEX8(ctrl, d[0]);
	TEST_ASSERT_EQUAL_HEX8(b1, d[1]);
	TEST_ASSERT_EQUAL_HEX8(b2, d[2]);
	TEST_ASSERT_EQUAL_HEX8(0xD8U, d[5]);
	TEST_ASSERT_EQUAL_HEX8(0xFEU, d[6]);
	TEST_ASSERT_EQUAL_HEX8(0x00U, d[7]);
}

static void tx_expect_empty(void) {
	TEST_ASSERT_EQUAL_UINT16(0U, s.tx.ring.count);
}

static void tx_drain(void) {
	while (j1939_tx_pop(&s) == J1939_RET_OK) {
	}
}

static void expect_state(j1939_ca_id_t ca, j1939_addr_state_t state, uint8_t address) {
	j1939_addr_state_t st = J1939_ADDR_STATE_UNCLAIMED;
	uint8_t a = 0U;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_addr_get(&s, ca, &a, &st));
	TEST_ASSERT_EQUAL(state, st);
	TEST_ASSERT_EQUAL_HEX8(address, a);
}

static j1939_ret_t send_bc(j1939_ca_id_t ca) {
	static const uint8_t payload[8] = {0};
	const j1939_msg_t msg = {.pgn = 0xFEF1U,
	                         .prio = 6U,
	                         .sa = 0U,
	                         .da = J1939_ADDR_GLOBAL,
	                         .len = 8U,
	                         .data = payload};

	return j1939_send(&s, ca, &msg);
}

/* Adds a CA at OWN that accepts commands and lets it claim its address. */
static j1939_ca_id_t claimed_ca(uint64_t name) {
	j1939_ca_id_t ca = ca_add(OWN, name, true);

	process(0U);
	tx_expect_claim(OWN, name);
	expect_state(ca, J1939_ADDR_STATE_CLAIMED, OWN);
	return ca;
}

void setUp(void) {
	init(NULL, 0U, BUF_LEN);
}

void tearDown(void) {
}

/* A broadcast command moves the CA with the next process(); the old address is given up. */
static void test_accepted_command_moves_ca(void) {
	j1939_ca_id_t ca = claimed_ca(NAME_OWN);

	rx_command_bam(NAME_OWN, NEW);
	tx_expect_empty();
	expect_state(ca, J1939_ADDR_STATE_CLAIMED, OWN);

	process(0U);
	tx_expect_claim(NEW, NAME_OWN);
	tx_expect_empty();
	expect_state(ca, J1939_ADDR_STATE_CLAIMED, NEW);
	TEST_ASSERT_EQUAL(J1939_RET_OK, send_bc(ca));
	TEST_ASSERT_EQUAL_HEX32(make_id(6U, 0xFEF1U, J1939_ADDR_GLOBAL, NEW),
	                        j1939_port_frame_id_get(j1939_tx_peek(&s)));
	tx_drain();

	/* Nothing is left of the command, and the old address no longer answers. */
	process(1000000U);
	tx_expect_empty();
	rx_cm(PEER, OWN, RTS, 9U, 0U, 2U, NA, CMD);
	process(0U);
	tx_expect_empty();
	TEST_ASSERT_NULL(j1939_msg_peek(&s));
}

/* A self-configurable address is claimed with the contention wait. */
static void test_command_to_self_configurable_address_waits(void) {
	j1939_ca_id_t ca = claimed_ca(NAME_OWN);

	rx_command_bam(NAME_OWN, SELF);
	process(100000U);
	tx_expect_claim(SELF, NAME_OWN);
	expect_state(ca, J1939_ADDR_STATE_CLAIMING, SELF);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_NO_ADDRESS, send_bc(ca));

	/* The wait counts from the next call. */
	process(J1939_ADDR_CLAIM_WAIT_US - 1U);
	expect_state(ca, J1939_ADDR_STATE_CLAIMING, SELF);
	process(1U);
	expect_state(ca, J1939_ADDR_STATE_CLAIMED, SELF);
	TEST_ASSERT_EQUAL(J1939_RET_OK, send_bc(ca));
}

/* A command during the contention wait abandons the address being claimed. */
static void test_command_during_claim_wait(void) {
	j1939_ca_id_t ca = ca_add(SELF, NAME_OWN, true);

	process(0U);
	tx_expect_claim(SELF, NAME_OWN);
	rx_command_bam(NAME_OWN, NEW);
	process(100000U);
	process(J1939_ADDR_CLAIM_WAIT_US);
	tx_expect_claim(NEW, NAME_OWN);
	tx_expect_empty();
	expect_state(ca, J1939_ADDR_STATE_CLAIMED, NEW);
	TEST_ASSERT_EQUAL(J1939_RET_OK, send_bc(ca));
}

/* Accepting is opt-in: the default configuration refuses, and the broadcast is not received. */
static void test_command_is_refused_by_default(void) {
	j1939_ca_id_t ca = ca_add(OWN, NAME_OWN, false);

	process(0U);
	tx_expect_claim(OWN, NAME_OWN);
	rx_command_bam(NAME_OWN, NEW);
	process(0U);
	process(0U);
	tx_expect_empty();
	expect_state(ca, J1939_ADDR_STATE_CLAIMED, OWN);
	TEST_ASSERT_EQUAL_UINT32(0U, j1939_stats_get(&s)->tp_rx_refused);
	TEST_ASSERT_EQUAL_UINT32(0U, j1939_stats_get(&s)->tp_rx_aborted);

	/* An RTS for it is refused like any PGN the node does not receive. */
	rx_cm(TOOL, OWN, RTS, 9U, 0U, 2U, NA, CMD);
	process(0U);
	tx_expect_cm(OWN, TOOL, 7U, ABORT, J1939_TP_ABORT_OTHER, NA);
	tx_expect_empty();
}

/* A listed PGN is delivered, whether or not a CA acts on it. */
static void test_listed_command_is_delivered(void) {
	const j1939_msg_t *msg;
	j1939_ca_id_t a;
	j1939_ca_id_t b;

	init(pgns_cmd, 1U, BUF_LEN);
	a = ca_add(OWN, NAME_OWN, false); /* refuses */
	b = ca_add(OWN_B, NAME_LOSE, true);
	process(0U);
	tx_drain();

	rx_command_bam(NAME_OWN, NEW);
	process(0U);
	msg = j1939_msg_peek(&s);
	TEST_ASSERT_NOT_NULL(msg);
	TEST_ASSERT_EQUAL_HEX32(CMD, msg->pgn);
	TEST_ASSERT_EQUAL_HEX8(TOOL, msg->sa);
	TEST_ASSERT_EQUAL_HEX8(J1939_ADDR_GLOBAL, msg->da);
	TEST_ASSERT_EQUAL_UINT16(J1939_ADDR_COMMAND_LEN, msg->len);
	TEST_ASSERT_EQUAL_HEX8(0x15U, msg->data[1]);
	TEST_ASSERT_EQUAL_HEX8(NEW, msg->data[8]);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_msg_pop(&s));
	process(0U);
	tx_expect_empty();
	expect_state(a, J1939_ADDR_STATE_CLAIMED, OWN);
	expect_state(b, J1939_ADDR_STATE_CLAIMED, OWN_B);

	/* The CA that accepts moves, and the application sees that command too. */
	rx_command_bam(NAME_LOSE, NEW);
	process(0U);
	process(0U);
	tx_expect_claim(NEW, NAME_LOSE);
	expect_state(b, J1939_ADDR_STATE_CLAIMED, NEW);
	TEST_ASSERT_EQUAL_HEX8(NEW, j1939_msg_peek(&s)->data[8]);
}

/* Without a free message slot a listed command is lost as a whole, and not acted on. */
static void test_listed_command_without_slot_is_dropped(void) {
	j1939_ca_id_t ca;

	init(pgns_cmd, 2U, BUF_LEN);
	ca = claimed_ca(NAME_OWN);
	rx_claim(PEER, NAME_WIN);
	rx_claim(TOOL, NAME_LOSE); /* both message slots in use */
	rx_command_bam(NAME_OWN, NEW);
	process(0U);
	process(0U);
	tx_expect_empty();
	expect_state(ca, J1939_ADDR_STATE_CLAIMED, OWN);
	TEST_ASSERT_EQUAL_UINT32(1U, j1939_stats_get(&s)->rx_msg_overflow);
	TEST_ASSERT_EQUAL_UINT32(1U, j1939_stats_get(&s)->tp_rx_aborted);
}

static void test_command_for_another_name_is_ignored(void) {
	j1939_ca_id_t ca = claimed_ca(NAME_OWN);

	rx_command_bam(NAME_WIN, NEW);
	process(0U);
	process(0U);
	tx_expect_empty();
	expect_state(ca, J1939_ADDR_STATE_CLAIMED, OWN);
	TEST_ASSERT_EQUAL_HEX8(J1939_ADDR_NULL, s.ca[ca].commanded);
}

/* 254 and 255 are no addresses: the CA keeps its own and sends nothing. */
static void test_command_to_invalid_address_is_ignored(void) {
	j1939_ca_id_t ca = claimed_ca(NAME_OWN);

	rx_command_bam(NAME_OWN, J1939_ADDR_NULL);
	process(0U);
	rx_command_bam(NAME_OWN, J1939_ADDR_GLOBAL);
	process(0U);
	process(0U);
	tx_expect_empty();
	expect_state(ca, J1939_ADDR_STATE_CLAIMED, OWN);
	TEST_ASSERT_EQUAL_UINT32(0U, j1939_stats_get(&s)->tp_rx_aborted);
}

/* The current address is announced again; an address of another local CA is refused. */
static void test_command_to_own_or_local_address(void) {
	j1939_ca_id_t a = ca_add(OWN, NAME_OWN, true);
	j1939_ca_id_t b = ca_add(SELF, NAME_LOSE, true);

	process(0U);
	tx_expect_claim(OWN, NAME_OWN);
	tx_expect_claim(SELF, NAME_LOSE);
	rx_command_bam(NAME_OWN, OWN);
	rx_command_bam(NAME_LOSE, SELF);
	process(100000U);
	process(100000U);
	tx_expect_claim(OWN, NAME_OWN);
	tx_expect_claim(SELF, NAME_LOSE);
	tx_expect_empty();
	expect_state(a, J1939_ADDR_STATE_CLAIMED, OWN);
	/* The wait is not restarted: 250 ms after the first claim. */
	process(J1939_ADDR_CLAIM_WAIT_US - 200000U - 1U);
	expect_state(b, J1939_ADDR_STATE_CLAIMING, SELF);
	process(1U);
	expect_state(b, J1939_ADDR_STATE_CLAIMED, SELF);

	rx_command_bam(NAME_OWN, SELF);
	rx_command_bam(NAME_LOSE, OWN);
	process(0U);
	process(0U);
	tx_expect_empty();
	expect_state(a, J1939_ADDR_STATE_CLAIMED, OWN);
	expect_state(b, J1939_ADDR_STATE_CLAIMED, SELF);
}

/* After the command the normal claiming rules apply: the lower NAME keeps the address. */
static void test_claim_contention_after_command(void) {
	j1939_ca_id_t ca = claimed_ca(NAME_OWN);

	rx_command_bam(NAME_OWN, SELF);
	process(0U);
	process(0U);
	tx_expect_claim(SELF, NAME_OWN);

	/* A weaker contender: the claim is defended and the wait keeps running. */
	rx_claim(SELF, NAME_LOSE);
	process(100000U);
	tx_expect_claim(SELF, NAME_OWN);
	expect_state(ca, J1939_ADDR_STATE_CLAIMING, SELF);
	process(J1939_ADDR_CLAIM_WAIT_US - 100000U);
	expect_state(ca, J1939_ADDR_STATE_CLAIMED, SELF);

	/* A stronger one: the CA is not arbitrary address capable, so it cannot claim. */
	rx_claim(SELF, NAME_WIN);
	process(0U);
	expect_state(ca, J1939_ADDR_STATE_CANNOT_CLAIM, J1939_ADDR_NULL);
	process(DELAY_OWN);
	tx_expect_claim(J1939_ADDR_NULL, NAME_OWN);
	tx_expect_empty();
}

/* An arbitrary address capable CA that loses the commanded address moves on. */
static void test_arbitrary_ca_moves_on_after_losing_commanded_address(void) {
	j1939_ca_id_t ca = claimed_ca(ARB | NAME_OWN);

	rx_command_bam(ARB | NAME_OWN, SELF);
	process(0U);
	process(0U);
	tx_expect_claim(SELF, ARB | NAME_OWN);
	rx_claim(SELF, NAME_WIN);
	process(0U);
	tx_expect_claim(SELF + 1U, ARB | NAME_OWN);
	expect_state(ca, J1939_ADDR_STATE_CLAIMING, SELF + 1U);
}

/* A CA without an address gets one by command; the pending Cannot Claim is dropped. */
static void test_command_leaves_cannot_claim(void) {
	j1939_ca_id_t ca = claimed_ca(NAME_OWN);

	rx_claim(OWN, NAME_WIN);
	process(0U);
	expect_state(ca, J1939_ADDR_STATE_CANNOT_CLAIM, J1939_ADDR_NULL);
	rx_command_bam(NAME_OWN, NEW);
	process(0U);
	process(0U);
	tx_expect_claim(NEW, NAME_OWN);
	tx_expect_empty();
	expect_state(ca, J1939_ADDR_STATE_CLAIMED, NEW);
	process(1000000U);
	tx_expect_empty();
}

/* A CA whose claim has not gone out yet claims the commanded address instead. */
static void test_command_before_first_claim(void) {
	j1939_ca_id_t a = claimed_ca(NAME_LOSE);
	j1939_ca_id_t b = ca_add(OWN_B, NAME_OWN, true);
	uint32_t i;

	for (i = 0U; i < TX_LEN; i++) {
		TEST_ASSERT_EQUAL(J1939_RET_OK, send_bc(a));
	}
	rx_command_bam(NAME_OWN, NEW);
	process(0U);
	expect_state(b, J1939_ADDR_STATE_UNCLAIMED, J1939_ADDR_NULL);

	/* The tx queue is still full when the command applies: the claim is retried. */
	process(0U);
	expect_state(b, J1939_ADDR_STATE_UNCLAIMED, J1939_ADDR_NULL);
	tx_drain();
	process(0U);
	tx_expect_claim(NEW, NAME_OWN);
	tx_expect_empty();
	expect_state(b, J1939_ADDR_STATE_CLAIMED, NEW);

	/* Commanded to the address it is about to claim: the claim goes out as usual. */
	init(NULL, 0U, BUF_LEN);
	a = claimed_ca(NAME_LOSE);
	b = ca_add(OWN_B, NAME_OWN, true);
	for (i = 0U; i < TX_LEN; i++) {
		TEST_ASSERT_EQUAL(J1939_RET_OK, send_bc(a));
	}
	rx_command_bam(NAME_OWN, OWN_B);
	process(0U);
	tx_drain();
	process(0U);
	tx_expect_claim(OWN_B, NAME_OWN);
	tx_expect_empty();
}

/* RTS/CTS to the old address: EndOfMsgAck from it, then the claim; other sessions end. */
static void test_rts_command_ends_sessions_of_old_address(void) {
	static const uint8_t payload[9] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U};
	const j1939_msg_t big = {
	        .pgn = PGN_DS, .prio = 6U, .sa = 0U, .da = PEER, .len = 9U, .data = payload};
	j1939_ca_id_t ca = claimed_ca(NAME_OWN);

	/* An unfinished connection of the old address. */
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_send(&s, ca, &big));
	tx_drain();

	rx_cm(TOOL, OWN, RTS, 9U, 0U, 2U, NA, CMD);
	process(0U);
	tx_expect_cm(OWN, TOOL, 7U, CTS, 2U, 1U);
	rx_command_packets(OWN, NAME_OWN, NEW);
	tx_expect_cm(OWN, TOOL, 7U, EOMA, 9U, 0U);
	tx_expect_empty();
	expect_state(ca, J1939_ADDR_STATE_CLAIMED, OWN);
	TEST_ASSERT_NULL(j1939_msg_peek(&s)); /* not listed: for the stack only */

	process(0U);
	tx_expect_claim(NEW, NAME_OWN);
	tx_expect_empty(); /* no Connection Abort: the old address is no longer ours */
	expect_state(ca, J1939_ADDR_STATE_CLAIMED, NEW);
	TEST_ASSERT_EQUAL_UINT32(0U, j1939_stats_get(&s)->tp_rx_aborted);
	TEST_ASSERT_EQUAL_UINT32(1U, j1939_stats_get(&s)->tp_tx_aborted);
	rx_cm(PEER, OWN, CTS, 2U, 1U, NA, NA, PGN_DS);
	process(J1939_TP_T3_US);
	tx_expect_empty();
}

/* A command for the stack only never waits for a message slot. */
static void test_unlisted_command_needs_no_message_slot(void) {
	j1939_ca_id_t ca;

	init(pgns_ds, 1U, BUF_LEN);
	ca = claimed_ca(NAME_OWN);
	rx(make_id(6U, PGN_DS, OWN, PEER), NULL, 0U);
	rx(make_id(6U, PGN_DS, OWN, PEER), NULL, 0U);
	process(0U);
	TEST_ASSERT_EQUAL_UINT32(0U, j1939_stats_get(&s)->rx_msg_overflow);

	rx_cm(TOOL, OWN, RTS, 9U, 0U, 2U, 1U, CMD);
	process(0U);
	tx_expect_cm(OWN, TOOL, 7U, CTS, 1U, 1U);
	rx_command_packets(OWN, NAME_OWN, NEW);
	process(0U);
	tx_expect_cm(OWN, TOOL, 7U, CTS, 1U, 2U); /* not a hold */
	tx_expect_cm(OWN, TOOL, 7U, EOMA, 9U, 0U);
	process(0U);
	tx_expect_claim(NEW, NAME_OWN);
	expect_state(ca, J1939_ADDR_STATE_CLAIMED, NEW);
}

/* 9 bytes never fit a single frame: a short one is an ordinary message. */
static void test_single_frame_command_is_not_acted_on(void) {
	uint8_t d[8];
	j1939_ca_id_t ca = claimed_ca(NAME_OWN);

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_name_to_bytes(NAME_OWN, d));
	rx(make_id(6U, CMD, J1939_ADDR_GLOBAL, TOOL), d, 8U);
	process(0U);
	process(0U);
	tx_expect_empty();
	expect_state(ca, J1939_ADDR_STATE_CLAIMED, OWN);
	TEST_ASSERT_NULL(j1939_msg_peek(&s));
}

/* A corrupted pending command is dropped; a corrupted claim state drops the command too. */
static void test_corrupted_command_fails_safe(void) {
	j1939_ca_id_t ca = claimed_ca(NAME_OWN);

	s.ca[ca].commanded = J1939_ADDR_GLOBAL;
	process(0U);
	tx_expect_empty();
	expect_state(ca, J1939_ADDR_STATE_CLAIMED, OWN);
	TEST_ASSERT_EQUAL_HEX8(J1939_ADDR_NULL, s.ca[ca].commanded);

	rx_command_bam(NAME_OWN, NEW);
	s.ca[ca].state = (j1939_addr_state_t)42;
	process(0U);
	expect_state(ca, J1939_ADDR_STATE_CANNOT_CLAIM, J1939_ADDR_NULL);
	process(DELAY_OWN);
	tx_expect_claim(J1939_ADDR_NULL, NAME_OWN);
	process(1000000U);
	tx_expect_empty();
	expect_state(ca, J1939_ADDR_STATE_CANNOT_CLAIM, J1939_ADDR_NULL);

	/* The same through the Request path, which runs outside the claim process. */
	rx_command_bam(NAME_OWN, NEW);
	s.ca[ca].state = (j1939_addr_state_t)42;
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_request_send(&s, ca, J1939_PGN_ADDRESS_CLAIMED, J1939_ADDR_GLOBAL));
	tx_drain();
	process(0U);
	process(0U);
	expect_state(ca, J1939_ADDR_STATE_CANNOT_CLAIM, J1939_ADDR_NULL);
}

/* Broadcast: BAM with the NAME and the new address, one packet per gap. */
static void test_command_send_broadcast(void) {
	uint8_t d[8];
	uint8_t name[J1939_NAME_LEN];
	j1939_ca_id_t ca = claimed_ca(NAME_OWN);

	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_addr_command_send(&s, ca, NAME_WIN, SELF, J1939_ADDR_GLOBAL));
	tx_expect_cm(OWN, J1939_ADDR_GLOBAL, 6U, BAM, 9U, 0U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_name_to_bytes(NAME_WIN, name));
	process(0U);
	process(J1939_CFG_TP_BAM_GAP_US);
	tx_expect(make_id(6U, J1939_PGN_TP_DT, J1939_ADDR_GLOBAL, OWN), 8U, d);
	TEST_ASSERT_EQUAL_HEX8(1U, d[0]);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(name, &d[1], 7U);
	process(J1939_CFG_TP_BAM_GAP_US);
	tx_expect(make_id(6U, J1939_PGN_TP_DT, J1939_ADDR_GLOBAL, OWN), 8U, d);
	TEST_ASSERT_EQUAL_HEX8(2U, d[0]);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(&name[7], &d[1], 1U);
	TEST_ASSERT_EQUAL_HEX8(SELF, d[2]);
	TEST_ASSERT_EQUAL_HEX8(0xFFU, d[3]);
	tx_expect_empty();
}

/* Destination specific: RTS/CTS to the target, although the PGN is PDU2. */
static void test_command_send_to_node(void) {
	uint8_t d[8];
	j1939_ca_id_t ca = claimed_ca(NAME_OWN);

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_addr_command_send(&s, ca, NAME_WIN, NEW, PEER));
	tx_expect_cm(OWN, PEER, 6U, RTS, 9U, 0U);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_BUSY, j1939_addr_command_send(&s, ca, NAME_WIN, NEW, PEER));
	rx_cm(PEER, OWN, CTS, 2U, 1U, NA, NA, CMD);
	process(0U);
	process(0U);
	tx_expect(make_id(6U, J1939_PGN_TP_DT, PEER, OWN), 8U, d);
	TEST_ASSERT_EQUAL_HEX8(1U, d[0]);
	tx_expect(make_id(6U, J1939_PGN_TP_DT, PEER, OWN), 8U, d);
	TEST_ASSERT_EQUAL_HEX8(2U, d[0]);
	TEST_ASSERT_EQUAL_HEX8(NEW, d[2]);
	rx_cm(PEER, OWN, EOMA, 9U, 0U, 2U, NA, CMD);
	process(0U);
	tx_expect_empty();
	TEST_ASSERT_EQUAL_UINT32(0U, j1939_stats_get(&s)->tp_tx_aborted);
}

/* The sender obeys the tx gating and needs a transmit buffer. */
static void test_command_send_rejects(void) {
	j1939_ca_id_t ca = ca_add(SELF, NAME_OWN, false);

	TEST_ASSERT_EQUAL(J1939_RET_ERR_NO_ADDRESS,
	                  j1939_addr_command_send(&s, ca, NAME_WIN, NEW, J1939_ADDR_GLOBAL));
	process(0U);
	tx_expect_claim(SELF, NAME_OWN);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_NO_ADDRESS,
	                  j1939_addr_command_send(&s, ca, NAME_WIN, NEW, J1939_ADDR_GLOBAL));
	process(J1939_ADDR_CLAIM_WAIT_US);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG,
	                  j1939_addr_command_send(&s, ca, NAME_WIN, J1939_ADDR_NULL, PEER));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG,
	                  j1939_addr_command_send(&s, ca, NAME_WIN, J1939_ADDR_GLOBAL, PEER));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG,
	                  j1939_addr_command_send(&s, ca, NAME_WIN, NEW, J1939_ADDR_NULL));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG,
	                  j1939_addr_command_send(NULL, ca, NAME_WIN, NEW, PEER));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_addr_command_send(&s, 1U, NAME_WIN, NEW, PEER));
	tx_expect_empty();

	init(NULL, 0U, 0U);
	ca = claimed_ca(NAME_OWN);
	TEST_ASSERT_EQUAL(J1939_RET_ERR_FULL,
	                  j1939_addr_command_send(&s, ca, NAME_WIN, NEW, J1939_ADDR_GLOBAL));
	tx_expect_empty();
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_accepted_command_moves_ca);
	RUN_TEST(test_command_to_self_configurable_address_waits);
	RUN_TEST(test_command_during_claim_wait);
	RUN_TEST(test_command_is_refused_by_default);
	RUN_TEST(test_listed_command_is_delivered);
	RUN_TEST(test_listed_command_without_slot_is_dropped);
	RUN_TEST(test_command_for_another_name_is_ignored);
	RUN_TEST(test_command_to_invalid_address_is_ignored);
	RUN_TEST(test_command_to_own_or_local_address);
	RUN_TEST(test_claim_contention_after_command);
	RUN_TEST(test_arbitrary_ca_moves_on_after_losing_commanded_address);
	RUN_TEST(test_command_leaves_cannot_claim);
	RUN_TEST(test_command_before_first_claim);
	RUN_TEST(test_rts_command_ends_sessions_of_old_address);
	RUN_TEST(test_unlisted_command_needs_no_message_slot);
	RUN_TEST(test_single_frame_command_is_not_acted_on);
	RUN_TEST(test_corrupted_command_fails_safe);
	RUN_TEST(test_command_send_broadcast);
	RUN_TEST(test_command_send_to_node);
	RUN_TEST(test_command_send_rejects);
	return UNITY_END();
}
