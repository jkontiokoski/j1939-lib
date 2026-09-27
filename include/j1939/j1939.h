/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939.h
 * @brief Umbrella header of the J1939 library.
 */

#ifndef J1939_H
#define J1939_H

#include <stdint.h>

#include "j1939/j1939_addr.h"
#include "j1939/j1939_config.h"
#include "j1939/j1939_diag.h"
#include "j1939/j1939_id.h"
#include "j1939/j1939_msg.h"
#include "j1939/j1939_name.h"
#include "j1939/j1939_queue.h"
#include "j1939/j1939_request.h"
#include "j1939/j1939_ret.h"
#include "j1939/j1939_signal.h"
#include "j1939/j1939_stack.h"

#define J1939_VERSION_MAJOR 0
#define J1939_VERSION_MINOR 1
#define J1939_VERSION_PATCH 0

/** Version packed as 0x00MMmmpp (major, minor, patch). */
#define J1939_VERSION                                                                   \
	(((uint32_t)J1939_VERSION_MAJOR << 16) | ((uint32_t)J1939_VERSION_MINOR << 8) | \
	 (uint32_t)J1939_VERSION_PATCH)

/**
 * @brief Version of the compiled library.
 *
 * Compare against @ref J1939_VERSION to detect a mismatch between the headers
 * an application was built with and the library it is linked against.
 *
 * @return Version packed as 0x00MMmmpp.
 */
uint32_t j1939_version_get(void);

#endif /* J1939_H */
