# SPDX-License-Identifier: MIT
# Copyright (c) 2026 jkontiokoski

# Linux SocketCAN port: socket helpers and pthread locks.
find_package(Threads REQUIRED)

set(J1939_PORT_SOURCES "${CMAKE_CURRENT_LIST_DIR}/j1939_socketcan.c")
set(J1939_PORT_LINK_LIBRARIES Threads::Threads)
set(J1939_PORT_TEST_FIXTURE "${CMAKE_CURRENT_LIST_DIR}/j1939_port_fixture.c")
