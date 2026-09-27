/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

#include "test_bus.h"

#include "unity.h"

void test_bus_init(test_bus_t *bus) {
	bus->count = 0U;
	bus->frames = 0U;
}

void test_bus_attach(test_bus_t *bus, j1939_t *node) {
	TEST_ASSERT_LESS_THAN_UINT8(TEST_BUS_NODES_MAX, bus->count);
	bus->nodes[bus->count] = node;
	bus->count++;
}

uint32_t test_bus_run(test_bus_t *bus) {
	uint32_t carried = 0U;
	uint8_t i;
	uint8_t j;

	for (i = 0U; i < bus->count; i++) {
		j1939_queue_t *tx = j1939_tx_queue(bus->nodes[i]);
		const j1939_port_frame_t *f;

		while ((f = j1939_queue_peek(tx)) != NULL) {
			for (j = 0U; j < bus->count; j++) {
				if (j != i) {
					TEST_ASSERT_EQUAL_MESSAGE(
					        J1939_RET_OK,
					        j1939_queue_put(j1939_rx_queue(bus->nodes[j]), f),
					        "rx queue full on the test bus");
				}
			}
			TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_queue_pop(tx));
			carried++;
		}
	}
	bus->frames += carried;
	return carried;
}

void test_bus_settle(test_bus_t *bus, uint32_t rounds) {
	uint32_t r;
	uint8_t i;
	uint32_t carried = 1U;

	for (r = 0U; (r < rounds) && (carried > 0U); r++) {
		for (i = 0U; i < bus->count; i++) {
			TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_process(bus->nodes[i], 0U));
		}
		carried = test_bus_run(bus);
	}
}
