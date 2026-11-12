Crosshair Overlay v1.17.0 - by simplysoul

NEW IN 1.17.0 - Layers, auto-contrast, menu/chat hiding, backups, debug info, publisher history
- Crosshair layers: the Crosshair, Color and Look tabs have a Main / Layer 2 switch next to the heading. In
  Layer 2 you pick its shape (or "No layer"), its color and its size; thickness, opacity, outline and effects
  are shared. It's drawn on top of the main crosshair, in the preview and in profiles (layer2_style/_color/_size;
  -1 = none). Share codes still only carry the main crosshair.
- Auto-contrast (Color tab, off by default): 10 times a second it looks at the spot behind the crosshair. If your
  color would blend in (contrast ratio under 1.6) it turns white or black until it stands out again (over 2.2).
  Not with Rainbow. Layered windows aren't in that picture, so the crosshair itself is never measured.
- Hide in Roblox menus & chat (Effects tab, on by default), only while Roblox is in front:
  Esc -> if Roblox's window gets clearly darker within 0.8 s (the menu's dark overlay), it hides; it comes back
  on Esc or when the window gets bright again (Resume). "/" hides it while you type; Enter, Esc or a click
  brings it back. Keys are only read with GetAsyncKeyState (no keyboard hook), nothing is stored or sent.
- Profiles tab heading: Export... saves every setting, profile and keybind to a .ini file; Import... loads one
  (asks first) and uses it right away. Things that belong to the PC (install id, tester state, update/news
  state, music file path) are never exported or overwritten.
- Updates tab: Copy debug info puts a short block (version, tester, Windows, screen, Roblox + FPS settings,
  crosshair + layer, setup, last problem) on the clipboard, formatted for Discord.
- Publisher: History lists the newest 10 GitHub releases (Everyone / Testers only, date, downloads). Test builds
  have "Release to everyone": it downloads that exe from GitHub and publishes it like the Publish button
  (GitHub release becomes the normal one, Discord post with its notes, ping per the switch). Others: Open on GitHub.
- Fixed: labels with letter spacing ("PANEL SIZE", "LIVE PREVIEW"...) were measured without the spacing and cut off.

NEW IN 1.16.3 - Hotfix: crosshair position back to 1.15.1
- 1.16.1 and 1.16.2 changed how the crosshair follows the mouse (1 ms position updates, a "Roblox mouse lock"
  guess, a built-in 1 px shift). The fast async updates could pile up and lag far behind, and the lock guess
  could hold the crosshair in the middle of the window. All of that is removed: the mouse-follow code (mover)
  and the position (target_point) are exactly 1.15.1's again, plus only the optional Fine-tune nudge (0 / 0 =
  1.15.1's position; reset to 0 for everyone on this update, nudge_v=3).
- Every crosshair is centered on the middle of the mouse's pixel, at every thickness (measured in a test: mouse at
  pixel 700,400 -> crosshair center 700.5,400.5 for all styles). Even thicknesses (2, 4, 6) get soft edges
  instead of sitting half a pixel up-left like 1.15.1. The thin 1-px parts (Cross-Dot lines, Shotgun Spread
  dashes) and the cover patch use the same center as the rest of the crosshair.

NEW IN 1.16.2 - Hotfix: follow-mouse alignment
- Follow mouse puts the crosshair 1 px right and 1 px down of the Windows mouse point (FOLLOW_FIX_X/Y in
  crosshair.c), which is where Roblox's cursor points. Same for every style (all 34 are symmetric around their
  center pixel - checked). Applies in shift-lock / first person too; Center mode is unchanged.
- Fine-tune position is reset to 0 / 0 once on this update (nudge_v=2), since 0 / 0 is now the right spot.

NEW IN 1.16.1 - Exact centering
- All 34 crosshairs are drawn centered on the middle of the mouse's pixel. Before, even thicknesses (2, 4, 6)
  and the thin 1-px parts (Cross-Dot lines, Shotgun dashes) sat half a pixel up-left; the cover patch too.
  Even thicknesses now have soft edges instead of being shifted. (Checked by measuring every shape's center.)
- Look tab > Fine-tune position: X / Y nudge, -10..+10 px (nudge_x / nudge_y), for games whose own cursor
  or aim point isn't exactly on the mouse point. Works in Follow mouse and Center.
- Follow mouse: checks every 1 ms while the mouse moves (so it doesn't trail behind), once per screen refresh
  when still. In Roblox shift-lock / first person (Roblox keeps snapping the mouse to the middle of its window),
  the crosshair now stays exactly on that middle point instead of jittering while you turn.

NEW IN 1.16.0 - Clear crash reasons and easier reporting
- Crash reasons in plain words. The exe now carries a table of its own function names (symbols.bin, made by
  make-symbols.py; build.sh links twice so the code doesn't move). When the app crashes it logs:
    CRASH: the app crashed while drawing the settings window (in ui_paint)
    CRASH why: it used something that wasn't set up yet (empty pointer, reading address 0x0)
    CRASH before: opened the settings panel (40s ago) > Themes tab (12s ago) > picked a theme (2s ago)
    CRASH fix: what the player can try (graphics driver, overlay, antivirus...) or "this is a bug in the app"
    CRASH detail: exception code, address and the call stack with function names (for you)
  If it crashes inside another program's DLL (NVIDIA/AMD/Intel driver, RivaTuner, Discord/Steam/OBS overlay,
  Nahimic, antivirus...), it names the program and what to try. The "before" trail is the last few things the
  player did (tabs, clicks, keybinds, Roblox starting/closing, updates).
- Discord reports are now a card: a title and color per type (crash red, error orange, update undone yellow,
  player report blue, Roblox crash purple), then What happened / Why / What to try / The player says, then
  fields: Before it happened, Version, Windows, Screen, Roblox, Crosshair, Setup, and the install id.
  The first line still pings you with the type, version and a reference (e.g. ref `3d55-KBCM`).
  report.txt starts with the same summary. (Relay users: the relay must forward "embeds"; see the worker file.)
- Report a problem window: 1. What's wrong (required), 2. How often, 3. What were you doing (both one click,
  optional), 4. Anything else (optional text). F1 in the panel or searching "report" opens it. After sending,
  the player sees a reference code to quote to you.

NEW IN 1.15.1 - Version history + numbering
- changelog.txt (the Updates tab list) now has one entry per release line: "v1.15  Title", with its fixes folded
  in, instead of every small version. Write new entries the same way: add lines under the current "v1.xx" heading
  for fixes, and start a new "v1.yy  Title" heading for a feature release.
- Version numbers: fixes and small changes = +0.0.1 (1.15.1, 1.15.2 ...), new features = +0.1.0 (1.16.0).
  The publisher now warns before publishing a version that skips numbers or is older than the last one you
  published (it checks published.txt).

NEW IN 1.15.0 - Search, hover tips, last tab, first-run tour
- Search box in the top bar: click it, press Ctrl+F, or just start typing a letter. Results show the setting
  and its tab; click one (or arrow keys + Enter) to jump there, and that setting glows for a moment.
  Esc clears the search. Search terms include common words (e.g. "fps", "obs", "uninstall", "rainbow").
- Hover tips: rest the mouse on a setting for about half a second to see a short explanation.
- The panel reopens on the tab you used last (saved as last_tab).
- First-run tour (brand-new installs only, once): 3 steps (pick a crosshair, Keybinds, Done) with a spotlight.
  Skip any time with Skip tour or Esc (saved as tour_done=1).

NEW IN 1.14.0 - Clearer reports, usage stats, tester tools
- Automatic reports are labelled by what happened: "App crash", "Update undone", "Error" or "Roblox crash",
  with a "What happened:" line taken from the log (e.g. "update: v1.13.2 didn't start properly ..."). Reports
  from testers also say "tester". (If you use the relay worker, paste the new report-relay-worker.js too.)
- General tab > Share anonymous usage stats (off by default). When a player turns it on, the app posts one
  short line a week (and after each update) to Discord: version, tester or not, Windows 10/11, screen size and
  Hz, theme, crosshair style, panel size, FPS cap / boost, Roblox-only, hide-from-recordings, and a scrambled
  6-character user id (so you can tell people apart without knowing who they are). No ping, no file.
  It goes to STATS_WEBHOOK_URL in webhook.h, or to the bug-report webhook when that's empty.
  Tip: make a #stats channel + webhook, put it in webhook.h, rebuild. Search the channel (e.g. "theme Aurora").
- Q/A testers: a "Q/A TESTER" badge in the header, and on the Updates tab a tester row with
  "Looks good" (posts a check-in for this version, no ping, once per version), "Problem..." (opens the report
  window; the report is marked tester) and a TEST BUILDS switch to pause test builds and get normal releases.

NEW IN 1.13.3 - Update safety net fix
- After an update, closing the app normally (tray Exit, sign out, shutdown) now always counts as "this version
  works". Before, closing it within about 5 seconds of the loading screen ending didn't count, so opening and
  closing a fresh update 3 times in a row rolled it back to the previous version and skipped it, even though
  nothing crashed. Rollback now only happens after a crash, or 3 starts that never closed normally (killed / hung).

NEW IN 1.13.2 - Recent codes, source on GitHub, Read more links, scrambled tester list
- Profiles tab > Recent codes: the last 10 crosshair codes you pasted, each with a little preview. Click one to
  switch to it, hover and click x to remove it (saved as recent_codes).
- Publishing now also pushes this version's source code to the GitHub repo (one commit, then the release tag
  points at it), so the release's "Source code (zip)" is the real source. CrosshairPublisher.exe carries the
  source of its own version (build.sh packs it with make-source-bundle.py; Discord webhook URLs are removed
  and testers.txt / news.txt are never included). If you publish an exe of another version, the source step
  is skipped and log.txt says why. The repo also gets a README.md with a download link.
- GitHub release text ends with "Read more: everything that changed" (changelog.txt at that version) and the
  Discord post's title + "Read more & download" link open the GitHub release page.
- testers.txt: IDs are replaced by a one-way scrambled form (t1-..., PBKDF2-SHA256, 200,000 rounds) when you
  save in the publisher, so nobody can copy a tester's ID from the public file. Only your GitHub token (kept
  encrypted on your PC) can change the file. Testers need v1.13.2+ to be recognised by a scrambled entry.

NEW IN 1.13.1 - New layout, share codes, Roblox-only mode, announcements
- Settings window redesigned: header + tab bar along the top, live crosshair preview (real size) always
  visible in the left column with "Now using" and Done under it. Text sizes itself so labels never get cut off.
- Share codes (Profiles tab, and the tray menu "Copy crosshair code"): a short code like CO-XXXX-XXXX-XXXXX
  holds style, color, size, thickness, outline, opacity and effects. "Paste code" applies a friend's code.
- General tab > Show crosshair: Always / In Roblox only. In Roblox only hides it whenever another window is in
  front or Roblox is minimised (saved as only_in_roblox).
- Announcements: in CrosshairPublisher click "Announcement...", write a short message, Save. It's stored as
  news.txt in the GitHub repo. Players' apps read it at launch / update checks, show it once as a popup, then
  as a banner at the bottom of the settings window until they close it. Lines starting with # are ignored;
  empty the file and Save to take it down.

NEW IN 1.13.0 - Themes and a smaller panel
- 5 new themes (numbers 12-16), all gently animated with frosted-glass cards: Aurora (northern lights over
  mountains), Galaxy (nebula, stars, ringed planet), Ember (rising sparks, firelight), Deep Sea (light rays,
  bubbles), Sakura (falling petals, branch, moon). Each also has its own loading-screen art.
- Every theme's background gets a soft vignette for depth.
- Smaller settings window: opens at 85% by default; the - / + buttons next to the close button step through
  75 / 85 / 100 / 115 % (saved as panel_size). Still shrinks further on short screens.
- Themes tab: smaller tiles, 7 per row, so all 17 themes fit.

NEW IN 1.12.9
- Bug reports go straight to the Discord webhook again (BUG_WEBHOOK_URL); the relay is optional.
- After taking over from an older running copy, a popup says where the old file is; the app no longer searches
  Downloads / Desktop or deletes files there.

NEW IN 1.12.8
- The app is just CrosshairOverlay.exe now (the version is inside the exe, not in its name). The publisher uploads
  it to GitHub and Discord as CrosshairOverlay.exe and finds the newest one next to it by the version inside.
- Updates at every launch: the app checks GitHub as soon as it starts; if there's a newer version it installs it
  and reopens right away (popup "Updating to vX..."). Later checks every 6 hours still just offer the update.
- Newer version takes over: opening a newer exe while an older one runs closes the older one (WM_CLOSE to its
  window; works with every old version) instead of "already running", then a popup says where the old file is
  so the player can delete it. (The app doesn't search other folders or delete files itself.)
- Start with Windows (General tab): HKCU\Software\Microsoft\Windows\CurrentVersion\Run, starts with
  --autostart (no intro, no panel). The entry follows the exe if it moves.
- Updates tab: version + update status, Check now / Restart to update, auto-install switch, tester status, the
  changelog built into the exe (changelog.txt, resource 13; add the new version at the top for each release),
  and "Remove everything..." (puts back Roblox's cursors, frame-rate setting, boost flags, GPU choice, removes
  Start with Windows and the app's settings folder, then shows the exe to delete). Needs Roblox closed.
- UI cleanup: loading screen + loading music moved to the Themes tab; General is shorter; the window is 672 px
  tall (fits 1366x768 laptops); tighter sidebar.
- Tester list is read straight from GitHub's API (no 5-minute cache); the cached raw file is the fallback.
- Bug reports go straight to your Discord webhook (BUG_WEBHOOK_URL in webhook.h), like before. Optional upgrade:
  the relay (see BUG REPORT RELAY) keeps the webhook out of the exe; once REPORT_RELAY_URL is set, it's used instead.
- Update safety net: closing the app normally (after 5 s) also counts as "this version works", so closing it
  quickly a few times right after an update no longer rolls it back.

BUG REPORT RELAY (Cloudflare Worker, free) - one-time setup
- dash.cloudflare.com (free account) > Workers & Pages > Create > Create Worker > name: crosshair-reports > Deploy.
- Edit code > delete what's there > paste all of report-relay-worker.js > Deploy.
- Settings > Variables and Secrets > Add:
    Type Secret, name DISCORD_WEBHOOK, value = your bug-report Discord webhook URL
    Type Text,   name PING_USER_ID,    value = 329379772826124288
  > Deploy.
- Copy the worker's address (https://crosshair-reports.<your-name>.workers.dev), put it in webhook.h as
  REPORT_RELAY_URL, rebuild. Opening that address in a browser should say "report relay is running".
- Then delete your old bug webhook in Discord and make a new one for the relay (the old one was inside
  earlier exes, so treat it as public).

NEW IN 1.12.1
- Q/A test build: same as 1.12.0, only the version and release notes changed (checks that tester updates work).

NEW IN 1.12.0 - Safer updates, Q/A testers, separate publisher
- Update safety net: after an update the previous exe is kept as <exe>.old while the new version is on
  probation (update\probation.ini). If it crashes, or doesn't run for 45 s three starts in a row, the next
  start swaps the old exe back, skips that version on this PC (skip_update in settings.ini) until a newer one
  is published, shows "Update undone", and logs an error (which becomes a bug report). After 45 s of running
  fine the .old is deleted.
- What's new window after an update, listing the release notes built into the exe.
- Q/A testers: testers.txt in the GitHub repo lists tester IDs (one per line + optional name). Testers' apps
  also install pre-releases. Players find their ID in the tray menu: "Copy my tester ID".
- Separate publisher: build.sh now also makes CrosshairPublisher.exe (same source, -DPUBLISHER_APP). The
  player exe no longer has any publishing code or Windows' password-decryption functions (no crypt32 import),
  which avoids antivirus "stealer" false alarms. Keep CrosshairPublisher.exe to yourself.
  Publisher: Who gets it = Everyone (GitHub release + Discord post) or Testers only (GitHub pre-release, no
  Discord); publishing a tester build again to Everyone promotes it. "Ping the release role" switch
  (remembered). "Testers..." edits testers.txt on GitHub.
- FPS Boost: asks once before the app first changes Roblox's settings files (and says how to undo it).
  The Max FPS option no longer pauses Roblox's lighting (DFFlagDebugPauseVoxelizer); it's removed from
  installs that had it.
- The settings window shrinks to fit screens that are too short for it (e.g. 1366x768 laptops).

NEW IN 1.11.5
- Removed the FPS counter (added in 1.11.3): it couldn't read the game's frame rate reliably on every PC.
  The fps_counter / fps_counter_corner lines left in settings.ini by 1.11.3-1.11.4 are ignored.


NEW IN 1.11.4 - FPS counter fix
- Fixed: the FPS counter could show "-- FPS" the whole time. It now tries every graphics adapter that shows the
  game's monitor, uses DuplicateOutput1 (works with HDR screens and any DPI mode) before DuplicateOutput, and if
  Windows still refuses (e.g. recorders already using every slot) it falls back to counting the Windows
  compositor's frames (DwmGetCompositionTimingInfo). The reason is written to the log ("fps counter: ...").

NEW IN 1.11.3 - FPS counter
- FPS Boost tab > FPS counter (on/off + corner), also in the tray menu. A small pill in a corner of the game
  window, green / yellow / red by how smooth it is. Shown while a game is in front (Roblox, or any window that
  fills its screen) and hidden while the settings window is open.
- It counts new frames reaching the screen with Windows' Desktop Duplication API (what recorders use) on the
  game's monitor, so it never touches the game (safe with anti-cheat). That's the FPS you see, so it tops out
  at the monitor's refresh rate; Roblox's own Shift+F5 shows its internal render rate.
- Hidden from recordings like the crosshair. Saved as fps_counter / fps_counter_corner in settings.ini.

NEW IN 1.11.2 - Hide from recordings
- The crosshair and the on-screen keybinds card are excluded from screen capture (Windows' WDA_EXCLUDEFROMCAPTURE):
  OBS (display/window capture), SteelSeries Moments, NVIDIA / AMD recording, Discord streams, Snipping Tool and
  Print Screen leave them out, while you still see them. On by default; Effects tab > Hide from recordings.
  Needs Windows 10 version 2004 or newer (on older Windows nothing changes - it would show a black box there,
  so it isn't used). Saved as hide_from_recordings in settings.ini.

NEW IN 1.11.1 - Roblox crash reports
- The app waits for RobloxPlayerBeta.exe to exit (it never reads Roblox's memory). An exit with a crash code
  (0xC0000005, 0xC0000409, DXGI device removed, ...) is analysed: Windows' crash log (Application Error 1000:
  which DLL crashed), the newest Roblox log's error lines (long numbers and IPs replaced with #), graphics
  card + driver date, peak RAM use while playing, free space on C:, programs known to clash with Roblox
  (old FPS unlockers, Cheat Engine, RivaTuner, Medal, Overwolf, ...), the launcher and this app's settings.
- The player gets a tray popup with the most likely cause; clicking it (or tray > "Why did Roblox crash?")
  shows the likely causes and fixes. A "Roblox crash (automatic)" report goes to the bug webhook (pings you)
  if bug reports are on - at most one every 20 minutes per PC. Normal closes and Task Manager kills are ignored.

NEW IN 1.11.0 - Automatic updates + publish from the app
- Automatic updates: the app checks GitHub (UPDATE_REPO in update.h) 20 s after start and every 6 hours.
  A newer release's exe is downloaded to %APPDATA%\CrosshairOverlay\update and checked (it's a Windows
  program, the size matches, and its SHA-256 matches GitHub's) before it can be installed. It installs
  when the player clicks "Restart to update" (General tab, tray menu or the popup) or by itself the next
  time the app starts. Installing renames the running exe to .old, puts the new one in the same place
  with the same name (shortcuts keep working), starts it and deletes the .old.
  General -> Automatic updates turns installing off (it still downloads and tells you).
- The release notes are built into the exe (release-notes.txt -> resource 12).
- Publish a release (developer only): Ctrl+Shift+D in the settings window, or "Publish a release..." in
  the tray menu once set up. Pick any Crosshair Overlay exe (this one by default, or an older/newer build);
  its version and built-in notes are read from that exe (exes from before 1.11.0 use release-notes.txt
  next to them). Notes can be edited. Publish creates the GitHub release with the exe (what players'
  apps update from) and posts to the Discord release channel, pinging RELEASE_PING_ROLE_ID (webhook.h).

NEW IN 1.10.1 - White screen fix
- Fixed: Roblox could get stuck on a white screen after picking "Max" on the FPS Boost tab. "Max" wrote a
  frame-rate cap of 0, which Roblox can read as 0 FPS. The option is gone, and on start the app repairs a 0
  cap left by 1.10.0 (back to the cap you chose, 240 for old "Max", or Roblox's default).
- "Undo all FPS changes" now also resets Roblox's own frame-rate setting to its default (close Roblox first).


NEW IN 1.10.0 - FPS Boost tab (Roblox FPS unlocker + booster)
- Roblox FPS cap: Off / 60 / 75 / 120 / 144 / 165 / 240 / Max. Sets Roblox's own "Maximum Frame Rate"
  setting (FramerateCap in %LOCALAPPDATA%\Roblox\GlobalBasicSettings_NN.xml). Roblox only reads it at start
  and rewrites it when it closes, so if Roblox is open the app waits and sets it the moment Roblox closes,
  and puts it back if Roblox resets it. 240 is Roblox's highest official cap; "Max" asks for no cap, but
  Roblox may still stop at 240. The old DFIntTaskSchedulerTargetFps flag no longer works (Roblox ignores
  flags that aren't on its allowlist), so it isn't used.
- Performance boost: Off / Balanced (no anti-aliasing, no grass) / Max FPS (+ lowest graphics level and
  textures, simpler distant parts, paused lighting re-bake). Only flags on Roblox's official fast-flag
  allowlist, written to ClientSettings\ClientAppSettings.json in each Roblox version folder and in the
  Modifications folder of Bloxstrap / Fishstrap / Voidstrap. Flags already there are kept; turning the
  boost off removes only the ones this app set. Re-applied automatically after Roblox updates.
- Use high-performance graphics card: Windows' per-app GPU choice for RobloxPlayerBeta.exe (laptops with
  two GPUs).
- "Undo all FPS changes" removes the boost flags and GPU choice. The FPS cap stays as Roblox's own setting
  (change it in Roblox's menu). Nothing touches the running game's memory or code.
- Saved in settings.ini as roblox_fps_cap, roblox_boost, roblox_gpu.
- Bug log + reports: the app keeps %APPDATA%\CrosshairOverlay\log.txt (start/exit, errors, crashes).
  After a crash or an error it asks once whether to send bug reports to the developer; if yes, reports are
  sent automatically to the Discord webhook in webhook.h on the next start (General -> Bug reports turns it
  on/off). Every report pings the Discord user in webhook.h (BUG_PING_USER_ID); only that user can be
  pinged, so "@everyone" typed by a player does nothing.
- Report a problem (General tab, or the tray menu): the player picks what's wrong (Crashing / closing,
  Crosshair not showing, FPS Boost not working, Mouse cursor problem, Settings / keybinds, Something else)
  and types what happened; it's sent with the log attached (max once a minute). After a crash, the tray
  balloon "click here to say what you were doing" opens it with "Crashing / closing" picked. A report has the app version, Windows
  version, screen, settings.ini and the log, with the user folder replaced by %USERPROFILE%.
  With an empty webhook.h the button saves bug-report.txt instead.
- Crash lines look like "exe+0x2c6eb". build.sh writes CrosshairOverlay-vX.map next to the exe; keep it
  for each release and look up the function whose address is just below 0x140000000 + that offset.
- New app icon (the crowned red scope); small sizes zoom in on the scope so it stays readable.
- Settings window is 76 px taller to fit the new tab and the bug report row.


NEW IN 1.9.0
- Themes tab (settings -> Themes): Police (default), Criminal, Special Ops, Intel, Chosen Company, Midnight Run, Red Ops (the v1.8 look),
  Tactical, Retro Arcade, Vaporwave, Cyberpunk and Custom (pick your own accent + background).
  The theme changes the settings window, the on-screen keybinds card and the loading screen.
- New crosshairs: X + Dot, Split Circle, Dot Ring, Tri, Star, Pentagon, Line, Plus + Corners,
  and roleplay sights: Shotgun Spread, Holographic (EOTech-style ring + 1 MOA dot) and Cross-Dot.
  Shapes that looked like another one are hidden (Solid Circle, Mil-Dot, Octagon, Reticle, Taser);
  saved settings, profiles and keybinds that used them switch to the look-alike.
- Chosen Company theme: the loading screen uses cc_bg.jpg (your screenshot, built into the exe)
  as its background, with the unit's helmet quote typed under the title.
- Midnight Run theme (fully animated): the settings window plays the Charger scene live behind
  frosted-glass cards (camera drift, breathing taillights, brake taps, exhaust smoke, roof glint,
  rain, passing headlights, backfire pops, wet-road reflections, drifting dust),
  and the loading screen animates it too (push-in, taillights power on, speedometer, brake flash).
  Animation only runs while the settings window is open with this theme.
- Criminal theme intro: climb the crew ranks (Prospect -> Kingpin) or raise your wanted level;
  pick one on the Themes tab.
- New animated loading screen, different for every theme. "Preview loading screen" on the
  Themes tab plays it any time. General -> Loading screen still turns it off.
- The theme is saved in %APPDATA%\CrosshairOverlay\settings.ini (theme, theme_accent, theme_bg).

- Loading music (General tab): pick a song file on your PC (mp3, wav, wma, m4a) and the second it
  should start from; it plays during the loading screen and fades out after. The song stays on your
  PC - it is not built into the app.
- Fixed: Roblox's own mouse could stay invisible after closing the app (mostly with Bloxstrap,
  Fishstrap or Voidstrap). The app no longer saves a blank cursor as the "original", and on start /
  exit it repairs cursor files an older version left blank (from a clean copy in any Roblox install,
  or by removing them so Roblox restores them). Restart Roblox once afterwards.

BUILD
- Linux: install mingw-w64, run ./build.sh
- Windows (MSYS2 MinGW64): same two commands in build.sh with windres/gcc instead of the
  x86_64-w64-mingw32- names.
- Add -DCH_TEST (EXTRA="-DCH_TEST" ./build.sh) for the test hooks (CH_TAB, CH_THEME,
  CH_SPLASH_DUMP) used to screenshot the app; leave it off for release builds.

PUBLISHING A RELEASE (only you)
- Edit release-notes.txt (one short change per line, what players see), add the same lines at the top of
  changelog.txt, bump version.h, run build.sh.
- Open CrosshairPublisher.exe (keep it next to the new CrosshairOverlay.exe; it picks the newest one).
- Pick Who gets it: Testers only first, check it with your testers, then publish the same exe to Everyone.
- Ping switch: ping the release role in Discord, or post quietly.
- First time: paste the Discord release webhook and a GitHub token. They're saved encrypted for your Windows
  account only (%APPDATA%\CrosshairOverlay-Publisher) and are never in either exe.
- GitHub token: github.com -> Settings -> Developer settings -> Fine-grained tokens: only the releases repo,
  "Contents: Read and write".
- Testers...: add tester IDs (players get theirs from the tray menu > Copy my tester ID).

PRIVACY / WEBHOOK
- webhook.h holds the Discord webhook URL. Don't share the source zip publicly with the URL in it (anyone
  could post to the channel); blank it first. If it ever gets spammed, delete the webhook in Discord, make
  a new one, paste it in and rebuild.

SIGNING (removes "Unknown publisher")
- See sign-windows.ps1. Needs a code-signing certificate in your name: Azure Artifact
  Signing (~$10/month, individuals in the US/Canada) or an OV certificate from a CA.
