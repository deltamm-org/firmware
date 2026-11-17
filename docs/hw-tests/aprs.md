# APRS

Beacons send the GPS position as an APRS position report, from the
codeplug's APRS settings (callsign and SSID, path, symbol, comment,
frequency, bandwidth, power, automatic interval). Each packet goes out
simplex on the APRS frequency, without a tone and with the microphone
muted, once the channel is free; from another channel the radio goes
there and comes back. APRS mode holds the receiver on the APRS frequency
and decodes the received audio on the radio's own processor
(`docs/dedicated-modes.md`, `docs/design/aprs-decoder.md`); Menu > APRS >
Heard lists the stations heard.

Most steps run from the console. Steps marked **TX** transmit, on the
APRS frequency or (steps 7 and 8) on the channel in use: run them only
with a person at the radio who has said transmitting is fine now. The
steps under "Keys and the speaker" need a person at the radio's keys.

Logs hold other stations' callsigns: keep them on the PC, out of the
repository; report counts only.

## Before you start

- The log port open in a terminal that logs to a file.
- An APRS callsign in the codeplug and the codeplug's APRS frequency
  (144.800 MHz in Europe), beacons off (Auto send Off in the codeplug,
  or `aprs every 0` after the start).
- GPS on, with a view of the sky; `gps` shows a fix.
- The radio on an ordinary analog channel that is not the APRS
  frequency, APRS mode off. Note the channel: `fm status`.
- For the receive steps, a second radio that sends and decodes APRS, or
  live APRS traffic on the frequency.

## Sending

| # | Do | Expect |
| --- | --- | --- |
| 1 | Flash or `restart` | `aprs: <CALL-SSID>, path ..., symbol ..., 144.800 MHz wide, power <the codeplug's APRS power>`, the comment, `beacons off`; no `crash:` |
| 2 | `aprs send` with a GPS fix (**TX**) | `packet: listening on 144.800 MHz`, `fm: transmitting 144.800 MHz, power ..., packet`, `transmit off (packet sent) after <about 0.8> s`, then `fm: receiving` on the working channel again; the notice `Beacon sent`. A second radio decodes the position, symbol and comment |
| 3 | `aprs every 60`; wait two and a half minutes (**TX**) | `aprs: beacons on`; a beacon about once a minute, each from the working channel and back; `APRS` in the status bar. `aprs every 0`: `beacons off`, the mark goes |
| 4 | `save 1`, `aprs every 300`; wait ten minutes (**TX**) | Between beacons `gps: receiver asleep`; the receiver wakes in time and each beacon has a fresh position. Then `aprs every 0` |
| 5 | `gps off`, `aprs send`; wait 3 minutes | `aprs: beacon waits for a GPS position`, then `beacon not sent: no GPS position within 3 min` and the notice `No GPS position`; nothing transmitted. Then `gps on` |
| 6 | `fm rx <a free simplex frequency in kHz>`; a second radio transmits there for 40 s; meanwhile `aprs send here` | Nothing is sent while the carrier is on; after 30 s `packet: dropped: the channel stayed busy for 30 s` and `Beacon not sent` |
| 7 | As 6, but `ptt 3000` while the second radio still transmits, and the second radio stops before the 3 s are over (**TX**) | The PTT transmission goes out for 3 s; the beacon waits for it and goes out once the channel is free (`Beacon sent`) |
| 8 | Back on the channel noted before: `aprs send`, then within 0.5 s `ptt 1000` (**TX**) | `packet: dropped: the radio is in use`, `fm: receiving` on the working channel, and the PTT transmission goes out there |
| 9 | On a DMR channel: `aprs send` (**TX**) | The packet goes out as FM on 144.800 MHz, `transmit off (packet sent)` without a DMR terminator; back on the DMR channel |
| 10 | Clear the APRS frequency in the codeplug, write it, and let the radio restart; `aprs send`, then `aprs mode on` | `aprs: ..., no frequency wide, ...` at start; `beacon not sent: the codeplug has no APRS frequency` and the notice `No APRS frequency`; nothing transmitted; `aprs mode on` gives `No APRS frequency` too. Put the frequency back |

## APRS mode and receiving

| # | Do | Expect |
| --- | --- | --- |
| 11 | `aprs mode on` | `ui: APRS mode on`, `radio: receiver held at 144.800 MHz, 25 kHz, speaker quiet`, `aprs: APRS mode on at 144.800 MHz`, about a second later `aprs: decoding on at 144.800 MHz`; the APRS home screen: top line `APRS 144.800`, large `Listening`, `STN` and `PKT` counts, bottom `Beacons off`, hint bar `Menu  P2 Heard  PTT Beacon` |
| 12 | `fm status`; `save` | 144.800 MHz; `battery save ...; awake` and no `receiver asleep` however long the radio is idle in the mode |
| 13 | `aprs rx` twice, 10 s apart | `APRS mode on; decoding in APRS mode, running, repair off`; `frames: ...; <s> s of audio, 0 samples lost, ...` with the seconds growing; `reads: longest gap` well under 400 ms |
| 14 | A station sends a beacon (the second radio, or wait for traffic) | `aprs: heard <packet>`; within a second the home screen's large name is that callsign, the panel its distance and bearing (or its comment or status) and its age; `STN` and `PKT` count up |
| 15 | `fm rx 145500` | `radio: refused: a dedicated mode holds the receiver`; still on 144.800 MHz |
| 16 | `ptt 500` | `fm: PTT: a dedicated mode holds the receiver`, the notice `Mode is on`; nothing transmitted |
| 17 | `aprs send` (**TX**) | `fm: transmitting 144.800 MHz, power ..., packet` without `packet: listening on` (no retune); `aprs: decoding off` before it and `decoding on` about a second after it |
| 18 | `aprs every 60` for two minutes, then `aprs every 0` (**TX**) | Bottom row `Next beacon 1:00` counting down; beacons go out on 144.800 MHz without a retune |
| 19 | `fm sq 5` with no signal on the frequency; wait for a packet; `aprs rx` | Packets are still decoded with the squelch closed for noise. Then `fm sq 3` |
| 20 | `aprs rx decode off`; `aprs rx`; then `aprs rx decode on` | `decoding off` at once and `not running`; `decoding on at ...` again a second after `on` |
| 21 | `aprs mode off` | `ui: dedicated mode off`, `radio: receiver back to the channel`, `fm: receiving <the channel noted before>`, `aprs: decoding off`; `fm status` as noted before |
| 22 | `fm rx 144800` (the APRS frequency, without the mode) | No `decoding on`: the decoder runs only in the mode |
| 23 | `aprs rx decode any` on an analog channel; `aprs rx`; `aprs rx decode on` | `decoding on at <that frequency>`; `decoding on any analog channel, running`; `decoding off` after `on` |
| 24 | `aprs mode on`, `restart`; `settings` | After the start screen the APRS home screen; `radio: receiver held at 144.800 MHz` before any `fm: receiving` on the channel; `APRS mode on` in the preferences record line |
| 25 | `aprs mode off`, `restart`; `settings` | The radio starts on its channel; `APRS mode off` in the preferences record line |
| 26 | Steps 11 and 21 on a DMR roaming channel, with `roam` before and after | `roam` after leaving the mode shows the same repeater in use as before |
| 27 | After an hour in the mode, the uptime line (`system: uptime ...`) | Unused stack words: aprs at least 100 |

## Keys and the speaker

These need a person at the radio. Start in APRS mode on its home screen.

| # | Do | Expect |
| --- | --- | --- |
| 28 | Press P1, then `#`, then `1` | Each: the notice `In APRS mode`, and `ui: key <name> refused in APRS mode` in the log; still on 144.800 MHz |
| 29 | Turn the channel knob one step | The heard list `APRS heard`: each station's call, how long ago it was heard and its distance; the knob moves the choice; Menu opens the station (position, distance and bearing, altitude, speed, text, the digipeater); Up and Down there step to the next station; Exit back to the list, Exit again to the APRS home screen |
| 30 | Press Up, then Exit; press P2, then Exit | The heard list each time; Exit goes back to the APRS home screen |
| 31 | Press Menu | Menu > APRS on the `APRS mode  On` row |
| 32 | Press PTT with a GPS fix (**TX**) | The notice `Beacon queued`, then a beacon on 144.800 MHz; never voice |
| 33 | With a packet coming in, listen | No packet sound; the status bar's rule turns green during the packet; the backlight does not come on for it |
| 34 | Menu > Settings > APRS > Hear packets: On | `radio: receiver held at 144.800 MHz, 25 kHz` without `speaker quiet`; the next packet is heard on the speaker. Set it Off again |
| 35 | Menu > APRS > APRS mode: Off | The channel's home screen as before the mode |
| 36 | Menu > APRS > Auto send: 5 min; Menu > Settings > APRS > Send power: Mid; wait 3 s; `restart`; `aprs` | `settings kept in the codeplug` after the second change; after the restart `power mid` and `beacon every 300 s`. Set both back the same way |
| 37 | Menu > APRS > Send now (**TX**) | The notice `Position queued`, then `Beacon sent` |

## A missed packet, offline

When another receiver decodes a packet the radio missed, record that
station's next packet from a receiver's speaker or discriminator output
as a mono WAV file (8000 Hz or more) and decode it on the PC with the
radio's own decoder: `build/host/tools/afsk_decode/afsk-decode
<file.wav>` prints each frame with the slicer that found it first, then
per slicer the frames with a right check and those that failed;
`--repair` shows whether one wrong bit or tone was the problem, and
`--save <out.wav>` writes the audio as the decoder takes it.

## Results

Not yet run on this branch as a whole. Its parts were run on the radio
on images that held them together with other branches.
