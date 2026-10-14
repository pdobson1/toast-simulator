#!/usr/bin/env python3
"""Fails the build if the compiled code reads or writes a word or long at an odd offset.

The 68000 can't do that (it raises an address error), and GCC 13 sometimes generates it anyway."""
import re, subprocess, sys
dis = subprocess.run(['m68k-linux-gnu-objdump', '-d', sys.argv[1]], capture_output=True, text=True).stdout
bad = []
for line in dis.splitlines():
    m = re.match(r'\s*([0-9a-f]+):\s+(?:[0-9a-f]{4} )+\s*(\S+)\s+(.*)', line)
    if not m: continue
    op, args = m.group(2), m.group(3)
    if int(m.group(1), 16) < 0x200: continue  # the vector table and header are data, not code
    if op.endswith('b') or op in ('pea', 'lea', 'jsr', 'jmp'): continue  # byte access and addresses are fine
    if re.search(r'@\(-?\d*[13579](,|\))', args): bad.append(line.strip())
if bad:
    print('Misaligned word/long accesses (address errors on a 68000):\n  ' + '\n  '.join(bad))
    sys.exit(1)
