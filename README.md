# CoD1 native

Work in progress: a native 64-bit Linux port of **Call of Duty** (PC,
2003), aiming at widescreen with proper UI scaling and high refresh
rates. Nothing runs yet; see [FINDINGS.md](FINDINGS.md) for status and
[ARCHITECTURE.md](ARCHITECTURE.md) for the plan.

This repository contains no game code or data. You need your own copy of
the game (the Steam release, v1.5b, is used for development); tools here
read it from where it is installed.

## Unpacking the Steam EXEs

The Steam copies of `CoDSP.exe` and `CoDMP.exe` are wrapped by Steam's
2004 DRM (PECompact 2). `tools/unpack_steam.py` undoes it without running
anything (Python 3, no extra packages):

```sh
python3 -I tools/unpack_steam.py "$GAME/CoDSP.exe" -o unpacked/CoDSP.exe
```

## Legal

Call of Duty is © Activision / Infinity Ward. This project is not
affiliated with or endorsed by them. It contains no game code, data or
assets.

The code here is under the [MIT License](LICENSE).
