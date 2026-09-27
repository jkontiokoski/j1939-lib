# SPDX-License-Identifier: MIT
# Copyright (c) 2026 jkontiokoski

# Example applications on Linux SocketCAN. They link a SocketCAN build of the
# library, whatever port J1939_PORT_DIR selects for the main j1939 target.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
	message(FATAL_ERROR "j1939: the examples run on Linux SocketCAN; "
		"configure with -DJ1939_BUILD_EXAMPLES=OFF on ${CMAKE_SYSTEM_NAME}")
endif()

set(_socketcan_port_dir "${PROJECT_SOURCE_DIR}/port/socketcan")
if(J1939_PORT_DIR STREQUAL _socketcan_port_dir)
	set(_example_lib j1939)
else()
	j1939_add_library(j1939_example_socketcan "${_socketcan_port_dir}")
	set(_example_lib j1939_example_socketcan)
endif()

add_library(example_common STATIC common/example_common.c)
set_target_properties(example_common PROPERTIES C_STANDARD 99 C_EXTENSIONS OFF)
target_include_directories(example_common PUBLIC common)
target_link_libraries(example_common PUBLIC ${_example_lib})
j1939_set_warnings(example_common)

foreach(_example addr_claim_demo pgn_listener)
	add_executable(${_example} ${_example}/${_example}.c)
	set_target_properties(${_example} PROPERTIES C_STANDARD 99 C_EXTENSIONS OFF)
	target_link_libraries(${_example} PRIVATE example_common)
	j1939_set_warnings(${_example})
endforeach()
