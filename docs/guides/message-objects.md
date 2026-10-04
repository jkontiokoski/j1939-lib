# Message objects

This guide shows how to let the stack handle the timing of application PGNs: a receive object keeps the latest payload of a PGN from one sender and supervises its timeout, a transmit object sends its payload periodically, on change and on Request.
It is for integrators; the exact rules are in the API reference, topics *Receive objects* and *Transmit objects*.

## Choose objects, slots or j1939_send()

| Need | Use |
| --- | --- |
| The current value of a state-like PGN (a status sent periodically) and whether it is still arriving | Receive object |
| Every occurrence of a message, in order (commands, events, diagnostic traffic) | Message slot (`rx_pgns`), see [Messages](messages.md) |
| A PGN sent periodically, on change or on Request | Transmit object |
| A one-off message | `j1939_send()`, see [Messages](messages.md) |

A PGN may use a receive object and a message slot at the same time; the object is updated even when the slots are full.

## Receive objects

Describe the received PGNs in a `const` table, typically generated, and give the stack a state array of the same length:

```c
enum { RX_PUMP, RX_COUNT };                       /* handles: indexes into the table */

static uint8_t pump_buf[8];
static const j1939_rxobj_cfg_t rx_cfg[RX_COUNT] = {
	[RX_PUMP] = {.buf = pump_buf, .buf_len = 8U, .min_len = 8U,
	             .pgn = 0xFF20U, .sa = 0x20U,      /* pump status from the controller at 0x20 */
	             .timeout_us = 500000U},           /* five times its 100 ms period */
};
static j1939_rxobj_t rx_obj[RX_COUNT];

j1939_rxobj_init(&stack, rx_cfg, rx_obj, RX_COUNT);
```

Read the state, then decode in place from the object's buffer:

```c
j1939_rxobj_status_t st;
int64_t rpm;
j1939_signal_class_t cls;

if ((j1939_rxobj_get(&stack, RX_PUMP, &st) == J1939_RET_OK) && (st.state == J1939_RXOBJ_VALID) &&
    (j1939_signal_decode(&sig_pump_speed, pump_buf, st.len, &rpm, &cls) == J1939_RET_OK) &&
    (cls == J1939_SIGNAL_VALID)) {
	/* use rpm */
}
```

| State | Meaning | The buffer holds |
| --- | --- | --- |
| `J1939_RXOBJ_NO_DATA` | Nothing received yet | Nothing |
| `J1939_RXOBJ_VALID` | Received within `timeout_us` | The latest payload |
| `J1939_RXOBJ_TIMEOUT` | The sender went silent | The last payload, for diagnostics only |

- `st.updated` tells whether a new payload arrived since the previous `j1939_rxobj_get()`.
- Choose `timeout_us` as a few periods of the PGN, typically three to five; 0 disables supervision.
- An object bound to a source address takes whatever node holds that address: when a node loses it, the node that claims it next reaches the same object.

### Follow a node by NAME

Set `name` instead of `sa` to receive a PGN from one node wherever it is on the bus. The object then takes the PGN from the address the [NAME table](addressing.md) records for that NAME, and keeps receiving when the node moves to another address:

```c
static j1939_names_entry_t names[16];
static const j1939_rxobj_cfg_t pump_by_name = {
	.buf = pump_buf, .buf_len = 8U, .min_len = 8U, .pgn = 0xFF20U,
	.timeout_us = 500000U, .name = PUMP_NAME,          /* NAME of the pump controller */
};

j1939_names_init(&stack, names, 16U);                    /* before j1939_rxobj_init() */
j1939_rxobj_init(&stack, &pump_by_name, &pump_obj, 1U);
```

- While the NAME has no address, or before the table has learned it, nothing reaches the object and its timeout runs as usual.
- A table can mix objects bound by address and by NAME; a message that matches one of each updates both.

## Transmit objects

Describe the transmitted PGNs the same way, after the CAs are added:

```c
enum { TX_PUMP, TX_IDENT, TX_COUNT };

static uint8_t pump_tx[8], ident_tx[20];
static const j1939_txobj_cfg_t tx_cfg[TX_COUNT] = {
	[TX_PUMP] = {.buf = pump_tx, .len = 8U, .pgn = 0xFF20U, .prio = 6U,
	             .da = J1939_ADDR_GLOBAL, .ca = 0U, .period_us = 100000U},
	[TX_IDENT] = {.buf = ident_tx, .len = 20U, .pgn = 0xFF22U, .prio = 6U,
	              .da = J1939_ADDR_GLOBAL, .ca = 0U},                  /* on Request only */
};
static j1939_txobj_t tx_obj[TX_COUNT];

j1939_txobj_init(&stack, tx_cfg, tx_obj, TX_COUNT);
```

| `period_us` | `inhibit_us` | Sent |
| --- | --- | --- |
| > 0 | 0 | Periodically |
| 0 | > 0 | When the payload changes, at most once per `inhibit_us` |
| > 0 | > 0 | Periodically, and on change in between |
| 0 | 0 | Only on Request |

Every object is also sent when another node requests its PGN; the application does not see those Requests.
Write new values by encoding a whole payload and handing it over; the stack copies it:

```c
uint8_t payload[8];

(void)memset(payload, 0xFF, sizeof(payload));    /* "not available" for unset parameters */
(void)j1939_signal_encode(&sig_pump_speed, payload, sizeof(payload), rpm);
(void)j1939_txobj_set(&stack, TX_PUMP, payload, sizeof(payload));
```

- Until the first `j1939_txobj_set()` the object sends all 0xFF, so receivers see the node with no values yet.
- Nothing is sent before the object's CA has claimed its address. Periodic and change-triggered objects go out once as soon as the claim completes.
- Never write the object's buffer directly; only `j1939_txobj_set()` detects changes.

## Reference

API reference, topic *Receive objects*: `j1939_rxobj_init()`, `j1939_rxobj_get()`, `j1939_rxobj_cfg_t`.
Topic *Transmit objects*: `j1939_txobj_init()`, `j1939_txobj_set()`, `j1939_txobj_cfg_t`.
Memory per object: [Configuration](../configuration.md).
