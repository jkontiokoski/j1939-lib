# Module rules

Detailed rules of the protocol modules, for integrators who need the exact behaviour: transport protocol, receive filtering, address claiming, signals, diagnostics and message objects.
How to use the library is described in [Getting started](getting-started.md) and [Concepts](concepts.md).

## Transport protocol (J1939/21)

- Sessions come from one pool of `J1939_CFG_TP_SESSIONS`, shared by both directions. Per pair of addresses there is at most one session per direction: one BAM per sender, one RTS/CTS connection per originator and responder. A second send to the same destination from the same CA returns `J1939_RET_ERR_BUSY`; no free session or transmit buffer returns `J1939_RET_ERR_FULL`.
- Address claiming: a multi-packet send needs a `CLAIMED` CA like a single frame, else `J1939_RET_ERR_NO_ADDRESS`. CTS, EndOfMsgAck and Connection Abort go out only from a claimed address: an RTS to a CA in `CLAIMING` is not answered and the originator times out. BAM reception transmits nothing and is independent of the claim. When a CA loses its address, or moves to another one, the sessions of its old address end without Connection Abort, since the address is no longer the stack's, and are counted in `tp_tx_aborted` / `tp_rx_aborted`.
- Received multi-packet messages reach the application through the message slots like single frames; `msg->data` points into the TP reassembly buffer. The buffer stays reserved until `j1939_msg_pop()` releases the slot. Only the stack's own context changes buffer state: a delivered buffer becomes free when its slot has been released or reused, checked when a new session needs a buffer.
- `rx_pgns` and the receive objects filter the reassembled PGN: a PGN in `rx_pgns`, or one a receive object takes from the sender, is received. An RTS for another PGN is answered with Connection Abort (reason 250), a BAM for it is ignored.
  A completed message updates its receive object whether or not a message slot is free. Unlisted in `rx_pgns`, it needs no message slot, so the responder never holds for it.
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

## Receive filtering

In `j1939_rx()`:

1. Standard (11-bit) frames, remote frames and frames with the extended data page bit set are dropped.
2. Frames addressed to an address none of the stack's CAs holds are dropped; global frames pass. Address Claimed (PGN 60928) is handled by address claiming whatever its destination, and is also delivered to the application if it is in `rx_pgns`.
3. A Request (PGN 59904) is handled by the Request module:
   - a Request for Address Claimed (PGN 60928) is answered by the stack for every CA it concerns and is never delivered;
   - a Request for DM1, DM2, and for DM3 or DM11 where the CA accepts them, is handled by the diagnostics of the CAs it addresses and is not delivered (see Diagnostics);
   - a Request for the PGN of a transmit object of a CA it addresses is answered by the stack and is not delivered (see Message objects);
   - for a PGN in `req_pgns` it is delivered to the application, which reads the PGN with `j1939_request_pgn_get()` and answers with `j1939_send()`;
   - a destination-specific Request for any other PGN is answered by the stack with a NACK (PGN 59392), sent to the global address with the requester in byte 5, as J1939/21 specifies; a CA still in its claim wait sends no NACK;
   - a global Request for any other PGN, and a Request shorter than 3 bytes, are ignored.
4. TP.CM (PGN 60416) and TP.DT (PGN 60160) frames go to the transport protocol. Frames that are not 8 bytes long or come from address 254 or 255 are dropped. An RTS to a CA whose claim is not complete is not answered.
5. Any other PGN is stored in the receive object for its PGN and source address, if there is one, and delivered to the application if it is in `rx_pgns`; the rest are dropped.

## Address claiming (J1939/81)

Every CA claims an address before it transmits; `j1939_ca_cfg_t` gives its NAME and preferred address.
Its timers run inside `j1939_process()`; their only clock is `elapsed_us`. A wait started by a received frame or an API call counts from the next call.
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

## Signals (J1939DA)

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

## Diagnostics (J1939/73)

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

## Message objects

Message objects keep the protocol behaviour of application data in the stack: receive objects hold the latest payload of a PGN from one sender and supervise its timeout, transmit objects hold a payload that `j1939_process()` sends periodically, on change and on Request.

- Object tables are `const` arrays, e.g. generated, indexed by handles the generator names. Every buffer is exactly as large as the integrator declares it.
- The application writes a transmit object's payload only through `j1939_txobj_set()`, so change detection sees every update.

### Receive objects

```c
static uint8_t eec1_buf[8];
static const j1939_rxobj_cfg_t rx_cfg[] = {
	/* buf, pgn, timeout_us, buf_len, min_len, sa */
	{eec1_buf, 0xF004U, 100000U, 8U, 8U, 0x00U},
};
static j1939_rxobj_t rx_obj[1];

j1939_rxobj_init(&stack, rx_cfg, rx_obj, 1);

j1939_rxobj_status_t st;
if ((j1939_rxobj_get(&stack, 0, &st) == J1939_RET_OK) && (st.state == J1939_RXOBJ_VALID)) {
	j1939_signal_decode(&sig_engine_speed, eec1_buf, st.len, &rpm, &cls);
}
```

- An object takes one PGN from one source address (0–253). It sees what passes the stack's destination filter: global messages and those addressed to an address a CA of the stack holds.
- `j1939_rxobj_init()` validates the whole table before changing the stack: a buffer, a valid PGN that the stack does not handle itself (Request, Address Claimed, TP.CM, TP.DT), `1 ≤ min_len ≤ buf_len ≤ J1939_CFG_TP_BUF_SIZE` and no two objects with the same PGN and source address. Every object starts in `J1939_RXOBJ_NO_DATA`. It may be called again to reset or replace the table; `j1939_init()` removes it.
- A matching single frame, or a completed transport protocol message, is copied into the object's buffer, its age is reset, and the object becomes `J1939_RXOBJ_VALID` with `updated` set. A message in `rx_pgns` is also delivered through the message slots; the object is updated even when no slot is free.
- A payload shorter than `min_len` or longer than `buf_len` is rejected and counted in `rxobj_rejected`. The buffer keeps the previous payload and the age keeps running, so a malformed message never hides a timeout.
- The age advances with `elapsed_us` of `j1939_process()`, from the call after the reception, and saturates. At `timeout_us` the object becomes `J1939_RXOBJ_TIMEOUT`, counted once per timeout in `rxobj_timeout`; the buffer keeps the last payload. `timeout_us` 0 disables supervision.
- `j1939_rxobj_get()` reports state, length, age and whether a payload arrived since the previous call, and clears that flag. The application decodes in place from its buffer: the buffer changes only during `j1939_rx()` and `j1939_process()`, in the stack's own context.
- Memory: the state is 12 bytes per object on 32-bit and 64-bit hosts, plus the payload buffer; the configuration is `const`.
- Senders are identified by source address. When a node loses its address, another node may claim it and its messages reach the same object.

### Transmit objects

The integrator describes the transmitted PGNs in a `const` table, typically generated, and supplies a state array of the same length and a payload buffer per object:

```c
static uint8_t eec1_buf[8], ident_buf[20];
static const j1939_txobj_cfg_t txobj_cfg[] = {
	{.buf = eec1_buf, .pgn = 0xF004U, .period_us = 20000U, .len = 8U, .prio = 3U,
	 .da = J1939_ADDR_GLOBAL, .ca = 0U},
	{.buf = ident_buf, .pgn = 0xFF22U, .len = 20U, .prio = 6U,     /* on Request only */
	 .da = J1939_ADDR_GLOBAL, .ca = 0U},
};
static j1939_txobj_t txobj[2];

j1939_txobj_init(&stack, txobj_cfg, txobj, 2U);   /* after j1939_ca_add() */
j1939_txobj_set(&stack, 0U, payload, 8U);         /* whenever the values change */
```

| Field         | Content                                                                                         |
| ------------- | ----------------------------------------------------------------------------------------------- |
| `buf`, `len`  | Payload buffer of exactly `len` bytes (1 to `J1939_CFG_TP_BUF_SIZE`); written only by the stack |
| `pgn`, `prio` | PGN and priority                                                                                |
| `da`          | `J1939_ADDR_GLOBAL` or a node; a single frame PDU2 PGN needs the global address                 |
| `period_us`   | Transmission period; 0 for no periodic transmission                                             |
| `inhibit_us`  | 0 for no change trigger; otherwise the least time from the object's last send to a change-triggered send |
| `ca`          | Sending CA                                                                                      |

- `j1939_txobj_init()` validates the whole table before it changes the stack: known CA, valid PGN, priority and destination, buffer present, length within the transport protocol buffer and above 8 only with a TP transmit buffer, no PGN the stack handles itself (Request, Acknowledgement, TP.CM, TP.DT, Address Claimed, Commanded Address, DM1, DM2, DM3, DM11) or that the application answers (`req_pgns`), no two entries with the same CA, PGN and destination. It fills every payload buffer with 0xFF, "not available" for every parameter, which is what goes out until the first `j1939_txobj_set()`. Called again it replaces or resets the objects; a length of 0 removes them.
- `j1939_txobj_set()` copies the payload; its length must equal `len`. With `inhibit_us` > 0 a payload that differs from the current one is a change.
- Nothing is sent before the CA has claimed its address. Periodic objects go out in the `j1939_process()` call in which the claim completes and then once per period, counted from the next call; a late call sends once and keeps the phase. Objects with a change trigger also go out on the claim.
- A change goes out with the next `j1939_process()` once `inhibit_us` has passed since the object's last send, otherwise as soon as it has. A periodic send carries the latest payload and serves a pending change; change-triggered sends do not move the periodic phase. With both `period_us` and `inhibit_us` 0 an object is sent only on Request.
- A Request for an object's PGN, global or to the address of its CA, is answered with the current payload and not delivered. A single frame of a PDU2 PGN goes to the global address, other answers to the requester; a multi-packet answer goes with BAM for a global Request and with RTS/CTS to the requester otherwise. Requests of several nodes while an answer is pending are answered once, to the global address. A periodic or change-triggered send to the global address serves a pending answer that would go to the global address too. When a CA has several objects of the PGN, the first in the table answers. A Request to a CA still in its claim wait is neither answered nor refused; a destination specific Request to a CA without such an object is answered with NACK.
- Payloads above 8 bytes go with the transport protocol through `j1939_send()`, so they share the CA's BAM and RTS/CTS sessions and the TP transmit buffers with other sends.
- Sends that find the tx queue full or the transport protocol busy are retried with every call and counted in `txobj_tx_retry`. A periodic send still unsent when the next one is due, an answer not sent within `J1939_TXOBJ_RESPONSE_US` (Tr, 200 ms), and answers pending when the CA loses its address are counted in `txobj_tx_dropped`. After losing its address a CA's objects stop; they start again as above with the next claim.
- Memory: an object costs its payload buffer plus a 16-byte state (three timers, the requester, flag bits) in RAM, and its `const` configuration in flash. Multi-packet objects add no transport protocol memory of their own.
