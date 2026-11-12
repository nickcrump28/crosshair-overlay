/*
 * Crosshair Overlay for Windows
 * A click-through, always-on-top crosshair that follows the mouse
 * (or locks to the center of the active window).
 *
 * Made by simplysoul.
 *
 * Default hotkeys (change them in the tray menu -> Keybinds...):
 *   F6 / Shift+F6     next / previous crosshair
 *   F7 / Shift+F7     next / previous color
 *   Ctrl+F6 / Ctrl+F7 smaller / bigger
 *   F8                show / hide
 *   F9                follow mouse <-> lock to window center
 *   F10               hide / show the mouse cursor
 *   Ctrl+Shift+F8     exit
 * Ctrl+F2 (or clicking the tray icon) opens the settings window.
 *
 * v1.10.0: FPS Boost tab - Roblox FPS cap (its own Maximum Frame Rate setting), allowlisted
 * performance fast flags and the high-performance GPU choice.
 *
 * v1.9.0: themes (Police, Criminal, Special Ops, Intel, Chosen Company, Midnight Run, Red Ops, Tactical, Retro Arcade, Vaporwave,
 * Cyberpunk, Custom) for the settings window, keybinds card and loading screen,
 * plus a new animated loading screen with a preview button on the Themes tab,
 * and new crosshair shapes including roleplay sights (look-alike shapes are hidden).
 */
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <math.h>
#include <stdint.h>
#include <wchar.h>
#include <string.h>
#include <commctrl.h>
#include <commdlg.h>
#include <windowsx.h>
#include <objbase.h>
#include <stdlib.h>
#include <stdio.h>
#include <tlhelp32.h>
#include <winhttp.h>
#include <stdarg.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <winevt.h>
#include <wctype.h>
#include <ctype.h>
#include "version.h"
#ifdef PUBLISHER_APP
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
#include "webhook.h"
#include "update.h"
#define APP_NAME L"Crosshair Overlay v" VER_WSTR

#define WSZ_MAX 512          /* largest overlay window ever needed */
static volatile LONG g_wsz = 64; /* overlay is sized to fit the crosshair */
static int g_nudge_x, g_nudge_y; /* fine-tune: move the crosshair a few pixels (to match a game's own aim point) */
#define WM_TRAY (WM_APP + 1)
#define WM_RBX_CHANGED (WM_APP + 2)
#define WM_REPORT_DONE (WM_APP + 3)
#define WM_NEWS (WM_APP + 9)
#define WM_TESTER_OK (WM_APP + 10)
#define WM_SAVE_LATER (WM_APP + 11) /* save settings on the main thread */
/* Announcement from the developer (news.txt in the repo). */
static wchar_t g_news[260];
static unsigned g_news_hash, g_news_seen, g_news_dismissed;
static int g_want_recent; /* recent codes still to be read from settings.ini */
#define WM_UPDATE_DONE (WM_APP + 4)
#define WM_PUBLISH_STEP (WM_APP + 5)

/* ---------------- options ---------------- */
static const wchar_t *STYLE_NAMES[] = {
    L"Dot", L"Plus", L"Gap Plus", L"Gap Plus + Dot", L"Circle",
    L"Circle + Dot", L"Scope", L"T-Shape", L"X", L"Chevron",
    L"Square", L"Square + Dot", L"Diamond", L"Triangle", L"Brackets",
    L"Double Circle", L"Corners", L"Hexagon", L"Solid Circle",
    /* added in v1.9 */
    L"X + Dot", L"Reticle", L"Mil-Dot", L"Split Circle", L"Dot Ring", L"Tri",
    L"Star", L"Pentagon", L"Octagon", L"Line", L"Plus + Corners",
    L"Taser", L"Shotgun Spread", L"Holographic", L"Cross-Dot"};
#define NSTYLES 34
/* Shapes that looked the same as another one are hidden from the menu. Settings,
   profiles and keybinds that used them switch to the look-alike instead. */
static int style_alias(int s) {
    switch (s) {
    case 18: return 0;   /* Solid Circle -> Dot (same filled circle, use the Size slider) */
    case 20: return 32;  /* Reticle -> Holographic */
    case 21: return 1;   /* Mil-Dot -> Plus */
    case 27: return 17;  /* Octagon -> Hexagon */
    case 30: return 5;   /* Taser -> Circle + Dot */
    }
    return -1;
}
static int style_shown(int s) { return style_alias(s) < 0; }
static int style_fix(int s) {
    if (s < 0 || s >= NSTYLES) s = 0;
    return style_shown(s) ? s : style_alias(s);
}
static int style_step(int s, int dir) { /* next / previous shape in the menu */
    do s = (s + dir + NSTYLES) % NSTYLES; while (!style_shown(s));
    return s;
}

/* The last entry is the user's own custom color (picked in the settings menu). */
static struct { const wchar_t *name; BYTE r, g, b; } COLORS[] = {
    {L"Red", 255, 0, 0},        {L"Green", 0, 255, 0},       {L"Cyan", 0, 255, 255},
    {L"Yellow", 255, 255, 0},   {L"Pink", 255, 40, 200},     {L"White", 255, 255, 255},
    {L"Orange", 255, 140, 0},   {L"Blue", 40, 120, 255},     {L"Purple", 170, 60, 255},
    {L"Lime", 170, 255, 0},     {L"Magenta", 255, 0, 255},   {L"Sky Blue", 90, 190, 255},
    {L"Teal", 0, 200, 170},     {L"Gold", 255, 200, 0},      {L"Coral", 255, 110, 90},
    {L"Mint", 120, 255, 190},   {L"Lavender", 190, 160, 255}, {L"Black", 0, 0, 0},
    {L"Custom", 255, 0, 128}};
#define NCOLORS 19
#define CUSTOM_COLOR (NCOLORS - 1)

static const int SIZES[] = {4, 6, 8, 10, 12, 16, 20, 26, 34, 44, 56};
#define NSIZES 11
static const int OPACITIES[] = {100, 85, 70, 50, 30};
#define NOPAC 5
/* Cover patch: a solid circle behind the crosshair that blocks the game's own crosshair */
static const int COVER_RADII[] = {0, 10, 16, 24, 34};
#define NCOVER 5
static const struct { const wchar_t *name; BYTE r, g, b; } COVER_COLORS[] = {
    {L"Black", 0, 0, 0}, {L"Dark gray", 40, 42, 48}, {L"Gray", 110, 112, 118}, {L"White", 245, 245, 245}};
#define NCOVERCOL 4
static int g_cover_last = 2; /* size used when toggling the cover back on */

typedef struct {
    int style, color, size, thick, outline, center, hidden, opacity, hidecur, splash, cover, cover_color, open_start,
        glow, anim, anim_speed, click_fx, theme,
        l2_style, l2_color, l2_size, /* second layer drawn on top (l2_style -1 = none) */
        auto_contrast, menu_hide;   /* switch color when it blends in; hide in Roblox's menu / chat */
} Settings;
static Settings S = {2, 0, 3, 2, 1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, -1, 5, 1, 0, 1}; /* size/opacity are indexes; theme 0 = Patrol */
static COLORREF g_cust_accent, g_cust_bg; /* the Custom theme's two colors (defaults set in load_settings) */
static int g_crim_intro; /* Criminal loading screen: 0 = climb the crew ranks, 1 = wanted level */
static wchar_t g_music[MAX_PATH]; /* loading music: a song file on this PC (empty = off) */
static int g_music_start;         /* where in the song to start, in seconds */
static int g_fps;    /* Roblox FPS cap: index into FPS_CAPS (0 = leave Roblox's setting alone) */
static int g_boost;  /* Roblox performance boost: 0 off, 1 balanced, 2 max FPS */
static volatile LONG g_is_tester; /* this PC is on the Q/A tester list */
static int g_first_run_ini; /* no settings.ini yet: a brand-new install */
static int g_last_tab;
static int g_stats;  /* opt-in anonymous usage stats (off until the player turns it on) */
static int g_tbuilds = 1; /* testers: get test builds (they can pause it) */
static wchar_t g_tester_ok[24]; /* testers: the version they already said "looks good" for */
static int g_gpu;    /* ask Windows to run Roblox on the high-performance GPU */
static int g_autoupd = 1; /* install updates automatically */
static int g_ui_size = 1;  /* settings window size: index into UI_SIZES (75/85/100/115 %), saved as panel_size */
static int g_autostarted;  /* started by Windows at sign-in (--autostart) */
static wchar_t g_skip_ver[32]; /* settings: a version that was rolled back on this PC (not installed again) */
static int g_nocap = 1;   /* keep the crosshair out of recordings / streams / screenshots */
static int g_onlyrbx;     /* show the crosshair only while Roblox is the window in front */
static int g_focus_hidden; /* hidden right now because Roblox isn't in front */
static int g_scene_hidden; /* hidden right now because Roblox's menu or chat is open */
static int g_ac_alt;       /* auto-contrast: 0 = your color, 1 = white, 2 = black (it would blend in) */
static int g_reports; /* bug reports: 0 = not asked yet, 1 = send automatically, 2 = don't send */
static wchar_t g_report_id[16]; /* random id so reports from one PC can be grouped */
#define NFPS 7

/* ---------------- globals ---------------- */
static HWND g_overlay, g_host;
static HWND g_rdlg; /* "Report a problem" window, when open */
static HWND g_pdlg; /* "Publish a release" window, when open */
static HWND g_wndlg; /* "What's new" window, when open */
static NOTIFYICONDATAW g_nid;
static HICON g_icon;
static volatile LONG g_running = 1;
static volatile LONG g_force_move = 1;
static wchar_t g_ini[MAX_PATH];
static int g_menu_open;
static HWND g_keydlg; /* keybinds window, when open */
static void card_refresh(void);

/* ---------------- start with Windows ---------------- */
/* An entry in HKCU\...\Run that starts the app (quietly: no intro, no panel) when you sign in. */
#define RUN_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
static int autostart_on(void) {
    HKEY k;
    int on = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_QUERY_VALUE, &k) == ERROR_SUCCESS) {
        on = RegQueryValueExW(k, L"CrosshairOverlay", NULL, NULL, NULL, NULL) == ERROR_SUCCESS;
        RegCloseKey(k);
    }
    return on;
}
static int autostart_set(int on) {
    HKEY k;
    int ok = 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS) return 0;
    if (on) {
        wchar_t exe[MAX_PATH], cmd[MAX_PATH + 32];
        DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);
        if (n && n < MAX_PATH) {
            wsprintfW(cmd, L"\"%s\" --autostart", exe);
            ok = RegSetValueExW(k, L"CrosshairOverlay", 0, REG_SZ, (const BYTE *)cmd, (DWORD)((wcslen(cmd) + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
        }
    } else ok = RegDeleteValueW(k, L"CrosshairOverlay") == ERROR_SUCCESS || !autostart_on();
    RegCloseKey(k);
    return ok;
}
static int has_arg(const wchar_t *a) {
    int argc = 0, f = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; i++) if (!wcscmp(argv[i], a)) f = 1;
    if (argv) LocalFree(argv);
    return f;
}

/* ---------------- hide from recordings ---------------- */
/* Windows 10 2004+ lets a window opt out of screen capture (WDA_EXCLUDEFROMCAPTURE): OBS,
   SteelSeries Moments, NVIDIA/AMD recording, Discord streams and screenshots leave it out,
   but it still shows on your screen. Older Windows would show a black box instead, so there
   it's simply not used. */
static HWND g_card;
static int capture_hide_apply(void) {
    typedef BOOL(WINAPI * SetAffinityFn)(HWND, DWORD);
    static SetAffinityFn f;
    static int looked;
    if (!looked) { looked = 1; f = (SetAffinityFn)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowDisplayAffinity"); }
    if (!f) return 0;
    DWORD a = g_nocap ? 0x11 /* WDA_EXCLUDEFROMCAPTURE */ : 0 /* WDA_NONE */;
    int ok = 1;
    if (g_overlay && !f(g_overlay, a)) ok = 0;
    if (g_card) f(g_card, a);
    return ok;
}

/* ---------------- settings file ---------------- */
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void settings_path(void) {
    wchar_t dir[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH - 40) { g_ini[0] = 0; return; }
    wcscat(dir, L"\\CrosshairOverlay");
    CreateDirectoryW(dir, NULL);
    wcscpy(g_ini, dir);
    wcscat(g_ini, L"\\settings.ini");
}

static int ini_get(const wchar_t *k, int def) {
    return g_ini[0] ? (int)GetPrivateProfileIntW(L"crosshair", k, def, g_ini) : def;
}
static void ini_set(const wchar_t *k, int v) {
    wchar_t b[16];
    if (!g_ini[0]) return;
    wsprintfW(b, L"%d", v);
    WritePrivateProfileStringW(L"crosshair", k, b, g_ini);
}

static void load_profiles(void);
static void load_settings(void) {
        g_first_run_ini = g_ini[0] && GetFileAttributesW(g_ini) == INVALID_FILE_ATTRIBUTES;
    S.style   = style_fix(ini_get(L"style", S.style));
    S.color   = clampi(ini_get(L"color", S.color), 0, NCOLORS - 1);
    S.size    = clampi(ini_get(L"size", S.size), 0, NSIZES - 1);
    S.thick   = clampi(ini_get(L"thickness", S.thick), 1, 6);
    S.outline = ini_get(L"outline", S.outline) ? 1 : 0;
    S.center  = ini_get(L"center", S.center) ? 1 : 0;
    S.opacity = clampi(ini_get(L"opacity", S.opacity), 0, NOPAC - 1);
    S.hidecur = ini_get(L"hide_cursor", S.hidecur) ? 1 : 0;
    S.splash  = ini_get(L"loading_screen", S.splash) ? 1 : 0;
    S.open_start = ini_get(L"open_on_start", S.open_start) ? 1 : 0;
    S.glow = ini_get(L"glow", S.glow) ? 1 : 0;
    S.anim = clampi(ini_get(L"effect", S.anim), 0, 2);
    S.anim_speed = clampi(ini_get(L"effect_speed", S.anim_speed), 0, 2);
    S.click_fx = ini_get(L"click_reaction", S.click_fx) ? 1 : 0;
    S.l2_style = ini_get(L"layer2_style", S.l2_style);
    if (S.l2_style >= 0) S.l2_style = style_fix(S.l2_style);
    S.l2_color = clampi(ini_get(L"layer2_color", S.l2_color), 0, NCOLORS - 1);
    S.l2_size = clampi(ini_get(L"layer2_size", S.l2_size), 0, NSIZES - 1);
    S.auto_contrast = ini_get(L"auto_contrast", S.auto_contrast) ? 1 : 0;
    S.menu_hide = ini_get(L"hide_in_menus", S.menu_hide) ? 1 : 0;
    load_profiles();
    S.cover   = clampi(ini_get(L"cover", S.cover), 0, NCOVER - 1);
    S.cover_color = clampi(ini_get(L"cover_color", S.cover_color), 0, NCOVERCOL - 1);
    {
        int c = ini_get(L"custom_color", -1);
        if (c >= 0) { COLORS[CUSTOM_COLOR].r = (BYTE)(c >> 16); COLORS[CUSTOM_COLOR].g = (BYTE)(c >> 8); COLORS[CUSTOM_COLOR].b = (BYTE)c; }
    }
    g_cover_last = clampi(ini_get(L"cover_last", g_cover_last), 1, NCOVER - 1);
    S.theme = clampi(ini_get(L"theme", S.theme), 0, 16 /* highest theme number */);
    g_crim_intro = ini_get(L"criminal_intro", 0) ? 1 : 0;
    g_music_start = clampi(ini_get(L"music_start", 0), 0, 3600);
    if (g_ini[0]) GetPrivateProfileStringW(L"crosshair", L"music_path", L"", g_music, MAX_PATH, g_ini);
    g_fps = clampi(ini_get(L"roblox_fps_cap", g_fps), 0, NFPS - 1);
    g_boost = clampi(ini_get(L"roblox_boost", g_boost), 0, 2);
    g_stats = ini_get(L"usage_stats", 0) ? 1 : 0;
    g_tbuilds = ini_get(L"tester_builds", 1) ? 1 : 0;
    g_last_tab = clampi(ini_get(L"last_tab", 0), 0, 9);
    g_nudge_x = clampi(ini_get(L"nudge_x", 0), -10, 10);
    g_nudge_y = clampi(ini_get(L"nudge_y", 0), -10, 10);
    if (ini_get(L"nudge_v", 0) < 3) { g_nudge_x = g_nudge_y = 0; ini_set(L"nudge_v", 3); } /* 1.16.3: back to the 1.15.1 position, start fresh */
    g_is_tester = ini_get(L"was_tester", 0) ? 1 : 0; /* until the update check confirms it */
    if (g_ini[0]) GetPrivateProfileStringW(L"crosshair", L"tester_ok", L"", g_tester_ok, 24, g_ini);
    g_gpu = ini_get(L"roblox_gpu", g_gpu) ? 1 : 0;
    g_reports = clampi(ini_get(L"bug_reports", g_reports), 0, 2);
    g_autoupd = ini_get(L"auto_update", g_autoupd) ? 1 : 0;
    g_ui_size = clampi(ini_get(L"panel_size", g_ui_size), 0, 3);
    if (g_ini[0]) GetPrivateProfileStringW(L"crosshair", L"skip_update", L"", g_skip_ver, 32, g_ini);
    g_nocap = ini_get(L"hide_from_recordings", g_nocap) ? 1 : 0;
    g_onlyrbx = ini_get(L"only_in_roblox", g_onlyrbx) ? 1 : 0;
    g_news_seen = (unsigned)ini_get(L"news_seen", 0);
    g_want_recent = 1;
    g_news_dismissed = (unsigned)ini_get(L"news_dismissed", 0);
    if (g_ini[0]) GetPrivateProfileStringW(L"crosshair", L"report_id", L"", g_report_id, 16, g_ini);
    if (!g_report_id[0]) {
        LARGE_INTEGER q;
        QueryPerformanceCounter(&q);
        wsprintfW(g_report_id, L"%08lx", (unsigned long)(q.LowPart ^ (GetCurrentProcessId() << 16) ^ GetTickCount() * 2654435761u));
        if (g_ini[0]) WritePrivateProfileStringW(L"crosshair", L"report_id", g_report_id, g_ini);
    }
    {
        int a = ini_get(L"theme_accent", -1), b = ini_get(L"theme_bg", -1);
        g_cust_accent = a >= 0 ? (COLORREF)(a & 0xFFFFFF) : RGB(0, 200, 170);
        g_cust_bg = b >= 0 ? (COLORREF)(b & 0xFFFFFF) : RGB(14, 17, 23);
    }
}

static void save_settings(void) {
    ini_set(L"style", S.style);     ini_set(L"color", S.color);
    ini_set(L"size", S.size);       ini_set(L"thickness", S.thick);
    ini_set(L"outline", S.outline); ini_set(L"center", S.center);
    ini_set(L"opacity", S.opacity); ini_set(L"hide_cursor", S.hidecur);
    ini_set(L"loading_screen", S.splash);
    ini_set(L"open_on_start", S.open_start);
    ini_set(L"glow", S.glow); ini_set(L"effect", S.anim);
    ini_set(L"effect_speed", S.anim_speed); ini_set(L"click_reaction", S.click_fx);
    ini_set(L"layer2_style", S.l2_style); ini_set(L"layer2_color", S.l2_color); ini_set(L"layer2_size", S.l2_size);
    ini_set(L"auto_contrast", S.auto_contrast); ini_set(L"hide_in_menus", S.menu_hide);
    ini_set(L"cover", S.cover); ini_set(L"cover_color", S.cover_color);
    ini_set(L"cover_last", g_cover_last);
    ini_set(L"custom_color", (COLORS[CUSTOM_COLOR].r << 16) | (COLORS[CUSTOM_COLOR].g << 8) | COLORS[CUSTOM_COLOR].b);
    ini_set(L"theme", S.theme);
    ini_set(L"criminal_intro", g_crim_intro);
    ini_set(L"music_start", g_music_start);
    if (g_ini[0]) WritePrivateProfileStringW(L"crosshair", L"music_path", g_music, g_ini);
    ini_set(L"roblox_fps_cap", g_fps); ini_set(L"roblox_boost", g_boost); ini_set(L"roblox_gpu", g_gpu);
    ini_set(L"nudge_x", g_nudge_x); ini_set(L"nudge_y", g_nudge_y);
    ini_set(L"usage_stats", g_stats); ini_set(L"tester_builds", g_tbuilds); ini_set(L"was_tester", g_is_tester ? 1 : 0);
    if (g_ini[0]) WritePrivateProfileStringW(L"crosshair", L"tester_ok", g_tester_ok, g_ini);
    ini_set(L"bug_reports", g_reports);
    ini_set(L"auto_update", g_autoupd);
    ini_set(L"panel_size", g_ui_size);
    ini_set(L"hide_from_recordings", g_nocap);
    ini_set(L"only_in_roblox", g_onlyrbx);
    ini_set(L"news_seen", (int)g_news_seen);
    ini_set(L"news_dismissed", (int)g_news_dismissed);
    ini_set(L"theme_accent", (int)g_cust_accent);
    ini_set(L"theme_bg", (int)g_cust_bg);
}


/* ---------------- bug log + reports ---------------- */
/* The app keeps a small log in %APPDATA%\CrosshairOverlay\log.txt (start/exit, errors,
   crashes). When it crashes or hits an error, a report is sent to the developer's
   Discord webhook (webhook.h) on the next start - only if the user said yes. The
   report holds the app version, Windows version, screen, settings.ini and the log,
   with the Windows user folder replaced by %USERPROFILE%. Nothing else is read. */
static wchar_t g_logp[MAX_PATH], g_pendp[MAX_PATH];
static int g_session_err;
static const wchar_t *g_report_extra; /* extra section for the next report (Roblox crash details) */
static int g_balloon_report; /* clicking the tray balloon opens "Report a problem" */
static int g_balloon_update; /* ...or installs the downloaded update */
static DWORD g_launch_tick; /* when the app started */
static const wchar_t *g_exit_why; /* how the app was closed (for the log) */
static DWORD g_last_manual;
static int roblox_running(void);
static void crumb(const wchar_t *fmt, ...); /* "what the player did" trail for crash reports */
static wchar_t g_report_summary[1400]; /* the plain-words summary at the top of report.txt */
static int refresh_rate(void);
static int file_exists(const wchar_t *p);
static char *read_small(const wchar_t *p, DWORD *len);
static int write_small(const wchar_t *p, const char *data, DWORD n);

static const wchar_t *report_target(void) { /* relay if set, else Discord */
#ifdef CH_TEST
    static wchar_t t[400];
    if (GetEnvironmentVariableW(L"CH_REPORT_HOOK", t, 400)) return t;
#endif
    return REPORT_RELAY_URL[0] ? REPORT_RELAY_URL : BUG_WEBHOOK_URL;
}
static int webhook_set(void) { return report_target()[0] != 0; }

static void log_paths(void) {
    wchar_t *sl;
    if (!g_ini[0]) return;
    wcscpy(g_logp, g_ini);
    if ((sl = wcsrchr(g_logp, L'\\'))) sl[1] = 0;
    wcscpy(g_pendp, g_logp);
    wcscat(g_logp, L"log.txt");
    wcscat(g_pendp, L"report_pending.txt");
}

static void append_utf8(const wchar_t *path, const wchar_t *text) {
    char buf[4096];
    int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, buf, sizeof(buf), NULL, NULL);
    if (n <= 1) return;
    HANDLE h = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w;
    WriteFile(h, buf, (DWORD)(n - 1), &w, NULL);
    CloseHandle(h);
}

static void log_v(const wchar_t *lvl, const wchar_t *fmt, va_list ap) {
    wchar_t msg[1100], line[1200];
    SYSTEMTIME t;
    if (!g_logp[0]) return;
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (GetFileAttributesExW(g_logp, GetFileExInfoStandard, &d) && d.nFileSizeLow > 256 * 1024) { /* keep it small */
        wchar_t old[MAX_PATH];
        wcscpy(old, g_logp);
        wcscpy(wcsrchr(old, L'\\') + 1, L"log.old.txt");
        MoveFileExW(g_logp, old, MOVEFILE_REPLACE_EXISTING);
    }
    wvsprintfW(msg, fmt, ap);
    GetLocalTime(&t);
    wsprintfW(line, L"%04d-%02d-%02d %02d:%02d:%02d  %-5s  %s\r\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
              t.wSecond, lvl, msg);
    append_utf8(g_logp, line);
}
static void log_info(const wchar_t *fmt, ...) { va_list ap; va_start(ap, fmt); log_v(L"INFO", fmt, ap); va_end(ap); }
static void log_warn(const wchar_t *fmt, ...) { va_list ap; va_start(ap, fmt); log_v(L"WARN", fmt, ap); va_end(ap); }
/* An error also marks a report to send on the next start (once per run). */
static void log_error(const wchar_t *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_v(L"ERROR", fmt, ap);
    va_end(ap);
    if (!g_session_err && g_pendp[0] && !file_exists(g_pendp)) append_utf8(g_pendp, L"error");
    g_session_err = 1;
}

static void win_version(wchar_t *out) {
    typedef LONG(WINAPI * RtlGetVersionFn)(OSVERSIONINFOW *);
    OSVERSIONINFOW v = {0};
    v.dwOSVersionInfoSize = sizeof(v);
    RtlGetVersionFn f = (RtlGetVersionFn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
    if (f && !f(&v)) wsprintfW(out, L"Windows %lu.%lu build %lu%s", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber,
                              v.dwBuildNumber >= 22000 ? L" (Windows 11)" : L"");
    else wcscpy(out, L"Windows (unknown version)");
}

/* Growable wide-text buffer for building a report. */
typedef struct { wchar_t *s; size_t n, cap; } WBuf;
static void wb_add(WBuf *b, const wchar_t *t, size_t len) {
    if (!b->s && b->cap) return; /* an earlier allocation failed */
    if (b->n + len + 1 > b->cap) {
        size_t nc = (b->n + len + 1) * 2;
        wchar_t *ns = (wchar_t *)realloc(b->s, nc * sizeof(wchar_t));
        if (!ns) { free(b->s); b->s = NULL; b->cap = 1; return; }
        b->s = ns; b->cap = nc;
    }
    memcpy(b->s + b->n, t, len * sizeof(wchar_t));
    b->n += len;
    b->s[b->n] = 0;
}
static void wb_str(WBuf *b, const wchar_t *t) { wb_add(b, t, wcslen(t)); }
/* A text file (UTF-8, UTF-16 or ANSI), only its last max_bytes. */
static void wb_file(WBuf *b, const wchar_t *path, DWORD max_bytes) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { wb_str(b, L"(none)\r\n"); return; }
    DWORD size = GetFileSize(h, NULL), start = 0, got = 0;
    if (size == INVALID_FILE_SIZE) size = 0;
    if (size > max_bytes) start = (size - max_bytes) & ~1u;
    char *raw = (char *)malloc(size - start + 2);
    if (raw && SetFilePointer(h, (LONG)start, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
        ReadFile(h, raw, size - start, &got, NULL)) {
        raw[got] = raw[got + 1] = 0;
        if (start) wb_str(b, L"(...older lines cut...)\r\n");
        if (got >= 2 && (BYTE)raw[0] == 0xFF && (BYTE)raw[1] == 0xFE) wb_str(b, (wchar_t *)(raw + 2)); /* UTF-16 */
        else {
            int utf8 = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw, (int)got, NULL, 0) > 0;
            int n = MultiByteToWideChar(utf8 ? CP_UTF8 : CP_ACP, 0, raw, (int)got, NULL, 0);
            wchar_t *w = n > 0 ? (wchar_t *)malloc((n + 1) * sizeof(wchar_t)) : NULL;
            if (w) { MultiByteToWideChar(utf8 ? CP_UTF8 : CP_ACP, 0, raw, (int)got, w, n); w[n] = 0; wb_str(b, w); free(w); }
        }
    }
    free(raw);
    CloseHandle(h);
    wb_str(b, L"\r\n");
}

/* Swap the user's profile folder (it has their Windows name in it) for %USERPROFILE%. */
static void wb_hide_user(WBuf *b) {
    wchar_t up[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH);
    if (!b->s || n < 4 || n >= MAX_PATH) return;
    WBuf o = {0};
    size_t from = 0;
    for (size_t r = 0; r + n <= b->n; r++)
        if (!_wcsnicmp(b->s + r, up, n)) {
            wb_add(&o, b->s + from, r - from);
            wb_str(&o, L"%USERPROFILE%");
            r += n - 1;
            from = r + 1;
        }
    wb_add(&o, b->s + from, b->n - from);
    if (!o.s) return; /* out of memory: leave as is */
    free(b->s);
    *b = o;
}

/* The report text, UTF-8 (caller frees). */
static char *build_report(const wchar_t *reason, const wchar_t *cat, const wchar_t *desc, DWORD *len) {
    WBuf b = {0};
    wchar_t t[600], wv[96];
    win_version(wv);
    SYSTEMTIME st;
    GetLocalTime(&st);
    wsprintfW(t, L"Crosshair Overlay v" VER_WSTR L" bug report\r\nReason: %s\r\nWhen: %04d-%02d-%02d %02d:%02d\r\n"
                 L"Install id: %s\r\n%s\r\nScreen: %dx%d @ %d Hz, %d monitor(s)\r\nRoblox open: %s\r\n\r\n",
              reason, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, g_report_id, wv, GetSystemMetrics(SM_CXSCREEN),
              GetSystemMetrics(SM_CYSCREEN), refresh_rate(), GetSystemMetrics(SM_CMONITORS), roblox_running() ? L"yes" : L"no");
    wb_str(&b, t);
    if (g_report_summary[0]) wb_str(&b, g_report_summary);
    if (cat && cat[0]) { wb_str(&b, L"===== what's wrong =====\r\n"); wb_str(&b, cat); wb_str(&b, L"\r\n\r\n"); }
    if (desc && desc[0] && !g_report_extra) { wb_str(&b, L"===== what happened (from the user) =====\r\n"); wb_str(&b, desc); wb_str(&b, L"\r\n\r\n"); }
    if (g_report_extra) { wb_str(&b, L"===== Roblox crash =====\r\n"); wb_str(&b, g_report_extra); wb_str(&b, L"\r\n"); }
    wb_str(&b, L"===== settings.ini =====\r\n");
    wb_file(&b, g_ini, 32 * 1024);
    wb_str(&b, L"===== log.txt (newest at the bottom) =====\r\n");
    wb_file(&b, g_logp, 120 * 1024);
    wb_hide_user(&b);
    if (!b.s) return NULL;
    int n = WideCharToMultiByte(CP_UTF8, 0, b.s, (int)b.n, NULL, 0, NULL, NULL);
    char *out = n > 0 ? (char *)malloc(n + 1) : NULL;
    if (out) { WideCharToMultiByte(CP_UTF8, 0, b.s, (int)b.n, out, n, NULL, NULL); out[n] = 0; *len = (DWORD)n; }
    free(b.s);
    return out;
}

/* POST the report to the webhook as a text file attachment. 1 = Discord accepted it. */
/* POST payload_json + one file to a Discord webhook. 1 = Discord accepted it. */
static int webhook_post_to(const wchar_t *hook, const char *report, DWORD rlen, const char *fname, const char *ctype,
                           const char *payload) {
    URL_COMPONENTS uc;
    wchar_t host[256], path[1024];
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 1024;
    if (!WinHttpCrackUrl(hook, 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) return 0;
    static const char B[] = "----CrosshairOverlayReport7f3a9c";
    char head[8192], tail[64];
    int hl = snprintf(head, sizeof(head),
        "--%s\r\nContent-Disposition: form-data; name=\"payload_json\"\r\nContent-Type: application/json\r\n\r\n%s\r\n"
        "--%s\r\nContent-Disposition: form-data; name=\"files[0]\"; filename=\"%s\"\r\n"
        "Content-Type: %s\r\n\r\n", B, payload, B, fname, ctype);
    if (hl < 0 || hl >= (int)sizeof(head)) return 0;
    int tl = snprintf(tail, sizeof(tail), "\r\n--%s--\r\n", B);
    DWORD total = (DWORD)hl + rlen + (DWORD)tl;
    char *body = (char *)malloc(total);
    if (!body) return 0;
    memcpy(body, head, hl); memcpy(body + hl, report, rlen); memcpy(body + hl + rlen, tail, tl);
    int ok = 0;
    HINTERNET s = WinHttpOpen(L"CrosshairOverlay/" VER_WSTR, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) s = WinHttpOpen(L"CrosshairOverlay/" VER_WSTR, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET c = s ? WinHttpConnect(s, host, uc.nPort, 0) : NULL;
    HINTERNET r = c ? WinHttpOpenRequest(c, L"POST", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : NULL;
    if (r) {
        wchar_t ct[128];
        WinHttpSetTimeouts(r, 10000, 10000, 120000, 120000);
#ifdef CH_TEST /* test builds post to a local server with a self-signed certificate */
        DWORD sf = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
        WinHttpSetOption(r, WINHTTP_OPTION_SECURITY_FLAGS, &sf, sizeof(sf));
#endif
        wsprintfW(ct, L"Content-Type: multipart/form-data; boundary=%S", B);
        if (WinHttpSendRequest(r, ct, (DWORD)-1, body, total, total, 0) && WinHttpReceiveResponse(r, NULL)) {
            DWORD code = 0, sz = sizeof(code);
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
            ok = code >= 200 && code < 300;
        }
        WinHttpCloseHandle(r);
    }
    if (c) WinHttpCloseHandle(c);
    if (s) WinHttpCloseHandle(s);
    free(body);
    return ok;
}

typedef struct { char *report; DWORD len; char payload[14000]; int manual, kind; } SendJob;
static DWORD WINAPI report_thread(LPVOID p) {
    SendJob *j = (SendJob *)p;
    int ok = webhook_post_to(report_target(), j->report, j->len, "report.txt", "text/plain; charset=utf-8", j->payload);
    if (ok && j->kind < 2 && g_pendp[0]) DeleteFileW(g_pendp);
    if (ok) log_info(L"bug report sent (%s)", j->manual ? L"by user" : j->kind == 3 ? L"roblox crash" : L"automatic");
    else log_warn(L"bug report could not be sent (no internet, or the webhook was removed)");
    if (g_host) PostMessageW(g_host, WM_REPORT_DONE, (WPARAM)ok, (LPARAM)j->manual);
    free(j->report);
    free(j);
    return 0;
}

/* UTF-8 of a wide string, at most max bytes, never cutting a character in half. */
static void to_utf8(const wchar_t *w, char *out, int max) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w ? w : L"", -1, out, max, NULL, NULL);
    if (n <= 0) { /* didn't fit: convert what fits */
        out[0] = 0;
        n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
        char *t = n > 0 ? (char *)malloc(n) : NULL;
        if (!t) return;
        WideCharToMultiByte(CP_UTF8, 0, w, -1, t, n, NULL, NULL);
        int k = max - 4;
        while (k > 0 && ((BYTE)t[k] & 0xC0) == 0x80) k--;
        memcpy(out, t, k);
        strcpy(out + k, "...");
        free(t);
    }
}
/* Append s to a JSON string (inside the quotes). */
static void json_cat(char *out, size_t cap, const char *s) {
    size_t n = strlen(out);
    for (; *s && n + 8 < cap; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { out[n++] = '\\'; out[n++] = (char)c; }
        else if (c == '\n') { out[n++] = '\\'; out[n++] = 'n'; }
        else if (c == '\r') continue;
        else if (c < 0x20) n += snprintf(out + n, cap - n, "\\u%04x", c);
        else out[n++] = (char)c;
    }
    out[n] = 0;
}

/* Send a report in the background. kind: 0 crash, 1 error, 2 sent by the user.
   cat / desc = what the user picked and typed (may be NULL). */
/* The newest log line that contains needle (the text after the time and level), or "". */
static void log_last(const wchar_t *needle, wchar_t *out, int cap) {
    out[0] = 0;
    if (!g_logp[0]) return;
    HANDLE h = CreateFileW(g_logp, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER sz;
    char *raw = NULL;
    DWORD got = 0;
    if (GetFileSizeEx(h, &sz)) {
        LONGLONG take = sz.QuadPart > 48 * 1024 ? 48 * 1024 : sz.QuadPart;
        LARGE_INTEGER at;
        at.QuadPart = sz.QuadPart - take;
        raw = (char *)malloc((size_t)take + 1);
        if (raw && SetFilePointerEx(h, at, NULL, FILE_BEGIN) && ReadFile(h, raw, (DWORD)take, &got, NULL)) raw[got] = 0;
        else got = 0;
    }
    CloseHandle(h);
    if (!raw || !got) { free(raw); return; }
    int n = MultiByteToWideChar(CP_UTF8, 0, raw, (int)got, NULL, 0);
    wchar_t *w = n > 0 ? (wchar_t *)malloc((n + 1) * sizeof(wchar_t)) : NULL;
    if (w) {
        MultiByteToWideChar(CP_UTF8, 0, raw, (int)got, w, n);
        w[n] = 0;
        wchar_t *hit = NULL;
        for (wchar_t *f = w; (f = wcsstr(f, needle)); f++) hit = f;
        if (hit) {
            wchar_t *b = hit, *e = hit;
            while (b > w && b[-1] != L'\n') b--;
            while (*e && *e != L'\r' && *e != L'\n') e++;
            /* skip "2026-11-10 19:12:35  ERROR  " */
            wchar_t *t = b;
            for (int sp = 0; t < e && sp < 3; t++) if (*t == L' ' && t[1] != L' ') sp++;
            while (t < e && *t == L' ') t++;
            if (t >= e) t = b;
            int L = (int)(e - t);
            if (L >= cap) L = cap - 1;
            wcsncpy(out, t, L);
            out[L] = 0;
        }
        free(w);
    }
    free(raw);
}

static void crumbs_text(wchar_t *out, int cap, int maxn);
static void report_fields(char *out, size_t cap); /* the PC / version / setup fields (defined further down) */
static wchar_t g_last_ref[24];  /* reference shown to the player, e.g. 3d55-K7QM */

/* A JSON string value (with quotes) from wide text. */
static void json_wstr(char *out, size_t cap, const wchar_t *w, int maxbytes) {
    char *u = (char *)malloc(maxbytes + 8);
    size_t n = strlen(out);
    if (!u || n + 4 >= cap) { free(u); return; }
    to_utf8(w, u, maxbytes);
    strcat(out, "\"");
    json_cat(out, cap - 2, u);
    strcat(out, "\"");
    free(u);
}
static void json_field(char *out, size_t cap, const char *name, const wchar_t *value, int inl) {
    if (!value || !value[0]) return;
    size_t n = strlen(out);
    if (n + 64 >= cap) return;
    snprintf(out + n, cap - n, "%s{\"name\":\"%s\",\"inline\":%s,\"value\":", out[n - 1] == '[' ? "" : ",", name, inl ? "true" : "false");
    json_wstr(out, cap - 4, value, 1000);
    strcat(out, "}");
}

/* What an error line in the log means, in plain words. */
static void explain_error(const wchar_t *line, wchar_t *what, wchar_t *why, wchar_t *fix) {
    what[0] = why[0] = fix[0] = 0;
    const wchar_t *v = wcsstr(line, L"update: v");
    if (wcsstr(line, L"rolled back") && v) {
        wchar_t ver[24] = L"";
        int k = 0;
        for (v += 9; *v && *v != L' ' && k < 23; v++) ver[k++] = *v;
        ver[k] = 0;
        wsprintfW(what, L"The update to v%s was undone \x2014 the app went back to the version before it.", ver);
        if (wcsstr(line, L"crashed")) wcscpy(why, L"The new version crashed soon after it was installed (see the CRASH lines in report.txt).");
        else wcscpy(why, L"The new version was started 3 times and never closed normally (it was killed, froze or the PC shut down hard). Each start counts as a failed try.");
        wsprintfW(fix, L"Nothing for the player to do: v%s is skipped on this PC, the next version installs normally. Check the \"exit (...)\" lines in report.txt to see how it closed.", ver);
    } else if (wcsstr(line, L"SHA-256")) {
        wcscpy(what, L"A downloaded update didn't match GitHub's checksum, so it wasn't installed.");
        wcscpy(why, L"The download was cut off or changed on the way (bad connection, proxy or antivirus).");
        wcscpy(fix, L"It tries again by itself. If it keeps happening, re-upload the exe in the GitHub release.");
    } else if (wcsstr(line, L"FPS cap")) {
        wcscpy(what, L"The FPS cap couldn't be saved into Roblox's settings file.");
        wcscpy(why, L"Roblox's settings file was locked or read-only (Roblox open, or a folder Windows protects).");
        wcscpy(fix, L"Close Roblox and click Apply now again, or run the app as administrator once.");
    } else if (wcsstr(line, L"boost:")) {
        wcscpy(what, L"The performance boost couldn't write Roblox's settings.");
        wcscpy(why, L"No permission to Roblox's folder (installed for all users, or protected by antivirus).");
        wcscpy(fix, L"Run the app as administrator once, or allow it in the antivirus.");
    } else lstrcpynW(what, line, 300);
}

/* Send a report in the background. kind: 0 crash, 1 error, 2 sent by the user, 3 Roblox crash.
   cat / desc = what the user picked and typed (may be NULL). */
static int report_send(int kind, const wchar_t *cat, const wchar_t *desc) {
    static const wchar_t *WHY[] = {L"the app crashed", L"the app logged an error", L"sent by the user", L"Roblox crashed"};
    /* the label in Discord says what happened, so you can tell reports apart without opening them */
    const char *tag = kind == 0 ? "App crash (automatic)" : kind == 1 ? "Error (automatic)" : kind == 2 ? "Problem report" : "Roblox crash (automatic)";
    const char *title = kind == 0 ? "\xF0\x9F\x92\xA5 The app crashed" : kind == 1 ? "\xE2\x9A\xA0\xEF\xB8\x8F The app hit an error"
                      : kind == 2 ? "\xF0\x9F\x93\x9D Problem report from a player" : "\xF0\x9F\x8E\xAE Roblox crashed";
    int color = kind == 0 ? 0xE74C3C : kind == 1 ? 0xE67E22 : kind == 2 ? 0x3498DB : 0x9B59B6;
    static wchar_t what[700], why[400], fix[400], before[600], line[400];
    what[0] = why[0] = fix[0] = before[0] = 0;
    if (kind == 0) {
        log_last(L"CRASH:", line, 400);
        if (line[0]) lstrcpynW(what, line + (wcsncmp(line, L"CRASH: ", 7) ? 0 : 7), 700);
        if (what[0]) { what[0] = towupper(what[0]); lstrcatW(what, L"."); }
        else wcscpy(what, L"The app closed unexpectedly (no details were logged).");
        log_last(L"CRASH why:", line, 400);
        if (line[0]) { wsprintfW(why, L"%s.", line + 11); why[0] = towupper(why[0]); }
        log_last(L"CRASH fix:", line, 400);
        if (line[0]) lstrcpynW(fix, line + 11, 400);
        log_last(L"CRASH before:", line, 400);
        if (line[0]) lstrcpynW(before, line + 14, 600);
    } else if (kind == 1) {
        log_last(L"  ERROR  ", line, 400);
        explain_error(line, what, why, fix);
        if (wcsstr(line, L"rolled back")) {
            tag = "Update undone (automatic)";
            title = "\xE2\x86\xA9\xEF\xB8\x8F An update was undone";
            color = 0xF1C40F;
            log_last(L"  exit (", line, 400);
            if (line[0]) wsprintfW(before, L"last close: %s", line);
        }
    } else if (kind == 3 && g_report_extra) {
        int L = 0;
        while (g_report_extra[L] && g_report_extra[L] != L'\r' && g_report_extra[L] != L'\n' && L < 699) L++;
        wcsncpy(what, g_report_extra, L);
        what[L] = 0;
        const wchar_t *rest = g_report_extra + L;
        while (*rest == L'\r' || *rest == L'\n') rest++;
        lstrcpynW(why, rest, 400);
    }
    if (kind != 0 && !before[0]) crumbs_text(before, 600, 5);
    if (!webhook_set()) return 0;
    /* reference the player can quote: install id + time */
    {
        static const wchar_t B32W[] = L"0123456789ABCDEFGHJKMNPQRSTVWXYZ";
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        DWORD t = (DWORD)((((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 600000000ull); /* minutes */
        wchar_t r4[5];
        for (int i = 3; i >= 0; i--) { r4[i] = B32W[t & 31]; t >>= 5; }
        r4[4] = 0;
        wsprintfW(g_last_ref, L"%.4s-%s", g_report_id, r4);
    }
    /* summary at the top of report.txt */
    wsprintfW(g_report_summary, L"===== summary =====\r\nWhat happened: %s\r\n%s%s%s%s%s%s%s%s%sRef: %s\r\n\r\n",
              what[0] ? what : (cat ? cat : L"-"), why[0] ? L"Why: " : L"", why, why[0] ? L"\r\n" : L"",
              fix[0] ? L"What to try: " : L"", fix, fix[0] ? L"\r\n" : L"", before[0] ? L"Before it happened: " : L"", before,
              before[0] ? L"\r\n" : L"", g_last_ref);
    SendJob *j = (SendJob *)calloc(1, sizeof(SendJob));
    if (!j) return 0;
    j->report = build_report(WHY[kind], cat, desc, &j->len);
    g_report_summary[0] = 0;
    if (!j->report) { free(j); return 0; }
    j->manual = kind == 2;
    j->kind = kind;
    char *P = j->payload, id[24], ref[24];
    size_t cap = sizeof(j->payload) - 200;
    to_utf8(g_report_id, id, sizeof(id));
    to_utf8(g_last_ref, ref, sizeof(ref));
    /* the ping + one line (also what the optional relay checks) */
    snprintf(P, cap, "{\"username\":\"Crosshair Overlay reports\",\"content\":\"%s%s%s**%s** \xE2\x80\xA2 v%s%s \xE2\x80\xA2 ref `%s`\",",
             BUG_PING_USER_ID[0] ? "<@" : "", BUG_PING_USER_ID, BUG_PING_USER_ID[0] ? "> " : "", tag, VER_STR,
             g_is_tester ? " \xE2\x80\xA2 \xF0\x9F\xA7\xAA tester" : "", ref);
    /* the card */
    {
        static wchar_t d[2600];
        d[0] = 0;
        if (what[0]) { lstrcatW(d, L"**What happened**\n"); lstrcatW(d, what); }
        if (why[0]) { lstrcatW(d, kind == 3 ? L"\n\n**Likely cause**\n" : L"\n\n**Why**\n"); lstrcatW(d, why); }
        if (fix[0]) { lstrcatW(d, L"\n\n**What to try**\n"); lstrcatW(d, fix); }
        if (cat && cat[0]) { lstrcatW(d, d[0] ? L"\n\n**The player says**\n" : L"**The player says**\n"); lstrcatW(d, cat); }
        if (desc && desc[0] && kind == 2) {
            wchar_t q[1700];
            int k = 0;
            q[k++] = L'>'; q[k++] = L' ';
            for (const wchar_t *c = desc; *c && k < 1680; c++) {
                if (*c == L'\r') continue;
                q[k++] = *c;
                if (*c == L'\n') { q[k++] = L'>'; q[k++] = L' '; }
            }
            q[k] = 0;
            lstrcatW(d, L"\n");
            if (lstrlenW(d) + k < 2590) lstrcatW(d, q);
        } else if (kind == 2) lstrcatW(d, L"\n*(no description)*");
        size_t n = strlen(P);
        snprintf(P + n, cap - n, "\"embeds\":[{\"title\":\"%s\",\"color\":%d,\"description\":", title, color);
        json_wstr(P, cap, d, 3800);
        strcat(P, ",\"fields\":[");
        json_field(P, cap, kind == 2 ? "What they did before" : "Before it happened", before, 0);
        report_fields(P, cap);
        n = strlen(P);
        snprintf(P + n, cap - n, "],\"footer\":{\"text\":\"report.txt has the full log and settings \xE2\x80\xA2 install id %s\"}}],", id);
    }
    /* only the developer can be pinged - "@everyone" typed in a report does nothing */
    size_t n = strlen(P);
    snprintf(P + n, sizeof(j->payload) - n, "\"allowed_mentions\":{\"users\":[%s%s%s]}}",
             BUG_PING_USER_ID[0] ? "\"" : "", BUG_PING_USER_ID, BUG_PING_USER_ID[0] ? "\"" : "");
    HANDLE th = CreateThread(NULL, 0, report_thread, j, 0, NULL);
    if (!th) { free(j->report); free(j); return 0; }
    CloseHandle(th);
    return 1;
}

/* No webhook: write the report next to the log and show it in Explorer. */
static int report_save(wchar_t *out_path, const wchar_t *cat, const wchar_t *desc) {
    DWORD len = 0;
    char *r = build_report(L"saved by the user", cat, desc, &len);
    if (!r || !g_logp[0]) { free(r); return 0; }
    wcscpy(out_path, g_logp);
    wcscpy(wcsrchr(out_path, L'\\') + 1, L"bug-report.txt");
    HANDLE h = CreateFileW(out_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD w = 0;
    int ok = h != INVALID_HANDLE_VALUE && WriteFile(h, r, len, &w, NULL) && w == len;
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    free(r);
    if (ok) {
        wchar_t args[MAX_PATH + 16];
        wsprintfW(args, L"/select,\"%s\"", out_path);
        ShellExecuteW(NULL, L"open", L"explorer.exe", args, NULL, SW_SHOWNORMAL);
    }
    return ok;
}

/* At start: something went wrong last time? Ask once, then send if allowed. */
static void report_startup(void) {
    wchar_t why[32] = L"";
    if (!g_pendp[0] || !file_exists(g_pendp)) return;
    HANDLE h = CreateFileW(g_pendp, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        char b[16] = {0}; DWORD got = 0;
        ReadFile(h, b, 15, &got, NULL);
        CloseHandle(h);
        MultiByteToWideChar(CP_UTF8, 0, b, -1, why, 32);
    }
    int crash = !wcsncmp(why, L"crash", 5);
    if (!webhook_set() || g_reports == 2) { DeleteFileW(g_pendp); return; }
    if (g_reports == 0) {
        wchar_t q[900];
        wsprintfW(q, L"%s\n\nSend bug reports to the developer (simplysoul) so it can be fixed?\n\n"
                     L"A report has: the app version, your Windows version, screen size, this app's settings and its "
                     L"log (start/exit, errors). Your Windows user name is removed. No other files, no passwords, "
                     L"nothing from Roblox or your games.\n\n"
                     L"Yes = send this one and future ones automatically (turn off any time in General).\n"
                     L"No = never send.",
                  crash ? L"Crosshair Overlay closed unexpectedly last time." : L"Crosshair Overlay ran into a problem last time.");
        int a = MessageBoxW(NULL, q, L"Crosshair Overlay - bug reports", MB_YESNO | MB_ICONQUESTION | MB_TOPMOST | MB_SETFOREGROUND);
        g_reports = a == IDYES ? 1 : 2;
        save_settings();
        log_info(L"bug reports turned %s", g_reports == 1 ? L"on" : L"off");
        if (g_reports != 1) { DeleteFileW(g_pendp); return; }
    }
    if (report_send(crash ? 0 : 1, NULL, NULL) && crash) {
        /* let them add what they were doing */
        g_nid.uFlags = NIF_INFO;
        wcscpy(g_nid.szInfoTitle, L"Sorry \x2014 the app crashed last time");
        wcscpy(g_nid.szInfo, L"A crash report was sent to simplysoul. Click here to say what you were doing when it happened.");
        g_nid.dwInfoFlags = NIIF_INFO;
        Shell_NotifyIconW(NIM_MODIFY, &g_nid);
        g_nid.szInfo[0] = 0;
        g_balloon_report = 1;
        g_balloon_update = 0;
    }
}

/* ---------------- automatic updates (GitHub releases) ---------------- */
/* The app asks GitHub for the newest release of UPDATE_REPO (update.h). If it's newer,
   the exe is downloaded to %APPDATA%\CrosshairOverlay\update, checked (it's a Windows
   program, its size matches, and its SHA-256 matches GitHub's), and installed when the
   player clicks "restart to update" - or by itself the next time the app starts.
   Installing = rename the running exe to .old, put the new one in its place, start it
   (it waits for this one to close, then deletes the .old). */
enum { UPD_IDLE, UPD_CHECKING, UPD_LATEST, UPD_READY, UPD_FAILED };
static volatile LONG g_upd_state, g_upd_busy;
static wchar_t g_upd_ver[32], g_upd_file[MAX_PATH];
static int g_just_updated;
#define UPD_TIMER 11

static int updates_set(void) { return UPDATE_REPO[0] != 0; }

static const wchar_t *gh_api(void) {
#ifdef CH_TEST
    static wchar_t t[200];
    if (GetEnvironmentVariableW(L"CH_GH_API", t, 200)) return t;
#endif
    return L"https://api.github.com";
}

/* One HTTPS request. Returns the HTTP status (0 = no answer). *out = response body (malloc, NUL-ended). */
static int https_req(const wchar_t *method, const wchar_t *url, const wchar_t *headers, const void *body, DWORD blen,
                     char **out, DWORD *olen, DWORD max_out) {
    URL_COMPONENTS uc;
    static __thread wchar_t host[256], path[2048], extra[2048], full[4100];
    if (out) *out = NULL;
    if (olen) *olen = 0;
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 2048;
    if (!WinHttpCrackUrl(url, 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) return 0;
    wcscpy(full, path);
    wcscat(full, extra);
    int status = 0;
    HINTERNET s = WinHttpOpen(L"CrosshairOverlay/" VER_WSTR, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) s = WinHttpOpen(L"CrosshairOverlay/" VER_WSTR, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET c = s ? WinHttpConnect(s, host, uc.nPort, 0) : NULL;
    HINTERNET r = c ? WinHttpOpenRequest(c, method, full, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : NULL;
    if (r) {
        WinHttpSetTimeouts(r, 15000, 15000, 120000, 120000);
#ifdef CH_TEST
        DWORD sf = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
        WinHttpSetOption(r, WINHTTP_OPTION_SECURITY_FLAGS, &sf, sizeof(sf));
#endif
        if (WinHttpSendRequest(r, headers ? headers : WINHTTP_NO_ADDITIONAL_HEADERS, headers ? (DWORD)-1 : 0,
                               (LPVOID)body, blen, blen, 0) && WinHttpReceiveResponse(r, NULL)) {
            DWORD code = 0, sz = sizeof(code);
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                &code, &sz, WINHTTP_NO_HEADER_INDEX);
            status = (int)code;
            if (out) {
                DWORD cap = 64 * 1024, n = 0, avail = 0;
                char *b = (char *)malloc(cap + 1);
                while (b && WinHttpQueryDataAvailable(r, &avail) && avail) {
                    if (n + avail > max_out) { free(b); b = NULL; status = 0; break; } /* too big */
                    if (n + avail > cap) {
                        while (n + avail > cap) cap *= 2;
                        char *nb = (char *)realloc(b, cap + 1);
                        if (!nb) { free(b); b = NULL; status = 0; break; }
                        b = nb;
                    }
                    DWORD got = 0;
                    if (!WinHttpReadData(r, b + n, avail, &got)) { free(b); b = NULL; status = 0; break; }
                    n += got;
                }
                if (b) { b[n] = 0; *out = b; if (olen) *olen = n; }
            }
        }
        WinHttpCloseHandle(r);
    }
    if (c) WinHttpCloseHandle(c);
    if (s) WinHttpCloseHandle(s);
    return status;
}

/* ---- tiny JSON readers (enough for GitHub's answers) ---- */
static const char *jkey(const char *from, const char *end, const char *key) {
    char pat[72];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    for (const char *p = from; p && (p = strstr(p, pat)) && (!end || p < end);) {
        const char *q = p + strlen(pat);
        while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') q++;
        if (*q == ':') {
            q++;
            while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') q++;
            return q;
        }
        p = q;
    }
    return NULL;
}
static int jstr(const char *v, char *out, int cap) { /* v at the opening quote */
    int n = 0;
    if (!v || *v != '"') return 0;
    for (v++; *v && *v != '"';) {
        unsigned c = (unsigned char)*v++;
        if (c == '\\' && *v) {
            char e = *v++;
            if (e == 'n') c = '\n'; else if (e == 'r') c = '\r'; else if (e == 't') c = '\t';
            else if (e == 'b' || e == 'f') c = ' ';
            else if (e == 'u') {
                unsigned u = 0;
                for (int i = 0; i < 4 && *v; i++, v++)
                    u = u * 16 + (unsigned)(*v >= 'a' ? *v - 'a' + 10 : *v >= 'A' ? *v - 'A' + 10 : *v - '0');
                char t[4]; int k = 0;
                if (u >= 0xD800 && u <= 0xDFFF) t[k++] = '?';
                else if (u < 0x80) t[k++] = (char)u;
                else if (u < 0x800) { t[k++] = (char)(0xC0 | (u >> 6)); t[k++] = (char)(0x80 | (u & 63)); }
                else { t[k++] = (char)(0xE0 | (u >> 12)); t[k++] = (char)(0x80 | ((u >> 6) & 63)); t[k++] = (char)(0x80 | (u & 63)); }
                for (int i = 0; i < k; i++) if (n < cap - 1) out[n++] = t[i];
                continue;
            } else c = (unsigned char)e;
        }
        if (n < cap - 1) out[n++] = (char)c;
    }
    out[n] = 0;
    return 1;
}

static int ver_parse(const char *s, int v[3]) {
    v[0] = v[1] = v[2] = 0;
    if (*s == 'v' || *s == 'V') s++;
    return sscanf(s, "%d.%d.%d", &v[0], &v[1], &v[2]) >= 2;
}
static int ver_cmp(const int a[3], const int b[3]) {
    for (int i = 0; i < 3; i++) if (a[i] != b[i]) return a[i] > b[i] ? 1 : -1;
    return 0;
}
static int ver_newer_than_me(const char *s) {
    int a[3], me[3] = {VER_MAJOR, VER_MINOR, VER_PATCH};
    return ver_parse(s, a) && ver_cmp(a, me) > 0;
}

static int sha256_hex(const BYTE *data, DWORD n, char out[65]) {
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE h = NULL;
    BYTE d[32];
    int ok = 0;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0) {
        if (BCryptCreateHash(alg, &h, NULL, 0, NULL, 0, 0) == 0) {
            if (BCryptHashData(h, (PUCHAR)data, n, 0) == 0 && BCryptFinishHash(h, d, 32, 0) == 0) {
                for (int i = 0; i < 32; i++) snprintf(out + i * 2, 3, "%02x", d[i]);
                ok = 1;
            }
            BCryptDestroyHash(h);
        }
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    return ok;
}

/* Tester IDs are never stored as-is in the public testers.txt: each one becomes "t1-" + a slow one-way
   hash (PBKDF2-SHA256, 200,000 rounds), so nobody can read an ID out of the file and copy it. */
static int tester_hash(const char *id, char out[40]) {
    static const char salt[] = "CrosshairOverlay/testers/v1";
    char low[24];
    BYTE d[12];
    BCRYPT_ALG_HANDLE alg = NULL;
    int n = 0, ok = 0;
    for (; id[n] && n < 23; n++) low[n] = (char)tolower((unsigned char)id[n]);
    low[n] = 0;
    out[0] = 0;
    if (!n) return 0;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG) == 0) {
        if (BCryptDeriveKeyPBKDF2(alg, (PUCHAR)low, (ULONG)n, (PUCHAR)salt, (ULONG)strlen(salt), 200000, d, sizeof(d), 0) == 0) {
            strcpy(out, "t1-");
            for (int i = 0; i < 12; i++) snprintf(out + 3 + i * 2, 3, "%02x", d[i]);
            ok = 1;
        }
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    return ok;
}
static __attribute__((unused)) int is_hex8(const char *a) { /* exactly 8 hex digits, then end / space */
    for (int i = 0; i < 8; i++) if (!isxdigit((unsigned char)a[i])) return 0;
    return !a[8] || a[8] == ' ' || a[8] == '\t' || a[8] == '\r' || a[8] == '\n' || a[8] == '#';
}

static void upd_dir(wchar_t *out) { /* %APPDATA%\CrosshairOverlay\update */
    wcscpy(out, g_logp);
    wchar_t *sl = wcsrchr(out, L'\\');
    if (sl) wcscpy(sl + 1, L"update");
    else out[0] = 0;
}
static int is_exe_file(const BYTE *b, DWORD n) { return n > 4096 && b[0] == 'M' && b[1] == 'Z'; }

/* A downloaded update newer than this exe, waiting in the update folder? */
static int update_pending(void) {
    wchar_t dir[MAX_PATH], pat[MAX_PATH];
    char best[32] = "";
    int bv[3] = {VER_MAJOR, VER_MINOR, VER_PATCH};
    upd_dir(dir);
    if (!dir[0]) return 0;
    wsprintfW(pat, L"%s\\CrosshairOverlay-v*.exe", dir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        char nm[MAX_PATH];
        int v[3];
        WideCharToMultiByte(CP_UTF8, 0, fd.cFileName + 17, -1, nm, sizeof(nm), NULL, NULL); /* after "CrosshairOverlay-" */
        if (ver_parse(nm, v) && ver_cmp(v, bv) > 0) {
            memcpy(bv, v, sizeof(v));
            snprintf(best, sizeof(best), "%d.%d.%d", v[0], v[1], v[2]);
            wsprintfW(g_upd_file, L"%s\\%s", dir, fd.cFileName);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (!best[0]) return 0;
    MultiByteToWideChar(CP_UTF8, 0, best, -1, g_upd_ver, 32);
    return 1;
}

/* ---- installing, and the safety net ----
   After an update the previous exe is kept as <exe>.old while the new version is "on probation".
   If the new version crashes, or fails to run for 45 s three starts in a row, the app swaps the old
   exe back, skips that version (until a newer one comes out), tells the player and logs an error
   (which becomes a bug report for the developer). */
static int g_probation;          /* this run is the first runs of a fresh update */
static wchar_t g_rolled_from[32];/* we were started by a rollback from this version */
/* g_is_tester: declared near the top */

#define PROB_TIMER 12

static void probation_path(wchar_t *out) { upd_dir(out); if (out[0]) wcscat(out, L"\\probation.ini"); }

/* Put the downloaded exe in place of this one and start it. 1 = new version started (exit now). */
static int update_install(void) {
    wchar_t self[MAX_PATH], old[MAX_PATH + 8], cmd[MAX_PATH + 64], pp[MAX_PATH], dir[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, self, MAX_PATH);
    if (!n || n >= MAX_PATH - 8 || !g_upd_file[0] || !file_exists(g_upd_file)) return 0;
    wsprintfW(old, L"%s.old", self);
    DeleteFileW(old);
    if (!MoveFileExW(self, old, MOVEFILE_REPLACE_EXISTING)) {
        log_warn(L"update: can't replace the exe in its folder (error %lu)", GetLastError());
        return 0;
    }
    if (!CopyFileW(g_upd_file, self, FALSE)) {
        log_warn(L"update: copy failed (error %lu), putting the old exe back", GetLastError());
        MoveFileExW(old, self, MOVEFILE_REPLACE_EXISTING);
        return 0;
    }
    upd_dir(dir);
    CreateDirectoryW(dir, NULL);
    probation_path(pp);
    DeleteFileW(pp);
    WritePrivateProfileStringW(L"probation", L"version", g_upd_ver, pp);
    WritePrivateProfileStringW(L"probation", L"previous", VER_WSTR, pp);
    WritePrivateProfileStringW(L"probation", L"starts", L"0", pp);
    WritePrivateProfileStringW(L"probation", L"crashed", L"0", pp);
    wsprintfW(cmd, L"\"%s\" --updated %lu", self, GetCurrentProcessId());
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    if (!CreateProcessW(self, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        log_warn(L"update: couldn't start the new version (error %lu), rolling back", GetLastError());
        DeleteFileW(self);
        MoveFileExW(old, self, MOVEFILE_REPLACE_EXISTING);
        DeleteFileW(pp);
        return 0;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    log_info(L"update: installing v%s, restarting", g_upd_ver);
    crumb(L"installing update v%s", g_upd_ver);
    return 1;
}

/* Started as "--updated <pid> [--rolledback <ver>]": wait for the old copy to close. Returns that pid. */
static DWORD update_wait_old(void) {
    int argc = 0;
    DWORD pid = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc >= 3 && !wcscmp(argv[1], L"--updated")) {
        pid = (DWORD)wcstoul(argv[2], NULL, 10);
        HANDLE p = pid ? OpenProcess(SYNCHRONIZE, FALSE, pid) : NULL;
        if (p) { WaitForSingleObject(p, 15000); CloseHandle(p); }
        if (argc >= 5 && !wcscmp(argv[3], L"--rolledback")) wcsncpy(g_rolled_from, argv[4], 31);
    }
    if (argv) LocalFree(argv);
    return pid;
}

static void del_retry(const wchar_t *f) { for (int i = 0; i < 20 && file_exists(f) && !DeleteFileW(f); i++) Sleep(250); }

/* Swap the previous exe back in and start it. 1 = it's starting (exit now). */
static int update_rollback(const wchar_t *why) {
    wchar_t self[MAX_PATH], old[MAX_PATH + 8], bad[MAX_PATH + 8], cmd[MAX_PATH + 96], pp[MAX_PATH];
    GetModuleFileNameW(NULL, self, MAX_PATH);
    wsprintfW(old, L"%s.old", self);
    wsprintfW(bad, L"%s.bad", self);
    probation_path(pp);
    DeleteFileW(pp);
    if (!file_exists(old)) { log_warn(L"update: v" VER_WSTR L" %s, but the previous version is gone - can't roll back", why); return 0; }
    DeleteFileW(bad);
    if (!MoveFileExW(self, bad, MOVEFILE_REPLACE_EXISTING)) return 0;
    if (!MoveFileExW(old, self, MOVEFILE_REPLACE_EXISTING)) { MoveFileExW(bad, self, MOVEFILE_REPLACE_EXISTING); return 0; }
    if (g_ini[0]) WritePrivateProfileStringW(L"crosshair", L"skip_update", VER_WSTR, g_ini);
    log_error(L"update: v" VER_WSTR L" %s - rolled back to the previous version", why);
    wsprintfW(cmd, L"\"%s\" --updated %lu --rolledback " VER_WSTR, self, GetCurrentProcessId());
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    if (!CreateProcessW(self, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return 0;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 1;
}

/* At start. Returns 1 if a rollback is starting the previous version (exit now). */
static int update_startup_checks(DWORD updated_from) {
    wchar_t self[MAX_PATH], bad[MAX_PATH + 8], pp[MAX_PATH], v[32];
    GetModuleFileNameW(NULL, self, MAX_PATH);
    wsprintfW(bad, L"%s.bad", self);
    if (file_exists(bad)) del_retry(bad);
    probation_path(pp);
    if (pp[0] && file_exists(pp)) {
        GetPrivateProfileStringW(L"probation", L"version", L"", v, 32, pp);
        if (!wcscmp(v, VER_WSTR)) {
            int starts = (int)GetPrivateProfileIntW(L"probation", L"starts", 0, pp) + 1;
            int crashed = (int)GetPrivateProfileIntW(L"probation", L"crashed", 0, pp);
            if (crashed && update_rollback(L"crashed right after updating")) return 1;
            if (starts >= 3 && update_rollback(L"didn't start properly (3 tries)")) return 1;
            wchar_t b[16];
            wsprintfW(b, L"%d", starts);
            WritePrivateProfileStringW(L"probation", L"starts", b, pp);
            g_probation = 1;
        } else DeleteFileW(pp); /* left from some other version */
    }
    if (updated_from) { /* first start after an update or a rollback: tidy the download folder */
        wchar_t dir[MAX_PATH], pat[MAX_PATH], f[MAX_PATH];
        upd_dir(dir);
        wsprintfW(pat, L"%s\\CrosshairOverlay-v*", dir);
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(pat, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do { wsprintfW(f, L"%s\\%s", dir, fd.cFileName); DeleteFileW(f); } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        if (!g_rolled_from[0]) {
            log_info(L"update: now running v" VER_WSTR L" (on probation until it has run for 45 s)");
            g_just_updated = 1;
        }
    }
    if (g_rolled_from[0]) log_info(L"update: back on v" VER_WSTR L" after v%s had a problem", g_rolled_from);
    return 0;
}
/* The new version ran fine: drop the safety copy. */
static void probation_pass(void) {
    wchar_t self[MAX_PATH], old[MAX_PATH + 8], pp[MAX_PATH];
    if (!g_probation) return;
    g_probation = 0;
    GetModuleFileNameW(NULL, self, MAX_PATH);
    wsprintfW(old, L"%s.old", self);
    probation_path(pp);
    DeleteFileW(pp);
    del_retry(old);
    log_info(L"update: v" VER_WSTR L" runs fine, previous version removed");
}

/* Is this PC on the tester list (testers.txt in the repo)? */
static const wchar_t *gh_raw(void) {
#ifdef CH_TEST
    static wchar_t t[200];
    if (GetEnvironmentVariableW(L"CH_GH_RAW", t, 200)) return t;
#endif
    return L"https://raw.githubusercontent.com";
}
static int tester_check(void) {
    wchar_t url[400];
    char *txt = NULL, id[24];
    wsprintfW(url, L"%s/repos/%s/contents/testers.txt", gh_api(), UPDATE_REPO);
    int st = https_req(L"GET", url, L"Accept: application/vnd.github.raw+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n", NULL, 0, &txt, NULL, 256 * 1024);
    if (st != 200 && st != 404) { /* GitHub's API busy (it allows 60 checks an hour per network): use the cached copy */
        free(txt);
        txt = NULL;
        wsprintfW(url, L"%s/%s/HEAD/testers.txt", gh_raw(), UPDATE_REPO);
        st = https_req(L"GET", url, NULL, NULL, 0, &txt, NULL, 256 * 1024);
    }
    if (st == 404) { free(txt); txt = NULL; st = 200; } /* no tester list = nobody is a tester */
    int yes = 0;
    static char myh[40];
    WideCharToMultiByte(CP_UTF8, 0, g_report_id, -1, id, sizeof(id), NULL, NULL);
    if (!myh[0] && id[0]) tester_hash(id, myh);
    if (st == 200 && txt && id[0])
        for (char *l = txt; l && *l;) {
            char *e = strchr(l, '\n');
            while (*l == ' ' || *l == '\t') l++;
            size_t L = strlen(id);
            if (*l != '#' && !_strnicmp(l, id, L) && (l[L] == 0 || l[L] == ' ' || l[L] == '\t' || l[L] == '\r' || l[L] == '\n' || l[L] == '#')) { yes = 1; break; }
            size_t M = strlen(myh);
            if (M && !_strnicmp(l, myh, M) && (l[M] == 0 || l[M] == ' ' || l[M] == '\t' || l[M] == '\r' || l[M] == '\n' || l[M] == '#')) { yes = 1; break; }
            l = e ? e + 1 : NULL;
        }
    free(txt);
    if (st == 200 && yes != g_is_tester) log_info(yes ? L"update: this PC is a beta tester" : L"update: this PC is not a beta tester");
    if (st == 200 && InterlockedExchange(&g_is_tester, yes) != yes && g_host) PostMessageW(g_host, WM_SAVE_LATER, 0, 0);
    return g_is_tester;
}

/* The developer's announcement: news.txt in the repo. Lines starting with # are skipped. */
static void news_check(void) {
    wchar_t url[400], w[260];
    char *txt = NULL, msg[1024];
    int n = 0;
    wsprintfW(url, L"%s/repos/%s/contents/news.txt", gh_api(), UPDATE_REPO);
    int st = https_req(L"GET", url, L"Accept: application/vnd.github.raw+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n", NULL, 0, &txt, NULL, 64 * 1024);
    if (st != 200 && st != 404) {
        free(txt);
        txt = NULL;
        wsprintfW(url, L"%s/%s/HEAD/news.txt", gh_raw(), UPDATE_REPO);
        st = https_req(L"GET", url, NULL, NULL, 0, &txt, NULL, 64 * 1024);
    }
    if (st != 200 && st != 404) { free(txt); return; } /* offline: keep what we had */
    msg[0] = 0;
    if (st == 200 && txt)
        for (char *l = txt; l && *l && n < 900;) {
            char *e = strchr(l, '\n');
            size_t L = e ? (size_t)(e - l) : strlen(l);
            while (L && (l[L - 1] == '\r' || l[L - 1] == ' ' || l[L - 1] == '\t')) L--;
            char *b = l;
            while (L && (*b == ' ' || *b == '\t')) { b++; L--; }
            if (L && *b != '#') {
                if (n) msg[n++] = ' ';
                if (L > (size_t)(900 - n)) L = 900 - n;
                memcpy(msg + n, b, L);
                n += (int)L;
                msg[n] = 0;
            }
            l = e ? e + 1 : NULL;
        }
    free(txt);
    w[0] = 0;
    MultiByteToWideChar(CP_UTF8, 0, msg, -1, w, 260);
    w[259] = 0;
    if ((int)wcslen(w) > 240) wcscpy(w + 237, L"...");
    for (wchar_t *c = w; *c; c++) if (*c < 32) *c = ' ';
    unsigned h = 2166136261u;
    for (wchar_t *c = w; *c; c++) h = (h ^ *c) * 16777619u;
    if (!w[0]) h = 0;
    h &= 0x7fffffff;
    if (h == g_news_hash) return;
    wcscpy(g_news, w);
    InterlockedExchange((volatile LONG *)&g_news_hash, (LONG)h);
    if (h) log_info(L"announcement: %s", w);
    if (g_host) PostMessageW(g_host, WM_NEWS, 0, 0);
}

/* A message with no file to a Discord webhook. 1 = accepted. */
static int webhook_post_json(const wchar_t *hook, const char *payload) {
    if (!hook || !hook[0]) return 0;
    int st = https_req(L"POST", hook, L"Content-Type: application/json\r\n", payload, (DWORD)strlen(payload), NULL, NULL, 0);
    return st >= 200 && st < 300;
}
/* Where usage stats / tester check-ins go: STATS_WEBHOOK_URL (webhook.h) if set, else the bug-report webhook. */
static const wchar_t *stats_target(void) {
#ifdef CH_TEST
    static wchar_t t[400];
    if (GetEnvironmentVariableW(L"CH_STATS_HOOK", t, 400)) return t;
#endif
    return STATS_WEBHOOK_URL[0] ? STATS_WEBHOOK_URL : BUG_WEBHOOK_URL;
}
static int day_number(void) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return (int)((((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 864000000000ull);
}

static void stats_maybe_send(int force);

/* Tester: "this build looks good" check-in (no ping, once per version). */
static DWORD WINAPI tester_ok_thread(LPVOID p) {
    (void)p;
    char u[600], payload[1000], id[24];
    wchar_t wv[96], line[400];
    win_version(wv);
    to_utf8(g_report_id, id, sizeof(id));
    wsprintfW(line, L"\x2705 **Tester: looks good** \x2022 v" VER_WSTR L" \x2022 %s \x2022 id `%S`", wv, id);
    to_utf8(line, u, sizeof(u));
    strcpy(payload, "{\"username\":\"Crosshair Overlay reports\",\"content\":\"");
    json_cat(payload, sizeof(payload) - 60, u);
    strcat(payload, "\",\"allowed_mentions\":{\"parse\":[]}}");
    int ok = webhook_post_json(report_target(), payload);
    log_info(ok ? L"tester: sent \"looks good\" for v" VER_WSTR : L"tester: couldn't send \"looks good\"");
    if (g_host) PostMessageW(g_host, WM_TESTER_OK, ok, 0);
    return 0;
}

/* Background check (+ download). manual = from the "Check now" button. Testers also get pre-releases. */
static DWORD WINAPI update_thread(LPVOID p) {
    int manual = (int)(INT_PTR)p, state = UPD_FAILED;
    wchar_t url[600], dl[1100], dir[MAX_PATH], part[MAX_PATH + 8], fin[MAX_PATH];
    char *js = NULL, *bin = NULL, tag[64] = "", dlurl[1024], digest[96] = "";
    const char *rs = NULL, *re = NULL; /* the chosen release's part of the answer */
    DWORD jl = 0, bl = 0;
    long long size = -1;
    InterlockedExchange(&g_upd_state, UPD_CHECKING);
    if (g_host) PostMessageW(g_host, WM_UPDATE_DONE, (WPARAM)manual, UPD_CHECKING);
    int tester = tester_check() && g_tbuilds;
    news_check();
    stats_maybe_send(0);
    if (tester) wsprintfW(url, L"%s/repos/%s/releases?per_page=20", gh_api(), UPDATE_REPO);
    else wsprintfW(url, L"%s/repos/%s/releases/latest", gh_api(), UPDATE_REPO);
    int st = https_req(L"GET", url, L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n",
                       NULL, 0, &js, &jl, 8 * 1024 * 1024);
    if (st != 200 || !js) { log_warn(L"update check failed (HTTP %d)", st); goto done; }
    if (!tester) {
        if (!jstr(jkey(js, NULL, "tag_name"), tag, sizeof(tag))) { log_warn(L"update check: no release found"); goto done; }
        rs = js;
    } else { /* newest version among all releases, test builds included */
        int best[3] = {0, 0, 0};
        for (const char *k = js; (k = jkey(k, NULL, "tag_name"));) {
            char t[64];
            int v[3];
            const char *next = strstr(k, "\"tag_name\"");
            if (jstr(k, t, sizeof(t)) && ver_parse(t, v) && (!tag[0] || ver_cmp(v, best) > 0)) {
                memcpy(best, v, sizeof(v));
                strcpy(tag, t);
                rs = k;
                re = next;
            }
            if (!next) break;
            k = next;
        }
        if (!tag[0]) { log_warn(L"update check: no release found"); goto done; }
    }
    if (!ver_newer_than_me(tag)) { state = UPD_LATEST; goto done; }
    {
        int a[3], b[3];
        char sk[32];
        WideCharToMultiByte(CP_UTF8, 0, g_skip_ver, -1, sk, sizeof(sk), NULL, NULL);
        if (sk[0] && ver_parse(sk, b) && ver_parse(tag, a) && !ver_cmp(a, b)) {
            log_info(L"update: skipping %S (it was rolled back on this PC)", tag);
            state = UPD_LATEST;
            goto done;
        }
    }
    { /* the release's .exe download (and its size / sha256, when GitHub gives them) */
        const char *prev = rs, *u;
        dlurl[0] = 0;
        while ((u = jkey(prev, re, "browser_download_url"))) {
            char tmp[1024];
            if (jstr(u, tmp, sizeof(tmp))) {
                size_t L = strlen(tmp);
                if (L > 4 && !_stricmp(tmp + L - 4, ".exe")) {
                    strcpy(dlurl, tmp);
                    const char *d = jkey(prev, u, "digest"), *z = jkey(prev, u, "size");
                    if (d) jstr(d, digest, sizeof(digest));
                    if (z) size = strtoll(z, NULL, 10);
                    break;
                }
            }
            prev = u + 1;
        }
    }
    if (!dlurl[0]) { log_warn(L"update: release %S has no .exe", tag); goto done; }
    MultiByteToWideChar(CP_UTF8, 0, dlurl, -1, dl, 1100);
    st = https_req(L"GET", dl, L"Accept: application/octet-stream\r\n", NULL, 0, &bin, &bl, 64 * 1024 * 1024);
    if (st != 200 || !bin || !is_exe_file((BYTE *)bin, bl) || (size > 0 && (long long)bl != size)) {
        log_warn(L"update: download of %S failed or isn't a program (HTTP %d, %lu bytes)", tag, st, bl);
        goto done;
    }
    if (!strncmp(digest, "sha256:", 7)) {
        char hx[65];
        if (!sha256_hex((BYTE *)bin, bl, hx) || _stricmp(hx, digest + 7)) {
            log_error(L"update: %S failed its SHA-256 check - not installed", tag);
            goto done;
        }
    }
    {
        int v[3];
        ver_parse(tag, v);
        upd_dir(dir);
        CreateDirectoryW(dir, NULL);
        wsprintfW(fin, L"%s\\CrosshairOverlay-v%d.%d.%d.exe", dir, v[0], v[1], v[2]);
        wsprintfW(part, L"%s.part", fin);
        HANDLE h = CreateFileW(part, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        DWORD w = 0;
        int ok = h != INVALID_HANDLE_VALUE && WriteFile(h, bin, bl, &w, NULL) && w == bl;
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        if (!ok || !MoveFileExW(part, fin, MOVEFILE_REPLACE_EXISTING)) { DeleteFileW(part); log_warn(L"update: couldn't save the download"); goto done; }
        wcscpy(g_upd_file, fin);
        wsprintfW(g_upd_ver, L"%d.%d.%d", v[0], v[1], v[2]);
        log_info(L"update: v%s downloaded and checked, ready to install%s", g_upd_ver, tester ? L" (tester build)" : L"");
        state = UPD_READY;
    }
done:
    free(js);
    free(bin);
    InterlockedExchange(&g_upd_state, state);
    InterlockedExchange(&g_upd_busy, 0);
    if (g_host) PostMessageW(g_host, WM_UPDATE_DONE, (WPARAM)manual, state);
    return 0;
}
static void update_check(int manual) {
    if (!updates_set() || g_upd_state == UPD_READY) return;
    if (InterlockedCompareExchange(&g_upd_busy, 1, 0)) return;
    HANDLE th = CreateThread(NULL, 0, update_thread, (LPVOID)(INT_PTR)manual, 0, NULL);
    if (th) { SetThreadPriority(th, THREAD_PRIORITY_BELOW_NORMAL); CloseHandle(th); }
    else InterlockedExchange(&g_upd_busy, 0);
}
/* ---------------- older copies ---------------- */
/* Players often download a new version while the old one is still running in the tray: the new one
   used to just say "already running". Now a newer version asks an older running copy to close and
   takes over, then tells the player where the old file is. */
static wchar_t g_replaced_exe[MAX_PATH]; /* the older copy we took over from */
static int g_test_yes; /* test builds: skip confirmation boxes */

/* Version of a Crosshair Overlay exe. 0 = not one. */
static int co_exe_version(const wchar_t *path, int v[3]) {
    DWORD dummy, n = GetFileVersionInfoSizeW(path, &dummy);
    int ok = 0;
    void *vi = n ? malloc(n) : NULL;
    if (vi && GetFileVersionInfoW(path, 0, n, vi)) {
        wchar_t *pn = NULL;
        UINT l = 0;
        VS_FIXEDFILEINFO *f = NULL;
        if (VerQueryValueW(vi, L"\\StringFileInfo\\040904B0\\ProductName", (void **)&pn, &l) && l && pn && wcsstr(pn, L"Crosshair Overlay") &&
            VerQueryValueW(vi, L"\\", (void **)&f, &l) && l && f) {
            v[0] = HIWORD(f->dwFileVersionMS); v[1] = LOWORD(f->dwFileVersionMS); v[2] = HIWORD(f->dwFileVersionLS);
            ok = 1;
        }
    }
    free(vi);
    return ok;
}

/* Another copy is running. If it's an older version, close it and carry on. 1 = took over. */
static int take_over_older(void) {
    HWND hw = FindWindowW(L"CrosshairOverlayHost", NULL);
    DWORD pid = 0;
    if (!hw || !GetWindowThreadProcessId(hw, &pid) || !pid) return 0;
    HANDLE p = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return 0;
    wchar_t img[MAX_PATH];
    DWORD l = MAX_PATH;
    int v[3], me[3] = {VER_MAJOR, VER_MINOR, VER_PATCH}, ok = 0;
    if (QueryFullProcessImageNameW(p, 0, img, &l) && co_exe_version(img, v) && ver_cmp(v, me) < 0) {
        wcscpy(g_replaced_exe, img);
        PostMessageW(hw, WM_CLOSE, 0, 0); /* it puts its cursor / Roblox changes back and exits */
        ok = WaitForSingleObject(p, 8000) == WAIT_OBJECT_0;
    }
    CloseHandle(p);
    return ok;
}

/* After taking over from an older copy: tell the player where its file is, so they can delete it.
   (The app doesn't go through other folders or delete files itself.) */
static void old_copy_notice(void) {
    wchar_t self[MAX_PATH];
    if (!g_replaced_exe[0]) return;
    GetModuleFileNameW(NULL, self, MAX_PATH);
    if (!_wcsicmp(self, g_replaced_exe)) return;
    const wchar_t *a = g_replaced_exe, *sl1 = wcsrchr(a, L'\\'), *sl2 = sl1;
    while (sl2 && sl2 > a && *(sl2 - 1) != L'\\') sl2--; /* "Folder\file.exe" */
    g_nid.uFlags = NIF_INFO;
    wcscpy(g_nid.szInfoTitle, L"Closed the older version");
    _snwprintf(g_nid.szInfo, 255, L"You're on v" VER_WSTR L" now and it updates itself. You can delete the old file: %s", sl2 ? sl2 : a);
    g_nid.szInfo[255] = 0;
    g_nid.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    g_nid.szInfo[0] = 0;
}


/* Lines of notes -> "<prefix>line\n..." (blank lines and leading "-", "*" or bullets dropped). */
static void notes_format(const char *notes, const char *prefix, char *out, int cap) {
    int n = 0;
    out[0] = 0;
    for (const char *p = notes; *p;) {
        const char *e = p;
        while (*e && *e != '\n') e++;
        const char *a = p, *b = e;
        while (a < b && (*a == ' ' || *a == '\t' || *a == '-' || *a == '*')) a++;
        if (b - a >= 3 && (BYTE)a[0] == 0xE2 && (BYTE)a[1] == 0x80 && (BYTE)a[2] == 0xA2) { a += 3; while (a < b && *a == ' ') a++; }
        while (b > a && (b[-1] == '\r' || b[-1] == ' ' || b[-1] == '\t')) b--;
        if (b > a && n + (int)strlen(prefix) + (int)(b - a) + 2 < cap) {
            n += snprintf(out + n, cap - n, "%s%.*s\n", prefix, (int)(b - a), a);
        }
        p = *e ? e + 1 : e;
    }
    if (n && out[n - 1] == '\n') out[--n] = 0;
}

#ifdef PUBLISHER_APP
/* ---------------- publishing a release (developer only) ---------------- */
/* Posts a Crosshair Overlay exe (this one or any other version you pick) to:
   - GitHub (UPDATE_REPO): a release with the exe, which is what players' apps update from;
   - Discord: the release channel, with the notes and the exe, pinging RELEASE_PING_ROLE_ID.
   The Discord webhook and GitHub token are only ever stored on the developer's PC, encrypted
   for their Windows account (%APPDATA%\CrosshairOverlay-Publisher), never in the exe, so
   players can't publish. */
static void pub_path(const wchar_t *name, wchar_t *out) {
    DWORD n = GetEnvironmentVariableW(L"APPDATA", out, MAX_PATH);
    if (!n || n > MAX_PATH - 60) { out[0] = 0; return; }
    wcscat(out, L"\\CrosshairOverlay-Publisher");
    if (name) { CreateDirectoryW(out, NULL); wcscat(out, L"\\"); wcscat(out, name); }
}
static int secret_load(const wchar_t *name, char *out, int cap) {
    wchar_t p[MAX_PATH];
    DWORD len = 0;
    out[0] = 0;
    pub_path(name, p);
    char *b = p[0] ? read_small(p, &len) : NULL;
    if (!b) return 0;
    DATA_BLOB in = {len, (BYTE *)b}, res = {0, NULL};
    int ok = CryptUnprotectData(&in, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &res) && res.cbData < (DWORD)cap;
    if (ok) { memcpy(out, res.pbData, res.cbData); out[res.cbData] = 0; }
    if (res.pbData) { SecureZeroMemory(res.pbData, res.cbData); LocalFree(res.pbData); }
    free(b);
    return ok;
}
static int secret_save(const wchar_t *name, const char *val) {
    wchar_t p[MAX_PATH];
    pub_path(name, p);
    if (!p[0]) return 0;
    DATA_BLOB in = {(DWORD)strlen(val), (BYTE *)val}, res = {0, NULL};
    if (!CryptProtectData(&in, L"Crosshair Overlay publisher", NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &res)) return 0;
    int ok = write_small(p, (const char *)res.pbData, res.cbData);
    LocalFree(res.pbData);
    return ok;
}
#define SECRET_HOOK L"release-webhook.dat"
#define SECRET_GH L"github-token.dat"
static int publisher_here(void) { wchar_t p[MAX_PATH]; pub_path(SECRET_HOOK, p); return p[0] && file_exists(p); }

/* Version and built-in notes of any Crosshair Overlay exe (notes = resource 12, from v1.11.0 on). */
static int exe_info(const wchar_t *exe, char *ver, int vcap, char *notes, int ncap) {
    DWORD dummy, n = GetFileVersionInfoSizeW(exe, &dummy);
    ver[0] = 0;
    notes[0] = 0;
    if (n) {
        void *vi = malloc(n);
        VS_FIXEDFILEINFO *ffi;
        UINT l;
        if (vi && GetFileVersionInfoW(exe, 0, n, vi) && VerQueryValueW(vi, L"\\", (void **)&ffi, &l) && l)
            snprintf(ver, vcap, "%lu.%lu.%lu", (unsigned long)HIWORD(ffi->dwFileVersionMS),
                     (unsigned long)LOWORD(ffi->dwFileVersionMS), (unsigned long)HIWORD(ffi->dwFileVersionLS));
        free(vi);
    }
    HMODULE m = LoadLibraryExW(exe, NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (m) {
        HRSRC r = FindResourceW(m, MAKEINTRESOURCEW(12), (LPCWSTR)RT_RCDATA);
        HGLOBAL g = r ? LoadResource(m, r) : NULL;
        DWORD sz = r ? SizeofResource(m, r) : 0;
        const char *d = g ? (const char *)LockResource(g) : NULL;
        if (d && sz) {
            if (sz >= 3 && (BYTE)d[0] == 0xEF && (BYTE)d[1] == 0xBB && (BYTE)d[2] == 0xBF) { d += 3; sz -= 3; }
            if (sz > (DWORD)ncap - 1) sz = ncap - 1;
            memcpy(notes, d, sz);
            notes[sz] = 0;
        }
        FreeLibrary(m);
    }
    if (!notes[0]) { /* older exe: release-notes.txt next to it */
        wchar_t f[MAX_PATH];
        wcscpy(f, exe);
        wchar_t *sl = wcsrchr(f, L'\\');
        if (sl && wcslen(f) < MAX_PATH - 20) {
            wcscpy(sl + 1, L"release-notes.txt");
            DWORD len = 0;
            char *b = read_small(f, &len);
            if (b) {
                char *t = b;
                if (len >= 3 && (BYTE)t[0] == 0xEF && (BYTE)t[1] == 0xBB && (BYTE)t[2] == 0xBF) t += 3;
                snprintf(notes, ncap, "%s", t);
                free(b);
            }
        }
    }
    return ver[0] != 0;
}

static int published_before(const char *ver, int testers) {
    wchar_t p[MAX_PATH];
    char want[48];
    pub_path(L"published.txt", p);
    DWORD len = 0;
    char *b = p[0] ? read_small(p, &len) : NULL;
    if (!b) return 0;
    snprintf(want, sizeof(want), "v%s ", ver);
    int found = 0;
    for (char *l = b; l && *l;) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        if (!strncmp(l, want, strlen(want)) && (strstr(l, "  testers") != NULL) == (testers != 0)) { found = 1; break; }
        l = e ? e + 1 : NULL;
    }
    free(b);
    return found;
}

/* The highest version in published.txt (either channel), as "1.15.0". 0 = nothing published yet. */
static int last_published(char *out, int cap) {
    wchar_t p[MAX_PATH];
    DWORD len = 0;
    int best[3] = {-1, -1, -1};
    out[0] = 0;
    pub_path(L"published.txt", p);
    char *b = p[0] ? read_small(p, &len) : NULL;
    if (!b) return 0;
    for (char *l = b; l && *l;) {
        char *e = strchr(l, '\n');
        int a = 0, c = 0, d = 0;
        if (sscanf(l, "v%d.%d.%d", &a, &c, &d) == 3 &&
            (a > best[0] || (a == best[0] && (c > best[1] || (c == best[1] && d > best[2]))))) { best[0] = a; best[1] = c; best[2] = d; }
        l = e ? e + 1 : NULL;
    }
    free(b);
    if (best[0] < 0) return 0;
    snprintf(out, cap, "%d.%d.%d", best[0], best[1], best[2]);
    return 1;
}

typedef struct { wchar_t exe[MAX_PATH]; char ver[32]; char notes[4000]; HWND notify; int chan, ping; } PubJob; /* chan 1 = testers only */
static wchar_t g_pub_msg[400];
static volatile LONG g_pub_busy;
static void pub_say(PubJob *j, int done, const wchar_t *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    wvsprintfW(g_pub_msg, fmt, ap);
    va_end(ap);
    if (j->notify && IsWindow(j->notify)) PostMessageW(j->notify, WM_PUBLISH_STEP, (WPARAM)done, 0);
}

/* Push the source code built into this publisher (resource 14) to the repo as one commit, so the
   release's "Source code" downloads hold the real source. 1 = commit sha in out, 0 = skipped/failed (msg says why). */
static int gh_api_json(const wchar_t *method, const wchar_t *path, const wchar_t *hdr, const char *body, char **js) {
    wchar_t url[700];
    *js = NULL;
    wsprintfW(url, L"%s/repos/%s%s", gh_api(), UPDATE_REPO, path);
    return https_req(method, url, hdr, body, body ? (DWORD)strlen(body) : 0, js, NULL, 4 * 1024 * 1024);
}
static int gh_push_source(PubJob *j, const wchar_t *hdr, char out[64], wchar_t *msg) {
    HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(14), (LPCWSTR)RT_RCDATA);
    HGLOBAL g = r ? LoadResource(NULL, r) : NULL;
    DWORD sz = r ? SizeofResource(NULL, r) : 0;
    const BYTE *d = g ? (const BYTE *)LockResource(g) : NULL;
    char *js = NULL, branch[100] = "", base[64] = "", tree[64] = "", ntree[64] = "", path[200];
    wchar_t wp[300];
    int ok = 0, st;
    out[0] = 0;
    msg[0] = 0;
    if (!d || sz < 8) { wcscpy(msg, L"no source in this publisher"); return 0; }
    if (strcmp(j->ver, VER_STR)) { wsprintfW(msg, L"source not uploaded: this publisher has the v" VER_WSTR L" source, the exe is v%S", j->ver); return 0; }
    pub_say(j, 0, L"Uploading the v%S source code to GitHub...", j->ver);
    st = gh_api_json(L"GET", L"", hdr, NULL, &js);
    if (st != 200 || !js || !jstr(jkey(js, NULL, "default_branch"), branch, sizeof(branch))) { wsprintfW(msg, L"couldn't read the repo (HTTP %d)", st); goto out; }
    free(js);
    wsprintfW(wp, L"/git/ref/heads/%S", branch);
    st = gh_api_json(L"GET", wp, hdr, NULL, &js);
    if (st != 200 || !js || !jstr(jkey(js, NULL, "sha"), base, sizeof(base))) { wsprintfW(msg, L"couldn't read branch %S (HTTP %d)", branch, st); goto out; }
    free(js);
    wsprintfW(wp, L"/git/commits/%S", base);
    st = gh_api_json(L"GET", wp, hdr, NULL, &js);
    const char *tk = js ? strstr(js, "\"tree\"") : NULL;
    if (st != 200 || !tk || !jstr(jkey(tk, NULL, "sha"), tree, sizeof(tree))) { wsprintfW(msg, L"couldn't read the last commit (HTTP %d)", st); goto out; }
    free(js);
    js = NULL;
    { /* one blob per file, then a tree on top of the current one (testers.txt / news.txt stay as they are) */
        size_t tcap = 64 * 1024, tl = 0;
        char *tb = (char *)malloc(tcap);
        if (!tb) goto out;
        tl = snprintf(tb, tcap, "{\"base_tree\":\"%s\",\"tree\":[", tree);
        int nf = 0;
        for (DWORD o = 0; o + 8 <= sz;) {
            DWORD nl, dl;
            memcpy(&nl, d + o, 4); o += 4;
            if (nl >= sizeof(path) || o + nl + 4 > sz) break;
            memcpy(path, d + o, nl); path[nl] = 0; o += nl;
            memcpy(&dl, d + o, 4); o += 4;
            if (o + dl > sz) break;
            DWORD bl = 0;
            CryptBinaryToStringA(d + o, dl, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &bl);
            char *body = (char *)malloc(bl + 100), *bj = NULL, bsha[64];
            if (!body) { free(tb); goto out; }
            int n0 = sprintf(body, "{\"encoding\":\"base64\",\"content\":\"");
            DWORD bl2 = bl;
            if (!dl) body[n0] = 0;
            else if (!CryptBinaryToStringA(d + o, dl, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, body + n0, &bl2)) { free(body); free(tb); goto out; }
            strcat(body, "\"}");
            o += dl;
            st = gh_api_json(L"POST", L"/git/blobs", hdr, body, &bj);
            free(body);
            if (st != 201 || !bj || !jstr(jkey(bj, NULL, "sha"), bsha, sizeof(bsha))) {
                free(bj); free(tb);
                wsprintfW(msg, L"GitHub didn't take %S (HTTP %d)", path, st);
                goto out;
            }
            free(bj);
            if (tl + 300 > tcap) break;
            tl += snprintf(tb + tl, tcap - tl, "%s{\"path\":\"%s\",\"mode\":\"%s\",\"type\":\"blob\",\"sha\":\"%s\"}", nf ? "," : "",
                           path, strstr(path, ".sh") || strstr(path, ".py") ? "100755" : "100644", bsha);
            nf++;
        }
        snprintf(tb + tl, tcap - tl, "]}");
        st = gh_api_json(L"POST", L"/git/trees", hdr, tb, &js);
        free(tb);
        if (st != 201 || !js || !jstr(jkey(js, NULL, "sha"), ntree, sizeof(ntree))) { wsprintfW(msg, L"couldn't build the file list (HTTP %d)", st); goto out; }
        free(js);
        js = NULL;
    }
    if (!strcmp(ntree, tree)) { strcpy(out, base); ok = 1; goto out; } /* already up to date */
    {
        char cb[400];
        snprintf(cb, sizeof(cb), "{\"message\":\"Crosshair Overlay v%s source\",\"tree\":\"%s\",\"parents\":[\"%s\"]}", j->ver, ntree, base);
        st = gh_api_json(L"POST", L"/git/commits", hdr, cb, &js);
        char nc[64];
        if (st != 201 || !js || !jstr(jkey(js, NULL, "sha"), nc, sizeof(nc))) { wsprintfW(msg, L"couldn't make the commit (HTTP %d)", st); goto out; }
        free(js);
        js = NULL;
        snprintf(cb, sizeof(cb), "{\"sha\":\"%s\",\"force\":false}", nc);
        wsprintfW(wp, L"/git/refs/heads/%S", branch);
        st = gh_api_json(L"PATCH", wp, hdr, cb, &js);
        if (st != 200) { wsprintfW(msg, L"couldn't update branch %S (HTTP %d)", branch, st); goto out; }
        strcpy(out, nc);
        ok = 1;
    }
out:
    free(js);
    if (ok) log_info(L"publish: source v%S pushed (commit %.7S)", j->ver, out);
    else log_warn(L"publish: %s", msg);
    return ok;
}

static DWORD WINAPI publish_thread(LPVOID p) {
    PubJob *j = (PubJob *)p;
    char hook[600], tok[300], fname[80], hx[65] = "";
    wchar_t whook[600], wtok[300], url[700], hdr[800];
    DWORD flen = 0;
    char *file = read_small(j->exe, &flen);
    int gh_ok = -1, dc_ok = 0, promoted = 0, testers = j->chan == 1;
    char src_commit[64] = "", repo8[120];
    wchar_t src_note[300] = L"";
    WideCharToMultiByte(CP_UTF8, 0, UPDATE_REPO, -1, repo8, sizeof(repo8), NULL, NULL);
    snprintf(fname, sizeof(fname), "CrosshairOverlay.exe"); /* simple name; the version is inside the exe */
    if (!file || !is_exe_file((BYTE *)file, flen)) { pub_say(j, 2, L"Couldn't read that exe."); goto out; }
    sha256_hex((BYTE *)file, flen, hx);
    if (!testers) {
        if (!secret_load(SECRET_HOOK, hook, sizeof(hook))) { pub_say(j, 2, L"No release webhook saved on this PC."); goto out; }
        MultiByteToWideChar(CP_UTF8, 0, hook, -1, whook, 600);
    }

    /* 1. GitHub release (what players' apps update from). Testers only = a pre-release. */
    if (updates_set() && secret_load(SECRET_GH, tok, sizeof(tok))) {
        char *body = (char *)malloc(12000), *js = NULL, up[1200] = "", gnotes[5000];
        DWORD jl = 0;
        gh_ok = 0;
        MultiByteToWideChar(CP_UTF8, 0, tok, -1, wtok, 300);
        SecureZeroMemory(tok, sizeof(tok));
        wsprintfW(hdr, L"Authorization: Bearer %s\r\nAccept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\nContent-Type: application/json\r\n", wtok);
        notes_format(j->notes, "- ", gnotes, sizeof(gnotes));
        { /* the source for this version goes in first, so the release's tag points at it */
            char commit[64];
            wchar_t smsg[300];
            if (gh_push_source(j, hdr, commit, smsg)) snprintf(src_commit, sizeof(src_commit), "%s", commit);
            else wcscpy(src_note, smsg);
        }
        pub_say(j, 0, testers ? L"Creating the GitHub pre-release v%S for testers..." : L"Creating the GitHub release v%S...", j->ver);
        snprintf(body, 12000, "{\"tag_name\":\"v%s\",%s%s%s\"name\":\"Crosshair Overlay v%s%s\",\"prerelease\":%s,\"make_latest\":\"%s\",\"body\":\"",
                 j->ver, src_commit[0] ? "\"target_commitish\":\"" : "", src_commit, src_commit[0] ? "\"," : "",
                 j->ver, testers ? " (test build)" : "", testers ? "true" : "false", testers ? "false" : "true");
        {
            char more[700];
            snprintf(more, sizeof(more), "%s\n\n**Download:** CrosshairOverlay.exe below (the app also updates itself).\n"
                                         "**Read more:** [everything that changed](https://github.com/%s/blob/v%s/changelog.txt)%s",
                     gnotes[0] ? "" : "Bug fixes and improvements.", repo8, j->ver,
                     src_commit[0] ? " \xE2\x80\xA2 the full source for this version is in \"Source code\" below." : "");
            json_cat(body, 11900, gnotes);
            json_cat(body, 11900, more);
        }
        strcat(body, "\"}");
        wsprintfW(url, L"%s/repos/%s/releases", gh_api(), UPDATE_REPO);
        int st = https_req(L"POST", url, hdr, body, (DWORD)strlen(body), &js, &jl, 1024 * 1024);
        int have_asset = 0;
        if (st == 422) { /* that version is already on GitHub: reuse it (and promote a test build to everyone) */
            free(js); js = NULL;
            wsprintfW(url, L"%s/repos/%s/releases/tags/v%S", gh_api(), UPDATE_REPO, j->ver);
            st = https_req(L"GET", url, hdr, NULL, 0, &js, &jl, 4 * 1024 * 1024);
            if (st == 200 && js) {
                char nm[100];
                snprintf(nm, sizeof(nm), "\"%s\"", fname);
                have_asset = strstr(js, nm) != NULL;
                const char *pr = jkey(js, NULL, "prerelease"), *id = jkey(js, NULL, "id");
                int was_pre = pr && !strncmp(pr, "true", 4);
                if (id && was_pre != testers) {
                    char pb[200], *r3 = NULL;
                    wsprintfW(url, L"%s/repos/%s/releases/%I64u", gh_api(), UPDATE_REPO, (unsigned long long)_strtoui64(id, NULL, 10));
                    snprintf(pb, sizeof(pb), "{\"prerelease\":%s,\"make_latest\":\"%s\",\"name\":\"Crosshair Overlay v%s%s\"}",
                             testers ? "true" : "false", testers ? "false" : "true", j->ver, testers ? " (test build)" : "");
                    pub_say(j, 0, testers ? L"Moving v%S back to testers only..." : L"Releasing the test build v%S to everyone...", j->ver);
                    int s3 = https_req(L"PATCH", url, hdr, pb, (DWORD)strlen(pb), &r3, NULL, 1024 * 1024);
                    free(r3);
                    if (s3 == 200) promoted = !testers;
                    else log_warn(L"publish: couldn't change the release (HTTP %d)", s3);
                }
                st = 201;
            }
        }
        if ((st == 201 || st == 200) && js && jstr(jkey(js, NULL, "upload_url"), up, sizeof(up))) {
            char *br = strchr(up, '{');
            if (br) *br = 0;
            if (have_asset) gh_ok = 1;
            else {
                wchar_t wup[1300];
                pub_say(j, 0, L"Uploading %S to GitHub...", fname);
                MultiByteToWideChar(CP_UTF8, 0, up, -1, wup, 1200);
                wsprintfW(url, L"%s?name=%S", wup, fname);
                wsprintfW(hdr, L"Authorization: Bearer %s\r\nAccept: application/vnd.github+json\r\nContent-Type: application/octet-stream\r\n", wtok);
                char *r2 = NULL;
                int s2 = https_req(L"POST", url, hdr, file, flen, &r2, NULL, 1024 * 1024);
                free(r2);
                gh_ok = s2 == 201;
                if (!gh_ok) log_warn(L"publish: GitHub upload failed (HTTP %d)", s2);
            }
        } else {
            log_warn(L"publish: GitHub release failed (HTTP %d)", st);
            pub_say(j, 0, st == 401 ? L"GitHub: the token was refused. Paste a new one." :
                          st == 404 ? L"GitHub: repo not found, or the token can't write to it." :
                                      L"GitHub: couldn't create the release (HTTP %d).", st);
            Sleep(1500);
        }
        SecureZeroMemory(wtok, sizeof(wtok));
        SecureZeroMemory(hdr, sizeof(hdr));
        free(js);
        free(body);
    }

    /* 2. Discord (public releases only; test builds stay quiet) */
    if (!testers) {
        char *payload = (char *)malloc(16000), dnotes[4200], content[64] = "";
        pub_say(j, 0, L"Posting v%S to Discord...", j->ver);
        notes_format(j->notes, "\xE2\x80\xA2 ", dnotes, 3900);
        if (!dnotes[0]) strcpy(dnotes, "Bug fixes and improvements.");
        int ping = j->ping && RELEASE_PING_ROLE_ID[0];
        if (ping) snprintf(content, sizeof(content), "<@&%s>", RELEASE_PING_ROLE_ID);
        snprintf(payload, 16000, "{\"username\":\"Crosshair Overlay\",\"content\":\"%s\",\"allowed_mentions\":{\"parse\":[],\"roles\":[%s%s%s]},"
                                 "\"embeds\":[{\"title\":\"Crosshair Overlay v%s\",\"color\":14684202,%s%s%s%s%s\"description\":\"",
                 content, ping ? "\"" : "", ping ? RELEASE_PING_ROLE_ID : "", ping ? "\"" : "", j->ver,
                 gh_ok == 1 ? "\"url\":\"https://github.com/" : "", gh_ok == 1 ? repo8 : "", gh_ok == 1 ? "/releases/tag/v" : "",
                 gh_ok == 1 ? j->ver : "", gh_ok == 1 ? "\"," : "");
        json_cat(payload, 15900, dnotes);
        if (gh_ok == 1) {
            char more[400];
            snprintf(more, sizeof(more), "\n\n[Read more & download](https://github.com/%s/releases/tag/v%s)", repo8, j->ver);
            json_cat(payload, 15900, more);
        }
        strcat(payload, "\"}]}");
        dc_ok = webhook_post_to(whook, file, flen, fname, "application/octet-stream", payload);
        free(payload);
        SecureZeroMemory(hook, sizeof(hook));
        SecureZeroMemory(whook, sizeof(whook));
    }
    if ((testers && gh_ok == 1) || (!testers && dc_ok)) {
        wchar_t lp[MAX_PATH], line[220];
        SYSTEMTIME t;
        GetLocalTime(&t);
        pub_path(L"published.txt", lp);
        wsprintfW(line, L"v%S  %04d-%02d-%02d %02d:%02d  sha256 %S%s%s\r\n", j->ver, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, hx,
                  gh_ok == 1 ? L"  github" : L"", testers ? L"  testers" : L"");
        if (lp[0]) append_utf8(lp, line);
        log_info(L"published v%S (%s, discord %d, github %d)", j->ver, testers ? L"testers" : L"everyone", dc_ok, gh_ok);
    }
    if (testers) {
        if (gh_ok == 1) pub_say(j, 2, L"v%S is out to your testers (GitHub pre-release). Their apps update within a few hours, or right away from Updates > Check now.", j->ver);
        else if (gh_ok == -1) pub_say(j, 2, L"Test builds go through GitHub \x2014 paste your GitHub token first.");
        else pub_say(j, 2, L"GitHub didn't take the test build. Check the token and try again.");
    } else if (dc_ok && gh_ok != 0)
        pub_say(j, 2, gh_ok != 1 ? L"Published v%S to Discord. (Auto-updates aren't set up, so apps won't update to it.)"
                      : promoted ? L"Released the test build v%S to everyone: posted to Discord and players' apps will update to it."
                                 : src_note[0] ? L"Published v%S to Discord and GitHub. Players' apps will update to it. (Source code: see log.txt)"
                                 : L"Published v%S to Discord and GitHub with its source code. Players' apps will update to it.", j->ver);
    else if (dc_ok) pub_say(j, 2, L"Posted to Discord, but GitHub failed \x2014 players' apps won't update to v%S yet. Click Publish again to retry.", j->ver);
    else pub_say(j, 2, L"Discord didn't accept the post. Check the release webhook (paste it again) and your internet.");
out:
    free(file);
    InterlockedExchange(&g_pub_busy, 0);
    free(j);
    return 0;
}
#endif /* PUBLISHER_APP */

/* ---------------- profiles: 3 saved setups ---------------- */
typedef struct {
    int saved, style, color, size, thick, outline, opacity, cover, cover_color, glow, anim, anim_speed, click_fx, custom,
        l2style1, l2color, l2size; /* layer 2 (l2style1 = style + 1, 0 = no layer) */
} Profile;
#define NPROF 3
#define NPFIELD 17
static Profile g_prof[NPROF];
static const wchar_t *PROF_FIELDS[NPFIELD] = {L"saved", L"style", L"color", L"size", L"thick", L"outline", L"opacity",
                                             L"cover", L"covercol", L"glow", L"effect", L"speed", L"click", L"custom",
                                             L"l2style", L"l2color", L"l2size"};
static void load_profiles(void) {
    wchar_t k[40];
    for (int p = 0; p < NPROF; p++) {
        int *v = (int *)&g_prof[p];
        for (int f = 0; f < NPFIELD; f++) {
            wsprintfW(k, L"p%d_%s", p + 1, PROF_FIELDS[f]);
            v[f] = ini_get(k, 0);
        }
        Profile *q = &g_prof[p];
        q->style = style_fix(q->style); q->color = clampi(q->color, 0, NCOLORS - 1);
        q->size = clampi(q->size, 0, NSIZES - 1); q->thick = clampi(q->thick, 1, 6);
        q->opacity = clampi(q->opacity, 0, NOPAC - 1); q->cover = clampi(q->cover, 0, NCOVER - 1);
        q->cover_color = clampi(q->cover_color, 0, NCOVERCOL - 1); q->anim = clampi(q->anim, 0, 2);
        q->anim_speed = clampi(q->anim_speed, 0, 2);
        q->l2style1 = q->l2style1 > 0 ? style_fix(q->l2style1 - 1) + 1 : 0;
        q->l2color = clampi(q->l2color, 0, NCOLORS - 1); q->l2size = clampi(q->l2size, 0, NSIZES - 1);
    }
}
static void save_profile(int p) {
    Profile *q = &g_prof[p];
    wchar_t k[40];
    q->saved = 1; q->style = S.style; q->color = S.color; q->size = S.size; q->thick = S.thick;
    q->outline = S.outline; q->opacity = S.opacity; q->cover = S.cover; q->cover_color = S.cover_color;
    q->glow = S.glow; q->anim = S.anim; q->anim_speed = S.anim_speed; q->click_fx = S.click_fx;
    q->custom = (COLORS[CUSTOM_COLOR].r << 16) | (COLORS[CUSTOM_COLOR].g << 8) | COLORS[CUSTOM_COLOR].b;
    q->l2style1 = S.l2_style + 1; q->l2color = S.l2_color; q->l2size = S.l2_size;
    int *v = (int *)q;
    for (int f = 0; f < NPFIELD; f++) {
        wsprintfW(k, L"p%d_%s", p + 1, PROF_FIELDS[f]);
        ini_set(k, v[f]);
    }
}
static int load_profile(int p) {
    Profile *q = &g_prof[p];
    if (!q->saved) return 0;
    S.style = q->style; S.color = q->color; S.size = q->size; S.thick = q->thick; S.outline = q->outline;
    S.opacity = q->opacity; S.cover = q->cover; S.cover_color = q->cover_color; S.glow = q->glow;
    S.anim = q->anim; S.anim_speed = q->anim_speed; S.click_fx = q->click_fx;
    S.l2_style = q->l2style1 - 1; S.l2_color = q->l2color; S.l2_size = q->l2size;
    if (S.cover) g_cover_last = S.cover;
    if (q->color == CUSTOM_COLOR) {
        COLORS[CUSTOM_COLOR].r = (BYTE)(q->custom >> 16); COLORS[CUSTOM_COLOR].g = (BYTE)(q->custom >> 8);
        COLORS[CUSTOM_COLOR].b = (BYTE)q->custom;
    }
    return 1;
}
static int profile_in_use(int p) {
    Profile *q = &g_prof[p];
    return q->saved && q->style == S.style && q->color == S.color && q->size == S.size && q->thick == S.thick &&
           q->outline == S.outline && q->opacity == S.opacity && q->cover == S.cover && q->glow == S.glow &&
           q->anim == S.anim && q->click_fx == S.click_fx && q->l2style1 == S.l2_style + 1 &&
           (S.l2_style < 0 || (q->l2color == S.l2_color && q->l2size == S.l2_size));
}

/* ---------------- system cursor hiding ---------------- */
/* Every standard Windows cursor is swapped for a blank one while active,
   and the user's normal cursors are restored on exit. */
static const DWORD CURSOR_IDS[] = {32512, 32513, 32514, 32515, 32516, 32640, 32641, 32642,
                                   32643, 32644, 32645, 32646, 32648, 32649, 32650, 32651,
                                   32671, 32672};
static HCURSOR g_blank;
static int g_cursor_hidden;

static void restore_cursor(void) {
    SystemParametersInfoW(SPI_SETCURSORS, 0, NULL, 0);
    g_cursor_hidden = 0;
}

static void blank_cursor(void) {
    if (!g_blank) {
        BYTE andm[32 * 32 / 8], xorm[32 * 32 / 8];
        memset(andm, 0xFF, sizeof(andm));
        memset(xorm, 0x00, sizeof(xorm));
        g_blank = CreateCursor(GetModuleHandleW(NULL), 0, 0, 32, 32, andm, xorm);
        if (!g_blank) return;
    }
    for (unsigned i = 0; i < sizeof(CURSOR_IDS) / sizeof(CURSOR_IDS[0]); i++) {
        HCURSOR c = CopyCursor(g_blank); /* SetSystemCursor takes ownership */
        if (c && !SetSystemCursor(c, CURSOR_IDS[i])) DestroyCursor(c);
    }
    g_cursor_hidden = 1;
}

/* ---------------- Roblox cursor hiding ---------------- */
/* Roblox draws its own cursor from PNG files in its install folder, so the
   Windows cursor setting doesn't reach it. While "hide cursor" is on, those
   PNGs are swapped for a transparent image (originals kept as .chbak) and put
   back when the option is turned off or the app exits. Roblox reads them when
   it starts, so open this app before Roblox. */
#include "blank_png.h"
static int g_rbx_patched;

static int file_exists(const wchar_t *p) {
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static int dir_exists(const wchar_t *p) {
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static int is_blank_file(const wchar_t *p);

static int write_blank(const wchar_t *p) {
    HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD w = 0;
    BOOL ok = WriteFile(h, BLANK_PNG, sizeof(BLANK_PNG), &w, NULL);
    CloseHandle(h);
    return ok && w == sizeof(BLANK_PNG);
}

static void mkdirs(const wchar_t *path) {
    wchar_t b[MAX_PATH];
    wcsncpy(b, path, MAX_PATH - 1); b[MAX_PATH - 1] = 0;
    for (wchar_t *c = b + 3; *c; c++)
        if (*c == L'\\') { *c = 0; CreateDirectoryW(b, NULL); *c = L'\\'; }
    CreateDirectoryW(b, NULL);
}

/* ---- where Roblox keeps its files ---- */
/* Roblox installs (per-user, Bloxstrap, Fishstrap, Voidstrap, all-users). ismod = the
   launcher copies its Modifications folder over Roblox at every launch. */
static int rbx_bases(wchar_t bases[8][MAX_PATH], int ismod[8]) {
    int nb = 0;
    wchar_t la[MAX_PATH], pf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH);
    if (n && n < MAX_PATH - 120) {
        static const wchar_t *LR[] = {L"Roblox", L"Bloxstrap", L"Fishstrap", L"Voidstrap"};
        for (int i = 0; i < 4; i++) { wsprintfW(bases[nb], L"%s\\%s", la, LR[i]); ismod[nb] = i > 0; nb++; }
    }
    static const wchar_t *PF[] = {L"ProgramFiles(x86)", L"ProgramFiles"};
    for (int i = 0; i < 2; i++) {
        n = GetEnvironmentVariableW(PF[i], pf, MAX_PATH);
        if (n && n < MAX_PATH - 120) { wsprintfW(bases[nb], L"%s\\Roblox", pf); ismod[nb] = 0; nb++; }
    }
    return nb;
}
/* The two cursor folders inside a Roblox version folder. */
static const wchar_t *RBX_SUB[2] = {L"content\\textures\\Cursors\\KeyboardMouse", L"content\\textures"};

/* Find a real (not blanked) copy of a Roblox cursor file in any installed Roblox
   version, or a good backup of one. Used to repair files and to back up correctly. */
static int find_original(int sub, const wchar_t *name, wchar_t *out) {
    wchar_t bases[8][MAX_PATH], pat[MAX_PATH], f[MAX_PATH], bak[MAX_PATH];
    int ismod[8], nb = rbx_bases(bases, ismod), found = 0;
    for (int b = 0; b < nb && !found; b++) {
        wsprintfW(pat, L"%s\\Versions\\*", bases[b]);
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(pat, &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.' || wcslen(fd.cFileName) > 60) continue;
            wsprintfW(f, L"%s\\Versions\\%s\\%s\\%s", bases[b], fd.cFileName, RBX_SUB[sub], name);
            wsprintfW(bak, L"%s.chbak", f);
            if (file_exists(bak) && !is_blank_file(bak)) { wcscpy(out, bak); found = 1; }
            else if (file_exists(f) && !is_blank_file(f)) { wcscpy(out, f); found = 1; }
        } while (!found && FindNextFileW(h, &fd));
        FindClose(h);
    }
    return found;
}

static int g_rbx_unfixable; /* cursor files that were broken and had no clean copy anywhere */

/* Blank one file. If it existed, back it up first; if not, mark it as ours.
   sub = which cursor folder (for finding originals), inver = it's inside a Roblox version folder. */
static void patch_file(const wchar_t *p, int create, int sub, const wchar_t *name, int inver) {
    wchar_t bak[MAX_PATH], mark[MAX_PATH], src[MAX_PATH];
    if (wcslen(p) > MAX_PATH - 10) return;
    wsprintfW(bak, L"%s.chbak", p);
    wsprintfW(mark, L"%s.chnew", p);
    if (file_exists(bak) && is_blank_file(bak)) DeleteFileW(bak); /* a blank "backup" (older versions) is useless */
    if (file_exists(bak) || file_exists(mark)) { /* already ours */
        if (!is_blank_file(p)) write_blank(p);       /* only write if Roblox replaced it */
        return;
    }
    if (file_exists(p)) {
        if (is_blank_file(p)) {
            /* Already blank, usually because Bloxstrap & co. copied our blank cursor in.
               Never back THAT up (it was the old bug: the cursor stayed invisible after
               closing). Back up a real copy from another Roblox version instead, if any. */
            if (inver && find_original(sub, name, src)) CopyFileW(src, bak, FALSE);
            return;
        }
        if (CopyFileW(p, bak, FALSE)) write_blank(p);
    } else if (create) {
        if (write_blank(p)) write_blank(mark);
    }
}

/* Put one file back the way Roblox had it. */
static void unpatch_file(const wchar_t *p, int sub, const wchar_t *name, int inver) {
    wchar_t bak[MAX_PATH], mark[MAX_PATH], src[MAX_PATH];
    if (wcslen(p) > MAX_PATH - 10) return;
    wsprintfW(bak, L"%s.chbak", p);
    wsprintfW(mark, L"%s.chnew", p);
    if (file_exists(bak)) {
        if (!is_blank_file(bak)) {
            if (CopyFileW(bak, p, FALSE)) DeleteFileW(bak);
            return;
        }
        DeleteFileW(bak); /* the backup itself was blank: repair below */
    } else if (file_exists(mark)) {
        DeleteFileW(p);
        DeleteFileW(mark);
        return;
    }
    /* Repair: a blank Roblox cursor that nothing accounts for (left behind by the old bug).
       Only inside Roblox's own version folders; a Modifications folder may hold someone's
       own invisible-cursor mod, so those are left alone. */
    if (inver && file_exists(p) && is_blank_file(p)) {
        if (find_original(sub, name, src)) CopyFileW(src, p, FALSE);
        else { DeleteFileW(p); g_rbx_unfixable++; } /* missing beats invisible: Roblox's repair/reinstall restores it */
    }
}

/* set 1 = normal pointers, set 2 = Roblox's built-in gun crosshair cursors
   (used by many gun kits when you pull a gun out) */
static const struct { const wchar_t *name; int set; } RBX_FILES[] = {
    {L"ArrowCursor.png", 1}, {L"ArrowFarCursor.png", 1}, {L"IBeamCursor.png", 1},
    {L"GunCursor.png", 2},   {L"GunWaitCursor.png", 2}};
#define NRBX 5
#define RBX_PTR 1
#define RBX_GUN 2

/* Apply (on=1) or undo (on=0) in one cursor folder. */
static void rbx_folder(const wchar_t *dir, int on, int create, int sets, int sub, int inver) {
    wchar_t f[MAX_PATH];
    if (!create && !dir_exists(dir)) return;
    if (on && create) mkdirs(dir);
    for (int i = 0; i < NRBX; i++) {
        if (!(RBX_FILES[i].set & sets)) continue;
        if (wcslen(dir) + wcslen(RBX_FILES[i].name) + 2 >= MAX_PATH) return;
        wsprintfW(f, L"%s\\%s", dir, RBX_FILES[i].name);
        if (on) patch_file(f, create, sub, RBX_FILES[i].name, inver);
        else unpatch_file(f, sub, RBX_FILES[i].name, inver);
    }
}

/* ---- status report (shown from the tray menu) ---- */
static wchar_t g_rep[6000];
static int g_reporting, g_found_dirs, g_blank_files, g_total_files;

static void rep_add(const wchar_t *line) {
    if (wcslen(g_rep) + wcslen(line) + 3 < sizeof(g_rep) / sizeof(g_rep[0])) {
        wcscat(g_rep, line);
        wcscat(g_rep, L"\n");
    }
}

static int is_blank_file(const wchar_t *p) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(p, GetFileExInfoStandard, &d)) return 0;
    return d.nFileSizeHigh == 0 && d.nFileSizeLow == sizeof(BLANK_PNG);
}

static void report_folder(const wchar_t *dir, int sets) {
    wchar_t f[MAX_PATH], line[MAX_PATH + 64];
    int blanks = 0, total = 0;
    for (int i = 0; i < NRBX; i++) {
        if (!(RBX_FILES[i].set & sets)) continue;
        wsprintfW(f, L"%s\\%s", dir, RBX_FILES[i].name);
        if (file_exists(f)) { total++; if (is_blank_file(f)) blanks++; }
    }
    if (!total) return;
    g_found_dirs++; g_blank_files += blanks; g_total_files += total;
    wsprintfW(line, L"%s %s  (%d of %d hidden)", blanks == total ? L"[OK]" : L"[!!]", dir, blanks, total);
    rep_add(line);
}

static void rbx_dir(const wchar_t *dir, int on, int create, int sets, int sub, int inver) {
    if (g_reporting) report_folder(dir, sets);
    else rbx_folder(dir, on, create, sets, sub, inver);
}

/* All Roblox installs: per-user, all-users, Bloxstrap, Fishstrap, Voidstrap. */
static void rbx_all(int on) {
    wchar_t bases[8][MAX_PATH], pat[MAX_PATH], dir[MAX_PATH];
    int ismod[8], nb = rbx_bases(bases, ismod);
    if (!on && !g_reporting) g_rbx_unfixable = 0;
    for (int b = 0; b < nb; b++) {
        if (!dir_exists(bases[b])) continue;
        wsprintfW(pat, L"%s\\Versions\\*", bases[b]);
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(pat, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
                if (wcslen(fd.cFileName) > 60) continue;
                wsprintfW(dir, L"%s\\Versions\\%s\\%s", bases[b], fd.cFileName, RBX_SUB[0]);
                rbx_dir(dir, on, 0, RBX_PTR, 0, 1);
                wsprintfW(dir, L"%s\\Versions\\%s\\%s", bases[b], fd.cFileName, RBX_SUB[1]);
                rbx_dir(dir, on, 0, RBX_PTR | RBX_GUN, 1, 1); /* gun cursors + older Roblox builds */
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        /* Bloxstrap-style launchers copy Modifications over Roblox at every launch */
        if (ismod[b]) {
            wsprintfW(dir, L"%s\\Modifications\\%s", bases[b], RBX_SUB[0]);
            rbx_dir(dir, on, 1, RBX_PTR, 0, 0);
            wsprintfW(dir, L"%s\\Modifications\\%s", bases[b], RBX_SUB[1]);
            rbx_dir(dir, on, 1, RBX_GUN, 1, 0);
        }
    }
    if (!g_reporting) g_rbx_patched = on;
    if (!on && !g_reporting && g_rbx_unfixable) log_warn(L"cursor: %d blank Roblox cursor file(s) had no clean copy and were removed", g_rbx_unfixable);
}

static int store_roblox_installed(void) {
    wchar_t la[MAX_PATH], pat[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH);
    if (!n || n > MAX_PATH - 60) return 0;
    wsprintfW(pat, L"%s\\Packages\\ROBLOXCORPORATION.ROBLOX_*", la);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    FindClose(h);
    return 1;
}

static void show_roblox_status(void) {
    wchar_t line[256];
    g_rep[0] = 0;
    g_found_dirs = g_blank_files = g_total_files = 0;
    wsprintfW(line, L"Hide cursor option: %s\n", S.hidecur ? L"ON" : L"OFF (press F10)");
    rep_add(line);
    rep_add(L"Roblox cursor folders found:");
    g_reporting = 1;
    rbx_all(1);
    g_reporting = 0;
    if (!g_found_dirs) rep_add(L"   (none)");
    rep_add(L"");
    if (store_roblox_installed())
        rep_add(L"* Microsoft Store Roblox is installed. Its files are locked by Windows, "
                L"so its cursor can't be hidden. Use Roblox from roblox.com instead.");
    if (!g_found_dirs)
        rep_add(L"* No Roblox install was found in the usual places.");
    if (g_rbx_unfixable)
        rep_add(L"* Some Roblox cursor files had been left blank by an older version of this app and no clean "
                L"copy was found, so they were removed. Reinstall Roblox (or use your launcher's reinstall "
                L"option) to get them back.");
    else if (S.hidecur && g_blank_files < g_total_files)
        rep_add(L"* Some files couldn't be changed. Try right-clicking CrosshairOverlay.exe "
                L"and choosing 'Run as administrator'.");
    else if (S.hidecur)
        rep_add(L"* Files are hidden (normal pointer + Roblox's built-in gun crosshair). "
                L"Fully close Roblox, then open it again.\n"
                L"* If a crosshair still appears when you pull out a gun, that game uses its own "
                L"custom mouse icon downloaded from the internet. Windows draws the mouse pointer "
                L"above every window, so no overlay can cover it. Check that game's settings for a "
                L"crosshair option.");
    MessageBoxW(g_keydlg, g_rep, L"Roblox cursor status", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
}

static void sync_roblox(void) {
    if (S.hidecur) rbx_all(1);
    else if (g_rbx_patched) rbx_all(0);
    else rbx_all(0); /* also cleans up after a crash */
}

/* ---------------- Roblox FPS unlocker + booster ---------------- */
/* Everything here goes through Roblox's own, supported settings files; nothing
   touches the running game's memory or code (that would trip Roblox's anti-cheat).
   - FPS cap: Roblox's "Maximum Frame Rate" setting (FramerateCap in
     %LOCALAPPDATA%\Roblox\GlobalBasicSettings_NN.xml). Roblox reads it at start and
     writes it back when it closes, so it is only changed while Roblox is closed.
   - Boost: graphics fast flags from Roblox's official allowlist, written to
     ClientSettings\ClientAppSettings.json (every Roblox version folder, and the
     Modifications folder of Bloxstrap / Fishstrap / Voidstrap). Flags already in that
     file (yours, or your launcher's) are kept.
   - High-performance GPU: Windows' per-app graphics preference for RobloxPlayerBeta.exe. */
/* No "unlimited" option: 1.10.0 had "Max", which wrote 0, and Roblox can take that as a
   0 FPS cap (stuck on a white screen). fps_repair() fixes files left with 0. */
static const int FPS_CAPS[] = {-1, 60, 75, 120, 144, 165, 240}; /* -1 = Roblox's default */
static const wchar_t *FPS_LABELS[] = {L"Off", L"60", L"75", L"120", L"144", L"165", L"240"};
static int g_fps_repaired;
static int g_perf_tick;
static FILETIME g_fps_seen; /* settings file time we last checked */

/* Allowlisted flags only (devforum: "Allowlist for local client configuration via Fast Flags").
   Others are ignored by Roblox now, including the old DFIntTaskSchedulerTargetFps unlocker. */
static const struct { const char *k, *v; int lvl; } BOOST_FLAGS[] = {
    {"FIntDebugForceMSAASamples", "\"0\"", 1},           /* no anti-aliasing */
    {"FIntFRMMinGrassDistance", "\"0\"", 1},             /* no grass */
    {"FIntFRMMaxGrassDistance", "\"0\"", 1},
    {"DFIntDebugFRMQualityLevelOverride", "\"1\"", 2},   /* lowest graphics quality */
    {"DFFlagTextureQualityOverrideEnabled", "\"True\"", 2},
    {"DFIntTextureQualityOverride", "\"0\"", 2},         /* lowest textures */
    {"DFIntCSGLevelOfDetailSwitchingDistance", "\"0\"", 2}, /* simpler unions sooner */
    {"DFIntCSGLevelOfDetailSwitchingDistanceL12", "\"0\"", 2},
    {"DFIntCSGLevelOfDetailSwitchingDistanceL23", "\"0\"", 2},
    {"DFIntCSGLevelOfDetailSwitchingDistanceL34", "\"0\"", 2},
};
#define NBOOST (int)(sizeof(BOOST_FLAGS) / sizeof(BOOST_FLAGS[0]))
/* Flags older versions set that are no longer used (risky): removed if they still have our value. */
static const struct { const char *k, *v; } RETIRED_FLAGS[] = {
    {"DFFlagDebugPauseVoxelizer", "\"True\""}, /* froze lighting; could leave the world dark or wrongly lit */
};
#define NRETIRED (int)(sizeof(RETIRED_FLAGS) / sizeof(RETIRED_FLAGS[0]))

static int roblox_running(void) {
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (s == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    int found = 0;
    if (Process32FirstW(s, &pe))
        do { if (!_wcsicmp(pe.szExeFile, L"RobloxPlayerBeta.exe")) found = 1; } while (!found && Process32NextW(s, &pe));
    CloseHandle(s);
    return found;
}

/* Whole small file into memory (NUL-terminated). */
static char *read_small(const wchar_t *p, DWORD *len) {
    HANDLE h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    DWORD n = GetFileSize(h, NULL), got = 0;
    char *b = NULL;
    if (n != INVALID_FILE_SIZE && n < 8 * 1024 * 1024 && (b = (char *)malloc(n + 1))) {
        if (!ReadFile(h, b, n, &got, NULL) || got != n) { free(b); b = NULL; }
        else { b[n] = 0; if (len) *len = n; }
    }
    CloseHandle(h);
    return b;
}
/* Write via a temp file + rename, so a half-written file is never left behind. */
static int write_small(const wchar_t *p, const char *data, DWORD n) {
    wchar_t tmp[MAX_PATH];
    if (wcslen(p) > MAX_PATH - 8) return 0;
    wsprintfW(tmp, L"%s.chtmp", p);
    HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD w = 0;
    BOOL ok = WriteFile(h, data, n, &w, NULL) && w == n;
    CloseHandle(h);
    if (ok) ok = MoveFileExW(tmp, p, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) DeleteFileW(tmp);
    return ok;
}

/* Roblox's settings file (the highest GlobalBasicSettings_NN.xml). */
static int fps_file(wchar_t *out) {
    wchar_t la[MAX_PATH], pat[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH);
    if (!n || n > MAX_PATH - 80) return 0;
    wsprintfW(pat, L"%s\\Roblox\\GlobalBasicSettings_*.xml", la);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int best = -1;
    do {
        int v = _wtoi(fd.cFileName + 20); /* after "GlobalBasicSettings_" */
        if (v > best && !(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            best = v;
            wsprintfW(out, L"%s\\Roblox\\%s", la, fd.cFileName);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return best >= 0;
}

enum { FPS_OFF, FPS_DONE, FPS_SAME, FPS_RUNNING, FPS_NOFILE, FPS_FAIL };
/* Write FramerateCap = cap. force = check even if the file hasn't changed since last time.
   only_if_zero = only touch it when it's 0 (the broken value from 1.10.0). */
static int fps_write(int cap, int force, int only_if_zero) {
    wchar_t p[MAX_PATH];
    if (!fps_file(p)) return FPS_NOFILE;
    WIN32_FILE_ATTRIBUTE_DATA ad;
    if (!GetFileAttributesExW(p, GetFileExInfoStandard, &ad)) return FPS_NOFILE;
    if (!force && !CompareFileTime(&ad.ftLastWriteTime, &g_fps_seen)) return FPS_SAME;
    if (roblox_running()) return FPS_RUNNING; /* it would overwrite us when it closes */
    DWORD len = 0;
    char *b = read_small(p, &len);
    if (!b) return FPS_FAIL;
    char val[16], *out = NULL;
    snprintf(val, sizeof(val), "%d", cap);
    static const char KEY[] = "name=\"FramerateCap\">";
    char *k = strstr(b, KEY);
    int res = FPS_SAME;
    if (k) {
        char *v0 = k + sizeof(KEY) - 1, *v1 = strchr(v0, '<');
        if (v1 && only_if_zero && !(v1 - v0 == 1 && *v0 == '0')) v1 = NULL; /* not broken: leave it */
        if (v1 && !(v1 - v0 == (int)strlen(val) && !memcmp(v0, val, v1 - v0))) {
            size_t pre = v0 - b, post = len - (v1 - b);
            out = (char *)malloc(pre + strlen(val) + post + 1);
            if (out) { memcpy(out, b, pre); strcpy(out + pre, val); memcpy(out + pre + strlen(val), v1, post); len = (DWORD)(pre + strlen(val) + post); }
        }
    } else if (!only_if_zero) {
        char *ug = strstr(b, "class=\"UserGameSettings\"");
        char *pe = ug ? strstr(ug, "</Properties>") : NULL;
        if (pe) {
            char add[80];
            snprintf(add, sizeof(add), "<int name=\"FramerateCap\">%s</int>\r\n\t\t", val);
            size_t pre = pe - b, al = strlen(add);
            out = (char *)malloc(len + al + 1);
            if (out) { memcpy(out, b, pre); memcpy(out + pre, add, al); memcpy(out + pre + al, pe, len - pre); len += (DWORD)al; }
        } else res = FPS_FAIL;
    }
    if (out) { res = write_small(p, out, len) ? FPS_DONE : FPS_FAIL; free(out); }
    free(b);
    {
        static int last = -1;
        if (res == FPS_FAIL && last != FPS_FAIL) log_error(L"FPS cap: couldn't update Roblox's settings file (error %lu)", GetLastError());
        if (res == FPS_DONE) log_info(only_if_zero ? L"FPS cap: repaired a 0 cap (white screen fix), now %d" : L"FPS cap set to %d", cap);
        last = res;
    }
    if (GetFileAttributesExW(p, GetFileExInfoStandard, &ad)) g_fps_seen = ad.ftLastWriteTime;
    return res;
}
static int fps_apply(int force) { return g_fps > 0 ? fps_write(FPS_CAPS[g_fps], force, 0) : FPS_OFF; }
/* A 0 cap (white screen) -> the chosen cap, or Roblox's default. Runs until it has worked once. */
static void fps_repair(void) {
    if (g_fps_repaired) return;
    int r = fps_write(g_fps > 0 ? FPS_CAPS[g_fps] : -1, 1, 1);
    if (r == FPS_DONE || r == FPS_SAME || r == FPS_NOFILE) g_fps_repaired = 1;
}

/* ---- ClientAppSettings.json: a flat {"Flag": value} object ---- */
#define MAXFLAGS 600
typedef struct { char k[128]; char v[256]; } FFlag;
static FFlag g_ff[MAXFLAGS];
static int g_nff;

static const char *json_ws(const char *s) { while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++; return s; }
/* Copy a JSON string starting at the opening quote (quotes included if keep). Returns the char after it. */
static const char *json_str(const char *s, char *out, int cap, int keep) {
    int n = 0;
    if (*s != '"') return NULL;
    if (keep) out[n++] = '"';
    s++;
    while (*s && *s != '"') {
        if (*s == '\\' && s[1]) { if (n < cap - 3) out[n++] = *s; s++; }
        if (n >= cap - 2) return NULL;
        out[n++] = *s++;
    }
    if (*s != '"') return NULL;
    if (keep) out[n++] = '"';
    out[n] = 0;
    return s + 1;
}
/* 1 = parsed (or empty), 0 = not something we understand: leave the file alone. */
static int json_parse(const char *s) {
    g_nff = 0;
    if (s[0] == (char)0xEF && s[1] == (char)0xBB && s[2] == (char)0xBF) s += 3; /* UTF-8 BOM */
    s = json_ws(s);
    if (!*s) return 1;
    if (*s++ != '{') return 0;
    s = json_ws(s);
    if (*s == '}') return 1;
    for (;;) {
        if (g_nff >= MAXFLAGS) return 0;
        FFlag *f = &g_ff[g_nff];
        s = json_str(json_ws(s), f->k, sizeof(f->k), 0);
        if (!s) return 0;
        s = json_ws(s);
        if (*s++ != ':') return 0;
        s = json_ws(s);
        if (*s == '"') { s = json_str(s, f->v, sizeof(f->v), 1); if (!s) return 0; }
        else {
            int n = 0;
            while (*s && *s != ',' && *s != '}' && *s != ' ' && *s != '\r' && *s != '\n' && *s != '\t') {
                if (*s == '{' || *s == '[' || n >= (int)sizeof(f->v) - 1) return 0;
                f->v[n++] = *s++;
            }
            f->v[n] = 0;
            if (!n) return 0;
        }
        g_nff++;
        s = json_ws(s);
        if (*s == ',') { s++; continue; }
        if (*s == '}') return 1;
        return 0;
    }
}
static int ff_find(const char *k) {
    for (int i = 0; i < g_nff; i++) if (!strcmp(g_ff[i].k, k)) return i;
    return -1;
}

static int g_boost_ok, g_boost_fail;
/* Bring one ClientAppSettings.json in line with the boost level. */
static void boost_file(const wchar_t *dir) {
    wchar_t p[MAX_PATH];
    if (wcslen(dir) > MAX_PATH - 40) return;
    wsprintfW(p, L"%s\\ClientAppSettings.json", dir);
    char *b = read_small(p, NULL);
    if (!json_parse(b ? b : "")) { free(b); g_boost_fail++; log_warn(L"boost: left %s alone (not a simple flag list)", p); return; }
    int changed = 0;
    for (int i = 0; i < NBOOST; i++) {
        int at = ff_find(BOOST_FLAGS[i].k);
        if (BOOST_FLAGS[i].lvl <= g_boost) {
            if (at < 0) {
                if (g_nff >= MAXFLAGS) continue;
                at = g_nff++;
                strcpy(g_ff[at].k, BOOST_FLAGS[i].k);
                g_ff[at].v[0] = 0;
            }
            if (strcmp(g_ff[at].v, BOOST_FLAGS[i].v)) { strcpy(g_ff[at].v, BOOST_FLAGS[i].v); changed = 1; }
        } else if (at >= 0 && !strcmp(g_ff[at].v, BOOST_FLAGS[i].v)) { /* ours: take it back out */
            memmove(&g_ff[at], &g_ff[at + 1], (g_nff - at - 1) * sizeof(FFlag));
            g_nff--;
            changed = 1;
        }
    }
    for (int i = 0; i < NRETIRED; i++) {
        int at = ff_find(RETIRED_FLAGS[i].k);
        if (at >= 0 && !strcmp(g_ff[at].v, RETIRED_FLAGS[i].v)) {
            memmove(&g_ff[at], &g_ff[at + 1], (g_nff - at - 1) * sizeof(FFlag));
            g_nff--;
            changed = 1;
        }
    }
    free(b);
    if (!changed) { if (g_boost) g_boost_ok++; return; }
    size_t cap = 8 + (size_t)g_nff * (sizeof(FFlag) + 12);
    char *out = (char *)malloc(cap);
    if (!out) { g_boost_fail++; return; }
    size_t n = 0;
    n += snprintf(out + n, cap - n, "{\r\n");
    for (int i = 0; i < g_nff; i++)
        n += snprintf(out + n, cap - n, "  \"%s\": %s%s\r\n", g_ff[i].k, g_ff[i].v, i < g_nff - 1 ? "," : "");
    n += snprintf(out + n, cap - n, "}\r\n");
    if (!dir_exists(dir)) mkdirs(dir);
    if (write_small(p, out, (DWORD)n)) g_boost_ok++;
    else { g_boost_fail++; log_error(L"boost: couldn't write %s (error %lu)", p, GetLastError()); }
    free(out);
}

/* Windows' "high performance" graphics choice for one exe (on = 1) or undo it. */
static void gpu_pref(const wchar_t *exe, int on) {
    HKEY k;
    static const wchar_t WANT[] = L"GpuPreference=2;";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\DirectX\\UserGpuPreferences", 0, NULL, 0,
                        KEY_QUERY_VALUE | KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS) return;
    wchar_t cur[64];
    DWORD sz = sizeof(cur), type = 0;
    LONG r = RegQueryValueExW(k, exe, NULL, &type, (BYTE *)cur, &sz);
    int ours = r == ERROR_SUCCESS && type == REG_SZ && sz >= sizeof(WANT) - 2 && !wcsncmp(cur, WANT, 15);
    if (on && !ours) RegSetValueExW(k, exe, 0, REG_SZ, (const BYTE *)WANT, sizeof(WANT));
    else if (!on && ours) RegDeleteValueW(k, exe);
    RegCloseKey(k);
}

/* Boost + GPU choice for every Roblox install (re-run after Roblox updates). */
static int g_perf_installs;
static void perf_sync_installs(void) {
    wchar_t bases[8][MAX_PATH], pat[MAX_PATH], dir[MAX_PATH], exe[MAX_PATH];
    int ismod[8], nb = rbx_bases(bases, ismod);
    g_boost_ok = g_boost_fail = g_perf_installs = 0;
    for (int b = 0; b < nb; b++) {
        if (!dir_exists(bases[b])) continue;
        wsprintfW(pat, L"%s\\Versions\\*", bases[b]);
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(pat, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.' || wcslen(fd.cFileName) > 60) continue;
                wsprintfW(exe, L"%s\\Versions\\%s\\RobloxPlayerBeta.exe", bases[b], fd.cFileName);
                if (!file_exists(exe)) continue; /* Studio or a half-installed update */
                g_perf_installs++;
                wsprintfW(dir, L"%s\\Versions\\%s\\ClientSettings", bases[b], fd.cFileName);
                if (g_boost || dir_exists(dir)) boost_file(dir);
                gpu_pref(exe, g_gpu);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        if (ismod[b]) { /* launchers copy Modifications over Roblox each launch, and keep their own flags there */
            wsprintfW(dir, L"%s\\Modifications\\ClientSettings", bases[b]);
            if (g_boost || dir_exists(dir)) boost_file(dir);
        }
    }
}

/* Called every 3 s from the host timer and when Roblox's folders change. */
static void perf_sync(int folders_changed) {
    fps_repair();
    if (g_fps > 0) fps_apply(0);
    if (folders_changed || ++g_perf_tick >= 10) { /* installs: on updates, plus every 30 s */
        g_perf_tick = 0;
        if (g_boost || g_gpu || folders_changed) perf_sync_installs();
    }
}

/* ---------------- Roblox crash reports ---------------- */
/* Watches RobloxPlayerBeta.exe (only waits for it to exit - never reads its memory).
   When it ends with a crash code, gathers clues and guesses the cause:
   - its exit code, and Windows' own crash log entry (Application Error 1000: which DLL crashed),
   - the newest Roblox log's error lines (numbers / IP addresses hidden),
   - the PC: graphics card + driver age, peak RAM use while playing, free space on C:,
   - programs known to clash with Roblox, the launcher (Bloxstrap & co.), this app's Roblox changes.
   The player gets the likely causes + fixes (tray popup); a report goes to the developer if
   bug reports are on. */
typedef struct {
    DWORD pid, code;
    DWORD ran_s;            /* how long Roblox ran */
    int peak_mem;           /* highest RAM use (%) seen while it ran */
    wchar_t launcher[32];
    wchar_t text[6000];     /* full details (report + "details" box) */
    wchar_t top[160];       /* most likely cause, one line */
    wchar_t tips[1600];     /* causes + fixes for the player */
} RbxCrash;
static RbxCrash *g_rbx_last; /* shown when the player clicks the popup */
static int g_balloon_rbx;
static DWORD g_rbx_sent_at;
static volatile int g_rbx_peak_mem;
#define WM_RBX_CRASH (WM_APP + 6)

static int rbx_is_crash_code(DWORD c) {
    if (c == 0 || c == 1 || c == STILL_ACTIVE) return 0; /* closed normally / ended from Task Manager */
    if ((c & 0xF0000000u) == 0xC0000000u) return 1;     /* NTSTATUS errors (access violation, fail-fast, ...) */
    return c == 0x80000003u || c == 0xE06D7363u || c == 3 || c == 0x40000015u || (c & 0xFFFFFFF0u) == 0x887A0000u;
}
static const wchar_t *rbx_code_text(DWORD c) {
    switch (c) {
    case 0xC0000005: return L"memory access crash";
    case 0xC0000409: return L"Roblox stopped itself on purpose (fail-fast)";
    case 0xC0000374: return L"memory (heap) corruption";
    case 0xC000001D: return L"CPU instruction not supported";
    case 0xC00000FD: return L"stack overflow";
    case 0xC0000017: return L"out of memory";
    case 0xC0000142: return L"a DLL failed to start";
    case 0xC0000135: return L"a needed DLL is missing";
    case 0xC0000022: return L"access denied";
    case 0x80000003: return L"breakpoint (Roblox stopped itself)";
    case 0xE06D7363: return L"unhandled C++ error";
    case 0x887A0005: return L"graphics device removed (driver crash / GPU reset)";
    case 0x887A0006: return L"graphics device hung";
    case 0x887A0007: return L"graphics device reset";
    case 3: return L"abort";
    default: return L"crash";
    }
}

/* Faulting DLL -> what it is + what to do. Matched on the start of the file name. */
static const struct { const char *pre; const wchar_t *what, *fix; } RBX_MODS[] = {
    {"nvwgf2um", L"NVIDIA graphics driver", L"Update your NVIDIA driver (NVIDIA app / nvidia.com), or do a clean install of it."},
    {"nvd3dum", L"NVIDIA graphics driver", L"Update your NVIDIA driver (NVIDIA app / nvidia.com), or do a clean install of it."},
    {"nvoglv", L"NVIDIA graphics driver", L"Update your NVIDIA driver (NVIDIA app / nvidia.com), or do a clean install of it."},
    {"nvspcap", L"NVIDIA overlay (Share / Instant Replay)", L"Turn off the NVIDIA in-game overlay, or update the NVIDIA app."},
    {"atiumd", L"AMD graphics driver", L"Update your AMD driver (AMD Software: Adrenalin / amd.com)."},
    {"atidxx", L"AMD graphics driver", L"Update your AMD driver (AMD Software: Adrenalin / amd.com)."},
    {"aticfx", L"AMD graphics driver", L"Update your AMD driver (AMD Software: Adrenalin / amd.com)."},
    {"amdxx", L"AMD graphics driver", L"Update your AMD driver (AMD Software: Adrenalin / amd.com)."},
    {"amdvlk", L"AMD graphics driver (Vulkan)", L"Update your AMD driver (AMD Software: Adrenalin / amd.com)."},
    {"igd", L"Intel graphics driver", L"Update your Intel graphics driver (Intel Driver & Support Assistant)."},
    {"igxe", L"Intel graphics driver", L"Update your Intel graphics driver (Intel Driver & Support Assistant)."},
    {"igc", L"Intel graphics driver", L"Update your Intel graphics driver (Intel Driver & Support Assistant)."},
    {"ig9", L"Intel graphics driver", L"Update your Intel graphics driver (Intel Driver & Support Assistant)."},
    {"ig7", L"Intel graphics driver", L"Update your Intel graphics driver (Intel Driver & Support Assistant)."},
    {"ig8", L"Intel graphics driver", L"Update your Intel graphics driver (Intel Driver & Support Assistant)."},
    {"d3d11", L"DirectX 11 (graphics)", L"Update your graphics driver. If it keeps happening, lower Roblox's graphics quality."},
    {"d3d12", L"DirectX 12 (graphics)", L"Update your graphics driver."},
    {"dxgi", L"DirectX graphics", L"Update your graphics driver; turn off any GPU overclock."},
    {"vulkan-1", L"Vulkan (graphics)", L"Update your graphics driver."},
    {"discordhook", L"Discord's in-game overlay", L"Turn off Discord's overlay: User Settings > Game Overlay."},
    {"gameoverlayrenderer", L"Steam's overlay", L"Don't launch Roblox through Steam, or turn off the Steam overlay."},
    {"rtsshooks", L"RivaTuner / MSI Afterburner overlay", L"Close RivaTuner (RTSS), or add Roblox to its exceptions with detection off."},
    {"graphics-hook", L"OBS game capture", L"In OBS use Window or Display capture for Roblox instead of Game capture."},
    {"obs-", L"OBS", L"In OBS use Window or Display capture for Roblox instead of Game capture."},
    {"nahimic", L"Nahimic audio overlay (MSI / Dell / Lenovo PCs)", L"Turn off Nahimic's overlay or uninstall Nahimic."},
    {"medal", L"Medal clip recorder", L"Close Medal or turn off its overlay."},
    {"ow-graphics", L"Overwolf overlay", L"Close Overwolf."},
    {"robloxplayerbeta", L"Roblox itself", L"Usually a Roblox bug or a broken install. Reinstall Roblox (or use your launcher's Reinstall option)."},
    {"ucrtbase", L"Microsoft Visual C++ runtime", L"Install the latest Visual C++ Redistributable (x64) from Microsoft."},
    {"msvcp", L"Microsoft Visual C++ runtime", L"Install the latest Visual C++ Redistributable (x64) from Microsoft."},
    {"vcruntime", L"Microsoft Visual C++ runtime", L"Install the latest Visual C++ Redistributable (x64) from Microsoft."},
    {"xinput", L"controller support (XInput)", L"Unplug extra controllers / update controller software."},
    {"audioses", L"Windows audio", L"Update your audio driver; unplug and re-plug your headset."},
    {"ntdll", L"Windows core (ntdll)", L"Often something injected into Roblox or a memory problem: close overlays/recorders, and run Windows Memory Diagnostic if it keeps happening."},
    {"kernelbase", L"Windows core (KernelBase)", L"Often Roblox stopping itself; reinstall Roblox and close overlays/recorders."},
};
/* Programs that are known to clash with Roblox or its anti-cheat. */
static const struct { const wchar_t *exe, *what, *fix; } RBX_CLASH[] = {
    {L"rbxfpsunlocker.exe", L"rbxfpsunlocker (old FPS unlocker)", L"Close it: it edits Roblox's memory, which Roblox's anti-cheat can crash (or ban). Use the FPS Boost tab instead."},
    {L"cheatengine-x86_64.exe", L"Cheat Engine", L"Close Cheat Engine: Roblox's anti-cheat shuts Roblox down when it sees it."},
    {L"cheatengine-x86_64-sse4-avx2.exe", L"Cheat Engine", L"Close Cheat Engine: Roblox's anti-cheat shuts Roblox down when it sees it."},
    {L"rtss.exe", L"RivaTuner Statistics Server", L"Close RivaTuner or turn its overlay off for Roblox."},
    {L"medal.exe", L"Medal", L"Close Medal or turn off its overlay."},
    {L"overwolf.exe", L"Overwolf", L"Close Overwolf."},
    {L"bdcam.exe", L"Bandicam", L"Close Bandicam (its game recording hooks into Roblox)."},
    {L"fraps.exe", L"Fraps", L"Close Fraps."},
};

/* Windows' crash log for this Roblox process: faulting DLL + exception code (if Windows logged it). */
static int rbx_eventlog(DWORD pid, wchar_t *module, int mcap, DWORD *code) {
    wchar_t q[300];
    int found = 0;
    module[0] = 0;
    wsprintfW(q, L"*[System[Provider[@Name='Application Error'] and (EventID=1000) and TimeCreated[timediff(@SystemTime) <= 300000]]]");
    EVT_HANDLE res = EvtQuery(NULL, L"Application", q, EvtQueryChannelPath | EvtQueryReverseDirection);
    if (!res) return 0;
    static const wchar_t *PATHS[] = {L"Event/EventData/Data[1]", L"Event/EventData/Data[4]", L"Event/EventData/Data[7]", L"Event/EventData/Data[9]"};
    EVT_HANDLE ctx = EvtCreateRenderContext(4, PATHS, EvtRenderContextValues);
    EVT_HANDLE ev[8];
    DWORD got = 0;
    while (ctx && !found && EvtNext(res, 8, ev, 2000, 0, &got)) {
        for (DWORD i = 0; i < got; i++) {
            BYTE buf[4096];
            DWORD used = 0, cnt = 0;
            if (!found && EvtRender(ctx, ev[i], EvtRenderEventValues, sizeof(buf), buf, &used, &cnt) && cnt >= 4) {
                EVT_VARIANT *v = (EVT_VARIANT *)buf;
                DWORD epid = 0;
                if (v[3].Type == EvtVarTypeString && v[3].StringVal) epid = (DWORD)wcstoul(v[3].StringVal, NULL, 0);
                else if (v[3].Type == EvtVarTypeHexInt32 || v[3].Type == EvtVarTypeUInt32) epid = v[3].UInt32Val;
                if (v[0].Type == EvtVarTypeString && v[0].StringVal && !_wcsicmp(v[0].StringVal, L"RobloxPlayerBeta.exe") &&
                    (!epid || epid == pid)) {
                    if (v[1].Type == EvtVarTypeString && v[1].StringVal) wcsncpy(module, v[1].StringVal, mcap - 1), module[mcap - 1] = 0;
                    if (v[2].Type == EvtVarTypeString && v[2].StringVal) *code = (DWORD)wcstoul(v[2].StringVal, NULL, 16);
                    else if (v[2].Type == EvtVarTypeHexInt32 || v[2].Type == EvtVarTypeUInt32) *code = v[2].UInt32Val;
                    found = 1;
                }
            }
            EvtClose(ev[i]);
        }
    }
    if (ctx) EvtClose(ctx);
    EvtClose(res);
    return found;
}

/* Hide anything identifying in a Roblox log line: long numbers (user / place / job ids) and IPs. */
static void rbx_scrub(wchar_t *s) {
    for (wchar_t *p = s; *p; p++) {
        if (iswdigit(*p)) {
            wchar_t *e = p;
            while (iswdigit(*e) || *e == L'.') e++;
            int dots = 0;
            for (wchar_t *k = p; k < e; k++) dots += *k == L'.';
            if (e - p >= 5 && (dots == 0 || dots == 3)) for (wchar_t *k = p; k < e; k++) if (*k != L'.') *k = L'#';
            p = e - 1;
        }
    }
}
/* Error lines from the newest Roblox player log. */
static void rbx_log_errors(WBuf *out, int *saw_oom, int *saw_device_lost) {
    wchar_t la[MAX_PATH], pat[MAX_PATH], best[MAX_PATH] = L"";
    FILETIME bt = {0, 0};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH);
    if (!n || n > MAX_PATH - 60) return;
    wsprintfW(pat, L"%s\\Roblox\\logs\\*Player*.log", la);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (CompareFileTime(&fd.ftLastWriteTime, &bt) > 0 && wcslen(fd.cFileName) < 200) {
            bt = fd.ftLastWriteTime;
            wsprintfW(best, L"%s\\Roblox\\logs\\%s", la, fd.cFileName);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (!best[0]) return;
    HANDLE f = CreateFileW(best, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD size = GetFileSize(f, NULL), want = size > 96 * 1024 ? 96 * 1024 : size, got = 0;
    char *b = (char *)malloc(want + 1);
    if (b && SetFilePointer(f, (LONG)(size - want), NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER && ReadFile(f, b, want, &got, NULL)) {
        b[got] = 0;
        static const char *KEYS[] = {"crash", "error", "exception", "fatal", "out of memory", "device removed", "device lost", "dxgi_error",
                                     "failed", "hyperion", "unexpected client behavior", "access violation"};
        int lines = 0;
        for (char *l = b; l && *l && lines < 25;) {
            char *e = strchr(l, '\n');
            if (e) *e = 0;
            char low[400];
            int L = (int)strlen(l);
            if (L > 6 && L < 1000) {
                int k;
                for (k = 0; k < L && k < 399; k++) low[k] = (char)tolower((unsigned char)l[k]);
                low[k] = 0;
                int hitk = 0;
                for (int i = 0; i < (int)(sizeof(KEYS) / sizeof(KEYS[0])); i++) if (strstr(low, KEYS[i])) { hitk = 1; break; }
                if (strstr(low, "out of memory")) *saw_oom = 1;
                if (strstr(low, "device removed") || strstr(low, "device lost") || strstr(low, "device_removed")) *saw_device_lost = 1;
                if (hitk) {
                    wchar_t w[260];
                    int m = MultiByteToWideChar(CP_UTF8, 0, l, L > 250 ? 250 : L, w, 259);
                    w[m > 0 ? m : 0] = 0;
                    for (wchar_t *c = w; *c; c++) if (*c == L'\r') *c = L' ';
                    rbx_scrub(w);
                    wb_str(out, L"  ");
                    wb_str(out, w);
                    wb_str(out, L"\r\n");
                    lines++;
                }
            }
            l = e ? e + 1 : NULL;
        }
        if (!lines) wb_str(out, L"  (no error lines)\r\n");
    }
    free(b);
    CloseHandle(f);
}

/* Main graphics card name + driver version/date (from its registry key). */
static void gpu_info(wchar_t *name, int ncap, wchar_t *ver, int vcap, int *driver_year) {
    DISPLAY_DEVICEW dd;
    name[0] = ver[0] = 0;
    *driver_year = 0;
    for (DWORD i = 0; ; i++) {
        memset(&dd, 0, sizeof(dd));
        dd.cb = sizeof(dd);
        if (!EnumDisplayDevicesW(NULL, i, &dd, 0)) break;
        if (!(dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)) continue;
        wcsncpy(name, dd.DeviceString, ncap - 1);
        name[ncap - 1] = 0;
        const wchar_t *k = dd.DeviceKey;
        if (!_wcsnicmp(k, L"\\Registry\\Machine\\", 18)) {
            HKEY hk;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, k + 18, 0, KEY_QUERY_VALUE, &hk) == ERROR_SUCCESS) {
                wchar_t v[64] = L"", d[64] = L"";
                DWORD sz = sizeof(v);
                RegQueryValueExW(hk, L"DriverVersion", NULL, NULL, (BYTE *)v, &sz);
                sz = sizeof(d);
                RegQueryValueExW(hk, L"DriverDate", NULL, NULL, (BYTE *)d, &sz);
                RegCloseKey(hk);
                if (v[0]) wsprintfW(ver, d[0] ? L"%s (%s)" : L"%s", v, d);
                const wchar_t *yy = wcsrchr(d, L'-');
                if (yy) *driver_year = _wtoi(yy + 1);
            }
        }
        break;
    }
}

/* Collect everything about one crash and work out the likely causes. */
static RbxCrash *rbx_analyze(DWORD pid, DWORD code, DWORD ran_s, int peak_mem, const wchar_t *launcher) {
    RbxCrash *c = (RbxCrash *)calloc(1, sizeof(RbxCrash));
    if (!c) return NULL;
    c->pid = pid; c->code = code; c->ran_s = ran_s; c->peak_mem = peak_mem;
    wcsncpy(c->launcher, launcher, 31);
    Sleep(5000); /* give Windows a moment to write its crash log */
    wchar_t mod[MAX_PATH] = L"", t[700];
    DWORD evcode = 0;
    int have_ev = rbx_eventlog(pid, mod, MAX_PATH, &evcode);
    if (have_ev && evcode && !rbx_is_crash_code(code)) code = c->code = evcode;
    /* the PC */
    wchar_t gpu[128], gver[128];
    int gyear, oom = 0, devlost = 0;
    gpu_info(gpu, 128, gver, 128, &gyear);
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);
    ULARGE_INTEGER freeb;
    int free_gb = GetDiskFreeSpaceExW(L"C:\\", &freeb, NULL, NULL) ? (int)(freeb.QuadPart >> 30) : -1;
    SYSTEMTIME now;
    GetLocalTime(&now);
    /* clashing programs */
    wchar_t clash[400] = L"";
    int clash_i[8], nclash = 0;
    {
        HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (s != INVALID_HANDLE_VALUE && Process32FirstW(s, &pe)) do {
            for (int i = 0; i < (int)(sizeof(RBX_CLASH) / sizeof(RBX_CLASH[0])); i++)
                if (!_wcsicmp(pe.szExeFile, RBX_CLASH[i].exe)) {
                    int dup = 0;
                    for (int k = 0; k < nclash; k++) if (!wcscmp(RBX_CLASH[clash_i[k]].what, RBX_CLASH[i].what)) dup = 1;
                    if (!dup && nclash < 8) clash_i[nclash++] = i;
                }
        } while (Process32NextW(s, &pe));
        if (s != INVALID_HANDLE_VALUE) CloseHandle(s);
        for (int k = 0; k < nclash; k++) { if (k) wcscat(clash, L", "); wcscat(clash, RBX_CLASH[clash_i[k]].what); }
    }
    WBuf logs = {0};
    rbx_log_errors(&logs, &oom, &devlost);

    /* ---- likely causes, most likely first ---- */
    const wchar_t *cw[8], *cf[8];
    int nc = 0;
#define ADDC(w, f) do { int _d = 0; for (int _k = 0; _k < nc; _k++) if (!wcscmp(cw[_k], (w))) _d = 1; if (!_d && nc < 8) { cw[nc] = (w); cf[nc] = (f); nc++; } } while (0)
    if (mod[0]) {
        char m8[MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, mod, -1, m8, sizeof(m8), NULL, NULL);
        for (char *q = m8; *q; q++) *q = (char)tolower((unsigned char)*q);
        for (int i = 0; i < (int)(sizeof(RBX_MODS) / sizeof(RBX_MODS[0])); i++)
            if (!strncmp(m8, RBX_MODS[i].pre, strlen(RBX_MODS[i].pre))) { ADDC(RBX_MODS[i].what, RBX_MODS[i].fix); break; }
    }
    for (int k = 0; k < nclash; k++) ADDC(RBX_CLASH[clash_i[k]].what, RBX_CLASH[clash_i[k]].fix);
    if ((code & 0xFFFFFFF0u) == 0x887A0000u || devlost)
        ADDC(L"The graphics driver crashed or the GPU reset", L"Update your graphics driver, turn off any GPU overclock, and check the GPU isn't overheating.");
    if (peak_mem >= 92 || code == 0xC0000017 || oom)
        ADDC(L"The PC ran out of memory (RAM)", L"Close browsers/other apps while playing, or lower Roblox's graphics quality.");
    if (code == 0xC0000409 || code == 0x80000003)
        ADDC(L"Roblox closed itself on purpose (often its anti-cheat)", L"Close overlays, recorders, macro/FPS tools; if it keeps happening reinstall Roblox.");
    if (code == 0xC0000374) ADDC(L"Memory corruption (software inside Roblox, or bad RAM)", L"Close overlays/recorders; run Windows Memory Diagnostic if it keeps happening.");
    if (code == 0xC000001D) ADDC(L"Unstable CPU or overclock", L"Turn off CPU overclocks / undervolts and update your BIOS.");
    if (code == 0xC0000142 || code == 0xC0000135 || code == 0xC0000022)
        ADDC(L"Roblox couldn't start properly (blocked or broken install)", L"Reinstall Roblox and allow it in your antivirus.");
    if (gyear && gyear < now.wYear - 1)
        ADDC(L"Old graphics driver", L"Your graphics driver is from more than a year ago \x2014 update it.");
    if (free_gb >= 0 && free_gb < 3) ADDC(L"Almost no free space on C:", L"Free up a few GB on C: (Roblox needs room for its cache and updates).");
    if (ran_s < 20) ADDC(L"Crashed while starting", L"Reinstall Roblox (or use your launcher's Reinstall option); check your antivirus isn't blocking it.");
    if (g_boost || g_fps > 0)
        ADDC(L"This app's FPS Boost settings (less likely)", L"To rule it out: FPS Boost tab > Undo all FPS changes, then try again.");
    if (!nc) ADDC(L"No clear cause found", L"If it keeps happening: update your graphics driver, close overlays, and reinstall Roblox.");
#undef ADDC
    wcsncpy(c->top, cw[0], 159);

    /* ---- the player's tips ---- */
    {
        WBuf tb = {0};
        for (int i = 0; i < nc && i < 4; i++) {
            wsprintfW(t, L"%d. %s\r\n    %s\r\n", i + 1, cw[i], cf[i]);
            wb_str(&tb, t);
        }
        if (tb.s) { wcsncpy(c->tips, tb.s, 1599); free(tb.s); }
    }
    /* ---- full details ---- */
    {
        WBuf d = {0};
        int mins = (int)(ran_s / 60);
        wsprintfW(t, L"Roblox crashed after %d min %d s \x2014 exit code 0x%08lX (%s)\r\n", mins, (int)(ran_s % 60),
                  (unsigned long)code, rbx_code_text(code));
        wb_str(&d, t);
        if (have_ev) { wsprintfW(t, L"Windows crash log: faulting file %s\r\n", mod[0] ? mod : L"(not given)"); wb_str(&d, t); }
        else wb_str(&d, L"Windows crash log: no entry (Roblox handled its own crash)\r\n");
        wb_str(&d, L"\r\nLikely causes:\r\n");
        for (int i = 0; i < nc; i++) { wsprintfW(t, L"%d. %s \x2014 %s\r\n", i + 1, cw[i], cf[i]); wb_str(&d, t); }
        wsprintfW(t, L"\r\nPC: %s, driver %s\r\nRAM: %d GB, peak use while playing %d%%\r\nFree on C: %d GB\r\n",
                  gpu[0] ? gpu : L"(unknown GPU)", gver[0] ? gver : L"(unknown)", (int)((ms.ullTotalPhys + (1u << 29)) >> 30),
                  peak_mem, free_gb);
        wb_str(&d, t);
        wsprintfW(t, L"Launcher: %s\r\nClashing programs running: %s\r\n", c->launcher[0] ? c->launcher : L"Roblox", clash[0] ? clash : L"none found");
        wb_str(&d, t);
        wsprintfW(t, L"This app's Roblox changes: FPS cap %s, boost %s, high-performance GPU %s, hide cursor %s\r\n",
                  g_fps > 0 ? FPS_LABELS[g_fps] : L"off", g_boost == 2 ? L"Max FPS" : g_boost == 1 ? L"Balanced" : L"off",
                  g_gpu ? L"on" : L"off", S.hidecur ? L"on" : L"off");
        wb_str(&d, t);
        wb_str(&d, L"\r\nRoblox log, error lines (numbers and IPs hidden):\r\n");
        if (logs.s) wb_str(&d, logs.s); else wb_str(&d, L"  (no Roblox log found)\r\n");
        if (d.s) { wcsncpy(c->text, d.s, 5999); free(d.s); }
    }
    free(logs.s);
    return c;
}

typedef struct { DWORD pid; DWORD code; DWORD ran_s; int peak; wchar_t launcher[32]; } RbxEnd;
static DWORD WINAPI rbx_analyze_thread(LPVOID p) {
    RbxEnd *e = (RbxEnd *)p;
    RbxCrash *c = rbx_analyze(e->pid, e->code, e->ran_s, e->peak, e->launcher);
    free(e);
    if (c && g_host) PostMessageW(g_host, WM_RBX_CRASH, 0, (LPARAM)c);
    else free(c);
    return 0;
}

/* Waits for Roblox player processes to end; starts an analysis when one crashes. */
static DWORD WINAPI rbx_crash_watch(LPVOID unused) {
    (void)unused;
    HANDLE hs[16];
    DWORD pids[16];
    ULONGLONG start[16];
    wchar_t launch[16][32];
    int n = 0;
    while (g_running) {
        /* new Roblox processes */
        HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        int any = 0;
        if (s != INVALID_HANDLE_VALUE && Process32FirstW(s, &pe)) do {
            if (_wcsicmp(pe.szExeFile, L"RobloxPlayerBeta.exe")) continue;
            any = 1;
            int known = 0;
            for (int i = 0; i < n; i++) if (pids[i] == pe.th32ProcessID) known = 1;
            if (known || n >= 16) continue;
            HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            if (!h) continue;
            crumb(L"Roblox started");
            FILETIME cr, ex, k, u;
            ULONGLONG st = 0;
            if (GetProcessTimes(h, &cr, &ex, &k, &u)) st = ((ULONGLONG)cr.dwHighDateTime << 32) | cr.dwLowDateTime;
            wchar_t img[MAX_PATH];
            DWORD il = MAX_PATH;
            launch[n][0] = 0;
            if (QueryFullProcessImageNameW(h, 0, img, &il)) {
                static const wchar_t *LN[] = {L"Bloxstrap", L"Fishstrap", L"Voidstrap"};
                for (int i = 0; i < 3; i++) {
                    wchar_t pat[40];
                    wsprintfW(pat, L"\\%s\\", LN[i]);
                    for (wchar_t *q = img; *q; q++) if (!_wcsnicmp(q, pat, wcslen(pat))) { wcscpy(launch[n], LN[i]); break; }
                }
            }
            hs[n] = h; pids[n] = pe.th32ProcessID; start[n] = st; n++;
        } while (Process32NextW(s, &pe));
        if (s != INVALID_HANDLE_VALUE) CloseHandle(s);
        /* RAM use while playing */
        if (any) {
            MEMORYSTATUSEX ms;
            ms.dwLength = sizeof(ms);
            if (GlobalMemoryStatusEx(&ms) && (int)ms.dwMemoryLoad > g_rbx_peak_mem) g_rbx_peak_mem = (int)ms.dwMemoryLoad;
        }
        DWORD r = n ? WaitForMultipleObjects(n, hs, FALSE, 2000) : (Sleep(2000), WAIT_TIMEOUT);
        if (r < WAIT_OBJECT_0 + (DWORD)n) {
            int i = (int)(r - WAIT_OBJECT_0);
            DWORD code = 0;
            GetExitCodeProcess(hs[i], &code);
            FILETIME now;
            GetSystemTimeAsFileTime(&now);
            ULONGLONG nw = ((ULONGLONG)now.dwHighDateTime << 32) | now.dwLowDateTime;
            DWORD ran = start[i] && nw > start[i] ? (DWORD)((nw - start[i]) / 10000000ULL) : 0;
            log_info(L"roblox ended: exit 0x%08lX after %lu s", (unsigned long)code, (unsigned long)ran);
            crumb(L"Roblox closed");
            if (rbx_is_crash_code(code)) {
                RbxEnd *e = (RbxEnd *)calloc(1, sizeof(RbxEnd));
                if (e) {
                    e->pid = pids[i]; e->code = code; e->ran_s = ran; e->peak = g_rbx_peak_mem;
                    wcscpy(e->launcher, launch[i]);
                    HANDLE th = CreateThread(NULL, 0, rbx_analyze_thread, e, 0, NULL);
                    if (th) CloseHandle(th); else free(e);
                }
            }
            CloseHandle(hs[i]);
            hs[i] = hs[n - 1]; pids[i] = pids[n - 1]; start[i] = start[n - 1]; wcscpy(launch[i], launch[n - 1]);
            n--;
            if (!n) g_rbx_peak_mem = 0;
        }
    }
    for (int i = 0; i < n; i++) CloseHandle(hs[i]);
    return 0;
}

/* On the main thread: tell the player, then send the report (if allowed). */
static void rbx_crash_report(RbxCrash *c) {
    log_warn(L"roblox crash: exit 0x%08lX after %lu s, likely: %s", (unsigned long)c->code, (unsigned long)c->ran_s, c->top);
    free(g_rbx_last);
    g_rbx_last = c;
    g_nid.uFlags = NIF_INFO;
    wcscpy(g_nid.szInfoTitle, L"Roblox crashed");
    wsprintfW(g_nid.szInfo, L"Likely cause: %.150s\nClick here for how to fix it.", c->top);
    g_nid.dwInfoFlags = NIIF_WARNING;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    g_nid.szInfo[0] = 0;
    g_balloon_rbx = 1;
    g_balloon_report = g_balloon_update = 0;
    if (!webhook_set() || g_reports == 2) return;
    if (g_rbx_sent_at && GetTickCount() - g_rbx_sent_at < 20 * 60 * 1000) { log_info(L"roblox crash report not sent (one was sent < 20 min ago)"); return; }
    if (g_reports == 0) {
        wchar_t q[700];
        wsprintfW(q, L"Roblox crashed. Likely cause: %s\n\nSend crash reports to the developer (simplysoul)? A report has: the crash code, "
                     L"your graphics card and driver, RAM and disk space, the error lines from Roblox's log (numbers and IPs hidden), "
                     L"and this app's settings and log. No passwords, no account info.\n\nYes = send this one and future ones automatically "
                     L"(turn off any time in General).\nNo = never send.", c->top);
        g_reports = MessageBoxW(NULL, q, L"Crosshair Overlay - Roblox crashed", MB_YESNO | MB_ICONQUESTION | MB_TOPMOST) == IDYES ? 1 : 2;
        save_settings();
        if (g_reports != 1) return;
    }
    wchar_t cat[200];
    wsprintfW(cat, L"Roblox crash \x2014 likely: %s", c->top);
    g_report_extra = c->text;
    if (report_send(3, cat, c->tips)) g_rbx_sent_at = GetTickCount();
    g_report_extra = NULL;
}
static void rbx_crash_details(void) {
    wchar_t m[2200];
    if (!g_rbx_last) return;
    wsprintfW(m, L"Roblox crashed (exit code 0x%08lX). Most likely causes and fixes:\r\n\r\n%.1800s", (unsigned long)g_rbx_last->code, g_rbx_last->tips);
    MessageBoxW(NULL, m, L"Crosshair Overlay - Roblox crashed", MB_OK | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND);
}

/* Cheap check so we only re-apply the blank cursor when Windows has reset it. */
static int arrow_is_blank(void) {
    /* Draw the current arrow cursor on black and on white; if nothing changes,
       it's our invisible cursor. Works for any cursor size or format. */
    HCURSOR arrow = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    if (!arrow) return 0;
    const int n = 256;
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = n;
    bi.bmiHeader.biHeight = -n;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void *bits = NULL;
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp || !bits) { if (bmp) DeleteObject(bmp); DeleteDC(dc); return 0; }
    HGDIOBJ old = SelectObject(dc, bmp);
    uint32_t *px = (uint32_t *)bits;
    int blank = 1;
    for (int bg = 0; bg < 2 && blank; bg++) {
        uint32_t fill = bg ? 0x00FFFFFFu : 0u;
        for (int i = 0; i < n * n; i++) px[i] = fill;
        GdiFlush();
        DrawIconEx(dc, 0, 0, arrow, 0, 0, 0, NULL, DI_NORMAL);
        GdiFlush();
        for (int i = 0; i < n * n; i++)
            if ((px[i] & 0x00FFFFFFu) != fill) { blank = 0; break; }
    }
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
    return blank;
}

/* Hide the cursor only when the option is on and the crosshair is visible. */
static void sync_cursor(void) {
    int want = S.hidecur && !S.hidden && !g_focus_hidden && !g_scene_hidden && !g_keydlg && !g_rdlg && !g_pdlg && !g_wndlg;
    if (want) blank_cursor();
    else if (g_cursor_hidden) restore_cursor();
}

/* ---- crash reports in plain words ----
   1. which function crashed: looked up in the exe's own table of function names (symbols.bin, resource 15)
   2. what kind of crash, in words
   3. what the player did just before (breadcrumbs)
   4. if it crashed inside another program's DLL (graphics driver, overlay, antivirus), which one and what to try */
#define NCRUMB 12
static struct { DWORD t; wchar_t s[72]; } g_crumb[NCRUMB];
static volatile LONG g_ncrumb;
static void crumb(const wchar_t *fmt, ...) {
    LONG i = InterlockedIncrement(&g_ncrumb) - 1;
    va_list ap;
    va_start(ap, fmt);
    wvsprintfW(g_crumb[i % NCRUMB].s, fmt, ap);
    va_end(ap);
    g_crumb[i % NCRUMB].s[71] = 0;
    g_crumb[i % NCRUMB].t = GetTickCount();
}
/* "opened the settings panel (40s ago) > Themes tab (12s ago) > clicked Aurora (2s ago)" - newest last */
static void crumbs_text(wchar_t *out, int cap, int maxn) {
    LONG n = g_ncrumb;
    int first = n > maxn ? n - maxn : 0;
    DWORD now = GetTickCount();
    out[0] = 0;
    for (LONG i = first; i < n; i++) {
        wchar_t one[120];
        if (!wcscmp(g_crumb[i % NCRUMB].s, L"opened Report a problem")) continue;
        DWORD ago = (now - g_crumb[i % NCRUMB].t) / 1000;
        if (ago < 120) wsprintfW(one, L"%s%s (%lus ago)", out[0] ? L" > " : L"", g_crumb[i % NCRUMB].s, (unsigned long)ago);
        else wsprintfW(one, L"%s%s (%lu min ago)", out[0] ? L" > " : L"", g_crumb[i % NCRUMB].s, (unsigned long)(ago / 60));
        if (lstrlenW(out) + lstrlenW(one) >= cap - 1) break;
        lstrcatW(out, one);
    }
}
/* function name for an address in the exe (from symbols.bin). off = bytes into the function. */
static int sym_lookup(ULONG_PTR rva, char *name, int cap, ULONG_PTR *off) {
    HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(15), (LPCWSTR)RT_RCDATA);
    HGLOBAL g = r ? LoadResource(NULL, r) : NULL;
    DWORD sz = r ? SizeofResource(NULL, r) : 0;
    const char *d = g ? (const char *)LockResource(g) : NULL;
    ULONG_PTR best = 0;
    const char *bn = NULL;
    name[0] = 0;
    if (!d) return 0;
    for (DWORD i = 0; i < sz;) {
        ULONG_PTR v = 0;
        DWORD j = i;
        while (j < sz && ((d[j] >= '0' && d[j] <= '9') || (d[j] >= 'a' && d[j] <= 'f'))) { v = v * 16 + (ULONG_PTR)(d[j] <= '9' ? d[j] - '0' : d[j] - 'a' + 10); j++; }
        if (j == i) { i++; continue; }
        if (v > rva) break;
        best = v;
        bn = d + j + 1;
        while (j < sz && d[j] != '\n') j++;
        i = j + 1;
    }
    if (!bn) return 0;
    int k = 0;
    while (bn[k] && bn[k] != '\n' && k < cap - 1) { name[k] = bn[k]; k++; }
    name[k] = 0;
    *off = rva - best;
    return 1;
}
/* which part of the app a function belongs to, in words */
static const wchar_t *func_area(const char *f) {
    static const struct { const char *pre; const wchar_t *area; } A[] = {
        {"tab_", L"drawing the settings window"}, {"ui_", L"the settings window"}, {"draw_", L"drawing the settings window"},
        {"setting_row", L"drawing the settings window"}, {"p_", L"drawing the settings window"}, {"q_text", L"drawing text"},
        {"flush_text", L"drawing text"}, {"fit_font", L"drawing text"}, {"make_fonts", L"loading fonts"}, {"art_", L"drawing the theme background"},
        {"ui_deco", L"drawing the theme background"}, {"theme", L"applying a theme"}, {"splash", L"showing the loading screen"},
        {"music", L"playing the loading music"}, {"render", L"drawing the crosshair"}, {"overlay", L"drawing the crosshair"},
        {"build_shape", L"drawing the crosshair"}, {"p_crosshair", L"drawing the crosshair"}, {"apply", L"applying crosshair settings"},
        {"update", L"checking for or installing an update"}, {"upd_", L"installing an update"}, {"probation", L"checking a new update"}, {"gh_", L"talking to GitHub"},
        {"https", L"connecting to the internet"}, {"tester", L"checking the tester list"}, {"news", L"loading announcements"},
        {"stats", L"sending usage stats"}, {"report", L"building or sending a bug report"}, {"build_report", L"building a bug report"},
        {"rd_", L"the Report a problem window"}, {"rbx", L"Roblox settings (FPS Boost)"}, {"roblox", L"watching Roblox"},
        {"fps", L"FPS Boost"}, {"boost", L"FPS Boost"}, {"perf", L"FPS Boost"}, {"kb_", L"keybinds"}, {"hotkey", L"keybinds"},
        {"register_hotkeys", L"keybinds"}, {"on_hotkey", L"running a keybind"}, {"card_", L"the keybinds card"},
        {"focus", L"checking which window is in front"}, {"cursor", L"hiding or showing the mouse cursor"},
        {"sync_cursor", L"hiding or showing the mouse cursor"}, {"profile", L"profiles"}, {"xh_code", L"share codes"},
        {"recent", L"recent codes"}, {"clip_", L"the clipboard"}, {"search", L"settings search"}, {"tip", L"hover tips"},
        {"tour", L"the first-run tour"}, {"wn_", L"the What's new window"}, {"whatsnew", L"the What's new window"},
        {"load_settings", L"reading settings"}, {"save_settings", L"saving settings"}, {"ini_", L"reading or saving settings"},
        {"log_", L"writing the log"}, {"tray", L"the tray icon"}, {"host_proc", L"handling a Windows message"},
        {"WinMain", L"starting up"}, {"wWinMain", L"starting up"}, {"uninstall", L"Remove everything"}, {"publish", L"publishing"},
    };
    for (int i = 0; i < (int)(sizeof(A) / sizeof(A[0])); i++) if (!strncmp(f, A[i].pre, strlen(A[i].pre))) return A[i].area;
    return L"the app's code";
}
/* known DLLs that other programs put into every app, and what to try */
static const wchar_t *dll_hint(const wchar_t *m, const wchar_t **what) {
    static const struct { const wchar_t *part, *what, *fix; } D[] = {
        {L"nvogl", L"the NVIDIA graphics driver", L"Update your NVIDIA driver (GeForce Experience / nvidia.com)."},
        {L"nvwgf", L"the NVIDIA graphics driver", L"Update your NVIDIA driver (GeForce Experience / nvidia.com)."},
        {L"nvd3d", L"the NVIDIA graphics driver", L"Update your NVIDIA driver (GeForce Experience / nvidia.com)."},
        {L"igd", L"the Intel graphics driver", L"Update your Intel graphics driver (Intel Driver & Support Assistant)."},
        {L"ig9", L"the Intel graphics driver", L"Update your Intel graphics driver (Intel Driver & Support Assistant)."},
        {L"amdx", L"the AMD graphics driver", L"Update your AMD driver (AMD Software: Adrenalin)."},
        {L"atio", L"the AMD graphics driver", L"Update your AMD driver (AMD Software: Adrenalin)."},
        {L"aticfx", L"the AMD graphics driver", L"Update your AMD driver (AMD Software: Adrenalin)."},
        {L"rtsshooks", L"RivaTuner / MSI Afterburner's overlay", L"Add CrosshairOverlay.exe to RivaTuner's list with Application detection level set to None."},
        {L"gameoverlayrenderer", L"Steam's overlay", L"This is Steam's overlay; it shouldn't hook this app. Restart Steam or turn its overlay off."},
        {L"discord", L"Discord's overlay", L"Turn off Discord's in-game overlay (Settings > Game Overlay)."},
        {L"graphics-hook", L"OBS's game capture", L"In OBS, use Window or Display capture instead of Game Capture for Roblox."},
        {L"obs-", L"OBS", L"In OBS, use Window or Display capture instead of Game Capture for Roblox."},
        {L"nahimic", L"Nahimic audio software", L"Nahimic is known to crash other apps; update or turn off Nahimic."},
        {L"ss3", L"SteelSeries software", L"Update SteelSeries GG / Sonar."},
        {L"medal", L"Medal's overlay", L"Turn off Medal's overlay."},
        {L"overwolf", L"Overwolf", L"Turn off Overwolf's overlay."},
        {L"asw", L"Avast / AVG antivirus", L"Add an exception for CrosshairOverlay.exe in your antivirus."},
        {L"bdh", L"Bitdefender antivirus", L"Add an exception for CrosshairOverlay.exe in your antivirus."},
        {L"klh", L"Kaspersky antivirus", L"Add an exception for CrosshairOverlay.exe in your antivirus."},
        {L"d3d", L"DirectX (graphics)", L"Update your graphics driver and Windows."},
        {L"dxgi", L"DirectX (graphics)", L"Update your graphics driver and Windows."},
        {L"winhttp", L"Windows' internet code", L"Usually a network glitch. If it keeps happening, tell simplysoul."},
    };
    wchar_t low[64];
    int k = 0;
    for (; m[k] && k < 63; k++) low[k] = towlower(m[k]);
    low[k] = 0;
    for (int i = 0; i < (int)(sizeof(D) / sizeof(D[0])); i++)
        if (wcsstr(low, D[i].part)) { *what = D[i].what; return D[i].fix; }
    *what = NULL;
    return NULL;
}
/* the first frame of the crashed call stack that's inside our exe (so a crash in a Windows DLL still says what called it) */
static int exe_frames(CONTEXT *ctx0, ULONG_PTR base, ULONG_PTR size, ULONG_PTR *out, int max) {
    static CONTEXT c;
    int n = 0;
    c = *ctx0;
    for (int i = 0; i < 40 && n < max; i++) {
        DWORD64 pc = c.Rip, ib = 0;
        if (!pc) break;
        if (pc >= base && pc < base + size) out[n++] = (ULONG_PTR)(pc - base);
        PRUNTIME_FUNCTION f = RtlLookupFunctionEntry(pc, &ib, NULL);
        if (!f) {
            if (IsBadReadPtr((void *)c.Rsp, 8)) break;
            c.Rip = *(DWORD64 *)c.Rsp;
            c.Rsp += 8;
        } else {
            PVOID hd = NULL;
            DWORD64 ef = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, ib, pc, f, &c, &hd, &ef, NULL);
        }
    }
    return n;
}

static LONG WINAPI crash_handler(EXCEPTION_POINTERS *e) {
    restore_cursor();
    if (g_probation) { /* a fresh update crashed: the next start goes back to the previous version */
        wchar_t pp[MAX_PATH];
        probation_path(pp);
        if (pp[0]) WritePrivateProfileStringW(L"probation", L"crashed", L"1", pp);
    }
    { /* note it in the log (in plain words) and send a report on the next start */
        static LONG once;
        static wchar_t m[600], why[300], before[500], chain[300], modn[MAX_PATH];
        static char fn[80];
        if (!InterlockedExchange(&once, 1) && e && e->ExceptionRecord) {
            EXCEPTION_RECORD *x = e->ExceptionRecord;
            DWORD code = x->ExceptionCode;
            ULONG_PTR addr = (ULONG_PTR)x->ExceptionAddress, base = (ULONG_PTR)GetModuleHandleW(NULL), size = 0x1000000, off = 0;
            IMAGE_DOS_HEADER *dh = (IMAGE_DOS_HEADER *)base;
            IMAGE_NT_HEADERS *nh = (IMAGE_NT_HEADERS *)(base + dh->e_lfanew);
            if (dh->e_magic == IMAGE_DOS_SIGNATURE && nh->Signature == IMAGE_NT_SIGNATURE) size = nh->OptionalHeader.SizeOfImage;
            /* what kind of crash */
            const wchar_t *fix = NULL;
            if (code == EXCEPTION_ACCESS_VIOLATION && x->NumberParameters >= 2) {
                ULONG_PTR rw = x->ExceptionInformation[0], at = x->ExceptionInformation[1];
                if (at < 0x10000) wsprintfW(why, L"it used something that wasn't set up yet (empty pointer, %s address 0x%lx)", rw == 1 ? L"writing" : L"reading", (unsigned long)at);
                else if (rw == 8) wsprintfW(why, L"it jumped to memory that isn't code (address 0x%p)", (void *)at);
                else wsprintfW(why, L"it %s memory it wasn't allowed to (address 0x%p)", rw == 1 ? L"wrote to" : L"read", (void *)at);
            } else if (code == EXCEPTION_STACK_OVERFLOW) wcscpy(why, L"it ran out of stack space (something kept calling itself)");
            else if (code == EXCEPTION_INT_DIVIDE_BY_ZERO) wcscpy(why, L"it divided a number by zero");
            else if (code == EXCEPTION_INT_OVERFLOW) wcscpy(why, L"a number got too big");
            else if (code == 0xC0000409) wcscpy(why, L"a safety check found that memory had been overwritten");
            else if (code == 0xC0000374) wcscpy(why, L"its memory got corrupted (heap corruption)");
            else if (code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_PRIV_INSTRUCTION) wcscpy(why, L"the processor couldn't run one of its instructions (very old CPU?)");
            else if (code == EXCEPTION_IN_PAGE_ERROR) { wcscpy(why, L"Windows couldn't read part of the app file from disk"); fix = L"Keep CrosshairOverlay.exe on your PC's own drive (not a USB stick or network drive) and check your antivirus didn't quarantine it."; }
            else if (code == 0xC0000017 || code == 0xC000012D) { wcscpy(why, L"the PC ran out of memory"); fix = L"Close some programs (or browser tabs) and try again."; }
            else if (code == EXCEPTION_BREAKPOINT) wcscpy(why, L"it hit a debugging breakpoint");
            else wsprintfW(why, L"Windows error code 0x%08lx", (unsigned long)code);
            /* where */
            int inexe = addr >= base && addr - base < size;
            const wchar_t *dllwhat = NULL;
            modn[0] = 0;
            if (!inexe) {
                HMODULE hm = NULL;
                if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)addr, &hm) && hm) {
                    wchar_t full[MAX_PATH];
                    if (GetModuleFileNameW(hm, full, MAX_PATH)) { wchar_t *b = wcsrchr(full, L'\\'); lstrcpynW(modn, b ? b + 1 : full, MAX_PATH); }
                }
                if (modn[0]) { const wchar_t *f2 = dll_hint(modn, &dllwhat); if (f2) fix = f2; }
            }
            ULONG_PTR fr[6];
            int nf = e->ContextRecord ? exe_frames(e->ContextRecord, base, size, fr, 6) : 0;
            if (inexe && !nf) { fr[0] = addr - base; nf = 1; }
            const wchar_t *area = L"the app's code";
            fn[0] = 0;
            if (nf && sym_lookup(fr[0], fn, sizeof(fn), &off)) area = func_area(fn);
            chain[0] = 0;
            for (int i = 0; i < nf && i < 4; i++) {
                char f2[80];
                ULONG_PTR o2 = 0;
                wchar_t one[100];
                if (sym_lookup(fr[i], f2, sizeof(f2), &o2)) wsprintfW(one, L"%s%S+0x%lx", i ? L" < " : L"", f2, (unsigned long)o2);
                else wsprintfW(one, L"%sexe+0x%lx", i ? L" < " : L"", (unsigned long)fr[i]);
                if (lstrlenW(chain) + lstrlenW(one) < 290) lstrcatW(chain, one);
            }
            if (inexe) wsprintfW(m, L"CRASH: the app crashed while %s (in %S)", area, fn[0] ? fn : "unknown code");
            else if (dllwhat) wsprintfW(m, L"CRASH: the app crashed inside %s (%s), while %s", dllwhat, modn, area);
            else wsprintfW(m, L"CRASH: the app crashed inside %s, while %s", modn[0] ? modn : L"another program's code", area);
            log_warn(L"%s", m);
            log_warn(L"CRASH why: %s", why);
            crumbs_text(before, 500, 6);
            if (before[0]) log_warn(L"CRASH before: %s", before);
            log_warn(L"CRASH fix: %s", fix ? fix : !inexe && !dllwhat ? L"Another program's code crashed inside this app. Note which programs you run (overlays, antivirus, RGB/audio tools) and tell simplysoul."
                                                                     : L"This is a bug in the app \x2014 simplysoul has what's needed to fix it.");
            log_warn(L"CRASH detail: exception 0x%08lx at %s+0x%lx, stack %s", (unsigned long)code, inexe ? L"exe" : (modn[0] ? modn : L"?"),
                     (unsigned long)(inexe ? addr - base : 0), chain[0] ? chain : L"-");
            if (g_pendp[0]) { DeleteFileW(g_pendp); append_utf8(g_pendp, L"crash"); }
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* ---------------- shape building (signed distance fields) ---------------- */
/* faint = drawn 1 px thin and see-through (for parts that shouldn't block your view) */
typedef struct { int type; float ax, ay, bx, by, r; int faint; } Prim; /* 0 bar, 1 ring, 2 disc, 3 rounded line, 4 disc at (ax, ay) */
#define MAXPRIM 48
static Prim P[MAXPRIM];
static int NP, g_faint;

static void add_prim(Prim p) { p.faint = g_faint; if (NP < MAXPRIM) P[NP++] = p; }
static void bar(float ax, float ay, float bx, float by) { Prim p = {0, ax, ay, bx, by, 0, 0}; add_prim(p); }
static void ring(float r) { Prim p = {1, 0, 0, 0, 0, r, 0}; add_prim(p); }
static void disc(float r) { Prim p = {2, 0, 0, 0, 0, r, 0}; add_prim(p); }
static void disc_at(float x, float y, float r) { Prim p = {4, x, y, 0, 0, r, 0}; add_prim(p); }
/* rounded-end line: joins cleanly at any angle, used for outlines */
static void cap(float ax, float ay, float bx, float by) { Prim p = {3, ax, ay, bx, by, 0, 0}; add_prim(p); }
/* part of a circle of radius r from angle a0 to a1, made of short rounded lines */
static void arc_caps(float r, float a0, float a1, int segs) {
    for (int i = 0; i < segs; i++) {
        float u = a0 + (a1 - a0) * i / segs, v = a0 + (a1 - a0) * (i + 1) / segs;
        cap(r * cosf(u), r * sinf(u), r * cosf(v), r * sinf(v));
    }
}
/* closed outline through n points on a circle of radius r, starting at angle a0 */
static void polygon(int n, float r, float a0) {
    float px = r * cosf(a0), py = r * sinf(a0);
    for (int i = 1; i <= n; i++) {
        float a = a0 + 6.2831853f * i / n;
        float qx = r * cosf(a), qy = r * sinf(a);
        cap(px, py, qx, qy);
        px = qx; py = qy;
    }
}

static void gap_plus(float g, float L, int top) {
    bar(g, 0, g + L, 0);
    bar(-g, 0, -g - L, 0);
    bar(0, g, 0, g + L);
    if (top) bar(0, -g, 0, -g - L);
}

static void build_shape(int style, float L, float t) {
    float g = 2.0f + L * 0.3f + t * 0.5f; /* gap grows with size */
    float dot = t * 0.5f + 1.0f;
    NP = 0;
    switch (style) {
    case 0: disc(fmaxf(2.0f, L * 0.3f + t * 0.5f)); break;
    case 1: bar(-L, 0, L, 0); bar(0, -L, 0, L); break;
    case 2: gap_plus(g, L, 1); break;
    case 3: gap_plus(g, L, 1); disc(dot); break;
    case 4: ring(L); break;
    case 5: ring(L); disc(dot); break;
    case 6: ring(L); gap_plus(L * 0.45f, L * 0.95f, 1); disc(dot * 0.8f); break;
    case 7: gap_plus(g, L, 0); disc(dot); break;
    case 8: {
        float d = 0.7071f;
        bar(g * d, g * d, (g + L) * d, (g + L) * d);
        bar(-g * d, g * d, -(g + L) * d, (g + L) * d);
        bar(g * d, -g * d, (g + L) * d, -(g + L) * d);
        bar(-g * d, -g * d, -(g + L) * d, -(g + L) * d);
        break;
    }
    case 9: bar(0, 0, -L, L * 0.8f); bar(0, 0, L, L * 0.8f); break;
    case 10: case 11: {                       /* Square, Square + Dot */
        float q = L * 0.8f;
        cap(-q, -q, q, -q); cap(q, -q, q, q); cap(q, q, -q, q); cap(-q, q, -q, -q);
        if (style == 11) disc(dot);
        break;
    }
    case 12: polygon(4, L, -1.5707963f); disc(dot); break;        /* Diamond */
    case 13: polygon(3, L, -1.5707963f); disc(dot); break;        /* Triangle */
    case 14: {                                                     /* Brackets [ ] */
        float y = L * 0.7f, in = L * 0.45f;
        cap(-L, -y, -L, y); cap(-L, -y, -in, -y); cap(-L, y, -in, y);
        cap(L, -y, L, y);   cap(L, -y, in, -y);   cap(L, y, in, y);
        disc(dot);
        break;
    }
    case 15: ring(L); ring(L * 0.5f); disc(dot * 0.8f); break;   /* Double Circle */
    case 16: {                                                     /* Corners */
        float q = L * 0.8f, a = L * 0.38f;
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sy = -1; sy <= 1; sy += 2) {
                cap(sx * q, sy * q, sx * (q - a), sy * q);
                cap(sx * q, sy * q, sx * q, sy * (q - a));
            }
        disc(dot);
        break;
    }
    case 17: polygon(6, L, 0.0f); disc(dot); break;               /* Hexagon */
    case 18: disc(L * 0.8f + t * 0.5f); break;                    /* Solid Circle */
    case 19: {                                                     /* X + Dot */
        float d = 0.7071f;
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sy = -1; sy <= 1; sy += 2) bar(sx * g * d, sy * g * d, sx * (g + L) * d, sy * (g + L) * d);
        disc(dot);
        break;
    }
    case 20: ring(L * 0.62f); gap_plus(L * 0.8f, L * 0.42f, 1); disc(dot * 0.8f); break; /* Reticle: ticks outside the ring */
    case 21: {                                                     /* Mil-Dot */
        bar(-L * 1.1f, 0, L * 1.1f, 0); bar(0, -L * 1.1f, 0, L * 1.1f);
        float r = t * 0.5f + 1.3f;
        for (int k = 1; k <= 2; k++) {
            float o = L * 0.38f * k;
            disc_at(o, 0, r); disc_at(-o, 0, r); disc_at(0, o, r); disc_at(0, -o, r);
        }
        break;
    }
    case 22:                                                       /* Split Circle */
        for (int k = 0; k < 4; k++) {
            float c = k * 1.5707963f + 0.7853982f;
            arc_caps(L, c - 0.52f, c + 0.52f, 5);
        }
        disc(dot);
        break;
    case 23:                                                       /* Dot Ring */
        for (int k = 0; k < 8; k++) disc_at(L * cosf(k * 0.7853982f), L * sinf(k * 0.7853982f), dot * 0.9f);
        disc(dot);
        break;
    case 24: {                                                     /* Tri */
        static const float DX[3] = {0.0f, 0.8660254f, -0.8660254f}, DY[3] = {-1.0f, 0.5f, 0.5f};
        for (int k = 0; k < 3; k++) bar(DX[k] * g, DY[k] * g, DX[k] * (g + L), DY[k] * (g + L));
        disc(dot);
        break;
    }
    case 25: {                                                     /* Star */
        float px = 0, py = -L;
        for (int i = 1; i <= 10; i++) {
            float a = -1.5707963f + i * 0.6283185f, r = (i & 1) ? L * 0.45f : L;
            float qx = r * cosf(a), qy = r * sinf(a);
            cap(px, py, qx, qy);
            px = qx; py = qy;
        }
        break;
    }
    case 26: polygon(5, L, -1.5707963f); disc(dot); break;        /* Pentagon */
    case 27: polygon(8, L, 0.3926991f); disc(dot); break;         /* Octagon */
    case 28: bar(g, 0, g + L * 1.2f, 0); bar(-g, 0, -g - L * 1.2f, 0); disc(dot); break; /* Line */
    case 30: ring(L * 1.15f); disc(fmaxf(1.3f, dot * 0.6f)); break; /* Taser: wide ring, small dot */
    case 31:                                                       /* Shotgun Spread */
        ring(L * 0.85f);
        disc(fmaxf(1.3f, dot * 0.6f));
        g_faint = 1; /* subtle outer dashes */
        for (int k = 0; k < 8; k++) {
            float a = k * 0.7853982f + 0.3926991f;
            cap(L * 1.08f * cosf(a), L * 1.08f * sinf(a), L * 1.32f * cosf(a), L * 1.32f * sinf(a));
        }
        g_faint = 0;
        break;
    case 32:                                                       /* Holographic: big ring, 1 MOA dot */
        ring(L * 1.1f);
        bar(0, -L * 1.1f, 0, -L * 0.88f); bar(0, L * 1.1f, 0, L * 0.88f);
        bar(-L * 1.1f, 0, -L * 0.88f, 0); bar(L * 1.1f, 0, L * 0.88f, 0);
        disc(1.2f);
        break;
    case 33:                                                       /* Cross-Dot: tiny dot, faint thin lines */
        disc(fmaxf(1.4f, t * 0.45f));
        g_faint = 1;
        gap_plus(g * 1.3f, L * 1.1f, 1);
        g_faint = 0;
        break;
    case 29: {                                                     /* Plus + Corners */
        gap_plus(g * 0.8f, L * 0.55f, 1);
        float q = L * 1.0f, a = L * 0.32f;
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sy = -1; sy <= 1; sy += 2) {
                cap(sx * q, sy * q, sx * (q - a), sy * q);
                cap(sx * q, sy * q, sx * q, sy * (q - a));
            }
        break;
    }
    }
}

static float sdist_sel(float px, float py, float h, int faint) {
    float best = 1e9f;
    for (int i = 0; i < NP; i++) {
        const Prim *p = &P[i];
        float d;
        if (p->faint != faint) continue;
        if (p->type == 0) {
            float dx = p->bx - p->ax, dy = p->by - p->ay;
            float len = sqrtf(dx * dx + dy * dy);
            if (len < 1e-3f) len = 1e-3f;
            float ux = dx / len, uy = dy / len;
            float qx = px - (p->ax + p->bx) * 0.5f, qy = py - (p->ay + p->by) * 0.5f;
            float a = fabsf(qx * ux + qy * uy) - len * 0.5f;
            float b = fabsf(-qx * uy + qy * ux) - h;
            float ox = a > 0 ? a : 0, oy = b > 0 ? b : 0;
            d = sqrtf(ox * ox + oy * oy) + fminf(fmaxf(a, b), 0.0f);
        } else if (p->type == 3) {
            float dx = p->bx - p->ax, dy = p->by - p->ay;
            float wx = px - p->ax, wy = py - p->ay;
            float k = (wx * dx + wy * dy) / fmaxf(dx * dx + dy * dy, 1e-6f);
            k = k < 0 ? 0 : k > 1 ? 1 : k;
            float ex = wx - dx * k, ey = wy - dy * k;
            d = sqrtf(ex * ex + ey * ey) - h;
        } else if (p->type == 1) {
            d = fabsf(sqrtf(px * px + py * py) - p->r) - h;
        } else if (p->type == 4) {
            float dx = px - p->ax, dy = py - p->ay;
            d = sqrtf(dx * dx + dy * dy) - p->r;
        } else {
            d = sqrtf(px * px + py * py) - p->r;
        }
        if (d < best) best = d;
    }
    return best;
}

static float sdist(float px, float py, float h) { return sdist_sel(px, py, h, 0); }

static float cov(float d) { float a = 0.5f - d; return a < 0 ? 0 : a > 1 ? 1 : a; }
static BYTE g_dyn_r = 255, g_dyn_g, g_dyn_b; /* color used when render() is given color -1 (rainbow) */

/* Render into a top-down BGRA buffer. premul=1 for layered windows, 0 for icons. */
static void render(uint32_t *buf, int w, int hgt, int style, int color, float L,
                   int thick, int outline, float opacity, int premul) {
    float h = thick * 0.5f;
    float off = 0.0f; /* every shape is centered on the middle of the mouse's pixel; even widths get soft edges instead of a half-pixel shift */
    float ow = 1.5f;
    float cx = (float)(w / 2), cy = (float)(hgt / 2);
    BYTE r, g, b;
    if (color < 0) { r = g_dyn_r; g = g_dyn_g; b = g_dyn_b; }
    else { r = COLORS[color].r; g = COLORS[color].g; b = COLORS[color].b; }
    int glow = S.glow;
    float gw = 2.5f + thick * 0.6f; /* glow falloff distance */
    /* black outline normally; white outline for very dark colors */
    float oc = (0.3f * r + 0.59f * g + 0.11f * b) < 70.0f ? 255.0f : 0.0f;
    build_shape(style, L, (float)thick);
    int has_faint = 0;
    for (int i = 0; i < NP; i++) has_faint |= P[i].faint;
    for (int y = 0; y < hgt; y++) {
        for (int x = 0; x < w; x++) {
            float d = sdist(x - cx + off, y - cy + off, h);
            float af = cov(d);
            if (has_faint) { /* thin see-through parts: 1 px wide, about half strength, no outline */
                float aff = cov(sdist_sel(x - cx + off, y - cy + off, 0.5f, 1)) * 0.5f; /* same center as the rest */
                af = af + aff * (1.0f - af);
            }
            float ao = outline ? cov(d - ow) : 0.0f;
            float A = (af + ao * (1.0f - af)) * opacity;
            float ga = (glow && d > 0) ? 0.55f * expf(-d / gw) * opacity * (1.0f - A) : 0.0f; /* soft halo */
            float At = A + ga;
            uint32_t px = 0;
            if (At > 0.002f) {
                float wf = af * opacity, wo = ao * (1.0f - af) * opacity; /* fill / outline weights */
                float div = premul ? 1.0f : At;
                float fr = (r * (wf + ga) + oc * wo) / div, fg = (g * (wf + ga) + oc * wo) / div,
                      fb = (b * (wf + ga) + oc * wo) / div;
                uint32_t R = (uint32_t)(fminf(fr, 255.0f) + 0.5f), G = (uint32_t)(fminf(fg, 255.0f) + 0.5f),
                         B = (uint32_t)(fminf(fb, 255.0f) + 0.5f), AA = (uint32_t)(fminf(At, 1.0f) * 255 + 0.5f);
                px = (AA << 24) | (R << 16) | (G << 8) | B;
            }
            buf[y * w + x] = px;
        }
    }
}

/* ---------------- overlay drawing ---------------- */
static void target_point(POINT *p);

/* ---- effects: rainbow / pulse colors and the click reaction ---- */
static float g_kick;           /* 1 right after a click, fades to 0 */
static DWORD g_kick_t;
static const float ANIM_SPEEDS[3] = {0.5f, 1.0f, 2.0f};
static void hsv_rgb(float h, BYTE *r, BYTE *g, BYTE *b) {
    h -= floorf(h);
    float x = h * 6.0f, f = x - floorf(x), q = 1.0f - f, R, G, B;
    switch ((int)x % 6) {
    case 0: R = 1; G = f; B = 0; break;
    case 1: R = q; G = 1; B = 0; break;
    case 2: R = 0; G = 1; B = f; break;
    case 3: R = 0; G = q; B = 1; break;
    case 4: R = f; G = 0; B = 1; break;
    default: R = 1; G = 0; B = q; break;
    }
    *r = (BYTE)(R * 255); *g = (BYTE)(G * 255); *b = (BYTE)(B * 255);
}
/* Color to draw with right now (-1 = rainbow color) and an opacity multiplier for pulse. */
static int fx_color(float *opac_mul) {
    float t = (GetTickCount() % 3600000u) / 1000.0f, sp = ANIM_SPEEDS[S.anim_speed];
    *opac_mul = 1.0f;
    if (S.anim == 1) { hsv_rgb(t * 0.15f * sp, &g_dyn_r, &g_dyn_g, &g_dyn_b); return -1; }
    if (S.anim == 2) *opac_mul = 0.45f + 0.55f * (0.5f + 0.5f * sinf(t * 3.0f * sp));
    if (S.auto_contrast && g_ac_alt) { /* it would blend into what's behind it: white or black instead */
        BYTE v = g_ac_alt == 1 ? 255 : 16;
        g_dyn_r = g_dyn_g = g_dyn_b = v;
        return -1;
    }
    return S.color;
}
static float fx_scale(void) { float k = g_kick; return 1.0f + 0.4f * k * k * (3.0f - 2.0f * k); }
static void kick_update(void) {
    DWORD now = GetTickCount();
    float dt = (now - g_kick_t) / 1000.0f;
    g_kick_t = now;
    if (dt > 0.1f) dt = 0.1f;
    if (g_kick > 0) { g_kick -= dt / 0.25f; if (g_kick < 0) g_kick = 0; }
}
static void kick(void) { g_kick = 1.0f; g_kick_t = GetTickCount(); }

static float shape_extent(int style, float L, float t) {
    float ext = 0;
    build_shape(style, L, t);
    for (int i = 0; i < NP; i++) {
        const Prim *q = &P[i];
        float m = (q->type == 0 || q->type == 3) ? fmaxf(fmaxf(fabsf(q->ax), fabsf(q->ay)), fmaxf(fabsf(q->bx), fabsf(q->by)))
                  : q->type == 4 ? sqrtf(q->ax * q->ax + q->ay * q->ay) + q->r : q->r;
        if (m > ext) ext = m;
    }
    return ext;
}
static int needed_size(void) {
    float k = S.click_fx ? 1.4f : 1.0f, t = (float)S.thick;
    float ext = shape_extent(S.style, (float)SIZES[S.size] * k, t);
    if (S.l2_style >= 0) ext = fmaxf(ext, shape_extent(S.l2_style, (float)SIZES[S.l2_size] * k, t));
    int half = (int)ceilf(ext + t * 0.5f + 1.5f + 3.0f);
    if (S.glow) half += (int)ceilf(4.0f * (2.5f + t * 0.6f));
    if (S.cover && COVER_RADII[S.cover] + 3 > half) half = COVER_RADII[S.cover] + 3;
    int w = half * 2;
    if (w < 16) w = 16;
    if (w > WSZ_MAX) w = WSZ_MAX;
    return w;
}

/* Draw the second layer on top of what's in buf (premultiplied "over"). */
static void layer2_over(uint32_t *buf, int W, float om) {
    static uint32_t *tmp;
    static int cap;
    if (W * W > cap) { free(tmp); tmp = (uint32_t *)malloc(sizeof(uint32_t) * W * W); cap = tmp ? W * W : 0; }
    if (!tmp) return;
    render(tmp, W, W, S.l2_style, S.l2_color, (float)SIZES[S.l2_size] * fx_scale(), S.thick, S.outline, OPACITIES[S.opacity] / 100.0f * om, 1);
    for (int i = 0; i < W * W; i++) {
        uint32_t t = tmp[i], A = t >> 24;
        if (!A) continue;
        uint32_t b = buf[i], inv = 255 - A;
        uint32_t a2 = A + ((b >> 24) * inv + 127) / 255, r2 = ((t >> 16) & 255) + (((b >> 16) & 255) * inv + 127) / 255,
                 g2 = ((t >> 8) & 255) + (((b >> 8) & 255) * inv + 127) / 255, b2 = (t & 255) + ((b & 255) * inv + 127) / 255;
        buf[i] = (min(a2, 255u) << 24) | (min(r2, 255u) << 16) | (min(g2, 255u) << 8) | min(b2, 255u);
    }
}

static void redraw_overlay(void) {
    int W = needed_size();
    HDC screen = GetDC(NULL);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -W;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void *bits = NULL;
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HGDIOBJ old = SelectObject(mem, bmp);

    float om;
    int col = fx_color(&om);
    render((uint32_t *)bits, W, W, S.style, col, (float)SIZES[S.size] * fx_scale(), S.thick,
           S.outline, OPACITIES[S.opacity] / 100.0f * om, 1);
    if (S.l2_style >= 0) layer2_over((uint32_t *)bits, W, om);
    if (S.cover) { /* solid patch underneath, fully opaque so nothing shows through */
        uint32_t *px = (uint32_t *)bits;
        float R = (float)COVER_RADII[S.cover], c0 = W / 2.0f + 0.5f; /* same center as the crosshair */
        uint32_t cr = COVER_COLORS[S.cover_color].r, cg = COVER_COLORS[S.cover_color].g,
                 cb = COVER_COLORS[S.cover_color].b;
        for (int y = 0; y < W; y++)
            for (int x = 0; x < W; x++) {
                float dx = x + 0.5f - c0, dy = y + 0.5f - c0;
                float ca = R + 0.5f - sqrtf(dx * dx + dy * dy);
                if (ca <= 0) continue;
                if (ca > 1) ca = 1;
                uint32_t p = px[y * W + x], A = p >> 24, inv = 255 - A;
                uint32_t ka = (uint32_t)(ca * 255.0f + 0.5f) * inv / 255; /* cover where crosshair isn't */
                uint32_t nA = A + ka;
                uint32_t nR = ((p >> 16) & 255) + cr * ka / 255, nG = ((p >> 8) & 255) + cg * ka / 255,
                         nB = (p & 255) + cb * ka / 255;
                px[y * W + x] = (nA << 24) | (nR << 16) | (nG << 8) | nB;
            }
    }

    SIZE sz = {W, W};
    POINT src = {0, 0}, c, dst;
    target_point(&c);
    dst.x = c.x - W / 2;
    dst.y = c.y - W / 2;
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    InterlockedExchange(&g_wsz, W);
    UpdateLayeredWindow(g_overlay, screen, &dst, &sz, mem, &src, 0, &bf, ULW_ALPHA);

    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
}

/* ---------------- tray icon ---------------- */
static HICON make_icon(void) {
    const int n = 32;
    BITMAPV5HEADER bh = {0};
    bh.bV5Size = sizeof(bh);
    bh.bV5Width = n;
    bh.bV5Height = -n;
    bh.bV5Planes = 1;
    bh.bV5BitCount = 32;
    bh.bV5Compression = BI_BITFIELDS;
    bh.bV5RedMask = 0x00FF0000; bh.bV5GreenMask = 0x0000FF00;
    bh.bV5BlueMask = 0x000000FF; bh.bV5AlphaMask = 0xFF000000;
    HDC dc = GetDC(NULL);
    void *bits = NULL;
    HBITMAP color = CreateDIBSection(dc, (BITMAPINFO *)&bh, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, dc);
    float L = (S.style == 2 || S.style == 3 || S.style == 7 || S.style == 8) ? 8.0f
            : S.style >= 10 ? 11.0f : 12.0f;
    int keep = S.glow;
    S.glow = 0;
    render((uint32_t *)bits, n, n, S.style, S.color, L, 3, 1, 1.0f, 0);
    S.glow = keep;
    HBITMAP mask = CreateBitmap(n, n, 1, 1, NULL);
    ICONINFO ii = {TRUE, 0, 0, mask, color};
    HICON ic = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return ic;
}

static void update_tray(void) {
    HICON old = g_icon;
    g_icon = make_icon();
    g_nid.hIcon = g_icon;
    wsprintfW(g_nid.szTip, L"Crosshair: %s, %s%s\nv" VER_WSTR L" by simplysoul", STYLE_NAMES[S.style],
              COLORS[S.color].name, S.hidden ? L" (hidden)" : L"");
    g_nid.uFlags = NIF_ICON | NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    if (old) DestroyIcon(old);
}

static int g_was_down;
static void anim_tick(void) { /* runs 30-60 times a second, only while an effect is on */
    int redraw = S.anim != 0;
    if (S.click_fx) {
        int down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (down && !g_was_down) kick();
        g_was_down = down;
    }
    if (g_kick > 0) { kick_update(); redraw = 1; }
    if (redraw && !S.hidden && !g_keydlg) redraw_overlay();
}
static void fx_timer_sync(void) {
    if (!g_host) return;
    if (S.anim || S.click_fx) SetTimer(g_host, 2, S.click_fx ? 16 : 33, NULL);
    else KillTimer(g_host, 2);
}

/* ---------------- crosshair share codes ---------------- */
/* The crosshair (shape, color + custom color, size, thickness, outline, opacity, glow, effect, effect speed,
   click reaction) packed into 64 bits with a check byte, written in Crockford base32: CO-XXXX-XXXX-XXXXX.
   Friends paste it to get exactly the same crosshair. Codes are made for this app only. */
static const char B32[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
static unsigned char code_check(unsigned long long v) { /* FNV-1a over the 56 data bits */
    unsigned int h = 2166136261u;
    for (int i = 0; i < 7; i++) { h ^= (unsigned)((v >> (i * 8)) & 0xFF); h *= 16777619u; }
    return (unsigned char)(h ^ (h >> 8) ^ (h >> 16) ^ (h >> 24));
}
static void xh_code(wchar_t *out) { /* out: at least 24 chars */
    unsigned long long v = 0;
    int sh = 0;
#define PUT(val, bits) do { v |= ((unsigned long long)((val) & ((1u << (bits)) - 1))) << sh; sh += (bits); } while (0)
    PUT(1, 4); /* format version */
    PUT(S.style, 6); PUT(S.color, 5);
    PUT(COLORS[CUSTOM_COLOR].r, 8); PUT(COLORS[CUSTOM_COLOR].g, 8); PUT(COLORS[CUSTOM_COLOR].b, 8);
    PUT(S.size, 4); PUT(S.thick, 3); PUT(S.outline, 1); PUT(S.opacity, 3);
    PUT(S.glow, 1); PUT(S.anim, 2); PUT(S.anim_speed, 2); PUT(S.click_fx, 1);
#undef PUT
    v |= (unsigned long long)code_check(v) << 56;
    char d[14];
    for (int i = 12; i >= 0; i--) { d[i] = B32[v & 31]; v >>= 5; }
    wsprintfW(out, L"CO-%c%c%c%c-%c%c%c%c-%c%c%c%c%c", d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7], d[8], d[9], d[10], d[11], d[12]);
}
typedef struct { int style, color, r, g, b, size, thick, outline, opac, glow, anim, speed, click; } XhCode;
/* Read a code without applying it. 1 = valid. */
static int xh_code_decode(const wchar_t *in, XhCode *o) {
    unsigned long long v = 0;
    int n = 0;
    const wchar_t *p = in;
    while (*p == L' ' || *p == L'\t' || *p == L'\r' || *p == L'\n') p++;
    if ((p[0] == L'C' || p[0] == L'c') && (p[1] == L'O' || p[1] == L'o') && p[2] == L'-') p += 3;
    for (; *p && n < 13; p++) {
        wchar_t c = towupper(*p);
        if (c == L'-' || c == L' ') continue;
        if (c == L'O') c = L'0';
        if (c == L'I' || c == L'L') c = L'1';
        const char *f = c < 128 ? strchr(B32, (char)c) : NULL;
        if (!f || !c) return 0;
        v = (v << 5) | (unsigned long long)(f - B32);
        n++;
    }
    if (n != 13) return 0;
    if ((unsigned char)(v >> 56) != code_check(v & 0x00FFFFFFFFFFFFFFull)) return 0;
    int sh = 0;
#define GET(bits) ((int)((v >> (sh += (bits), sh - (bits))) & ((1u << (bits)) - 1)))
    if (GET(4) != 1) return 0;
    o->style = GET(6); o->color = GET(5); o->r = GET(8); o->g = GET(8); o->b = GET(8); o->size = GET(4); o->thick = GET(3);
    o->outline = GET(1); o->opac = GET(3); o->glow = GET(1); o->anim = GET(2); o->speed = GET(2); o->click = GET(1);
#undef GET
    if (o->style >= NSTYLES || o->color >= NCOLORS || o->size >= NSIZES || o->thick < 1 || o->thick > 6 || o->opac >= NOPAC || o->anim > 2 || o->speed > 2) return 0;
    o->style = style_fix(o->style);
    return 1;
}
/* Apply a pasted code. 1 = done, 0 = not a valid code. */
static int xh_code_apply(const wchar_t *in) {
    XhCode c;
    if (!xh_code_decode(in, &c)) return 0;
    S.style = c.style;
    S.color = c.color;
    if (c.color == CUSTOM_COLOR) { COLORS[CUSTOM_COLOR].r = (BYTE)c.r; COLORS[CUSTOM_COLOR].g = (BYTE)c.g; COLORS[CUSTOM_COLOR].b = (BYTE)c.b; }
    S.size = c.size; S.thick = c.thick; S.outline = c.outline; S.opacity = c.opac;
    S.glow = c.glow; S.anim = c.anim; S.anim_speed = c.speed; S.click_fx = c.click;
    return 1;
}
/* "Recent codes": the last 10 codes pasted, newest first (ini recent_codes, ";"-separated). */
#define NRECENT 10
static wchar_t g_recent[NRECENT][20];
static int g_nrecent;
static void recent_load(void) {
    wchar_t b[NRECENT * 22 + 8];
    g_nrecent = 0;
    if (!g_ini[0]) return;
    GetPrivateProfileStringW(L"crosshair", L"recent_codes", L"", b, NRECENT * 22 + 8, g_ini);
    for (wchar_t *t = b, *e; *t && g_nrecent < NRECENT; t = e ? e + 1 : t + wcslen(t)) {
        e = wcschr(t, L';');
        if (e) *e = 0;
        XhCode c;
        if (wcslen(t) == 18 && xh_code_decode(t, &c)) wcscpy(g_recent[g_nrecent++], t);
        if (!e) break;
    }
}
static void recent_save(void) {
    wchar_t b[NRECENT * 22 + 8] = L"";
    for (int i = 0; i < g_nrecent; i++) { if (i) wcscat(b, L";"); wcscat(b, g_recent[i]); }
    if (g_ini[0]) WritePrivateProfileStringW(L"crosshair", L"recent_codes", b, g_ini);
}
static void recent_add(const wchar_t *code) { /* code in CO-XXXX-XXXX-XXXXX form */
    int i, n = g_nrecent;
    for (i = 0; i < n; i++) if (!wcscmp(g_recent[i], code)) break;
    if (i == n) { if (n < NRECENT) n++; i = n - 1; }
    for (; i > 0; i--) wcscpy(g_recent[i], g_recent[i - 1]);
    lstrcpynW(g_recent[0], code, 20);
    g_nrecent = n;
    recent_save();
}
static void recent_remove(int k) {
    if (k < 0 || k >= g_nrecent) return;
    for (int i = k; i < g_nrecent - 1; i++) wcscpy(g_recent[i], g_recent[i + 1]);
    g_nrecent--;
    recent_save();
}
static void clip_set(HWND owner, const wchar_t *t) {
    size_t n = (wcslen(t) + 1) * sizeof(wchar_t);
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, n);
    if (hg && OpenClipboard(owner)) {
        memcpy(GlobalLock(hg), t, n);
        GlobalUnlock(hg);
        EmptyClipboard();
        if (!SetClipboardData(CF_UNICODETEXT, hg)) GlobalFree(hg);
        CloseClipboard();
    } else if (hg) GlobalFree(hg);
}
static int clip_get(HWND owner, wchar_t *out, int cap) {
    int ok = 0;
    out[0] = 0;
    if (!OpenClipboard(owner)) return 0;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    const wchar_t *t = h ? (const wchar_t *)GlobalLock(h) : NULL;
    if (t) { wcsncpy(out, t, cap - 1); out[cap - 1] = 0; GlobalUnlock(h); ok = 1; }
    CloseClipboard();
    return ok;
}

static void apply(void) {
    save_settings();
    sync_cursor();
    if (S.hidecur != g_rbx_patched) sync_roblox();
    fx_timer_sync();
    redraw_overlay();
    update_tray();
    card_refresh();
    if (g_keydlg) InvalidateRect(g_keydlg, NULL, FALSE);
    InterlockedExchange(&g_force_move, 1);
}

static void set_hidden(int h) {
    S.hidden = h;
    if (!g_keydlg) ShowWindow(g_overlay, h || g_focus_hidden || g_scene_hidden ? SW_HIDE : SW_SHOWNOACTIVATE);
    sync_cursor();
    update_tray();
    card_refresh();
}

/* "In Roblox only": hide the crosshair (and give the mouse cursor back) whenever Roblox isn't the window
   in front - alt-tab, desktop, browser, minimized. Driven by Windows' foreground-change event. */
static void focus_check(void) {
    int hide = 0;
    if (g_onlyrbx) {
        HWND fg = GetForegroundWindow();
        wchar_t cls[64] = L"";
        DWORD pid = 0;
        if (fg) GetClassNameW(fg, cls, 64);
        if (fg) GetWindowThreadProcessId(fg, &pid);
        if (pid == GetCurrentProcessId() || !wcscmp(cls, L"#32768") || !fg) return; /* our own windows / a menu: leave it as it is */
        hide = wcscmp(cls, L"WINDOWSCLIENT") != 0 || IsIconic(fg);
    }
    if (hide == g_focus_hidden) return;
    g_focus_hidden = hide;
    if (!g_keydlg && !g_menu_open) ShowWindow(g_overlay, !S.hidden && !hide && !g_scene_hidden ? SW_SHOWNOACTIVATE : SW_HIDE);
    sync_cursor();
    card_refresh();
}
static void CALLBACK focus_event(HWINEVENTHOOK hk, DWORD ev, HWND w, LONG obj, LONG child, DWORD th, DWORD t) {
    (void)hk; (void)ev; (void)w; (void)obj; (void)child; (void)th; (void)t;
    focus_check();
}

/* ---- scene watcher (30 times a second, only while Auto-contrast or "Hide in Roblox menus & chat" is on) ----
   It looks at a few small spots of the screen. Layered windows (like the crosshair) aren't in that picture,
   so it sees the game behind the crosshair. Nothing is saved or sent anywhere. */
#define SCENE_TIMER 13
static float srgb_lin(int c) { float v = c / 255.0f; return v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f); }
static float lum_rgb(int r, int g, int b) { return 0.2126f * srgb_lin(r) + 0.7152f * srgb_lin(g) + 0.0722f * srgb_lin(b); }
static float contrast_ratio(float a, float b) { return (fmaxf(a, b) + 0.05f) / (fminf(a, b) + 0.05f); }
/* average color of n square patches (half = half-size) around the given screen points */
static int screen_patches(const POINT *pt, int n, int half, int *R, int *G, int *B) {
    int side = half * 2;
    HDC sdc = GetDC(NULL);
    if (!sdc) return 0;
    HDC m = CreateCompatibleDC(sdc);
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = side;
    bi.bmiHeader.biHeight = -side * n;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void *bits = NULL;
    HBITMAP bmp = CreateDIBSection(m, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    int ok = 0;
    if (bmp && bits) {
        HGDIOBJ old = SelectObject(m, bmp);
        for (int i = 0; i < n; i++) BitBlt(m, 0, i * side, side, side, sdc, pt[i].x - half, pt[i].y - half, SRCCOPY);
        GdiFlush();
        unsigned long long r = 0, g = 0, b = 0;
        uint32_t *px = (uint32_t *)bits;
        int cnt = side * side * n;
        for (int i = 0; i < cnt; i++) { r += (px[i] >> 16) & 255; g += (px[i] >> 8) & 255; b += px[i] & 255; }
        *R = (int)(r / cnt); *G = (int)(g / cnt); *B = (int)(b / cnt);
        SelectObject(m, old);
        ok = 1;
    }
    if (bmp) DeleteObject(bmp);
    DeleteDC(m);
    ReleaseDC(NULL, sdc);
    return ok;
}
static void scene_apply(int hide) {
    if (hide == g_scene_hidden) return;
    g_scene_hidden = hide;
    log_info(hide ? L"Roblox menu / chat open: crosshair hidden" : L"Roblox menu / chat closed: crosshair back");
    if (!g_keydlg && !g_menu_open) ShowWindow(g_overlay, !S.hidden && !g_focus_hidden && !hide ? SW_SHOWNOACTIVATE : SW_HIDE);
    sync_cursor();
}
static void scene_tick(void) {
    static int tick, chat, menu, pend, was[4];
    static DWORD pend_t;
    static float bright = -1, b0 = -1, mb = 0;
    DWORD now = GetTickCount();
    HWND fg = GetForegroundWindow();
    wchar_t cls[32] = L"";
    RECT rc;
    int rbx = fg && GetClassNameW(fg, cls, 32) && !wcscmp(cls, L"WINDOWSCLIENT") && !IsIconic(fg) && GetClientRect(fg, &rc) && rc.right > 200;
    tick++;
    /* --- Roblox menu (Esc) and chat (/) --- */
    if (S.menu_hide && rbx) {
        static const int VK[4] = {VK_ESCAPE, VK_OEM_2, VK_RETURN, VK_LBUTTON};
        int edge[4];
        for (int i = 0; i < 4; i++) {
            SHORT k = GetAsyncKeyState(VK[i]);
            int down = (k & 0x8000) != 0;
            edge[i] = (down && !was[i]) || ((k & 1) && !down); /* pressed since the last look (even a quick tap) */
            was[i] = down;
        }
        if (tick % 3 == 0 || pend) { /* how bright Roblox's window is, away from the middle (where the menu panel sits) */
            POINT pts[8], o = {0, 0};
            static const float FX[8] = {0.12f, 0.5f, 0.88f, 0.12f, 0.5f, 0.88f, 0.06f, 0.94f}, FY[8] = {0.2f, 0.1f, 0.2f, 0.8f, 0.9f, 0.8f, 0.5f, 0.5f};
            ClientToScreen(fg, &o);
            for (int i = 0; i < 8; i++) { pts[i].x = o.x + (int)(rc.right * FX[i]); pts[i].y = o.y + (int)(rc.bottom * FY[i]); }
            int r, g, b;
            if (screen_patches(pts, 8, 5, &r, &g, &b)) {
                float nb = lum_rgb(r, g, b);
                if (!pend && !menu) b0 = nb; /* the "before" brightness */
                bright = nb;
            }
        }
        if (edge[0]) { /* Esc */
            if (chat) chat = 0;                 /* closes the chat box, not the menu */
            else if (menu) { menu = 0; pend = 0; }
            else { pend = 1; pend_t = now; }    /* menu opening? confirmed below by the screen going darker */
        }
        if (pend && !menu) {
            if (b0 > 0.012f && bright >= 0 && bright < b0 * 0.72f) { menu = 1; mb = bright; pend = 0; }
            else if (now - pend_t > 800) pend = 0;
        }
        if (menu && bright >= 0 && bright > mb * 1.3f + 0.01f) menu = 0; /* it got bright again: Resume was clicked */
        if (edge[1] && !menu) chat = 1;                       /* "/" opens Roblox's chat box */
        if (chat && (edge[2] || edge[3])) chat = 0;            /* Enter sends, a click leaves it */
        scene_apply(chat || menu);
    } else {
        if (!rbx) chat = 0;
        if (!S.menu_hide) { chat = menu = pend = 0; }
        scene_apply(0);
    }
    /* --- auto-contrast: would the crosshair blend into what's behind it? --- */
    if (S.auto_contrast && S.anim != 1 && !S.hidden && !g_scene_hidden && tick % 3 == 1) {
        POINT c;
        target_point(&c);
        int r, g, b, half = clampi(needed_size() / 2, 8, 28), alt = g_ac_alt;
        if (screen_patches(&c, 1, half, &r, &g, &b)) {
            float bl = lum_rgb(r, g, b), cl = lum_rgb(COLORS[S.color].r, COLORS[S.color].g, COLORS[S.color].b);
            float cr = contrast_ratio(bl, cl);
            if (!alt && cr < 1.6f) alt = bl > 0.3f ? 2 : 1;
            else if (alt && cr > 2.2f) alt = 0;
            else if (alt && ((alt == 1 && bl > 0.45f) || (alt == 2 && bl < 0.15f))) alt = bl > 0.3f ? 2 : 1;
        }
        if (alt != g_ac_alt) { g_ac_alt = alt; if (!g_keydlg) redraw_overlay(); }
    } else if (g_ac_alt && (!S.auto_contrast || S.anim == 1)) { g_ac_alt = 0; redraw_overlay(); }
}
static void scene_timer_sync(void) {
    if (!g_host) return;
    if (S.auto_contrast || S.menu_hide) SetTimer(g_host, SCENE_TIMER, 33, NULL);
    else { KillTimer(g_host, SCENE_TIMER); scene_apply(0); if (g_ac_alt) { g_ac_alt = 0; redraw_overlay(); } }
}

/* ---------------- keybinds ---------------- */
enum { A_NEXT, A_PREV, A_CNEXT, A_CPREV, A_SMALL, A_BIG, A_HIDE, A_MODE, A_CURSOR, A_QUIT,
       A_SHOWKEYS, A_SETTINGS, A_COVER, A_PROF1, A_PROF2, A_PROF3, A_STYLE0 };
#define NACT (A_STYLE0 + NSTYLES)

typedef struct { UINT mods, vk; } Bind;
static Bind KB[NACT];
static int g_bind_fail[NACT];

/* Default: only Insert (reopen the settings panel). Everything else is done in the panel. */
static const Bind DEFAULT_BINDS[A_STYLE0] = {
    {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
    {0, VK_INSERT}, {0, 0}, {0, 0}, {0, 0}, {0, 0}};
/* Optional classic layout, applied with the "Use F-key layout" button */
static const Bind FKEY_BINDS[A_STYLE0] = {
    {0, VK_F6}, {MOD_SHIFT, VK_F6}, {0, VK_F7}, {MOD_SHIFT, VK_F7}, {MOD_CONTROL, VK_F6},
    {MOD_CONTROL, VK_F7}, {0, VK_F8}, {0, VK_F9}, {0, VK_F10}, {MOD_CONTROL | MOD_SHIFT, VK_F8}, {MOD_CONTROL, VK_F1}, {MOD_CONTROL, VK_F2}, {MOD_CONTROL, VK_F3}, {0, 0}, {0, 0}, {0, 0}};
static const wchar_t *ACTION_NAMES[A_STYLE0] = {
    L"Next crosshair", L"Previous crosshair", L"Next color", L"Previous color", L"Smaller",
    L"Bigger", L"Show / hide crosshair", L"Follow mouse / center", L"Hide / show mouse cursor",
    L"Exit the app", L"Show keybinds on screen", L"Open settings menu", L"Cover game crosshair on/off",
    L"Load profile 1", L"Load profile 2", L"Load profile 3"};
static const wchar_t *ACTION_KEYS[A_STYLE0] = {
    L"next", L"prev", L"color_next", L"color_prev", L"smaller", L"bigger", L"hide",
    L"center", L"cursor", L"exit", L"show_keys", L"settings", L"cover", L"profile1", L"profile2", L"profile3"};

static void action_name(int a, wchar_t *out) {
    if (a < A_STYLE0) wcscpy(out, ACTION_NAMES[a]);
    else wsprintfW(out, L"Switch to %s", STYLE_NAMES[a - A_STYLE0]);
}
static void action_ini_key(int a, wchar_t *out) {
    if (a < A_STYLE0) wsprintfW(out, L"key_%s", ACTION_KEYS[a]);
    else wsprintfW(out, L"key_style%d", a - A_STYLE0);
}

static void default_binds(void) {
    memset(KB, 0, sizeof(KB));
    for (int i = 0; i < A_STYLE0; i++) KB[i] = DEFAULT_BINDS[i];
}
static void fkey_binds(void) {
    memset(KB, 0, sizeof(KB));
    for (int i = 0; i < A_STYLE0; i++) KB[i] = FKEY_BINDS[i];
}
static void load_binds(void) {
    wchar_t k[40];
    default_binds();
    for (int i = 0; i < NACT; i++) {
        action_ini_key(i, k);
        int v = ini_get(k, -1);
        if (v >= 0) { KB[i].mods = (v >> 16) & 0x0F; KB[i].vk = v & 0xFF; }
    }
    if (!ini_get(L"binds_v2", 0)) default_binds(); /* v1.7: panel replaces the F-key defaults (once) */
    for (int i = 0; i < NSTYLES; i++) /* no keys for hidden look-alike shapes */
        if (!style_shown(i)) { KB[A_STYLE0 + i].vk = 0; KB[A_STYLE0 + i].mods = 0; }
}
static void save_binds(void) {
    wchar_t k[40];
    for (int i = 0; i < NACT; i++) {
        action_ini_key(i, k);
        ini_set(k, (int)((KB[i].mods << 16) | KB[i].vk));
    }
    ini_set(L"binds_v2", 1);
}

/* "Ctrl+Shift+F8" etc. out must hold 64 chars. */
static void key_text(Bind b, wchar_t *out) {
    static const struct { UINT vk; const wchar_t *n; } NAMES[] = {
        {VK_LEFT, L"Left"}, {VK_RIGHT, L"Right"}, {VK_UP, L"Up"}, {VK_DOWN, L"Down"},
        {VK_INSERT, L"Insert"}, {VK_DELETE, L"Delete"}, {VK_HOME, L"Home"}, {VK_END, L"End"},
        {VK_PRIOR, L"Page Up"}, {VK_NEXT, L"Page Down"}, {VK_SPACE, L"Space"}, {VK_TAB, L"Tab"},
        {VK_RETURN, L"Enter"}, {VK_BACK, L"Backspace"}, {VK_ESCAPE, L"Esc"}, {VK_PAUSE, L"Pause"},
        {VK_SCROLL, L"Scroll Lock"}, {VK_CAPITAL, L"Caps Lock"}, {VK_NUMLOCK, L"Num Lock"},
        {VK_MULTIPLY, L"Num *"}, {VK_ADD, L"Num +"}, {VK_SUBTRACT, L"Num -"},
        {VK_DIVIDE, L"Num /"}, {VK_DECIMAL, L"Num ."}};
    wchar_t k[40] = {0};
    out[0] = 0;
    if (!b.vk) { wcscpy(out, L"(none)"); return; }
    if (b.mods & MOD_CONTROL) wcscat(out, L"Ctrl+");
    if (b.mods & MOD_ALT) wcscat(out, L"Alt+");
    if (b.mods & MOD_SHIFT) wcscat(out, L"Shift+");
    if (b.mods & MOD_WIN) wcscat(out, L"Win+");
    if (b.vk >= VK_F1 && b.vk <= VK_F24) wsprintfW(k, L"F%d", (int)(b.vk - VK_F1 + 1));
    else if ((b.vk >= '0' && b.vk <= '9') || (b.vk >= 'A' && b.vk <= 'Z')) k[0] = (wchar_t)b.vk;
    else if (b.vk >= VK_NUMPAD0 && b.vk <= VK_NUMPAD9) wsprintfW(k, L"Num %d", (int)(b.vk - VK_NUMPAD0));
    else {
        for (unsigned i = 0; i < sizeof(NAMES) / sizeof(NAMES[0]); i++)
            if (NAMES[i].vk == b.vk) { wcscpy(k, NAMES[i].n); break; }
        if (!k[0]) {
            UINT sc = MapVirtualKeyW(b.vk, MAPVK_VK_TO_VSC);
            if (!sc || !GetKeyNameTextW((LONG)(sc << 16), k, 39)) wsprintfW(k, L"Key %u", b.vk);
        }
    }
    wcscat(out, k);
}

/* A key with no Ctrl/Alt that you'd normally use while playing. */
static int is_game_key(Bind b) {
    if (b.mods & (MOD_CONTROL | MOD_ALT | MOD_WIN)) return 0;
    if (b.vk >= VK_F1 && b.vk <= VK_F24) return 0;
    if (b.vk >= VK_NUMPAD0 && b.vk <= VK_DIVIDE) return 0;
    if (b.vk == VK_INSERT || b.vk == VK_HOME || b.vk == VK_END || b.vk == VK_PRIOR ||
        b.vk == VK_NEXT || b.vk == VK_PAUSE || b.vk == VK_SCROLL)
        return 0;
    return 1;
}

static void unregister_hotkeys(HWND h) {
    for (int i = 0; i < NACT; i++) UnregisterHotKey(h, i + 1);
}
static void register_hotkeys(HWND h) {
    unregister_hotkeys(h);
    for (int i = 0; i < NACT; i++) {
        g_bind_fail[i] = 0;
        if (!KB[i].vk) continue;
        UINT m = KB[i].mods;
        if (i != A_SMALL && i != A_BIG) m |= MOD_NOREPEAT;
        if (!RegisterHotKey(h, i + 1, m, KB[i].vk)) g_bind_fail[i] = 1;
    }
}

/* Menu label "Text<TAB>Key" */
static void with_key(wchar_t *out, const wchar_t *txt, int a) {
    wchar_t k[64];
    if (a >= 0 && KB[a].vk) { key_text(KB[a], k); wsprintfW(out, L"%s\t%s", txt, k); }
    else wcscpy(out, txt);
}

/* ---------------- loading music ---------------- */
/* Plays the song you picked (a file on your PC) from your chosen start time while
   the loading screen runs, keeps going a moment after it closes, then fades out. */
static int g_music_on, g_music_vol;
#define MUSIC_FADE_TIMER 9
static void music_stop(void) {
    if (!g_music_on) return;
    mciSendStringW(L"close chmusic", NULL, 0, NULL);
    g_music_on = 0;
    if (g_host) KillTimer(g_host, MUSIC_FADE_TIMER);
}
static void music_play(void) {
    wchar_t cmd[MAX_PATH + 64];
    music_stop();
    if (!g_music[0] || !file_exists(g_music)) return;
    wsprintfW(cmd, L"open \"%s\" type mpegvideo alias chmusic", g_music);
    if (mciSendStringW(cmd, NULL, 0, NULL) != 0) return;
    mciSendStringW(L"set chmusic time format milliseconds", NULL, 0, NULL);
    g_music_vol = 1000;
    mciSendStringW(L"setaudio chmusic volume to 1000", NULL, 0, NULL);
    wsprintfW(cmd, L"play chmusic from %d", g_music_start * 1000);
    if (mciSendStringW(cmd, NULL, 0, NULL) != 0) { mciSendStringW(L"close chmusic", NULL, 0, NULL); return; }
    g_music_on = 1;
}
/* called when the loading screen closes: play on ~2 s, then fade out over ~1.5 s */
static void music_fade_later(void) {
    if (!g_music_on || !g_host) return;
    g_music_vol = 1000 + 2000 / 40 * 40; /* the first 2 s count down without lowering the volume */
    SetTimer(g_host, MUSIC_FADE_TIMER, 40, NULL);
}
static void music_fade_tick(void) {
    wchar_t cmd[64];
    if (!g_music_on) return;
    g_music_vol -= 27;
    if (g_music_vol <= 0) { music_stop(); return; }
    if (g_music_vol <= 1000) {
        wsprintfW(cmd, L"setaudio chmusic volume to %d", g_music_vol);
        mciSendStringW(cmd, NULL, 0, NULL);
    }
}

/* ================= drawing toolkit (settings window + loading screen) ================= */
/* Everything is drawn into a 32-bit pixel buffer with smooth (anti-aliased)
   edges, then text is drawn on top with GDI. */
static int g_capture = -1;
static HFONT F_TITLE, F_HEAD, F_TEXT, F_BOLD, F_SMALL, F_TINY, F_HUD, F_TAB, F_HUDS, F_XS;
static HDC g_paint_dc; /* the settings window's drawing DC while it paints (for measuring text) */
static float g_sharp = 1.0f; /* < 1 while the settings window draws: sharper, HUD-style corners */
static int g_dpi = 96;
static int g_ui_shrink = 100; /* % scale of the settings window (panel size, shrunk more on short screens) */
static const int UI_SIZES[] = {75, 85, 100, 115}; /* panel sizes the - / + buttons step through */
static int dpx(int v) { return MulDiv(v, g_dpi, 96); }
static float dpf(float v) { return v * (float)g_dpi / 96.0f; }

static const wchar_t *g_font_face = L"Segoe UI";
static HFONT face_font(const wchar_t *face, int pt, int weight) {
    return CreateFontW(-MulDiv(pt, g_dpi, 72), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}
static HFONT ui_font(int pt, int weight) { return face_font(g_font_face, pt, weight); }
static int CALLBACK font_found(const LOGFONTW *a, const TEXTMETRICW *b, DWORD c, LPARAM p) { (void)a; (void)b; (void)c; *(int *)p = 1; return 0; }
static int font_installed(const wchar_t *face) {
    LOGFONTW lf = {0};
    int found = 0;
    lf.lfCharSet = DEFAULT_CHARSET;
    lstrcpynW(lf.lfFaceName, face, LF_FACESIZE);
    HDC dc = GetDC(NULL);
    EnumFontFamiliesExW(dc, &lf, (FONTENUMPROCW)font_found, (LPARAM)&found, 0);
    ReleaseDC(NULL, dc);
    return found;
}
/* Bold condensed face for headings and tabs (Bahnschrift ships with Windows 10 and 11). */
static const wchar_t *hud_face(void) {
    static const wchar_t *f;
#ifdef CH_TEST
    static wchar_t t[64];
    if (!f && GetEnvironmentVariableW(L"CH_HUDFONT", t, 64)) f = t;
#endif
    if (!f) f = font_installed(L"Bahnschrift Condensed") ? L"Bahnschrift Condensed" : font_installed(L"Bahnschrift") ? L"Bahnschrift" : L"Segoe UI";
    return f;
}

static void make_fonts(void) {
    HDC dc = GetDC(NULL);
    g_dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(NULL, dc);
    if (g_dpi < 96) g_dpi = 96;
    g_dpi = MulDiv(g_dpi, g_ui_shrink, 100); /* settings window shrunk to fit a short screen */
    HFONT old[10] = {F_TITLE, F_HEAD, F_TEXT, F_BOLD, F_SMALL, F_TINY, F_HUD, F_TAB, F_HUDS, F_XS};
    for (int i = 0; i < 10; i++) if (old[i]) DeleteObject(old[i]);
    F_TITLE = ui_font(12, FW_BOLD);
    F_HEAD = ui_font(17, FW_SEMIBOLD);
    F_TEXT = ui_font(10, FW_NORMAL);
    F_BOLD = ui_font(10, FW_SEMIBOLD);
    F_SMALL = ui_font(9, FW_NORMAL);
    F_TINY = face_font(hud_face(), 8, FW_SEMIBOLD);
    F_XS = ui_font(8, FW_NORMAL);
    int cond = wcsstr(hud_face(), L"Condensed") != NULL; /* a wide fallback font gets smaller sizes so tabs still fit */
    F_HUD = face_font(hud_face(), cond ? 15 : 13, FW_BOLD);
    F_TAB = face_font(hud_face(), cond ? 11 : 9, FW_SEMIBOLD);
    F_HUDS = face_font(hud_face(), 9, FW_SEMIBOLD);
}

/* ---------------- themes ---------------- */
/* Every color in the settings window, keybinds card and loading screen comes
   from the active theme. "deco" picks the background art, "segbar" a segmented
   loading bar, "radius" scales how round the corners are. */
enum { DECO_GRID, DECO_PATROL, DECO_CRIMINAL, DECO_TACTICAL, DECO_ARCADE, DECO_SUNSET, DECO_CYBER, DECO_OPS, DECO_INTEL, DECO_RAID, DECO_DRIVE,
       DECO_AURORA, DECO_EMBER, DECO_GALAXY, DECO_OCEAN, DECO_SAKURA };
static int deco_animated(int d);
typedef struct {
    const wchar_t *name, *tag;
    COLORREF bg, side_top, side_bot, card, hover, border, track, text, muted, dim, accent, accent2, on_accent;
    float radius;
    int deco, segbar;
    const wchar_t *font, *title_font;
} Theme;
/* TH_CUSTOM keeps number 7 so settings saved by earlier builds still open the right theme */
enum { TH_PATROL, TH_CRIMINAL, TH_REDOPS, TH_TACTICAL, TH_ARCADE, TH_VAPOR, TH_CYBER, TH_CUSTOM, TH_OPS, TH_INTEL, TH_RAID, TH_DRIVE,
       TH_AURORA, TH_EMBER, TH_GALAXY, TH_OCEAN, TH_SAKURA, NTHEMES };
static const int THEME_ORDER[NTHEMES] = {TH_AURORA, TH_GALAXY, TH_EMBER, TH_OCEAN, TH_SAKURA, TH_DRIVE, TH_PATROL,
                                         TH_CRIMINAL, TH_OPS, TH_INTEL, TH_RAID, TH_REDOPS, TH_TACTICAL, TH_ARCADE,
                                         TH_VAPOR, TH_CYBER, TH_CUSTOM}; /* order on the Themes tab */
static const Theme THEMES[NTHEMES - 1] = {
    {L"Police", L"Lightbar, night highway, 10-8",
     RGB(9, 13, 24), RGB(17, 24, 42), RGB(8, 11, 20), RGB(17, 24, 41), RGB(27, 37, 62), RGB(36, 48, 78),
     RGB(44, 57, 90), RGB(241, 244, 252), RGB(148, 160, 188), RGB(94, 106, 136), RGB(46, 104, 255),
     RGB(255, 38, 56), RGB(255, 255, 255), 0.75f, DECO_PATROL, 0, L"Segoe UI", L"Bahnschrift Condensed"},
    {L"Criminal", L"Crimson & black, crew ranks",
     RGB(10, 8, 8), RGB(30, 14, 14), RGB(9, 7, 7), RGB(26, 17, 17), RGB(44, 25, 25), RGB(70, 36, 36),
     RGB(76, 42, 42), RGB(246, 236, 234), RGB(180, 150, 148), RGB(118, 94, 92), RGB(222, 30, 40),
     RGB(255, 128, 104), RGB(255, 255, 255), 0.5f, DECO_CRIMINAL, 1, L"Segoe UI", L"Bahnschrift Condensed"},
    {L"Red Ops", L"The original v1.8 look",
     RGB(13, 15, 20), RGB(22, 24, 33), RGB(13, 14, 19), RGB(23, 26, 34), RGB(32, 36, 47), RGB(39, 43, 56),
     RGB(46, 50, 64), RGB(238, 240, 246), RGB(140, 146, 164), RGB(92, 98, 116), RGB(235, 32, 48),
     RGB(255, 84, 98), RGB(255, 255, 255), 1.0f, DECO_GRID, 0, L"Segoe UI", L"Segoe UI"},
    {L"Tactical", L"Olive drab & amber HUD",
     RGB(15, 17, 12), RGB(25, 28, 20), RGB(13, 15, 10), RGB(26, 29, 21), RGB(37, 41, 30), RGB(54, 59, 42),
     RGB(60, 65, 47), RGB(228, 226, 204), RGB(152, 154, 126), RGB(100, 104, 82), RGB(255, 176, 32),
     RGB(140, 168, 80), RGB(22, 18, 6), 0.3f, DECO_TACTICAL, 1, L"Bahnschrift", L"Bahnschrift Condensed"},
    {L"Retro Arcade", L"Neon CRT with scanlines",
     RGB(7, 5, 16), RGB(16, 10, 34), RGB(6, 4, 14), RGB(17, 12, 34), RGB(32, 22, 62), RGB(56, 40, 104),
     RGB(50, 38, 92), RGB(240, 238, 255), RGB(164, 154, 206), RGB(106, 98, 146), RGB(57, 255, 120),
     RGB(255, 60, 200), RGB(6, 12, 8), 0.0f, DECO_ARCADE, 1, L"Consolas", L"Consolas"},
    {L"Vaporwave", L"Pink & cyan sunset grid",
     RGB(24, 12, 40), RGB(46, 20, 70), RGB(20, 10, 36), RGB(38, 20, 62), RGB(56, 30, 88), RGB(84, 48, 122),
     RGB(82, 52, 118), RGB(255, 238, 250), RGB(208, 170, 216), RGB(148, 112, 168), RGB(255, 92, 205),
     RGB(64, 232, 255), RGB(255, 255, 255), 1.25f, DECO_SUNSET, 0, L"Segoe UI", L"Segoe UI"},
    {L"Cyberpunk", L"Hazard yellow & glitch",
     RGB(10, 11, 14), RGB(22, 22, 18), RGB(9, 10, 12), RGB(20, 21, 24), RGB(34, 34, 30), RGB(62, 60, 34),
     RGB(58, 58, 42), RGB(242, 242, 232), RGB(164, 164, 144), RGB(106, 106, 94), RGB(250, 236, 20),
     RGB(0, 236, 255), RGB(10, 10, 10), 0.2f, DECO_CYBER, 1, L"Bahnschrift", L"Bahnschrift SemiCondensed"},
    /* below: stored after Custom (theme numbers 8, 9) */
    {L"Special Ops", L"Charcoal camo & maroon",
     RGB(11, 11, 12), RGB(26, 24, 24), RGB(10, 10, 11), RGB(25, 24, 25), RGB(40, 36, 36), RGB(62, 50, 50),
     RGB(68, 56, 56), RGB(228, 226, 222), RGB(162, 158, 154), RGB(106, 102, 98), RGB(156, 34, 32),
     RGB(196, 194, 198), RGB(255, 255, 255), 0.5f, DECO_OPS, 1, L"Segoe UI", L"Georgia"},
    {L"Intel", L"Black & red, world watch",
     RGB(8, 8, 10), RGB(22, 16, 18), RGB(7, 7, 9), RGB(21, 18, 20), RGB(36, 28, 30), RGB(60, 40, 42),
     RGB(68, 46, 48), RGB(242, 242, 244), RGB(168, 160, 162), RGB(110, 102, 104), RGB(200, 36, 46),
     RGB(130, 22, 30), RGB(255, 255, 255), 0.6f, DECO_INTEL, 0, L"Segoe UI", L"Bahnschrift"},
    {L"Chosen Company", L"Night ops: VHS, lasers, taillights",
     RGB(7, 8, 12), RGB(16, 17, 26), RGB(6, 7, 10), RGB(17, 18, 26), RGB(28, 29, 42), RGB(40, 40, 62),
     RGB(48, 48, 72), RGB(226, 228, 240), RGB(150, 152, 172), RGB(96, 98, 118), RGB(104, 86, 255),
     RGB(255, 34, 44), RGB(255, 255, 255), 0.4f, DECO_RAID, 0, L"Segoe UI", L"Bahnschrift"},
    {L"Midnight Run", L"Animated: taillights, smoke, push-in",
     RGB(10, 11, 15), RGB(22, 24, 30), RGB(9, 10, 13), RGB(21, 23, 29), RGB(34, 37, 46), RGB(48, 52, 64),
     RGB(56, 60, 74), RGB(232, 234, 240), RGB(152, 156, 170), RGB(98, 102, 116), RGB(225, 30, 40),
     RGB(170, 180, 200), RGB(255, 255, 255), 0.6f, DECO_DRIVE, 0, L"Segoe UI", L"Bahnschrift"},
    /* added in 1.13 (theme numbers 12-16), all gently animated */
    {L"Aurora", L"Northern lights over the mountains",
     RGB(6, 14, 20), RGB(10, 26, 34), RGB(5, 12, 17), RGB(11, 26, 33), RGB(18, 40, 50), RGB(26, 56, 66),
     RGB(30, 62, 72), RGB(232, 246, 244), RGB(140, 176, 176), RGB(88, 118, 120), RGB(46, 230, 160),
     RGB(150, 110, 255), RGB(4, 20, 14), 0.9f, DECO_AURORA, 0, L"Segoe UI", L"Bahnschrift"},
    {L"Ember", L"Rising sparks and firelight",
     RGB(14, 9, 7), RGB(32, 16, 10), RGB(12, 8, 6), RGB(28, 17, 12), RGB(46, 26, 17), RGB(72, 40, 24),
     RGB(80, 46, 28), RGB(250, 238, 228), RGB(190, 150, 126), RGB(128, 98, 80), RGB(255, 110, 30),
     RGB(255, 196, 70), RGB(24, 10, 4), 0.6f, DECO_EMBER, 1, L"Segoe UI", L"Bahnschrift Condensed"},
    {L"Galaxy", L"Nebula, stars and a ringed planet",
     RGB(8, 7, 18), RGB(20, 16, 40), RGB(7, 6, 16), RGB(18, 15, 36), RGB(30, 25, 58), RGB(46, 40, 86),
     RGB(52, 46, 96), RGB(238, 236, 255), RGB(160, 154, 206), RGB(104, 98, 146), RGB(140, 120, 255),
     RGB(255, 120, 200), RGB(255, 255, 255), 1.0f, DECO_GALAXY, 0, L"Segoe UI", L"Bahnschrift"},
    {L"Deep Sea", L"Light rays and rising bubbles",
     RGB(4, 12, 22), RGB(8, 26, 44), RGB(4, 10, 19), RGB(8, 24, 40), RGB(13, 38, 62), RGB(20, 56, 86),
     RGB(24, 62, 94), RGB(228, 244, 255), RGB(136, 172, 200), RGB(84, 116, 142), RGB(0, 190, 255),
     RGB(60, 255, 210), RGB(2, 16, 26), 1.1f, DECO_OCEAN, 0, L"Segoe UI", L"Bahnschrift"},
    {L"Sakura", L"Cherry blossoms under the moon",
     RGB(16, 10, 16), RGB(36, 20, 34), RGB(14, 9, 14), RGB(32, 19, 30), RGB(50, 30, 46), RGB(78, 46, 70),
     RGB(84, 52, 76), RGB(252, 240, 248), RGB(200, 162, 186), RGB(138, 108, 128), RGB(255, 120, 170),
     RGB(255, 210, 226), RGB(30, 8, 18), 1.2f, DECO_SAKURA, 0, L"Segoe UI", L"Segoe UI"},
};
static Theme g_th;
static COLORREF g_side_mid, g_acc_hi, g_acc_lt;

#define C_BG     (g_th.bg)
#define C_SIDE   (g_side_mid)
#define C_CARD   (g_th.card)
#define C_HOVER  (g_th.hover)
#define C_BORDER (g_th.border)
#define C_TRACK  (g_th.track)
#define C_TEXT   (g_th.text)
#define C_MUTED  (g_th.muted)
#define C_DIM    (g_th.dim)
#define C_ACCENT (g_th.accent)
#define C_ACC2   (g_th.accent2)
#define C_ONACC  (g_th.on_accent)
#define C_ACCHI  (g_acc_hi)   /* accent, hovered */
#define C_ACCLT  (g_acc_lt)   /* accent tinted toward the text color, for colored text */
#define C_WHITE  RGB(255, 255, 255)
#define C_WARN   RGB(255, 186, 92)
#define C_GOLD   RGB(255, 196, 40)

static uint32_t *g_px;
static int g_pw, g_ph;
static RECT g_clip;

static void canvas_set(uint32_t *px, int w, int h) {
    g_px = px; g_pw = w; g_ph = h;
    SetRect(&g_clip, 0, 0, w, h);
}
static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static uint32_t rgbpx(COLORREF c) { return ((uint32_t)GetRValue(c) << 16) | ((uint32_t)GetGValue(c) << 8) | GetBValue(c); }
static COLORREF lerpc(COLORREF a, COLORREF b, float t) {
    t = clamp01(t);
    return RGB((int)(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t),
               (int)(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t),
               (int)(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t));
}

/* ---- theme switching ---- */
static int luma(COLORREF c) { return (GetRValue(c) * 299 + GetGValue(c) * 587 + GetBValue(c) * 114) / 1000; }
static int theme_is_light(void) { return luma(g_th.bg) > 150; }

/* "Custom" is built from just two picks: an accent and a background. Every other
   shade is mixed from those, with dark text automatically on light backgrounds. */
static void theme_build_custom(Theme *t) {
    COLORREF bg = g_cust_bg, a = g_cust_accent;
    int light = luma(bg) > 150;
    COLORREF ink = light ? RGB(0, 0, 0) : RGB(255, 255, 255);
    t->name = L"Custom";
    t->tag = L"Your own accent & background";
    t->bg = bg;
    t->side_top = lerpc(bg, ink, 0.05f);
    t->side_bot = lerpc(bg, light ? RGB(255, 255, 255) : RGB(0, 0, 0), 0.3f);
    t->card = lerpc(bg, ink, 0.06f);
    t->hover = lerpc(bg, ink, 0.11f);
    t->border = lerpc(bg, ink, 0.16f);
    t->track = lerpc(bg, ink, 0.21f);
    t->text = light ? RGB(22, 24, 30) : RGB(238, 240, 246);
    t->muted = lerpc(t->text, bg, 0.38f);
    t->dim = lerpc(t->text, bg, 0.58f);
    t->accent = a;
    t->accent2 = lerpc(a, t->text, 0.4f);
    t->on_accent = luma(a) > 165 ? RGB(12, 12, 14) : RGB(255, 255, 255);
    t->radius = 1.0f;
    t->deco = DECO_GRID;
    t->segbar = 0;
    t->font = L"Segoe UI";
    t->title_font = L"Segoe UI";
}

static void theme_get(int idx, Theme *out) {
    if (idx == TH_CUSTOM) theme_build_custom(out);
    else *out = THEMES[clampi(idx > TH_CUSTOM ? idx - 1 : idx, 0, NTHEMES - 2)];
}

static void card_fonts_reset(void);
static void make_fonts(void);
static void theme_set(int idx) {
    theme_get(idx, &g_th);
    g_side_mid = lerpc(g_th.side_top, g_th.side_bot, 0.25f);
    g_acc_hi = lerpc(g_th.accent, theme_is_light() ? RGB(0, 0, 0) : RGB(255, 255, 255), 0.14f);
    g_acc_lt = lerpc(g_th.accent, g_th.text, 0.3f);
    g_font_face = g_th.font;
    if (F_TITLE) make_fonts(); /* fonts already exist: rebuild them in the new typeface */
    card_fonts_reset();
}

static void blend(int x, int y, COLORREF c, float a) {
    if (a <= 0.0f || x < g_clip.left || y < g_clip.top || x >= g_clip.right || y >= g_clip.bottom) return;
    uint32_t *p = &g_px[y * g_pw + x];
    if (a >= 0.999f) { *p = rgbpx(c); return; }
    uint32_t d = *p;
    int ia = (int)(a * 256.0f);
    int r = (d >> 16) & 255, g = (d >> 8) & 255, b = d & 255;
    r += ((GetRValue(c) - r) * ia) >> 8;
    g += ((GetGValue(c) - g) * ia) >> 8;
    b += ((GetBValue(c) - b) * ia) >> 8;
    *p = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void bounds(float x0, float y0, float x1, float y1, int *ix0, int *iy0, int *ix1, int *iy1) {
    *ix0 = (int)floorf(x0); *iy0 = (int)floorf(y0);
    *ix1 = (int)ceilf(x1);  *iy1 = (int)ceilf(y1);
    if (*ix0 < g_clip.left) *ix0 = g_clip.left;
    if (*iy0 < g_clip.top) *iy0 = g_clip.top;
    if (*ix1 > g_clip.right) *ix1 = g_clip.right;
    if (*iy1 > g_clip.bottom) *iy1 = g_clip.bottom;
}

static void p_fill(int x0, int y0, int x1, int y1, COLORREF c) {
    int ix0, iy0, ix1, iy1;
    uint32_t v = rgbpx(c);
    bounds((float)x0, (float)y0, (float)x1, (float)y1, &ix0, &iy0, &ix1, &iy1);
    for (int y = iy0; y < iy1; y++)
        for (int x = ix0; x < ix1; x++) g_px[y * g_pw + x] = v;
}

static float rbox_d(float px, float py, float cx, float cy, float hw, float hh, float r) {
    float qx = fabsf(px - cx) - (hw - r), qy = fabsf(py - cy) - (hh - r);
    float ox = fmaxf(qx, 0), oy = fmaxf(qy, 0);
    return sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0) - r;
}

/* Rounded rectangle. border > 0 draws just an outline of that width. */
static void p_rrect(float x0, float y0, float x1, float y1, float r, float border, COLORREF c, float a) {
    float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, hw = (x1 - x0) / 2, hh = (y1 - y0) / 2;
    int ix0, iy0, ix1, iy1;
    if (hw <= 0 || hh <= 0) return;
    r *= g_th.radius;
    if (r < fminf(hw, hh) - 0.5f) r *= g_sharp; /* pills (toggles, dots) stay round */
    if (deco_animated(g_th.deco) && border <= 0 && a >= 0.999f && (c == C_CARD || c == C_HOVER || c == C_BG))
        a = 0.80f; /* animated theme: frosted-glass cards let the moving scene show through */
    if (r > hw) r = hw;
    if (r > hh) r = hh;
    bounds(x0 - 1, y0 - 1, x1 + 1, y1 + 1, &ix0, &iy0, &ix1, &iy1);
    for (int y = iy0; y < iy1; y++)
        for (int x = ix0; x < ix1; x++) {
            float d = rbox_d(x + 0.5f, y + 0.5f, cx, cy, hw, hh, r);
            float cv = clamp01(0.5f - d);
            if (border > 0) cv -= clamp01(0.5f - (d + border));
            if (cv > 0) blend(x, y, c, cv * a);
        }
}

static void p_circle(float cx, float cy, float R, float border, COLORREF c, float a) {
    int ix0, iy0, ix1, iy1;
    bounds(cx - R - 1, cy - R - 1, cx + R + 1, cy + R + 1, &ix0, &iy0, &ix1, &iy1);
    for (int y = iy0; y < iy1; y++)
        for (int x = ix0; x < ix1; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float d = sqrtf(dx * dx + dy * dy) - R;
            float cv = clamp01(0.5f - d);
            if (border > 0) cv -= clamp01(0.5f - (d + border));
            if (cv > 0) blend(x, y, c, cv * a);
        }
}

static void p_line(float x0, float y0, float x1, float y1, float w, COLORREF c, float a) {
    int ix0, iy0, ix1, iy1;
    float hw = w / 2, dx = x1 - x0, dy = y1 - y0, l2 = fmaxf(dx * dx + dy * dy, 1e-6f);
    bounds(fminf(x0, x1) - w, fminf(y0, y1) - w, fmaxf(x0, x1) + w, fmaxf(y0, y1) + w, &ix0, &iy0, &ix1, &iy1);
    for (int y = iy0; y < iy1; y++)
        for (int x = ix0; x < ix1; x++) {
            float px = x + 0.5f - x0, py = y + 0.5f - y0;
            float k = clamp01((px * dx + py * dy) / l2);
            float ex = px - dx * k, ey = py - dy * k;
            float cv = clamp01(0.5f - (sqrtf(ex * ex + ey * ey) - hw));
            if (cv > 0) blend(x, y, c, cv * a);
        }
}

/* Part of a ring from angle a0 for 'span' radians, with rounded ends. */
static void p_arc(float cx, float cy, float R, float w, float a0, float span, COLORREF c, float a) {
    int ix0, iy0, ix1, iy1;
    float hw = w / 2;
    float e0x = cx + R * cosf(a0), e0y = cy + R * sinf(a0);
    float e1x = cx + R * cosf(a0 + span), e1y = cy + R * sinf(a0 + span);
    bounds(cx - R - w, cy - R - w, cx + R + w, cy + R + w, &ix0, &iy0, &ix1, &iy1);
    for (int y = iy0; y < iy1; y++)
        for (int x = ix0; x < ix1; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float dist = sqrtf(dx * dx + dy * dy);
            if (fabsf(dist - R) > w + 1) continue;
            float rel = fmodf(atan2f(dy, dx) - a0, 6.2831853f);
            if (rel < 0) rel += 6.2831853f;
            float d;
            if (rel <= span) d = fabsf(dist - R) - hw;
            else {
                float d0 = hypotf(x + 0.5f - e0x, y + 0.5f - e0y), d1 = hypotf(x + 0.5f - e1x, y + 0.5f - e1y);
                d = fminf(d0, d1) - hw;
            }
            float cv = clamp01(0.5f - d);
            if (cv > 0) blend(x, y, c, cv * a);
        }
}

/* A crosshair preview drawn into the canvas. */
static uint32_t g_tmp[256 * 256];
static void p_crosshair(float cx, float cy, int n, int style, int color, float L, int thick, int outline, float opac) {
    if (n > 256) n = 256;
    if (n < 4 || L <= 0.3f) return;
    render(g_tmp, n, n, style, color, L, thick, outline, opac, 0);
    int ox = (int)(cx - n / 2), oy = (int)(cy - n / 2);
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            uint32_t s = g_tmp[y * n + x], A = s >> 24;
            if (A) blend(ox + x, oy + y, RGB((s >> 16) & 255, (s >> 8) & 255, s & 255), A / 255.0f);
        }
}

/* ---- theme art: shared by the settings background and the loading screen ---- */
static uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}
static float rnd01(uint32_t seed) { return (hash32(seed) & 0xFFFFFF) / 16777216.0f; }

/* soft round light, brightest in the middle */
static void p_glow(float gx, float gy, float GR, COLORREF c, float a) {
    int ix0, iy0, ix1, iy1;
    if (a <= 0.003f || GR < 1) return;
    bounds(gx - GR, gy - GR, gx + GR, gy + GR, &ix0, &iy0, &ix1, &iy1);
    for (int y = iy0; y < iy1; y++)
        for (int x = ix0; x < ix1; x++) {
            float dx = x + 0.5f - gx, dy = y + 0.5f - gy, d2 = (dx * dx + dy * dy) / (GR * GR);
            if (d2 < 1) { float k = 1 - sqrtf(d2); blend(x, y, c, a * k * k); }
        }
}

/* horizontal run of pixels with smooth ends (x0/x1 can be fractional) */
static void hspan(int y, float xa, float xb, COLORREF c, float a) {
    if (xb <= xa || a <= 0) return;
    int i0 = (int)floorf(xa), i1 = (int)ceilf(xb);
    for (int x = i0; x < i1; x++) {
        float cv = fminf((float)x + 1, xb) - fmaxf((float)x, xa);
        if (cv > 0) blend(x, y, c, a * clamp01(cv));
    }
}

/* filled five-point star (signed distance to the outline, anti-aliased) */
static void p_star(float cx, float cy, float R, COLORREF c, float a) {
    float vx[10], vy[10];
    if (R < 0.5f) return;
    for (int i = 0; i < 10; i++) {
        float an = -1.5707963f + i * 0.6283185f, r = (i & 1) ? R * 0.42f : R;
        vx[i] = cx + r * cosf(an); vy[i] = cy + r * sinf(an);
    }
    int ix0, iy0, ix1, iy1;
    bounds(cx - R - 1, cy - R - 1, cx + R + 1, cy + R + 1, &ix0, &iy0, &ix1, &iy1);
    for (int y = iy0; y < iy1; y++)
        for (int x = ix0; x < ix1; x++) {
            float px = x + 0.5f, py = y + 0.5f, best = 1e9f;
            int inside = 0;
            for (int i = 0, j = 9; i < 10; j = i++) {
                float ex = vx[i] - vx[j], ey = vy[i] - vy[j], wx = px - vx[j], wy = py - vy[j];
                float k = clamp01((wx * ex + wy * ey) / (ex * ex + ey * ey));
                float qx = wx - ex * k, qy = wy - ey * k, d = qx * qx + qy * qy;
                if (d < best) best = d;
                if (((vy[i] > py) != (vy[j] > py)) && (px < (vx[j] - vx[i]) * (py - vy[i]) / (vy[j] - vy[i]) + vx[i]))
                    inside = !inside;
            }
            float sd = inside ? -sqrtf(best) : sqrtf(best);
            float cv = clamp01(0.5f - sd);
            if (cv > 0) blend(x, y, c, cv * a);
        }
}

/* retro sunset sun: warm gradient with widening gaps across its lower half */
static void p_sun(float cx, float cy, float R, COLORREF top, COLORREF bot, float a, float scroll) {
    int ix0, iy0, ix1, iy1;
    bounds(cx - R - 1, cy - R - 1, cx + R + 1, cy + R + 1, &ix0, &iy0, &ix1, &iy1);
    for (int y = iy0; y < iy1; y++) {
        float v = (y + 0.5f - (cy - R)) / (2 * R);
        if (v > 0.48f) { /* stripes */
            float band = 0.085f, k = (v - 0.48f + scroll * band) / band, f = k - floorf(k);
            float gap = 0.12f + 0.5f * (v - 0.48f) / 0.52f;
            if (f < gap) continue;
        }
        COLORREF c = lerpc(top, bot, v);
        for (int x = ix0; x < ix1; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float cv = clamp01(R + 0.5f - sqrtf(dx * dx + dy * dy));
            if (cv > 0) blend(x, y, c, cv * a);
        }
    }
}

/* perspective floor grid that scrolls toward you (horizon at hy) */
static void p_floor_grid(int x0, int x1, float hy, int y1, COLORREF c, float a, float scroll, int cols) {
    float vx = (x0 + x1) / 2.0f, depth = y1 - hy;
    if (depth < 4) return;
    for (int k = 0; k < 24; k++) { /* rows: closer ones are further apart */
        float d = k + 1 - (scroll - floorf(scroll));
        if (d < 1) continue;
        float y = hy + depth / d;
        int iy = (int)y;
        float fr = y - iy, al = a * fminf(1.0f, 2.2f / d);
        hspan(iy, (float)x0, (float)x1, c, al * (1 - fr));
        hspan(iy + 1, (float)x0, (float)x1, c, al * fr);
    }
    float sp = (x1 - x0) / (float)cols;
    for (int y = (int)hy + 1; y < y1; y++) { /* columns meeting at the vanishing point */
        float z = (y + 0.5f - hy) / depth, al = a * clamp01(z * 3.0f);
        for (int i = -cols * 2; i <= cols * 2; i++) {
            float x = vx + i * sp * z * 1.6f;
            hspan(y, x - 0.6f, x + 0.6f, c, al);
        }
    }
    hspan((int)hy, (float)x0, (float)x1, c, a * 1.4f); /* horizon line */
}

/* night highway: asphalt, white edge lines, double yellow center, dashed lanes */
static void p_road(int x0, int x1, float hy, int y1, float half_bottom, float a, float scroll) {
    float vx = (x0 + x1) / 2.0f, depth = y1 - hy;
    for (int y = (int)hy + 1; y < y1; y++) {
        float z = (y + 0.5f - hy) / depth;           /* 0 at the horizon, 1 at the bottom */
        float half = half_bottom * z, w = fmaxf(0.7f, 3.2f * z), fade = a * clamp01(z * 4.0f);
        hspan(y, vx - half, vx + half, RGB(34, 38, 48), fade * 0.85f);           /* asphalt */
        hspan(y, vx - half, vx - half + w, RGB(225, 228, 236), fade * 0.8f);     /* edge lines */
        hspan(y, vx + half - w, vx + half, RGB(225, 228, 236), fade * 0.8f);
        float yo = half * 0.018f + w * 0.5f;                                      /* double yellow */
        hspan(y, vx - yo - w, vx - yo, RGB(255, 200, 30), fade);
        hspan(y, vx + yo, vx + yo + w, RGB(255, 200, 30), fade);
        float dist = 1.0f / fmaxf(z, 0.02f), ph = dist * 0.9f - scroll;           /* dashed lanes */
        if (ph - floorf(ph) < 0.45f) {
            hspan(y, vx - half * 0.5f - w * 0.5f, vx - half * 0.5f + w * 0.5f, RGB(225, 228, 236), fade * 0.75f);
            hspan(y, vx + half * 0.5f - w * 0.5f, vx + half * 0.5f + w * 0.5f, RGB(225, 228, 236), fade * 0.75f);
        }
    }
}

/* Police lightbar strobe: 0..1 brightness of the left (red) and right (blue) side. */
static void strobe(float t, float *left, float *right) {
    float c = fmodf(t, 0.9f) / 0.9f;
    *left = (c < 0.08f || (c > 0.14f && c < 0.22f)) ? 1.0f : 0.0f;
    *right = ((c > 0.5f && c < 0.58f) || (c > 0.64f && c < 0.72f)) ? 1.0f : 0.0f;
}

/* twisted rope ring, like the edge of a crew patch */
static void p_rope(float cx, float cy, float R, float w, float a) {
    int ix0, iy0, ix1, iy1;
    bounds(cx - R - w, cy - R - w, cx + R + w, cy + R + w, &ix0, &iy0, &ix1, &iy1);
    for (int y = iy0; y < iy1; y++)
        for (int x = ix0; x < ix1; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy, dist = sqrtf(dx * dx + dy * dy);
            float d = fabsf(dist - R) - w / 2;
            if (d > 1) continue;
            float u = (dist - R) / (w / 2);                        /* -1..1 across the rope */
            float tw = sinf(atan2f(dy, dx) * 46.0f + u * 2.2f);    /* twisted strands */
            float shade = 0.55f + 0.3f * tw + 0.15f * (1 - u * u);
            COLORREF c = lerpc(RGB(40, 40, 44), RGB(190, 190, 196), clamp01(shade));
            blend(x, y, c, a * clamp01(0.5f - d));
        }
}
/* red sunset disc with a pine treeline across the bottom */
static void p_sunset_badge(float cx, float cy, float R, float a) {
    int ix0, iy0, ix1, iy1;
    bounds(cx - R - 1, cy - R - 1, cx + R + 1, cy + R + 1, &ix0, &iy0, &ix1, &iy1);
    for (int y = iy0; y < iy1; y++) {
        float v = (y + 0.5f - (cy - R)) / (2 * R);
        COLORREF c = v < 0.55f ? lerpc(RGB(236, 112, 100), RGB(196, 40, 44), v / 0.55f)
                               : lerpc(RGB(196, 40, 44), RGB(92, 10, 14), (v - 0.55f) / 0.45f);
        for (int x = ix0; x < ix1; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float cv = clamp01(R + 0.5f - sqrtf(dx * dx + dy * dy));
            if (cv <= 0) continue;
            float u = (x + 0.5f - (cx - R)) / (2 * R); /* pine trees: zig-zag skyline */
            float tree = 0.70f + 0.07f * rnd01((uint32_t)(u * 13)) - 0.11f * fabsf(fmodf(u * 13, 1.0f) - 0.5f) * 2;
            blend(x, y, v > tree ? RGB(8, 4, 5) : c, cv * a);
        }
    }
}
/* security laser: a thin bright core with a soft red halo */
static void p_laser(float ax, float ay, float bx, float by, float a) {
    p_line(ax, ay, bx, by, dpf(7), RGB(255, 30, 40), 0.10f * a);
    p_line(ax, ay, bx, by, dpf(3), RGB(255, 40, 50), 0.30f * a);
    p_line(ax, ay, bx, by, dpf(1.1f), RGB(255, 190, 190), 0.9f * a);
}

/* smooth random blobs (value noise), used for camo */
static float vnoise(float x, float y, uint32_t seed) {
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - ix, fy = y - iy;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
#define VN(a, b) rnd01((uint32_t)((a) * 73856093) ^ (uint32_t)((b) * 19349663) ^ seed)
    float v00 = VN(ix, iy), v10 = VN(ix + 1, iy), v01 = VN(ix, iy + 1), v11 = VN(ix + 1, iy + 1);
#undef VN
    return (v00 + (v10 - v00) * fx) + ((v01 + (v11 - v01) * fx) - (v00 + (v10 - v00) * fx)) * fy;
}
/* charcoal digital camo: square pixels in four tones */
static void p_camo(int x0, int y0, int x1, int y1, int cell, float a) {
    static const COLORREF T[4] = {RGB(14, 14, 15), RGB(25, 25, 27), RGB(38, 37, 39), RGB(30, 22, 22)};
    if (cell < 2) cell = 2;
    for (int cy = y0; cy < y1; cy += cell)
        for (int cx = x0; cx < x1; cx += cell) {
            float u = (float)(cx - x0) / cell, v = (float)(cy - y0) / cell;
            float n = vnoise(u / 7.0f, v / 7.0f, 11) * 0.65f + vnoise(u / 2.5f, v / 2.5f, 29) * 0.35f;
            COLORREF c = T[n < 0.40f ? 0 : n < 0.52f ? 1 : n < 0.62f ? 2 : 3];
            for (int y = cy; y < cy + cell && y < y1; y++) hspan(y, (float)cx, (float)(cx + cell < x1 ? cx + cell : x1), c, a);
        }
}
/* wireframe globe, spinning on a tilted axis; the far side is drawn faintly */
static void p_globe(float cx, float cy, float R, float rot, COLORREF c, float a) {
    const float tilt = 0.38f, ct = cosf(tilt), st = sinf(tilt);
    p_circle(cx, cy, R, dpf(1.2f), c, a);
    for (int pass = 0; pass < 2; pass++)       /* 0 = latitude rings, 1 = meridians */
        for (int k = 0; k < (pass ? 6 : 5); k++)
            for (int i = 0; i < 28; i++) {
                float px[2], py[2], pz[2];
                for (int e = 0; e < 2; e++) {
                    float u = (i + e) / 28.0f, lat, lon;
                    if (pass) { lat = -1.5707963f + u * 3.1415927f; lon = rot + k * 0.5235988f; }
                    else { lat = (k - 2) * 0.5235988f; lon = u * 6.2831853f; }
                    float x = cosf(lat) * cosf(lon), y = sinf(lat), z = cosf(lat) * sinf(lon);
                    px[e] = cx + R * x; py[e] = cy - R * (y * ct - z * st); pz[e] = y * st + z * ct;
                }
                int front = pz[0] + pz[1] > 0;
                p_line(px[0], py[0], px[1], py[1], dpf(front ? 1.3f : 1.0f), c, a * (front ? 1.0f : 0.28f));
            }
}

/* infrared-style rifle laser: thin violet line with a soft halo */
static void p_ir_laser(float ax, float ay, float bx, float by, float a) {
    p_line(ax, ay, bx, by, dpf(6), RGB(90, 70, 255), 0.10f * a);
    p_line(ax, ay, bx, by, dpf(2.6f), RGB(100, 80, 255), 0.35f * a);
    p_line(ax, ay, bx, by, dpf(1.1f), RGB(170, 160, 255), 0.95f * a);
}
/* soft-edged rounded box (edges fade over 'feather' pixels) */
static void p_soft_rrect(float x0, float y0, float x1, float y1, float r, float feather, COLORREF c, float a) {
    float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, hw = (x1 - x0) / 2, hh = (y1 - y0) / 2;
    int ix0, iy0, ix1, iy1;
    if (r > hw) r = hw;
    if (r > hh) r = hh;
    bounds(x0 - feather, y0 - feather, x1 + feather, y1 + feather, &ix0, &iy0, &ix1, &iy1);
    for (int y = iy0; y < iy1; y++)
        for (int x = ix0; x < ix1; x++) {
            float d = rbox_d(x + 0.5f, y + 0.5f, cx, cy, hw, hh, r);
            float cv = clamp01(0.5f - d / feather);
            if (cv > 0) blend(x, y, c, cv * a);
        }
}
/* cinematic light: a long horizontal lens streak plus a reflection on the wet road below */
static void p_light_fx(float x, float y, COLORREF c, float a, float x0, float x1) {
    if (a <= 0.01f) return;
    for (int row = -1; row <= 1; row++) {
        float ra = row ? 0.35f : 1.0f;
        for (int px = (int)x0; px < (int)x1; px++) {
            float k = expf(-fabsf(px - x) / dpf(110));
            blend(px, (int)y + row, c, 0.55f * a * ra * k);
        }
    }
    for (int k = 1; k <= 5; k++) /* reflection: a column of fading glows below the light */
        p_glow(x, y + k * dpf(22), dpf(26) - k * dpf(2), c, 0.12f * a * (1 - k / 6.0f));
}
static uint32_t *bgimg_get(int res, int w, int h);

/* Midnight Run extras, placed on the picture: picture point (u, v) is at (bx + u*sx, by + v*sy).
   Rain, a passing car's headlights, exhaust backfire pops, wet-road reflections and dust. */
/* pop < 0: backfire every few seconds; otherwise pop (0..1) is the backfire strength right now */
static void drive_extras(float bx, float by, float sx, float sy, float t, int x0, int y0, int x1, int y1, float pop) {
#define PX(u) (bx + (u) * sx)
#define PY(v) (by + (v) * sy)
    float W = (float)(x1 - x0), H = (float)(y1 - y0);
    /* red taillight reflections shimmering on the wet road */
    for (int k = 0; k < 9; k++) {
        float u = 0.38f + k * 0.075f, sh = 0.5f + 0.5f * sinf(t * 3.1f + k * 1.3f);
        p_line(PX(u), PY(0.80f), PX(u) + sinf(t * 2 + k) * dpf(2), PY(1.02f), dpf(3), RGB(255, 40, 50), 0.07f + 0.06f * sh);
    }
    /* a passing car's headlights sweep across the body every few seconds */
    float pc = fmodf(t, 7.0f) / 1.4f;
    if (pc < 1) {
        float lx = x0 - dpf(150) + pc * (W + dpf(300)), fade = sinf(pc * 3.14159f);
        for (int k = 0; k < 5; k++) p_glow(lx + k * dpf(18), PY(0.32f + k * 0.08f), dpf(80), RGB(225, 230, 255), 0.09f * fade);
    }
    /* backfire: a quick orange pop with sparks from both tailpipes */
    float bf = fmodf(t + 2.3f, 5.0f), k = pop >= 0 ? pop : bf < 0.35f ? 1 - bf / 0.35f : 0;
    if (k > 0) {
        for (int e = 0; e < 2; e++) {
            float ex = PX(e ? 0.90f : 0.47f), ey = fminf(PY(0.96f), y1 - dpf(34)); /* keep the pop on screen */
            if (ex > x1 - dpf(20)) ex = x1 - dpf(20);
            p_glow(ex, ey, dpf(170), RGB(255, 110, 20), 0.30f * k);              /* wide orange wash */
            p_glow(ex, ey, dpf(60) * (0.6f + 0.4f * k), RGB(255, 140, 30), 0.95f * k);
            p_glow(ex, ey, dpf(16), RGB(255, 230, 160), 0.9f * k);
            for (int i = 0; i < 7; i++) { /* sparks fly out and fall */
                float a = -2.6f + rnd01(i * 13 + e) * 2.2f, d = (1 - k) * dpf(70) * (0.5f + rnd01(i * 7 + e));
                p_circle(ex + cosf(a) * d, ey + sinf(a) * d + (1 - k) * (1 - k) * dpf(40), dpf(1.4f), 0, RGB(255, 200, 90), k);
            }
        }
    }
    /* light rain */
    for (int i = 0; i < 44; i++) {
        float sp = 0.7f + 0.6f * rnd01(i * 3 + 70), len = dpf(12) + dpf(12) * rnd01(i * 3 + 71);
        float rx = x0 + rnd01(i * 3 + 72) * W, ry = y0 + fmodf(rnd01(i * 3 + 73) * H + t * dpf(480) * sp, H + len);
        p_line(rx, ry - len, rx - dpf(2), ry, dpf(1), RGB(180, 192, 214), 0.16f);
    }
    /* dust drifting through the light */
    for (int i = 0; i < 26; i++) {
        float dx = x0 + fmodf(rnd01(i * 5 + 90) * W + t * dpf(9) * (0.5f + rnd01(i * 5 + 91)), W);
        float dy = y0 + fmodf(rnd01(i * 5 + 92) * H - t * dpf(5) + sinf(t * 0.7f + i) * dpf(6) + H * 4, H);
        blend((int)dx, (int)dy, RGB(230, 232, 240), 0.25f + 0.25f * sinf(t * 1.3f + i));
    }
#undef PX
#undef PY
}

/* Background art for the settings window's content area. */
/* ---- art for the newer themes (drawn live, gently animated; t = seconds) ---- */
static float ui_anim_t(void) { return (GetTickCount() % 3600000u) / 1000.0f; }
static int deco_animated(int d) { return d == DECO_DRIVE || d == DECO_AURORA || d == DECO_EMBER || d == DECO_GALAXY || d == DECO_OCEAN || d == DECO_SAKURA; }

static void art_stars(int x0, int y0, int x1, int y1, int n, float t, float a, uint32_t seed) {
    int W = x1 - x0, H = y1 - y0;
    for (int i = 0; i < n; i++) {
        float sx = x0 + rnd01(seed + i * 5 + 1) * W, sy = y0 + rnd01(seed + i * 5 + 2) * H;
        float tw = 0.55f + 0.45f * sinf(t * (0.8f + rnd01(seed + i * 5 + 3) * 2.2f) + i);
        float r = dpf(0.6f + rnd01(seed + i * 5 + 4) * 1.1f);
        p_circle(sx, sy, r, 0, RGB(235, 240, 255), a * tw * (0.35f + 0.65f * rnd01(seed + i * 5 + 5)));
    }
}
static void art_vignette(int x0, int y0, int x1, int y1) { /* darker edges = depth */
    int H = y1 - y0, top = dpx(70), bot = dpx(110), side = dpx(70);
    for (int y = y0; y < y1; y++) {
        float a = 0;
        if (y - y0 < top) a += 0.22f * (1 - (y - y0) / (float)top);
        if (y1 - y < bot) a += 0.32f * (1 - (y1 - y) / (float)bot);
        if (a > 0) hspan(y, (float)x0, (float)x1, RGB(0, 0, 0), a);
        for (int x = x1 - side; x < x1; x++) blend(x, y, RGB(0, 0, 0), 0.20f * (x - (x1 - side)) / (float)side);
    }
    (void)H;
}

static void art_aurora(int x0, int y0, int x1, int y1, float t) {
    int W = x1 - x0, H = y1 - y0;
    for (int y = y0; y < y1; y++) hspan(y, (float)x0, (float)x1, C_ACC2, 0.07f * (1 - (y - y0) / (float)H)); /* violet sky at the top */
    art_stars(x0, y0, x1, y0 + H * 2 / 3, W / 9, t, 0.75f, 11);
    for (int b = 0; b < 2; b++) { /* two curtains of light */
        COLORREF c = b ? C_ACC2 : C_ACCENT;
        for (int x = x0; x < x1; x++) {
            float u = (x - x0) / (float)W;
            float cy = y0 + H * (0.30f + 0.12f * b) + sinf(u * 5.5f + t * 0.30f + b * 2.1f) * H * 0.07f + sinf(u * 13.0f + t * 0.7f) * H * 0.018f;
            float len = H * (0.20f + 0.07f * sinf(u * 8.0f + t * 0.45f + b));
            float ray = 0.45f + 0.55f * powf(0.5f + 0.5f * sinf(u * 46.0f + t * 1.1f + b * 3.0f), 2.0f);
            float strength = (b ? 0.28f : 0.40f) * ray * (0.6f + 0.4f * sinf(u * 3.0f - t * 0.25f + b));
            int ys = (int)(cy - len), ye = (int)(cy + dpf(14));
            for (int y = ys; y < ye; y++) {
                float k = y < cy ? powf((y - ys) / (cy - ys), 1.6f) : 1 - (y - cy) / dpf(14);
                blend(x, y, c, strength * clamp01(k));
            }
        }
    }
    { /* mountains on the horizon */
        float hy = y1 - H * 0.16f;
        for (int x = x0; x < x1; x++) {
            float u = (x - x0) / (float)W;
            float m = hy - H * (0.05f + 0.05f * vnoise(u * 6.0f, 0.5f, 77) + 0.025f * vnoise(u * 18.0f, 2.5f, 78));
            for (int y = (int)m; y < y1; y++) blend(x, y, lerpc(C_BG, RGB(0, 0, 0), 0.4f), y < m + 1 ? 0.6f : 0.92f);
        }
    }
}

static void art_ember(int x0, int y0, int x1, int y1, float t) {
    int W = x1 - x0, H = y1 - y0;
    for (int y = y0 + H / 2; y < y1; y++) { /* fire glow from below */
        float k = (y - (y0 + H / 2)) / (float)(H / 2);
        hspan(y, (float)x0, (float)x1, C_ACCENT, 0.20f * k * k);
    }
    float flick = 0.85f + 0.15f * sinf(t * 7.0f) * sinf(t * 3.1f);
    p_glow(x0 + W * 0.70f, (float)y1 + dpf(40), dpf(320), C_ACCENT, 0.20f * flick);
    p_glow(x0 + W * 0.25f, (float)y1 + dpf(60), dpf(260), C_ACC2, 0.10f * flick);
    for (int i = 0; i < 70; i++) { /* sparks rising and drifting */
        float sp = 0.4f + rnd01(i * 9 + 1), life = fmodf(t * 0.12f * sp + rnd01(i * 9 + 2), 1.0f);
        float ex = x0 + rnd01(i * 9 + 3) * W + sinf(t * (0.9f + rnd01(i * 9 + 4)) + i) * dpf(16) * life;
        float ey = y1 + dpf(10) - life * H * (0.6f + 0.5f * rnd01(i * 9 + 5));
        float fade = sinf(life * 3.14159f);
        COLORREF c = i % 4 ? C_ACCENT : C_ACC2;
        p_circle(ex, ey, dpf(0.9f + 1.3f * rnd01(i * 9 + 6)), 0, c, 0.85f * fade);
        if (i % 7 == 0) p_glow(ex, ey, dpf(10), c, 0.18f * fade);
    }
}

static void art_galaxy(int x0, int y0, int x1, int y1, float t) {
    int W = x1 - x0, H = y1 - y0, cell = dpx(4);
    for (int by = y0; by < y1; by += cell) /* nebula clouds (coarse noise, slowly drifting) */
        for (int bx = x0; bx < x1; bx += cell) {
            float u = (bx - x0) / (float)W, v = (by - y0) / (float)H;
            float n = vnoise(u * 3.0f + t * 0.010f, v * 2.2f, 31) * 0.65f + vnoise(u * 7.0f, v * 5.0f - t * 0.008f, 32) * 0.35f;
            float band = 1 - fabsf(v - (0.35f + 0.25f * u)) * 2.2f; /* a diagonal milky band */
            float a = clamp01(n * 1.2f - 0.35f) * clamp01(band) * 0.26f;
            if (a < 0.01f) continue;
            COLORREF c = lerpc(C_ACCENT, C_ACC2, clamp01(vnoise(u * 2.0f, v * 2.0f, 33)));
            for (int yy = by; yy < by + cell && yy < y1; yy++)
                for (int xx = bx; xx < bx + cell && xx < x1; xx++) blend(xx, yy, c, a);
        }
    art_stars(x0, y0, x1, y1, W / 6, t, 0.9f, 41);
    for (int i = 0; i < 4; i++) /* a few bright stars with flares */
        p_star(x0 + rnd01(i * 3 + 91) * W, y0 + rnd01(i * 3 + 92) * H * 0.7f, dpf(5 + 3 * rnd01(i + 93)), RGB(240, 240, 255), 0.5f + 0.3f * sinf(t * 1.3f + i));
    { /* a planet with a ring, low on the right */
        float px = x1 - W * 0.18f, py = y1 - H * 0.16f, R = dpf(70);
        p_arc(px, py, R * 1.75f, dpf(3), 3.14159f, 3.14159f, lerpc(C_ACC2, C_TEXT, 0.4f), 0.35f);
        p_circle(px, py, R, 0, lerpc(C_ACCENT, C_BG, 0.70f), 0.85f);
        for (int k = 0; k < 10; k++) p_circle(px - R * 0.25f + k * dpf(2), py - R * 0.25f, R * (1 - k * 0.08f), 0, lerpc(C_ACCENT, C_TEXT, 0.2f), 0.035f);
        p_arc(px, py, R * 1.75f, dpf(3), 0, 3.14159f, lerpc(C_ACC2, C_TEXT, 0.4f), 0.55f);
        p_glow(px, py, R * 1.6f, C_ACCENT, 0.10f);
    }
}

static void art_ocean(int x0, int y0, int x1, int y1, float t) {
    int W = x1 - x0, H = y1 - y0;
    for (int y = y0; y < y1; y++) /* lighter water near the surface */
        hspan(y, (float)x0, (float)x1, C_ACCENT, 0.12f * powf(1 - (y - y0) / (float)H, 2.0f));
    for (int r = 0; r < 6; r++) { /* light rays from the surface, swaying */
        float sx = x0 + W * (0.12f + r * 0.16f) + sinf(t * 0.4f + r * 1.7f) * dpf(20), slope = 0.35f + 0.1f * sinf(r * 2.3f);
        float w0 = dpf(18 + 14 * rnd01(r + 51)), a0 = 0.07f + 0.04f * sinf(t * 0.6f + r);
        for (int y = y0; y < y1; y++) {
            float k = 1 - (y - y0) / (float)H, cx = sx + (y - y0) * slope, w = w0 * (1 + (y - y0) / (float)H);
            for (int x = (int)(cx - w); x < cx + w; x++) blend(x, y, C_ACC2, a0 * k * k * (1 - fabsf(x - cx) / w));
        }
    }
    for (int i = 0; i < 34; i++) { /* bubbles */
        float sp = 0.5f + rnd01(i * 5 + 61), life = fmodf(t * 0.07f * sp + rnd01(i * 5 + 62), 1.0f);
        float bx = x0 + rnd01(i * 5 + 63) * W + sinf(t * 1.8f + i) * dpf(5), by = y1 - life * (H + dpf(20));
        float R = dpf(1.5f + 3.5f * rnd01(i * 5 + 64));
        p_circle(bx, by, R, dpf(1), RGB(220, 245, 255), 0.35f * sinf(life * 3.14159f));
    }
    { /* sea floor */
        float fy = y1 - H * 0.10f;
        for (int x = x0; x < x1; x++) {
            float u = (x - x0) / (float)W, m = fy - H * 0.04f * vnoise(u * 5.0f, 1.0f, 66);
            for (int y = (int)m; y < y1; y++) blend(x, y, lerpc(C_BG, RGB(0, 0, 0), 0.35f), 0.85f);
        }
    }
}

static void art_sakura(int x0, int y0, int x1, int y1, float t) {
    int W = x1 - x0, H = y1 - y0;
    float mx = x1 - W * 0.04f, my = y1 - H * 0.06f, R = dpf(56); /* moon, low on the right where it doesn't cover anything */
    p_glow(mx, my, R * 3.0f, C_ACC2, 0.08f);
    p_circle(mx, my, R, 0, lerpc(C_ACC2, RGB(255, 255, 255), 0.5f), 0.45f);
    p_circle(mx + R * 0.25f, my - R * 0.1f, R * 0.18f, 0, C_ACC2, 0.18f);
    p_circle(mx - R * 0.3f, my + R * 0.3f, R * 0.12f, 0, C_ACC2, 0.15f);
    { /* a branch from the top-left with blossoms */
        float bx = (float)x0, by = y0 + H * 0.10f;
        COLORREF bark = lerpc(C_BG, RGB(0, 0, 0), 0.5f);
        float pts[5][2] = {{0, 0}, {0.18f, 0.06f}, {0.34f, 0.04f}, {0.48f, 0.12f}, {0.56f, 0.10f}};
        for (int i = 0; i < 4; i++)
            p_line(bx + pts[i][0] * W, by + pts[i][1] * H, bx + pts[i + 1][0] * W, by + pts[i + 1][1] * H, dpf(7 - i * 1.4f), bark, 0.55f);
        p_line(bx + 0.18f * W, by + 0.06f * H, bx + 0.26f * W, by + 0.20f * H, dpf(3), bark, 0.55f);
        p_line(bx + 0.34f * W, by + 0.04f * H, bx + 0.40f * W, by - 0.02f * H, dpf(2.5f), bark, 0.55f);
        for (int i = 0; i < 46; i++) {
            float k = rnd01(i * 4 + 71);
            int seg = (int)(k * 3.99f);
            float f = k * 4 - seg;
            float px = bx + (pts[seg][0] + (pts[seg + 1][0] - pts[seg][0]) * f) * W + (rnd01(i * 4 + 72) - 0.5f) * dpf(34);
            float py = by + (pts[seg][1] + (pts[seg + 1][1] - pts[seg][1]) * f) * H + (rnd01(i * 4 + 73) - 0.5f) * dpf(26);
            p_circle(px, py, dpf(3 + 3 * rnd01(i * 4 + 74)), 0, i % 3 ? C_ACCENT : C_ACC2, 0.40f);
        }
    }
    for (int i = 0; i < 40; i++) { /* falling petals */
        float sp = 0.5f + rnd01(i * 6 + 81), life = fmodf(t * 0.05f * sp + rnd01(i * 6 + 82), 1.0f);
        float px = x0 + rnd01(i * 6 + 83) * W + sinf(t * 0.9f + i) * dpf(30) + life * dpf(60);
        float py = y0 - dpf(10) + life * (H + dpf(20));
        float sz = dpf(2.2f + 2.0f * rnd01(i * 6 + 84)), ang = t * (1 + rnd01(i * 6 + 85)) + i;
        p_line(px - cosf(ang) * sz, py - sinf(ang) * sz * 0.5f, px + cosf(ang) * sz, py + sinf(ang) * sz * 0.5f, sz * 1.1f, C_ACCENT, 0.7f);
    }
    for (int y = y1 - H / 4; y < y1; y++) hspan(y, (float)x0, (float)x1, C_ACC2, 0.07f * (y - (y1 - H / 4)) / (H / 4.0f)); /* mist */
}

static void ui_deco(int x0, int y0, int x1, int y1) {
    RECT old = g_clip;
    SetRect(&g_clip, x0, y0, x1, y1);
    IntersectRect(&g_clip, &g_clip, &old);
    int gs = dpx(28), W = x1 - x0, H = y1 - y0;
    COLORREF ink = C_TEXT;
    switch (g_th.deco) {
    case DECO_PATROL:
        for (int gx = x0 + gs; gx < x1; gx += gs) for (int y = y0; y < y1; y++) blend(gx, y, ink, 0.016f);
        for (int gy = y0 + gs; gy < y1; gy += gs) for (int x = x0; x < x1; x++) blend(x, gy, ink, 0.016f);
        p_glow((float)(x1 - dpx(260)), (float)-dpx(70), dpf(330), C_ACC2, 0.11f);
        p_glow((float)(x1 - dpx(20)), (float)-dpx(50), dpf(330), C_ACCENT, 0.17f);
        p_road(x0, x1, (float)(y1 - dpx(90)), y1 + dpx(10), W * 0.42f, 0.22f, 0);
        break;
    case DECO_CRIMINAL:
        p_glow((float)(x1 - dpx(40)), (float)-dpx(40), dpf(340), C_ACCENT, 0.16f);
        p_glow((float)(x0 + dpx(60)), (float)(y1 + dpx(60)), dpf(300), C_ACC2, 0.06f);
        p_line((float)(x0 + W * 0.15f), (float)y0, (float)(x0 + W * 0.65f), (float)y1, dpf(1), C_ACCENT, 0.14f);
        p_line((float)(x0 + W * 0.95f), (float)y0, (float)(x0 + W * 0.40f), (float)y1, dpf(1), C_ACCENT, 0.10f);
        p_rope((float)(x1 - dpx(70)), (float)(y1 - dpx(60)), dpf(110), dpf(10), 0.10f);
        break;
    case DECO_OPS:
        p_camo(x0, y0, x1, y1, dpx(6), 0.55f);
        p_glow((float)(x1 - dpx(40)), (float)-dpx(40), dpf(320), C_ACCENT, 0.12f);
        p_rope((float)(x1 - dpx(70)), (float)(y1 - dpx(60)), dpf(110), dpf(10), 0.10f);
        break;
    case DECO_INTEL:
        p_glow((float)(x1 - dpx(150)), (float)(y1 - dpx(110)), dpf(260), C_ACCENT, 0.08f);
        p_globe((float)(x1 - dpx(150)), (float)(y1 - dpx(110)), dpf(150), 0.4f, C_ACC2, 0.35f);
        for (int gy = y0 + gs; gy < y1; gy += gs) for (int x = x0; x < x1; x++) blend(x, gy, ink, 0.015f);
        break;
    case DECO_DRIVE: { /* the Charger scene, live: drifting camera, breathing taillights, smoke */
        float t = (GetTickCount() % 3600000u) / 1000.0f;
        int ch2 = H * 106 / 100, cw2 = ch2 * 1210 / 735; /* cover the area by height, a little extra to drift */
        uint32_t *img = W >= dpx(300) ? bgimg_get(11, cw2, ch2) : NULL;
        if (img && cw2 >= W) {
            /* keep the taillights (right part of the shot) in view, drifting slowly side to side */
            float ox = W * 0.5f - cw2 * 0.70f + sinf(t * 0.12f) * dpf(26), oy = -(ch2 - H) * (0.5f + 0.45f * sinf(t * 0.09f));
            if (ox > 0) ox = 0;
            if (ox < W - cw2) ox = (float)(W - cw2);
            int ix = (int)ox, iy = (int)oy;
            for (int y = 0; y < H; y++) {
                int sy = y - iy;
                if (sy < 0 || sy >= ch2 || y0 + y < g_clip.top || y0 + y >= g_clip.bottom) continue;
                int cx0 = x0 < g_clip.left ? g_clip.left : x0, cx1 = x1 > g_clip.right ? g_clip.right : x1;
                if (cx1 > cx0) memcpy(g_px + (y0 + y) * g_pw + cx0, img + sy * cw2 + (cx0 - x0 - ix), (cx1 - cx0) * 4);
            }
#define UX(u) (x0 + ox + (u) * cw2)
#define VY(v) (y0 + oy + (v) * ch2)
            float breathe = 0.55f + 0.45f * (0.5f + 0.5f * sinf(t * 1.6f));
            static const float TL[8][2] = {{0.347f, 0.564f}, {0.372f, 0.525f}, {0.453f, 0.518f}, {0.539f, 0.532f},
                                           {0.690f, 0.541f}, {0.841f, 0.532f}, {0.927f, 0.559f}, {1.000f, 0.585f}};
            for (int i = 0; i < 7; i++) {
                p_line(UX(TL[i][0]), VY(TL[i][1]), UX(TL[i + 1][0]), VY(TL[i + 1][1]), dpf(10), RGB(255, 20, 30), 0.12f * breathe);
                p_line(UX(TL[i][0]), VY(TL[i][1]), UX(TL[i + 1][0]), VY(TL[i + 1][1]), dpf(3), RGB(255, 50, 60), 0.6f * breathe);
            }
            p_glow(UX(0.66f), VY(0.535f), dpf(200), RGB(255, 20, 30), 0.10f * breathe);
            float pulse = fmodf(t, 6.0f) < 0.5f ? 1.0f - fmodf(t, 6.0f) / 0.5f : 0; /* brake tap every few seconds */
            if (pulse > 0) p_glow(UX(0.668f), VY(0.381f), dpf(60), RGB(255, 30, 40), 0.5f * pulse);
            p_glow(UX(0.372f), VY(0.293f), dpf(40), RGB(240, 244, 255), 0.18f + 0.22f * (0.5f + 0.5f * sinf(t * 2.3f)));
            for (int e = 0; e < 2; e++)
                for (int i = 0; i < 9; i++) {
                    float life = fmodf(t * 0.4f + i / 9.0f + e * 0.37f, 1.0f);
                    float ex = UX(e ? 0.90f : 0.47f) + (e ? 1 : -1) * life * dpf(50) + sinf(t * 1.5f + i * 1.7f) * dpf(8) * life;
                    float ey = VY(0.96f) - life * dpf(80);
                    p_glow(ex, ey, dpf(14) + life * dpf(44), RGB(170, 176, 190), 0.22f * sinf(life * 3.14159f));
                }
#undef UX
#undef VY
            drive_extras(x0 + ox, y0 + oy, (float)cw2, (float)ch2, t, x0, y0, x1, y1, -1);
            for (int y = y0; y < y1; y++) /* shade it so the controls on top stay readable */
                hspan(y, (float)x0, (float)x1, C_BG, 0.46f + 0.30f * clamp01((float)(y - y0 - H * 0.6f) / (H * 0.4f)));
            for (int y = y0; y < y1; y++) /* darker on the left, under the page title */
                for (int x = x0; x < x0 + W / 2; x++) blend(x, y, C_BG, 0.25f * (1 - (x - x0) / (W / 2.0f)));
        } else { /* small previews: a red taillight streak and its glow */
            float ty = (float)(y1 - dpx(70));
            p_glow((x0 + x1) / 2.0f, ty, dpf(260), C_ACCENT, 0.10f);
            for (int x = x0; x < x1; x++) {
                float u = (x - x0) / (float)W, k = 1 - fabsf(u - 0.5f) * 2;
                float y = ty + dpf(18) * (u - 0.5f) * (u - 0.5f) * 4;
                blend(x, (int)y, C_ACCENT, 0.55f * k);
                blend(x, (int)y + 1, C_ACCENT, 0.25f * k);
            }
        }
        break;
    }
    case DECO_RAID:
        p_glow((float)(x1 - dpx(120)), (float)(y1 - dpx(90)), dpf(200), RGB(200, 210, 255), 0.07f);
        p_ir_laser((float)x0, (float)(y0 + H * 0.62f), (float)x1, (float)(y0 + H * 0.48f), 0.35f);
        p_ir_laser((float)x0, (float)(y0 + H * 0.70f), (float)x1, (float)(y0 + H * 0.86f), 0.25f);
        for (int y = y0; y < y1; y += 2) hspan(y, (float)x0, (float)x1, RGB(0, 0, 0), 0.18f);
        break;
    case DECO_TACTICAL:
        for (int gy = y0 + gs; gy < y1; gy += gs)
            for (int gx = x0 + gs; gx < x1; gx += gs) { blend(gx, gy, ink, 0.12f); blend(gx + 1, gy, ink, 0.05f); }
        for (int x = x0 + gs / 2, i = 0; x < x1; x += gs / 2, i++) /* ruler along the top */
            p_fill(x, y0, x + 1, y0 + (i % 4 == 0 ? dpx(9) : dpx(4)), lerpc(C_BG, C_ACCENT, 0.35f));
        p_glow((float)(x1 - dpx(40)), (float)-dpx(40), dpf(300), C_ACCENT, 0.07f);
        p_glow((float)(x0 + dpx(80)), (float)(y1 + dpx(40)), dpf(280), C_ACC2, 0.06f);
        { /* HUD brackets in the corners */
            float m = dpf(12), L = dpf(22), w = dpf(1.5f);
            COLORREF c = lerpc(C_BG, C_ACCENT, 0.5f);
            p_line(x0 + m, y1 - m, x0 + m + L, y1 - m, w, c, 1); p_line(x0 + m, y1 - m, x0 + m, y1 - m - L, w, c, 1);
            p_line(x1 - m, y1 - m, x1 - m - L, y1 - m, w, c, 1); p_line(x1 - m, y1 - m, x1 - m, y1 - m - L, w, c, 1);
        }
        break;
    case DECO_ARCADE:
        p_glow((float)(x1 - dpx(30)), (float)-dpx(40), dpf(340), C_ACC2, 0.15f);
        p_glow((float)(x0 + dpx(40)), (float)(y1 + dpx(30)), dpf(300), C_ACCENT, 0.09f);
        for (int i = 0; i < 70; i++) { /* pixel stars */
            int sx = x0 + (int)(rnd01(i * 3 + 1) * W), sy = y0 + (int)(rnd01(i * 3 + 2) * H), sz = dpx(2);
            float al = 0.12f + 0.25f * rnd01(i * 3 + 3);
            for (int yy = 0; yy < sz; yy++) for (int xx = 0; xx < sz; xx++) blend(sx + xx, sy + yy, ink, al);
        }
        for (int y = y0; y < y1; y += 3) for (int x = x0; x < x1; x++) blend(x, y, RGB(0, 0, 0), 0.35f);
        break;
    case DECO_SUNSET:
        for (int y = y0; y < y1; y++) hspan(y, (float)x0, (float)x1, C_ACCENT, 0.10f * (float)(y - y0) / H);
        p_sun((float)(x1 - dpx(150)), (float)(y1 - dpx(70)), dpf(120), RGB(255, 214, 90), C_ACCENT, 0.22f, 0);
        p_floor_grid(x0, x1, (float)(y1 - dpx(120)), y1, C_ACC2, 0.13f, 0, 10);
        p_glow((float)(x0 + dpx(60)), (float)-dpx(50), dpf(300), C_ACC2, 0.08f);
        break;
    case DECO_CYBER:
        for (int y = y0; y < y1; y++) /* diagonal pinstripes */
            for (int x = x0 + ((dpx(22) - (y % dpx(22))) % dpx(22)); x < x1; x += dpx(22)) blend(x, y, ink, 0.025f);
        p_glow((float)(x1 - dpx(30)), (float)-dpx(40), dpf(320), C_ACCENT, 0.09f);
        p_glow((float)(x0 + dpx(30)), (float)(y1 + dpx(40)), dpf(320), C_ACC2, 0.09f);
        { /* hazard tape at the bottom right */
            int hx0 = x1 - dpx(220), hy0 = y1 - dpx(6), st = dpx(10);
            for (int y = hy0; y < y1; y++)
                for (int x = hx0; x < x1; x++) blend(x, y, ((x + y) / st) % 2 ? C_ACCENT : RGB(0, 0, 0), 0.75f);
        }
        break;
    case DECO_AURORA: art_aurora(x0, y0, x1, y1, ui_anim_t()); break;
    case DECO_EMBER: art_ember(x0, y0, x1, y1, ui_anim_t()); break;
    case DECO_GALAXY: art_galaxy(x0, y0, x1, y1, ui_anim_t()); break;
    case DECO_OCEAN: art_ocean(x0, y0, x1, y1, ui_anim_t()); break;
    case DECO_SAKURA: art_sakura(x0, y0, x1, y1, ui_anim_t()); break;
    default: /* grid + accent glow in the top-right corner (the original look) */
        for (int gx = x0 + gs; gx < x1; gx += gs) for (int y = y0; y < y1; y++) blend(gx, y, ink, 0.022f);
        for (int gy = y0 + gs; gy < y1; gy += gs) for (int x = x0; x < x1; x++) blend(x, gy, ink, 0.022f);
        p_glow((float)(x1 - dpx(40)), (float)-dpx(40), dpf(360), C_ACCENT, 0.13f);
        break;
    }
    if (g_th.deco != DECO_DRIVE && W > dpx(300)) art_vignette(x0, y0, x1, y1); /* depth on every theme (not the tiny previews) */
    g_clip = old;
}

/* Text is queued while shapes are drawn, then drawn last so it sits on top. */
typedef struct { RECT r, clip; HFONT f; COLORREF c; UINT fmt; int spacing; wchar_t s[260]; } TextCmd;
static TextCmd g_tq[256];
static int g_ntq;
static void q_text_ex(int x0, int y0, int x1, int y1, const wchar_t *s, HFONT f, COLORREF c, UINT fmt, int spacing) {
    if (g_ntq >= 256) return;
    TextCmd *t = &g_tq[g_ntq++];
    SetRect(&t->r, x0, y0, x1, y1);
    t->clip = g_clip;
    t->f = f; t->c = c; t->fmt = fmt; t->spacing = spacing;
    wcsncpy(t->s, s, 259);
    t->s[259] = 0;
}
static void q_text(int x0, int y0, int x1, int y1, const wchar_t *s, HFONT f, COLORREF c, UINT fmt) {
    q_text_ex(x0, y0, x1, y1, s, f, c, fmt, 0);
}
static void flush_text(HDC dc) {
    SetBkMode(dc, TRANSPARENT);
    for (int i = 0; i < g_ntq; i++) {
        TextCmd *t = &g_tq[i];
        SaveDC(dc);
        IntersectClipRect(dc, t->clip.left, t->clip.top, t->clip.right, t->clip.bottom);
        SelectObject(dc, t->f);
        SetTextColor(dc, t->c);
        SetTextCharacterExtra(dc, t->spacing);
        UINT fmt = t->fmt;
        if (t->spacing && !(fmt & DT_WORDBREAK)) {
            /* letter-spaced labels: measure with the spacing included and place them ourselves
               (DrawText measured without it, so the end of the word got cut off) */
            SIZE z = {0};
            int n = lstrlenW(t->s);
            GetTextExtentPoint32W(dc, t->s, n, &z);
            z.cx -= t->spacing; /* no gap after the last letter */
            int x = fmt & DT_RIGHT ? t->r.right - z.cx : fmt & DT_CENTER ? (t->r.left + t->r.right - z.cx) / 2 : t->r.left;
            int y = (t->r.top + t->r.bottom - z.cy) / 2;
            TextOutW(dc, x, y, t->s, n);
            RestoreDC(dc, -1);
            continue;
        }
        if (!(fmt & DT_WORDBREAK)) fmt |= DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;
        DrawTextW(dc, t->s, -1, &t->r, fmt | DT_NOPREFIX);
        RestoreDC(dc, -1);
    }
    g_ntq = 0;
}

/* ================= "Report a problem" window ================= */
/* The player picks what's wrong and says what happened; that goes to Discord with
   the log attached (or into bug-report.txt when no webhook is set). Plain GDI +
   one native edit box, drawn in the current theme's colors. */
static const wchar_t *REPORT_CATS[] = {L"Crashing / closing", L"Crosshair not showing", L"FPS Boost not working",
                                       L"Mouse cursor problem", L"Settings / keybinds", L"Something else"};
#define NRCAT 6
/* optional quick answers, so most reports need no typing */
static const wchar_t *REPORT_OFTEN[] = {L"Every time", L"Sometimes", L"Just once"};
static const wchar_t *REPORT_DOING[] = {L"Playing Roblox", L"In the settings", L"Starting the app", L"After an update"};
#define NROFTEN 3
#define NRDOING 4
#define RD_W 580
#define RD_H 612
enum { RH_NONE = -1, RH_OFTEN = 20, RH_DOING = 40, RH_SEND = 100, RH_CANCEL = 101 };
static HWND g_redit, g_rowner;
static int g_rcat = -1, g_roften = -1, g_rdoing = -1, g_rhov = RH_NONE, g_rwarn, g_rowner_cap;
static RECT g_rchip[NRCAT], g_roft[NROFTEN], g_rdo[NRDOING], g_rsend, g_rcancel, g_reditbox;
static HBRUSH g_rcardbr;
static void ui_set_status(const wchar_t *s);
static void ui_dwm(HWND h);
static int dpx(int v);
static void sync_cursor(void);

static void rd_rrect(HDC dc, const RECT *r, int rad, COLORREF fill, COLORREF line) {
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, line);
    HGDIOBJ ob = SelectObject(dc, br), op = SelectObject(dc, pen);
    RoundRect(dc, r->left, r->top, r->right, r->bottom, rad, rad);
    SelectObject(dc, ob); SelectObject(dc, op);
    DeleteObject(br); DeleteObject(pen);
}
static void rd_text(HDC dc, int x0, int y0, int x1, int y1, const wchar_t *t, HFONT f, COLORREF c, UINT fmt) {
    RECT r = {x0, y0, x1, y1};
    SelectObject(dc, f);
    SetTextColor(dc, c);
    DrawTextW(dc, t, -1, &r, fmt | DT_NOPREFIX);
}
static void rd_layout(void) {
    int pad = dpx(26), w = dpx(RD_W), gap = dpx(10), cw = (w - 2 * pad - 2 * gap) / 3, ch = dpx(38);
    for (int i = 0; i < NRCAT; i++) {
        int x = pad + (i % 3) * (cw + gap), y = dpx(122) + (i / 3) * (ch + gap);
        SetRect(&g_rchip[i], x, y, x + cw, y + ch);
    }
    int ow = (w - 2 * pad - 2 * gap) / 3, sh = dpx(32);
    for (int i = 0; i < NROFTEN; i++) SetRect(&g_roft[i], pad + i * (ow + gap), dpx(254), pad + i * (ow + gap) + ow, dpx(254) + sh);
    int dw = (w - 2 * pad - 3 * gap) / 4;
    for (int i = 0; i < NRDOING; i++) SetRect(&g_rdo[i], pad + i * (dw + gap), dpx(326), pad + i * (dw + gap) + dw, dpx(326) + sh);
    SetRect(&g_reditbox, pad, dpx(400), w - pad, dpx(496));
    SetRect(&g_rsend, w - pad - dpx(170), dpx(RD_H - 64), w - pad, dpx(RD_H - 24));
    SetRect(&g_rcancel, g_rsend.left - dpx(12) - dpx(110), g_rsend.top, g_rsend.left - dpx(12), g_rsend.bottom);
}
static void rd_paint(HWND h) {
    PAINTSTRUCT ps;
    HDC wdc = BeginPaint(h, &ps);
    RECT cr;
    GetClientRect(h, &cr);
    HDC dc = CreateCompatibleDC(wdc);
    HBITMAP bm = CreateCompatibleBitmap(wdc, cr.right, cr.bottom);
    HGDIOBJ obm = SelectObject(dc, bm);
    HBRUSH bg = CreateSolidBrush(C_BG);
    FillRect(dc, &cr, bg);
    DeleteObject(bg);
    { RECT strip = {0, 0, cr.right, dpx(3)}; HBRUSH a = CreateSolidBrush(C_ACCENT); FillRect(dc, &strip, a); DeleteObject(a); }
    SetBkMode(dc, TRANSPARENT);
    int pad = dpx(26), w = cr.right;
    rd_text(dc, pad, dpx(20), w - pad, dpx(52), L"Report a problem", F_HEAD, C_TEXT, DT_LEFT | DT_SINGLELINE);
    rd_text(dc, pad, dpx(54), w - pad, dpx(76), L"A few clicks is enough \x2014 it goes straight to simplysoul with your app's log.", F_SMALL, C_MUTED, DT_LEFT | DT_SINGLELINE);
    rd_text(dc, pad, dpx(92), w - pad, dpx(116), L"1.  What's wrong?", F_BOLD, g_rwarn ? C_WARN : C_TEXT, DT_LEFT | DT_SINGLELINE);
    if (g_rwarn) rd_text(dc, pad, dpx(92), w - pad, dpx(116), L"Pick one first", F_SMALL, C_WARN, DT_RIGHT | DT_SINGLELINE);
    rd_text(dc, pad, dpx(226), w - pad, dpx(250), L"2.  How often?", F_BOLD, C_TEXT, DT_LEFT | DT_SINGLELINE);
    rd_text(dc, pad, dpx(226), w - pad, dpx(250), L"optional", F_SMALL, C_DIM, DT_RIGHT | DT_SINGLELINE);
    rd_text(dc, pad, dpx(298), w - pad, dpx(322), L"3.  What were you doing?", F_BOLD, C_TEXT, DT_LEFT | DT_SINGLELINE);
    rd_text(dc, pad, dpx(298), w - pad, dpx(322), L"optional", F_SMALL, C_DIM, DT_RIGHT | DT_SINGLELINE);
    for (int i = 0; i < NROFTEN; i++) {
        int sel = i == g_roften, hv = RH_OFTEN + i == g_rhov;
        rd_rrect(dc, &g_roft[i], dpx(12), sel ? C_ACCENT : hv ? C_HOVER : C_CARD, sel ? C_ACCENT : C_BORDER);
        rd_text(dc, g_roft[i].left, g_roft[i].top, g_roft[i].right, g_roft[i].bottom, REPORT_OFTEN[i], sel ? F_BOLD : F_SMALL, sel ? C_ONACC : C_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    for (int i = 0; i < NRDOING; i++) {
        int sel = i == g_rdoing, hv = RH_DOING + i == g_rhov;
        rd_rrect(dc, &g_rdo[i], dpx(12), sel ? C_ACCENT : hv ? C_HOVER : C_CARD, sel ? C_ACCENT : C_BORDER);
        rd_text(dc, g_rdo[i].left, g_rdo[i].top, g_rdo[i].right, g_rdo[i].bottom, REPORT_DOING[i], sel ? F_BOLD : F_SMALL, sel ? C_ONACC : C_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    for (int i = 0; i < NRCAT; i++) {
        int sel = i == g_rcat, hv = i == g_rhov;
        rd_rrect(dc, &g_rchip[i], dpx(14), sel ? C_ACCENT : hv ? C_HOVER : C_CARD, sel ? C_ACCENT : C_BORDER);
        rd_text(dc, g_rchip[i].left, g_rchip[i].top, g_rchip[i].right, g_rchip[i].bottom, REPORT_CATS[i],
                sel ? F_BOLD : F_TEXT, sel ? C_ONACC : C_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    rd_text(dc, pad, dpx(370), w - pad, dpx(394), L"4.  Anything else?", F_BOLD, C_TEXT, DT_LEFT | DT_SINGLELINE);
    rd_text(dc, pad, dpx(370), w - pad, dpx(394), L"optional \x2014 which game, what you clicked...", F_SMALL, C_DIM, DT_RIGHT | DT_SINGLELINE);
    rd_rrect(dc, &g_reditbox, dpx(12), C_CARD, GetFocus() == g_redit ? C_ACCENT : C_BORDER);
    rd_text(dc, pad, dpx(504), w - pad, dpx(526),
            L"Sent with it: the app's log, settings, Windows version and screen size. No personal files.",
            F_SMALL, C_DIM, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    rd_rrect(dc, &g_rcancel, dpx(16), g_rhov == RH_CANCEL ? C_HOVER : C_CARD, C_BORDER);
    rd_text(dc, g_rcancel.left, g_rcancel.top, g_rcancel.right, g_rcancel.bottom, L"Cancel", F_BOLD, C_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    rd_rrect(dc, &g_rsend, dpx(16), g_rhov == RH_SEND ? C_ACCHI : C_ACCENT, g_rhov == RH_SEND ? C_ACCHI : C_ACCENT);
    rd_text(dc, g_rsend.left, g_rsend.top, g_rsend.right, g_rsend.bottom, webhook_set() ? L"Send report" : L"Save report",
            F_BOLD, C_ONACC, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    { HBRUSH b = CreateSolidBrush(C_BORDER); FrameRect(dc, &cr, b); DeleteObject(b); }
    BitBlt(wdc, 0, 0, cr.right, cr.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, obm);
    DeleteObject(bm);
    DeleteDC(dc);
    EndPaint(h, &ps);
}
static int rd_hit(int x, int y) {
    POINT p = {x, y};
    for (int i = 0; i < NRCAT; i++) if (PtInRect(&g_rchip[i], p)) return i;
    for (int i = 0; i < NROFTEN; i++) if (PtInRect(&g_roft[i], p)) return RH_OFTEN + i;
    for (int i = 0; i < NRDOING; i++) if (PtInRect(&g_rdo[i], p)) return RH_DOING + i;
    if (PtInRect(&g_rsend, p)) return RH_SEND;
    if (PtInRect(&g_rcancel, p)) return RH_CANCEL;
    return RH_NONE;
}
static void rd_close(void) { if (g_rdlg) DestroyWindow(g_rdlg); }
static void rd_send(void) {
    if (g_rcat < 0) { g_rwarn = 1; InvalidateRect(g_rdlg, NULL, FALSE); return; }
    wchar_t desc[1601], cat[200];
    GetWindowTextW(g_redit, desc, 1601);
    lstrcpynW(cat, REPORT_CATS[g_rcat], 200);
    if (g_roften >= 0) { lstrcatW(cat, L" \x2022 "); lstrcatW(cat, REPORT_OFTEN[g_roften]); }
    if (g_rdoing >= 0) { lstrcatW(cat, L" \x2022 "); lstrcatW(cat, g_rdoing == 1 ? L"in the settings panel" : g_rdoing == 0 ? L"while playing Roblox" : g_rdoing == 2 ? L"when starting the app" : L"after an update"); }
    wchar_t msg[200];
    if (webhook_set()) {
        if (g_last_manual && GetTickCount() - g_last_manual < 60000) {
            MessageBoxW(g_rdlg, L"A report was just sent. Please wait a minute before sending another.", L"Report a problem", MB_OK | MB_ICONINFORMATION);
            return;
        }
        if (!report_send(2, cat, desc)) { MessageBoxW(g_rdlg, L"Couldn't build the report.", L"Report a problem", MB_OK | MB_ICONWARNING); return; }
        g_last_manual = GetTickCount();
        log_info(L"user report: %s", cat);
        wsprintfW(msg, L"Sending your report (ref %s)...", g_last_ref);
    } else {
        wchar_t path[MAX_PATH];
        wcscpy(msg, report_save(path, cat, desc) ? L"Saved bug-report.txt \x2014 send that file to simplysoul on Discord." : L"Couldn't save the report.");
    }
    rd_close();
    if (g_keydlg) ui_set_status(msg);
}
static LRESULT CALLBACK rd_edit_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref) {
    (void)id; (void)ref;
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) { rd_close(); return 0; }
    if (msg == WM_KEYDOWN && wp == VK_RETURN && (GetKeyState(VK_CONTROL) & 0x8000)) { rd_send(); return 0; }
    if (msg == WM_CHAR && (wp == 1 || wp == 27 || (wp == '\n' && (GetKeyState(VK_CONTROL) & 0x8000)))) { /* Ctrl+A, Esc, Ctrl+Enter */
        if (wp == 1) SendMessageW(h, EM_SETSEL, 0, -1);
        return 0;
    }
    if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS) InvalidateRect(GetParent(h), &g_reditbox, FALSE);
    return DefSubclassProc(h, msg, wp, lp);
}
static LRESULT CALLBACK rd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: rd_paint(h); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp, C_TEXT);
        SetBkColor((HDC)wp, C_CARD);
        return (LRESULT)g_rcardbr;
    case WM_NCHITTEST: { /* drag by any empty spot */
        POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(h, &p);
        LRESULT r = DefWindowProcW(h, msg, wp, lp);
        if (r == HTCLIENT && rd_hit(p.x, p.y) == RH_NONE && p.y < g_reditbox.top) return HTCAPTION;
        return r;
    }
    case WM_MOUSEMOVE: {
        int hv = rd_hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (hv != g_rhov) { g_rhov = hv; InvalidateRect(h, NULL, FALSE); }
        TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, h, 0};
        TrackMouseEvent(&t);
        return 0;
    }
    case WM_MOUSELEAVE: if (g_rhov != RH_NONE) { g_rhov = RH_NONE; InvalidateRect(h, NULL, FALSE); } return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            POINT p; GetCursorPos(&p); ScreenToClient(h, &p);
            SetCursor(LoadCursorW(NULL, (LPCWSTR)(rd_hit(p.x, p.y) != RH_NONE ? IDC_HAND : IDC_ARROW)));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN: {
        int k = rd_hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (k >= 0 && k < NRCAT) { g_rcat = k; g_rwarn = 0; InvalidateRect(h, NULL, FALSE); }
        else if (k >= RH_OFTEN && k < RH_OFTEN + NROFTEN) { g_roften = g_roften == k - RH_OFTEN ? -1 : k - RH_OFTEN; InvalidateRect(h, NULL, FALSE); }
        else if (k >= RH_DOING && k < RH_DOING + NRDOING) { g_rdoing = g_rdoing == k - RH_DOING ? -1 : k - RH_DOING; InvalidateRect(h, NULL, FALSE); }
        else if (k == RH_SEND) rd_send();
        else if (k == RH_CANCEL) rd_close();
        return 0;
    }
    case WM_KEYDOWN: if (wp == VK_ESCAPE) rd_close(); return 0;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY:
        if (g_rowner && IsWindow(g_rowner)) { EnableWindow(g_rowner, TRUE); SetForegroundWindow(g_rowner); }
        if (g_rowner == g_keydlg) g_capture = g_rowner_cap;
        g_rdlg = g_redit = g_rowner = NULL;
        if (g_rcardbr) { DeleteObject(g_rcardbr); g_rcardbr = NULL; }
        sync_cursor();
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
/* Open the window. cat = a category to start with (-1 = none). */
static void report_dialog(int cat) {
    crumb(L"opened Report a problem");
    static int registered;
    if (g_rdlg) { SetForegroundWindow(g_rdlg); return; }
    if (!registered) {
        WNDCLASSW wc = {0};
        wc.lpfnWndProc = rd_proc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = L"CrosshairReport";
        wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
        wc.style = CS_DROPSHADOW;
        registered = RegisterClassW(&wc) != 0;
    }
    g_rcat = cat; g_rhov = RH_NONE; g_rwarn = 0; g_roften = -1; g_rdoing = -1;
    if (cat == 0) { g_roften = 2; } /* from the "it crashed" popup */
    else if (cat < 0 && g_keydlg) g_rdoing = 1;
    g_rowner = g_keydlg;
    rd_layout();
    int w = dpx(RD_W), hh = dpx(RD_H);
    RECT wa;
    POINT c;
    GetCursorPos(&c);
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    SetRect(&wa, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    if (g_rowner) GetWindowRect(g_rowner, &wa);
    else if (GetMonitorInfoW(MonitorFromPoint(c, MONITOR_DEFAULTTOPRIMARY), &mi)) wa = mi.rcWork;
    g_rdlg = CreateWindowExW(WS_EX_TOPMOST | (g_rowner ? 0 : WS_EX_APPWINDOW), L"CrosshairReport", L"Report a problem",
                             WS_POPUP | WS_SYSMENU, wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - hh) / 2,
                             w, hh, g_rowner, NULL, GetModuleHandleW(NULL), NULL);
    if (!g_rdlg) return;
    g_rcardbr = CreateSolidBrush(C_CARD);
    g_redit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                              g_reditbox.left + dpx(12), g_reditbox.top + dpx(10), g_reditbox.right - g_reditbox.left - dpx(18),
                              g_reditbox.bottom - g_reditbox.top - dpx(18), g_rdlg, NULL, GetModuleHandleW(NULL), NULL);
    SendMessageW(g_redit, WM_SETFONT, (WPARAM)F_TEXT, TRUE);
    SendMessageW(g_redit, EM_SETLIMITTEXT, 1500, 0);
    SetWindowSubclass(g_redit, rd_edit_proc, 1, 0);
    ui_dwm(g_rdlg);
    if (g_rowner) { g_rowner_cap = g_capture; g_capture = -2; EnableWindow(g_rowner, FALSE); } /* keep the panel open */
    if (g_cursor_hidden) restore_cursor();
    ShowWindow(g_rdlg, SW_SHOWNORMAL);
    SetForegroundWindow(g_rdlg);
    SetFocus(cat >= 0 ? g_redit : g_rdlg);
}

/* ================= "What's new" window (after an update) ================= */
/* Shows the release notes built into this exe (resource 12). */
#define WN_W 520
static wchar_t g_wnlines[12][200];
static int g_wnn, g_wnhov;
static RECT g_wnok;
static int wn_height(void) { return dpx(150) + g_wnn * dpx(30) + dpx(64); }
static void wn_paint(HWND h) {
    PAINTSTRUCT ps;
    HDC wdc = BeginPaint(h, &ps);
    RECT cr;
    GetClientRect(h, &cr);
    HDC dc = CreateCompatibleDC(wdc);
    HBITMAP bm = CreateCompatibleBitmap(wdc, cr.right, cr.bottom);
    HGDIOBJ obm = SelectObject(dc, bm);
    HBRUSH bg = CreateSolidBrush(C_BG);
    FillRect(dc, &cr, bg);
    DeleteObject(bg);
    { RECT strip = {0, 0, cr.right, dpx(3)}; HBRUSH a = CreateSolidBrush(C_ACCENT); FillRect(dc, &strip, a); DeleteObject(a); }
    SetBkMode(dc, TRANSPARENT);
    int pad = dpx(26), w = cr.right;
    rd_text(dc, pad, dpx(20), w - pad, dpx(52), L"What's new in v" VER_WSTR, F_HEAD, C_TEXT, DT_LEFT | DT_SINGLELINE);
    rd_text(dc, pad, dpx(54), w - pad, dpx(76), L"Crosshair Overlay updated itself. Here's what changed:", F_SMALL, C_MUTED, DT_LEFT | DT_SINGLELINE);
    RECT card = {pad, dpx(92), w - pad, dpx(92) + g_wnn * dpx(30) + dpx(24)};
    rd_rrect(dc, &card, dpx(12), C_CARD, C_BORDER);
    for (int i = 0; i < g_wnn; i++) {
        int y = card.top + dpx(12) + i * dpx(30);
        HBRUSH a = CreateSolidBrush(C_ACCENT);
        RECT dot = {pad + dpx(16), y + dpx(11), pad + dpx(22), y + dpx(17)};
        FillRect(dc, &dot, a);
        DeleteObject(a);
        rd_text(dc, pad + dpx(32), y, w - pad - dpx(14), y + dpx(28), g_wnlines[i], F_TEXT, C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    rd_rrect(dc, &g_wnok, dpx(16), g_wnhov ? C_ACCHI : C_ACCENT, g_wnhov ? C_ACCHI : C_ACCENT);
    rd_text(dc, g_wnok.left, g_wnok.top, g_wnok.right, g_wnok.bottom, L"Got it", F_BOLD, C_ONACC, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    { HBRUSH b = CreateSolidBrush(C_BORDER); FrameRect(dc, &cr, b); DeleteObject(b); }
    BitBlt(wdc, 0, 0, cr.right, cr.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, obm);
    DeleteObject(bm);
    DeleteDC(dc);
    EndPaint(h, &ps);
}
static LRESULT CALLBACK wn_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: wn_paint(h); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_NCHITTEST: {
        POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(h, &p);
        LRESULT r = DefWindowProcW(h, msg, wp, lp);
        return r == HTCLIENT && !PtInRect(&g_wnok, p) ? HTCAPTION : r;
    }
    case WM_MOUSEMOVE: {
        POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int hv = PtInRect(&g_wnok, p);
        if (hv != g_wnhov) { g_wnhov = hv; InvalidateRect(h, NULL, FALSE); }
        TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, h, 0};
        TrackMouseEvent(&t);
        return 0;
    }
    case WM_MOUSELEAVE: if (g_wnhov) { g_wnhov = 0; InvalidateRect(h, NULL, FALSE); } return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) { SetCursor(LoadCursorW(NULL, (LPCWSTR)(g_wnhov ? IDC_HAND : IDC_ARROW))); return TRUE; }
        break;
    case WM_LBUTTONDOWN: { POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}; if (PtInRect(&g_wnok, p)) DestroyWindow(h); return 0; }
    case WM_KEYDOWN: if (wp == VK_ESCAPE || wp == VK_RETURN) DestroyWindow(h); return 0;
    case WM_DESTROY: g_wndlg = NULL; sync_cursor(); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
static void whatsnew_dialog(void) {
    static int registered;
    if (g_wndlg) return;
    /* this exe's built-in notes */
    g_wnn = 0;
    HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(12), (LPCWSTR)RT_RCDATA);
    HGLOBAL gl = r ? LoadResource(NULL, r) : NULL;
    DWORD sz = r ? SizeofResource(NULL, r) : 0;
    const char *d = gl ? (const char *)LockResource(gl) : NULL;
    if (d && sz) {
        char *t = (char *)malloc(sz + 1), *fmt = (char *)malloc(sz + 64);
        if (t && fmt) {
            memcpy(t, d, sz);
            t[sz] = 0;
            notes_format(t, "", fmt, (int)sz + 64);
            for (char *l = fmt; l && *l && g_wnn < 12;) {
                char *e = strchr(l, '\n');
                if (e) *e = 0;
                MultiByteToWideChar(CP_UTF8, 0, l, -1, g_wnlines[g_wnn], 200);
                g_wnlines[g_wnn][199] = 0;
                g_wnn++;
                l = e ? e + 1 : NULL;
            }
        }
        free(t);
        free(fmt);
    }
    if (!g_wnn) { wcscpy(g_wnlines[0], L"Bug fixes and improvements."); g_wnn = 1; }
    if (!registered) {
        WNDCLASSW wc = {0};
        wc.lpfnWndProc = wn_proc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = L"CrosshairWhatsNew";
        wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
        wc.style = CS_DROPSHADOW;
        registered = RegisterClassW(&wc) != 0;
    }
    int w = dpx(WN_W), hh = wn_height();
    SetRect(&g_wnok, w - dpx(26) - dpx(140), hh - dpx(60), w - dpx(26), hh - dpx(22));
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    g_wndlg = CreateWindowExW(WS_EX_TOPMOST | WS_EX_APPWINDOW, L"CrosshairWhatsNew", L"What's new", WS_POPUP | WS_SYSMENU,
                              wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - hh) / 2, w, hh, NULL, NULL,
                              GetModuleHandleW(NULL), NULL);
    if (!g_wndlg) return;
    ui_dwm(g_wndlg);
    if (g_cursor_hidden) restore_cursor();
    ShowWindow(g_wndlg, SW_SHOWNORMAL);
    SetForegroundWindow(g_wndlg);
}

#ifdef PUBLISHER_APP
/* ================= "Publish a release" window (developer only) ================= */
/* Ctrl+Shift+D in the settings window, or the tray menu on the developer's PC. Picks any
   Crosshair Overlay exe (this one by default), shows its version + built-in notes (editable),
   and publishes it to GitHub (auto-updates) and Discord (release channel). */
#define PD_W 640
#define PD_H 700
enum { PH_NONE = -1, PH_CHOOSE = 1, PH_CLOSE, PH_PUBLISH, PH_TESTERS, PH_NEWS, PH_CHAN0, PH_CHAN1, PH_PING, PH_HISTORY };
static HWND g_pnotes, g_phook, g_ptok, g_powner;
static int g_phov = PH_NONE, g_powner_cap, g_pdone, g_pwarn;
static RECT g_pchoose, g_pclose, g_ppub, g_pnotesbox, g_phookbox, g_ptokbox, g_pexebox, g_ptesters, g_pnews, g_phist, g_pchan[2];
static int g_pchan_sel; /* 0 everyone, 1 testers only */
static void testers_dialog(void);
static void history_dialog(void);
static int g_tkind;
static HWND g_tdlg;
static void set_dpi_aware(void);
static int g_pping = 1; /* ping the release role (remembered in publisher.ini) */
static RECT g_ppingr;
static wchar_t g_pexe[MAX_PATH], g_pstatus[400];
static char g_pver[32];
static HBRUSH g_pcardbr;

static void pd_layout(void) {
    int pad = dpx(26), w = dpx(PD_W);
    SetRect(&g_pexebox, pad, dpx(112), w - pad, dpx(156));
    SetRect(&g_pchoose, w - pad - dpx(150), dpx(118), w - pad - dpx(8), dpx(150));
    SetRect(&g_pnotesbox, pad, dpx(200), w - pad, dpx(312));
    SetRect(&g_pchan[0], pad, dpx(352), pad + dpx(180), dpx(388));
    SetRect(&g_pchan[1], pad + dpx(180), dpx(352), pad + dpx(360), dpx(388));
    SetRect(&g_phookbox, pad, dpx(448), w - pad, dpx(484));
    SetRect(&g_ptokbox, pad, dpx(524), w - pad, dpx(560));
    SetRect(&g_ptesters, pad, dpx(PD_H - 64), pad + dpx(92), dpx(PD_H - 24));
    SetRect(&g_pnews, g_ptesters.right + dpx(8), dpx(PD_H - 64), g_ptesters.right + dpx(8) + dpx(130), dpx(PD_H - 24));
    SetRect(&g_phist, g_pnews.right + dpx(8), dpx(PD_H - 64), g_pnews.right + dpx(8) + dpx(84), dpx(PD_H - 24));
    SetRect(&g_ppingr, pad, dpx(392), pad + dpx(330), dpx(416));
    SetRect(&g_ppub, w - pad - dpx(150), dpx(PD_H - 64), w - pad, dpx(PD_H - 24));
    SetRect(&g_pclose, g_ppub.left - dpx(8) - dpx(86), g_ppub.top, g_ppub.left - dpx(8), g_ppub.bottom);
}
static void pd_load_exe(const wchar_t *exe) {
    char notes[4000];
    wcscpy(g_pexe, exe);
    if (!exe_info(exe, g_pver, sizeof(g_pver), notes, sizeof(notes))) {
        g_pver[0] = 0;
        wcscpy(g_pstatus, L"That file has no version info \x2014 pick a Crosshair Overlay exe.");
        g_pwarn = 1;
    } else { g_pstatus[0] = 0; g_pwarn = 0; }
    /* UTF-8 -> wide, \n -> \r\n for the edit box */
    int n = MultiByteToWideChar(CP_UTF8, 0, notes, -1, NULL, 0);
    wchar_t *w = (wchar_t *)malloc((n + 1) * sizeof(wchar_t)), *w2 = (wchar_t *)malloc((2 * n + 1) * sizeof(wchar_t));
    if (w && w2) {
        MultiByteToWideChar(CP_UTF8, 0, notes, -1, w, n);
        int k = 0;
        for (int i = 0; w[i]; i++) { if (w[i] == L'\n' && (i == 0 || w[i - 1] != L'\r')) w2[k++] = L'\r'; w2[k++] = w[i]; }
        w2[k] = 0;
        while (k > 0 && (w2[k - 1] == L'\n' || w2[k - 1] == L'\r')) w2[--k] = 0;
        SetWindowTextW(g_pnotes, w2);
    }
    free(w);
    free(w2);
    if (g_pdlg) InvalidateRect(g_pdlg, NULL, FALSE);
}
static void pd_paint(HWND h) {
    PAINTSTRUCT ps;
    HDC wdc = BeginPaint(h, &ps);
    RECT cr;
    GetClientRect(h, &cr);
    HDC dc = CreateCompatibleDC(wdc);
    HBITMAP bm = CreateCompatibleBitmap(wdc, cr.right, cr.bottom);
    HGDIOBJ obm = SelectObject(dc, bm);
    HBRUSH bg = CreateSolidBrush(C_BG);
    FillRect(dc, &cr, bg);
    DeleteObject(bg);
    { RECT strip = {0, 0, cr.right, dpx(3)}; HBRUSH a = CreateSolidBrush(C_ACCENT); FillRect(dc, &strip, a); DeleteObject(a); }
    SetBkMode(dc, TRANSPARENT);
    int pad = dpx(26), w = cr.right;
    wchar_t t[400];
    char sec[600];
    rd_text(dc, pad, dpx(20), w - pad, dpx(52), L"Publish a release", F_HEAD, C_TEXT, DT_LEFT | DT_SINGLELINE);
    rd_text(dc, pad, dpx(54), w - pad, dpx(76), L"Posts an exe to GitHub (players' apps update to it) and your Discord release channel.",
            F_SMALL, C_MUTED, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    rd_text(dc, pad, dpx(88), w - pad, dpx(110), L"Exe to publish", F_BOLD, C_TEXT, DT_LEFT | DT_SINGLELINE);
    rd_rrect(dc, &g_pexebox, dpx(12), C_CARD, C_BORDER);
    {
        const wchar_t *nm = wcsrchr(g_pexe, L'\\');
        nm = nm ? nm + 1 : g_pexe;
        if (g_pver[0]) wsprintfW(t, L"%s   \x2022   v%S%s", nm, g_pver, published_before(g_pver, 0) ? L"   \x2022   published" : published_before(g_pver, 1) ? L"   \x2022   with testers" : L"");
        else wsprintfW(t, L"%s", nm);
        rd_text(dc, g_pexebox.left + dpx(14), g_pexebox.top, g_pchoose.left - dpx(8), g_pexebox.bottom, t, F_TEXT, C_TEXT,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS);
    }
    rd_rrect(dc, &g_pchoose, dpx(14), g_phov == PH_CHOOSE ? C_HOVER : C_BG, C_BORDER);
    rd_text(dc, g_pchoose.left, g_pchoose.top, g_pchoose.right, g_pchoose.bottom, L"Choose exe...", F_BOLD, C_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    rd_text(dc, pad, dpx(170), w - pad, dpx(194), L"What's new", F_BOLD, C_TEXT, DT_LEFT | DT_SINGLELINE);
    rd_text(dc, pad, dpx(170), w - pad, dpx(194), L"one change per line \x2014 edit before publishing if you like", F_SMALL, C_MUTED, DT_RIGHT | DT_SINGLELINE);
    rd_rrect(dc, &g_pnotesbox, dpx(12), C_CARD, GetFocus() == g_pnotes ? C_ACCENT : C_BORDER);
    rd_text(dc, pad, dpx(326), w - pad, dpx(348), L"Who gets it", F_BOLD, C_TEXT, DT_LEFT | DT_SINGLELINE);
    {
        RECT all = {g_pchan[0].left, g_pchan[0].top, g_pchan[1].right, g_pchan[1].bottom};
        static const wchar_t *CH[] = {L"Everyone", L"Testers only"};
        rd_rrect(dc, &all, dpx(14), C_BG, C_BORDER);
        for (int i = 0; i < 2; i++) {
            RECT r = g_pchan[i];
            InflateRect(&r, -dpx(3), -dpx(3));
            if (g_pchan_sel == i) rd_rrect(dc, &r, dpx(12), C_ACCENT, C_ACCENT);
            else if (g_phov == PH_CHAN0 + i) rd_rrect(dc, &r, dpx(12), C_HOVER, C_HOVER);
            rd_text(dc, r.left, r.top, r.right, r.bottom, CH[i], g_pchan_sel == i ? F_BOLD : F_TEXT, g_pchan_sel == i ? C_ONACC : C_MUTED,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        rd_text(dc, g_pchan[1].right + dpx(14), g_pchan[0].top, w - pad, g_pchan[0].bottom,
                g_pchan_sel ? L"A GitHub pre-release only your testers' apps install. Not posted to Discord."
                            : L"Players' apps update to it, and it's posted to Discord.",
                F_SMALL, C_MUTED, DT_LEFT | DT_VCENTER | DT_WORDBREAK);
        /* ping switch (Everyone only) */
        int on = g_pping && !g_pchan_sel && RELEASE_PING_ROLE_ID[0], dis = g_pchan_sel || !RELEASE_PING_ROLE_ID[0];
        RECT sw = {pad, dpx(394), pad + dpx(38), dpx(414)};
        rd_rrect(dc, &sw, dpx(20), on ? C_ACCENT : C_TRACK, on ? C_ACCENT : C_TRACK);
        RECT knob = {on ? sw.right - dpx(18) : sw.left + dpx(2), sw.top + dpx(2), on ? sw.right - dpx(2) : sw.left + dpx(18), sw.bottom - dpx(2)};
        rd_rrect(dc, &knob, dpx(16), dis ? C_MUTED : C_WHITE, dis ? C_MUTED : C_WHITE);
        rd_text(dc, sw.right + dpx(10), dpx(392), w - pad, dpx(416),
                !RELEASE_PING_ROLE_ID[0] ? L"Ping: no release role set (RELEASE_PING_ROLE_ID in webhook.h)"
                : g_pchan_sel ? L"Ping the release role  14 not for test builds (no Discord post)"
                : g_pping ? L"Ping the release role in Discord" : L"Don't ping  14 post quietly",
                F_SMALL, dis ? C_DIM : C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    int hook_saved = secret_load(SECRET_HOOK, sec, sizeof(sec));
    SecureZeroMemory(sec, sizeof(sec));
    rd_text(dc, pad, dpx(422), w - pad, dpx(444), L"Discord release webhook", F_BOLD, C_TEXT, DT_LEFT | DT_SINGLELINE);
    rd_text(dc, pad, dpx(422), w - pad, dpx(444), hook_saved ? L"\x2713 saved on this PC (paste to change)" : L"not set \x2014 paste it below",
            F_SMALL, hook_saved ? C_ACCLT : C_WARN, DT_RIGHT | DT_SINGLELINE);
    rd_rrect(dc, &g_phookbox, dpx(12), C_CARD, GetFocus() == g_phook ? C_ACCENT : C_BORDER);
    int tok_saved = secret_load(SECRET_GH, sec, sizeof(sec));
    SecureZeroMemory(sec, sizeof(sec));
    rd_text(dc, pad, dpx(498), w - pad, dpx(520), L"GitHub token (for auto-updates and testers)", F_BOLD, C_TEXT, DT_LEFT | DT_SINGLELINE);
    rd_text(dc, pad, dpx(498), w - pad, dpx(520),
            !updates_set() ? L"off \x2014 set UPDATE_REPO in update.h" : tok_saved ? L"\x2713 saved on this PC (paste to change)" : L"not set \x2014 paste it below",
            F_SMALL, (updates_set() && tok_saved) ? C_ACCLT : C_WARN, DT_RIGHT | DT_SINGLELINE);
    rd_rrect(dc, &g_ptokbox, dpx(12), C_CARD, GetFocus() == g_ptok ? C_ACCENT : C_BORDER);
    if (g_pstatus[0])
        rd_text(dc, pad, dpx(574), w - pad, dpx(626), g_pstatus, F_SMALL, g_pwarn ? C_WARN : g_pdone ? C_ACCLT : C_TEXT, DT_LEFT | DT_WORDBREAK);
    else
        rd_text(dc, pad, dpx(574), w - pad, dpx(626), L"Your webhook and token are saved encrypted for your Windows account only. They're never in the exe, so nobody else can publish.",
                F_SMALL, C_DIM, DT_LEFT | DT_WORDBREAK);
    rd_rrect(dc, &g_ptesters, dpx(16), g_phov == PH_TESTERS ? C_HOVER : C_CARD, C_BORDER);
    rd_text(dc, g_ptesters.left, g_ptesters.top, g_ptesters.right, g_ptesters.bottom, L"Testers", F_BOLD, C_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    rd_rrect(dc, &g_pnews, dpx(16), g_phov == PH_NEWS ? C_HOVER : C_CARD, C_BORDER);
    rd_text(dc, g_pnews.left, g_pnews.top, g_pnews.right, g_pnews.bottom, L"Announcement", F_BOLD, C_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    rd_rrect(dc, &g_phist, dpx(16), g_phov == PH_HISTORY ? C_HOVER : C_CARD, C_BORDER);
    rd_text(dc, g_phist.left, g_phist.top, g_phist.right, g_phist.bottom, L"History", F_BOLD, C_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    rd_rrect(dc, &g_pclose, dpx(16), g_phov == PH_CLOSE ? C_HOVER : C_CARD, C_BORDER);
    rd_text(dc, g_pclose.left, g_pclose.top, g_pclose.right, g_pclose.bottom, L"Close", F_BOLD, C_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    COLORREF pc = g_pub_busy ? C_TRACK : g_phov == PH_PUBLISH ? C_ACCHI : C_ACCENT;
    rd_rrect(dc, &g_ppub, dpx(16), pc, pc);
    rd_text(dc, g_ppub.left, g_ppub.top, g_ppub.right, g_ppub.bottom, g_pub_busy ? L"Publishing..." : L"Publish",
            F_BOLD, g_pub_busy ? C_MUTED : C_ONACC, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    { HBRUSH b = CreateSolidBrush(C_BORDER); FrameRect(dc, &cr, b); DeleteObject(b); }
    BitBlt(wdc, 0, 0, cr.right, cr.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, obm);
    DeleteObject(bm);
    DeleteDC(dc);
    EndPaint(h, &ps);
}
static int pd_hit(int x, int y) {
    POINT p = {x, y};
    if (PtInRect(&g_pchoose, p)) return PH_CHOOSE;
    if (PtInRect(&g_pclose, p)) return PH_CLOSE;
    if (PtInRect(&g_ppub, p)) return PH_PUBLISH;
    if (PtInRect(&g_ptesters, p)) return PH_TESTERS;
    if (PtInRect(&g_pnews, p)) return PH_NEWS;
    if (PtInRect(&g_phist, p)) return PH_HISTORY;
    if (PtInRect(&g_pchan[0], p)) return PH_CHAN0;
    if (PtInRect(&g_pchan[1], p)) return PH_CHAN1;
    if (PtInRect(&g_ppingr, p)) return PH_PING;
    return PH_NONE;
}
static void pd_status(const wchar_t *s, int warn) { wcsncpy(g_pstatus, s, 399); g_pstatus[399] = 0; g_pwarn = warn; g_pdone = 0; InvalidateRect(g_pdlg, NULL, FALSE); }
static void pd_choose(void) {
    wchar_t file[MAX_PATH];
    OPENFILENAMEW of;
    wcscpy(file, g_pexe);
    memset(&of, 0, sizeof(of));
    of.lStructSize = sizeof(of);
    of.hwndOwner = g_pdlg;
    of.lpstrFilter = L"Crosshair Overlay (*.exe)\0*.exe\0";
    of.lpstrFile = file;
    of.nMaxFile = MAX_PATH;
    of.lpstrTitle = L"Choose the Crosshair Overlay exe to publish";
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&of)) pd_load_exe(file);
}
static int starts_with(const char *s, const char *p) { return !strncmp(s, p, strlen(p)); }
static void pd_publish(void) {
    char a[700];
    wchar_t w[700], q[600];
    if (g_pub_busy) return;
    if (!g_pver[0]) { pd_status(L"Pick a Crosshair Overlay exe first.", 1); return; }
    /* save anything newly pasted */
    GetWindowTextW(g_phook, w, 700);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, a, sizeof(a), NULL, NULL);
    { char *t = a; while (*t == ' ') t++; memmove(a, t, strlen(t) + 1); size_t L = strlen(a); while (L && (a[L - 1] == ' ' || a[L - 1] == '/')) a[--L] = 0; }
    if (a[0]) {
        if (!(starts_with(a, "") || starts_with(a, "") ||
              starts_with(a, "") || starts_with(a, ""))) {
            pd_status(L"That isn't a Discord webhook URL (it starts with ", 1);
            return;
        }
        if (!secret_save(SECRET_HOOK, a)) { pd_status(L"Couldn't save the webhook.", 1); return; }
        SetWindowTextW(g_phook, L"");
    }
    SecureZeroMemory(a, sizeof(a)); SecureZeroMemory(w, sizeof(w));
    GetWindowTextW(g_ptok, w, 700);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, a, sizeof(a), NULL, NULL);
    { char *t = a; while (*t == ' ') t++; memmove(a, t, strlen(t) + 1); size_t L = strlen(a); while (L && a[L - 1] == ' ') a[--L] = 0; }
    if (a[0]) {
        if (!(starts_with(a, "github_pat_") || starts_with(a, "ghp_"))) { pd_status(L"That isn't a GitHub token (they start with github_pat_ or ghp_).", 1); return; }
        if (!secret_save(SECRET_GH, a)) { pd_status(L"Couldn't save the token.", 1); return; }
        SetWindowTextW(g_ptok, L"");
    }
    SecureZeroMemory(a, sizeof(a)); SecureZeroMemory(w, sizeof(w));
    if (!g_pchan_sel && !publisher_here()) { pd_status(L"Paste your Discord release webhook first.", 1); return; }
    { /* keep version numbers in order: fixes go +0.0.1, new features +0.1.0 */
        char lastv[32];
        int a = 0, b = 0, c = 0, x = 0, y = 0, z = 0;
        if (last_published(lastv, sizeof(lastv)) && sscanf(lastv, "%d.%d.%d", &a, &b, &c) == 3 && sscanf(g_pver, "%d.%d.%d", &x, &y, &z) == 3) {
            int newer = x > a || (x == a && (y > b || (y == b && z > c)));
            int next = (x == a && y == b && z == c + 1) || (x == a && y == b + 1 && z == 0) || (x == a + 1 && y == 0 && z == 0);
            if (!newer && !(x == a && y == b && z == c)) {
                wsprintfW(q, L"v%S is older than v%S, which you already published.\n\nPlayers' apps only update to newer versions, so nobody would get this one. Publish anyway?", g_pver, lastv);
                if (MessageBoxW(g_pdlg, q, L"Publish a release", MB_YESNO | MB_ICONWARNING) != IDYES) return;
            } else if (newer && !next) {
                wsprintfW(q, L"The last version you published is v%S, so the next one would be:\n\n"
                             L"\x2022 v%d.%d.%d for fixes and small changes\n\x2022 v%d.%d.0 for new features\n\n"
                             L"v%S skips numbers. Publish it anyway?",
                          lastv, a, b, c + 1, a, b + 1, g_pver);
                if (MessageBoxW(g_pdlg, q, L"Publish a release", MB_YESNO | MB_ICONWARNING) != IDYES) return;
            }
        }
    }
    if (published_before(g_pver, g_pchan_sel)) {
        wsprintfW(q, g_pchan_sel ? L"v%S already went to your testers. Send it to them again?" : L"v%S was already published. Publish it again?", g_pver);
        if (MessageBoxW(g_pdlg, q, L"Publish a release", MB_YESNO | MB_ICONWARNING) != IDYES) return;
    }
    {
        char t[300];
        int gh = updates_set() && secret_load(SECRET_GH, t, sizeof(t));
        SecureZeroMemory(t, sizeof(t));
        if (g_pchan_sel && !gh) { pd_status(L"Test builds go through GitHub \x2014 paste your GitHub token first.", 1); return; }
        if (g_pchan_sel)
            wsprintfW(q, L"Send Crosshair Overlay v%S to your testers?\n\n\x2022 GitHub: a pre-release that only testers' apps install\n\x2022 Discord: not posted", g_pver);
        else
            wsprintfW(q, L"Publish Crosshair Overlay v%S to everyone?\n\n\x2022 Discord: posts the notes + exe%s\n\x2022 GitHub: %s", g_pver,
                      RELEASE_PING_ROLE_ID[0] && g_pping ? L" and pings the release role" : L" (no ping)",
                      gh ? L"players' apps update to this version" : L"skipped (auto-updates not set up)");
        if (MessageBoxW(g_pdlg, q, L"Publish a release", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    }
    PubJob *j = (PubJob *)calloc(1, sizeof(PubJob));
    if (!j) return;
    wcscpy(j->exe, g_pexe);
    strcpy(j->ver, g_pver);
    {
        int n = GetWindowTextLengthW(g_pnotes);
        wchar_t *nw = (wchar_t *)malloc((n + 2) * sizeof(wchar_t));
        if (nw) { GetWindowTextW(g_pnotes, nw, n + 1); WideCharToMultiByte(CP_UTF8, 0, nw, -1, j->notes, sizeof(j->notes), NULL, NULL); free(nw); }
        j->notes[sizeof(j->notes) - 1] = 0;
    }
    j->notify = g_pdlg;
    j->chan = g_pchan_sel;
    j->ping = g_pping;
    InterlockedExchange(&g_pub_busy, 1);
    HANDLE th = CreateThread(NULL, 0, publish_thread, j, 0, NULL);
    if (!th) { InterlockedExchange(&g_pub_busy, 0); free(j); pd_status(L"Couldn't start publishing.", 1); return; }
    CloseHandle(th);
    pd_status(L"Publishing...", 0);
}
static void pd_close(void) { if (g_pdlg && !g_pub_busy) DestroyWindow(g_pdlg); }
static LRESULT CALLBACK pd_edit_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref) {
    (void)id; (void)ref;
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) { pd_close(); return 0; }
    if (msg == WM_CHAR && (wp == 1 || wp == 27)) { if (wp == 1) SendMessageW(h, EM_SETSEL, 0, -1); return 0; }
    if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS) InvalidateRect(GetParent(h), NULL, FALSE);
    return DefSubclassProc(h, msg, wp, lp);
}
static LRESULT CALLBACK pd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: pd_paint(h); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
        SetTextColor((HDC)wp, msg == WM_CTLCOLORSTATIC ? C_MUTED : C_TEXT);
        SetBkColor((HDC)wp, C_CARD);
        return (LRESULT)g_pcardbr;
    case WM_PUBLISH_STEP:
        wcscpy(g_pstatus, g_pub_msg);
        g_pwarn = 0;
        g_pdone = wp == 2;
        if (wp == 2 && (wcsstr(g_pstatus, L"didn't") || wcsstr(g_pstatus, L"failed") || wcsstr(g_pstatus, L"Couldn't") || wcsstr(g_pstatus, L"No release"))) { g_pwarn = 1; g_pdone = 0; }
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_NCHITTEST: {
        POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(h, &p);
        LRESULT r = DefWindowProcW(h, msg, wp, lp);
        if (r == HTCLIENT && pd_hit(p.x, p.y) == PH_NONE && p.y < g_pexebox.top) return HTCAPTION;
        return r;
    }
    case WM_MOUSEMOVE: {
        int hv = pd_hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (hv != g_phov) { g_phov = hv; InvalidateRect(h, NULL, FALSE); }
        TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, h, 0};
        TrackMouseEvent(&t);
        return 0;
    }
    case WM_MOUSELEAVE: if (g_phov != PH_NONE) { g_phov = PH_NONE; InvalidateRect(h, NULL, FALSE); } return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            POINT p; GetCursorPos(&p); ScreenToClient(h, &p);
            SetCursor(LoadCursorW(NULL, (LPCWSTR)(pd_hit(p.x, p.y) != PH_NONE ? IDC_HAND : IDC_ARROW)));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN: {
        int k = pd_hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (k == PH_CHOOSE && !g_pub_busy) pd_choose();
        else if (k == PH_PUBLISH) pd_publish();
        else if (k == PH_CLOSE) pd_close();
        else if (k == PH_TESTERS) { if (!g_tdlg) g_tkind = 0; testers_dialog(); }
        else if (k == PH_NEWS) { if (!g_tdlg) g_tkind = 1; testers_dialog(); }
        else if (k == PH_HISTORY) history_dialog();
        else if (k == PH_PING && !g_pub_busy && !g_pchan_sel) {
            wchar_t ip[MAX_PATH];
            g_pping = !g_pping;
            pub_path(L"publisher.ini", ip);
            if (ip[0]) WritePrivateProfileStringW(L"publisher", L"ping", g_pping ? L"1" : L"0", ip);
            InvalidateRect(h, NULL, FALSE);
        }
        else if ((k == PH_CHAN0 || k == PH_CHAN1) && !g_pub_busy) { g_pchan_sel = k - PH_CHAN0; InvalidateRect(h, NULL, FALSE); }
        return 0;
    }
    case WM_KEYDOWN: if (wp == VK_ESCAPE) pd_close(); return 0;
    case WM_CLOSE: pd_close(); return 0;
    case WM_DESTROY:
        if (g_powner && IsWindow(g_powner)) { EnableWindow(g_powner, TRUE); SetForegroundWindow(g_powner); }
        if (g_powner == g_keydlg) g_capture = g_powner_cap;
        g_pdlg = g_pnotes = g_phook = g_ptok = g_powner = NULL;
        if (g_pcardbr) { DeleteObject(g_pcardbr); g_pcardbr = NULL; }
        PostQuitMessage(0); /* the publisher app is just this window */
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
static HWND pd_edit(const RECT *box, DWORD style) {
    HWND e = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, box->left + dpx(12), box->top + dpx(8),
                             box->right - box->left - dpx(20), box->bottom - box->top - dpx(14), g_pdlg, NULL, GetModuleHandleW(NULL), NULL);
    SendMessageW(e, WM_SETFONT, (WPARAM)F_TEXT, TRUE);
    SetWindowSubclass(e, pd_edit_proc, 2, 0);
    return e;
}
static void publish_dialog(void) {
    static int registered;
    if (g_pdlg) { SetForegroundWindow(g_pdlg); return; }
    if (!registered) {
        WNDCLASSW wc = {0};
        wc.lpfnWndProc = pd_proc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = L"CrosshairPublish";
        wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
        wc.style = CS_DROPSHADOW;
        registered = RegisterClassW(&wc) != 0;
    }
    g_phov = PH_NONE; g_pstatus[0] = 0; g_pwarn = g_pdone = 0;
    { wchar_t ip[MAX_PATH]; pub_path(L"publisher.ini", ip); if (ip[0]) g_pping = GetPrivateProfileIntW(L"publisher", L"ping", 1, ip) ? 1 : 0; }
    g_powner = g_keydlg;
    pd_layout();
    int w = dpx(PD_W), hh = dpx(PD_H);
    RECT wa;
    POINT c;
    MONITORINFO mi;
    GetCursorPos(&c);
    mi.cbSize = sizeof(mi);
    SetRect(&wa, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    if (g_powner) GetWindowRect(g_powner, &wa);
    else if (GetMonitorInfoW(MonitorFromPoint(c, MONITOR_DEFAULTTOPRIMARY), &mi)) wa = mi.rcWork;
    g_pdlg = CreateWindowExW(WS_EX_APPWINDOW, L"CrosshairPublish", L"Crosshair Publisher",
                             WS_POPUP | WS_SYSMENU, wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - hh) / 2,
                             w, hh, g_powner, NULL, GetModuleHandleW(NULL), NULL);
    if (!g_pdlg) return;
    g_pcardbr = CreateSolidBrush(C_CARD);
    g_pnotes = pd_edit(&g_pnotesbox, ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN);
    SendMessageW(g_pnotes, EM_SETLIMITTEXT, 3000, 0);
    g_phook = pd_edit(&g_phookbox, ES_AUTOHSCROLL | ES_PASSWORD);
    SendMessageW(g_phook, EM_SETCUEBANNER, TRUE, (LPARAM)L"Paste the release webhook URL to set or change it");
    g_ptok = pd_edit(&g_ptokbox, ES_AUTOHSCROLL | ES_PASSWORD);
    SendMessageW(g_ptok, EM_SETCUEBANNER, TRUE, (LPARAM)L"Paste a GitHub token (github_pat_...) to set or change it");
    if (!updates_set()) EnableWindow(g_ptok, FALSE);
    { /* newest Crosshair Overlay exe next to the publisher (by the version inside it, whatever it's called) */
        wchar_t dir[MAX_PATH], pat[MAX_PATH], best[MAX_PATH] = L"";
        int bv[3] = {-1, 0, 0};
        GetModuleFileNameW(NULL, dir, MAX_PATH);
        wchar_t *sl = wcsrchr(dir, L'\\');
        if (sl) *sl = 0;
        wsprintfW(pat, L"%s\\CrosshairOverlay*.exe", dir);
        WIN32_FIND_DATAW fd;
        HANDLE fh = FindFirstFileW(pat, &fd);
        if (fh != INVALID_HANDLE_VALUE) {
            do {
                wchar_t full[MAX_PATH];
                int v[3];
                if (wcslen(dir) + wcslen(fd.cFileName) + 2 >= MAX_PATH) continue;
                wsprintfW(full, L"%s\\%s", dir, fd.cFileName);
                if (co_exe_version(full, v) && ver_cmp(v, bv) > 0) { memcpy(bv, v, sizeof(v)); wcscpy(best, full); }
            } while (FindNextFileW(fh, &fd));
            FindClose(fh);
        }
        if (best[0]) pd_load_exe(best);
        else { g_pexe[0] = 0; g_pver[0] = 0; wcscpy(g_pstatus, L"Click \"Choose exe...\" and pick the Crosshair Overlay exe to publish."); }
    }
    ui_dwm(g_pdlg);
    if (g_powner) { g_powner_cap = g_capture; g_capture = -2; EnableWindow(g_powner, FALSE); }
    if (g_cursor_hidden) restore_cursor();
    ShowWindow(g_pdlg, SW_SHOWNORMAL);
    SetForegroundWindow(g_pdlg);
}

/* ================= Q/A testers list (publisher) ================= */
/* testers.txt in the GitHub repo: one tester ID per line (+ optional name). Players' apps read it
   to decide whether they get test builds. Only the publisher (with your token) can change it. */
#define TD_W 560
#define TD_H 560
enum { TH_NONE = -1, TH_SAVE = 1, TH_CLOSE };
static HWND g_tedit;
static char g_tsha[80];
static wchar_t g_tstatus[300];
static const wchar_t *TFILE[] = {L"testers.txt", L"news.txt"};
static int g_twarn, g_thov, g_tloaded;
static RECT g_tsave, g_tclose, g_teditbox;

static int gh_auth_header(wchar_t *hdr, int cap, const wchar_t *ctype) {
    char tok[300];
    wchar_t wtok[300];
    if (!secret_load(SECRET_GH, tok, sizeof(tok))) return 0;
    MultiByteToWideChar(CP_UTF8, 0, tok, -1, wtok, 300);
    SecureZeroMemory(tok, sizeof(tok));
    _snwprintf(hdr, cap, L"Authorization: Bearer %s\r\nAccept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n%s", wtok,
               ctype ? ctype : L"");
    hdr[cap - 1] = 0;
    SecureZeroMemory(wtok, sizeof(wtok));
    return 1;
}
/* Load testers.txt. 1 = loaded (or doesn't exist yet), 0 = failed. */
static int testers_load(void) {
    wchar_t url[400], hdr[800];
    char *js = NULL;
    g_tsha[0] = 0;
    if (!gh_auth_header(hdr, 800, NULL)) { wcscpy(g_tstatus, L"Paste your GitHub token in the publisher first (and click Publish once, or it's saved when you publish)."); g_twarn = 1; return 0; }
    wsprintfW(url, L"%s/repos/%s/contents/%s", gh_api(), UPDATE_REPO, TFILE[g_tkind]);
    int st = https_req(L"GET", url, hdr, NULL, 0, &js, NULL, 1024 * 1024);
    SecureZeroMemory(hdr, sizeof(hdr));
    if (st == 404) {
        SetWindowTextW(g_tedit, g_tkind ? L"" : L"# Q/A testers: one tester ID per line, a name after it is optional\r\n");
        wcscpy(g_tstatus, g_tkind ? L"No announcement yet. Write one and click Save." : L"No testers yet. Add their IDs and click Save.");
        g_twarn = 0;
        free(js);
        return 1;
    }
    char *b64 = js ? (char *)malloc(strlen(js) + 1) : NULL;
    if (st != 200 || !b64 || !jstr(jkey(js, NULL, "content"), b64, (int)strlen(js) + 1)) {
        wsprintfW(g_tstatus, st == 401 ? L"GitHub refused the token. Paste a new one in the publisher." : L"Couldn't load the tester list (HTTP %d).", st);
        g_twarn = 1;
        free(js); free(b64);
        return 0;
    }
    jstr(jkey(js, NULL, "sha"), g_tsha, sizeof(g_tsha));
    DWORD n = 0;
    CryptStringToBinaryA(b64, 0, CRYPT_STRING_BASE64, NULL, &n, NULL, NULL);
    BYTE *raw = (BYTE *)malloc(n + 1);
    if (raw && CryptStringToBinaryA(b64, 0, CRYPT_STRING_BASE64, raw, &n, NULL, NULL)) {
        raw[n] = 0;
        int wl = MultiByteToWideChar(CP_UTF8, 0, (char *)raw, -1, NULL, 0);
        wchar_t *w = (wchar_t *)malloc((wl * 2 + 1) * sizeof(wchar_t)), *w2 = (wchar_t *)malloc((wl * 2 + 1) * sizeof(wchar_t));
        if (w && w2) {
            MultiByteToWideChar(CP_UTF8, 0, (char *)raw, -1, w, wl);
            int k = 0;
            for (int i = 0; w[i]; i++) { if (w[i] == L'\n' && (i == 0 || w[i - 1] != L'\r')) w2[k++] = L'\r'; w2[k++] = w[i]; }
            w2[k] = 0;
            SetWindowTextW(g_tedit, w2);
        }
        free(w); free(w2);
    }
    free(raw); free(js); free(b64);
    wcscpy(g_tstatus, L"Loaded from GitHub. Edit the list and click Save.");
    g_twarn = 0;
    return 1;
}
static void testers_save(void) {
    wchar_t url[400], hdr[900];
    if (!g_tloaded) return;
    int n = GetWindowTextLengthW(g_tedit);
    wchar_t *w = (wchar_t *)malloc((n + 2) * sizeof(wchar_t));
    if (!w) return;
    GetWindowTextW(g_tedit, w, n + 1);
    int k = 0; /* \r\n -> \n */
    for (int i = 0; w[i]; i++) if (w[i] != L'\r') w[k++] = w[i];
    w[k] = 0;
    int ul = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *u = (char *)malloc(ul + 2);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, u, ul, NULL, NULL);
    free(w);
    if (!g_tkind) { /* raw tester IDs -> one-way hashes, so the public file never shows a real ID */
        char *o = (char *)malloc(ul * 6 + 64);
        int on = 0, changed = 0;
        if (o) {
            for (char *l = u; *l;) {
                char *e = strchr(l, '\n');
                size_t L = e ? (size_t)(e - l) : strlen(l);
                char *a = l;
                while (a < l + L && (*a == ' ' || *a == '\t')) a++;
                char hh[40];
                char id8[9];
                if (a + 8 <= l + L && is_hex8(a)) { memcpy(id8, a, 8); id8[8] = 0; }
                else id8[0] = 0;
                if (id8[0] && tester_hash(id8, hh)) {
                    on += sprintf(o + on, "%s%.*s", hh, (int)(l + L - (a + 8)), a + 8);
                    changed = 1;
                } else { memcpy(o + on, l, L); on += (int)L; }
                if (e) o[on++] = '\n';
                l = e ? e + 1 : l + L;
            }
            o[on] = 0;
            if (changed) {
                free(u);
                u = o;
                int wl = MultiByteToWideChar(CP_UTF8, 0, u, -1, NULL, 0);
                wchar_t *ww = (wchar_t *)malloc((wl * 2 + 2) * sizeof(wchar_t));
                if (ww) { /* show the hashed list in the editor (\n -> \r\n) */
                    wchar_t *t2 = (wchar_t *)malloc(wl * sizeof(wchar_t));
                    if (t2) {
                        MultiByteToWideChar(CP_UTF8, 0, u, -1, t2, wl);
                        int q = 0;
                        for (int i = 0; t2[i]; i++) { if (t2[i] == L'\n') ww[q++] = L'\r'; ww[q++] = t2[i]; }
                        ww[q] = 0;
                        SetWindowTextW(g_tedit, ww);
                        free(t2);
                    }
                    free(ww);
                }
            } else free(o);
        }
    }
    DWORD bl = 0;
    CryptBinaryToStringA((BYTE *)u, (DWORD)strlen(u), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &bl);
    char *b64 = (char *)malloc(bl + 1), *body = (char *)malloc(bl + 300), *js = NULL;
    if (!b64 || !body || !CryptBinaryToStringA((BYTE *)u, (DWORD)strlen(u), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, b64, &bl)) goto out;
    b64[bl] = 0;
    snprintf(body, bl + 300, g_tkind ? "{\"message\":\"Update announcement\"," "\"content\":\"%s\"%s%s%s}" : "{\"message\":\"Update Q/A testers\",\"content\":\"%s\"%s%s%s}", b64, g_tsha[0] ? ",\"sha\":\"" : "", g_tsha, g_tsha[0] ? "\"" : "");
    if (!gh_auth_header(hdr, 900, L"Content-Type: application/json\r\n")) goto out;
    wsprintfW(url, L"%s/repos/%s/contents/%s", gh_api(), UPDATE_REPO, TFILE[g_tkind]);
    SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_WAIT));
    int st = https_req(L"PUT", url, hdr, body, (DWORD)strlen(body), &js, NULL, 1024 * 1024);
    SecureZeroMemory(hdr, sizeof(hdr));
    if ((st == 200 || st == 201) && js) {
        const char *c = jkey(js, NULL, "content");
        if (c) jstr(jkey(c, NULL, "sha"), g_tsha, sizeof(g_tsha));
        wcscpy(g_tstatus, g_tkind ? L"Saved. Players see it the next time their app opens or checks for updates. Empty it and Save to remove it."
                                  : L"Saved. Testers' apps pick it up within a few hours (or right away from Updates > Check now).");
        g_twarn = 0;
        log_info(g_tkind ? L"publisher: announcement saved" : L"publisher: tester list saved");
    } else {
        wsprintfW(g_tstatus, st == 409 ? L"The list changed on GitHub meanwhile \x2014 close and reopen this window, then try again."
                                       : L"GitHub didn't save it (HTTP %d).", st);
        g_twarn = 1;
    }
out:
    free(u); free(b64); free(body); free(js);
    InvalidateRect(g_tdlg, NULL, FALSE);
}
static void td_paint(HWND h) {
    PAINTSTRUCT ps;
    HDC wdc = BeginPaint(h, &ps);
    RECT cr;
    GetClientRect(h, &cr);
    HDC dc = CreateCompatibleDC(wdc);
    HBITMAP bm = CreateCompatibleBitmap(wdc, cr.right, cr.bottom);
    HGDIOBJ obm = SelectObject(dc, bm);
    HBRUSH bg = CreateSolidBrush(C_BG);
    FillRect(dc, &cr, bg);
    DeleteObject(bg);
    { RECT strip = {0, 0, cr.right, dpx(3)}; HBRUSH a = CreateSolidBrush(C_ACCENT); FillRect(dc, &strip, a); DeleteObject(a); }
    SetBkMode(dc, TRANSPARENT);
    int pad = dpx(26), w = cr.right;
    rd_text(dc, pad, dpx(20), w - pad, dpx(52), g_tkind ? L"Announcement" : L"Q/A testers", F_HEAD, C_TEXT, DT_LEFT | DT_SINGLELINE);
    if (g_tkind) {
    rd_text(dc, pad, dpx(56), w - pad, dpx(96), L"A short message every player sees once as a popup, then as a banner in the settings window until they close it. Keep it under 240 characters.",
            F_SMALL, C_MUTED, DT_LEFT | DT_WORDBREAK);
    rd_text(dc, pad, dpx(100), w - pad, dpx(140), L"Lines starting with # are notes for you and aren't shown. Empty it and click Save to take the announcement down.",
            F_SMALL, C_DIM, DT_LEFT | DT_WORDBREAK);
    } else {
    rd_text(dc, pad, dpx(56), w - pad, dpx(96), L"Testers get builds you publish to \"Testers only\" before everyone else. One tester ID per line, a name after it is optional. IDs are scrambled (one-way) when you save, so the public file can't be copied.",
            F_SMALL, C_MUTED, DT_LEFT | DT_WORDBREAK);
    rd_text(dc, pad, dpx(100), w - pad, dpx(140), L"Testers find their ID by right-clicking the Crosshair Overlay icon near the clock \x2192 \"Copy my tester ID\", then send it to you.",
            F_SMALL, C_DIM, DT_LEFT | DT_WORDBREAK);
    }
    rd_rrect(dc, &g_teditbox, dpx(12), C_CARD, GetFocus() == g_tedit ? C_ACCENT : C_BORDER);
    if (g_tstatus[0]) rd_text(dc, pad, g_teditbox.bottom + dpx(10), w - pad, g_teditbox.bottom + dpx(52), g_tstatus, F_SMALL, g_twarn ? C_WARN : C_ACCLT, DT_LEFT | DT_WORDBREAK);
    rd_rrect(dc, &g_tclose, dpx(16), g_thov == TH_CLOSE ? C_HOVER : C_CARD, C_BORDER);
    rd_text(dc, g_tclose.left, g_tclose.top, g_tclose.right, g_tclose.bottom, L"Close", F_BOLD, C_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    COLORREF sc = !g_tloaded ? C_TRACK : g_thov == TH_SAVE ? C_ACCHI : C_ACCENT;
    rd_rrect(dc, &g_tsave, dpx(16), sc, sc);
    rd_text(dc, g_tsave.left, g_tsave.top, g_tsave.right, g_tsave.bottom, L"Save", F_BOLD, g_tloaded ? C_ONACC : C_MUTED, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    { HBRUSH b = CreateSolidBrush(C_BORDER); FrameRect(dc, &cr, b); DeleteObject(b); }
    BitBlt(wdc, 0, 0, cr.right, cr.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, obm);
    DeleteObject(bm);
    DeleteDC(dc);
    EndPaint(h, &ps);
}
static int td_hit(int x, int y) {
    POINT p = {x, y};
    if (PtInRect(&g_tsave, p)) return TH_SAVE;
    if (PtInRect(&g_tclose, p)) return TH_CLOSE;
    return TH_NONE;
}
static LRESULT CALLBACK td_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: td_paint(h); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp, C_TEXT);
        SetBkColor((HDC)wp, C_CARD);
        return (LRESULT)g_pcardbr;
    case WM_NCHITTEST: {
        POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(h, &p);
        LRESULT r = DefWindowProcW(h, msg, wp, lp);
        return r == HTCLIENT && td_hit(p.x, p.y) == TH_NONE && p.y < g_teditbox.top ? HTCAPTION : r;
    }
    case WM_MOUSEMOVE: {
        int hv = td_hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (hv != g_thov) { g_thov = hv; InvalidateRect(h, NULL, FALSE); }
        TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, h, 0};
        TrackMouseEvent(&t);
        return 0;
    }
    case WM_MOUSELEAVE: if (g_thov != TH_NONE) { g_thov = TH_NONE; InvalidateRect(h, NULL, FALSE); } return 0;
    case WM_LBUTTONDOWN: {
        int k = td_hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (k == TH_SAVE) testers_save();
        else if (k == TH_CLOSE) DestroyWindow(h);
        return 0;
    }
    case WM_KEYDOWN: if (wp == VK_ESCAPE) DestroyWindow(h); return 0;
    case WM_DESTROY:
        if (g_pdlg) { EnableWindow(g_pdlg, TRUE); SetForegroundWindow(g_pdlg); }
        g_tdlg = g_tedit = NULL;
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
static void testers_dialog(void) {
    static int registered;
    if (g_tdlg) { SetForegroundWindow(g_tdlg); return; }
    if (!updates_set()) { pd_status(L"Testers need auto-updates: set UPDATE_REPO in update.h.", 1); return; }
    {   /* a token pasted in the publisher but not saved yet */
        wchar_t w[700];
        char a[700];
        GetWindowTextW(g_ptok, w, 700);
        WideCharToMultiByte(CP_UTF8, 0, w, -1, a, sizeof(a), NULL, NULL);
        if (starts_with(a, "github_pat_") || starts_with(a, "ghp_")) { if (secret_save(SECRET_GH, a)) SetWindowTextW(g_ptok, L""); }
        SecureZeroMemory(a, sizeof(a)); SecureZeroMemory(w, sizeof(w));
    }
    if (!registered) {
        WNDCLASSW wc = {0};
        wc.lpfnWndProc = td_proc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = L"CrosshairTesters";
        wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
        wc.style = CS_DROPSHADOW;
        registered = RegisterClassW(&wc) != 0;
    }
    int w = dpx(TD_W), hh = dpx(TD_H), pad = dpx(26);
    SetRect(&g_teditbox, pad, dpx(150), w - pad, dpx(420));
    SetRect(&g_tsave, w - pad - dpx(150), hh - dpx(64), w - pad, hh - dpx(24));
    SetRect(&g_tclose, g_tsave.left - dpx(12) - dpx(110), g_tsave.top, g_tsave.left - dpx(12), g_tsave.bottom);
    RECT wa;
    GetWindowRect(g_pdlg, &wa);
    g_tdlg = CreateWindowExW(0, L"CrosshairTesters", g_tkind ? L"Announcement" : L"Q/A testers", WS_POPUP | WS_SYSMENU,
                             wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - hh) / 2, w, hh, g_pdlg, NULL,
                             GetModuleHandleW(NULL), NULL);
    if (!g_tdlg) return;
    g_tedit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                              g_teditbox.left + dpx(12), g_teditbox.top + dpx(10), g_teditbox.right - g_teditbox.left - dpx(18),
                              g_teditbox.bottom - g_teditbox.top - dpx(18), g_tdlg, NULL, GetModuleHandleW(NULL), NULL);
    SendMessageW(g_tedit, WM_SETFONT, (WPARAM)F_TEXT, TRUE);
    SendMessageW(g_tedit, EM_SETLIMITTEXT, 60000, 0);
    ui_dwm(g_tdlg);
    EnableWindow(g_pdlg, FALSE);
    ShowWindow(g_tdlg, SW_SHOWNORMAL);
    SetForegroundWindow(g_tdlg);
    UpdateWindow(g_tdlg);
    SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_WAIT));
    g_tloaded = testers_load();
    InvalidateRect(g_tdlg, NULL, FALSE);
    SetFocus(g_tedit);
}

/* ---- History: what's on GitHub, with one-click "release to everyone" for test builds ---- */
#define HD_W 660
#define HD_H 600
#define NHIST 10
typedef struct { char tag[32], date[16], url[300], page[300], body[3000]; int pre, downloads; } HistRow;
static HistRow g_hist[NHIST];
static int g_nhist, g_hhov = -1, g_hloaded;
static wchar_t g_hstatus[300];
static HWND g_hdlg;
static RECT g_hrow[NHIST], g_hbtn[NHIST], g_hclose;
enum { HH_CLOSE = 100, HH_BTN = 200 };
static int history_load(void) {
    char tok[300], *js = NULL;
    wchar_t wtok[300], hdr[800], url[400];
    g_nhist = 0;
    if (!secret_load(SECRET_GH, tok, sizeof(tok))) { wcscpy(g_hstatus, L"Paste your GitHub token in the publisher first."); return 0; }
    MultiByteToWideChar(CP_UTF8, 0, tok, -1, wtok, 300);
    SecureZeroMemory(tok, sizeof(tok));
    wsprintfW(hdr, L"Authorization: Bearer %s\r\nAccept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n", wtok);
    SecureZeroMemory(wtok, sizeof(wtok));
    wsprintfW(url, L"%s/repos/%s/releases?per_page=%d", gh_api(), UPDATE_REPO, NHIST);
    int st = https_req(L"GET", url, hdr, NULL, 0, &js, NULL, 8 * 1024 * 1024);
    SecureZeroMemory(hdr, sizeof(hdr));
    if (st != 200 || !js) { wsprintfW(g_hstatus, L"Couldn't load the releases from GitHub (HTTP %d).", st); free(js); return 0; }
    /* walk the top-level objects of the array */
    int depth = 0, instr = 0;
    const char *start = NULL;
    for (const char *c = js; *c && g_nhist < NHIST; c++) {
        if (instr) { if (*c == '\\' && c[1]) c++; else if (*c == '"') instr = 0; continue; }
        if (*c == '"') { instr = 1; continue; }
        if (*c == '{') { if (++depth == 1) start = c; }
        else if (*c == '}' && --depth == 0 && start) {
            HistRow *r = &g_hist[g_nhist];
            memset(r, 0, sizeof(*r));
            const char *e = c + 1, *v;
            jstr(jkey(start, e, "tag_name"), r->tag, sizeof(r->tag));
            jstr(jkey(start, e, "html_url"), r->page, sizeof(r->page));
            if ((v = jkey(start, e, "published_at")) && *v == '"') { jstr(v, r->date, sizeof(r->date)); r->date[10] = 0; }
            r->pre = (v = jkey(start, e, "prerelease")) && !strncmp(v, "true", 4);
            for (const char *q = start; (q = jkey(q, e, "download_count")); q++) r->downloads += atoi(q);
            for (const char *q = start; (q = jkey(q, e, "browser_download_url")); q++) {
                char u[300];
                if (jstr(q, u, sizeof(u)) && strstr(u, "CrosshairOverlay.exe")) { strcpy(r->url, u); break; }
            }
            jstr(jkey(start, e, "body"), r->body, sizeof(r->body));
            if (r->tag[0]) g_nhist++;
            start = NULL;
        }
    }
    free(js);
    wsprintfW(g_hstatus, g_nhist ? L"Newest first. Test builds can go to everyone with one click." : L"Nothing published on GitHub yet.");
    return 1;
}
/* turn a test build into a normal release: download its exe from GitHub and publish it to everyone (same as Publish) */
static void history_promote(int i) {
    HistRow *r = &g_hist[i];
    wchar_t q[400], tmp[MAX_PATH], url[400];
    if (InterlockedCompareExchange(&g_pub_busy, 0, 0)) { wcscpy(g_hstatus, L"Already publishing \x2014 wait for it to finish."); InvalidateRect(g_hdlg, NULL, FALSE); return; }
    wsprintfW(q, L"Release the test build %S to everyone?\n\n\x2022 GitHub: it becomes the normal release, every player's app updates to it\n\x2022 Discord: posts its notes + exe%s",
              r->tag, RELEASE_PING_ROLE_ID[0] && g_pping ? L" and pings the release role" : L" (no ping)");
    if (
#ifdef CH_TEST
        !GetEnvironmentVariableW(L"CH_HIST_YES", NULL, 0) &&
#endif
        MessageBoxW(g_hdlg, q, L"Release to everyone", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    if (!r->url[0]) { wcscpy(g_hstatus, L"That release has no CrosshairOverlay.exe to post."); InvalidateRect(g_hdlg, NULL, FALSE); return; }
    SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_WAIT));
    wcscpy(g_hstatus, L"Downloading the test build from GitHub...");
    InvalidateRect(g_hdlg, NULL, FALSE);
    UpdateWindow(g_hdlg);
    char *exe = NULL;
    DWORD el = 0;
    MultiByteToWideChar(CP_UTF8, 0, r->url, -1, url, 400);
    int st = https_req(L"GET", url, L"Accept: application/octet-stream\r\n", NULL, 0, &exe, &el, 64 * 1024 * 1024);
    if (st != 200 || !exe || !is_exe_file((BYTE *)exe, el)) { wsprintfW(g_hstatus, L"Couldn't download the exe (HTTP %d).", st); free(exe); InvalidateRect(g_hdlg, NULL, FALSE); return; }
    GetTempPathW(MAX_PATH, tmp);
    wsprintfW(tmp + lstrlenW(tmp), L"CrosshairOverlay-%S.exe", r->tag);
    HANDLE f = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD w = 0;
    int ok = f != INVALID_HANDLE_VALUE && WriteFile(f, exe, el, &w, NULL) && w == el;
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    free(exe);
    if (!ok) { wcscpy(g_hstatus, L"Couldn't save the downloaded exe."); InvalidateRect(g_hdlg, NULL, FALSE); return; }
    PubJob *j = (PubJob *)calloc(1, sizeof(PubJob));
    if (!j) return;
    wcscpy(j->exe, tmp);
    lstrcpynA(j->ver, r->tag[0] == 'v' ? r->tag + 1 : r->tag, sizeof(j->ver));
    { /* its notes: the release text before the Download / Read more lines, without the "- " bullets */
        char *cut = strstr(r->body, "\n\n**Download:**");
        if (cut) *cut = 0;
        int n = 0;
        for (char *l = r->body; *l && n < (int)sizeof(j->notes) - 4;) {
            char *e = strchr(l, '\n');
            size_t L = e ? (size_t)(e - l) : strlen(l);
            if (L >= 2 && l[0] == '-' && l[1] == ' ') { l += 2; L -= 2; }
            while (L && l[L - 1] == '\r') L--;
            if (L && strncmp(l, "Bug fixes and improvements.", L)) {
                if (L > sizeof(j->notes) - 4 - n) L = sizeof(j->notes) - 4 - n;
                memcpy(j->notes + n, l, L); n += (int)L;
                j->notes[n++] = '\r'; j->notes[n++] = '\n';
            }
            if (!e) break;
            l = e + 1;
        }
        j->notes[n] = 0;
    }
    j->notify = g_pdlg;
    j->chan = 0;
    j->ping = g_pping;
    InterlockedExchange(&g_pub_busy, 1);
    HANDLE th = CreateThread(NULL, 0, publish_thread, j, 0, NULL);
    if (!th) { InterlockedExchange(&g_pub_busy, 0); free(j); return; }
    CloseHandle(th);
    log_info(L"publisher: releasing test build %S to everyone", r->tag);
    pd_status(L"Releasing the test build to everyone...", 0);
    DestroyWindow(g_hdlg);
}
static void hd_paint(HWND h) {
    PAINTSTRUCT ps;
    HDC wdc = BeginPaint(h, &ps);
    RECT cr;
    GetClientRect(h, &cr);
    HDC dc = CreateCompatibleDC(wdc);
    HBITMAP bm = CreateCompatibleBitmap(wdc, cr.right, cr.bottom);
    HGDIOBJ obm = SelectObject(dc, bm);
    HBRUSH bg = CreateSolidBrush(C_BG);
    FillRect(dc, &cr, bg);
    DeleteObject(bg);
    { RECT strip = {0, 0, cr.right, dpx(3)}; HBRUSH a = CreateSolidBrush(C_ACCENT); FillRect(dc, &strip, a); DeleteObject(a); }
    SetBkMode(dc, TRANSPARENT);
    int pad = dpx(26), w = cr.right;
    rd_text(dc, pad, dpx(20), w - pad, dpx(52), L"Release history", F_HEAD, C_TEXT, DT_LEFT | DT_SINGLELINE);
    rd_text(dc, pad, dpx(54), w - pad, dpx(76), g_hstatus, F_SMALL, C_MUTED, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    for (int i = 0; i < g_nhist; i++) {
        HistRow *r = &g_hist[i];
        RECT *rr = &g_hrow[i];
        rd_rrect(dc, rr, dpx(12), g_hhov == i ? C_HOVER : C_CARD, C_BORDER);
        wchar_t t[80], d[120];
        wsprintfW(t, L"%S", r->tag);
        rd_text(dc, rr->left + dpx(14), rr->top, rr->left + dpx(110), rr->bottom, t, F_BOLD, C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT bd = {rr->left + dpx(112), rr->top + dpx(10), rr->left + dpx(112) + dpx(104), rr->bottom - dpx(10)};
        rd_rrect(dc, &bd, dpx(10), r->pre ? C_WARN : C_ACCENT, r->pre ? C_WARN : C_ACCENT);
        rd_text(dc, bd.left, bd.top, bd.right, bd.bottom, r->pre ? L"Testers only" : L"Everyone", F_SMALL, C_ONACC, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        wsprintfW(d, L"%S  \x2022  %d download%s", r->date, r->downloads, r->downloads == 1 ? L"" : L"s");
        rd_text(dc, bd.right + dpx(14), rr->top, rr->right - dpx(190), rr->bottom, d, F_SMALL, C_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        RECT *b = &g_hbtn[i];
        int hb = g_hhov == HH_BTN + i;
        if (r->pre) { rd_rrect(dc, b, dpx(14), hb ? C_ACCHI : C_ACCENT, hb ? C_ACCHI : C_ACCENT); rd_text(dc, b->left, b->top, b->right, b->bottom, L"Release to everyone", F_SMALL, C_ONACC, DT_CENTER | DT_VCENTER | DT_SINGLELINE); }
        else { rd_rrect(dc, b, dpx(14), hb ? C_HOVER : C_CARD, C_BORDER); rd_text(dc, b->left, b->top, b->right, b->bottom, L"Open on GitHub", F_SMALL, C_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE); }
    }
    rd_rrect(dc, &g_hclose, dpx(16), g_hhov == HH_CLOSE ? C_HOVER : C_CARD, C_BORDER);
    rd_text(dc, g_hclose.left, g_hclose.top, g_hclose.right, g_hclose.bottom, L"Close", F_BOLD, C_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    { HBRUSH b = CreateSolidBrush(C_BORDER); FrameRect(dc, &cr, b); DeleteObject(b); }
    BitBlt(wdc, 0, 0, cr.right, cr.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, obm);
    DeleteObject(bm);
    DeleteDC(dc);
    EndPaint(h, &ps);
}
static int hd_hit(int x, int y) {
    POINT p = {x, y};
    if (PtInRect(&g_hclose, p)) return HH_CLOSE;
    for (int i = 0; i < g_nhist; i++) {
        if (PtInRect(&g_hbtn[i], p)) return HH_BTN + i;
        if (PtInRect(&g_hrow[i], p)) return i;
    }
    return -1;
}
static LRESULT CALLBACK hd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: hd_paint(h); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_NCHITTEST: {
        POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(h, &p);
        LRESULT r = DefWindowProcW(h, msg, wp, lp);
        return r == HTCLIENT && hd_hit(p.x, p.y) < 0 ? HTCAPTION : r;
    }
    case WM_MOUSEMOVE: {
        int hv = hd_hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (hv != g_hhov) { g_hhov = hv; InvalidateRect(h, NULL, FALSE); }
        TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, h, 0};
        TrackMouseEvent(&t);
        return 0;
    }
    case WM_MOUSELEAVE: if (g_hhov != -1) { g_hhov = -1; InvalidateRect(h, NULL, FALSE); } return 0;
    case WM_LBUTTONDOWN: {
        int k = hd_hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (k == HH_CLOSE) DestroyWindow(h);
        else if (k >= HH_BTN && k < HH_BTN + g_nhist) {
            HistRow *r = &g_hist[k - HH_BTN];
            if (r->pre) history_promote(k - HH_BTN);
            else if (r->page[0]) { wchar_t u[300]; MultiByteToWideChar(CP_UTF8, 0, r->page, -1, u, 300); ShellExecuteW(NULL, L"open", u, NULL, NULL, SW_SHOWNORMAL); }
        }
        return 0;
    }
    case WM_KEYDOWN: if (wp == VK_ESCAPE) DestroyWindow(h); return 0;
    case WM_DESTROY:
        if (g_pdlg) { EnableWindow(g_pdlg, TRUE); SetForegroundWindow(g_pdlg); }
        g_hdlg = NULL;
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
static void history_dialog(void) {
    static int registered;
    if (g_hdlg) { SetForegroundWindow(g_hdlg); return; }
    if (!updates_set()) { pd_status(L"History needs auto-updates: set UPDATE_REPO in update.h.", 1); return; }
    {   /* a token pasted in the publisher but not saved yet */
        wchar_t w[700];
        char a[700];
        GetWindowTextW(g_ptok, w, 700);
        WideCharToMultiByte(CP_UTF8, 0, w, -1, a, sizeof(a), NULL, NULL);
        if (starts_with(a, "github_pat_") || starts_with(a, "ghp_")) { if (secret_save(SECRET_GH, a)) SetWindowTextW(g_ptok, L""); }
        SecureZeroMemory(a, sizeof(a)); SecureZeroMemory(w, sizeof(w));
    }
    if (!registered) {
        WNDCLASSW wc = {0};
        wc.lpfnWndProc = hd_proc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = L"CrosshairHistory";
        wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
        wc.style = CS_DROPSHADOW;
        registered = RegisterClassW(&wc) != 0;
    }
    int w = dpx(HD_W), hh = dpx(HD_H), pad = dpx(26), rh = dpx(42), gap = dpx(6);
    for (int i = 0; i < NHIST; i++) {
        SetRect(&g_hrow[i], pad, dpx(88) + i * (rh + gap), w - pad, dpx(88) + i * (rh + gap) + rh);
        SetRect(&g_hbtn[i], w - pad - dpx(176), g_hrow[i].top + dpx(7), w - pad - dpx(8), g_hrow[i].bottom - dpx(7));
    }
    SetRect(&g_hclose, w - pad - dpx(120), hh - dpx(56), w - pad, hh - dpx(18));
    RECT wa;
    GetWindowRect(g_pdlg, &wa);
    g_hdlg = CreateWindowExW(0, L"CrosshairHistory", L"Release history", WS_POPUP | WS_SYSMENU,
                             wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - hh) / 2, w, hh, g_pdlg, NULL,
                             GetModuleHandleW(NULL), NULL);
    if (!g_hdlg) return;
    wcscpy(g_hstatus, L"Loading from GitHub...");
    g_nhist = 0;
    ui_dwm(g_hdlg);
    EnableWindow(g_pdlg, FALSE);
    ShowWindow(g_hdlg, SW_SHOWNORMAL);
    SetForegroundWindow(g_hdlg);
    UpdateWindow(g_hdlg);
    SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_WAIT));
    g_hloaded = history_load();
    InvalidateRect(g_hdlg, NULL, FALSE);
}

/* The publisher app: just the Publish window. */
static int publisher_main(void) {
    set_dpi_aware();
    settings_path();
    log_paths();
    load_settings();
    theme_set(S.theme);
    make_fonts();
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
#ifdef CH_TEST
    {
        wchar_t tv[600];
        char a8[600];
        if (GetEnvironmentVariableW(L"CH_PUB_HOOK", tv, 600)) { WideCharToMultiByte(CP_UTF8, 0, tv, -1, a8, 600, NULL, NULL); secret_save(SECRET_HOOK, a8); }
        if (GetEnvironmentVariableW(L"CH_PUB_TOKEN", tv, 600)) { WideCharToMultiByte(CP_UTF8, 0, tv, -1, a8, 600, NULL, NULL); secret_save(SECRET_GH, a8); }
        if (GetEnvironmentVariableW(L"CH_PUBLISH", tv, 600)) {
            PubJob *j = (PubJob *)calloc(1, sizeof(PubJob));
            wchar_t c[8] = L"0";
            wcscpy(j->exe, tv);
            exe_info(tv, j->ver, sizeof(j->ver), j->notes, sizeof(j->notes));
            GetEnvironmentVariableW(L"CH_PUB_CHAN", c, 8); j->chan = _wtoi(c);
            wcscpy(c, L"1"); GetEnvironmentVariableW(L"CH_PUB_PING", c, 8); j->ping = _wtoi(c);
            g_pub_busy = 1;
            publish_thread(j);
            log_info(L"test publish result: %s", g_pub_msg);
            return 0;
        }
        if (GetEnvironmentVariableW(L"CH_HISTORY", tv, 600)) { /* open History; CH_HISTORY=promote releases the newest test build */
            publish_dialog();
            history_dialog();
            log_info(L"test history: %d releases, %s", g_nhist, g_hstatus);
            for (int i = 0; i < g_nhist; i++) log_info(L"test history: %S pre=%d date=%S dl=%d", g_hist[i].tag, g_hist[i].pre, g_hist[i].date, g_hist[i].downloads);
            if (!wcscmp(tv, L"promote")) for (int i = 0; i < g_nhist; i++) if (g_hist[i].pre) { history_promote(i); break; }
            MSG m;
            DWORD t0 = GetTickCount();
            while (GetTickCount() - t0 < 20000 && InterlockedCompareExchange(&g_pub_busy, 0, 0) == 0 && PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
            while (GetTickCount() - t0 < 30000 && InterlockedCompareExchange(&g_pub_busy, 0, 0)) { while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } Sleep(50); }
            log_info(L"test history done: %s", g_pstatus);
            return 0;
        }
        if (GetEnvironmentVariableW(L"CH_TESTERS", tv, 600)) {
            publish_dialog();
            { wchar_t k[4] = L"0"; GetEnvironmentVariableW(L"CH_TKIND", k, 4); g_tkind = _wtoi(k); }
            testers_dialog();
            for (wchar_t *q = tv; *q; q++) if (*q == L'|') *q = L'\n';
            if (g_tedit) SetWindowTextW(g_tedit, tv);
            testers_save();
            log_info(L"test testers save: %s", g_tstatus);
            return 0;
        }
    }
#endif
    publish_dialog();
    if (!g_pdlg) return 1;
    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}
#endif /* PUBLISHER_APP */

/* ================= settings window ================= */
static void card_toggle(void);
static void card_hide(void);

#define UI_W 900
#define UI_H 726
#define SIDE_W 214
#define UI_HEAD 50 /* header bar */
#define UI_TABS 40 /* tab bar under it */
enum { TAB_CROSSHAIR, TAB_COLOR, TAB_LOOK, TAB_FX, TAB_THEME, TAB_PROFILES, TAB_KEYS, TAB_PERF, TAB_GENERAL, TAB_UPDATES, NTABS };
static const wchar_t *TAB_NAMES[NTABS] = {L"Crosshair", L"Color", L"Size & Look", L"Effects", L"Themes", L"Profiles",
                                          L"Keybinds", L"FPS Boost", L"General", L"Updates"};
static const wchar_t *TAB_SHORT[NTABS] = {L"Crosshair", L"Color", L"Look", L"Effects", L"Themes", L"Profiles", L"Keybinds", L"FPS Boost", L"General", L"Updates"};
static const wchar_t *TAB_SUBS[NTABS] = {
    L"Pick the shape of your crosshair", L"Pick a color, or make your own",
    L"Size, thickness, opacity and the cover patch", L"Glow, rainbow, pulse, click reaction and recordings",
    L"Change how this panel and the loading screen look",
    L"Save up to three setups and switch in one click",
    L"Optional shortcuts \x2014 everything also works by clicking",
    L"Unlock Roblox's frame rate and squeeze out more FPS", L"How the overlay behaves",
    L"Your version, updates and everything that changed"};
enum { H_NONE, H_CLOSE, H_TAB, H_STYLE, H_COLOR, H_PICK, H_SLIDER, H_TOGGLE, H_COVER, H_COVERCOL,
       H_KEY, H_KEYCLEAR, H_KRESET, H_MODE, H_BUTTON, H_KFKEY, H_DONE, H_FXSEG, H_SPEED, H_PREVIEW,
       H_PLOAD, H_PSAVE, H_THEME, H_THPICK, H_SPLASHPREV, H_CRIMINTRO, H_MUSIC, H_FPS, H_BOOST, H_PERFBTN, H_SIZE, H_ONLYRBX, H_NEWSX, H_RCODE, H_RCODEX, H_SEARCH, H_SEARCHRES, H_TOURNEXT, H_TOURSKIP, H_TOURBLOCK, H_NUDGE, H_LAYERSEL };
enum { SL_SIZE, SL_THICK, SL_OPAC };
enum { T_OUTLINE, T_HIDECUR, T_SPLASH, T_SHOW, T_OPENSTART, T_GLOW, T_CLICK, T_GPU, T_REPORTS, T_AUTOUPD, T_NOCAP, T_AUTOSTART, T_STATS, T_TBUILDS, T_AUTOCON, T_MENUHIDE };
enum { B_SHOWKEYS, B_RBX, B_EXIT, B_REPORT, B_UPDATE, B_UNINSTALL, B_COPYCODE, B_PASTECODE, B_TESTOK, B_TESTBAD, B_DEBUG, B_EXPORT, B_IMPORT };

typedef struct { RECT r; int kind, val; } Hit;
static Hit g_hits[220];
static int g_nhits;
static int g_hk = H_NONE, g_hv = -1; /* what the mouse is over */
static int g_drag = -1;              /* slider being dragged */
static int g_edit_l2; /* Crosshair / Color / Look tabs are editing the second layer */
static int g_tab, g_kscroll, g_kscroll_max, g_tracking, g_uscroll, g_uscroll_max;

/* ---- hover tips: rows drawn this frame, and the one the mouse rests on ---- */
typedef struct { RECT r; wchar_t title[64]; } TipRow;
static TipRow g_rows[48];
static int g_nrows, g_tip_show;
static wchar_t g_tip_hover[64];
static POINT g_tip_pt;
static void tip_row(int x0, int y0, int x1, int y1, const wchar_t *title) {
    if (g_nrows >= 48 || !title) return;
    SetRect(&g_rows[g_nrows].r, x0, y0, x1, y1);
    lstrcpynW(g_rows[g_nrows].title, title, 64);
    g_nrows++;
}
typedef struct { const wchar_t *title, *tip; } Tip;
static const Tip TIPS[] = {
    {L"Size", L"How big the crosshair is, in real pixels on your screen. The preview on the left is real size."},
    {L"Thickness", L"How thick the lines and dots are. 2\x2013" L"3 is easiest to see without blocking your view."},
    {L"Opacity", L"Lower it to see through the crosshair. 100% is fully solid."},
    {L"Outline", L"A thin dark edge around the crosshair so it stays visible on bright and busy backgrounds."},
    {L"Cover game crosshair", L"Draws a small solid patch under yours, so Roblox's own gun crosshair doesn't show through."},
    {L"Glow", L"A soft neon halo in the crosshair's color. Looks great on dark maps."},
    {L"Color effect", L"Rainbow slowly cycles through colors. Pulse makes the crosshair fade in and out."},
    {L"Effect speed", L"How fast Rainbow and Pulse move."},
    {L"Click reaction", L"The crosshair kicks out a little each time you click, like recoil."},
    {L"Hide from recordings", L"OBS, Discord streams, SteelSeries / Xbox clips and screenshots won't capture the crosshair. You still see it. Needs Windows 10 (2004) or newer."},
    {L"Crosshair position", L"Follow mouse puts it where your cursor is (best for shift-lock and first person). Center pins it to the middle of the screen."},
    {L"Show crosshair", L"Turn the crosshair on or off. \"In Roblox only\" hides it whenever another window is in front or Roblox is minimised."},
    {L"Hide mouse cursor", L"Hides the Windows pointer and Roblox's own gun crosshair while you play, so you only see yours. It comes back when you open this panel."},
    {L"Start with Windows", L"Starts the app quietly when you sign in to Windows: no intro and no panel, just your crosshair."},
    {L"Open this panel on startup", L"When off, the app starts straight into the game with your crosshair. Open the panel any time from the tray icon by the clock."},
    {L"Bug reports", L"If the app or Roblox crashes, a report with the app's log goes to simplysoul so it can be fixed. No personal files are included. To report something yourself, press F1."},
    {L"Share anonymous usage stats", L"One short line a week: version, Windows, screen size, theme and settings. No name, no files, nothing you type."},
    {L"Use high-performance graphics card", L"On laptops with two graphics chips, makes Windows run Roblox on the faster one."},
    {L"Roblox FPS cap", L"Roblox normally stops at 60 FPS. Pick your monitor's refresh rate (or higher) for smoother gameplay."},
    {L"Performance boost", L"Lowers graphics settings Roblox allows you to change, for more FPS. Off keeps Roblox's normal graphics."},
    {L"Share your crosshair", L"Copy gives you a short code for your exact crosshair. Send it to a friend; they click Paste code to get it."},
    {L"Custom colors", L"Pick your own accent and background colors for the Custom theme."},
    {L"Loading screen", L"The short intro that plays when the app starts. It matches your theme."},
    {L"Loading music", L"Music that plays with the loading screen. Pick a file or turn it off."},
    {L"Fine-tune position", L"Leave it at 0 / 0 for the normal position. If a game's aim point sits a little off, + X moves the crosshair right and + Y moves it down."},
    {L"Auto-contrast", L"Watches the spot behind your crosshair. If your color would blend in (red on red walls, white on snow), it turns white or black until it's visible again. Doesn't work with Rainbow."},
    {L"Hide in Roblox menus & chat", L"Hides the crosshair while Roblox's Esc menu is open or while you type in chat (after pressing /). It comes back when you close them."},
    {L"Q/A tester", L"You get test builds before everyone else. Tell simplysoul how they work with Looks good or Problem."},
};
static const wchar_t *tip_for(const wchar_t *title) {
    for (int i = 0; i < (int)(sizeof(TIPS) / sizeof(TIPS[0])); i++) if (!wcscmp(TIPS[i].title, title)) return TIPS[i].tip;
    return NULL;
}

/* ---- search ---- */
typedef struct { int tab; const wchar_t *title, *keys; } SearchItem;
static const SearchItem SEARCH[] = {
    {TAB_CROSSHAIR, L"Crosshair style", L"shape dot cross circle plus gap scope sniper pick"},
    {TAB_COLOR, L"Crosshair color", L"colour red green blue custom rgb hex picker"},
    {TAB_LOOK, L"Size", L"bigger smaller scale"},
    {TAB_LOOK, L"Thickness", L"thick thin width lines"},
    {TAB_LOOK, L"Opacity", L"transparent see through alpha fade"},
    {TAB_LOOK, L"Outline", L"border edge black visible"},
    {TAB_LOOK, L"Cover game crosshair", L"patch hide roblox crosshair background"},
    {TAB_LOOK, L"Fine-tune position", L"offset nudge center centered align move pixel off"},
    {TAB_FX, L"Glow", L"neon halo bloom shine"},
    {TAB_FX, L"Color effect", L"rainbow pulse animation rgb"},
    {TAB_FX, L"Effect speed", L"slow fast animation"},
    {TAB_FX, L"Click reaction", L"click shoot fire recoil kick"},
    {TAB_FX, L"Hide in Roblox menus & chat", L"esc menu pause chat typing hide"},
    {TAB_COLOR, L"Auto-contrast", L"visible blend contrast background auto color"},
    {TAB_CROSSHAIR, L"Layer 2", L"layer second stack combine two crosshairs dot circle"},
    {TAB_FX, L"Hide from recordings", L"obs stream record capture screenshot steelseries clip discord"},
    {TAB_THEME, L"Themes", L"theme skin dark light background art colors look"},
    {TAB_THEME, L"Custom colors", L"custom theme accent background"},
    {TAB_THEME, L"Loading screen", L"intro splash startup animation"},
    {TAB_THEME, L"Loading music", L"music song sound audio intro"},
    {TAB_PROFILES, L"Profiles", L"save load slot preset"},
    {TAB_PROFILES, L"Share your crosshair", L"code copy paste friend share"},
    {TAB_PROFILES, L"Recent codes", L"friends codes history pasted"},
    {TAB_PROFILES, L"Export / import settings", L"backup restore save file another pc transfer move"},
    {TAB_UPDATES, L"Copy debug info", L"debug info version discord help support"},
    {TAB_KEYS, L"Keybinds", L"hotkey shortcut key bind keyboard"},
    {TAB_PERF, L"Roblox FPS cap", L"fps unlocker unlock frame rate 60 144 240 hz"},
    {TAB_PERF, L"Performance boost", L"lag boost graphics low settings smooth"},
    {TAB_PERF, L"Use high-performance graphics card", L"gpu nvidia amd laptop dedicated"},
    {TAB_PERF, L"Undo all FPS changes", L"white screen reset fix roblox"},
    {TAB_GENERAL, L"Crosshair position", L"center middle follow mouse cursor"},
    {TAB_GENERAL, L"Show crosshair", L"hide toggle on off roblox only focus"},
    {TAB_GENERAL, L"Hide mouse cursor", L"pointer cursor arrow"},
    {TAB_GENERAL, L"Start with Windows", L"startup boot autostart login"},
    {TAB_GENERAL, L"Open this panel on startup", L"panel settings open launch"},
    {TAB_GENERAL, L"Bug reports", L"report problem crash bug discord"},
    {TAB_GENERAL, L"Share anonymous usage stats", L"stats privacy telemetry"},
    {TAB_GENERAL, L"Exit app", L"quit close exit"},
    {TAB_UPDATES, L"Check for updates", L"update version new download auto install"},
    {TAB_UPDATES, L"What's changed", L"changelog notes history patch"},
    {TAB_UPDATES, L"Remove everything", L"uninstall delete remove clean"},
    {TAB_UPDATES, L"Q/A tester", L"tester test builds beta feedback"},
    {-1, L"Panel size", L"bigger smaller window scale zoom ui"},
    {-2, L"Report a problem", L"bug issue broken help crash not working feedback"},
};
#define NSEARCH ((int)(sizeof(SEARCH) / sizeof(SEARCH[0])))
static wchar_t g_search[40];
static int g_search_on, g_search_sel, g_sres[7], g_nsres;
static wchar_t g_flash_title[64]; /* the setting search took you to: it glows for a moment */
static DWORD g_flash_t;
static int icontains(const wchar_t *hay, const wchar_t *q) {
    int n = lstrlenW(q);
    for (; *hay; hay++) {
        int i = 0;
        while (i < n && hay[i] && towlower(hay[i]) == towlower(q[i])) i++;
        if (i == n) return 1;
    }
    return 0;
}
static void search_update(void) {
    int sc[NSEARCH];
    g_nsres = 0;
    g_search_sel = 0;
    if (!g_search[0]) return;
    for (int i = 0; i < NSEARCH; i++) {
        const wchar_t *tn = SEARCH[i].tab >= 0 ? TAB_NAMES[SEARCH[i].tab] : L"";
        sc[i] = !_wcsnicmp(SEARCH[i].title, g_search, lstrlenW(g_search)) ? 4 : icontains(SEARCH[i].title, g_search) ? 3
              : icontains(SEARCH[i].keys, g_search) ? 2 : icontains(tn, g_search) ? 1 : 0;
    }
    for (int want = 4; want >= 1; want--)
        for (int i = 0; i < NSEARCH && g_nsres < 7; i++) if (sc[i] == want) g_sres[g_nsres++] = i;
}

/* ---- first-run tour ---- */
static int g_tour;
static wchar_t g_ui_status[300];
static RECT g_slider_track[3];
static int g_ui_alpha;

static void ui_redraw(void) { if (g_keydlg) InvalidateRect(g_keydlg, NULL, FALSE); }
static void ui_set_status(const wchar_t *s) {
    wcsncpy(g_ui_status, s, 299);
    g_ui_status[299] = 0;
    ui_redraw();
}
static void hit(int x0, int y0, int x1, int y1, int kind, int val) {
    RECT r = {x0, y0, x1, y1};
    if (g_nhits >= 220 || !IntersectRect(&r, &r, &g_clip)) return;
    g_hits[g_nhits].r = r;
    g_hits[g_nhits].kind = kind;
    g_hits[g_nhits].val = val;
    g_nhits++;
}
static int hov(int kind, int val) { return g_hk == kind && g_hv == val; }
static const Hit *hit_at(int x, int y) {
    POINT p = {x, y};
    for (int i = g_nhits - 1; i >= 0; i--)
        if (PtInRect(&g_hits[i].r, p)) return &g_hits[i];
    return NULL;
}

/* ---- keybind capture ---- */
static void kb_check(void) { /* mark keys another program already owns */
    for (int i = 0; i < NACT; i++) {
        g_bind_fail[i] = 0;
        if (!KB[i].vk) continue;
        if (RegisterHotKey(g_keydlg, 900, KB[i].mods | MOD_NOREPEAT, KB[i].vk))
            UnregisterHotKey(g_keydlg, 900);
        else
            g_bind_fail[i] = 1;
    }
}

static int is_modifier(UINT vk) {
    return vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LSHIFT ||
           vk == VK_RSHIFT || vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_LMENU ||
           vk == VK_RMENU || vk == VK_LWIN || vk == VK_RWIN;
}

static void kb_capture(UINT vk) {
    wchar_t msg[300], k[64], nm[64];
    int a = g_capture;
    UINT mods = 0;
    if (a < 0 || is_modifier(vk) || vk == VK_SNAPSHOT) return; /* wait for the real key */
    if (GetKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
    if (GetKeyState(VK_MENU) & 0x8000) mods |= MOD_ALT;
    if (GetKeyState(VK_SHIFT) & 0x8000) mods |= MOD_SHIFT;
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) mods |= MOD_WIN;
    g_capture = -1;
    msg[0] = 0;
    if (vk == VK_ESCAPE && !mods) {
        /* cancelled */
    } else if ((vk == VK_BACK || vk == VK_DELETE) && !mods) {
        KB[a].vk = 0;
        KB[a].mods = 0;
        action_name(a, nm);
        wsprintfW(msg, L"\"%s\" now has no key.", nm);
    } else {
        Bind nb = {mods, vk};
        key_text(nb, k);
        for (int i = 0; i < NACT; i++)
            if (i != a && KB[i].vk == vk && KB[i].mods == mods) {
                KB[i].vk = 0;
                KB[i].mods = 0;
                action_name(i, nm);
                wsprintfW(msg, L"%s was being used for \"%s\", so that one now has no key.", k, nm);
            }
        KB[a] = nb;
        if (!msg[0] && is_game_key(nb))
            wsprintfW(msg, L"Heads up: %s will stop working inside Roblox while this app is open. "
                           L"A combo like Ctrl+%s is safer.", k, k);
    }
    save_binds();
    kb_check();
    if (KB[a].vk && g_bind_fail[a]) {
        key_text(KB[a], k);
        wsprintfW(msg, L"%s is already taken by another program. Pick a different key.", k);
    }
    ui_set_status(msg);
}

/* ask Windows for a color; returns 1 if one was picked */
static int pick_color(COLORREF *c) {
    static COLORREF custom[16];
    CHOOSECOLORW cc;
    HWND owner = g_keydlg ? g_keydlg : g_host;
    memset(&cc, 0, sizeof(cc));
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = owner;
    cc.rgbResult = *c;
    cc.lpCustColors = custom;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
    g_capture = -2; /* keep the panel open while the color dialog has focus */
    int ok = ChooseColorW(&cc);
    g_capture = -1;
    if (ok) *c = cc.rgbResult & 0xFFFFFF;
    return ok;
}

static void ui_dwm(HWND h);
static void splash_preview(void);
static void use_theme(int idx) {
    S.theme = idx;
    theme_set(idx);
    save_settings();
    if (g_keydlg) { ui_dwm(g_keydlg); InvalidateRect(g_keydlg, NULL, FALSE); }
    card_refresh();
}

static void pick_custom_color(void) {
    static COLORREF custom[16];
    CHOOSECOLORW cc;
    HWND owner = g_keydlg ? g_keydlg : g_host;
    memset(&cc, 0, sizeof(cc));
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = owner;
    cc.rgbResult = RGB(COLORS[CUSTOM_COLOR].r, COLORS[CUSTOM_COLOR].g, COLORS[CUSTOM_COLOR].b);
    cc.lpCustColors = custom;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
    g_menu_open = 1;
    if (g_cursor_hidden) restore_cursor();
    SetForegroundWindow(owner);
    int ok = ChooseColorW(&cc);
    g_menu_open = (g_keydlg != NULL);
    if (ok) {
        COLORS[CUSTOM_COLOR].r = GetRValue(cc.rgbResult);
        COLORS[CUSTOM_COLOR].g = GetGValue(cc.rgbResult);
        COLORS[CUSTOM_COLOR].b = GetBValue(cc.rgbResult);
        if (g_edit_l2) S.l2_color = CUSTOM_COLOR;
        else S.color = CUSTOM_COLOR;
        apply();
    }
}

/* ---- widgets ---- */
/* HUD corner brackets: the "targeting frame" look on selected / featured cards. */
static void hud_corners(float x0, float y0, float x1, float y1, float L, COLORREF c, float a) {
    float t = dpf(2);
    p_rrect(x0, y0, x0 + L, y0 + t, 0, 0, c, a); p_rrect(x0, y0, x0 + t, y0 + L, 0, 0, c, a);
    p_rrect(x1 - L, y0, x1, y0 + t, 0, 0, c, a); p_rrect(x1 - t, y0, x1, y0 + L, 0, 0, c, a);
    p_rrect(x0, y1 - t, x0 + L, y1, 0, 0, c, a); p_rrect(x0, y1 - L, x0 + t, y1, 0, 0, c, a);
    p_rrect(x1 - L, y1 - t, x1, y1, 0, 0, c, a); p_rrect(x1 - t, y1 - L, x1, y1, 0, 0, c, a);
}
/* soft accent glow around a box */
static void glow_box(float x0, float y0, float x1, float y1, float a) {
    p_rrect(x0 - dpf(6), y0 - dpf(6), x1 + dpf(6), y1 + dpf(6), dpf(10), 0, C_ACCENT, a * 0.45f);
    p_rrect(x0 - dpf(3), y0 - dpf(3), x1 + dpf(3), y1 + dpf(3), dpf(7), 0, C_ACCENT, a);
}

static void draw_toggle(int x1, int cy, int on, int kind, int val) {
    int w = dpx(44), hh = dpx(24), x0 = x1 - w, y0 = cy - hh / 2, hv = hov(kind, val);
    if (on) p_rrect((float)x0 - dpf(4), (float)y0 - dpf(4), (float)(x0 + w) + dpf(4), (float)(y0 + hh) + dpf(4), hh / 2.0f + dpf(4), 0, C_ACCENT, hv ? 0.24f : 0.13f);
    p_rrect((float)x0, (float)y0, (float)(x0 + w), (float)(y0 + hh), hh / 2.0f, 0, on ? C_ACCENT : C_TRACK, 1);
    float kx = on ? x0 + w - hh / 2.0f : x0 + hh / 2.0f;
    if (hv) p_circle(kx, (float)cy, hh / 2.0f + dpf(5), 0, on ? C_ACCENT : C_TEXT, 0.15f);
    p_circle(kx, (float)cy, hh / 2.0f - dpf(3), 0, on ? C_ONACC : lerpc(C_MUTED, C_TEXT, 0.4f), 1);
    hit(x0 - dpx(4), y0 - dpx(4), x0 + w + dpx(4), y0 + hh + dpx(4), kind, val);
}

static void draw_button(int x0, int y0, int x1, int y1, const wchar_t *s, int kind, int val, int style) {
    int hv = hov(kind, val);
    COLORREF bg, fg, bd;
    if (style == 1) { bg = hv ? C_ACCHI : C_ACCENT; fg = C_ONACC; bd = bg; glow_box((float)x0, (float)y0, (float)x1, (float)y1, hv ? 0.26f : 0.13f); }
    else if (style == 2) { /* danger: always red, whatever the theme */
        bg = hv ? lerpc(C_CARD, RGB(220, 40, 56), 0.22f) : C_CARD;
        fg = theme_is_light() ? RGB(200, 30, 46) : RGB(255, 112, 122);
        bd = lerpc(C_CARD, RGB(220, 40, 56), 0.5f);
    }
    else { bg = hv ? C_HOVER : C_CARD; fg = C_TEXT; bd = C_BORDER; }
    p_rrect((float)x0, (float)y0, (float)x1, (float)y1, dpf(9), 0, bg, 1);
    p_rrect((float)x0, (float)y0, (float)x1, (float)y1, dpf(9), 1, bd, 1);
    q_text(x0, y0, x1, y1, s, F_BOLD, fg, DT_CENTER);
    hit(x0, y0, x1, y1, kind, val);
}

static void draw_segmented(int x0, int y0, int x1, int y1, const wchar_t **labels, int n, int sel, int kind) {
    p_rrect((float)x0, (float)y0, (float)x1, (float)y1, dpf(9), 0, C_BG, 1);
    p_rrect((float)x0, (float)y0, (float)x1, (float)y1, dpf(9), 1, C_BORDER, 1);
    int sw = (x1 - x0) / n;
    for (int i = 0; i < n; i++) {
        int a = x0 + i * sw + dpx(3), b = (i == n - 1 ? x1 : x0 + (i + 1) * sw) - dpx(3);
        if (i == sel) {
            p_rrect((float)a - dpf(2), (float)(y0 + dpx(1)), (float)b + dpf(2), (float)(y1 - dpx(1)), dpf(7), 0, C_ACCENT, 0.18f);
            p_rrect((float)a, (float)(y0 + dpx(3)), (float)b, (float)(y1 - dpx(3)), dpf(7), 0, C_ACCENT, 1);
        }
        else if (hov(kind, i)) p_rrect((float)a, (float)(y0 + dpx(3)), (float)b, (float)(y1 - dpx(3)), dpf(7), 0, C_HOVER, 1);
        q_text(a, y0, b, y1, labels[i], i == sel ? F_BOLD : F_TEXT, i == sel ? C_ONACC : C_MUTED, DT_CENTER);
        hit(a, y0, b, y1, kind, i);
    }
}

static void draw_slider(int x0, int x1, int y, const wchar_t *label, const wchar_t *value, int pos, int count, int id) {
    tip_row(x0, y, x1, y + dpx(52), label);
    q_text(x0, y, x1, y + dpx(22), label, F_TEXT, C_TEXT, DT_LEFT);
    q_text(x0, y, x1, y + dpx(22), value, F_BOLD, C_TEXT, DT_RIGHT);
    float ty = (float)(y + dpx(38));
    p_rrect((float)x0, ty - dpf(2), (float)x1, ty + dpf(2), dpf(2), 0, C_TRACK, 1);
    float kx = x0 + (x1 - x0) * (float)pos / (float)(count - 1);
    p_rrect((float)x0, ty - dpf(2), kx, ty + dpf(2), dpf(2), 0, C_ACCENT, 1);
    for (int i = 0; i < count; i++) {
        float tx = x0 + (x1 - x0) * (float)i / (float)(count - 1);
        p_circle(tx, ty + dpf(12), dpf(1.5f), 0, i <= pos ? lerpc(C_ACCENT, C_CARD, 0.4f) : C_DIM, 1);
    }
    p_rrect((float)x0, ty - dpf(4), kx, ty + dpf(4), dpf(4), 0, C_ACCENT, 0.18f); /* glow on the filled part */
    if (hov(H_SLIDER, id) || g_drag == id) p_circle(kx, ty, dpf(14), 0, C_ACCENT, 0.22f);
    p_circle(kx, ty, dpf(8), 0, theme_is_light() ? C_ACCENT : C_WHITE, 1);
    SetRect(&g_slider_track[id], x0, 0, x1, 0);
    hit(x0 - dpx(10), (int)ty - dpx(14), x1 + dpx(10), (int)ty + dpx(18), H_SLIDER, id);
}

static void slider_set(int id, int x) {
    RECT *t = &g_slider_track[id];
    int count = id == SL_SIZE ? NSIZES : id == SL_THICK ? 6 : NOPAC;
    int span = t->right - t->left;
    if (span < 1) span = 1;
    int pos = (int)floorf((float)(x - t->left) / span * (count - 1) + 0.5f);
    pos = clampi(pos, 0, count - 1);
    int changed = 0;
    if (id == SL_SIZE && g_edit_l2) { if (S.l2_size != pos) { S.l2_size = pos; changed = 1; } }
    else if (id == SL_SIZE) { if (S.size != pos) { S.size = pos; changed = 1; } }
    else if (id == SL_THICK) { if (S.thick != pos + 1) { S.thick = pos + 1; changed = 1; } }
    else { int o = NOPAC - 1 - pos; if (S.opacity != o) { S.opacity = o; changed = 1; } }
    if (changed) apply();
}

static __attribute__((unused)) void nav_icon(int t, float cx, float cy, COLORREF c) {
    float s = dpf(1);
    switch (t) {
    case TAB_CROSSHAIR:
        p_circle(cx, cy, 5.5f * s, 1.6f * s, c, 1);
        p_line(cx, cy - 9.5f * s, cx, cy - 3 * s, 1.6f * s, c, 1);
        p_line(cx, cy + 3 * s, cx, cy + 9.5f * s, 1.6f * s, c, 1);
        p_line(cx - 9.5f * s, cy, cx - 3 * s, cy, 1.6f * s, c, 1);
        p_line(cx + 3 * s, cy, cx + 9.5f * s, cy, 1.6f * s, c, 1);
        break;
    case TAB_COLOR:
        p_circle(cx, cy, 7.5f * s, 0, RGB(COLORS[S.color].r, COLORS[S.color].g, COLORS[S.color].b), 1);
        p_circle(cx, cy, 7.5f * s, 1.3f * s, c, 0.7f);
        break;
    case TAB_LOOK:
        for (int i = -1; i <= 1; i++) {
            float y = cy + i * 5.5f * s, kx = cx + (i == -1 ? -3.5f : i == 0 ? 4.0f : -0.5f) * s;
            p_line(cx - 9 * s, y, cx + 9 * s, y, 1.4f * s, c, 0.6f);
            p_circle(kx, y, 2.6f * s, 0, c, 1);
        }
        break;
    case TAB_FX: /* sparkle */
        p_line(cx, cy - 9 * s, cx, cy + 9 * s, 1.8f * s, c, 1);
        p_line(cx - 9 * s, cy, cx + 9 * s, cy, 1.8f * s, c, 1);
        p_line(cx - 4.5f * s, cy - 4.5f * s, cx + 4.5f * s, cy + 4.5f * s, 1.4f * s, c, 0.7f);
        p_line(cx - 4.5f * s, cy + 4.5f * s, cx + 4.5f * s, cy - 4.5f * s, 1.4f * s, c, 0.7f);
        p_circle(cx, cy, 2.4f * s, 0, c, 1);
        break;
    case TAB_PROFILES: /* stacked cards */
        p_rrect(cx - 6 * s, cy - 9 * s, cx + 9 * s, cy + 4 * s, 2.5f * s, 1.4f * s, c, 0.55f);
        p_rrect(cx - 9 * s, cy - 5 * s, cx + 6 * s, cy + 8 * s, 2.5f * s, 0, C_SIDE, 1);
        p_rrect(cx - 9 * s, cy - 5 * s, cx + 6 * s, cy + 8 * s, 2.5f * s, 1.6f * s, c, 1);
        break;
    case TAB_KEYS:
        p_rrect(cx - 10 * s, cy - 7 * s, cx + 10 * s, cy + 7 * s, 2.5f * s, 1.5f * s, c, 1);
        for (int i = -1; i <= 1; i++) p_circle(cx + i * 4.5f * s, cy - 2 * s, 1.1f * s, 0, c, 1);
        p_line(cx - 4.5f * s, cy + 3 * s, cx + 4.5f * s, cy + 3 * s, 1.5f * s, c, 1);
        break;
    case TAB_THEME: /* paint palette */
        p_circle(cx, cy, 8.5f * s, 1.5f * s, c, 1);
        p_circle(cx - 3.8f * s, cy - 2.6f * s, 1.7f * s, 0, c, 1);
        p_circle(cx + 1.2f * s, cy - 4.6f * s, 1.7f * s, 0, c, 1);
        p_circle(cx + 4.8f * s, cy - 0.6f * s, 1.7f * s, 0, c, 1);
        p_circle(cx - 0.5f * s, cy + 3.8f * s, 2.2f * s, 0, c, 1);
        break;
    case TAB_PERF: /* speedometer */
        p_arc(cx, cy + 1.5f * s, 8.5f * s, 1.7f * s, 2.618f, 4.189f, c, 1);
        p_line(cx, cy + 1.5f * s, cx + 5.2f * s, cy - 3.6f * s, 1.8f * s, c, 1);
        p_circle(cx, cy + 1.5f * s, 2.0f * s, 0, c, 1);
        break;
    case TAB_UPDATES: /* circular arrow */
        p_arc(cx, cy, 7.5f * s, 1.8f * s, -1.2f, 4.9f, c, 1);
        p_line(cx + 7.5f * s * cosf(-1.2f), cy + 7.5f * s * sinf(-1.2f), cx + 7.5f * s * cosf(-1.2f) + 5.0f * s, cy + 7.5f * s * sinf(-1.2f) - 0.5f * s, 1.8f * s, c, 1);
        p_line(cx + 7.5f * s * cosf(-1.2f), cy + 7.5f * s * sinf(-1.2f), cx + 7.5f * s * cosf(-1.2f) + 1.2f * s, cy + 7.5f * s * sinf(-1.2f) - 4.8f * s, 1.8f * s, c, 1);
        break;
    case TAB_GENERAL:
        for (int k = 0; k < 8; k++) {
            float an = k * 0.7853982f;
            p_line(cx + 6 * s * cosf(an), cy + 6 * s * sinf(an), cx + 9.5f * s * cosf(an), cy + 9.5f * s * sinf(an), 2.6f * s, c, 1);
        }
        p_circle(cx, cy, 6.5f * s, 0, c, 1);
        p_circle(cx, cy, 2.6f * s, 0, C_SIDE, 1);
        break;
    }
}

/* ---- tabs ---- */
static HFONT fit_font(HDC dc, const wchar_t *t, int spacing, int maxw, HFONT a, HFONT b, HFONT c);
/* a font for t that fits maxw: a, else the smaller ones */
static HFONT fitf(const wchar_t *t, int maxw, HFONT a, HFONT b, HFONT c) { return g_paint_dc ? fit_font(g_paint_dc, t, 0, maxw, a, b, c) : a; }
static int shown_styles(void) { int n = 0; for (int i = 0; i < NSTYLES; i++) if (style_shown(i)) n++; return n; }
#define NSTYLES_SHOWN_LAST shown_styles()
static void tab_crosshair(int cx0, int top, int cx1) {
    int cols = 7, gap = dpx(8);
    int tw = (cx1 - cx0 - gap * (cols - 1)) / cols, th = dpx(88);
    for (int i = 0, slot = 0; i < NSTYLES; i++) {
        if (!style_shown(i)) continue;
        int x = cx0 + (slot % cols) * (tw + gap), y = top + (slot / cols) * (th + gap);
        slot++;
        int sel = i == (g_edit_l2 ? S.l2_style : S.style), h = hov(H_STYLE, i);
        p_rrect((float)x, (float)y, (float)(x + tw), (float)(y + th), dpf(12), 0, h ? C_HOVER : C_CARD, 1);
        if (sel) {
            p_rrect((float)x - dpf(5), (float)y - dpf(5), (float)(x + tw) + dpf(5), (float)(y + th) + dpf(5), dpf(16), 0, C_ACCENT, 0.10f);
            p_rrect((float)x, (float)y, (float)(x + tw), (float)(y + th), dpf(12), 0, C_ACCENT, 0.10f);
        }
        p_rrect((float)x, (float)y, (float)(x + tw), (float)(y + th), dpf(12), sel ? dpf(2) : 1, sel ? C_ACCENT : h ? lerpc(C_BORDER, C_TEXT, 0.15f) : C_BORDER, 1);
        if (sel) {
            float bx = (float)(x + tw - dpx(14)), by = (float)(y + dpx(14));
            p_circle(bx, by, dpf(8), 0, C_ACCENT, 1);
            p_line(bx - dpf(3.5f), by, bx - dpf(1), by + dpf(2.8f), dpf(1.6f), C_ONACC, 1);
            p_line(bx - dpf(1), by + dpf(2.8f), bx + dpf(3.8f), by - dpf(2.6f), dpf(1.6f), C_ONACC, 1);
        }
        int n = dpx(58);
        p_crosshair(x + tw / 2.0f, (float)(y + dpx(36)), n, i, g_edit_l2 ? S.l2_color : S.color, n * 0.27f, dpx(2) < 2 ? 2 : dpx(2), S.outline, 1.0f);
        q_text(x + dpx(3), y + th - dpx(26), x + tw - dpx(3), y + th - dpx(8), STYLE_NAMES[i],
               fitf(STYLE_NAMES[i], tw - dpx(6), F_SMALL, F_XS, F_TINY), sel ? C_TEXT : C_MUTED, DT_CENTER);
        if (i >= 19) { /* little NEW tag on the v1.9 additions */
            p_rrect((float)(x + dpx(6)), (float)(y + dpx(6)), (float)(x + dpx(36)), (float)(y + dpx(19)), dpf(6), 0, C_ACCENT, 0.9f);
            q_text_ex(x + dpx(6), y + dpx(6), x + dpx(36), y + dpx(19), L"NEW", F_TINY, C_ONACC, DT_CENTER, 0);
        }
        hit(x, y, x + tw, y + th, H_STYLE, i);
        if (g_edit_l2 && slot == NSTYLES_SHOWN_LAST) { /* layer 2: a "None" tile in the free slot */
            int x2 = cx0 + (slot % cols) * (tw + gap), y2 = top + (slot / cols) * (th + gap), s2 = S.l2_style < 0, h2 = hov(H_STYLE, -1);
            p_rrect((float)x2, (float)y2, (float)(x2 + tw), (float)(y2 + th), dpf(12), 0, h2 ? C_HOVER : C_CARD, 1);
            p_rrect((float)x2, (float)y2, (float)(x2 + tw), (float)(y2 + th), dpf(12), s2 ? dpf(2) : 1, s2 ? C_ACCENT : C_BORDER, 1);
            float mx = x2 + tw / 2.0f, my = (float)(y2 + dpx(36));
            p_circle(mx, my, dpf(13), dpf(2), C_MUTED, 1);
            p_line(mx - dpf(9), my + dpf(9), mx + dpf(9), my - dpf(9), dpf(2), C_MUTED, 1);
            q_text(x2 + dpx(3), y2 + th - dpx(26), x2 + tw - dpx(3), y2 + th - dpx(8), L"No layer", F_SMALL, s2 ? C_TEXT : C_MUTED, DT_CENTER);
            hit(x2, y2, x2 + tw, y2 + th, H_STYLE, -1);
        }
    }
}

static void color_dot(float x, float y, float R, int i, int sel, int h) {
    if (h || sel) p_circle(x, y, R + dpf(6), 0, C_HOVER, 1);
    if (sel) p_circle(x, y, R + dpf(6), dpf(2), C_ACCENT, 1);
    p_circle(x, y, R, 0, RGB(COLORS[i].r, COLORS[i].g, COLORS[i].b), 1);
    p_circle(x, y, R, 1, C_TEXT, 0.14f);
}

static void setting_row(int x0, int y, int x1, int ctrl_w, const wchar_t *title, const wchar_t *sub, int hk, int hv);
static void tab_color(int cx0, int top, int cx1) {
    int cols = 6, cw = (cx1 - cx0) / cols, ch = dpx(84);
    for (int i = 0; i < CUSTOM_COLOR; i++) {
        float x = cx0 + (i % cols) * cw + cw / 2.0f, y = (float)(top + (i / cols) * ch + dpx(26));
        int sel = i == (g_edit_l2 ? S.l2_color : S.color);
        color_dot(x, y, dpf(18), i, sel, hov(H_COLOR, i));
        q_text((int)x - cw / 2, (int)y + dpx(28), (int)x + cw / 2, (int)y + dpx(46), COLORS[i].name, F_SMALL, sel ? C_TEXT : C_MUTED, DT_CENTER);
        hit((int)x - cw / 2 + dpx(4), (int)y - dpx(28), (int)x + cw / 2 - dpx(4), (int)y + dpx(48), H_COLOR, i);
    }
    int y0 = top + 3 * ch + dpx(10), y1 = y0 + dpx(72);
    int sel = (g_edit_l2 ? S.l2_color : S.color) == CUSTOM_COLOR;
    p_rrect((float)cx0, (float)y0, (float)cx1, (float)y1, dpf(14), 0, hov(H_COLOR, CUSTOM_COLOR) ? C_HOVER : C_CARD, 1);
    p_rrect((float)cx0, (float)y0, (float)cx1, (float)y1, dpf(14), sel ? dpf(2) : 1, sel ? C_ACCENT : C_BORDER, 1);
    color_dot((float)(cx0 + dpx(40)), (y0 + y1) / 2.0f, dpf(18), CUSTOM_COLOR, 0, 0);
    wchar_t hex[64];
    wsprintfW(hex, L"#%02X%02X%02X  \x2022  %s", COLORS[CUSTOM_COLOR].r, COLORS[CUSTOM_COLOR].g,
              COLORS[CUSTOM_COLOR].b, sel ? L"in use" : L"click to use");
    q_text(cx0 + dpx(74), y0 + dpx(14), cx1 - dpx(190), y0 + dpx(36), L"Custom color", F_BOLD, C_TEXT, DT_LEFT);
    q_text(cx0 + dpx(74), y0 + dpx(36), cx1 - dpx(190), y0 + dpx(58), hex, F_SMALL, C_MUTED, DT_LEFT);
    hit(cx0, y0, cx1 - dpx(180), y1, H_COLOR, CUSTOM_COLOR);
    draw_button(cx1 - dpx(170), y0 + dpx(18), cx1 - dpx(18), y1 - dpx(18), L"Pick a color...", H_PICK, 0, 1);
    { /* auto-contrast */
        int y = y1 + dpx(14);
        hit(cx0, y, cx1, y + dpx(64), H_TOGGLE, T_AUTOCON);
        setting_row(cx0, y, cx1, dpx(90), L"Auto-contrast",
                    S.auto_contrast && g_ac_alt ? (g_ac_alt == 1 ? L"Switched to white right now \x2014 your color would blend in"
                                                                 : L"Switched to black right now \x2014 your color would blend in")
                                                : L"Turns white or black when your color would blend into the map", H_TOGGLE, T_AUTOCON);
        draw_toggle(cx1 - dpx(18), y + dpx(32), S.auto_contrast, H_TOGGLE, T_AUTOCON);
    }
}

static void draw_preview(int x0, int y0, int x1, int y1) {
    float cx = (x0 + x1) / 2.0f, cy = (y0 + y1) / 2.0f, hw = (x1 - x0) / 2.0f, hh = (y1 - y0) / 2.0f, r = dpf(14) * g_th.radius * g_sharp;
    int horizon = y0 + (int)((y1 - y0) * 0.64f);
    int ix0, iy0, ix1, iy1;
    bounds((float)x0, (float)y0, (float)x1, (float)y1, &ix0, &iy0, &ix1, &iy1);
    for (int y = iy0; y < iy1; y++) {
        COLORREF c = y < horizon ? lerpc(RGB(70, 108, 160), RGB(168, 192, 216), (float)(y - y0) / (horizon - y0))
                                 : lerpc(RGB(84, 106, 72), RGB(46, 60, 40), (float)(y - horizon) / (y1 - horizon));
        for (int x = ix0; x < ix1; x++) {
            float cv = clamp01(0.5f - rbox_d(x + 0.5f, y + 0.5f, cx, cy, hw, hh, r));
            if (cv > 0) blend(x, y, c, cv);
        }
    }
    float w = (float)(x1 - x0);
    p_rrect(x0 + w * 0.06f, (float)(horizon - dpx(64)), x0 + w * 0.28f, (float)horizon, dpf(3), 0, RGB(62, 72, 92), 1);
    p_rrect(x0 + w * 0.60f, (float)(horizon - dpx(110)), x0 + w * 0.79f, (float)horizon, dpf(3), 0, RGB(54, 62, 80), 1);
    p_rrect(x0 + w * 0.79f, (float)(horizon - dpx(48)), x0 + w * 0.94f, (float)horizon, dpf(3), 0, RGB(70, 80, 100), 1);
    p_rrect((float)x0, (float)y0, (float)x1, (float)y1, dpf(14), 1, C_BORDER, 1);
    hud_corners((float)x0 - dpf(4), (float)y0 - dpf(4), (float)x1 + dpf(4), (float)y1 + dpf(4), dpf(14), C_ACCENT, 1);
    p_rrect((float)(x0 + dpx(10)), (float)(y0 + dpx(10)), (float)(x0 + dpx(118)), (float)(y0 + dpx(30)), dpf(3), 0, RGB(0, 0, 0), 0.5f);
    p_circle((float)(x0 + dpx(20)), (float)(y0 + dpx(20)), dpf(3), 0, RGB(255, 70, 70), 0.95f);
    q_text_ex(x0 + dpx(28), y0 + dpx(10), x0 + dpx(118), y0 + dpx(30), L"LIVE PREVIEW", F_TINY, C_WHITE, DT_LEFT, 1);
    if (S.cover)
        p_circle(cx, cy, (float)COVER_RADII[S.cover], 0,
                 RGB(COVER_COLORS[S.cover_color].r, COVER_COLORS[S.cover_color].g, COVER_COLORS[S.cover_color].b), 1);
    int n = needed_size();
    float om;
    int col = fx_color(&om);
    p_crosshair(cx, cy, n, S.style, col, (float)SIZES[S.size] * fx_scale(), S.thick, S.outline,
                OPACITIES[S.opacity] / 100.0f * om);
    if (S.l2_style >= 0)
        p_crosshair(cx, cy, n, S.l2_style, S.l2_color, (float)SIZES[S.l2_size] * fx_scale(), S.thick, S.outline, OPACITIES[S.opacity] / 100.0f * om);
    q_text(x0, y1 + dpx(8), x1, y1 + dpx(28), L"Real size  \x2022  click it to test", F_SMALL, C_DIM, DT_CENTER);
    hit(x0, y0, x1, y1, H_PREVIEW, 0);
}

static void tab_look(int cx0, int top, int cx1) {
    int lx1 = cx1;
    wchar_t v[64], k[64];
    if (g_edit_l2) {
        wsprintfW(v, L"%d px", SIZES[S.l2_size]);
        draw_slider(cx0, lx1, top, L"Layer 2 size", v, S.l2_size, NSIZES, SL_SIZE);
    } else {
        wsprintfW(v, L"%d px", SIZES[S.size]);
        draw_slider(cx0, lx1, top, L"Size", v, S.size, NSIZES, SL_SIZE);
    }
    wsprintfW(v, L"%d px", S.thick);
    draw_slider(cx0, lx1, top + dpx(64), L"Thickness", v, S.thick - 1, 6, SL_THICK);
    wsprintfW(v, L"%d%%", OPACITIES[S.opacity]);
    draw_slider(cx0, lx1, top + dpx(128), L"Opacity", v, NOPAC - 1 - S.opacity, NOPAC, SL_OPAC);

    int y = top + dpx(196);
    q_text(cx0, y, lx1, y + dpx(22), L"Outline", F_TEXT, C_TEXT, DT_LEFT);
    q_text(cx0, y + dpx(21), lx1 - dpx(60), y + dpx(40), L"Edge that keeps it visible on any background", F_SMALL, C_MUTED, DT_LEFT);
    hit(cx0, y, lx1, y + dpx(42), H_TOGGLE, T_OUTLINE);
    draw_toggle(lx1, y + dpx(20), S.outline, H_TOGGLE, T_OUTLINE);

    y = top + dpx(258);
    q_text(cx0, y, lx1, y + dpx(22), L"Cover game crosshair", F_TEXT, C_TEXT, DT_LEFT);
    key_text(KB[A_COVER], k);
    wsprintfW(v, L"Solid patch behind yours  \x2022  %s", KB[A_COVER].vk ? k : L"no key");
    q_text(cx0, y + dpx(21), lx1, y + dpx(40), v, F_SMALL, C_MUTED, DT_LEFT);
    static const wchar_t *SEG[] = {L"Off", L"S", L"M", L"L", L"XL"};
    int sy0 = y + dpx(50), sy1 = sy0 + dpx(34);
    draw_segmented(cx0, sy0, lx1, sy1, SEG, NCOVER, S.cover, H_COVER);
    int py = sy1 + dpx(28);
    q_text(cx0, py - dpx(12), cx0 + dpx(120), py + dpx(12), L"Patch color", F_SMALL, C_MUTED, DT_LEFT);
    for (int i = 0; i < NCOVERCOL; i++) {
        float x = (float)(cx0 + dpx(132) + i * dpx(38));
        int sel = S.cover_color == i;
        if (sel) p_circle(x, (float)py, dpf(15), dpf(2), C_ACCENT, 1);
        else if (hov(H_COVERCOL, i)) p_circle(x, (float)py, dpf(15), 0, C_HOVER, 1);
        p_circle(x, (float)py, dpf(11), 0, RGB(COVER_COLORS[i].r, COVER_COLORS[i].g, COVER_COLORS[i].b), 1);
        p_circle(x, (float)py, dpf(11), 1, C_WHITE, 0.16f);
        hit((int)x - dpx(16), py - dpx(16), (int)x + dpx(16), py + dpx(16), H_COVERCOL, i);
    }
    { /* fine-tune position */
        int ny = py + dpx(34);
        tip_row(cx0, ny, lx1, ny + dpx(44), L"Fine-tune position");
        q_text(cx0, ny, lx1, ny + dpx(22), L"Fine-tune position", F_TEXT, C_TEXT, DT_LEFT);
        q_text(cx0, ny + dpx(21), lx1 - dpx(330), ny + dpx(40), L"Line it up with a game's own aim point",
               fitf(L"Line it up with a game's own aim point", lx1 - dpx(330) - cx0, F_SMALL, F_XS, F_XS), C_MUTED, DT_LEFT);
        int bx = lx1 - dpx(318), by = ny + dpx(4), bs = dpx(30);
        for (int a = 0; a < 2; a++) { /* X then Y: [-] value [+] */
            int x0 = bx + a * dpx(136), vv = a ? g_nudge_y : g_nudge_x;
            wchar_t lb[24];
            q_text(x0, by, x0 + dpx(18), by + bs, a ? L"Y" : L"X", F_BOLD, C_MUTED, DT_LEFT);
            for (int k = 0; k < 2; k++) {
                int b0 = x0 + dpx(20) + k * dpx(76), hv = hov(H_NUDGE, a * 2 + k);
                p_rrect((float)b0, (float)by, (float)(b0 + bs), (float)(by + bs), dpf(6), 0, hv ? C_HOVER : C_CARD, 1);
                p_rrect((float)b0, (float)by, (float)(b0 + bs), (float)(by + bs), dpf(6), 1, C_BORDER, 1);
                float mx = b0 + bs / 2.0f, my = by + bs / 2.0f;
                p_line(mx - dpf(5), my, mx + dpf(5), my, dpf(1.6f), C_TEXT, 1);
                if (k) p_line(mx, my - dpf(5), mx, my + dpf(5), dpf(1.6f), C_TEXT, 1);
                hit(b0, by, b0 + bs, by + bs, H_NUDGE, a * 2 + k);
            }
            wsprintfW(lb, vv > 0 ? L"+%d" : L"%d", vv);
            q_text(x0 + dpx(50), by, x0 + dpx(96), by + bs, lb, F_BOLD, vv ? C_ACCLT : C_TEXT, DT_CENTER);
        }
        if (g_nudge_x || g_nudge_y) {
            int r0 = bx + dpx(272), hv = hov(H_NUDGE, 4);
            q_text(r0, by, lx1, by + bs, L"Reset", F_SMALL, hv ? C_TEXT : C_ACCLT, DT_RIGHT);
            hit(r0, by, lx1, by + bs, H_NUDGE, 4);
        }
    }
}

static void key_hint(wchar_t *out, const wchar_t *text, int a);
static void toggle_line(int cx0, int lx1, int y, const wchar_t *title, const wchar_t *sub, int on, int val) {
    tip_row(cx0, y, lx1, y + dpx(44), title);
    q_text(cx0, y, lx1, y + dpx(22), title, F_TEXT, C_TEXT, DT_LEFT);
    q_text(cx0, y + dpx(21), lx1 - dpx(60), y + dpx(40), sub, F_SMALL, C_MUTED, DT_LEFT);
    hit(cx0, y, lx1, y + dpx(42), H_TOGGLE, val);
    draw_toggle(lx1, y + dpx(20), on, H_TOGGLE, val);
}

static void tab_fx(int cx0, int top, int cx1) {
    int lx1 = cx1, y = top;
    toggle_line(cx0, lx1, y, L"Glow", L"Soft neon halo around your crosshair", S.glow, T_GLOW);
    y += dpx(66);
    tip_row(cx0, y, lx1, y + dpx(86), L"Color effect");
    q_text(cx0, y, lx1, y + dpx(22), L"Color effect", F_TEXT, C_TEXT, DT_LEFT);
    q_text(cx0, y + dpx(21), lx1, y + dpx(40), L"Rainbow cycles colors, Pulse breathes in and out", F_SMALL, C_MUTED, DT_LEFT);
    static const wchar_t *FX[] = {L"Off", L"Rainbow", L"Pulse"};
    draw_segmented(cx0, y + dpx(48), lx1, y + dpx(82), FX, 3, S.anim, H_FXSEG);
    y += dpx(104);
    tip_row(cx0, y, lx1, y + dpx(86), L"Effect speed");
    q_text(cx0, y, lx1, y + dpx(22), L"Effect speed", F_TEXT, C_TEXT, DT_LEFT);
    static const wchar_t *SP[] = {L"Slow", L"Medium", L"Fast"};
    draw_segmented(cx0, y + dpx(28), lx1, y + dpx(62), SP, 3, S.anim_speed, H_SPEED);
    y += dpx(86);
    toggle_line(cx0, lx1, y, L"Click reaction", L"Your crosshair kicks out when you click", S.click_fx, T_CLICK);
    y += dpx(60);
    toggle_line(cx0, lx1, y, L"Hide from recordings", L"Clips, streams & screenshots won't show it", g_nocap, T_NOCAP);
    y += dpx(60);
    toggle_line(cx0, lx1, y, L"Hide in Roblox menus & chat", L"Gone while the Esc menu or the chat box is open", S.menu_hide, T_MENUHIDE);
}

static void setting_row(int x0, int y, int x1, int ctrl_w, const wchar_t *title, const wchar_t *sub, int hk, int hv);
static void tab_profiles(int cx0, int top, int cx1) {
    int gap = dpx(16), cw = (cx1 - cx0 - 2 * gap) / 3, ch = dpx(228);
    if (g_want_recent) { recent_load(); g_want_recent = 0; }
    wchar_t t[96], sub[128];
    for (int p = 0; p < NPROF; p++) {
        Profile *pr = &g_prof[p];
        int x0 = cx0 + p * (cw + gap), x1 = x0 + cw, y0 = top, y1 = top + ch;
        int inuse = profile_in_use(p);
        p_rrect((float)x0, (float)y0, (float)x1, (float)y1, dpf(14), 0, C_CARD, 1);
        p_rrect((float)x0, (float)y0, (float)x1, (float)y1, dpf(14), inuse ? dpf(2) : 1, inuse ? C_ACCENT : C_BORDER, 1);
        wsprintfW(t, L"Profile %d", p + 1);
        q_text(x0 + dpx(16), y0 + dpx(14), x1 - dpx(70), y0 + dpx(36), t, F_BOLD, C_TEXT, DT_LEFT);
        if (inuse) {
            p_rrect((float)(x1 - dpx(70)), (float)(y0 + dpx(15)), (float)(x1 - dpx(14)), (float)(y0 + dpx(35)), dpf(10), 0, C_ACCENT, 1);
            q_text_ex(x1 - dpx(70), y0 + dpx(15), x1 - dpx(14), y0 + dpx(35), L"IN USE", F_TINY, C_ONACC, DT_CENTER, 1);
        }
        key_hint(sub, pr->saved ? L"Saved" : L"Empty slot", A_PROF1 + p);
        q_text(x0 + dpx(16), y0 + dpx(34), x1 - dpx(14), y0 + dpx(52), sub, F_SMALL, C_MUTED, DT_LEFT);
        int bx0 = x0 + dpx(14), bx1 = x1 - dpx(14), by0 = y0 + dpx(58), by1 = y0 + dpx(128);
        p_rrect((float)bx0, (float)by0, (float)bx1, (float)by1, dpf(10), 0, C_BG, 1);
        float pcx = (bx0 + bx1) / 2.0f, pcy = (by0 + by1) / 2.0f;
        if (pr->saved) {
            int keep = S.glow, col = pr->color;
            S.glow = pr->glow;
            if (col == CUSTOM_COLOR) {
                g_dyn_r = (BYTE)(pr->custom >> 16); g_dyn_g = (BYTE)(pr->custom >> 8); g_dyn_b = (BYTE)pr->custom;
                col = -1;
            }
            int n = (by1 - by0) - dpx(8);
            float L = fminf((float)SIZES[pr->size], n * 0.27f);
            if (pr->cover)
                p_circle(pcx, pcy, fminf((float)COVER_RADII[pr->cover], n * 0.42f), 0,
                         RGB(COVER_COLORS[pr->cover_color].r, COVER_COLORS[pr->cover_color].g, COVER_COLORS[pr->cover_color].b), 1);
            p_crosshair(pcx, pcy, n, pr->style, col, L, pr->thick, pr->outline, OPACITIES[pr->opacity] / 100.0f);
            S.glow = keep;
            q_text(x0 + dpx(16), y0 + dpx(134), x1 - dpx(14), y0 + dpx(154), STYLE_NAMES[pr->style], F_BOLD, C_TEXT, DT_LEFT);
            wsprintfW(sub, L"%s  \x2022  %d px%s%s%s", COLORS[pr->color].name, SIZES[pr->size],
                      pr->glow ? L"  \x2022  Glow" : L"", pr->anim == 1 ? L"  \x2022  Rainbow" : pr->anim == 2 ? L"  \x2022  Pulse" : L"",
                      pr->click_fx ? L"  \x2022  Click" : L"");
            q_text(x0 + dpx(16), y0 + dpx(153), x1 - dpx(14), y0 + dpx(171), sub, fitf(sub, x1 - x0 - dpx(30), F_SMALL, F_XS, F_XS), C_MUTED, DT_LEFT | DT_END_ELLIPSIS);
        } else {
            q_text(bx0, by0, bx1, by1, L"Empty", F_SMALL, C_DIM, DT_CENTER);
            q_text(x0 + dpx(16), y0 + dpx(140), x1 - dpx(14), y0 + dpx(170), L"Save your current setup here",
                   F_SMALL, C_MUTED, DT_LEFT | DT_WORDBREAK);
        }
        int b0 = y1 - dpx(48), bm = (x0 + x1) / 2;
        if (pr->saved) {
            draw_button(x0 + dpx(14), b0, bm - dpx(4), b0 + dpx(34), inuse ? L"Loaded" : L"Load", H_PLOAD, p, 1);
            draw_button(bm + dpx(4), b0, x1 - dpx(14), b0 + dpx(34), L"Save here", H_PSAVE, p, 0);
        } else draw_button(x0 + dpx(14), b0, x1 - dpx(14), b0 + dpx(34), L"Save current", H_PSAVE, p, 0);
    }
    { /* share codes */
        int y = top + ch + dpx(14);
        wchar_t code[32], sub2[96];
        xh_code(code);
        wsprintfW(sub2, L"Your code: %s", code);
        setting_row(cx0, y, cx1, dpx(300), L"Share your crosshair", sub2, 0, 0);
        draw_button(cx1 - dpx(292), y + dpx(14), cx1 - dpx(158), y + dpx(50), L"Copy code", H_BUTTON, B_COPYCODE, 1);
        draw_button(cx1 - dpx(150), y + dpx(14), cx1 - dpx(16), y + dpx(50), L"Paste code", H_BUTTON, B_PASTECODE, 0);
    }
    { /* recent codes: one click to switch back to a friend's crosshair */
        int y = top + ch + dpx(92);
        q_text_ex(cx0 + dpx(2), y, cx1, y + dpx(18), L"RECENT CODES", F_TINY, C_DIM, DT_LEFT, 1);
        y += dpx(22);
        int cg = dpx(8), cols = 5, chw = (cx1 - cx0 - cg * (cols - 1)) / cols, chh = dpx(46);
        wchar_t cur[32];
        xh_code(cur);
        if (!g_nrecent) {
            p_rrect((float)cx0, (float)y, (float)cx1, (float)(y + chh), dpf(10), 0, C_CARD, 0.6f);
            p_rrect((float)cx0, (float)y, (float)cx1, (float)(y + chh), dpf(10), 1, C_BORDER, 1);
            q_text(cx0 + dpx(16), y, cx1 - dpx(16), y + chh, L"Codes you paste show up here, so you can switch back to a friend's crosshair in one click.",
                   fitf(L"Codes you paste show up here, so you can switch back to a friend's crosshair in one click.", cx1 - cx0 - dpx(32), F_SMALL, F_XS, F_XS), C_MUTED, DT_LEFT);
        }
        for (int i = 0; i < g_nrecent; i++) {
            XhCode c;
            if (!xh_code_decode(g_recent[i], &c)) continue;
            int x0 = cx0 + (i % cols) * (chw + cg), y0 = y + (i / cols) * (chh + cg), x1 = x0 + chw, y1 = y0 + chh;
            int hv = hov(H_RCODE, i) || hov(H_RCODEX, i), on = !wcscmp(cur, g_recent[i]);
            p_rrect((float)x0, (float)y0, (float)x1, (float)y1, dpf(10), 0, hv ? C_HOVER : C_CARD, 1);
            p_rrect((float)x0, (float)y0, (float)x1, (float)y1, dpf(10), on ? dpf(2) : 1, on ? C_ACCENT : C_BORDER, 1);
            int pv = chh - dpx(10);
            p_rrect((float)(x0 + dpx(5)), (float)(y0 + dpx(5)), (float)(x0 + dpx(5) + pv), (float)(y1 - dpx(5)), dpf(7), 0, C_BG, 1);
            int col = c.color, keep = S.glow;
            if (col == CUSTOM_COLOR) { g_dyn_r = (BYTE)c.r; g_dyn_g = (BYTE)c.g; g_dyn_b = (BYTE)c.b; col = -1; }
            S.glow = c.glow;
            int n = pv - dpx(4);
            p_crosshair(x0 + dpx(5) + pv / 2.0f, (y0 + y1) / 2.0f, n, c.style, col, fminf((float)SIZES[c.size], n * 0.3f), c.thick, c.outline, OPACITIES[c.opac] / 100.0f);
            S.glow = keep;
            int tx = x0 + dpx(10) + pv, tr = x1 - dpx(hv ? 22 : 6);
            q_text(tx, y0 + dpx(6), tr, y0 + dpx(24), STYLE_NAMES[c.style], fitf(STYLE_NAMES[c.style], tr - tx, F_SMALL, F_XS, F_TINY), C_TEXT, DT_LEFT);
            q_text(tx, y0 + dpx(23), x1 - dpx(6), y1 - dpx(5), g_recent[i] + 3, fitf(g_recent[i] + 3, x1 - dpx(6) - tx, F_XS, F_TINY, F_TINY), on ? C_ACCLT : C_DIM, DT_LEFT);
            hit(x0, y0, x1, y1, H_RCODE, i);
            if (hv) { /* remove */
                float bx = (float)(x1 - dpx(13)), by = (float)(y0 + dpx(12)), r4 = dpf(3.5f);
                int hx = hov(H_RCODEX, i);
                p_circle(bx, by, dpf(8), 0, hx ? C_ACCENT : C_BG, 1);
                p_line(bx - r4, by - r4, bx + r4, by + r4, dpf(1.4f), hx ? C_ONACC : C_MUTED, 1);
                p_line(bx - r4, by + r4, bx + r4, by - r4, dpf(1.4f), hx ? C_ONACC : C_MUTED, 1);
                hit((int)bx - dpx(9), (int)by - dpx(9), (int)bx + dpx(9), (int)by + dpx(9), H_RCODEX, i);
            }
        }
    }
}

static void tab_keys(int cx0, int top, int cx1, int bottom) {
    RECT old = g_clip;
    int lx1 = cx1 - dpx(14);
    SetRect(&g_clip, cx0 - dpx(4), top, cx1, bottom);
    IntersectRect(&g_clip, &g_clip, &old);
    int y = top - g_kscroll;
    wchar_t t[96], nm[64];
    for (int sec = 0; sec < 2; sec++) {
        q_text_ex(cx0 + dpx(2), y, lx1, y + dpx(26), sec ? L"JUMP STRAIGHT TO A CROSSHAIR" : L"GENERAL",
                  F_TINY, C_DIM, DT_LEFT, 1);
        y += dpx(30);
        int from = sec ? A_STYLE0 : 0, to = sec ? NACT : A_STYLE0;
        for (int i = from; i < to; i++) {
            if (sec && !style_shown(i - A_STYLE0)) continue;
            int ry0 = y, ry1 = y + dpx(38);
            if (ry1 > top && ry0 < bottom) {
                int cap = g_capture == i, h = hov(H_KEY, i) || hov(H_KEYCLEAR, i);
                p_rrect((float)cx0, (float)ry0, (float)lx1, (float)ry1, dpf(10), 0, h || cap ? C_HOVER : C_CARD, 1);
                int tx = cx0 + dpx(16);
                if (sec) {
                    int si = i - A_STYLE0, n = dpx(30);
                    p_crosshair((float)(cx0 + dpx(24)), (ry0 + ry1) / 2.0f, n, si, S.color, n * 0.27f, 2, 0, 1.0f);
                    tx = cx0 + dpx(46);
                    wcscpy(nm, STYLE_NAMES[si]);
                } else
                    action_name(i, nm);
                q_text(tx, ry0, lx1 - dpx(226), ry1, nm, F_TEXT, C_TEXT, DT_LEFT);
                int kx1 = lx1 - dpx(44), kx0 = kx1 - dpx(170);
                COLORREF bd = cap ? C_ACCENT : g_bind_fail[i] ? RGB(190, 70, 76) : C_BORDER;
                p_rrect((float)kx0, (float)(ry0 + dpx(6)), (float)kx1, (float)(ry1 - dpx(6)), dpf(7), 0, C_BG, 1);
                p_rrect((float)kx0, (float)(ry0 + dpx(6)), (float)kx1, (float)(ry1 - dpx(6)), dpf(7), cap ? dpf(2) : 1, bd, 1);
                COLORREF tc;
                if (cap) { wcscpy(t, L"Press a key..."); tc = C_ACCLT; }
                else if (KB[i].vk) {
                    key_text(KB[i], t);
                    if (g_bind_fail[i]) { wcscat(t, L"  (in use)"); tc = RGB(255, 120, 120); }
                    else tc = C_TEXT;
                } else { wcscpy(t, L"Not set"); tc = C_DIM; }
                q_text(kx0, ry0, kx1, ry1, t, KB[i].vk || cap ? F_BOLD : F_SMALL, tc, DT_CENTER);
                hit(cx0, ry0, lx1 - dpx(40), ry1, H_KEY, i);
                if (KB[i].vk && !cap) {
                    float bx = (float)(lx1 - dpx(22)), by = (ry0 + ry1) / 2.0f, s = dpf(4);
                    int hc = hov(H_KEYCLEAR, i);
                    if (hc) p_circle(bx, by, dpf(13), 0, lerpc(C_CARD, RGB(220, 40, 56), 0.3f), 1);
                    p_line(bx - s, by - s, bx + s, by + s, dpf(1.6f), hc ? RGB(255, 120, 130) : C_DIM, 1);
                    p_line(bx - s, by + s, bx + s, by - s, dpf(1.6f), hc ? RGB(255, 120, 130) : C_DIM, 1);
                    hit((int)bx - dpx(15), (int)by - dpx(15), (int)bx + dpx(15), (int)by + dpx(15), H_KEYCLEAR, i);
                }
            }
            y += dpx(44);
        }
        y += dpx(10);
    }
    int total = y + g_kscroll - top, view = bottom - top;
    g_kscroll_max = total > view ? total - view : 0;
    if (g_kscroll_max > 0) {
        float th = (float)view * view / total, ty = top + (view - th) * g_kscroll / (float)g_kscroll_max;
        p_rrect((float)(cx1 - dpx(5)), ty, (float)(cx1 - dpx(1)), ty + th, dpf(2), 0, C_TRACK, 1);
    }
    g_clip = old;
    /* footer */
    draw_button(cx0, bottom + dpx(14), cx0 + dpx(160), bottom + dpx(48), L"Reset to defaults", H_KRESET, 0, 0);
    draw_button(cx0 + dpx(170), bottom + dpx(14), cx0 + dpx(330), bottom + dpx(48), L"Use F-key layout", H_KFKEY, 0, 0);
    q_text(cx0 + dpx(346), bottom + dpx(14), cx1, bottom + dpx(48),
           L"Backspace removes  \x2022  Esc cancels", F_SMALL, C_DIM, DT_LEFT);
}

static void setting_row(int x0, int y0, int x1, int ctrl_w, const wchar_t *title, const wchar_t *sub, int kind, int val) {
    int y1 = y0 + dpx(64);
    int hv = kind && hov(kind, val);
    p_rrect((float)x0, (float)y0, (float)x1, (float)y1, dpf(12), 0, hv ? C_HOVER : C_CARD, 1);
    p_rrect((float)x0, (float)y0, (float)x1, (float)y1, dpf(12), 1, C_BORDER, 1);
    p_rrect((float)x0, (float)(y0 + dpx(16)), (float)(x0 + dpx(3)), (float)(y1 - dpx(16)), 0, 0, C_ACCENT, hv ? 1.0f : 0.6f); /* HUD tick */
    tip_row(x0, y0, x1, y1, title);
    q_text(x0 + dpx(18), y0 + dpx(12), x1 - ctrl_w, y0 + dpx(34), title, fitf(title, x1 - ctrl_w - x0 - dpx(20), F_BOLD, F_TEXT, F_SMALL), C_TEXT, DT_LEFT);
    q_text(x0 + dpx(18), y0 + dpx(33), x1 - ctrl_w, y0 + dpx(53), sub, fitf(sub, x1 - ctrl_w - x0 - dpx(20), F_SMALL, F_XS, F_XS), C_MUTED, DT_LEFT);
}

static void key_hint(wchar_t *out, const wchar_t *text, int a) {
    wchar_t k[64];
    if (KB[a].vk) { key_text(KB[a], k); wsprintfW(out, L"%s  \x2022  %s", text, k); }
    else wcscpy(out, text);
}

/* A small picture of the settings window drawn in another theme's colors. */
static void theme_mini(int idx, int x0, int y0, int x1, int y1) {
    Theme keep = g_th, tt;
    COLORREF keep_mid = g_side_mid;
    theme_get(idx, &tt);
    g_th = tt; /* draw with that theme's colors, radius and art */
    g_side_mid = lerpc(tt.side_top, tt.side_bot, 0.25f);
    RECT old = g_clip;
    SetRect(&g_clip, x0, y0, x1, y1);
    IntersectRect(&g_clip, &g_clip, &old);
    int W = x1 - x0, H = y1 - y0, hb = H * 13 / 100, tb = hb + H * 11 / 100, sw = W * 30 / 100;
    p_fill(x0, y0, x1, y1, tt.bg);
    ui_deco(x0, y0 + tb, x1, y1);
    for (int y = y0; y < y0 + hb; y++) p_fill(x0, y, x1, y + 1, lerpc(tt.side_top, tt.side_bot, (float)(y - y0) / hb));
    p_fill(x0, y0 + hb, x1, y0 + tb, lerpc(tt.side_bot, tt.bg, 0.5f));
    p_fill(x0, y0 + tb - 1, x1, y0 + tb, tt.border);
    if (tt.deco == DECO_PATROL) { p_fill(x0, y0, x0 + W / 2, y0 + dpx(2), tt.accent2); p_fill(x0 + W / 2, y0, x1, y0 + dpx(2), tt.accent); }
    p_rrect(x0 + dpf(5), y0 + hb * 0.45f - dpf(1.5f), x0 + W * 0.42f, y0 + hb * 0.45f + dpf(1.5f), dpf(1.5f), 0, tt.text, 0.9f); /* title */
    for (int i = 0; i < 5; i++) { /* tabs */
        float tx = x0 + dpf(5) + i * (W - dpf(10)) / 5.0f, tw = (W - dpf(10)) / 5.0f;
        float ty = y0 + hb + (tb - hb) / 2.0f;
        p_rrect(tx + tw * 0.2f, ty - dpf(1), tx + tw * 0.8f, ty + dpf(1), dpf(1), 0, i == 0 ? tt.text : tt.muted, 0.8f);
        if (!i) p_fill((int)(tx + tw * 0.1f), y0 + tb - dpx(2), (int)(tx + tw * 0.9f), y0 + tb, tt.accent);
    }
    float px0 = (float)(x0 + dpx(5)), px1 = (float)(x0 + sw), py0 = (float)(y0 + tb + dpx(5)), py1 = (float)(y1 - dpx(14));
    p_rrect(px0, py0, px1, py1, dpf(4), 0, lerpc(tt.card, RGB(70, 108, 160), 0.35f), 1); /* preview */
    p_rrect(px0, (float)(y1 - dpx(11)), px1, (float)(y1 - dpx(4)), dpf(3), 0, tt.accent, 1); /* Done */
    float cx0 = (float)(x0 + sw + dpx(6)), cx1 = (float)(x1 - dpx(5));
    p_rrect(cx0, y0 + tb + dpf(6), cx0 + dpf(30), y0 + tb + dpf(10), dpf(2), 0, tt.text, 0.9f); /* heading */
    float ry0 = y0 + tb + dpf(16), ry1 = ry0 + dpf(20);
    p_rrect(cx0, ry0, cx1, ry1, dpf(5), 0, tt.card, 1); /* a setting row */
    p_rrect(cx0, ry0, cx1, ry1, dpf(5), 1, tt.border, 1);
    p_rrect(cx0 + dpf(6), ry0 + dpf(8), cx0 + dpf(30), ry0 + dpf(11), dpf(1.5f), 0, tt.muted, 0.8f);
    p_rrect(cx1 - dpf(22), ry0 + dpf(5), cx1 - dpf(6), ry1 - dpf(5), dpf(5), 0, tt.accent, 1); /* toggle */
    p_circle(cx1 - dpf(11), (ry0 + ry1) / 2, dpf(3), 0, tt.on_accent, 1);
    g_dyn_r = GetRValue(tt.accent); g_dyn_g = GetGValue(tt.accent); g_dyn_b = GetBValue(tt.accent);
    int n = dpx(26);
    p_crosshair((px0 + px1) / 2, (py0 + py1) / 2, n, S.style, -1, n * 0.26f, 2, 1, 1.0f);
    g_clip = old;
    g_th = keep;
    g_side_mid = keep_mid;
}

static void tab_theme(int cx0, int top, int cx1) {
    int cols = 7, gap = dpx(8), tw = (cx1 - cx0 - gap * (cols - 1)) / cols, th = dpx(98);
    for (int slot = 0; slot < NTHEMES; slot++) { /* presets + Custom */
        int i = THEME_ORDER[slot];
        int x = cx0 + (slot % cols) * (tw + gap), y = top + (slot / cols) * (th + gap);
        int sel = i == S.theme, h = hov(H_THEME, i);
        Theme tt;
        theme_get(i, &tt);
        p_rrect((float)x, (float)y, (float)(x + tw), (float)(y + th), dpf(12), 0, h ? C_HOVER : C_CARD, 1);
        if (sel) p_rrect((float)x - dpf(5), (float)y - dpf(5), (float)(x + tw) + dpf(5), (float)(y + th) + dpf(5), dpf(16), 0, C_ACCENT, 0.10f);
        theme_mini(i, x + dpx(6), y + dpx(6), x + tw - dpx(6), y + dpx(62));
        p_rrect((float)x, (float)y, (float)(x + tw), (float)(y + th), dpf(12), sel ? dpf(2) : 1,
                sel ? C_ACCENT : h ? lerpc(C_BORDER, C_TEXT, 0.15f) : C_BORDER, 1);
        q_text(x + dpx(8), y + dpx(64), x + tw - dpx(4), y + dpx(82), tt.name, fitf(tt.name, tw - dpx(12), F_BOLD, F_SMALL, F_XS), sel ? C_ACCLT : C_TEXT, DT_LEFT | DT_END_ELLIPSIS);
        if (sel) q_text(x + dpx(8), y + dpx(80), x + tw - dpx(4), y + dpx(96), L"In use", F_SMALL, C_ACCLT, DT_LEFT);
        if (sel) {
            float bx = (float)(x + tw - dpx(18)), by = (float)(y + dpx(18));
            p_circle(bx, by, dpf(9), 0, C_ACCENT, 1);
            p_line(bx - dpf(3.5f), by, bx - dpf(1), by + dpf(2.8f), dpf(1.6f), C_ONACC, 1);
            p_line(bx - dpf(1), by + dpf(2.8f), bx + dpf(3.8f), by - dpf(2.6f), dpf(1.6f), C_ONACC, 1);
        }
        hit(x, y, x + tw, y + th, H_THEME, i);
    }
    int y = top + 3 * (th + gap) - dpx(2);
    {
        setting_row(cx0, y, cx1, dpx(300), L"Custom colors", L"Pick an accent and a background for the Custom theme", 0, 0);
    }
    static const wchar_t *LBL[2] = {L"Accent", L"Background"};
    COLORREF sw[2] = {g_cust_accent, g_cust_bg};
    for (int k = 0; k < 2; k++) {
        int bx0 = cx1 - dpx(290) + k * dpx(140), bx1 = bx0 + dpx(130), by0 = y + dpx(14), by1 = y + dpx(50);
        int hv = hov(H_THPICK, k);
        p_rrect((float)bx0, (float)by0, (float)bx1, (float)by1, dpf(9), 0, hv ? C_HOVER : C_BG, 1);
        p_rrect((float)bx0, (float)by0, (float)bx1, (float)by1, dpf(9), 1, C_BORDER, 1);
        p_circle((float)(bx0 + dpx(20)), (by0 + by1) / 2.0f, dpf(9), 0, sw[k], 1);
        p_circle((float)(bx0 + dpx(20)), (by0 + by1) / 2.0f, dpf(9), 1, C_TEXT, 0.25f);
        q_text(bx0 + dpx(36), by0, bx1 - dpx(6), by1, LBL[k], F_BOLD, C_TEXT, DT_LEFT);
        hit(bx0, by0, bx1, by1, H_THPICK, k);
    }
    y += dpx(70);
    hit(cx0, y, cx1 - dpx(S.theme == TH_CRIMINAL ? 510 : 320), y + dpx(64), H_TOGGLE, T_SPLASH);
    if (S.theme == TH_CRIMINAL) {
        static const wchar_t *CI[] = {L"Ranks", L"Wanted"};
        setting_row(cx0, y, cx1, dpx(510), L"Loading screen", L"Crew ranks or wanted level", H_TOGGLE, T_SPLASH);
        draw_segmented(cx1 - dpx(488), y + dpx(15), cx1 - dpx(318), y + dpx(49), CI, 2, g_crim_intro, H_CRIMINTRO);
    } else
        setting_row(cx0, y, cx1, dpx(320), L"Loading screen", S.splash ? L"Plays when the app starts, in your theme" : L"Off \x2014 you can still preview it", H_TOGGLE, T_SPLASH);
    draw_button(cx1 - dpx(300), y + dpx(14), cx1 - dpx(84), y + dpx(50), L"Preview loading screen", H_SPLASHPREV, 0, 1);
    draw_toggle(cx1 - dpx(18), y + dpx(32), S.splash, H_TOGGLE, T_SPLASH);
    y += dpx(70);
    { /* loading music: song file + start time */
        wchar_t sub2[MAX_PATH + 40], st[16];
        const wchar_t *nm = g_music[0] ? wcsrchr(g_music, L'\\') : NULL;
        nm = nm ? nm + 1 : g_music;
        if (g_music[0]) wsprintfW(sub2, L"%s  \x2022  starts at %d:%02d", nm, g_music_start / 60, g_music_start % 60);
        else wcscpy(sub2, L"Off \x2014 pick a song file on your PC to play during the intro");
        setting_row(cx0, y, cx1, dpx(370), L"Loading music", sub2, 0, 0);
        int bx = cx1 - dpx(356), by0 = y + dpx(14), by1 = y + dpx(50);
        if (g_music[0]) {
            draw_button(bx, by0, bx + dpx(34), by1, L"\x2212", H_MUSIC, 1, 0);           /* start earlier */
            wsprintfW(st, L"%d:%02d", g_music_start / 60, g_music_start % 60);
            q_text(bx + dpx(34), by0, bx + dpx(90), by1, st, F_BOLD, C_TEXT, DT_CENTER);
            draw_button(bx + dpx(90), by0, bx + dpx(124), by1, L"+", H_MUSIC, 2, 0);       /* start later */
        }
        draw_button(cx1 - dpx(222), by0, cx1 - dpx(70), by1, g_music[0] ? L"Change song..." : L"Choose song...", H_MUSIC, 0, g_music[0] ? 0 : 1);
        if (g_music[0]) draw_button(cx1 - dpx(62), by0, cx1 - dpx(16), by1, L"Off", H_MUSIC, 3, 2); /* turn off */
    }
}

static int refresh_rate(void);
static void tab_perf(int cx0, int top, int cx1) {
    wchar_t sub[200];
    int y = top;
    tip_row(cx0, y, cx1, y + dpx(90), L"Roblox FPS cap");
    q_text(cx0, y, cx1, y + dpx(22), L"Roblox FPS cap", F_TEXT, C_TEXT, DT_LEFT);
    wsprintfW(sub, L"Your monitor runs at %d Hz. Pick that (or higher) to stop Roblox holding you at 60.", refresh_rate());
    q_text(cx0, y + dpx(21), cx1, y + dpx(40), sub, F_SMALL, C_MUTED, DT_LEFT);
    draw_segmented(cx0, y + dpx(48), cx1, y + dpx(84), FPS_LABELS, NFPS, g_fps, H_FPS);
    y += dpx(106);
    tip_row(cx0, y, cx1, y + dpx(90), L"Performance boost");
    q_text(cx0, y, cx1, y + dpx(22), L"Performance boost", F_TEXT, C_TEXT, DT_LEFT);
    q_text(cx0, y + dpx(21), cx1, y + dpx(40), L"Uses graphics settings Roblox officially allows \x2014 less detail, more FPS", F_SMALL, C_MUTED, DT_LEFT);
    static const wchar_t *BOOST[] = {L"Off", L"Balanced", L"Max FPS"};
    draw_segmented(cx0, y + dpx(48), cx0 + dpx(400), y + dpx(84), BOOST, 3, g_boost, H_BOOST);
    static const wchar_t *BDESC[] = {
        L"Roblox's normal graphics.",
        L"No anti-aliasing and no grass. Looks almost the same, runs smoother.",
        L"Balanced + lowest graphics level and textures, and simpler far-away objects. Biggest FPS gain, looks plain."};
    q_text(cx0, y + dpx(92), cx1, y + dpx(132), BDESC[g_boost], F_SMALL, C_DIM, DT_LEFT | DT_WORDBREAK);
    y += dpx(144);
    hit(cx0, y, cx1, y + dpx(64), H_TOGGLE, T_GPU);
    setting_row(cx0, y, cx1, dpx(90), L"Use high-performance graphics card",
                L"For laptops with two GPUs: tells Windows to run Roblox on the faster one", H_TOGGLE, T_GPU);
    draw_toggle(cx1 - dpx(18), y + dpx(32), g_gpu, H_TOGGLE, T_GPU);
    y += dpx(80);
    draw_button(cx0, y, cx0 + dpx(160), y + dpx(40), L"Apply now", H_PERFBTN, 0, 1);
    draw_button(cx0 + dpx(172), y, cx0 + dpx(372), y + dpx(40), L"Undo all FPS changes", H_PERFBTN, 1, 2);
    y += dpx(56);
    static const wchar_t *NOTE[] = {
        L"Changes take effect the next time Roblox starts.",
        L"The FPS cap is only written while Roblox is closed \x2014 if it's open, the app sets it the moment you close it.",
        L"240 is the highest cap Roblox supports. Stuck on a white screen? Close Roblox and click Undo all FPS changes.",
        L"Only Roblox's own settings files are changed \x2014 never the game itself, so it's safe with Roblox's anti-cheat."};
    for (int i = 0; i < 4; i++) q_text(cx0, y + i * dpx(19), cx1, y + (i + 1) * dpx(19), NOTE[i], F_SMALL, C_DIM, DT_LEFT);
}

/* Ask once before the app first changes Roblox's own settings. 1 = go ahead. */
static int roblox_change_ok(void) {
    if (ini_get(L"roblox_changes_ok", 0)) return 1;
    int a = MessageBoxW(g_keydlg,
        L"This changes Roblox's own settings files (its frame-rate setting and graphics options). "
        L"It never touches the game itself, so it's safe with Roblox's anti-cheat.\n\n"
        L"If Roblox ever acts strange after this (white screen, crashes, odd graphics): close Roblox, then "
        L"FPS Boost tab > Undo all FPS changes puts everything back.\n\nContinue?",
        L"Crosshair Overlay - FPS Boost", MB_YESNO | MB_ICONINFORMATION);
    if (a != IDYES) return 0;
    ini_set(L"roblox_changes_ok", 1);
    return 1;
}

static void perf_report(int fps_res) {
    wchar_t m[300], a[140] = L"", b[140] = L"";
    if (g_fps > 0) {
        if (fps_res == FPS_DONE || fps_res == FPS_SAME)
            wsprintfW(a, L"FPS cap set to %d.", FPS_CAPS[g_fps]);
        else if (fps_res == FPS_RUNNING) wcscpy(a, L"Roblox is open \x2014 the FPS cap is set the moment you close it.");
        else if (fps_res == FPS_NOFILE) wcscpy(a, L"Open Roblox once (so it makes its settings file), close it, then try again.");
        else wcscpy(a, L"Couldn't write Roblox's settings file.");
    }
    if (g_boost || g_gpu) {
        if (!g_perf_installs) wcscpy(b, L"No Roblox install found for the boost.");
        else if (g_boost_fail) wsprintfW(b, L"Boost failed in %d place(s) \x2014 try Run as administrator.", g_boost_fail);
        else wsprintfW(b, L"Boost applied to %d Roblox install(s).", g_perf_installs);
    }
    wsprintfW(m, L"%s %s %s", a, b, (a[0] || b[0]) ? L"Restart Roblox to use it." : L"Roblox FPS changes are off.");
    ui_set_status(m);
}

/* Opt-in anonymous usage stats: one short line a week (and after an update) so simplysoul can see which versions,
   themes and settings people use. No name, no files, no IP kept by the app; the id is a scrambled version of the install id. */
static void stats_maybe_send(int force) {
    if (!g_stats || !stats_target()[0] || !g_ini[0]) return;
    wchar_t lastv[32] = L"";
    GetPrivateProfileStringW(L"crosshair", L"stats_ver", L"", lastv, 32, g_ini);
    int last = GetPrivateProfileIntW(L"crosshair", L"stats_day", 0, g_ini), today = day_number();
    if (!force && !wcscmp(lastv, VER_WSTR) && today - last < 7) return;
    wchar_t wv[96], th[64] = L"Custom", line[900];
    win_version(wv);
    if (S.theme >= 0 && S.theme != TH_CUSTOM) { Theme t; theme_get(S.theme, &t); lstrcpynW(th, t.name, 64); }
    unsigned hsh = 2166136261u;
    for (const wchar_t *c = L"stats:"; *c; c++) hsh = (hsh ^ *c) * 16777619u;
    for (const wchar_t *c = g_report_id; *c; c++) hsh = (hsh ^ *c) * 16777619u;
    static const wchar_t *BOOST[] = {L"off", L"balanced", L"max FPS"};
    wsprintfW(line, L"\xD83D\xDCCA **Usage** \x2022 v" VER_WSTR L"%s \x2022 %s \x2022 %dx%d @ %d Hz \x2022 theme %s \x2022 %s \x2022 "
                    L"panel %d%% \x2022 FPS cap %s, boost %s \x2022 %s \x2022 hide from recordings %s \x2022 user `%06x`",
              g_is_tester ? L" (tester)" : L"", wcsstr(wv, L"Windows 11") ? L"Windows 11" : wcsstr(wv, L"Windows 10") ? L"Windows 10" : L"older Windows",
              GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), refresh_rate(), th, STYLE_NAMES[S.style],
              UI_SIZES[g_ui_size], FPS_LABELS[g_fps], BOOST[g_boost], g_onlyrbx ? L"Roblox only" : L"always shown",
              g_nocap ? L"on" : L"off", hsh & 0xffffff);
    char u[1400], payload[2000];
    to_utf8(line, u, sizeof(u));
    strcpy(payload, "{\"username\":\"Crosshair Overlay stats\",\"content\":\"");
    json_cat(payload, sizeof(payload) - 60, u);
    strcat(payload, "\",\"allowed_mentions\":{\"parse\":[]}}");
    if (webhook_post_json(stats_target(), payload)) {
        wchar_t d[16];
        wsprintfW(d, L"%d", today);
        WritePrivateProfileStringW(L"crosshair", L"stats_day", d, g_ini);
        WritePrivateProfileStringW(L"crosshair", L"stats_ver", VER_WSTR, g_ini);
        log_info(L"usage stats sent");
    }
}

/* The fields every report card shows: version, PC, Roblox, crosshair and setup. */
static void report_fields(char *out, size_t cap) {
    wchar_t v[300], wv[96], th[64] = L"Custom";
    win_version(wv);
    wsprintfW(v, L"v" VER_WSTR L"%s", g_is_tester ? L" (Q/A tester)" : L"");
    json_field(out, cap, "Version", v, 1);
    json_field(out, cap, "Windows", wv, 1);
    wsprintfW(v, L"%dx%d @ %d Hz%s", GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), refresh_rate(),
              GetSystemMetrics(SM_CMONITORS) > 1 ? L", 2+ monitors" : L"");
    json_field(out, cap, "Screen", v, 1);
    static const wchar_t *BOOST[] = {L"off", L"balanced", L"max FPS"};
    wsprintfW(v, L"%s \x2022 FPS cap %s \x2022 boost %s", roblox_running() ? L"open" : L"closed", FPS_LABELS[g_fps], BOOST[g_boost]);
    json_field(out, cap, "Roblox", v, 1);
    if (S.theme >= 0 && S.theme != TH_CUSTOM) { Theme t; theme_get(S.theme, &t); lstrcpynW(th, t.name, 64); }
    wsprintfW(v, L"%s, %s, %d px \x2022 theme %s", STYLE_NAMES[S.style], COLORS[S.color].name, SIZES[S.size], th);
    json_field(out, cap, "Crosshair", v, 1);
    wsprintfW(v, L"%s \x2022 %s \x2022 cursor %s \x2022 recordings %s", S.hidden ? L"hidden" : g_onlyrbx ? L"Roblox only" : L"always shown",
              S.center ? L"centered" : L"follows mouse", S.hidecur ? L"hidden" : L"shown", g_nocap ? L"hidden" : L"visible");
    json_field(out, cap, "Setup", v, 1);
}

/* ---- Copy debug info: a short block for Discord ---- */
static wchar_t *debug_info_text(void) {
    wchar_t *t = (wchar_t *)malloc(4096 * sizeof(wchar_t)), wv[96], th[64] = L"Custom", last[300], l2[96] = L"";
    if (!t) return NULL;
    win_version(wv);
    if (S.theme >= 0 && S.theme != TH_CUSTOM) { Theme tt; theme_get(S.theme, &tt); lstrcpynW(th, tt.name, 64); }
    static const wchar_t *BOOST[] = {L"off", L"balanced", L"max FPS"};
    if (S.l2_style >= 0) wsprintfW(l2, L" + layer %s, %s, %d px", STYLE_NAMES[S.l2_style], COLORS[S.l2_color].name, SIZES[S.l2_size]);
    log_last(L"CRASH:", last, 300);
    if (!last[0]) log_last(L"  ERROR  ", last, 300);
    wsprintfW(t,
              L"```\r\nCrosshair Overlay v" VER_WSTR L"%s \x2022 id %s\r\n"
              L"%s \x2022 %dx%d @ %d Hz \x2022 %d monitor(s)\r\n"
              L"Roblox: %s \x2022 FPS cap %s \x2022 boost %s\r\n"
              L"Crosshair: %s, %s, %d px, thickness %d%s \x2022 theme %s\r\n"
              L"Setup: %s \x2022 %s \x2022 cursor %s \x2022 recordings %s \x2022 auto-contrast %s \x2022 hide in menus %s\r\n"
              L"Last problem: %s\r\n```",
              g_is_tester ? L" (Q/A tester)" : L"", g_report_id, wv, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), refresh_rate(),
              GetSystemMetrics(SM_CMONITORS), roblox_running() ? L"open" : L"closed", FPS_LABELS[g_fps], BOOST[g_boost],
              STYLE_NAMES[S.style], COLORS[S.color].name, SIZES[S.size], S.thick, l2, th,
              S.hidden ? L"hidden" : g_onlyrbx ? L"Roblox only" : L"always shown", S.center ? L"centered" : L"follows mouse",
              S.hidecur ? L"hidden" : L"shown", g_nocap ? L"hidden" : L"visible", S.auto_contrast ? L"on" : L"off", S.menu_hide ? L"on" : L"off",
              last[0] ? last : L"none");
    return t;
}

/* ---- Backup / restore: every setting and profile in one file ---- */
static int pc_only_key(const wchar_t *k) { /* things that belong to this PC, not to the player's setup */
    static const wchar_t *K[] = {L"report_id", L"was_tester", L"tester_ok", L"news_seen", L"news_dismissed", L"stats_day", L"stats_ver",
                                 L"skip_update", L"tour_done", L"nudge_v", L"retired_flags_cleaned", L"last_tab", L"music_path"};
    for (int i = 0; i < (int)(sizeof(K) / sizeof(K[0])); i++) if (!_wcsicmp(k, K[i])) return 1;
    return 0;
}
static void settings_export(HWND owner) {
    wchar_t file[MAX_PATH] = L"CrosshairOverlay-settings.ini";
    OPENFILENAMEW of = {0};
    of.lStructSize = sizeof(of);
    of.hwndOwner = owner;
    of.lpstrFilter = L"Crosshair Overlay backup (*.ini)\0*.ini\0";
    of.lpstrFile = file;
    of.nMaxFile = MAX_PATH;
    of.lpstrDefExt = L"ini";
    of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    save_settings();
#ifdef CH_TEST
    if (GetEnvironmentVariableW(L"CH_EXPORT_FILE", file, MAX_PATH)) {} else
#endif
    if (!GetSaveFileNameW(&of)) return;
    static wchar_t sec[32768];
    DWORD n = GetPrivateProfileSectionW(L"crosshair", sec, 32768, g_ini);
    HANDLE f = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) { ui_set_status(L"Couldn't save the backup there. Try another folder."); return; }
    DWORD w;
    wchar_t head[200];
    SYSTEMTIME st;
    GetLocalTime(&st);
    wsprintfW(head, L"\xFEFF; Crosshair Overlay settings backup (v" VER_WSTR L", %04d-%02d-%02d). Import it from the Profiles tab.\r\n[crosshair]\r\n",
              st.wYear, st.wMonth, st.wDay);
    WriteFile(f, head, lstrlenW(head) * sizeof(wchar_t), &w, NULL);
    int keys = 0;
    for (wchar_t *e = sec; e < sec + n && *e; e += lstrlenW(e) + 1) {
        wchar_t k[64];
        const wchar_t *eq = wcschr(e, L'=');
        if (!eq || eq - e >= 63) continue;
        lstrcpynW(k, e, (int)(eq - e) + 1);
        if (pc_only_key(k)) continue;
        WriteFile(f, e, lstrlenW(e) * sizeof(wchar_t), &w, NULL);
        WriteFile(f, L"\r\n", 2 * sizeof(wchar_t), &w, NULL);
        keys++;
    }
    CloseHandle(f);
    log_info(L"settings exported (%d settings)", keys);
    crumb(L"exported settings");
    ui_set_status(L"Backup saved. Import it on any PC from this tab.");
}
static void settings_import(HWND owner) {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW of = {0};
    of.lStructSize = sizeof(of);
    of.hwndOwner = owner;
    of.lpstrFilter = L"Crosshair Overlay backup (*.ini)\0*.ini\0All files\0*.*\0";
    of.lpstrFile = file;
    of.nMaxFile = MAX_PATH;
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
#ifdef CH_TEST
    if (GetEnvironmentVariableW(L"CH_IMPORT_FILE", file, MAX_PATH)) {} else
#endif
    if (!GetOpenFileNameW(&of)) return;
    static wchar_t sec[32768];
    DWORD n = GetPrivateProfileSectionW(L"crosshair", sec, 32768, file);
    int ok = 0;
    for (wchar_t *e = sec; e < sec + n && *e; e += lstrlenW(e) + 1) if (!wcsncmp(e, L"style=", 6)) ok = 1;
    if (!ok) { ui_set_status(L"That file isn't a Crosshair Overlay backup."); return; }
    if (
#ifdef CH_TEST
        !GetEnvironmentVariableW(L"CH_IMPORT_YES", NULL, 0) &&
#endif
        MessageBoxW(owner, L"Replace your crosshair, profiles, keybinds and settings with the ones in this backup?",
                    L"Import settings", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    int keys = 0;
    for (wchar_t *e = sec; e < sec + n && *e; e += lstrlenW(e) + 1) {
        wchar_t k[64];
        const wchar_t *eq = wcschr(e, L'=');
        if (!eq || eq - e >= 63) continue;
        lstrcpynW(k, e, (int)(eq - e) + 1);
        if (pc_only_key(k)) continue;
        WritePrivateProfileStringW(L"crosshair", k, eq + 1, g_ini);
        keys++;
    }
    /* use them right away */
    load_settings();
    load_profiles();
    load_binds();
    register_hotkeys(g_host);
    if (S.theme != -1) theme_set(S.theme);
    g_want_recent = 1;
    apply();
    fx_timer_sync();
    scene_timer_sync();
    log_info(L"settings imported (%d settings)", keys);
    crumb(L"imported settings");
    ui_set_status(L"Settings imported \x2014 everything from the backup is in use now.");
    ui_redraw();
}

static DWORD WINAPI stats_thread(LPVOID p) { (void)p; stats_maybe_send(1); return 0; }

static void tab_general(int cx0, int top, int cx1) {
    wchar_t sub[200];
    int y = top, step = dpx(66);
    static const wchar_t *MODES[] = {L"Follow mouse", L"Center"};
    key_hint(sub, L"Follow your mouse, or stay in the middle of the game", A_MODE);
    setting_row(cx0, y, cx1, dpx(290), L"Crosshair position", sub, 0, 0);
    draw_segmented(cx1 - dpx(262), y + dpx(15), cx1 - dpx(16), y + dpx(49), MODES, 2, S.center, H_MODE);
    y += step;
    key_hint(sub, g_onlyrbx ? L"Only while Roblox is in front \x2014 hides when you alt-tab" : L"Turn the overlay on or off", A_HIDE);
    hit(cx0, y, cx1 - dpx(330), y + dpx(64), H_TOGGLE, T_SHOW);
    setting_row(cx0, y, cx1, dpx(330), L"Show crosshair", sub, H_TOGGLE, T_SHOW);
    {
        static const wchar_t *WHERE[] = {L"Always", L"In Roblox only"};
        draw_segmented(cx1 - dpx(318), y + dpx(15), cx1 - dpx(84), y + dpx(49), WHERE, 2, g_onlyrbx, H_ONLYRBX);
    }
    draw_toggle(cx1 - dpx(18), y + dpx(32), !S.hidden, H_TOGGLE, T_SHOW);
    y += step;
    key_hint(sub, L"Hides the pointer and Roblox's built-in gun crosshair", A_CURSOR);
    hit(cx0, y, cx1, y + dpx(64), H_TOGGLE, T_HIDECUR);
    setting_row(cx0, y, cx1, dpx(90), L"Hide mouse cursor", sub, H_TOGGLE, T_HIDECUR);
    draw_toggle(cx1 - dpx(18), y + dpx(32), S.hidecur, H_TOGGLE, T_HIDECUR);
    y += step;
    hit(cx0, y, cx1, y + dpx(64), H_TOGGLE, T_AUTOSTART);
    setting_row(cx0, y, cx1, dpx(90), L"Start with Windows", L"Starts quietly (no intro, no panel) when you sign in", H_TOGGLE, T_AUTOSTART);
    draw_toggle(cx1 - dpx(18), y + dpx(32), autostart_on(), H_TOGGLE, T_AUTOSTART);
    y += step;
    hit(cx0, y, cx1, y + dpx(64), H_TOGGLE, T_OPENSTART);
    setting_row(cx0, y, cx1, dpx(90), L"Open this panel on startup", L"Turn off to start straight in the game", H_TOGGLE, T_OPENSTART);
    draw_toggle(cx1 - dpx(18), y + dpx(32), S.open_start, H_TOGGLE, T_OPENSTART);
    y += step;
    { /* bug reports */
        const wchar_t *sub3 = webhook_set() ? L"Send app + Roblox crash reports to simplysoul automatically"
                                            : L"The app keeps a log; save a report file to send to simplysoul";
        if (webhook_set()) hit(cx0, y, cx1 - dpx(250), y + dpx(64), H_TOGGLE, T_REPORTS);
        setting_row(cx0, y, cx1, dpx(290), L"Bug reports", sub3, webhook_set() ? H_TOGGLE : 0, T_REPORTS);
        int bx1 = webhook_set() ? cx1 - dpx(84) : cx1 - dpx(16);
        draw_button(bx1 - dpx(170), y + dpx(14), bx1, y + dpx(50), L"Report a problem...", H_BUTTON, B_REPORT, 0);
        if (webhook_set()) draw_toggle(cx1 - dpx(18), y + dpx(32), g_reports == 1, H_TOGGLE, T_REPORTS);
    }
    y += step;
    if (stats_target()[0]) { /* opt-in usage stats */
        hit(cx0, y, cx1, y + dpx(64), H_TOGGLE, T_STATS);
        setting_row(cx0, y, cx1, dpx(90), L"Share anonymous usage stats",
                    L"Once a week: version, Windows, screen, theme & settings. No name, no files.", H_TOGGLE, T_STATS);
        draw_toggle(cx1 - dpx(18), y + dpx(32), g_stats, H_TOGGLE, T_STATS);
        y += step;
    }
    y += dpx(4);
    draw_button(cx0, y, cx0 + dpx(212), y + dpx(40), L"Show keybinds on screen", H_BUTTON, B_SHOWKEYS, 0);
    draw_button(cx0 + dpx(224), y, cx0 + dpx(424), y + dpx(40), L"Roblox cursor status", H_BUTTON, B_RBX, 0);
    draw_button(cx1 - dpx(130), y, cx1, y + dpx(40), L"Exit app", H_BUTTON, B_EXIT, 2);
}

/* The changelog built into the exe (changelog.txt, resource 13): "vX.Y.Z" lines, then one change per line. */
static wchar_t *changelog_text(void) {
    static wchar_t *w;
    if (w) return w;
    HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(13), (LPCWSTR)RT_RCDATA);
    HGLOBAL g = r ? LoadResource(NULL, r) : NULL;
    DWORD sz = r ? SizeofResource(NULL, r) : 0;
    const char *d = g ? (const char *)LockResource(g) : NULL;
    if (!d || !sz) { w = L""; return w; }
    if (sz >= 3 && (BYTE)d[0] == 0xEF && (BYTE)d[1] == 0xBB && (BYTE)d[2] == 0xBF) { d += 3; sz -= 3; }
    int n = MultiByteToWideChar(CP_UTF8, 0, d, (int)sz, NULL, 0);
    w = (wchar_t *)calloc(n + 1, sizeof(wchar_t));
    if (!w) { w = L""; return w; }
    MultiByteToWideChar(CP_UTF8, 0, d, (int)sz, w, n);
    return w;
}

static void tab_updates(int cx0, int top, int cx1, int bottom) {
    int y = top;
    { /* version + updates */
        wchar_t sub4[160];
        int st = (int)g_upd_state;
        if (!updates_set()) wcscpy(sub4, L"Automatic updates aren't set up in this build");
        else if (st == UPD_READY) wsprintfW(sub4, L"v%s is downloaded and ready \x2014 restart to update", g_upd_ver);
        else if (st == UPD_CHECKING) wcscpy(sub4, L"Checking for updates...");
        else if (st == UPD_LATEST) wcscpy(sub4, g_is_tester ? L"Q/A tester \x2022 you're on the newest build" : L"You're on the newest version");
        else wcscpy(sub4, g_autoupd ? L"Updates download and install by themselves" : L"Auto-install off \x2014 you'll be told when there's an update");
        if (updates_set()) hit(cx0, y, cx1 - dpx(250), y + dpx(64), H_TOGGLE, T_AUTOUPD);
        setting_row(cx0, y, cx1, dpx(440), L"Crosshair Overlay v" VER_WSTR, sub4, updates_set() ? H_TOGGLE : 0, T_AUTOUPD);
        draw_button(cx1 - dpx(84) - dpx(170) - dpx(150), y + dpx(14), cx1 - dpx(84) - dpx(178), y + dpx(50), L"Copy debug info", H_BUTTON, B_DEBUG, 0);
        if (updates_set()) {
            int bx1 = cx1 - dpx(84);
            draw_button(bx1 - dpx(170), y + dpx(14), bx1, y + dpx(50), st == UPD_READY ? L"Restart to update" : L"Check now",
                        H_BUTTON, B_UPDATE, st == UPD_READY ? 1 : 0);
            draw_toggle(cx1 - dpx(18), y + dpx(32), g_autoupd, H_TOGGLE, T_AUTOUPD);
        }
    }
    if (g_is_tester) { /* Q/A tester tools */
        y += dpx(72);
        int sent = !wcscmp(g_tester_ok, VER_WSTR);
        const wchar_t *ts = !g_tbuilds ? L"Test builds paused \x2014 you get the normal releases"
                          : sent ? L"Thanks! You said v" VER_WSTR L" looks good"
                                 : L"How's v" VER_WSTR L"? Let simplysoul know";
        p_rrect((float)cx0, (float)y, (float)cx1, (float)(y + dpx(64)), dpf(12), 0, lerpc(C_CARD, C_ACCENT, 0.10f), 1);
        setting_row(cx0, y, cx1, dpx(420), L"Q/A tester", ts, 0, 0);
        int bx1 = cx1 - dpx(84);
        draw_button(bx1 - dpx(124), y + dpx(14), bx1, y + dpx(50), L"Problem...", H_BUTTON, B_TESTBAD, 0);
        draw_button(bx1 - dpx(256), y + dpx(14), bx1 - dpx(132), y + dpx(50), sent ? L"Sent \x2713" : L"Looks good", H_BUTTON, B_TESTOK, sent ? 0 : 1);
        draw_toggle(cx1 - dpx(18), y + dpx(32), g_tbuilds, H_TOGGLE, T_TBUILDS);
        q_text_ex(cx1 - dpx(78), y + dpx(48), cx1, y + dpx(62), L"TEST BUILDS", F_TINY, C_DIM, DT_CENTER, 0);
    }
    y += dpx(82);
    q_text_ex(cx0 + dpx(2), y, cx1, y + dpx(20), L"WHAT'S CHANGED", F_TINY, C_DIM, DT_LEFT, 1);
    y += dpx(26);
    /* scrolling list of versions */
    RECT old = g_clip;
    int ltop = y, lx1 = cx1 - dpx(14);
    SetRect(&g_clip, cx0 - dpx(4), ltop, cx1, bottom);
    IntersectRect(&g_clip, &g_clip, &old);
    y = ltop - g_uscroll;
    const wchar_t *p = changelog_text();
    int cardy0 = -1;
    wchar_t line[200];
    while (*p) {
        const wchar_t *e = p;
        while (*e && *e != L'\n') e++;
        int L = (int)(e - p);
        while (L > 0 && (p[L - 1] == L'\r' || p[L - 1] == L' ')) L--;
        if (L > 199) L = 199;
        wcsncpy(line, p, L);
        line[L] = 0;
        p = *e ? e + 1 : e;
        if (!L) continue;
        if (line[0] == L'v' && iswdigit(line[1])) { /* a release: "v1.15  Title" (patches are folded into it) */
            if (cardy0 >= 0) y += dpx(14);
            wchar_t ver[24], mm[24];
            const wchar_t *title = line;
            int vl = 0;
            while (line[vl] && line[vl] != L' ' && vl < 23) { ver[vl] = line[vl]; vl++; }
            ver[vl] = 0;
            title = line + vl;
            while (*title == L' ') title++;
            wsprintfW(mm, L"v%d.%d", VER_MAJOR, VER_MINOR);
            int cur = !wcscmp(ver, mm) || !wcscmp(ver + 1, VER_WSTR);
            if (y + dpx(30) > ltop && y < bottom) {
                SIZE z = {0};
                HGDIOBJ of = SelectObject(g_paint_dc, F_HUD);
                GetTextExtentPoint32W(g_paint_dc, ver, lstrlenW(ver), &z);
                SelectObject(g_paint_dc, of);
                q_text(cx0 + dpx(4), y, lx1, y + dpx(28), ver, F_HUD, cur ? C_ACCLT : C_TEXT, DT_LEFT);
                if (title[0]) q_text(cx0 + dpx(14) + z.cx, y + dpx(2), lx1 - dpx(150), y + dpx(28), title, F_BOLD, cur ? C_TEXT : C_MUTED, DT_LEFT);
                if (cur) {
                    int bx1 = lx1 - dpx(10), bx0 = bx1 - dpx(132);
                    p_rrect((float)bx0, (float)(y + dpx(4)), (float)bx1, (float)(y + dpx(24)), dpf(10), 0, C_ACCENT, 0.16f);
                    p_rrect((float)bx0, (float)(y + dpx(4)), (float)bx1, (float)(y + dpx(24)), dpf(10), 1, C_ACCENT, 0.8f);
                    q_text(bx0, y + dpx(4), bx1, y + dpx(24), L"you have v" VER_WSTR, F_SMALL, C_ACCLT, DT_CENTER);
                }
            }
            y += dpx(30);
            cardy0 = y;
        } else {
            if (y + dpx(24) > ltop && y < bottom) {
                p_circle((float)(cx0 + dpx(12)), (float)(y + dpx(11)), dpf(2.4f), 0, C_ACCENT, 1);
                q_text(cx0 + dpx(24), y, lx1, y + dpx(22), line, F_TEXT, C_MUTED, DT_LEFT | DT_END_ELLIPSIS);
            }
            y += dpx(24);
        }
    }
    int total = y + g_uscroll - ltop, view = bottom - ltop;
    g_uscroll_max = total > view ? total - view : 0;
    if (g_uscroll > g_uscroll_max) g_uscroll = g_uscroll_max;
    if (g_uscroll_max > 0) {
        float th = (float)view * view / total, ty = ltop + (view - th) * g_uscroll / (float)g_uscroll_max;
        p_rrect((float)(cx1 - dpx(5)), ty, (float)(cx1 - dpx(1)), ty + th, dpf(2), 0, C_TRACK, 1);
    }
    g_clip = old;
    /* footer */
    draw_button(cx0, bottom + dpx(14), cx0 + dpx(200), bottom + dpx(52), L"Remove everything...", H_BUTTON, B_UNINSTALL, 2);
    q_text(cx0 + dpx(214), bottom + dpx(14), cx1, bottom + dpx(52), L"Undo everything this app changed, before you delete it", F_SMALL, C_DIM, DT_LEFT);
}

/* The biggest of the given fonts that fits the text into maxw (so labels never get cut off). */
static HFONT fit_font(HDC dc, const wchar_t *t, int spacing, int maxw, HFONT a, HFONT b, HFONT c) {
    HFONT f[3] = {a, b, c};
    HGDIOBJ of = SelectObject(dc, a);
    HFONT best = c;
    SetTextCharacterExtra(dc, spacing);
    for (int i = 0; i < 3; i++) {
        SIZE z = {0};
        SelectObject(dc, f[i]);
        GetTextExtentPoint32W(dc, t, lstrlenW(t), &z);
        if (z.cx <= maxw) { best = f[i]; break; }
    }
    SetTextCharacterExtra(dc, 0);
    SelectObject(dc, of);
    return best;
}

static void ui_paint(HWND h) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT cr;
    GetClientRect(h, &cr);
    int W = cr.right, H = cr.bottom;
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -H;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void *bits = NULL;
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp || !bits || W <= 0 || H <= 0) { if (bmp) DeleteObject(bmp); DeleteDC(mem); EndPaint(h, &ps); return; }
    HGDIOBJ ob = SelectObject(mem, bmp);
    canvas_set((uint32_t *)bits, W, H);
    g_nhits = 0;
    g_ntq = 0;

    /* ---- Gaming HUD layout: header + top tab bar, live preview column on the left, content on the right ---- */
    int SW = dpx(SIDE_W), HB = dpx(UI_HEAD), BAR = HB + dpx(UI_TABS);
    g_sharp = 0.7f;
    g_paint_dc = mem;
    g_nrows = 0;
    { static int lt = -1; if (g_tab != lt) { if (lt >= 0) crumb(L"%s tab", TAB_NAMES[g_tab]); lt = g_tab; } }
    p_fill(0, 0, W, H, C_BG);
    ui_deco(0, BAR, W, H); /* the theme's background art */
    for (int y = 0; y < HB; y++) p_fill(0, y, W, y + 1, lerpc(g_th.side_top, g_th.side_bot, (float)y / HB));
    p_glow(dpf(30), dpf(24), dpf(170), g_th.deco == DECO_PATROL ? C_ACC2 : C_ACCENT, 0.16f);
    for (int y = HB; y < BAR; y++) p_fill(0, y, W, y + 1, lerpc(g_th.side_bot, C_BG, 0.35f + 0.5f * (y - HB) / (float)(BAR - HB)));
    p_fill(0, HB - 1, W, HB, lerpc(C_BORDER, C_BG, 0.35f));
    p_fill(0, BAR - 1, W, BAR, C_BORDER);
    if (g_th.deco == DECO_PATROL) { /* red/blue lightbar */
        p_fill(0, 0, W / 2, dpx(3), C_ACC2);
        p_fill(W / 2, 0, W, dpx(3), C_ACCENT);
    } else p_rrect(0, 0, (float)W, dpf(2), 0, 0, C_ACCENT, 0.85f);

    /* brand */
    {
        float lx0 = dpf(16), ly0 = (HB - dpf(30)) / 2, L = dpf(30);
        p_rrect(lx0, ly0, lx0 + L, ly0 + L, dpf(4), 0, C_CARD, 1);
        p_rrect(lx0, ly0, lx0 + L, ly0 + L, dpf(4), 1, lerpc(C_BORDER, C_ACCENT, 0.5f), 1);
        p_crosshair(lx0 + L / 2, ly0 + L / 2, dpx(28), S.style, S.color, dpx(28) * 0.27f, 2, 1, 1.0f);
        q_text_ex(dpx(58), dpx(6), W - dpx(260), dpx(29), L"CROSSHAIR OVERLAY", fit_font(mem, L"CROSSHAIR OVERLAY", 1, W - dpx(320), F_HUD, F_TAB, F_HUDS), C_TEXT, DT_LEFT, 1);
        q_text(dpx(59), dpx(27), dpx(400), dpx(44), L"v" VER_WSTR L"  \x2022  made by simplysoul", F_SMALL, C_MUTED, DT_LEFT);
        if (g_is_tester) { /* Q/A badge: a small outlined tag after the title */
            HFONT tf = fit_font(mem, L"CROSSHAIR OVERLAY", 1, W - dpx(320), F_HUD, F_TAB, F_HUDS);
            SIZE z = {0}, bz = {0};
            HGDIOBJ of = SelectObject(mem, tf);
            SetTextCharacterExtra(mem, 1);
            GetTextExtentPoint32W(mem, L"CROSSHAIR OVERLAY", 17, &z);
            const wchar_t *bt = g_tbuilds ? L"Q/A TESTER" : L"TESTER (PAUSED)";
            SelectObject(mem, F_TINY);
            GetTextExtentPoint32W(mem, bt, lstrlenW(bt), &bz);
            SetTextCharacterExtra(mem, 0);
            SelectObject(mem, of);
            int bh = dpx(18), bx = dpx(58) + z.cx + dpx(14), by = dpx(17) - bh / 2 + dpx(1);
            int bw = bz.cx + dpx(26);
            p_rrect((float)bx, (float)by, (float)(bx + bw), (float)(by + bh), (float)bh / 2, 0, C_ACCENT, 0.14f);
            p_rrect((float)bx, (float)by, (float)(bx + bw), (float)(by + bh), (float)bh / 2, 1, C_ACCENT, 0.85f);
            p_circle((float)(bx + dpx(9)), (float)by + bh / 2.0f, dpf(2.6f), 0, C_ACCENT, 1);
            q_text_ex(bx + dpx(15), by, bx + bw - dpx(4), by + bh, bt, F_TINY, C_ACCLT, DT_LEFT, 1);
        }
    }
    { /* search field */
        int sx1 = W - dpx(252), sx0 = sx1 - dpx(206), sy0 = (HB - dpx(30)) / 2, sy1 = sy0 + dpx(30);
        int hv = hov(H_SEARCH, 0);
        if (g_search_on) glow_box((float)sx0, (float)sy0, (float)sx1, (float)sy1, 0.14f);
        p_rrect((float)sx0, (float)sy0, (float)sx1, (float)sy1, dpf(8), 0, lerpc(C_BG, C_CARD, 0.5f), 1);
        p_rrect((float)sx0, (float)sy0, (float)sx1, (float)sy1, dpf(8), 1, g_search_on ? C_ACCENT : hv ? lerpc(C_BORDER, C_TEXT, 0.3f) : C_BORDER, 1);
        float mx = sx0 + dpf(16), my = (sy0 + sy1) / 2.0f - dpf(1); /* magnifier */
        p_circle(mx, my, dpf(5), dpf(1.6f), g_search_on ? C_ACCENT : C_MUTED, 1);
        p_line(mx + dpf(3.6f), my + dpf(3.6f), mx + dpf(7), my + dpf(7), dpf(1.8f), g_search_on ? C_ACCENT : C_MUTED, 1);
        int tx = sx0 + dpx(30);
        if (g_search[0]) {
            q_text(tx, sy0, sx1 - dpx(8), sy1, g_search, F_SMALL, C_TEXT, DT_LEFT);
        } else q_text(tx, sy0, sx1 - dpx(8), sy1, g_search_on ? L"Type a setting..." : L"Search settings   Ctrl+F", F_SMALL, C_DIM, DT_LEFT);
        if (g_search_on) { /* caret */
            SIZE z = {0};
            HGDIOBJ of = SelectObject(mem, F_SMALL);
            GetTextExtentPoint32W(mem, g_search, lstrlenW(g_search), &z);
            SelectObject(mem, of);
            int cxp = tx + z.cx + dpx(1);
            if (cxp < sx1 - dpx(8)) p_fill(cxp, sy0 + dpx(8), cxp + dpx(1) + 1, sy1 - dpx(8), C_ACCENT);
        }
        hit(sx0, sy0, sx1, sy1, H_SEARCH, 0);
    }
    { /* close + panel size buttons */
        int bs = dpx(28), by0 = (HB - bs) / 2;
        int x1 = W - dpx(14), x0 = x1 - bs, hv = hov(H_CLOSE, 0);
        if (hv) glow_box((float)x0, (float)by0, (float)x1, (float)(by0 + bs), 0.2f);
        p_rrect((float)x0, (float)by0, (float)x1, (float)(by0 + bs), dpf(4), 0, hv ? C_ACCENT : C_CARD, 1);
        p_rrect((float)x0, (float)by0, (float)x1, (float)(by0 + bs), dpf(4), 1, hv ? C_ACCENT : C_BORDER, 1);
        float bx = (x0 + x1) / 2.0f, byc = by0 + bs / 2.0f, s5 = dpf(5);
        p_line(bx - s5, byc - s5, bx + s5, byc + s5, dpf(1.7f), hv ? C_ONACC : C_TEXT, 1);
        p_line(bx - s5, byc + s5, bx + s5, byc - s5, dpf(1.7f), hv ? C_ONACC : C_TEXT, 1);
        hit(x0, by0, x1, by0 + bs, H_CLOSE, 0);
        for (int k = 1; k >= 0; k--) { /* panel size: smaller / bigger */
            x1 = x0 - dpx(k ? 14 : 6);
            x0 = x1 - bs;
            int can = k ? g_ui_size < 3 : g_ui_size > 0, hv2 = hov(H_SIZE, k ? 1 : -1);
            p_rrect((float)x0, (float)by0, (float)x1, (float)(by0 + bs), dpf(4), 0, hv2 && can ? C_HOVER : C_CARD, can ? 1.0f : 0.5f);
            p_rrect((float)x0, (float)by0, (float)x1, (float)(by0 + bs), dpf(4), 1, C_BORDER, 1);
            COLORREF ic = can ? C_TEXT : C_DIM;
            float sx = (x0 + x1) / 2.0f;
            p_line(sx - dpf(5), byc, sx + dpf(5), byc, dpf(1.6f), ic, 1);
            if (k) p_line(sx, byc - dpf(5), sx, byc + dpf(5), dpf(1.6f), ic, 1);
            hit(x0, by0, x1, by0 + bs, H_SIZE, k ? 1 : -1);
        }
        q_text_ex(x0 - dpx(110), by0, x0 - dpx(10), by0 + bs, L"PANEL SIZE", F_TINY, C_MUTED, DT_RIGHT, 1);
    }

    /* tab bar */
    {
        int tx0 = dpx(10), tw = (W - dpx(20)) / NTABS;
        HFONT tf = F_BOLD;
        for (int t = 0; t < NTABS; t++) {
            HFONT f = fit_font(mem, TAB_SHORT[t], 0, tw - dpx(12), F_BOLD, F_SMALL, F_XS);
            if (f == F_XS || (f == F_SMALL && tf == F_BOLD)) tf = f;
        }
        for (int t = 0; t < NTABS; t++) {
            int x0 = tx0 + t * tw, x1 = x0 + tw, sel = t == g_tab, hv = hov(H_TAB, t);
            const wchar_t *up = TAB_SHORT[t];
            if (sel) {
                for (int y = HB; y < BAR - 1; y++)
                    for (int x = x0; x < x1; x++) blend(x, y, C_ACCENT, 0.20f * (y - HB) / (float)(BAR - HB));
                p_rrect((float)x0 + dpf(6), (float)(BAR - dpx(7)), (float)x1 - dpf(6), (float)(BAR + dpx(1)), dpf(4), 0, C_ACCENT, 0.25f);
                p_rrect((float)x0 + dpf(4), (float)(BAR - dpx(3)), (float)x1 - dpf(4), (float)BAR, 0, 0, C_ACCENT, 1);
            } else if (hv) {
                p_rrect((float)x0 + dpf(2), (float)HB + dpf(5), (float)x1 - dpf(2), (float)BAR - dpf(5), dpf(3), 0, C_HOVER, 0.8f);
                p_rrect((float)x0 + dpf(14), (float)(BAR - dpx(2)), (float)x1 - dpf(14), (float)BAR, 0, 0, C_ACCENT, 0.5f);
            }
            if (t) p_fill(x0, HB + dpx(13), x0 + 1, BAR - dpx(13), lerpc(C_BORDER, C_BG, 0.2f));
            q_text(x0, HB, x1, BAR - dpx(1), up, tf, sel ? C_TEXT : hv ? lerpc(C_MUTED, C_TEXT, 0.6f) : C_MUTED, DT_CENTER);
            hit(x0, HB, x1, BAR, H_TAB, t);
        }
    }

    /* left column: big live preview, what's in use, Done */
    {
        int x0 = dpx(16), x1 = SW + dpx(6);
        draw_preview(x0, BAR + dpx(18), x1, H - dpx(234));
        int y0 = H - dpx(196), y1 = H - dpx(110);
        p_rrect((float)x0, (float)y0, (float)x1, (float)y1, dpf(8), 0, C_CARD, 1);
        p_rrect((float)x0, (float)y0, (float)x1, (float)y1, dpf(8), 1, C_BORDER, 1);
        p_rrect((float)x0, (float)(y0 + dpx(14)), (float)(x0 + dpx(3)), (float)(y1 - dpx(14)), 0, 0, C_ACCENT, 1);
        int tx = x0 + dpx(16);
        q_text_ex(tx, y0 + dpx(10), x1 - dpx(8), y0 + dpx(26), L"NOW USING", F_TINY, C_DIM, DT_LEFT, 1);
        wchar_t upb[96];
        if (S.l2_style >= 0) wsprintfW(upb, L"%s + %s", STYLE_NAMES[S.style], STYLE_NAMES[S.l2_style]);
        else lstrcpynW(upb, STYLE_NAMES[S.style], 96);
        const wchar_t *up = upb;
        q_text(tx, y0 + dpx(25), x1 - dpx(8), y0 + dpx(50), up, fit_font(mem, up, 0, x1 - tx - dpx(10), F_HUD, F_TITLE, F_BOLD), C_TEXT, DT_LEFT);
        p_rrect((float)tx, (float)(y0 + dpx(58)), (float)(tx + dpx(10)), (float)(y0 + dpx(68)), dpf(2), 0, RGB(COLORS[S.color].r, COLORS[S.color].g, COLORS[S.color].b), 1);
        wchar_t sz[64];
        wsprintfW(sz, L"%s  \x2022  %d px", COLORS[S.color].name, SIZES[S.size]);
        q_text(tx + dpx(16), y0 + dpx(53), x1 - dpx(8), y0 + dpx(73), sz, F_SMALL, C_MUTED, DT_LEFT);
    }
    {
        int x0 = dpx(16), x1 = SW + dpx(6), y0 = H - dpx(66), y1 = H - dpx(20);
        int hv = hov(H_DONE, 0);
        glow_box((float)x0, (float)y0, (float)x1, (float)y1, hv ? 0.30f : 0.16f);
        p_rrect((float)x0, (float)y0, (float)x1, (float)y1, dpf(6), 0, hv ? C_ACCHI : C_ACCENT, 1);
        const wchar_t *dt = L"Done  \x2014  start playing";
        q_text(x0, y0, x1, y1, dt, fit_font(mem, dt, 0, x1 - x0 - dpx(16), F_BOLD, F_SMALL, F_XS), C_ONACC, DT_CENTER);
        hit(x0, y0, x1, y1, H_DONE, 0);
    }

    /* content heading: TAB NAME // what it does */
    int cx0 = SW + dpx(36), cx1 = W - dpx(24);
    {
        int hy0 = BAR + dpx(12), hy1 = BAR + dpx(40);
        const wchar_t *up = TAB_NAMES[g_tab];
        SIZE ts = {0};
        HGDIOBJ of = SelectObject(mem, F_HUD);
        GetTextExtentPoint32W(mem, up, lstrlenW(up), &ts);
        SelectObject(mem, of);
        p_rrect((float)cx0, (float)(hy0 + dpx(6)), (float)(cx0 + dpx(4)), (float)(hy1 - dpx(6)), dpf(2), 0, C_ACCENT, 1);
        q_text(cx0 + dpx(14), hy0, cx0 + dpx(14) + ts.cx + dpx(8), hy1, up, F_HUD, C_TEXT, DT_LEFT);
        int lay = g_tab == TAB_CROSSHAIR || g_tab == TAB_COLOR || g_tab == TAB_LOOK, sx1 = lay || g_tab == TAB_PROFILES ? cx1 - dpx(216) : cx1;
        q_text(cx0 + dpx(14) + ts.cx + dpx(16), hy0 + dpx(2), sx1, hy1, TAB_SUBS[g_tab],
               fitf(TAB_SUBS[g_tab], sx1 - (cx0 + dpx(14) + ts.cx + dpx(16)), F_SMALL, F_XS, F_XS), C_MUTED, DT_LEFT);
        if (g_tab == TAB_PROFILES) { /* back up / restore everything */
            draw_button(cx1 - dpx(208), hy0, cx1 - dpx(108), hy1, L"Export...", H_BUTTON, B_EXPORT, 0);
            draw_button(cx1 - dpx(100), hy0, cx1, hy1, L"Import...", H_BUTTON, B_IMPORT, 0);
        }
        if (lay) { /* which layer these tabs edit */
            static const wchar_t *LY[] = {L"Main", L"Layer 2"};
            draw_segmented(cx1 - dpx(200), hy0 + dpx(1), cx1, hy1 - dpx(1), LY, 2, g_edit_l2, H_LAYERSEL);
            if (g_edit_l2 && S.l2_style < 0) p_circle((float)(cx1 - dpx(12)), (float)(hy0 + dpx(6)), dpf(4), 0, C_WARN, 1);
        }
        p_fill(cx0, hy1 + dpx(8), cx1, hy1 + dpx(9), lerpc(C_BORDER, C_BG, 0.2f));
        p_rrect((float)cx0, (float)(hy1 + dpx(7)), (float)(cx0 + dpx(56)), (float)(hy1 + dpx(10)), 0, 0, C_ACCENT, 1);
        p_rrect((float)(cx0 + dpx(60)), (float)(hy1 + dpx(7)), (float)(cx0 + dpx(68)), (float)(hy1 + dpx(10)), 0, 0, C_ACCENT, 0.5f);
    }

    int top = BAR + dpx(64);
    switch (g_tab) {
    case TAB_CROSSHAIR: tab_crosshair(cx0, top, cx1); break;
    case TAB_COLOR: tab_color(cx0, top, cx1); break;
    case TAB_LOOK: tab_look(cx0, top, cx1); break;
    case TAB_FX: tab_fx(cx0, top, cx1); break;
    case TAB_PROFILES: tab_profiles(cx0, top, cx1); break;
    case TAB_KEYS: tab_keys(cx0, top, cx1, H - dpx(122)); break;
    case TAB_THEME: tab_theme(cx0, top, cx1); break;
    case TAB_PERF: tab_perf(cx0, top, cx1); break;
    case TAB_GENERAL: tab_general(cx0, top, cx1); break;
    case TAB_UPDATES: tab_updates(cx0, top, cx1, H - dpx(124)); break;
    }
    if (g_ui_status[0]) {
        p_rrect((float)cx0, (float)(H - dpx(52)), (float)cx1, (float)(H - dpx(16)), dpf(10), 0, lerpc(C_CARD, C_WARN, 0.14f), 1);
        q_text(cx0 + dpx(14), H - dpx(52), cx1 - dpx(14), H - dpx(16), g_ui_status, F_SMALL,
               theme_is_light() ? RGB(150, 90, 0) : C_WARN, DT_LEFT);
    } else if (g_news_hash && g_news_hash != g_news_dismissed && g_news[0]) { /* the developer's announcement */
        int y0 = H - dpx(58), y1 = H - dpx(14);
        p_rrect((float)cx0, (float)y0, (float)cx1, (float)y1, dpf(10), 0, lerpc(C_CARD, C_ACCENT, 0.16f), 1);
        p_rrect((float)cx0, (float)y0, (float)(cx0 + dpx(4)), (float)y1, dpf(2), 0, C_ACCENT, 1);
        q_text_ex(cx0 + dpx(16), y0, cx0 + dpx(112), y1, L"NEWS", F_TAB, C_ACCLT, DT_LEFT, 1);
        {
            int mw = cx1 - dpx(44) - (cx0 + dpx(70));
            HFONT nf = fit_font(mem, g_news, 0, mw, F_SMALL, F_XS, F_XS);
            SIZE z = {0};
            HGDIOBJ of = SelectObject(mem, nf);
            GetTextExtentPoint32W(mem, g_news, lstrlenW(g_news), &z);
            SelectObject(mem, of);
            if (z.cx <= mw) q_text(cx0 + dpx(70), y0, cx1 - dpx(44), y1, g_news, nf, C_TEXT, DT_LEFT);
            else q_text(cx0 + dpx(70), y0 + dpx(5), cx1 - dpx(44), y1 - dpx(2), g_news, F_XS, C_TEXT, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
        }
        int bx = cx1 - dpx(36), by = y0 + (y1 - y0) / 2 - dpx(12), hv = hov(H_NEWSX, 0);
        p_rrect((float)bx, (float)by, (float)(bx + dpx(24)), (float)(by + dpx(24)), dpf(6), 0, hv ? C_HOVER : C_CARD, 1);
        {
            float mx = bx + dpf(12), my = by + dpf(12), r4 = dpf(4);
            p_line(mx - r4, my - r4, mx + r4, my + r4, dpf(1.5f), hv ? C_TEXT : C_MUTED, 1);
            p_line(mx - r4, my + r4, mx + r4, my - r4, dpf(1.5f), hv ? C_TEXT : C_MUTED, 1);
        }
        hit(bx, by, bx + dpx(24), by + dpx(24), H_NEWSX, 0);
    }

    /* overlays (glow, tip, search results, tour) go on top of the page, text included */
    if (g_flash_title[0] || g_tip_show || (g_search_on && g_search[0]) || g_tour) {
        GdiFlush();
        flush_text(mem);
        g_ntq = 0;
        GdiFlush();
    }
    /* the setting search took you to glows for a moment */
    if (g_flash_title[0]) {
        DWORD age = GetTickCount() - g_flash_t;
        if (age > 1800) g_flash_title[0] = 0;
        else
            for (int i = 0; i < g_nrows; i++)
                if (!wcscmp(g_rows[i].title, g_flash_title)) {
                    RECT r = g_rows[i].r;
                    float a = 0.5f + 0.5f * cosf(age / 1800.0f * 3.14159f * 3);
                    a *= 1.0f - age / 1800.0f;
                    glow_box((float)r.left, (float)r.top, (float)r.right, (float)r.bottom, 0.35f * a);
                    p_rrect((float)r.left - dpf(2), (float)r.top - dpf(2), (float)r.right + dpf(2), (float)r.bottom + dpf(2), dpf(12), dpf(2), C_ACCENT, a);
                    break;
                }
    }
    /* hover tip */
    if (g_tip_show && g_tip_hover[0] && !g_tour) {
        const wchar_t *tp = tip_for(g_tip_hover);
        if (tp) {
            int tw = dpx(330);
            RECT cr = {0, 0, tw - dpx(24), 0};
            HGDIOBJ of = SelectObject(mem, F_SMALL);
            DrawTextW(mem, tp, -1, &cr, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
            SelectObject(mem, of);
            int th = cr.bottom + dpx(20);
            int x0 = clampi(g_tip_pt.x + dpx(14), dpx(8), W - tw - dpx(8)), y0 = g_tip_pt.y + dpx(22);
            if (y0 + th > H - dpx(8)) y0 = g_tip_pt.y - th - dpx(12);
            p_rrect((float)x0 + dpf(3), (float)y0 + dpf(4), (float)(x0 + tw) + dpf(3), (float)(y0 + th) + dpf(4), dpf(8), 0, RGB(0, 0, 0), 0.35f);
            p_rrect((float)x0, (float)y0, (float)(x0 + tw), (float)(y0 + th), dpf(8), 0, lerpc(C_CARD, C_BG, 0.25f), 1);
            p_rrect((float)x0, (float)y0, (float)(x0 + tw), (float)(y0 + th), dpf(8), 1, lerpc(C_BORDER, C_ACCENT, 0.5f), 1);
            p_rrect((float)x0, (float)y0 + dpf(8), (float)x0 + dpf(3), (float)(y0 + th) - dpf(8), 0, 0, C_ACCENT, 1);
            q_text(x0 + dpx(12), y0 + dpx(10), x0 + dpx(12) + cr.right, y0 + th - dpx(6), tp, F_SMALL, C_TEXT, DT_LEFT | DT_WORDBREAK);
        }
    }
    /* search results */
    if (g_search_on && g_search[0]) {
        int rx1 = W - dpx(252), rx0 = rx1 - dpx(330), ry = HB - dpx(4), rh = dpx(38);
        int n = g_nsres ? g_nsres : 1, ph = n * rh + dpx(12);
        p_rrect((float)rx0 + dpf(3), (float)ry + dpf(5), (float)rx1 + dpf(3), (float)(ry + ph) + dpf(5), dpf(10), 0, RGB(0, 0, 0), 0.4f);
        p_rrect((float)rx0, (float)ry, (float)rx1, (float)(ry + ph), dpf(10), 0, lerpc(C_CARD, C_BG, 0.2f), 1);
        p_rrect((float)rx0, (float)ry, (float)rx1, (float)(ry + ph), dpf(10), 1, lerpc(C_BORDER, C_ACCENT, 0.4f), 1);
        hit(rx0, ry, rx1, ry + ph, H_SEARCHRES, -1);
        if (!g_nsres) q_text(rx0 + dpx(16), ry + dpx(6), rx1 - dpx(10), ry + dpx(6) + rh, L"No settings match. Try \"fps\", \"color\" or \"hide\".", F_SMALL, C_MUTED, DT_LEFT);
        for (int i = 0; i < g_nsres; i++) {
            const SearchItem *it = &SEARCH[g_sres[i]];
            int y0 = ry + dpx(6) + i * rh, sel = i == g_search_sel || hov(H_SEARCHRES, i);
            if (sel) {
                p_rrect((float)(rx0 + dpx(6)), (float)y0, (float)(rx1 - dpx(6)), (float)(y0 + rh), dpf(7), 0, C_HOVER, 1);
                p_rrect((float)(rx0 + dpx(6)), (float)(y0 + dpx(9)), (float)(rx0 + dpx(9)), (float)(y0 + rh - dpx(9)), 0, 0, C_ACCENT, 1);
            }
            q_text(rx0 + dpx(18), y0, rx1 - dpx(110), y0 + rh, it->title, fitf(it->title, rx1 - rx0 - dpx(130), F_BOLD, F_SMALL, F_XS), C_TEXT, DT_LEFT);
            q_text(rx1 - dpx(108), y0, rx1 - dpx(16), y0 + rh, it->tab >= 0 ? TAB_SHORT[it->tab] : it->tab == -2 ? L"Opens a window" : L"Top bar", F_SMALL, sel ? C_ACCLT : C_DIM, DT_RIGHT);
            hit(rx0 + dpx(6), y0, rx1 - dpx(6), y0 + rh, H_SEARCHRES, i);
        }
    }
    /* first-run tour */
    if (g_tour) {
        RECT t;
        const wchar_t *tt, *tx;
        int tw = (W - dpx(20)) / NTABS;
        if (g_tour == 1) {
            SetRect(&t, cx0 - dpx(8), top - dpx(8), cx1 + dpx(8), top + dpx(286));
            tt = L"Pick your crosshair";
            tx = L"Click any style here. The Color, Look and Effects tabs fine-tune it, and the preview on the left shows it at real size.";
        } else if (g_tour == 2) {
            SetRect(&t, dpx(10) + TAB_KEYS * tw, HB, dpx(10) + (TAB_KEYS + 1) * tw, BAR);
            tt = L"Set a hotkey";
            tx = L"In Keybinds you can hide the crosshair, switch styles or open this panel with one key, even in the middle of a game.";
        } else {
            SetRect(&t, dpx(12), H - dpx(70), SW + dpx(10), H - dpx(16));
            tt = L"You're ready";
            tx = L"Hit Done to start playing. To come back here, click the crosshair icon by the clock (or use your hotkey).";
        }
        COLORREF dim = RGB(0, 0, 0);
        p_rrect(0, 0, (float)W, (float)t.top, 0, 0, dim, 0.62f);
        p_rrect(0, (float)t.bottom, (float)W, (float)H, 0, 0, dim, 0.62f);
        p_rrect(0, (float)t.top, (float)t.left, (float)t.bottom, 0, 0, dim, 0.62f);
        p_rrect((float)t.right, (float)t.top, (float)W, (float)t.bottom, 0, 0, dim, 0.62f);
        /* outline + outer glow only, so the spotlighted part stays clear */
        p_rrect((float)t.left - dpf(6), (float)t.top - dpf(6), (float)t.right + dpf(6), (float)t.bottom + dpf(6), dpf(14), dpf(5), C_ACCENT, 0.12f);
        p_rrect((float)t.left - dpf(3), (float)t.top - dpf(3), (float)t.right + dpf(3), (float)t.bottom + dpf(3), dpf(12), dpf(3), C_ACCENT, 0.25f);
        p_rrect((float)t.left, (float)t.top, (float)t.right, (float)t.bottom, dpf(10), dpf(2), C_ACCENT, 1);
        int cw = dpx(330), ch = dpx(178), kx, ky;
        if (g_tour == 1) { kx = t.right - cw; ky = t.bottom + dpx(16); }
        else if (g_tour == 2) { kx = clampi(t.left + (t.right - t.left) / 2 - cw / 2, dpx(12), W - cw - dpx(12)); ky = t.bottom + dpx(16); }
        else { kx = t.right + dpx(18); ky = t.bottom - ch; }
        if (ky + ch > H - dpx(10)) ky = H - dpx(10) - ch;
        hit(0, 0, W, H, H_TOURBLOCK, 0);
        p_rrect((float)kx + dpf(3), (float)ky + dpf(5), (float)(kx + cw) + dpf(3), (float)(ky + ch) + dpf(5), dpf(12), 0, RGB(0, 0, 0), 0.45f);
        p_rrect((float)kx, (float)ky, (float)(kx + cw), (float)(ky + ch), dpf(12), 0, C_CARD, 1);
        p_rrect((float)kx, (float)ky, (float)(kx + cw), (float)(ky + ch), dpf(12), 1, C_ACCENT, 1);
        wchar_t stp[24];
        wsprintfW(stp, L"STEP %d OF 3", g_tour);
        q_text_ex(kx + dpx(18), ky + dpx(14), kx + cw - dpx(18), ky + dpx(30), stp, F_TINY, C_ACCLT, DT_LEFT, 1);
        for (int i = 0; i < 3; i++) p_circle((float)(kx + cw - dpx(46) + i * dpx(12)), (float)(ky + dpx(22)), dpf(3.2f), 0, i < g_tour ? C_ACCENT : C_TRACK, 1);
        q_text(kx + dpx(18), ky + dpx(32), kx + cw - dpx(18), ky + dpx(60), tt, F_HUD, C_TEXT, DT_LEFT);
        q_text(kx + dpx(18), ky + dpx(64), kx + cw - dpx(28), ky + dpx(122), tx, F_SMALL, C_MUTED, DT_LEFT | DT_WORDBREAK);
        int by0 = ky + ch - dpx(50), by1 = ky + ch - dpx(16);
        draw_button(kx + cw - dpx(140), by0, kx + cw - dpx(18), by1, g_tour < 3 ? L"Next" : L"Let's go", H_TOURNEXT, 0, 1);
        if (g_tour < 3) draw_button(kx + dpx(18), by0, kx + dpx(118), by1, L"Skip tour", H_TOURSKIP, 0, 0);
    }

    g_sharp = 1.0f;
    g_paint_dc = NULL;
    GdiFlush();
    flush_text(mem);
    BitBlt(dc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(h, &ps);
}

/* "Remove everything": undo all this app changed, so the exe can simply be deleted. */
static void uninstall_all(void) {
    if (roblox_running()) {
        MessageBoxW(g_keydlg, L"Please close Roblox first, so its settings can be put back.", L"Remove everything", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (!g_test_yes && MessageBoxW(g_keydlg,
            L"This puts back everything Crosshair Overlay changed and then closes it:\n\n"
            L"\x2022 Roblox's mouse cursor images\n"
            L"\x2022 Roblox's frame-rate setting and the FPS Boost graphics options\n"
            L"\x2022 the high-performance graphics choice\n"
            L"\x2022 Start with Windows\n"
            L"\x2022 this app's settings, profiles, keybinds and logs\n\n"
            L"After that, just delete the Crosshair Overlay exe. Continue?",
            L"Remove everything", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;
    int touched_fps = g_fps > 0 || ini_get(L"roblox_changes_ok", 0);
    S.hidecur = 0;
    restore_cursor();
    rbx_all(0);                         /* Roblox's cursor images */
    g_fps = 0; g_boost = 0; g_gpu = 0;
    perf_sync_installs();               /* boost flags + GPU choice */
    if (touched_fps) fps_write(-1, 1, 0); /* Roblox's frame-rate setting back to its default */
    autostart_set(0);
    {   /* our folder in %APPDATA% (settings, logs, downloaded updates) */
        wchar_t dir[MAX_PATH + 2];
        wcscpy(dir, g_ini);
        wchar_t *sl = wcsrchr(dir, L'\\');
        if (sl && wcsstr(dir, L"CrosshairOverlay")) {
            *sl = 0;
            dir[wcslen(dir) + 1] = 0; /* double-NUL for SHFileOperation */
            g_ini[0] = g_logp[0] = g_pendp[0] = 0; /* nothing writes there any more */
            SHFILEOPSTRUCTW op;
            memset(&op, 0, sizeof(op));
            op.wFunc = FO_DELETE;
            op.pFrom = dir;
            op.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
            SHFileOperationW(&op);
        }
    }
    {   /* leftovers next to the exe from updates */
        wchar_t self[MAX_PATH], f[MAX_PATH + 8], args[MAX_PATH + 16];
        GetModuleFileNameW(NULL, self, MAX_PATH);
        wsprintfW(f, L"%s.old", self); DeleteFileW(f);
        wsprintfW(f, L"%s.bad", self); DeleteFileW(f);
        wsprintfW(args, L"/select,\"%s\"", self);
        ShellExecuteW(NULL, L"open", L"explorer.exe", args, NULL, SW_SHOWNORMAL);
    }
    if (!g_test_yes) MessageBoxW(NULL, L"Done \x2014 everything is put back. Delete the Crosshair Overlay exe (it's selected in the window that opened) to finish.",
                L"Remove everything", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
    if (g_keydlg) DestroyWindow(g_keydlg);
    g_exit_why = L"Remove everything";
    DestroyWindow(g_host);
}

static void tour_end(void) {
    g_tour = 0;
    ini_set(L"tour_done", 1);
    ui_redraw();
}
/* Jump to a search result: open its tab and make the setting glow. */
static void search_go(int i) {
    if (i < 0 || i >= g_nsres) return;
    const SearchItem *it = &SEARCH[g_sres[i]];
    if (it->tab >= 0) { g_tab = it->tab; g_kscroll = 0; g_uscroll = 0; }
    else if (it->tab == -2) { g_search_on = 0; g_search[0] = 0; g_nsres = 0; ui_redraw(); report_dialog(-1); return; }
    else ui_set_status(L"Panel size: use the \x2013 and + buttons at the top right.");
    lstrcpynW(g_flash_title, it->title, 64);
    g_flash_t = GetTickCount();
    g_search_on = 0;
    g_search[0] = 0;
    g_nsres = 0;
    g_tip_show = 0;
    ui_redraw();
}

static void ui_click(HWND h, int x, int y) {
    const Hit *ht = hit_at(x, y);
    if (ht) { /* breadcrumb: what was clicked, in words */
        const wchar_t *rt = NULL;
        for (int i = g_nrows - 1; i >= 0; i--)
            if (x >= g_rows[i].r.left && x < g_rows[i].r.right && y >= g_rows[i].r.top && y < g_rows[i].r.bottom) { rt = g_rows[i].title; break; }
        if (ht->kind == H_STYLE && ht->val >= 0 && ht->val < NSTYLES) crumb(L"picked %s", STYLE_NAMES[ht->val]);
        else if (rt) crumb(L"clicked %s", rt);
        else if (ht->kind == H_BUTTON) {
            static const wchar_t *BN[] = {L"Show keybinds", L"Roblox cursor status", L"Exit app", L"Report a problem", L"Check now",
                                          L"Remove everything", L"Copy code", L"Paste code", L"Looks good", L"Problem..."};
            if (ht->val >= 0 && ht->val < (int)(sizeof(BN) / sizeof(BN[0]))) crumb(L"clicked %s", BN[ht->val]);
        } else if (ht->kind == H_THEME) crumb(L"picked a theme");
        else if (ht->kind == H_COLOR) crumb(L"picked a color");
        else if (ht->kind == H_PLOAD || ht->kind == H_PSAVE) crumb(L"%s profile %d", ht->kind == H_PLOAD ? L"loaded" : L"saved", ht->val + 1);
        else if (ht->kind == H_SIZE) crumb(L"changed panel size");
        else if (ht->kind == H_PERFBTN) crumb(ht->val ? L"clicked Undo all FPS changes" : L"clicked Apply now (FPS Boost)");
    }
    if (g_tip_show) { g_tip_show = 0; ui_redraw(); }
    if (g_search_on && (!ht || (ht->kind != H_SEARCH && ht->kind != H_SEARCHRES))) { g_search_on = 0; g_search[0] = 0; g_nsres = 0; ui_redraw(); }
    if (!ht) return;
    int k = ht->kind, v = ht->val;
    if (g_capture >= 0 && k != H_KEY) { g_capture = -1; ui_redraw(); }
    switch (k) {
    case H_NUDGE: {
        if (v == 4) g_nudge_x = g_nudge_y = 0;
        else if (v < 2) g_nudge_x = clampi(g_nudge_x + (v ? 1 : -1), -10, 10);
        else g_nudge_y = clampi(g_nudge_y + (v == 3 ? 1 : -1), -10, 10);
        save_settings();
        InterlockedExchange(&g_force_move, 1);
        wchar_t m[120];
        wsprintfW(m, L"Crosshair moved %d px %s and %d px %s from the standard aim point.", abs(g_nudge_x), g_nudge_x < 0 ? L"left" : L"right",
                  abs(g_nudge_y), g_nudge_y < 0 ? L"up" : L"down");
        ui_set_status(g_nudge_x || g_nudge_y ? m : L"Crosshair is back on the standard aim point.");
        ui_redraw();
        return;
    }
    case H_SEARCH:
        g_search_on = 1;
        ui_redraw();
        return;
    case H_SEARCHRES:
        if (v >= 0) search_go(v);
        return;
    case H_TOURNEXT:
        if (++g_tour > 3) tour_end();
        else ui_redraw();
        return;
    case H_TOURSKIP: tour_end(); return;
    case H_TOURBLOCK: return;
    case H_CLOSE: DestroyWindow(h); return;
    case H_RCODE:
        if (v >= 0 && v < g_nrecent && xh_code_apply(g_recent[v])) {
            wchar_t m[96];
            apply();
            wsprintfW(m, L"Switched to %s.", g_recent[v]);
            recent_add(g_recent[v]); /* move it to the front */
            ui_set_status(m);
        }
        ui_redraw();
        return;
    case H_RCODEX:
        recent_remove(v);
        ui_redraw();
        return;
    case H_NEWSX:
        g_news_dismissed = g_news_hash;
        save_settings();
        ui_redraw();
        return;
    case H_ONLYRBX:
        g_onlyrbx = v;
        save_settings();
        focus_check();
        ui_set_status(v ? L"The crosshair now only shows while Roblox is the window in front." : L"The crosshair shows everywhere again.");
        return;
    case H_SIZE: { /* make the whole panel smaller or bigger, keeping it centred where it is */
        int ns = clampi(g_ui_size + v, 0, 3);
        if (ns == g_ui_size) return;
        RECT wr;
        GetWindowRect(h, &wr);
        int ocx = (wr.left + wr.right) / 2, ocy = (wr.top + wr.bottom) / 2;
        g_ui_size = ns;
        save_settings();
        g_ui_shrink = UI_SIZES[ns];
        make_fonts();
        MONITORINFO mi3;
        mi3.cbSize = sizeof(mi3);
        if (GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi3)) {
            int avail = mi3.rcWork.bottom - mi3.rcWork.top - 16;
            if (dpx(UI_H) > avail) { g_ui_shrink = clampi(g_ui_shrink * avail / dpx(UI_H), 60, 115); make_fonts(); }
            int w2 = dpx(UI_W), h2 = dpx(UI_H);
            int nx = clampi(ocx - w2 / 2, mi3.rcWork.left, mi3.rcWork.right - w2), ny = clampi(ocy - h2 / 2, mi3.rcWork.top, mi3.rcWork.bottom - h2);
            SetWindowPos(h, NULL, nx, ny, w2, h2, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        ui_redraw();
        return;
    }
    case H_TAB: g_tab = v; g_capture = -1; g_ui_status[0] = 0; ui_redraw(); break;
    case H_STYLE:
        if (g_edit_l2) { S.l2_style = v; if (v >= 0) crumb(L"layer 2: %s", STYLE_NAMES[v]); }
        else S.style = v;
        apply();
        break;
    case H_COLOR:
        if (g_edit_l2) S.l2_color = v;
        else S.color = v;
        apply();
        break;
    case H_LAYERSEL:
        g_edit_l2 = v;
        if (v && S.l2_style < 0 && g_tab == TAB_CROSSHAIR) ui_set_status(L"Pick a shape for layer 2 \x2014 it's drawn on top of your main crosshair.");
        else if (v && S.l2_style < 0) ui_set_status(L"Pick a shape for layer 2 on the Crosshair tab first.");
        ui_redraw();
        break;
    case H_PICK: pick_custom_color(); break;
    case H_SLIDER: g_drag = v; SetCapture(h); slider_set(v, x); ui_redraw(); break;
    case H_TOGGLE:
        if (v == T_OUTLINE) { S.outline = !S.outline; apply(); }
        else if (v == T_HIDECUR) { S.hidecur = !S.hidecur; apply(); }
        else if (v == T_SPLASH) { S.splash = !S.splash; save_settings(); ui_redraw(); }
        else if (v == T_SHOW) { set_hidden(!S.hidden); ui_redraw(); }
        else if (v == T_OPENSTART) { S.open_start = !S.open_start; save_settings(); ui_redraw(); }
        else if (v == T_GLOW) { S.glow = !S.glow; apply(); }
        else if (v == T_AUTOSTART) {
            int on = !autostart_on();
            if (!autostart_set(on)) ui_set_status(L"Couldn't change the Windows startup setting.");
            else ui_set_status(on ? L"Crosshair Overlay will start (quietly) when you sign in to Windows." : L"It won't start with Windows anymore.");
            log_info(L"start with Windows %s", on ? L"on" : L"off");
        }
        else if (v == T_AUTOUPD) {
            g_autoupd = !g_autoupd;
            save_settings();
            ui_set_status(g_autoupd ? L"Updates will download and install by themselves." : L"Automatic updates off. The app still tells you when there's a new version.");
        }
        else if (v == T_STATS) {
            g_stats = !g_stats;
            save_settings();
            log_info(L"usage stats turned %s", g_stats ? L"on" : L"off");
            ui_set_status(g_stats ? L"Thanks! Anonymous usage stats go to simplysoul once a week." : L"Usage stats off. Nothing will be sent.");
            if (g_stats) { HANDLE th = CreateThread(NULL, 0, stats_thread, NULL, 0, NULL); if (th) CloseHandle(th); }
        }
        else if (v == T_TBUILDS) {
            g_tbuilds = !g_tbuilds;
            save_settings();
            log_info(L"tester: test builds %s", g_tbuilds ? L"on" : L"paused");
            ui_set_status(g_tbuilds ? L"You'll get test builds again." : L"Test builds paused. You'll get the normal releases until you turn this back on.");
        }
        else if (v == T_REPORTS) {
            g_reports = g_reports == 1 ? 2 : 1;
            save_settings();
            log_info(L"bug reports turned %s", g_reports == 1 ? L"on" : L"off");
            ui_set_status(g_reports == 1 ? L"Crash & error reports will be sent automatically. Thanks!" : L"Bug reports won't be sent.");
        }
        else if (v == T_GPU) { if (!g_gpu && !roblox_change_ok()) break; g_gpu = !g_gpu; save_settings(); perf_sync_installs(); perf_report(FPS_SAME); }
        else if (v == T_NOCAP) {
            g_nocap = !g_nocap;
            save_settings();
            if (!capture_hide_apply() && g_nocap)
                ui_set_status(L"Your Windows is too old to hide windows from recordings (needs Windows 10 version 2004 or newer).");
            else ui_set_status(g_nocap ? L"Crosshair hidden from recordings, streams and screenshots \x2014 you still see it."
                                       : L"Crosshair will now show up in recordings, streams and screenshots.");
        }
        else if (v == T_CLICK) { S.click_fx = !S.click_fx; apply(); if (S.click_fx) kick(); }
        else if (v == T_AUTOCON) { S.auto_contrast = !S.auto_contrast; save_settings(); scene_timer_sync(); ui_redraw(); }
        else if (v == T_MENUHIDE) { S.menu_hide = !S.menu_hide; save_settings(); scene_timer_sync(); ui_redraw(); }
        break;
    case H_COVER: S.cover = v; if (v) g_cover_last = v; apply(); break;
    case H_COVERCOL: S.cover_color = v; if (!S.cover) S.cover = g_cover_last; apply(); break;
    case H_KEY: g_capture = v; g_ui_status[0] = 0; SetFocus(h); ui_redraw(); break;
    case H_KEYCLEAR: {
        wchar_t nm[64], msg[128];
        KB[v].vk = 0;
        KB[v].mods = 0;
        save_binds();
        kb_check();
        action_name(v, nm);
        wsprintfW(msg, L"\"%s\" now has no key.", nm);
        ui_set_status(msg);
        break;
    }
    case H_KRESET:
        g_capture = -1;
        default_binds();
        save_binds();
        kb_check();
        ui_set_status(L"Keybinds reset to the defaults.");
        break;
    case H_MODE: S.center = v; apply(); break;
    case H_DONE: DestroyWindow(h); return;
    case H_FXSEG: S.anim = v; apply(); break;
    case H_SPEED: S.anim_speed = v; apply(); break;
    case H_PREVIEW: kick(); ui_redraw(); break;
    case H_THEME: use_theme(v); break;
    case H_THPICK: {
        COLORREF c = v ? g_cust_bg : g_cust_accent;
        if (pick_color(&c)) {
            if (v) g_cust_bg = c; else g_cust_accent = c;
            use_theme(TH_CUSTOM);
            ui_set_status(L"Custom theme on. Pick the other color too, or choose a preset above.");
        }
        break;
    }
    case H_SPLASHPREV: splash_preview(); break;
    case H_FPS: if (v && !roblox_change_ok()) break; g_fps = v; save_settings(); perf_report(fps_apply(1)); break;
    case H_BOOST: if (v && !roblox_change_ok()) break; g_boost = v; save_settings(); perf_sync_installs(); perf_report(g_fps > 0 ? FPS_SAME : FPS_OFF); break;
    case H_PERFBTN:
        if (v == 1) { /* undo everything, including Roblox's frame-rate setting */
            g_fps = 0; g_boost = 0; g_gpu = 0;
            save_settings();
            perf_sync_installs();
            int r = fps_write(-1, 1, 0);
            log_info(L"undo all FPS changes (cap reset: %d)", r);
            if (r == FPS_RUNNING)
                ui_set_status(L"Boost removed. Now fully close Roblox and click Undo again to reset its frame rate.");
            else if (r == FPS_FAIL)
                ui_set_status(L"Boost removed, but Roblox's frame-rate setting couldn't be reset. Try Run as administrator.");
            else
                ui_set_status(L"All FPS changes undone \x2014 Roblox is back to its normal settings. Start Roblox again.");
        }
        else { int r = fps_apply(1); perf_sync_installs(); perf_report(r); }
        break;
    case H_CRIMINTRO: g_crim_intro = v; save_settings(); ui_redraw(); break;
    case H_MUSIC:
        if (v == 0) { /* choose a song file */
            wchar_t file[MAX_PATH];
            OPENFILENAMEW of;
            wcscpy(file, g_music);
            memset(&of, 0, sizeof(of));
            of.lStructSize = sizeof(of);
            of.hwndOwner = h;
            of.lpstrFilter = L"Audio files (mp3, wav, wma, m4a)\0*.mp3;*.wav;*.wma;*.m4a;*.aac\0All files\0*.*\0";
            of.lpstrFile = file;
            of.nMaxFile = MAX_PATH;
            of.lpstrTitle = L"Choose loading music";
            of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
            g_capture = -2; /* keep the panel open while the file dialog has focus */
            int ok = GetOpenFileNameW(&of);
            g_capture = -1;
            if (ok) {
                wcscpy(g_music, file);
                save_settings();
                ui_set_status(L"Loading music set. Use \x2212 / + to start at the best part, then Themes \x2192 Preview loading screen.");
            }
        } else if (v == 1 || v == 2) { /* move the start 5 s earlier / later and play a short sample */
            g_music_start = clampi(g_music_start + (v == 1 ? -5 : 5), 0, 3600);
            save_settings();
            music_play();
            music_fade_later();
        } else if (v == 3) {
            music_stop();
            g_music[0] = 0;
            save_settings();
            ui_set_status(L"Loading music turned off.");
        }
        ui_redraw();
        break;
    case H_PLOAD: {
        wchar_t m[64];
        if (load_profile(v)) { apply(); wsprintfW(m, L"Loaded Profile %d.", v + 1); ui_set_status(m); }
        break;
    }
    case H_PSAVE: {
        wchar_t m[64];
        save_profile(v);
        wsprintfW(m, L"Saved your current setup to Profile %d.", v + 1);
        ui_set_status(m);
        break;
    }
    case H_KFKEY:
        g_capture = -1;
        fkey_binds();
        save_binds();
        kb_check();
        ui_set_status(L"F-key layout on: F6/F7 change crosshair/color, F8 hides, F9 centers, F10 cursor.");
        break;
    case H_BUTTON:
        if (v == B_SHOWKEYS) card_toggle();
        else if (v == B_RBX) {
            if (S.hidecur) rbx_all(1);
            show_roblox_status();
        } else if (v == B_UPDATE) {
            if (g_upd_state == UPD_READY) { if (update_install()) { g_exit_why = L"restart to update"; DestroyWindow(g_host); return; } ui_set_status(L"Couldn't update in place. Try Run as administrator, or download it from the Discord."); }
            else { update_check(1); ui_set_status(L"Checking for updates..."); }
        } else if (v == B_COPYCODE) {
            wchar_t code[32], m[96];
            xh_code(code);
            clip_set(h, code);
            wsprintfW(m, L"Copied %s \x2014 paste it in Discord so friends can use your crosshair.", code);
            ui_set_status(m);
        } else if (v == B_PASTECODE) {
            wchar_t in[128];
            if (!clip_get(h, in, 128) || !in[0]) ui_set_status(L"Copy a crosshair code first (it looks like CO-XXXX-XXXX-XXXXX), then click Paste code.");
            else if (!xh_code_apply(in)) ui_set_status(L"That isn't a valid crosshair code. Codes look like CO-XXXX-XXXX-XXXXX.");
            else {
                wchar_t norm[32];
                apply();
                xh_code(norm);
                recent_add(norm);
                ui_set_status(L"Crosshair loaded from the code and added to Recent codes.");
            }
        } else if (v == B_UNINSTALL) {
            uninstall_all();
            return;
        } else if (v == B_REPORT) {
            report_dialog(-1);
        } else if (v == B_DEBUG) {
            wchar_t *t = debug_info_text();
            if (t) { clip_set(h, t); free(t); ui_set_status(L"Debug info copied \x2014 paste it in Discord (Ctrl+V)."); crumb(L"copied debug info"); }
        } else if (v == B_EXPORT) {
            settings_export(h);
        } else if (v == B_IMPORT) {
            settings_import(h);
        } else if (v == B_TESTOK) {
            if (wcscmp(g_tester_ok, VER_WSTR)) {
                HANDLE th = CreateThread(NULL, 0, tester_ok_thread, NULL, 0, NULL);
                if (th) { CloseHandle(th); ui_set_status(L"Sending..."); }
            }
        } else if (v == B_TESTBAD) {
            report_dialog(NRCAT - 1);
        } else if (v == B_EXIT) { g_exit_why = L"Exit app button"; DestroyWindow(g_host); }
        break;
    }
}

static LRESULT CALLBACK ui_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: ui_paint(h); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_ACTIVATEAPP:
        /* You clicked into the game (or any other app): close the panel so the
           crosshair, cursor hiding and shortcuts all switch back on. */
        if (!wp && g_capture == -1) PostMessageW(h, WM_CLOSE, 0, 0);
        return 0;
    case WM_TIMER:
        if (wp == 9) { /* hover tip delay */
            KillTimer(h, 9);
            if (g_tip_hover[0] && tip_for(g_tip_hover) && g_drag < 0) { GetCursorPos(&g_tip_pt); ScreenToClient(h, &g_tip_pt); g_tip_show = 1; ui_redraw(); }
            return 0;
        }
        if (wp == 8) { /* animate the live preview */
            if (g_flash_title[0]) { ui_redraw(); return 0; } /* a search result is glowing */
            if (g_kick > 0) kick_update();
            if ((g_tab == TAB_FX || g_tab == TAB_LOOK) && (S.anim || g_kick > 0)) ui_redraw();
            else if (deco_animated(g_th.deco)) { /* animated theme background */
                static DWORD last;
                DWORD now = GetTickCount();
                if (now - last >= 66) { last = now; ui_redraw(); } /* ~15 fps is plenty for slow smoke and glow */
            }
            return 0;
        }
        if (wp == 7) {
            g_ui_alpha += 36;
            if (g_ui_alpha >= 255) {
                KillTimer(h, 7);
                SetLayeredWindowAttributes(h, 0, 255, LWA_ALPHA);
                SetWindowLongW(h, GWL_EXSTYLE, GetWindowLongW(h, GWL_EXSTYLE) & ~WS_EX_LAYERED);
                RedrawWindow(h, NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
            } else
                SetLayeredWindowAttributes(h, 0, (BYTE)g_ui_alpha, LWA_ALPHA);
        }
        return 0;
    case WM_NCHITTEST: {
        LRESULT r = DefWindowProcW(h, msg, wp, lp);
        if (r == HTCLIENT) {
            POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(h, &p);
            if (p.y < dpx(UI_HEAD) && !hit_at(p.x, p.y)) return HTCAPTION; /* drag the window */
        }
        return r;
    }
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            POINT p;
            GetCursorPos(&p);
            ScreenToClient(h, &p);
            SetCursor(LoadCursorW(NULL, (LPCWSTR)(hit_at(p.x, p.y) ? IDC_HAND : IDC_ARROW)));
            return TRUE;
        }
        break;
    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (!g_tracking) {
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, h, 0};
            TrackMouseEvent(&tme);
            g_tracking = 1;
        }
        if (g_drag >= 0) { slider_set(g_drag, x); return 0; }
        const Hit *ht = hit_at(x, y);
        int k = ht ? ht->kind : H_NONE, v = ht ? ht->val : -1;
        if (k != g_hk || v != g_hv) { g_hk = k; g_hv = v; ui_redraw(); }
        { /* hover tips: rest on a setting for a moment */
            const wchar_t *t = L"";
            for (int i = g_nrows - 1; i >= 0; i--)
                if (x >= g_rows[i].r.left && x < g_rows[i].r.right && y >= g_rows[i].r.top && y < g_rows[i].r.bottom) { t = g_rows[i].title; break; }
            if (g_search_on && g_search[0]) t = L"";
#ifdef CH_TEST
            if (GetEnvironmentVariableW(L"CH_TIP", NULL, 0)) t = g_tip_hover;
#endif
            if (wcscmp(t, g_tip_hover)) {
                lstrcpynW(g_tip_hover, t, 64);
                if (g_tip_show) { g_tip_show = 0; ui_redraw(); }
                if (t[0] && tip_for(t)) SetTimer(h, 9, 650, NULL);
                else KillTimer(h, 9);
            }
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_tracking = 0;
        g_tip_hover[0] = 0;
        KillTimer(h, 9);
        if (g_tip_show) { g_tip_show = 0; ui_redraw(); }
        if (g_hk != H_NONE && g_drag < 0) { g_hk = H_NONE; g_hv = -1; ui_redraw(); }
        return 0;
    case WM_LBUTTONDOWN: ui_click(h, GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); return 0;
    case WM_LBUTTONUP:
        if (g_drag >= 0) { g_drag = -1; ReleaseCapture(); ui_redraw(); }
        return 0;
    case WM_MOUSEWHEEL:
        if (g_tip_show) { g_tip_show = 0; ui_redraw(); }
        if (g_tab == TAB_UPDATES) {
            g_uscroll = clampi(g_uscroll - GET_WHEEL_DELTA_WPARAM(wp) * dpx(64) / WHEEL_DELTA, 0, g_uscroll_max);
            ui_redraw();
        }
        if (g_tab == TAB_KEYS) {
            g_kscroll -= GET_WHEEL_DELTA_WPARAM(wp) * dpx(64) / WHEEL_DELTA;
            g_kscroll = clampi(g_kscroll, 0, g_kscroll_max);
            ui_redraw();
        }
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (g_capture >= 0) { kb_capture((UINT)wp); return 0; }
        if (g_tour) {
            if (wp == VK_ESCAPE) tour_end();
            else if (wp == VK_RETURN || wp == VK_RIGHT) { if (++g_tour > 3) tour_end(); else ui_redraw(); }
            return 0;
        }
        if (wp == 'F' && (GetKeyState(VK_CONTROL) & 0x8000)) { g_search_on = 1; ui_redraw(); return 0; }
        if (wp == VK_F1) { report_dialog(-1); return 0; } /* F1 = report a problem */
        if (g_search_on) {
            if (wp == VK_ESCAPE) { if (g_search[0]) { g_search[0] = 0; g_nsres = 0; } else g_search_on = 0; }
            else if (wp == VK_RETURN) { if (g_nsres) search_go(g_search_sel); return 0; }
            else if (wp == VK_DOWN && g_nsres) g_search_sel = (g_search_sel + 1) % g_nsres;
            else if (wp == VK_UP && g_nsres) g_search_sel = (g_search_sel + g_nsres - 1) % g_nsres;
            else if (wp == VK_BACK) { int n = lstrlenW(g_search); if (n) { g_search[n - 1] = 0; search_update(); } }
            ui_redraw();
            return 0; /* typing goes to the search box, not the tab shortcuts */
        }
        if (wp == VK_ESCAPE) { DestroyWindow(h); return 0; }
        if (wp == VK_TAB) {
            g_tab = (g_tab + ((GetKeyState(VK_SHIFT) & 0x8000) ? NTABS - 1 : 1)) % NTABS;
            g_ui_status[0] = 0;
            ui_redraw();
            return 0;
        }
        if (wp >= '1' && wp < '1' + NTABS && !(GetKeyState(VK_CONTROL) & 0x8000)) { g_tab = (int)(wp - '1'); g_ui_status[0] = 0; ui_redraw(); return 0; }
        break;
    case WM_CHAR:
        if (g_capture >= 0 || g_tour) return 0;
        if (wp >= 32 && wp != 127 && !(GetKeyState(VK_CONTROL) & 0x8000)) {
            /* typing a letter anywhere starts a search */
            if (!g_search_on && !iswalpha((wint_t)wp)) return 0;
            g_search_on = 1;
            int n = lstrlenW(g_search);
            if (n < 38) { g_search[n] = (wchar_t)wp; g_search[n + 1] = 0; }
            search_update();
            ui_redraw();
        }
        return 0;
    case WM_SYSKEYUP:
    case WM_SYSCHAR:
        if (g_capture >= 0) return 0;
        break;
    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_KEYMENU) return 0;
        break;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY:
        ini_set(L"last_tab", g_tab); /* open on the same tab next time */
        crumb(L"closed the settings panel");
        g_keydlg = NULL;
        g_capture = -1;
        g_search_on = 0;
        g_search[0] = 0;
        g_tip_show = 0;
        g_tip_hover[0] = 0;
        g_drag = -1;
        g_tracking = 0;
        register_hotkeys(g_host); /* turn the hotkeys back on */
        if (g_ui_shrink != 100) { g_ui_shrink = 100; make_fonts(); } /* normal size for everything else */
        g_menu_open = 0;
        if (!S.hidden && !g_focus_hidden && !g_scene_hidden) ShowWindow(g_overlay, SW_SHOWNOACTIVATE);
        InterlockedExchange(&g_force_move, 1);
        sync_cursor();
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

/* Windows 11: rounded corners, border in the theme's color */
static void ui_dwm(HWND h) {
    static HMODULE dwm;
    if (!dwm) dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return;
    typedef HRESULT(WINAPI * SetAttr)(HWND, DWORD, LPCVOID, DWORD);
    SetAttr f = (SetAttr)(void *)GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (!f) return;
    int round = g_th.radius < 0.5f ? 3 : 2, dark = !theme_is_light(); /* 3 = small corners */
    COLORREF border = C_BORDER;
    f(h, 33, &round, sizeof(round));
    f(h, 20, &dark, sizeof(dark));
    f(h, 34, &border, sizeof(border));
}

static void ui_open(int tab) {
    static int first = 1;
    if (first) { first = 0; g_tab = g_last_tab; } /* the tab you used last time */
    if (tab >= 0) g_tab = tab;
    if (!g_keydlg && g_first_run_ini && !ini_get(L"tour_done", 0)) { g_tour = 1; g_tab = TAB_CROSSHAIR; }
    if (!g_keydlg) crumb(L"opened the settings panel");
#ifdef CH_TEST
    { wchar_t tv[4]; if (!g_keydlg && GetEnvironmentVariableW(L"CH_TOUR", tv, 4)) { g_tour = _wtoi(tv); g_tab = TAB_CROSSHAIR; } }
    { wchar_t tv[8]; if (!g_keydlg && GetEnvironmentVariableW(L"CH_LAYER", tv, 8)) { g_edit_l2 = 1; S.l2_style = _wtoi(tv); S.l2_color = 5; } }
    { wchar_t tv[40]; if (!g_keydlg && GetEnvironmentVariableW(L"CH_SEARCH", tv, 40)) { g_search_on = 1; lstrcpynW(g_search, tv, 40); search_update(); } }
    { wchar_t tv[64]; if (!g_keydlg && GetEnvironmentVariableW(L"CH_TIP", tv, 64)) { lstrcpynW(g_tip_hover, tv, 64); g_tip_show = 1; g_tip_pt.x = dpx(420); g_tip_pt.y = dpx(260); } }
#endif
    if (g_keydlg) {
        ShowWindow(g_keydlg, SW_SHOWNORMAL);
        SetForegroundWindow(g_keydlg);
        ui_redraw();
        return;
    }
    card_hide();
    unregister_hotkeys(g_host); /* so keys reach the window while you edit them */
    g_menu_open = 1;
    if (g_cursor_hidden) restore_cursor();
    if (!S.hidden) ShowWindow(g_overlay, SW_HIDE);
    g_ui_status[0] = 0;
    g_capture = -1;
    g_hk = H_NONE;
    g_hv = -1;
    g_kscroll = 0;

    { /* too tall for this screen (e.g. 1366x768 laptops)? draw the whole window a bit smaller */
        POINT pc;
        MONITORINFO mi2;
        GetCursorPos(&pc);
        mi2.cbSize = sizeof(mi2);
        if (GetMonitorInfoW(MonitorFromPoint(pc, MONITOR_DEFAULTTOPRIMARY), &mi2)) {
            int avail = mi2.rcWork.bottom - mi2.rcWork.top - 16;
            g_ui_shrink = UI_SIZES[g_ui_size];
            make_fonts();
            int need = dpx(UI_H);
            if (need > avail && need > 0) { g_ui_shrink = clampi(g_ui_shrink * avail / need, 60, 115); make_fonts(); }
        }
    }
    int w = dpx(UI_W), hh = dpx(UI_H);
    POINT c;
    GetCursorPos(&c);
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    RECT wa = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    if (GetMonitorInfoW(MonitorFromPoint(c, MONITOR_DEFAULTTOPRIMARY), &mi)) wa = mi.rcWork;
    g_keydlg = CreateWindowExW(WS_EX_TOPMOST | WS_EX_APPWINDOW | WS_EX_LAYERED, L"CrosshairSettings", APP_NAME,
                               WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX,
                               wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - hh) / 2,
                               w, hh, NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!g_keydlg) {
        register_hotkeys(g_host);
        g_menu_open = 0;
        if (!S.hidden && !g_focus_hidden && !g_scene_hidden) ShowWindow(g_overlay, SW_SHOWNOACTIVATE);
        return;
    }
    ui_dwm(g_keydlg);
    kb_check();
    g_ui_alpha = 0;
    SetLayeredWindowAttributes(g_keydlg, 0, 0, LWA_ALPHA);
    ShowWindow(g_keydlg, SW_SHOWNORMAL);
    SetForegroundWindow(g_keydlg);
    SetTimer(g_keydlg, 7, 15, NULL); /* fade in */
    SetTimer(g_keydlg, 8, 33, NULL); /* preview animation */
}

static void ui_toggle(void) {
    if (g_keydlg) DestroyWindow(g_keydlg);
    else ui_open(-1);
}

/* ---------------- on-screen keybinds card ---------------- */
/* Pops up when you press the "show keybinds" key, then fades out by itself. */
#define CARD_SECONDS 6
static int g_card_vis, g_card_alpha;
static DWORD g_card_t;
static HFONT g_cf_title, g_cf_text, g_cf_bold, g_cf_small;

static HFONT card_font(int pt, int weight) {
    return CreateFontW(-MulDiv(pt, g_dpi, 72), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH,
                       g_font_face);
}
static void card_fonts_reset(void) { /* theme changed: fonts are rebuilt on the next draw */
    HFONT f[4] = {g_cf_title, g_cf_text, g_cf_bold, g_cf_small};
    for (int i = 0; i < 4; i++) if (f[i]) DeleteObject(f[i]);
    g_cf_title = g_cf_text = g_cf_bold = g_cf_small = NULL;
}

static void text_at(HDC dc, const wchar_t *t, int x0, int y0, int x1, int y1, UINT fmt) {
    RECT r = {x0, y0, x1, y1};
    DrawTextW(dc, t, -1, &r, fmt | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
}

static void card_draw(void) {
    if (!g_card) return;
    if (!g_cf_title) {
        g_cf_title = card_font(13, FW_SEMIBOLD);
        g_cf_text = card_font(10, FW_NORMAL);
        g_cf_bold = card_font(10, FW_SEMIBOLD);
        g_cf_small = card_font(9, FW_NORMAL);
    }
    int rows = 0;
    for (int i = 0; i < NACT; i++)
        if (KB[i].vk) rows++;
    int W = dpx(390), rowh = dpx(24), top = dpx(68), pad = dpx(18);
    int H = top + rows * rowh + dpx(70);

    HDC screen = GetDC(NULL);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -H;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void *bits = NULL;
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp || !bits) { DeleteDC(mem); ReleaseDC(NULL, screen); return; }
    HGDIOBJ oldb = SelectObject(mem, bmp);

    RECT r = {0, 0, W, H};
    HBRUSH br = CreateSolidBrush(lerpc(C_BG, C_CARD, 0.5f));
    FillRect(mem, &r, br);
    DeleteObject(br);
    if (g_th.deco == DECO_PATROL) { /* red | blue lightbar stripe */
        RECT a1 = {0, 0, W / 2, dpx(3)}, a2 = {W / 2, 0, W, dpx(3)};
        br = CreateSolidBrush(C_ACC2); FillRect(mem, &a1, br); DeleteObject(br);
        br = CreateSolidBrush(C_ACCENT); FillRect(mem, &a2, br); DeleteObject(br);
    } else {
        RECT ac = {0, 0, W, dpx(3)};
        br = CreateSolidBrush(C_ACCENT);
        FillRect(mem, &ac, br);
        DeleteObject(br);
    }

    SetBkMode(mem, TRANSPARENT);
    HGDIOBJ oldf = SelectObject(mem, g_cf_title);
    SetTextColor(mem, C_TEXT);
    text_at(mem, L"Keybinds", pad, dpx(12), W - pad, dpx(40), DT_LEFT);
    SelectObject(mem, g_cf_small);
    SetTextColor(mem, C_ACCLT);
    text_at(mem, APP_NAME L"  \x2022  by simplysoul", pad, dpx(38), W - pad, dpx(58), DT_LEFT);

    int y = top;
    wchar_t k[64], nm[96];
    for (int i = 0; i < NACT; i++) {
        if (!KB[i].vk) continue;
        key_text(KB[i], k);
        action_name(i, nm);
        if (g_bind_fail[i]) wcscat(nm, L"  (in use!)");
        SelectObject(mem, g_cf_bold);
        SetTextColor(mem, C_TEXT);
        text_at(mem, k, pad, y, dpx(150), y + rowh, DT_RIGHT);
        SelectObject(mem, g_cf_text);
        SetTextColor(mem, g_bind_fail[i] ? RGB(255, 110, 110) : C_MUTED);
        text_at(mem, nm, dpx(166), y, W - pad, y + rowh, DT_LEFT);
        y += rowh;
    }
    if (!rows) {
        SelectObject(mem, g_cf_text);
        SetTextColor(mem, C_MUTED);
        text_at(mem, L"No keybinds set. Open settings \x2192 Keybinds", pad, y, W - pad, y + rowh, DT_LEFT);
    }

    y += dpx(10);
    RECT sep = {pad, y, W - pad, y + 1};
    br = CreateSolidBrush(C_BORDER);
    FillRect(mem, &sep, br);
    DeleteObject(br);

    wchar_t line[200];
    SelectObject(mem, g_cf_small);
    SetTextColor(mem, C_MUTED);
    wsprintfW(line, L"Now:  %s  \x2022  %s  \x2022  %d px%s%s", STYLE_NAMES[S.style], COLORS[S.color].name,
              SIZES[S.size], S.cover ? L"  \x2022  cover on" : L"", S.hidden ? L"  \x2022  hidden" : L"");
    text_at(mem, line, pad, y + dpx(8), W - pad, y + dpx(30), DT_LEFT);
    if (KB[A_SHOWKEYS].vk) {
        key_text(KB[A_SHOWKEYS], k);
        wsprintfW(line, L"Hides by itself  \x2022  %s to close  \x2022  change keys in settings", k);
    } else
        wcscpy(line, L"Hides by itself  \x2022  change keys in settings");
    SetTextColor(mem, C_DIM);
    text_at(mem, line, pad, y + dpx(30), W - pad, y + dpx(52), DT_LEFT);
    SelectObject(mem, oldf);
    GdiFlush();

    /* rounded corners + slight see-through, premultiplied for the layered window */
    uint32_t *px = (uint32_t *)bits;
    float rr = (float)dpx(14) * g_th.radius;
    for (int yy = 0; yy < H; yy++)
        for (int xx = 0; xx < W; xx++) {
            float cx = xx + 0.5f, cy = yy + 0.5f, dxc = 0, dyc = 0, cov = 1.0f;
            if (cx < rr) dxc = rr - cx; else if (cx > W - rr) dxc = cx - (W - rr);
            if (cy < rr) dyc = rr - cy; else if (cy > H - rr) dyc = cy - (H - rr);
            if (dxc > 0 && dyc > 0) {
                cov = rr + 0.5f - sqrtf(dxc * dxc + dyc * dyc);
                cov = cov < 0 ? 0 : cov > 1 ? 1 : cov;
            }
            uint32_t A = (uint32_t)(238.0f * cov + 0.5f), p = px[yy * W + xx];
            uint32_t R = ((p >> 16) & 255) * A / 255, G = ((p >> 8) & 255) * A / 255,
                     Bc = (p & 255) * A / 255;
            px[yy * W + xx] = (A << 24) | (R << 16) | (G << 8) | Bc;
        }

    POINT c;
    GetCursorPos(&c);
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    RECT wa = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    if (GetMonitorInfoW(MonitorFromPoint(c, MONITOR_DEFAULTTOPRIMARY), &mi)) wa = mi.rcWork;
    POINT pos = {wa.left + (wa.right - wa.left - W) / 2, wa.top + dpx(70)};
    SIZE sz = {W, H};
    POINT src = {0, 0};
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, (BYTE)g_card_alpha, AC_SRC_ALPHA};
    UpdateLayeredWindow(g_card, screen, &pos, &sz, mem, &src, 0, &bf, ULW_ALPHA);

    SelectObject(mem, oldb);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
}

static void card_hide(void) {
    if (!g_card_vis) return;
    KillTimer(g_card, 1);
    ShowWindow(g_card, SW_HIDE);
    g_card_vis = 0;
}

static void card_toggle(void) {
    if (!g_card) return;
    if (g_card_vis) { card_hide(); return; }
    g_card_alpha = 255;
    card_draw();
    ShowWindow(g_card, SW_SHOWNOACTIVATE);
    SetWindowPos(g_card, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    g_card_t = GetTickCount();
    g_card_vis = 1;
    SetTimer(g_card, 1, 30, NULL);
}

static void card_refresh(void) {
    if (g_card_vis) card_draw();
}

static LRESULT CALLBACK card_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_TIMER:
        if (GetTickCount() - g_card_t > CARD_SECONDS * 1000) {
            g_card_alpha -= 20;
            if (g_card_alpha <= 0) { card_hide(); return 0; }
            BLENDFUNCTION bf = {AC_SRC_OVER, 0, (BYTE)g_card_alpha, AC_SRC_ALPHA};
            UpdateLayeredWindow(h, NULL, NULL, NULL, NULL, NULL, 0, &bf, ULW_ALPHA);
        }
        return 0;
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    }
    return DefWindowProcW(h, msg, wp, lp);
}


/* ---------------- tray menu (kept short; everything else is in the settings window) ---------------- */
enum { ID_OPEN = 1000, ID_SHOWKEYS, ID_HIDE, ID_ABOUT, ID_EXIT, ID_REPORT, ID_PUBLISH, ID_UPDATE, ID_RBXTIPS, ID_TESTERID, ID_COPYCODE };

static void show_menu(void) {
    HMENU m = CreatePopupMenu();
    wchar_t buf[160];
    AppendMenuW(m, MF_STRING | MF_GRAYED, ID_ABOUT, APP_NAME L"  \x2022  by simplysoul");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    with_key(buf, L"Open settings", A_SETTINGS);
    AppendMenuW(m, MF_STRING, ID_OPEN, buf);
    with_key(buf, L"Show keybinds on screen", A_SHOWKEYS);
    AppendMenuW(m, MF_STRING, ID_SHOWKEYS, buf);
    with_key(buf, L"Hide crosshair", A_HIDE);
    AppendMenuW(m, MF_STRING | (S.hidden ? MF_CHECKED : 0), ID_HIDE, buf);
    AppendMenuW(m, MF_STRING, ID_REPORT, L"Report a problem...");
    if (g_rbx_last) AppendMenuW(m, MF_STRING, ID_RBXTIPS, L"Why did Roblox crash?");
    AppendMenuW(m, MF_STRING, ID_COPYCODE, L"Copy crosshair code");
    if (updates_set()) AppendMenuW(m, MF_STRING, ID_TESTERID, g_is_tester ? L"Copy my tester ID (you're a tester)" : L"Copy my tester ID");
    if (g_upd_state == UPD_READY) { wsprintfW(buf, L"Restart to update to v%s", g_upd_ver); AppendMenuW(m, MF_STRING, ID_UPDATE, buf); }
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    with_key(buf, L"Exit", A_QUIT);
    AppendMenuW(m, MF_STRING, ID_EXIT, buf);
    SetMenuDefaultItem(m, ID_OPEN, FALSE);

    POINT pt;
    GetCursorPos(&pt);
    g_menu_open = 1;
    if (g_cursor_hidden) restore_cursor();
    SetForegroundWindow(g_host);
    if (!S.hidden && !g_keydlg) ShowWindow(g_overlay, SW_HIDE); /* don't cover the menu */
    UINT cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, g_host, NULL);
    if (!S.hidden && !g_focus_hidden && !g_scene_hidden && !g_keydlg) ShowWindow(g_overlay, SW_SHOWNOACTIVATE);
    PostMessageW(g_host, WM_NULL, 0, 0);
    DestroyMenu(m);
    g_menu_open = (g_keydlg != NULL);

    switch (cmd) {
    case ID_OPEN: ui_open(-1); return;
    case ID_SHOWKEYS: card_toggle(); break;
    case ID_HIDE: set_hidden(!S.hidden); break;
    case ID_EXIT: g_exit_why = L"tray menu Exit"; DestroyWindow(g_host); return;
    case ID_REPORT: report_dialog(-1); return;
    case ID_RBXTIPS: rbx_crash_details(); return;
    case ID_COPYCODE: {
        wchar_t code[32];
        xh_code(code);
        clip_set(g_host, code);
        g_nid.uFlags = NIF_INFO;
        wsprintfW(g_nid.szInfoTitle, L"Copied %s", code);
        wcscpy(g_nid.szInfo, L"Paste it in Discord. Friends use Profiles > Paste code to get your crosshair.");
        g_nid.dwInfoFlags = NIIF_INFO;
        Shell_NotifyIconW(NIM_MODIFY, &g_nid);
        g_nid.szInfo[0] = 0;
        return;
    }
    case ID_TESTERID: { /* the install id, for the developer's tester list */
        size_t n = (wcslen(g_report_id) + 1) * sizeof(wchar_t);
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, n);
        if (hg && OpenClipboard(g_host)) {
            memcpy(GlobalLock(hg), g_report_id, n);
            GlobalUnlock(hg);
            EmptyClipboard();
            if (!SetClipboardData(CF_UNICODETEXT, hg)) GlobalFree(hg);
            CloseClipboard();
        } else if (hg) GlobalFree(hg);
        g_nid.uFlags = NIF_INFO;
        wsprintfW(g_nid.szInfoTitle, L"Tester ID copied: %s", g_report_id);
        wcscpy(g_nid.szInfo, L"Send it to simplysoul to become a Q/A tester. Testers get new versions early.");
        g_nid.dwInfoFlags = NIIF_INFO;
        Shell_NotifyIconW(NIM_MODIFY, &g_nid);
        g_nid.szInfo[0] = 0;
        return;
    }
    case ID_UPDATE: if (update_install()) { g_exit_why = L"update from tray"; DestroyWindow(g_host); } return;
    }
    sync_cursor();
}

static void on_hotkey(int id) {
    int a = id - 1;
    if (a >= 0 && a < A_STYLE0) crumb(L"keybind: %s", ACTION_NAMES[a]);
    else crumb(L"keybind: crosshair style");
    if (a >= A_STYLE0 && a < NACT) S.style = style_fix(a - A_STYLE0);
    else {
    switch (a) {
    case A_NEXT:   S.style = style_step(S.style, 1); break;
    case A_PREV:   S.style = style_step(S.style, -1); break;
    case A_CNEXT:  S.color = (S.color + 1) % NCOLORS; break;
    case A_CPREV:  S.color = (S.color + NCOLORS - 1) % NCOLORS; break;
    case A_SMALL:  if (S.size > 0) S.size--; break;
    case A_BIG:    if (S.size < NSIZES - 1) S.size++; break;
    case A_HIDE:   set_hidden(!S.hidden); return;
    case A_MODE:   S.center = !S.center; break;
    case A_CURSOR: S.hidecur = !S.hidecur; break;
    case A_QUIT:   g_exit_why = L"Exit keybind"; DestroyWindow(g_host); return;
    case A_SHOWKEYS: card_toggle(); return;
    case A_SETTINGS: ui_toggle(); return;
    case A_PROF1: case A_PROF2: case A_PROF3:
        if (!load_profile(a - A_PROF1)) return;
        break;
    case A_COVER:
        if (S.cover) { g_cover_last = S.cover; S.cover = 0; }
        else S.cover = g_cover_last;
        break;
    default: return;
    }
    }
    if (S.hidden) set_hidden(0);
    apply();
}

/* ---------------- mover thread ---------------- */
static void target_point(POINT *p) {
    if (S.center) {
        HWND fg = GetForegroundWindow();
        RECT rc;
        if (fg && fg != GetDesktopWindow() && fg != GetShellWindow() && GetClientRect(fg, &rc) &&
            rc.right > 100 && rc.bottom > 100) {
            POINT c = {rc.right / 2, rc.bottom / 2};
            ClientToScreen(fg, &c);
            p->x = c.x + g_nudge_x;
            p->y = c.y + g_nudge_y;
            return;
        }
        p->x = GetSystemMetrics(SM_CXSCREEN) / 2 + g_nudge_x;
        p->y = GetSystemMetrics(SM_CYSCREEN) / 2 + g_nudge_y;
        return;
    }
    GetCursorPos(p);
    p->x += g_nudge_x;
    p->y += g_nudge_y;
}

/* Update once per screen refresh (e.g. 144 times a second on a 144 Hz monitor)
   using a high-resolution timer, instead of spinning at 1000 Hz and raising
   the whole system's timer resolution. */
static int refresh_rate(void) {
    DEVMODEW dm = {0};
    dm.dmSize = sizeof(dm);
    int hz = 60;
    if (EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1)
        hz = (int)dm.dmDisplayFrequency;
    return clampi(hz, 30, 500);
}

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

static DWORD WINAPI mover(LPVOID unused) {
    (void)unused;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST); /* = Roblox's normal level */
    HANDLE timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                          TIMER_ALL_ACCESS);
    int coarse = 0;
    if (!timer) { timer = CreateWaitableTimerW(NULL, FALSE, NULL); coarse = 1; }
    if (coarse) timeBeginPeriod(1); /* only on old Windows without high-res timers */

    POINT last = {-100000, -100000};
    DWORD last_top = 0, last_hz = 0;
    LONG period_100ns = 10000000 / 60;
    while (g_running) {
        DWORD now = GetTickCount();
        if (now - last_hz > 5000) { period_100ns = 10000000 / refresh_rate(); last_hz = now; }

        if (!S.hidden) {
            POINT p;
            target_point(&p);
            if (p.x != last.x || p.y != last.y || InterlockedExchange(&g_force_move, 0)) {
                LONG w = g_wsz;
                SetWindowPos(g_overlay, NULL, p.x - w / 2, p.y - w / 2, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING |
                             SWP_ASYNCWINDOWPOS);
                last = p;
            }
            /* Force on top: every frame, check whether any visible window has
               been placed above the crosshair (a game, another overlay, a popup)
               and jump straight back to the very top. The check is cheap;
               re-raising is limited to 20 times a second so two always-on-top
               apps can't fight and waste CPU. */
            if (now - last_top >= 50) {
                HWND above = GetWindow(g_overlay, GW_HWNDPREV);
                while (above && (above == g_card || !IsWindowVisible(above)))
                    above = GetWindow(above, GW_HWNDPREV);
                if (above) {
                    SetWindowPos(g_overlay, HWND_TOPMOST, 0, 0, 0, 0,
                                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER |
                                     SWP_ASYNCWINDOWPOS);
                    last_top = now;
                }
            }
        }

        if (timer) {
            LARGE_INTEGER due;
            due.QuadPart = -(LONGLONG)(S.hidden ? 1000000 : period_100ns); /* 10 Hz when hidden */
            SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE);
            WaitForSingleObject(timer, 1000);
        } else {
            Sleep(S.hidden ? 100 : 4);
        }
    }
    if (timer) CloseHandle(timer);
    if (coarse) timeEndPeriod(1);
    return 0;
}

/* ---------------- Roblox update watcher ---------------- */
/* Roblox installs each update into a new folder with fresh cursor images, often
   right before it starts. Watch its folders and blank new cursor files the
   moment they're written, so they're hidden before Roblox loads them. */
static DWORD WINAPI rbx_watch(LPVOID unused) {
    (void)unused;
    while (g_running) {
        HANDLE hs[8];
        int n = 0;
        wchar_t la[MAX_PATH], pf[MAX_PATH], dir[MAX_PATH], ver[MAX_PATH];
        const wchar_t *bases[8];
        int nb = 0;
        static wchar_t store[8][MAX_PATH];
        DWORD l = GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH);
        if (l && l < MAX_PATH - 40) {
            static const wchar_t *LR[] = {L"Roblox", L"Bloxstrap", L"Fishstrap", L"Voidstrap"};
            for (int i = 0; i < 4; i++) { wsprintfW(store[nb], L"%s\\%s", la, LR[i]); bases[nb] = store[nb]; nb++; }
        }
        static const wchar_t *PF[] = {L"ProgramFiles(x86)", L"ProgramFiles"};
        for (int i = 0; i < 2; i++) {
            l = GetEnvironmentVariableW(PF[i], pf, MAX_PATH);
            if (l && l < MAX_PATH - 40) { wsprintfW(store[nb], L"%s\\Roblox", pf); bases[nb] = store[nb]; nb++; }
        }
        for (int b = 0; b < nb && n < 8; b++) {
            if (!dir_exists(bases[b])) continue;
            wsprintfW(ver, L"%s\\Versions", bases[b]);
            wcscpy(dir, dir_exists(ver) ? ver : bases[b]); /* Versions avoids Roblox's busy log folder */
            HANDLE h = FindFirstChangeNotificationW(dir, TRUE, FILE_NOTIFY_CHANGE_FILE_NAME |
                                                               FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE);
            if (h != INVALID_HANDLE_VALUE) hs[n++] = h;
        }
        DWORD start = GetTickCount();
        while (g_running && GetTickCount() - start < 30000) { /* re-check the folder list every 30 s */
            if (!n) { Sleep(1000); continue; }
            DWORD r = WaitForMultipleObjects(n, hs, FALSE, 1000);
            if (r < WAIT_OBJECT_0 + (DWORD)n) {
                Sleep(150); /* let a burst of writes finish */
                for (int i = 0; i < n; i++) FindNextChangeNotification(hs[i]);
                PostMessageW(g_host, WM_RBX_CHANGED, 0, 0);
            }
        }
        for (int i = 0; i < n; i++) FindCloseChangeNotification(hs[i]);
    }
    return 0;
}

/* ---------------- exclusive fullscreen warning ---------------- */
/* In "exclusive fullscreen" the game takes over the screen directly and Windows
   can't draw any other window on top, so the crosshair would vanish. Detect it
   and tell the user how to fix it (once each time it happens). */
static void check_exclusive_fullscreen(void) {
    static int warned;
    QUERY_USER_NOTIFICATION_STATE st;
    if (FAILED(SHQueryUserNotificationState(&st))) return;
    if (st == QUNS_RUNNING_D3D_FULL_SCREEN && !S.hidden) {
        if (!warned) {
            warned = 1;
            log_info(L"game is in exclusive fullscreen (overlay can't show)");
            g_nid.uFlags = NIF_INFO;
            wcscpy(g_nid.szInfoTitle, L"Crosshair can't show in exclusive fullscreen");
            wcscpy(g_nid.szInfo, L"The game is in exclusive fullscreen, which blocks every overlay. "
                                 L"Switch the game to borderless/windowed fullscreen "
                                 L"(in Roblox: press F11, then maximize the window).");
            g_nid.dwInfoFlags = NIIF_WARNING;
            Shell_NotifyIconW(NIM_MODIFY, &g_nid);
            g_nid.szInfo[0] = 0;
        }
    } else
        warned = 0;
}

/* ---------------- window procs ---------------- */
static UINT WM_TASKBARCREATED;

static LRESULT CALLBACK host_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TASKBARCREATED && WM_TASKBARCREATED) {
        g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
        Shell_NotifyIconW(NIM_ADD, &g_nid);
        return 0;
    }
    switch (msg) {
    case WM_TIMER: /* Windows sometimes resets cursors (UAC, display changes) */
        if (wp == MUSIC_FADE_TIMER) { music_fade_tick(); return 0; }
        if (wp == 2) { anim_tick(); return 0; }
        if (wp == PROB_TIMER) { KillTimer(h, PROB_TIMER); probation_pass(); return 0; }
        if (wp == SCENE_TIMER) { scene_tick(); return 0; }
#ifdef CH_TEST
        if (wp == 0x7777) { DestroyWindow(h); return 0; }
#endif
        if (wp == UPD_TIMER) { SetTimer(h, UPD_TIMER, 6 * 3600 * 1000, NULL); update_check(0); return 0; }
        if (!g_menu_open && S.hidecur && !S.hidden && !arrow_is_blank()) blank_cursor();
        check_exclusive_fullscreen();
        if (S.hidecur) rbx_all(1); /* backup check every 3 s (only rewrites files Roblox replaced) */
        perf_sync(0);
        return 0;
    case WM_RBX_CHANGED: /* Roblox wrote files (usually an update): blank its cursors right away */
        if (S.hidecur) rbx_all(1);
        perf_sync(1); /* new version folder: give it the boost / GPU choice too */
        return 0;
    case WM_DISPLAYCHANGE:
        InterlockedExchange(&g_force_move, 1);
        return 0;
    case WM_CLOSE: /* only sent by something outside the app (a newer copy taking over, Alt+F4, another program) */
        if (!g_exit_why) g_exit_why = L"asked to close by a newer copy, Alt+F4 or another program";
        DestroyWindow(h);
        return 0;
    case WM_ENDSESSION:
        if (wp) { g_exit_why = L"Windows shutting down / signing out"; log_info(L"exit (%s)", g_exit_why); }
        if (wp && g_probation) probation_pass();
        if (wp) { restore_cursor(); if (g_rbx_patched) rbx_all(0); }
        return 0;
    case WM_HOTKEY: on_hotkey((int)wp); return 0;
    case WM_TRAY:
        if (lp == NIN_BALLOONUSERCLICK && g_balloon_update) { g_balloon_update = 0; if (update_install()) { g_exit_why = L"update popup clicked"; DestroyWindow(g_host); } return 0; }
        if (lp == NIN_BALLOONUSERCLICK && g_balloon_rbx) { g_balloon_rbx = 0; rbx_crash_details(); return 0; }
        if (lp == NIN_BALLOONUSERCLICK && g_balloon_report) { g_balloon_report = 0; report_dialog(0); return 0; }
        if (lp == NIN_BALLOONTIMEOUT || lp == NIN_BALLOONHIDE) g_balloon_report = g_balloon_update = 0; /* the Roblox crash tips stay in the tray menu */
        if (lp == WM_LBUTTONUP) ui_open(-1);
        else if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) show_menu();
        return 0;
    case WM_RBX_CRASH: rbx_crash_report((RbxCrash *)lp); return 0;
    case WM_UPDATE_DONE:
        if (lp == UPD_READY && !wp && g_autoupd && GetTickCount() - g_launch_tick < 3 * 60 * 1000) {
            /* found while the app was just starting: update right now, so every launch runs the newest version */
            g_nid.uFlags = NIF_INFO;
            wsprintfW(g_nid.szInfoTitle, L"Updating to v%s...", g_upd_ver);
            wcscpy(g_nid.szInfo, L"Crosshair Overlay is installing the newest version and will reopen in a moment.");
            g_nid.dwInfoFlags = NIIF_INFO;
            Shell_NotifyIconW(NIM_MODIFY, &g_nid);
            g_nid.szInfo[0] = 0;
            log_info(L"update: found v%s at launch, installing now", g_upd_ver);
            if (update_install()) { g_exit_why = L"installing update"; DestroyWindow(g_host); return 0; }
        }
        if (lp == UPD_READY) {
            if (wp && g_keydlg) ui_set_status(L"Update downloaded. Click \"Restart to update\" on the Updates tab.");
            g_nid.uFlags = NIF_INFO;
            wsprintfW(g_nid.szInfoTitle, L"Crosshair Overlay v%s is ready", g_upd_ver);
            wcscpy(g_nid.szInfo, L"Click here to restart and update now, or it updates by itself next time you open the app.");
            g_nid.dwInfoFlags = NIIF_INFO;
            Shell_NotifyIconW(NIM_MODIFY, &g_nid);
            g_nid.szInfo[0] = 0;
            g_balloon_update = 1;
            g_balloon_report = 0;
        } else if (wp && g_keydlg && lp != UPD_CHECKING)
            ui_set_status(lp == UPD_LATEST ? L"You're on the newest version." : L"Couldn't check for updates (no internet?). It'll try again later.");
        ui_redraw();
        return 0;
    case WM_SAVE_LATER:
        save_settings();
        ui_redraw();
        return 0;
    case WM_TESTER_OK:
        if (wp) { lstrcpynW(g_tester_ok, VER_WSTR, 24); save_settings(); ui_set_status(L"Sent to simplysoul \x2014 thanks for testing!"); }
        else ui_set_status(L"Couldn't send it (no internet?). Try again in a bit.");
        ui_redraw();
        return 0;
    case WM_NEWS:
        if (g_news_hash && g_news_hash != g_news_seen) { /* new announcement: a popup once */
            g_news_seen = g_news_hash;
            save_settings();
            g_nid.uFlags = NIF_INFO;
            wcscpy(g_nid.szInfoTitle, L"Announcement from simplysoul");
            lstrcpynW(g_nid.szInfo, g_news, 256);
            g_nid.dwInfoFlags = NIIF_INFO;
            Shell_NotifyIconW(NIM_MODIFY, &g_nid);
            g_nid.szInfo[0] = 0;
            g_balloon_update = 0;
            g_balloon_report = 0;
        }
        ui_redraw();
        return 0;
    case WM_REPORT_DONE:
        if (lp && g_keydlg) {
            wchar_t m2[160];
            wsprintfW(m2, L"Report sent \x2014 thanks! Your reference: %s (quote it if you talk to simplysoul).", g_last_ref);
            ui_set_status(wp ? m2 : L"Couldn't send the report (no internet?). Try again in a bit.");
        }
        else if (lp) { /* sent from the tray: say so with a balloon */
            g_nid.uFlags = NIF_INFO;
            wcscpy(g_nid.szInfoTitle, wp ? L"Report sent" : L"Couldn't send the report");
            if (wp) wsprintfW(g_nid.szInfo, L"Thanks! simplysoul got your report. Your reference: %s", g_last_ref);
            else wcscpy(g_nid.szInfo, L"Check your internet and try again in a bit.");
            g_nid.dwInfoFlags = wp ? NIIF_INFO : NIIF_WARNING;
            Shell_NotifyIconW(NIM_MODIFY, &g_nid);
            g_nid.szInfo[0] = 0;
        }
        return 0;
    case WM_DESTROY:
        log_info(L"exit (%s)", g_exit_why ? g_exit_why : L"closed by Windows or another program");
        if (g_probation) probation_pass(); /* closed normally (any time): this version works. Only crashes / killed runs count against it */
        music_stop();
        InterlockedExchange(&g_running, 0);
        KillTimer(h, 1);
        restore_cursor();
        if (g_rbx_patched) rbx_all(0);
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static LRESULT CALLBACK overlay_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(h, msg, wp, lp);
}

static void set_dpi_aware(void) {
    HMODULE u = GetModuleHandleW(L"user32.dll");
    typedef BOOL(WINAPI * Fn)(HANDLE);
    Fn f = u ? (Fn)(void *)GetProcAddress(u, "SetProcessDpiAwarenessContext") : NULL;
    if (!f || !f((HANDLE)-4 /* PER_MONITOR_AWARE_V2 */)) SetProcessDPIAware();
}

/* ---------------- startup ---------------- */
#define NSTARTUP 4
#ifdef CH_TEST
static int g_fake_menu;
static LRESULT CALLBACK fake_rbx_proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_KEYDOWN && wp == VK_ESCAPE) { g_fake_menu = !g_fake_menu; InvalidateRect(h, NULL, TRUE); return 0; }
    if (m == WM_ERASEBKGND) {
        RECT r;
        GetClientRect(h, &r);
        HBRUSH b = CreateSolidBrush(g_fake_menu ? RGB(60, 70, 80) : RGB(170, 200, 230));
        FillRect((HDC)wp, &r, b);
        DeleteObject(b);
        return 1;
    }
    return DefWindowProcW(h, m, wp, lp);
}
#endif
static void startup_step(int i) {
    switch (i) {
    case 0:
        load_settings();
        load_binds();
        if (S.theme != -1) { Theme cur = g_th; theme_get(S.theme, &cur); if (memcmp(&cur, &g_th, sizeof(cur))) theme_set(S.theme); }
        break;
    case 1:
        redraw_overlay();
        fx_timer_sync();
        scene_timer_sync();
        g_nid.cbSize = sizeof(g_nid);
        g_nid.hWnd = g_host;
        g_nid.uID = 1;
        g_nid.uCallbackMessage = WM_TRAY;
        g_icon = make_icon();
        g_nid.hIcon = g_icon;
        g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
        wsprintfW(g_nid.szTip, L"Crosshair: %s, %s\nv" VER_WSTR L" by simplysoul", STYLE_NAMES[S.style], COLORS[S.color].name);
        Shell_NotifyIconW(NIM_ADD, &g_nid);
        break;
    case 2:
        register_hotkeys(g_host);
        break;
    case 3:
        sync_cursor();
        sync_roblox();
        fps_repair();
        if (g_fps > 0) fps_apply(1);
        if (g_boost || g_gpu || !ini_get(L"retired_flags_cleaned", 0)) { perf_sync_installs(); ini_set(L"retired_flags_cleaned", 1); }
        break;
    }
}

/* ---------------- background picture (Chosen Company theme) ---------------- */
/* A JPEG stored in the exe (resource 10) is decoded with Windows' own GDI+ and
   scaled to the loading card. Loaded on first use; if anything fails the
   loading screen just draws its own scene instead. */
typedef struct { UINT32 ver; void *cb; BOOL no_thread, no_codecs; } GpStartIn;
/* resource 10 = Chosen Company truck, 11 = Midnight Run car */
static struct { uint32_t *img; int res, w, h; } g_bgc[4]; /* a few decoded sizes, reused */
static int g_bgfail[2], g_bgnext;
static uint32_t *bgimg_get(int res, int w, int h) {
    if (res < 10 || res > 11 || w < 2 || h < 2) return NULL;
    for (int i = 0; i < 4; i++)
        if (g_bgc[i].img && g_bgc[i].res == res && g_bgc[i].w == w && g_bgc[i].h == h) return g_bgc[i].img;
    int slot = g_bgnext;
#define g_bgimg g_bgc[slot].img
#define g_bgw g_bgc[slot].w
#define g_bgh g_bgc[slot].h
#define g_bg_failed g_bgfail[res - 10]
    if (g_bg_failed) return NULL;
    g_bg_failed = 1; /* until it succeeds */
    HRSRC rs = FindResourceW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(res), (LPCWSTR)RT_RCDATA);
    HGLOBAL rg = rs ? LoadResource(GetModuleHandleW(NULL), rs) : NULL;
    const BYTE *data = rg ? (const BYTE *)LockResource(rg) : NULL;
    DWORD size = rs ? SizeofResource(GetModuleHandleW(NULL), rs) : 0;
    HMODULE gp = LoadLibraryW(L"gdiplus.dll"), sh = LoadLibraryW(L"shlwapi.dll");
    if (!data || !size || !gp || !sh) return NULL;
    typedef int(WINAPI * FStartup)(ULONG_PTR *, const GpStartIn *, void *);
    typedef int(WINAPI * FFromStream)(void *, void **);
    typedef int(WINAPI * FFromHDC)(HDC, void **);
    typedef int(WINAPI * FSetInterp)(void *, int);
    typedef int(WINAPI * FDrawRect)(void *, void *, int, int, int, int, int, int, int, int, int, void *, void *, void *);
    typedef int(WINAPI * FGetDim)(void *, UINT *);
    typedef int(WINAPI * FDispose)(void *);
    typedef void *(WINAPI * FMemStream)(const BYTE *, UINT);
    FStartup startup = (FStartup)(void *)GetProcAddress(gp, "GdiplusStartup");
    FFromStream from_stream = (FFromStream)(void *)GetProcAddress(gp, "GdipCreateBitmapFromStream");
    FFromHDC from_hdc = (FFromHDC)(void *)GetProcAddress(gp, "GdipCreateFromHDC");
    FSetInterp set_interp = (FSetInterp)(void *)GetProcAddress(gp, "GdipSetInterpolationMode");
    FDrawRect draw_rect = (FDrawRect)(void *)GetProcAddress(gp, "GdipDrawImageRectRectI");
    FGetDim get_w = (FGetDim)(void *)GetProcAddress(gp, "GdipGetImageWidth");
    FGetDim get_h = (FGetDim)(void *)GetProcAddress(gp, "GdipGetImageHeight");
    FDispose dispose_img = (FDispose)(void *)GetProcAddress(gp, "GdipDisposeImage");
    FDispose delete_gfx = (FDispose)(void *)GetProcAddress(gp, "GdipDeleteGraphics");
    FMemStream mem_stream = (FMemStream)(void *)GetProcAddress(sh, "SHCreateMemStream");
    if (!startup || !from_stream || !from_hdc || !set_interp || !draw_rect || !dispose_img || !delete_gfx || !mem_stream || !get_w || !get_h) return NULL;
    static ULONG_PTR token;
    GpStartIn in = {1, NULL, FALSE, FALSE};
    if (!token && startup(&token, &in, NULL) != 0) return NULL;
    IUnknown *stream = (IUnknown *)mem_stream(data, size);
    if (!stream) return NULL;
    void *img = NULL, *gfx = NULL;
    int ok = from_stream(stream, &img) == 0;
    stream->lpVtbl->Release(stream);
    if (!ok) return NULL;
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void *bits = NULL;
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp && bits) {
        HGDIOBJ old = SelectObject(dc, bmp);
        if (from_hdc(dc, &gfx) == 0) {
            set_interp(gfx, 7 /* high-quality bicubic */);
            UINT iw = 0, ih = 0; /* exact source pixels, so the file's DPI setting can't rescale it */
            get_w(img, &iw);
            get_h(img, &ih);
            draw_rect(gfx, img, 0, 0, w, h, 0, 0, (int)iw, (int)ih, 2 /* UnitPixel */, NULL, NULL, NULL);
            delete_gfx(gfx);
            free(g_bgimg);
            g_bgimg = (uint32_t *)malloc((size_t)w * h * 4);
            if (g_bgimg) {
                memcpy(g_bgimg, bits, (size_t)w * h * 4);
                g_bgw = w; g_bgh = h; g_bgc[slot].res = res; g_bg_failed = 0;
                g_bgnext = (g_bgnext + 1) % 4;
            }
        }
        SelectObject(dc, old);
    }
    if (bmp) DeleteObject(bmp);
    DeleteDC(dc);
    dispose_img(img);
    return g_bgimg;
#undef g_bgimg
#undef g_bgw
#undef g_bgh
#undef g_bg_failed
}

/* ---------------- loading screen ---------------- */
/* An animated intro that plays while the app starts up, styled by the theme:
   the card opens from a scan line, a radar sweep spins behind your crosshair,
   lock-on brackets snap onto it and the title decodes letter by letter.
   Patrol adds a flashing police lightbar, a night highway and gold stars;
   the other themes bring their own background (sunset grid, CRT scanlines,
   glitches...). Every startup step pulses the reticle and fills the bar. */
#define SP_CW 560
#define SP_CH 340
#define SP_M 34 /* room around the card for its shadow */
static const wchar_t *SP_TEXT[NSTARTUP] = {L"LOADING SETTINGS", L"BUILDING CROSSHAIRS",
                                           L"SETTING UP KEYBINDS", L"HIDING CURSOR"};
static struct {
    HWND h;
    HFONT title, sub, small, pct, tiny;
    int step, fading, done, demo, x, y;
    float prog, target, fade;
    DWORD t0, step_t, fade_t, ready_t, pulse_t;
    const wchar_t *text;
    HDC dc;
    HBITMAP bmp;
    uint32_t *bits;
} SP;
static int g_fake_clock; /* tests only: drive the animation with a fixed clock */
static DWORD g_fake_ms;
static DWORD sp_now(void) { return g_fake_clock ? g_fake_ms : GetTickCount(); }

static float ease_out(float t) { t = clamp01(t); return 1 - (1 - t) * (1 - t) * (1 - t); }
static float ease_out_back(float t) {
    float c1 = 1.70158f, c3 = c1 + 1;
    t = clamp01(t) - 1;
    return 1 + c3 * t * t * t + c1 * t * t;
}

/* Criminal intro (ranks mode): the crew's ranks, climbed one by one while the app loads */
static const wchar_t *RANKS[] = {L"PROSPECT", L"REBEL", L"RAIDER", L"SOLDATO", L"ENFORCER", L"UNDERBOSS", L"KINGPIN"};
#define NRANKS 7
static int sp_rank(float ready) {
    if (ready >= 0) return NRANKS - 1; /* Kingpin once everything is loaded */
    return clampi((int)(SP.prog * (NRANKS - 1) + 0.0001f), 0, NRANKS - 2);
}

/* draw one string several times to fake an outline / shadow / color split */
static void sp_text_at(HDC dc, int x, int y, const wchar_t *s, int n, COLORREF c) {
    SetTextColor(dc, c);
    TextOutW(dc, x, y, s, n);
}

/* "CROSSHAIR OVERLAY": each letter scrambles through random symbols, then locks in */
static void sp_title(HDC dc, int cx, int y, float t, float glitch) {
    const wchar_t *T = L"CROSSHAIR OVERLAY";
    static const wchar_t GLYPHS[] = L"#$%&@*+=?/<>0123456789XKZ";
    const int n = (int)wcslen(T), ng = (int)(sizeof(GLYPHS) / sizeof(GLYPHS[0])) - 1;
    int spacing = g_th.deco == DECO_ARCADE ? dpx(2) : dpx(5);
    SelectObject(dc, SP.title);
    SetBkMode(dc, TRANSPARENT);
    SetTextCharacterExtra(dc, spacing);
    SIZE full;
    GetTextExtentPoint32W(dc, T, n, &full);
    int x0 = cx - (full.cx - spacing) / 2;
    int o = dpx(2) > 1 ? dpx(2) : 2;
    for (int i = 0; i < n; i++) {
        float st = 0.30f + i * 0.035f, settle = st + 0.28f;
        if (t < st || T[i] == L' ') continue;
        SIZE pre, cw, gw;
        GetTextExtentPoint32W(dc, T, i, &pre);
        GetTextExtentPoint32W(dc, &T[i], 1, &cw);
        wchar_t ch = T[i];
        int scr = t < settle;
        if (scr) ch = GLYPHS[hash32(i * 131u + (uint32_t)(t * 22.0f)) % ng];
        GetTextExtentPoint32W(dc, &ch, 1, &gw);
        int x = x0 + pre.cx + (cw.cx - gw.cx) / 2;
        COLORREF main = scr ? C_ACCLT : C_TEXT;
        int gx = (int)(glitch * dpx(6));
        switch (g_th.deco) {
        case DECO_PATROL:
        case DECO_DRIVE:
        case DECO_CRIMINAL:
        case DECO_OPS:
        case DECO_INTEL:
        case DECO_TACTICAL: /* bold dark outline + drop shadow, like a game logo */
            sp_text_at(dc, x, y + o + dpx(2), &ch, 1, RGB(0, 0, 0));
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                    if (dx || dy) sp_text_at(dc, x + dx * o, y + dy * o, &ch, 1, RGB(4, 6, 12));
            break;
        case DECO_ARCADE: /* hard offset shadow */
            sp_text_at(dc, x + o, y + o, &ch, 1, C_ACC2);
            main = scr ? C_TEXT : C_ACCENT;
            break;
        case DECO_RAID: /* red title with a violet split, like the footage */
            sp_text_at(dc, x - o - gx, y, &ch, 1, C_ACCENT);
            main = scr ? C_TEXT : C_ACC2;
            break;
        case DECO_SUNSET:
        case DECO_CYBER: /* red/blue color split, wider while glitching */
            sp_text_at(dc, x - o / 2 - gx, y, &ch, 1, C_ACC2);
            sp_text_at(dc, x + o / 2 + gx, y, &ch, 1, C_ACCENT);
            break;
        }
        sp_text_at(dc, x, y, &ch, 1, main);
    }
    SetTextCharacterExtra(dc, 0);
}

static void splash_render(void) {
    int CW = dpx(SP_CW), CH = dpx(SP_CH), M = dpx(SP_M), W = CW + 2 * M, H = CH + 2 * M;
    if (!SP.dc) {
        BITMAPINFO bi = {0};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = W;
        bi.bmiHeader.biHeight = -H;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        void *bits = NULL;
        SP.dc = CreateCompatibleDC(NULL);
        SP.bmp = CreateDIBSection(SP.dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        if (!SP.bmp || !bits) return;
        SelectObject(SP.dc, SP.bmp);
        SP.bits = (uint32_t *)bits;
    }
    uint32_t *px = SP.bits;
    canvas_set(px, W, H);
    g_ntq = 0;
    DWORD now = sp_now();
    float t = (now - SP.t0) / 1000.0f;
    float ready = SP.ready_t ? (now - SP.ready_t) / 1000.0f : -1.0f; /* seconds since everything loaded */
    int x0 = M, y0 = M, x1 = M + CW, y1 = M + CH, deco = g_th.deco, patrol = deco == DECO_PATROL;
    float cxm = W / 2.0f, rcy = (float)(y0 + dpx(112)), pop = ease_out(t / 0.6f);
    float sl = 0, sr = 0;
    int criminal = deco == DECO_CRIMINAL, ops = deco == DECO_OPS, intel = deco == DECO_INTEL, raid = deco == DECO_RAID, drive = deco == DECO_DRIVE;
    int badge = criminal || ops; /* rope-ring patch around the crosshair */
    if (patrol || criminal) strobe(t, &sl, &sr);
    RECT full = g_clip, card = {x0, y0, x1, y1};

    /* ---- background ---- */
    COLORREF top = lerpc(g_th.bg, g_th.card, 0.9f), bot = lerpc(g_th.bg, RGB(0, 0, 0), 0.35f);
    for (int y = 0; y < H; y++) p_fill(0, y, W, y + 1, lerpc(top, bot, (float)(y - y0) / CH));
    g_clip = card;
    int gs = dpx(26);
    switch (deco) {
    case DECO_PATROL: {
        float hy = (float)(y0 + dpx(150));
        p_glow(cxm, hy, dpf(300), lerpc(C_ACCENT, C_TEXT, 0.3f), 0.12f); /* city glow on the horizon */
        for (int i = 0; i < 22; i++) { /* skyline */
            int bw = CW / 22, bx = x0 + i * bw, bh = dpx(8) + (int)(rnd01(i + 7) * dpx(30));
            if (fabsf(bx + bw / 2.0f - cxm) < dpf(60)) bh /= 3; /* lower in the middle, so the road reads */
            p_fill(bx, (int)hy - bh, bx + bw - dpx(2), (int)hy, lerpc(g_th.bg, g_th.card, 0.55f));
            for (int k = 0; k < 4; k++)
                if (rnd01(i * 17 + k) < 0.5f)
                    blend(bx + dpx(3) + (int)(rnd01(i * 31 + k) * (bw - dpx(8))), (int)hy - bh + dpx(3) + k * dpx(6),
                          C_GOLD, 0.55f);
        }
        p_road(x0, x1, hy, y1, CW * 0.40f, 0.9f, t * 1.8f);
        p_glow((float)(x0 + dpx(30)), (float)y0, dpf(340), C_ACC2, 0.08f + 0.36f * sl); /* strobe wash */
        p_glow((float)(x1 - dpx(30)), (float)y0, dpf(340), C_ACCENT, 0.08f + 0.40f * sr);
        break;
    }
    case DECO_OPS: {
        p_camo(x0, y0, x1, y1, dpx(7), 1.0f);
        for (int y = y0; y < y1; y++) hspan(y, (float)x0, (float)x1, RGB(0, 0, 0), 0.35f);
        p_glow(cxm, rcy, dpf(240), C_ACCENT, 0.16f);
        float sy = y0 + fmodf(t * 0.45f, 1.0f) * CH; /* recon scan line */
        for (int k = 0; k < dpx(26); k++) hspan((int)sy - k, (float)x0, (float)x1, C_ACC2, 0.10f * (1 - k / (float)dpx(26)));
        hspan((int)sy, (float)x0, (float)x1, C_ACC2, 0.35f);
        break;
    }
    case DECO_DRIVE: {
        /* slow push-in on your screenshot: sample a slightly larger copy with smooth (bilinear) scaling */
        int BW = CW * 108 / 100, BH = CH * 108 / 100;
        uint32_t *bg = bgimg_get(11, BW, BH);
        float z = 1.0f + 0.07f * ease_out(t / 3.2f);
        float shake = ready >= 0 && ready < 0.3f ? dpf(2.5f) * sinf(ready * 90) * (1 - ready / 0.3f) : 0;
        float icx = BW / 2.0f, icy = BH / 2.0f;
#define IMGX(u) (cxm + ((u) * BW - icx) * z + shake)
#define IMGY(v) (y0 + CH / 2.0f + ((v) * BH - icy) * z)
        if (bg) {
            for (int y = 0; y < CH; y++) {
                float sy = icy + (y - CH / 2.0f) / z;
                int iy = (int)sy;
                float fy = sy - iy;
                if (iy < 0) { iy = 0; fy = 0; }
                if (iy > BH - 2) { iy = BH - 2; fy = 1; }
                uint32_t *dst = px + (y0 + y) * W + x0, *r0 = bg + iy * BW, *r1 = r0 + BW;
                for (int x = 0; x < CW; x++) {
                    float sx = icx + (x - CW / 2.0f - shake) / z;
                    int ix = (int)sx;
                    float fx = sx - ix;
                    if (ix < 0) { ix = 0; fx = 0; }
                    if (ix > BW - 2) { ix = BW - 2; fx = 1; }
                    uint32_t a = r0[ix], b = r0[ix + 1], c = r1[ix], d = r1[ix + 1], o = 0;
                    for (int sh2 = 0; sh2 < 24; sh2 += 8) {
                        float top = ((a >> sh2) & 255) * (1 - fx) + ((b >> sh2) & 255) * fx;
                        float bot = ((c >> sh2) & 255) * (1 - fx) + ((d >> sh2) & 255) * fx;
                        o |= (uint32_t)(top + (bot - top) * fy + 0.5f) << sh2;
                    }
                    dst[x] = o;
                }
            }
            for (int y = y0; y < y1; y++) /* darker toward the bottom for the text */
                hspan(y, (float)x0, (float)x1, RGB(6, 7, 10), 0.35f * clamp01((float)(y - y0 - CH * 0.62f) / (CH * 0.38f)));
        } else
            p_glow(cxm, rcy, dpf(240), C_ACCENT, 0.12f);
        /* taillight bar powers on: a bright head sweeps left to right, then the whole bar glows */
        static const float TL[8][2] = {{0.347f, 0.564f}, {0.372f, 0.525f}, {0.453f, 0.518f}, {0.539f, 0.532f},
                                       {0.690f, 0.541f}, {0.841f, 0.532f}, {0.927f, 0.559f}, {1.000f, 0.585f}};
        float sweep = clamp01((t - 0.35f) / 0.9f), lit = clamp01((t - 1.0f) / 0.4f);
        float brake = ready >= 0 ? (ready < 0.25f ? 1.0f : clamp01(1 - (ready - 0.25f) / 0.4f) * 0.6f) : 0;
        float level = 0.35f + 0.45f * lit + 0.6f * brake;
        for (int i = 0; i < 7; i++) {
            float u0 = i / 7.0f, u1 = (i + 1) / 7.0f;
            float on = clamp01((sweep - u0) * 7.0f);
            if (on <= 0) continue;
            float ax = IMGX(TL[i][0]), ay = IMGY(TL[i][1]), bx = IMGX(TL[i + 1][0]), by = IMGY(TL[i + 1][1]);
            p_line(ax, ay, bx, by, dpf(10), RGB(255, 20, 30), 0.12f * on * level);
            p_line(ax, ay, bx, by, dpf(3.5f), RGB(255, 40, 50), 0.75f * on * level);
            p_line(ax, ay, bx, by, dpf(1.2f), RGB(255, 170, 170), 0.8f * on * level);
            if (sweep < 1 && u1 > sweep && u0 <= sweep) { /* the moving bright head */
                float k = (sweep - u0) * 7.0f;
                p_glow(ax + (bx - ax) * k, ay + (by - ay) * k, dpf(30), RGB(255, 90, 90), 0.8f);
            }
        }
        if (sweep >= 1) p_glow(IMGX(0.66f), IMGY(0.535f), dpf(150) * (1 + brake * 0.4f), RGB(255, 20, 30), 0.12f + 0.25f * brake);
        if (brake > 0) { /* brake light in the rear window */
            p_glow(IMGX(0.668f), IMGY(0.381f), dpf(50), RGB(255, 30, 40), 0.6f * brake);
            p_line(IMGX(0.61f), IMGY(0.381f), IMGX(0.725f), IMGY(0.381f), dpf(2.5f), RGB(255, 80, 80), brake);
        }
        { /* roof glint twinkles */
            float g = 0.5f + 0.5f * sinf(t * 2.3f);
            p_glow(IMGX(0.372f), IMGY(0.293f), dpf(34), RGB(240, 244, 255), 0.25f + 0.35f * g);
        }
        for (int e = 0; e < 2; e++) /* exhaust smoke curling out of both tailpipes */
            for (int i = 0; i < 9; i++) {
                float life = fmodf(t * 0.55f + i / 9.0f + e * 0.37f, 1.0f);
                float ex = IMGX(e ? 0.90f : 0.47f) + (e ? 1 : -1) * life * dpf(40) + sinf(t * 2 + i * 1.7f) * dpf(6) * life;
                float ey = IMGY(0.96f) - life * dpf(56);
                p_glow(ex, ey, dpf(10) + life * dpf(34), RGB(170, 176, 190), 0.26f * sinf(life * 3.14159f) * clamp01(t / 0.6f));
            }
        drive_extras(IMGX(0), IMGY(0), BW * z, BH * z, t, x0, y0, x1, y1, ready >= 0 && ready < 0.45f ? 1 - ready / 0.45f : 0); /* pops on GONE */
#undef IMGX
#undef IMGY
        break;
    }
    case DECO_RAID: {
        uint32_t *bg = bgimg_get(10, CW, CH);
        float hxl, hxr, hy2, sgx, sgy; /* headlight and turn-signal positions */
        if (bg) { /* your screenshot, with the lights placed on the truck's real lights */
            for (int y = 0; y < CH; y++) memcpy(px + (y0 + y) * W + x0, bg + y * CW, CW * sizeof(uint32_t));
            hxl = x0 + CW * 0.121f; hxr = x0 + CW * 0.457f; hy2 = y0 + CH * 0.449f;
            sgx = x0 + CW * 0.520f; sgy = y0 + CH * 0.444f;
            for (int y = y0; y < y1; y++) /* top-to-bottom shade so the text stays readable */
                hspan(y, (float)x0, (float)x1, RGB(5, 6, 12), 0.05f + 0.5f * clamp01((float)(y - y0 - CH * 0.5f) / (CH * 0.5f)));
        } else { /* fallback: a soft drawn silhouette */
            hxl = cxm - dpf(150); hxr = cxm + dpf(150); hy2 = rcy + dpf(14); sgx = cxm + dpf(186); sgy = hy2;
            p_soft_rrect(cxm - dpf(165), rcy - dpf(96), cxm + dpf(165), rcy - dpf(30), dpf(26), dpf(9), RGB(30, 33, 45), 0.95f);
            p_soft_rrect(cxm - dpf(205), rcy - dpf(52), cxm + dpf(205), rcy + dpf(84), dpf(30), dpf(12), RGB(30, 33, 45), 0.95f);
        }
        for (int k = 0; k < 4; k++) { /* slow drifting fog */
            float fx = x0 + fmodf(rnd01(k * 9 + 1) * CW + t * dpf(14) * (k + 1), (float)CW + dpf(200)) - dpf(100);
            p_glow(fx, y0 + CH * 0.62f + k * dpf(16), dpf(150), RGB(120, 128, 160), 0.05f);
        }
        { /* orange turn signal blinks */
            float blink = fmodf(t, 0.7f) < 0.35f && ready < 0 ? 1.0f : 0.15f;
            p_glow(sgx, sgy, dpf(26), RGB(255, 120, 20), 0.55f * blink);
        }
        /* headlights: flicker on, then two high-beam flashes when everything is loaded */
        float on = t < 0.3f ? 0 : clamp01((t - 0.3f) / 0.25f);
        if (t < 0.8f && rnd01((uint32_t)(t * 30)) < 0.35f) on *= 0.3f;
        float hb = 0;
        if (ready >= 0) hb = (ready < 0.12f || (ready > 0.22f && ready < 0.34f)) ? 1.0f : 0.0f;
        for (int sd = 0; sd < 2; sd++) {
            float hx = sd ? hxr : hxl;
            p_light_fx(hx, hy2, RGB(210, 220, 255), on * (0.6f + hb), (float)x0, (float)x1);
            p_glow(hx, hy2, dpf(60 + 70 * hb), RGB(220, 228, 255), (0.12f + 0.45f * hb) * on);
        }
        if (hb > 0) for (int y = y0; y < y1; y++) hspan(y, (float)x0, (float)x1, RGB(200, 210, 255), 0.10f);
        for (int i = 0; i < 46; i++) { /* rain */
            float sp = 0.7f + 0.6f * rnd01(i * 3 + 7), len = dpf(14) + dpf(14) * rnd01(i * 3 + 8);
            float rx = x0 + rnd01(i * 3 + 9) * CW, ry = y0 + fmodf(rnd01(i * 3 + 10) * CH + t * dpf(520) * sp, (float)CH + len);
            p_line(rx, ry - len, rx - dpf(2), ry, dpf(1), RGB(170, 200, 230), 0.22f);
        }
        /* rifle lasers sweep in and paint the target */
        float aim = ease_out((t - 0.45f) / 0.5f), jit = dpf(1.5f) * sinf(t * 23);
        if (t > 0.45f && ready < 0.4f) {
            float fade = ready >= 0 ? 1 - ready / 0.4f : 1;
            float sx[3] = {(float)x0, (float)x0, (float)x1}, sy[3] = {rcy - dpf(20), rcy + dpf(46), rcy + dpf(64)};
            float mx[3] = {(float)x1, (float)x1, (float)x0}, my[3] = {rcy - dpf(90), rcy + dpf(120), rcy - dpf(40)};
            for (int k = 0; k < 3; k++) {
                float ex = mx[k] + (cxm - mx[k]) * aim, ey = my[k] + (rcy + jit * (k - 1) - my[k]) * aim;
                p_ir_laser(sx[k], sy[k], ex, ey, fade);
                if (aim > 0.98f) p_circle(ex, ey, dpf(2.2f), 0, RGB(200, 190, 255), fade);
            }
        }
        p_glow(cxm, (float)(y0 + dpx(206)), dpf(220), C_ACC2, 0.10f); /* red glow behind the title */
        break;
    }
    case DECO_INTEL: {
        p_glow(cxm, rcy, dpf(240), C_ACCENT, 0.14f);
        for (int gy = y0 + gs / 2; gy < y1; gy += gs) for (int x = x0; x < x1; x++) blend(x, gy, C_TEXT, 0.02f);
        p_globe(cxm, rcy, dpf(98) * (0.85f + 0.15f * pop), t * 0.35f, lerpc(C_ACC2, C_ACCENT, 0.4f), 0.8f * pop);
        break;
    }
    case DECO_CRIMINAL: {
        p_glow(cxm, rcy, dpf(260), C_ACCENT, 0.22f);
        for (int k = 0; k < 4; k++) { /* sweeping security lasers */
            float sw = sinf(t * (0.7f + k * 0.23f) + k * 1.7f);
            float ax = x0 + CW * (0.08f + k * 0.28f), bx = ax + CW * (0.25f * sw + (k & 1 ? -0.2f : 0.2f));
            p_laser(ax, (float)y0, bx, (float)y1, 0.75f * pop);
        }
        p_glow((float)(x0 + dpx(10)), (float)y1, dpf(260), C_ACCENT, 0.05f + 0.30f * sl); /* cops behind you */
        p_glow((float)(x1 - dpx(10)), (float)y1, dpf(260), RGB(46, 104, 255), 0.05f + 0.34f * sr);
        break;
    }
    case DECO_SUNSET: {
        float hy = (float)(y0 + dpx(176));
        p_glow(cxm, hy, dpf(320), C_ACCENT, 0.28f);
        RECT sky = {x0, y0, x1, (int)hy};
        g_clip = sky;
        p_sun(cxm, hy - dpf(2), dpf(96), RGB(255, 214, 90), C_ACCENT, 0.95f, t * 0.6f);
        g_clip = card;
        p_floor_grid(x0, x1, hy, y1, C_ACC2, 0.55f, t * 1.4f, 12);
        for (int i = 0; i < 40; i++) {
            float sx = x0 + rnd01(i * 5 + 1) * CW, sy = y0 + rnd01(i * 5 + 2) * (hy - y0 - dpf(30));
            blend((int)sx, (int)sy, C_TEXT, 0.25f + 0.35f * (0.5f + 0.5f * sinf(t * 3 + i)));
        }
        break;
    }
    case DECO_ARCADE:
        p_glow((float)(x1 - dpx(20)), (float)y0, dpf(320), C_ACC2, 0.20f);
        p_glow((float)(x0 + dpx(20)), (float)y1, dpf(320), C_ACCENT, 0.12f);
        for (int i = 0; i < 70; i++) { /* falling pixel starfield */
            float sp = 0.4f + rnd01(i * 3 + 9);
            int sx = x0 + (int)(rnd01(i * 3 + 1) * CW);
            int sy = y0 + (int)fmodf(rnd01(i * 3 + 2) * CH + t * dpf(40) * sp, (float)CH), sz = dpx(2);
            for (int yy = 0; yy < sz; yy++)
                for (int xx = 0; xx < sz; xx++) blend(sx + xx, sy + yy, i % 5 ? C_TEXT : C_ACC2, 0.2f + 0.4f * sp / 1.4f);
        }
        p_floor_grid(x0, x1, (float)(y0 + dpx(214)), y1, C_ACCENT, 0.22f, t * 1.6f, 10);
        break;
    case DECO_TACTICAL:
        for (int gy = y0 + gs; gy < y1; gy += gs)
            for (int gx = x0 + gs; gx < x1; gx += gs) blend(gx, gy, C_TEXT, 0.14f);
        for (int k = 1; k < 6; k++) p_circle(cxm, rcy, dpf(40) * k + dpf(60), dpf(1), C_ACC2, 0.10f);
        for (int x = x0 + gs / 2, i = 0; x < x1; x += gs / 2, i++)
            p_fill(x, y1 - (i % 4 == 0 ? dpx(10) : dpx(5)), x + 1, y1, lerpc(g_th.bg, C_ACCENT, 0.45f));
        p_glow(cxm, rcy, dpf(240), C_ACCENT, 0.10f);
        break;
    case DECO_CYBER:
        for (int y = y0; y < y1; y++)
            for (int x = x0 + ((dpx(22) - ((y - y0) % dpx(22))) % dpx(22)); x < x1; x += dpx(22)) blend(x, y, C_TEXT, 0.03f);
        p_glow((float)(x1 - dpx(20)), (float)y0, dpf(330), C_ACCENT, 0.14f);
        p_glow((float)(x0 + dpx(20)), (float)y1, dpf(330), C_ACC2, 0.14f);
        for (int y = y1 - dpx(7); y < y1; y++) /* hazard tape */
            for (int x = x0; x < x1; x++)
                blend(x, y, ((x + y + (int)(t * dpx(30))) / dpx(10)) % 2 ? C_ACCENT : RGB(0, 0, 0), 0.85f);
        break;
    case DECO_AURORA: art_aurora(x0, y0, x1, y1, t); break;
    case DECO_EMBER: art_ember(x0, y0, x1, y1, t); break;
    case DECO_GALAXY: art_galaxy(x0, y0, x1, y1, t); break;
    case DECO_OCEAN: art_ocean(x0, y0, x1, y1, t); break;
    case DECO_SAKURA: art_sakura(x0, y0, x1, y1, t); break;
    default:
        for (int gx = x0 + gs / 2; gx < x1; gx += gs) for (int y = y0; y < y1; y++) blend(gx, y, C_TEXT, 0.03f);
        for (int gy = y0 + gs / 2; gy < y1; gy += gs) for (int x = x0; x < x1; x++) blend(x, gy, C_TEXT, 0.03f);
        p_glow(cxm, rcy, dpf(200), C_ACCENT, 0.24f * ease_out(t / 0.8f));
        for (int i = 0; i < 26; i++) { /* rising embers */
            float sp = 0.5f + rnd01(i * 7 + 1), life = fmodf(t * 0.35f * sp + rnd01(i * 7 + 2), 1.0f);
            float ex = x0 + rnd01(i * 7 + 3) * CW + sinf(t * 1.5f + i) * dpf(8), ey = y1 - life * CH;
            p_circle(ex, ey, dpf(1.4f), 0, i % 3 ? C_ACCENT : C_ACC2, 0.7f * sinf(life * 3.14159f));
        }
        break;
    }
    if (deco != DECO_GRID && deco != DECO_TACTICAL && deco != DECO_CYBER) { /* soft dark band behind the title */
        int b0 = y0 + dpx(176), b1 = y0 + dpx(282);
        for (int y = b0; y < b1; y++) {
            float k = sinf(3.14159f * (y - b0) / (float)(b1 - b0));
            hspan(y, (float)x0, (float)x1, bot, 0.62f * k);
        }
    }
    /* darken the lower part so the text and bar stay readable over the art */
    float dk = deco == DECO_DRIVE ? 0.55f : 0.82f; /* Midnight Run: lighter, so the backfire shows */
    for (int y = y1 - dpx(118); y < y1; y++) {
        float k = (float)(y - (y1 - dpx(118))) / dpx(118);
        hspan(y, (float)x0, (float)x1, bot, dk * k * k * (3 - 2 * k));
    }

    /* ---- reticle: radar sweep, tick ring, spinning arcs ---- */
    {
        float R = dpf(72) * (0.85f + 0.15f * pop), sa = t * 2.4f;
        if (ops) { /* unit patch: rope ring around a dark center */
            float bR = dpf(66) * (0.6f + 0.4f * ease_out_back(t / 0.6f));
            p_circle(cxm, rcy + dpf(4), bR + dpf(14), 0, RGB(0, 0, 0), 0.5f * pop);
            p_circle(cxm, rcy, bR + dpf(6), 0, RGB(12, 12, 13), pop);
            p_circle(cxm, rcy, bR - dpf(3), dpf(1.5f), C_ACCENT, pop);
            p_rope(cxm, rcy, bR + dpf(7), dpf(9), pop);
        }
        if (criminal) { /* crew patch: rope ring around a red sunset */
            float bR = dpf(66) * (0.6f + 0.4f * ease_out_back(t / 0.6f));
            p_circle(cxm, rcy + dpf(4), bR + dpf(14), 0, RGB(0, 0, 0), 0.45f * pop);
            p_circle(cxm, rcy, bR + dpf(6), 0, RGB(14, 10, 10), pop);
            p_sunset_badge(cxm, rcy, bR, pop);
            p_rope(cxm, rcy, bR + dpf(7), dpf(9), pop);
        }
        int ix0, iy0, ix1, iy1;
        bounds(cxm - R, rcy - R, cxm + R, rcy + R, &ix0, &iy0, &ix1, &iy1);
        for (int y = iy0; y < iy1; y++)
            for (int x = ix0; x < ix1; x++) {
                float dx = x + 0.5f - cxm, dy = y + 0.5f - rcy, dist = sqrtf(dx * dx + dy * dy);
                if (dist >= R) continue;
                float rel = fmodf(sa - atan2f(dy, dx), 6.2831853f);
                if (rel < 0) rel += 6.2831853f;
                if (rel < 1.5f && !criminal && !intel && !raid && !drive) {
                    float k = 1 - rel / 1.5f;
                    blend(x, y, C_ACCENT, 0.30f * pop * k * k * clamp01((R - dist) / dpf(6)));
                }
            }
        if (!badge && !intel && !raid && !drive) p_circle(cxm, rcy, R + dpf(4), dpf(1.2f), C_TEXT, 0.18f * pop);
        for (int i = 0; i < 48 && !badge && !intel && !raid && !drive; i++) {
            float an = i * 0.1308997f + t * 0.15f, len = i % 12 == 0 ? dpf(9) : i % 4 == 0 ? dpf(5) : dpf(2.5f);
            float r0 = R + dpf(7), r1 = r0 + len;
            p_line(cxm + r0 * cosf(an), rcy + r0 * sinf(an), cxm + r1 * cosf(an), rcy + r1 * sinf(an), dpf(1.2f),
                   i % 12 == 0 ? C_ACCENT : C_TEXT, (i % 12 == 0 ? 0.9f : 0.35f) * pop);
        }
        float R1 = dpf(56) * (0.8f + 0.2f * pop), R2 = dpf(44) * (0.8f + 0.2f * pop);
        if (badge) R1 = dpf(90);
        if (intel) R1 = dpf(108);
        for (int k = 0; k < 2; k++) p_arc(cxm, rcy, R1, dpf(3), t * 3.4f + k * 3.14159f, 1.0f, k && patrol ? C_ACC2 : C_ACCENT, pop);
        if (!criminal)
            for (int k = 0; k < 3; k++) p_arc(cxm, rcy, R2, dpf(1.6f), -t * 2.2f + k * 2.0944f, 0.6f, C_TEXT, 0.45f * pop);

        /* lock-on brackets fly in and snap onto the crosshair */
        float lk = ease_out_back((t - 0.25f) / 0.6f), ba = clamp01((t - 0.25f) / 0.15f);
        float d = dpf(30) + (1 - lk) * dpf(90), rot = (1 - lk) * 0.9f, L = dpf(11);
        float ux = cosf(rot), uy = sinf(rot);
        COLORREF bc = criminal ? C_WHITE : t > 0.85f ? C_ACCENT : C_ACCLT;
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sy = -1; sy <= 1; sy += 2) {
                float px0 = cxm + sx * d * ux - sy * d * uy, py0 = rcy + sx * d * uy + sy * d * ux;
                p_line(px0, py0, px0 - sx * L * ux, py0 - sx * L * uy, dpf(2.2f), bc, ba);
                p_line(px0, py0, px0 + sy * L * uy, py0 - sy * L * ux, dpf(2.2f), bc, ba);
            }
        /* pulse ring when it locks on, and after every startup step */
        DWORD pulses[2] = {SP.t0 + 850, SP.pulse_t};
        for (int k = 0; k < 2; k++) {
            if (!pulses[k] || now < pulses[k]) continue;
            float pr = (now - pulses[k]) / 480.0f;
            if (pr < 1) p_circle(cxm, rcy, dpf(28) + dpf(64) * ease_out(pr), dpf(2), C_ACCENT, 0.7f * (1 - pr));
        }
        /* your crosshair pops in */
        float s = t < 0.55f ? 0 : ease_out_back((t - 0.55f) / 0.45f);
        if (t > 1.0f) s *= 1 + 0.05f * sinf((t - 1.0f) * 4.5f);
        int n = dpx(70), th = dpx(3);
        if (th > 6) th = 6;
        if (criminal) { /* dark scope behind your crosshair so any color shows on the red badge */
            p_circle(cxm, rcy, dpf(31) * s, 0, RGB(8, 4, 5), 0.78f);
            p_circle(cxm, rcy, dpf(31) * s, dpf(1.5f), C_WHITE, 0.35f);
        }
        p_crosshair(cxm, rcy, n, S.style, S.color, n * 0.24f * s, th, 1, 1.0f);
    }

    /* ---- Patrol: police lightbar ---- */
    if (patrol) {
        float lw = CW * 0.44f, lx0 = cxm - lw / 2, ly = (float)(y0 + dpx(20)), lh = dpf(11);
        p_rrect(lx0 - dpf(3), ly - lh / 2 - dpf(3), lx0 + lw + dpf(3), ly + lh / 2 + dpf(3), dpf(8), 0, RGB(14, 16, 22), 1);
        int mods = 7;
        float mw = (lw / 2 - dpf(10)) / mods;
        for (int side = 0; side < 2; side++) {
            float on = side ? sr : sl;
            COLORREF lit = side ? C_ACCENT : C_ACC2, off = lerpc(lit, RGB(10, 10, 14), 0.75f);
            for (int m = 0; m < mods; m++) {
                float mx0 = side ? cxm + dpf(10) + m * mw : lx0 + m * mw;
                p_rrect(mx0 + dpf(1), ly - lh / 2, mx0 + mw - dpf(1), ly + lh / 2, dpf(2), 0, lerpc(off, lit, on), 1);
                if (on > 0) p_rrect(mx0 + dpf(2), ly - lh / 2 + dpf(1), mx0 + mw - dpf(2), ly - dpf(1), dpf(1), 0, C_WHITE, 0.45f);
            }
            if (on > 0) p_glow(side ? cxm + lw / 4 : cxm - lw / 4, ly, dpf(90), lit, 0.55f);
        }
        p_rrect(cxm - dpf(8), ly - lh / 2, cxm + dpf(8), ly + lh / 2, dpf(2), 0, RGB(200, 204, 214), 0.5f); /* takedown light */
    }

    /* ---- Patrol: five gold stars pop in under the title ---- */
    int title_y = y0 + dpx(188), star_y = y0 + dpx(246), sub_y = y0 + (patrol || criminal ? dpx(262) : dpx(240));
    if (criminal && !g_crim_intro && t > 0.9f) { /* crew ranks: a chevron per rank, filling up as the app loads */
        int rk = sp_rank(ready);
        for (int i = 0; i < NRANKS; i++) {
            float sx = cxm + (i - (NRANKS - 1) / 2.0f) * dpf(22), sy = (float)star_y;
            float k = ease_out_back((t - 0.9f - i * 0.04f) / 0.3f), w = dpf(7) * k, h = dpf(4.5f) * k;
            int on = i <= rk, newest = (i == rk && ready < 0) || (ready >= 0 && ready < 0.9f);
            COLORREF c = !on ? RGB(60, 48, 48) : newest && fmodf(t, 0.3f) < 0.15f ? C_WHITE : C_GOLD;
            for (int j = 0; j < 2; j++) { /* double chevron, like a rank patch */
                float yy = sy + (j ? dpf(2.5f) : -dpf(2.5f)) + h / 2;
                p_line(sx - w, yy, sx, yy - h, dpf(3.2f), RGB(0, 0, 0), 0.9f * k);
                p_line(sx, yy - h, sx + w, yy, dpf(3.2f), RGB(0, 0, 0), 0.9f * k);
                p_line(sx - w, yy, sx, yy - h, dpf(1.8f), c, 1);
                p_line(sx, yy - h, sx + w, yy, dpf(1.8f), c, 1);
            }
        }
    }
    if (criminal && g_crim_intro && t > 0.9f) { /* wanted level: one more star for every startup step, the newest one flashes */
        int lvl = ready >= 0 ? 5 : SP.step + 1;
        if (lvl > 5) lvl = 5;
        for (int i = 0; i < 5; i++) {
            float sx = cxm + (i - 2) * dpf(24), R = dpf(9) * ease_out_back((t - 0.9f - i * 0.05f) / 0.3f);
            int on = i < lvl, newest = (i == lvl - 1 && ready < 0) || (ready >= 0 && ready < 0.9f);
            int flash = newest && fmodf(t, 0.3f) < 0.15f;
            p_star(sx, (float)star_y, R + dpf(2), RGB(0, 0, 0), 1);
            p_star(sx, (float)star_y, R, on ? (flash ? C_WHITE : C_GOLD) : RGB(54, 46, 46), 1);
        }
    }
    if (patrol)
        for (int i = 0; i < 5; i++) {
            float st = (t - 1.05f - i * 0.09f) / 0.35f, s = st <= 0 ? 0 : ease_out_back(st);
            float sx = cxm + (i - 2) * dpf(24), R = dpf(9) * s;
            p_star(sx, (float)star_y, R + dpf(2), RGB(18, 14, 4), 1);
            p_star(sx, (float)star_y, R, C_GOLD, 1);
            if (ready >= 0 && ready < 0.5f) /* glint on READY */
                p_star(sx, (float)star_y, R, C_WHITE, 0.7f * (1 - ready / 0.5f) * clamp01(1 - fabsf(ready * 10 - i)));
        }

    /* ---- progress bar ---- */
    float bx0 = (float)(x0 + dpx(48)), bx1 = (float)(x1 - dpx(48)), by = (float)(y1 - dpx(42));
    float fx = bx0 + (bx1 - bx0) * SP.prog;
    if (g_th.segbar) {
        int nseg = 24, lit = (int)(SP.prog * nseg + 0.001f);
        float segw = (bx1 - bx0) / nseg;
        for (int i = 0; i < nseg; i++) {
            float sx0 = bx0 + i * segw, sx1 = sx0 + segw - dpf(3);
            int blink = i == lit && fmodf(t, 0.3f) < 0.15f;
            if (i < lit) p_rrect(sx0 - dpf(1), by - dpf(6), sx1 + dpf(1), by + dpf(6), dpf(2), 0, C_ACCENT, 0.12f);
            p_rrect(sx0, by - dpf(3.5f), sx1, by + dpf(3.5f), dpf(1), 0, i < lit || blink ? C_ACCENT : C_TEXT,
                    i < lit ? 1.0f : blink ? 0.55f : 0.09f);
        }
    } else {
        p_rrect(bx0, by - dpf(2.5f), bx1, by + dpf(2.5f), dpf(2.5f), 0, C_TEXT, 0.09f);
        if (fx > bx0 + dpf(4)) {
            p_rrect(bx0, by - dpf(8), fx, by + dpf(8), dpf(8), 0, C_ACCENT, 0.08f);
            p_rrect(bx0, by - dpf(2.5f), fx, by + dpf(2.5f), dpf(2.5f), 0, C_ACCENT, 1);
            for (int y = (int)(by - dpf(2.5f)) + 1; y < (int)(by + dpf(2.5f)); y++)
                for (int x = (int)bx0 + dpx(2); x < (int)fx - dpx(2); x++) {
                    if (deco == DECO_SUNSET) /* pink-to-cyan gradient */
                        blend(x, y, lerpc(C_ACCENT, C_ACC2, (x - bx0) / (bx1 - bx0)), 1);
                    else if (patrol && ((x + y + (int)(-t * dpf(40))) / dpx(6) & 1)) /* moving livery stripes */
                        blend(x, y, C_WHITE, 0.22f);
                }
            if (!patrol) p_glow(bx0 + fmodf(t * 0.7f, 1.0f) * (fx - bx0), by, dpf(14), C_WHITE, 0.30f); /* shimmer */
            p_circle(fx, by, dpf(9), 0, C_ACCENT, 0.22f);
            p_circle(fx, by, dpf(3.4f), 0, lerpc(C_ACCENT, C_WHITE, 0.8f), 1);
        }
    }

    /* ---- HUD corners and tiny labels ---- */
    {
        float m = dpf(10), L = dpf(16), w = dpf(1.6f), a = 0.75f * pop;
        COLORREF c = patrol ? C_TEXT : C_ACCENT;
        p_line(x0 + m, y0 + m, x0 + m + L, y0 + m, w, c, a); p_line(x0 + m, y0 + m, x0 + m, y0 + m + L, w, c, a);
        p_line(x1 - m, y0 + m, x1 - m - L, y0 + m, w, c, a); p_line(x1 - m, y0 + m, x1 - m, y0 + m + L, w, c, a);
        p_line(x0 + m, y1 - m, x0 + m + L, y1 - m, w, c, a); p_line(x0 + m, y1 - m, x0 + m, y1 - m - L, w, c, a);
        p_line(x1 - m, y1 - m, x1 - m - L, y1 - m, w, c, a); p_line(x1 - m, y1 - m, x1 - m, y1 - m - L, w, c, a);
        wchar_t lab[64];
        wsprintfW(lab, L"%s", patrol ? L"UNIT 1  \x2022  POLICE" : criminal ? L"CREW 1  \x2022  CRIMINAL"
                              : ops ? L"CLASSIFIED  \x2022  SPECIAL OPS" : intel ? L"INTEL  \x2022  EYES ONLY"
                              : raid ? L"[CHOSEN COMPANY]" : drive ? L"MIDNIGHT RUN" : L"SYS.INIT");
        q_text_ex(x0 + dpx(30), y0 + dpx(12), x0 + dpx(200), y0 + dpx(30), lab, raid ? SP.pct : SP.tiny, raid ? C_ACC2 : C_DIM, DT_LEFT, 1);
        if (drive) {
            wsprintfW(lab, L"%d MPH", (int)(SP.prog * 186.0f)); /* speedo climbs with the loading bar */
        } else if (raid) { /* camcorder: blinking REC dot + running timecode */
            int f = (int)(t * 30);
            wsprintfW(lab, L"%s REC   00:00:%02d:%02d", fmodf(t, 1.0f) < 0.6f ? L"\x25CF" : L"  ", f / 30, f % 30);
        } else {
            wsprintfW(lab, L"THEME: %s", g_th.name);
            CharUpperW(lab);
        }
        q_text_ex(x1 - dpx(200), y0 + dpx(12), x1 - dpx(30), y0 + dpx(30), lab, SP.tiny, raid ? C_ACC2 : C_DIM, DT_RIGHT, 1);
    }

    /* ---- Special Ops: "MISSION READY" stamp slams down when everything is loaded ---- */
    float stamp_k = 0, stamp_ang = -0.18f, stx = cxm + dpf(150), sty = rcy + dpf(34);
    if (ops && ready >= 0) {
        stamp_k = 1 + 0.9f * (1 - ease_out(ready / 0.2f));
        float hw2 = dpf(84) * stamp_k, hh2 = dpf(22) * stamp_k, ca = cosf(stamp_ang), sa2 = sinf(stamp_ang);
        for (int b = 0; b < 2; b++) {
            float iw = hw2 - b * dpf(5), ih = hh2 - b * dpf(5), qx[4], qy[4];
            float lx[4] = {-iw, iw, iw, -iw}, ly[4] = {-ih, -ih, ih, ih};
            for (int q = 0; q < 4; q++) { qx[q] = stx + lx[q] * ca - ly[q] * sa2; qy[q] = sty + lx[q] * sa2 + ly[q] * ca; }
            for (int q = 0; q < 4; q++)
                p_line(qx[q], qy[q], qx[(q + 1) % 4], qy[(q + 1) % 4], dpf(b ? 1.4f : 3.2f), RGB(210, 46, 42), 0.92f);
        }
    }

    /* ---- Intel: scrolling encrypted data down both sides ---- */
    if (intel)
        for (int side = 0; side < 2; side++)
            for (int r = 0; r < 9; r++) {
                wchar_t hx[24];
                uint32_t hh = hash32((uint32_t)(r * 7 + side * 131) + (uint32_t)(t * 9.0f) * 977u);
                wsprintfW(hx, L"%02X %02X %02X %02X", hh & 255, (hh >> 8) & 255, (hh >> 16) & 255, hh >> 24);
                int ry = y0 + dpx(44) + r * dpx(15);
                if (side) q_text_ex(x1 - dpx(130), ry, x1 - dpx(26), ry + dpx(14), hx, SP.tiny, r == (int)(t * 6) % 9 ? C_ACCENT : C_DIM, DT_RIGHT, 1);
                else q_text_ex(x0 + dpx(26), ry, x0 + dpx(130), ry + dpx(14), hx, SP.tiny, r == (int)(t * 6 + 4) % 9 ? C_ACCENT : C_DIM, DT_LEFT, 1);
            }

    /* ---- Chosen Company: the unit's helmet quote types itself out ---- */
    if (raid && t > 1.15f) {
        static const wchar_t Q[] = L"\x201C" L"BETTER TO DIE WITH HONOR THAN TO LIVE WITH SHAME" L"\x201D";
        int nq = (int)(sizeof(Q) / sizeof(Q[0])) - 1, shown = (int)((t - 1.15f) * 46.0f);
        wchar_t buf[64];
        if (shown > nq) shown = nq;
        wcsncpy(buf, Q, shown);
        buf[shown] = 0;
        if (shown < nq && fmodf(t, 0.5f) < 0.3f) wcscat(buf, L"_");
        int ly = y0 + dpx(272);
        /* lay it out against the full quote so the line doesn't slide while typing */
        q_text_ex(x0 + dpx(40), ly - dpx(9), x1 - dpx(40), ly + dpx(9), buf, SP.small, RGB(110, 100, 255), DT_LEFT, dpx(1));
        g_tq[g_ntq - 1].r.left = (int)cxm - dpx(190);
    }

    /* ---- Midnight Run: cinematic black bars slide in ---- */
    if (drive) {
        int bh = (int)(dpf(30) * ease_out(t / 0.6f));
        for (int y = y0; y < y0 + bh; y++) hspan(y, (float)x0, (float)x1, RGB(0, 0, 0), 0.92f);
        for (int y = y1 - bh; y < y1; y++) hspan(y, (float)x0, (float)x1, RGB(0, 0, 0), 0.55f);
    }

    /* ---- READY flash ---- */
    if (ready >= 0 && ready < 0.45f) {
        float fl = 0.22f * (1 - ready / 0.45f);
        for (int y = y0; y < y1; y++) {
            if (patrol) { hspan(y, (float)x0, cxm, C_ACC2, fl); hspan(y, cxm, (float)x1, C_ACCENT, fl); }
            else hspan(y, (float)x0, (float)x1, C_ACCENT, fl);
        }
    }

    /* ---- text ---- */
    {
        wchar_t st[96], pct[16];
        int blink = fmodf(t, 0.8f) < 0.4f;
        if (ready >= 0) wcscpy(st, patrol ? L"10-8  \x2022  IN SERVICE" : criminal ? (g_crim_intro ? L"WANTED LEVEL 5  \x2022  GET AWAY" : L"KINGPIN  \x2022  RANKED UP")
                              : ops ? L"OPERATOR READY  \x2022  OSCAR MIKE" : intel ? L"ACCESS GRANTED  \x2022  EYES ONLY"
                              : raid ? L"BYE BYE  \x2022  EXFIL" : drive ? L"IMFASTR  \x2022  GONE" : L"READY  \x2022  LOCKED ON");
        else if (patrol) wsprintfW(st, L"DISPATCH  \x203A  %s", SP.text);
        else if (ops) wsprintfW(st, L"OPS  \x203A  %s", SP.text);
        else if (raid) wsprintfW(st, L"INFIL  \x203A  %s", SP.text);
        else if (drive) wsprintfW(st, L"WARMING UP  \x203A  %s", SP.text);
        else if (intel) wsprintfW(st, L"DECRYPTING  \x203A  %s", SP.text);
        else if (criminal && g_crim_intro) wsprintfW(st, L"HEIST  \x203A  %s", SP.text);
        else if (criminal) wsprintfW(st, L"%s  \x203A  %s", RANKS[sp_rank(ready)], SP.text);
        else wsprintfW(st, L"> %s%s", SP.text, blink ? L"_" : L"");
        q_text_ex((int)bx0, (int)by + dpx(10), (int)bx1, (int)by + dpx(28), st, SP.small, ready >= 0 ? (raid ? C_ACC2 : drive ? C_ACCENT : C_ACCLT) : C_MUTED, DT_LEFT, dpx(1));
        wsprintfW(pct, L"%d%%", (int)(SP.prog * 100.0f + 0.5f));
        q_text((int)bx0, (int)by + dpx(10), (int)bx1, (int)by + dpx(28), pct, SP.pct, C_TEXT, DT_RIGHT);
    }
    GdiFlush();
    flush_text(SP.dc);
    float glitch = 0;
    if (deco == DECO_CYBER) {
        float g = fmodf(t, 1.7f);
        if ((g > 0.9f && g < 1.02f) || (ready >= 0 && ready < 0.15f)) glitch = 1;
    }
    sp_title(SP.dc, W / 2, title_y, t, glitch);
    if (stamp_k > 0) {
        HFONT sf = face_font(L"Bahnschrift", (int)(15 * stamp_k + 0.5f), FW_BOLD), of;
        LOGFONTW lf;
        GetObjectW(sf, sizeof(lf), &lf);
        DeleteObject(sf);
        lf.lfEscapement = lf.lfOrientation = (LONG)(-stamp_ang * 572.958f); /* tenths of a degree */
        sf = CreateFontIndirectW(&lf);
        of = (HFONT)SelectObject(SP.dc, sf);
        SetTextAlign(SP.dc, TA_CENTER | TA_BASELINE);
        SetTextCharacterExtra(SP.dc, dpx(2));
        SetTextColor(SP.dc, RGB(220, 52, 46));
        float off = dpf(6) * stamp_k;
        TextOutW(SP.dc, (int)(stx - sinf(stamp_ang) * off), (int)(sty + cosf(stamp_ang) * off), L"MISSION READY", 13);
        SetTextAlign(SP.dc, TA_LEFT | TA_TOP);
        SetTextCharacterExtra(SP.dc, 0);
        SelectObject(SP.dc, of);
        DeleteObject(sf);
    }
    if (t > 1.0f) { /* "MADE BY SIMPLYSOUL  •  V1.x" with the name in the accent color */
        const wchar_t *parts[3] = {L"MADE BY  ", L"SIMPLYSOUL", (L"   \x2022   V" VER_WSTR)};
        COLORREF cols[3] = {C_MUTED, C_ACCLT, C_MUTED};
        SIZE sz[3];
        int total = 0;
        SelectObject(SP.dc, SP.sub);
        SetTextCharacterExtra(SP.dc, dpx(2));
        SetBkMode(SP.dc, TRANSPARENT);
        for (int i = 0; i < 3; i++) {
            GetTextExtentPoint32W(SP.dc, parts[i], (int)wcslen(parts[i]), &sz[i]);
            total += sz[i].cx;
        }
        int tx = W / 2 - total / 2;
        if (patrol || criminal) sub_y = star_y + dpx(14);
        for (int i = 0; i < 3; i++) {
            SetTextColor(SP.dc, cols[i]);
            TextOutW(SP.dc, tx, sub_y, parts[i], (int)wcslen(parts[i]));
            tx += sz[i].cx;
        }
        SetTextCharacterExtra(SP.dc, 0);
    }
    GdiFlush();

    /* ---- after-effects ---- */
    if (raid) { /* VHS tape: wavy rows, tearing bands, grain and scanlines */
        int tear0 = -1, tear1 = -1;
        float tp = fmodf(t, 1.3f);
        if (tp < 0.18f || (ready >= 0 && ready < 0.2f)) {
            uint32_t hh = hash32((uint32_t)(t * 20));
            tear0 = y0 + (int)((hh & 1023) / 1024.0f * CH);
            tear1 = tear0 + dpx(10) + (int)((hh >> 10) % (unsigned)dpx(30));
        }
        for (int y = y0; y < y1; y++) {
            float wob = sinf(y * 0.11f + t * 9.0f) * dpf(1.3f) + sinf(y * 0.031f - t * 4.0f) * dpf(1.0f);
            if (y >= tear0 && y < tear1) wob += dpf(10) * sinf(y * 0.5f + t * 40);
            int sh = (int)wob;
            uint32_t *row = px + y * W;
            if (sh > 0) memmove(row + x0 + sh, row + x0, (CW - sh) * sizeof(uint32_t));
            else if (sh < 0) memmove(row + x0, row + x0 - sh, (CW + sh) * sizeof(uint32_t));
            if ((y & 1) == 0) hspan(y, (float)x0, (float)x1, RGB(0, 0, 0), 0.16f);
        }
        uint32_t fr = (uint32_t)(t * 24);
        for (int y = y0; y < y1; y += 2)
            for (int x = x0; x < x1; x += 2) {
                float n = rnd01((uint32_t)(x * 7919 + y * 104729) ^ (fr * 2654435761u));
                if (n > 0.93f) blend(x, y, RGB(255, 255, 255), 0.10f);
                else if (n < 0.05f) blend(x, y, RGB(0, 0, 0), 0.25f);
            }
    }
    if (deco == DECO_ARCADE) /* CRT scanlines over everything */
        for (int y = y0; y < y1; y += 3) hspan(y, (float)x0, (float)x1, RGB(0, 0, 0), 0.32f);
    if (glitch > 0) /* slice a few bands sideways */
        for (int k = 0; k < 6; k++) {
            uint32_t hsh = hash32(k * 977u + (uint32_t)(t * 30.0f));
            int by0 = y0 + (int)((hsh & 1023) / 1024.0f * CH), bh = dpx(3) + (int)((hsh >> 10) % (unsigned)dpx(14));
            int sh = (int)((hsh >> 16) % (unsigned)dpx(16)) - dpx(8);
            for (int y = by0; y < by0 + bh && y < y1; y++) {
                uint32_t *row = px + y * W;
                if (sh > 0) memmove(row + x0 + sh, row + x0, (CW - sh) * sizeof(uint32_t));
                else if (sh < 0) memmove(row + x0, row + x0 - sh, (CW + sh) * sizeof(uint32_t));
                hspan(y, (float)x0, (float)x1, k & 1 ? C_ACC2 : C_ACCENT, 0.10f);
            }
        }
    g_clip = full;

    /* ---- open / close like a scanner: the card grows from a bright line ---- */
    float rev = SP.fading ? 0.12f + 0.88f * SP.fade : ease_out(t / 0.42f);
    float ccx = W / 2.0f, ccy = H / 2.0f, hw = CW / 2.0f, hh = fmaxf(CH / 2.0f * rev, dpf(1.5f));
    float r = fminf(dpf(18) * g_th.radius, hh), sh = dpf(8);
    if (rev < 0.995f) {
        for (int e = 0; e < 2; e++) {
            int ly = (int)(e ? ccy + hh - 2 : ccy - hh + 1);
            if (patrol) { hspan(ly, (float)x0, ccx, C_ACC2, 1); hspan(ly, ccx, (float)x1, C_ACCENT, 1); }
            else hspan(ly, (float)x0, (float)x1, lerpc(C_ACCENT, C_WHITE, 0.3f), 1);
        }
    }
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float d = rbox_d(x + 0.5f, y + 0.5f, ccx, ccy, hw, hh, r);
            float cv = clamp01(0.5f - d);
            float sd = rbox_d(x + 0.5f, y + 0.5f - sh, ccx, ccy, hw, hh, r);
            float q = clamp01(1 - sd / (M * 0.95f));
            float sa = sd <= 0 ? 0.5f : 0.5f * q * q * q;
            float A = cv + sa * (1 - cv);
            uint32_t p = px[y * W + x];
            uint32_t R = (uint32_t)(((p >> 16) & 255) * cv), G = (uint32_t)(((p >> 8) & 255) * cv),
                     B = (uint32_t)((p & 255) * cv), AA = (uint32_t)(A * 255.0f + 0.5f);
            px[y * W + x] = (AA << 24) | (R << 16) | (G << 8) | B;
        }
    if (SP.h && !g_fake_clock) {
        POINT pos = {SP.x, SP.y}, src = {0, 0};
        SIZE size = {W, H};
        BLENDFUNCTION bf = {AC_SRC_OVER, 0, (BYTE)(SP.fade * 255.0f), AC_SRC_ALPHA};
        HDC screen = GetDC(NULL);
        UpdateLayeredWindow(SP.h, screen, &pos, &size, SP.dc, &src, 0, &bf, ULW_ALPHA);
        ReleaseDC(NULL, screen);
    }
}

static void splash_tick(HWND h) {
    DWORD now = sp_now();
    if (!SP.fading) {
        SP.fade = clamp01((now - SP.t0) / 200.0f);
        SP.prog += (SP.target - SP.prog) * 0.12f;
        if (SP.step < NSTARTUP && now - SP.t0 >= 550 && now - SP.step_t >= 280 && SP.prog >= SP.target - 0.03f) {
            if (!SP.demo) startup_step(SP.step);
            SP.step++;
            SP.target = (float)SP.step / NSTARTUP;
            SP.text = SP.step < NSTARTUP ? SP_TEXT[SP.step] : L"READY";
            SP.step_t = SP.pulse_t = sp_now();
        } else if (SP.step >= NSTARTUP && SP.prog > 0.995f) {
            SP.prog = 1.0f;
            if (!SP.ready_t) SP.ready_t = now;
            else if (now - SP.ready_t >= 800) { SP.fading = 1; SP.fade_t = now; }
        }
    } else {
        SP.fade = 1 - clamp01((now - SP.fade_t) / 320.0f);
        if (SP.fade <= 0) {
            SP.done = 1;
            music_fade_later();
            if (!g_fake_clock) { KillTimer(h, 1); DestroyWindow(h); }
            return;
        }
    }
    splash_render();
}

static void splash_free(void) {
    if (SP.dc) { DeleteDC(SP.dc); SP.dc = NULL; }
    if (SP.bmp) { DeleteObject(SP.bmp); SP.bmp = NULL; }
    HFONT f[5] = {SP.title, SP.sub, SP.small, SP.pct, SP.tiny};
    for (int i = 0; i < 5; i++) if (f[i]) DeleteObject(f[i]);
    SP.title = SP.sub = SP.small = SP.pct = SP.tiny = NULL;
    SP.bits = NULL;
}

static LRESULT CALLBACK splash_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_TIMER: splash_tick(h); return 0;
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_DESTROY: SP.done = 1; return 0;
    case WM_NCDESTROY: SP.h = NULL; splash_free(); break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

/* demo = 1: just the animation (the "Preview loading screen" button), nothing is loaded */
static int splash_start(HINSTANCE inst, int demo) {
    if (SP.h) return 0;
    if (!g_fake_clock) music_play();
    memset(&SP, 0, sizeof(SP));
    SP.demo = demo;
    SP.title = face_font(g_th.title_font, g_th.deco == DECO_ARCADE ? 18 : 21, FW_BOLD);
    SP.sub = ui_font(8, FW_SEMIBOLD);
    SP.small = ui_font(8, FW_SEMIBOLD);
    SP.pct = ui_font(9, FW_BOLD);
    SP.tiny = ui_font(7, FW_SEMIBOLD);
    SP.text = SP_TEXT[0];
    SP.target = 0.1f;
    SP.t0 = SP.step_t = sp_now();
    int W = dpx(SP_CW + 2 * SP_M), H = dpx(SP_CH + 2 * SP_M);
    POINT c;
    GetCursorPos(&c);
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    RECT wa = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    if (GetMonitorInfoW(MonitorFromPoint(c, MONITOR_DEFAULTTOPRIMARY), &mi)) wa = mi.rcWork;
    SP.x = wa.left + (wa.right - wa.left - W) / 2;
    SP.y = wa.top + (wa.bottom - wa.top - H) / 2;
    SP.h = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                           L"CrosshairSplash", APP_NAME, WS_POPUP, SP.x, SP.y, W, H, NULL, NULL, inst, NULL);
    if (!SP.h) { splash_free(); return 0; }
    splash_render();
    ShowWindow(SP.h, SW_SHOWNOACTIVATE);
    SetTimer(SP.h, 1, 15, NULL);
    return 1;
}

static void run_splash(HINSTANCE inst) {
    if (splash_start(inst, 0)) {
        MSG m;
        while (!SP.done) {
            BOOL r = GetMessageW(&m, NULL, 0, 0);
            if (r <= 0) {
                if (r == 0) PostQuitMessage((int)m.wParam);
                break;
            }
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        if (SP.h && IsWindow(SP.h)) DestroyWindow(SP.h);
    }
    while (SP.step < NSTARTUP) startup_step(SP.step++); /* finish anything left */
}

static void splash_preview(void) { splash_start(GetModuleHandleW(NULL), 1); }

#ifdef CH_TEST
/* CH_SPLASH_DUMP=<folder>: render the intro with a fixed clock and save frames as .pam images */
static void splash_dump(HINSTANCE inst, const wchar_t *dir) {
    static const int AT[] = {120, 260, 420, 650, 900, 1200, 1600, 2100, 2500, 2700, 2950, 3300};
    int na = (int)(sizeof(AT) / sizeof(AT[0])), k = 0, every = 0;
    {
        wchar_t ev[16];
        if (GetEnvironmentVariableW(L"CH_SPLASH_EVERY", ev, 16)) { every = _wtoi(ev); na = 1000; }
    }
    g_fake_clock = 1;
    g_fake_ms = 100000;
    if (!splash_start(inst, 1)) return;
    KillTimer(SP.h, 1);
    DWORD t0 = SP.t0;
    while (!SP.done && k < na) {
        g_fake_ms += 15;
        splash_tick(SP.h);
        int due = every ? (int)(g_fake_ms - t0) >= k * every : (int)(g_fake_ms - t0) >= AT[k];
        if (due && SP.bits) {
            int stamp = every ? k * every : AT[k];
            wchar_t fn[MAX_PATH];
            char hdr[128];
            int W = dpx(SP_CW + 2 * SP_M), H = dpx(SP_CH + 2 * SP_M);
            wsprintfW(fn, L"%s\\splash_%s_%05d.pam", dir, g_th.name, stamp);
            HANDLE f = CreateFileW(fn, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
            if (f != INVALID_HANDLE_VALUE) {
                DWORD w;
                int hl = wsprintfA(hdr, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n", W, H);
                WriteFile(f, hdr, hl, &w, NULL);
                BYTE *row = (BYTE *)malloc(W * 4);
                for (int y = 0; y < H; y++) {
                    for (int x = 0; x < W; x++) {
                        uint32_t p = SP.bits[y * W + x], A = p >> 24;
                        row[x * 4 + 0] = A ? (BYTE)fminf(255, ((p >> 16) & 255) * 255 / A) : 0;
                        row[x * 4 + 1] = A ? (BYTE)fminf(255, ((p >> 8) & 255) * 255 / A) : 0;
                        row[x * 4 + 2] = A ? (BYTE)fminf(255, (p & 255) * 255 / A) : 0;
                        row[x * 4 + 3] = (BYTE)A;
                    }
                    WriteFile(f, row, W * 4, &w, NULL);
                }
                free(row);
                CloseHandle(f);
            }
            k++;
        }
    }
    DestroyWindow(SP.h);
}
#endif

/* ---------------- cursor safety net ---------------- */
/* A tiny helper copy of this exe waits in the background. However the app is
   closed (Exit, Task Manager "End task", a crash), the helper puts the normal
   Windows cursor and Roblox's cursor files back. */
#define MUTEX_NAME L"CrosshairOverlay_SingleInstance"

static int run_watchdog_if_asked(void) {
    int argc = 0, handled = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc >= 3 && !wcscmp(argv[1], L"--restore-watch")) {
        handled = 1;
        DWORD pid = (DWORD)wcstoul(argv[2], NULL, 10);
        HANDLE p = OpenProcess(SYNCHRONIZE, FALSE, pid);
        if (p) {
            WaitForSingleObject(p, INFINITE);
            CloseHandle(p);
        }
        Sleep(150);
        /* If the app was already reopened, leave its cursor alone */
        HANDLE again = OpenMutexW(SYNCHRONIZE, FALSE, MUTEX_NAME);
        if (again) CloseHandle(again);
        else {
            restore_cursor();
            rbx_all(0);
        }
    }
    if (argv) LocalFree(argv);
    return handled;
}

static void start_watchdog(void) {
    wchar_t exe[MAX_PATH], cmd[MAX_PATH + 64];
    DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (!n || n >= MAX_PATH) return;
    wsprintfW(cmd, L"\"%s\" --restore-watch %lu", exe, GetCurrentProcessId());
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    if (CreateProcessW(exe, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS,
                       NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    (void)prev; (void)cmd; (void)show;
#ifdef PUBLISHER_APP
    (void)inst;
    return publisher_main();
#endif
    if (run_watchdog_if_asked()) return 0;
    DWORD updated_from = update_wait_old(); /* started by an update: let the old copy close first */
    HANDLE mtx = CreateMutexW(NULL, TRUE, MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS && !take_over_older()) { /* an older copy is closed and replaced */
        MessageBoxW(NULL, L"Crosshair Overlay is already running.\nPress Ctrl+F2 or click its icon near the clock to open settings.",
                    L"Crosshair Overlay", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    start_watchdog();
    set_dpi_aware();
    /* Give Roblox priority over this app for CPU time */
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
    restore_cursor(); /* undo anything left over from an earlier crash */
    SetUnhandledExceptionFilter(crash_handler);
    settings_path();
    log_paths();
    int first_run = g_ini[0] && GetFileAttributesW(g_ini) == INVALID_FILE_ATTRIBUTES;
    load_settings();
    load_binds();
    {
        wchar_t wv[96];
        win_version(wv);
        log_info(L"start v" VER_WSTR L" on %s, %dx%d", wv, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    }
    if (update_startup_checks(updated_from)) { /* the new version is broken: the previous one is starting */
        restore_cursor();
        if (mtx) { ReleaseMutex(mtx); CloseHandle(mtx); }
        return 0;
    }
    if (!updated_from && g_autoupd && update_pending()) { /* an update downloaded last time: install it before anything starts */
        if (update_install()) {
            restore_cursor();
            if (mtx) { ReleaseMutex(mtx); CloseHandle(mtx); }
            return 0;
        }
    }
#ifdef CH_TEST
    { wchar_t tb[8]; if (GetEnvironmentVariableW(L"CH_THEME", tb, 8)) S.theme = _wtoi(tb); }
    { wchar_t tb[8]; if (GetEnvironmentVariableW(L"CH_CRIM", tb, 8)) g_crim_intro = _wtoi(tb); }
#endif
    theme_set(S.theme);
    make_fonts();
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    WM_TASKBARCREATED = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = host_proc;
    wc.hInstance = inst;
    wc.lpszClassName = L"CrosshairOverlayHost";
    RegisterClassW(&wc);
    wc.lpfnWndProc = overlay_proc;
    wc.lpszClassName = L"CrosshairOverlayWindow";
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    RegisterClassW(&wc);
    wc.lpfnWndProc = ui_proc;
    wc.lpszClassName = L"CrosshairSettings";
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hbrBackground = NULL;
    wc.style = CS_DROPSHADOW;
    RegisterClassW(&wc);
    wc.style = 0;
    wc.lpfnWndProc = splash_proc;
    wc.lpszClassName = L"CrosshairSplash";
    wc.hbrBackground = NULL;
    RegisterClassW(&wc);
    wc.lpfnWndProc = card_proc;
    wc.lpszClassName = L"CrosshairKeysCard";
    RegisterClassW(&wc);

    g_host = CreateWindowExW(WS_EX_TOOLWINDOW, L"CrosshairOverlayHost", L"Crosshair Overlay",
                             WS_POPUP, 0, 0, 0, 0, NULL, NULL, inst, NULL);
    g_overlay = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
                                    WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                L"CrosshairOverlayWindow", L"Crosshair", WS_POPUP, 0, 0, 64, 64,
                                NULL, NULL, inst, NULL);
    g_card = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW |
                                 WS_EX_NOACTIVATE,
                             L"CrosshairKeysCard", L"Keybinds", WS_POPUP, 0, 0, 10, 10, NULL, NULL, inst, NULL);
    if (!g_host || !g_overlay) return 1;
    if (!capture_hide_apply() && g_nocap) log_info(L"hide from recordings: not supported on this Windows");

#ifdef CH_TEST
    {
        wchar_t dd[MAX_PATH];
        if (GetEnvironmentVariableW(L"CH_SPLASH_DUMP", dd, MAX_PATH)) {
            splash_dump(inst, dd);
            return 0;
        }
    }
#endif
    g_autostarted = has_arg(L"--autostart");
    if (autostart_on()) autostart_set(1); /* keep the entry pointing at this exe (it may have moved) */
    if (S.splash && !g_autostarted) run_splash(inst); /* no intro when Windows starts it at sign-in */
    else for (int i = 0; i < NSTARTUP; i++) startup_step(i);
    SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, NULL, focus_event, 0, 0, WINEVENT_OUTOFCONTEXT);
    SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND, NULL, focus_event, 0, 0, WINEVENT_OUTOFCONTEXT);
    focus_check();
    if (!g_focus_hidden && !g_scene_hidden) ShowWindow(g_overlay, SW_SHOWNOACTIVATE);
    InterlockedExchange(&g_force_move, 1);
    if (first_run) {
        save_settings();
        save_binds();
    }

    {
        wchar_t k1[64], k2[64], k3[64];
        (void)k3;
        key_text(KB[A_SETTINGS], k1);
        key_text(KB[A_HIDE], k2);
        g_nid.uFlags = NIF_INFO;
        wcscpy(g_nid.szInfoTitle, APP_NAME L" by simplysoul");
        if (KB[A_SETTINGS].vk)
            wsprintfW(g_nid.szInfo, L"Click this icon or press %s to open the settings panel.", k1);
        else
            wcscpy(g_nid.szInfo, L"Click this icon to open the settings panel.");
        g_nid.dwInfoFlags = NIIF_INFO;
        Shell_NotifyIconW(NIM_MODIFY, &g_nid);
        g_nid.szInfo[0] = 0;
    }
    if (S.open_start && !g_autostarted) ui_open(-1);
#ifdef CH_TEST
    {
        wchar_t tb[8];
        if (GetEnvironmentVariableW(L"CH_TAB", tb, 8)) ui_open(_wtoi(tb));
        if (GetEnvironmentVariableW(L"CH_AUTOSTART", tb, 8)) log_info(L"test autostart_set(%d) = %d, now %d", _wtoi(tb), autostart_set(_wtoi(tb)), autostart_on());
        if (GetEnvironmentVariableW(L"CH_UNINSTALL", tb, 8)) { g_test_yes = 1; uninstall_all(); }
        if (GetEnvironmentVariableW(L"CH_FAKE_RBX", tb, 8)) { /* a stand-in "Roblox" window: Esc toggles a dark menu overlay */
            WNDCLASSW fc = {0};
            fc.lpfnWndProc = fake_rbx_proc;
            fc.hInstance = GetModuleHandleW(NULL);
            fc.lpszClassName = L"WINDOWSCLIENT";
            fc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
            RegisterClassW(&fc);
            HWND fw = CreateWindowExW(0, L"WINDOWSCLIENT", L"Roblox", WS_POPUP | WS_VISIBLE, 0, 0, 1400, 900, NULL, NULL, fc.hInstance, NULL);
            SetForegroundWindow(fw);
        }
        if (GetEnvironmentVariableW(L"CH_EXPORT_FILE", tb, 8)) settings_export(g_host);
        if (GetEnvironmentVariableW(L"CH_IMPORT_FILE", tb, 8)) { settings_import(g_host); save_settings(); }
        if (GetEnvironmentVariableW(L"CH_DUMP", tb, 8)) { /* raw RGBA dumps of every style for checking */
            static uint32_t buf[64 * 64];
            for (int st = 0; st < NSTYLES; st++) {
                render(buf, 64, 64, st, 0, 12.0f, S.thick, 1, 1.0f, 0);
                wchar_t fn[MAX_PATH];
                wsprintfW(fn, L"Z:\\tmp\\claude-0\\dump\\s%02d.raw", st);
                HANDLE fh = CreateFileW(fn, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
                DWORD wr; if (fh != INVALID_HANDLE_VALUE) { WriteFile(fh, buf, sizeof(buf), &wr, NULL); CloseHandle(fh); }
            }
        }
        if (GetEnvironmentVariableW(L"CH_SYMTEST", tb, 8)) { /* where is each shape's center of mass vs the window center? */
            static uint32_t buf[96 * 96];
            int keepg = S.glow; S.glow = 0;
            for (int th = 2; th <= 3; th++)
                for (int st = 0; st < NSTYLES; st++) {
                    render(buf, 96, 96, st, 0, 12.0f, th, 1, 1.0f, 1);
                    double sx = 0, sy = 0, sa = 0;
                    for (int y = 0; y < 96; y++) for (int x = 0; x < 96; x++) { double A = buf[y * 96 + x] >> 24; sx += A * (x + 0.5); sy += A * (y + 0.5); sa += A; }
                    char line[160];
                    snprintf(line, sizeof(line), "SYM thick %d style %2d %-16S dx %+.3f dy %+.3f", th, st, STYLE_NAMES[st], sa ? sx / sa - 48.5 : 0, sa ? sy / sa - 48.5 : 0);
                    wchar_t wl[160]; MultiByteToWideChar(CP_UTF8, 0, line, -1, wl, 160); log_info(L"%s", wl);
                }
            S.glow = keepg;
        }
        if (GetEnvironmentVariableW(L"CH_CLOSE_AFTER", tb, 8)) SetTimer(g_host, 0x7777, _wtoi(tb) * 1000, NULL);
        if (GetEnvironmentVariableW(L"CH_QUIT_AFTER", tb, 8)) { Sleep(_wtoi(tb) * 1000); DestroyWindow(g_host); }
        if (GetEnvironmentVariableW(L"CH_REPORT", tb, 8)) {
            report_dialog(_wtoi(tb));
            wchar_t txt[300];
            if (GetEnvironmentVariableW(L"CH_REPORT_TEXT", txt, 300) && g_redit) SetWindowTextW(g_redit, txt);
            if (GetEnvironmentVariableW(L"CH_REPORT_SEND", tb, 8)) SetTimer(g_rdlg, 1, 6000, (TIMERPROC)(void (*)(void))rd_send);
        }
    }
#endif

    report_startup(); /* crash or error last time: ask once / send */
#ifdef CH_BAD
    *(volatile int *)0 = 0; /* test build that crashes right after starting */
#endif
    if (g_just_updated) whatsnew_dialog();
    if (g_rolled_from[0]) {
        g_nid.uFlags = NIF_INFO;
        wcscpy(g_nid.szInfoTitle, L"Update undone");
        wsprintfW(g_nid.szInfo, L"v%s had a problem on your PC, so you're back on v" VER_WSTR L". The next fixed version will install by itself.", g_rolled_from);
        g_nid.dwInfoFlags = NIIF_WARNING;
        Shell_NotifyIconW(NIM_MODIFY, &g_nid);
        g_nid.szInfo[0] = 0;
    }
    if (g_probation) SetTimer(g_host, PROB_TIMER, 45000, NULL);
    if (g_replaced_exe[0]) log_info(L"took over from an older running copy: %s", g_replaced_exe);
    old_copy_notice();
    g_launch_tick = GetTickCount();
    if (updates_set()) SetTimer(g_host, UPD_TIMER, 1500, NULL); /* check right away at every launch, then every 6 h */
#ifdef CH_TEST
    {
        wchar_t tv[600];
        char a8[600];
        if (GetEnvironmentVariableW(L"CH_UPD_FAST", tv, 8)) SetTimer(g_host, UPD_TIMER, 1500, NULL);
    }
#endif
#ifdef CH_TEST
    { wchar_t tb[8]; if (GetEnvironmentVariableW(L"CH_CRASH", tb, 8)) { Sleep(1500); if (tb[0] == L'2') { volatile const char *np = NULL; char d[8]; memcpy(d, (const void *)np, 8); log_info(L"%S", d); } *(volatile int *)0 = 0; } }
#endif
    SetTimer(g_host, 1, 3000, NULL);
    HANDLE th = CreateThread(NULL, 0, mover, NULL, 0, NULL);
    HANDLE wt = CreateThread(NULL, 0, rbx_watch, NULL, 0, NULL);
    { HANDLE ct = CreateThread(NULL, 0, rbx_crash_watch, NULL, 0, NULL); if (ct) { SetThreadPriority(ct, THREAD_PRIORITY_BELOW_NORMAL); CloseHandle(ct); } }
    if (wt) SetThreadPriority(wt, THREAD_PRIORITY_BELOW_NORMAL);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    InterlockedExchange(&g_running, 0);
    if (th) { WaitForSingleObject(th, 1000); CloseHandle(th); }
    restore_cursor();
    if (g_icon) DestroyIcon(g_icon);
    if (mtx) { ReleaseMutex(mtx); CloseHandle(mtx); }
    return 0;
}
