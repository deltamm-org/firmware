# Menu: groups by use, large value screens, values kept

The menu lists what is used while operating first: Channel (squelch,
squelch type, power), Roaming (on or off, status, roaming zone), GPS
(on or off, status, satellite systems) and Display (brightness, light
time). Settings holds the setup in groups of its own: Keys (what the
lock covers, auto lock), Battery (battery save) and Clock (time zone).
Tools opens the log and shows the version; a debug build also has the
key test there.

A list shows a title band with where it is and the place in it (`2/5`),
five rows, a scroll bar when there are more, a panel of two lines about
the chosen row and a hint bar. Menu on a row opens a group, a screen or
the row's value screen; a digit from 1 to 9 opens the row at that place.
A value screen shows its choices in one font, the largest its longest
choice fits:

- up to four choices as large buttons, the one in use starred;
- more as one large value between arrows, its place on a scale, and the
  value in use under it; Up raises it, Down lowers it;
- choices with notes (Battery save) as a list.

The squelch, squelch type, brightness and time zone take effect as soon
as they are chosen, so the change is heard or seen at once; Menu keeps
the value, Exit puts back the one in use before. Other values take
effect on Menu, and Exit leaves them as they were. Exit in a list goes
back one level, onto the row it came from. P2 opens Channel straight
from the standby screen; Exit or a value set goes back there. The zone
list (P1) and the roaming status look like the menu's lists; the
roaming status names the repeaters heard and points at the one in use.
On the standby screen Up and Down do nothing: the channel knob steps
the channel. Every screen is drawn in place: nothing blanks before it is
redrawn.

Values set in the menu are kept in the codeplug
(`docs/settings-storage.md`): the squelch, light, key lock, battery
save, GPS, satellites, time zone, roaming on or off and roaming zone with
the general settings, power and squelch type with the channel.

## Running it

The steps are key presses on the radio and what its screen then shows;
keep the log port open for the log lines named under Expect and for the
console commands. Keys are named as on the radio (Menu, Exit, Up,
Down, P1, P2, the digits); "hold" means holding the key until the screen
changes. "Knob 1 on" turns the channel knob one click clockwise, "knob 2
back" two clicks anticlockwise. Each step starts where the one before
ended. Steps 31 and 32 need an FM channel and a roaming DMR channel.

## Steps

| # | Do | Expect |
| --- | --- | --- |
| 1 | On the standby screen press Menu; look at the screen | Title `Menu`, `1/6`; Channel, Roaming, GPS, Display, Settings above the panel, each with `>`, and a scroll bar; the panel reads `Squelch and power of the channel in use`; hint `Menu: open   Exit: back` |
| 2 | Press Down five times; look; knob 1 on; look | Tools at `6/6`, the panel `The log and the version` (`The log, the key test and the version` in a debug build); the knob wraps round to Channel |
| 3 | Press Menu; look | `Menu > Channel`, Squelch, Squelch type, Power with their values; the panel `Open, or 1 to 5: higher` / `needs a stronger signal`; hint `Menu: change   Exit: back` |
| 4 | Press Menu; look | `Channel > Squelch`, the level large between arrows, a scale of six steps lit up to it, `In use: <n>` under it |
| 5 | Press Up; look; knob 2 back; look; press Exit | Up raises the level by one and the radio uses it at once: `ui: squelch <n+1>`; the knob two clicks back lowers it: `ui: squelch <n-1>`; Exit puts the old level back: `ui: squelch <n>`, the list shows it, and no `settings kept` follows |
| 6 | Press Menu, Up, Menu | `ui: squelch <n+1>` on Up; the list shows it; 3 s after Menu `settings kept in the codeplug` |
| 7 | Press Down, Menu; look | `Squelch type` (the group left out of the title, which would not fit): two large buttons, `Carrier` and `CTCSS/DCS` in the same font, the one in use starred |
| 8 | Press Exit, Down, Menu; look; press Down, Menu | Power: four buttons, the power in use starred and chosen; no `ui: power` line before Menu; then `ui: power <n>`; back on Power with the new value; `settings kept` 3 s later |
| 9 | Press Exit; look | `Menu`, Channel chosen |
| 10 | Press Down, Menu; look; press Down, Menu; look; press Exit | `Menu > Roaming`: Roaming, Status, Roam zone, on Roaming; the roaming status list in the menu's style, hint `Menu: search   Exit: back`; Exit: `Menu > Roaming` on Status |
| 11 | Press Exit, Down; look; press Menu; look; press Down, Menu; look; press Exit | GPS chosen, the panel `On or off, the fix and` / `the satellite systems`; `Menu > GPS`: Enabled, Status, Satellites, on Enabled, the panel `The GPS receiver on or` / `off`; the GPS screen; Exit: `Menu > GPS` on Status |
| 11b | Press Down; look; press Menu; turn the knob through every choice; press Exit | The panel `Satellite systems used:` / `GPS, GLONASS, BeiDou`; `GPS > Satellites`: every choice in the same font, the last `All types`; Exit leaves the systems as they were (no `gps:` line) |
| 12 | Press Exit, Down, Menu; look; press Menu; look; press Up; look; press Exit | `Menu > Display`: Brightness and Light time with their values, the panel `Display brightness`; Menu: `Brightness`, the level large between arrows with `In use: <n>` under it; Up: the display changes brightness at once, `ui: brightness <n+1>` (1 after 5); Exit: as bright as before, `ui: brightness <n>`, back on Brightness, the level unchanged |
| 13 | Press Exit, Down, Menu; look | `Menu > Settings`: Keys, Battery, Clock; the panel `What the key lock covers,` / `and auto lock` |
| 14 | Press Menu; look; press Exit; look | `Settings > Keys`: Lock covers and Auto lock; Exit: `Menu > Settings` on Keys |
| 15 | Press Down, Menu, Menu; look; press Exit | `Settings > Battery`; Battery save as a list: Off, On `150 ms`, Fast wake `70 ms`, the one in use chosen; Exit leaves it |
| 16 | Press Exit, Down, Menu, Menu; look; press Up; look; press Exit | `Clock > Time zone`: the zone large (`UTC+2:00`), a marker on a track, `In use:` under it, the place `n/53`; Up: the clock in the status bar half an hour on at once (when the clock is set), `ui: time zone ...`; Exit: the clock and the zone as before |
| 17 | Press Exit twice, then Down, Menu, Up; look; press Menu | `Menu > Tools` on Version (the last row): the panel shows the version and `up <n> min`; hint `Exit: back`; Menu does nothing |
| 18 | Press 1; look; hold Exit; in a debug build hold Exit again | 1 opens the log; holding Exit goes on to standby, in a debug build first to the key test (title `Keys (hold Exit)`) |
| 19 | Press P2; look; press Exit | `Menu > Channel`; Exit goes straight back to standby |
| 20 | Press P2, Up, Up, Menu, Down, Menu | P2 opens on the row last chosen (Power); on Squelch, Down lowers it by one; `ui: squelch <n>` and straight back to standby |
| 21 | Press P1; look; press Exit | The zone list in the same style, hint `Menu: use   Exit: back` |
| 22 | Watching the screen: P2, knob 3 on, knob 3 back, Menu, knob 2 on, Exit, Exit, P1, knob 1 on, Exit, Menu, knob 1 on, knob 1 back, Exit | Each screen and each change is drawn in place: nothing blanks or flashes |
| 23 | Note the squelch and power set above. `restart`; after the start `settings`; press P2; look; press Exit | The `codeplug:` line holds the squelch; Channel shows the squelch and power as set before the restart |
| 24 | Menu > Roaming > Roaming Off: press Menu, 2, 1, choose Off, Menu; wait 3 s; `restart`; press Menu, 2; look | `ui: auto roaming off`, `settings kept`; after the restart Roaming shows Off and the `codeplug:` line of `settings` shows `auto roaming off` |
| 25 | QDMR round trip: `dmrconf read -D <data> -R d878uv2 a.yaml`; `dmrconf write -D <data> -R d878uv2 a.yaml`; wait for the restart | The write completes; the menu shows the same values as before; the `codeplug:` line still shows `auto roaming off` |
| 26 | `dmrconf read -D <data> -R d878uv2 b.yaml`; `diff a.yaml b.yaml` | No differences |
| 27 | By hand: turn the knob fast in a list, on Power and on Time zone | Only the rows, buttons and values that change are redrawn; nothing blanks or flashes |
| 28 | Wait for the `system: uptime ...` line (each minute) | `unused stack words: ui` above 50 |
| 29 | On the standby screen press Menu, 1; look; press 3; look; press 9; press Exit three times | 1 opens `Menu > Channel`; 3 opens the Power value screen (no `ui: power`); 9 does nothing; Exit, Exit, Exit: standby |
| 30 | On the standby screen press Up, then Down; look; knob 1 on, 1 back | Up and Down leave the channel as it is (no `fm: receiving` or `ui:` line); the knob steps one channel on and back |
| 31 | On an FM channel press Menu, 2; look; press 2; look; press Exit twice, then Exit | Roaming > Status shows `DMR only`; the status screen's In use row `DMR only` |
| 32 | On the roaming DMR channel, with its repeaters heard: press Menu, 2, 2; look; press Exit three times | Each repeater heard by its name from the codeplug (its frequency when it has none), its level and age; a green pointer left of the name of the one in use (black on the chosen row); In use shows the state alone (`Good`) |

## Results

Run 2026-11-03 on a D878UVII PLUS, with this branch, the settings store,
home screen, microphone gain and frequency offset branches, the write
stall branch and the loader in one image. Step 25 with a codeplug write:
2026-11-01; step 27 by hand: 2026-11-02, both on the same branches
before the review changes. Step 24 again on 2026-11-10, with Roaming as
the codeplug's Auto Roaming.

Steps changed for the review of 2026-11-11 (live values, number keys,
the merged GPS group, Keys and Battery, the key test in debug builds,
Up and Down on the standby screen, the roaming status) are to be run
again: 2, 5, 6, 7, 8, 10, 11, 11b, 12 to 18, 24 and 29 to 32.

| # | Result |
| --- | --- |
| 1 | Pass: `1/6`, Channel to Settings above the panel |
| 2 | To be run again (the Tools panel without the key test) |
| 3-4 | Pass |
| 5-7 | To be run again (the squelch tried at once; one font) |
| 8 | To be run again; before: pass for the four buttons and Exit, another power was not set |
| 9 | Pass |
| 10-11b | To be run again (the switch first, the merged GPS group) |
| 12 | To be run again (the brightness tried at once) |
| 13-18 | To be run again (Keys and Battery, the time zone tried at once, the key test in debug builds) |
| 19-21 | Pass |
| 22 | Pass: nothing blanked or flashed |
| 23 | Pass: squelch and power as before the restart |
| 24 | To be run again (the new keys); before: pass, `ui: auto roaming off` and `settings kept` about 3.5 s later; Off after the restart, `auto roaming off` in the `codeplug:` line |
| 25 | Not run again with Auto Roaming |
| 26 | Pass: no differences |
| 27 | Pass, by eye: only what changes is redrawn; turning very fast can outrun the drawing |
| 28 | Pass: 390 unused stack words in the ui task |
| 29-32 | To be run |

## Undo

- Menu > Roaming > Roaming On, and put back the squelch and power noted
  in step 23; wait for `settings kept`.
- Or `dmrconf write -D <data> -R d878uv2 before.yaml` with a codeplug read
  before the steps.
