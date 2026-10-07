# Octaplay

Playhead modes for the Elektron Octatrack: run the sequencer **normal,
reversed, pingpong, random or shuffle**, shared by every track or set per
track (audio T1–T8 and MIDI M1–M8). A firmware module for original OS
1.40C, built with the [octabam](https://github.com/sambanks/octabam) /
octamod remixer.

![Play Modes: normal, reversed, pingpong and shuffle playhead paths over 16 steps](playmodes/presentation/thumbnail.svg)

**Status: experimental.** Played on an MKII in test images; not
stress-tested. Back up your projects and keep a stock OS file at hand.
Flashing modified firmware is at your own risk.

## What it does

| mode | 16 steps play as |
|---|---|
| NORMAL | 1 2 3 … 16 (stock) |
| REVERSED | 16 15 14 … 1 |
| PINGPONG | 1 … 16 15 … 2, 1 … 16 …: the end steps play once |
| RANDOM | any step each time; a step can repeat |
| SHUFFLE | every step once per pass, in a new order every pass |

- **Hold [TRACK] + [UP]/[DOWN]** to change the mode; a popup shows it
  (`ALL PINGPONG`, `T3 REVERSED`, `M2 SHUFFLE`).
- **SCALE MODE NORMAL:** one mode for all tracks, moving together.
  **PER TRACK:** each track its own mode, length and scale.
- Each step's trig, trig condition, micro-timing, swing bit and p-locks move
  with it. The trig LEDs, live recording and lock editing follow the step
  you hear. Tempo, lengths, pattern changes and chains stay stock.
- PLAY and pattern changes start every track from its first step again.
- Every pattern keeps its own modes. They are saved with the project
  (`#PLAY_MODES=` lines in `project.work`, carried to `project.strd` by
  SAVE), survive a power cycle and go along with pattern copy / paste; clearing a pattern resets them.
  (Not combinable with PLOCKS P2.)

Full description, controls and limitations: [playmodes/README.md](playmodes/README.md).

## Building a test image

You need an octamod (or octabam) checkout that already builds images: the
m68k-elf toolchain, the vendor tools (`make setup`) and your **own** original
OS 1.40C (`make os` / `make recon`, or your existing `out/raw` and
`downloads/extracted`). No firmware is included here or should ever be
committed.

```sh
# 1. host tests, then the ColdFire unit
python3 playmodes/verify.py
python3 playmodes/generate.py

# 2. into the remixer (paths relative to octamod's sdk/octabam)
cp -R playmodes/ <octamod>/sdk/octabam/modules/playmodes/
mkdir -p <octamod>/sdk/octabam/remixes/playmodes-test
cp playmodes/test-remix.py <octamod>/sdk/octabam/remixes/playmodes-test/remix.py

# 3. build
cd <octamod>/sdk/octabam
make image REMIX=playmodes-test BUILD=<n>
```

Flash `out/OCTATRACK_OCTABAM<n>.bin` from the card (or the `.syx` over
SysEx) as with any octabam image.

## How it works

The stock sequencer keeps counting its steps; Octaplay replaces the step
number at the two places the sequencer turns it into "play this step"
(audio and MIDI), at the rebuild that runs after edits, and at the UI's
"which step is this track on" query. Three more detours read and write the
project file's lines, and twenty pattern-copy sites carry the modes along. Every answer is a pure function of
(mode, length, pass, step, seed), so look-ahead readers and what the
sequencer prepares while stopped agree with what then plays. Thirty-five
guarded detours in total; each refuses to build if the stock bytes at its
site differ. Details and every address with its source:
[playmodes/INVESTIGATION.md](playmodes/INVESTIGATION.md).

## Repository layout

| path | what |
|---|---|
| `playmodes/` | the module: source, manifest, tests, docs, thumbnail |
| `playmodes/investigate.py` | probe of your own 1.40C: `--os <path>/section_3_MAIN_OS.bin`, writes to `out/` (Elektron code: never commit it) |

## Credits and licence

By [devilfish707](https://github.com/devilfish707), MIT
([LICENSE](LICENSE)). Built on the reverse-engineering and tooling of
octabam (Sam Banks), SCALE QUANTIZER and DIRECT JUMP (Tim Hastie), the KYOTI
firmware notes (Zac-Kyoti) and OctaKit (June Kiff). Octatrack is a
trademark of Elektron; this project is not affiliated with Elektron, and
contains no Elektron code, tables or images.
