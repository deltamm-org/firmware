# Architecture

DeltaMM is layered firmware on a small real-time kernel. Every piece of
hardware belongs to exactly one task, tasks talk through message queues,
and the app sees hardware only through abstract parts. The same app code
therefore runs on a radio and in the desktop simulator.

## Layers

| Layer | Folder | May use | Examples |
| --- | --- | --- | --- |
| App | `app/` | services, os, lib | screens, menus, FM and DMR features |
| Services | `services/` | drivers, os, lib | radio control, storage, log, power, the interfaces |
| Drivers | `drivers/` | hal, lib | USB device stack, FM transceiver, DMR baseband, display, SPI NAND |
| HAL | `hal/common/`, `hal/cpu/<core>/`, `hal/mcu/<family>/` | lib | start-up, clocks, GPIO, watchdog, timers, USB device |
| OS | `os/` | lib | tasks, queues, mutexes, time |
| Library | `lib/` | nothing | fixed-size containers, formatting, hashes |

Each layer is its own CMake library and links only to what the table
allows, so a wrong include fails the build. `boards/<name>/` is the one
place that sees every layer: it builds the peripherals and drivers as
static objects and hands the services and the app their parts.

A radio board folder keeps its pin list in `pins.h` and one file per
board-level service (display, input, ...), with `main.cpp` wiring them up.
It also has a `board.cmake`, read before the layers are
built: it names the MCU family and the kernel port and provides the
kernel configuration.

The simulator boards implement the services' parts directly with Qt,
so they replace the drivers and HAL, not the app.

## Parts: virtual or chosen at build time

A part the app or a service uses comes in one of two forms.

- A virtual interface where it has a reason to be one: one image holds
  several implementations at once (USB functions), or shared code is
  tested against stand-ins (the buses under the chip drivers, the USB
  controller, the display, audio hardware, storage, codeplug, log sink,
  and the board hooks of the chip services). These calls are rare or move
  whole blocks.
- Otherwise a concrete type the board names in its parts header,
  `boards/<name>/include/dmm/board/parts.h`, the one board header shared
  code includes: `board::Fm`, `board::Baseband`, `board::Calibration`,
  `board::Input`, `board::Battery`, `board::DataChannel`, `board::Gps`,
  `board::GpsReceiver`, `board::Rtc`. The shared header writes the part
  down as a C++20 concept, and the board checks its type against it. Calls
  are direct. The headers in the board's `include/dmm/board/` include no
  MCU headers.
- LEDs, log output and system control stay virtual: the two simulators
  implement them differently and share one build of the app.

Every radio has an FM transceiver. A radio without a DMR baseband names
`services::NoBaseband`; the app reaches the baseband only through
`app::withBaseband`, so no baseband code is built into that radio. The
simulators' parts are in `boards/sim_common/include`.

The service interfaces are abstract: the app sees frequencies, levels and
states, never a chip's registers or a radio's data formats. Code for one
chip sits in files named after it (`rda1846_transceiver`,
`sct3258_baseband`); code for one radio's data formats in files named
after the model (`d878uv2_calibration`, `d878uv2_codeplug`). Values only
a chip understands, such as its tuning tables, codec wiring and
calibrated gains, go from the board straight to that chip's service.

## Operating system

On a radio, `os/` sits on the FreeRTOS kernel with static allocation only:
every task, stack and queue is a static object and the kernel has no heap.
The tick is 1 kHz. The idle task sleeps the CPU until the next interrupt,
and power saving is part of each feature from the start: every task waits
on a queue or timer, and anything that needs the receiver holds a named
stay-awake reason. Periodic work uses `os::sleepUntil` with its own period, which
keeps loops drift-free.

On the PC, the same `os/` API is implemented with Qt threads, so the
simulator and tests run alike on Linux, Windows and macOS. Priorities are
not modelled there; anything timing-critical is tested on hardware.

## Tasks

Each task owns its hardware and has one inbox queue. Higher number = more
urgent.

| Prio | Task | Owns |
| --- | --- | --- |
| 7 | radio | FM transceiver, DMR baseband, PA power, RF switches, GPS supply, transmit timeout |
| 6 | audio | speaker amplifier and routing, beeps |
| 5 | input | keypad, side keys, PTT, channel knob, volume knob (every 10 ms) |
| 4 | ui | screens, menus, key lock, codeplug reading, display, backlight |
| 3 | gps | GPS receiver's serial link: NMEA sentences, fix, when the receiver sleeps |
| 2 | link | USB: data protocol, log console |
| 2 | aprs | BT+APRS board's serial link: beacons, received packets, heard list |
| 1 | system | power latch, battery, watchdog, calibration, real-time clock |

Messages are small `std::variant`s of plain structs. Commands go down
(`ui → radio`), events go up (`radio → ui`). No task shares mutable state
with another.

Safety rules that the structure enforces:

- PTT goes `input → ui → radio`, so the key lock decides in one place.
- The radio task keeps its own transmit timeout and band limits, and any
  other command to it ends a transmission first.
- Packets go out from the radio task too: simplex on their own
  frequency, low power by default, without a tone and with the microphone
  muted, once the channel is free. PTT or a channel change gives a waiting
  packet up.

## Memory

The core folders never use the heap. The display has no frame buffer:
drawing goes straight to the panel.

## Storage

The data flash is read through `services::Storage`, which follows the
radio's bad-block table. It is read only: the driver has no write or erase
operation, and the tasks that read it share it behind a lock.

## PC protocol

The radio has two USB serial ports: a data port and the log. The data
port speaks DeltaMM's own binary protocol (`services/data_link.h`):
requests start with 0xDD, which no command of the radio's programming
protocol starts with.

## Update mode

The vendor boot loader rewrites the firmware image when the radio is
switched on holding PTT and PF3. `tools/dmm_radio` speaks its protocol
(`dmm-radio flash`). The boot loader itself is never written.
`docs/update-mode.md` describes update mode.

## Data on the radio

Settings, codeplug and calibration are kept in the radio's existing
formats, so the radio can be switched back to its factory firmware without
losing anything, and existing codeplug tools keep working.

Calibration is read-only by default. Without valid calibration the radio
receives but refuses to transmit.

## Backup

`tools/dmm_radio` backs up the radio's per-unit data (calibration and radio
information) once, before the first DeltaMM image goes onto that radio,
over the factory firmware's programming link. Preparing a flash image
refuses until a verified backup of the connected radio exists; a setting
turns that check off. Backups are stored outside the repository, and
restore is never automatic.

## SCT3258 boot program

The SCT3258 DMR baseband chip needs a 2 KB boot program made for that
chip, loaded into it at every power-on. The build takes it from the
user's own factory firmware file (`-DDMM_STOCK_FIRMWARE=<file>`), cuts it
out at a known offset, checks its hash and links it in. Features that use
the SCT3258 are built when the option is given.

## AnyTone D878UVII

- MCU: GD32F303 (Cortex-M4F, 1 MB flash, 96 KB RAM), 12 MHz crystal, run at
  120 MHz. The vendor boot loader occupies the first 16 KB; DeltaMM starts
  at `0x08004000`, never touches the boot loader, and ends below
  `0x080F5800`, where radio data starts.
- Power: the knob switches the supply on; an MCU output holds it on, and
  the MCU reads the knob to know when to let go.
- FM: RDA1846/AT1846S transceiver on a 3-wire serial link. One of its
  GPIO pins mutes the microphone (high = muted).
- DMR: SCT3258 baseband on an 8-bit parallel host port. It also carries the
  FM audio path, so audio needs the SCT3258 boot program too.
- Speaker: an MCU pin switches the amplifier on, a second one connects the
  codec's receive audio to it. Beeps are an MCU PWM tone mixed in before the
  amplifier, so they need neither the codec nor the baseband.
- Clock: the MCU's real-time clock on a 32.768 kHz crystal, kept running
  from the battery while the radio is off. It counts UTC seconds since
  1970; backup data register 0 holds 0x5050 once it is set up.
- GPS: an ATGM336H module (AT6558 chip, CASIC command set, as the
  receiver reports itself) sending NMEA at 9600 baud, 8N1, on USART1 (PA3
  in, PA2 out). Its supply is the FM transceiver's GPIO2, so the radio task
  switches it; the gps task owns the serial link and sets the receiver up
  through `casic_receiver`. The module has no backup supply and ignores
  the CASIC standby command, so switching the supply off is the only
  sleep, and every power-on is a cold start: from half a minute to a few
  minutes to a fix indoors. `gps_schedule` decides when the supply is on from how often
  each client needs a position.
- APRS sending: AFSK samples go to the beep PWM (PB9, TIMER3 channel 3,
  32 kHz samples), which also reaches the transmitter's modulation input
  while the microphone is muted (the FM transceiver's GPIO3).
- APRS receiving: the BT+APRS board, a daughterboard with its own MCU,
  decodes the receiver's audio and sends each packet it decodes to the
  radio over UART3 (PC10 to it, PC11 from it); PA1 powers it
  (`d878uv2_link_board`).
- Codeplug APRS settings and beacon comment: read by `d878uv2_codeplug`.
- Display: 160 × 128 colour panel on an 8-bit parallel bus.
- Storage: SPI NAND flash.
- USB: full-speed device on PA11/PA12, clocked from the crystal PLL; a
  board pin switches the D+ pull-up. DeltaMM uses the pid.codes test IDs
  (1209:0001) during development.

## Testing

1. Host unit tests (doctest, built without exceptions or RTTI).
2. Simulator runs.
3. Firmware build checks: size, heap-free core (both on every build).
4. A hardware checklist per release in `docs/hw-tests/`.
