# Findings

## macOS arm64 (2026-10-13, branch macos-metal)
Builds and starts on an M3 Pro with x86recomp's SDL_GPU backend on Metal
(no dxvk; CMake leaves DXVK_NATIVE_DIR empty on Apple hosts). A fresh
Ghidra 12.1.4 analysis plus EaxMan.dll lifts to the module counts in the
table below (CoDSP 5291, gamex86 1996, cgamex86 1214, uix86 769
functions; EaxMan 339). With an intact install (an earlier copy had
damaged pk3s: "17 sound file(s) are missing or in a bad format"; test
pk3s with Python's zipfile) it plays the intro cinematic and, with
`-- +map training` and a scripted click to start the mission, reaches
the training level: sky, terrain, alpha-tested foliage, models, HUD,
autosave. Not yet played by hand or compared against the d3d9 backend.
Killed (scripted) runs leave the __CoDSP safe-mode marker in the overlay,
~/Library/Application Support/cod1/install on macOS; background runs then
take safe mode's default and safemode.cfg's picmip 2 / 11 kHz end up in
main/config.cfg. Delete the marker before each run (and that config once
it has them): autoconfigure then picks 2000/512/256 (r_mode 6, picmip 0,
44 kHz) as on Linux. With the HLE sync barriers (x86recomp 0869565) the
training run is unchanged.

## Status (2026-10-12)
M0 (recon) done; M1 under way. Decided: static recompilation on the
shared x86recomp library (factored out of DS1 the same day), graphics
through gfx::Device so macOS/Metal stays open (ARCHITECTURE.md).

- The Steam wrapper on CoDSP.exe and CoDMP.exe is undone statically by
  tools/unpack_steam.py.
- Ghidra (cod1_recomp.gpr, plain auto-analysis) has the unpacked
  CoDSP.exe and the three SP DLLs; symbols exported per module.
- All four modules are lifted in one recomp_lift run (x86recomp
  multi-module support, 56c26cf) and linked into cod1; the three game
  DLLs are GameConfig::lifted_dlls, mapped at startup at their
  preferred bases. Coverage below.
- The game boots to its main menu: intro cinematics with sound (atvi,
  iw_logo, cod_intro), the menu with its backdrop, menu music, at the
  autoconfigured best settings (1024x768, picmip 0, 44 kHz). New Game
  (skill popup, briefing, "Click to Start the Mission") starts the
  training level, which renders correctly and takes mouselook; the
  level-start autosave is written. See "Run log".
- Game patches are replacements of lifted functions (manifest
  `replace`, src/): one cinematic fix (src/cinematic.cpp), and native
  CPU skinning (src/skinning.cpp, bit-exact; "Run log" 2026-10-13).

Training plays through to completion (2026-10-12, played by hand on EAX
3). Its frame-rate drops were CPU-bound: x86recomp's GL draw path and
native skinning removed them (no frame over 25 ms in a played session;
"Run log" 2026-10-13).

Next: collision traces (about 45% of the main thread in training, on the
x87 model); then widescreen/FOV, UI scaling and high frame rates (design
and order: "Widescreen, FOV, 2D placement, frame rate").

EAX works: with `mss_3d_provider "Creative Labs EAX 3 (TM)"` (a latched
cvar: takes effect on the next start, or pick it in the sound options)
the game runs Creative's EAX Manager (EaxMan.dll, lifted as a fifth
module) on x86recomp's EAX providers. See "Run log".

## Widescreen, FOV, 2D placement, frame rate: design (2026-10-13)
Investigation only: nothing implemented, nothing renamed in Ghidra (the
Quake 3 names below are likenesses, not confirmed names). Addresses are
VAs in the unpacked CoDSP.exe, cgamex86 (0x3...), uix86 (0x4...) and
gamex86 (0x2...). Decided with the user: the RoQ cinematics and the menus
are 4:3 assets and stay pillarboxed (black bars), not stretched or
cropped.

### Today (scripted runs in training, isolated XDG_DATA_HOME)
- `+set r_mode -1 +set r_customwidth 1280 +set r_customheight 720
  +set r_fullscreen 0` works: a 1286x745 window, back buffer 1280x720.
  The view is Hor- (fov_x 80 kept, the vertical cut), the HUD is
  stretched (compass and stance icon are ellipses), text glyphs are not.
- The same with r_fullscreen 1: "calling CDS: failed, bad mode", then
  "trying next higher resolution: ok": x86recomp's ChangeDisplaySettingsA
  only knows 640x480-1600x1200 (4:3 and 5:4, up to the desktop size), so
  the virtual display becomes a larger 4:3/5:4 mode and the game opens a
  1280x720 popup in it. The host window is never fullscreen: it is a
  borderless window of that size, and present stretches the front
  buffer to the window without keeping the aspect.

### Resolution
- Renderer cvars (0x4A7A50): r_mode "3" (latched), r_fullscreen "1",
  r_customwidth "1600", r_customheight "1024", r_customaspect "1",
  r_displayRefresh "0", r_swapInterval "0" (archived).
- Mode table at 0x564A28, 13 entries {description, width, height, pixel
  aspect 1.0}, count at 0x564AF8: 0-10 are 320x240 to 2048x1536 (4:3 and
  5:4), 11 is 856x480 and 12 is 1920x1200 ("(wide)"). The graphics menu
  (options_graphics.menu) offers only 3, 4, 6, 7, 8, 9, 10; its 856x480
  entry is commented out.
- 0x4A5610 (mode in EAX; out width, height, window aspect): -1 takes
  r_customwidth/height and window aspect = r_customaspect (a window
  aspect, not a pixel aspect); a table mode gives w / (h * pixelAspect).
  The window aspect (glconfig +0x8C, 0x15347CC) is read by neither cgame
  nor ui.
- 0x4FDB60 sets the mode ("...setting mode %d:"): fullscreen does
  ChangeDisplaySettingsA (colour bits, r_displayRefresh), on failure the
  first EnumDisplaySettings mode at least as large, else windowed.
  0x4FD810 creates the "Call of Duty" window: fullscreen WS_POPUP at 0,0
  (style 0x90080000), windowed WS_CAPTION at vid_xpos/vid_ypos (style
  0x10C80000, AdjustWindowRect).
- vidWidth/vidHeight: renderer glConfig 0x15347C4/C8, client copy
  0x143942C/30, cgame 0x300FE984/88 (its glconfig at 0x300FE900, syscall
  0x50), ui 0x401DE750/54 (glconfig 0x401DE6CC, syscall 0x33).

Design:
- x86recomp, generic Windows behaviour: Modes() also lists the desktop
  mode and the common wide sizes up to it (1280x720, 1366x768, 1600x900,
  1920x1080, 1920x1200, 2560x1080, 2560x1440, 3440x1440, 3840x2160). A
  top-level popup covering the whole (virtual) screen after a mode change
  goes host fullscreen (SDL borderless desktop fullscreen, no real mode
  switch), and Show() blits with the aspect kept (black bars) instead of
  stretching, so a 4:3 mode on a 16:9 monitor is pillarboxed. An accessor
  for the desktop size for game code. DS1 (ddraw exclusive mode) shares
  the path and must keep working. On macOS SDL3 display sizes are points:
  "native" means points unless the window asks for high pixel density.
- CoD1: replace 0x4A5610 so that r_mode -1 with r_customwidth/height 0
  is the desktop size, window aspect w/h. Native resolution is then
  `seta r_mode -1; seta r_customwidth 0; seta r_customheight 0` (the menu
  cannot select it); any other size or aspect is r_mode -1 with explicit
  numbers, windowed or fullscreen.

### FOV
- 0x30025200 returns fov_x in ST0: cg_fov (vmCvar 0x301E7B40, value at
  +8; "80", archived) clamped to [1, 160]; while aiming down the sights
  it moves toward the weapon def's (0x30125668) adsZoomFov (+0x218) by
  the ADS fraction 0x3011E15C (adsZoomInFrac +0x21C, adsZoomOutFrac
  +0x220); bits 0xC000 of 0x3011E124 force 55 (state not identified).
- Callers: the view setup 0x30025C80 (refdef at 0x30125724: x, y, width
  0x3012572C, height 0x30125730, fov_x 0x30125734, fov_y 0x30125738)
  computes fov_y = atan2(height, width / tan(fov_x / 2)) * 360 / pi:
  Hor- on wider screens. It then sets the zoom sensitivity 0x301257AC =
  fov_x / cg_fov, which 0x30026B10 sends with the usercmds. The script
  camera path (syscall 0xDB, "setCameraOrigin") sets its own FOV. The sky
  portal 0x300260A0 ("CG_DrawSkyBoxPortal" errors) recomputes both from
  0x30025200 the same way.
- Hor+: replace 0x30025200: the lifted value, then, when the screen
  aspect A = vidWidth / vidHeight (cgame's glconfig) is above 4/3,
  fov' = 2 atan(tan(fov / 2) * A / (4/3)). Both callers derive fov_y from
  it, so the vertical FOV is what 4:3 gets at the same cg_fov, for hip,
  ADS and the 55 case alike. At 4:3 and below nothing changes. cg_fov 80
  gives 96.4 at 16:9, 111.5 at 21:9.
- Mouse feel: wrap 0x30025C80 and, when 0x30025200 ran inside it, write
  0x301257AC = 4:3 fov / cg_fov again (otherwise the sensitivity grows by
  fov'/fov).
- The viewmodel is drawn in the same scene and projection (only
  r_znear_depthhack differs; there is no gun FOV, cg_gun_* are offsets),
  so Hor+ keeps its vertical framing. Check the arm's cut ends at 21:9.
- Scope overlay 0x3000F770: four mirrored quarters of the weapon's
  adsOverlayShader (weapon def word 0x89) of adsOverlayWidth/Height
  (0x8B/0x8C) times the screen scales, centred on the aim point from
  0x3000F500, then black fills from it to the refdef edges in real
  pixels. 0x3000F500 (also used by the crosshair/reticle code 0x3000FF00,
  0x30010670, 0x300109A0) turns a view direction into a 640x480 offset
  with the refdef's fov_x/fov_y, so it follows Hor+ by itself; see the 2D
  placement for the overlay. 0x3000FF00 also reads fov_x/fov_y directly
  (0x3001043A, 0x30010166): check the crosshair spread under Hor+.

### 2D placement (menus, HUD)
- ui: 0x4000C1E0 (fetches the glconfig, fills the display-context table)
  sets yscale 0x401C045C = H/480, xscale 0x401C0460 = W/640 and a wide
  bias 0x401C0464 = (W - H*4/3)/2 that nothing reads. The draw helpers
  multiply by the scales inline (0x40007590, 0x40007660, 0x400076D0,
  0x40007770, 0x40007BF0, 0x40007C90; 0x40007D80 picks a font by
  yscale) and draw through 0x40018370 (DrawStretchPic syscall).
- cgame: 0x3001AF80 sets screenXScale 0x300FE9A0 = W/640 and
  screenYScale 0x300FE9A4 = H/480; some 20 functions read them inline
  (0x30012040, 0x300120E0, 0x30012200, 0x30012260, 0x3001CA20,
  0x3001DFA0, 0x3000F770, 0x3000FF00, ...) and draw through 0x30022DD0
  (syscall 0x4B) and the text syscall 0x38 (0x30022B40). There is no
  single adjust function to patch.
- Engine: 0x40FED0 has the shape of SCR_AdjustFrom640 (client glconfig
  copy); CIN_DrawCinematic-like 0x4076F0 uses it for the RoQ rectangle
  and its letterbox bars, then DrawStretchRaw.
- Renderer: the refexport is filled by 0x4A94C0 (REF_API_VERSION 14)
  and copied to the client's table at 0x1426D60 by 0x40E460. 2D enters
  through SetColor 0x4D42A0 (command 1), StretchPic 0x4D4330 (command 2:
  shader, x, y, w, h, s1, t1, s2, t2), 0x4D43D0 (command 3: the same plus
  a colour and one more word), 0x4D44D0 (0x2C-byte command), 0x4D45B0
  (0x48 bytes: four vertices; cgame's rotated quads 0x30012260, syscall
  0x4E), text 0x4D6400 (command 6: x, y, font, scale, colour, ...,
  string; also via 0x4D5D60, 0x4D6110, 0x4D6250) and StretchRaw 0x4CCD10
  (cinematics, drawn at once). The command loop 0x4CEA70 switches on ids
  1-11; the 2D setup 0x4CCC10 is an ortho of 0..vidWidth x 0..vidHeight,
  so all 2D arrives in pixels. Text has one uniform scale (0x4CDCC0),
  which is why glyphs are not stretched.
- The HUD is ui/hud.menu (localized_english_pak0) painted by cgame's
  menu code: 0x3003D3E0 ("fps: %f", Menu_PaintAll-like) calls
  0x3003B710 (Menu_Paint-like: menu in ESI, forcePaint in EAX, flags at
  +0x48 with visible = 4, fullscreen menus fill 0,0,640,480). Menus:
  "Compass" (x -25..135, y 345), "stance" and "tankstatus" (100, 434),
  "weaponinfo" (0..640 at y 420; weapon name and ammo at x 242-637),
  "Health" (488, 460), "Cursorhints" (300, 325). Outside menus: crosshair
  and reticles, the scope overlay, script hudelems, centre prints and
  subtitles (cg_subtitles), the damage direction. Game messages
  ("Objectives updated.", con_gamemessagetime) are the engine console's
  and already sit at the screen's left edge.

Design: one placement transform in pixel space where 2D enters the
renderer. With A = W/H > 4/3, k = (4/3)/A and b = W(1-k)/2 (identity
when A <= 4/3):
- stretch: unchanged (the original).
- center: x' = kx + b, w' = kw; a rectangle spanning the full width
  stays full width (fades, letterbox bars, full-screen hudelem shaders).
- left: x' = kx. right: x' = kx + 2b.
- scope: center, and rectangles touching the left or right screen edge
  are extended to it (the overlay's black fills).
Text moves x only (its scale is already uniform); the four-vertex quads
move each vertex; StretchRaw is a rectangle. Each 2D entry above becomes
a wrapper that rewrites its x/w arguments in place and calls the lifted
body. The mode comes from a save/restore stack set by wrappers of:
- uix86 vmMain: center (pillarboxed menus and loading screens).
- cgamex86 vmMain: center.
- Menu_Paint 0x3003B710: by menu name (window name pointer; offset to
  confirm, +0x20 in Q3TA's layout): Compass, stance, tankstatus left;
  weaponinfo, Health right; others center.
- 0x3000F770: scope. 0x3000F500 divides its x output by k in center and
  scope mode, since the offset is computed with the real fov_x and the
  transform would shrink it.
- 0x4076F0: center (cinematics pillarboxed; their bars are full width).
- Everything the engine draws itself (console, game messages, warning
  icons): stretch.
The UI cursor moves in 640x480 space and is drawn through the same
transform, so it stays on the items. To check: right- or centre-aligned
text shifts by w(1 - k) (positions move, glyph widths do not); whether
script hudelems at the screen edges need anchoring.

### Frame rate
- Com_Frame-like 0x430690 (it holds the error longjmp's setjmp): minMsec
  = 1000 / com_maxfps (integer; com_maxfps "0" means 1 ms), then it spins
  on 0x42F180 (event loop, timeGetTime ms) until that much has passed.
  No sleep: a cap burns a core, and only 1000/n rates exist (120 gives
  125, 144 gives 166, 240 gives 250). Then 0x430620 (fixedtime,
  timescale, clamp), the server frame 0x44CF10 and the client frame
  0x40DF80. r_swapInterval 1 reaches SDL_GPU as VSYNC, 0 as mailbox
  (x86recomp 32f0634).
- The server frame runs whole 50 ms game frames (20 Hz) from an
  accumulator (0x14F484C): AI, scripts, entities and snapshots do not
  depend on the frame rate.
- Player movement does. ClientThink-like 0x20012430: pmove_msec
  (0x20294C80, "8", clamped to 8..33) and pmove_fixed (0x202BD460, "0";
  when set, the command time is rounded up to a multiple of pmove_msec).
  Pmove-like 0x20008580 cuts each command into steps of at most 66 ms
  (pmove_msec when fixed) for 0x20007FD0 (msec clamped to 1..200,
  frametime msec * 0.001). Without pmove_fixed each frame's usercmd is
  one step of the frame's integer msec. Quake 3's per-step velocity
  snapping is not at the end of 0x20007FD0 (CoD recomputes the velocity
  there from the distance moved when blocked), so how much jump height
  and speed change with fps is unmeasured. cgame mirrors it (pmove_fixed
  0x301E6DC0, pmove_msec 0x301034E0).
- Prediction scales with fps: 0x3001EF70 (CG_PredictPlayerState-like)
  replays every usercmd newer than the latest snapshot (up to 64)
  through Pmove 0x30007C30 each frame. Snapshots come at 20 Hz, so a
  frame replays about fps x (time since the snapshot) commands: roughly
  2-3 at 60 fps, 7 at 240, and the per-second trace work grows about
  with fps squared. This is the cgame -> CM trace path that is ~45% of
  the main thread now.
- RoQ playback follows the audio clock (0x407040). Animations and
  effects use integer cg.time. Menu fades: Q3TA's Fade advances one step
  per frame when fadeCycle is shorter than a frame (hud.menu has
  fadeCycle 1), so fades would run faster at high fps; not yet located
  in CoD's code.
- Budget: the training walk averages 5.45 ms (p99 9.3 ms, run log
  2026-10-13), about 180 fps; 240 fps needs 4.17 ms.

Design:
- Pacing: wrap 0x430690 so that it first sleeps until the next frame
  deadline on a host high-resolution clock (period 1/com_maxfps, not
  rounded; sleep to ~1 ms before it, then a precise wait such as SDL3's
  SDL_DelayPrecise), then runs the lifted body. Its own integer wait is
  then already over: no spinning, and 144/165/240 are exact. Check that
  the lifted body still catches the longjmp (manifest `setjmp`) when it
  is f_00430690_lifted. For VRR: r_swapInterval 0 and com_maxfps a few
  under the refresh rate; fixed refresh: r_swapInterval 1, com_maxfps 0.
- Measure before changing physics: scripted jumps and runs at com_maxfps
  60/125/250/333/500 with pmove_fixed 0 and 1 (apex and speed from a
  probe on the playerstate origin), menu fade durations, gun sway/bob at
  60 vs 240.
- If movement depends on fps: fixed pmove steps on both sides (pmove_fixed
  behaviour) plus interpolating the predicted view origin between the
  last two steps in cgame, because pmove_fixed alone moves the view at
  125 Hz.
- Prediction: keep the predicted playerstate per command number and run
  only the new commands while the snapshot is unchanged (a full replay
  on each new snapshot): a native 0x3001EF70 with an oracle test against
  the full replay. Overlaps the collision-trace work.

### Implementation order
| # | Item | Where | Size |
|---|---|---|---|
| 1 | Hor+: replace 0x30025200, wrap 0x30025C80 (zoom sensitivity); check 4:3 unchanged, 16:9/21:9 screenshots | CoD1 | S, ~60 lines |
| 2 | Display modes: desktop and wide modes, host fullscreen for full-screen popups, aspect-kept present; desktop-size accessor | x86recomp | M, ~150 lines; DS1 retest |
| 3 | Native resolution: replace 0x4A5610 (r_mode -1 with 0x0 = desktop) | CoD1 | S, ~30 lines |
| 4 | Frame pacing wrapper on 0x430690 | CoD1 | S, ~60 lines |
| 5 | 2D transform: the renderer 2D entries, mode stack, ui/cgame vmMain, 0x4076F0 | CoD1 | M, ~250 lines |
| 6 | HUD anchors: Menu_Paint by name, scope overlay, 0x3000F500 | CoD1 | S-M, ~100 lines + screenshot checks |
| 7 | Measurements: pmove vs fps, fades, sway | scripted runs | S |
| 8 | Prediction cache for 0x3001EF70 + oracle test | CoD1 | L, ~300 lines + test |
| 9 | Fixed-step pmove with view interpolation, if 7 shows the need | CoD1 | M-L |
| 10 | Time-based menu fades, if 7 shows the need | CoD1 | S |

FOV first (small, self-contained, the biggest visible gain), then the
mode work so widescreen can be fullscreen, pacing (cheap), then the 2D
placement (the stretched HUD is the most visible remaining fault). The
measurements decide 9 and 10; 8 is what 240 fps needs in CPU time.

## Lift coverage (x86recomp 5640e2f, before SSE support; no manifest tuning)
Measured with one lift per module; the multi-module lift (56c26cf)
gives the same function and instruction counts per module (its pointer
scans only accept targets in the module the pointer is in; without
that, DLL data produced 50 bogus weak entries in the EXE).

| Module | Functions (Ghidra + discovered) | Instructions | Unimplemented sites |
|---|---|---|---|
| CoDSP.exe | 3290 + 1999 | 389 369 | 549 |
| gamex86.dll | 1432 + 564 | 146 103 | 333 |
| cgamex86.dll | 965 + 249 | 83 316 | 356 |
| uix86.dll | 541 + 228 | 41 470 | 356 |

- Every module has the same ~330 SSE2 sites (movlpd, pextrw, mulsd,
  addsd, ...): the statically linked MSVC 7 CRT's SSE2 libm, taken when
  the CPU reports SSE2. CoDSP.exe also has packed-single SSE (addps,
  mulps, shufps, movaps, prefetchnta), presumably engine SIMD paths
  chosen by CPU detection.
- Since x86recomp e24e849 the CPU is reported as SSE2-class and SSE/SSE2
  is lifted exactly: all three DLLs lift with no unimplemented sites;
  CoDSP.exe keeps 84, all junk decodes from weak entries that are really
  data (aaa, daa, in/out, iretd, les, ...) plus one SSE3 fisttp (SSE3
  is not claimed). lift_sse passes CoDSP.exe's 244 SSE forms bit for
  bit (2.44M checks at 10k trials); ctest with COD1_GAME_EXE: 6/6.
- Rich header: everything built with VS.NET 2002 (build 9466).
- SEH: the CRT's __except_handler3 matches DS1's byte for byte; its
  continuation call is a manifest `resume` site in every module
  (CoDSP 0x528277, gamex86 0x2008126B, cgamex86 0x3004AB5B,
  uix86 0x40023A8B).

## Miles 6.5e
The game ships Miles 6.5e (mss32.dll, miles/msssoft.m3d). Their digital
master reverb, sample levels and Fast 2D are written up in x86recomp's
docs/miles.md (addresses, constants) and implemented there bit for bit;
the oracle tests take the DLLs from -DCOD1_MSS32_DLL. What the game
does with them:
- It never changes the driver's master volume (1.0), Fast 2D's "Mute at
  maximum" (on) or rolloff factor (1.0), and makes no velocity calls
  (no Doppler).
- Rooms and wet levels: see the digital reverb run-log entry.

## Run log
### 2026-10-13: faster exact x87 (x86recomp x87-fast 7e0dc70)
- The game runs its float maths at the CRT's PC=53/RC=nearest (CW
  0x027F). The only _controlfp call, 0x528FE1, sets _PC_53, and the
  GL HLE never touches the CW.
- Lifted runs had only the PC=24 arithmetic inline, so every fadd,
  fmul, fcom and rounding fst went out of line. In the scripted
  training walk the out-of-line x87 helpers were 57.7% of the main
  thread: run::ArithF32 alone 33%, run::Arith 8%, compares 9%.
- x86recomp now has these inline and exact (integer only):
  - PC=53 add/sub/mul/div of up to 53-bit significands;
  - zero operands;
  - m32/m64 loads, rounding stores and compares;
  - single-instruction runs;
  - an empty mask instead of the tag word;
  - a control-word layout that no longer misses store forwarding.
- Scripted training walk: the uncapped walk with COD1_TEST_INPUT, an
  isolated XDG_DATA_HOME, and F8 bound to quit. Measured over the
  in-game frames, 12-88 s, three runs of the final build and four of
  the baseline, repeatable to 0.02 ms:
  - Mean frame: 5.40 -> 4.77 ms (-11.7%).
  - p95: 8.5 -> 7.3 ms. p99: 9.4 -> 8.0 ms.
  - Frames in the window: 14070 -> 15930.
- Main thread, perf:
  - Out-of-line x87 helpers: 57.7% -> 1.1%.
  - The collision code (0x419000-0x41DFFF, now with the x87 work
    inline) is 60% of the main thread. Before, its self time (19%)
    plus the helpers was about 77%.
- What is left is the latency of exact integer arithmetic, about 5 ns
  per dependent operation. Host doubles would be about 5x faster on
  this code but need a change to x86recomp's rules.
- ctest 12/12 with COD1_GAME_EXE and COD1_MSS32_DLL. That includes
  lift_x87 (4110 forms, 82.2M checks against the CPU), skinning_oracle,
  digital_reverb_oracle and fast2d_oracle.

### 2026-10-13: SDL_GPU (Vulkan) is the default backend
- x86recomp 32f0634: SDL_GPU on Vulkan by default; dxvk-native optional
  (COD1_GFX=d3d9). ctest 12/12 (oracles included) on it.
- Same scripted runs on both backends (COD1_SHOT_EVERY): the training
  briefing is identical (0 pixels over 2% fuzz); in-level frames look
  the same, 35 dB PSNR at equal frame numbers from animation timing
  (the NPC and trees move). Same 15 loading hitches either way.

### 2026-10-13: native CPU skinning (src/skinning.cpp)
- Frame drops in training were CPU-bound on the main thread; after
  x86recomp's GL draw path speed-up (branch gl-draw-perf) the x87/SSE
  model was 72-84% of it, and in the user's heaviest stretch the two SSE
  skinning functions alone were 59%: 0x507D20 (weighted) 43% and
  0x5070E0 (rigid) 16%, packed SSE through the SoftFloat lanes.
- Both are slots of the surface function table at 0x564270 (5: rigid,
  10: weighted; the other slots include the non-SSE versions), called
  through it from 0x4CA987, 0x4CC6EC and 0x4CCB06. Layouts (arg block,
  surface, vertices, extra bones, matrices) are in src/skinning.cpp
  and in the Ghidra plate comments. Each: RB_CheckOverflow (0x500A60,
  named from its error strings), indexes appended by 0x480820 (+ first
  vertex), texcoords by 0x42B0A0 (a memcpy with the structure of Quake
  3's Com_Memcpy, left unnamed), then per vertex position and normal
  into the tessellator, stored with 16-byte movups at a 12-byte stride.
  The normal of a weighted vertex takes the first bone only.
- Replaced natively (manifest `replace 005070E0` / `replace 00507D20`):
  same per-lane IEEE single operations in the same order (the two
  functions sum the translation row at different points), compiled
  with -ffp-contract=off; a vertex with a NaN result is redone with
  SSE's NaN rules (first NaN operand quietened, else 0xFFC00000), which
  host arithmetic does not follow. The lifted callees are called as the
  original's call sites do (same frame depth and return addresses), and
  the argument slot gets the original's last value. Not reproduced:
  MXCSR's sticky flags (no ldmxcsr in any module; the only stmxcsr, in
  the CRT's SSE2 math entries at 0x51A7A0, 0x51AB30, 0x52E2D0 and in
  cgame/ui, mask with 0x1F80), scratch/XMM registers and EFLAGS (all
  three call sites overwrite them first).
- ctest skinning_oracle (configure with -DCOD1_GAME_EXE): the originals
  lifted with --only and run on the SSE model against the native code,
  memory and callee-saved registers compared byte for byte: 6000 random
  surfaces (zeros, denormals, infinities, quiet/signalling NaNs, overflow
  and underflow, raw bits; 245733 NaN outputs) plus captured game calls
  (-DCOD1_SKINNING_CAPTURE=ON build, COD1_SKINNING_CAPTURE=file): 3703
  calls from a training walk, 0 mismatches. Mutations (NaN operand
  order, default NaN sign, add order, argument slot, NaN redo, 12-byte
  store) each fail it. `--only` leaves out noreturn callees, so the
  test lists them too.
- Scripted training walk (uncapped, gl-draw-perf x86recomp, 3 runs each,
  repeatable to 0.1 ms): skinning was 29.5% of the main thread in the
  15-30 s stretch (12.1% over the walk), now 0.4%; that stretch's
  frame time 7.0-8.5 -> 5.9-6.4 ms, walk mean 5.86 -> 5.45 ms, p95 9.4
  -> 8.6 ms, p99 10.5 -> 9.3 ms. The SSE model is now ~0% of the main
  thread; the x87 model is still 60%, mostly collision traces (0x41B8F0
  14%, 0x41CD60 13%, 0x41D3B0 7%, 0x419EE0 6%, 0x4196F0 4%).
- The user's manual training session (perf 300 Hz, COD1_HITCH=25, own
  config: vsync 60 Hz, ~130 s in game) against the same kind of session
  on the gl-draw-perf build before it: main thread busy 72% -> 60% of
  the in-game time; the stretch that was saturated for ~25 s (98-100%
  busy at 20-48 s, skinning up to 69% of it) has no counterpart: the
  busiest 5 s is now 95%, most 50-80%. Skinning 33.8% -> 0.6%, SSE
  model 33.3% -> ~0%. No frame over 25 ms once in game (before: one of
  31.5 ms at mission start). What fills the main thread now: x87 model
  59% (leaf), collision traces from cgame (0x3001EF70 > 0x30007C30 >
  CM 0x41CD60 / 0x41C0A0) ~45% (self: 0x41B8F0 18%, 0x4196F0 15%,
  0x41CD60 7%, 0x41D3B0 5%), GL draw 6.5%.

### 2026-10-12: snd_restart and the mouse; in-game restarts crash
- snd_restart (the sound options' apply, e.g. picking another 3D
  provider) also restarts the renderer: DestroyWindow of the game window
  and a new one at the same place, without ClipCursor(NULL) first. The
  game then never clips again (its mouse is still "active"), so the new
  window got no grab: mouse look drifted and the pointer escaped. Fixed
  in x86recomp (a clip outlives its window, as on Windows). Checked
  with a scripted menu snd_restart then training: relative motion turns
  the view as before the restart.
- From inside a level the restart is "savegame internal\snd_restart"
  (same for vid_restart); once the save is written ("Game saved for
  snd_restart") the game queues "disconnect;snd_restart;loadgame
  internal\snd_restart" itself. The disconnect is an error drop
  ("ERROR: Disconnected"), which ends in the CRT's longjmp (jmp at
  0x51CAB1) back to 0x4306A6 in 0x430690: was fatal, unlifted indirect
  target.
- Fixed with x86recomp b66a6ec (setjmp/longjmp; manifest `setjmp
  0052E84C` for __setjmp3, `resume 0051CAB1` on _longjmp's jmp): its
  two callers, 0x42FDE0 and 0x430690, catch the drop and continue at
  their setjmp return points 0x42FE16 / 0x4306A6. Checked with a
  scripted F11 bound to "savegame internal\snd_restart" in training:
  disconnect, Miles re-init, training reloads from the internal save,
  and after the briefing click the level plays on. Binding the whole
  line instead disconnects before the save is written (the save
  completes after the rest of the line has run), so its loadgame finds
  no save: an artefact of that test bind, not of the menu.
- cod1 now quotes game arguments with spaces (`-- +set mss_3d_provider
  "Creative Labs EAX 3 (TM)"` reached the game as "Creative").

### 2026-10-12: Miles 6.5e Fast 2D and sample levels (x86recomp 0acb1ee)
- x86recomp's runtime/audio/miles65 implements docs/miles.md's "Miles
  6.5e sample levels and Fast 2D" for 2D samples, streams and Fast
  2D 3D samples of a Miles 6.5 game (Voice::miles; Dungeon Siege keeps
  the 6.1c rule, its renders bit-identical; EAX voices keep the 6.1c
  rule too).
- ctest fast2d_oracle (configure with -DCOD1_MSS32_DLL; msssoft.m3d is
  taken from miles/ next to it) lifts msssoft's update and listener
  setters and mss32's level setters and pow: 240000 results per pow
  path (x87, SSE2) bit-identical, among them muted, centred, one-channel
  and NaN (acos domain) cases.
- Against the Audigy 2 ZS recordings (fast2d set, eax_render --provider
  fast2d): every L/R level within 0.1 dB of the hardware (45/90/135/180
  degrees both sides, distance 1-8 x min); before, centre was 0 dB per
  channel, +-45 near 0 / far -6.1, behind -3.1, and -90 far -36.4.
- Audible change in game: 2D sounds and music now get 0.5^0.3
  (-1.8 dB) per channel at centre and volume^(5/3); Fast 2D sounds -6
  dB per channel at centre relative to hard-panned ones, as in the
  original. Fast 2D now refuses ADPCM and stereo 3D data, as msssoft
  does.

### 2026-10-12: Miles-compatible digital reverb (x86recomp 4d0597e)
- The behaviour of mss32 6.5e's digital master reverb is written up in
  x86recomp's docs/miles.md; its runtime/audio/digital_reverb implements
  that description and matches the original bit for bit (ctest digital_reverb_oracle, which lifts the
  routines from the install's mss32.dll at build time; configure with
  -DCOD1_MSS32_DLL="<Call of Duty>/mss32.dll").
- AIL_3D_provider_attribute (0x2111DB30) stores -1 before the lookup.
  The game presets its "EAX3 room LF" probe to 0 (0x4417FA), so with
  the HLE's old "leave unchanged" every provider counted as EAX
  (0x14F47F0 = 1) and the room setter 0x445190 only reached the digital
  reverb for "underwater". Fixed in the same x86recomp commit.
- In game: rooms reach the reverb from 0x445190 (mss_roomtype /
  mss_wetlevel when modified, via 0x4452E0 in the sound update
  0x445780; the setReverb script call, only in sewer.gsc: "stoneroom",
  .7, 3; cgame syscall 0xD6). The wet level reaches 2D samples at start
  (0x442850) and 3D samples as effects level (0x44542C). With
  `+set mss_roomtype 7 +set mss_wetlevel 0.5` the main menu arms room 7
  (concert hall) and it times out after 4.1 s, but every sound reset
  (cinematic end, 0x445A15) sets "generic", 0 again, and in levels
  cgame's shell-shock update (0x30021820) does so every frame when no
  shock is active, overriding sewer's stoneroom right after it is set.
  No sample with a wet send was heard in the scenes reached (training,
  pathfinder, sewer, menu), so the reverb ran silently; whether the
  original behaves the same is unverified.

### 2026-10-12: EAX (x86recomp 23adb0b)
- Provider choice (0x4417F0): `mss_3d_provider` names one of the
  enumerated providers (fallback Fast 2D). Then
  AIL_3D_provider_attribute(provider, "EAX3 room LF", &v) with v preset
  to -1: if v changes the provider is EAX 3 and EAX is enabled
  (0x14F47F0), which loads EAXMan.dll and calls EaxManagerCreate.
- EAX Manager (Creative's EAGLE runtime) ships in the install
  (EaxMan.dll, MSVC 6 CRT, imports kernel32/advapi32 and one ole32
  call for COM self-registration, which the game never uses). Per map
  the game loads eagle/<mapname>.eal (pak9: the campaign and MP maps,
  not training) into it (0x447970); each listener move asks it for the
  environment at the listener (vtable +0x40) and, when that changes,
  for the environment's EAX 3.0 listener properties (+0x30), passed
  whole as "EAX all parameters" with air absorption zeroed (0x447B20).
  Each 3D sound update gets obstruction, occlusion and their LF/room
  ratios from it (0x447BD0, "EAX2 sample ..." preferences). Without EAX
  the game sets "EAX2 room" to -10000 (reverb off).
- Pathfinder's start (outdoors, night) is EAX Manager's "no reverb"
  environment (26, decay 0.1 s, reflections/reverb -10000 mB); buildings
  in the .eal get rooms.
- `mss_roomtype` / `mss_wetlevel` drive Miles 6.5's digital master
  reverb for 2D sounds.

### 2026-10-12: New Game reaches the training level (x86recomp 8afebf1)
- Two switch jump tables Ghidra did not recover (gamex86 0x20034474,
  CoDSP 0x4DBD5A; the second with a `mov` scheduled between cmp and
  ja) were fatal on the way into the level; the lifter now recovers
  MSVC switch tables itself (2 found in CoD, none needed in DS1).
- In-level rendering needed three GL HLE fixes: the sky is drawn with
  w = 0 vertices (directions at infinity) under glDepthRange(1, 1) and
  an infinite far plane (P22 = -1); w was dropped and glDepthRange never
  reached the device; and wglSwapIntervalEXT's swap-chain reset left
  3D drawing with no depth buffer bound (the "streaks" under text and
  foliage, the sky over everything).
- Menus take the mouse like the game: SetCursorPos to the centre, then
  GetCursorPos deltas, applied 1:1 in the UI's 640x480 space (scripted
  runs: TEST_INPUT `goto` in 640x480 coordinates).
- Background (automated) runs ignore host mouse/keyboard by design, so
  clicks on those windows do nothing.

### 2026-10-12: cinematics, menu, autoconfigure (x86recomp 7f40262)
- Cinematic clock (0x407040): with sound, the frame to show is
  (bytes loaded into the streaming sample + AIL_sample_position) in ms
  x fps (0x443CD0), and a cinematic whose frame does not advance for
  4000/fps ms is stopped. AIL_sample_position on a double-buffered
  sample must be the offset into the buffer playing now; it was 0, so
  the clock moved only in 100 ms steps and the movies stopped early.
- Cinematic slots (16 x 0x1D0 at 0x86A4A8, current index 0x56EE44): the
  run loop keeps the highest frame asked for (+0x14C) and decodes until
  the frame count reaches it; nothing resets it when a slot is reused.
  The original recovers only if the first info chunk restarts the clock
  at a different millisecond from the reset (0x406DA0). atvi.roq's
  nextmap starts iw_logo.roq synchronously, both land in the same
  millisecond, and the stale target (312) made the loop decode all of
  iw_logo at once, paced only by the 32 x 100 ms audio ring: it ended
  after 2.3 of 5.8 s and cod_intro never started. Probably a latent bug
  in the original that a fast machine exposes. Fixed by replacing
  0x406DA0 (src/cinematic.cpp: also clears the target). Now atvi plays
  10.3 s, iw_logo 5.6 s, cod_intro 80.5 s (their full lengths).
- The menu backdrop is static (shaders main_back_top/main_back_bottom,
  campaign variants by ui_campaign); there is no slideshow in the menu
  itself, the photo montage is cod_intro.roq. It was invisible because
  the GL HLE swapped the cull modes (2D quads are drawn with
  glCullFace(GL_FRONT)); fixed in x86recomp.
- Safe mode: CoDSP writes a marker file (__CoDSP in the install dir,
  in our overlay) at startup and removes it on a clean quit. If it is
  there, "Run In Safe Mode?" (Yes/No/Cancel) appears and Yes runs
  safemode.cfg (11 kHz sound, picmip 2). The MessageBox HLE used to
  answer Yes silently, so every start after a killed run was in safe
  mode (the low-quality sound). Message boxes are now real dialogs.
- Autoconfigure (0x42F5D0) picks the configure.csv row with the highest
  cpu MHz / sys MB / vid MB not above the machine's: CPU MHz is RDTSC
  timed against QPC (was 1000: x86recomp's TSC ran at 1 GHz, now 3),
  system memory capped at 1 GB, video memory from DirectDraw (now
  GameConfig::video_memory_mb = 256). The best row is 2000/512/256:
  r_mode 6, picmip 0/0, mss_khz 44. A change in the measured CPU speed
  brings up "Set Optimal Settings?" (0x451540) on the next start.
- Still open: two "[mp3float] Header missing" warnings when the menu
  music (MP3 through Miles streams) starts.

### 2026-10-12: renderer up through R_Init (x86recomp OpenGL HLE)
opengl32 loads; QGL takes all 336 entry points; mode 4 (800x600
"fullscreen", a virtual display mode) is set; pixel format 1 and a GL
context on a gfx::Device (dxvk on the RTX 4080). The renderer picks
GL_ARB_multitexture (4 units), texture_env_add/combine, compiled vertex
arrays and WGL_EXT_swap_control; it queries GL_MAX_LIGHTS and uses
fixed-function lighting (glLightModelfv first). Shader scripts load and
R_Init finishes. Alt+Tab is "blocked" with RegisterHotKey(NULL, 0,
MOD_ALT, VK_TAB) (the Win9x path uses SPI_SETSCREENSAVERRUNNING).
No frame is presented before sound init.

Stops in Miles init: the "Miles Fast 2D Positional Audio" provider is
chosen, then AIL_3D_provider_attribute("Maximum supported samples")
(capped to 32 by the game) and an "EAX3 room LF" probe;
AIL_set_3D_distance_factor(0.0254, inches to metres). Sounds are kept
as AILSOUNDINFO (format 1 PCM or 0x11 IMA ADPCM, data, len, rate, bits,
channels, samples, block size) and played from memory: 2D through
set_sample_type/set_sample_address/set_sample_adpcm_block_size, 3D
through set_3D_sample_info, with Miles 6.5 float volume/pan, reverb
levels and ms positions. Sounds above the engine's rate/bits/channel
limits are converted with AIL_process_digital_audio (0x445A30). A
streaming voice (0x443D10) feeds a sample through
AIL_sample_buffer_ready/AIL_load_sample_buffer.

### 2026-10-12: Win32 bring-up up to R_Init (x86recomp after e24e849)
The engine now starts through to the renderer:
- Console window (0x4533C0 is its WndProc, Q3 ConWndProc-like: two
  solid brushes and a timer on WM_CREATE, WM_CTLCOLOR colouring): needs
  the system control classes; x86recomp now provides static/edit/button
  with host window procedures, and a read-only multi-line edit echoes
  what the game appends to stdout. That is the engine's console log
  (Com_Printf), visible in the terminal from here on.
- Timers (SetTimer/KillTimer, WM_TIMER synthesised like Windows), MulDiv,
  SetErrorMode, GetTimeZoneInformation (host offset, no DST rules),
  GlobalMemoryStatusEx, InitializeCriticalSectionAndSpinCount, the
  winmm joystick/MIDI-in calls (no devices), timeBegin/EndPeriod,
  SetEnvironmentVariable, GetSystemDirectoryA, SetEndOfFile, CopyFileA,
  GlobalSize, LoadImageA (splash bitmap: existence only, nothing drawn),
  GetSystemMetrics 16/17.
- The game writes `__CoDSP` (hidden, the "did not quit properly" marker)
  into its install directory. x86recomp now treats the install as
  read-only with a per-user overlay ($XDG_DATA_HOME/cod1/install/),
  so the Steam folder is never modified. (Earlier runs had left the
  marker in the Steam folder; removed by hand.)
- CPU detection (0x452FF0) toggles EFLAGS bit 21 to test for CPUID; the
  flags model now keeps AC and ID, and 16-bit pusha/popa (66 60/66 61)
  around its cpuid are lifted. Result: "Intel Pentium III", 1.00 GHz,
  SSE supported.
- With no clean-exit marker the game asks "Run In Safe Mode?"; the
  MessageBox HLE takes the default (Yes), so safemode.cfg runs and the
  renderer is forced to 640x480.
- "fs_basepath is write protected" is the Q3-style cvar message for a
  config setting a CVAR_INIT cvar; it is printed on Windows too.

Log excerpt: FS_Startup lists all pk3s (30160 files), execs default.cfg,
safemode.cfg, autoconfigure ("800 cpu MHz 512 sys MB 64 vid MB"),
Client Initialization, then `R_Init` -> "Initializing OpenGL subsystem"
-> LoadLibrary("C:\WINDOWS\system32\opengl32.dll") fails -> "Could
not load OpenGL" fatal error. Next: the opengl32/wgl HLE on gfx::Device.

### 2026-10-12: four modules lifted (x86recomp 56c26cf)
`cod1 --game-dir ... --exe unpacked/CoDSP.exe`:
1. CRT start, WinMain; the "did not quit properly, run in safe mode?"
   MessageBox (the HLE returns its default button), as no clean-exit
   marker exists yet.
2. 0x44FC60 is a video-memory probe: DirectDrawCreateEx(guid,
   IID_IDirectDraw7) then IDirectDraw7::GetAvailableVidMem(DDSCAPS_
   VIDEOMEMORY) and Release; the caller enumerates adapters with
   DirectDrawEnumerateExA. x86recomp's ddraw HLE (recomp_d3d7) already
   does all of it without creating a device, so cod1 links it
   (with dxvk-native, needed for gfx::Device anyway).
3. Stops: `unimplemented import gdi32.dll!CreateSolidBrush` from
   0x453583, in 0x4533C0. That function references the `viewlog` cvar,
   like Q3's Win32 console window setup (Sys_CreateConsole; a likeness
   only, not confirmed). The function before it (0x4531D0) creates the
   "CoD Splash Screen" window with a Static child showing cod.bmp.

Imports with no handler yet, across all four modules (COD1_TRACE=1
lists them at startup): gdi32 ChoosePixelFormat, CreateSolidBrush,
DescribePixelFormat, Get/SetDeviceGammaRamp, SetPixelFormat,
SwapBuffers; kernel32 CompareStringA/W, CopyFileA, FatalAppExitA,
GetSystemDirectoryA, GetTimeZoneInformation, GlobalSize,
Module32First/Next, MulDiv, SetConsoleCtrlHandler, SetEndOfFile,
SetEnvironmentVariableA/W, SetErrorMode, SetThreadExecutionState;
user32 ChangeDisplaySettingsA, ClipCursor, CloseWindow, LoadImageA,
Register/UnregisterHotKey, ReleaseCapture, SetCapture, SetTimer;
shell32 ShellExecuteA; winmm joyGetDevCapsA, joyGetNumDevs,
joyGetPosEx, midiIn* (6), timeBegin/EndPeriod; mss32: 28 AIL functions
beyond Dungeon Siege's set (3D provider/sample attributes,
sample/stream ms positions and playback rate, volume_pan, reverb
levels, ADPCM, sample buffers for the streaming path). Dynamic:
GetProcAddress(kernel32, InitializeCriticalSectionAndSpinCount /
GlobalMemoryStatusEx) returns NULL with a warning.

Game DLL loading, checked with a throwaway harness (not committed) that
called the kernel32 thunks before the entry point: for each DLL,
LoadLibraryA (full path, bare name, and "UIX86" without extension) runs
_DllMainCRTStartup (the static CRT's init: heap, locale tables,
GetModuleFileNameA) and returns the image base; GetProcAddress gives
dllEntry/vmMain by name and dllEntry as ordinal 1; GetModuleHandleA
and GetModuleFileNameA work while loaded; FreeLibrary runs DllMain
detach, after which GetModuleHandleA fails; a second LoadLibraryA maps
the DLL afresh and attaches again. The same with gamex86 lifted and
loaded at --base 50000000 (relocated): exports at 0x5004xxxx, DllMain
fine. A config/lift base mismatch stops at startup ("entry point
20075E1B is not lifted").

## The install (Steam app 2620, v1.5b)
| File | What | Notes |
|---|---|---|
| CoDSP.exe | SP engine (client, server, renderer, sound, FS) | Steam-wrapped, MSVC 7.0, built 2004-03-02 |
| CoDMP.exe | MP engine | Steam-wrapped, built 2004-11-17; imports WSOCK32 |
| gamex86.dll | SP server game (base 0x20000000) | not packed; exports dllEntry, vmMain |
| cgamex86.dll | SP client game (base 0x30000000) | not packed; exports dllEntry, vmMain |
| uix86.dll | SP UI (base 0x40000000) | not packed; exports dllEntry, vmMain |
| Main/{game,cgame,ui}_mp_x86.dll | MP equivalents | same bases as SP |
| mss32.dll + miles/ | Miles Sound System 6.5e | imported statically by both EXEs |
| EaxMan.dll | EAX manager | loaded dynamically (EaxManagerCreate) |
| validatekey.dll | CD-key check | not referenced by CoDSP.exe |
| pb/ | PunkBuster | MP only; out of scope |
| TestApp2620s.exe | Steam test copy of CoDSP.exe | unpacks to the same image apart from 2 bytes (header checksum, one .rsrc byte) |
| TestApp2620m.exe | Steam test copy of CoDMP.exe | byte-identical to CoDMP.exe |
| Main/*.pk3 | assets (zip) | ~1.2 GB, read by the engine itself |

The game DLLs are native x86 (Q3's QVMs are not used): the EXE loads
`%sx86.dll` with LoadLibrary and calls dllEntry(syscall) and vmMain, as
in RtCW SP. Their preferred bases don't overlap the EXE or each other.

## CoDSP.exe Steam wrapper
Valve's 2004 Steam DRM is PECompact 2 plus a steam.dll check:
- Entry (0x401000) is `mov eax, stub; jmp eax`; the stub lives at the end
  of .rsrc. It VirtualAllocs a buffer, aPLib-unpacks the loader into it
  and calls it. The loader's own context sits just before its entry
  (dword at entry-4 gives the distance back).
- The loader runs these record types (0x1c-byte records at ctx+0x50,
  count at ctx+0x3c): 0x10 = LZMA-packed blob (one block: the original
  sections back to back), 0x02 = move a section's data from the blob to
  its RVA, 0x08 = undo an E8/E9 call filter over the code (marker byte,
  then a big-endian 24-bit absolute target; 32-bit additive checksum in
  the record), 0x200 = write a fixed word at a list of RVAs. Then it
  resolves the original import directory (ctx+0x34), calls
  steam.dll SteamStartup/SteamIsAppSubscribed/SteamCleanup, and
  jumps to the OEP (ctx+0x0c).
- LZMA props byte is 0x62 (lc=8, lp=0, pb=2). liblzma (Python's lzma)
  rejects lc+lp > 4, so tools/lzma1.py decodes it.
- The original section table is gone; tools/unpack_steam.py rebuilds it
  from the records (filter range = .text; each moved block starts a
  section; the one with the import directory is .rdata). Section names
  other than .text/.rdata/.rsrc are guesses.

Results:
| | OEP | imports | .text | .rdata | .data | .data2 |
|---|---|---|---|---|---|---|
| CoDSP | 0x51DD4C | RVA 0x1608D4, 272 functions | 0x401000 | 0x533000 | 0x563000 (bss to 0x1831000) | 0x1831000 (starts with an 8-bit 512x384 DIB) |
| CoDMP | 0x560E1D | RVA 0x1AD120 | 0x401000 | 0x576000 | 0x5AF000 | 0x1C97000 |

Checks: OEP is MSVC 7's WinMainCRTStartup; the filter checksum matches;
89 % of E8 rel32 targets from a raw byte scan land in .text.

## Imports and dynamic loads (CoDSP.exe)
- KERNEL32, USER32 (window, cursor, clipboard, ChangeDisplaySettings),
  GDI32 (pixel format, SwapBuffers, gamma ramp), WINMM (timeGetTime,
  joystick, MIDI in), ADVAPI32 (GetUserNameA), SHELL32 (ShellExecuteA),
  DDRAW (DirectDrawEnumerateExA, DirectDrawCreateEx only; likely
  monitor/vid-mem detection), mss32 (82 AIL functions: 2D/3D samples,
  streams, providers, reverb).
- Dynamic: opengl32.dll (QGL table via GetProcAddress, wgl*; extensions
  via wglGetProcAddress include ARB_multitexture, ARB_vertex_program,
  ARB_vertex_buffer_object, compressed textures), EAXMan.dll,
  `%sx86.dll` game modules.
- No DirectInput: mouse via Win32 cursor calls/messages.
- Cinematics are RoQ (atvi.RoQ, iw_logo.roq, cod_intro.roq, ...).

## Cvars of interest
- EXE: `r_customwidth` (custom resolutions exist), `com_maxfps`.
- gamex86/cgamex86: `pmove_fixed`, `pmove_msec` (fixed-step player
  physics, as in later Q3 point releases), cgamex86: `cg_fov`.

## Reference material
- ../tools_ext/ioq3 (ioquake3, GPLv2, 83a7762) and ../tools_ext/iortcw
  (RtCW SP/MP, GPLv3, 438e7d4): closest public relatives. The SP
  architecture (native game/cgame/ui DLLs, dllEntry/vmMain) matches RtCW
  SP.
- Not in this install but worth knowing: Activision shipped a Linux
  dedicated server for CoD 1.5 and Aspyr a Mac port. Either could carry
  symbols for shared code (qcommon, server) if the user has them.
