# Portable SAE 1939 CAN protocol library

This project is a SAE 1939 CAN protocol library written in C99.
The philosophy behind this library is similar to CANopenNode for CANopen protocol, which is a portable protocol stack that can be used with multiple driver / CAN interface backends.

Refer to 'docs/' for more information about the project.

## Building

Requires CMake 3.21 or newer and a C99 compiler.

```sh
make            # build the library
make test       # build with sanitizers and run the tests
```

See [docs/architecture.md](docs/architecture.md) for all build targets and development standards.

## Quick start

The example applications in `examples/` run on Linux SocketCAN and are the starting point for an integration: each shows the main loop that moves frames between the CAN driver and the stack, runs `j1939_process()` and pulls received messages.

```sh
sudo ip link add dev vcan0 type vcan && sudo ip link set up vcan0
make examples
cd build-examples/examples

./pgn_listener -a 0x90 0xFECA 0xEF00 &    # prints DM1 and Proprietary A messages sent to it
./bam_sender -a 0x80 -d 0x90 -c 3         # DM1 with BAM, 100 bytes with RTS/CTS to 0x90
./addr_claim_demo -a 0x90 -n 5 -A         # loses 0x90 to the listener, moves to 0x80
```

Watch the bus with `candump -ta vcan0`.
[docs/porting.md](docs/porting.md) walks through the examples, the reference ports and a bare-metal port for a microcontroller.
