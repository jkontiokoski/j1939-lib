# SPDX-License-Identifier: MIT
# Copyright (c) 2026 jkontiokoski

# Convenience wrapper around the CMake presets of CMakePresets.json. The build itself is defined
# in CMakeLists.txt.

CMAKE ?= cmake
CTEST ?= ctest

# Build trees, one per configure preset of CMakePresets.json, all under build/.
BUILD_ROOT := build
COV_DIR    := $(BUILD_ROOT)/coverage

COVERAGE_MIN := 90

EXAMPLES := addr_claim_demo pgn_listener bam_sender

FORMAT_FILES = $(shell find include src tests port examples \
	-path tests/vendor -prune -o -type f \( -name '*.c' -o -name '*.h' \) -print 2>/dev/null)
CPPCHECK_FLAGS = --std=c99 --enable=all --inconclusive --error-exitcode=1 --inline-suppr \
	--suppressions-list=cppcheck-suppressions.txt

.PHONY: all lib test coverage cross examples docs docs-internal check format format-check lint clean

all: lib

lib:
	$(CMAKE) --preset dev
	$(CMAKE) --build --preset dev

test:
	$(CMAKE) --preset test
	$(CMAKE) --build --preset test
	$(CTEST) --preset test

coverage:
	$(CMAKE) --preset coverage
	$(CMAKE) --build --preset coverage
	$(CTEST) --preset coverage
	gcovr --root . --object-directory $(COV_DIR) --filter src/ --filter include/ \
		--txt --txt-summary --fail-under-line $(COVERAGE_MIN)

cross:
	$(CMAKE) --preset arm
	$(CMAKE) --build --preset arm

examples:
	$(CMAKE) --preset dev
	$(CMAKE) --build --preset dev --target $(EXAMPLES)

docs:
	$(CMAKE) --preset dev -DJ1939_BUILD_DOCS=ON
	$(CMAKE) --build --preset dev --target docs

docs-internal:
	$(CMAKE) --preset dev -DJ1939_BUILD_DOCS=ON
	$(CMAKE) --build --preset dev --target docs-internal

# Every quality gate, one after the other; stops at the first failure.
check:
	$(MAKE) format-check
	$(MAKE) lint
	$(MAKE) test
	$(MAKE) coverage
	$(MAKE) cross
	$(MAKE) docs
	$(MAKE) docs-internal

format:
	clang-format -i $(FORMAT_FILES)

format-check:
	clang-format --dry-run --Werror $(FORMAT_FILES)

lint:
	cppcheck $(CPPCHECK_FLAGS) --addon=misra --suppressions-list=cppcheck-misra-suppressions.txt \
		-I include -I port/mock -i port/mock/j1939_port_fixture.c src include port/mock
	cppcheck $(CPPCHECK_FLAGS) \
		-I include -I port/socketcan -i port/socketcan/j1939_port_fixture.c port/socketcan
	cppcheck $(CPPCHECK_FLAGS) -I include -I port/socketcan -I examples/common \
		examples/common examples/addr_claim_demo examples/pgn_listener examples/bam_sender

clean:
	rm -rf $(BUILD_ROOT)
