/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/* Diagnostics between two nodes on the test bus: a reporting ECU and a diagnostic tool. */

#include <stdbool.h>
#include <stdint.h>

#include "unity.h"

#include "j1939/j1939.h"
#include "test_bus.h"

#define TX_LEN  16U
#define MSG_LEN 4U
#define DTC_LEN 8U

#define ADDR_ECU  0x00U
#define ADDR_TOOL 0xF9U
#define NAME_ECU  0x100U
#define NAME_TOOL 0x200U

#define STEP_US 10000U /* process period */

typedef struct node {
	j1939_t s;
	j1939_ca_id_t ca;
	j1939_port_frame_t tx[TX_LEN];
	j1939_msg_slot_t msgs[MSG_LEN];
	j1939_tp_buf_t tp_tx[1];
	j1939_tp_buf_t tp_rx[1];
} node_t;

static const uint32_t tool_pgns[] = {J1939_PGN_DM1, J1939_PGN_DM2, J1939_PGN_ACK};

static const j1939_diag_dtc_t dtc_a = {110U, 0U, 1U, J1939_DIAG_CM_V4};
static const j1939_diag_dtc_t dtc_b = {190U, 2U, 4U, J1939_DIAG_CM_V4};
static const j1939_diag_dtc_t dtc_c = {0x7FFF0U, 31U, 126U, J1939_DIAG_CM_V4};

static node_t ecu;
static node_t tool;
static test_bus_t bus;
static j1939_diag_dtc_t active[DTC_LEN];
static j1939_diag_dtc_t prev[DTC_LEN];
static j1939_dm_hold_t hold[DTC_LEN];
static uint8_t dm_buf[J1939_DM_BUF_LEN(DTC_LEN)];
static j1939_dm_t dm;

/* What the tool has received. */
static j1939_diag_dtc_t got[DTC_LEN];
static uint16_t got_count;
static j1939_diag_lamps_t got_lamps;
static uint32_t dm1_count;
static uint32_t dm2_count;
static uint32_t ack_count;
static uint8_t ack_ctrl;
static uint8_t dm2_da;
static uint32_t ack_pgn;

static void node_init(node_t *n, uint8_t address, uint64_t name, const uint32_t *pgns,
                      uint16_t pgns_len) {
	const j1939_cfg_t cfg = {
	        .tx_buf = n->tx,
	        .tx_len = TX_LEN,
	        .msg_buf = n->msgs,
	        .msg_len = MSG_LEN,
	        .rx_pgns = pgns,
	        .rx_pgns_len = pgns_len,
	        .tp_tx_buf = n->tp_tx,
	        .tp_tx_buf_len = 1U,
	        .tp_rx_buf = n->tp_rx,
	        .tp_rx_buf_len = 1U,
	};

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_init(&n->s, &cfg));
	TEST_ASSERT_EQUAL(
	        J1939_RET_OK,
	        j1939_ca_add(&n->s, &(j1939_ca_cfg_t){.address = address, .name = name}, &n->ca));
	test_bus_attach(&bus, &n->s);
}

/* The tool's application: parses DM1 and DM2, records acknowledgements. */
static void tool_poll(void) {
	const j1939_msg_t *msg;

	while ((msg = j1939_msg_peek(&tool.s)) != NULL) {
		TEST_ASSERT_EQUAL_HEX8(ADDR_ECU, msg->sa);
		if (msg->pgn == J1939_PGN_ACK) {
			ack_count++;
			ack_ctrl = msg->data[0];
			TEST_ASSERT_EQUAL_HEX8(ADDR_TOOL, msg->data[4]);
			ack_pgn = (uint32_t)msg->data[5] | ((uint32_t)msg->data[6] << 8) |
			          ((uint32_t)msg->data[7] << 16);
		} else {
			TEST_ASSERT_EQUAL(J1939_RET_OK,
			                  j1939_diag_dm_parse(msg->data, msg->len, &got_lamps, got,
			                                      DTC_LEN, &got_count));
			if (msg->pgn == J1939_PGN_DM1) {
				dm1_count++;
			} else {
				dm2_count++;
				dm2_da = msg->da;
			}
		}
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_msg_pop(&tool.s));
	}
}

static void steps(uint32_t count) {
	uint32_t i;

	for (i = 0U; i < count; i++) {
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(&ecu.s, STEP_US));
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(&tool.s, STEP_US));
		(void)test_bus_run(&bus);
		tool_poll();
	}
}

static void expect_got(const j1939_diag_dtc_t *dtcs, uint16_t n) {
	uint16_t i;

	TEST_ASSERT_EQUAL_UINT16(n, got_count);
	for (i = 0U; i < n; i++) {
		TEST_ASSERT_EQUAL_UINT32(dtcs[i].spn, got[i].spn);
		TEST_ASSERT_EQUAL_UINT8(dtcs[i].fmi, got[i].fmi);
		TEST_ASSERT_EQUAL_UINT8(dtcs[i].oc, got[i].oc);
		TEST_ASSERT_EQUAL_UINT8(J1939_DIAG_CM_V4, got[i].cm);
	}
}

void setUp(void) {
	const j1939_dm_cfg_t dm_cfg = {
	        .active = active,
	        .active_len = DTC_LEN,
	        .prev = prev,
	        .prev_len = DTC_LEN,
	        .hold = hold,
	        .hold_len = DTC_LEN,
	        .buf = dm_buf,
	        .buf_len = sizeof(dm_buf),
	        .dm3_enable = true,
	        .dm11_enable = true,
	};

	test_bus_init(&bus);
	node_init(&ecu, ADDR_ECU, NAME_ECU, NULL, 0U);
	node_init(&tool, ADDR_TOOL, NAME_TOOL, tool_pgns, 3U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_dm_init(&ecu.s, ecu.ca, &dm, &dm_cfg));
	dm1_count = 0U;
	dm2_count = 0U;
	ack_count = 0U;
	got_count = 0xFFFFU;
}

void tearDown(void) {
}

static void test_tool_receives_dm1_single_frame_and_bam(void) {
	const j1939_diag_dtc_t three[3] = {dtc_a, dtc_b, dtc_c};
	const j1939_diag_lamps_t lamps = {1U, 0U, 1U, 0U, 3U, 3U, 0U, 3U};

	/* Claims, then the first DM1: no DTCs. */
	steps(2U);
	TEST_ASSERT_EQUAL_UINT32(1U, dm1_count);
	expect_got(NULL, 0U);

	/* One DTC: a single frame, sent on the change. */
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_dm_lamps_set(&ecu.s, ecu.ca, &lamps));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_dm_active_set(&ecu.s, ecu.ca, &dtc_a, 1U));
	steps(2U); /* sent by the ECU, then handled by the tool */
	TEST_ASSERT_EQUAL_UINT32(2U, dm1_count);
	expect_got(&dtc_a, 1U);
	TEST_ASSERT_EQUAL_UINT8(1U, got_lamps.mil);
	TEST_ASSERT_EQUAL_UINT8(1U, got_lamps.amber_warning);
	TEST_ASSERT_EQUAL_UINT8(J1939_DIAG_FLASH_SLOW, got_lamps.amber_warning_flash);

	/* Three DTCs: BAM, reassembled by the tool. */
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_dm_active_set(&ecu.s, ecu.ca, three, 3U));
	steps(20U);
	TEST_ASSERT_EQUAL_UINT32(3U, dm1_count);
	expect_got(three, 3U);

	/* Periodic DM1s continue once per second. */
	steps(100U);
	TEST_ASSERT_EQUAL_UINT32(4U, dm1_count);
	expect_got(three, 3U);
	TEST_ASSERT_EQUAL_UINT32(0U, j1939_stats_get(&ecu.s)->dm_tx_dropped);
	TEST_ASSERT_EQUAL_UINT32(0U, j1939_stats_get(&tool.s)->tp_rx_aborted);
}

static void test_tool_requests_dm2_and_clears_with_dm11(void) {
	const j1939_diag_dtc_t two[2] = {dtc_b, dtc_c};
	uint32_t pgn = 0U;

	steps(2U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_dm_prev_set(&ecu.s, ecu.ca, two, 2U));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_dm_active_set(&ecu.s, ecu.ca, &dtc_a, 1U));
	steps(2U); /* sent by the ECU, then handled by the tool */
	expect_got(&dtc_a, 1U);

	/* DM2 with two DTCs to a destination specific Request: RTS/CTS to the tool. */
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_request_send(&tool.s, tool.ca, J1939_PGN_DM2, ADDR_ECU));
	steps(20U);
	TEST_ASSERT_EQUAL_UINT32(1U, dm2_count);
	TEST_ASSERT_EQUAL_HEX8(ADDR_TOOL, dm2_da);
	expect_got(two, 2U);

	/* DM11: the ECU application decides, the tool gets the acknowledgement. */
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_request_send(&tool.s, tool.ca, J1939_PGN_DM11, ADDR_ECU));
	steps(2U);
	TEST_ASSERT_EQUAL_UINT32(0U, ack_count);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_dm_clear_get(&ecu.s, ecu.ca, &pgn));
	TEST_ASSERT_EQUAL_HEX32(J1939_PGN_DM11, pgn);
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_dm_clear_confirm(&ecu.s, ecu.ca, J1939_PGN_DM11, true));
	steps(2U);
	TEST_ASSERT_EQUAL_UINT32(1U, ack_count);
	TEST_ASSERT_EQUAL_UINT8(J1939_ACK_CTRL_ACK, ack_ctrl);
	TEST_ASSERT_EQUAL_HEX32(J1939_PGN_DM11, ack_pgn);

	/* The active DTCs are gone at the next periodic DM1 (A's change is held). */
	steps(100U);
	expect_got(NULL, 0U);

	/* DM2 is not affected by DM11. A global Request is answered with BAM. */
	TEST_ASSERT_EQUAL(J1939_RET_OK,
	                  j1939_request_send(&tool.s, tool.ca, J1939_PGN_DM2, J1939_ADDR_GLOBAL));
	steps(20U);
	TEST_ASSERT_EQUAL_UINT32(2U, dm2_count);
	TEST_ASSERT_EQUAL_HEX8(J1939_ADDR_GLOBAL, dm2_da);
	expect_got(two, 2U);
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_tool_receives_dm1_single_frame_and_bam);
	RUN_TEST(test_tool_requests_dm2_and_clears_with_dm11);
	return UNITY_END();
}
