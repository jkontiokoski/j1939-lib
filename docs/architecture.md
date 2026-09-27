# Architecture

## Design principles

- **Portable C99.** The library core depends only on `<stdint.h>`, `<stdbool.h>`, `<stddef.h>` and `<string.h>`.
- **Suitable for safety-rated systems (e.g. ISO 13849).** The code is written to be statically auditable:
  - no function pointers and no runtime dispatch; all binding to the target happens at compile time,
  - no dynamic memory; the integrator allocates and supplies every buffer,
  - no recursion, no VLAs, no stdio in the core,
  - every loop has a static bound,
  - state machines are explicit `switch` statements over enums with an error-handling `default`.
- **Native CAN frames.** The library has no CAN frame type of its own.
  It operates directly on the integrator's frame type through compile-time accessors supplied by the port.
- **The library never calls out.** Received frames are pushed in, outgoing frames and received messages are pulled out.
- **No global state.** All state lives in integrator-owned objects; several buses can be run by one binary.
- **Time is passed in.** The library never reads a clock.

## Layering

Each layer depends only on the layers below it.

```
 Application: pulls received messages, queues messages to send, signal access, DM1
 ──────────────────────────────────────────────────────────────────────────────
 j1939_diag (J1939/73)   j1939_db / signals (J1939/71 + DA schema)        optional modules
 ──────────────────────────────────────────────────────────────────────────────
 j1939_addr (J1939/81)   j1939_tp (J1939/21 TP.BAM / TP.CM)               protocol core
 j1939_stack: j1939_t, rx/tx queues, CA objects, DA/PGN filtering, Request/ACK
 ──────────────────────────────────────────────────────────────────────────────
 j1939_id / j1939_name: pure codecs on uint32_t / uint64_t                no state, no I/O
 ──────────────────────────────────────────────────────────────────────────────
 PORT BOUNDARY (compile time): j1939_target.h, supplied by the port
 ──────────────────────────────────────────────────────────────────────────────
 port/socketcan, port/mock, integrator ports
```

## Source tree

```
include/j1939/        public headers
                        j1939.h                 umbrella header, version macros
                        j1939_ret.h             return codes
                        j1939_id.h              29-bit identifier / PGN codec
                        j1939_name.h            NAME codec
                        j1939_msg.h             logical message type
                        j1939_stack.h           stack object, queues, process
                        j1939_tp.h              transport protocol
                        j1939_addr.h            address claiming
                        j1939_config.h          compile-time configuration and defaults
                        j1939_port_contract.h   required target API, compile-time checks
src/                  implementation (*.c) and private headers (*_priv.h)
port/mock/            test port with a deliberately unusual frame layout
port/socketcan/       Linux SocketCAN (CAN_RAW) port
examples/             example applications (SocketCAN)
tests/port/           port conformance tests, compiled once per port
tests/unit/           unit tests per module (mock port)
tests/integration/    multi-node scenarios (mock port)
tests/vendor/unity/   vendored Unity test framework
cmake/                build helpers
                        warnings.cmake          project warning set, j1939_set_warnings()
                        instrumentation.cmake   sanitizer and coverage options
                        arm-none-eabi.cmake     Cortex-M0+ toolchain for the portability check
docs/                 project documentation
CMakeLists.txt        build definition
Makefile              convenience wrapper around CMake
cppcheck-suppressions.txt  static analysis deviations
.clang-format         formatting rules
```

The version is defined once, in `j1939.h`; `CMakeLists.txt` reads it from there.

## Key abstractions

| Abstraction            | Type                                             | Role                                                                                  |
| ---------------------- | ------------------------------------------------ | ------------------------------------------------------------------------------------- |
| Native frame           | `j1939_port_frame_t` (typedef by the port)       | The only type crossing the port boundary. Accessed only through the port accessors    |
| ID codec               | `j1939_id_*()` pure functions on `uint32_t`      | Priority, EDP, DP, PF, PS, SA; PDU1 (PF < 240, PS = DA) / PDU2 rules; PGN handling    |
| NAME codec             | `j1939_name_*()` pure functions on `uint64_t`    | J1939/81 NAME fields                                                                  |
| Message                | `j1939_msg_t {pgn, prio, sa, da, len, data}`     | Logical message, 0–1785 bytes; `data` points to integrator memory                     |
| Stack                  | `j1939_t`                                        | One per CAN bus. Queue indices, TP sessions, CA state, pointers to integrator buffers |
| Controller Application | `j1939_ca_t`                                     | NAME, address-claim state, source address                                            |
| Return codes           | `enum j1939_ret`                                 | Returned by every fallible API                                                        |

## Interface boundaries

### Port boundary

The library includes `j1939_target.h`, found through the include path selected by the build (`J1939_PORT_DIR`).
The port typedefs its native frame type as `j1939_port_frame_t` and provides `static inline` accessors for it, plus critical-section macros.
The library is compiled against exactly one port.
The full contract is described in [porting.md](porting.md).

### Integrator buffers

The integrator allocates all memory and hands it to the stack at initialisation:

```c
j1939_port_frame_t rx_buf[16];
j1939_port_frame_t tx_buf[16];
uint8_t tp_rx_mem[2][1785];

j1939_cfg_t cfg = {
	.rx_buf = rx_buf, .rx_len = 16,
	.tx_buf = tx_buf, .tx_len = 16,
	.tp_rx_mem = ..., /* TP reassembly memory, one area per session */
	.pgn_accept = my_pgns, .pgn_accept_len = N,
};
j1939_init(&stack, &cfg);
```

### Runtime interface

| Direction           | API                                                              | Context                          |
| ------------------- | ---------------------------------------------------------------- | -------------------------------- |
| CAN rx (zero copy)  | `j1939_rx_acquire()` → driver writes the slot → `j1939_rx_commit()` | ISR or rx thread (single producer) |
| CAN rx (copy)       | `j1939_rx_put(&stack, &frame)`                                   | ISR or rx thread (single producer) |
| Processing          | `j1939_process(&stack, elapsed_us)`                              | Main loop / task                 |
| CAN tx              | `j1939_tx_peek()` → driver writes the frame → `j1939_tx_pop()`   | Main loop, tx-complete ISR, DMA  |
| Application rx      | `j1939_msg_get(&stack, &msg)`, application switches on `msg.pgn` | Main loop / task                 |
| Application tx      | `j1939_send(&ca, &msg)`                                          | Main loop / task                 |

- The rx queue is a single-producer single-consumer ring over the integrator's `rx_buf`.
- `j1939_process()` drains the rx ring, runs the TP and address-claim state machines and queues outgoing frames into `tx_buf`.
- Only messages whose PGN is in the integrator's constant PGN accept list, and whose destination is one of the stack's addresses or global, reach the application.
- A Request (PGN 59904) for a PGN outside the accept list is answered with a NACK (PGN 59392) by the stack. Requests for accepted PGNs are delivered to the application, which answers with `j1939_send()`.

### Configuration

- `include/j1939/j1939_config.h` defines the defaults: `J1939_CFG_TP_SESSIONS`, `J1939_CFG_CA_MAX` and module enables (`J1939_CFG_TP_ENABLE`, `J1939_CFG_DIAG_ENABLE`, ...).
- An integrator overrides them with `-DJ1939_CONFIG_FILE="my_cfg.h"`.
- Buffer sizes are runtime parameters given at initialisation.

### J1939DA

The J1939DA content is copyrighted by SAE.
The library provides the database engine (SPN descriptor schema, bit extraction, scaling, J1939/71 not-available/error ranges) and a small illustrative table.
Integrators supply their licensed DA content as `const` tables.

## Development standards

### Language and rules

- C99 (`-std=c99 -pedantic`).
- MISRA-C:2012 inspired. Deviations are listed below and in `cppcheck-suppressions.txt`.
- Fixed-width integer types throughout.
- Single exit point per function is preferred.
- Warnings: `-Wall -Wextra -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wcast-align -Wundef`.

### Naming

- All public identifiers are prefixed `j1939_` (functions, types) or `J1939_` (macros).
- Types end in `_t`.
- Functions are prefixed by their module: `j1939_tp_*`, `j1939_addr_*`.
- The port API is prefixed `j1939_port_`.
- The public API lives only in `include/j1939/`.

### Formatting and file headers

- `.clang-format`: tabs for indentation, 100-column limit, opening braces on the same line, braces on every control statement. Run `make format` / `make format-check`.
- Every source file starts with an SPDX header:

  ```c
  /* SPDX-License-Identifier: MIT */
  /* Copyright (c) 2026 jkontiokoski */
  ```

- Public declarations carry Doxygen comments.

### Testing

- Unity (vendored in `tests/vendor/unity/`), run through `ctest`.
- Unit tests per module use the mock port.
- Port conformance tests in `tests/port/` are compiled and run against every port.
- Integration tests run several `j1939_t` instances in one process; the test harness moves frames from one stack's tx queue to the others' rx queues.
- Host test builds run with AddressSanitizer and UndefinedBehaviorSanitizer.
- Coverage with gcov/gcovr; target ≥ 90 % line coverage on the protocol core, branch coverage reported.

### Static analysis deviations

| Suppression                                    | Scope                | Reason                                                                     |
| ---------------------------------------------- | -------------------- | -------------------------------------------------------------------------- |
| `unusedFunction`                               | All                  | Public API functions have no callers inside the library                    |
| `preprocessorErrorDirective`                   | `j1939_config.h`     | The optional `J1939_CONFIG_FILE` include is resolved only in integrator builds |
| MISRA 2.3 (unused type), 2.4 (unused tag)      | `include/j1939/`     | Public headers declare types for the integrator's use                     |

### Tooling

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

CMake options:

| Option              | Default               | Effect                                               |
| ------------------- | --------------------- | ---------------------------------------------------- |
| `J1939_BUILD_TESTS` | ON when top level     | Builds the test suite                                |
| `J1939_WERROR`      | ON when top level     | Treats warnings as errors                            |
| `J1939_SANITIZE`    | OFF                   | AddressSanitizer + UndefinedBehaviorSanitizer        |
| `J1939_COVERAGE`    | OFF                   | gcov instrumentation                                 |
| `J1939_COMPILE_COMMANDS` | ON when top level | Writes `compile_commands.json` into the build directory |

clangd finds the compilation database of the `make` build in `build/` without further configuration.

Make targets:

| Target              | Action                                                                   |
| ------------------- | ------------------------------------------------------------------------ |
| `make`              | Debug build in `build/`                                                  |
| `make test`         | Builds with sanitizers in `build-test/` and runs `ctest`                 |
| `make coverage`     | Builds with coverage in `build-coverage/`, runs tests, fails under 90 % line coverage |
| `make cross`        | Compiles the library for Cortex-M0+ in `build-arm/`                      |
| `make lint`         | cppcheck with the MISRA addon                                            |
| `make format`       | Formats all project sources                                              |
| `make format-check` | Fails if any project source is not formatted                             |
| `make clean`        | Removes all build directories                                            |

### Versioning and version control

- Semantic versioning, exposed as `J1939_VERSION_MAJOR`, `J1939_VERSION_MINOR`, `J1939_VERSION_PATCH` in `j1939.h`.
- Conventional Commits (`feat:`, `fix:`, `docs:`, `test:`, `build:`, `refactor:`).
- One branch per task. Documentation is updated in the same commit as the code it describes.
