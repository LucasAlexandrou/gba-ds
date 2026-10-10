#!/usr/bin/env python3
"""Find bytes that distinguish 'map open' dumps from 'gameplay' dumps.
ramdiff.py --on a.ram b.ram ... --off c.ram d.ram ..."""
import sys
args = sys.argv[1:]
on, off, cur = [], [], None
for a in args:
    if a == "--on": cur = on
    elif a == "--off": cur = off
    else: cur.append(open(a, "rb").read())
def addr(i): return 0x02000000 + i if i < 0x40000 else 0x03000000 + i - 0x40000
n = len(on[0]); res = []
for i in range(n):
    vo = {d[i] for d in on}; vf = {d[i] for d in off}
    if len(vo) == 1 and not (vo & vf):
        res.append((i, vo.pop(), sorted(vf)))
for i, v, f in res:
    print(f"{addr(i):08X} on={v:02X} off={[hex(x) for x in f]}")
print(len(res), "candidates")
