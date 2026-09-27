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
