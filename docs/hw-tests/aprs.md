# APRS

Beacons send the GPS position as an APRS position report, from the
codeplug's APRS settings (callsign and SSID, path, symbol, comment,
frequency, bandwidth, automatic interval). Each packet goes out simplex on
the APRS frequency at the beacon power, without a tone and with the
microphone muted, once the channel is free; from another channel the radio
goes there and comes back. The BT+APRS board decodes what the receiver
hears, and Menu, APRS, Heard lists the stations.

## Before you start

- The log port open in a terminal.
- An APRS callsign in the codeplug, and the codeplug's APRS frequency
  (144.800 MHz in Europe).
- GPS on, with a view of the sky.
- A second radio that decodes APRS, on the APRS frequency.

## Steps

| # | Do | Expect |
| --- | --- | --- |
| 1 | Flash or restart | `aprs: <CALL-SSID>, path ..., symbol ..., <frequency> MHz wide, power low`, the comment, `beacon every <the codeplug's interval> s`; `aprs: receiving through the board`, `aprs: board: +READY`; an A in the status bar while the interval is not Off |
| 2 | With a GPS fix, on a channel other than the APRS frequency: Menu, APRS, Send now | `Beacon queued`; `packet: listening on <frequency> MHz`, `fm: transmitting <frequency> MHz, power low, packet`, `transmit off (packet sent) after <about 0.8> s`, back on the working channel; `Beacon sent`. The second radio decodes the position and the comment |
| 3 | Menu, APRS, Beacon: 1 min; wait two minutes | A beacon about once a minute; the A stays. Beacon: Off removes the A and the beacons |
| 4 | Battery save on, Beacon: 5 min | Between beacons `gps: receiver asleep`; it wakes in time and each beacon has a fresh position |
| 5 | Send now with the GPS off | `No GPS position` about three minutes later; nothing sent |
| 6 | On a free simplex channel (not the APRS frequency), the second radio transmits there for 40 s; meanwhile console `aprs send here` | The beacon waits for the channel; after 30 s busy, `packet: dropped: the channel stayed busy for 30 s` and `Beacon not sent` |
| 7 | As 6, but press PTT while the second radio still transmits, let the second radio stop, and release PTT 5 s later | The voice transmission goes out; the beacon waits for it and goes out once the channel is free (`Beacon sent`) |
| 8 | On the APRS frequency, the second radio sends a beacon | `aprs: heard <its packet>`; Menu, APRS, Heard lists it with the age and the distance; Menu on it shows the position, distance and bearing, altitude, speed, text and the digipeater |
| 9 | Menu, APRS, Receive: Off | `aprs: receiving off`; nothing more is heard. On starts the board again |
| 10 | Clear the APRS frequency in the codeplug and write it; restart; Menu, APRS, Send now | `aprs: ..., no frequency wide, ...` at start; `beacon not sent: the codeplug has no APRS frequency` and `No APRS frequency`; nothing sent. Put the frequency back |

## Results

2026-10-27: steps 1 to 6, 8 and 9 pass; step 7 was not run then, because
the log port drops while the second radio transmits next to the first. A
beacon took about 0.8 s on air and the radio was back on the working
channel at once. Received packets were listed, but the board decoded only
strong ones: a station the second radio decoded at a noise reading of
42 dB was missed.

2026-10-31, step 2 on upstream 0.22.0 with the open pull requests
merged, from a DMR roaming channel working through a hotspot, beacon
power low: the radio listened on 144.800 MHz for 1.6 s, sent the
72-byte beacon in 0.8 s and went straight back to the hotspot; roaming
went on, and the hotspot's next beacon came on time.

2026-10-31, step 10 on the same build: with the codeplug's APRS
frequency cleared, the start line said `no frequency wide`; `aprs send`
gave `beacon not sent: the codeplug has no APRS frequency` and the
notice `No APRS frequency`, and nothing was transmitted.

2026-10-31, step 7 on the same build, with the second radio at 0.5 W on
a short antenna, the console's timed PTT for the key and a made-up
position: failed first, then passed after a fix. The beacon queued while
the second radio transmitted was dropped as having no FM channel once
the voice transmission started, since the transmitter being on was
checked after the receiver. After the fix it waited through the second
radio and the 9.8 s voice transmission and went out 0.3 s after it
(`packet: sending 63 bytes after 11342 ms`).
