# Findings

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
  `replace`, src/): so far one cinematic fix (src/cinematic.cpp).

Training plays through to completion (2026-10-12, played by hand on EAX
3), with occasional frame-rate drops not yet investigated.

Next: the frame-rate drops in training; then widescreen/FOV, UI scaling
and high frame rates.

EAX works: with `mss_3d_provider "Creative Labs EAX 3 (TM)"` (a latched
cvar: takes effect on the next start, or pick it in the sound options)
the game runs Creative's EAX Manager (EaxMan.dll, lifted as a fifth
module) on x86recomp's EAX providers. See "Run log".

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
