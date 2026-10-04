# Getting started

This page takes an integrator from an empty project to a running J1939 node: adding the library to a CMake build, setting up a stack and running its main loop.
It assumes a port for your CAN driver; the library ships one for Linux SocketCAN, and [Porting](porting.md) shows how to write one.

## Requirements

- A C99 compiler and CMake 3.21 or newer.
- A port: a directory with a `j1939_target.h` that adapts your CAN driver's frame type, see [Porting](porting.md).

## Add the library to a CMake project

The library builds the static library `j1939::j1939` against the port that `J1939_PORT_DIR` names; set it before adding the library (a relative path is resolved against the top-level source directory).
As a subproject, the library builds no tests, examples or documentation.

From a copy inside the project, such as an extracted source archive or a git submodule:

```cmake
set(J1939_PORT_DIR "${CMAKE_SOURCE_DIR}/j1939_port")
add_subdirectory(third_party/j1939-lib)
target_link_libraries(app PRIVATE j1939::j1939)
```

With `FetchContent` from a release's source archive, checked against its `SHA256SUMS`:

```cmake
include(FetchContent)
FetchContent_Declare(j1939
	URL https://github.com/jkontiokoski/j1939-lib/releases/download/vX.Y.Z/j1939-lib-X.Y.Z.tar.gz
	URL_HASH SHA256=<checksum from SHA256SUMS>)
set(J1939_PORT_DIR "${CMAKE_SOURCE_DIR}/j1939_port")
FetchContent_MakeAvailable(j1939)
target_link_libraries(app PRIVATE j1939::j1939)
```

To use a port shipped with the library, point at the fetched sources:

```cmake
set(J1939_PORT_DIR "${FETCHCONTENT_BASE_DIR}/j1939-src/port/socketcan")
```

- The optional frame queue is the target `j1939::queue`; it is compiled only when linked.
- Pass a configuration header ([Configuration](configuration.md)) to the library target with `PUBLIC` visibility, so that the application sees the same type layouts as the library:

  ```cmake
  target_compile_definitions(j1939 PUBLIC J1939_CONFIG_FILE="my_j1939_config.h")
  target_include_directories(j1939 PUBLIC "${CMAKE_SOURCE_DIR}/config")
  ```

## Set up a stack

One `j1939_t` runs one CAN bus.
The application allocates its memory and hands it over at initialisation, then adds a Controller Application (CA) for each J1939 function the node performs:

```c
#include "j1939/j1939.h"

static j1939_port_frame_t tx_buf[16];
static j1939_msg_slot_t msg_buf[8];
static j1939_tp_buf_t tp_tx_buf[1];                    /* multi-packet sends */
static j1939_tp_buf_t tp_rx_buf[2];                    /* multi-packet reassembly */
static const uint32_t rx_pgns[] = {0xFF20U, 0xE800U};  /* delivered to the application */
static const uint32_t req_pgns[] = {0xFF22U};          /* answered on Request by the application */
static j1939_t stack;
static j1939_ca_id_t ca;

const j1939_cfg_t cfg = {
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

`j1939_init()` rejects an invalid configuration without changing the stack, for example a PGN above 0x3FFFF or a PDU1 PGN whose lowest byte is not 0.
[Configuration](configuration.md) explains how to size each buffer.

<a id="gs-main-loop"></a>
## Run the main loop

Every integration runs the same cycle in one task or main loop:

1. Pass the received frames to `j1939_rx()`.
2. Call `j1939_process()` with the time since the previous call, read from a monotonic clock.
3. Read application messages with `j1939_msg_peek()` and `j1939_msg_pop()`, send with `j1939_send()`, check the claim with `j1939_addr_get()`.
4. Move the tx queue to the driver with `j1939_tx_peek()` and `j1939_tx_pop()`.

```c
j1939_port_frame_t frame;
const j1939_port_frame_t *f;
const j1939_msg_t *msg;

while (driver_read(&frame) == DRIVER_OK) {
	(void)j1939_rx(&stack, &frame);  /* handled during the call, read in place */
}
(void)j1939_process(&stack, elapsed_us);
while ((msg = j1939_msg_peek(&stack)) != NULL) {
	/* switch (msg->pgn) ... */
	(void)j1939_msg_pop(&stack);
}
while ((f = j1939_tx_peek(&stack)) != NULL) {
	if (driver_write(f) != DRIVER_OK) {
		break; /* The frame stays queued for the next attempt. */
	}
	(void)j1939_tx_pop(&stack);
}
```

- The stack has no lock. The driver's own FIFO (a socket, an RTOS queue, a hardware FIFO) carries frames from interrupts or threads to this loop; a driver without one can use the optional frame queue, see [Porting](porting.md).
- Frames of other protocols on the same bus may be passed to `j1939_rx()`; it ignores them.
- The cycle period is the resolution of the stack's timers. While the stack broadcasts with BAM it sends one data packet per call, at most 200 ms apart, so call it at least every 200 ms; a 10 ms period keeps the packets 50 to 60 ms apart.
- A wrapping 32-bit microsecond counter works: the unsigned difference of two readings is correct while calls are less than 71 minutes apart.

## Try it on Linux

The [example applications](examples.md) run this loop in complete programs on a virtual SocketCAN interface; that page shows how to set the interface up and what to watch on the bus.

## Next steps

- The guides cover the features one task at a time: [Addressing](guides/addressing.md), [Messages](guides/messages.md), [Message objects](guides/message-objects.md), [Signals](guides/signals.md) and [Diagnostics](guides/diagnostics.md).
- [Concepts](concepts.md) explains the design behind the loop above.
