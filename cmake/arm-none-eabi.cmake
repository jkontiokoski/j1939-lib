# SPDX-License-Identifier: MIT
# Copyright (c) 2026 jkontiokoski

# Compile-only portability check for a bare-metal Cortex-M0+ target.
# Usage: cmake --preset arm (see CMakePresets.json), or
#        cmake -B build/arm --toolchain cmake/arm-none-eabi.cmake -DJ1939_BUILD_TESTS=OFF

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_AR arm-none-eabi-ar)
set(CMAKE_RANLIB arm-none-eabi-ranlib)

set(CMAKE_C_FLAGS_INIT "-mcpu=cortex-m0plus -mthumb")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
