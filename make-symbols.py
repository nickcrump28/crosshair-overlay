#!/usr/bin/env python3
"""Builds symbols.bin: the app's function names and where they start, built into the exe.
When the app crashes it looks up the crash address here, so the report says *where* it
crashed in plain words (e.g. "while drawing the settings window (ui_paint)") instead of a number.
The file always has the same size, so adding it doesn't move any code (build.sh links twice).
  make-symbols.py --placeholder      write an empty table
  make-symbols.py <unstripped.exe>   write the real table"""
import os, subprocess, sys
SIZE = 128 * 1024
here = os.path.dirname(os.path.abspath(__file__))
out = os.path.join(here, "symbols.bin")
body = b""
if len(sys.argv) > 1 and sys.argv[1] != "--placeholder":
    nm = subprocess.run(["x86_64-w64-mingw32-nm", "--defined-only", sys.argv[1]], capture_output=True, text=True, check=True).stdout
    syms = {}
    for line in nm.splitlines():
        p = line.split()
        if len(p) == 3 and p[1] in "tT" and not p[2].startswith("."):
            rva = int(p[0], 16) - 0x140000000
            if rva > 0:
                syms.setdefault(rva, p[2])
    lines = [f"{r:x} {n}\n" for r, n in sorted(syms.items())]
    body = "".join(lines).encode()
    if len(body) > SIZE:
        print("symbols.bin: too many symbols, keeping the app's own")
        lines = [l for l in lines if not l.split()[1].startswith(("_", "mingw", "__"))]
        body = "".join(lines).encode()[:SIZE]
with open(out, "wb") as f:
    f.write(body + b"\n" * (SIZE - len(body)))
print(f"symbols.bin: {body.count(b'\n')} functions")
