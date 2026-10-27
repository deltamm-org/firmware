# Codeplug programming

The data port speaks the radio's programming protocol, so a codeplug tool
reads and writes the codeplug. These steps use QDMR's `dmrconf` with
DeltaMM's USB ID added.

## Before you start

- A verified backup of the radio (`dmm-radio check-backup` matches it).
- The codeplug saved with a codeplug tool, to restore if needed.
- The log port open in a terminal.

## Steps

| # | Do | Expect |
| --- | --- | --- |
| 1 | `dmrconf detect` | Only the data port is tried; `Found radio 'D878UV2', version 'V101'`, `Found: Anytone AT-D878UVII` |
| 2 | `dmrconf read -y a.yaml` | Done in seconds; channels, zones, contacts and the APRS callsign as in the codeplug |
| 3 | `dmrconf write -y a.yaml` | Done in under a minute; `programming: codeplug written from the PC: restarting`, and the radio starts again |
| 4 | `dmrconf read -y b.yaml`; also `dmrconf encode` a.yaml to a binary and `dmrconf decode` it | b.yaml is what QDMR's own encode and decode give for a.yaml |
| 5 | Rename one channel in b.yaml, write it | The radio shows the new name after it starts again |
| 6 | Start a write, stop `dmrconf` halfway | Ten seconds later the radio starts again; the block being written is as before |
| 7 | `dmm-radio check-backup` | Calibration, radio information and system area still match the backup |

## Results

2026-10-27: steps 1 to 5 and 7 pass. A read took 2 s and a write of the
whole codeplug 9 s; copying a flash page takes about 6 ms, a 128 KB block
under 0.4 s. Step 6 was seen in earlier builds, whose slow answers made
QDMR stop: the radio started again ten seconds later with the block being
written as before, and the blocks already written kept. QDMR's own
encode and decode of the written file gave exactly what was read back.
