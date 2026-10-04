/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

#define _POSIX_C_SOURCE 200809L /* clock_gettime, sigaction */

#include "example_common.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define US_PER_S  1000000U
#define NS_PER_US 1000U
#define HEX_WRAP  16U /* Payload bytes per printed line. */

static volatile sig_atomic_t stop_requested;
static uint64_t start_us;

static void on_signal(int sig) {
	(void)sig;
	stop_requested = 1;
}

void example_start(void) {
	struct sigaction sa;

	start_us = example_now_us();
	(void)setvbuf(stdout, NULL, _IOLBF, 0);

	/* No SA_RESTART: a signal interrupts poll() so the loop ends at once. */
	(void)memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_signal;
	(void)sigemptyset(&sa.sa_mask);
	(void)sigaction(SIGINT, &sa, NULL);
	(void)sigaction(SIGTERM, &sa, NULL);
}

bool example_running(void) {
	return stop_requested == 0;
}

uint64_t example_now_us(void) {
	struct timespec ts;

	(void)clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((uint64_t)ts.tv_sec * US_PER_S) + ((uint64_t)ts.tv_nsec / NS_PER_US);
}

uint32_t example_elapsed_us(uint64_t *last_us) {
	uint64_t now = example_now_us();
	uint64_t elapsed = now - *last_us;

	*last_us = now;
	return (elapsed > UINT32_MAX) ? UINT32_MAX : (uint32_t)elapsed;
}

double example_uptime_s(void) {
	return (double)(example_now_us() - start_us) / (double)US_PER_S;
}

bool example_parse_u32(const char *s, uint32_t max, uint32_t *value) {
	char *end = NULL;
	unsigned long v;
	bool ok = false;

	errno = 0;
	v = strtoul(s, &end, 0);
	if ((errno == 0) && (end != s) && (*end == '\0') && (s[0] != '-') && (v <= max)) {
		*value = (uint32_t)v;
		ok = true;
	}
	return ok;
}

uint64_t example_name(uint32_t identity, bool arbitrary_address) {
	/* Invented example values, not assigned codes. */
	const j1939_name_fields_t fields = {
	        .arbitrary_address = arbitrary_address,
	        .industry_group = 0U,   /* Global. */
	        .vehicle_system = 0U,   /* Non-specific system. */
	        .function = 0x80U,      /* First industry group specific function. */
	        .manufacturer = 0x7FFU, /* Largest code, used here for "example". */
	        .identity = identity & J1939_NAME_IDENTITY_MAX,
	};
	uint64_t name = 0U;

	(void)j1939_name_encode(&fields, &name);
	return name;
}

const char *example_state_name(j1939_addr_state_t state) {
	const char *name = "?";

	switch (state) {
	case J1939_ADDR_STATE_UNCLAIMED:
		name = "UNCLAIMED";
		break;
	case J1939_ADDR_STATE_CLAIMING:
		name = "CLAIMING";
		break;
	case J1939_ADDR_STATE_CLAIMED:
		name = "CLAIMED";
		break;
	case J1939_ADDR_STATE_CANNOT_CLAIM:
		name = "CANNOT_CLAIM";
		break;
	case J1939_ADDR_STATE_REQUESTING:
		name = "REQUESTING";
		break;
	default:
		break;
	}
	return name;
}

void example_msg_print(const j1939_msg_t *msg) {
	uint16_t i;

	(void)printf("[%8.3f] PGN 0x%05X (%u) prio %u SA 0x%02X DA 0x%02X len %u",
	             example_uptime_s(), (unsigned)msg->pgn, (unsigned)msg->pgn,
	             (unsigned)msg->prio, (unsigned)msg->sa, (unsigned)msg->da, (unsigned)msg->len);
	for (i = 0U; i < msg->len; i++) {
		if ((i % HEX_WRAP) == 0U) {
			(void)printf("\n           ");
		}
		(void)printf(" %02X", (unsigned)msg->data[i]);
	}
	(void)printf("\n");
}
