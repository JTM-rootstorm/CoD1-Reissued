# CoD1-Reissued

A native port of **Call of Duty** (PC, 2003, single player) to 64-bit
Linux and macOS on Apple Silicon (Metal). Goals beyond running
natively: widescreen at any resolution with a UI and HUD that scale
properly, Hor+ field of view, and high refresh rates (144 Hz and up)
without the Quake 3 engine family's frame-rate-dependent physics.

There is no emulator, interpreter or Wine involved, and no game code or
data in this repository: you need your own copy of the game, and the
build reads it from where you installed it.

## Status

In game on Linux. The game's five modules are lifted and linked into one
native program. It boots through the intro cinematics to the main menu
with its music, starts a new game through the briefing, and the training
level plays through to completion (played by hand), with EAX 3 through
the game's own EAX Manager. Widescreen at any resolution, Hor+ FOV, a HUD
that keeps its shapes on wide screens and exact frame caps are in, and
the hottest engine code runs as native C++.

On macOS (Apple Silicon, Metal) it builds, passes its tests and runs:
scripted runs reach the training level at 60 fps (also fullscreen on a 5K
display), and the training level has been played by hand on the Mac
through the obstacle course, not yet to its end.

[FINDINGS.md](FINDINGS.md) tracks progress and
[ARCHITECTURE.md](ARCHITECTURE.md) the plan.

Developed against the Steam release (v1.5b, app 2620). Other releases
need their own lifter manifests.

## How it works

The port is built on [x86recomp](https://github.com/JTM-rootstorm/x86recomp),
a static-recompilation library shared with
[DS1-Rebuilt](https://github.com/JTM-rootstorm/DS1-Rebuilt) (Dungeon
Siege):

- The game's 32-bit x86 code (`CoDSP.exe` plus `gamex86.dll`,
  `cgamex86.dll`, `uix86.dll` and `EaxMan.dll`) is translated ahead of
  time into C++, with exact integer, x87 and SSE/SSE2 semantics, and
  linked with a runtime that reimplements the Windows and Miles Sound
  System APIs the game uses, OpenGL 1.x included.
- That recompiled code is scaffolding. The aim is native source: lifted
  functions are replaced by hand-written C++ one subsystem at a time
  (widescreen, UI scaling and frame pacing first), each checked against
  the exact lifted version, until nothing lifted is left.
- Graphics go through x86recomp's backend-neutral fixed-function device
  on SDL_GPU: Vulkan on Linux, Metal on macOS. Host OpenGL is never
  used.

This repository holds what is specific to Call of Duty: the Steam DRM
unpacker, the game configuration, lifter manifests, (later) the native
replacements, and the docs.

## What you need

- Call of Duty, the Steam release (v1.5b). Assets stay where Steam put
  them.
- Linux x86-64 with a Vulkan driver.
- Clang and LLVM (22 used; the static `libLLVMDemangle.a` and headers),
  CMake 3.25 or newer, Ninja, Python 3, SDL3, ffmpeg and zlib
  development packages (zlib comes with the macOS SDK), and glslang
  (`glslangValidator`, for the shaders).
- Ghidra (12.1 used), headless, to find the game's functions.
- A checkout of [x86recomp](https://github.com/JTM-rootstorm/x86recomp).

## Building

Paths below assume the game is in `$GAME` (e.g.
`~/.local/share/Steam/steamapps/common/Call of Duty`) and this repository
in `$REPO`, with x86recomp checked out at `$REPO/../../x86recomp` (pass
`-DX86RECOMP_DIR=...` to cmake otherwise).

1. **x86recomp**:

   ```sh
   git clone https://github.com/JTM-rootstorm/x86recomp.git "$REPO/../../x86recomp"
   X86RECOMP="$REPO/../../x86recomp"
   ```

2. **dxvk-native** (optional and off by default, needs Meson; DXVK
   v3.1.1, Direct3D 9 only, SDL3 window system). It adds the d3d9 backend
   (`COD1_GFX=d3d9`), the reference image when checking the SDL_GPU one;
   configure with `-DDXVK_NATIVE_DIR=<install prefix>` (e.g. the Dungeon
   Siege project's `tools_ext/dxvk-native`) to build it:

   ```sh
   git clone --branch v3.1.1 https://github.com/doitsujin/dxvk.git
   cd dxvk
   meson setup build-native -Dbuildtype=release -Dprefix="$PWD/../dxvk-native" \
     -Denable_dxgi=false -Denable_d3d8=false -Denable_d3d10=false \
     -Denable_d3d11=false -Dnative_sdl3=enabled -Dnative_sdl2=disabled \
     -Dnative_glfw=disabled
   ninja -C build-native install
   ```

3. **Unpack the EXE.** The Steam `CoDSP.exe` is wrapped by Steam's 2004
   DRM (PECompact 2). This replays the wrapper's loader in Python without
   running anything and writes a normal PE (no extra packages needed):

   ```sh
   cd "$REPO"
   python3 -I tools/unpack_steam.py "$GAME/CoDSP.exe" -o unpacked/CoDSP.exe
   ```

4. **Configure and build the lifter**:

   ```sh
   cmake --preset release -DRECOMP_LLVM_ROOT=/usr/lib/llvm/22
   cmake --build --preset release --target recomp_lift
   ```

5. **Symbols** for each module (written to `symbols/`, ignored by git).
   Ghidra's plain auto-analysis is all that is needed:

   ```sh
   python3 -I "$X86RECOMP/tools/pe_symbols.py" unpacked/CoDSP.exe --out symbols/CoDSP
   for m in gamex86 cgamex86 uix86 EaxMan; do
     python3 -I "$X86RECOMP/tools/pe_symbols.py" "$GAME/$m.dll" --out symbols/$m
   done
   mkdir -p /tmp/cod1_ghidra
   bash /usr/share/ghidra/support/analyzeHeadless /tmp/cod1_ghidra cod1 \
     -import unpacked/CoDSP.exe "$GAME/gamex86.dll" "$GAME/cgamex86.dll" "$GAME/uix86.dll" \
       "$GAME/EaxMan.dll"
   for m in CoDSP.exe gamex86.dll cgamex86.dll uix86.dll EaxMan.dll; do
     bash /usr/share/ghidra/support/analyzeHeadless /tmp/cod1_ghidra cod1 \
       -process "$m" -noanalysis -readOnly -scriptPath "$X86RECOMP/tools/ghidra" \
       -postScript ExportFunctions.java "symbols/${m%.*}/functions.tsv" \
       -postScript ExportJumpTables.java "symbols/${m%.*}/jumptables.tsv"
   done
   ```

6. **Lift** all five modules (the C++ goes to `generated/`, also ignored)
   and build:

   ```sh
   out/build/release/x86recomp/tools/lifter/recomp_lift \
     --exe unpacked/CoDSP.exe --symbols symbols/CoDSP --manifest manifest/CoDSP.manifest \
     --module "$GAME/gamex86.dll" --symbols symbols/gamex86 --manifest manifest/gamex86.manifest \
     --module "$GAME/cgamex86.dll" --symbols symbols/cgamex86 --manifest manifest/cgamex86.manifest \
     --module "$GAME/uix86.dll" --symbols symbols/uix86 --manifest manifest/uix86.manifest \
     --module "$GAME/EaxMan.dll" --symbols symbols/EaxMan --manifest manifest/EaxMan.manifest \
     --out generated
   cmake --preset release
   cmake --build --preset release
   ```

7. **Run**:

   ```sh
   out/build/release/cod1 --game-dir "$GAME" --exe unpacked/CoDSP.exe
   ```

   Settings and saves will live under `$XDG_DATA_HOME/cod1` (by default
   `~/.local/share/cod1`). `COD1_TRACE=1` logs every call into the
   reimplemented Windows API.

   Display and frame rate (set in the console or with `+seta` after
   `--`):
   - `r_mode -1` with `r_customwidth 0` and `r_customheight 0` is the
     desktop's resolution; any other size is `r_mode -1` with the numbers.
     Wide modes (1280x720 to 3840x2160, up to the desktop) work
     fullscreen too.
   - Fullscreen is borderless at the desktop's resolution, with black
     bars when the mode's shape differs (a 4:3 mode on a 16:9 screen).
     `COD1_FULLSCREEN=exclusive` switches the display mode instead,
     `COD1_FULLSCREEN=window` keeps a window the mode's size.
   - The field of view is Hor+: `cg_fov` is the horizontal FOV at 4:3, and
     wider screens see more at the sides, not less at the top and bottom.
     Mouse sensitivity does not change with the aspect.
   - The HUD keeps its shapes on wide screens: compass and stance at the
     left edge, weapon, ammo and health at the right, the rest centred.
     Menus, briefings and cinematics stay 4:3 with black bars.
   - `com_maxfps` is exact (144 gives 144, not 166) and the cap sleeps
     instead of keeping a core busy. With a variable-refresh display use
     `r_swapInterval 0` and a `com_maxfps` a little under its refresh rate.

**macOS** (Apple Silicon): SDL_GPU runs on Metal. With Homebrew (`brew
install cmake ninja sdl3 ffmpeg llvm@22 glslang spirv-cross ghidra
openjdk@21`), the steps are the same with these differences: CMake finds
Homebrew's LLVM; pass `-DX86RECOMP_DIR=<path>` unless x86recomp is at
`../../x86recomp`; `pe_symbols.py` needs `/opt/homebrew/opt/llvm@22/bin`
on `PATH` (for `llvm-undname`); Ghidra's headless script is
`$(brew --prefix ghidra)/libexec/support/analyzeHeadless`, run with
`JAVA_HOME=$(brew --prefix openjdk@21)/libexec/openjdk.jdk/Contents/Home`
(without a terminal it cannot ask for the JDK). Settings and saves live
under `~/Library/Application Support/cod1`.

## Tests

Configure with `-DCOD1_GAME_EXE="$REPO/unpacked/CoDSP.exe"` and
`-DCOD1_GAME_DIR="$GAME"`, then `ctest --test-dir out/build/release`.
Besides x86recomp's own tests, this runs every x87, integer and SSE
instruction form found in the EXE both lifted and natively on your CPU,
from random states, and requires bit-identical results; and each native
replacement against the original it replaces, lifted (the pk3 reads and
savegame compression over the install's pk3 files, the JPEG inverse DCT
over their images, which needs the libjpeg development package).

## Legal

Call of Duty is © Activision Publishing / Infinity Ward. This project is
not affiliated with or endorsed by them. It contains no game code, data
or assets: the unpacked EXE, symbol lists and lifted code are generated
locally from your own copy and are never committed. GPL-licensed Quake 3
family sources (ioquake3, iortcw) are used only as references for
understanding the engine; no code from them is in this repository.

The code here is under the [MIT License](LICENSE). Dependencies keep
their own licences; see x86recomp's README.
