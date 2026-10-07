# Instruction counts for Play Modes

Counts the instructions the module's own code executes, in octabam's
ColdFire emulator core (`tools/emu/ot_emu`'s `Machine`: Musashi plus the V4e
layer), with **no firmware**: the machine is empty, the unit's linked code is
loaded at its build address, and the firmware bytes it reads (sequencer
state, pattern records) are set to synthetic values. Each entry is called
with a return address the loop stops at.

```sh
# 1. this module's linked code from an octabam/octamod build (no firmware in it)
python3 extract.py <octabam>/out/platform/runtime/runtime.elf
# 2. ot_machine from a modwerk / octabam checkout (vendor/mc68k, vendor/dsp56300 fetched)
cmake -B /tmp/emu -S <octabam>/tools/emu/ot_emu -DCMAKE_BUILD_TYPE=Release -DOT_LTO=OFF
cmake --build /tmp/emu --target ot_machine -j8
# 3. build and run
E=<octabam>/tools/emu/ot_emu V=<octabam>/vendor
g++ -std=c++17 -O2 -I$E -I$V -I$V/dsp56300/source -I. harness.cpp \
    /tmp/emu/libot_machine.a $(find /tmp/emu -name '*.a' ! -name libot_machine.a) \
    $(find /tmp/emu -name '*.a' ! -name libot_machine.a) -lpthread -o harness
./harness 2>/dev/null | grep -v @
# SHUFFLE's walk: cost per walk length (walk.py finds seeds with a given walk)
g++ ... mapcost.cpp ... -o mapcost && ./mapcost 0 2 1  0 1 2  125058 28 24
```

Results for build 19 (Octaplay `1d2e9dc`): see the module's
`evidence/cycles.md` in the modwerk pull request.
