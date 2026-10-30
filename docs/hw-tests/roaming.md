# DMR roaming

A DMR channel with roaming on takes its frequencies, and its colour code
when the repeater sets one, from a repeater of the codeplug's roaming
zone; its talkgroup and slot stay the channel's. One channel per
talkgroup then covers every repeater of the zone.

This is basic roaming: the radio picks a repeater by trying them all and
comparing signal strength. Roaming as the ETSI DMR standards describe it
is still to come.

Codeplug (D878UVII format, as codeplug tools such as QDMR write it):

- Roaming repeaters: receive and transmit frequency, colour code (or the
  channel's), name. Their time slot is not used.
- Roaming zones: up to 64 repeaters each. The general settings' default
  roaming zone is the one used at start.
- A digital channel's roaming setting.
- Automatic roaming: on or off; start when the repeater has been quiet for
  the delay, or every period.
- A side key (PF1-PF3, short or long press) set to roaming.

Between calls the radio learns the beacon of the repeater in use and
visits the zone's other repeaters, one at a time, to keep their levels:
see `docs/roaming.md`.

A search tries the zone's repeaters, most likely first: the one in use,
then the repeaters heard lately (strongest first), then the rest of the
zone from where the last search left off. At each it listens 150 ms for
one already on the air, else sends one wake-up block at the channel's
power and waits for the repeater's timing. It stops at the first
repeater at -90 dBm or better; otherwise the strongest that answered is
used from then on, by every roaming channel. An automatic search moves
only to a repeater at least 6 dB stronger.

The radio keeps the last 8 repeaters heard, with their level. Menu >
Channel > Roam status lists them, updated each second: position (a star
on the one in use), output frequency, beacon period when known, level
and its age; its last row, Search now, searches. `roam` logs the same
list.

A search starts:

- on a side key the codeplug sets to roaming, or `roam now`;
- on PTT when no repeater is in use yet, and when the repeater in use
  does not answer the wake-up; the call goes out if PTT is still held;
- by itself, when automatic roaming is on: after the repeater in use has
  been quiet for the codeplug's delay (at least 30 s), or every period
  (at least 1 min).

PTT, a channel change or another search ends a search early.

Menu > Channel > Roaming Off makes every channel work on its own
frequencies until it is switched on again or the radio restarts. Menu >
Channel > Roam zone picks another of the codeplug's roaming zones until
restart; the radio then looks for a repeater afresh.

The standby screen shows the channel name large; under it, small, the
repeater's output frequency, the slot, the colour code and the
repeater's name, or `Searching...`.

## Before you start

- The log port open.
- A codeplug with a roaming zone set as the default one, at least two of
  its repeaters in reach (one stronger than the other helps), and a few
  talkgroup channels with roaming on.
- PF3 set to roaming.
- A way to see the network's last heard list.

## Steps

| # | Do | Expect |
| --- | --- | --- |
| 1 | Flash and start | `roam: N repeaters in the roaming zone; automatic ...`; no `crash:` |
| 2 | Pick a roaming talkgroup channel | The name large; under it the frequency, `TS`, `CC` and the repeater's name when the channel's own frequency is one of the zone's |
| 3 | Press PF3 | `Searching...`; `roam: searching N repeaters (asked)`; per repeater `on the air`, `answered the wake-up` or `no answer`, with dBm; then `now through repeater i` or `staying on repeater i`; the line shows that repeater |
| 4 | Turn the knob to another roaming channel | No search; the same repeater, the new channel's slot |
| 5 | PTT on a roaming channel, repeater idle | `repeater wake-up 1: answered`, `transmitting` on the repeater's input; the network shows the call on the channel's talkgroup |
| 6 | Restart on a roaming channel whose own frequency is not in the zone; PTT and hold | `searching ... (for a call)`, then the call goes out on the chosen repeater |
| 7 | As 6, but release PTT during the search | Nothing transmitted after the search |
| 8 | PTT while the repeater in use is out of reach (or off) | `the repeater did not answer`, a search, then the call on another repeater or `No repeater` |
| 9 | Automatic roaming on with a delay, the repeater in use quiet for longer | `searching ... (automatic)` |
| 10 | PF3 on an FM channel | Notice `Not roaming` |
| 11 | Menu > Channel > Roaming: Off, on a roaming channel | `roam: roaming off`; the channel's own frequency, the usual frequency line; PF3 says `Not roaming`. On again: the repeater in use comes back |
| 12 | Menu > Channel > Roam zone: another zone | Its name in the menu; `roaming zone N, M repeaters`; the line shows that zone's repeater after PF3 |
| 13 | `roam` | The repeater in use, its frequency and colour code, automatic on or off |
| 14 | Read the latest uptime line | Every task above 50 unused stack words |
| 15 | After a restart, PF3 with one repeater on the air | The search stops at that repeater (`now through repeater N`) without trying the rest of the zone |
| 16 | PF3 again | Only the repeater in use is tried: `staying on repeater N` |
| 17 | Menu > Channel > Roam status | The repeater in use with a star, its level and an age counting up; Menu on Search now searches, Exit goes back |
| 18 | Leave the radio on a roaming channel with a repeater sending timed beacons, 3 minutes | `roam` shows that repeater's beacon period (within 0.1 s of the repeater's setting) and the next beacon's time; Roam status shows the period |
| 19 | Keep watching the log | About once a minute, once the repeater in use has been quiet for 10 s and no key pressed for 5 s: `visit to repeater N (sweep): nothing` for the zone's other repeaters; never one during a call |
| 20 | Press PTT during a visit (watch the log for `visit to`), or `ptt 2000` on the console | The visit ends at once (`cut short`) and the call goes out as usual |
| 21 | Switch the repeater's beacons off (or the repeater) | `beacon of repeater N missed` twice; about 3 minutes after the last beacon, Roam status shows `N Lost` |
| 22 | Switch it back on | Its next beacon brings Roam status back to `N Good` |
| 23 | With two repeaters on the air at different distances, `roam auto on`, move away from the one in use | Visits learn the other's beacon; when the one in use falls below -95 dBm and the other is 6 dB better, `moving from repeater N (weak) to repeater M` |
| 24 | Codeplug: repeater range check on, 10 s, 3 tries, notice on; the repeater off | A wake-up of the repeater every 10 s while it is not heard; after the third unanswered, `repeater N out of range` and an `Out of range` notice |
| 25 | Codeplug: automatic roaming on, quiet time 0; a repeater whose beacon comes a fixed time (60 s) after its last transmission; restart on its roaming channel | Searches 30 s, 60 s, then 120 s apart, not every minute; its beacon is heard between them and `roam` shows its period within about 5 minutes |

Undo: flash the previous release.

## Results

Run 2026-10-28 on a D878UVII PLUS with a development build of 0.20.3 plus
roaming and codeplug programming, a roaming zone of 15 repeaters, PF3 set
to roaming, and a duplex hotspot as the only repeater on the air.

| # | Result |
| --- | --- |
| 1 | Pass: `15 repeaters in the roaming zone; automatic off` |
| 2 | Pass: the frequency, slot, colour code and repeater name under the channel name |
| 3 | Pass: with no repeater on the air, all 15 `no answer`, then `no repeater answered`; the radio stayed on the repeater in use. With the hotspot on, the search chose it (`in use: 2`) |
| 10 | Pass: `Not roaming` with roaming off |
| 11 | Pass: Roaming Off made the channels work on their own frequencies |
| 12 | Pass: of three roaming zones, the two with the hotspot's pair found it after PF3; the third, without it, found nothing |
| 4-9, 13, 14 | Open (run on 2026-10-31, below) |

Steps 4 to 9, 13 and 14, run 2026-10-31 on 0.22.0 with this and the other
open pull requests merged, beacon roaming among them, the same zone and
hotspot, and the codeplug's automatic roaming on with a quiet time of 0:

| # | Result |
| --- | --- |
| 4 | Pass: the knob to two other roaming channels kept the hotspot with no search; slot 1, then slot 2, as each channel says |
| 5 | Pass: wake-up answered, the call through the hotspot to the Parrot, its reply received |
| 6 | Pass: channel 1 moved off the zone's frequencies; after a restart, PTT started `searching ... (for a call)` at once, with nothing sent on the channel's own frequency; the call went out through the hotspot 1.8 s after PTT |
| 7 | Pass: PTT released during the search for a call; the search found the hotspot and nothing was transmitted after it |
| 8 | Pass: on a channel whose repeater (1) is off the air, PTT woke it twice without an answer, searched for the call, found the hotspot and sent the call through it 2.9 s after PTT |
| 9 | Pass: 35 s after start a search by itself; repeater 1 did not answer two wake-ups, the hotspot answered and the radio moved to it |
| 13 | Pass: `roam` shows the hotspot in use, -35 dBm, automatic on |
| 14 | Pass: lowest unused stack 95 words (ui) |

Steps 15 to 17, run 2026-10-29 on 0.20.3-6HL-2 with the same zone and
hotspot:

| # | Result |
| --- | --- |
| 15 | Pass: repeater 1 did not answer two wake-ups, the hotspot (repeater 2) was on the air at -47 dBm and the search stopped there, 1.5 s after PF3 |
| 16 | Pass: the hotspot alone, 0.15 s to 0.5 s; `staying on repeater 2` |
| 17 | Pass: the screen listed the hotspot; Menu searched, six times in a row |

Steps 18 to 22, run 2026-10-29 on 0.20.3-6HL-6 (with the codeplug's
automatic roaming on) and the same hotspot, beacons every 60 s for 3 s:

| # | Result |
| --- | --- |
| 18 | Pass: beacons 60.0 s apart, each heard as one of 3.37 s to 3.44 s; no search while they came. Found: a sweep visit could start just as the hotspot's own beacon began, and that beacon then counted as missed (fixed after the run) |
| 19 | Pass: one sweep visit about every 63.5 s, 1 s each, around the zone in order |
| 20 | Open (passed on 0.20.3-6HL-7, below) |
| 21 | Pass: hotspot off, two beacons missed, then searches 30 s, 60 s and 120 s apart (the cap is now 2 min) |
| 22 | Pass: hotspot on, its first beacon was heard on its own frequency between searches, with no wake-up |
| 23, 24 | Open: need two repeaters, and the range check on |

Also seen: with automatic roaming on, the radio searched by itself 35 s
after start and took the hotspot.

Step 20 and a repeat of step 18, run 2026-10-29 on 0.20.3-6HL-7 with the
same hotspot and a Parrot talkgroup:

| # | Result |
| --- | --- |
| 18 | Pass: beacons 60.0 s apart for 5 minutes, none counted as missed while sweep visits ran |
| 20 | Pass: `ptt 2000` 35 ms into a sweep visit; the visit was cut short 10 ms later, the call went out 0.5 s after, lasted 1.9 s and released itself; the Parrot reply was received in full |

Also seen: PTT on a repeater that did not answer started a search for
the call, which found the hotspot in 1.5 s and sent the call through it.

Findings:

- The USB link drops while the radio transmits next to the PC; the log
  lines wait in the radio until the port is opened again.
- The codeplug's per-channel roaming bit excludes a channel from roaming
  when set; QDMR writes it clear unless the channel has no roaming zone.
