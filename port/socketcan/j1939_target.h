/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_target.h
 * @brief Linux SocketCAN (CAN_RAW) port.
 *
 * Native frame: struct can_frame. The port defines no lock: the socket is
 * the frame queue between the kernel and the stack.
 */

#ifndef J1939_TARGET_H
#define J1939_TARGET_H

#include <linux/can.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef struct can_frame j1939_port_frame_t;

static inline bool j1939_port_frame_is_ext(const j1939_port_frame_t *f) {
	return (f->can_id & CAN_EFF_FLAG) != 0U;
}

static inline bool j1939_port_frame_is_rtr(const j1939_port_frame_t *f) {
	return (f->can_id & CAN_RTR_FLAG) != 0U;
}

static inline uint32_t j1939_port_frame_id_get(const j1939_port_frame_t *f) {
	uint32_t mask = j1939_port_frame_is_ext(f) ? CAN_EFF_MASK : CAN_SFF_MASK;

	return f->can_id & mask;
}

/* can_dlc is used instead of len so that kernel headers older than 5.11 work. */
static inline uint8_t j1939_port_frame_len_get(const j1939_port_frame_t *f) {
	uint8_t len = f->can_dlc;

	if (len > CAN_MAX_DLEN) {
		len = CAN_MAX_DLEN;
	}
	return len;
}

static inline const uint8_t *j1939_port_frame_data(const j1939_port_frame_t *f) {
	return f->data;
}

static inline void j1939_port_frame_build(j1939_port_frame_t *f, uint32_t id29, const uint8_t *data,
                                          uint8_t len) {
	uint8_t n = (len > CAN_MAX_DLEN) ? (uint8_t)CAN_MAX_DLEN : len;

	(void)memset(f, 0, sizeof(*f));
	f->can_id = (id29 & CAN_EFF_MASK) | CAN_EFF_FLAG;
	f->can_dlc = n;
	if ((data != NULL) && (n > 0U)) {
		(void)memcpy(f->data, data, n);
	}
}

#endif /* J1939_TARGET_H */
