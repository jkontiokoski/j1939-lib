# Portable SAE J1939 CAN protocol library guidelines

- README.md serves as an introduction to the project.
It is intended to contain introductory information for the project.
- The license shall be MIT.
- The owner of this project is 'jkontiokoski'.
Use this name when making copyright stamps.

## Docs

- The user documentation is maintained in the 'docs/' directory, with README.md as its main page.
It is released: `make docs` builds it into the HTML reference that `make dist` ships.
- One place per fact: what a module or function does, its rules and limits, is documented in its header (the API reference), grouped into one Doxygen topic per module.
The pages in 'docs/' explain how to do a task and why the design is as it is, and link to the reference instead of repeating it.
- Each page serves one audience, opens with two or three sentences on what it covers and for whom, and stays short (a guide at most about 150 lines, no page over about 250).
`cmake/docs.cmake` (`_j1939_docs_pages`) and the list in README.md hold the pages in reading order.
- Development standards and tooling live in CONTRIBUTING.md, the release process in RELEASING.md; neither goes into 'docs/' or the generated documentation.
- The docs shall be kept up to date when working on tasks.
- Do not maintain a 'history' of the project in the documentation after changing a design.
Always keep the documentation to represent the current status of the project.
Additional 'this is this, not that' type of documentation are not to be written.
- No diagrams (ASCII art or Mermaid): the generated documentation cannot render them. Use tables for structure and numbered steps for flows.

### Scope

- `docs/scope.md`: the scope of the project, what parts of the protocols are implemented and what are planned/not planned etc. live in this document.
It shall serve this purpose during development and after publishing.

### Concepts

- `docs/concepts.md`: the architecture of the library: design principles and their reasons, layering, data flow, execution model, and which header holds what.

### Portability

- `docs/porting.md`: the guide on how to port the project to new targets, with `docs/porting-bxcan.md` as a worked bare-metal example.
It will serve this purpose after the library is published, but also for the author to inspect how the boundary looks like during development.

### Other pages

- `docs/guides/`: one task-oriented guide per feature (addressing, messages, message objects, signals, diagnostics).
- `docs/getting-started.md` (integration and the main loop), `docs/configuration.md` (settings and buffer sizing), `docs/examples.md` (the example applications), `docs/safety.md` (coding standard, deviations, verification and release evidence, for reviewers).


## Knowledge

- This section of this file is for you to upkeep your knowledge about the project.
- When you are corrected after making a mistake, update this knowledge base with this known mistake and the solution.

### Design constraints (set by the owner)

- The library is designed to be usable in ISO 13849 safety-related systems: no function pointers or runtime dispatch anywhere, including application callbacks. Bind at compile time.
- Do not define a library-owned CAN frame struct. Operate on the integrator's native frame type through `static inline` accessors in the port's `j1939_target.h`.
- The integrator allocates and supplies all buffers (tx queue, message slots, TP memory). No dynamic memory.
- The library never calls out: tx frames and received messages are pulled by the integrator.
- All functions of a stack instance run in one execution context; the stack has no lock. Received frames are pushed with `j1939_rx()`, the tx queue is drained with `j1939_tx_peek()` / `j1939_tx_pop()`. The frame queue (`j1939::queue`) is an optional helper outside the stack; only it needs the port lock.

### Tooling notes

- cppcheck (2.13) suppressions lists accept `//` comments only; `#` lines fail with "Failed to add suppression. No id". An unmatched suppression fails `make lint`, so add suppressions only when they match something.
- cppcheck 2.13 does not set its exit code for whole-program findings (e.g. MISRA 5.9, static names unique across files); `make lint` therefore also fails on any finding in its output. When two branches add static helpers, check the combined result for name collisions.

### Workflow

- When several tasks can be worked on simultaneously, act as an orchestrator: spawn one subagent per task, each in its own git worktree and feature branch. Every task is delivered as a pull request from its feature branch; never commit or push to `main` directly.
- Update a feature branch that is behind `main` by rebasing it onto `origin/main` and pushing with `--force-with-lease`; do not merge `main` into feature branches.
- The release process is in RELEASING.md. It is development-only: keep it out of `docs/` and the generated documentation.
- Concurrent agents share `vcan0`. Agents run `make check` with `J1939_TEST_CANIF` set to a missing interface so the SocketCAN loopback test is skipped; the orchestrator runs it after merging.

### Public content

- This repository, including CLAUDE.md, commit messages and PR descriptions, is public. Never record details of the owner's personal workspace (local paths, symlinks, machine setup) in it.

### Commit messages

- Never add a `Co-Authored-By` trailer or any other AI attribution to commits or PR descriptions. The owner is responsible for the commits.
- Never put roadmap identifiers (M0, M1, ...) in commit messages or branch names. Scope commits by module or feature and keep the body to a short what-and-why. The full policy is in CONTRIBUTING.md.
