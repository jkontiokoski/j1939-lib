# Addressing

This guide shows how a node gets and keeps its J1939 address: building a NAME, adding a Controller Application, following its claim, and moving it with Commanded Address.
It is for integrators setting up a node; the exact protocol rules are in the API reference, topic *Address claiming*.

## Build a NAME

Every Controller Application (CA) has a 64-bit NAME that must be unique on the network.
The NAME is its identity and its priority: in an address conflict the numerically lower NAME wins.
Build it from its fields with `j1939_name_encode()`:

```c
const j1939_name_fields_t fields = {
	.arbitrary_address = true,     /* may move to a free address when it loses its own */
	.industry_group = 0U,          /* global */
	.function = 0x81U,
	.manufacturer = 0x123U,        /* your SAE manufacturer code */
	.identity = 42U,               /* unique per unit, e.g. from the serial number */
};
uint64_t name;

if (j1939_name_encode(&fields, &name) != J1939_RET_OK) {
	/* a field is out of range */
}
```

Give every unit its own identity number, for example from its serial number; two units with the same NAME cannot share a bus.

## Choose the preferred address

| Address range | Use | Behaviour on start-up |
| --- | --- | --- |
| 0–127, 248–253 | Assigned by the system design | The CA may transmit as soon as its claim is queued |
| 128–247 | Self-configurable | The CA waits `J1939_ADDR_CLAIM_WAIT_US` (250 ms) so that a holder can object |
| 254 | NULL address, used for Cannot Claim | Not a valid preferred address |
| 255 | Global address | Not a valid preferred address |

A node whose functions have fixed addresses in the system design uses those.
A node that may meet unknown devices sets `arbitrary_address` in its NAME, so that it moves to a free self-configurable address instead of going silent when it loses.

## Add the CA and follow its claim

```c
static j1939_ca_id_t ca;

j1939_ca_add(&stack, &(j1939_ca_cfg_t){.address = 0x80U, .name = name}, &ca);
```

The claim starts with the next `j1939_process()`; nothing else is needed.
Read the result whenever it matters, for example before enabling functions that transmit:

```c
uint8_t address;
j1939_addr_state_t state;

(void)j1939_addr_get(&stack, ca, &address, &state);
switch (state) {
case J1939_ADDR_STATE_CLAIMED:
	/* transmitting from address */
	break;
case J1939_ADDR_STATE_CANNOT_CLAIM:
	/* no address: report a fault, the CA stays silent */
	break;
default:
	/* claim in progress */
	break;
}
```

- The address can change at runtime: a CA that loses its address to a node with a lower NAME moves (arbitrary address capable) or becomes `J1939_ADDR_STATE_CANNOT_CLAIM`.
- `j1939_send()` returns `J1939_RET_ERR_NO_ADDRESS` while the CA has no address, so sends need no separate check.
- Requests for Address Claimed from other nodes are answered by the stack.

## Learn which nodes are on the bus

A node that joins late has not seen the claims of the nodes already present.
`j1939_request_send()` for PGN 60928 (`J1939_PGN_ADDRESS_CLAIMED`) to the global address makes every node answer with its Address Claimed; it works before the CA has an address.
List `J1939_PGN_ADDRESS_CLAIMED` in `rx_pgns` to receive the answers, and decode the NAME from the payload with `j1939_name_from_bytes()`.

## Move a CA with Commanded Address

A service tool moves a CA to another address with Commanded Address.
The target accepts it only if its configuration allows it:

```c
j1939_ca_add(&stack, &(j1939_ca_cfg_t){.address = 0x80U, .name = name, .accept_commanded = true}, &ca);
```

The tool side sends the command from its own claimed CA:

```c
j1939_addr_command_send(&tool_stack, tool_ca, target_name, 0x85U, J1939_ADDR_GLOBAL);
```

- The command is 9 bytes long and travels with the transport protocol, so both sides need transport protocol buffers (`tp_tx_buf` on the tool, `tp_rx_buf` on the target).
- The target claims the new address and reports it with Address Claimed. The library does not store it: to keep it across restarts, read it with `j1939_addr_get()` and save it as the preferred address, for example in non-volatile memory.

## Reference

API reference, topic *Address claiming*: `j1939_addr_get()`, `j1939_addr_command_send()`, `j1939_addr_state_t`, `j1939_ca_cfg_t`.
The NAME fields are in the topic *NAME*: `j1939_name_encode()`, `j1939_name_fields_t`.
