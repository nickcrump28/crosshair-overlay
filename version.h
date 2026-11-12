#ifndef VERSION_H
#define VERSION_H
#define VER_MAJOR 1
#define VER_MINOR 17
#define VER_PATCH 0
#define VER_STR "1.17.0"
#define VER_WIDEN2(x) L##x
#define VER_WIDEN(x) VER_WIDEN2(x)
#define VER_WSTR VER_WIDEN(VER_STR)
#endif
