# Contributing

How to work on the library: building and checking it, the coding and documentation rules, and how changes reach `main`.
This file is for contributors; it is not part of the released documentation. Releases are described in `RELEASING.md`.

## Build and check

Build configurations are CMake presets in `CMakePresets.json`; each has its own build tree under `build/`, because the toolchain and the instrumentation flags are fixed per tree.
The Makefile is a set of shortcuts over them.

| Target              | Action                                                                   |
| ------------------- | ------------------------------------------------------------------------ |
| `make`              | Builds the `dev` preset in `build/dev/`                                  |
| `make test`         | Builds the `test` preset (sanitizers) in `build/test/` and runs its tests |
| `make coverage`     | Builds the `coverage` preset in `build/coverage/`, runs the tests, fails under 90 % line coverage |
| `make cross`        | Builds the `arm` preset: the library and the frame queue for Cortex-M0+ in `build/arm/` |
| `make examples`     | Builds the SocketCAN example applications of the `dev` preset, in `build/dev/examples/` |
| `make docs`         | Generates the public documentation in `build/dev/docs/public/html/`, fails on any Doxygen warning |
| `make docs-internal` | Generates the internal documentation in `build/dev/docs/internal/html/`, fails on any Doxygen warning |
| `make lint`         | cppcheck (core and mock port with the MISRA addon, SocketCAN port and examples with the general checks) and the Markdown link check; reports in `build/lint/`, any finding fails |
| `make format`       | Formats all project sources                                              |
| `make format-check` | Fails if any project source is not formatted                             |
| `make check`        | Runs every gate in turn: `format-check`, `lint`, `test`, `coverage`, `cross`, `docs`, `docs-internal`; stops at the first failure |
| `make dist`         | Builds the release archives, see `RELEASING.md`                          |
| `make clean`        | Removes `build/`                                                         |

Run `make check` before opening a pull request.
The SocketCAN loopback test uses `vcan0`, or the interface in `J1939_TEST_CANIF`, and reports "skipped" when it does not exist.

| Preset     | Build tree       | Configuration                                                    |
| ---------- | ---------------- | ---------------------------------------------------------------- |
| `dev`      | `build/dev`      | Debug: library, tests, examples on Linux, documentation targets on request |
| `test`     | `build/test`     | Debug with AddressSanitizer and UndefinedBehaviorSanitizer, tests |
| `coverage` | `build/coverage` | Debug with gcov instrumentation, tests                           |
| `arm`      | `build/arm`      | Cortex-M0+ toolchain, library and frame queue only               |

Each preset also works directly with CMake: `cmake --preset test`, `cmake --build --preset test`, `ctest --preset test` (no test preset for `arm`).
`.clangd` points clangd at the compilation database of `build/dev`.

| CMake option        | Default               | Effect                                               |
| ------------------- | --------------------- | ---------------------------------------------------- |
| `J1939_PORT_DIR`    | `port/mock` when top level, required otherwise | Port directory; relative paths are resolved against the top-level source directory |
| `J1939_BUILD_TESTS` | ON when top level     | Builds the test suite                                |
| `J1939_WERROR`      | ON when top level     | Treats warnings as errors                            |
| `J1939_SANITIZE`    | OFF                   | AddressSanitizer + UndefinedBehaviorSanitizer        |
| `J1939_COVERAGE`    | OFF                   | gcov instrumentation                                 |
| `J1939_COMPILE_COMMANDS` | ON when top level | Writes `compile_commands.json` into the build directory |
| `J1939_BUILD_EXAMPLES` | ON when top level on a Linux host, OFF otherwise | Builds the example applications (Linux only) against a SocketCAN build of the library, whatever `J1939_PORT_DIR` selects |
| `J1939_BUILD_DOCS`  | OFF                   | Adds the `docs` and `docs-internal` targets; requires Doxygen ≥ 1.9.8 and Graphviz |

## Source tree

```
include/j1939/   public headers, the whole public API
src/             implementation (*.c) and private headers (*_priv.h)
port/<name>/     one directory per port: j1939_target.h, optional port.cmake and conformance fixture
port/mock/       test port with a deliberately unusual frame layout
port/socketcan/  Linux SocketCAN port and socket helpers
examples/        SocketCAN example applications; examples/signals/ holds an illustrative signal table
tests/           unit/, integration/, port/ (conformance), support/ (virtual bus), config/ (test builds), vendor/unity/
cmake/           library, warnings, instrumentation, toolchain and documentation helpers
docs/            user documentation, Doxyfile.in, vendored stylesheet; the page order is in cmake/docs.cmake
tools/           dist.sh (release archives), check-links.sh (Markdown link check)
```

The version is defined once, in `j1939.h`; `CMakeLists.txt` reads it from there.

## Code

- C99 (`-std=c99 -pedantic`), MISRA C:2012 inspired; deviations are listed in [Safety and quality](docs/safety.md) and in the cppcheck suppression lists. Fixed-width integer types throughout. Single exit point per function is preferred.
- Warnings: `-Wall -Wextra -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wcast-align -Wundef`.
- The design rules in [Concepts](docs/concepts.md) are binding: no function pointers, no dynamic memory, no recursion, bounded loops, explicit state machines with a `default`.
- Naming: public identifiers are prefixed `j1939_` (functions, types) or `J1939_` (macros); types end in `_t`; functions are prefixed by their module (`j1939_tp_*`, `j1939_addr_*`); the port API is prefixed `j1939_port_`. The public API lives only in `include/j1939/`.
- Static functions have names unique across the library (MISRA rule 5.9); a module prefix avoids collisions.
- Formatting: `.clang-format` (tabs, 100 columns, braces on the same line and on every control statement). Run `make format`.
- Every source file starts with an SPDX header:

  ```c
  /* SPDX-License-Identifier: MIT */
  /* Copyright (c) 2026 jkontiokoski */
  ```

## Static analysis

`make lint` runs cppcheck three times (core and mock port with the MISRA addon, the SocketCAN port, the examples), keeps each report in `build/lint/` and fails on any finding in it.
A new deviation goes into `cppcheck-suppressions.txt` or `cppcheck-misra-suppressions.txt` (comments with `//` only; an unmatched suppression fails the check) and into the deviation table of `docs/safety.md`.

## SAE material

- No J1939DA content (PGN and SPN definitions, parameter names, scaling, rates) and no text from the SAE standards in code, docs or tests. Protocol constants needed to work on the bus (protocol PGNs, codes, timers, bit layouts) are written in our own words.
- Examples and tests use invented identifiers: Proprietary B PGNs (0xFF00-0xFFFF) and SPNs from the proprietary range (520192-524287).
- Standards documents and DA exports never go into the repository, issues or pull requests.

## Documentation

- **One place per fact.** What a function or module does, its rules and limits, is documented in its header: that is the API reference. Pages in `docs/` explain how to do a task and why the design is as it is, and link to the reference instead of repeating it.
- **Topics.** Each module's description is a Doxygen topic. The groups are defined in reading order in `j1939.h`; each header adds its description and declarations with `@addtogroup` and closes the group before its include guard ends.
- **Doxygen gates.** Every declaration carries a Doxygen comment: public and internal functions, types, struct members and macros, and a `@file` block per source file. Both documentation builds fail on any warning:

  | Build    | Target               | Input                                         | Graphs                      |
  | -------- | -------------------- | --------------------------------------------- | --------------------------- |
  | Public   | `make docs`          | `include/j1939/`, `README.md`, the pages of `docs/` | include and dependency |
  | Internal | `make docs-internal` | public input and `src/`, with source browsing | also call and caller graphs |

  A `*_priv.h` function is documented at its declaration; Doxygen merges it with the definition. `WARN_NO_PARAMDOC` is off because Doxygen 1.9.8 reports documented parameters of declarations as missing; `WARN_IF_INCOMPLETE_DOC` checks parameters instead. Both builds fail on any warning.
- **Pages.** `_j1939_docs_pages` in `cmake/docs.cmake` lists the pages in reading order; a new page is added there and to the list in `README.md`. Each page serves one audience, opens with two or three sentences on what it covers and for whom, and stays short: a guide at most about 150 lines, no page over about 250. Reference data goes into tables, flows into numbered steps; no diagrams (ASCII art or Mermaid), which the generated documentation cannot render. Pages describe the current state only.
- **Links.** Pages link to each other with relative links, which work on GitHub and in the generated HTML. API names are written as code, `j1939_send()`, `j1939_cfg_t`, `j1939_stack.h`; Doxygen 1.9.8 turns functions, struct types and headers into links, macros and enum values stay plain code. An in-page link needs an explicit `<a id="...">` anchor with a name unique in the project, because Doxygen anchors are global. Files outside the documentation (`CONTRIBUTING.md`, `RELEASING.md`) are not linked with relative links: they are neither in the generated HTML nor in the source archive. The README links `CONTRIBUTING.md` by its GitHub URL. `tools/check-links.sh` (part of `make lint`) checks every relative link and in-page anchor.

## Tests

- Unity 2.6.1 (vendored in `tests/vendor/unity/`), run through `ctest`.
- The test build compiles the library once per port with `j1939_add_library()`. Unit tests link the mock port variant, whatever port the main `j1939` target uses.
- `tests/unit/` tests one module on one stack with injected frames; `tests/integration/` runs several `j1939_t` instances in one process, connected by `tests/support/test_bus.c`, which passes every frame of one stack's tx queue to `j1939_rx()` of all others (each run carries the frames queued when it starts; answers wait for the next run).
- `tests/port/test_port_conformance.c` runs against the mock port, the SocketCAN port (on Linux) and the configured `J1939_PORT_DIR` port if it is another one. Frames the port API cannot build come from the port's `j1939_port_fixture.c`.
- Unit and integration tests link a library variant built with `tests/config/j1939_test_config.h` through `J1939_CONFIG_FILE`; `test_tp_small_buf` links one built with `j1939_test_small_tp_config.h`.
- `test_queue` links the frame queue built for the mock port; the mock lock records nesting depth and call count.
- Timers are tested by passing the elapsed time to `j1939_process()`, for example one call 1 µs before and one at a deadline.
- Coverage with gcov and gcovr: at least 90 % line coverage of the library core, branch coverage reported.

## Tooling

| Tool                 | Use                                                  |
| -------------------- | ---------------------------------------------------- |
| gcc                  | Host compiler                                        |
| CMake (≥ 3.21)       | Build system                                         |
| Unity 2.6.1          | Unit test framework, vendored                        |
| cppcheck (+ misra)   | Static analysis                                      |
| clang-format         | Formatting                                           |
| gcovr                | Coverage reports                                     |
| can-utils            | Manual testing on `vcan0` (`candump`, `cansend`)     |
| arm-none-eabi-gcc    | Compile-only portability check                       |
| Doxygen (≥ 1.9.8)    | API reference and documentation gate                 |
| Graphviz (dot)       | Include, dependency and call graphs of the documentation |
| doxygen-awesome-css 2.5.0 | Documentation stylesheet, vendored in `docs/vendor/` |

## Version control

- Semantic versioning, exposed as `J1939_VERSION_MAJOR`, `J1939_VERSION_MINOR`, `J1939_VERSION_PATCH` in `j1939.h`.
- One branch per task, named after the feature or module (`stack-core`, `tp-bam`). Documentation is updated in the same commit as the code it describes.
- Work reaches `main` only through a pull request from its feature branch. A feature branch is kept current by rebasing it onto `main` and force-pushing it with `--force-with-lease`; `main` itself is never rewritten.
- Tasks that do not depend on each other are developed in parallel, each in its own git worktree and branch.
- Commit messages follow Conventional Commits, `<type>(<scope>): <summary>`:
  - types: `feat`, `fix`, `docs`, `test`, `build`, `refactor`;
  - the scope names the module or feature, e.g. `stack`, `tp`, `addr`, `names`, `rxobj`, `txobj`, `dm`, `signal`, `socketcan`, `docs`, `build`, `lint`;
  - the summary says what changed, in the imperative mood; the body is a few lines on what was done and why;
  - roadmap milestone identifiers do not appear in commit messages or branch names;
  - no `Co-Authored-By` or other tool attribution trailers: the committer is responsible for every commit.

  ```
  feat(queue): add frame queue over integrator-supplied storage

  Rx and tx frames need a FIFO that an ISR and the main loop can share
  without the library allocating memory. Index updates run inside the
  port lock; frames are written in place.
  ```
