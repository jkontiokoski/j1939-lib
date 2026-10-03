# Porting

A port binds the library to a CAN driver at compile time.
It is a directory containing a header named `j1939_target.h`, selected with the `J1939_PORT_DIR` CMake option.
The library is built against exactly one port.

The library never calls into the port at runtime except through the `static inline` functions of the target header.
A port may ship helper code that moves frames between the driver and the stack, see `port/socketcan/j1939_socketcan.c`.

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

The library includes the target header only through `include/j1939/j1939_port_contract.h`, which repeats every required declaration.
A definition with a different signature fails with "conflicting types"; a missing definition fails with "declared static but never defined".

## Moving frames

Every function of a stack instance runs in one execution context, the stack's task or main loop.
The stack has no lock: the CAN driver's own frame FIFOs (a socket, an RTOS message queue, a hardware FIFO) carry frames between the interrupts or threads of the driver and that context.

Receiving: the stack's context takes each frame from the driver and passes it to `j1939_rx()`, which handles it during the call and reads it in place.
Frames of other protocols on the same bus may be passed as well, or filtered out before.

```c
j1939_port_frame_t frame;
while (driver_read(&frame) == DRIVER_OK) {
	(void)j1939_rx(&stack, &frame);
}
```

Transmitting: the stack queues the frames it generates in its tx queue, over the integrator's `tx_buf`.
The stack's context moves them to the driver with `j1939_tx_peek()` and `j1939_tx_pop()`:

```c
const j1939_port_frame_t *f;
while ((f = j1939_tx_peek(&stack)) != NULL) {
	if (driver_write(f) != DRIVER_OK) {
		break; /* The frame stays queued for the next attempt. */
	}
	(void)j1939_tx_pop(&stack);
}
```

A driver without a frame FIFO of its own, typically on bare metal, can use the optional frame queue between its interrupt and the stack's context, see [Optional frame queue](#optional-frame-queue).

## The main loop

Every integration runs the same cycle, in one task or the main loop:

1. Pass the received frames to `j1939_rx()`.
2. Call `j1939_process(&stack, elapsed_us)` with the time since the previous call, read from a monotonic clock.
3. Read application messages with `j1939_msg_peek()` and `j1939_msg_pop()`, send with `j1939_send()`, check the claim with `j1939_addr_get()`.
4. Move the tx queue to the driver.

The stack's timers advance only through `elapsed_us`, so the cycle period is their resolution.
BAM data packets are sent one per call and at most 200 ms apart, so the stack needs a call at least every 200 ms while it broadcasts; a period of 10 ms keeps the gap between 50 and 60 ms.
A wrapping 32-bit microsecond counter is fine: the unsigned difference of two readings is the elapsed time as long as calls are less than 71 minutes apart.

Sizing the buffers:

- The driver's receive FIFO: the frames that can arrive during one cycle. As a responder the stack asks for all remaining packets of an RTS/CTS transfer in one CTS, up to the originator's packets-per-CTS limit, and they arrive back to back: at 250 kbit/s about 1800 frames per second, so up to 255 frames in 140 ms. A packet the driver loses ends the connection with Connection Abort (bad sequence number).
- tx queue (`tx_buf`): the largest burst the stack generates in one cycle, a CTS window of data packets when it sends with RTS/CTS. Packets that do not fit are sent as the queue drains, within Tr (200 ms).
- TP buffers: `j1939_tp_buf_t` takes `J1939_CFG_TP_BUF_SIZE` bytes plus a few bytes of bookkeeping. A microcontroller that never handles 1785 byte messages lowers it with `-DJ1939_CONFIG_FILE`.

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
The bare-metal sketch below uses the queue between the receive interrupt and the main loop.

## Verifying a port

1. Implement the three fixture functions of `tests/port/j1939_port_fixture.h` in the port's `j1939_port_fixture.c` and name it in `port.cmake`.
   They build the frames the port API cannot: a standard frame, a remote frame and a raw DLC above 8.
2. Configure with `-DJ1939_PORT_DIR=<port directory> -DJ1939_BUILD_TESTS=ON`, build, and run `ctest`.
   The test `test_port_configured` runs the conformance suite against the port.

## Reference ports

| Port             | Frame type                  | Purpose                                                              |
| ---------------- | --------------------------- | -------------------------------------------------------------------- |
| `port/mock`      | `struct j1939_mock_frame`   | Unit tests. Layout of a bxCAN style mailbox: identifier left-aligned in a 32-bit word with IDE and RTR flags in the low bits, raw 4-bit DLC. It differs from SocketCAN so that tests catch any layout assumption. A lock for the optional frame queue that records nesting depth and call count |
| `port/socketcan` | `struct can_frame`          | Linux SocketCAN (CAN_RAW). Uses `can_dlc` so that kernel headers before 5.11 work. No lock: the socket is the frame FIFO. `j1939_socketcan.h` opens a non-blocking socket that receives extended data frames only, and moves frames between the socket and the stack |

### Mock port

`port/mock/j1939_target.h` shows that the frame type is the driver's, not the library's.
Its frame is the image of a bxCAN receive mailbox: the identifier register holds the 29-bit identifier in bits 31..3, IDE in bit 2 and RTR in bit 1, and the DLC is kept raw, so values 9 to 15 occur.
The accessors translate:

```c
static inline uint32_t j1939_port_frame_id_get(const j1939_port_frame_t *f) {
	uint32_t id;

	if (j1939_port_frame_is_ext(f)) {
		id = (f->ir >> J1939_MOCK_IR_EXID_SHIFT) & J1939_MOCK_EXID_MASK;
	} else {
		id = (f->ir >> J1939_MOCK_IR_STID_SHIFT) & J1939_MOCK_STID_MASK;
	}
	return id;
}

static inline uint8_t j1939_port_frame_len_get(const j1939_port_frame_t *f) {
	uint8_t len = (uint8_t)(f->dlc & J1939_MOCK_DLC_MASK);

	if (len > J1939_MOCK_DATA_MAX) {
		len = J1939_MOCK_DATA_MAX;
	}
	return len;
}
```

The mock port also defines the lock of the optional frame queue.
It has no concurrency to guard: it records the nesting depth and the number of calls, which the queue's unit tests check.

### SocketCAN port

`port/socketcan` is the port the example applications use.

- `j1939_target.h`: `typedef struct can_frame j1939_port_frame_t;` and accessors over `can_id` (`CAN_EFF_FLAG`, `CAN_RTR_FLAG`, `CAN_EFF_MASK`) and `can_dlc`. No lock: the kernel's socket buffer is the frame FIFO between the CAN driver and the stack's thread.
- `port.cmake`: compiles `j1939_socketcan.c` into the library.
- `j1939_socketcan.h`: `j1939_socketcan_open()` returns a non-blocking CAN_RAW socket with a kernel filter for extended data frames. `j1939_socketcan_rx()` reads frames and passes each to `j1939_rx()`, until the socket is empty or a given number of frames has been read; the rest wait in the socket. `j1939_socketcan_tx()` writes the tx queue until it is empty or the socket reports `EAGAIN` or `ENOBUFS`.

The main loop of the examples:

```c
uint64_t last_us = now_us(); /* CLOCK_MONOTONIC */

while (running) {
	struct pollfd pfd = {.fd = fd, .events = POLLIN, .revents = 0};
	if (j1939_tx_peek(&stack) != NULL) {
		pfd.events = (short)(POLLIN | POLLOUT); /* Frames wait for the socket. */
	}
	(void)poll(&pfd, 1, 10); /* One 10 ms tick at most. */

	(void)j1939_socketcan_rx(fd, &stack, 256U, NULL); /* Socket -> stack. */
	uint64_t now = now_us();
	(void)j1939_process(&stack, (uint32_t)(now - last_us));
	last_us = now;

	while ((msg = j1939_msg_peek(&stack)) != NULL) {
		/* switch (msg->pgn) ... */
		(void)j1939_msg_pop(&stack);
	}

	(void)j1939_socketcan_tx(fd, &stack, NULL); /* tx queue -> socket. */
}
```

A virtual interface stands in for a CAN bus:

```sh
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
```

The loopback test `test_socketcan_vcan` then runs as part of `make test`.

### Example applications

The examples in `examples/` run on a SocketCAN interface, `vcan0` unless `-i` names another.
Build them with `make examples` (or `-DJ1939_BUILD_EXAMPLES=ON`); the binaries are in `build-examples/examples/`.
Watch the traffic with `candump -ta vcan0` in another shell.

| Example           | Shows                                                                                              |
| ----------------- | -------------------------------------------------------------------------------------------------- |
| `addr_claim_demo` | Address claiming: the CA's state changes and the claims on the bus. Several instances show arbitration |
| `pgn_listener`    | Receiving the PGNs given on the command line, single frame and reassembled from BAM or RTS/CTS; DM1 decoding; answering a Request for Software Identification (PGN 65242) through `req_pgns` |
| `bam_sender`      | A DM1 with four DTCs built with `j1939_diag_dm_build()`, sent with BAM; a 100 byte Proprietary A message (PGN 0xEF00) sent with RTS/CTS to the address given with `-d` |

Arbitration, three instances that prefer address 0x80:

```sh
./addr_claim_demo -n 1 &       # keeps 0x80: lowest NAME
./addr_claim_demo -n 2 -A &    # arbitrary address capable: moves to 0x81
./addr_claim_demo -n 3         # not arbitrary address capable: Cannot Claim
```

```
 (1790525816.054137)  vcan0  18EEFF80   [8]  01 00 E0 FF 00 80 00 00   # -n 1 claims 0x80
 (1790525816.855334)  vcan0  18EEFF80   [8]  02 00 E0 FF 00 80 00 80   # -n 2 -A claims 0x80
 (1790525816.855407)  vcan0  18EEFF80   [8]  01 00 E0 FF 00 80 00 00   # lower NAME defends
 (1790525816.855446)  vcan0  18EEFF81   [8]  02 00 E0 FF 00 80 00 80   # loser moves to 0x81
 (1790525817.656380)  vcan0  18EEFF80   [8]  03 00 E0 FF 00 80 00 00   # -n 3 claims 0x80
 (1790525817.656438)  vcan0  18EEFF80   [8]  01 00 E0 FF 00 80 00 00   # lower NAME defends
 (1790525817.758302)  vcan0  18EEFFFE   [8]  03 00 E0 FF 00 80 00 00   # Cannot Claim after 102 ms
```

The first instance prints:

```
[   0.000] NAME 0x00008000FFE00001, preferred address 0x80 on vcan0
[   0.010] state UNCLAIMED    -> CLAIMING     address 0x80
[   0.264] state CLAIMING     -> CLAIMED      address 0x80
[   0.811] Address Claimed from 0x80, NAME 0x80008000FFE00002
[   0.811] Address Claimed from 0x81, NAME 0x80008000FFE00002
```

0x80 is a self-configurable address, so a CA waits 250 ms in `CLAIMING` before it transmits.
The Cannot Claim delay comes from the NAME, here 156 steps of 0.6 ms, plus up to one main loop tick.

Transport protocol, a listener at 0x90 and one round of the sender at 0x80:

```sh
./pgn_listener -a 0x90 0xFECA 0xEF00 &
./bam_sender -a 0x80 -d 0x90 -c 1
```

```
 (1790526240.800564)  vcan0  18ECFF80   [8]  20 12 00 03 FF CA FE 00   # BAM: DM1, 18 bytes, 3 packets
 (1790526240.800577)  vcan0  18EC9080   [8]  10 64 00 0F FF 00 EF 00   # RTS: 100 bytes, 15 packets
 (1790526240.800603)  vcan0  1CEC8090   [8]  11 0F 01 FF FF 00 EF 00   # CTS: 15 packets from 1
 (1790526240.800653)  vcan0  18EB9080   [8]  01 01 02 03 04 05 06 07
 (1790526240.800658)  vcan0  18EB9080   [8]  02 08 09 0A 0B 0C 0D 0E
 ...
 (1790526240.800694)  vcan0  18EB9080   [8]  0F 63 64 FF FF FF FF FF
 (1790526240.800789)  vcan0  1CEC8090   [8]  13 64 00 0F FF 00 EF 00   # EndOfMsgAck
 (1790526240.851184)  vcan0  18EBFF80   [8]  01 04 FF 64 00 01 03 6E   # BAM packets 50.3-50.6 ms apart
 (1790526240.901531)  vcan0  18EBFF80   [8]  02 00 00 01 BE 00 02 07
 (1790526240.951874)  vcan0  18EBFF80   [8]  03 00 F0 FF 01 FF FF FF
```

The listener prints the reassembled messages and decodes the DM1:

```
[   0.915] PGN 0x0FECA (65226) prio 6 SA 0x80 DA 0xFF len 18
            04 FF 64 00 01 03 6E 00 00 01 BE 00 02 07 00 F0
            FF 01
           DM1: MIL 0 red 0 amber 1 protect 0, 4 DTC(s)
           SPN 100 FMI 1 OC 3
           SPN 110 FMI 0 OC 1
           SPN 190 FMI 2 OC 7
           SPN 520192 FMI 31 OC 1
```

Requests to the listener take both paths: a PGN in `req_pgns` is delivered and answered by the application (Software Identification, 17 bytes, so a BAM); any other PGN is NACKed by the stack.

```sh
cansend vcan0 18EA90F9#DAFE00  # Request from 0xF9 for PGN 0xFEDA
cansend vcan0 18EA90F9#00EF00  # Request from 0xF9 for PGN 0xEF00
```

```
 (1790526242.556673)  vcan0  18EA90F9   [3]  DA FE 00
 (1790526242.556738)  vcan0  18ECFF90   [8]  20 11 00 03 FF DA FE 00   # BAM: 17 bytes
 (1790526242.617184)  vcan0  18EBFF90   [8]  01 01 6A 31 39 33 39 2D   # 1 field, "j1939-lib 0.1.0*"
 (1790526242.667576)  vcan0  18EBFF90   [8]  02 6C 69 62 20 30 2E 31
 (1790526242.717961)  vcan0  18EBFF90   [8]  03 2E 30 2A FF FF FF FF
 (1790526243.058263)  vcan0  18EA90F9   [3]  00 EF 00
 (1790526243.058299)  vcan0  18E8FF90   [8]  01 FF FF FF F9 00 EF 00   # NACK to global, requester 0xF9
```

Without a node at the destination the RTS goes unanswered. After T3 (1.25 s) the sender sends Connection Abort with reason 3 (timeout) and counts the transfer in `tp_tx_aborted`:

```
 (1790526012.226336)  vcan0  18EC5510   [8]  10 64 00 0F FF 00 EF 00   # RTS to 0x55
 (1790526013.494589)  vcan0  1CEC5510   [8]  FF 03 FF FF FF 00 EF 00   # Connection Abort: timeout
```

## Sketch: bare-metal port for STM32 bxCAN

A microcontroller port consists of a target header, an rx interrupt, a tx path and a time base.
The bxCAN receive FIFO holds three frames, too few for a main loop to poll, so the rx interrupt hands frames to the main loop through the optional frame queue.
The sketch targets the bxCAN peripheral of STM32F0/F1/F4 parts through the CMSIS device header; it is not compiled in this repository.
The structure carries over to other mailbox controllers.

### Target header

The native frame is the image of a bxCAN mailbox, the layout the mock port also uses: the rx ISR copies the mailbox registers into a queue slot and the tx path copies a frame into a transmit mailbox, without conversion.
The lock is there for the frame queue.

```c
/* port/stm32_bxcan/j1939_target.h */
#ifndef J1939_TARGET_H
#define J1939_TARGET_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "stm32f4xx.h" /* CMSIS: registers, __get_PRIMASK(), __disable_irq() */

#define BXCAN_IR_IDE       0x4U /* RIR/TIR: identifier extension */
#define BXCAN_IR_RTR       0x2U /* RIR/TIR: remote frame */
#define BXCAN_IR_EXID_POS  3U   /* RIR/TIR: 29-bit identifier in bits 31..3 */
#define BXCAN_IR_STID_POS  21U  /* RIR/TIR: 11-bit identifier in bits 31..21 */
#define BXCAN_DTR_DLC_MASK 0xFU /* RDTR/TDTR: DLC in bits 3..0 */

typedef struct bxcan_frame {
	uint32_t ir;     /* RIR / TIR */
	uint32_t dtr;    /* RDTR / TDTR */
	uint8_t data[8]; /* RDLR, RDHR, least significant byte first */
} j1939_port_frame_t;

/* Lock of the frame queue: saved PRIMASK. On a single core the lock disables
 * every interrupt, so the ISR and the main loop cannot overwrite each other's
 * saved value. */
typedef uint32_t j1939_port_lock_t;

static inline bool j1939_port_frame_is_ext(const j1939_port_frame_t *f) {
	return (f->ir & BXCAN_IR_IDE) != 0U;
}

static inline bool j1939_port_frame_is_rtr(const j1939_port_frame_t *f) {
	return (f->ir & BXCAN_IR_RTR) != 0U;
}

static inline uint32_t j1939_port_frame_id_get(const j1939_port_frame_t *f) {
	return j1939_port_frame_is_ext(f) ? (f->ir >> BXCAN_IR_EXID_POS)
	                                  : (f->ir >> BXCAN_IR_STID_POS);
}

static inline uint8_t j1939_port_frame_len_get(const j1939_port_frame_t *f) {
	uint8_t len = (uint8_t)(f->dtr & BXCAN_DTR_DLC_MASK);

	return (len > 8U) ? 8U : len;
}

static inline const uint8_t *j1939_port_frame_data(const j1939_port_frame_t *f) {
	return f->data;
}

static inline void j1939_port_frame_build(j1939_port_frame_t *f, uint32_t id29,
                                          const uint8_t *data, uint8_t len) {
	uint8_t n = (len > 8U) ? 8U : len;

	(void)memset(f, 0, sizeof(*f));
	f->ir = ((id29 & 0x1FFFFFFFU) << BXCAN_IR_EXID_POS) | BXCAN_IR_IDE;
	f->dtr = n;
	if ((data != NULL) && (n > 0U)) {
		(void)memcpy(f->data, data, n);
	}
}

static inline void j1939_port_lock_init(j1939_port_lock_t *lock) {
	*lock = 0U;
}

/* The CMSIS intrinsics are compiler barriers. */
static inline void j1939_port_lock(j1939_port_lock_t *lock) {
	uint32_t primask = __get_PRIMASK();

	__disable_irq();
	*lock = primask;
}

static inline void j1939_port_unlock(j1939_port_lock_t *lock) {
	__set_PRIMASK(*lock);
}

#endif /* J1939_TARGET_H */
```

Instead of disabling every interrupt, the lock may mask only the CAN interrupts (`NVIC_DisableIRQ()` followed by `__DSB()` and `__ISB()`) and leave unrelated interrupts running; the lock object then saves which of them were enabled.

### Receiving in the rx interrupt

The rx FIFO interrupt is the frame queue's only producer, the main loop its only consumer.
The interrupt copies each pending mailbox into a queue slot and commits it.
The queue functions take the lock inside the ISR as well, which is harmless: nothing else runs meanwhile.

```c
#include "j1939/j1939_queue.h"

static j1939_port_frame_t rx_storage[64];
static j1939_queue_t rx_q; /* j1939_queue_init(&rx_q, rx_storage, 64) in board_init() */
static volatile uint32_t rx_dropped;

void CAN1_RX0_IRQHandler(void) {
	j1939_queue_t *q = &rx_q;
	uint32_t n;

	/* Bounded: the hardware FIFO holds three frames. */
	for (n = 0U; (n < 3U) && ((CAN1->RF0R & CAN_RF0R_FMP0) != 0U); n++) {
		j1939_port_frame_t *f = j1939_queue_acquire(q);

		if (f != NULL) {
			uint32_t lo = CAN1->sFIFOMailBox[0].RDLR;
			uint32_t hi = CAN1->sFIFOMailBox[0].RDHR;

			f->ir = CAN1->sFIFOMailBox[0].RIR;
			f->dtr = CAN1->sFIFOMailBox[0].RDTR;
			(void)memcpy(&f->data[0], &lo, 4U); /* Cortex-M is little endian. */
			(void)memcpy(&f->data[4], &hi, 4U);
			(void)j1939_queue_commit(q);
		} else {
			rx_dropped++; /* Queue full: drop, as a hardware overrun would. */
		}
		CAN1->RF0R = CAN_RF0R_RFOM0; /* Release the FIFO output mailbox. */
	}
}
```

The FIFO output mailbox is released even when the queue is full; otherwise the interrupt fires again at once.
If the bus carries 11-bit traffic, acceptance filters that pass extended frames only save interrupt load; the library drops such frames anyway.

### Transmitting from the main loop

The stack's tx queue belongs to the main loop like every other stack call.
The main loop fills the free transmit mailboxes; the transmit mailbox empty interrupt only acknowledges and wakes the loop, which then fills the mailboxes again.

```c
extern j1939_t stack;

static void tx_fill(void) {
	const j1939_port_frame_t *f;

	/* Bounded by the three transmit mailboxes. */
	while (((CAN1->TSR & CAN_TSR_TME) != 0U) && ((f = j1939_tx_peek(&stack)) != NULL)) {
		uint32_t box = (CAN1->TSR & CAN_TSR_CODE) >> CAN_TSR_CODE_Pos;
		uint32_t lo;
		uint32_t hi;

		(void)memcpy(&lo, &f->data[0], 4U);
		(void)memcpy(&hi, &f->data[4], 4U);
		CAN1->sTxMailBox[box].TDTR = f->dtr & BXCAN_DTR_DLC_MASK;
		CAN1->sTxMailBox[box].TDLR = lo;
		CAN1->sTxMailBox[box].TDHR = hi;
		CAN1->sTxMailBox[box].TIR = f->ir | CAN_TI0R_TXRQ; /* Request transmission. */
		(void)j1939_tx_pop(&stack);
	}
}

void CAN1_TX_IRQHandler(void) {
	CAN1->TSR = CAN_TSR_RQCP0 | CAN_TSR_RQCP1 | CAN_TSR_RQCP2; /* Acknowledge; wakes the loop. */
}
```

Set `CAN_MCR_TXFP` during initialisation so that the three mailboxes transmit in request order.
By default bxCAN sends the mailbox with the highest priority identifier first and, for equal identifiers, the lowest mailbox number: transport protocol data packets share one identifier and would leave out of sequence.
Enable the interrupts with `CAN_IER_FMPIE0` and `CAN_IER_TMEIE`.

To refill the mailboxes from the interrupt instead, the main loop moves the stack's frames into a second frame queue, which the tx interrupt drains.

### Main loop and time base

A free-running 32-bit timer at 1 MHz (TIM2 on most STM32 parts) gives `elapsed_us` by unsigned subtraction, which handles the wrap.

```c
int main(void) {
	uint32_t last;

	board_init(); /* clocks, CAN bit timing, filters, TXFP, interrupts, TIM2, tick */
	(void)j1939_init(&stack, &cfg);
	(void)j1939_ca_add(&stack, &ca_cfg, &ca);
	last = TIM2->CNT;

	for (;;) {
		uint32_t now = TIM2->CNT;
		const j1939_port_frame_t *f;
		const j1939_msg_t *msg;

		while ((f = j1939_queue_peek(&rx_q)) != NULL) {
			(void)j1939_rx(&stack, f);
			(void)j1939_queue_pop(&rx_q);
		}
		(void)j1939_process(&stack, now - last);
		last = now;

		while ((msg = j1939_msg_peek(&stack)) != NULL) {
			app_handle(msg);
			(void)j1939_msg_pop(&stack);
		}
		app_send(); /* j1939_send() of the messages that are due */

		tx_fill();
		__WFI(); /* An rx, tx or tick interrupt wakes the loop. */
	}
}
```

The CAN interrupts wake the loop when frames arrive; a periodic tick of about 10 ms keeps the stack's timers and the BAM pacing running on a quiet bus.
Build the library with the ARM toolchain and `-DJ1939_PORT_DIR=port/stm32_bxcan`, link `j1939::queue` as well, and lower `J1939_CFG_TP_BUF_SIZE` and the number of TP buffers to fit the RAM.

### Other controllers

| Controller                      | Native frame                                                | Differences to the bxCAN sketch                                                    |
| ------------------------------- | ----------------------------------------------------------- | ---------------------------------------------------------------------------------- |
| STM32 FDCAN (G0, G4, H7), M_CAN | Message RAM element: word R0/T0, word R1/T1, 8 data bytes   | Identifier in bits 28..0 of R0, not shifted; XTD in bit 30, RTR in bit 29; DLC in bits 19..16 of R1. The rx ISR copies the element at the rx FIFO get index and acknowledges it. Tx FIFO mode, not Tx queue mode, keeps frames in order |
| Vendor HAL with a header struct | `struct { HAL_RxHeader hdr; uint8_t data[8]; }`             | The accessors read the header fields (identifier, IDE, RTR, DLC). The HAL receive call writes straight into an acquired slot of the frame queue. The tx path converts the header into the HAL's transmit header |
| Mailbox controllers in general  | The receive mailbox image                                   | Keep the frame layout equal to the hardware's so that the ISR copies without conversion, and make the tx path send in queue order |
