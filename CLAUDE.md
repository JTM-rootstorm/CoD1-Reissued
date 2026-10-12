# Call of Duty (PC, 2003) → native Linux port

Rules only. Status, findings and next steps go in FINDINGS.md; design in
ARCHITECTURE.md.

## Layout
- Project: ~/Documents/Programming/Recomp/CoD/CoD1 (CoD/ may get sibling
  projects for later games; shared external material goes in CoD/tools_ext)
- Game (Steam, v1.5b; NEVER commit or copy into the repo):
  $HOME/.local/share/Steam/steamapps/common/Call of Duty/
  (CoDSP.exe, CoDMP.exe, gamex86.dll, cgamex86.dll, uix86.dll, Main/*.pk3)
- Unpacked EXEs: unpacked/ (gitignored, regenerate, see below)
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

## Rules
- Paths with spaces ("Call of Duty"): always quote them in shell commands,
  scripts and CMake.
- No game files, unpacked binaries, memory dumps, extracted pk3 contents,
  captures or symbol dumps in git. Lifted/generated code is gitignored too.
- ioq3/iortcw are GPLv2/GPLv3 and this repo is MIT: use them only to
  understand behaviour and names. Never copy or paraphrase their code into
  this repo.
- Ghidra names need evidence (strings, call-graph position matching
  ioq3/iortcw, data layout); record it in a plate comment. A function
  that merely looks like a Quake 3 one stays unnamed until confirmed.
- Python scripts that read game files run with `python3 -I`.
- /usr/share/ghidra/support/analyzeHeadless has no exec bit: run it as
  `bash /usr/share/ghidra/support/analyzeHeadless ...`.
- Commits: GPG-signed (global config), no Claude co-author trailers or
  generated-by markers.
- Record findings in FINDINGS.md, not here.
