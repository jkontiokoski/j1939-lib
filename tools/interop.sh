#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 jkontiokoski

# Interoperability check: runs a library node (tests/interop/interop_node.c) against the J1939
# stack of the Linux kernel (can-j1939) and its tools from can-utils, on one CAN interface. The
# kernel stack is an independent implementation of the transport protocol and of address
# claiming, so a scenario that fails here shows a deviation the library's own tests cannot.
#
# Usage: tools/interop.sh <interop_node> <output directory>
#
# Environment: J1939_TEST_CANIF names the interface, default vcan0. Nothing else may use the
# interface during the run.
#
# Each scenario prints PASS or FAIL and leaves its files in the output directory:
# <scenario>.log (candump of the bus), <scenario>.node (output of the library node) and, for a
# transfer, <scenario>.in and <scenario>.out (payload sent and received).
#
# Exit status: 0 if every scenario passed, 1 if one failed, 77 if the check cannot run here
# (no j1939cat, j1939acd or candump, no interface, or no J1939 support in the kernel).

set -u

NODE=${1:-}
OUT=${2:-}
IF=${J1939_TEST_CANIF:-vcan0}

LIB=0x20        # Library node; outside the self-configurable range, so claimed at once.
KERNEL=0x90     # Kernel node, static address.
SHARED=0x80     # Address both sides want in the claim scenarios.
PGN_P2P=0xEF00  # Proprietary A: destination specific.
PGN_BAM=0xFF10  # Proprietary B: broadcast.

# NAMEs of the claim scenarios, as j1939acd takes them. A lower NAME wins the address.
NAME_LOW=00008000FFE00001
NAME_HIGH=00008000FFE00005
NAME_HIGH_ARBITRARY=80008000FFE00005

FAILED=0
CANDUMP_PID=

if [ -z "$NODE" ] || [ -z "$OUT" ]; then
	echo "usage: $0 <interop_node> <output directory>" >&2
	exit 1
fi

skip() {
	echo "interop: skipped: $1"
	exit 77
}

for tool in j1939cat j1939acd candump timeout cmp awk; do
	command -v "$tool" > /dev/null 2>&1 || skip "$tool not found"
done
[ -x "$NODE" ] || skip "$NODE not built"
[ -d "/sys/class/net/$IF" ] || skip "no CAN interface $IF"
# A J1939 socket that stays open until the timeout shows that the kernel supports the protocol.
timeout 0.3 j1939cat -r "$IF:0xFD" > /dev/null 2>&1
[ $? -eq 124 ] || skip "the kernel has no J1939 support (can-j1939)"

mkdir -p "$OUT" || exit 1

# pattern <bytes>: a payload without zero bytes, the same on every run.
pattern() {
	LC_ALL=C awk -v n="$1" 'BEGIN { for (i = 0; i < n; i++) printf "%c", (i * 7 + 3) % 255 + 1 }'
}

# name_bytes <NAME>: the NAME as the payload of its Address Claimed, least significant byte first.
name_bytes() {
	echo "$1" | sed 's/../& /g' | awk '{ for (i = NF; i > 0; i--) printf "%s", $i }'
}

trace_start() {
	candump -L "$IF" > "$OUT/$1.log" 2> /dev/null &
	CANDUMP_PID=$!
	sleep 0.1
}

trace_stop() {
	sleep 0.1
	kill "$CANDUMP_PID" 2> /dev/null
	wait "$CANDUMP_PID" 2> /dev/null
}

# in_order <log> <text>...: true if the texts appear in the log on separate lines, in this order.
in_order() {
	log=$1
	shift
	for text in "$@"; do
		printf '%s\n' "$text"
	done | awk -v log_file="$log" '
		{ want[n++] = $0 }
		END {
			i = 0
			while ((getline line < log_file) > 0) {
				if (i < n && index(line, want[i]) > 0) {
					i++
				}
			}
			exit (i == n) ? 0 : 1
		}'
}

verdict() {
	if [ "$2" -eq 0 ]; then
		echo "PASS $1"
	else
		echo "FAIL $1"
		FAILED=1
	fi
}

# send_ms <bytes> <ms between packets>: run time that lets a transfer of this size finish.
send_ms() {
	echo $((1000 + ($1 / 7 + 1) * $2))
}

# Library sends with RTS/CTS, kernel receives.
rts_to_kernel() {
	name=rts_lib_to_kernel_$1
	pattern "$1" > "$OUT/$name.in"
	trace_start "$name"
	timeout 20 j1939cat -r "$IF:$KERNEL" > "$OUT/$name.out" 2> /dev/null &
	peer=$!
	sleep 0.3
	"$NODE" -i "$IF" -a "$LIB" -t "$(send_ms "$1" 2)" send "$KERNEL" "$PGN_P2P" \
		"$OUT/$name.in" > "$OUT/$name.node" 2>&1
	node=$?
	sleep 0.2
	kill "$peer" 2> /dev/null
	wait "$peer" 2> /dev/null
	trace_stop
	[ "$node" -eq 0 ] && cmp -s "$OUT/$name.in" "$OUT/$name.out"
	verdict "$name" $?
}

# Kernel sends with RTS/CTS, library receives.
rts_to_lib() {
	name=rts_kernel_to_lib_$1
	pattern "$1" > "$OUT/$name.in"
	rm -f "$OUT/$name.out"
	trace_start "$name"
	"$NODE" -i "$IF" -a "$LIB" -t 5000 recv "$PGN_P2P" "$OUT/$name.out" \
		> "$OUT/$name.node" 2>&1 &
	node_pid=$!
	sleep 0.3
	timeout 5 j1939cat -i "$OUT/$name.in" "$IF:$KERNEL" ":$LIB,$PGN_P2P" > /dev/null 2>&1
	peer=$?
	wait "$node_pid"
	node=$?
	trace_stop
	[ "$peer" -eq 0 ] && [ "$node" -eq 0 ] && cmp -s "$OUT/$name.in" "$OUT/$name.out"
	verdict "$name" $?
}

# Library broadcasts with BAM, kernel receives.
bam_to_kernel() {
	name=bam_lib_to_kernel_$1
	pattern "$1" > "$OUT/$name.in"
	trace_start "$name"
	timeout 30 j1939cat -B -r "$IF:$KERNEL,$PGN_BAM" > "$OUT/$name.out" 2> /dev/null &
	peer=$!
	sleep 0.3
	"$NODE" -i "$IF" -a "$LIB" -t "$(send_ms "$1" 60)" send 0xFF "$PGN_BAM" \
		"$OUT/$name.in" > "$OUT/$name.node" 2>&1
	node=$?
	sleep 0.2
	kill "$peer" 2> /dev/null
	wait "$peer" 2> /dev/null
	trace_stop
	[ "$node" -eq 0 ] && cmp -s "$OUT/$name.in" "$OUT/$name.out"
	verdict "$name" $?
}

# Kernel broadcasts with BAM, library receives.
bam_to_lib() {
	name=bam_kernel_to_lib_$1
	pattern "$1" > "$OUT/$name.in"
	rm -f "$OUT/$name.out"
	trace_start "$name"
	"$NODE" -i "$IF" -a "$LIB" -t "$(send_ms "$1" 60)" recv "$PGN_BAM" "$OUT/$name.out" \
		> "$OUT/$name.node" 2>&1 &
	node_pid=$!
	sleep 0.3
	timeout 30 j1939cat -B -i "$OUT/$name.in" "$IF:$KERNEL" ":0xff,$PGN_BAM" > /dev/null 2>&1
	peer=$?
	wait "$node_pid"
	node=$?
	trace_stop
	[ "$peer" -eq 0 ] && [ "$node" -eq 0 ] && cmp -s "$OUT/$name.in" "$OUT/$name.out"
	verdict "$name" $?
}

# The library holds the address with the lower NAME. The kernel node asks for the claims, gets
# the library's answer and takes the next address.
claim_lib_keeps() {
	name=claim_lib_keeps
	trace_start "$name"
	"$NODE" -i "$IF" -a "$SHARED" -n "0x$NAME_LOW" -t 3000 claim > "$OUT/$name.node" 2>&1 &
	node_pid=$!
	sleep 0.6
	timeout 2 j1939acd -a "$SHARED" -r 0x80-0x81 "$NAME_HIGH" "$IF" > /dev/null 2>&1
	wait "$node_pid"
	trace_stop
	grep -q "^address 0x80 CLAIMED" "$OUT/$name.node" &&
		in_order "$OUT/$name.log" \
			"18EAFFFE#00EE00" \
			"18EEFF80#$(name_bytes "$NAME_LOW")" \
			"18EEFF81#$(name_bytes "$NAME_HIGH")"
	verdict "$name" $?
}

# The kernel node holds the address with the lower NAME and defends it.
# claim_lib_loses <scenario> <library NAME> <state line of the library> <frame the library sends>
claim_lib_loses() {
	name=$1
	trace_start "$name"
	timeout 10 j1939acd -a "$SHARED" -r "$SHARED" "$NAME_LOW" "$IF" > /dev/null 2>&1 &
	peer=$!
	# The kernel node waits 1250 ms after its Request before it claims.
	sleep 1.8
	"$NODE" -i "$IF" -a "$SHARED" -n "0x$2" -t 1200 claim > "$OUT/$name.node" 2>&1
	kill "$peer" 2> /dev/null
	wait "$peer" 2> /dev/null
	trace_stop
	grep -q "^$3" "$OUT/$name.node" &&
		in_order "$OUT/$name.log" \
			"18EEFF80#$(name_bytes "$NAME_LOW")" \
			"18EEFF80#$(name_bytes "$2")" \
			"18EEFF80#$(name_bytes "$NAME_LOW")" \
			"$4#$(name_bytes "$2")"
	verdict "$name" $?
}

echo "interop: library node against the Linux J1939 stack on $IF"

rts_to_kernel 9
rts_to_kernel 100
rts_to_kernel 1785
rts_to_lib 9
rts_to_lib 100
rts_to_lib 1785
bam_to_kernel 100
bam_to_lib 100
claim_lib_keeps
claim_lib_loses claim_lib_moves "$NAME_HIGH_ARBITRARY" "address 0x81 CLAIMED" "18EEFF81"
claim_lib_loses claim_lib_cannot_claim "$NAME_HIGH" "address 0xFE CANNOT_CLAIM" "18EEFFFE"

if [ "$FAILED" -ne 0 ]; then
	echo "interop: FAILED, see $OUT"
	exit 1
fi
echo "interop: all scenarios passed"
exit 0
