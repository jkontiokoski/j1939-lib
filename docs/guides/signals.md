# Signals

This guide shows how to turn PGN payloads into engineering values and back: describing signals (SPNs) in a table, decoding received payloads, encoding payloads to send, and handling the J1939/71 indicator values.
It is for integrators; the bit layout, scaling and value-range rules are in the API reference, topic *Signals*.

## Where the definitions come from

The J1939 Digital Annex (J1939DA), which defines the standard PGNs and SPNs, is copyrighted by SAE and is not part of the library.
The library provides the engine: the descriptor type `j1939_signal_t` and the functions that use it.
Write a `const` table of descriptors from your licensed copy of the DA, or generate it from a DBC file or your own database.

The table in `examples/signals/` shows the format with invented signals in the Proprietary B range (0xFF00–0xFFFF); it contains no DA definitions.

## Describe a signal

A descriptor is plain data, translated from the DA entry of the SPN:

```c
static const j1939_signal_t sig_pump_speed = {
	.spn = 520192U,
	.pgn = 0xFF20U,
	.start = J1939_SIGNAL_POS(1U, 1U),   /* DA start position "1.1": byte 1, bit 1 */
	.bits = 16U,
	.type = J1939_SIGNAL_TYPE_CONTINUOUS,
	.res_num = 1U, .res_den = 8U,        /* 0.125 rpm/bit */
	.offset = 0,
	.unit = "rpm", .name = "Pump speed",
};
```

| DA entry | Descriptor field |
| --- | --- |
| Start position "B.b" | `start = J1939_SIGNAL_POS(B, b)` |
| Length | `bits` |
| Resolution, e.g. 0.125 rpm/bit | `res_num` / `res_den` in the integer unit you choose, here 1/8 rpm |
| Offset | `offset`, in the same unit |
| Data range with indicator values (1, 2 or 4 bytes) | `J1939_SIGNAL_TYPE_CONTINUOUS` |
| 2-bit status | `J1939_SIGNAL_TYPE_DISCRETE` |
| Anything else (counters, bit fields, proprietary data) | `J1939_SIGNAL_TYPE_PLAIN` |

The library has no floating point: values are 64-bit integers in the unit you choose.
The unit sets the precision; the same 0.125 rpm/bit signal decodes to whole rpm with `1/8`, or exactly to millirpm with `125/1`.
Validate each table once at start-up, or in a unit test, with `j1939_signal_check()`.

## Decode a received payload

```c
int64_t rpm;
j1939_signal_class_t cls;

if ((j1939_signal_decode(&sig_pump_speed, data, len, &rpm, &cls) == J1939_RET_OK) &&
    (cls == J1939_SIGNAL_VALID)) {
	/* use rpm */
}
```

The class tells whether the raw value was a valid signal or an indicator:

| Class | Meaning | Typical handling |
| --- | --- | --- |
| `J1939_SIGNAL_VALID` | A measured value | Use it |
| `J1939_SIGNAL_NOT_AVAILABLE` | The sender does not provide it | Treat as missing, no fault |
| `J1939_SIGNAL_ERROR` | The sender detected a fault in it | Treat as faulty |
| `J1939_SIGNAL_PARAM_SPECIFIC`, `J1939_SIGNAL_RESERVED` | Other indicator values | Treat as missing |

`j1939_signal_msg_decode()` does the same directly on a message from a message slot and also checks that the PGNs match.
For a receive object, decode from the object's buffer, see [Message objects](message-objects.md).

## Encode a payload to send

Start from a payload filled with 0xFF, so that every parameter you do not set reads as "not available", then encode each value:

```c
uint8_t payload[8];

(void)memset(payload, 0xFF, sizeof(payload));
if (j1939_signal_encode(&sig_pump_speed, payload, sizeof(payload), rpm) != J1939_RET_OK) {
	/* out of range: send "error" instead of a wrong value */
	(void)j1939_signal_indicator_set(&sig_pump_speed, payload, sizeof(payload), J1939_SIGNAL_ERROR);
}
```

Encoding leaves every other bit of the payload unchanged, so signals sharing a payload can be written one after another.
It rejects values that would fall into the indicator range; write indicators deliberately with `j1939_signal_indicator_set()`.

## Reference

API reference, topic *Signals*: `j1939_signal_t`, `j1939_signal_decode()`, `j1939_signal_encode()`, `j1939_signal_msg_decode()`, `j1939_signal_indicator_set()`, `j1939_signal_check()`.
