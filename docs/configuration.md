# Configuration

This page is for integrators: the compile-time settings, the buffers handed to the stack at runtime, and rules of thumb for sizing them.

## Compile-time settings

`j1939_config.h` defines the defaults.
To change them, write a header that defines the values you need and build with `-DJ1939_CONFIG_FILE="my_cfg.h"`; values you leave out keep their defaults.
These settings change the layout of public types, so the library and the application must be compiled with the same header, see [Getting started](getting-started.md).

| Macro | Default | Range | Meaning |
| --- | --- | --- | --- |
| `J1939_CFG_CA_MAX` | 1 | 1–253 | Controller Applications per stack |
| `J1939_CFG_TP_SESSIONS` | 2 | 1–32 | Concurrent transport protocol sessions per stack, both directions together |
| `J1939_CFG_TP_BUF_SIZE` | 1785 | 9–1785 | Largest multi-packet message: the size of each transport protocol (TP) buffer |
| `J1939_CFG_TP_BAM_GAP_US` | 50000 | 50000–200000 | Gap between the packets of a sent broadcast (BAM), in microseconds |

A value outside its range stops the build with an `#error`.

## Runtime buffers

All buffers are passed to `j1939_init()` in `j1939_cfg_t` and must outlive the stack.

| Buffer | Fields | Holds |
| --- | --- | --- |
| tx queue | `tx_buf`, `tx_len` (≥ 1) | Frames the stack generates, until the driver takes them |
| Message slots | `msg_buf`, `msg_len` (≥ 1) | Received messages for the application, until `j1939_msg_pop()` |
| Receive list | `rx_pgns`, `rx_pgns_len` | PGNs delivered to the application |
| Request list | `req_pgns`, `req_pgns_len` | PGNs whose Requests the application answers |
| TP transmit buffers | `tp_tx_buf`, `tp_tx_buf_len` | Multi-packet messages being sent; without one, a multi-packet send returns `J1939_RET_ERR_FULL` |
| TP reassembly buffers | `tp_rx_buf`, `tp_rx_buf_len` | Multi-packet messages being received; without one, RTS is refused (Connection Abort reason 2) and BAM ignored |

Sizing:

- **The driver's receive FIFO** (outside the library): the frames that can arrive during one main loop cycle. As a responder the stack asks for all remaining packets of an RTS/CTS transfer in one CTS, up to the originator's packets-per-CTS limit, and they arrive back to back: at 250 kbit/s about 1800 frames per second, so up to 255 frames in 140 ms. A packet the driver loses ends the connection with Connection Abort (bad sequence number). With a NAME table, it also holds one frame per node on the bus: the answers to the table's startup Request arrive back to back.
- **tx queue**: the largest burst the stack generates in one cycle, a CTS window of data packets when it sends with RTS/CTS. Packets that do not fit are sent as the queue drains, within Tr (200 ms).
- **Message slots**: the messages that arrive between two `j1939_msg_pop()` loops. Messages that find no free slot are dropped and counted in `j1939_stats_t`.
- **TP buffers**: each takes `J1939_CFG_TP_BUF_SIZE` bytes plus a few bytes of bookkeeping. A microcontroller that never handles 1785-byte messages lowers `J1939_CFG_TP_BUF_SIZE`.

## Per-feature storage

Optional features take their storage from the application too, when they are enabled:

| Feature | Storage | Size |
| --- | --- | --- |
| Diagnostics of a CA | `j1939_dm_t`, DTC lists, hold records and a payload buffer in `j1939_dm_cfg_t` | Payload buffer `J1939_DM_BUF_LEN(n)` for up to `n` DTCs |
| Receive objects | A `const` table of `j1939_rxobj_cfg_t`, a `j1939_rxobj_t` per entry, a payload buffer per object | 12 bytes of state per object plus its buffer |
| Transmit objects | A `const` table of `j1939_txobj_cfg_t`, a `j1939_txobj_t` per entry, a payload buffer per object | 16 bytes of state per object plus its buffer; multi-packet objects share the TP transmit buffers |
| NAME table | An array of `j1939_names_entry_t`, one entry per other node | 12 bytes per entry |

Every payload buffer is exactly as large as the application declares it; configuration tables are `const` and can live in flash.
