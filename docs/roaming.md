# DMR roaming: design

How a roaming channel chooses and keeps a repeater. The basic roaming in
place today tries every repeater of the roaming zone with a wake-up and
takes the strongest answer (`docs/hw-tests/roaming.md`). This document is
the plan for what comes next: listen for repeater beacons first, keep the
repeaters in a learned order instead of trying them from the top of the
list, and change repeater only on clear, lasting evidence.

The codeplug stays in the D878UVII format, so codeplug tools such as
QDMR keep writing it. Everything learned on the air lives in the radio.

## 1. What the standards say

### DMR conventional (Tier II)

ETSI TS 102 361-2 (voice and generic services) leaves roaming out of scope
in so many words. There is no standard way for a radio to roam between
conventional repeaters. A repeater sends no site identity, no neighbour
list and no quality parameters; what a radio can learn from it on the air
is its frequency, colour code, slot timing and signal level.

The `CT_CSBK_Beacon` of TS 102 361-2 (clause 6.2.2.3.1) is not a roaming
beacon: it carries slot timing between radios in TDMA direct mode.

### DMR trunking (Tier III), ETSI TS 102 361-4

Tier III has the only DMR rules for finding a site. The radio searches for
a control channel (TSCC) that runs all the time.

- **Acquisition** (6.3.2) is two steps. *Verification*: the channel
  carries this network's system code. *Confirmation*: its signal quality
  is above a threshold. The radio holds two thresholds (6.3.2.3): a higher
  one to acquire a channel while hunting, a lower one to stay on it once
  confirmed.
- **Leaving** (6.3.3.1): signal quality below the staying threshold, no
  decodable control PDU for `T_Nosig` (1 s to 15 s), a different system
  code `NSYSerr` (1 to 3) times in a row, or being moved by the network.
- **Hunting** (Annex D, informative) runs in stages, most likely first:
  1. *Resume*: back to the control channel last confirmed.
  2. *Commanded / single channel*: the channel the network named, or the
     one last confirmed on this network, kept in non-volatile storage
     across power-off.
  3. *Short hunt*: a stored list of likely channels, room for 64, at least
     `Nmax_Ch` = 50. One example strategy runs the list twice, the first
     pass taking only a channel above a higher level (`L_Short`), the
     second anything usable. Another samples the whole list and takes the
     best. A channel may appear several times to favour it.
  4. *Comprehensive hunt*: every channel the network may use. Can be
     switched off in personalization.

  The order within a stage is left open, but should not always start at
  the same place: start at a random position, or go in random order.
- **Network help** (6.7.1): the control channel announces adjacent sites
  (channel and system code), which the radio may use to put likely
  channels first in its hunt. *Vote Now* invites idle radios to leave
  briefly and sample a named neighbour; the network holds back call
  set-up for `VOTE_BLK` (2 to 10) TDMA frames meanwhile, because a radio
  away sampling misses its calls. Sites may carry priorities 1 to 7 (or
  0, none); how a radio weighs priority against signal is left to the
  maker.

### TETRA, ETSI EN 300 392-2

TETRA's mobile link entity (clause 18.3.4) has the most complete rules
for leaving a cell for a better one:

- **Surveillance** of the serving cell: its path loss parameter C1, in
  dB above the minimum level for access.
- **Monitoring** neighbours (from the neighbour list the cell
  broadcasts), or **scanning** them, which costs time on the serving
  cell. Monitoring may run all the time, or start only when the serving
  cell falls below a threshold and stop again above the threshold plus a
  hysteresis (18.3.4.5.1). A measurement older than 60 s is not used.
- **Ranking** of neighbours strictly by measured downlink quality.
- **States** (18.3.4.5.4 to 18.3.4.5.6):
  - *Improvable*: for 5 s the serving cell has been below
    `SLOW_RESELECT_THRESHOLD`, and a neighbour has been better by
    `SLOW_RESELECT_HYSTERESIS`. Change cell.
  - *Relinquishable*: for 5 s the serving cell has been below
    `FAST_RESELECT_THRESHOLD`, and a neighbour has been better by
    `FAST_RESELECT_HYSTERESIS`. Change cell, even to one with fewer
    services.
  - *Usable* neighbour: for 5 s above the fast threshold plus its
    hysteresis.
  - In each case no cell change in the last 15 s.
  - *Radio link failure*: C1 < 0, or the downlink cannot be decoded.
- The four parameters run 0 dB to 30 dB in 2 dB steps (18.5.4). The slow
  threshold sits above the fast one.

### LTE, 3GPP TS 36.304

- **Stored information cell selection** (5.2.3): the phone first tries
  the carriers and cells it stored before. Only if none of them is
  suitable does it search every band (initial cell selection). In both,
  the first suitable cell is taken. The search does not start from
  channel 0.
- **Measuring neighbours only when needed** (5.2.4.2): while the serving
  cell is above `S_IntraSearch`, the phone may skip neighbour
  measurements altogether.
- **Ranking** (5.2.4.6): a hysteresis `Qhyst` is added to the serving
  cell's measurement and a per-neighbour offset `Qoffset` to each
  neighbour's. A neighbour must rank higher for `Treselection` seconds,
  and the phone must have stayed at least 1 s on the serving cell, before
  it moves.

### Repeater beacons

A repeater set to send beacons keys its output for a few seconds at a
fixed interval while it is idle. It sends idle bursts on both slots,
with its colour code and slot timing, the same as when it hangs after a
call, and does not send a beacon while it is busy. A beacon is therefore
a short, periodic transmission that lets a radio measure a repeater
without transmitting. It names nothing but the colour code. Each
repeater is set up with its own beacon period, 10 s to 210 s, and length,
0.72 s to 16.32 s; 60 s and about 4.3 s are the usual setting.

### Measured on the radio

A D878UVII listening to a hotspot sending beacons (every 60 s, for 3 s),
console `dmr rx` and `fm status` every 250 ms, October 2026:

- Beacons came every 60.0 s, within 30 ms over five intervals. Each
  lasted 3.4 s on the air: idle bursts with the colour code on both
  slots.
- A transmission in between (another radio's traffic) did not shift the
  beacon times.
- The baseband reported the repeater's slot timing 20 to 60 ms after the
  carrier appeared, and confirmed it 70 to 160 ms after. Seven
  transmissions, beacons and traffic alike, all fell in this range.
- Hopping during a beacon, with the console's `dmr rx` (the full receive
  set-up, not a bare retune): leaving drops the carrier within 10 ms.
  Coming back, the carrier shows 20 ms after the retune, slot timing 40
  to 80 ms after, and is confirmed 80 to 140 ms after (eight hops). A
  visit to a beacon that is already on the air therefore costs about
  150 ms; a 0.5 s window around a predicted beacon leaves a wide margin.
- Over 15 minutes the beacon period was 60.02 s: a known beacon's time
  has to be learned from the beacons themselves, not taken as exactly
  60 s, or the prediction drifts about 1 s every 45 minutes.
- With nothing on the air the signal level reads -98 to -105 dBm, so
  levels below about -100 dBm cannot be told from no signal. The
  thresholds in section 4 have to sit above that floor, or use the
  chip's noise or quality figure too.
- Without beacons the hotspot transmits only when used, for the traffic
  plus 3 to 20 s of hang time.

## 2. Our situation

| | Tier III / TETRA / LTE | Our conventional repeaters |
|---|---|---|
| Serving site on the air | always (control channel) | only during calls, hang time, beacons |
| Neighbour list | broadcast by the network | none; learn it |
| Site identity | system code, cell ID | frequency and colour code |
| Measuring a neighbour | in gaps of the serving channel | leaves the serving channel: deaf to it meanwhile |
| Asking a site to answer | not needed | wake-up block: a transmission per site |

The network is sparse. Finland has 32 DMR-only repeaters on the air (25
on 70 cm, 7 on 2 m); multimode repeaters need their own activation and
are left out. Sites are high, so a repeater reaches about 50 km. Within
50 km of a repeater there are no other DMR repeaters for 12 of them, one
or two for 14, and at most 6, in the capital area. A cellular network
plans for many overlapping cells; here the radio keeps track of a
handful.

What follows from this:

1. A quiet repeater looks the same as one out of range. The serving
   repeater can be measured only when it transmits, so missed beacons are
   the main sign that it has been lost.
2. One receiver: every moment on another frequency is a moment the radio
   cannot hear its own repeater. Such visits must be short (Vote Now has
   the same cost).
3. A wake-up costs a transmission and battery, and keys a repeater for
   everyone. Listening comes first; waking only when listening has not
   found a repeater and one is needed now (PTT) or the serving one is
   lost.
4. A radio spends most of its time idle, waiting for traffic. That idle
   time pays for the listening. With so few neighbours, a list of their
   levels can be kept fresh at a small cost (3.3).

## 3. Design

### 3.1 The heard list

The radio keeps a short list of the repeaters it has heard, not a table
of the whole zone: with at most a handful in reach (section 2), 8
entries are plenty. The zone's frequencies stay in the codeplug and are
read from flash when a repeater is visited, rather than copied to RAM.

Each entry, 16 bytes:

| Field | Size | |
|---|---|---|
| Position in the roaming zone | 1 | frequencies and colour code from the codeplug |
| Level | 1 | dBm, smoothed |
| Misses | 1 | beacons missed in a row |
| Samples | 1 | levels measured while fresh |
| Beacon period | 2 | 10 ms steps: the period has to be learned to the tens of ms (measured, section 1) |
| Beacon length | 2 | 10 ms steps, measured on the repeater in use |
| Last beacon heard | 4 | ms |
| Last measured | 4 | ms; older than 60 s counts as unknown, as in TETRA; after a beacon, older than 60 s and the longest beacon (16.5 s), so the next beacon of the usual period comes first; once the period is known, older than a period and the longest beacon |

8 entries take 128 bytes. The basic roaming in place today holds the
whole zone's frequencies and one probe result per repeater, about 1.3 kB
for a 64-repeater zone. The new list replaces both.

When the list is full, the entry heard least recently gives way. The
list is the neighbour list: with so few repeaters in reach, there is no
need to learn which repeater lies next to which.

### 3.2 The search order

When the radio looks for a repeater it goes through these stages, as in
Tier III hunting and LTE's stored information selection:

1. **Resume**: the repeater last used in this zone. At power-on too, if
   it was kept across the power-off (see 3.7).
2. **The heard list**, best level first.
3. **The rest of the zone** in codeplug order, starting after the
   position where the last full pass stopped, not at entry 0. The
   codeplug order still tells the radio what the user prefers.

Each stage is tried in two passes, as in the Tier III short hunt example.
The first pass stops at the first repeater above *Good*. The second
accepts the best one above *Usable*. A pass does not run through all 15
repeaters when the first two will do.

### 3.3 Listening in the background (passive)

While a roaming channel sits idle, the radio uses that time to visit the
other repeaters of the zone and keep their levels fresh. It never
transmits for this, and it needs nothing from the user.

**Voice comes first.** The serving repeater is the *base*. The radio
stays on the base and leaves it only for one short visit at a time,
always coming straight back, and only while the base is quiet:

```
base ... visit A ... base ......... visit B ... base ......... visit A
         ≤ 1 s        ≥ 10 s quiet   ≤ 1 s       ≥ 10 s quiet
```

- It leaves only when the base has carried nothing for at least 10 s.
  Conversations come in overs with short gaps, and the reply to an over
  comes within seconds.
- After every visit it returns to the base, never to a second neighbour.
- Back on the base, any carrier holds the radio there until the call and
  its hang time are over.
- A visit gives up and returns as soon as the expected beacon is not
  there: no carrier within 0.5 s of its expected start.

When a repeater keys up for a call, there is usually about a second of
signal (the wake-up, the call's header) before anyone speaks. A visit of
at most 1 s therefore brings the radio back before the first words in
most cases. If a call did start meanwhile, the radio joins it in
progress (see section 6).

A **visit**: tune to the other repeater's output, wait for carrier, slot
timing and a burst with the expected colour code, read the signal level,
tune back. The baseband reports all of these (carrier, `SlotFound`,
`Burst` with its colour code). Only the start of a beacon is needed:
slot timing comes within 160 ms of the carrier (measured, section 1).
A retune and back takes about 20 ms each way. Arriving 200 ms before a
beacon that keeps its time to within 30 ms, a visit takes about 0.5 s
with a margin.

- **Known timing**: once two beacons of a repeater have been heard, the
  next one is expected at a known time, and the radio visits just then.
- **First contact (phase sweep)**: before the radio knows when a
  repeater beacons, each visit covers a different part of the beacon
  interval. A 1 s visit catches a beacon that starts anywhere in a
  window of about 3.5 s (the visit plus the 3 s beacon, less the half
  second needed to measure it), so about 17 visits cover a 60 s
  interval. One visit a minute finds a repeater's timing within about 17
  minutes, with the radio away from its base under 2 % of the time.
  While the radio has no repeater at all, or right after power-on,
  nothing is lost by staying longer: it listens to a whole interval at
  once.
- **How often**, per neighbour whose timing is known:
  - base good: every 5 minutes, to keep the list fresh. With 2
    neighbours that is 2 s away in 5 minutes, under 1 % of the time; with
    6, the most anywhere, 2 %;
  - serving repeater weak: at every beacon, so a better one is found in a
    minute or two;
  - serving repeater lost: continuously, until one is found (3.5).
- **Visits wait** while a call is heard or made, during hang time, while
  the user is using the radio (keys, menus), and while a packet waits to
  be sent.
- **Traffic counts too**: a call heard on another repeater during a visit
  is a measurement like a beacon.
- A beacon that fails to come at the expected time counts once. After
  two misses in a row the radio forgets its timing and sweeps again.
  Traffic on a repeater can shift its beacon, since the beacon timer
  waits until the repeater is idle.

Visits run whenever a roaming channel is in use, since they transmit
nothing. Whether the radio then *moves* to a better repeater by itself
follows the codeplug's automatic roaming setting (3.6). With automatic
roaming off, the list is still kept, so a search from the PF key or PTT
starts from the best known repeater.

Beacons keep a steady period: measured, within tens of milliseconds over
15 minutes (section 1).

### 3.4 Waking (active)

The wake-up search stays, used later and more sparingly:

- on PTT when no repeater is known, or the serving one does not answer
  the call's wake-up (the current behaviour);
- when the serving repeater is lost (3.5) and listening has found no
  usable repeater;
- from the PF key set to roaming, which always searches now.

It follows the order of 3.2 and stops at the first repeater above
*Good*, rather than waking the whole zone every time.

### 3.5 Keeping or leaving the serving repeater

The serving repeater is measured whenever it transmits: its beacons,
calls, hang time, answers to the radio's own wake-ups. Its state follows
TETRA:

| State | Condition | Action |
|---|---|---|
| Good | level at or above *Weak* | visits only to keep the list fresh (3.3) |
| Weak | below *Weak* | visits at every beacon; move to one *Hyst slow* better |
| Failing | below *Failing* | move to one *Hyst fast* better |
| Lost | two beacons missed and nothing heard for two periods, or out of range by the range check | move to any repeater heard; else search after 30 s, then 1 and at most 2 minutes apart while nothing is found |
| Unknown | not heard for 60 s and no beacon timing | visits as when good; the codeplug's period or quiet time start a search, as without beacons, 30 s, then 1 and at most 2 minutes apart until beacons judge the repeater (a search finding it does not start the wait over, so a repeater that beacons a fixed time after its last transmission is not kept busy) |

- The level is the latest smoothed one: each measurement of the repeater
  in use comes from a transmission, so a level below a threshold has
  already lasted from one transmission to the next.
- No move within 15 s of the last one.
- A repeater must have been measured at least twice while fresh (see
  Validity) before the radio moves to it (once, when the one in use is lost).
- The radio moves by itself only when the codeplug's automatic roaming
  is on; `roam auto on|off` overrides it for a session.
- A beacon of the repeater in use is a transmission of idle bursts only,
  at most 16.5 s long, not starting within 3 s of the radio's own wake-up
  and not already on when the radio came back from a visit.

### 3.6 Codeplug settings used

From the D878UVII general settings (`d878uv2_codeplug.cpp` has where
each lives); the range check's values are still to be confirmed on the
radio:

| Setting | Use |
|---|---|
| Default roaming zone | the zone at start |
| Automatic roaming | whether the radio moves by itself (3.5) |
| Start: periodic or out of range | periodic: a full search every period; out of range: when the repeater in use has been quiet for the delay. Either only while that repeater cannot be judged by its beacons |
| Period (min) | for the periodic start |
| Delay (s) | quiet time before a search, without a range check |
| Repeater range check | wake the repeater in use while it is not heard |
| Range check interval | how often: 5 s to 50 s |
| Range check tries | unanswered in a row before out of range: 3 to 5 |
| Out-of-range notice and count | `Out of range` notices, one an interval, 1 to 10 times |
| Side keys | the roaming key |

The codeplug has no signal thresholds, hysteresis or beacon settings.
These are constants in the firmware for now (section 4). A later DeltaMM
settings area could hold them.

### 3.7 Kept across power-off

The repeater last used per zone, so a power-on starts at *Resume*, as in
Tier III. The heard list itself starts empty; it fills in a minute or two
of listening. Not done yet: DeltaMM has no settings of its own that
survive a power-off. Until it has, a power-on starts with the repeater
whose frequencies the channel itself has, if it is one of the zone's.

### 3.8 What the user sees

Roaming is meant to be invisible: the radio keeps a repeater and moves
on by itself, and the user only talks.

- Under the channel name: the repeater in use, as today, or
  `Searching...`.
- A move to another repeater changes that line; there is no notice.
- `Out of range` when the range check gives up on the repeater, as often
  as the codeplug asks.
- **A roaming screen for field debugging** (Menu > Channel > Roam
  status): the repeater in use and its state (`2 Good`), then each
  repeater heard: position, a star on the one in use, output frequency,
  beacon period once learned, level and its age. It updates every second,
  so a walk or drive shows what the radio sees. Its last row, Search
  now, searches.
- The console: `roam` logs the same list with the next beacon's time;
  each visit, missed beacon and move is logged too.

## 4. Values

As built; to be tuned on the air. Levels are as the radio reads them;
with nothing on the air it reads -98 to -105 dBm (section 1), so the
thresholds sit above that. A repeater whose slot timing and colour code
were found counts as usable at any level.

| Name | Value | Meaning |
|---|---|---|
| *Good* | -90 dBm | a search stops here |
| *Weak* | -95 dBm | below: visit at every beacon, move to a clearly better one |
| *Failing* | -105 dBm | below: move to any better one |
| *Hyst slow* | 6 dB | better by this to move while weak |
| *Hyst fast* | 3 dB | better by this to move while failing |
| Guard | 15 s | no move within this time of the last one |
| Validity | 60 s; 76.5 s after a beacon; a beacon period and 16.5 s once the period is known | older measurements count as unknown |
| Beacon miss | 2 | misses before a beacon's timing is forgotten |
| Base quiet | 10 s | the repeater in use quiet this long before a visit |
| User quiet | 5 s | no key this long before a visit |
| Timed visit | 0.3 s before the beacon to 0.5 s after | |
| Sweep visit | 1 s; every 63.5 s (good), 16 s (weak), 2 s (lost) | |
| Follow-up | 4.5 s, 56 s after a sweep found a beacon under way | |
| Refresh | 5 min | a timed repeater's level, while the one in use is good |

## 5. Order of work

1. Done (0.20.3-6HL-2): the heard list and search order (3.1, 3.2), with
   the wake-up search using the new order, and the roaming screen (3.8).
2. Done: visits (3.3), first contact by phase sweep, then known beacon
   timing.
3. Done: the state of the repeater in use (3.5), and moving by itself.
   Still to test with two repeaters at different distances.
4. Done: the codeplug's range check settings (3.6) and its notices.
5. Open: keeping the last repeater across power-off (3.7) needs settings
   that survive a power-off.

## 6. Open questions

- Does the D878's receiver read signal level well enough at the
  thresholds above, and how fast does it settle after a retune?
- How long do visits take in practice (retune, carrier, slot timing),
  and how much of a beacon do they need?
- Do the repeaters in use send timed or network beacons, and how steady
  is their period?
- Sweep visits and the follow-up are spaced for the usual 60 s period;
  with other periods they cover the beacon interval unevenly. A setting
  for the period, or a spacing that adapts, would help there.
- A call that starts on the base while the radio is away is heard from
  the radio's return. Does the radio join a call already running (late
  entry, from the call information repeated in the voice bursts), and
  how much of the first words is lost? How long is the signal before the
  first words on our repeaters, really?
- The automatic roaming setting and the range check fields: confirm what
  each value does on the radio.

## Sources

- ETSI TS 102 361-2 V2.3.1 (2016-02), DMR voice and generic services:
  scope (roaming excluded), clause 6.2.2.3.1 (`CT_CSBK_Beacon`).
- ETSI TS 102 361-4 V1.12.1 (2023-07), DMR trunking protocol: clauses
  6.3 (control channel acquisition and retention), 6.7.1 (announcements:
  Vote Now, adjacent sites), Annex A (timers and levels), Annex D
  (control channel hunting).
- ETSI EN 300 392-2 V3.8.1 (2016-08), TETRA air interface: clauses
  18.3.4.1 to 18.3.4.7 (scanning, monitoring, surveillance, ranking,
  reselection criteria), 18.5.4 (reselection parameters).
- 3GPP TS 36.304, LTE idle mode: clauses 5.2.3 (cell selection),
  5.2.4.2 (measurement rules), 5.2.4.6 (ranking).
- The Finnish amateur radio station list (automatic.sral.fi), October
  2026: locations of the DMR repeaters on the air.
