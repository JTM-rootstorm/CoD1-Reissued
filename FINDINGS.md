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

Next: playing through training (weapons, AI, saves/loads); then
widescreen/FOV, UI scaling and high frame rates. EAX in x86recomp first.

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

## Miles 6.5e digital reverb: behaviour
Observed in the game's mss32.dll (Miles 6.5e, image base 0x21100000;
scratch Ghidra project, no names applied) to make x86recomp's
Miles-compatible reverb (runtime/audio/digital_reverb) produce the same
output. Addresses are the evidence; constants are listed with where
they live and what they are for.

API and state
- Room type n (0x21115930) = set reverb (time, predelay, damping) and
  levels (dry 1, wet) from entry n of a 26-entry table at 0x21155838,
  16 bytes per entry: wet, time, damping, predelay (floats). n is the
  EAX environment number and is not range-checked.
- Set reverb (0x2112B0F0) stores the three values and the tail length
  round((time + predelay) * 1000) ms (1000.0f at 0x2114BE08), and
  recomputes the coefficients. Only when the driver wet level and time
  are both above 1e-4 (0x2114BD1C) does it also wake the reverb and move
  the read position to `round(rate * predelay)` samples behind the
  write position (wrapping in the ring).
- Set levels (0x2112AA10) stores dry/wet and wakes the reverb when wet
  and time are above 1e-4. Waking = timeout at now + tail + 100 ms
  (AIL_ms_count) and the active flag set.
- A sample starting, resuming or getting a buffer (0x2112B690/B760/
  B940), or getting new reverb levels while playing (0x2112A8B0), wakes
  it when the sample's wet, the driver's wet and the time are all above
  1e-4.
- Driver open: dry 1, wet 1, time 0 (off); a sample opens with dry 1,
  wet 0. The driver dry level multiplies every sample's dry gains.

Buffers (driver open, 0x2112AA70)
- One zeroed allocation holds a mono ring of 32-bit samples and the
  delay lines. Ring length: the maximum predelay (DIG preference 2,
  default 500 ms at 0x21156618) in samples, rounded up to whole mixer
  fragments.
- Four comb delay lines of round(rate * d) samples, d = 0.0297,
  0.0371, 0.0411, 0.0437 s (floats at 0x21142238..0x21142244), and two
  allpass lines of 0.005 and 0.0017 s (0x21142248, 0x2114224C).

Per mixer fragment (0x2112DF90)
- Before mixing (0x2112BCC0): if active and the ms clock has passed the
  timeout, it goes inactive (ring and delay lines keep their contents
  until it wakes again); otherwise the ring slot at the write position
  is cleared.
- While active, every sample whose wet scale round(driver wet x sample
  wet x sample volume x master x 2048) (max 2048) is non-zero is mixed
  mono into that slot (in 0x2112BD40) and pushes the timeout to
  now + tail + 100 ms if it is closer than that.
- After mixing, while active (0x2112B040): one fragment of ring samples
  starting at the read position runs through the reverb and is added
  to the stereo 32-bit build buffer (16-bit samples x gains out of
  2048); both positions advance by one fragment and wrap.

Coefficients (0x2112ACE0), each stored as a float
- inv = float(1 / time). Comb gain i = pow(0.001, inv * d_i), 0.001
  being the double at 0x2114BD38 (the float 0.001 widened): -60 dB
  after `time` seconds.
- Allpass gains: pow(0.001, 0.005 / 0.0968535) and
  pow(0.001, 0.0017 / 0.032924) (decay times at 0x21142250/54).
- Damping low-pass: 2nd-order Butterworth by the bilinear transform,
  c = 1 / tan(damping * pi/8) (pi/8 float at 0x2114BD34), so the cutoff
  is damping/8 of the rate: a0 = 1 / ((sqrt2 + c) c + 1),
  b1 = 2 (1 - c^2) a0, b2 = ((1 - sqrt2 c) + c^2) a0.
  Damping 0 (rooms 1 padded cell, 4 living room) gives c = inf and NaN
  coefficients: that reverb adds nothing (a NaN converted to an integer
  is the indefinite value, low 32 bits 0) and its state stays NaN until
  the driver reopens.

Per sample (0x2112ADE0), x the ring sample
- Low-pass: y = ((2 p1 + p2) + a0 x) - b1 y1 - b2 y2, where p1, p2 are
  the last two values of a0 x stored as floats, and y1, y2 the last two
  of float(y + 1e-10) (1e-10 at 0x2114BD30).
- Combs: s = float(((c1 + c0) + c3) + c2) of the four lines' current
  outputs, then each line's slot becomes float(g_i * c_i + y) (feed
  after the sum).
- Allpass 1 (gain g, line value a): v = g a + s, stored as a float;
  o = float(a - g v).
- Allpass 2 (gain h, line value b): p = float(h b); left: w = 0.25 o + p
  (0.25 at 0x2114BD2C), stored in the line; L = b - h w. Right uses
  0.15 (0x2114BD28) for w and is not stored: R = b - h (0.15 o + p).
- L and R are rounded to nearest integers (64-bit, low 32 bits kept)
  and added to the build buffer; mono drivers get L only.
- Arithmetic: x87, control word never set by Miles, so the Windows
  default 53-bit precision on its mixer thread: every operation above
  rounds to double, every "float(...)" to float. Rounding to single
  precision instead changes 112 of the 120 test scenarios.

## Miles 6.5e sample levels and Fast 2D: behaviour
Observed in the game's mss32.dll (6.5e, base 0x21100000) and
miles/msssoft.m3d (base 0x22400000, the "Miles Fast 2D Positional
Audio" provider) in a scratch Ghidra project, no names applied, so
that x86recomp's mixer gives 2D and Fast 2D samples the same channel
gains. Miles 6.1c (Dungeon Siege: Mss32 balance table, Mssfast.m3d's
volume/pan) works differently; see x86recomp's mixer.cpp. Arithmetic
as for the reverb: x87 at 53-bit precision, "float(...)" marks where
a value is stored as a float; pow is the CRT's (x87 or SSE2 path, by
the CRT's SSE2 flag: mss32 0x2115AD20, msssoft 0x22417228), acos the
CRT's x87 one (atan2(sqrt((1 + x)(1 - x)), x) by fpatan; exactly 0 at
x = 1 and fldpi at x = -1; NaN with a domain error for |x| > 1).

Sample fields (mss32's sample block)
- 0x64 volume, 0x68 pan, 0x6C left level, 0x70 right level, all
  floats; 0x78 sample dry level, 0x74 sample wet level.
- AIL_set_sample_volume_pan (0x2112A930, also AIL_init_sample with
  1, 0.5 at 0x2112A700, and AIL_set_stream_volume_pan on the stream's
  sample): volume = float(pow(v, 5/3)) (double 5/3 at 0x2114BD50);
  pan = p as given; left = float(pow(1 - p, e) x volume), right =
  float(pow(p, e) x volume) with e = float 0.3 widened (0x2114BD48).
  No clamping: v < 0 or p outside 0..1 gives NaN levels.
- AIL_set_sample_volume_levels (0x2112A810): left and right stored as
  given; volume and pan are derived for the wet send and read-back:
  if left > 1e-4 (0x2114BD1C): q = pow(right / left, 10/3) (0x2114BD60),
  pan = float(q / (1 + q)), and volume = left if pan < 1e-4, else
  float(pow(pan, -e) x right) (-e at 0x2114BD58). Otherwise pan = 0,
  volume = right.
- AIL_sample_volume_pan (0x211153E0; streams 0x2111AD70) reports
  float(pow(volume, 0.6)) (0x2114BD40) and the stored pan;
  AIL_sample_volume_levels the two levels.

Channel gains (per mixer fragment, 0x2112BDB8)
- d = driver dry (0x108) x sample dry; gain = round(((level x master)
  x d) x 2048.0) (2048 double at 0x2114BDF8), per channel; the 64-bit
  result's low 32 bits are taken and capped at 2048 (0x800) from above
  only (so NaN or a huge level gives 0). master = driver block 0x10
  (1.0 from open; CoD never changes it).
- Digital reverb send: round((((driver wet x sample wet) x volume) x
  master) x 2048.0), same capping: it follows the derived volume, not
  the levels.
- The mixer scales 16-bit samples by these gains (out of 2048); no
  balance table, so a centred 2D sample (v = 1, p = 0.5) gets
  0.5^0.3 = -1.8 dB on each channel, a hard-panned one 0 dB on one.

Fast 2D (msssoft.m3d)
- Listener: position as given; orientation (0x224019B0) keeps face as
  given (0x22415654..5C, not normalised) and right = up x face, each
  component float(a - b) of the two float products (0x22413C40..48).
- Per sample: 0x08 volume (AIL_set_3D_sample_volume's float as
  given), 0x10 position, 0x44 max and 0x48 min distance (mss32
  0x2111F91D passes the larger as max, as 6.1c does), 0x54 the 2D
  sample. Setting the 3D sample's data (0x22401D30) requires PCM
  ("Not linear PCM data") and mono ("Not mono data"), and resets
  volume 1, max 200, min 1, face (0,0,1) up (0,1,0), velocity 0,
  effects level 0, obstruction and exclusion 0; not the position.
- Every change to a sample's position, volume, distances, effects
  levels, and every listener move or turn (0x22401470: all samples),
  runs the update (0x22401000):
  1. d = sqrt((dz^2 + dy^2) + dx^2) of the double differences
     sample - listener. If d > max: with "Mute at maximum" (provider
     preference 1, default 1, at 0x2241322C; CoD never sets it) the
     sample gets volume/pan (0, 0.5), i.e. silence (unless it is
     stopped, status 8: then nothing changes) and the update ends;
     without it d = max. Below, D = float(d) (or max).
  2. g = float(pow(volume, 5/3)) (0x2240E340). m = min if min >= 1e-4,
     else 1e-4 (0x2240E33C). If 1e-4 + m < D:
     g = float((m / ((D - m) x rolloff + m)) x g), rolloff = provider
     factor at 0x22413230, 1.0 (CoD never sets it): 1/distance falloff,
     -6 dB per doubling past min_dist.
  3. The direction n = the float differences normalised (0x22402980):
     length sqrt((x^2 + y^2) + z^2), each component float(c x 1/length);
     (1, 0, 0) if the length is 0.
  4. front = float(face . n); pan = 0.5 if D <= 1e-4, else
     acos(right . n) x float(1/pi) (0x2240E338), kept as a double; the
     dot products are ((x + y) + z) of the float products. (At -1 the
     x87's pi times float(1/pi) rounds to the same double as the
     double pi's product.)
  5. v = g, or g x 0.75 (0x2240E34C) if front < 0 (behind).
     AIL_set_sample_volume_levels(left = float(pan x v),
     right = float((1 - pan) x v)): a source on the right
     (right . n = 1) gets 0 on the left and v on the right; centred,
     v/2 each.
  6. Velocities (none in CoD: it has no velocity calls) would scale
     the playback rate (Doppler, 0.355 at 0x2240E328); then
     AIL_set_sample_reverb_levels(dry = 1 - obstruction,
     wet = (1 - exclusion) x |effects level|) (0x224013A0 stores the
     effects level's absolute value).
- Consequences: centred 0.5 per channel (-6 dB relative to a hard pan's
  near channel), 45 degrees 0.75 / 0.25, side 1 / 0, behind x 0.75;
  a slightly-over-1 dot product (unit vectors rounded to floats) makes
  acos NaN and the sample silent until it moves.

Checked against the Audigy 2 ZS recordings (eax_captures/
audigy2zs_win98se, fast2d set, relative to front centre): +-45 degrees
near +3.5, far -6.0 dB (model 3.52 / -6.02); +-90 near +6.0, far
-52.6 (model 0: the recording's floor); 135 near +1.0, far -8.5
(1.02 / -8.52); 180 -2.5 both (-2.50); distance 2/4/8 x min: -6.0,
-12.0, -18.1 (-6.02, -12.04, -18.06).

Miles 6.1c's asymmetry at +-90 degrees (Dungeon Siege) is real: its
Mssfast.m3d (base 0x22600000) builds its 2049-entry pan table
(0x22603950) with 127 - floor(acos(i / 1024 - 1) x 127 x c),
c = 0.31830987732601135 (double at 0x2260B938, just under 1/pi), so a
source hard left gets acos(-1) x 127 x c = 126.9999965, pan 1, and
2/128 (-36 dB) on the right channel, while one hard right gets pan 127
and nothing on the left.

## Run log
### 2026-10-12: Miles 6.5e Fast 2D and sample levels (x86recomp 0acb1ee)
- "Miles 6.5e sample levels and Fast 2D: behaviour" above; x86recomp's
  runtime/audio/miles65 implements it for 2D samples, streams and Fast
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
  "Miles 6.5e digital reverb: behaviour" below; x86recomp's
  runtime/audio/digital_reverb implements that description and matches
  the original bit for bit (ctest digital_reverb_oracle, which lifts the
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
