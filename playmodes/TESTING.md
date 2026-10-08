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

Next build (20): the length follows the stock playhead (learnt from where it wraps) and the PER TRACK master scale is read from `0x8e52`; NORMAL passes the step through untouched.

Earlier plan (18): per-pattern modes, their project lines, the whole table
in battery RAM, pattern copy / paste / undo, clear pattern, PINGPONG 2; the
MASTER LENGTH cut.

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
