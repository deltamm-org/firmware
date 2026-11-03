# Menu: groups by use, large value screens, values kept

The menu lists what is used while operating first: Channel (squelch,
squelch type, power), Roaming (status, on or off, roaming zone), GPS
(status, receiver on or off) and Display (brightness, light time).
Settings holds the setup in groups of its own: Keys & battery (what the
lock covers, auto lock, battery save), Clock (time zone) and GPS
(satellite systems). Tools opens the log and the key test and shows the
version.

A list shows a title band with where it is and the place in it (`2/5`),
five rows, a scroll bar when there are more, a panel of two lines about
the chosen row and a hint bar. Menu on a row opens a group, a screen or
the row's value screen:

- up to four choices as large buttons, the one in use starred;
- more as one large value between arrows, its place on a scale, and the
  value in use under it; Up raises it, Down lowers it;
- choices with notes (Battery save) as a list.

Menu sets the value, Exit leaves it as it was. Exit in a list goes back
one level, onto the row it came from. P2 opens Channel straight from the
standby screen; Exit or a value set goes back there. The zone list (P1)
and the roaming status look like the menu's lists. Every screen is drawn
in place: nothing blanks before it is redrawn.

Values set in the menu take effect at once and are kept in the codeplug
(`docs/settings-storage.md`): the squelch, light, key lock, battery
save, GPS, satellites, time zone, roaming on or off and roaming zone with
the general settings, power and squelch type with the channel.

## Running it

The steps are key presses on the radio and what its screen then shows;
keep the log port open for the log lines named under Expect and for the
console commands. Keys are named as on the key test (Menu, Exit, Up,
Down, P1, P2); "hold" means holding the key until the screen changes.
"Knob 1 on" turns the channel knob one click clockwise, "knob 2 back"
two clicks anticlockwise. Each step starts where the one before ended.

## Steps

| # | Do | Expect |
| --- | --- | --- |
| 1 | On the standby screen press Menu; look at the screen | Title `Menu`, `1/6`; Channel, Roaming, GPS, Display, Settings above the panel, each with `>`, and a scroll bar; the panel reads `Squelch and power of the channel in use`; hint `Menu: open   Exit: back` |
| 2 | Press Down five times; look; knob 1 on; look | Tools at `6/6`, the panel `The log, the key test and the version`; the knob wraps round to Channel |
| 3 | Press Menu; look | `Menu > Channel`, Squelch, Squelch type, Power with their values; the panel `Open, or 1 to 5: higher` / `needs a stronger signal`; hint `Menu: change   Exit: back` |
| 4 | Press Menu; look | `Channel > Squelch`, the level large between arrows, a scale of six steps lit up to it, `In use: <n>` under it |
| 5 | Press Up; look; knob 2 back; look; press Exit | Up raises the level by one, the knob two clicks back lowers it; Exit: the list shows the old value, no `squelch` line in the log |
| 6 | Press Menu, Up, Menu | `ui: squelch <n+1>`; the list shows it; 3 s later `settings kept in the codeplug` |
| 7 | Press Down, Menu; look | `Squelch type` (the group left out of the title, which would not fit): two large buttons, the one in use starred |
| 8 | Press Exit, Down, Menu; look; press Down, Menu | Power: four buttons, the power in use starred and chosen; `ui: power <n>`; back on Power with the new value; `settings kept` 3 s later |
| 9 | Press Exit; look | `Menu`, Channel chosen |
| 10 | Press Down, Menu, Menu; look; press Exit | The roaming status list in the menu's style, hint `Menu: search   Exit: back`; Exit: `Menu > Roaming` on Status |
| 11 | Press Exit, Down, Menu, Menu; look; press Exit; look | The GPS screen; Exit: `Menu > GPS` on Status |
| 12 | Press Exit, Down, Menu; look; press Menu; look; press Exit | `Menu > Display`: Brightness and Light time with their values, the panel `Display brightness`; Menu: `Brightness`, the level large between arrows with `In use: <n>` under it; Exit: back on Brightness, the level unchanged |
| 13 | Press Exit, Down, Menu; look | `Menu > Settings`: Keys & battery, Clock, GPS; the panel `The key lock and battery save` |
| 14 | Press Menu; look; press Exit; look | `Keys & battery` (the long name alone); Exit: `Menu > Settings` on Keys & battery |
| 15 | Press Menu, Up, Menu; look; press Exit | Battery save as a list: Off, On `150 ms`, Fast wake `70 ms`, the one in use chosen; Exit leaves it |
| 16 | Press Exit, Down, Menu, Menu; look | `Clock > Time zone`: the zone large (`UTC+2:00`), a marker on a track, `In use:` under it, the place `n/53` |
| 17 | Press Exit three times, then Down, Menu, Down, Down; look; press Menu | `Menu > Tools` on Version: the panel shows the version and `up <n> min`; hint `Exit: back`; Menu does nothing |
| 18 | Press Up, Up, Menu; hold Exit, then hold Exit again | The log; holding Exit goes on to the key test, then standby |
| 19 | Press P2; look; press Exit | `Menu > Channel`; Exit goes straight back to standby |
| 20 | Press P2, Up, Up, Menu, Down, Menu | P2 opens on the row last chosen (Power); on Squelch, Down lowers it by one; `ui: squelch <n>` and straight back to standby |
| 21 | Press P1; look; press Exit | The zone list in the same style, hint `Menu: use   Exit: back` |
| 22 | Watching the screen: P2, knob 3 on, knob 3 back, Menu, knob 2 on, Exit, Exit, P1, knob 1 on, Exit, Menu, knob 1 on, knob 1 back, Exit | Each screen and each change is drawn in place: nothing blanks or flashes |
| 23 | Note the squelch and power set above. `restart`; after the start `settings`; press P2; look; press Exit | The `codeplug:` line holds the squelch; Channel shows the squelch and power as set before the restart |
| 24 | Menu > Roaming > Roaming Off: press Menu, Down, Menu, Down, Menu, Up, Menu; wait 3 s; `restart`; press Menu, Down, Menu; look | `ui: auto roaming off`, `settings kept`; after the restart Roaming shows Off and the `codeplug:` line of `settings` shows `auto roaming off` |
| 25 | QDMR round trip: `dmrconf read -D <data> -R d878uv2 a.yaml`; `dmrconf write -D <data> -R d878uv2 a.yaml`; wait for the restart | The write completes; the menu shows the same values as before; the `codeplug:` line still shows `auto roaming off` |
| 26 | `dmrconf read -D <data> -R d878uv2 b.yaml`; `diff a.yaml b.yaml` | No differences |
| 27 | By hand: turn the knob fast in a list, on Power and on Time zone | Only the rows, buttons and values that change are redrawn; nothing blanks or flashes |
| 28 | Wait for the `system: uptime ...` line (each minute) | `unused stack words: ui` above 50 |

## Results

Run 2026-11-03 on a D878UVII PLUS, with this branch, the settings store,
home screen, microphone gain and frequency offset branches, the write
stall branch and the loader in one image. Step 25 with a codeplug write:
2026-11-01; step 27 by hand: 2026-11-02, both on the same branches
before the review changes. Step 24 again on 2026-11-10, with Roaming as
the codeplug's Auto Roaming.

| # | Result |
| --- | --- |
| 1 | Pass: `1/6`, Channel to Settings above the panel |
| 2 | Pass: Tools at `6/6`; the knob wraps round to Channel |
| 3-7 | Pass |
| 8 | Pass for the four buttons and Exit; another power was not set |
| 9-11 | Pass |
| 12 | Pass: `Menu > Display`, the Brightness value screen; Exit leaves it |
| 13 | Pass: the Settings groups, Display not among them |
| 14-21 | Pass |
| 22 | Pass: nothing blanked or flashed |
| 23 | Pass: squelch and power as before the restart |
| 24 | Pass: `ui: auto roaming off` and `settings kept` about 3.5 s later; Off after the restart, `auto roaming off` in the `codeplug:` line |
| 25 | Not run again with Auto Roaming |
| 26 | Pass: no differences |
| 27 | Pass, by eye: only what changes is redrawn; turning very fast can outrun the drawing |
| 28 | Pass: 390 unused stack words in the ui task |

## Undo

- Menu > Roaming > Roaming On, and put back the squelch and power noted
  in step 23; wait for `settings kept`.
- Or `dmrconf write -D <data> -R d878uv2 before.yaml` with a codeplug read
  before the steps.
