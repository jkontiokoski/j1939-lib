# Safety and quality

This page is for reviewers and assessors who qualify the library for a safety-rated system, for example under ISO 13849.
It describes how the code is written and checked, what each release documents as evidence, and what remains the application's responsibility.

## Coding standard

- C99 (`-std=c99 -pedantic`), fixed-width integer types, compiled with `-Wall -Wextra -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wcast-align -Wundef`.
- MISRA C:2012 is checked with the cppcheck MISRA addon on the library core (`src/`, `include/`) and the mock port.
  The SocketCAN port and the example applications are operating system glue built on POSIX interfaces; they get the general cppcheck checks only. Port test fixtures are test code and are not checked.
- Every reported finding fails the check, including whole-program findings such as MISRA rule 5.9, for which cppcheck 2.13 does not set its exit code.
- The design rules behind the code are in [Concepts](concepts.md).

## Deviations

Each deviation is listed in `cppcheck-suppressions.txt` or `cppcheck-misra-suppressions.txt` with its reason:

| Suppression                                    | Scope                | Reason                                                                     |
| ---------------------------------------------- | -------------------- | -------------------------------------------------------------------------- |
| `unusedFunction`                               | All                  | Public API functions have no callers inside the library                    |
| `preprocessorErrorDirective`                   | `j1939_config.h`     | The optional `J1939_CONFIG_FILE` include is resolved only in integrator builds |
| MISRA 2.5 (unused macro)                       | `include/j1939/`     | Public headers define macros for the integrator's use                     |

## Verification

| Level | What runs |
| --- | --- |
| Unit tests | Every module on one stack, with injected peer frames; timers checked at their boundaries (one call 1 µs before and one at a deadline) |
| Integration tests | Several stacks in one process on a virtual bus: address arbitration, transport protocol transfers of up to 1785 bytes with lost packets and aborts, diagnostics and message objects between nodes |
| Port conformance | The same suite against the mock port, the SocketCAN port and any configured port |
| Configuration variants | The library built with a test configuration (two CAs) and with small TP buffers (100 bytes), exercising the configuration override and the size limits |
| Reference models | Signal bit extraction and insertion compared bit by bit against a reference model for every offset and length |
| SocketCAN loopback | Frames exchanged through the Linux SocketCAN stack on a virtual interface (`vcan0`) |

- Host test builds run with AddressSanitizer and UndefinedBehaviorSanitizer.
- Line coverage of the library core must stay at or above 90 %; branch coverage is reported. Branches that guard against states the design rules out remain uncovered.
- The mock port deliberately uses a frame layout unlike SocketCAN's, so that tests catch layout assumptions.

## Documentation for review

The internal reference (`make docs-internal`, also shipped in each release) documents every function and macro of the implementation, static ones included, with source browsing and call and caller graphs.
The code has no function pointers, so the call graphs are complete.
Undocumented code fails the documentation build, as it does for the public API.

## Release evidence

Each release ships an evidence archive next to the source and documentation archives, produced by building and checking exactly the released source:

- JUnit results of the sanitizer and coverage test runs,
- the coverage report, as text and HTML,
- the cppcheck and MISRA results with both suppression lists,
- the full log of the checks and the versions of the tools used.

## The application's responsibilities

The library implements the J1939 protocol; the safety function stays with the application:

- End-to-end protection of safety-related messages (message counters, checksums): the library carries those signals and does not compute or check them, see [Scope](scope.md).
- Fault management: detecting faults, keeping the fault memory, and deciding whether a DM3/DM11 clear request is accepted.
- Timing: calling `j1939_process()` often enough, from a monotonic clock, and supervising received PGNs (receive objects report timeouts; acting on them is the application's).
- Resources: sizing the buffers ([Configuration](configuration.md)) and checking the event counters in `j1939_stats_t` for dropped messages and frames.
- The port: running the port conformance test against it ([Porting](porting.md)).
