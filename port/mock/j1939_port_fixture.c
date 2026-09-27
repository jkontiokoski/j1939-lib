/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/* Port conformance test fixture for the mock port. */

#include "j1939_port_fixture.h"

#include <string.h>

void j1939_port_fixture_std(j1939_port_frame_t *f, uint16_t id11) {
	(void)memset(f, 0, sizeof(*f));
	f->ir = ((uint32_t)id11 & J1939_MOCK_STID_MASK) << J1939_MOCK_IR_STID_SHIFT;
}

void j1939_port_fixture_rtr(j1939_port_frame_t *f, uint32_t id29) {
	j1939_port_frame_build(f, id29, NULL, 0U);
	f->ir |= J1939_MOCK_IR_RTR;
}

void j1939_port_fixture_raw_dlc(j1939_port_frame_t *f, uint8_t dlc) {
	f->dlc = (uint8_t)(dlc & J1939_MOCK_DLC_MASK);
}
