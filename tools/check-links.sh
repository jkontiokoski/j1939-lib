#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 jkontiokoski

# Checks the relative links of Markdown files: every linked file must exist, and every
# "#anchor" must be an explicit <a id="anchor"> in the linked file (Doxygen does not create
# anchors from headings). Links to web addresses are not checked; links inside code blocks and
# inline code are ignored.
#
# Usage: tools/check-links.sh <file.md>...
# Prints one line per broken link and exits with 1 if there is any.

set -u

# Prints "<line>\t<target>" for every Markdown link outside code.
links() {
	awk '
		/^[ \t]*```/ { code = !code; next }
		code { next }
		{
			line = $0
			gsub(/`[^`]*`/, "", line)
			while (match(line, /\]\([^)]*\)/)) {
				target = substr(line, RSTART + 2, RLENGTH - 3)
				sub(/[ \t].*$/, "", target)
				print FNR "\t" target
				line = substr(line, RSTART + RLENGTH)
			}
		}
	' "$1"
}

# True if file has an explicit <a id="anchor">.
has_anchor() {
	grep -q "<a id=\"$2\">" "$1"
}

broken=$(
	for file in "$@"; do
		dir=$(dirname "$file")
		links "$file" | while IFS="$(printf '\t')" read -r line target; do
			case "$target" in
			http://* | https://* | mailto:*)
				continue
				;;
			'#'*)
				path="$file"
				anchor="${target#\#}"
				;;
			*'#'*)
				path="$dir/${target%%#*}"
				anchor="${target#*#}"
				;;
			*)
				path="$dir/$target"
				anchor=""
				;;
			esac
			if [ ! -e "$path" ]; then
				echo "$file:$line: broken link: $target (no such file)"
			elif [ -n "$anchor" ] && ! has_anchor "$path" "$anchor"; then
				echo "$file:$line: broken link: $target (no anchor '$anchor')"
			fi
		done
	done
)

if [ -n "$broken" ]; then
	echo "$broken"
	exit 1
fi
