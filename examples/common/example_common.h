/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file example_common.h
 * @brief Small helpers shared by the SocketCAN example applications.
 *
 * Only the pieces that have nothing to do with J1939 live here: reading the
 * monotonic clock, stopping on Ctrl-C, parsing numbers and printing. Each
 * example spells out its own main loop, since that loop is what an
 * integrator copies.
 */

#ifndef EXAMPLE_COMMON_H
#define EXAMPLE_COMMON_H

#include <stdbool.h>
#include <stdint.h>

#include "j1939/j1939.h"

#define EXAMPLE_IFNAME_DEFAULT "vcan0" /**< Interface used when -i is not given. */

/**
 * Main loop tick: the longest poll() waits for a frame. The stack's timers
 * and the BAM packet gap advance only when j1939_process() runs, so the tick
 * bounds their jitter: a 50 ms BAM gap is sent 50-60 ms apart.
 */
#define EXAMPLE_TICK_MS 10

/**
 * Most frames read from the socket per tick. Further frames wait in the
 * socket, so an RTS/CTS burst of up to 255 data packets is read in one tick.
 */
#define EXAMPLE_RX_PER_TICK 256U

/**
 * @brief Prepares the process: SIGINT and SIGTERM make example_running()
 * return false, stdout is line buffered so that output piped to a file
 * appears at once, and example_uptime_s() counts from here.
 */
void example_start(void);

/** @return false once SIGINT or SIGTERM has been received. */
bool example_running(void);

/** @return CLOCK_MONOTONIC in microseconds. */
uint64_t example_now_us(void);

/**
 * @brief Time since the previous call, for j1939_process().
 *
 * @param last_us  Time of the previous call; updated to now. Initialise it
 *                 with example_now_us().
 * @return Elapsed microseconds, saturated to UINT32_MAX.
 */
uint32_t example_elapsed_us(uint64_t *last_us);

/** @return Seconds since example_start(), for log lines. */
double example_uptime_s(void);

/**
 * @brief Parses a decimal or 0x-prefixed hexadecimal number.
 *
 * @return true if @p s is a whole number no larger than @p max.
 */
bool example_parse_u32(const char *s, uint32_t max, uint32_t *value);

/**
 * @brief Builds the NAME used by the examples.
 *
 * The fields are invented example values. The identity number makes NAMEs
 * of several instances differ; together with the arbitrary address capable
 * bit (bit 63, which makes a NAME numerically larger) it decides which
 * instance wins address arbitration: the lower NAME wins.
 */
uint64_t example_name(uint32_t identity, bool arbitrary_address);

/** @return Printable name of an address claim state. */
const char *example_state_name(j1939_addr_state_t state);

/** Prints a message header and its payload in hex. */
void example_msg_print(const j1939_msg_t *msg);

#endif /* EXAMPLE_COMMON_H */
