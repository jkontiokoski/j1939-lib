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
