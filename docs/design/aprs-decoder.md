# APRS decoder: design

How the radio hears APRS packets without a separate decoder: the FM
transceiver's audio is sampled by the MCU and decoded in software (`services/afsk_demodulator`). The same code runs on the PC in
the host tests and in `afsk-decode`, so a recording can be decoded
exactly as the radio would.

## 1. Where the audio comes from

- The FM transceiver's audio output reaches an ADC input of the MCU
  ahead of the baseband and the speaker switch (the D878UVII's
  `pins.h`). DeltaMM leaves the transceiver's own squelch muting off, so
  the input carries audio with the squelch closed too; the decoder does
  not depend on the squelch level.
- A timer starts each conversion at 9600 Hz and DMA copies the results
  into a ring of 4096 samples (0.4 s), with no interrupt per sample
  (`timed_adc`, `board_audio_sampler`). The board turns the 12-bit
  counts into mV.
- The aprs task reads the ring every 20 ms and runs above the ui, gps
  and console tasks, so a long screen redraw or codeplug write cannot
  hold it up. If it ever falls a whole ring behind, the lost samples are
  counted, the frame in progress is dropped, and `aprs rx` shows the
  longest gap between two reads.
- Sampling runs only while the radio listens on the frequency the
  decoder is for (APRS mode, `docs/dedicated-modes.md`), and stops for
  the radio's own transmissions. Outside it the ADC, its timer and the
  DMA channel are powered down.

## 2. From audio to bits

1200 baud Bell 202: a mark tone of 1200 Hz and a space tone of 2200 Hz,
eight samples per bit at 9600 Hz.

1. **DC level.** Followed slowly and taken off, so the input's bias does
   not matter.
2. **Band-pass.** A 17-tap windowed-sinc filter, 600 to 2600 Hz, with no
   response at DC: the tones and their keying sidebands pass, hum and
   hiss outside them do not.
3. **Tone correlators.** The last eight filtered samples (one bit) are
   correlated with a mark and a space tone, in phase and in quadrature,
   so the level of each tone over the last bit is known whatever its
   phase.
4. **Slicers.** Seven slicers each compare mark power with space power.
   Before the comparison each one tilts the balance between the tones by
   a set amount, -6, -3, 0, +2, +4, +6 and +9 dB in favour of the mark
   tone, with a three-tap filter on the correlator outputs (the same as
   that filter on the audio before them). The level difference between
   the two tones as received ("twist") depends on the sender's
   pre-emphasis, the receiver's de-emphasis and the path between; the
   slicers whose tilt undoes it see the cleanest bits. Noise also hits
   the slicers differently, so a frame that one slicer loses another
   often reads.
5. **Bit clock.** Each slicer keeps its own bit clock, a phase-locked
   loop nudged at every tone change towards having the change halfway
   between two bits: firmly while it looks for a frame, gently inside
   one, where it also follows the sender's bit rate within 3 %.
6. **Framing.** NRZI is undone (the same tone is a 1, a change a 0),
   stuffed zeros are dropped, a flag ends one frame and starts the next,
   and seven ones in a row (an abort, or no signal) end a frame without
   passing it on.

## 3. From bits to frames

- **Check.** A frame between two flags, of at least two addresses and
  whole bytes, is passed on only if its frame check sequence is right.
  Failed frames are counted per slicer.
- **One copy.** Several slicers usually read the same frame; frames that
  end within a second of one with the same length and check are copies,
  and only the first is passed on (`ax25::RecentFrames`).
- **Repair** (off by default, `aprs rx repair on`). A failed frame whose
  check is explained by one flipped bit, or by two neighbouring bits (one
  wrong tone flips two NRZI bits), is repaired if the result still has a
  well-formed UI header. The check is linear, so the effect of a flipped
  bit on the check depends only on how many bits follow it, and one walk
  back from the last bit tries every bit and every pair. A repaired frame
  waits three bits for a slicer that reads it without a repair, and is
  dropped if any slicer read a right frame within a second of it: it is
  then most likely a damaged copy. Even so a repair can now and then let
  a wrong frame through, which is why it is off by default.

A frame with a right check goes to `ax25::decode`, then to the APRS
position reader (plain, compressed and Mic-E positions) and the heard
list.

## 4. What it costs

- RAM: the decoder's state is about 3.5 KB, the sampler's ring 8 KB,
  and the aprs task's stack 2.5 KB (sized from the deepest path, a
  decoded frame down to the heard list and the log line).
- Processing: per sample, the 17 filter taps, four eight-tap
  correlators and seven slicers, all in single-precision float on the
  MCU's FPU; nothing is done while the decoder is off.

## 5. Tests and tools

- Host tests (`tests/services/test_afsk_demodulator.cpp`) send frames
  through made-up audio (`tests/services/afsk_signal.h`): clean, with
  white or FM-like noise down to a few dB of signal-to-noise, with a
  twist of -6 to +9 dB, with the sender's clock off by up to 1.5 %, quiet,
  loud and clipped audio, a short preamble, frames back to back, the
  radio's own modulator's output, repairs and their limits, and noise
  alone (no frames).
- `afsk-decode <file.wav>` (built with the host tools) runs the same
  decoder over a mono WAV recording of 8000 Hz or more, resampled to
  9600 Hz, and prints each frame with the slicer that found it first and
  the counts per slicer; `--repair` and `--save` as its usage says.
