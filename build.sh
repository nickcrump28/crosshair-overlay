#!/bin/sh
# Cross-compile from Linux (MinGW-w64). On Windows with MSYS2/MinGW, the same two commands work
# with "windres" and "gcc" instead of the x86_64-w64-mingw32- prefixed names.
set -e
cd "$(dirname "$0")"
VER=$(sed -n 's/#define VER_STR "\(.*\)"/\1/p' version.h)
LIBS="-luser32 -lgdi32 -lshell32 -lwinmm -lcomctl32 -lcomdlg32 -ladvapi32 -lwinhttp -lbcrypt -lcrypt32 -lversion -lwevtapi -static-libgcc"
# pass 1: placeholder symbol table (same size as the real one, so code addresses don't move)
python3 make-symbols.py --placeholder
x86_64-w64-mingw32-windres app.rc -O coff -o app_res.o
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -mwindows $EXTRA \
  crosshair.c app_res.o -o "CrosshairOverlay.full.exe" $LIBS
# pass 2: the real table of function names, then the final (stripped) exe
python3 make-symbols.py CrosshairOverlay.full.exe
x86_64-w64-mingw32-windres app.rc -O coff -o app_res.o
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -mwindows $EXTRA \
  crosshair.c app_res.o -o "CrosshairOverlay.exe" -Wl,-Map,"CrosshairOverlay-v$VER.map" $LIBS -s
# the code must be byte-for-byte where pass 1 put it, or crash lookups would be wrong
[ "$(x86_64-w64-mingw32-objdump -h CrosshairOverlay.full.exe | awk '$2==".text"{print $3,$4}')" = \
  "$(x86_64-w64-mingw32-objdump -h CrosshairOverlay.exe | awk '$2==".text"{print $3,$4}')" ] || { echo "code moved between passes"; exit 1; }
rm -f CrosshairOverlay.full.exe
# the developer-only publisher (not for players): same source, just the Publish window,
# plus this version's source code (pushed to GitHub when you publish; webhooks removed)
python3 make-source-bundle.py
x86_64-w64-mingw32-windres -DPUBLISHER_APP app.rc -O coff -o app_pub.o
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -mwindows -DPUBLISHER_APP $EXTRA \
  crosshair.c app_pub.o -o "CrosshairPublisher.exe" \
  -luser32 -lgdi32 -lshell32 -lwinmm -lcomctl32 -lcomdlg32 -ladvapi32 -lwinhttp -lbcrypt -lcrypt32 -lversion -lwevtapi -s -static-libgcc
echo "built CrosshairOverlay.exe (v$VER) + CrosshairPublisher.exe (keep the publisher to yourself)"
