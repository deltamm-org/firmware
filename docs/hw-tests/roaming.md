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

A search tries every repeater of the zone in turn: it listens 150 ms for
one already on the air, else sends one wake-up block at the channel's
power and waits for the repeater's timing. The strongest repeater that
answered is used from then on, by every roaming channel. An automatic
search moves only to a repeater at least 6 dB stronger.

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
| 4-9, 13, 14 | Open |

Findings:

- The search found nothing at first: this radio's frequency reference
  had drifted, and nothing it sent was decoded, neither by the hotspot nor
  by a second radio. With the menu's frequency offset set, the hotspot
  answered the wake-up and the search chose it.
- The USB link drops while the radio transmits next to the PC; the log
  lines wait in the radio until the port is opened again.
- The codeplug's per-channel roaming bit excludes a channel from roaming
  when set; QDMR writes it clear unless the channel has no roaming zone.
