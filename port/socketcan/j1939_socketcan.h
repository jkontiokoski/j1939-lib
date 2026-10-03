/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

/**
 * @file j1939_socketcan.h
 * @brief Moves frames between a CAN_RAW socket and a stack.
 *
 * The socket is non-blocking and only receives extended data frames. Wait
 * for readability with poll() or select() on the descriptor if needed.
 */

#ifndef J1939_SOCKETCAN_H
#define J1939_SOCKETCAN_H

#include <stdint.h>

#include "j1939/j1939_ret.h"
#include "j1939/j1939_stack.h"

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
 * @brief Reads pending frames from the socket and passes each to j1939_rx().
 *
 * Reading stops when the socket has no more frames or @p max frames have
 * been read. Further frames remain in the socket for the next call.
 *
 * @param fd    Socket descriptor.
 * @param s     Stack.
 * @param max   Most frames to read in this call.
 * @param n_rx  Number of frames read. May be NULL.
 * @return J1939_RET_OK, J1939_RET_ERR_ARG on a NULL stack, or J1939_RET_ERR_IO.
 */
j1939_ret_t j1939_socketcan_rx(int fd, j1939_t *s, uint16_t max, uint16_t *n_rx);

/**
 * @brief Writes the stack's tx queue to the socket.
 *
 * Writing stops when the tx queue is empty or the socket cannot take more
 * frames. Frames not written remain in the tx queue.
 *
 * @param fd    Socket descriptor.
 * @param s     Stack.
 * @param n_tx  Number of frames written. May be NULL.
 * @return J1939_RET_OK, J1939_RET_ERR_ARG on a NULL stack, or J1939_RET_ERR_IO.
 */
j1939_ret_t j1939_socketcan_tx(int fd, j1939_t *s, uint16_t *n_tx);

#endif /* J1939_SOCKETCAN_H */
