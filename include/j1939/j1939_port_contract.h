/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_port_contract.h
 * @brief The API a port provides in its j1939_target.h.
 *
 * A port is a directory with a header named j1939_target.h, selected at
 * build time with the J1939_PORT_DIR CMake option. The library includes that
 * header only through this file. The declarations below repeat the API the
 * header must define: a port whose definitions do not match them fails to
 * compile, and a missing definition fails the port conformance tests.
 *
 * The port supplies:
 *
 * - **j1939_port_frame_t**: a typedef of the integrator's native CAN frame
 *   type, for example the driver's own receive and transmit structure. The
 *   library has no frame type of its own. The type must be copyable by
 *   assignment.
 * - **Frame accessors**: the static inline functions below. They hide the
 *   driver's layout, such as identifier flag bits or the name of the length
 *   field, and are the library's only access to a frame.
 * - **Lock** (only with the optional frame queue): j1939_port_lock_t and the
 *   functions declared in j1939_queue.h.
 *
 * Accessors run in the stack's execution context and must not block. The
 * porting guide, docs/porting.md, shows complete ports.
 */

#ifndef J1939_PORT_CONTRACT_H
#define J1939_PORT_CONTRACT_H

#include <stdbool.h>
#include <stdint.h>

#include "j1939_target.h"

/* j1939_port_frame_t: the integrator's native CAN frame type, typedef by the port. */

/**
 * @brief Tells whether a frame has an extended identifier.
 *
 * @param f  Frame.
 * @return true if the frame carries a 29-bit identifier.
 */
static inline bool j1939_port_frame_is_ext(const j1939_port_frame_t *f);

/**
 * @brief Tells whether a frame is a remote frame.
 *
 * @param f  Frame.
 * @return true if the frame is a remote transmission request.
 */
static inline bool j1939_port_frame_is_rtr(const j1939_port_frame_t *f);

/**
 * @brief Reads the identifier of a frame.
 *
 * @param f  Frame.
 * @return The bare identifier without flag bits (29 bits for extended frames).
 */
static inline uint32_t j1939_port_frame_id_get(const j1939_port_frame_t *f);

/**
 * @brief Reads the payload length of a frame.
 *
 * @param f  Frame.
 * @return Payload length, 0..8. Raw DLC values above 8 are reported as 8.
 */
static inline uint8_t j1939_port_frame_len_get(const j1939_port_frame_t *f);

/**
 * @brief Gives access to the payload of a frame.
 *
 * @param f  Frame.
 * @return Pointer to the payload bytes, valid as long as the frame.
 */
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
