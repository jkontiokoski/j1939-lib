# SPDX-License-Identifier: MIT
# Copyright (c) 2026 jkontiokoski

set(J1939_CORE_SOURCES
	"${PROJECT_SOURCE_DIR}/src/j1939_id.c"
	"${PROJECT_SOURCE_DIR}/src/j1939_queue.c"
	"${PROJECT_SOURCE_DIR}/src/j1939_ring.c"
	"${PROJECT_SOURCE_DIR}/src/j1939_request.c"
	"${PROJECT_SOURCE_DIR}/src/j1939_stack.c"
	"${PROJECT_SOURCE_DIR}/src/j1939_version.c"
)

# j1939_add_library(<target> <port_dir>)
#
# Builds the library against the j1939_target.h in <port_dir>. If the port
# directory contains a port.cmake, it may set:
#   J1939_PORT_SOURCES         port helper sources compiled into the library
#   J1939_PORT_LINK_LIBRARIES  libraries the port needs
#   J1939_PORT_TEST_FIXTURE    source implementing tests/port/j1939_port_fixture.h
# The fixture path is stored in the target property J1939_PORT_TEST_FIXTURE.
function(j1939_add_library target port_dir)
	if(NOT EXISTS "${port_dir}/j1939_target.h")
		message(FATAL_ERROR "j1939: no j1939_target.h in port directory '${port_dir}'")
	endif()

	set(J1939_PORT_SOURCES "")
	set(J1939_PORT_LINK_LIBRARIES "")
	set(J1939_PORT_TEST_FIXTURE "")
	include("${port_dir}/port.cmake" OPTIONAL)

	add_library(${target} STATIC ${J1939_CORE_SOURCES} ${J1939_PORT_SOURCES})
	set_target_properties(${target} PROPERTIES
		C_STANDARD 99
		C_STANDARD_REQUIRED ON
		C_EXTENSIONS OFF
		J1939_PORT_TEST_FIXTURE "${J1939_PORT_TEST_FIXTURE}"
	)
	target_include_directories(${target} PUBLIC "${PROJECT_SOURCE_DIR}/include" "${port_dir}")
	target_link_libraries(${target} PUBLIC ${J1939_PORT_LINK_LIBRARIES})
	j1939_set_warnings(${target})
endfunction()
