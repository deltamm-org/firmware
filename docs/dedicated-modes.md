# Dedicated receive modes

The radio has one receiver. Most of the time it follows the channel the
user picked. A dedicated mode takes that receiver over for one job and
keeps it until the user gives it back: APRS first, and later whatever
else needs the receiver on one frequency for hours, such as a digital
text mode. This document is the contract every such mode follows; the
APRS mode is its first user.

## 1. Turning a mode on and off

- Each mode has an On/Off row at the top of its own group in the
  operating part of the menu (Menu, APRS, "APRS mode"). Only one mode is
  on at a time; turning one on turns any other off first.
- The row's panel explains that the radio has one receiver and what the
  mode does with it: "One receiver: APRS mode" / "listens only on APRS".
  Without a frequency for the mode On is refused with a notice ("No APRS
  frequency").
- Choosing On or Off goes straight to the home screen, so the change is
  seen at once.

### The mode's frequency

- APRS uses the codeplug's APRS frequency and its wide/narrow setting; it
  is changed in the codeplug, not in the mode.
- A mode without a codeplug field for its frequency has a Frequency row
  in its menu group, under the On/Off row. The user enters the frequency
  there before turning the mode on; it can be changed only while the
  mode is off. On is refused with a notice until a frequency is set.
- The user is a licensed amateur and takes responsibility for the choice.
  DeltaMM offers no built-in default frequencies and no list of
  "allowed" ones; the only limits are the transmit band limits that apply
  everywhere (144-146 MHz and 430-440 MHz), for a mode that transmits at
  all.
- The last frequency entered is kept in the preferences record and shown
  again the next time.
- A mode stays on until the user turns it off in the menu. Nothing else
  turns it off: not a channel key, not a call, not a timeout.
- The console can do the same for tests (`aprs mode on|off`).

### Kept over a restart

The mode is kept in the radio's preferences record (the codeplug's DeltaMM
record) and comes back on after a restart, once the radio is ready.

Why: the radio restarts on its own after every codeplug save made on the
radio, and a battery change is a restart too. Someone who left the radio
listening to APRS expects it to keep listening, and a mode that silently
dropped back to a voice channel would lose hours of packets. The home
screen says plainly that the mode is on (its top row names the mode and
frequency, section 3), so the radio cannot be mistaken for one on a
channel. If the mode's frequency is gone after a restart (codeplug
changed), the mode stays off and the radio works on its channel.

## 2. While a mode is on

### The receiver

- Locked to the mode's frequency, analog FM, with the mode's bandwidth.
  APRS: the codeplug's APRS frequency and its wide/narrow setting.
- The channel stays chosen underneath, untouched: the channel selector
  does not move, and the radio keeps the last channel it was given (with
  its roaming state) without tuning to it. Leaving the mode tunes back to
  exactly that channel.
- Roaming is off (the channel is not in use), the DMR receive path is off
  (analog only), battery save does not sleep the receiver (a decoder
  needs every sample), and a scan, once there is one, stops.
- The squelch level of the channel still drives the squelch gate: the
  status strip's rule turns green and the LEDs show a signal as usual, but
  a signal does not wake the backlight.
- The decoder needs the audio whether the squelch is open or not. The
  received audio on the MCU's ADC input comes from the FM transceiver's
  audio output, ahead of the baseband and the speaker switch; DeltaMM
  leaves the transceiver's own squelch muting off, so the input carries
  audio with the squelch closed too. The speaker stays quiet
  in the mode unless the user asks to hear packets (Menu, Settings, APRS,
  "Hear packets": On); then it opens with the squelch as on a channel.

### Keys

| Key | In the mode |
| --- | --- |
| Menu | The menu, opened in the mode's group on its On/Off row |
| Exit, Exit held | As usual: back, the next screen |
| Knob, Up, Down, P2 | The mode's own list (APRS: the heard list; the knob then picks a station, Menu opens it, Exit comes back) |
| P1 (zones), #, digits | Refused with a notice ("In APRS mode", 1.5 s as every notice) and a log line: each would change the channel or the frequency |
| Side keys | Their functions as usual; Roam answers "Not roaming" |
| PTT | The mode's own action (below); never voice |

The key lock works as usual, PTT included.

### PTT

A mode decides what PTT does; it never keys the microphone. APRS: a PTT
press sends one beacon now, as Menu, APRS, Send now does: the GPS
position at the beacon power (Menu, Settings, APRS, Send power) on the
APRS frequency. Holding PTT does nothing more. The transmit limits apply
as to every packet: only inside the 2 m and 70 cm amateur bands, only once
the channel has been free for a slot (it waits up to 30 s, then gives up
with "Beacon not sent"), and only with a GPS position no older than
2 minutes (it waits up to 3 minutes for one). A mode that only receives
refuses PTT with a notice.

Automatic beacons go on as set (Menu, APRS, Auto send). In the mode they
go out on the locked frequency without retuning.

### Leaving

Off in the menu (or `aprs mode off`) unlocks the receiver: the radio tunes
back to the channel it kept, with the squelch, power, tones, roaming
repeater and DMR settings it had, and the decoder stops. The heard list
and the counts stay.

## 3. The home screen

The mode uses the working screen's rows (StandbyScreen: top line with the
clock, the large name, the panel under it, the line beside the power
label, the bottom row, the hint bar), drawn the same way, filled with the
mode's data instead of the channel's. The clock follows the home screen
setting (Menu, Settings, Home screen, Clock). It must be useful at a
glance: what was heard last, how far, and whether the radio itself is
doing its job.

APRS:

| Row | Shows |
| --- | --- |
| Top | "APRS 144.800": the mode and its frequency; the clock on the right |
| Big | The callsign of the station heard last; "Listening" until one is |
| Panel | Its distance and bearing from our GPS position ("12.3 km NE"), or its comment or status without positions (cut at a word to leave room for the age; "Our GPS: no fix" or "No position" when it sent no text), and how long ago it was heard ("2 min") on the right |
| Under the panel | STN: stations in the heard list; PKT: packets heard, in amber while the decoder is not running |
| Bottom | Our beacon: "Next beacon 4:12", "Beacons off", "Beacon waits for GPS", "Beacon queued", "No APRS callsign"; "no GPS" added while beacons are on without a fix |
| Hint bar | "Menu  P2 Heard  PTT Beacon" |

It is redrawn when a station is heard and each second (ages, the beacon
countdown), in place.

## 4. The decoder

Sampling the audio and decoding run only while the mode is on and the
radio listens on the mode's frequency: they stop at once for the radio's
own transmissions (beacons) and start again a second after. Outside a
mode nothing samples the audio in the background. For tests,
`aprs rx decode any` decodes on any analog channel without the mode, and
`aprs rx decode off` keeps the decoder off even in the mode.

The audio sampler fills a ring by DMA. The decoding task reads it every
20 ms and runs above the ui, gps and console tasks, so a long screen
redraw or codeplug write cannot hold it up; the ring covers 0.4 s.
`aprs rx` reports samples lost and the longest gap between reads.

## 5. Adding a mode

A new mode brings:

1. A value in `services::DedicatedMode`, and its key and PTT rules in
   `services::DedicatedModeState` (knob and Up/Down: the mode's list or
   refused; PTT: its action or refused).
2. Its frequency and bandwidth as a `services::ReceiverLock`, from one of
   two sources: the codeplug's field where the codeplug has one (APRS),
   else a Frequency row in the mode's menu group that the user enters
   while the mode is off, kept in the preferences record (section 1). No
   default frequencies, no allowed list; no frequency, no mode
   (`DedicatedModeState::turnOn` answers `NoFrequency`).
3. Its settings: on/off and the mode's own options in the preferences
   record (`RadioPreferences`), statistics in RAM only.
4. A task that owns its decoder, told by a message when the mode starts
   and stops; it samples only while the radio listens on the mode's
   frequency (`radio::analogListeningHz()`). The audio sampler has one
   user at a time: the task of the mode that is on.
5. A home screen builder: a pure function in services that turns the
   mode's state into the rows of section 3, with host tests. The ui only
   draws what it returns.
6. A menu group with the On/Off row first, its panel naming the
   frequency, the Frequency row next where the codeplug has none, and the
   mode's other rows under it.
7. A `docs/hw-tests/` checklist runnable from the console.

The radio side is shared: `radio::LockReceiver` locks and unlocks the
receiver for any mode, and the ui keeps one `DedicatedModeState`.
