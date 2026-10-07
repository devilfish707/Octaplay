# PLAY MODES — what is known, what has to be found

OS 1.40C, ColdFire side. Sections 1-3 come from published sources, cited per
row. Section 4 was read from the owner's original 1.40C with
`investigate.py` (3 Oct 2026); only addresses and conclusions are kept
here, never the listings. ✅ = measured
by the cited author (emulator or unit), 📖 = read from code by them, ❓ =
inferred, unconfirmed.

Sources: octabam `docs/firmware/` ([sambanks/octabam](https://github.com/sambanks/octabam),
`f6ce41d`), SCALE QUANTIZER and DIRECT JUMP ([timhastie/octatrick-modules](https://github.com/timhastie/octatrick-modules)),
the KYOTI notes and modules ([Zac-Kyoti/octatrack-kyoti-fw](https://github.com/Zac-Kyoti/octatrack-kyoti-fw)
`NOTES.md`), OctaKit's recipe (`../octakit/upstream/runtime/abi.inc`) and
EUCLID (`../../octabam/modules/euclid/control.c`).

## 1. Anchors

### The clock and the master step

| what | where | source | |
|---|---|---|---|
| tick interrupt (24 PPQN, 6 ticks per 1/16) | `0x400a1e10`; body `FUN_400a1eea` | `tools/emu/emu_frames.py`, KYOTI | ✅ |
| transport | `0x800065b8`: 0 stopped, 1 playing | EUCLID, emu_frames | ✅ |
| tick within the step | `0x800065b6`, 0..5 | DIRECT JUMP README | ✅ |
| master step counter | `0x800065b2` (word), wraps at the pattern end | DIRECT JUMP, SCALE QUANTIZER | ✅ |
| the step byte the trig LEDs follow | `0x800065b5` | DIRECT JUMP ("b5") | ✅ |
| step boundary, pattern switch, per-track repositioning | `0x400a4220` (tick 0), `0x400a42fa..0x400a439c`, `0x400a47f6..0x400a4b7c` | DIRECT JUMP | 📖✅ |
| common per-step tail (every step, switch or not) | `0x400a4ba0` | KYOTI | 📖 |
| playing bank / pattern | `0x800065bd` / `0x800065be` | EUCLID, DIRECT JUMP | ✅ |

### Per track

| what | where | source | |
|---|---|---|---|
| running state | `0x80006500[t]` (`== 1` = processed) | emu_frames, KYOTI | ✅ |
| sub-step tick divider | `0x800064f0[t]`, wraps at `ticks[scale]` (`0x400aba50`: 3 4 6 8 12 24 48) | KYOTI | 📖 |
| step-in-pattern counter (REFILL) | `0x800064d0[t]`, compared with the track length, then `jsr 0x400a536c(t)` | KYOTI | 📖✅ |
| pass / remainder tables | `0x800065e4/f4[t]`, `0x80006604/14[t]` (audio / MIDI) | KYOTI (roles disputed across sessions) | ❓ |
| per-track trig-timing dispatcher | `0x400a3ca4..0x400a3ecc`, audio then MIDI loop | KYOTI | 📖 |
| **the step handler** | `0x4009d1e8(track, bank, pattern, step, n)`: tests the step's mask bits, schedules the fire time `0x80001904[t]`, builds the lock record (`0x4009d8c6` plain, `0x4009d704` slide) | emu_frames, STEP_LOCKS.md, KYOTI | ✅ |
| its mask/condition consumers | `0x4009d382..0x4009da12`, flag word `0x46c7a6c0` | KYOTI | 📖 |
| OctaKit's sequencer wrap points | condition `0x40099ddc`, advance `0x40099ef4`, tick sites `0x400a1778..0x400a1e02` | OctaKit `abi.inc`, recipe | 📖 |

### Pattern data (bank blob `0x400e21e0 + bank·0x9b340`)

| what | offset | source | |
|---|---|---|---|
| pattern | `+ p·0x8ed8` | STEP_LOCKS.md | ✅ |
| length / scale / scale mode | `+0x8e53` / `+0x8e54` / `+0x8e55` | EUCLID, RLEN PLEN | ✅ |
| audio track record | `+ t·0x91a`; length `+0x50`, scale `+0x51`, trig masks `+0x00..0x0f` | EUCLID, KYOTI | ✅ |
| MIDI track record | `+0x48d0 + t·0x8b0`; scale `+0x29` | STEP_LOCKS.md, KYOTI | 📖 |
| MIDI track length | `+0x28` | by analogy with audio | ❓ |
| step locks | track `+0x59 + step·32 + slot` | STEP_LOCKS.md | ✅ |

### UI

| what | where | source | |
|---|---|---|---|
| toast | `0x4005a2b8(text, duration)`, duration > 0 (1/60 s units) | KYOTI quantize-live-rec-toggle | ✅ unit |
| toast on screen | handle `0x460d1e70` ≠ 0, countdown `0x460d1e6c` | same | ✅ unit |
| key code | row·8 + bit; REC `0x29` (handler `0x40048774`), PLAY `0x28` (`0x40061778`), LEFT `0x34`, RIGHT `0x21` | KYOTI, SCALE QUANTIZER, octabam PANEL.md | ✅ |
| current track | `0x100b14cc` | octabam ARCHITECTURE.md | 📖 |

### Rules learned on units (keep)

- Open or close a toast **only from a key handler**. Reaching the kernel post
  `0x40000c3c` from the frame path hard-crashed a MKI (KYOTI, Session 93).
- Toast duration ≤ 0 hung a MKI (KYOTI, Session 50).
- `0x80006a40..` is not scratch: the live DSP path overwrites it (KYOTI,
  Session 95). State lives in loader-owned DRAM (`hooks.s`), as EUCLID's.
- EUCLID owns the PLAY detours `0x4009c3d4` / `0x4009c4d4`; PLAY MODES only
  observes the transport byte, so the two can share a remix.

## 2. What has to be found

1. **The step source.** Where the tick turns a track's position into the
   `step` argument of `0x4009d1e8` (and, for MIDI tracks, the equivalent
   call). One detour there, calling `pm_step_entry(track, raw)` and using its
   result as `step`, gives REVERSED / PINGPONG / RANDOM / SHUFFLE for trigs,
   trigless trigs, locks and slides together.
2. **Every other reader of the same step**, which must see the mapped step
   too (`pm_peek_entry` where it looks ahead):
   - micro-timing: a trig with a negative offset is scheduled before its
     step, so the scheduler looks one step ahead;
   - trig conditions (FILL, PRE / NEI, 1ST, A:B) and their pass counters;
   - slides (the previous / next step's values);
   - live recording, which should land on the step you hear;
   - the tick-2 pre-check before a pattern change (DIRECT JUMP's notes).
3. **The trig LED playhead.** `0x800065b5` is one byte for all tracks; the
   track display must show the mapped step of the selected track.
4. **The keys.** The [TRACK 1..8] handlers (and how MIDI mode selects M1..8),
   a "TRACK held" flag, and the [UP] / [DOWN] handlers, plus whether stock
   already uses TRACK + UP/DOWN anywhere (if it does, use another chord,
   e.g. FUNC + TRACK + UP/DOWN).
5. **Persistence (later).** A battery-backed home for 17 bytes, or the
   project file, so modes survive power-off; then per-pattern storage.

## 3. Procedure

### Step 1: static read (your machine, a few seconds)

```sh
python3 sdk/drafts/playmodes/investigate.py --os <path>/section_3_MAIN_OS.bin
```

It checks the file is the original 1.40C main OS, disassembles the ranges in
section 1 (about 4,000 lines), lists every instruction naming the watched
RAM and dumps the key-map pointer tables around the known REC / PLAY
handlers. The result goes to `sdk/octabam/out/playmodes-probe.txt` (git-
ignored). It is Elektron code: never commit it. Whether it is shared is the
owner's decision.

### Step 2: name the sites

From the listing: the call(s) to `0x4009d1e8` and where their `step` comes
from; the micro-timing look-ahead; the condition counters; the LED byte's
writers and readers; the TRACK / UP / DOWN handlers from the key map.

### Step 3: confirm under the port before any flash

With `tools/emu` (or the octabam panel server), a project with one trig on
step 1 of a 16-step pattern: watch the chosen site's `step` and the fire
table `0x80001904[t]`; with a diagnostic hook that only logs, the raw step
must run 0..15. Then with REVERSED, the trig on step 1 must fire at step 16's
time, its locks with it.

### Step 4: hardware, in order

1. REVERSED, all tracks, NORMAL scale mode, plain trigs only.
2. PINGPONG and SHUFFLE / RANDOM.
3. PER TRACK scale mode with different lengths and scales per track.
4. Micro-timing, conditions, slides, live recording.
5. MIDI tracks.
6. The 60-minute eight-track stress project (`tools/harness/stress_project.py`).

## 4. Read from 1.40C (investigate.py rounds 1 and 2, 3 Oct 2026)

### The step every track plays

| what | where | |
|---|---|---|
| audio: the tick loads `step = 0x800064d0[t]` (t in d5) and calls `0x4009d1e8(t, bank, pattern, step, n)` | `0x400a2d6a` lea, `0x400a2d70` mvzb, call `0x400a2d7e` | 📖 |
| MIDI: `step = 0x800064d0[8 + t]` through the walking pointer at `100(%sp)`, t in d7, call `0x4009cf4c(t, bank, pattern, step, n)` | `0x400a39b6`, `0x400a39ba`, call `0x400a39c6` | 📖 |
| `0x4009d1e8` derives everything from its `step` argument: trig mask bit (`1 << step` over the four masks at track `+0x00/08/10/18`), condition word at track `+0x89a + 2·step` tested by `0x400a5164`, micro-timing from the same word, swing mask at track `+0x40`, locks | `0x4009d280..0x4009d3f2` | 📖 |
| the only other caller: the working-set rebuild `0x4009da20` after an edit, step computed from the clock into d2, `n = -1`; audio at `0x4009dc86`, MIDI (d6 > 7) at `0x4009e3dc` → `0x4009cf4c` | | 📖 |
| `0x800064d0[t]` is cleared on restarts at `0x400a29fc` / `0x400a2b0a` (Kyoti's "REFILL" counter is the step index) | | 📖 |
| trig conditions count passes per track in `0x46107918[t]` (`0x400a536c`), independent of the step | | 📖 |

So one remap of the step argument at each call moves trig, trigless trig,
condition, micro-timing, swing and locks together. Swing therefore follows
the **played** step's swing bit.

### Keys (two key maps of 26-byte records: MKI `0x400bfbf6..`, MKII `0x400c01f4..`)

| codes | handler | what |
|---|---|---|
| `0x00..0x0f` | `0x40060ce0` | the 16 trig keys |
| `0x10..0x17` | `0x40040250` | TRACK 1..8; `0x400c0aac` is its double-tap memory (set on press, kept after release), not a held flag |
| `0x22..0x26` | `0x4005578c` | the five parameter page keys |
| `0x34`, `0x21` | `0x4004b970` | LEFT, RIGHT (SCALE QUANTIZER) |
| `0x33`, `0x20` | `0x400491a0` | UP, DOWN (which is which: to confirm; `0x33` assumed UP) |
| `0x29`, `0x28`, `0x2f` | REC, PLAY, BANK | as published |

Handlers are `handler(code, event)`; the REC handler `0x40048774` takes
event 0 as the release, so 1 = press, 0 = release. `0x400491a0(code, event)`: on a press it only acts inside the arranger
(`0x460d1aec`) or one mode check (`0x46c7dde6`, `0x8000004b == 3`, not MIDI
mode); otherwise it returns. TRACK held + UP/DOWN is therefore free on the
main screen. MIDI mode flag `0x80000012`.

### Detours in the first test build

| site | bytes | stub |
|---|---|---|
| `0x400a2d6a` | 6 | `pm_audio_step` |
| `0x400a39b6` | 8 (6 + nop) | `pm_midi_step` |
| `0x4009dc86` | 6 | `pm_audio_rebuild` |
| `0x4009e3dc` | 6 | `pm_midi_rebuild` |
| `0x40040250` | 6 | `pm_track_key` (keeps the held TRACK key) |
| `0x400491a0` | 6 | `pm_arrow_key` |

No branch in the probed ranges lands inside a displaced span.

### The playhead the UI shows (round 3)

`0x4009b2b0(track)`: `track < 0` returns the master step `0x800065b4`;
otherwise it branches to `0x4009b2be` for the track's own step. Its 11
callers are all UI code (`0x40033ffc`, `0x40035b5a`, `0x40035d88`,
`0x400373e8`, `0x40038686`, `0x40038894`, `0x40044528`, `0x4004d1dc`,
`0x4004d504`, `0x40061f34`, `0x40061f4e`): the trig LEDs, live recording
and lock editing, with the current track `0x80000000` (MIDI as 8 + t) or
-1. Detour `pm_step_getter` at `0x4009b2b0` (6: `movel %sp@(4),%d0 ;
bges`) maps both answers.

### The project file (round 4, 7 Oct 2026)

`project.work` is text, one `KEY=value` line per setting. SCALE QUANTIZER
(Tim Hastie) found the loader and writer and measured how the file moves:
SAVE writes `project.work` and copies every `.work` to its `.strd`; RELOAD
copies `.strd` back and loads; PROJECT > CHANGE first writes the working
state, then loads. A power cycle reads no file: the unit comes back from
battery RAM. Read here from 1.40C:

- Loader `0x400866c4(file, storing)`: `0x400866d4: movel %d0,%sp@(1158) ;
  seq %d0` with d0 = `storing` (0 on the parse-only pass); `58(sp)` keeps
  the inverse (nonzero = parse-only). The quantizer's entry stub at
  `0x400866cc` returns here. Detour `pm_proj_begin`: a storing pass resets
  the modes to NORMAL.
- Its comment check `0x400867aa: cmpl %d0,%d5 ; beqw 0x40088224`, d5 = '#',
  d0 = the line's first character, d3 = the line (NUL-ended, no CR LF);
  `0x40088224` is the loop's next line. The quantizer's line stub at
  `0x400867a2` returns here. Detour `pm_proj_line`.
- Writer `0x400882a2`: per setting `sprintf(d2, fmt, v)` (a4), `strlen`
  (a3), `write(d3, d2, n)` (a2 = `0x400166b8`). At `0x400888b2` (`pea
  0x400b8244`, PATTERN_CHANGE_AUTO_SILENCE_TRACKS's format) its value is
  already pushed; the quantizer's writer stub at `0x400888aa` returns here.
  Detour `pm_proj_write` writes `#PLAY_MODES=<17 digits>\r\n` first.
- No dirty flag: the CHAIN AFTER setter `0x400659ec` only stores
  `0x8000004e` and its battery mirror `0x100b14ae`; the PERSONALIZE setter
  `0x40068ca0` likewise (`0x800000ac`, `0x100fff3c`). Project settings
  reach the file whenever the writer runs. `0x100f8598` (set before
  `0x40027e00` at 461 sites) is the pattern-edit path, not needed here.
- Battery RAM `0x100b14e2..0x100b14ef` is linker padding before the project
  record `0x100b14f0`, with no stock reference (the quantizer's watch and
  reference scan); the quantizer holds `0x100b14ec..ee`. The modes take
  `0x100b14e2..ea` as nibbles.

### Still open

- Whether `0x800064d0[t]` runs 0..len-1 or 1..len at the call (a REVERSED
  test with one trig on step 1 tells: it must sound on step 16).
- Whether TRACK + UP/DOWN reaches `0x400491a0` on every screen (a page
  with its own input layer may take the arrows first).
- MIDI per-track length offset (`+0x28`, inferred).
- PROJECT > NEW: whether it passes through the loader. If not, a new
  project keeps the previous modes until it is saved and reloaded.
