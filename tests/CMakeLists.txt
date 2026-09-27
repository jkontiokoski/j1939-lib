# SPDX-License-Identifier: MIT
# Copyright (c) 2026 jkontiokoski

# Vendored third-party code is built without the project's warning set.
add_library(unity STATIC vendor/unity/unity.c)
target_include_directories(unity PUBLIC vendor/unity)
set_target_properties(unity PROPERTIES C_STANDARD 99 C_EXTENSIONS OFF)

# j1939_add_test(<name> <library> <sources>...)
function(j1939_add_test name library)
	add_executable(${name} ${ARGN})
	set_target_properties(${name} PROPERTIES C_STANDARD 99 C_EXTENSIONS OFF)
	target_link_libraries(${name} PRIVATE ${library} unity)
	j1939_set_warnings(${name})
	add_test(NAME ${name} COMMAND ${name})
endfunction()

# j1939_add_port_conformance(<name> <library>)
function(j1939_add_port_conformance name library)
	get_target_property(fixture ${library} J1939_PORT_TEST_FIXTURE)
	if(fixture)
		j1939_add_test(${name} ${library} port/test_port_conformance.c "${fixture}")
		target_include_directories(${name} PRIVATE port)
	else()
		message(WARNING "j1939: port of ${library} has no test fixture, conformance test skipped")
	endif()
endfunction()

set(MOCK_PORT_DIR "${PROJECT_SOURCE_DIR}/port/mock")

# Unit tests always run against the mock port.
j1939_add_library(j1939_test_mock "${MOCK_PORT_DIR}")
j1939_add_test(test_version j1939_test_mock unit/test_version.c)
j1939_add_test(test_id j1939_test_mock unit/test_id.c)
j1939_add_test(test_queue j1939_test_mock unit/test_queue.c)

# Port conformance: the mock port, plus the configured port if it is another one.
j1939_add_port_conformance(test_port_mock j1939_test_mock)

if(NOT J1939_PORT_DIR STREQUAL MOCK_PORT_DIR)
	j1939_add_port_conformance(test_port_configured j1939)
endif()
