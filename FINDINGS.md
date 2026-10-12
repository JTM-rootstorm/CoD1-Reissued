# Findings

## Status (2026-10-12)
M0 (recon) done; M1 under way. Decided: static recompilation on the
shared x86recomp library (factored out of DS1 the same day), graphics
through gfx::Device so macOS/Metal stays open (ARCHITECTURE.md).

- The Steam wrapper on CoDSP.exe and CoDMP.exe is undone statically by
  tools/unpack_steam.py.
- Ghidra (cod1_recomp.gpr, plain auto-analysis) has the unpacked
  CoDSP.exe and the three SP DLLs; symbols exported per module.
- Trial lifts with the unchanged DS1 lifter (below) cover all four
  modules apart from CPUID-gated SSE/SSE2 code.
- The EXE alone lifts, builds and runs: the CRT starts, WinMain runs,
  and it stops at the first missing import, ddraw!DirectDrawCreateEx
  (called from 0x44FC60, early in startup; probably video-memory
  detection, to be checked in Ghidra).

Next: x86recomp multi-module lifting + LoadLibrary of lifted DLLs; the
small ddraw surface CoD uses (DirectDrawEnumerateExA, DirectDrawCreateEx);
then the opengl32/wgl HLE on gfx::Device.

## Lift coverage (x86recomp 5640e2f, no manifest tuning yet)
| Module | Functions (Ghidra + discovered) | Instructions | Unimplemented sites |
|---|---|---|---|
| CoDSP.exe | 3290 + 1999 | 389 369 | 549 |
| gamex86.dll | 1432 + 564 | 146 103 | 333 |
| cgamex86.dll | 965 + 249 | 83 316 | 356 |
| uix86.dll | 541 + 228 | 41 470 | 356 |

- Every module has the same ~330 SSE2 sites (movlpd, pextrw, mulsd,
  addsd, ...): the statically linked MSVC 7 CRT's SSE2 libm, taken only
  when the CPU reports SSE2. Our Cpuid reports a Pentium III without
  SSE, so they should never run (to verify when the CRT's
  __sse2_available check is found).
- CoDSP.exe also has packed-single SSE (addps, mulps, shufps, movaps,
  prefetchnta), presumably engine SIMD paths chosen by CPU detection,
  and a few dozen junk decodes (aaa, daa, in/out, iretd) from weak
  entries that are really data.
- Rich header: everything built with VS.NET 2002 (build 9466).
- SEH: the CRT's __except_handler3 matches DS1's byte for byte; its
  continuation call is a manifest `resume` site in every module
  (CoDSP 0x528277, gamex86 0x2008126B, cgamex86 0x3004AB5B,
  uix86 0x40023A8B).

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
