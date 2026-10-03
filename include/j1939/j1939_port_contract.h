/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_port_contract.h
 * @brief The API a port must provide in its j1939_target.h.
 *
 * The library includes the target header only through this file. The
 * declarations below repeat the required API: a port whose definitions do
 * not match them fails to compile, and a missing definition fails the port
 * conformance tests. See docs/porting.md.
 */

#ifndef J1939_PORT_CONTRACT_H
#define J1939_PORT_CONTRACT_H

#include <stdbool.h>
#include <stdint.h>

#include "j1939_target.h"

/* j1939_port_frame_t: the integrator's native CAN frame type. It must be
 * copyable by assignment. */

/** @return true if the frame carries a 29-bit identifier. */
static inline bool j1939_port_frame_is_ext(const j1939_port_frame_t *f);

/** @return true if the frame is a remote transmission request. */
static inline bool j1939_port_frame_is_rtr(const j1939_port_frame_t *f);

/** @return The bare identifier without flag bits (29 bits for extended frames). */
static inline uint32_t j1939_port_frame_id_get(const j1939_port_frame_t *f);

/** @return Payload length, 0..8. Raw DLC values above 8 are reported as 8. */
static inline uint8_t j1939_port_frame_len_get(const j1939_port_frame_t *f);

/** @return Pointer to the payload bytes. */
static inline const uint8_t *j1939_port_frame_data(const j1939_port_frame_t *f);

/**
 * @brief Overwrites @p f with an extended data frame.
 *
 * Every field of the frame is set, so no content of a previous frame remains.
 *
 * @param f     Frame to fill.
 * @param id29  Identifier. Bits above bit 28 are ignored.
 * @param data  Payload, may be NULL when @p len is 0.
 * @param len   Payload length, 0..8. Larger values are treated as 8.
 */
static inline void j1939_port_frame_build(j1939_port_frame_t *f, uint32_t id29, const uint8_t *data,
                                          uint8_t len);

#endif /* J1939_PORT_CONTRACT_H */
