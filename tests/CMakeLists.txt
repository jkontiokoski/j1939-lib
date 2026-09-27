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
set(SOCKETCAN_PORT_DIR "${PROJECT_SOURCE_DIR}/port/socketcan")

# Unit and integration tests run against the mock port, with the test configuration.
j1939_add_library(j1939_test_mock "${MOCK_PORT_DIR}")
target_include_directories(j1939_test_mock PUBLIC config)
target_compile_definitions(j1939_test_mock PUBLIC J1939_CONFIG_FILE="j1939_test_config.h")

add_library(test_support STATIC support/test_bus.c "${MOCK_PORT_DIR}/j1939_port_fixture.c")
set_target_properties(test_support PROPERTIES C_STANDARD 99 C_EXTENSIONS OFF)
target_include_directories(test_support PUBLIC support port)
target_link_libraries(test_support PUBLIC j1939_test_mock unity)
j1939_set_warnings(test_support)

j1939_add_test(test_version test_support unit/test_version.c)
j1939_add_test(test_id test_support unit/test_id.c)
j1939_add_test(test_name test_support unit/test_name.c)
j1939_add_test(test_queue test_support unit/test_queue.c)
j1939_add_test(test_stack test_support unit/test_stack.c)
j1939_add_test(test_request test_support unit/test_request.c)
j1939_add_test(test_diag test_support unit/test_diag.c)
j1939_add_test(test_signal test_support unit/test_signal.c)
j1939_add_test(test_signal_example test_support unit/test_signal_example.c
	"${PROJECT_SOURCE_DIR}/examples/signals/example_signals.c")
target_include_directories(test_signal_example PRIVATE "${PROJECT_SOURCE_DIR}/examples/signals")
j1939_add_test(test_addr test_support unit/test_addr.c)
j1939_add_test(test_request_exchange test_support integration/test_request_exchange.c)
j1939_add_test(test_addr_claim test_support integration/test_addr_claim.c)

# Port conformance: the bundled ports, plus the configured port if it is another one.
j1939_add_port_conformance(test_port_mock j1939_test_mock)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	j1939_add_library(j1939_test_socketcan "${SOCKETCAN_PORT_DIR}")
	j1939_add_port_conformance(test_port_socketcan j1939_test_socketcan)
	j1939_add_test(test_socketcan_vcan j1939_test_socketcan port/test_socketcan_vcan.c)
	set_tests_properties(test_socketcan_vcan PROPERTIES SKIP_RETURN_CODE 77)
endif()

if(NOT J1939_PORT_DIR STREQUAL MOCK_PORT_DIR AND NOT J1939_PORT_DIR STREQUAL SOCKETCAN_PORT_DIR)
	j1939_add_port_conformance(test_port_configured j1939)
endif()
