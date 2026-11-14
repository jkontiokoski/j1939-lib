/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_config.h
 * @brief Compile-time configuration.
 */

#ifndef J1939_CONFIG_H
#define J1939_CONFIG_H

#ifdef J1939_CONFIG_FILE
#include J1939_CONFIG_FILE
#endif

/**
 * @addtogroup grp_config
 *
 * An integrator overrides any of the values below by defining them in a
 * header of their own and building with -DJ1939_CONFIG_FILE="my_cfg.h".
 * Values not defined there fall back to the defaults in this file.
 *
 * @{
 */

/**
 * Transport protocol (BAM and RTS/CTS): 1 builds it, 0 leaves it out. Without it, messages
 * are one frame at most (J1939_MAX_MSG_LEN is 8), j1939_cfg_t has no TP buffers and received
 * TP.CM and TP.DT frames are dropped.
 */
#ifndef J1939_TP_ENABLE
#define J1939_TP_ENABLE 1
#endif

/** Maximum number of Controller Applications per stack instance. */
#ifndef J1939_CFG_CA_MAX
#define J1939_CFG_CA_MAX 1
#endif

/** Number of concurrent transport protocol sessions per stack instance. */
#ifndef J1939_CFG_TP_SESSIONS
#define J1939_CFG_TP_SESSIONS 2
#endif

/** Largest message a transport protocol buffer holds, in bytes. */
#ifndef J1939_CFG_TP_BUF_SIZE
#define J1939_CFG_TP_BUF_SIZE 1785
#endif

/** Gap between the packets of a sent broadcast (BAM), in microseconds. */
#ifndef J1939_CFG_TP_BAM_GAP_US
#define J1939_CFG_TP_BAM_GAP_US 50000
#endif

#if (J1939_TP_ENABLE != 0) && (J1939_TP_ENABLE != 1)
#error "J1939_TP_ENABLE must be 0 or 1"
#endif

#if (J1939_CFG_CA_MAX < 1) || (J1939_CFG_CA_MAX > 253)
#error "J1939_CFG_CA_MAX must be in range 1..253"
#endif

#if J1939_TP_ENABLE
#if (J1939_CFG_TP_SESSIONS < 1) || (J1939_CFG_TP_SESSIONS > 32)
#error "J1939_CFG_TP_SESSIONS must be in range 1..32"
#endif

#if (J1939_CFG_TP_BUF_SIZE < 9) || (J1939_CFG_TP_BUF_SIZE > 1785)
#error "J1939_CFG_TP_BUF_SIZE must be in range 9..1785"
#endif

#if (J1939_CFG_TP_BAM_GAP_US < 50000) || (J1939_CFG_TP_BAM_GAP_US > 200000)
#error "J1939_CFG_TP_BAM_GAP_US must be in range 50000..200000"
#endif

/** Largest message payload the stack sends or receives: J1939_CFG_TP_BUF_SIZE with the
 * transport protocol, one frame (8 bytes) without it. */
#define J1939_MAX_MSG_LEN (J1939_CFG_TP_BUF_SIZE)

#else

#define J1939_MAX_MSG_LEN 8

#endif

/** @} */

#endif /* J1939_CONFIG_H */
