# Real-time clock calibration

Menu, GPS and time, Calibrate measures how fast the real-time clock runs
against GPS time for 60 or 30 minutes, keeping the receiver on, and then
corrects the clock's rate. The correction stays with the clock while the
radio is off. The console has `time cal <min>` (0 stops) and
`time cal reset`.

## Before you start

- The log port open in a terminal, and a PC whose clock is right (NTP).
- A view of the sky for the whole run.
- GPS on.

## Steps

| # | Do | Expect |
| --- | --- | --- |
| 1 | Console `time cal reset`, then `time` | `clock: rate correction +0.0 ppm (was ...)`; `time` shows no rate correction |
| 2 | Menu, GPS and time: look at Calibrate | `None` |
| 3 | Calibrate: 30 min | `clock: calibrating against GPS for 30 min`; the item counts `1/30 min`, `2/30 min`, ...; the receiver stays on |
| 4 | Wait for the end | `clock: calibration: the clock ran <rate> (<s> s a day) over <n> seconds, scatter <ms> ms`, `clock: rate correction <-rate>`; the notice `Clock calibrated`; Calibrate shows the correction |
| 5 | Switch the radio off for a minute and on again | `clock: <time> UTC, rate correction <the same>` |
| 6 | Calibrate: 30 min again | The clock now runs within about 5 ppm (`0.4 s a day`) |
| 7 | Start a run, then Calibrate: Stop | `clock: calibration stopped`; the correction is unchanged |
| 8 | Start a run indoors far from a window, or with GPS off | Without enough fixes: `Too few fixes` and the correction unchanged; with GPS off: `Switch GPS on` |

## Results

2026-10-27: all steps pass. Before calibration the clock ran +32.7 ppm
against an NTP-synced PC (2.8 s a day). Six 30-minute runs by a window
measured the rate within 0.1 to 4.9 ppm of the PC's value, 2.8 ppm RMS.
The error follows the receiver's sentence delay, which wandered between
24 and 40 ms; the crystal stayed within a ppm all afternoon.
