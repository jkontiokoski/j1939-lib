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

# Optional directory for machine-readable reports: JUnit results of test and coverage, the
# coverage report as text and HTML. Unset, the targets only print their results.
REPORT_DIR ?=
REPORT_ABS  = $(abspath $(REPORT_DIR))

EXAMPLES := addr_claim_demo pgn_listener bam_sender

FORMAT_FILES = $(shell find include src tests port examples \
	-path tests/vendor -prune -o -type f \( -name '*.c' -o -name '*.h' \) -print 2>/dev/null)
CPPCHECK_FLAGS = --std=c99 --enable=all --inconclusive --error-exitcode=1 --inline-suppr \
	--suppressions-list=cppcheck-suppressions.txt

# cppcheck reports, one per run of make lint.
LINT_DIR := $(BUILD_ROOT)/lint
# A reported finding: <file>:<line>:<column>: <severity>: ...
LINT_FINDING := ^[^ ]+:[0-9]+:[0-9]+: (error|warning|style|performance|portability|information):
# Markdown files whose relative links are checked; files left out of the source archive are
# checked only where they exist.
LINK_FILES = $(wildcard README.md CONTRIBUTING.md RELEASING.md docs/*.md docs/guides/*.md)

# $(call cppcheck_run,<report name>,<arguments>): runs cppcheck, keeps its output in
# $(LINT_DIR)/<report name>.txt and fails on its exit code or on any reported finding. cppcheck
# 2.13 leaves the exit code at 0 for findings of whole-program checks such as MISRA rule 5.9, so
# the report is checked as well.
define cppcheck_run
	cppcheck $(CPPCHECK_FLAGS) $(2) > $(LINT_DIR)/$(1).txt 2>&1; \
		status=$$?; cat $(LINT_DIR)/$(1).txt; \
		if [ $$status -ne 0 ] || grep -Eq '$(LINT_FINDING)' $(LINT_DIR)/$(1).txt; then exit 1; fi
endef

.PHONY: all lib test coverage cross examples docs docs-internal check dist format format-check lint \
	clean

all: lib

lib:
	$(CMAKE) --preset dev
	$(CMAKE) --build --preset dev

test:
	$(CMAKE) --preset test
	$(CMAKE) --build --preset test
	$(if $(REPORT_DIR),mkdir -p $(REPORT_ABS))
	$(CTEST) --preset test $(if $(REPORT_DIR),--output-junit $(REPORT_ABS)/test-junit.xml)

coverage:
	$(CMAKE) --preset coverage
	$(CMAKE) --build --preset coverage
	$(if $(REPORT_DIR),mkdir -p $(REPORT_ABS)/coverage)
	$(CTEST) --preset coverage $(if $(REPORT_DIR),--output-junit $(REPORT_ABS)/coverage-junit.xml)
	gcovr --root . --object-directory $(COV_DIR) --filter src/ --filter include/ \
		--txt $(if $(REPORT_DIR),$(REPORT_ABS)/coverage.txt) --txt-summary \
		$(if $(REPORT_DIR),--html-details $(REPORT_ABS)/coverage/index.html) \
		--fail-under-line $(COVERAGE_MIN)

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

# Release artifacts of the current commit in build/dist/; see RELEASING.md.
dist:
	sh tools/dist.sh

format:
	clang-format -i $(FORMAT_FILES)

format-check:
	clang-format --dry-run --Werror $(FORMAT_FILES)

lint:
	@mkdir -p $(LINT_DIR)
	$(call cppcheck_run,core,--addon=misra --suppressions-list=cppcheck-misra-suppressions.txt \
		-I include -I port/mock -i port/mock/j1939_port_fixture.c src include port/mock)
	$(call cppcheck_run,socketcan, \
		-I include -I port/socketcan -i port/socketcan/j1939_port_fixture.c port/socketcan)
	$(call cppcheck_run,examples,-I include -I port/socketcan -I examples/common \
		examples/common examples/addr_claim_demo examples/pgn_listener examples/bam_sender)
	sh tools/check-links.sh $(LINK_FILES) > $(LINT_DIR)/links.txt 2>&1; \
		status=$$?; cat $(LINT_DIR)/links.txt; [ $$status -eq 0 ]

clean:
	rm -rf $(BUILD_ROOT)
