/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/*
 * Virtual CAN bus for tests: several stack instances in one process. The
 * harness moves each transmitted frame into the rx queue of every other
 * stack, as a real bus would.
 */

#ifndef TEST_BUS_H
#define TEST_BUS_H

#include <stdint.h>

#include "j1939/j1939_stack.h"

#define TEST_BUS_NODES_MAX 4U

typedef struct test_bus {
	j1939_t *nodes[TEST_BUS_NODES_MAX];
	uint8_t count;
	uint32_t frames; /* Frames carried since init. */
} test_bus_t;

void test_bus_init(test_bus_t *bus);
void test_bus_attach(test_bus_t *bus, j1939_t *node);

/* Transmits every queued frame of every node. Returns the number of frames carried. */
uint32_t test_bus_run(test_bus_t *bus);

/* Runs process on every node and the bus until no frame is carried, at most rounds times. */
void test_bus_settle(test_bus_t *bus, uint32_t rounds);

#endif /* TEST_BUS_H */
