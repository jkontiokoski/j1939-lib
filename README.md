# Portable SAE J1939 protocol library

A SAE J1939 protocol stack in C99 for embedded and Linux systems.
Like CANopenNode for CANopen, it is portable across CAN drivers: the stack works directly on the integrator's own frame type, bound at compile time.
It is designed so that it can be used in safety-related systems such as those built to ISO 13849: no dynamic memory, no function pointers, statically bounded loops.

## Notice

- The library aims to be usable in ISO 13849 safety-related systems, but it is not safety certified by any means. Qualifying it for a product is the integrator's task; [Safety and quality](docs/safety.md) describes what the project provides for that.
- It does not claim compliance with the SAE J1939 standard. Its goal is an easy to understand, portable library for working with the protocol.
- SAE and J1939 are trademarks of SAE International. This project is not affiliated with or endorsed by SAE International.

## Features

- **J1939/21**: identifiers and PGNs, Request and Acknowledgement, transport protocol (BAM and RTS/CTS, up to 1785 bytes).
- **J1939/81**: NAME, address claiming with arbitration, arbitrary address capability, Cannot Claim, Commanded Address.
- **J1939/71 and the DA schema**: signal descriptors, bit extraction and insertion, scaling, value ranges; the licensed DA content is supplied by the integrator.
- **J1939/73**: DTC and lamp codec; DM1 and DM2 sent by the stack; DM3 and DM11 clearing decided by the application.
- **Message objects**: PGNs sent periodically, on change and on Request; received PGNs with timeout supervision.
- **Ports**: Linux SocketCAN included; any other CAN driver through one header.

## Status

Version 0.1.0, under development: the API may still change.
[Scope](docs/scope.md) lists what is implemented and what is planned.

## Example

```c
#include "j1939/j1939.h"

j1939_init(&stack, &cfg);                     /* buffers in cfg, see Getting started */
j1939_ca_add(&stack, &ca_cfg, &ca);           /* NAME and preferred address */

for (;;) {
	while (driver_read(&frame)) {
		j1939_rx(&stack, &frame);             /* push received frames */
	}
	j1939_process(&stack, elapsed_us());      /* timers, address claim, transport protocol */
	while ((msg = j1939_msg_peek(&stack)) != NULL) {
		handle(msg);                          /* pull received messages */
		j1939_msg_pop(&stack);
	}
	while (((f = j1939_tx_peek(&stack)) != NULL) && driver_write(f)) {
		j1939_tx_pop(&stack);                 /* drain the tx queue */
	}
}
```

## Documentation

1. [Getting started](docs/getting-started.md): add the library to a project, set up a stack, run the main loop.
2. [Concepts](docs/concepts.md): design principles, layers, data flow, which header holds what.
3. [Configuration](docs/configuration.md): compile-time settings and buffer sizing.
4. Guides: [Addressing](docs/guides/addressing.md), [Messages](docs/guides/messages.md), [Message objects](docs/guides/message-objects.md), [Signals](docs/guides/signals.md), [Diagnostics](docs/guides/diagnostics.md).
5. [Porting](docs/porting.md): the contract between the library and a CAN driver.
6. [Example applications](docs/examples.md): the SocketCAN examples and the traffic they produce.
7. [STM32 bxCAN sketch](docs/porting-bxcan.md): a bare-metal port, worked through.
8. [Safety and quality](docs/safety.md): coding standard, verification and release evidence, for reviewers.
9. [Scope](docs/scope.md): features and roadmap.

The API reference is generated from the headers with `make docs` and ships with every release; its topics hold the exact rules of each module.

## Releases

Releases are GitHub Releases of this repository, shipped as source: the port and the configuration are bound at compile time, so there are no prebuilt binaries.
Each release contains the source archive, the generated documentation (public and internal reference), verification evidence and SHA-256 checksums; [Safety and quality](docs/safety.md) lists what the evidence holds.

## Contributing and license

To work on the library, see [CONTRIBUTING.md](https://github.com/jkontiokoski/j1939-lib/blob/main/CONTRIBUTING.md).
The library is licensed under the MIT license, see `LICENSE`.
