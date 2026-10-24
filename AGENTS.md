# AGENTS.md

DeltaMM: from-scratch C++20 firmware for DMR/FM handheld radios, plus a
desktop simulator and PC tools. `README.md` has the overview,
`ARCHITECTURE.md` the structure. This file covers rules that are easy to
break by accident.

## Setup commands

```sh
cmake --preset host -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.x/gcc_64
cmake --build --preset host
ctest --preset host
cmake --preset fw-d878uv2-release && cmake --build --preset fw-d878uv2-release
```

The firmware presets need the Arm GNU Toolchain on `PATH` or its root folder
in `ARM_GNU_TOOLCHAIN`.

## After any source change

1. Host build and `ctest --preset host`.
2. Firmware build for every board that compiles the touched code.
3. `clang-format --dry-run --Werror` over every `.h`/`.cpp` outside `build/`.
4. `reuse lint`.

## Code conventions

- Names: types `PascalCase`, functions and variables `camelCase`, data
  members `m_camelCase`, constants and enum values `PascalCase`. Headers
  are `.h`, included with quotes (`#include "dmm/lib/ring_buffer.h"`).
- Core folders (`lib/`, `os/`, `hal/`, `drivers/`, `services/`) never
  allocate from the heap, never throw and never use RTTI. The
  `core-heap-free` test checks their object files. `app/` and `tools/` may
  use the heap.
- A layer includes only from the layers below it (`app` → `services` →
  `drivers` → `hal`), plus `os/` and `lib/`. Only `boards/` sees every layer.
- `hal/` and `drivers/` never include FreeRTOS or Qt headers.
- Fallible calls return a result or error code; nothing throws.
- Hardware facts (registers, bit meanings, board wiring, levels) are
  written in our own words, with the chip's documentation named where it
  is the source.

## Shared code stays abstract

- Code outside chip-named, model-named and `boards/` files knows no radio
  model, vendor or chip, in code or comments.
- Shared interfaces are fully abstract. They speak in the app's terms
  (Hz, dB, dBm, mV, levels, on/off) and never in a chip's: no register
  addresses, register or bit layouts, value widths or ranges, chip
  scales, raw packets or byte blocks, chip pin or channel names, vendor
  key names or data formats. A value the interface cannot name in the
  app's terms does not belong in it.
- Chip facts live in chip-named files (`rda1846*`, `sct3258*`,
  `st7735*`, `gd32f30x/`); a radio's data formats in model-named files
  (`d878uv2_*`); wiring, levels and tuning tables in `boards/<name>/`.
- Values only a chip understands go from the board straight to that
  chip's service (a table or a source function the board supplies), not
  through the app.
- Debug access is the chip service's own: it parses its console commands;
  the app only forwards the text.

## Never in the repository

- Vendor firmware bytes, vendor tools, or code or names taken from
  decompiled vendor firmware. The SCT3258 boot program is cut out of
  the user's own firmware file at build time and stays in `build/`.
- Personal data: names other than in copyright notices, callsigns, DMR
  IDs, locations, channel or zone names and frequencies from a real
  codeplug, calibration values, backup dumps, serial numbers, keys.
  Callsigns may appear only in `CREDITS.md`.
- Files under a copyleft licence.

## Comment style

- Clear code first; a comment only where the code cannot say it.
- A comment states what the code does, impersonally and briefly. No
  design history, no test stories, no comparisons with other firmware.

## Commits, versions, and tags

- Subject: imperative, capitalized, no trailing period. Body (if any) up to
  about 5 lines on what the change does. Whole message about 480
  characters, 600 at most.
- No trailers, no test procedures or results, no personal data, no
  comparisons with other firmware. Test steps live in `docs/hw-tests/`.
- Don't reference files outside this repository.
- Each release that adds something testable on hardware bumps the version
  and gets a `docs/hw-tests/` checklist. Tag right after the bump.
