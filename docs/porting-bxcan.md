# Bare-metal port: STM32 bxCAN

This page is a worked example for port authors: a complete bare-metal port for a microcontroller, following the contract in [Porting](porting.md).
A microcontroller port consists of a target header, an rx interrupt, a tx path and a time base.
The bxCAN receive FIFO holds three frames, too few for a main loop to poll, so the rx interrupt hands frames to the main loop through the optional frame queue.
The sketch targets the bxCAN peripheral of STM32F0/F1/F4 parts through the CMSIS device header; it is not compiled in this repository.
The structure carries over to other mailbox controllers.

## Target header

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

## Receiving in the rx interrupt

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

## Transmitting from the main loop

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

## Main loop and time base

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

## Other controllers

| Controller                      | Native frame                                                | Differences to the bxCAN sketch                                                    |
| ------------------------------- | ----------------------------------------------------------- | ---------------------------------------------------------------------------------- |
| STM32 FDCAN (G0, G4, H7), M_CAN | Message RAM element: word R0/T0, word R1/T1, 8 data bytes   | Identifier in bits 28..0 of R0, not shifted; XTD in bit 30, RTR in bit 29; DLC in bits 19..16 of R1. The rx ISR copies the element at the rx FIFO get index and acknowledges it. Tx FIFO mode, not Tx queue mode, keeps frames in order |
| Vendor HAL with a header struct | `struct { HAL_RxHeader hdr; uint8_t data[8]; }`             | The accessors read the header fields (identifier, IDE, RTR, DLC). The HAL receive call writes straight into an acquired slot of the frame queue. The tx path converts the header into the HAL's transmit header |
| Mailbox controllers in general  | The receive mailbox image                                   | Keep the frame layout equal to the hardware's so that the ISR copies without conversion, and make the tx path send in queue order |
