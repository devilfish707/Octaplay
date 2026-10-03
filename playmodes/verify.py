#!/usr/bin/env python3
"""PLAY MODES gate. Reads no firmware and runs no emulator.

1. Compiles the engine and the adapter for the host and runs their tests
   (test_playmode.c, test_adapter.c): every mode's sequence, shuffle being a
   permutation every pass, random's spread, look-ahead == what then plays,
   per-track vs shared modes, the popup text, PLAY / pattern-switch resets.
2. With m68k-elf-gcc on the PATH, compiles the ColdFire unit and refuses
   any call outside it (generate.py), and checks playmodes.s is current.

What it cannot show: the detours running in the firmware (port or unit).
"""
import pathlib
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
CC = shutil.which('cc') or shutil.which('clang') or shutil.which('gcc')
FLAGS = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-O2']


def host(name, sources, defines=()):
    with tempfile.TemporaryDirectory() as td:
        exe = pathlib.Path(td) / name
        subprocess.run([CC, *FLAGS, *defines, '-o', str(exe), *sources], cwd=HERE, check=True)
        result = subprocess.run([str(exe)], capture_output=True, text=True)
        print(result.stdout.strip())
        if result.returncode:
            sys.exit(f'[FAIL] {name}')


if __name__ == '__main__':
    if not CC:
        sys.exit('[FAIL] no host C compiler')
    host('engine', ['playmode.c', 'test_playmode.c'])
    host('adapter', ['playmode.c', 'adapter.c', 'test_adapter.c'], ['-DPM_HOST'])
    if shutil.which('m68k-elf-gcc'):
        mode = '--check' if (HERE / 'playmodes.s').exists() else '--dry'
        if subprocess.run([sys.executable, 'generate.py', mode], cwd=HERE).returncode:
            sys.exit('[FAIL] ColdFire unit (see the message above)')
        if mode == '--dry':
            print('[NOTE] playmodes.s not generated yet: run python3 generate.py')
    else:
        print('[SKIP] m68k-elf-gcc not on PATH: ColdFire compile not checked')
    print('[PASS] PLAY MODES ' + ('engine, glue and ColdFire unit' if shutil.which('m68k-elf-gcc') else 'engine and glue (host only)') + ' -- hardware untested')
