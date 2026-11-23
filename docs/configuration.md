# Configuration

This page is for integrators: the compile-time settings, the buffers handed to the stack at runtime, and rules of thumb for sizing them.

## Compile-time settings

`j1939_config.h` defines the defaults.
To change them, write a header that defines the values you need and build with `-DJ1939_CONFIG_FILE="my_cfg.h"`; values you leave out keep their defaults.
These settings change the layout of public types, so the library and the application must be compiled with the same header, see [Getting started](getting-started.md).

| Macro | Default | Range | Meaning |
| --- | --- | --- | --- |
| `J1939_TP_ENABLE` | 1 | 0, 1 | Builds the transport protocol; 0 limits messages to one frame and removes the TP buffers |
| `J1939_DM_ENABLE` | 1 | 0, 1 | Builds the diagnostics (DM1, DM2, DM3, DM11); 0 removes their API, `j1939_ca_cfg_t::dm` and the DM counters |
| `J1939_CFG_CA_MAX` | 1 | 1–253 | Controller Applications per stack |
| `J1939_CFG_TP_SESSIONS` | 2 | 1–32 | Concurrent transport protocol sessions per stack, both directions together |
| `J1939_CFG_TP_BUF_SIZE` | 1785 | 9–1785 | Largest multi-packet message: the size of each transport protocol (TP) buffer |
| `J1939_CFG_TP_CTS_PACKETS` | 16 | 1–255 | Most data packets the stack asks for with one CTS when it receives with RTS/CTS |
| `J1939_CFG_TP_BAM_GAP_US` | 50000 | 50000–200000 | Gap between the packets of a sent broadcast (BAM), in microseconds |

A value outside its range stops the build with an `#error`.
The `J1939_CFG_TP_*` settings apply only while `J1939_TP_ENABLE` is 1.

## Runtime buffers

All buffers, lists and tables are named in one `j1939_cfg_t` given to `j1939_init()`.
The stack keeps a pointer to it, so the configuration and everything it names must outlive the stack; the lists and tables can be `static const` data in flash.

| Buffer | Fields | Holds |
| --- | --- | --- |
| tx queue | `tx_buf`, `tx_len` (≥ 1) | Frames the stack generates, until the driver takes them |
| Message slots | `msg_buf`, `msg_len` (≥ 1) | Received messages for the application, until `j1939_msg_pop()` |
| Receive list | `rx_pgns`, `rx_pgns_len` | PGNs delivered to the application |
| Request list | `req_pgns`, `req_pgns_len` | PGNs whose Requests the application answers |
| Controller Applications | `ca`, `ca_len` (≤ `J1939_CFG_CA_MAX`) | One `j1939_ca_cfg_t` per CA; the stack keeps their state in `j1939_t` |
| TP transmit buffers (with `J1939_TP_ENABLE`) | `tp_tx_buf`, `tp_tx_buf_len` | Multi-packet messages being sent; without one, a multi-packet send returns `J1939_RET_ERR_FULL` |
| TP reassembly buffers (with `J1939_TP_ENABLE`) | `tp_rx_buf`, `tp_rx_buf_len` | Multi-packet messages being received; without one, RTS is refused (Connection Abort reason 2) and BAM ignored |

Sizing:

- **The driver's receive FIFO** (outside the library): the frames that can arrive during one main loop cycle. As a responder the stack asks for at most `J1939_CFG_TP_CTS_PACKETS` packets of an RTS/CTS transfer with one CTS, and they arrive back to back: a fast originator sends them within a few milliseconds, so the FIFO must hold that many frames next to the other traffic. Raise the setting only as far as the FIFO allows: a packet the driver loses ends the connection, with Connection Abort (bad sequence number) or after the T1 timeout. With a NAME table, it also holds one frame per node on the bus: the answers to the table's startup Request arrive back to back.
- **tx queue**: the largest burst the stack generates in one cycle, a CTS window of data packets when it sends with RTS/CTS. Packets that do not fit are sent as the queue drains, within Tr (200 ms).
- **Message slots**: the messages that arrive between two `j1939_msg_pop()` loops. Messages that find no free slot are dropped and counted in `j1939_stats_t`.
- **TP buffers**: each takes `J1939_CFG_TP_BUF_SIZE` bytes plus a few bytes of bookkeeping. A microcontroller that never handles 1785-byte messages lowers `J1939_CFG_TP_BUF_SIZE`.
- **Lists and tables, for processing time**: `j1939_rx()` compares each received frame with every receive object and with `rx_pgns`, one entry after the other, and a receive object bound by NAME also searches the NAME table. `j1939_process()` visits every transmit object and every CA. Tens of entries cost a few microseconds per frame on a Cortex-M; with hundreds, check the time of one `j1939_rx()` against the frame rate of the bus.

## Per-feature storage

Optional features take their storage from the application too, when they are enabled:

| Feature | Fields | Storage | Size |
| --- | --- | --- | --- |
| Diagnostics of a CA | `j1939_ca_cfg_t::dm` | A `j1939_dm_cfg_t` naming a `j1939_dm_t`, DTC lists, hold records and a payload buffer | Payload buffer `J1939_DM_BUF_LEN(n)` for up to `n` DTCs |
| Receive objects | `rxobj_cfg`, `rxobj`, `rxobj_len` | A `const` table of `j1939_rxobj_cfg_t` (32 bytes per entry on 32- and 64-bit targets), a `j1939_rxobj_t` per entry, a payload buffer per object | 12 bytes of state per object plus its buffer |
| Transmit objects | `txobj_cfg`, `txobj`, `txobj_len` | A `const` table of `j1939_txobj_cfg_t`, a `j1939_txobj_t` per entry, a payload buffer per object | 16 bytes of state per object plus its buffer; multi-packet objects share the TP transmit buffers |
| NAME table | `nametab`, `nametab_len` | An array of `j1939_nametab_entry_t`, one entry per other node | 12 bytes per entry |

Every payload buffer is exactly as large as the application declares it; configuration tables are `const` and can live in flash.
