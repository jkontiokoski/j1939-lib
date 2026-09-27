/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file example_signals.h
 * @brief Illustrative signal table.
 *
 * The signals are invented for this example and are not J1939DA
 * definitions. They are placed in the Proprietary B PGN range
 * (0xFF00-0xFFFF) and use invented SPN numbers from 520192 up. An
 * integrator builds the same kind of table from their licensed J1939DA.
 *
 * PGN 0xFF20, pump controller status, 8 bytes:
 *
 * | Position | Bits | Signal          | Type       | Scaling                        |
 * | -------- | ---- | --------------- | ---------- | ------------------------------ |
 * | 1.1      | 16   | Pump speed      | continuous | 0.125 rpm/bit, in rpm          |
 * | 3.1      | 8    | Oil temperature | continuous | 1 degC/bit, -40 degC           |
 * | 4.1      | 16   | Supply pressure | continuous | 1/128 bar/bit, in mbar         |
 * | 6.1      | 2    | Pump enabled    | discrete   |                                |
 * | 6.3      | 2    | Filter clogged  | discrete   |                                |
 * | 6.5      | 4    | Pump mode       | plain      |                                |
 * | 7.1      | 10   | Valve position  | plain      | 0.1 %/bit, in %                |
 *
 * PGN 0xFF21, pump controller counters, 8 bytes:
 *
 * | Position | Bits | Signal          | Type       | Scaling                        |
 * | -------- | ---- | --------------- | ---------- | ------------------------------ |
 * | 1.1      | 32   | Operating time  | continuous | 0.05 h/bit, in minutes         |
 * | 5.1      | 16   | Tilt angle      | continuous | 1/128 deg/bit, -250 deg, in mdeg |
 * | 7.1      | 8    | Fault count     | continuous | 1/bit                          |
 */

#ifndef EXAMPLE_SIGNALS_H
#define EXAMPLE_SIGNALS_H

#include "j1939/j1939_signal.h"

#define EXAMPLE_PGN_PUMP_STATUS   0xFF20U /**< Pump controller status. */
#define EXAMPLE_PGN_PUMP_COUNTERS 0xFF21U /**< Pump controller counters. */

/** Index of each signal in example_signals. */
typedef enum example_signal_id {
	EXAMPLE_PUMP_SPEED = 0,
	EXAMPLE_OIL_TEMP,
	EXAMPLE_SUPPLY_PRESSURE,
	EXAMPLE_PUMP_ENABLED,
	EXAMPLE_FILTER_CLOGGED,
	EXAMPLE_PUMP_MODE,
	EXAMPLE_VALVE_POSITION,
	EXAMPLE_OPERATING_TIME,
	EXAMPLE_TILT_ANGLE,
	EXAMPLE_FAULT_COUNT,
	EXAMPLE_SIGNAL_COUNT
} example_signal_id_t;

/** Descriptors, indexed by example_signal_id_t. */
extern const j1939_signal_t example_signals[EXAMPLE_SIGNAL_COUNT];

#endif /* EXAMPLE_SIGNALS_H */
