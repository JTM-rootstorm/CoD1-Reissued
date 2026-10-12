# Architecture

## Goals
- Call of Duty (2003) single player running natively on Linux x86-64:
  no Wine, no emulator in the product. Multiplayer later, if at all.
- macOS on Apple Silicon with native Metal rendering: a requirement.
  Nothing may tie the port to Linux or to host OpenGL.
- Widescreen at any resolution: Hor+ field of view and a UI/HUD that
  scales without stretching (4:3 menus pillarboxed, HUD anchored to the
  screen edges).
- Arbitrary frame rate (144 Hz and up) without the Quake 3 family's
  frame-rate-dependent behaviour (physics, timers, animation steps).
- Users bring their own copy; the repo holds no game code or data.

## Recompilation is the scaffold
The end product is a native source port: the lifted code gets CoD running
natively first, then hand-written C++ replaces it one subsystem at a time
until nothing lifted is left, on Linux and macOS alike. The widescreen,
UI and frame-rate work is the first of those replacements. The lifted
code stays bit-exact (integer, x87, SSE) because it is the reference each
replacement is checked against. ioq3/iortcw help find names and
structure, but replacements are written from CoD's own behaviour, never
from their code.

## Decisions (2026-10-12)
- **Static recompilation** of CoDSP.exe and the game/cgame/ui DLLs, not
  a source port on ioq3/iortcw (those stay references only).
- **Shared library**: the lifter, x87 model, runtime and HLE are
  x86recomp (github.com/JTM-rootstorm/x86recomp, a sibling checkout at
  ../../x86recomp), factored out of DS1-Rebuilt. Generic work (multi-
  module lifting, opengl32 HLE, SDL_GPU/Metal backend) goes there; this
  repo holds CoD's GameConfig, manifests, hooks, replacements and docs.
  x86recomp/ARCHITECTURE.md has the generic design.
- **Graphics through gfx::Device**: the engine's OpenGL 1.x + ARB
  extensions are implemented as an opengl32/wgl HLE that drives
  x86recomp's backend-neutral device, never host GL. Linux bring-up uses
  the existing d3d9 (dxvk-native) backend; the planned SDL_GPU backend
  gives Vulkan on Linux and Metal on macOS for DS1 and CoD alike. GL
  extensions we do not want to implement (ARB_vertex_program, VBOs) are
  simply not advertised, so the engine takes its fallback paths.

## Shape
- Modules: CoDSP.exe at 0x400000 plus gamex86 (0x20000000), cgamex86
  (0x30000000) and uix86 (0x40000000), lifted and loaded at their
  preferred bases. LoadLibrary of `%sx86.dll` maps the lifted module;
  dllEntry/vmMain and the syscall pointer the EXE passes stay guest
  addresses. Needs x86recomp's multi-module support (its roadmap).
- GL state machine on gfx::Device: GL matrices -> transforms, texenv /
  multitexture -> texture stage states, client arrays (guest pointers,
  separate strides) repacked into interleaved vertices, depth range and
  clip-space z adjusted. wgl*/pixel format/SwapBuffers map onto SDL3.
- CPU: x86recomp reports a fixed SSE2-class CPU, so CoD runs its SSE
  paths as on any real PC: MSVC 7's SSE2 maths library (pow, exp, log,
  sin/cos... in every module) and the EXE's packed-single loops. The
  scaffold lifts them exactly (runtime/sse.cpp). In the native port the
  CRT maths becomes the host libm and the SIMD loops portable C++ (or
  SIMDe/sse2neon where it matters on Apple Silicon), each checked
  against the lifted version.
- Audio: Miles 6.5e through recomp_mss32, extended for the calls CoD1
  adds (reverb levels, room types, 3D sample offsets).
- Input/window: SDL3 behind the USER32 HLE (no DirectInput in CoD1).
- Files: the engine reads pk3 (zip) itself; only Win32 file APIs are
  needed. Config and saves go under $XDG_DATA_HOME/cod1.

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
- M0 recon: unpack, Ghidra, inventory imports and modules. (done)
- M1 lifter covers CoDSP.exe and the three DLLs; symbols exported; the
  three DLLs load as lifted modules (x86recomp multi-module).
- M2 boots to the console log: Com_Init, file system on the pk3s, cvars.
- M3 main menu renders through the opengl32 HLE.
- M4 single player playable end to end, with sound and RoQ cinematics.
- M5 widescreen: resolution, Hor+ FOV, UI/HUD scaling.
- M6 high refresh rate.
