/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_socketcan.h
 * @brief Moves frames between a CAN_RAW socket and library queues.
 *
 * The socket is non-blocking and only receives extended data frames. Wait
 * for readability with poll() or select() on the descriptor if needed.
 */

#ifndef J1939_SOCKETCAN_H
#define J1939_SOCKETCAN_H

#include <stdint.h>

#include "j1939/j1939_queue.h"
#include "j1939/j1939_ret.h"

/**
 * @brief Opens a non-blocking CAN_RAW socket bound to @p ifname.
 *
 * @param ifname  Interface name, e.g. "can0" or "vcan0".
 * @param fd      Socket descriptor. Written only on success.
 * @return J1939_RET_OK, J1939_RET_ERR_ARG on a NULL pointer, or J1939_RET_ERR_IO.
 */
j1939_ret_t j1939_socketcan_open(const char *ifname, int *fd);

/** @brief Closes a socket opened with j1939_socketcan_open(). */
void j1939_socketcan_close(int fd);

/**
 * @brief Reads pending frames from the socket into @p q.
 *
 * Reading stops when the socket has no more frames or @p q is full. Frames
 * that do not fit remain in the socket.
 *
 * @param fd    Socket descriptor.
 * @param q     Receive queue.
 * @param n_rx  Number of frames read. May be NULL.
 * @return J1939_RET_OK, J1939_RET_ERR_ARG on a NULL queue, or J1939_RET_ERR_IO.
 */
j1939_ret_t j1939_socketcan_rx(int fd, j1939_queue_t *q, uint16_t *n_rx);

/**
 * @brief Writes queued frames from @p q to the socket.
 *
 * Writing stops when @p q is empty or the socket cannot take more frames.
 * Frames not written remain in @p q.
 *
 * @param fd    Socket descriptor.
 * @param q     Transmit queue.
 * @param n_tx  Number of frames written. May be NULL.
 * @return J1939_RET_OK, J1939_RET_ERR_ARG on a NULL queue, or J1939_RET_ERR_IO.
 */
j1939_ret_t j1939_socketcan_tx(int fd, j1939_queue_t *q, uint16_t *n_tx);

#endif /* J1939_SOCKETCAN_H */
