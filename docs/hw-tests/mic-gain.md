# Microphone gain

The codeplug's Mic gain (DMR) and FM mic gain, levels 1 to 5, set the
transmit voice level as +8, +12, +16, +20 and +24 dB; level 3, +16 dB,
is the level the radio was set up with. The format gives the levels no
unit: the dB values are a choice, to be checked by ear here.

Menu > Settings > Audio sets either gain in dB instead: DMR mic gain
+0 to +30 dB, FM mic gain +0 to +24 dB, in 1 dB steps. The first choice,
`Codeplug +N dB`, follows the codeplug's level again; the row then shows
`Plug +N dB`. A value set on the radio is kept in the preferences record
and wins over the codeplug's level until set back to Codeplug.

- DMR: the gain is the most the codec's microphone AGC applies. Quiet
  speech gets all of it; louder speech is held at the AGC's target
  (10 dB below full scale), so a high gain does not clip.
- FM: the microphone reaches the FM transceiver directly, with no AGC on
  the way. Each level is the transceiver's transmit gain the board gives
  it (bits 11:8 of `fm 44`: 7, 8, 9, 10 and 12); a gain between or beyond
  the levels takes the nearest level and scales the transceiver's voice
  gain (`fm 41`, 15 at every level) for the rest. To keep loud speech
  from over-deviating, FM stops at +24 dB, the top level. With `fm
  txaudio bb` or `both` the codec takes the FM gain too, as on DMR.

A new gain takes effect on the next transmission. The console sets the
same kept values as the menu: `mic gain` (logs both), `mic gain dmr
<0-30>|codeplug` and `mic gain fm <0-24>|codeplug`.

Steps marked **Listen** need a person to listen; steps 6, 7, 8, 12 and
13 also need the radio's keys (named as on the key test) and a look at
its screen. The rest are console commands and log lines.

## Before you start

- The log port open in a terminal, the codeplug read with `dmrconf read
  -y a.yaml` and kept as the original.
- A DMR channel through a repeater or hotspot with a parrot (echo)
  service. The parrot answers private calls only: `dmr pc <parrot ID>`
  before the DMR steps (again after a restart).
- An FM simplex channel, and a second radio on it at a distance that
  gives a clean, full-quieting signal, with someone listening on it.
- The same test sentence for every call, spoken at a normal level about
  10 cm from the microphone, or played from a phone held there: a
  recording gives the same input every time.

## Steps

Without calls:

| # | Do | Expect |
| --- | --- | --- |
| 1 | Note the codeplug's levels in a.yaml (`settings: audio: micGain` and `fmMicGain`), restart, `mic gain` | `radio: microphone gain: DMR +N dB, FM +M dB`, N and M the dB of the codeplug's levels; `ui: microphone gain: DMR +N dB (codeplug), FM +M dB (codeplug)` |
| 2 | `mic gain dmr 31`, `mic gain fm 25` | `unknown command` both times; `mic gain` as before |
| 3 | `mic gain dmr 30`, `mic gain fm 24`; after 5 s `settings` | `DMR +30 dB, FM +24 dB` without `(codeplug)`; `ui: settings kept in the codeplug`; `preferences record: ..., mic gain DMR +30 dB, FM +24 dB` |
| 4 | Select another channel and the FM channel again, `fm 41`, `fm 44` | `register 41 = 070f`, `register 44 = 0cf9`: the top level |
| 5 | `mic gain fm 8`, select another channel and the FM channel again, `fm 41`, `fm 44` | `register 41 = 070f`, `register 44 = 07f9`: the lowest level |
| 6 | On the standby screen press Menu, Down four times, Menu, Down, Menu; look | Settings > Audio: DMR mic gain `+30 dB`, FM mic gain `+8 dB` |
| 7 | Press Menu; look | DMR mic gain's value screen: `+30 dB`, `In use: +30 dB` |
| 8 | Press Exit four times (back to the standby screen); `restart`; `mic gain` | `DMR +30 dB, FM +8 dB`, kept across the restart |
| 9 | In a.yaml set `micGain: 4` and `fmMicGain: 8` (levels 2 and 4), `dmrconf write -y a.yaml` | After the restart still `DMR +30 dB, FM +8 dB`: the values set on the radio win, and the codeplug write kept them |
| 10 | `mic gain dmr codeplug`, `mic gain fm codeplug` | `DMR +12 dB (codeplug), FM +20 dB (codeplug)` |
| 11 | `dmrconf read -y b.yaml` | `micGain: 3`, `fmMicGain: 7` (the same levels, as QDMR shows them); the rest as in a.yaml |
| 12 | Menu > Settings > Audio (keys as in 6); look | `Plug +12 dB`, `Plug +20 dB` |
| 13 | Press Exit three times; `mic gain fm 16`, select the FM channel again, `fm 41`, `fm 44` | `register 41 = 070f`, `register 44 = 09f9`, the standard |

Calls, each started with `ptt 4000` while the sentence is spoken:

| # | Do | Expect |
| --- | --- | --- |
| 14 | DMR channel, `dmr pc <parrot ID>`; `mic gain dmr 8`, `ptt 4000` | The parrot plays it back |
| 15 | **Listen**: `mic gain dmr 16`, `ptt 4000`; then `mic gain dmr 24`, `ptt 4000` | Each louder than the one before; no distortion at 24 |
| 16 | **Listen**: `mic gain dmr 30`, `ptt 4000`, the sentence spoken loudly | Not clipped (held at the AGC's target); the room between words louder |
| 17 | FM channel; **Listen** on the second radio: `mic gain fm 8`, `ptt 4000`; `mic gain fm 16`, `ptt 4000`; `mic gain fm 24`, `ptt 4000` | Each louder than the one before, the steps about as large as on DMR |
| 18 | **Listen**: `mic gain fm 24`, `ptt 4000`, the sentence spoken loudly | Rate: clean, or distorted (over-deviation); note which |
| 19 | `fm txaudio bb`; **Listen**: `mic gain fm 8`, `16`, `24`, `ptt 4000` each; then `fm txaudio direct` | The same steps as in 17, the voice through the baseband |

Rate each **Listen** call against the one before: quieter, same or
louder, and distorted or not.

## Undo

`mic gain dmr codeplug`, `mic gain fm codeplug`; `dmrconf write -y` the
original a.yaml.
