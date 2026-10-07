#!/usr/bin/env python3
"""PLAY MODES -- local probe. Run it on YOUR machine against YOUR own original
OS 1.40C extraction; it prints a disassembly of the few sequencer and key
routines PLAY MODES needs to hook, plus where their RAM is touched.

    python3 sdk/drafts/playmodes/investigate.py \
        --os <path>/section_3_MAIN_OS.bin  [--out <file>]

It reads the image, checks it is the original 1.40C main OS (SHA-256), runs
m68k-elf-objdump over fixed address ranges and writes one text file
(default: out/playmodes-probe.txt at the repository root, git-ignored). It
writes nothing else, changes nothing, and never copies the image.

The output is a disassembly listing of Elektron code. Whether it leaves
your machine (for example to an assistant) is your decision; it must never
be committed (out/ and playmodes-probe*.txt are ignored for that reason).
"""
import argparse
import hashlib
import pathlib
import re
import shutil
import subprocess
import sys

BASE = 0x40000400
INSN = re.compile(r"^\s*[0-9a-f]+:\t")
OS_SHA256 = "164f31224bf61181e3f50e7dec40df9afcae5b16dbf6e4c0d0cc5e986af0a84e"
REPO = pathlib.Path(__file__).resolve().parents[1]   # the Octaplay repository root

# (start, stop, why) -- every range is cited in INVESTIGATION.md.
RANGES = (
    (0x40099d80, 0x4009a380, "sequencer condition / advance readers (OctaKit's "
                             "sequencer-condition / -advance sites 0x40099ddc, 0x40099ef4)"),
    (0x4009d1e8, 0x4009da20, "the per-track step handler 0x4009d1e8(track, bank, pattern, "
                             "step, n): trig masks, conditions, locks"),
    (0x400a1778, 0x400a1e10, "the sequencer tick's per-track pass (OctaKit's "
                             "sequencer-tick-000..014 sites)"),
    (0x400a1e10, 0x400a1f40, "the tick interrupt entry 0x400a1e10 and FUN_400a1eea's head"),
    (0x400a3ca4, 0x400a3ed0, "the per-track trig-timing dispatcher (Kyoti: 0x400a3ca6)"),
    (0x400a3f80, 0x400a4c20, "the master per-step handler and its common tail 0x400a4ba0"),
    (0x400a536c, 0x400a53a0, "0x400a536c(track), called by the dispatcher"),
)

# Round 2: the two callers of 0x4009d1e8 found by round 1, in full.
RANGES_2 = (
    (0x400a1f40, 0x400a3ca4, "the rest of FUN_400a1eea: the per-track playback pass that calls "
                             "0x4009d1e8 at 0x400a2d7e"),
    (0x4009da20, 0x4009e400, "the working-set rebuild 0x4009da20, which calls 0x4009d1e8 at 0x4009dc9e"),
    (0x400a5164, 0x400a5370, "0x400a5164, the trig-condition test the step handler calls"),
)
# Round 3: who shows the playhead. Every function that names one of these
# addresses is printed whole, then every call to it, with its context.
LED_WATCH = {
    0x800064d0: "the per-track step table (audio 0..7, MIDI 8..15)",
    0x800065b4: "the latched master step word (its low byte 0x800065b5 drives the trig LEDs)",
    0x800065b5: "the trig LED step byte",
    0x800065b2: "the running master step word",
}
SKIP_RANGE = (0x400a1e10, 0x400a4c20)   # the tick itself: rounds 1-2 cover it

# The key maps: 26-byte records (code, flag, six longs: press, release,
# hold handlers ...) around the REC record round 1 found at 0x400c003a.
KEYMAP_ANCHORS = (0x400c003a, 0x400c061e)
KEYMAP_RECORD = 26

# RAM the sequencer keeps per track or per step; every instruction naming
# one of these, anywhere in the image, is listed with its neighbours.
WATCH = {
    0x4009d1e8: "step handler (calls into it)",
    0x800065b2: "master step / bar word",
    0x800065b5: "the step byte the trig LEDs follow",
    0x800065b6: "master step / tick byte",
    0x800064d0: "per-track REFILL counter (Kyoti)",
    0x800064f0: "per-track sub-step tick divider (Kyoti)",
    0x80006500: "per-track running state",
    0x800065c3: "per-track trig-fire countdown (Kyoti)",
    0x800065d3: "per-track step limit (Kyoti)",
    0x800065e4: "per-track audio step / pass table",
    0x800065f4: "per-track MIDI step / pass table",
    0x80006604: "per-track audio remainder table",
    0x80006614: "per-track MIDI remainder table",
    0x46c7a6c0: "sequencer flag word (Kyoti)",
    0x46c79e9a: "tick track state (OctaKit)",
    0x46c775ce: "edit / playhead step base (Kyoti)",
}

# Key handlers whose address and key code are known (Kyoti quantize-live-rec
# -toggle: REC 0x29, PLAY 0x28; SCALE QUANTIZER: LEFT 0x34, RIGHT 0x21 read
# inside 0x40045918). Pointer tables holding them are the key maps; dumping
# them names the [TRACK] and [UP]/[DOWN] handlers.
KNOWN_HANDLERS = {0x40048774: "REC (key code 0x29)", 0x40061778: "PLAY (key code 0x28)"}


def objdump():
    tool = shutil.which("m68k-elf-objdump")
    if not tool:
        sys.exit("m68k-elf-objdump not found (it comes with the SDK's m68k-elf toolchain)")
    return tool


def disassemble(tool, image_path, start, stop):
    result = subprocess.run(
        [tool, "-D", "-b", "binary", "-m", "m68k:cfv4e", f"--adjust-vma=0x{BASE:x}",
         f"--start-address=0x{start:x}", f"--stop-address=0x{stop:x}", str(image_path)],
        capture_output=True, text=True, check=True)
    return [line for line in result.stdout.splitlines() if INSN.match(line)]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--os", type=pathlib.Path,
                    required=True,
                    help="your original 1.40C MAIN OS extraction")
    ap.add_argument("--round", type=int, default=1, choices=(1, 2, 3, 4),
                    help="2: the step handler's callers and the key maps; 3: the playhead display; "
                         "4: the project file loader / writer and how settings mark it for saving")
    ap.add_argument("--out", type=pathlib.Path, default=REPO / "out/playmodes-probe.txt")
    args = ap.parse_args()
    if not args.os.is_file():
        sys.exit(f"no OS extraction at {args.os}; pass --os <path>/section_3_MAIN_OS.bin")
    image = args.os.read_bytes()
    if hashlib.sha256(image).hexdigest() != OS_SHA256:
        sys.exit("that file is not the original OS 1.40C main OS (SHA-256 differs)")
    tool = objdump()
    out = [f"PLAY MODES probe -- original OS 1.40C main OS, base 0x{BASE:x}", ""]
    if args.round == 2:
        round_two(tool, args, image, out)
        return write(args, out)
    if args.round == 3:
        round_three(tool, args, image, out)
        return write(args, out)
    if args.round == 4:
        round_four(tool, args, image, out)
        return write(args, out)

    for start, stop, why in RANGES:
        out += [f"==== 0x{start:08x}..0x{stop:08x}  {why}"]
        out += disassemble(tool, args.os, start, stop)
        out.append("")

    out.append("==== references to the watched addresses (whole image, +-4 lines)")
    full = disassemble(tool, args.os, BASE, BASE + len(image))
    for address, why in WATCH.items():
        needle = f"0x{address:x}"
        hits = [i for i, line in enumerate(full) if needle in line.lower()]
        out.append(f"-- {needle} {why}: {len(hits)} reference(s)")
        for i in hits[:40]:
            out += full[max(0, i - 4):i + 5] + ["   ..."]
    out.append("")

    out.append("==== pointer tables holding known key handlers (key maps)")
    for handler, why in KNOWN_HANDLERS.items():
        needle = handler.to_bytes(4, "big")
        at = image.find(needle)
        while at >= 0:
            if at % 2 == 0:
                table = BASE + at
                out.append(f"-- {why} 0x{handler:08x} stored at 0x{table:08x}; 64 longs around it:")
                for k in range(-32, 32):
                    off = at + 4 * k
                    if 0 <= off <= len(image) - 4:
                        value = int.from_bytes(image[off:off + 4], "big")
                        out.append(f"   0x{BASE + off:08x} [{k:+3d}] 0x{value:08x}")
            at = image.find(needle, at + 1)
    out.append("")

    write(args, out)


def write(args, out):
    if args.round > 1 and args.out.name == "playmodes-probe.txt":
        args.out = args.out.with_name(f"playmodes-probe-{args.round}.txt")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(out) + "\n")
    print(f"wrote {args.out} ({len(out)} lines). Not for commit: it is git-ignored.")


def function_start(lines, i):
    """Walk back from line i to the instruction after the previous rts /
    jmp / bra.w (and any zero padding): the enclosing function's entry."""
    j = i
    while j > 0:
        text = lines[j - 1].split("\t")[-1].strip()
        if text.startswith(("rts", "rte")) or text.startswith(("jmp ", "braw ", "bras ")):
            break
        j -= 1
    while j < i and ".short 0x0000" in lines[j]:
        j += 1
    return j


def address_of(line):
    return int(line.split(":")[0].strip(), 16)


def round_three(tool, args, image, out):
    full = disassemble(tool, args.os, BASE, BASE + len(image))
    index = {address_of(line): k for k, line in enumerate(full)}
    functions = {}
    for address, why in LED_WATCH.items():
        needle = f"0x{address:x}"
        for i, line in enumerate(full):
            if needle in line.lower():
                start = function_start(full, i)
                functions.setdefault(address_of(full[start]), set()).add(why)
    out.append(f"==== {len(functions)} functions naming the watched addresses")
    for entry in sorted(functions):
        if SKIP_RANGE[0] <= entry < SKIP_RANGE[1]:
            out.append(f"-- 0x{entry:08x}: inside the tick handler, skipped (rounds 1-2)")
            continue
        out.append(f"-- function 0x{entry:08x} ({'; '.join(sorted(functions[entry]))})")
        k = index[entry]
        body = []
        while k < len(full) and len(body) < 220:
            body.append(full[k])
            if full[k].split("\t")[-1].strip().startswith("rts") and len(body) > 3:
                break
            k += 1
        out += body
        needle = f"0x{entry:x}"
        callers = [i for i, line in enumerate(full)
                   if needle in line.lower() and ("jsr" in line or "bsr" in line or "jmp" in line)]
        out.append(f"   callers of 0x{entry:08x}: {len(callers)}")
        for i in callers[:12]:
            out += ["   " + l for l in full[max(0, i - 8):i + 3]] + ["   ..."]
        out.append("")


# Round 4: saving the play modes in the project file. The code places come
# from SCALE QUANTIZER's project hooks (loader entry 0x400866cc, the '#'
# line check 0x400867a2, the writer line 0x400888aa) and the stock setters of
# two project-level settings (CHAIN AFTER 0x400659ec, the PERSONALIZE
# setter 0x40068ca0); the data are the "edited" flags STEP_LOCKS.md names.
PROJECT_CODE = {
    0x400866cc: "the project file loader (SCALE QUANTIZER's entry hook)",
    0x400867a2: "the loader's '#' comment-line check",
    0x400888aa: "the project file writer (SCALE QUANTIZER's line hook)",
    0x400659ec: "the CHAIN AFTER menu setter (a project setting)",
    0x40068ca0: "the QUANTIZE LIVE REC PERSONALIZE setter",
}
PROJECT_DATA = {
    0x100f8598: "the 'edited' flag a lock edit sets",
    0x40027e00: "the routine a lock edit calls after storing",
}


RANGES_4 = (
    (0x400866c4, 0x40086900, "the loader's head and its line loop up to the '#' check"),
    (0x40088200, 0x40088240, "the loader's next-line point 0x40088224"),
    (0x40088840, 0x40088940, "the writer's lines around 0x400888aa"),
)


def round_four(tool, args, image, out):
    for start, stop, why in RANGES_4:
        out.append(f"==== 0x{start:08x}..0x{stop:08x}  {why}")
        out += disassemble(tool, args.os, start, stop)
        out.append("")
    full = disassemble(tool, args.os, BASE, BASE + len(image))
    index = {address_of(line): k for k, line in enumerate(full)}

    def function_of(i, limit=400):
        start = function_start(full, i)
        body, k = [], start
        while k < len(full) and len(body) < limit:
            body.append(full[k])
            if full[k].split("\t")[-1].strip().startswith("rts") and k > i:
                break
            k += 1
        return start, body

    def callers(entry, limit=16):
        needle = f"0x{entry:x}"
        found = [i for i, line in enumerate(full)
                 if needle in line.lower() and ("jsr" in line or "bsr" in line or "jmp" in line or "pea" in line)]
        rows = []
        for i in found[:limit]:
            rows += ["   " + l for l in full[max(0, i - 10):i + 3]] + ["   ..."]
        return found, rows

    for address, why in PROJECT_CODE.items():
        i = index.get(address)
        if i is None:
            out.append(f"==== 0x{address:08x} {why}: not an instruction boundary")
            continue
        start, body = function_of(i)
        entry = address_of(full[start])
        out.append(f"==== 0x{address:08x} {why}: in the function at 0x{entry:08x}")
        out += body
        found, rows = callers(entry)
        out.append(f"   callers of 0x{entry:08x}: {len(found)}")
        out += rows
        out.append("")

    for address, why in PROJECT_DATA.items():
        needle = f"0x{address:x}"
        hits = [i for i, line in enumerate(full) if needle in line.lower()]
        out.append(f"==== 0x{address:08x} {why}: {len(hits)} reference(s)")
        for i in hits[:30]:
            out += full[max(0, i - 6):i + 4] + ["   ..."]
        out.append("")


def is_code(value, image):
    return BASE <= value < BASE + len(image) and value % 2 == 0


def keymap(image, anchor):
    """Walk the 26-byte key records both ways from a known one."""
    def record(at):
        off = at - BASE
        if off < 0 or off + KEYMAP_RECORD > len(image):
            return None
        code, flag = image[off], image[off + 1]
        longs = [int.from_bytes(image[off + 2 + 4 * i:off + 6 + 4 * i], "big") for i in range(6)]
        handlers = longs[:3]
        if (code > 0x7f or not (code or any(handlers))
                or not all(h == 0 or is_code(h, image) for h in handlers)):
            return None
        return code, flag, longs
    start = anchor
    while record(start - KEYMAP_RECORD) and anchor - start < 128 * KEYMAP_RECORD:
        start -= KEYMAP_RECORD
    rows, at = [], start
    while (r := record(at)) and len(rows) < 256:
        rows.append((at, *r))
        at += KEYMAP_RECORD
    return rows


def round_two(tool, args, image, out):
    for start, stop, why in RANGES_2:
        out.append(f"==== 0x{start:08x}..0x{stop:08x}  {why}")
        out += disassemble(tool, args.os, start, stop)
        out.append("")
    handlers = {}
    for anchor in KEYMAP_ANCHORS:
        rows = keymap(image, anchor)
        out.append(f"==== key map around 0x{anchor:08x}: {len(rows)} records "
                   f"(code, flag, press, release, hold, l3, l4, l5)")
        for at, code, flag, longs in rows:
            out.append(f"   0x{at:08x} code 0x{code:02x} flag 0x{flag:02x} "
                       + " ".join(f"0x{v:08x}" for v in longs))
            for h in longs[:3]:
                if h:
                    handlers.setdefault(h, set()).add(code)
        out.append("")
    out.append("==== the first instructions of every key handler")
    for h in sorted(handlers):
        codes = ", ".join(f"0x{c:02x}" for c in sorted(handlers[h]))
        out.append(f"-- 0x{h:08x} (key codes {codes})")
        out += disassemble(tool, args.os, h, min(h + 0x90, BASE + len(image)))
    out.append("")


if __name__ == "__main__":
    main()
