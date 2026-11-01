# Settings storage

Settings changed on the radio hold after a restart. They are written to
the codeplug, in the codeplug's own fields where it has one, so codeplug
tools read the radio's current values and the factory firmware finds
them too. Settings of DeltaMM's own, which the codeplug has no field for,
go to a small preferences record of their own in the codeplug.

The code: `services/settings_store.h` (the interface the app uses),
`d878uv2_settings_store` (the D878UVII's store), `d878uv2_preferences`
(the record) and `services/src/d878uv2_layout.h` (where each part is).

## What is kept, and where

| Changed on the radio | Kept in | Layout constant |
| --- | --- | --- |
| Squelch level of side A and B | General settings | `Settings` |
| Key lock: what it covers; auto lock time | General settings | `Settings` |
| Battery save | General settings | `Settings` |
| Backlight time and brightness | General settings | `Settings` |
| GPS on or off; time zone | General settings | `Settings` |
| Roaming zone | General settings | `Settings` |
| Zone of each side | General settings | `Settings` |
| Place in each zone's channel list | Zone channel selections | `ZoneSelections` |
| GPS satellite systems | Extended general settings | `SettingsExtension` |
| A channel's power and squelch mode | The channel's record | `ChannelBanks` |
| Roaming on or off (Menu > Channel > Roaming) | Preferences record | `Preferences` |

The auto lock time shares its field with the menu exit time, in 5 s
steps up to 60 s.

The app hands over values as they change: the menu, the zone list, the
channel knob, and the console's `save` and `light time`. The VFO and the
console's other commands (`lock ...`, `light`, `fm ...`, `gps ...`) are
not kept.

## When it is written

- Once nothing has changed for `D878Uv2SettingsStore::QuietMs`, the
  store reads the parts concerned and writes only the bytes that differ,
  all in one write, in rising address order. A run of changes costs one
  write; a value set back to what the codeplug holds costs none.
- A change on another channel writes the previous channel's values at
  once: one channel is kept at a time.
- Changes made in the quiet time before the radio is switched off are
  lost.
- The `settings` console command logs what is pending, the last write's
  outcome, and the settings and preferences as the codeplug holds them.

## Never while the PC programs

The store writes through the same codeplug writer as the PC programming
protocol (`D878Uv2CodeplugWriter`), from the ui task, while the PC's
commands come in on the link task. `D878Uv2Programming::beginLocalWrite`
hands the writer over: it refuses while the PC is in program mode, and a
PC entering program mode waits for a write in progress
(`LocalWriteWaitMs`) before it answers. A refused write is tried again
after the next quiet time, as is a failed one; after
`D878Uv2SettingsStore::MaxFailures` failures in a row the values are
dropped. When the PC wrote the codeplug, the radio starts again with the
PC's codeplug and whatever was still pending is dropped: the PC's write
wins.

## Why it is safe

**Interrupted writes.** The writer puts each touched 128 KB block into
its other copy, filled from the valid copy with the new bytes on top,
and marks the new copy valid only once it is complete (see `Storage` in
`ARCHITECTURE.md`). A write cut short, by switching off for example,
leaves the block as it was.

**Codeplug fields.** The store writes the same encoding codeplug tools
write, and changes only the bits of a field it owns; the rest of each
byte, and every byte it does not own, is written back as read. A tool's
next read sees the radio's values; a tool's next write replaces them, so
the last writer wins. Fields a tool does not write keep the radio's
values, because the writer keeps every byte a write leaves out.

**The preferences record.** `Preferences` lies in the first channel
bank's 128 KB block, past the most that block's channels can take (128
records of `ChannelBytes`, and their extension records
`ChannelExtensionOffset` after them) and before the copy markers in the
block's last 8 bytes. Codeplugs keep nothing there: QDMR and the
manufacturer's programming software read and write only the channels in
use and their extension records in that block, never the record's
place. A codeplug written from the PC therefore keeps the record, and
the record never shows up in a codeplug file read from the radio. If
anything else ever overwrites it, its mark or checksum no longer match
and every preference takes its default; the radio never reads a damaged
record as values.

**Wear.** Each write rewrites one copy of every block it touches: at
most three (the general settings' block, the channel's bank, and the
first channel bank for the record). The quiet time keeps that to one
write per run of changes.

## Room

The record is `PreferencesRecordBytes` (128) bytes: a 6-byte header, up
to `PreferencesFieldBytes` (118) bytes of fields, and a 4-byte checksum.
Past the record, the block has about 8 KB more that codeplugs do not use,
should the record ever need to grow; growing it means a new version (see
below).

## The record's format

| Bytes | Holds |
| --- | --- |
| 0-3 | The mark `DMMR` |
| 4 | The format version, 1 |
| 5 | How many field bytes follow |
| 6 on | The fields, each at a fixed place; unused bytes 0xFF |
| last 4 | CRC-32 of everything before, little-endian |

A record with another mark or version, more field bytes than fit, or a
wrong checksum counts as none: every preference then has its default.
A field past the stated field bytes (a record written before the field
existed), or with a value out of range, reads as its default. Fields
past the ones a version knows are ignored and dropped at its next write.

The version changes only if the header or an existing field changes its
meaning, which a new field never needs.

## Adding a preference

1. Add the member, with its default, to `RadioPreferences` in
   `services/codeplug.h`.
2. In `services/src/d878uv2_preferences.cpp`, give it the place right
   after the last field (`constexpr std::size_t MicGainAt = FieldsAt +
   1;`) and move `FieldsEnd` past it. Never move or reuse a place.
3. Encode it in `encodePreferences`; decode it in `decodePreferences`
   through `Fields`, which gives nothing for a byte the record does not
   hold, and fall back to the default for that and for values out of
   range (`f.flag(...)` does both for on/off values).
4. In `app/src/ui.cpp`, set it in `keepPreferences()`, call that where
   the value changes, and read it at start from
   `codeplug->preferences()`.
5. Tests in `tests/services/test_d878uv2_settings_store.cpp`: the round
   trip, and a record whose field bytes end before the new field gives
   the default.

## Keeping a codeplug setting

1. Decode the field in `decodeSettings` (`d878uv2_codeplug.cpp`) into
   `CodeplugSettings`.
2. Encode it in `encodeSettings` (`d878uv2_settings_store.cpp`), changing
   only the field's own bits, and name it in `keepSettings`'s list.
3. In `app/src/ui.cpp`, set it in `keepSettings()` from the radio's
   current value and call that where the value changes.

A value in a part the store does not read yet needs its own piece in
`D878Uv2SettingsStore::collect` and room for it in `MaxPieces`.
