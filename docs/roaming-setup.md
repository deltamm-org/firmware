# Setting up roaming

With roaming, one DMR channel per talkgroup covers a whole repeater
network: the channel keeps its talkgroup and slot, and the radio puts it
on whichever repeater of the network answers best. This page shows how to
set it up in the codeplug and how to use it.

Roaming works on DMR channels only. FM channels always use their own
frequency.

## What the codeplug needs

All of it goes into the codeplug with a codeplug tool, such as QDMR or
the AnyTone CPS, and is written to the radio as usual (see
`docs/hw-tests/programming.md`). The names below are QDMR's YAML keys;
the CPS has the same items under Roaming Channel, Roaming Zone, the
digital channels and the optional settings.

### 1. One roaming channel per frequency pair

In a network where repeaters far apart share frequencies, a roaming
channel stands for a frequency pair, not for one repeater: the radio
works with whichever repeater on that pair it reaches.

| Item | What to enter |
| --- | --- |
| Receive frequency | The pair's repeater output |
| Transmit frequency | The pair's repeater input |
| Colour code | The colour code used on the pair; tools that leave it out use the channel's |
| Name | Short, such as the pair's channel name; the working screen shows it for the pair in use |

Its time slot is not used: the slot comes from the channel, so the same
pair serves channels on either slot.

The radio cannot tell apart two repeaters on the same pair and colour
code; in places where both are in reach it hears them as one.

### 2. A roaming zone with all the pairs

A roaming zone lists the frequency pairs the radio may move between, up
to 64. Put in every pair the network's repeaters use, so that whichever
repeater is in reach can be found. The zone's name shows in the menu. A
codeplug can hold several zones, for example one per band; the radio
uses one at a time.

Choose the zone used at start as the default roaming zone in the general
settings (QDMR: `settings.anytone.roamingSettings.defaultZone`).

### 3. One digital channel per talkgroup, with roaming on

For each talkgroup, a digital channel as usual: its contact (the
talkgroup), time slot and colour code. Turn roaming on for it (QDMR:
`roaming` set to the default zone, which is QDMR's default for a digital
channel; in the CPS, the channel's roaming option left on).

The channel's own frequency is used only while roaming is off. Setting
it to one of the zone's pairs, such as your local repeater's, is a good
choice: the radio then starts there, and with roaming off the channel
still works there.

A talkgroup that you use on both time slots, for example one that some
repeaters carry on slot 1 and others on slot 2, needs two channels: the
same contact, one channel on slot 1 and one on slot 2. The slot always
comes from the channel, never from the repeater, so pick the channel
whose slot the repeater in use carries the talkgroup on.

Channels you never want to roam, such as simplex or hotspot channels,
get roaming off.

### 4. A side key for searching

Set a side key's short or long press to Roaming (QDMR: one of
`settings.anytone.keySettings.funcKeyAShort` … `funcKeyCLong`, the side
keys PF1 to PF3, set to `Roaming`). Pressing it makes the radio look for
the best repeater now.

### 5. Automatic roaming (optional)

With automatic roaming on, the radio also looks by itself (QDMR:
`settings.anytone.roamingSettings`):

| Item (QDMR key) | Meaning | Start with |
| --- | --- | --- |
| Automatic roaming (`autoRoam`) | On or off | On |
| Start (`roamStart`) | `OutOfRange`: when the repeater in use has been quiet for the delay; `Periodic`: every period | `OutOfRange` |
| Delay (`autoRoamDelay`) | Seconds of quiet before a search; at least 30 s is used | 30 s (0 also gives 30 s) |
| Period (`autoRoamPeriod`) | Minutes between searches; at least 1 min is used | 1 min (0 also gives 1 min) |

The values in the last column are a good start for most networks; a
longer delay makes the radio stay longer on a repeater that has gone
quiet.

An automatic search moves only to a repeater clearly stronger than the
one in use, so the radio does not swap back and forth between two that
are heard about equally.

### Example

A network on three frequency pairs, each shared by several repeaters,
and two talkgroups, the second on both slots, in QDMR's YAML (the frequencies, IDs and names are
made up):

```yaml
roamingChannels:
  - id: rch1
    name: R1
    rxFrequency: 439.1000 MHz
    txFrequency: 431.5000 MHz
  - id: rch2
    name: R2
    rxFrequency: 439.1250 MHz
    txFrequency: 431.5250 MHz
  - id: rch3
    name: R3
    rxFrequency: 439.1500 MHz
    txFrequency: 431.5500 MHz
roamingZones:
  - id: roam1
    name: Network
    channels: [rch1, rch2, rch3]
channels:
  - dmr:
      id: ch1
      name: Local TG
      rxFrequency: 439.1000 MHz
      txFrequency: 431.5000 MHz
      colorCode: 1
      timeSlot: TS2
      contact: cont1
      roaming: !<!default> ""
  - dmr:
      id: ch2
      name: Wide TG TS1
      rxFrequency: 439.1000 MHz
      txFrequency: 431.5000 MHz
      colorCode: 1
      timeSlot: TS1
      contact: cont2
      roaming: !<!default> ""
  - dmr:
      id: ch3
      name: Wide TG TS2
      rxFrequency: 439.1000 MHz
      txFrequency: 431.5000 MHz
      colorCode: 1
      timeSlot: TS2
      contact: cont2
      roaming: !<!default> ""
settings:
  anytone:
    keySettings:
      funcKeyCShort: Roaming
    roamingSettings:
      autoRoam: true
      roamStart: OutOfRange
      autoRoamDelay: 30 s
      autoRoamPeriod: 1 min
      defaultZone: roam1
```

## Using roaming

- **Changing talkgroup:** turn the knob or pick another channel. The
  repeater in use stays; the new channel's talkgroup and slot apply at
  once, without a search.
- **The working screen** shows `DMR ROAMING`, and under the channel name
  the pair in use: its frequency, the slot, the colour code and the
  roaming channel's name. While the radio looks for a repeater, the line
  says `Searching...`.
- **Searching on the key:** press the side key set to Roaming. The radio
  tries the zone's pairs in turn, listening for a repeater already on
  the air and waking quiet ones, and uses the strongest that answered.
  `No repeater` means none answered.
- **PTT:** before a call the radio wakes the repeater in use. If it does
  not answer, or no repeater is in use yet, the radio searches and the
  call goes out through the repeater it found, as long as PTT is still
  held.
- **Automatic roaming** searches by itself as set in the codeplug, and
  never during a call.
- **Menu > Channel > Roaming:** off makes every channel use its own
  frequency again, until the radio restarts or roaming is turned back
  on. **Roam zone** picks another of the codeplug's roaming zones for the
  session.
- On an FM channel the key says `Not roaming`.

## Repeater beacons and the range check

Roaming works best when the network's repeaters send beacons: a short
transmission at a fixed period, set up on the repeater by its keeper
(any period from 10 s to 210 s, up to 16.5 s long). The radio learns
the beacon timing of the repeater it hears on each pair by itself;
nothing in the codeplug is needed for it.

- **Judging the repeater in use:** from its beacons and calls the radio
  rates it Good, Weak, Failing or Lost. Missed beacons tell it a
  repeater has gone quiet or out of reach before a call is tried.
- **Visits:** while the channel is quiet (no call for 10 s and no key
  for 5 s), the radio briefly listens on the zone's other pairs, one at
  a time, timed to their beacons, so it knows how they are heard. It
  never leaves during a call, and PTT cuts a visit short.
- **Automatic roaming** then moves to another pair when the one in use
  gets weak (below about -95 dBm) and the other is clearly better
  (6 dB). The delay and period settings are used only while beacons
  cannot judge the repeater, and searches that find nothing come further
  and further apart, up to 2 minutes.
- **Searches** try the pairs most likely to answer first and stop at a
  good one.

The codeplug's **repeater range check** is followed too (QDMR:
`rangeCheck`, `checkInterval`, `retryCount`, `notification` and
`notificationCount` under `settings.anytone.roamingSettings`): while the
repeater in use is not heard, the radio wakes it every interval (5 s to
50 s), and after the set number of unanswered tries says
`Out of range`.

**Menu > Channel > Roam status** shows the pair in use with a star and
its state, then each pair heard with its level, how long ago it was
measured and its beacon period once learned. Search now searches
from there.

## Notes

- Each search wakes quiet repeaters with a short transmission at the
  channel's power, so it uses a little battery and air time; a search
  over many pairs takes a few seconds.
- The repeater in use is shared by all roaming channels, and the radio
  starts again from the channel's own frequency after a restart.
