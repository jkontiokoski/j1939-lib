# Porting

A port binds the library to a CAN driver at compile time.
It is a directory containing a header named `j1939_target.h`, selected with the `J1939_PORT_DIR` CMake option.
The library is built against exactly one port.

The library never calls into the port at runtime except through the `static inline` functions of the target header.
A port may ship helper code that moves frames between the driver and the library's queues, see `port/socketcan/j1939_socketcan.c`.

## Port directory

| File                   | Required | Content                                                                  |
| ---------------------- | -------- | ------------------------------------------------------------------------ |
| `j1939_target.h`       | Yes      | Native frame type, frame accessors, lock type and lock functions        |
| `port.cmake`           | No       | Port helper sources, libraries the port needs, conformance test fixture |
| `j1939_port_fixture.c` | No       | Fixture for the port conformance test                                   |

`port.cmake` is included by the build and may set:

| Variable                    | Content                                                               |
| --------------------------- | --------------------------------------------------------------------- |
| `J1939_PORT_SOURCES`        | Helper sources compiled into the library                              |
| `J1939_PORT_LINK_LIBRARIES` | Libraries linked to the library, e.g. `Threads::Threads`              |
| `J1939_PORT_TEST_FIXTURE`   | Path of the source implementing `tests/port/j1939_port_fixture.h`     |

## The target header

### Native frame type

The port declares the integrator's own CAN frame type as the library's frame type.
The library stores and reads frames in this type only, in buffers allocated by the integrator.
The type must be copyable by assignment.

```c
typedef struct can_frame j1939_port_frame_t; /* SocketCAN */
```

### Frame accessors

All accessors are `static inline` functions.
They hide driver-specific details such as identifier flag bits, the position of the identifier in a register-like word or the name of the length field.

| Function                                                                 | Contract                                                             |
| ------------------------------------------------------------------------ | -------------------------------------------------------------------- |
| `bool j1939_port_frame_is_ext(const j1939_port_frame_t *f)`              | `true` if the frame has a 29-bit identifier                         |
| `bool j1939_port_frame_is_rtr(const j1939_port_frame_t *f)`              | `true` if the frame is a remote frame                                |
| `uint32_t j1939_port_frame_id_get(const j1939_port_frame_t *f)`          | The bare identifier without flag bits (29 bits for extended frames) |
| `uint8_t j1939_port_frame_len_get(const j1939_port_frame_t *f)`          | Payload length, 0–8; raw DLC values above 8 read as 8                |
| `const uint8_t *j1939_port_frame_data(const j1939_port_frame_t *f)`      | Pointer to the payload bytes                                         |
| `void j1939_port_frame_build(j1939_port_frame_t *f, uint32_t id29, const uint8_t *data, uint8_t len)` | Overwrites every field of `f` with an extended data frame. Identifier bits above bit 28 are ignored, `len` above 8 is treated as 8, `data` may be `NULL` when `len` is 0 |

The library ignores frames for which `is_ext` is `false` or `is_rtr` is `true`.

### Lock

```c
typedef pthread_mutex_t j1939_port_lock_t; /* SocketCAN */

static inline void j1939_port_lock_init(j1939_port_lock_t *lock);
static inline void j1939_port_lock(j1939_port_lock_t *lock);
static inline void j1939_port_unlock(j1939_port_lock_t *lock);
```

Every library queue embeds one lock object and holds it only while updating its indices.
Critical sections are short and never nested.

- Lock and unlock must act as compiler and memory barriers, so that a frame written before a queue update is visible to the other execution context.
- On a single-core microcontroller where an ISR produces into the rx queue, disabling that interrupt (or all interrupts) is sufficient.
- A port without concurrency may use any scalar type and empty functions.

### Contract check

The library includes the target header only through `include/j1939/j1939_port_contract.h`, which repeats every required declaration.
A definition with a different signature fails with "conflicting types"; a missing definition fails with "declared static but never defined".

## Moving frames

Receiving, zero copy:

```c
j1939_queue_t *rx_q = j1939_rx_queue(&stack);
j1939_port_frame_t *slot = j1939_queue_acquire(rx_q);
if (slot != NULL) {
	driver_read(slot);
	j1939_queue_commit(rx_q);
}
```

Transmitting:

```c
j1939_queue_t *tx_q = j1939_tx_queue(&stack);
const j1939_port_frame_t *f;
while ((f = j1939_queue_peek(tx_q)) != NULL) {
	if (driver_write(f) != DRIVER_OK) {
		break;
	}
	j1939_queue_pop(tx_q);
}
```

A full rx queue leaves frames in the driver; a failed write leaves the frame in the tx queue.

## Verifying a port

1. Implement the three fixture functions of `tests/port/j1939_port_fixture.h` in the port's `j1939_port_fixture.c` and name it in `port.cmake`.
   They build the frames the port API cannot: a standard frame, a remote frame and a raw DLC above 8.
2. Configure with `-DJ1939_PORT_DIR=<port directory> -DJ1939_BUILD_TESTS=ON`, build, and run `ctest`.
   The test `test_port_configured` runs the conformance suite against the port.

## Reference ports

| Port             | Frame type                  | Purpose                                                              |
| ---------------- | --------------------------- | -------------------------------------------------------------------- |
| `port/mock`      | `struct j1939_mock_frame`   | Unit tests. Layout of a bxCAN style mailbox: identifier left-aligned in a 32-bit word with IDE and RTR flags in the low bits, raw 4-bit DLC. It differs from SocketCAN so that tests catch any layout assumption. The lock records nesting depth and call count |
| `port/socketcan` | `struct can_frame`          | Linux SocketCAN (CAN_RAW). Uses `can_dlc` so that kernel headers before 5.11 work. Pthread mutex lock. `j1939_socketcan.h` opens a non-blocking socket that receives extended data frames only, and moves frames between the socket and library queues |

### SocketCAN on a virtual interface

```sh
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
```

The loopback test `test_socketcan_vcan` then runs as part of `make test`.
