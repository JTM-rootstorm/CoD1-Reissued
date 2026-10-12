# Architecture (draft)

## Goals
- Call of Duty (2003) single player running natively on Linux x86-64:
  no Wine, no emulator in the product. Multiplayer later, if at all.
- Widescreen at any resolution: Hor+ field of view and a UI/HUD that
  scales without stretching (4:3 menus pillarboxed, HUD anchored to the
  screen edges).
- Arbitrary frame rate (144 Hz and up) without the Quake 3 family's
  frame-rate-dependent behaviour (physics, timers, animation steps).
- Users bring their own copy; the repo holds no game code or data.

## Open decision: how to get native code
The engine is closed source and Quake 3 derived; game logic sits in three
native DLLs. Two ways in:

**A. Static recompilation (recommended).** Lift CoDSP.exe and the
game/cgame/ui DLLs from x86-32 to C++ ahead of time and link them
against a runtime that reimplements the Windows APIs they use. This is
what DS1 does, and its lifter, x87 model, guest memory, SEH, threads and
Win32/Miles HLE already run a 2002 MSVC game playably. The work for CoD1
is mostly new HLE (opengl32, a few more Win32 calls, Sys_LoadDll for the
three modules) plus multi-module lifting. Enhancements are written as
hand-written C++ replacements for specific lifted functions, found in
Ghidra with ioq3/iortcw as a map.

**B. Source port.** Rebuild the engine from ioq3/iortcw and reverse
engineer every CoD change (renderer, XModel/XAnim formats, BSP version,
scripting, AI, menus). Cleaner code in the end but years of work, and
the GPL sources would make the project GPL.

With A, sharing code with DS1 needs a choice: copy the lifter/runtime
into this repo and let them diverge, or factor them into a shared
library both projects use (more work up front, fixes flow both ways).

## Shape under A (to be confirmed)
- Modules: CoDSP.exe at 0x400000 plus gamex86 (0x20000000), cgamex86
  (0x30000000) and uix86 (0x40000000) lifted separately and loaded at
  their preferred bases, so no relocation at runtime. LoadLibrary of
  `%sx86.dll` resolves to the lifted module; dllEntry/vmMain and the
  syscall pointer the EXE passes stay guest addresses.
- Graphics: the engine is OpenGL 1.x plus ARB extensions through a
  GetProcAddress table, so opengl32 HLE forwards to a host GL
  compatibility context created with SDL3 (wgl*/pixel format/SwapBuffers
  map onto SDL). Client-array pointers are guest addresses and translate
  with the arena base. macOS caps legacy GL at 2.1, which CoD1 fits.
- Audio: Miles 6.5e; reuse the DS1 Miles HLE (2D/3D samples, streams)
  and extend for the calls CoD1 adds (reverb levels, room types).
- Input/window: SDL3 behind the USER32 HLE (no DirectInput in CoD1).
- Files: the engine reads pk3 (zip) itself; only Win32 file APIs are
  needed. Config and saves go under $XDG_DATA_HOME.

## Enhancements (after a faithful baseline)
- Resolution: `r_customwidth`/`r_customheight` exist in the EXE; check
  r_mode -1 and drive them from the host window size.
- FOV: cg_fov is horizontal for 4:3. Keep the vertical FOV of the 4:3
  case and derive horizontal from the real aspect (Hor+), in the cgame
  refdef setup; the view-model FOV likewise.
- UI: menus and HUD draw in a 640x480 virtual space scaled to the screen.
  Plan: scale by the height only and centre 4:3 content; HUD items that
  belong to an edge get anchored by element (needs RE of the cgame HUD
  draw calls and menu alignment fields).
- Frame rate: `com_maxfps` caps frames; game time is integer
  milliseconds, so physics varies with frame time unless
  `pmove_fixed 1` is used. Plan: force fixed-step pmove, interpolate the
  predicted player between steps for display, and audit integer-ms
  timers in cgame (view bob, weapon animations, effects) at 144/240 Hz.

## Milestones (provisional)
- M0 recon: unpack, Ghidra, inventory imports and modules. (in progress)
- M1 lifter covers CoDSP.exe and the three DLLs; symbols exported.
- M2 boots to the console log: Com_Init, file system on the pk3s, cvars.
- M3 main menu renders through the opengl32 HLE.
- M4 single player playable end to end, with sound and RoQ cinematics.
- M5 widescreen: resolution, Hor+ FOV, UI/HUD scaling.
- M6 high refresh rate.
