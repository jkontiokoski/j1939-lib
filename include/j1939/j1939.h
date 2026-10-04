/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939.h
 * @brief Umbrella header of the J1939 library: includes every module header.
 */

/**
 * @defgroup grp_stack Stack
 * @brief One stack instance per CAN bus: configuration, Controller
 * Applications, receiving, processing and transmitting.
 */

/**
 * @defgroup grp_addr Address claiming
 * @brief J1939/81 address claiming and Commanded Address.
 */

/**
 * @defgroup grp_request Requests
 * @brief J1939/21 Request (PGN 59904) and Acknowledgement (PGN 59392).
 */

/**
 * @defgroup grp_tp Transport protocol
 * @brief J1939/21 multi-packet messages: broadcast (BAM) and connection mode
 * (RTS/CTS).
 */

/**
 * @defgroup grp_objects Message objects
 * @brief PGNs the stack receives with timeout supervision or sends
 * periodically, on change and on Request.
 *
 * Message objects keep the protocol behaviour of application data in the
 * stack. A receive object holds the latest payload of one PGN from one sender
 * and supervises its timeout; a transmit object holds a payload that
 * j1939_process() sends periodically, on change and on Request. Both tables
 * are `const` integrator data, e.g. generated, and every buffer is exactly as
 * large as the integrator declares it.
 */

/**
 * @defgroup grp_rxobj Receive objects
 * @ingroup grp_objects
 * @brief The latest payload of one PGN from one sender, with timeout
 * supervision.
 */

/**
 * @defgroup grp_txobj Transmit objects
 * @ingroup grp_objects
 * @brief PGNs that the stack sends periodically, on change and on Request.
 */

/**
 * @defgroup grp_signal Signals
 * @brief SPN descriptors, bit access, scaling and J1939/71 value ranges.
 */

/**
 * @defgroup grp_diag Diagnostics
 * @brief J1939/73 diagnostics of a Controller Application: DM1, DM2, DM3 and
 * DM11.
 */

/**
 * @defgroup grp_diag_codec Diagnostic message codec
 * @ingroup grp_diag
 * @brief DTCs, lamp status and the DM1/DM2 payload.
 */

/**
 * @defgroup grp_codecs Identifier and NAME codecs
 * @brief Pure codecs of the 29-bit identifier, the PGN and the NAME.
 */

/**
 * @defgroup grp_id Identifier and PGN
 * @ingroup grp_codecs
 * @brief J1939/21 29-bit identifier and PGN codec.
 */

/**
 * @defgroup grp_name NAME
 * @ingroup grp_codecs
 * @brief J1939/81 NAME codec.
 */

/**
 * @defgroup grp_config Configuration
 * @brief Compile-time configuration.
 */

/**
 * @defgroup grp_port Port contract
 * @brief The API a port provides in its j1939_target.h.
 */

/**
 * @defgroup grp_queue Frame queue
 * @brief Optional CAN frame queue between two execution contexts.
 */

/**
 * @defgroup grp_version Version
 * @brief Version of the headers and of the compiled library.
 */

#ifndef J1939_H
#define J1939_H

#include <stdint.h>

#include "j1939/j1939_addr.h"
#include "j1939/j1939_config.h"
#include "j1939/j1939_diag.h"
#include "j1939/j1939_dm.h"
#include "j1939/j1939_id.h"
#include "j1939/j1939_msg.h"
#include "j1939/j1939_name.h"
#include "j1939/j1939_request.h"
#include "j1939/j1939_ret.h"
#include "j1939/j1939_rxobj.h"
#include "j1939/j1939_signal.h"
#include "j1939/j1939_stack.h"
#include "j1939/j1939_tp.h"
#include "j1939/j1939_txobj.h"

/**
 * @addtogroup grp_version
 * @{
 */

#define J1939_VERSION_MAJOR 0 /**< Major version: incompatible API changes. */
#define J1939_VERSION_MINOR 1 /**< Minor version: compatible additions. */
#define J1939_VERSION_PATCH 0 /**< Patch version: compatible fixes. */

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

/** @} */

#endif /* J1939_H */
