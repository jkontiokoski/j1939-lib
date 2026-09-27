/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_msg.h
 * @brief Logical J1939 message.
 */

#ifndef J1939_MSG_H
#define J1939_MSG_H

#include <stdint.h>

#define J1939_MSG_SINGLE_FRAME_MAX 8U /**< Largest payload sent in one CAN frame. */

/** A J1939 message, independent of how it travels on the bus. */
typedef struct j1939_msg {
	uint32_t pgn;        /**< Parameter group number. */
	uint8_t prio;        /**< Priority, 0 (highest) .. 7. */
	uint8_t sa;          /**< Source address. Ignored when sending: the CA's address is used. */
	uint8_t da;          /**< Destination address; J1939_ADDR_GLOBAL for PDU2 in one frame. */
	uint16_t len;        /**< Payload length in bytes. */
	const uint8_t *data; /**< Payload; may be NULL when len is 0. */
} j1939_msg_t;

#endif /* J1939_MSG_H */
