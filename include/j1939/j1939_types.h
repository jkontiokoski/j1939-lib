/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_types.h
 * @brief Types every module shares: return codes, the logical message, the stack handle.
 *
 * The module headers include only this file. j1939_stack.h, which defines the stack instance,
 * includes the module headers.
 */

#ifndef J1939_TYPES_H
#define J1939_TYPES_H

#include <stdint.h>

/**
 * @addtogroup grp_stack
 * @{
 */

/** Result of a library operation. */
typedef enum j1939_ret {
	J1939_RET_OK = 0,         /**< Operation succeeded. */
	J1939_RET_ERR_ARG,        /**< Invalid argument. */
	J1939_RET_ERR_STATE,      /**< Operation not allowed in the current state. */
	J1939_RET_ERR_FULL,       /**< Queue or pool has no free entry. */
	J1939_RET_ERR_EMPTY,      /**< Queue has no entry. */
	J1939_RET_ERR_BUSY,       /**< Resource temporarily in use, retry later. */
	J1939_RET_ERR_TIMEOUT,    /**< Protocol timeout expired. */
	J1939_RET_ERR_NO_ADDRESS, /**< No source address claimed. */
	J1939_RET_ERR_IO,         /**< CAN driver reported an error (port helpers). */
} j1939_ret_t;

#define J1939_MSG_SINGLE_FRAME_MAX 8U /**< Largest payload sent in one CAN frame. */

/** A J1939 message, independent of how it travels on the bus. */
typedef struct j1939_msg {
	uint32_t pgn;        /**< Parameter group number. */
	uint8_t prio;        /**< Priority, 0 (highest) .. 7. */
	uint8_t sa;          /**< Source address. Ignored when sending: the CA's address is used. */
	uint8_t da;          /**< Destination address; J1939_ADDR_GLOBAL for PDU2 in one frame. */
	uint16_t len;        /**< Payload length in bytes. */
	const uint8_t *data; /**< Payload; may be NULL when len is 0. */
} j1939_msg_t;

/** Stack instance: one per CAN bus. Defined in j1939_stack.h; members are private. */
typedef struct j1939 j1939_t;

/** Handle of a Controller Application within its stack. */
typedef uint8_t j1939_ca_id_t;

/**
 * Ring indices of a queue with one producer and one consumer. Public only so that the objects
 * that embed it can be allocated; members are private.
 */
typedef struct j1939_ring {
	uint16_t len;   /**< Number of slots. */
	uint16_t head;  /**< Next slot to be written. */
	uint16_t tail;  /**< Oldest written slot. */
	uint16_t count; /**< Number of written slots. */
} j1939_ring_t;

/** @} */

#endif /* J1939_TYPES_H */
