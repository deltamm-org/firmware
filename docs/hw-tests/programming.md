# Codeplug programming

The data port speaks the radio's programming protocol, so a codeplug tool
reads and writes the codeplug. These steps use QDMR's `dmrconf`.

## Before you start

- A verified backup of the radio (`dmm-radio check-backup` matches it).
- The codeplug saved with a codeplug tool, to restore if needed.
- The log port open in a terminal.

## Steps

| # | Do | Expect |
| --- | --- | --- |
| 1 | `dmrconf detect` | `Found radio 'D878UV2', version 'V101'`, `Found: Anytone AT-D878UVII` |
| 2 | `dmrconf read -y a.yaml` | Done in seconds; channels, zones, contacts and the APRS callsign as in the codeplug |
| 3 | `dmrconf write -y a.yaml` | Done in under a minute; `programming: codeplug written from the PC: restarting`, and the radio starts again |
| 4 | `dmrconf read -y b.yaml`; also `dmrconf encode` a.yaml to a binary and `dmrconf decode` it | b.yaml is what QDMR's own encode and decode give for a.yaml |
| 5 | Rename one channel in b.yaml, write it | The radio shows the new name after it starts again |
| 6 | Start a write, stop `dmrconf` halfway | Ten seconds later the radio starts again; the block being written is as before |
| 7 | `dmm-radio check-backup` | Calibration, radio information and system area still match the backup |
| 8 | `lsusb -v -d 28e9:018a`; `dmm-radio ports` | `iSerial` `DeltaMM`, two CDC interfaces "DeltaMM data" and "DeltaMM log"; `dmm-radio ports` lists both as DeltaMM, none as factory firmware |
| 9 | An unchanged QDMR: `dmrconf detect`, then `dmrconf read` on the data port | The radio found as a D878UVII; the read as in step 2 |
| 10 | `dmm-radio flash --wait 600 <.CDD>` with the radio already in update mode | The update starts at once |
| 11 | As 10, with the radio running its factory firmware; then switch it off and on in update mode | `No answer on <port>: the radio is not in update mode`; after the switch the update starts |

## Results

2026-10-27: steps 1 to 5 and 7 pass. A read took 2 s and a write of the
whole codeplug 9 s; copying a flash page takes about 6 ms, a 128 KB block
under 0.4 s. Step 6 was seen in earlier builds, whose slow answers made
QDMR stop: the radio started again ten seconds later with the block being
written as before, and the blocks already written kept. QDMR's own
encode and decode of the written file gave exactly what was read back.

2026-10-31, step 6 on upstream 0.22.0 with the open pull requests
merged: one block written with a change and the session left without
`END`; ten seconds later the radio started again and the block read as
before. The log line then still says `codeplug written from the PC`.

2026-10-31, step 10: with the radio already in update mode when the wait
started, the flash command asked it, got the boot loader's answer and
flashed at once. Step 11 needs the factory firmware on the radio and is
still to run.
