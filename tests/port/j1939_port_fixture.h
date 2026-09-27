/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_port_fixture.h
 * @brief Frames the port conformance tests cannot build through the port API.
 *
 * Each port implements these in its j1939_port_fixture.c.
 */

#ifndef J1939_PORT_FIXTURE_H
#define J1939_PORT_FIXTURE_H

#include <stdint.h>

#include "j1939/j1939_port_contract.h"

/** @brief Overwrites @p f with a standard (11-bit) data frame without payload. */
void j1939_port_fixture_std(j1939_port_frame_t *f, uint16_t id11);

/** @brief Overwrites @p f with an extended remote frame. */
void j1939_port_fixture_rtr(j1939_port_frame_t *f, uint32_t id29);

/** @brief Sets the raw data length code of @p f, including values 9..15. */
void j1939_port_fixture_raw_dlc(j1939_port_frame_t *f, uint8_t dlc);

#endif /* J1939_PORT_FIXTURE_H */
