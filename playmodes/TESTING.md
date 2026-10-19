# Play Modes testing

## Host gate

`python3 verify.py` builds and runs the two host suites, and with
`m68k-elf-gcc` on the PATH compiles and assembles the ColdFire unit and
checks `playmodes.s` is current. On the author's Mac (Homebrew python 3.14,
m68k-elf-gcc): all pass. A deliberate defect (PINGPONG playing its end
step twice) fails the engine suite with 382 failures, so the checks bite.

What the host suites cover:

- NORMAL / REVERSED for every length 1–64 over three passes.
- PINGPONG: exact sequences for lengths 1, 2, 4; for 3–64 every move is
  ±1 and the end steps are not doubled; it keeps bouncing across passes.
- SHUFFLE: for every length 1–64 and four seeds, each of eight passes plays
  every step exactly once; the order changes between passes.
- PINGPONG 2: exact sequences for lengths 1 and 4; for 2–64 every move is
  ±1 and only the end steps repeat, once per turn.
- RANDOM: 64,000 steps over 16 within ±10 % of uniform; repeats occur.
- NORMAL scale mode: all tracks share one RANDOM / SHUFFLE order; PER TRACK:
  each its own.
- Look-ahead: the step predicted one ahead (also across the pattern end) is
  the step that then plays.
- While stopped: what stock prepares is what PLAY then plays, every mode,
  both scale modes (the "phantom step 1 at step 16" fix).
- PLAY restarts every track (via the transport-start stubs); playback alone
  never does.
- Lengths: pattern length under NORMAL scale mode; per-track length under
  PER TRACK, cut to the steps MASTER LENGTH lets the track reach, at
  different track scales; INF and 0 do not cut.
- The display (the UI's step query), the popup text, held TRACK + arrows.
- The playhead wins over the pattern bytes: pattern bytes say 10 but stock
  plays 16, and the reverse; a length edit is taken at once; NORMAL passes
  every step through, also past the computed length; MASTER LENGTH uses
  the MASTER SCALE (`0x8e52`), not the NORMAL-mode scale.
- Per pattern: two patterns keep their own modes across switches (the
  build 17 report: A01 REVERSED, A02 changed, back to A01 must be
  REVERSED).
- The project lines: their exact text, one per pattern that is not all
  NORMAL, a storing load pass starting from NORMAL, the parse-only pass
  storing nothing, other `#` lines and bad pattern names left alone, short
  lines and bad digits, build 17's line applied to every pattern.
- Battery RAM: the whole table (all 256 full rows) back after a simulated
  power cycle, a damaged copy read as all NORMAL, nothing written outside
  `0x100f8600..0x100f8f06`.
- Pattern copy / paste / undo through the memcpy hook: the clipboard and
  undo rows, the battery copy ignored, track copies and odd addresses left
  alone, the playing pattern picking up a paste.
- Clear pattern: only that pattern back to NORMAL, the playing one at once,
  the clipboard and odd addresses left alone, the clear in battery RAM.

## Hardware (author's MKII, 3 Oct 2026)

Test images `playmodes-test`, builds 12–16, on 1.40C with stock effects.

| build | result |
|---|---|
| 12 | Modes switch with TRACK + UP/DOWN, popup shows `ALL …` / `T1 …`; arrow direction right; PER TRACK shows the track's own mode. LEDs still stock. |
| 13 | LEDs follow the played step. Found: STOP + PLAY did not restart PINGPONG; PER TRACK tracks stopped at 16 (MASTER LENGTH 16, stock). |
| 14 | Restart by run start time: PINGPONG only played forward (that time is rewritten during playback). Reverted. |
| 15 | Restart from the four transport-start sites: PINGPONG bounces and restarts on PLAY. Found: NORMAL → STOP → REVERSED → PLAY fired step 1's trig once at step 16's place. |
| 16 | Mode changes rebuild the prepared step; stopped preparation uses the next run. The phantom is gone. Longer patterns (32/48/64), PER TRACK with MASTER LENGTH INF and various lengths and modes, pattern changes across banks 1–2 and tempo changes all behaved. |

| 17 | Modes saved with the project (one set for all patterns). Found: the set was shared by every pattern: A01 REVERSED, A02 changed, back on A01 it played A02's mode (as designed then). |

| 18–19 | Per-pattern modes, battery table, copy / paste / undo, clear, PINGPONG 2: about 15 minutes, all working. Found: NORMAL scale mode LEN 10, switched to PER TRACK (FUNC + SCALE shows 16/16): still played 10 steps. |

| 20 | Length learnt from the playhead: the NORMAL → PER TRACK case fixed. Found: PER TRACK, track 14 steps, MASTER LENGTH 16, REVERSED: started on step 5 and looped steps 1–2 (the master restart's 2-step pass was taken as the length). |

| 21 | Longest pass kept: REVERSED under MASTER LENGTH fixed. Found: PINGPONG under MASTER LENGTH 16 (14-step track) drifts instead of starting over at each master loop. |

Next build (22): a MASTER LENGTH restart (a pass shorter than the track's length) starts PINGPONG and PINGPONG 2 over from step 1; RANDOM and SHUFFLE start a new order, REVERSED its last step.

Earlier plan (21): the longest pass is kept, so MASTER LENGTH's short passes no longer count as the track's length.

Earlier plan (20): the length follows the stock playhead (learnt from where it wraps) and the PER TRACK master scale is read from `0x8e52`; NORMAL passes the step through untouched.

Earlier plan (18): per-pattern modes, their project lines, the whole table
in battery RAM, pattern copy / paste / undo, clear pattern, PINGPONG 2; the
MASTER LENGTH cut.

## STEP REPEAT (branch `step-repeat`, 19 Oct 2026)

Host gate: `python3 verify.py` passes (engine, glue and the ColdFire unit,
Homebrew python 3.12, m68k-elf-gcc 16.2.0 bottled). New cases: the held
steps in step order, round and round; steps past a track's length left
out; keys pressed or let go mid-loop join or leave in their place; the
second page; the look-ahead and the LEDs naming the repeated step; let go
→ the stock step; stopped, keys busy, a release of a key stock pressed:
stock's; PER TRACK only tracks in REPEAT; MIDI tracks; the REPEAT digit in
the project line and battery RAM. Planted defects fail them: a wrong
comparison in `pm_repeat_next` (7 engine, 11 adapter failures), the
repeat call removed from `pm_seq_step` (37 adapter failures). `playmodes.s` regenerated from unchanged
`main` is byte-identical, so the compiler matches.

Emulator (the ColdFire port `ot_emu`, Modwerk's `sdk/octabam` at
`c0f513b9`, remix `playmodes-test` = this module + the 14 stock effects,
the contributor's own project on a staged card, clock and transport
receive off in the staged copy, NORMAL scale mode, 16 steps, 1X, about
152 BPM; `--live-script` key events, `--watch-mem` on the step record
`pm_hist`, the held keys and the stock flags). Port evidence only: no
audio, no hardware, no MKI.

| case | result |
|---|---|
| the image boots, the loader runs once, no fatal hang | `verify_dram_boot` PASS; its runtime-bytes line FAILs on 87 bytes of `pm_state` the runtime initialises itself, the same on unchanged Play Modes (the gate knows only MIDI Scenes' words) |
| TRACK 1 + DOWN × 6 | REVERSED, PINGPONG, PINGPONG 2, RANDOM, SHUFFLE, REPEAT (stored 6) |
| T1, hold trigs 5 + 8 during stock step 6 | from stock step 7: 5 8 5 8 … for 20 steps while the stock counter ran 7..16, 1..10; held mask `0x10`, `0x90` |
| let go during stock step 10 | the next step plays 11, then 12, 13 …: the background count is where the track goes on |
| MIDI mode (M1), the same hold | M1 and T1 both 5 8 5 8 … and both go on at 11 (the shared mode reaches every track) |
| GRID REC on, the same hold | nothing taken (held mask never written), every step stock's |
| LIVE REC (REC + PLAY), the same hold | nothing taken, every step stock's |
| trig mode 1 (poked: CHROMATIC), the same hold | nothing taken, every step stock's |
| stopped, the same hold | nothing taken, nothing plays |

Emulator setup notes: the stock SET DATE/TIME popup opens after the load
(battery RAM has no "last set" date, `--rtc host` or not) and takes UP /
DOWN; the script dismisses it with NO first. With `--scenario`, pass
`--watch-mem` to the parent: the ranges are armed before the load.

Not yet for STEP REPEAT: hardware (either model); audio and MIDI out
(note lengths over repeated steps); PER TRACK and patterns longer than 16
steps under the port; the page the trig LEDs show (the playhead's page is
used); micro-timing, conditions and slides on repeated steps; the stress
project.

## Not yet

- MIDI tracks beyond a first check; the MIDI length offset `+0x28` is
  inferred.
- Micro-timing, trig conditions, slides and live recording, checked on
  purpose.
- MKI; combinations with other modules.
- Worst-case cycles, the 60-minute eight-track stress project, UI captures.

## Resources (estimate, unmeasured)

DRAM: 224 bytes of state, 32 of flags and popup text, plus a few hundred
bytes of code. CPU: per track per step, a few table reads, a pass test and
one mapping; SHUFFLE's walk is two permutation evaluations on average
(three multiplies each). No DSP code.
