/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_names.h
 * @brief NAME table: the NAMEs of the other nodes on the bus and their addresses.
 */

#ifndef J1939_NAMES_H
#define J1939_NAMES_H

#include <stdint.h>

#include "j1939/j1939_ret.h"
#include "j1939/j1939_stack.h"

/**
 * @addtogroup grp_names
 *
 * Normal traffic identifies a sender only by its source address; the NAME
 * appears only in Address Claimed. j1939_names_init() gives the stack a table
 * in which it records the NAME and the current address of every other node,
 * so that the application can tell who sent a message. The stack's own CAs
 * are never listed.
 *
 * - Every received Address Claimed updates the table after the stack's own
 *   arbitration, whatever its destination. If no CA of the stack holds the
 *   claimed address, the NAME gets that address, and an entry of another NAME
 *   that held it loses its address.
 * - Cannot Claim (Address Claimed from J1939_ADDR_NULL) keeps the NAME in the
 *   table with the address J1939_ADDR_NULL: the node is present but has no
 *   address.
 * - J1939/81 has no message for a node leaving the bus, so entries never
 *   expire; they change only through claims.
 * - A new NAME that finds the table full is not recorded and is counted in
 *   j1939_stats_t::names_dropped.
 * - Every change increments a counter, read with j1939_names_changes(), so
 *   that the application can poll for changes.
 *
 * The stack fills the table itself with Requests for Address Claimed (PGN
 * 60928):
 *
 * - Once after j1939_names_init(): a global Request, sent by the
 *   j1939_process() call in which a CA of the stack has claimed its address,
 *   from that CA. Every node answers it, so nodes that claimed before this
 *   one came online are listed too. The answers arrive within a few
 *   milliseconds; the receive path from the CAN driver must hold one frame
 *   per node on the bus.
 * - When j1939_names_name_get() finds no NAME at an address: a Request to that
 *   address, sent by the next j1939_process(), from a CA that has claimed its
 *   address, otherwise from J1939_ADDR_NULL. At most one such Request is
 *   pending, and at most one is sent per J1939_NAMES_REQUEST_HOLD_US; a later
 *   lookup finds the answer.
 *
 * A Request that finds the tx queue full is sent by a later j1939_process().
 *
 * @{
 */

/** Least time between two Requests for Address Claimed sent for unknown addresses. */
#define J1939_NAMES_REQUEST_HOLD_US 1000000U

/** Entry of the NAME table. Allocated by the integrator, members are private. */
typedef struct j1939_names_entry {
	uint32_t name_lo; /**< Bits 0..31 of the NAME. */
	uint32_t name_hi; /**< Bits 32..63 of the NAME. */
	uint8_t address;  /**< Address the NAME holds; J1939_ADDR_NULL if none. */
} j1939_names_entry_t;

/**
 * @brief Gives the stack its NAME table, or replaces it.
 *
 * The table starts empty, and the global Request for Address Claimed is sent
 * again as described above. @p len 0 removes the table, and so does
 * j1939_init(). The storage must outlive the stack.
 *
 * @param s    Stack.
 * @param buf  Storage for @p len entries. May be NULL if @p len is 0.
 * @param len  Number of entries: the most other nodes the table lists.
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG on a NULL stack or a NULL
 *         @p buf with @p len above 0.
 */
j1939_ret_t j1939_names_init(j1939_t *s, j1939_names_entry_t *buf, uint16_t len);

/**
 * @brief Reads the NAME of the node at an address.
 *
 * An address no node of the table holds makes the stack send a Request for
 * Address Claimed to it, see above. An address a CA of the stack holds is
 * not looked up.
 *
 * @param s        Stack.
 * @param address  Source address, 0..253.
 * @param name     NAME. Written only on success.
 * @return J1939_RET_OK;
 *         J1939_RET_ERR_EMPTY if no other node is known at @p address;
 *         J1939_RET_ERR_STATE if the stack has no NAME table;
 *         J1939_RET_ERR_ARG on a NULL pointer or an address above 253.
 */
j1939_ret_t j1939_names_name_get(j1939_t *s, uint8_t address, uint64_t *name);

/**
 * @brief Reads the address of a node.
 *
 * @param s        Stack.
 * @param name     NAME of the node.
 * @param address  Its address; J1939_ADDR_NULL if it sent Cannot Claim or lost
 *                 its address. Written only on success.
 * @return J1939_RET_OK;
 *         J1939_RET_ERR_EMPTY if the NAME is not in the table;
 *         J1939_RET_ERR_STATE if the stack has no NAME table;
 *         J1939_RET_ERR_ARG on a NULL pointer.
 */
j1939_ret_t j1939_names_address_get(const j1939_t *s, uint64_t name, uint8_t *address);

/**
 * @brief Number of nodes in the table.
 * @param s  Stack.
 * @return Entries in use; 0 without a table or if @p s is NULL.
 */
uint16_t j1939_names_count(const j1939_t *s);

/**
 * @brief Reads one entry of the table, to list all nodes.
 *
 * Entries keep their index until the table is set up again, so the
 * application can list them from 0 to j1939_names_count() - 1.
 *
 * @param s        Stack.
 * @param index    Index, 0 .. j1939_names_count() - 1.
 * @param name     NAME. Written only on success.
 * @param address  Its address, J1939_ADDR_NULL if none. Written only on success.
 * @return J1939_RET_OK, or J1939_RET_ERR_ARG on a NULL pointer or an index
 *         beyond the entries in use.
 */
j1939_ret_t j1939_names_at(const j1939_t *s, uint16_t index, uint64_t *name, uint8_t *address);

/**
 * @brief Change counter of the table.
 * @param s  Stack.
 * @return A counter incremented, with wrap-around, by every change of the
 *         table; 0 if @p s is NULL.
 */
uint16_t j1939_names_changes(const j1939_t *s);

/** @} */

#endif /* J1939_NAMES_H */
