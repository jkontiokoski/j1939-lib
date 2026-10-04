# Concepts

This page explains how the library is built and why: its design principles, its layers, how data moves between the CAN driver and the application, and which header holds what.
Read it before designing an integration or reviewing the code.

## Design principles

| Principle | What it means | Why |
| --- | --- | --- |
| Portable C99 | The core needs only `<stdint.h>`, `<stdbool.h>`, `<stddef.h>` and `<string.h>` | Runs on any target, from Cortex-M0+ to Linux |
| Statically auditable | No function pointers, recursion, VLAs or stdio; every loop has a static bound; state machines are `switch` statements with an error-handling `default` | Control flow and resource use can be analysed without running the code, as safety-rated systems (ISO 13849) require |
| No dynamic memory | The application supplies every buffer | Memory use is fixed at build time |
| Native CAN frames | The library has no frame type of its own; the port's compile-time accessors read the driver's type | No conversion, no binding at runtime |
| Never calls out | Received frames are pushed in; outgoing frames and received messages are pulled out | No callbacks: the application decides when stack work happens |
| One execution context | All calls on a stack run in the task or loop that calls `j1939_process()` | No lock in the stack; the driver's FIFOs cross between contexts |
| No global state | All state lives in application-owned objects | Several buses in one binary |
| Time is passed in | `j1939_process()` receives the elapsed time; the library never reads a clock | Deterministic, testable timing |

## Layers

Each layer uses only the layers below it.

| Layer | Modules | Role |
| --- | --- | --- |
| Optional modules | `j1939_diag`, `j1939_signal` | DTC and DM1/DM2 payload codec; signal descriptors and scaling |
| Protocol core | `j1939_dm`, `j1939_rxobj`, `j1939_txobj`, `j1939_addr`, `j1939_names`, `j1939_tp`, `j1939_stack`, `j1939_request` | Diagnostics, message objects, address claiming, NAME table, transport protocol, frame handling and message slots, Requests |
| Codecs | `j1939_id`, `j1939_name`, `j1939_ring` | Identifier, NAME and FIFO index helpers without state or I/O |
| Port | `j1939_target.h` | The driver's frame type and accessors, bound at compile time |

The optional frame queue (`j1939_queue.h`) is outside the layers: application code uses it to hand frames from an interrupt to the stack's context.

## Data flow

Receiving:

1. The application takes frames from the CAN driver and passes each to `j1939_rx()`, which handles it during the call.
2. The stack handles protocol traffic itself: address claiming, Requests it answers, the transport protocol and diagnostics.
3. A message whose PGN and sender match a receive object updates the object; one whose PGN is in `rx_pgns` goes into a message slot. Multi-packet messages take the same path once complete.
4. The application reads objects with `j1939_rxobj_get()`, messages with `j1939_msg_peek()` and `j1939_msg_pop()`, and decodes signals with `j1939_signal_decode()`.

Transmitting:

1. The application encodes signals into a payload and writes it to a transmit object with `j1939_txobj_set()`, or sends it once with `j1939_send()`.
2. `j1939_process()` sends each transmit object when its period, a change or a Request makes it due.
3. A send builds a single frame, or starts a transport protocol transfer whose packets `j1939_process()` queues; frames wait in the tx queue.
4. The application moves the tx queue to the driver with `j1939_tx_peek()` and `j1939_tx_pop()`.

Frames exist only at these two edges; everything in between works on payloads.
[Message objects](guides/message-objects.md) explains when to use objects, message slots or `j1939_send()`.

## Execution model

- Application logic in other tasks exchanges data with the stack's task through mailboxes of its own.
- Timers advance only through `j1939_process()`. A timer started by a received frame or an API call counts from the next call, so a timeout expires between its nominal value and one call period later.
- When a message slot or the tx queue is full, the stack drops what it generated and counts it in `j1939_stats_t`; application sends return `J1939_RET_ERR_FULL` instead.
- The API reference, topic *Stack*, has the exact rules of each call.

## Where to find what

The API reference groups the headers into one topic per module; the guides show how to use them.

| Header | Content | Main types |
| --- | --- | --- |
| `j1939.h` | Umbrella header, version | |
| `j1939_stack.h` | Stack instance, configuration, frame rx, tx queue, sending, message slots, event counters | `j1939_t`, `j1939_cfg_t`, `j1939_ca_cfg_t`, `j1939_msg_slot_t`, `j1939_stats_t` |
| `j1939_msg.h` | Logical message of 0–1785 bytes | `j1939_msg_t` |
| `j1939_addr.h` | Address claiming and Commanded Address (J1939/81) | |
| `j1939_names.h` | NAME table: the NAMEs and addresses of the other nodes | `j1939_names_entry_t` |
| `j1939_request.h` | Request and Acknowledgement | |
| `j1939_tp.h` | Transport protocol: abort reasons, timers | `j1939_tp_buf_t` |
| `j1939_rxobj.h`, `j1939_txobj.h` | Receive and transmit objects | `j1939_rxobj_cfg_t`, `j1939_txobj_cfg_t` |
| `j1939_signal.h` | Signal descriptors, bit fields, scaling, value ranges | `j1939_signal_t` |
| `j1939_diag.h` | DTC, lamp status and DM1/DM2 payload codec | `j1939_diag_dtc_t`, `j1939_diag_lamps_t` |
| `j1939_dm.h` | Diagnostics of a CA: DM1, DM2, DM3, DM11 | `j1939_dm_cfg_t`, `j1939_dm_t` |
| `j1939_id.h`, `j1939_name.h` | Identifier, PGN and NAME codecs | `j1939_name_fields_t` |
| `j1939_config.h` | Compile-time configuration | |
| `j1939_ret.h` | Return codes | |
| `j1939_port_contract.h` | What a port's `j1939_target.h` provides | |
| `j1939_queue.h` | Optional frame queue between two contexts | `j1939_queue_t` |
