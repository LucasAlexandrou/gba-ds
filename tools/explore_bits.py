#!/usr/bin/env python3
"""Find bytes whose bits only ever get set across an ordered series of RAM dumps."""
import sys
ds = [open(f, "rb").read() for f in sys.argv[1:]]
def addr(i): return 0x02000000 + i if i < 0x40000 else 0x03000000 + i - 0x40000
out = []
for i in range(len(ds[0])):
    vals = [d[i] for d in ds]
    if len(set(vals)) < 2: continue
    ok = all((vals[k] & vals[k+1]) == vals[k] for k in range(len(vals)-1))
    if ok:
        out.append((i, vals))
for i, v in out: print(f"{addr(i):08X} " + " ".join(f"{x:02X}" for x in v))
print(len(out), "monotonic bytes", file=sys.stderr)
