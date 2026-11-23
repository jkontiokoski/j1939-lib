/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/* A stack built without the diagnostics: the DM PGNs are handled like any other PGN. */

#include <stdint.h>

#include "unity.h"

#include "j1939/j1939.h"

#if J1939_DM_ENABLE
#error "test_dm_disabled needs a library built with J1939_DM_ENABLE 0"
#endif

#define TX_LEN  8U
#define MSG_LEN 2U

#define OWN  0x10U
#define PEER 0x42U
#define NAME 0x100000U

#define DM_PGNS 4U

static const uint32_t dm_pgns[DM_PGNS] = {J1939_PGN_DM1, J1939_PGN_DM2, J1939_PGN_DM3,
                                          J1939_PGN_DM11};
static const j1939_dtc_lamps_t lamps_off = {.mil = J1939_DTC_LAMP_OFF,
                                            .red_stop = J1939_DTC_LAMP_OFF,
                                            .amber_warning = J1939_DTC_LAMP_OFF,
                                            .protect = J1939_DTC_LAMP_OFF,
                                            .mil_flash = J1939_DTC_FLASH_OFF,
                                            .red_stop_flash = J1939_DTC_FLASH_OFF,
                                            .amber_warning_flash = J1939_DTC_FLASH_OFF,
                                            .protect_flash = J1939_DTC_FLASH_OFF};
static const j1939_dtc_t dtc_a = {520210U, 3U, 1U, J1939_DTC_CM_V4};
static j1939_port_frame_t tx_buf[TX_LEN];
static j1939_msg_slot_t msg_buf[MSG_LEN];
static j1939_ca_cfg_t ca_cfg;
static j1939_cfg_t cfg;
static j1939_t s;

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

static void rx_request(uint8_t da, uint32_t pgn) {
	const uint8_t d[3] = {(uint8_t)pgn, (uint8_t)(pgn >> 8), (uint8_t)(pgn >> 16)};

	rx_raw(make_id(6U, J1939_PGN_REQUEST, da, PEER), d, 3U);
}

static void process(uint32_t us) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(&s, us));
}

static void expect_none(void) {
	TEST_ASSERT_EQUAL_UINT16(0U, s.tx.count);
}

/* Pops the next sent frame, an 8 byte frame of OWN to the global address, and checks its PGN;
 * returns its payload. */
static const uint8_t *expect_pgn(uint32_t pgn) {
	static uint8_t sent[8];
	const j1939_port_frame_t *f = j1939_tx_peek(&s);
	uint32_t i;

	TEST_ASSERT_NOT_NULL_MESSAGE(f, "no frame sent");
	TEST_ASSERT_EQUAL_HEX32(pgn, j1939_id_pgn_get(j1939_port_frame_id_get(f)));
	TEST_ASSERT_EQUAL_HEX8(J1939_ADDR_GLOBAL, j1939_id_da_get(j1939_port_frame_id_get(f)));
	TEST_ASSERT_EQUAL_HEX8(OWN, j1939_id_sa_get(j1939_port_frame_id_get(f)));
	TEST_ASSERT_EQUAL_UINT8(8U, j1939_port_frame_len_get(f));
	for (i = 0U; i < 8U; i++) {
		sent[i] = j1939_port_frame_data(f)[i];
	}
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_tx_pop(&s));
	return sent;
}

void setUp(void) {
	cfg = (j1939_cfg_t){
	        .tx_buf = tx_buf,
	        .tx_len = TX_LEN,
	        .msg_buf = msg_buf,
	        .msg_len = MSG_LEN,
	        .ca = &ca_cfg,
	        .ca_len = 1U,
	};
	ca_cfg = (j1939_ca_cfg_t){.address = OWN, .name = NAME};
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&s, &cfg));
}

void tearDown(void) {
}

/* Claims OWN: the CA may transmit right after its Address Claimed. */
static void claim(void) {
	process(0U);
	(void)expect_pgn(J1939_PGN_ADDRESS_CLAIMED);
}

static void test_claimed_ca_sends_no_dm1(void) {
	claim();
	expect_none();
	process(1000000U);
	expect_none();
}

static void test_dm_requests_are_nacked(void) {
	uint32_t i;

	claim();
	for (i = 0U; i < DM_PGNS; i++) {
		const uint8_t *ack;

		rx_request(OWN, dm_pgns[i]);
		process(0U);
		ack = expect_pgn(J1939_PGN_ACK);
		TEST_ASSERT_EQUAL_HEX8(J1939_ACK_CTRL_NACK, ack[0]);
		TEST_ASSERT_EQUAL_HEX8(PEER, ack[4]);
		TEST_ASSERT_EQUAL_HEX32(dm_pgns[i], (uint32_t)ack[5] | ((uint32_t)ack[6] << 8) |
		                                            ((uint32_t)ack[7] << 16));
		TEST_ASSERT_NULL(j1939_msg_peek(&s));
	}
	/* A global Request is neither answered nor refused. */
	rx_request(J1939_ADDR_GLOBAL, J1939_PGN_DM1);
	process(0U);
	expect_none();
	TEST_ASSERT_NULL(j1939_msg_peek(&s));
}

static void test_dm_requests_in_req_pgns_are_delivered(void) {
	const j1939_msg_t *msg;
	uint32_t pgn = 0U;

	cfg.req_pgns = dm_pgns;
	cfg.req_pgns_len = DM_PGNS;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&s, &cfg));
	claim();
	rx_request(OWN, J1939_PGN_DM11);
	process(0U);
	msg = j1939_msg_peek(&s);
	TEST_ASSERT_NOT_NULL(msg);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_request_pgn_get(msg, &pgn));
	TEST_ASSERT_EQUAL_HEX32(J1939_PGN_DM11, pgn);
	expect_none();
}

static void test_txobj_sends_dm1(void) {
	static uint8_t buf[8];
	const j1939_txobj_cfg_t ocfg = {.buf = buf,
	                                .pgn = J1939_PGN_DM1,
	                                .period_us = 1000000U,
	                                .len = 8U,
	                                .prio = 6U,
	                                .da = J1939_ADDR_GLOBAL,
	                                .ca = 0U};
	j1939_txobj_t obj;
	uint8_t payload[8];
	uint16_t len = 0U;

	cfg.txobj_cfg = &ocfg;
	cfg.txobj = &obj;
	cfg.txobj_len = 1U;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&s, &cfg));
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_dtc_dm_build(&lamps_off, NULL, 0U, payload, sizeof(payload), &len));
	TEST_ASSERT_EQUAL_UINT16(8U, len);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_txobj_set(&s, 0U, payload, len));

	/* Periodic from the claim on, and the answer to a Request. */
	claim();
	TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, expect_pgn(J1939_PGN_DM1), 8U);
	expect_none();
	rx_request(OWN, J1939_PGN_DM1);
	process(0U);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, expect_pgn(J1939_PGN_DM1), 8U);
	expect_none();
}

static void test_received_dm1_is_delivered_for_the_codec(void) {
	static const uint32_t rx_pgns[] = {J1939_PGN_DM1};
	uint8_t d[8];
	uint16_t len = 0U;
	const j1939_msg_t *msg;
	j1939_dtc_lamps_t lamps;
	j1939_dtc_t dtcs[1];
	uint16_t count = 0U;

	cfg.rx_pgns = rx_pgns;
	cfg.rx_pgns_len = 1U;
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&s, &cfg));
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_dtc_dm_build(&lamps_off, &dtc_a, 1U, d, sizeof(d), &len));
	rx_raw(make_id(6U, J1939_PGN_DM1, J1939_ADDR_GLOBAL, PEER), d, (uint8_t)len);
	process(0U);
	msg = j1939_msg_peek(&s);
	TEST_ASSERT_NOT_NULL(msg);
	TEST_ASSERT_EQUAL_HEX32(J1939_PGN_DM1, msg->pgn);
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_dtc_dm_parse(msg->data, msg->len, &lamps, dtcs, 1U, &count));
	TEST_ASSERT_EQUAL_UINT16(1U, count);
	TEST_ASSERT_EQUAL_UINT32(dtc_a.spn, dtcs[0].spn);
	TEST_ASSERT_EQUAL_UINT8(dtc_a.fmi, dtcs[0].fmi);
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_claimed_ca_sends_no_dm1);
	RUN_TEST(test_dm_requests_are_nacked);
	RUN_TEST(test_dm_requests_in_req_pgns_are_delivered);
	RUN_TEST(test_txobj_sends_dm1);
	RUN_TEST(test_received_dm1_is_delivered_for_the_codec);
	return UNITY_END();
}
