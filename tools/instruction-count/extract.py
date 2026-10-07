#!/usr/bin/env python3
"""Extract the Play Modes unit's linked code and symbols from an octabam
build's runtime ELF (out/platform/runtime/runtime.elf: this module's code
only, no firmware) into blob.txt and syms.h for harness.cpp / mapcost.cpp."""
import struct
import sys

d = open(sys.argv[1], 'rb').read()
e_shoff = struct.unpack('>I', d[0x20:0x24])[0]
shentsize, shnum, shstrndx = struct.unpack('>HHH', d[0x2e:0x34])
secs = [struct.unpack('>IIIIIIIIII', d[e_shoff + i * shentsize:e_shoff + i * shentsize + 40]) for i in range(shnum)]


def name(table, off):
    s = d[secs[table][4] + off:]
    return s[:s.index(b'\0')].decode()


with open('blob.txt', 'w') as out:
    for s in secs:
        if name(shstrndx, s[0]) in ('.text', '.rodata', '.data') and s[5] and s[3]:
            out.write(f'{s[3]:x} {d[s[4]:s[4] + s[5]].hex()}\n')
symtab = [s for s in secs if s[1] == 2][0]
syms = {}
for i in range(symtab[5] // 16):
    n, v, *_ = struct.unpack('>IIIBBH', d[symtab[4] + i * 16:symtab[4] + i * 16 + 16])
    syms[name(secs.index(secs[symtab[6]]), n)] = v
with open('syms.h', 'w') as f:
    for k in ('pm_step_entry', 'pm_state', 'pm_ready', 'pm_cur', 'pm_table', 'pm_restart', 'pm_show_entry',
              'pm_peek_entry', 'pm_key_entry', 'pm_proj_line', 'pm_memcpy', 'pm_key_updown', 'pm_project_line',
              'pm_project_begin', 'pm_project_format', 'pm_pattern_copy', 'pm_pattern_clear', 'pm_line', 'pm_map'):
        f.write(f'#define S_{k.upper()} 0x{syms[k]:x}u\n')
print('wrote blob.txt, syms.h')
