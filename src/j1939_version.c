/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_version.c
 * @brief Library version query.
 */

#include "j1939/j1939.h"

uint32_t j1939_version_get(void) {
	return J1939_VERSION;
}
