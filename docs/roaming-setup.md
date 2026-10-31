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

### 1. One roaming channel per repeater

A roaming channel describes one repeater:

| Item | What to enter |
| --- | --- |
| Receive frequency | The repeater's output |
| Transmit frequency | The repeater's input |
| Colour code | The repeater's colour code; tools that leave it out use the channel's |
| Name | Short; the working screen shows it for the repeater in use |

Its time slot is not used: the slot comes from the channel, so the same
repeater serves channels on either slot.

### 2. A roaming zone with the network's repeaters

A roaming zone lists the repeaters the radio may move between, up to 64.
Put every repeater of the network in it that a call on your talkgroups
can go through. The zone's name shows in the menu. A codeplug can hold
several zones, for example one per region or band; the radio uses one at
a time.

Choose the zone used at start as the default roaming zone in the general
settings (QDMR: `settings.anytone.roamingSettings.defaultZone`).

### 3. One digital channel per talkgroup, with roaming on

For each talkgroup, a digital channel as usual: its contact (the
talkgroup), time slot and colour code. Turn roaming on for it (QDMR:
`roaming` set to the default zone, which is QDMR's default for a digital
channel; in the CPS, the channel's roaming option left on).

The channel's own frequency is used only while roaming is off. Setting
it to one of the zone's repeaters is a good choice: the radio then starts
on that repeater, and with roaming off the channel still works there.

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

| Item (QDMR key) | Meaning |
| --- | --- |
| Automatic roaming (`autoRoam`) | On or off |
| Start (`roamStart`) | `OutOfRange`: when the repeater in use has been quiet for the delay; `Periodic`: every period |
| Delay (`autoRoamDelay`) | Seconds of quiet before a search; at least 30 s is used |
| Period (`autoRoamPeriod`) | Minutes between searches; at least 1 min is used |

An automatic search moves only to a repeater clearly stronger than the
one in use, so the radio does not swap back and forth between two that
are heard about equally.

### Example

A network of three repeaters and two talkgroups, in QDMR's YAML (the
frequencies, IDs and names are made up):

```yaml
roamingChannels:
  - id: rch1
    name: North
    rxFrequency: 439.1000 MHz
    txFrequency: 431.5000 MHz
  - id: rch2
    name: Hill
    rxFrequency: 439.1250 MHz
    txFrequency: 431.5250 MHz
  - id: rch3
    name: Coast
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
      name: Wide TG
      rxFrequency: 439.1000 MHz
      txFrequency: 431.5000 MHz
      colorCode: 1
      timeSlot: TS1
      contact: cont2
      roaming: !<!default> ""
settings:
  anytone:
    keySettings:
      funcKeyCShort: Roaming
    roamingSettings:
      autoRoam: true
      roamStart: OutOfRange
      autoRoamDelay: 60 s
      defaultZone: roam1
```

## Using roaming

- **Changing talkgroup:** turn the knob or pick another channel. The
  repeater in use stays; the new channel's talkgroup and slot apply at
  once, without a search.
- **The working screen** shows `DMR ROAMING`, and under the channel name
  the repeater in use: its frequency, the slot, the colour code and the
  roaming channel's name. While the radio looks for a repeater, the line
  says `Searching...`.
- **Searching on the key:** press the side key set to Roaming. The radio
  tries the zone's repeaters in turn, listening for one already on the
  air and waking quiet ones, and uses the strongest that answered.
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

## Notes

- Each search wakes quiet repeaters with a short transmission at the
  channel's power, so it uses a little battery and air time; a search
  over many repeaters takes a few seconds.
- The repeater in use is shared by all roaming channels, and the radio
  starts again from the channel's own frequency after a restart.
