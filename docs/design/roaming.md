# DMR roaming: design

How a roaming channel chooses and keeps a repeater: listen first, wake a
repeater only when the user is about to talk, and move only on clear
evidence. The design is kept small on purpose. It is based on real world
measurements and simulation; the simulation used real repeater
information from BrandMeister, with invented towns and dense networks
added, so that it does not fit one place.

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

## 2. Our situation

| | Tier III / TETRA / LTE | Conventional DMR repeaters |
|---|---|---|
| Serving site on the air | always (control channel) | only during calls, hang time, beacons |
| Neighbour list | broadcast by the network | none |
| Site identity | system code, cell ID | frequency and colour code |
| Measuring a neighbour | in gaps of the serving channel | leaves the serving channel: deaf to it meanwhile |
| Asking a site to answer | not needed | wake-up: a transmission per repeater |

What follows from this:

1. A quiet repeater looks the same as one out of reach. The repeater in
   use can be measured only when it transmits.
2. One receiver: every moment on another frequency is a moment the radio
   cannot hear its own repeater.
3. A wake-up keys a repeater for everyone and costs battery. Listening
   comes first; waking only when the user is about to talk.
4. Networks are sparse: most repeaters have no other repeater, or one or
   two, within reach. Some send beacons, many do not, and beacon periods
   and lengths vary. Nothing may depend on beacon timing.
5. The radio sits idle most of the time. Listening is done in bursts so
   that the battery saver still gets its time.

## 3. Design

### 3.1 Levels

The radio judges a repeater by the level it receives from it, against a
floor for each band that the board sets: -100 dBm on 2 m and on 70 cm.
The value was selected based on simulation and field testing. The floor
is a reference the thresholds count from, not the receiver's noise
floor.

The thresholds count from the floor:

| Name | Value | |
|---|---|---|
| Good | floor + 10 dB | a search stops here; no listening at or above it |
| Weak | below floor + 6 dB | look for a better repeater; move to one 6 dB better |

These are fixed thresholds, as commercial DMR radios use: there the
installer sets them per system after a coverage test. A floor learned on
the air would measure the noise around the radio, but whether a
repeater hears the radio depends on the path to it and on the noise at
its site, which the radio cannot measure. The only test of that is a
wake-up: answered, the repeater heard the radio. So the thresholds only
steer listening and moving; a call always wakes the repeater first.

### 3.2 The repeater in use

| State | When |
|---|---|
| Good | heard lately at or above Weak |
| Weak | heard lately below Weak, or heard lately but a wake-up went unanswered |
| Quiet | not heard for 2 minutes, last heard at or above Weak; or none in use yet |
| Lost | not heard for 2 minutes, and last heard below Weak or a wake-up went unanswered |

Silence alone never makes it lost: a quiet repeater looks the same as one
out of reach, and many repeaters never send beacons. How it was last heard
tells them apart: one that went silent while strong is likely idle, one
that faded first likely out of reach. A full heard list never drops the
repeater in use, so that this stays known.

### 3.3 The heard list

Up to 8 repeaters heard lately, each with its level (the average of the
last two readings), how many readings and when. A level is current for 2
minutes. It is read when the slot timing of a transmission is found.
While a repeater keeps transmitting (calls back to back), each DMR burst
from it keeps its level current; a carrier alone does not, since it may
be another signal or noise.

A wake-up the repeater in use did not answer marks it in the list until
it is heard again. A repeater marked so and never heard, with no level,
leaves the list once another repeater is in use.

### 3.4 Busy and idle

From the DMR heard on the repeater in use: **busy** while DMR came in the
last 2 s, or during a call and for 10 s after its end (the gap between
overs; a call that ends without a terminator ends when it is lost); **idle**
while it has been on the air for 1.5 s with idle bursts only (a beacon,
or hang time long after a call). A call starts with idle bursts too; its
header follows well within 1.5 s.

### 3.5 Listening (no transmitting)

A **check** tunes to another repeater's output for a moment: a carrier,
then slot timing, then a burst with its colour code, whose level is
read. An empty frequency costs about 0.1 s.

- **The repeater in use is Good at the Good level:** no listening.
- **Good below the Good level, or Weak:** single checks, only while the
  repeater in use is idle, at most one every 400 ms. The radio hears
  nothing on its own repeater then anyway.
- **Quiet or Lost:** passes, check after check without going back in
  between, in bursts: 1 minute, long enough to span a beacon period, then
  gaps of 2 minutes doubling up to 5, so that a repeater met after a long
  stretch without any is found within minutes. While Lost the gaps stay
  at 2 minutes: no call on the repeater in use can be missed then, and
  the next repeater, even one that only carries a call now and then, is
  found sooner. After each pass the radio stays 0.5 s on the repeater in
  use. No pass starts while it is busy.
- **Order of a pass:** the repeaters heard lately, strongest first, then
  the rest of the zone in its order. Each frequency once a pass.

### 3.6 Moving by itself

Only with the codeplug's automatic roaming on (`roam auto on|off`
overrides it for a session), and never while the repeater in use is busy:

- Weak: to one heard twice and 6 dB better;
- Quiet: only to one heard at the Good level;
- Lost: to the strongest heard; after fading out, only to one stronger
  than the repeater in use was last heard.

Moving back needs the same margin the other way, so the radio does not
swing between two repeaters.

### 3.7 Waking (on PTT or the roam key)

PTT wakes the repeater in use. If it does not answer and automatic
roaming is on, or the roam key is pressed, the radio searches; with
automatic roaming off, PTT only reports `No answer`, as a manual roaming
radio would:

1. the repeater in use, two tries;
2. the repeaters heard lately, strongest first;
3. the rest of the zone in its order, one try each.

It stops at the first repeater at or above Good; otherwise it takes the
strongest that answered. A repeater that answers is judged by its level
in the heard list, so a current reading from before (a beacon) counts
with the new one: one reading can be a few dB off. A repeater already on
the air needs no wake-up; one that shows a carrier but no timing yet
after a wake-up is slower to start and gets as long as one already on
the air. Calls and searches wake a repeater the same way. A repeater in
use that does not answer is marked: weak while it is still heard, lost
once it is not.

### 3.8 The zone's order

A roaming zone's repeaters follow the roaming zone's own order. The user
puts the most used ones first: that order decides where a search and a
pass begin until the radio has heard which repeaters are strong. Each
roaming zone can order the same repeaters its own way.

When another roaming zone is chosen, a repeater both roaming zones have
keeps what was heard of it, and the repeater in use stays in use when the
new roaming zone has it; otherwise the channel starts on its own
frequencies, as at the start. A pass starts afresh in the new order.

### 3.9 What the user sees

- Under the channel name: the repeater in use, or `Searching...`.
- `No repeater` while the repeater in use is lost; `No repeater answered`
  when a search found none.
- Menu > Channel > Roam status: the repeater in use and its state, then
  each repeater heard with its level and age.
- Menu > Channel > Roaming off and on, and Roam zone, take effect at
  once.
- The console: `roam` logs the state and the heard list; each check
  (with the level read, also on an empty channel), search and move is
  logged.

### 3.10 Left out on purpose

Simulation and testing showed these cost more than they gave:
learning beacon timing and visiting other repeaters at their beacons,
automatic searches that wake repeaters, the codeplug's range check,
a second weak level with its own margin, listening while the repeater in
use is at the Good level, taking silence alone as lost, a minimum time
between moves, resuming a search where the last one stopped, and learned
noise floors.

## 4. Open questions

- The thresholds are fixed per band, from measurements at one place. A
  setting may be needed where they do not fit.
- A repeater that never sends beacons can be found only from its traffic
  or by a wake-up.
- Keeping the repeater in use across power-off needs settings that
  survive a power-off.

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
- BrandMeister's public repeater information: the sites and frequencies
  the simulation used.
