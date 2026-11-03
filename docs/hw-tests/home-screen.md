# Home screen: rows set in the menu

The standby screen is the home screen, in rows under the status bar:

- the top line (left) and the clock (right);
- the large name;
- a row on a panel;
- the power, with a reading next to it;
- the bottom row;
- the key hints.

Menu > Settings > Home screen, first in Settings, sets each row:

| Row | Choices |
| --- | --- |
| Preset | Detailed, Simple, Roaming; `Own` once a row is changed by itself |
| Top line | Talkgroup (`TG 9`, `CHANNEL` without a contact), Zone, Number (`CH 2/5`), None |
| Clock | On, Off |
| Large name | Channel, Contact (the contact's name), Zone |
| Middle row | Frequency (`MHz`, slot and colour code, or `FM` and the bandwidth), Talkgroup (the contact and a name), Signal (meter) |
| Next to power | Voltage (`BAT 7.80V`), Volume (`VOL 8`), Signal (`SIG -90`) |
| Bottom row | Details, Last caller, Position, None |

Detailed is the default. Simple: no top line, the contact's name large,
the contact and the channel's name in the middle row, the last caller at
the bottom. Roaming: the signal meter in the middle row.

Details are the repeater a roaming channel works through (`via 2 NAME`,
counted from 1 as `roam` counts), an FM channel's tones and offset, or
else the zone and the channel's place in it (the channel's name instead
when something else is large), also on an FM simplex channel without
tones.

The signal meter has 10 bars, one per 4 dB from -105 dBm (none) to -65
dBm (all), and the level in dBm. The level is read only while a carrier is heard and
shown for 2 s after; on a roaming channel the repeater's last beacon
level is shown while it is fresh. Otherwise, and after a restart until
the first signal, it shows `-` and no bars.

The status bar keeps its TX, RX, LOCK and GPS marks; the battery is an
icon filled by its level, red when low. Only rows whose shown value
changes are drawn again: the clock each minute, the voltage when its
digits change, the volume, the signal level, the last call's age, the
position.

The layout is kept in the preferences record and survives a restart and
a codeplug write from QDMR. A record from an earlier build gives the
default layout.

## Before

- A DMR channel whose contact is a talkgroup with a name other than the
  channel's, a roaming channel, and an FM channel in the codeplug.
- A codeplug read with QDMR to undo with: `dmrconf read -D <data> -R
  d878uv2 before.yaml`.
- The log port open for the log lines named under Expect and for the
  console commands. Keys are named as on the key test (Menu, Exit, Up,
  Down); "look" means looking at the radio's screen.

The menu remembers the row last chosen in each group until a restart, so
the way into the group depends on what came before:

- `H`, after a restart: press Menu, Down, Down, Down, Down, Menu, Menu (Menu,
  Settings, Home screen), on Preset.
- `M`, when the menu was last left from the group: press Menu three
  times, back on the row last chosen there.
- `X`: press Exit three times, back to the home screen.

## Steps

| # | Do | Expect |
| --- | --- | --- |
| 1 | `restart`; on the DMR channel, look | `TG <n>` top left, the time top right; the channel's name large; `MHz`, the frequency, `TS<n>` and `CC<n>` on the panel; `PWR` and `BAT` with the voltage to 0.01 V; the zone and `<n>/<m>` at the bottom; a battery icon in the status bar |
| 2 | Turn the channel knob to the FM channel; look | `CHANNEL` top left; `FM` and `25k` or `12.5k` on the panel; tones and offset at the bottom; on a simplex channel without tones the zone and `<n>/<m>` instead |
| 3 | Turn the channel knob back to the DMR channel; `H`; look | `Home screen` (title alone), `1/7`: Preset Detailed, Top line Talkgroup, Clock On, Large name Channel, Middle row Frequency, then Next to power Voltage, Bottom row Details below; panel `A whole layout at once;` / `rows can then change` |
| 4 | Press Menu; look | `Preset`: Detailed, Simple, Roaming as buttons, Detailed starred |
| 5 | Press Down, Menu, then `X`; look | `ui: home screen Simple`; no top line; the contact's name large; the panel `TG <n>` with the channel's name after it (the contact's name not repeated); `No call heard yet` at the bottom; 3 s later `settings kept in the codeplug` |
| 6 | `M`, press Menu, Down, Menu, then `X`; on a quiet DMR channel that does not roam, look | `ui: home screen Roaming`; the panel `SIG`, ten dark bars, `-`, `dBm` |
| 7 | Key another radio on that channel for 5 s, then stop; watch the meter meanwhile | Bars and the level while it transmits; `-` and dark bars again about 2 s after it stops |
| 8 | Turn the channel knob to the roaming channel; wait for a repeater (`roam` names it); look | The meter shows the repeater's last level between its beacons; the bottom row `via <n> <name>` with `<n>` as `roam` numbers it |
| 9 | `restart`; look on the quiet channel of step 6 | The Roaming layout is kept; the meter shows `-` until a signal is heard, not the receiver's noise |
| 10 | `H`, press Down, Menu, Down, Down, Menu | Top line Number; Preset shows `Own` |
| 11 | Press Down four times, Menu, Down, Menu, Down, Menu, Down, Down, Menu, then `X`; look | `CH <n>/<m>` top left; the meter still in the middle; `VOL <n>` next to the power; `No GPS position` at the bottom (GPS off) |
| 12 | Turn the volume knob | The volume notice; after it `VOL` shows the new step |
| 13 | `gps on`, wait for a fix; look | The position at the bottom, `<lat> N  <lon> E` with five decimals |
| 14 | `restart`; `settings`; look | `preferences record: roaming on, home screen 2 1 0 2 1 2`; the screen as in step 13 |
| 15 | `H`, press Down six times, Menu, Up, Menu, then `X`; a call heard on the DMR channel (a test call answered by an echo service will do) | After the call screen: `Last: <caller>  now`; a minute later `1 min ago` |
| 16 | `restart`; `H`, press Down, Down, Menu, Up, Menu (Clock Off), Up, Menu, Down, Menu (Top line None), Down, Down, Menu, Down, Down, Menu (Large name Zone), Down, Down, Menu, Down, Menu (Next to power Signal), then `X`; look | No top line and no clock; the zone's name large; `SIG -` next to the power |
| 17 | With the layout of step 16 on a quiet channel: watch the screen for a minute without touching a key | The screen stays still apart from the reading next to the power now and then; nothing blanks or flashes |
| 18 | Turn the channel knob one click on and one back, then turn the volume knob, watching the screen | Each change is drawn in place: nothing blanks or flashes |
| 19 | QDMR round trip: `dmrconf read -D <data> -R d878uv2 a.yaml`; `dmrconf write -D <data> -R d878uv2 a.yaml`; wait for the restart; look; `settings` | The write completes; the layout of step 16 is still in use and in `preferences record`; channels and zones as before |
| 20 | `dmrconf read -D <data> -R d878uv2 b.yaml`; `diff a.yaml b.yaml` | No differences |
| 21 | Wait for the `system: uptime ...` line (each minute) | `unused stack words: ui` above 50 |

## Undo

- `restart`, then `H`, press Menu, Menu (Preset Detailed), then `X`;
  wait for `settings kept`.
- Or `dmrconf write -D <data> -R d878uv2 before.yaml`.
