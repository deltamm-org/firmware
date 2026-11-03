# DeltaMM's loader

The loader (`docs/loader.md`): the stub at 0x08004000, the loader at
0x080ED000, the app in slot A at 0x08005000, updates over USB into slot
B with a trial, a confirmation and a fallback. Nothing in these steps
transmits.

## Before you start

- A host build of this branch (`dmm-radio`), and a verified backup of the
  radio (`dmm-radio backup`, while on factory firmware).
- A loader build: `cmake --preset fw-d878uv2-loader
  -DDMM_STOCK_FIRMWARE=<factory .CDD>`, then build it. Its folder holds
  `deltamm_loader_slot.bin`, `dmm_stub.bin` and `dmm_loader.bin`.
- Test images, each a loader build with a different version and, where
  named, a change made for the test only (never shipped):
  - **good B**: the same code, another version (a dirty tree or a local
    version bump is enough);
  - **fault**: `system.testFault()` called in `systemMain` right after the
    reset report: crashes at every start;
  - **hang**: `for (;;) {}` at the start of `boardMain`, before the
    watchdog is set: hangs at every start;
  - **no-confirm**: any good image, sent with `--try` and never confirmed.
- The log port open in a terminal, opened again each time the radio's
  USB ports come back after a restart.
- The factory firmware set at hand: update mode stays the way back at
  every step (`dmm-radio flash <factory .CDD> --replace-loader`).

## Flash speed probe (optional)

| # | Do | Expect |
| --- | --- | --- |
| 0.1 | Plain build with `-DDMM_FLASH_PROBE=ON`, `prepare`; radio off, hold PTT and PF3, switch on; `flash` | The image (about 512 KB) starts as usual |
| 0.2 | Console `flashprobe`, three times | `flash probe, cycles below 256K/past 256K/bank 1: code pass .../.../... jump pass ... word read ...`; the same within a few percent each time. Note the three columns: the first is the zero-wait case |

## First installation (once, at the radio)

| # | Do | Expect |
| --- | --- | --- |
| 1 | `dmm-radio prepare --loader <loader build folder> <factory .CDD> <out>` | Stub, app and loader sizes and versions; `DeltaMM_<version>_loader.CDD/.spi/.CDI` written |
| 2 | Radio off; hold PTT and PF3 and switch on (red LED blinks). `dmm-radio flash <out>/DeltaMM_<version>_loader.CDD --wait 600` | About 80 s; `Done`, then `Running: DeltaMM <version>` |
| 3 | Log port, from the start | `last reset: ...`, then `loader: image confirmed, previous image not kept, last fallback: none`; no `crash:` |
| 4 | `dmm-radio info` | The version and `Loader: image confirmed; previous image not kept` |
| 5 | Radio off; hold PTT and PF3 and switch on | Update mode as before (red LED blinks): the stub does not stand in its way. Switch off and on normally |

## From the PC

| # | Do | Expect |
| --- | --- | --- |
| 6 | `dmm-radio loader` | `Restarting into the loader`, then the loader's port: `Waiting: asked for`, run slot confirmed with its version, spare empty. Blue LED blinks |
| 7 | Do nothing for 70 s | The loader restarts by itself; the app runs (log `last reset: software`) |
| 8 | `dmm-radio update <good B> --try` | Upload, `committed`, `Swapping`; within about a minute `Running: DeltaMM <B>`, `on trial (confirm with dmm-radio confirm), start 1 of 3; previous image kept` |
| 9 | `dmm-radio confirm`, then console `restart` | `Confirmed`; after the restart still B, `image confirmed` |
| 10 | `dmm-radio rollback` | Within about a minute the first version runs, confirmed; `Fallback: asked for`; previous image kept |
| 11 | `dmm-radio rollback` again | B again, confirmed |
| 12 | `dmm-radio update <good A>` (automatic) | A runs on trial (`confirms itself after 60 s`); after 60 s the log says `loader: image confirmed after 60 s`; `dmm-radio info` agrees |
| 13 | `dmm-radio update <fault> --try` | The new image crashes 3 times (`crash: test fault` in the log of the image after), then the previous one runs: `Fallback: the new image used its starts without being confirmed`; `dmm-radio info` in loader mode (`dmm-radio loader`) shows the spare `failed` |
| 14 | `dmm-radio update <hang> --try` | Three starts that hang until the loader's watchdog (about 20 s each, about a minute in all), then the previous image runs with the same fallback |
| 15 | `dmm-radio update <no-confirm> --try`, then console `restart` three times | After the third restart the previous image runs; the trial image is marked failed |
| 16 | `dmm-radio update <good B> --test-refusals` | `Bad chunk: refused (good)`, `Wrong CRC: commit refused (good)`, `Restarted without a commit`; the running version unchanged |
| 17 | `dmm-radio update <good B>`; kill dmm-radio by its PID while the percentage runs; run the same command again | The first run stops; the radio stays in loader mode, then restarts the old image by itself after 60 s. The second run says `... chunks, N already there; committed` and B runs |
| 18 | `dmm-radio update <good A> --swap-cut 20` | `Test: the loader restarts after 20 swap steps`; the swap resumes after the restart and A runs on trial |
| 19 | During the swap of an update (red LED on, after `Swapping`), turn the knob off | The red LED stays on until the swap ends (10–15 s); then the radio goes off. Switched on: red while the loader checks the new image, then red and blue blinking while it waits briefly for a PC still asking; then the new image runs on trial |
| 20 | 20 updates in a row, alternating two good images, each without `--try` and 70 s apart (a shell loop) | All 20 succeed; `dmm-radio info` at the end shows the right version, confirmed; no `crash:` in the log |
| 21 | Radio off; hold PTT and PF1 (the upper side key), switch on | Loader mode (blue LED blinks), `dmm-radio info` shows `Waiting: force key held at power-on`; after 60 s without the PC the app starts |
| 22 | Read the latest uptime line | Every task above 50 unused stack words |

Optional, at the radio: during the swap of step 19 or 20, pull the
battery instead of turning the knob; on the next start the swap resumes
and the new image runs.

Undo: `dmm-radio flash <plain image set or factory .CDD> --replace-loader`
in update mode (PTT+PF3).

## Results

Run 2026-11-01 on a D878UVII PLUS; steps 1-4 again and step 21 with the
force key PTT and PF1 on 2026-11-02.

| # | Result |
| --- | --- |
| 0.1, 0.2 | Pass. Cycles below 256 KB / past it / bank 1: straight-line code 67.00 / 68.15 / 68.15, taken branches 67.00 / 67.86 / 67.86, word reads 7.01 / 25.82 / 25.80 |
| 1-4 | Pass: installed through update mode, `image confirmed` |
| 5 | Pass: update mode as before |
| 6, 7 | Pass: loader mode on request; it restarts the app after 60 s without the PC |
| 8 | Pass: 31 s from the start of the update to the new image running |
| 9-11 | Pass: confirm, restart, rollback twice |
| 12 | Pass: confirmed by itself after 60 s |
| 13 | Pass: fell back after three crashing starts; the crash record shown after |
| 14 | Pass: fell back through the loader's watchdog |
| 15 | Pass: fell back after three restarts without confirming |
| 16 | Pass: the bad chunk and the wrong CRC refused |
| 17 | Pass: resumed at 77 %, 15 chunks sent, 56 already there |
| 18 | Pass: the swap resumed from the record after 20 steps |
| 19 | Pass: the red LED stayed on 10-15 s until the swap ended, then the radio went off; the next start finished normally |
| 20 | Pass: 20 of 20, no crash in 29 starts |
| 21 | Pass: blue LED, `Waiting: force key held at power-on`; the app started by itself about 60 s later |
| 22 | Pass: every task above 50 unused stack words |
