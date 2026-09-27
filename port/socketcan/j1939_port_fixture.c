/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/* Port conformance test fixture for the SocketCAN port. */

#include "j1939_port_fixture.h"

#include <string.h>

void j1939_port_fixture_std(j1939_port_frame_t *f, uint16_t id11) {
	(void)memset(f, 0, sizeof(*f));
	f->can_id = (canid_t)id11 & CAN_SFF_MASK;
}

void j1939_port_fixture_rtr(j1939_port_frame_t *f, uint32_t id29) {
	j1939_port_frame_build(f, id29, NULL, 0U);
	f->can_id |= CAN_RTR_FLAG;
}

void j1939_port_fixture_raw_dlc(j1939_port_frame_t *f, uint8_t dlc) {
	f->can_dlc = dlc;
}
