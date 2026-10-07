"""PLAY MODES -- the sequencer playhead plays NORMAL, REVERSED, PINGPONG,
RANDOM or SHUFFLE: one shared mode when the pattern's scale mode is NORMAL,
one per track (T1..T8 and M1..M8) when it is PER TRACK. Hold a [TRACK] key
and press [UP] / [DOWN] to change it; a toast shows the result.

How: the stock per-track step counter keeps running (tempo, scale, length,
swing, pattern end, chains and CHAIN AFTER stay stock); where the sequencer
turns a track's step into "fire this step's trig, with these locks", the
step is replaced by the engine's answer (playmode.c). Every answer is a pure
function of (mode, length, pass, step, seed), so look-ahead readers
(micro-timing, the tick-2 pre-check) see exactly the step that then plays.

STATUS (7 Oct 2026): experimental. Engine and glue host-tested; the sites
below were read from the author's original 1.40C (investigate.py,
INVESTIGATION.md sections 4 and 6) and are guarded by their stock bytes. Played on
the author's MKII (test builds 12-16); the MASTER LENGTH cut and the
project save are not yet played (build 17). Not stress-tested (TESTING.md).

  0x400a2d6a  audio tick: step = 0x800064d0[t] before 0x4009d1e8 -> mapped
  0x400a39b6  MIDI tick: step = 0x800064d8[t] before 0x4009cf4c -> mapped
  0x4009dc86  audio rebuild after an edit (n = -1): the current step, mapped
  0x4009e3dc  MIDI rebuild: the same
  0x4009b2b0  UI step(track): trig LEDs, live recording, lock editing
  0x40040250  TRACK key handler: remember which TRACK key is held
  0x400491a0  UP / DOWN handler: with a TRACK key held, change the mode

  0x4009c3da, 0x400a2210, 0x400a24d6, 0x400a27e8  transport starts: restart

  0x400866d4  project loader head: a storing pass starts from NORMAL
  0x400867aa  project loader '#' lines: read "#PLAY_MODES=A01:<17 digits>"
  0x400888b2  project writer: one such line per pattern that is not all
              NORMAL (project.work; SAVE copies it to project.strd)

EUCLID owns 0x4009c3d4 / 0x4009c4d4 (the PLAYING stores); its PLAY stub
returns to 0x4009c3da, this module's site, so the two compose. Pattern
switches are observed from the sequencer's own bytes. No 0x80006a40 scratch
RAM (overwritten by the live DSP path on hardware); state is loader-owned
DRAM (hooks.s). Every pattern of every bank has its own modes (a 256-row
table in that DRAM), kept in the project file's "#PLAY_MODES=" lines and,
for a power cycle (which reads no project file), sparse in battery RAM
0x100ffe00..0x100fff00 + 0x100f859c..0x100f8600 (free; PLOCKS P2 holds the
range between). SCALE QUANTIZER's three project-file stubs return to
this module's three sites, so the two compose.
"""
from remix.schema import Category, Proof, Detour, Kind, Linked, Module, Gate
from remix.stock_guard import stock_guard

MODULE = Module(
    name="playmodes",
    key="PLAY MODES",
    kind=Kind.CF_PATCH,
    category=Category.TRACK,
    author="devilfish707", author_url="https://github.com/devilfish707",
    proof=Proof.UNTESTED,
    proof_note="host-tested; played on the author's MKII (test builds 12-16), not stress-tested",
    doc="Playhead direction per track or shared: normal, reversed, pingpong, random, shuffle.",
    linked=(Linked("playmodes", "modules/playmodes/playmodes.s", cpu="5475", dram=True),),
    detours=(
        Detour(0x400a2d6a, stock_guard(0x400a2d6a, 6, "1cb5e6a4478a2248b033baac126f63d4930860b767556518139cf5f717c08433"),
               "playmodes", "pm_audio_step", "audio tick: play the mapped step", kind="jmp"),
        Detour(0x400a39b6, stock_guard(0x400a39b6, 8, "18e60ebae134411f69992d7a95cb9359e37d3de14ef3606cb7da616fad9839bb"),
               "playmodes", "pm_midi_step", "MIDI tick: play the mapped step", kind="jmp", pad_to=8),
        Detour(0x4009dc86, stock_guard(0x4009dc86, 6, "18ec2e9a3e0b6dfa081236308c0b0d2f647926bb95014ec823e84f3a4bcd8b09"),
               "playmodes", "pm_audio_rebuild", "audio rebuild after an edit: the mapped step", kind="jmp"),
        Detour(0x4009e3dc, stock_guard(0x4009e3dc, 6, "18ec2e9a3e0b6dfa081236308c0b0d2f647926bb95014ec823e84f3a4bcd8b09"),
               "playmodes", "pm_midi_rebuild", "MIDI rebuild after an edit: the mapped step", kind="jmp"),
        Detour(0x4009b2b0, stock_guard(0x4009b2b0, 6, "6b9dc784b6d9954576ec3076f47f503b7e820106be6e2052c973c3ae96b761d0"),
               "playmodes", "pm_step_getter", "UI step(track): LEDs and live recording see the played step", kind="jmp"),
        Detour(0x4009c3da, stock_guard(0x4009c3da, 6, "9ae28e99416c05676f990666dec19a846dc0d69d6b4e4f42a479205fe6d5264d"),
               "playmodes", "pm_start_play", "PLAY: every track starts again from step 1", kind="jmp"),
        Detour(0x400a2210, stock_guard(0x400a2210, 6, "9ae28e99416c05676f990666dec19a846dc0d69d6b4e4f42a479205fe6d5264d"),
               "playmodes", "pm_start_ext1", "external start: the same", kind="jmp"),
        Detour(0x400a24d6, stock_guard(0x400a24d6, 6, "9ae28e99416c05676f990666dec19a846dc0d69d6b4e4f42a479205fe6d5264d"),
               "playmodes", "pm_start_ext2", "external start: the same", kind="jmp"),
        Detour(0x400a27e8, stock_guard(0x400a27e8, 6, "9ae28e99416c05676f990666dec19a846dc0d69d6b4e4f42a479205fe6d5264d"),
               "playmodes", "pm_start_ext3", "external start: the same", kind="jmp"),
        Detour(0x40040250, stock_guard(0x40040250, 6, "d6c9f1f60272197a17511f496fc19dd21ddd7df5f1f1ec0cab7a16cad60b0609"),
               "playmodes", "pm_track_key", "remember which TRACK key is held", kind="jmp"),
        Detour(0x400491a0, stock_guard(0x400491a0, 6, "25f6cf58764096a92fba2355b3a003ad25d064d44e1e91102deb6abcadbed9d7"),
               "playmodes", "pm_arrow_key", "TRACK held + UP/DOWN: change the play mode", kind="jmp"),
        Detour(0x400866d4, stock_guard(0x400866d4, 6, "2fd04d71c59a1c9023b7b2b5ad2cf5a7445d464be69c48340b45e33b9ad07056"),
               "playmodes", "pm_proj_begin", "project load: a storing pass starts from NORMAL", kind="jmp"),
        Detour(0x400867aa, stock_guard(0x400867aa, 6, "b304cfb26163106388891990b3c67d3fe0b7f927d6a9f973d8d93d26d1171259"),
               "playmodes", "pm_proj_line", "project load: read the #PLAY_MODES= line", kind="jmp"),
        Detour(0x400888b2, stock_guard(0x400888b2, 6, "b7d260815d4140a98163e14d6e541df300de70a7bfc32b99792b5e61e46235c3"),
               "playmodes", "pm_proj_write", "project write: the #PLAY_MODES= line", kind="jmp"),
    ),
    gates=(Gate("modules/playmodes/verify.py", remix_arg=False),),
)
