# Messages

This guide shows how the application sends and receives J1939 messages directly: sending with `j1939_send()`, receiving through the message slots, answering and sending Requests, and multi-packet messages.
It is for integrators; for PGNs sent periodically or supervised for timeouts, see [Message objects](message-objects.md).

## Send a message

A message is a PGN, a priority, a destination and a payload; the source address is the CA's own.

```c
const uint8_t data[8] = {0x01U, 0x02U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU};
const j1939_msg_t msg = {
	.pgn = 0xEF00U,             /* Proprietary A, PDU1 */
	.prio = 6U,
	.da = 0x90U,                /* J1939_ADDR_GLOBAL for PDU2 PGNs */
	.len = sizeof(data),
	.data = data,
};

switch (j1939_send(&stack, ca, &msg)) {
case J1939_RET_OK:
	break;
case J1939_RET_ERR_FULL:        /* tx queue or transport protocol resources full: retry later */
case J1939_RET_ERR_BUSY:        /* a multi-packet send to this destination is in progress */
case J1939_RET_ERR_NO_ADDRESS:  /* the CA has not claimed an address yet */
default:
	break;
}
```

- The message and its data may be reused right after the call: a single frame is built at once, a longer payload is copied.
- Fill unused bytes of PGNs of 8 bytes or less with 0xFF, "not available".

## Receive messages

List the PGNs the application wants in `rx_pgns` of `j1939_cfg_t`.
Each received message of a listed PGN, addressed to a CA of the stack or to all nodes, waits in a message slot until the application releases it:

```c
const j1939_msg_t *msg;

while ((msg = j1939_msg_peek(&stack)) != NULL) {
	switch (msg->pgn) {
	case 0xFEF1U:               /* msg->sa, msg->len and msg->data describe the message */
		break;
	default:
		break;
	}
	(void)j1939_msg_pop(&stack);
}
```

- A message and its data stay valid until `j1939_msg_pop()`.
- When every slot is in use, new messages are dropped and counted in `j1939_stats_t::rx_msg_overflow`. Size `msg_len` for the longest time between two reads, see [Configuration](../configuration.md).
- Frames the stack handles itself, such as transport protocol frames and the Requests it answers, never reach the slots. Address Claimed and Commanded Address reach them too when listed.

## Answer a Request

PGNs the application answers itself go into `req_pgns`.
A Request for one of them arrives as a message with `pgn` `J1939_PGN_REQUEST`:

```c
uint32_t requested;

if ((msg->pgn == J1939_PGN_REQUEST) && (j1939_request_pgn_get(msg, &requested) == J1939_RET_OK) &&
    (requested == 0xFEEBU)) {
	const j1939_msg_t answer = {.pgn = 0xFEEBU, .prio = 6U, .da = J1939_ADDR_GLOBAL,
	                            .len = ident_len, .data = ident};
	(void)j1939_send(&stack, ca, &answer);
}
```

Answer to the global address for a PDU2 PGN; for a PDU1 PGN requested by one node, answer to `msg->sa`.
Requests for PGNs that neither the application nor the stack supports are answered by the stack with a NACK when they were addressed to this node.
PGNs sent by a transmit object are answered by the stack and must not be in `req_pgns`.

## Send a Request

```c
(void)j1939_request_send(&stack, ca, 0xFEE5U, 0x00U);  /* ask node 0x00 for engine hours */
```

The answer arrives like any received message, so list the requested PGN in `rx_pgns`.

## Multi-packet messages

Payloads of 9 to 1785 bytes travel with the transport protocol; the API stays the same:

- **Sending**: `j1939_send()` with a longer payload. It is broadcast with BAM when `da` is `J1939_ADDR_GLOBAL`, sent with RTS/CTS to a node otherwise. It needs a free transmit buffer (`tp_tx_buf`).
- **Receiving**: a listed PGN arrives in a message slot once complete; `msg->data` points into a reassembly buffer (`tp_rx_buf`) until `j1939_msg_pop()`.
- `J1939_CFG_TP_BUF_SIZE` sets the size of every transport protocol buffer; lower it to the longest message the node uses, see [Configuration](../configuration.md).

## Reference

API reference, topic *Stack*: `j1939_send()`, `j1939_msg_peek()`, `j1939_msg_pop()`, `j1939_cfg_t`.
Topic *Requests*: `j1939_request_send()`, `j1939_request_pgn_get()`.
Topic *Transport protocol*: the session rules, timers and abort reasons (`J1939_TP_ABORT_BUSY` and the following).
