# Concepts

This page explains how the library is built and why: its design principles, its layers, how data flows between the CAN driver and the application, and which header holds what.
Read it before designing an integration or reviewing the code.

## Design principles

| Principle | What it means | Why |
| --- | --- | --- |
| Portable C99 | The core depends only on `<stdint.h>`, `<stdbool.h>`, `<stddef.h>` and `<string.h>` | Runs on any compiler and target, from Cortex-M0+ to Linux |
| Statically auditable | No function pointers or runtime dispatch, no recursion, no VLAs, no stdio in the core; every loop has a static bound; state machines are `switch` statements over enums with an error-handling `default` | Suitable for safety-rated systems (ISO 13849): control flow and resource use can be analysed without running the code |
| No dynamic memory | The integrator allocates and supplies every buffer | Memory use is fixed at build time |
| Native CAN frames | The library has no frame type of its own; it works on the integrator's frame type through compile-time accessors supplied by the port | No conversion between driver and stack, and no binding at runtime |
| Never calls out | Received frames are pushed in; outgoing frames and received messages are pulled out | No callbacks: the application decides when stack work happens |
| One execution context, no lock | All functions of a stack instance run in the task or main loop that calls `j1939_process()` | No locking inside the stack; the driver's FIFOs cross between contexts |
| No global state | All state lives in integrator-owned objects | Several buses can run in one binary |
| Time is passed in | The library never reads a clock; `j1939_process()` receives the elapsed time | Deterministic and testable timing |

## Layering

Each layer uses only the layers below it.

| Layer | Modules | Role |
| --- | --- | --- |
| Optional modules | `j1939_diag`, `j1939_signal` | DTC and DM1/DM2 payload codec; SPN descriptors, scaling and value ranges |
| Protocol core | `j1939_dm`, `j1939_rxobj`, `j1939_txobj`, `j1939_addr`, `j1939_tp`, `j1939_stack`, `j1939_request` | Diagnostics per CA, message objects, address claiming, transport protocol, frame handling and message slots, Request and Acknowledgement |
| Codecs | `j1939_id`, `j1939_name`, `j1939_ring` | Identifier and NAME codecs, FIFO indices; no state, no I/O |
| Port | `j1939_target.h` | The integrator's frame type and accessors, bound at compile time |

The optional frame queue (`j1939_queue.h`) sits outside these layers: integrator code uses it to hand frames from a driver interrupt to the stack's context.

## Data flow

Receiving:

1. The integrator takes a frame from the CAN driver, optionally through `j1939_queue` when the driver delivers it in another context, and passes it to `j1939_rx()`.
2. The stack handles protocol PGNs itself: address claiming, Requests, the transport protocol and diagnostics.
3. A frame, or a completed multi-packet message, whose PGN and sender match a receive object updates that object. One whose PGN is in `rx_pgns` is stored in a message slot.
4. The application reads objects with `j1939_rxobj_get()` and messages with `j1939_msg_peek()` / `j1939_msg_pop()`, and decodes signals with `j1939_signal_decode()`.

Transmitting:

1. The application encodes signals into a payload and either writes it to a transmit object with `j1939_txobj_set()` or sends it once with `j1939_send()`.
2. `j1939_process()` sends transmit objects when their period, a change or a Request makes them due, through `j1939_send()`.
3. `j1939_send()` builds a single frame or starts a transport protocol transfer; the frames go into the tx queue.
4. The integrator drains the tx queue to the driver with `j1939_tx_peek()` / `j1939_tx_pop()`.

- **Frames** are the integrator's native type and exist only at the edges: `j1939_rx()` reads one in place, the tx queue holds the frames the stack builds. Everything above works on payloads; the transport protocol splits and joins frames.
- **Signals** (`j1939_signal_t`) are pure codecs over a payload byte array. The stack moves payloads; the application, or a generated service, encodes and decodes them.
- **Message slots or receive objects**: a slot holds every received message of a PGN in `rx_pgns`, in order, until `j1939_msg_pop()`, and the slots can overflow. A receive object holds the latest payload of one PGN from one sender, never overflows and tells whether it is current. State-like PGNs (EEC1, ET1) suit objects; events that must not be missed (commands, Commanded Address, diagnostic traffic) suit slots. A PGN may use both.
- **`j1939_send()` or transmit objects**: `j1939_send()` sends once, now. A transmit object keeps its payload, and the stack sends it periodically, on change and on Request.

## Execution model

- Every function of a stack instance runs in one context, the task or main loop that calls `j1939_process()`. An application whose logic runs in other tasks exchanges data with the stack's task through mailboxes of its own.
- `j1939_rx()` handles a frame during the call and reads it in place; the caller may reuse the frame afterwards. Answers the stack generates go into the tx queue, application messages into the message slots. Standard, remote and non-J1939 frames are ignored, so every frame of a shared bus may be passed.
- `j1939_process()` advances the timers of address claiming, the transport protocol, diagnostics, receive objects and transmit objects, and queues what is due. A timer started by a received frame or an API call between two calls counts from the next call.
- The tx queue is a FIFO over the integrator's `tx_buf`; every slot is usable. A frame stays in it until `j1939_tx_pop()`, so a frame the driver refuses is offered again by the next `j1939_tx_peek()`.
- A received message and its data stay valid in its slot until `j1939_msg_pop()`.
- `j1939_send()` builds a single frame immediately. A payload of 9 to `J1939_CFG_TP_BUF_SIZE` bytes is copied into a free TP transmit buffer and sent with BAM to the global address, with RTS/CTS to a specific one, PDU2 PGNs included; the BAM or RTS frame is queued at once, the data packets by `j1939_process()`. Either way the message and its data may be reused after the call.
- When the message slots or the tx queue are full, the stack drops the message or frame it generated and counts it in `j1939_stats_t`. Application sends report `J1939_RET_ERR_FULL` instead.

## API at a glance

| Direction           | API                                                                                       |
| ------------------- | ----------------------------------------------------------------------------------------- |
| CAN rx              | `j1939_rx()` for each frame taken from the driver                                         |
| Processing          | `j1939_process()` with the elapsed time                                                   |
| CAN tx              | `j1939_tx_peek()` → driver takes the frame → `j1939_tx_pop()`                             |
| Application rx      | `j1939_msg_peek()`, switch on `msg->pgn`, then `j1939_msg_pop()`                          |
| Application tx      | `j1939_send()`, `j1939_request_send()`                                                    |
| Address claim state | `j1939_addr_get()`                                                                        |
| Commanded Address   | `j1939_addr_command_send()`                                                               |
| Diagnostics         | `j1939_dm_active_set()`, `j1939_dm_prev_set()`, `j1939_dm_lamps_set()`; `j1939_dm_clear_get()` → `j1939_dm_clear_confirm()` |
| Receive objects     | `j1939_rxobj_init()`; `j1939_rxobj_get()`, then decode from the object's buffer           |
| Transmit objects    | `j1939_txobj_init()` once after the CAs are added; `j1939_txobj_set()`                    |

## Where to find what

The API reference groups the headers into topics, one per module. The guides, starting with [Addressing](guides/addressing.md), show how to use each feature.

| Header | Content | Main types |
| --- | --- | --- |
| `j1939.h` | Umbrella header, version | |
| `j1939_stack.h` | Stack instance, configuration, frame rx, tx queue, sending, message pull, event counters | `j1939_t`, `j1939_cfg_t`, `j1939_ca_cfg_t`, `j1939_addr_state_t`, `j1939_msg_slot_t`, `j1939_stats_t` |
| `j1939_msg.h` | Logical message, 0–1785 bytes, `data` in integrator memory | `j1939_msg_t` |
| `j1939_addr.h` | Address claiming and Commanded Address (J1939/81) | |
| `j1939_request.h` | Request and Acknowledgement | |
| `j1939_tp.h` | Transport protocol (BAM, RTS/CTS): abort reasons, timers | `j1939_tp_buf_t` |
| `j1939_rxobj.h` | Receive objects | `j1939_rxobj_cfg_t`, `j1939_rxobj_t` |
| `j1939_txobj.h` | Transmit objects | `j1939_txobj_cfg_t`, `j1939_txobj_t` |
| `j1939_signal.h` | Signal (SPN) descriptors, bit extraction, scaling, value ranges | `j1939_signal_t` |
| `j1939_diag.h` | DTC, lamp status and DM1/DM2 payload codec | `j1939_diag_dtc_t`, `j1939_diag_lamps_t` |
| `j1939_dm.h` | Diagnostics of a CA: DM1, DM2, DM3, DM11 | `j1939_dm_cfg_t`, `j1939_dm_t` |
| `j1939_id.h`, `j1939_name.h` | Identifier, PGN and NAME codecs | |
| `j1939_config.h` | Compile-time configuration | |
| `j1939_ret.h` | Return codes of every fallible function | `j1939_ret_t` |
| `j1939_port_contract.h` | What a port's `j1939_target.h` must provide | `j1939_port_frame_t` |
| `j1939_queue.h` | Optional frame queue between two contexts | `j1939_queue_t` |
