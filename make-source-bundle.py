#!/usr/bin/env python3
"""Packs the source files into source-bundle.bin (built into CrosshairPublisher.exe only).
When you publish, the publisher commits these files to the GitHub repo, so the release's
"Source code" downloads hold the real source for that version.
Secrets are removed first: any Discord webhook URL is blanked, and testers.txt / news.txt
are never included (they live only in the repo and are edited from the publisher)."""
import os, re, struct
here = os.path.dirname(os.path.abspath(__file__))
FILES = ["crosshair.c", "version.h", "update.h", "webhook.h", "blank_png.h", "app.rc", "app.manifest", "app.ico",
         "cc_bg.jpg", "drive_bg.jpg", "build.sh", "make-source-bundle.py", "make-symbols.py", "changelog.txt", "release-notes.txt",
         "README.txt", "report-relay-worker.js", "sign-windows.ps1"]
HOOK = re.compile(rb'https://(?:[a-z]+\.)?discord(?:app)?\.com/api/webhooks/[^"\s]*')
TEXT = (".c", ".h", ".rc", ".manifest", ".sh", ".py", ".txt", ".js", ".ps1", ".md")

def read(name):
    with open(os.path.join(here, name), "rb") as f:
        b = f.read()
    if name.endswith(TEXT):
        b = HOOK.sub(b"", b)  # never publish a webhook
    return b

ver = re.search(rb'#define VER_STR "([^"]+)"', read("version.h")).group(1).decode()
repo = re.search(rb'#define UPDATE_REPO L"([^"]*)"', read("update.h")).group(1).decode()
notes = [l.strip(" -*\t") for l in read("release-notes.txt").decode("utf-8-sig").splitlines() if l.strip()]
readme = f"""# Crosshair Overlay

A crosshair overlay and FPS booster for Roblox, by simplysoul.

**[Download the latest version](https://github.com/{repo}/releases/latest/download/CrosshairOverlay.exe)**
(Windows 10 / 11 — just run CrosshairOverlay.exe, it updates itself.)

## What's new in v{ver}

""" + "\n".join(f"- {n}" for n in notes) + f"""

Every version's changes: [changelog.txt](changelog.txt) • All downloads: [Releases](https://github.com/{repo}/releases)

## Building from source

Install MinGW-w64, then run `./build.sh`. It makes `CrosshairOverlay.exe` (and the developer-only publisher).
See [README.txt](README.txt) for the details of every feature.
"""
out = bytearray()
def add(name, data):
    n = name.encode()
    out.extend(struct.pack("<I", len(n)) + n + struct.pack("<I", len(data)) + data)
add("README.md", readme.encode())
for f in FILES:
    if os.path.exists(os.path.join(here, f)):
        add(f, read(f))
with open(os.path.join(here, "source-bundle.bin"), "wb") as f:
    f.write(bytes(out))
print(f"source bundle: v{ver}, {len(out)//1024} KB")
