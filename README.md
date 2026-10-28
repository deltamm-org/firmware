# DeltaMM

Open firmware for DMR and FM handheld radios, written from scratch in
C++20.

<img src="docs/images/standby.png" width="320" alt="DeltaMM standby screen: zone, channel name, frequency, mode, tone, power and offset">

The first target is the AnyTone D878UVII. The radio features sit apart
from the board support, so a new radio, or the desktop simulator, is a
new board folder rather than a fork.

DeltaMM is for licensed radio amateurs. It is not affiliated with or
endorsed by AnyTone or the makers of the chips it drives.

## Features

**FM radio.** Receive and transmit using the radio's own factory
calibration. Squelch levels, repeater offsets, four power levels, a
transmit timeout and amateur-band limits. CTCSS and DCS tones both ways,
with the codeplug's squelch mode (implemented, not yet tested on the
radio).

**DMR radio.** Voice calls so far: receive and transmit through a
repeater, using the radio's own calibration. Calls on the channel's
talkgroup, its receive group list or to the radio itself are heard,
others stay muted; through a repeater only the channel's slot. A heard call fills the screen with the
caller's callsign and name from the codeplug's contacts and user
directory. PTT wakes an idle repeater and calls the channel's contact.
Direct (simplex) calls, voice enhancement and the shaped amplifier
keying at High and Turbo are implemented, not yet tested on the radio.

**Channels.** Channels, zones and the VFO come from the codeplug already
on the radio. Pick zones from a list, step channels with the knob, or type
a frequency on the keypad.

**Interface.** A first version of the user interface: a standby screen
with zone, channel, frequency, tones, power, volume and battery, and a
short grouped settings menu. Key lock
covers the keypad, side keys and knob, with auto lock and forced lock.

**Battery.** Battery save cycles the receiver while the radio is idle,
with the codeplug's setting, and the backlight goes off after its light
time. A battery monitor and auto power-off.

**Platform.** Beeps, volume and backlight. A USB log with a debug
console, and crash details kept across restarts.

**Tools.** `dmm-radio` backs up the radio and prepares the update files.
A desktop simulator runs the same firmware with a drawn radio face.

## Roadmap

1. Audio level adjustment: volume range and steps, FM and DMR levels in
   both directions, beeps and tones
2. Refined user interface: screen layouts, menus, icons and fonts tuned
   as features are added
3. Programming over USB, compatible with existing codeplug tools
4. Scan and dual watch
5. GPS
6. Calibration tool
7. APRS, Bluetooth, recording, DTMF and selective calling
8. Complete DMR: talkgroups and contacts picked on the radio, private
   call set-up and replies, call log, talker alias, radio check and the
   other DMR functions, roaming, SMS and data

## Getting started

### 1. Build

Host build: unit tests, simulators and `dmm-radio`. It needs Qt 6.8 with
Qt Quick and Qt Serial Port; without Qt, the core libraries and their
tests build.

```sh
cmake --preset host -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.x/gcc_64
cmake --build --preset host
ctest --preset host
build/host/boards/sim_qt/simqt     # the simulator
```

Firmware, with the Arm GNU Toolchain 14.2 or newer on `PATH` or in
`ARM_GNU_TOOLCHAIN`:

```sh
cmake --preset fw-d878uv2-release -DDMM_STOCK_FIRMWARE=/path/to/firmware.CDD
cmake --build --preset fw-d878uv2-release
```

If a configure failed, for example before the toolchain was found,
configure again from scratch with `--fresh` (or delete the build folder):
a failed first configure can leave the build without optimisation, and
such firmware does not start.

The D878UVII's DMR baseband chip, the SCT3258, needs a 2 KB boot program
made for that chip, loaded into it at every power-on. The build takes it
from your own copy of the radio's factory firmware update file
(`DMM_STOCK_FIRMWARE`) and checks its hash. Everything that uses the
SCT3258, FM audio included, is built when this option is given.

### 2. Back up the radio

Save the radio's per-unit data (calibration and radio information) while
the factory firmware is still on it:

```sh
dmm-radio backup          # 2-5 minutes, includes the whole system area
dmm-radio backup --quick  # calibration and radio information only
dmm-radio list
```

Backups go to your user data folder, outside this repository. A backup is
marked verified once every check passes.

### 3. Flash

DeltaMM is sent through the radio's own update mode (power on holding
PF1 + PTT) as three update files. They are made from the DeltaMM image
and your factory `.spi` and `.CDI` files, once a verified backup exists:

```sh
cmake --preset fw-d878uv2-release -DDMM_STOCK_FIRMWARE=/path/to/firmware.CDD \
    -DDMM_RADIO_TOOL=/path/to/dmm-radio
cmake --build --preset fw-d878uv2-release --target update-files
```

The radio's boot loader stays as it is, so the factory firmware can be
flashed back the same way at any time. Each release has a hardware
checklist in `docs/hw-tests/`.

## Using the radio

| Key | Standby screen |
| --- | --- |
| Channel knob, Up/Down | Next or previous channel, or VFO step |
| `0`-`9` | Type a frequency |
| `#` | Switch between VFO and memory |
| P1 | Zone list |
| P2 | Channel settings |
| Menu | Settings menu |
| Hold `*` | Lock keys; Menu then `*` unlocks |
| Hold Exit | Cycle standby, log and key test screens |

## Log and console

A radio running DeltaMM shows up as two USB serial ports, "DeltaMM data"
and "DeltaMM log". Open the log port with any terminal program at any
speed; lines written before the port was opened arrive first. Type `help`
for the console commands.

## Project layout

| Folder | Contents |
| --- | --- |
| `lib/` | Containers and utilities, heap-free and OS-free |
| `os/` | Task, queue and mutex API: FreeRTOS on radios, Qt threads on the PC |
| `hal/` | CPU core and MCU support, one folder per MCU family |
| `drivers/` | Chip drivers |
| `services/` | Radio, storage, log, power and the interfaces the app uses |
| `app/` | User interface and features |
| `boards/` | One folder per radio, plus the simulators |
| `tools/` | PC tools, `dmm-radio` among them |
| `tests/` | Host unit tests |
| `docs/hw-tests/` | One hardware checklist per release |

`ARCHITECTURE.md` describes how the parts fit together, `AGENTS.md`
lists the coding and commit rules, and `CREDITS.md` thanks the people who
helped.

## Disclaimer

DeltaMM comes without any warranty. Installing it voids the radio's type
approval for anything but amateur use; you are responsible for
transmitting only where and how your licence allows. AnyTone and the chip
names used here are trademarks of their owners.

## Licence

MIT, see `LICENSE.md`. The Spleen font in `third_party/spleen/` is
BSD-2-Clause. The repository follows the REUSE specification.
