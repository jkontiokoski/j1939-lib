# Portable SAE 1939 CAN protocol library guidelines

- README.md serves as an introduction to the project.
It is intended to contain introductory information for the project.
- The license shall be MIT.
- The owner of this project is 'jkontiokoski'.
Use this name when making copyright stamps.

## Docs

- The docs of this projects are maintained in the 'docs/' directory.
- The docs shall be kept up to date when working on tasks.
- Do not maintain a 'history' of the project in the documentation after changing a design.
Always keep the documentation to represent the current status of the project.
Additional 'this is this, not that' type of documentation are not to be written.

### Scope

- The scope of the project, what parts of the protocols are implemented and what are planned/not planned etc. live in this document.
It shall serve this purpose during development and after publishing.

### Architecture

- The architecture of the library is maintained in this file.
- The design guidelines, tooling choices, development standards are described in this file.
- It shall describe the source tree structure, key abstractions, and interface boundaries and their intended usage.

### Portability

- This document holds the guide on how to port the project to new targets.
It will serve this purpose after the library is published, but also for the author to inspect how the boundary looks like during development.


## Knowledge

- This section of this file is for you to upkeep your knowledge about the project.
- When you are corrected after making a mistake, update this knowledge base with this known mistake and the solution.

### Design constraints (set by the owner)

- The library targets ISO 13849 safety-rated systems: no function pointers or runtime dispatch anywhere, including application callbacks. Bind at compile time.
- Do not define a library-owned CAN frame struct. Operate on the integrator's native frame type through `static inline` accessors in the port's `j1939_target.h`.
- The integrator allocates and supplies all buffers (CAN rx/tx queues, TP memory). No dynamic memory.
- The library never calls out: tx frames and received messages are pulled by the integrator.

### Tooling notes

- cppcheck (2.13) suppressions lists accept `//` comments only; `#` lines fail with "Failed to add suppression. No id". An unmatched suppression fails `make lint`, so add suppressions only when they match something.

### Commit messages

- Never put roadmap identifiers (M0, M1, ...) in commit messages or branch names. Scope commits by module or feature and keep the body to a short what-and-why. The full policy is in docs/architecture.md.
