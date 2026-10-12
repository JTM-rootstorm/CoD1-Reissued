# CoD1-Reissued

A native port of **Call of Duty** (PC, 2003, single player) to 64-bit
Linux, with macOS on Apple Silicon (Metal) to follow. Goals beyond running
natively: widescreen at any resolution with a UI and HUD that scale
properly, Hor+ field of view, and high refresh rates (144 Hz and up)
without the Quake 3 engine family's frame-rate-dependent physics.

There is no emulator, interpreter or Wine involved, and no game code or
data in this repository: you need your own copy of the game, and the
build reads it from where you installed it.

## Status

Early. Nothing is playable yet. The game's code is lifted and starts
natively. Its C runtime and WinMain run, and it gets past video-memory
detection. It stops at the first Windows function not yet reimplemented,
on the way to its renderer. [FINDINGS.md](FINDINGS.md) tracks progress
and [ARCHITECTURE.md](ARCHITECTURE.md) the plan.

Developed against the Steam release (v1.5b, app 2620). Other releases
need their own lifter manifests.

## How it works

The port is built on [x86recomp](https://github.com/JTM-rootstorm/x86recomp),
a static-recompilation library shared with
[DS1-Rebuilt](https://github.com/JTM-rootstorm/DS1-Rebuilt) (Dungeon
Siege):

- The game's 32-bit x86 code (`CoDSP.exe` plus `gamex86.dll`,
  `cgamex86.dll` and `uix86.dll`) is translated ahead of time into C++,
  with exact integer, x87 and SSE/SSE2 semantics, and linked with a
  runtime that reimplements the Windows and Miles Sound System APIs the
  game uses (OpenGL is next).
- That recompiled code is scaffolding. The aim is native source: lifted
  functions are replaced by hand-written C++ one subsystem at a time
  (widescreen, UI scaling and frame pacing first), each checked against
  the exact lifted version, until nothing lifted is left.
- Graphics go through x86recomp's backend-neutral fixed-function device:
  Direct3D 9 via dxvk-native (Vulkan) for bring-up on Linux, SDL_GPU
  (Vulkan, Metal) later. Host OpenGL is never used, which keeps macOS
  open.

This repository holds what is specific to Call of Duty: the Steam DRM
unpacker, the game configuration, lifter manifests, (later) the native
replacements, and the docs.

## What you need

- Call of Duty, the Steam release (v1.5b). Assets stay where Steam put
  them.
- Linux x86-64 with a Vulkan driver.
- Clang and LLVM (22 used; the static `libLLVMDemangle.a` and headers),
  CMake 3.25 or newer, Ninja, Python 3, SDL3 and ffmpeg development
  packages, and Meson (for dxvk-native).
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

2. **dxvk-native** (DXVK v3.1.1, Direct3D 9 only, SDL3 window system):

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
   cmake --preset release -DDXVK_NATIVE_DIR=/path/to/dxvk-native \
     -DRECOMP_LLVM_ROOT=/usr/lib/llvm/22
   cmake --build --preset release --target recomp_lift
   ```

5. **Symbols** for each module (written to `symbols/`, ignored by git).
   Ghidra's plain auto-analysis is all that is needed:

   ```sh
   python3 -I "$X86RECOMP/tools/pe_symbols.py" unpacked/CoDSP.exe --out symbols/CoDSP
   for m in gamex86 cgamex86 uix86; do
     python3 -I "$X86RECOMP/tools/pe_symbols.py" "$GAME/$m.dll" --out symbols/$m
   done
   mkdir -p /tmp/cod1_ghidra
   bash /usr/share/ghidra/support/analyzeHeadless /tmp/cod1_ghidra cod1 \
     -import unpacked/CoDSP.exe "$GAME/gamex86.dll" "$GAME/cgamex86.dll" "$GAME/uix86.dll"
   for m in CoDSP.exe gamex86.dll cgamex86.dll uix86.dll; do
     bash /usr/share/ghidra/support/analyzeHeadless /tmp/cod1_ghidra cod1 \
       -process "$m" -noanalysis -readOnly -scriptPath "$X86RECOMP/tools/ghidra" \
       -postScript ExportFunctions.java "symbols/${m%.*}/functions.tsv" \
       -postScript ExportJumpTables.java "symbols/${m%.*}/jumptables.tsv"
   done
   ```

6. **Lift** all four modules (the C++ goes to `generated/`, also ignored)
   and build:

   ```sh
   out/build/release/x86recomp/tools/lifter/recomp_lift \
     --exe unpacked/CoDSP.exe --symbols symbols/CoDSP --manifest manifest/CoDSP.manifest \
     --module "$GAME/gamex86.dll" --symbols symbols/gamex86 --manifest manifest/gamex86.manifest \
     --module "$GAME/cgamex86.dll" --symbols symbols/cgamex86 --manifest manifest/cgamex86.manifest \
     --module "$GAME/uix86.dll" --symbols symbols/uix86 --manifest manifest/uix86.manifest \
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

## Tests

Configure with `-DCOD1_GAME_EXE="$REPO/unpacked/CoDSP.exe"`, then
`ctest --test-dir out/build/release`. Besides x86recomp's own tests, this
runs every x87, integer and SSE instruction form found in the EXE both
lifted and natively on your CPU, from random states, and requires
bit-identical results.

## Legal

Call of Duty is © Activision Publishing / Infinity Ward. This project is
not affiliated with or endorsed by them. It contains no game code, data
or assets: the unpacked EXE, symbol lists and lifted code are generated
locally from your own copy and are never committed. GPL-licensed Quake 3
family sources (ioquake3, iortcw) are used only as references for
understanding the engine; no code from them is in this repository.

The code here is under the [MIT License](LICENSE). Dependencies keep
their own licences; see x86recomp's README.
