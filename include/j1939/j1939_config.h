/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_config.h
 * @brief Compile-time configuration.
 *
 * An integrator overrides any of the values below by defining them in a
 * header of their own and building with -DJ1939_CONFIG_FILE="my_cfg.h".
 * Values not defined there fall back to the defaults in this file.
 */

#ifndef J1939_CONFIG_H
#define J1939_CONFIG_H

#ifdef J1939_CONFIG_FILE
#include J1939_CONFIG_FILE
#endif

/** Maximum number of Controller Applications per stack instance. */
#ifndef J1939_CFG_CA_MAX
#define J1939_CFG_CA_MAX 1
#endif

/** Number of concurrent transport protocol sessions per stack instance. */
#ifndef J1939_CFG_TP_SESSIONS
#define J1939_CFG_TP_SESSIONS 2
#endif

#if (J1939_CFG_CA_MAX < 1) || (J1939_CFG_CA_MAX > 253)
#error "J1939_CFG_CA_MAX must be in range 1..253"
#endif

#if (J1939_CFG_TP_SESSIONS < 1) || (J1939_CFG_TP_SESSIONS > 32)
#error "J1939_CFG_TP_SESSIONS must be in range 1..32"
#endif

#endif /* J1939_CONFIG_H */
