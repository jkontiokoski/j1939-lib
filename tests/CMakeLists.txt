# SPDX-License-Identifier: MIT
# Copyright (c) 2026 jkontiokoski

# Vendored third-party code is built without the project's warning set.
add_library(unity STATIC vendor/unity/unity.c)
target_include_directories(unity PUBLIC vendor/unity)
set_target_properties(unity PROPERTIES C_STANDARD 99 C_EXTENSIONS OFF)

# j1939_add_test(<name> <sources>...)
function(j1939_add_test name)
	add_executable(${name} ${ARGN})
	set_target_properties(${name} PROPERTIES C_STANDARD 99 C_EXTENSIONS OFF)
	target_link_libraries(${name} PRIVATE j1939 unity)
	j1939_set_warnings(${name})
	add_test(NAME ${name} COMMAND ${name})
endfunction()

j1939_add_test(test_version unit/test_version.c)
j1939_add_test(test_id unit/test_id.c)
