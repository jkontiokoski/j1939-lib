# SPDX-License-Identifier: MIT
# Copyright (c) 2026 jkontiokoski

cmake_minimum_required(VERSION 3.21)

# The version is defined once, in j1939.h.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/j1939/j1939.h" _j1939_version_lines
	REGEX "^#define J1939_VERSION_(MAJOR|MINOR|PATCH) +[0-9]+$")
foreach(_part MAJOR MINOR PATCH)
	string(REGEX MATCH "J1939_VERSION_${_part} +([0-9]+)" _ "${_j1939_version_lines}")
	set(_j1939_version_${_part} "${CMAKE_MATCH_1}")
endforeach()

project(j1939
	VERSION ${_j1939_version_MAJOR}.${_j1939_version_MINOR}.${_j1939_version_PATCH}
	DESCRIPTION "Portable SAE J1939 protocol library"
	LANGUAGES C)

option(J1939_BUILD_TESTS "Build the test suite" ${PROJECT_IS_TOP_LEVEL})
option(J1939_WERROR "Treat compiler warnings as errors" ${PROJECT_IS_TOP_LEVEL})
option(J1939_SANITIZE "Instrument with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(J1939_COVERAGE "Instrument for gcov coverage" OFF)
option(J1939_COMPILE_COMMANDS "Generate compile_commands.json for tooling such as clangd"
	${PROJECT_IS_TOP_LEVEL})

if(J1939_COMPILE_COMMANDS)
	set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

include(cmake/warnings.cmake)
include(cmake/instrumentation.cmake)

add_library(j1939 STATIC
	src/j1939_version.c
)
add_library(j1939::j1939 ALIAS j1939)
set_target_properties(j1939 PROPERTIES
	C_STANDARD 99
	C_STANDARD_REQUIRED ON
	C_EXTENSIONS OFF
)
target_include_directories(j1939 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
j1939_set_warnings(j1939)

if(J1939_BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()
