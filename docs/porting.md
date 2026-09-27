# Porting

A port binds the library to a CAN driver at compile time.
It consists of a single header, `j1939_target.h`, placed in a port directory that is selected with the `J1939_PORT_DIR` build option.
The library is built against exactly one port.

A port may also ship helper code for moving frames between the driver and the stack's queues, but the library itself never calls into the port at runtime.

## The target header

### Native frame type

The port declares the integrator's own CAN frame type as the library's frame type.
The library stores and reads frames in this type only, in buffers allocated by the integrator.

```c
typedef struct can_frame j1939_port_frame_t; /* SocketCAN */
```

### Frame accessors

All accessors are `static inline` functions.
They hide driver-specific details such as identifier flag bits, the location of the extended-frame indicator or the name of the length field.

| Function                                                                 | Contract                                                             |
| ------------------------------------------------------------------------ | -------------------------------------------------------------------- |
| `bool j1939_port_frame_is_ext(const j1939_port_frame_t *f)`              | `true` if the frame has a 29-bit identifier                         |
| `bool j1939_port_frame_is_rtr(const j1939_port_frame_t *f)`              | `true` if the frame is a remote frame                                |
| `uint32_t j1939_port_frame_id_get(const j1939_port_frame_t *f)`          | The bare 29-bit identifier, without any flag bits                   |
| `uint8_t j1939_port_frame_len_get(const j1939_port_frame_t *f)`          | Payload length, 0–8                                                  |
| `const uint8_t *j1939_port_frame_data(const j1939_port_frame_t *f)`      | Pointer to the payload bytes                                         |
| `void j1939_port_frame_build(j1939_port_frame_t *f, uint32_t id29, const uint8_t *data, uint8_t len)` | Fills a complete 29-bit data frame, including any flags the driver needs |

The library ignores frames for which `is_ext` is `false` or `is_rtr` is `true`.

### Critical sections

```c
#define J1939_PORT_LOCK(ctx)
#define J1939_PORT_UNLOCK(ctx)
```

Needed only when queue access crosses execution contexts (ISR and main loop, or several threads).
Empty definitions are valid for single-context use.

## Moving frames

Receiving, zero copy:

```c
j1939_port_frame_t *slot = j1939_rx_acquire(&stack);
if (slot != NULL) {
	driver_read(slot);
	j1939_rx_commit(&stack);
}
```

Transmitting:

```c
const j1939_port_frame_t *f;
while ((f = j1939_tx_peek(&stack)) != NULL) {
	if (driver_write(f) != DRIVER_OK) {
		break;
	}
	j1939_tx_pop(&stack);
}
```

Processing, called periodically with the time elapsed since the previous call:

```c
j1939_process(&stack, elapsed_us);
```

## Verifying a port

`j1939_port_contract.h` checks at compile time that the target header provides the required API.
The conformance tests in `tests/port/` are built against the port and exercise the accessors: identifier and payload round-trips, extended/standard/remote frame classification and length limits.

## Reference ports

| Port             | Frame type                  | Purpose                                                              |
| ---------------- | --------------------------- | -------------------------------------------------------------------- |
| `port/mock`      | Test-specific struct        | Unit and integration tests. Uses an unusual layout (flags in the upper identifier bits, separate length field) so that tests catch any assumption about frame layout |
| `port/socketcan` | `struct can_frame`          | Linux SocketCAN (CAN_RAW). Includes helpers for reading and writing a raw socket into the integrator's buffers |
