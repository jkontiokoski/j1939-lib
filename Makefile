# SPDX-License-Identifier: MIT
# Copyright (c) 2026 jkontiokoski

# Convenience wrapper around CMake. The build itself is defined in CMakeLists.txt.

CMAKE ?= cmake
CTEST ?= ctest

BUILD_DIR := build
TEST_DIR  := build-test
COV_DIR   := build-coverage
ARM_DIR   := build-arm

COVERAGE_MIN := 90

FORMAT_FILES = $(shell find include src tests port examples \
	-path tests/vendor -prune -o -type f \( -name '*.c' -o -name '*.h' \) -print 2>/dev/null)
CPPCHECK_FLAGS = --std=c99 --enable=all --inconclusive --error-exitcode=1 --inline-suppr \
	--suppressions-list=cppcheck-suppressions.txt

.PHONY: all lib test coverage cross format format-check lint clean

all: lib

lib:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug
	$(CMAKE) --build $(BUILD_DIR)

test:
	$(CMAKE) -S . -B $(TEST_DIR) -DCMAKE_BUILD_TYPE=Debug -DJ1939_BUILD_TESTS=ON -DJ1939_SANITIZE=ON
	$(CMAKE) --build $(TEST_DIR)
	$(CTEST) --test-dir $(TEST_DIR) --output-on-failure

coverage:
	$(CMAKE) -S . -B $(COV_DIR) -DCMAKE_BUILD_TYPE=Debug -DJ1939_BUILD_TESTS=ON -DJ1939_COVERAGE=ON
	$(CMAKE) --build $(COV_DIR)
	$(CTEST) --test-dir $(COV_DIR) --output-on-failure
	gcovr --root . --object-directory $(COV_DIR) --filter src/ --filter include/ \
		--txt --txt-summary --fail-under-line $(COVERAGE_MIN)

cross:
	$(CMAKE) -S . -B $(ARM_DIR) --toolchain cmake/arm-none-eabi.cmake -DJ1939_BUILD_TESTS=OFF
	$(CMAKE) --build $(ARM_DIR)

format:
	clang-format -i $(FORMAT_FILES)

format-check:
	clang-format --dry-run --Werror $(FORMAT_FILES)

lint:
	cppcheck $(CPPCHECK_FLAGS) --addon=misra --suppressions-list=cppcheck-misra-suppressions.txt \
		-I include -I port/mock -i port/mock/j1939_port_fixture.c src include port/mock
	cppcheck $(CPPCHECK_FLAGS) \
		-I include -I port/socketcan -i port/socketcan/j1939_port_fixture.c port/socketcan

clean:
	rm -rf $(BUILD_DIR) $(TEST_DIR) $(COV_DIR) $(ARM_DIR)
