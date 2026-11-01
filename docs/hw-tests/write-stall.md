# Write stall: watchdog stall record and paced block copies

A watchdog left unfed for 1.5 s is recorded as a crash naming the task
that kept the CPU, with its pc and lr; the next start logs it. A codeplug
write from the PC pauses a tick every four pages while it copies a block,
so the task that feeds the watchdog keeps running during the write.

## Before you start

- A verified backup of the radio (`dmm-radio check-backup` matches it).
- The codeplug saved with a codeplug tool, to restore if needed.
- The log port open in a terminal, logging with PC times.

## Steps

| # | Do | Expect |
| --- | --- | --- |
| 1 | `hang` on the console | `watchdog no longer fed; expect a restart within about 3 s`; the radio starts again after about 1.5 s with `last reset: software` and `crash: watchdog stall in task <name>, detail 1500` and its pc and lr |
| 2 | `dmrconf read -y a.yaml` | Done in seconds |
| 3 | `dmrconf write -y a.yaml` | Every write answered, no `Timeout`; `programming: codeplug written from the PC: restarting`, and the radio starts again with `last reset: software` and no crash line |
| 4 | During step 3, watch the `uptime` lines | One every 10 s through the whole write, each at most a few tens of ms after the full second (`.304`) |
| 5 | `dmrconf read -y b.yaml` | b.yaml is what QDMR's own encode and decode give for a.yaml |
| 6 | `dmm-radio check-backup` | Calibration, radio information and system area still match the backup |

## If a write still stalls

Note the `crash: watchdog stall` line: its task, pc and lr say what kept
the CPU (look the pc up in the image's map or with `addr2line`). Then
repeat steps 2-5 on these builds in turn, to find the first one that
stalls:

1. upstream main (e18632e)
2. the settings store branch
3. the menu branch
4. the home screen branch
5. the microphone gain branch

## Undo

Restore the saved codeplug with the codeplug tool, and flash the previous
image if needed.
