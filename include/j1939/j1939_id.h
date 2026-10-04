/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_id.h
 * @brief J1939/21 29-bit identifier and PGN codec.
 */

#ifndef J1939_ID_H
#define J1939_ID_H

#include <stdbool.h>
#include <stdint.h>

#include "j1939/j1939_ret.h"

/**
 * @addtogroup grp_id
 *
 * Identifier layout (bit 28 is the most significant):
 *
 * | Bits  | Field                     |
 * | ----- | ------------------------- |
 * | 28-26 | Priority                  |
 * | 25    | Extended data page (EDP)  |
 * | 24    | Data page (DP)            |
 * | 23-16 | PDU format (PF)           |
 * | 15-8  | PDU specific (PS)         |
 * | 7-0   | Source address (SA)       |
 *
 * The PGN is the 18-bit field EDP, DP, PF, PS. For PDU1 formats (PF < 240)
 * PS carries the destination address and the PGN's lowest byte is 0. For
 * PDU2 formats (PF >= 240) PS is the group extension and the message is
 * always sent to all nodes.
 *
 * All functions are pure.
 *
 * @{
 */

#define J1939_ID_MASK      0x1FFFFFFFU /**< Valid bits of a 29-bit identifier. */
#define J1939_PGN_MAX      0x3FFFFU    /**< Largest 18-bit PGN. */
#define J1939_PGN_EDP      0x20000U /**< Extended data page bit; set PGNs are not J1939 messages. */
#define J1939_PRIO_MAX     7U       /**< Lowest priority. */
#define J1939_PRIO_DEFAULT 6U       /**< Default priority of most messages. */
#define J1939_PF_PDU2_MIN  240U     /**< First PDU2 PDU format. */
#define J1939_ADDR_NULL    0xFEU    /**< Null address, used before a successful claim. */
#define J1939_ADDR_GLOBAL  0xFFU    /**< Global (broadcast) destination address. */

/** @return Priority, 0 (highest) .. 7. */
uint8_t j1939_id_prio_get(uint32_t id);

/** @return PGN with the destination address removed for PDU1 formats. */
uint32_t j1939_id_pgn_get(uint32_t id);

/** @return Destination address; J1939_ADDR_GLOBAL for PDU2 formats. */
uint8_t j1939_id_da_get(uint32_t id);

/** @return Source address. */
uint8_t j1939_id_sa_get(uint32_t id);

/** @return true if @p pgn uses the PDU1 (destination specific) format. */
bool j1939_pgn_is_pdu1(uint32_t pgn);

/**
 * @brief Builds a 29-bit identifier.
 *
 * @param prio  Priority, 0..7.
 * @param pgn   PGN, 0..J1939_PGN_MAX. For PDU1 formats the lowest byte must be 0.
 * @param da    Destination address for PDU1 formats. Must be J1939_ADDR_GLOBAL
 *              for PDU2 formats.
 * @param sa    Source address.
 * @param id    Built identifier. Written only on success.
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG if an argument is out of range or
 *         @p id is NULL.
 */
j1939_ret_t j1939_id_build(uint8_t prio, uint32_t pgn, uint8_t da, uint8_t sa, uint32_t *id);

/** @} */

#endif /* J1939_ID_H */
