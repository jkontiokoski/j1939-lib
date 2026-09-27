# SPDX-License-Identifier: MIT
# Copyright (c) 2026 jkontiokoski

# Applies the project's warning set to a target.
function(j1939_set_warnings target)
	if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
		target_compile_options(${target} PRIVATE
			-Wall
			-Wextra
			-pedantic
			-Wconversion
			-Wsign-conversion
			-Wshadow
			-Wstrict-prototypes
			-Wmissing-prototypes
			-Wcast-align
			-Wundef
		)
		if(J1939_WERROR)
			target_compile_options(${target} PRIVATE -Werror)
		endif()
	endif()
endfunction()
