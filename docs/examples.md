# Example applications

The programs in `examples/` show complete integrations on Linux SocketCAN: each runs the main loop of [Getting started](getting-started.md) and prints what the stack does.
This page lists them and walks through four scenarios with the traffic they put on the bus.

## The programs

| Example           | Shows                                                                                              |
| ----------------- | -------------------------------------------------------------------------------------------------- |
| `addr_claim_demo` | Address claiming: the CA's state changes and the claims on the bus. Several instances show arbitration |
| `pgn_listener`    | Receiving the PGNs given on the command line, single frame and reassembled from BAM or RTS/CTS; DM1 decoding; answering a Request for an identification text (PGN 0xFF22, an invented Proprietary B PGN) through `req_pgns` |
| `bam_sender`      | A DM1 with four DTCs built with `j1939_diag_dm_build()`, sent with BAM; a 100 byte Proprietary A message (PGN 0xEF00) sent with RTS/CTS to the address given with `-d` |

## Running them

The examples use `vcan0` unless `-i` names another interface. A virtual interface stands in for a CAN bus:

```sh
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
```

Build them with `make examples`; the binaries are in `build/dev/examples/`.
Watch the traffic with `candump -ta vcan0` in another shell.

## Address claiming and arbitration

Three instances that prefer address 0x80:

```sh
./addr_claim_demo -n 1 &       # keeps 0x80: lowest NAME
./addr_claim_demo -n 2 -A &    # arbitrary address capable: moves to 0x81
./addr_claim_demo -n 3         # not arbitrary address capable: Cannot Claim
```

```
 (1790525816.054137)  vcan0  18EEFF80   [8]  01 00 E0 FF 00 80 00 00   # -n 1 claims 0x80
 (1790525816.855334)  vcan0  18EEFF80   [8]  02 00 E0 FF 00 80 00 80   # -n 2 -A claims 0x80
 (1790525816.855407)  vcan0  18EEFF80   [8]  01 00 E0 FF 00 80 00 00   # lower NAME defends
 (1790525816.855446)  vcan0  18EEFF81   [8]  02 00 E0 FF 00 80 00 80   # loser moves to 0x81
 (1790525817.656380)  vcan0  18EEFF80   [8]  03 00 E0 FF 00 80 00 00   # -n 3 claims 0x80
 (1790525817.656438)  vcan0  18EEFF80   [8]  01 00 E0 FF 00 80 00 00   # lower NAME defends
 (1790525817.758302)  vcan0  18EEFFFE   [8]  03 00 E0 FF 00 80 00 00   # Cannot Claim after 102 ms
```

The first instance prints:

```
[   0.000] NAME 0x00008000FFE00001, preferred address 0x80 on vcan0
[   0.010] state UNCLAIMED    -> CLAIMING     address 0x80
[   0.264] state CLAIMING     -> CLAIMED      address 0x80
[   0.811] Address Claimed from 0x80, NAME 0x80008000FFE00002
[   0.811] Address Claimed from 0x81, NAME 0x80008000FFE00002
```

0x80 is a self-configurable address, so a CA waits 250 ms in `CLAIMING` before it transmits.
The Cannot Claim delay comes from the NAME, here 156 steps of 0.6 ms, plus up to one main loop tick.

## Transport protocol

A listener at 0x90 and one round of the sender at 0x80:

```sh
./pgn_listener -a 0x90 0xFECA 0xEF00 &
./bam_sender -a 0x80 -d 0x90 -c 1
```

```
 (1790526240.800564)  vcan0  18ECFF80   [8]  20 12 00 03 FF CA FE 00   # BAM: DM1, 18 bytes, 3 packets
 (1790526240.800577)  vcan0  18EC9080   [8]  10 64 00 0F FF 00 EF 00   # RTS: 100 bytes, 15 packets
 (1790526240.800603)  vcan0  1CEC8090   [8]  11 0F 01 FF FF 00 EF 00   # CTS: 15 packets from 1
 (1790526240.800653)  vcan0  18EB9080   [8]  01 01 02 03 04 05 06 07
 (1790526240.800658)  vcan0  18EB9080   [8]  02 08 09 0A 0B 0C 0D 0E
 ...
 (1790526240.800694)  vcan0  18EB9080   [8]  0F 63 64 FF FF FF FF FF
 (1790526240.800789)  vcan0  1CEC8090   [8]  13 64 00 0F FF 00 EF 00   # EndOfMsgAck
 (1790526240.851184)  vcan0  18EBFF80   [8]  01 04 FF 0A F0 E1 03 0B   # BAM packets 50.3-50.6 ms apart
 (1790526240.901531)  vcan0  18EBFF80   [8]  02 F0 E0 01 0C F0 E2 07
 (1790526240.951874)  vcan0  18EBFF80   [8]  03 00 F0 FF 01 FF FF FF
```

The listener prints the reassembled messages and decodes the DM1:

```
[   0.915] PGN 0x0FECA (65226) prio 6 SA 0x80 DA 0xFF len 18
            04 FF 0A F0 E1 03 0B F0 E0 01 0C F0 E2 07 00 F0
            FF 01
           DM1: MIL 0 red 0 amber 1 protect 0, 4 DTC(s)
           SPN 520202 FMI 1 OC 3
           SPN 520203 FMI 0 OC 1
           SPN 520204 FMI 2 OC 7
           SPN 520192 FMI 31 OC 1
```

## Requests

Requests to the listener take both paths: a PGN in `req_pgns` is delivered and answered by the application (the identification text, 15 bytes, so a BAM); any other PGN is NACKed by the stack.

```sh
cansend vcan0 18EA90F9#22FF00  # Request from 0xF9 for PGN 0xFF22
cansend vcan0 18EA90F9#00EF00  # Request from 0xF9 for PGN 0xEF00
```

```
 (1790526242.556673)  vcan0  18EA90F9   [3]  22 FF 00
 (1790526242.556738)  vcan0  18ECFF90   [8]  20 0F 00 03 FF 22 FF 00   # BAM: 15 bytes
 (1790526242.617184)  vcan0  18EBFF90   [8]  01 6A 31 39 33 39 2D 6C   # "j1939-lib 0.1.0"
 (1790526242.667576)  vcan0  18EBFF90   [8]  02 69 62 20 30 2E 31 2E
 (1790526242.717961)  vcan0  18EBFF90   [8]  03 30 FF FF FF FF FF FF
 (1790526243.058263)  vcan0  18EA90F9   [3]  00 EF 00
 (1790526243.058299)  vcan0  18E8FF90   [8]  01 FF FF FF F9 00 EF 00   # NACK to global, requester 0xF9
```

## Unanswered RTS

Without a node at the destination the RTS goes unanswered. After T3 (1.25 s) the sender sends Connection Abort with reason 3 (timeout) and counts the transfer in `tp_tx_aborted`:

```
 (1790526012.226336)  vcan0  18EC5510   [8]  10 64 00 0F FF 00 EF 00   # RTS to 0x55
 (1790526013.494589)  vcan0  1CEC5510   [8]  FF 03 FF FF FF 00 EF 00   # Connection Abort: timeout
```
