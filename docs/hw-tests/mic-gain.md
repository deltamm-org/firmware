# Microphone gain

The codeplug's Mic gain (DMR) and FM mic gain, levels 1 to 5, set the
transmit voice level as +8, +12, +16, +20 and +24 dB; level 3, +16 dB,
is the level the radio was set up with. The format gives the levels no
unit: the dB values are a choice, to be checked by ear here.

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

A new gain takes effect on the next transmission. The console has
`mic gain` (logs both), `mic gain dmr <0-30>` and `mic gain fm <0-24>`;
these last until the radio restarts.

Steps marked **Listen** are the only ones that need a person: the rest
are console commands and log lines.

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

Console only:

| # | Do | Expect |
| --- | --- | --- |
| 1 | Note the codeplug's levels in a.yaml (`settings: audio: micGain` and `fmMicGain`), restart | `radio: microphone gain: DMR +N dB, FM +M dB`, N and M the dB of the codeplug's levels |
| 2 | `mic gain` | The same line |
| 3 | `mic gain dmr 31`, `mic gain fm 25` | `unknown command` both times; the gains stay |
| 4 | `mic gain dmr 30`, `mic gain fm 24` | `DMR +30 dB, FM +N dB`, then `DMR +30 dB, FM +24 dB` |
| 5 | Select another channel and the FM channel again, `fm 41`, `fm 44` | `register 41 = 070f`, `register 44 = 0cf9`: the top level |
| 6 | `mic gain fm 8`, select another channel and the FM channel again, `fm 41`, `fm 44` | `register 41 = 070f`, `register 44 = 07f9`: the lowest level |
| 7 | `mic gain fm 16`, select the FM channel again, `fm 41`, `fm 44` | `register 41 = 070f`, `register 44 = 09f9`, the standard |
| 8 | In a.yaml set `micGain: 4` and `fmMicGain: 8` (levels 2 and 4), `dmrconf write -y a.yaml` | After the restart `DMR +12 dB, FM +20 dB` |
| 9 | `dmrconf read -y b.yaml` | `micGain: 3`, `fmMicGain: 7` (the same levels, as QDMR shows them) |

Calls, the manager starting each with `ptt 4000` while the sentence is
spoken:

| # | Do | Expect |
| --- | --- | --- |
| 10 | DMR channel, `dmr pc <parrot ID>`; `mic gain dmr 8`, `ptt 4000` | The parrot plays it back |
| 11 | **Listen**: `mic gain dmr 16`, `ptt 4000`; then `mic gain dmr 24`, `ptt 4000` | Each louder than the one before; no distortion at 24 |
| 12 | **Listen**: `mic gain dmr 30`, `ptt 4000`, the sentence spoken loudly | Not clipped (held at the AGC's target); the room between words louder |
| 13 | FM channel; **Listen** on the second radio: `mic gain fm 8`, `ptt 4000`; `mic gain fm 16`, `ptt 4000`; `mic gain fm 24`, `ptt 4000` | Each louder than the one before, the steps about as large as on DMR |
| 14 | **Listen**: `mic gain fm 24`, `ptt 4000`, the sentence spoken loudly | Rate: clean, or distorted (over-deviation); note which |
| 15 | `fm txaudio bb`; **Listen**: `mic gain fm 8`, `16`, `24`, `ptt 4000` each; then `fm txaudio direct` | The same steps as in 13, the voice through the baseband |

Rate each **Listen** call against the one before: quieter, same or
louder, and distorted or not.

## Undo

`dmrconf write -y` the original a.yaml; `mic gain dmr 16` and `mic gain
fm 16` reset the running radio.
