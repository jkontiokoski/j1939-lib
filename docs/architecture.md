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
 j1939_diag (J1939/73)   j1939_signal (J1939/71 + DA schema)              optional modules
 ──────────────────────────────────────────────────────────────────────────────
 j1939_addr (J1939/81)   j1939_tp (J1939/21 TP.BAM / TP.CM)               protocol core
 j1939_stack: j1939_t, rx/tx queues, CA objects, DA/PGN filtering, message slots
 j1939_request: Request (PGN 59904) and Acknowledgement (PGN 59392)
 ──────────────────────────────────────────────────────────────────────────────
 j1939_id / j1939_name: pure codecs on uint32_t / uint64_t                no state, no I/O
 j1939_queue / j1939_ring: FIFOs over integrator storage                 port lock only
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
                        j1939_stack.h           stack object, configuration, process, send, message pull
                        j1939_request.h         Request and Acknowledgement
                        j1939_signal.h          signal (SPN) descriptors, extraction, scaling, validity
                        j1939_tp.h              transport protocol
                        j1939_addr.h            address claiming
                        j1939_queue.h           CAN frame queue over integrator storage
                        j1939_ring.h            ring index type shared by the queues (members private)
                        j1939_config.h          compile-time configuration and defaults
                        j1939_port_contract.h   required target API, compile-time checks
src/                  implementation (*.c) and private headers (*_priv.h)
port/<name>/          one directory per port
                        j1939_target.h          native frame type, accessors, lock
                        port.cmake              optional: port sources, libraries, test fixture
                        j1939_port_fixture.c    optional: conformance test fixture
port/mock/            test port with a deliberately unusual frame layout
port/socketcan/       Linux SocketCAN (CAN_RAW) port, plus j1939_socketcan.[ch] socket helpers
examples/             example applications (SocketCAN)
examples/signals/     illustrative signal table: invented Proprietary B signals
tests/port/           port conformance tests, compiled once per port; SocketCAN loopback test
tests/unit/           unit tests per module (mock port)
tests/integration/    multi-node scenarios (mock port)
tests/support/        test_bus.[ch]: virtual CAN bus connecting several stacks in one process
tests/config/         j1939_test_config.h: configuration of the test library builds
tests/vendor/unity/   vendored Unity test framework
cmake/                build helpers
                        library.cmake           j1939_add_library(): builds the library for a port
                        warnings.cmake          project warning set, j1939_set_warnings()
                        instrumentation.cmake   sanitizer and coverage options
                        arm-none-eabi.cmake     Cortex-M0+ toolchain for the portability check
docs/                 project documentation
CMakeLists.txt        build definition
Makefile              convenience wrapper around CMake
cppcheck-suppressions.txt        general static analysis deviations
cppcheck-misra-suppressions.txt  MISRA deviations
.clang-format         formatting rules
```

The version is defined once, in `j1939.h`; `CMakeLists.txt` reads it from there.

## Key abstractions

| Abstraction            | Type                                             | Role                                                                                  |
| ---------------------- | ------------------------------------------------ | ------------------------------------------------------------------------------------- |
| Native frame           | `j1939_port_frame_t` (typedef by the port)       | The only type crossing the port boundary. Accessed only through the port accessors    |
| Lock                   | `j1939_port_lock_t` (typedef by the port)        | Critical-section object embedded in each queue                                        |
| Frame queue            | `j1939_queue_t`                                  | FIFO of native frames over integrator storage; one producer, one consumer            |
| ID codec               | `j1939_id_*()` pure functions on `uint32_t`      | Priority, EDP, DP, PF, PS, SA; PDU1 (PF < 240, PS = DA) / PDU2 rules; PGN handling    |
| NAME codec             | `j1939_name_*()` pure functions on `uint64_t`    | J1939/81 NAME fields                                                                  |
| Message                | `j1939_msg_t {pgn, prio, sa, da, len, data}`     | Logical message, 0–1785 bytes; `data` points to integrator memory                     |
| Message slot           | `j1939_msg_slot_t`                               | Integrator storage for one received message; the application reads it in place        |
| Signal descriptor      | `j1939_signal_t`, `j1939_signal_*()` functions   | SPN position, length, scaling and J1939/71 range type; `const` integrator data        |
| Stack                  | `j1939_t`, configured by `j1939_cfg_t`           | One per CAN bus. Queues, message slots, PGN lists, CAs, event counters                |
| Controller Application | `j1939_ca_t`, configured by `j1939_ca_cfg_t`     | Source address; NAME and address-claim state with J1939/81. Referenced by `j1939_ca_id_t` |
| Event counters         | `j1939_stats_t`                                  | Messages and frames dropped because integrator storage was full                       |
| Return codes           | `enum j1939_ret`                                 | Returned by every fallible API                                                        |

## Interface boundaries

### Port boundary

The library includes `j1939_target.h`, found through the include path selected by the build (`J1939_PORT_DIR`).
The port typedefs its native frame type as `j1939_port_frame_t` and provides `static inline` accessors for it, plus a lock type with `static inline` lock functions.
The library includes the target header only through `j1939_port_contract.h`, which repeats the required declarations: a port whose definitions differ fails to compile, and a missing definition is reported as declared but never defined.
The library is compiled against exactly one port.
The full contract is described in [porting.md](porting.md).

### Integrator buffers

The integrator allocates all memory and hands it to the stack at initialisation:

```c
static j1939_port_frame_t rx_buf[16];
static j1939_port_frame_t tx_buf[16];
static j1939_msg_slot_t msg_buf[8];
static const uint32_t rx_pgns[] = {0xFEF1U, 0xE800U};  /* delivered to the application */
static const uint32_t req_pgns[] = {0xFEEBU};          /* answered on Request by the application */
static j1939_t stack;
static j1939_ca_id_t ca;

const j1939_cfg_t cfg = {
	.rx_buf = rx_buf, .rx_len = 16,
	.tx_buf = tx_buf, .tx_len = 16,
	.msg_buf = msg_buf, .msg_len = 8,
	.rx_pgns = rx_pgns, .rx_pgns_len = 2,
	.req_pgns = req_pgns, .req_pgns_len = 1,
};
j1939_init(&stack, &cfg);
j1939_ca_add(&stack, &(j1939_ca_cfg_t){.address = 0x80}, &ca);
```

`j1939_init()` validates the whole configuration before it changes the stack object.
PGNs in the lists must be valid PGNs: at most 0x3FFFF, lowest byte 0 for PDU1 formats.

### Runtime interface

| Direction           | API                                                                                       | Context                            |
| ------------------- | ----------------------------------------------------------------------------------------- | ---------------------------------- |
| CAN rx (zero copy)  | `j1939_queue_acquire(j1939_rx_queue(&stack))` → driver writes the slot → `j1939_queue_commit()` | ISR or rx thread (single producer) |
| CAN rx (copy)       | `j1939_queue_put(j1939_rx_queue(&stack), &frame)`                                         | ISR or rx thread (single producer) |
| Processing          | `j1939_process(&stack, elapsed_us)`                                                       | Main loop / task                   |
| CAN tx              | `j1939_queue_peek(j1939_tx_queue(&stack))` → driver writes the frame → `j1939_queue_pop()` | Main loop, tx-complete ISR, DMA    |
| Application rx      | `j1939_msg_peek(&stack)`, application switches on `msg->pgn`, then `j1939_msg_pop(&stack)` | Main loop / task                   |
| Application tx      | `j1939_send(&stack, ca, &msg)`, `j1939_request_send(&stack, ca, pgn, da)`                 | Main loop / task                   |

- The rx and tx queues are `j1939_queue_t` instances over the integrator's buffers, reached through `j1939_rx_queue()` and `j1939_tx_queue()`. Each has one producer and one consumer, which may run in different contexts; index updates run inside the port lock, frame contents are written and read outside it. Every slot of the buffer is usable.
- `j1939_process()` handles the frames present in the rx queue when it starts; frames arriving meanwhile wait for the next call. It stores application messages in the message slots and queues frames the stack generates into the tx queue.
- A received message and its data stay valid in its slot until `j1939_msg_pop()`.
- `j1939_send()` builds the frame immediately; the message and its data may be reused after the call. Payloads longer than 8 bytes are rejected until the transport protocol is available.
- When the message slots or the tx queue are full, the stack drops the message or frame it generated and counts it in `j1939_stats_t`. Application sends report `J1939_RET_ERR_FULL` instead.

Receive filtering in `j1939_process()`:

1. Standard (11-bit) frames, remote frames and frames with the extended data page bit set are dropped.
2. Frames addressed to a node other than one of the stack's CAs are dropped; global frames pass.
3. A Request (PGN 59904) is handled by the Request module:
   - for a PGN in `req_pgns` it is delivered to the application, which reads the PGN with `j1939_request_pgn_get()` and answers with `j1939_send()`;
   - a destination-specific Request for any other PGN is answered by the stack with a NACK (PGN 59392), sent to the global address with the requester in byte 5, as J1939/21 specifies;
   - a global Request for any other PGN, and a Request shorter than 3 bytes, are ignored.
4. Any other PGN in `rx_pgns` is delivered to the application; the rest are dropped.

### Configuration

- `include/j1939/j1939_config.h` defines the defaults: `J1939_CFG_TP_SESSIONS`, `J1939_CFG_CA_MAX` and module enables (`J1939_CFG_TP_ENABLE`, `J1939_CFG_DIAG_ENABLE`, ...).
- An integrator overrides them with `-DJ1939_CONFIG_FILE="my_cfg.h"`.
- Buffer sizes are runtime parameters given at initialisation.

### J1939DA

The J1939DA content is copyrighted by SAE.
The library provides the database engine in `j1939_signal.h`: the SPN descriptor schema, bit extraction and insertion, scaling and the J1939/71 value ranges.
Integrators supply their licensed DA content as `const` tables of `j1939_signal_t`.
The illustrative table in `examples/signals/` uses invented signals in the Proprietary B PGN range (0xFF00–0xFFFF), not DA definitions.

A descriptor is plain data:

| Field                | Content                                                                                   |
| -------------------- | ----------------------------------------------------------------------------------------- |
| `spn`, `pgn`         | SPN (19 bits) and the PGN the signal is transmitted in                                    |
| `start`              | Bit offset in the payload. The DA start position "B.b" (byte and bit from 1) is `J1939_SIGNAL_POS(B, b)` |
| `bits`               | Length, 1–32 bits                                                                         |
| `type`               | Range convention: `PLAIN`, `CONTINUOUS` (8, 16, 32 bits) or `DISCRETE` (2 bits)           |
| `res_num`, `res_den` | Resolution as a fraction: engineering units per bit                                       |
| `offset`             | Offset in engineering units (`int64_t`)                                                   |
| `unit`, `name`       | Optional strings for display                                                              |

- Payload bit `n` is bit `n % 8` of byte `n / 8`; multi-byte values are little endian. Insertion leaves all other payload bits unchanged.
- Engineering value = raw × `res_num` / `res_den` + `offset`, in 64-bit integer arithmetic without floating point, rounded to nearest with halves up. The integrator chooses the integer engineering unit and with it the precision: 0.125 rpm/bit is `1/8` in rpm or `125/1` in millirpm.
- `j1939_signal_check()` rejects descriptors whose largest raw value would scale beyond `INT64_MAX`, so decoding cannot overflow. Every function taking a descriptor validates it first.
- Encoding rejects values that round to a raw value below 0, beyond the field or outside the J1939/71 valid range. Indicators are written with `j1939_signal_indicator_set()`.
- J1939/71 ranges: continuous parameters of 1, 2 and 4 bytes are classified by their most significant byte: 0x00–0xFA valid, 0xFB parameter specific, 0xFC–0xFD reserved, 0xFE error, 0xFF not available. 2-bit discrete parameters: 00 and 01 valid, 10 error, 11 not available. Plain signals have no reserved values.

Decoding a received message:

```c
const j1939_msg_t *msg = j1939_msg_peek(&stack);
int64_t rpm;
j1939_signal_class_t cls;

if ((msg != NULL) &&
    (j1939_signal_msg_decode(&example_signals[EXAMPLE_PUMP_SPEED], msg, &rpm, &cls) == J1939_RET_OK) &&
    (cls == J1939_SIGNAL_VALID)) {
	/* use rpm */
}
```

`j1939_signal_msg_decode()` fails if the message's PGN differs from the descriptor's or the message is too short to contain the signal.

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
- The test build compiles the library once per port with `j1939_add_library()`. Unit tests link the mock port variant, whatever port the main `j1939` target uses.
- The port conformance test `tests/port/test_port_conformance.c` runs against the mock port, the SocketCAN port (on Linux) and the configured `J1939_PORT_DIR` port if it is another one. Frames the port API cannot build (standard, remote, raw DLC above 8) come from the port's `j1939_port_fixture.c`.
- The mock lock records nesting depth and call count; unit tests check that every critical section is left and none is nested.
- `test_socketcan_vcan` exchanges frames over a real SocketCAN interface, `vcan0` by default or `J1939_TEST_CANIF`. It reports "skipped" when the interface does not exist.
- Unit and integration tests link a library variant built with `tests/config/j1939_test_config.h` through `J1939_CONFIG_FILE`, which also exercises the configuration override.
- Integration tests run several `j1939_t` instances in one process; `tests/support/test_bus.c` moves every frame from one stack's tx queue to the rx queues of all others.
- `test_signal` compares bit extraction and insertion against a bit-by-bit reference model for every offset and length in payloads of 1 to 8 bytes. `test_signal_example` builds and decodes messages with the example table from `examples/signals/`.
- Host test builds run with AddressSanitizer and UndefinedBehaviorSanitizer.
- Coverage with gcov/gcovr; target ≥ 90 % line coverage on the protocol core, branch coverage reported. Defensive checks against states the design rules out remain as uncovered branches.

### Static analysis deviations

The MISRA checks apply to the library core (`src/`, `include/`) and the mock port.
The SocketCAN port is operating system glue built on POSIX interfaces; it gets the general cppcheck checks only.
Port test fixtures are test code and are not linted.

| Suppression                                    | Scope                | Reason                                                                     |
| ---------------------------------------------- | -------------------- | -------------------------------------------------------------------------- |
| `unusedFunction`                               | All                  | Public API functions have no callers inside the library                    |
| `preprocessorErrorDirective`                   | `j1939_config.h`     | The optional `J1939_CONFIG_FILE` include is resolved only in integrator builds |
| MISRA 2.5 (unused macro)                       | `include/j1939/`     | Public headers define macros for the integrator's use                     |

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
| `J1939_PORT_DIR`    | `port/mock` when top level, required otherwise | Port directory; relative paths are resolved against the top-level source directory |
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
| `make lint`         | cppcheck: core and mock port with the MISRA addon, SocketCAN port with the general checks |
| `make format`       | Formats all project sources                                              |
| `make format-check` | Fails if any project source is not formatted                             |
| `make clean`        | Removes all build directories                                            |

### Versioning and version control

- Semantic versioning, exposed as `J1939_VERSION_MAJOR`, `J1939_VERSION_MINOR`, `J1939_VERSION_PATCH` in `j1939.h`.
- One branch per task, named after the feature or module (`stack-core`, `tp-bam`). Documentation is updated in the same commit as the code it describes.
- Commit messages follow Conventional Commits: `<type>(<scope>): <summary>`.
  - Types: `feat`, `fix`, `docs`, `test`, `build`, `refactor`.
  - The scope names the module or feature: `id`, `queue`, `port`, `socketcan`, `stack`, `tp`, `addr`, `build`.
  - The summary says what changed, in the imperative mood.
  - The body is a few lines on what was done and why. Short enough to read at a glance.
  - Roadmap milestone identifiers do not appear in commit messages or branch names.
  - No `Co-Authored-By` or other tool attribution trailers. The committer is responsible for every commit.

  ```
  feat(queue): add frame queue over integrator-supplied storage

  Rx and tx frames need a FIFO that an ISR and the main loop can share
  without the library allocating memory. Index updates run inside the
  port lock; frames are written in place.
  ```
