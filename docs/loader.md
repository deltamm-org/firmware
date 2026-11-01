# DeltaMM's loader

With the loader installed, a radio running DeltaMM takes new images over
USB with nobody at the radio, keeps the previous image, and brings it
back by itself when a new one does not confirm that it runs:

```sh
dmm-radio update deltamm_loader_slot.bin          # stays once it has run 60 s
dmm-radio update deltamm_loader_slot.bin --try    # stays only after dmm-radio confirm
dmm-radio confirm
dmm-radio rollback
dmm-radio loader                                  # wait in loader mode for the PC
dmm-radio info                                    # app or loader, slots, trial state
```

The loader is a build option, `DMM_LOADER` (off by default). The first
installation goes through the radio's update mode once
(`docs/update-mode.md`); everything after it goes through the loader.

## Flash layout

| Area | From | To (excl.) | Size | Written by |
| --- | --- | --- | --- | --- |
| Radio's boot loader | 0x08000000 | 0x08004000 | 16 KB | never |
| Stub | 0x08004000 | 0x08005000 | 4 KB | update mode only |
| Slot A (run slot) | 0x08005000 | 0x08078000 | 460 KB | loader (swap) |
| Slot B (spare slot) | 0x08078000 | 0x080EA000 | 456 KB | loader (download, swap) |
| Boot record | 0x080EA000 | 0x080EC000 | 2 x 4 KB | loader; the app appends its confirmation |
| Reserved | 0x080EC000 | 0x080ED000 | 4 KB | nobody yet |
| Loader | 0x080ED000 | 0x080F5000 | 32 KB | update mode only |
| Unused | 0x080F5000 | 0x080F5800 | 2 KB | never |
| Radio data | 0x080F5800 | 0x08100000 | 42 KB | never |

The numbers live in `services/include/dmm/services/d878uv2_boot_layout.h`
and the linker scripts in `boards/anytone_d878uv2/`. Every area is whole
4 KB sectors; the flash has 2 KB pages in its first 512 KB and 4 KB pages
above, so a sector there is two page erases.

Images are linked for slot A, so the app always runs from 0x08005000 and
its code stays in the first 256 KB, which the MCU executes without wait
states. An image may be as large as slot B. Slot A has one sector more,
for the swap.

Only one function erases or programs flash, behind a guard that lets
through slot A, slot B and the boot record and nothing else. The app can
only append to the boot record. Option bytes are never touched.

## Start-up

1. The radio's boot loader starts the **stub** at 0x08004000. The stub
   drives the transmitter, amplifier and speaker lines low, checks the
   loader's CRC-32 (stored right after the loader by `dmm-radio prepare
   --loader`) and starts it. A loader that fails the check is skipped and
   the image in slot A is started directly: the radio works, without
   updates or trials.
2. The **loader** reads the reset cause (and leaves the flags for the
   app), the hand-over block an image may have left in RAM (trusted only
   after a software or watchdog restart, and cleared once read), and at
   power-on the force key: **PTT and PF1** (the upper side key, below
   PTT) held keep it in loader mode.
3. It reads the boot record, finishes a swap in progress, and decides:

| Condition | Action |
| --- | --- |
| Asked to wait, or the force key | Loader mode |
| Asked to roll back, and slot B holds a confirmed image | Swap back, restart |
| A committed download in slot B (it passes its checks) | Swap it in as a trial, restart |
| Slot A on trial, started fewer than 3 times | Count the start, start it |
| Slot A on trial, 3 starts without a confirmation, or it fails its check | Swap the previous image back, mark the new one failed, restart |
| Slot A confirmed and good | Start it |
| Slot A fails its check, slot B holds a confirmed image | Swap back, restart |
| Nothing to start | Loader mode |

The loader starts the free watchdog at about 20 s as soon as it runs;
the app sets its own 2.5 s a moment after it starts. An image that hangs
before that is restarted and loses a start. All of this runs at the reset
clock. Loader mode starts the PLL and USB; the loader then always ends
with a restart, so the app starts from a reset state.

## Trial and confirmation

A new image runs as a trial. It is kept once it confirms:

- **automatic** (`dmm-radio update`): the app confirms itself after 60 s
  of running;
- **manual** (`dmm-radio update --try`): only `dmm-radio confirm` (or the
  console's `loader confirm`) confirms it.

Every start before the confirmation counts. After 3 the loader swaps the
previous image back and the app reports why (`loader:` line at start,
`dmm-radio info`). `dmm-radio rollback` brings the previous image back
while slot B still holds it, also after a confirmation; a second rollback
swaps again.

`dmm-radio update` while the running image is still on trial first brings
the confirmed image back, then sends the new one: slot B never loses the
only confirmed image.

## The swap

Slot A has one spare sector. Over the length of the larger of the two
images, every sector of slot A first moves up by one, from the top; then
for each sector i, B[i] goes to A[i] and the moved old A[i] to B[i]. Each
step erases and programs one sector and compares it, then the boot
record notes the step as done. A step's source stays intact until the
step after it, so after a power loss the loader redoes the step that was
not noted and goes on. Swapping back is the same procedure.

The loader keeps the power on (PowerHold) while it writes flash, so the
knob can be turned off; only a battery pulled during a swap is a real
power loss, and the record covers it.

## Boot record

Two 4 KB sectors, one in use. Each entry is 16 bytes: a kind with a
24-bit value, two 32-bit values and a check over them. Entries go into
erased flash one after another; one cut short by a power loss fails its
check and is skipped. When a sector fills up, the loader writes the
current state alone into the other sector, its header last, and uses
that one; of two sectors with a valid header the one with the higher
generation counts. The loader keeps a few entries free for the app's
confirmation; the app never erases.

## Hand-over block

Sixteen bytes at 0x20016FF0, just below the RAM the radio's boot loader
uses, in every image's linker script: a magic, the request (wait in
loader mode, roll back, or the test cut), an argument and a check. The
app writes it and restarts for `0xDD 'B'` on the data port and the
console's `loader` and `loader rollback`; they are refused while
transmitting or while a codeplug is written.

## Loader mode

One USB serial port with the radio's USB IDs, product "DeltaMM loader"
and serial number "DeltaMM-loader", so tools tell it from the app and
from update mode. The blue LED blinks; the red LED is on while flash is
written. Without the PC for 60 s, and with an image to start, the loader
restarts by itself.

The protocol (`services/include/dmm/services/loader_protocol.h`) uses the
framing of the app's data protocol: requests 0xDD and a letter, replies
with a status and a CRC-32.

| Request | Does |
| --- | --- |
| `i` | Versions, the slots (state, length, CRC, the image's version), trial state, last fallback, why it waits |
| `e` length | Erases slot B; refused while slot B holds the only confirmed image |
| `w` offset, length, data, CRC | Programs into erased slot B (up to 4 KB), reads back |
| `k` offset, length | CRC-32 of part of slot B |
| `c` length, CRC, mode | Checks the image in slot B (CRC, vector table, info block linked for slot A) and commits it |
| `b` mode | Restarts: boot, roll back, or (tests) restart after n swap steps |

Nothing is used until it is committed, so a broken upload leaves the
running image as it was. `dmm-radio update` picks up an upload that
broke off: chunks already in an otherwise erased slot B are checked with
`k` and kept.

## Image info block

Every image (app, loader, stub) carries 40 bytes right after its vector
table, at offset 0x150: "DMMI", the kind, the address it is linked for,
its end (from the linker) and the version. The loader refuses an image
linked for another address; `dmm-radio flash` refuses a slot image, and
`dmm-radio update` a plain one.

## Building

```sh
cmake --preset fw-d878uv2-loader -DDMM_STOCK_FIRMWARE=/path/to/firmware.CDD
cmake --build --preset fw-d878uv2-loader
```

The build folder `build/fw-d878uv2-loader/boards/anytone_d878uv2/` then
holds `deltamm_loader_slot.bin` (the app for slot A), `dmm_stub.bin` and
`dmm_loader.bin`. The build fails when the stub passes 4 KB, the loader
32 KB or the app slot B's 456 KB.

## First installation

```sh
dmm-radio prepare --loader build/fw-d878uv2-loader/boards/anytone_d878uv2 \
    /path/to/firmware.CDD build/fw-d878uv2-loader/package
dmm-radio flash build/fw-d878uv2-loader/package/DeltaMM_<version>_loader.CDD --wait 600
```

The set holds the stub, the app padded to the end of slot A, erased
bytes over slot B and the record, and the loader with its CRC-32: about
964 KB, about 80 s in update mode. At its first start the loader finds an
empty record, checks the app in slot A and adopts it as confirmed.

Flashing any other image through update mode (a plain DeltaMM image, the
factory firmware) replaces the stub; the loader is then no longer
started. `dmm-radio flash` refuses that after it has installed a loader,
unless `--replace-loader` is given. The radio's boot loader and the radio
data are never written, so update mode stays the way back in every case.

## On the PC: loader_sim

`tools/loader_sim` runs the same loader code with a file as the flash and
a pseudo-terminal as the USB port; a small stand-in answers the app's
data protocol from the image in slot A:

```sh
loader_sim --flash /tmp/flash.bin --link /tmp/radio --install deltamm_loader_slot.bin
dmm-radio update new.bin --try --port /tmp/radio
```

Killing it is a power cut: started again on the same file it goes on
from the record. `--op-delay <ms>` slows every flash operation, so a cut
can land inside a swap; `--confirm-after` and `--idle` shorten the 60 s
times. Images whose version contains "crash" fail at every start. The
host test `loader-sim-update` runs dmm-radio against it.

## Flash speed probe

`-DDMM_FLASH_PROBE=ON` (plain builds) adds identical code and data at the
start of the image, at 0x08040000 and at 0x08080000; the console command
`board flashprobe` reports cycles per pass of straight-line code and of
taken branches, and per word read, at the three places. The build warns
when code passes 0x08040000; `.text.hot` sections are placed first.
