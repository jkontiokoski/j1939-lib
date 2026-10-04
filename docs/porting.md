# Porting

This page is for port authors: the contract between the library and a CAN driver, and the ports the library ships.
A port is a directory with a header named `j1939_target.h`, selected with the `J1939_PORT_DIR` CMake option ([Getting started](getting-started.md)); the library is built against exactly one port.
At runtime the library reaches the driver only through the `static inline` functions of that header; moving frames between driver and stack is the application's or a port helper's job (see `port/socketcan/j1939_socketcan.c`).

## Port directory

| File                   | Required | Content                                                                  |
| ---------------------- | -------- | ------------------------------------------------------------------------ |
| `j1939_target.h`       | Yes      | Native frame type and frame accessors; the lock if the optional frame queue is used |
| `port.cmake`           | No       | Port helper sources, libraries the port needs, conformance test fixture |
| `j1939_port_fixture.c` | No       | Fixture for the port conformance test                                   |

`port.cmake` is included by the build and may set:

| Variable                    | Content                                                               |
| --------------------------- | --------------------------------------------------------------------- |
| `J1939_PORT_SOURCES`        | Helper sources compiled into the library                              |
| `J1939_PORT_LINK_LIBRARIES` | Libraries linked to the library, e.g. a vendor driver library         |
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

### Contract check

The library includes the target header only through `j1939_port_contract.h`, which repeats every required declaration; its page in the API reference documents each function.
A definition with a different signature fails with "conflicting types"; a missing definition fails with "declared static but never defined".

## Optional frame queue

`j1939_queue.h` is a helper the stack does not use.
It is a first-in first-out queue of native frames over integrator storage for one producer and one consumer in different execution contexts, typically a receive interrupt and the stack's main loop.
Integrators whose driver already has a frame FIFO do not need it.

It is built as its own library target, which only exists in a build that links it:

```cmake
target_link_libraries(my_app PRIVATE j1939::j1939 j1939::queue)
```

The queue needs a lock in the port's `j1939_target.h`, in addition to the frame accessors:

```c
typedef uint32_t j1939_port_lock_t; /* e.g. saved PRIMASK */

static inline void j1939_port_lock_init(j1939_port_lock_t *lock);
static inline void j1939_port_lock(j1939_port_lock_t *lock);
static inline void j1939_port_unlock(j1939_port_lock_t *lock);
```

- Each queue embeds one lock object and holds it only while updating its indices. Critical sections are short and never nested.
- Lock and unlock must act as compiler and memory barriers, so that a frame written before a queue update is visible to the other execution context.
- On a single-core microcontroller where an interrupt is the producer, disabling that interrupt (or all interrupts) is sufficient.

The producer writes a frame in place (`j1939_queue_acquire()`, then `j1939_queue_commit()`) or copies one in (`j1939_queue_put()`); the consumer reads it in place with `j1939_queue_peek()` and releases it with `j1939_queue_pop()`.
A full queue returns no slot; the producer drops the frame or leaves it in the hardware.
The [STM32 bxCAN sketch](porting-bxcan.md) uses the queue between the receive interrupt and the main loop.

## Verifying a port

1. Implement the three fixture functions of `tests/port/j1939_port_fixture.h` in the port's `j1939_port_fixture.c` and name it in `port.cmake`.
   They build the frames the port API cannot: a standard frame, a remote frame and a raw DLC above 8.
2. Configure with `-DJ1939_PORT_DIR=<port directory> -DJ1939_BUILD_TESTS=ON`, build, and run `ctest`.
   The test `test_port_configured` runs the conformance suite against the port.

## Reference ports

| Port             | Frame type                | Lock | Use                                   |
| ---------------- | ------------------------- | ---- | ------------------------------------- |
| `port/mock`      | `struct j1939_mock_frame` | Yes  | Unit tests                            |
| `port/socketcan` | `struct can_frame`        | No   | Linux SocketCAN, the example programs |

### Mock port

The mock port serves the library's own tests. Its frame is the image of a bxCAN receive mailbox, deliberately unlike SocketCAN's layout, so that the tests catch any assumption about the frame layout; its lock records nesting and call counts for the frame queue tests.

### SocketCAN port

- `j1939_target.h`: `typedef struct can_frame j1939_port_frame_t;` and accessors over `can_id` (`CAN_EFF_FLAG`, `CAN_RTR_FLAG`, `CAN_EFF_MASK`) and `can_dlc`, which kernel headers before 5.11 also have. No lock: the socket buffer is the frame FIFO between the driver and the stack's thread.
- `port.cmake`: compiles `j1939_socketcan.c` into the library.
- `j1939_socketcan.h`: `j1939_socketcan_open()` returns a non-blocking CAN_RAW socket with a kernel filter for extended data frames. `j1939_socketcan_rx()` reads frames and passes each to `j1939_rx()`, until the socket is empty or a given number of frames has been read; the rest wait in the socket. `j1939_socketcan_tx()` writes the tx queue until it is empty or the socket reports `EAGAIN` or `ENOBUFS`.

The example programs run the main loop of [Getting started](getting-started.md#gs-main-loop) with these helpers: `poll()` waits at most one 10 ms tick, for received frames or, while the tx queue holds frames, for room in the socket. `examples/common/` holds the shared code.

With a virtual interface `vcan0` up ([Example applications](examples.md) shows how), the loopback test `test_socketcan_vcan` runs as part of `make test`.

## Further reading

- [STM32 bxCAN sketch](porting-bxcan.md): a complete bare-metal port with the frame queue and notes on other controllers.
- [Example applications](examples.md): the SocketCAN port in complete programs.
