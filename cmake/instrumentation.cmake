# SPDX-License-Identifier: MIT
# Copyright (c) 2026 jkontiokoski

# Instrumentation applies to every target in the build, including test code.

if(J1939_SANITIZE)
	add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
	add_link_options(-fsanitize=address,undefined)
endif()

if(J1939_COVERAGE)
	add_compile_options(--coverage -O0 -g)
	add_link_options(--coverage)
endif()
