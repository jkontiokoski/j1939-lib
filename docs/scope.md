# Scope

## Feature table

| Priority                           | Standard               | Library importance                                    | Implement?                                          |
| ---------------------------------- | ---------------------- | ----------------------------------------------------- | --------------------------------------------------- |
| 🔴 **Essential**                   | **J1939/21**           | CAN/J1939 identifier, PGN, addressing in messages, TP | **Yes**                                             |
| 🔴 **Essential**                   | **J1939/81**           | NAME, address claiming, address management            | **Yes, preferably separate module**                 |
| 🔴 **Essential data**              | **J1939DA**            | PGN/SPN definitions, scaling, units, NAME data, etc.  | **Database layer**                                  |
| 🟠 **Strongly recommended**        | **J1939/71**           | Application-layer conventions, SPN/PGN interpretation | **Yes, for signal decoding**                        |
| 🟠 **Strongly recommended**        | **J1939/73**           | DM1, DTCs, diagnostics                                | **Separate diagnostics module**                     |
| 🟡 **Optional depending on scope** | **J1939/31**           | Network-layer functionality                           | **Implement if supporting network-layer functions** |
| 🟡 **Optional**                    | **J1939/74**           | Configurable messaging                                | **Plugin/module**                                   |
| 🟡 **Optional**                    | **J1939/75**           | Generator-set/industrial application layer            | **Only if needed**                                  |
| 🟡 **Optional**                    | Other J1939/7x         | Industry/application-specific PGNs                    | **As required**                                     |
| ⚪ **Usually outside library**      | **J1939/11, /15, /14** | Physical CAN bus                                      | **Leave to CAN driver/hardware**                    |
| ⚪ **Usually outside library**      | **J1939/13**           | Diagnostic connector                                  | **No**                                              |
| ⚪ **Usually outside library**      | **J1939/16**           | Baud-rate detection                                   | **Optional hardware integration**                   |
| 🟣 **Specialized**                 | **J1939/76**           | Functional safety communications                      | **Only if product requires it**                     |
| 🟣 **Specialized**                 | **J1939/91 series**    | Cybersecurity                                         | **Separate security architecture**                  |

## Roadmap

| Milestone | Content                                                                                                                                                   | Status  |
| --------- | --------------------------------------------------------------------------------------------------------------------------------------------------------- | ------- |
| M0        | Foundations: source tree, CMake + Makefile wrapper, LICENSE, formatting, Unity, documentation                                                              | Done    |
| M1        | Port boundary: port contract, mock port, SocketCAN port, ID/PGN codec, rx/tx queues over integrator buffers, port conformance tests                          | Planned |
| M2        | J1939/21 core: stack init/process, DA and PGN filtering, message pull API, single-frame send, Request (PGN 59904), Acknowledgement (PGN 59392)             | Planned |
| M3        | J1939/21 transport protocol: BAM and RTS/CTS tx/rx, timers T1–T4/Tr/Th, aborts, session pool with integrator-supplied reassembly memory                   | Planned |
| M4        | J1939/81 network management: NAME codec, address claiming, Cannot Claim, Request for Address Claimed, arbitrary address capability, Commanded Address     | Planned |
| M5        | SocketCAN example applications, porting guide worked examples                                                                                             | Planned |
| M6        | Signals / database layer (J1939/71 + DA schema): SPN descriptors, bit extraction and insertion, scaling, validity ranges                                  | Planned |
| M7        | Diagnostics (J1939/73): DTC codec, lamp status, DM1, then DM2, DM3, DM11                                                                                  | Planned |

J1939/31, /74 and /75 are opt-in modules implemented on demand.
J1939/76 and the J1939/91 series are outside the scope unless a product requires them.
