# Diagnostics

This guide shows how a CA reports its faults with J1939/73: enabling diagnostics, feeding the active and previously active DTCs and the lamps, deciding on clear requests, and reading other nodes' DM1.
It is for integrators; the transmission rules are in the API reference, topic *Diagnostics*, and the payload format in topic *Diagnostic message codec*.

## Who does what

| The application | The stack |
| --- | --- |
| Detects faults, keeps occurrence counts, moves a DTC from active to previously active | Sends DM1 once per second and when the active set changes |
| Copies the result into the stack | Answers Requests for DM1 and DM2 |
| Decides whether a clear request (DM3, DM11) is allowed | Receives the clear request and acknowledges the decision |


## Enable diagnostics for a CA

The application supplies the storage, sized for the most DTCs it reports at once:

```c
static j1939_diag_dtc_t active[8], prev[8];
static j1939_dm_hold_t hold[4];
static uint8_t dm_buf[J1939_DM_BUF_LEN(8)];
static j1939_dm_t dm;

const j1939_dm_cfg_t dm_cfg = {
	.active = active, .active_len = 8,
	.prev = prev, .prev_len = 8,
	.hold = hold, .hold_len = 4,          /* DTCs whose changes can trigger a DM1 per second */
	.buf = dm_buf, .buf_len = sizeof(dm_buf),
	.dm3_enable = false, .dm11_enable = true,
};
j1939_dm_init(&stack, ca, &dm, &dm_cfg);
```

- Two or more DTCs make a multi-packet DM1, so the stack needs a transport protocol transmit buffer (`tp_tx_buf`).
- A DM1 with many DTCs is long on the bus; size the lists for what the bus can carry once per second.

## Report faults

Whenever the fault set changes, hand the complete lists to the stack:

```c
const j1939_diag_dtc_t now[] = {
	{.spn = 520202U, .fmi = 1U, .oc = 3U}, /* invented proprietary SPN, below normal */
};
(void)j1939_dm_active_set(&stack, ca, now, 1U);
(void)j1939_dm_prev_set(&stack, ca, previously_active, n_prev);

const j1939_diag_lamps_t lamps = {
	.amber_warning = J1939_DIAG_LAMP_ON,
	.mil_flash = J1939_DIAG_FLASH_OFF, .red_stop_flash = J1939_DIAG_FLASH_OFF,
	.amber_warning_flash = J1939_DIAG_FLASH_OFF, .protect_flash = J1939_DIAG_FLASH_OFF,
};
(void)j1939_dm_lamps_set(&stack, ca, &lamps);
```

- The stack keeps the order you give and identifies DTCs by SPN and FMI.
- A new or vanished active DTC sends a DM1 right away, at most once per second per DTC; occurrence count and lamp changes go out with the next periodic DM1.
- Nothing is sent before the CA has claimed its address.

## Decide on clear requests

DM3 clears the previously active DTCs, DM11 the active ones.
Clearing erases evidence of faults, so in a safety-related system it is a decision of the application's fault management (operating state, access rights, non-volatile memory), not of the protocol layer.
The stack therefore only reports the request; poll it from the main loop:

```c
uint32_t pgn;

if (j1939_dm_clear_get(&stack, ca, &pgn) == J1939_RET_OK) {
	bool allowed = vehicle_stopped() && service_mode();
	if ((j1939_dm_clear_confirm(&stack, ca, pgn, allowed) == J1939_RET_OK) && allowed) {
		fault_memory_clear(pgn == J1939_PGN_DM11);  /* before the next j1939_process() */
	}
}
```

- Decide within `J1939_DM_RESPONSE_US` (200 ms); a request left undecided is refused.
- On acceptance the stack empties its copy of the cleared list; clear your own records before the next `j1939_process()`. Faults still present are reported again with the next `j1939_dm_active_set()`.
- With `dm3_enable` or `dm11_enable` false, the request is handled like any unsupported PGN: a NACK when it was addressed to the CA.

## Read other nodes' DM1

List `J1939_PGN_DM1` in `rx_pgns` and parse the received payload into your own storage:

```c
j1939_diag_lamps_t lamps;
j1939_diag_dtc_t dtcs[16];
uint16_t count;

if (j1939_diag_dm_parse(msg->data, msg->len, &lamps, dtcs, 16U, &count) == J1939_RET_OK) {
	/* count DTCs from msg->sa */
}
```

A DM1 with more DTCs than `dtcs` holds returns `J1939_RET_ERR_FULL` and still reports the count.

## Reference

API reference, topic *Diagnostics*: `j1939_dm_init()`, `j1939_dm_active_set()`, `j1939_dm_prev_set()`, `j1939_dm_lamps_set()`, `j1939_dm_clear_get()`, `j1939_dm_clear_confirm()`.
Topic *Diagnostic message codec*: `j1939_diag_dm_parse()`, `j1939_diag_dm_build()`, `j1939_diag_dtc_t`, `j1939_diag_lamps_t`.
