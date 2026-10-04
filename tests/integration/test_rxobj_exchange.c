/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/* Receive objects on the test bus: an ECU broadcasts, a display node supervises it. */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "j1939/j1939.h"
#include "test_bus.h"

#define TX_LEN  16U
#define MSG_LEN 2U

#define ADDR_ECU     0x20U
#define ADDR_DISPLAY 0x30U
#define NAME_ECU     0x2000U /* not arbitrary address capable */
#define NAME_RIVAL   0x1000U /* lower NAME: wins ADDR_ECU */
#define NAME_DISPLAY 0x3000U

#define PGN_STATUS 0xFF10U /* 8 bytes every 100 ms */
#define PGN_INFO   0xFF20U /* 20 bytes, BAM */

#define PERIOD_US  100000U
#define TIMEOUT_US 300000U
#define STEP_US    10000U

enum { OBJ_STATUS = 0, OBJ_INFO, OBJ_COUNT };

typedef struct node {
	j1939_t s;
	j1939_ca_id_t ca;
	j1939_port_frame_t tx[TX_LEN];
	j1939_msg_slot_t msgs[MSG_LEN];
	j1939_tp_buf_t tp_tx[1];
	j1939_tp_buf_t tp_rx[1];
} node_t;

static node_t ecu;
static node_t display;
static node_t rival;
static test_bus_t bus;
static uint8_t status_buf[8];
static uint8_t info_buf[20];
static const j1939_rxobj_cfg_t objs_cfg[OBJ_COUNT] = {
        {status_buf, PGN_STATUS, TIMEOUT_US, 8U, 8U, ADDR_ECU},
        {info_buf, PGN_INFO, 0U, 20U, 9U, ADDR_ECU},
};
static j1939_rxobj_t objs[OBJ_COUNT];
static uint32_t elapsed;
static uint8_t counter;

static void node_init(node_t *n, uint8_t address, uint64_t name) {
	const j1939_cfg_t cfg = {
	        .tx_buf = n->tx,
	        .tx_len = TX_LEN,
	        .msg_buf = n->msgs,
	        .msg_len = MSG_LEN,
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

/* The ECU application: sends PGN_STATUS every PERIOD_US while it holds its address. */
static void ecu_app(void) {
	if (elapsed >= PERIOD_US) {
		uint8_t d[8] = {counter, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU};
		const j1939_msg_t msg = {PGN_STATUS, 6U, 0U, J1939_ADDR_GLOBAL, 8U, d};

		elapsed = 0U;
		if (j1939_send(&ecu.s, ecu.ca, &msg) == J1939_RET_OK) {
			counter++;
		}
	}
}

static void steps(uint32_t count) {
	uint32_t i;
	uint8_t k;

	for (i = 0U; i < count; i++) {
		elapsed += STEP_US;
		ecu_app();
		for (k = 0U; k < bus.count; k++) {
			TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(bus.nodes[k], STEP_US));
		}
		(void)test_bus_run(&bus);
	}
}

static j1939_rxobj_status_t status_of(uint16_t index) {
	j1939_rxobj_status_t st;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_rxobj_get(&display.s, index, &st));
	return st;
}

void setUp(void) {
	test_bus_init(&bus);
	node_init(&ecu, ADDR_ECU, NAME_ECU);
	node_init(&display, ADDR_DISPLAY, NAME_DISPLAY);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_rxobj_init(&display.s, objs_cfg, objs, OBJ_COUNT));
	elapsed = 0U;
	counter = 0U;
	test_bus_settle(&bus, 4U);
}

void tearDown(void) {
}

static void test_periodic_stays_valid(void) {
	uint32_t i;

	TEST_ASSERT_EQUAL(J1939_RXOBJ_NO_DATA, status_of(OBJ_STATUS).state);
	for (i = 0U; i < 20U; i++) {
		steps(PERIOD_US / STEP_US);
		TEST_ASSERT_EQUAL(J1939_RXOBJ_VALID, status_of(OBJ_STATUS).state);
		TEST_ASSERT_LESS_THAN_UINT32(TIMEOUT_US, status_of(OBJ_STATUS).age_us);
	}
	TEST_ASSERT_EQUAL_UINT8((uint8_t)(counter - 1U), status_buf[0]);
	TEST_ASSERT_EQUAL_UINT32(0U, j1939_stats_get(&display.s)->rxobj_timeout);
	/* Nothing was queued for the display application. */
	TEST_ASSERT_NULL(j1939_msg_peek(&display.s));
}

static void test_bam_received_without_slot(void) {
	static const uint8_t info[20] = {0x10U, 0x11U, 0x12U, 0x13U, 0x14U, 0x15U, 0x16U,
	                                 0x17U, 0x18U, 0x19U, 0x1AU, 0x1BU, 0x1CU, 0x1DU,
	                                 0x1EU, 0x1FU, 0x20U, 0x21U, 0x22U, 0x23U};
	const j1939_msg_t msg = {PGN_INFO, 6U, 0U, J1939_ADDR_GLOBAL, 20U, info};

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_send(&ecu.s, ecu.ca, &msg));
	steps(J1939_CFG_TP_BAM_GAP_US * 4U / STEP_US);
	TEST_ASSERT_EQUAL(J1939_RXOBJ_VALID, status_of(OBJ_INFO).state);
	TEST_ASSERT_EQUAL_UINT16(20U, status_of(OBJ_INFO).len);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(info, info_buf, 20U);
	TEST_ASSERT_NULL(j1939_msg_peek(&display.s));
}

static void test_timeout_when_sender_loses_address(void) {
	j1939_rxobj_status_t st;
	j1939_addr_state_t state;
	uint8_t address;

	steps(5U * PERIOD_US / STEP_US);
	TEST_ASSERT_EQUAL(J1939_RXOBJ_VALID, status_of(OBJ_STATUS).state);

	/* A node with a lower NAME claims the ECU's address; the ECU cannot claim another. */
	node_init(&rival, ADDR_ECU, NAME_RIVAL);
	steps(2U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_addr_get(&ecu.s, ecu.ca, &address, &state));
	TEST_ASSERT_EQUAL(J1939_ADDR_STATE_CANNOT_CLAIM, state);

	steps(TIMEOUT_US / STEP_US + 1U);
	st = status_of(OBJ_STATUS);
	TEST_ASSERT_EQUAL(J1939_RXOBJ_TIMEOUT, st.state);
	TEST_ASSERT_GREATER_OR_EQUAL_UINT32(TIMEOUT_US, st.age_us);
	TEST_ASSERT_EQUAL_UINT32(1U, j1939_stats_get(&display.s)->rxobj_timeout);
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_periodic_stays_valid);
	RUN_TEST(test_bam_received_without_slot);
	RUN_TEST(test_timeout_when_sender_loses_address);
	return UNITY_END();
}
