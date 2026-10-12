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
- The run gets through the Win32 setup, the file system (all pk3s),
  configs, CPU detection and client init to R_Init, and stops loading
  opengl32.dll (see "Run log"). The engine console log goes to stdout.
- The engine has not reached its own LoadLibrary of a game DLL yet. The
  loader path was exercised with a throwaway harness (see "Run log").

Next: the opengl32/wgl HLE on gfx::Device; the game DLLs load after
the renderer starts (as in RtCW SP, to be confirmed).

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

## Run log
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
