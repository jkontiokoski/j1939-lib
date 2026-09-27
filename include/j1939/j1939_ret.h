/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_ret.h
 * @brief Return codes shared by all library functions.
 */

#ifndef J1939_RET_H
#define J1939_RET_H

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

#endif /* J1939_RET_H */
