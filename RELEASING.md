# Releasing

How a release of the library is made.
This file is for the maintainer: it is not part of the documentation and is left out of the source archive.

## What a release is

A release is a tag `vX.Y.Z` on `main` and a GitHub Release with these files:

| File | Content |
| --- | --- |
| `j1939-lib-X.Y.Z.tar.gz` | Source archive of the tagged commit: library, ports, tests, build files, documentation sources |
| `j1939-lib-X.Y.Z-docs.tar.gz` | Generated documentation: the public API reference (`public/index.html`) and the internal reference with call graphs (`internal/index.html`) |
| `j1939-lib-X.Y.Z-evidence.tar.gz` | Verification evidence: JUnit results of the sanitizer and coverage test runs, the coverage report (text and HTML), the cppcheck and MISRA results with both suppression lists, the full `make check` log and the tool versions |
| `SHA256SUMS` | SHA-256 checksums of the three archives |

No binaries are published.
The port is bound at compile time through the `static inline` accessors of the integrator's `j1939_target.h`, and the `J1939_CFG_*` settings change the layout of public types.
A prebuilt library would only fit one port, one configuration and one toolchain, so integrators build the library from the source archive.

The source archive is reproducible: `git archive` of the tagged commit, compressed with `gzip -n`.
Rebuilding it from the tag gives the same checksum.
The evidence and documentation are produced from the extracted source archive, not from the working tree, so they describe exactly what is shipped.

## Versions

- Semantic versioning; the version is defined once, in `include/j1939/j1939.h` (`J1939_VERSION_MAJOR`, `_MINOR`, `_PATCH`).
- Releases stay at 0.x while the scope in `docs/scope.md` is still being implemented and the API may change. 1.0.0 follows when the planned scope is complete and the API is considered stable.
- Within 0.x, a minor version may contain incompatible API changes; the release notes list them.

## Steps

1. **Version.** Open a pull request that sets the version in `j1939.h`. Merge it.
2. **Tag.** On an up-to-date `main`, create an annotated tag and push it:

   ```sh
   git switch main && git pull --ff-only
   git tag -a vX.Y.Z -m "j1939-lib X.Y.Z"
   git push origin vX.Y.Z
   ```

3. **Build.** On a clean checkout of the tag, with `vcan0` up so the SocketCAN loopback test runs:

   ```sh
   make dist
   ```

   `make dist` refuses uncommitted changes to tracked files and runs `make check` on the extracted source archive (formatting, static analysis, sanitizer tests, coverage, Cortex-M0+ build, both documentation builds).
   The artifacts land in `build/dist/`.
   When `HEAD` is not exactly the tag `vX.Y.Z` of the version in `j1939.h`, the names carry `-g<commit>` and the script reports that it is not a release build; such artifacts are never published.
4. **Review.** Check `build/dist/`: `sha256sum -c SHA256SUMS`, open both documentation entry pages, read `versions.txt` in the evidence and check that the vcan test ran (not skipped) in the JUnit results.
5. **Publish.** Write the release notes and create the GitHub Release:

   ```sh
   gh release create vX.Y.Z build/dist/* --title "j1939-lib X.Y.Z" --notes-file notes.md
   ```

## Release notes

Release notes live in the GitHub Release, not in a file in the repository.
They are written for integrators:

- what changed: new features, fixes, behaviour changes;
- incompatible API or configuration changes and how to migrate;
- the evidence summary: test count, line and branch coverage, static analysis result, and the tool versions it was produced with.
