# Settings storage

Settings changed on the radio are written to the codeplug after a quiet
time of 3 s, and hold after a restart (`docs/settings-storage.md`). The
radio never writes while the PC programs it.

This is the first image that writes the codeplug by itself. Steps 1-6,
11 and 13-16 run from the console and the PC. Steps 7-9, 12 and the
screen and menu checks of step 10 need the radio's keys, knob and
screen.

## Before you start

- The log port open; the data port free for `dmrconf`
  (`dmm-radio ports` names both).
- `dmrconf` 0.15.1 or later. In the commands below `<data>` is the data
  port's device.
- A codeplug backup from before this image:
  `dmrconf read -D <data> -R d878uv2 before.yaml` and
  `dmrconf read -D <data> -R d878uv2 before.dfu`.
- The radio on a memory channel, not the VFO.

## Steps

| # | Do | Expect |
| --- | --- | --- |
| 1 | Flash and start; `settings` | No `crash:`; `nothing pending` or a pending zone, `last write unchanged` within 3 s; a `codeplug:` line with the codeplug's values; `no preferences record` |
| 2 | `save 1`, then `light time 60`; within 3 s `settings` | `pending: settings ...`; about 3 s after the last command one `settings kept in the codeplug` |
| 3 | `settings` | `nothing pending; last write saved`; the `codeplug:` line shows `battery save 1` and light `1 min` |
| 4 | `light time 5`, `light time 10`, `light time 15` within 2 s | One `settings kept in the codeplug`, 3 s after the last; `settings` shows light `15 s` |
| 5 | `save 1` again | No `settings kept` line; `settings` shows `last write unchanged` |
| 6 | `restart`; then `save` and `settings` | `power: battery save on, 150 ms sleep; ...`; the `codeplug:` line as in step 4; the backlight goes off 15 s after the last key |
| 7 | Menu > Channel: Squelch 4, Squelch type the other one, Power the other one; Menu > Roaming: Roaming Off, Roam zone another one | Each takes effect at once; 3 s after the last one `settings kept in the codeplug` |
| 8 | Menu > Display: Brightness 2, Light time 30 s; Menu > GPS: Receiver On; Menu > Settings > GPS: Satellites GPS; Menu > Settings > Clock: Time zone one step on | One `settings kept` line after the last |
| 9 | Zone key: pick another zone; knob two channels on; wait 3 s; `settings` | `settings kept`; `codeplug:` shows the new zone, squelch 4, light 2 30 s, GPS on systems 1, the new time zone, `auto roaming off` and the new roaming zone |
| 10 | `restart`; `settings`; look at the screen | Starts in the zone and on the channel of step 9, dimmer (level 2); the channel of step 7 has its new power and squelch type; `automatic off` in the `roam:` start line, and Menu > Roaming > Roaming shows Off |
| 11 | `light time 20`; about 2 s later start `dmrconf read -D <data> -R d878uv2 during.yaml`; when it has ended, `light time 30` | While it reads, `settings wait: the PC is programming`; the read completes without an error; right after it, `settings kept in the codeplug`; 3 s after `light time 30` (the light time of step 8 back) `settings kept` again |
| 12 | Menu > Channel > Squelch type the other one; within 3 s `stty -F /dev/<data> raw -echo; printf PROGRAM > /dev/<data>` (program mode, left 10 s after its last command); within those 10 s: knob one channel on, Menu > Channel > Squelch type the other one, `settings` | `settings wait: the PC is programming`; `pending: channel <n> channel <n+1>`; about 10 s after `PROGRAM` one `settings kept in the codeplug`; knob back and forth: both channels show their new squelch type |
| 13 | QDMR round trip: `settings`; `dmrconf read -D <data> -R d878uv2 a.yaml` | The `codeplug:` line noted |
| 14 | `dmrconf write -D <data> -R d878uv2 a.yaml`; wait for the radio to start again; `settings` | The write completes; the radio restarts; the `codeplug:` line as in step 13 (still `auto roaming off`), except that squelch B now has squelch A's value |
| 15 | `dmrconf read -D <data> -R d878uv2 b.yaml`; `diff a.yaml b.yaml` | No differences |
| 16 | Wait for the `system: uptime ...` line (each minute) | `unused stack words: ui` above 50 |

## Results

Run 2026-11-03 on a D878UVII PLUS, with this branch, the menu, home
screen, microphone gain and frequency offset branches, the write stall
branch and the loader in one image. Steps 14 and 15 with a codeplug
write: 2026-11-01, on the same branches before the review changes.

| # | Result |
| --- | --- |
| 1 | Pass: no crash; the `codeplug:` and preferences lines |
| 2 | Pass: one `settings kept` 3.4 s after the last command |
| 3 | Pass |
| 4 | Pass: one write |
| 5 | Pass: no write, `last write unchanged` |
| 6 | Pass: battery save on and light 15 s after the restart; the backlight going off was not watched |
| 7 | Pass for squelch, squelch type and another roam zone, one write; Power was not changed |
| 8 | Pass: one write after the last |
| 9 | Pass: the `codeplug:` line with all of them |
| 10 | Pass: the zone, light 2 30 s and the other values as in step 9 after the restart |
| 11 | Pass: `settings wait` during the read, kept right after it, and again 3 s after `light time 30` |
| 12 | Pass: both channels pending while the writes were refused; one write after program mode ended; both with the new squelch type. Back on the first channel before the write, its new value was in use |
| 13 | Pass |
| 14 | Pass: the write completes, the radio restarts |
| 15 | Pass: no differences other than two fields the codeplug tool changes on every write |
| 16 | Pass: 375 unused stack words in the ui task |

Roaming Off as the codeplug's Auto Roaming, run 2026-11-10 on the same
kind of image (the stack, the test tools and the loader):

| # | Result |
| --- | --- |
| 7 | Pass: `roam: automatic off`, then `settings kept in the codeplug` about 3.5 s later; On and `roam auto on\|off` kept the same way |
| 9 | Pass: `auto roaming off` in the `codeplug:` line |
| 10 | Pass: `automatic off` in the `roam:` start line after the restart |
| 14 | Not run again |

## Undo

- `dmrconf write -D <data> -R d878uv2 before.yaml` puts the codeplug's
  channels and settings back. Battery save and the zones' last channels
  are not part of what `dmrconf` writes: set them back in the menu or
  with `save <0-2>` if they differ.
- A full byte-for-byte restore: write `before.dfu` back with the data
  link's write command, block by block.

## If something is off

- `settings not kept: the codeplug write failed`: send `settings` and
  `storage`, and report both lines. The values are tried twice more,
  then dropped; the codeplug keeps its old values.
- A setting back at its old value after step 6 or 10: report which one
  and the `codeplug:` line before and after the restart.
- Step 11 read fails or hangs: report the `dmrconf` output and the log
  around it.
- `diff` shows differences in step 15: report the lines. Differences in
  fields the radio changed in steps 7-9 between steps 13 and 15 would
  mean a write slipped in during step 14.

## Report back

- The `codeplug:` lines of steps 1, 9, 10 and 14.
- The ui task's unused stack words from step 16.
- Whether the screen, the menu and the channel matched after step 10.
