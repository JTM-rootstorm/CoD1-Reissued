# Call of Duty (PC, 2003) → native Linux port

Rules only. Status, findings and next steps go in FINDINGS.md; design in
ARCHITECTURE.md. The lifter, x87 model, runtime and HLE are the shared
x86recomp library (../../x86recomp, its own git repo with its own
CLAUDE.md, also used by DS1); this repo holds what is specific to CoD.

## Layout
- Project: ~/Documents/Programming/Recomp/CoD/CoD1 (CoD/ may get sibling
  projects for later games; shared external material goes in CoD/tools_ext)
- Game (Steam, v1.5b; NEVER commit or copy into the repo):
  $HOME/.local/share/Steam/steamapps/common/Call of Duty/
  (CoDSP.exe, CoDMP.exe, gamex86.dll, cgamex86.dll, uix86.dll, Main/*.pk3)
- Library: ~/Documents/Programming/Recomp/x86recomp
  (github.com/JTM-rootstorm/x86recomp; added with add_subdirectory,
  X86RECOMP_DIR overrides the path). Library changes are committed
  there, its rules apply to them, and DS1 must keep building and
  passing its tests after them.
- Unpacked EXEs: unpacked/ (gitignored, regenerate, see below)
- Per-module symbols: symbols/<module>/ (gitignored; CoDSP, gamex86,
  cgamex86, uix86). Per-module lifter manifests: manifest/<module>.manifest
- Ghidra project: cod1_recomp.gpr / cod1_recomp.rep (gitignored)
- Reference sources (GPL, read-only): ../tools_ext/ioq3 (ioquake3) and
  ../tools_ext/iortcw (Return to Castle Wolfenstein SP/MP). Do not edit.

## Unpacking
- The Steam CoDSP.exe/CoDMP.exe are wrapped (Steam DRM on PECompact 2).
  Analyse and lift the unpacked copies, never the wrapped ones:
  `python3 -I tools/unpack_steam.py "$HOME/.local/share/Steam/steamapps/common/Call of Duty/CoDSP.exe" -o unpacked/CoDSP.exe`
  (same for CoDMP.exe). The game DLLs are not packed; use them in place.
- The unpacker only replays the wrapper's loader statically. Never run the
  wrapped EXE to dump it.

## Build
- `cmake --preset release && cmake --build --preset release` (debug:
  `--preset debug`). Output in out/build/<preset>/.
- dxvk-native (the d3d9 gfx::Device backend, and the DirectDraw HLE) is
  taken from the Dungeon Siege project's build
  (../../Dungeon Siege/tools_ext/dxvk-native); override with
  -DDXVK_NATIVE_DIR. A build tree configured before that default existed
  needs `cmake --preset release -UDXVK_NATIVE_DIR` once.
- Symbols (after any Ghidra change that matters), per module:
  `python3 -I ../../x86recomp/tools/pe_symbols.py unpacked/CoDSP.exe --out symbols/CoDSP`
  and `bash /usr/share/ghidra/support/analyzeHeadless "$PWD" cod1_recomp -process CoDSP.exe -noanalysis -readOnly -scriptPath ../../x86recomp/tools/ghidra -postScript ExportFunctions.java symbols/CoDSP/functions.tsv -postScript ExportJumpTables.java symbols/CoDSP/jumptables.tsv`
- Lift all four modules in one run (then re-run cmake; the generated
  file list is globbed):
  `G="$HOME/.local/share/Steam/steamapps/common/Call of Duty"; out/build/release/x86recomp/tools/lifter/recomp_lift --exe unpacked/CoDSP.exe --symbols symbols/CoDSP --manifest manifest/CoDSP.manifest --module "$G/gamex86.dll" --symbols symbols/gamex86 --manifest manifest/gamex86.manifest --module "$G/cgamex86.dll" --symbols symbols/cgamex86 --manifest manifest/cgamex86.manifest --module "$G/uix86.dll" --symbols symbols/uix86 --manifest manifest/uix86.manifest --out generated`
  The DLLs are listed in GameConfig::lifted_dlls (src/main.cpp); a DLL
  given --base must get the same base there.
- Run: `out/build/release/cod1 --game-dir "$HOME/.local/share/Steam/steamapps/common/Call of Duty" --exe unpacked/CoDSP.exe`
  (COD1_TRACE=1 logs every HLE call; COD1_BACKGROUND=1 for automated
  runs).

## Product constraints
x86recomp's (its CLAUDE.md): no emulation or Wine in the product, 64-bit
host only, portable to macOS arm64 + Metal (graphics only through
gfx::Device, never host OpenGL), exact x87.

## Rules
- Paths with spaces ("Call of Duty"): always quote them in shell commands,
  scripts and CMake.
- No game files, unpacked binaries, memory dumps, extracted pk3 contents,
  captures or symbol dumps in git. Lifted/generated code is gitignored too.
- ioq3/iortcw are GPLv2/GPLv3 and this repo is MIT: use them only to
  understand behaviour and names. Never copy or paraphrase their code into
  this repo.
- CoD knowledge (paths, registry, engine struct offsets, game patches
  such as widescreen/FOV/frame-rate fixes) goes in src/ here (GameConfig,
  hooks, replacements), never in x86recomp.
- Ghidra names need evidence (strings, call-graph position matching
  ioq3/iortcw, data layout); record it in a plate comment. A function
  that merely looks like a Quake 3 one stays unnamed until confirmed.
- Python scripts that read game files run with `python3 -I`.
- /usr/share/ghidra/support/analyzeHeadless has no exec bit: run it as
  `bash /usr/share/ghidra/support/analyzeHeadless ...`.
- Commits: GPG-signed (global config), no Claude co-author trailers or
  generated-by markers.
- Record findings in FINDINGS.md, not here.
