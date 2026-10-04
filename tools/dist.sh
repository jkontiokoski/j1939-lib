#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 jkontiokoski

# Builds the release artifacts of the current commit in build/dist/ (see RELEASING.md):
#   <name>.tar.gz           source archive of the commit, reproducible
#   <name>-docs.tar.gz      public and internal documentation (HTML)
#   <name>-evidence.tar.gz  test, coverage and static analysis results, tool versions
#   SHA256SUMS              checksums of the three archives
# All quality gates run on the extracted source archive, so the documentation and the
# evidence come from exactly the source that is shipped. They run in a directory under /tmp,
# outside the checkout, so no path of the developer's workspace ends up in the published logs
# and reports; a final check refuses artifacts that contain one.
# <name> is j1939-lib-<version> when the commit is tagged v<version>, otherwise
# j1939-lib-<version>-g<commit>.

set -eu

cd "$(dirname "$0")/.."

dist=build/dist
header=include/j1939/j1939.h

fail() {
	echo "dist: $*" >&2
	exit 1
}

if ! git diff --quiet HEAD --; then
	fail "uncommitted changes to tracked files; commit or discard them first"
fi

# The version is defined once, in j1939.h.
version_part() {
	awk -v part="$1" '$1 == "#define" && $2 == "J1939_VERSION_" part { print $3 }' "$header"
}
version="$(version_part MAJOR).$(version_part MINOR).$(version_part PATCH)"
case "$version" in
*[!0-9.]* | .* | *. | *..*) fail "cannot read the version from $header" ;;
esac

commit=$(git rev-parse HEAD)
if git tag --points-at HEAD | grep -qx "v$version"; then
	name="j1939-lib-$version"
	release=yes
else
	name="j1939-lib-$version-g$(git rev-parse --short=12 HEAD)"
	release=no
fi

rm -rf "$dist"
mkdir -p "$dist"
stage=$(mktemp -d /tmp/j1939-dist.XXXXXX)
trap 'rm -rf "$stage"' EXIT
src="$stage/$name"
reports="$stage/reports"

echo "dist: source archive $dist/$name.tar.gz"
git archive --format=tar --prefix="$name/" HEAD | gzip -n -9 >"$dist/$name.tar.gz"

tar -xzf "$dist/$name.tar.gz" -C "$stage"

log="$stage/check.log"
echo "dist: make check on the extracted source"
if ! (cd "$src" && make check REPORT_DIR="$reports") >"$log" 2>&1; then
	cp "$log" "$dist/check.log"
	tail -n 40 "$log" >&2
	fail "make check failed on the source archive, see $dist/check.log"
fi

docs="$stage/$name-docs"
mkdir -p "$docs"
cp -R "$src/build/dev/docs/public/html" "$docs/public"
cp -R "$src/build/dev/docs/internal/html" "$docs/internal"

evidence="$stage/$name-evidence"
mkdir -p "$evidence/lint"
cp -R "$reports/." "$evidence/"
cp "$log" "$evidence/check.log"
cp "$src/cppcheck-suppressions.txt" "$src/cppcheck-misra-suppressions.txt" "$evidence/lint/"
for f in "$src"/build/lint/*.txt; do
	if [ -f "$f" ]; then
		cp "$f" "$evidence/lint/"
	fi
done
{
	echo "name: $name"
	echo "version: $version"
	echo "commit: $commit"
	echo "release build: $release"
	echo "J1939_TEST_CANIF: ${J1939_TEST_CANIF:-vcan0 (default)}"
	echo
	echo "Tools:"
	gcc --version | head -n 1
	arm-none-eabi-gcc --version | head -n 1
	cmake --version | head -n 1
	cppcheck --version
	clang-format --version
	gcovr --version | head -n 1
	echo "doxygen $(doxygen --version)"
	dot -V 2>&1
} >"$evidence/versions.txt"

# Published artifacts must not reveal the developer's workspace.
for path in "$(pwd -P)" "$(pwd)" "$HOME"; do
	if grep -rqF -- "$path" "$docs" "$evidence"; then
		fail "documentation or evidence contains the local path $path"
	fi
done

echo "dist: documentation $dist/$name-docs.tar.gz"
tar -czf "$dist/$name-docs.tar.gz" -C "$stage" "$name-docs"
echo "dist: evidence $dist/$name-evidence.tar.gz"
tar -czf "$dist/$name-evidence.tar.gz" -C "$stage" "$name-evidence"

(cd "$dist" && sha256sum "$name.tar.gz" "$name-docs.tar.gz" "$name-evidence.tar.gz" >SHA256SUMS)

echo "dist: done"
ls -l "$dist"
if [ "$release" = no ]; then
	echo "dist: HEAD is not tagged v$version: not a release build" >&2
fi
