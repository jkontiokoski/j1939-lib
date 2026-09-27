/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

#define _DEFAULT_SOURCE /* SOCK_NONBLOCK */

#include "j1939_socketcan.h"

#include <errno.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

j1939_ret_t j1939_socketcan_open(const char *ifname, int *fd) {
	j1939_ret_t ret = J1939_RET_ERR_IO;
	struct sockaddr_can addr;
	/* Extended data frames only. */
	const struct can_filter filter = {
	        .can_id = CAN_EFF_FLAG,
	        .can_mask = CAN_EFF_FLAG | CAN_RTR_FLAG,
	};

	if ((ifname == NULL) || (fd == NULL)) {
		ret = J1939_RET_ERR_ARG;
	} else {
		unsigned int ifindex = if_nametoindex(ifname);
		int s = socket(PF_CAN, SOCK_RAW | SOCK_NONBLOCK, CAN_RAW);
		if ((ifindex != 0U) && (s >= 0)) {
			(void)memset(&addr, 0, sizeof(addr));
			addr.can_family = AF_CAN;
			addr.can_ifindex = (int)ifindex;
			if ((setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, &filter, sizeof(filter)) ==
			     0) &&
			    (bind(s, (struct sockaddr *)&addr, sizeof(addr)) == 0)) {
				*fd = s;
				ret = J1939_RET_OK;
			}
		}
		if ((ret != J1939_RET_OK) && (s >= 0)) {
			(void)close(s);
		}
	}
	return ret;
}

void j1939_socketcan_close(int fd) {
	(void)close(fd);
}

j1939_ret_t j1939_socketcan_rx(int fd, j1939_queue_t *q, uint16_t *n_rx) {
	j1939_ret_t ret = J1939_RET_OK;
	uint16_t n = 0U;
	bool done = false;

	if (q == NULL) {
		ret = J1939_RET_ERR_ARG;
		done = true;
	}
	/* Bounded by the queue length: every iteration either fills a slot or stops. */
	while (!done) {
		j1939_port_frame_t *slot = j1939_queue_acquire(q);

		if (slot == NULL) {
			done = true;
		} else {
			ssize_t r = read(fd, slot, sizeof(*slot));
			if (r == (ssize_t)sizeof(*slot)) {
				(void)j1939_queue_commit(q);
				n++;
			} else {
				if ((r >= 0) || ((errno != EAGAIN) && (errno != EWOULDBLOCK))) {
					ret = J1939_RET_ERR_IO;
				}
				done = true;
			}
		}
	}
	if (n_rx != NULL) {
		*n_rx = n;
	}
	return ret;
}

j1939_ret_t j1939_socketcan_tx(int fd, j1939_queue_t *q, uint16_t *n_tx) {
	j1939_ret_t ret = J1939_RET_OK;
	uint16_t n = 0U;
	bool done = false;

	if (q == NULL) {
		ret = J1939_RET_ERR_ARG;
		done = true;
	}
	/* Bounded by the queue length: every iteration either empties a slot or stops. */
	while (!done) {
		const j1939_port_frame_t *frame = j1939_queue_peek(q);

		if (frame == NULL) {
			done = true;
		} else {
			ssize_t r = write(fd, frame, sizeof(*frame));
			if (r == (ssize_t)sizeof(*frame)) {
				(void)j1939_queue_pop(q);
				n++;
			} else {
				/* ENOBUFS: the interface transmit queue is full. */
				if ((r >= 0) || ((errno != EAGAIN) && (errno != EWOULDBLOCK) &&
				                 (errno != ENOBUFS))) {
					ret = J1939_RET_ERR_IO;
				}
				done = true;
			}
		}
	}
	if (n_tx != NULL) {
		*n_tx = n;
	}
	return ret;
}
