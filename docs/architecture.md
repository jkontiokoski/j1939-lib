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
 j1939_dm (J1939/73): DM1/DM2 transmission, DM3/DM11 handling per CA
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
                        j1939_diag.h            J1939/73 DTC, lamp status and DM1/DM2 payload codec
                        j1939_dm.h              J1939/73 diagnostics of a CA: DM1, DM2, DM3, DM11
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
examples/             example applications on Linux SocketCAN, built with J1939_BUILD_EXAMPLES
                        common/                 clock, Ctrl-C and printing helpers of the examples
                        addr_claim_demo/        address claiming and arbitration between instances
                        pgn_listener/           receives PGNs (single frame and TP), answers a Request
                        bam_sender/             DM1 with BAM, proprietary message with RTS/CTS
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
| Event counters         | `j1939_stats_t`                                  | Messages and frames dropped because integrator storage was full; transport protocol aborts and refusals; diagnostic sends retried or given up |
| DTC                    | `j1939_diag_dtc_t {spn, fmi, oc, cm}`            | J1939/73 diagnostic trouble code; 4-byte codec `j1939_diag_dtc_*()`                   |
| Lamp status            | `j1939_diag_lamps_t`                             | MIL, red stop, amber warning, protect lamp status and flash; 2-byte codec             |
| DM payload codec       | `j1939_diag_dm_build()`, `j1939_diag_dm_parse()` | DM1/DM2 payloads over caller buffers: lamp bytes and a DTC list, up to 1785 bytes     |
| Diagnostic state       | `j1939_dm_t`, configured by `j1939_dm_cfg_t`     | Per CA, integrator storage: copies of the active and previously active DTCs and the lamps; DM1 schedule, change hold records, pending answers and clear requests |
| TP buffer              | `j1939_tp_buf_t`                                 | Integrator storage for one multi-packet message (`J1939_CFG_TP_BUF_SIZE` bytes), for sending or reassembly |
| TP session             | `j1939_tp_session_t`, pool of `J1939_CFG_TP_SESSIONS` in `j1939_t` | One BAM or RTS/CTS transfer in either direction; explicit state machine with its timer |
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
static j1939_tp_buf_t tp_tx_buf[1];                    /* multi-packet sends */
static j1939_tp_buf_t tp_rx_buf[2];                    /* multi-packet reassembly */
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
	.tp_tx_buf = tp_tx_buf, .tp_tx_buf_len = 1,
	.tp_rx_buf = tp_rx_buf, .tp_rx_buf_len = 2,
};
j1939_init(&stack, &cfg);
j1939_ca_add(&stack, &(j1939_ca_cfg_t){.address = 0x80, .name = MY_NAME}, &ca);
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
| Address claim state | `j1939_addr_get(&stack, ca, &address, &state)`                                            | Main loop / task                   |
| Commanded Address   | `j1939_addr_command_send(&stack, ca, name, address, da)`                                  | Main loop / task                   |
| Diagnostics         | `j1939_dm_active_set()`, `j1939_dm_prev_set()`, `j1939_dm_lamps_set()`; `j1939_dm_clear_get()` → `j1939_dm_clear_confirm()` | Main loop / task                   |

- Execution contexts: the frame queue functions and `j1939_msg_peek()` / `j1939_msg_pop()` are protected by the port lock and may run in a context other than `j1939_process()`, one producer and one consumer per queue. Every other stack function (initialisation, `j1939_process()`, sending, and the address and diagnostic calls) reads or changes unlocked stack state: call them all from the context that runs `j1939_process()`, or serialise them with a lock of the integrator's own.
- The rx and tx queues are `j1939_queue_t` instances over the integrator's buffers, reached through `j1939_rx_queue()` and `j1939_tx_queue()`. Each has one producer and one consumer, which may run in different contexts; index updates run inside the port lock, frame contents are written and read outside it. Every slot of the buffer is usable.
- `j1939_process()` handles the frames present in the rx queue when it starts; frames arriving meanwhile wait for the next call. It stores application messages in the message slots and queues frames the stack generates into the tx queue.
- A received message and its data stay valid in its slot until `j1939_msg_pop()`.
- `j1939_send()` builds a single frame immediately. A payload of 9 to `J1939_CFG_TP_BUF_SIZE` bytes is copied into a free TP transmit buffer and sent with BAM to the global address, with RTS/CTS to a specific one, PDU2 PGNs included; the BAM or RTS frame is queued at once, the data packets by `j1939_process()`. Either way the message and its data may be reused after the call.
- When the message slots or the tx queue are full, the stack drops the message or frame it generated and counts it in `j1939_stats_t`. Application sends report `J1939_RET_ERR_FULL` instead.

Transport protocol (J1939/21, `src/j1939_tp.c`):

- Sessions come from one pool of `J1939_CFG_TP_SESSIONS`, shared by both directions. Per pair of addresses there is at most one session per direction: one BAM per sender, one RTS/CTS connection per originator and responder. A second send to the same destination from the same CA returns `J1939_RET_ERR_BUSY`; no free session or transmit buffer returns `J1939_RET_ERR_FULL`.
- Address claiming: a multi-packet send needs a `CLAIMED` CA like a single frame, else `J1939_RET_ERR_NO_ADDRESS`. CTS, EndOfMsgAck and Connection Abort go out only from a claimed address: an RTS to a CA in `CLAIMING` is not answered and the originator times out. BAM reception transmits nothing and is independent of the claim. When a CA loses its address, or moves to another one, the sessions of its old address end without Connection Abort, since the address is no longer the stack's, and are counted in `tp_tx_aborted` / `tp_rx_aborted`.
- Received multi-packet messages reach the application through the message slots like single frames; `msg->data` points into the TP reassembly buffer. The buffer stays reserved until `j1939_msg_pop()` releases the slot. Only the stack's own context changes buffer state: a delivered buffer becomes free when its slot has been released or reused, checked when a new session needs a buffer.
- `rx_pgns` filters the reassembled PGN. An RTS for another PGN is answered with Connection Abort (reason 250), a BAM for it is ignored.
  Commanded Address (PGN 65240) is also received while a CA of the stack accepts commands; the completed message goes to address claiming, and to the application only if listed. Unlisted, it needs no message slot, so the responder never holds for it.
- `msg->prio` of a received message is the priority of the RTS or BAM frame. The originator sends RTS, BAM and data packets with the message priority; CTS, EndOfMsgAck and Connection Abort use priority 7. `msg->da` is the responder's address for RTS/CTS and `J1939_ADDR_GLOBAL` for BAM.
- Timers (T1 750 ms, T2 1250 ms, T3 1250 ms, T4 1050 ms, Tr 200 ms, Th 500 ms) advance only through `elapsed_us` of `j1939_process()`. A timer started by an event counts from the next call, so a timeout expires between its nominal value and one call period later.
- BAM data packets go out one per `j1939_process()` call, `J1939_CFG_TP_BAM_GAP_US` (50–200 ms, default 50 ms) apart at the least. The integrator calls `j1939_process()` often enough to keep the gap under 200 ms.
- The originator sends the packets a CTS requests as far as the tx queue has room, and waits for room at most Tr. It honours CTS with 0 packets (hold, T4) and retransmission requests.
- The responder asks for all remaining packets in one CTS, limited by the RTS's packets-per-CTS value. While all message slots are in use it holds the connection before requesting the packets that complete the message (CTS with 0 packets, repeated every Th, at most `J1939_TP_HOLD_MAX` times, then Connection Abort reason 2).
- Robustness on reception: a packet already received is ignored; a skipped sequence number aborts a connection (reason 7) and drops a BAM; a data packet while holding aborts (reason 6). A repeated RTS for the same PGN from an open originator restarts the connection without an abort; an RTS for another PGN is refused with reason 1 and the open connection continues. A new BAM from a sender replaces its unfinished one. A Connection Abort ends the matching session (same peer and PGN). RTS without free session: reason 1; without buffer, or larger than `J1939_CFG_TP_BUF_SIZE`: reason 2; larger than 1785 bytes: reason 9; inconsistent size and packet count: reason 250.
- A completed RTS/CTS message that finds no free message slot is answered with Connection Abort (reason 2) instead of EndOfMsgAck, so the originator learns that it was lost.
- Frames the stack generates (CTS, EndOfMsgAck, Connection Abort) are dropped and counted in `tx_overflow` when the tx queue is full; the protocol timers then end the session.
- `tp_tx_aborted` and `tp_rx_aborted` count sessions that ended without the message (abort, timeout, sequence error); `tp_rx_refused` counts RTS and BAM refused for lack of a session or buffer.

Receive filtering in `j1939_process()`:

1. Standard (11-bit) frames, remote frames and frames with the extended data page bit set are dropped.
2. Frames addressed to an address none of the stack's CAs holds are dropped; global frames pass. Address Claimed (PGN 60928) is handled by address claiming whatever its destination, and is also delivered to the application if it is in `rx_pgns`.
3. A Request (PGN 59904) is handled by the Request module:
   - a Request for Address Claimed (PGN 60928) is answered by the stack for every CA it concerns and is never delivered;
   - a Request for DM1, DM2, and for DM3 or DM11 where the CA accepts them, is handled by the diagnostics of the CAs it addresses and is not delivered (see Diagnostics);
   - for a PGN in `req_pgns` it is delivered to the application, which reads the PGN with `j1939_request_pgn_get()` and answers with `j1939_send()`;
   - a destination-specific Request for any other PGN is answered by the stack with a NACK (PGN 59392), sent to the global address with the requester in byte 5, as J1939/21 specifies; a CA still in its claim wait sends no NACK;
   - a global Request for any other PGN, and a Request shorter than 3 bytes, are ignored.
4. TP.CM (PGN 60416) and TP.DT (PGN 60160) frames go to the transport protocol. Frames that are not 8 bytes long or come from address 254 or 255 are dropped. An RTS to a CA whose claim is not complete is not answered.
5. Any other PGN in `rx_pgns` is delivered to the application; the rest are dropped.

### Address claiming (J1939/81)

Every CA claims an address before it transmits; `j1939_ca_cfg_t` gives its NAME and preferred address.
The procedure runs inside `j1939_process()`; its only clock is `elapsed_us`.
`j1939_addr_get()` reports the CA's address and `j1939_addr_state_t`, which can change at runtime.

| State          | Address held | May transmit                           | Left by                                                        |
| -------------- | ------------ | -------------------------------------- | -------------------------------------------------------------- |
| `UNCLAIMED`    | No           | Only a Request for Address Claimed     | Address Claimed queued (retried while the tx queue is full)   |
| `CLAIMING`     | Yes          | Only a Request for Address Claimed     | 250 ms without losing the address                              |
| `CLAIMED`      | Yes          | Yes                                    | Losing the address; an accepted Commanded Address              |
| `CANNOT_CLAIM` | No           | Only a Request for Address Claimed     | An accepted Commanded Address                                  |

- Address Claimed (PGN 60928) is sent to the global address from the claimed address with the NAME as data; Cannot Claim is the same message from the NULL address 254.
- Transmission start: a CA claiming an address in 0–127 or 248–253 goes to `CLAIMED` as soon as its Address Claimed is queued. A CA claiming a self-configurable address (128–247) stays in `CLAIMING` for 250 ms after queuing it, so that a contending CA can answer first.
- An Address Claimed from another node for an address a CA holds is arbitrated on the NAME; the numerically lower NAME wins. The winner sends its Address Claimed again. The loser, if its NAME is arbitrary address capable, claims the lowest self-configurable address that no other node has claimed and no CA of the stack uses; otherwise, or if none is free, it goes to `CANNOT_CLAIM`.
- A CA in `CANNOT_CLAIM` sends Cannot Claim after a pseudo-random delay of 0–153 ms (0–255 steps of 0.6 ms), on entering the state and on every Request for Address Claimed that concerns it. The step count is the XOR of the NAME's eight bytes, so no random source is needed and CAs with different NAMEs usually differ.
- A Request for Address Claimed to the global address or to a held address is answered for every CA it concerns: Address Claimed from `CLAIMING` or `CLAIMED`, Cannot Claim from `CANNOT_CLAIM`. A CA in `UNCLAIMED` sends its claim with the next `j1939_process()` anyway.
- `j1939_request_send()` for PGN 60928 works in every state and uses the NULL address unless the CA is `CLAIMED`. The stack answers such a Request for its own CAs too, as J1939/21 requires of a node that sends a global Request.
- Each stack records the self-configurable addresses that other nodes have claimed (a 120-bit table in `j1939_t`) to choose a free address. Entries are never cleared: J1939/81 has no message that releases an address.
- An Address Claimed carrying a CA's own NAME is ignored, so a driver that loops back transmitted frames does no harm.
- A corrupted claim state sends the CA to `CANNOT_CLAIM`.
- Losing an address, to arbitration, to a Commanded Address or through a corrupted claim state, ends the transport protocol sessions of that address without Connection Abort.
- Not implemented: retrying a claim after `CANNOT_CLAIM` other than by Commanded Address.

Commanded Address (PGN 65240, 9 bytes: the target's NAME, then the new address) moves a CA to another address.
It travels with the transport protocol: BAM to the global address, or RTS/CTS to the target's address, which then needs a `CLAIMED` target.

- Accepting is opt-in per CA: `j1939_ca_cfg_t::accept_commanded`, false by default. Commands for a CA that refuses, for a NAME the stack does not have, or with the new address 254 or 255 are ignored and nothing is sent. The stack receives the message only while one of its CAs accepts commands.
- The reception is recorded in the CA and applied by the next `j1939_process()`, so that the transport protocol session carrying it ends first: an RTS/CTS command is acknowledged with EndOfMsgAck from the old address, then Address Claimed goes out from the new one.
- The CA gives up its old address, from any claim state, as when it loses it, and claims the new one with the normal rules: immediately `CLAIMED` outside 128–247, 250 ms in `CLAIMING` inside; arbitration on the NAME if another node claims the address; a pending Cannot Claim is dropped. A command during the contention wait abandons the address being claimed.
- A command to the address the CA holds repeats its Address Claimed without restarting the wait. A command to an address another CA of the stack uses is ignored.
- The new address is not written back to the configuration. The application reads it with `j1939_addr_get()` and may keep it as the preferred address, e.g. in non-volatile memory.
- Commanded Address messages are also delivered to the application if PGN 65240 is in `rx_pgns`, whether or not a CA acts on them. A listed command that finds no free message slot is lost as a whole and not acted on.
- A corrupted claim state drops a pending command as it sends the CA to `CANNOT_CLAIM`.
- `j1939_addr_command_send()` sends a command from a `CLAIMED` CA, as `j1939_send()` does, and rejects the new addresses 254 and 255.

### Configuration

- `include/j1939/j1939_config.h` defines the defaults: `J1939_CFG_TP_SESSIONS`, `J1939_CFG_TP_BUF_SIZE` (largest multi-packet message, 9–1785 bytes), `J1939_CFG_TP_BAM_GAP_US`, `J1939_CFG_CA_MAX` and module enables (`J1939_CFG_TP_ENABLE`, `J1939_CFG_DIAG_ENABLE`, ...).
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

### Diagnostics (J1939/73)

`j1939_diag` is a pure codec; it holds no state and does not send.
A received payload is decoded in place from the message slot with `j1939_diag_dm_parse()` into caller-supplied lamp and DTC storage.

- DTCs are encoded with SPN conversion method 0 (the version 4 layout) only; a DTC with CM set is rejected.
  On decode a set CM bit is reported in `cm` and the SPN is read at its version 4 position, unconverted: versions 1–3 cannot be told apart from the message.
- No DTCs are sent as the lamp bytes, the all-zero DTC and two 0xFF bytes. One DTC is padded to eight bytes with 0xFF. Two or more DTCs give 2 + 4n bytes, sent with the transport protocol.
- The parser accepts 2 + 4n bytes (n ≥ 1) and the padded eight-byte form. A single DTC with SPN 0 and FMI 0 means no DTCs. For the builder that DTC is reserved and rejected in a list.
- Malformed payloads return `J1939_RET_ERR_ARG`; a too small output buffer or DTC array returns `J1939_RET_ERR_FULL`, and the parser then reports the number of DTCs the payload holds.

`j1939_dm` (`src/j1939_dm.c`) transmits the diagnostic state of a CA.
The application owns its fault memory (detection, occurrence counts, the move of a DTC from active to previously active) and copies the result into the stack; the stack never calls out.

```c
static j1939_diag_dtc_t active[8], prev[8];
static j1939_dm_hold_t hold[4];
static uint8_t dm_buf[J1939_DM_BUF_LEN(8)];
static j1939_dm_t dm;

const j1939_dm_cfg_t dm_cfg = {
	.active = active, .active_len = 8,
	.prev = prev, .prev_len = 8,
	.hold = hold, .hold_len = 4,
	.buf = dm_buf, .buf_len = sizeof(dm_buf),
	.dm3_enable = false, .dm11_enable = true,
};
j1939_dm_init(&stack, ca, &dm, &dm_cfg);
j1939_dm_active_set(&stack, ca, dtcs, n);   /* whenever the fault set changes */
```

- `j1939_dm_active_set()`, `j1939_dm_prev_set()` and `j1939_dm_lamps_set()` validate and copy; the lists keep the application's order. A DTC is identified by SPN and FMI; duplicates are rejected.
- DM1 goes to the global address once per second (`J1939_DM1_PERIOD_US`), also without DTCs. The first DM1 goes out in the `j1939_process()` call in which the CA's claim completes; the period counts from the next call and keeps its phase across late calls. Before the claim, or after the CA lost its address, nothing is sent.
- On a change of the active set (a DTC new or gone) a DM1 goes out with the next `j1939_process()`, besides the periodic ones. J1939/73 recommends at most one reported state change per DTC per second: a DTC whose change triggered a DM1 holds for one second (`j1939_dm_hold_t` records, fresh records count from the next call) and a further change of it triggers nothing; its new state goes out with the next DM1. A change of another DTC still triggers. While every record is held, a change triggers nothing and waits for the periodic DM1, so at most `hold_len` change-triggered DM1s go out per second. Occurrence count and lamp changes trigger nothing.
- A Request for DM1 or DM2, global or to the CA's address, is answered by the stack: DM1 with the active list, DM2 with the previously active list. Both are PDU2 PGNs, so a single frame answer goes to the global address. A multi-packet answer goes with BAM to the global address for a global Request and with RTS/CTS to the requester for a destination specific one, as J1939/21 specifies. Requests of several nodes while an answer is pending are answered once, with BAM. The periodic and change-triggered DM1 always use the global address.
- Sends that find the tx queue full or the CA's broadcast busy are retried with every call and counted in `dm_tx_retry`. A DM2 answer or acknowledgement not sent within `J1939_DM_RESPONSE_US` (Tr, 200 ms), or pending when the CA loses its address, is given up and counted in `dm_tx_dropped`; so is a periodic DM1 still unsent when the next one is due. A BAM of many DTCs can outlast a period: size the lists for what the bus carries.
- DM3 (clear previously active DTCs) and DM11 (clear active DTCs) are opt-in per CA (`dm3_enable`, `dm11_enable`). Without the option the Request is handled as any unsupported PGN: NACK when destination specific, delivered if the PGN is in `req_pgns`.
- Clearing is the application's decision. The stack records the request and reports it with `j1939_dm_clear_get()`; `j1939_dm_clear_confirm()` accepts or refuses it within `J1939_DM_RESPONSE_US` after the first `j1939_process()` following the request, otherwise it is refused. On acceptance the stack empties its copy of the concerned list (DM3: previously active, DM11: active; the other list stays), the application clears its own records before the next `j1939_process()`, which acknowledges (ACK) a destination specific request. A refused destination specific request is answered with NACK. A global request is never acknowledged. A further request from another requester while one is in progress is answered with Cannot Respond; a global one is covered by it. Faults still present are set again by the application and reported as new.
  Reasoning: clearing erases evidence of faults, which in a safety-rated system is a decision of the application's fault management (operating state, access rights, non-volatile memory), not of the protocol layer. The pull style keeps the library free of callbacks, and the time limit keeps a request that the application does not handle from staying open.
- Not implemented: DM1 on several networks; lamp changes as a DM1 trigger; the OBD rule that DM11 is accepted only globally.

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
- Timers are tested by passing the elapsed time to `j1939_process()`, for example one call 1 µs before and one at a deadline.
- `test_addr_command` drives Commanded Address on one stack with injected BAM and RTS/CTS transfers; `test_addr_command_exchange` lets a tool node move a target with BAM and with RTS/CTS while a monitor node watches the Address Claimed messages.
- `test_dm` checks the DM1 period at its boundaries, change triggers and their per-DTC hold, 0, 1 and several DTCs (BAM), Request answers (single frame, BAM, RTS/CTS to the requester), the DM3/DM11 decisions and timeouts, claim gating, full tx queue and busy broadcast; `test_dm_exchange` has a diagnostic tool node parse DM1 (single frame and BAM), request DM2 (RTS/CTS and BAM) and clear with DM11.
- `test_tp` drives the transport protocol of one stack with injected peer frames and checks every sent frame and timer boundary; `test_tp_exchange` runs BAM and RTS/CTS transfers of up to 1785 bytes between three stacks, with a lost packet, aborts, concurrent sessions and exhausted reassembly memory.
- Host test builds run with AddressSanitizer and UndefinedBehaviorSanitizer.
- Coverage with gcov/gcovr; target ≥ 90 % line coverage on the protocol core, branch coverage reported. Defensive checks against states the design rules out remain as uncovered branches.

### Static analysis deviations

The MISRA checks apply to the library core (`src/`, `include/`) and the mock port.
The SocketCAN port and the example applications are operating system glue built on POSIX interfaces; they get the general cppcheck checks only.
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
| `J1939_BUILD_EXAMPLES` | OFF                 | Builds the example applications (Linux only) against a SocketCAN build of the library, whatever `J1939_PORT_DIR` selects |

clangd finds the compilation database of the `make` build in `build/` without further configuration.

Make targets:

| Target              | Action                                                                   |
| ------------------- | ------------------------------------------------------------------------ |
| `make`              | Debug build in `build/`                                                  |
| `make test`         | Builds with sanitizers in `build-test/` and runs `ctest`                 |
| `make coverage`     | Builds with coverage in `build-coverage/`, runs tests, fails under 90 % line coverage |
| `make cross`        | Compiles the library for Cortex-M0+ in `build-arm/`                      |
| `make examples`     | Builds the SocketCAN example applications in `build-examples/`           |
| `make lint`         | cppcheck: core and mock port with the MISRA addon, SocketCAN port and examples with the general checks |
| `make format`       | Formats all project sources                                              |
| `make format-check` | Fails if any project source is not formatted                             |
| `make clean`        | Removes all build directories                                            |

### Versioning and version control

- Semantic versioning, exposed as `J1939_VERSION_MAJOR`, `J1939_VERSION_MINOR`, `J1939_VERSION_PATCH` in `j1939.h`.
- One branch per task, named after the feature or module (`stack-core`, `tp-bam`). Documentation is updated in the same commit as the code it describes.
- Work reaches `main` only through a pull request from its feature branch.
- A feature branch is kept current by rebasing it onto `main` and force-pushing it with `--force-with-lease`. `main` itself is never rewritten.
- Tasks that do not depend on each other are developed in parallel, each in its own git worktree and branch.
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
