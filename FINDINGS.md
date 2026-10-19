# Findings

## The pk3 directory scan (2026-10-19, branch fs-scan-hash)
With zlib and the IDCT native, the largest remaining blocks of training's
level load were in the file system: FS_LoadZipFile 0x4237A0 (14.7% of the
main thread; FS_Startup rescans all 22 pk3s, 30160 entries, at every map
load) and the file-name hash FS_HashFileName 0x421980 (8.4%). Both came
down to the CRT: unzip reads every field of a central directory entry
with an fread of its own (17 for the fixed 46 bytes, each with the FILE
lock and __getptd), and the hash lowers every character with the CRT's
tolower, which asks __getptd (TlsGetValue) for the thread's locale each
time (tolower 10.2% in all). Two replacements (manifest `replace`):
- unzlocal_GetCurrentFileInfoInternal 0x49DE20 (src/unzip.cpp): the same
  fseek, then one fread of the fixed part where the original makes 17,
  then the name, extra field and comment as the original reads them. The
  FILE is left as the original leaves it (the same bytes in order through
  the same buffer). The original's quirks are kept: shorts sign-extended
  (LittleShort returns a short), sizes compared unsigned, nothing copied
  out on an error. It hands over to the original, which redoes the fseek,
  when that fseek fails (the original then reads on without the
  signature), when the fixed part is short (it keeps stale values), and
  when LittleShort/LittleLong are not the no-swap functions.
- tolower 0x51B3DD (src/crt.cpp): 'A'-'Z' lowered, everything else as is,
  which is what the original does in the C locale. setlocale is linked;
  it is the only code that replaces __ptlocinfo 0x563404 (nothing writes
  to the initial locale in place). While __ptlocinfo is still
  __initiallocinfo 0x5633B0 with no LC_CTYPE handle, no thread can have
  another locale, so the result is the original's; otherwise the
  original runs.
- Oracles: unzip_oracle compares the directory read for every entry of
  every pk3 and of the generated zip (now with extra fields, comments and
  random 16-bit attributes), as the walk and FS_LoadZipFile call it and
  with random buffer sizes, and the hand-over cases (no signature, a
  short read, past the end, a failing fseek, a file that ends inside an
  entry): the result, every buffer byte, where the FILE is left and the
  registers: 90951 directory reads (562654 calls in all), 0 mismatches.
  crt_oracle runs tolower over every int from -70000 to 70000 and 1000000
  random ones, in the C locale and another: 2280002 calls, 0
  mismatches. Mutations caught: the sign extension, the month, the NUL
  after a short name, the extra-field length and seek, the signature,
  the short-read hand-over, the 'A'-'Z' range. ctest 27/27.
- Result (Run log 2026-10-19): training's load 1.619 -> 1.277 s, and
  from launch to the map's start 1.19 -> 0.96 s. What is left of the
  load is mostly inflate itself (host zlib, 31% of the main thread);
  the directory scan is 5.8% (each entry's fseek still refills the
  CRT's buffer, as in the original), tolower 0.2%.

## zlib on the host (2026-10-19, branch host-zlib-jpeg)
CoDSP.exe links zlib 1.1.4 (inflate and deflate), unzip 0.15 and IJG
libjpeg 6 statically. The zlib calls the engine makes now run on the
host's zlib (find_package(ZLIB): the system's on Linux, the SDK's on
macOS), checked against the lifted originals by ctest unzip_oracle and
zlib_oracle. libjpeg stays lifted (below). Names and evidence: Ghidra
plate comments (34 functions). Measurements: "Run log", 2026-10-19.
- Identification: " deflate 1.1.4 Copyright 1995-2002 Jean-loup Gailly "
  (0x537088), " inflate 1.1.4 Copyright 1995-2002 Mark Adler "
  (0x5372FC), " unzip 0.15 Copyright 1998 Gilles Vollant " (0x536624);
  ZLIB_VERSION "1.1.4" (0x54EEFC) and sizeof(z_stream) 0x38 passed to
  inflateInit2_/deflateInit2_; crc_table 0x5373B0; deflate's
  configuration_table 0x537338 (deflate_stored 0x498E00, deflate_fast
  0x498FC0, deflate_slow 0x4992C0). libjpeg: the message table 0x535B68
  holds JMSG_VERSION "6  2-Aug-95" and "Copyright (C) 1995, Thomas G.
  Lane": IJG release 6, as in Quake 3 (jpeg_create_decompress without
  version arguments).
- Who calls zlib:
  - pk3 reads: the file system through unzip (unzOpenCurrentFile
    0x49E6D0, unzReadCurrentFile 0x49E7F0, unzCloseCurrentFile
    0x49EAA0), which inflates raw deflate (-MAX_WBITS) with
    Z_SYNC_FLUSH. The engine reads one field of unzip's read state
    itself: total_out (FS_Seek, unztell inlined).
  - Savegames: the EXE's handler for the game module's syscalls (not
    a function in Ghidra, so both calls were missed) calls compress2
    0x497D00 at level 1 (call at 0x44B2D3; gamex86's G_SaveGame, syscall
    0xA0, "Compressed %lu bytes into %lu bytes", sizes the buffer as
    sourceLen + 0.1% + 13, zlib's documented bound) and uncompress
    0x49D8A0 (0x44B2EC; G_LoadGame). compress 0x497DA0 (level -1) and
    deflateInit_ have no callers. A .svg is a 247-byte header and the
    zlib stream.
- Replaced (manifest `replace`):
  - unzOpenCurrentFile, unzReadCurrentFile, unzCloseCurrentFile
    (src/unzip.cpp). unzip's state stays in guest memory (unz_s, the
    0x6C-byte read info and its 16 KB read buffer from the game's
    malloc, refilled with the game's CRT fseek/fread); only the inflate
    state is a host z_stream per open file, inflating from the read
    buffer straight into the caller's buffer, with the host's crc32.
    Exact for everything read back: return values, output, total_out,
    crc32, the remaining sizes, unz_s. Not reproduced: where zlib 1.1.4
    is between calls (it decodes ahead into its 32 KB window and so
    consumes input earlier: next_in, avail_in, total_in, and when the
    read buffer is refilled, pos_in_zipfile, rest_read_compressed), its
    msg strings, and its allocations from the game's heap (zcalloc
    0x49EBB0 -> 0x42A480). The lifted inflate is now reached only from
    uncompress's fallback.
  - compress2 and uncompress (src/zlib.cpp), with 1.1.4's contract (one
    deflate or inflate with Z_FINISH, Z_BUF_ERROR when it does not
    finish, *destLen written only on success), not the host's
    compress2/uncompress (1.3's uncompress turns a truncated stream into
    Z_DATA_ERROR and writes *destLen on errors). Where the host's result
    is not 1.1.4's, the original runs: compress2 at levels 0, 2 and 5
    (1.1.4 puts (level-1)>>1 in the header's FLEVEL, other bits than the
    host at 2 and 5; level 0 also writes other stored blocks), and every
    uncompress that fails (1.1.4 leaves other partial output, and
    accepted a changed-byte stream the host rejects). uncompress
    therefore inflates into a host buffer first.
- Oracles (configure with -DCOD1_GAME_EXE and -DCOD1_GAME_DIR=<the
  install>; the originals lifted with --only, their CRT calls stubbed via
  tests/crt_stubs.manifest):
  - unzip_oracle: every entry of the 22 pk3s (30160: 14894 deflated;
    the 15266 stored ones are all directories) and a zip the test makes
    (64 stored entries with data, the same deflated by the host at every
    level), walked with the lifted unzOpen/unzGoToNextFile, each read
    whole and once more, in random chunks of 1 B to 100 KB, and (up to
    256 KB) half then reopened. 471678 calls, 2.4 GB: 0 mismatches,
    36 s. Mutations caught: total_out, the stream-end return, crc32 of
    stored data, pos_local_extrafield, the zero-length read. The read
    buffer's refill size is not observable, by design.
  - zlib_oracle: 400 generated buffers (empty to 1.1 MB) at every level,
    at level 1 also with destinations short by one byte and by half, and
    the contents of every pk3 entry up to 256 KB (28768) at level 1;
    uncompress of each result into exact, larger and short destinations
    and of corrupt copies (truncated, a byte changed, the Adler-32
    changed). 217584 calls (117503 return errors): 0 mismatches, 98 s.
    Without the fallbacks (generated inputs and pak8): levels 0, 2 and 5
    differed in 400 of 400 cases each; failed uncompresses differed in
    their partial output in 1033 of 18298 (corrupt and truncated
    streams; short destinations and bad checksums matched), and one
    changed-byte stream succeeded in 1.1.4 only.
  - A real autosave (training.svg, 130391 bytes) recompressed by the
    host's zlib 1.3.2 at level 1: byte-identical (30180 bytes).
- ctest 25/25. The game boots to training with textures intact, writes
  the autosave with the native compress2 and loads it with the native
  uncompress ("Uncompressed 30046 bytes into 130419 bytes"). Loading it
  then stops at "buffer overflow while restoring sound state: probably
  due to an old or corrupt savegame" on the base build too (main
  741c785): not a zlib problem.

### JPEG decoding stays lifted
- LoadJPG 0x4E23B0 (Quake 3's): ri.FS_ReadFile, libjpeg 6 with Quake
  3's memory source (jpeg_stdio_src 0x50B9A0) and RGB_PIXELSIZE 4
  (output_components 4; LoadJPG sets alpha 255), a "LoadJPG"-tagged
  allocation; libjpeg errors are ri.Error(ERR_FATAL). Only the float
  IDCT is compiled in: default_decompress_parms sets dct_method
  JDCT_FLOAT, and jddctmgr's start_pass takes jpeg_idct_float 0x519140
  for it and raises JERR_NOT_COMPILED for any other (progressive files
  are JERR_NOT_COMPILED too). jpeg_idct_float is x87 code (CW 0x027F)
  and converts with the CRT's float-to-integer helper 0x52E218.
- libjpeg-turbo 3.1.4.1 against the lifted LoadJPG on all 211 pk3 JPEGs
  (all three-component; a scratch harness, not committed): images
  identical: JDCT_FLOAT 5 of 211, 13.4% of all pixels differ, at most by
  3 (12.1% with its SIMD off, JSIMD_FORCENONE=1); JDCT_ISLOW 5, 12.9%,
  max 3; JDCT_IFAST 2, 62.3%, max 29. jpeg-6's float IDCT computes in x87
  precision and truncates before descaling; libjpeg-turbo's (6b/7
  lineage) adds a rounding bias up front and runs in single precision.
  So LoadJPG and libjpeg stay lifted, and JPEG screenshot encoding with
  them (not tried).
- It matters for load time: with zlib on the host, LoadJPG was 13.7% of
  the main thread in training's load, jpeg_idct_float 12.5% inclusive
  (most of it the _ftol2 calls). So jpeg_idct_float alone is native now
  (2026-10-19, branch jpeg-idct-float): src/jpeg_idct.cpp, manifest
  `replace` 0x519140, the rest of libjpeg lifted.
  - The x87 code was read by executing it symbolically (every value as
    an expression, every m32 store marked): pass 1 down the columns
    (DC-only shortcut, then the even and odd parts) into the float
    workspace, pass 2 unrolled over the eight rows, which all round at
    the same points. The compiler keeps some of jidctflt.c's float
    variables on the stack unrounded (pass 1's tmp0, tmp12 and z10;
    pass 2's tmp10, tmp11, tmp13, tmp2, z13, z10, tmp6 and the odd
    tmp11), so the native code is written from the binary, not the
    source, in src/x87_value.h's terms. _ftol2 (0x52E218) is a
    truncation to 64 bits; the sample is (low 32 bits + 4) >> 3 &
    0x3FF into sample_range_limit + 128.
  - ctest jpeg_idct_oracle (lifted on the exact x87 model, CW 0x027F):
    200000 random blocks (DC only, sparse, dense, extreme coefficients;
    jpeg-6 multipliers from random tables, or random floats; a random
    range-limit table) and all 912624 blocks of the 211 pk3 JPEGs, their
    coefficients read with the host's libjpeg (jpeg_read_coefficients,
    lossless) and multipliers as jpeg-6's jddctmgr makes them: 0
    mismatches, 8.5 s. Mutations caught: one rounding dropped (46642
    blocks), one added (40467), a constant swapped, the descale bias,
    rounding instead of truncation. The test links the host's libjpeg
    (find_package(JPEG)).
  - The "Click to Start the Mission" screen (the diary) is byte-identical
    in a frame capture from both builds.

## Collision traces (2026-10-18, branch collision-traces)
CoDSP.exe's trace code is native C++ (src/cm_trace.cpp), checked bit for
bit against the lifted originals by ctest cm_trace_oracle. Profile that
motivated it: "Run log", 2026-10-18.
- Replaced (manifest `replace`): CM_BoxTrace 0x41D380, CM_TransformedBox-
  Trace 0x41D870, its body 0x41D3B0 and CM_Trace 0x41CD60. Under them,
  native: CM_TraceThroughTree, -Leaf, -Brush, -Patch, the capsule traces
  (capsule and box through capsule, sphere, vertical cylinder), CoD's
  terrain (point 0x419580, capsule 0x4196F0, box 0x41A2D0 with its
  one-time warning, dispatch 0x41A350), the leaf-surface bounds test
  0x41B690, and the position tests (CM_PositionTest, CM_TestInLeaf,
  CM_TestBoxInBrush, the two capsule tests, terrain 0x419EE0). Their
  lifted bodies stay for their other callers (point contents, area
  code).
- Called lifted, from the original's call sites: CM_ClipHandleToModel,
  0x426940 (normalises a vector: its square root is the CRT's _CIsqrt,
  which sets errno on a NaN), 0x4271E0 (angle vectors, fsincos), patch
  collision (CM_TraceThroughPatchCollide 0x415A40, CM_PositionTestIn-
  PatchCollide 0x4163C0), CM_BoxLeafnums_r 0x41A5D0 with its leaf
  callback, the console print. The trace work (0x118 bytes) stays in
  CM_Trace's guest frame, so they get the original's pointers; guest
  frames are made only where such a call needs memory below them. Not
  replaced: point contents (0x41A730, 0x41A820: about 1.5% of the main
  thread) and the rest of 0x419000-0x41DFFF (area portals, leaf
  queries, terrain generation at load).
- Calling conventions are the originals' whole-program ones, e.g.
  CM_BoxTrace(EAX results, EDX brushmask, ECX capsule; start, end, mins,
  maxs, model) and CM_Trace(EAX sphere, EDX mins, ECX maxs; results,
  start, end, model, origin (unused), brushmask, capsule). results->
  fraction is the fraction to beat (the wrappers set 1). Every caller
  of the four entries overwrites EAX, ECX, EDX and EFLAGS before reading
  them (the cgame syscall exit does `xor eax,eax`), so those are not
  reproduced; ESP, EBX, ESI, EDI, EBP and the x87 stack depth are.
- x87 at CW 0x027F (PC=53, round to nearest) is IEEE double arithmetic
  with a wider exponent: src/x87_value.h keeps each value the original
  holds on the x87 stack as a double (St) and each one it stores as a
  float (F32), operand for operand (`St(a) * b + c` is fld a / fmul b /
  fadd c; two F32s cannot be added by mistake). It is exact while values
  stay in double range, which float inputs through these few products
  and quotients cannot leave. NaN results are recomputed with the x87
  rules (one NaN operand quietened; of two, a QNaN beats a memory SNaN,
  then the larger significand, equal ones the positive; invalid gives
  the negative indefinite), which differ from SSE's and arm64's. Each
  flag test is written as the C++ comparison with the same NaN branch:
  `test ah,1; je` (taken only for an ordered >=) is `>=`, `test ah,5; jp`
  is `!(a < b)`, and so on. -ffp-contract=off, -fno-strict-aliasing
  (overlays of the clip map in guest memory).
- Not reproduced: the x87 status word (sticky flags, C0-C3; read only by
  the CRT's math-error path), stale x87 registers, /GS cookies, the dead
  stack below ESP.
- Data layouts (src/cm_trace.cpp): cplane_t, cbrush_t (0x34, int16
  shaders of the six axial planes at +0x28), cbrushside_t, cNode_t (8:
  plane, int16 children), cLeaf_t (16-bit counts), leaf surfaces (0x2C:
  patch collide at +0x24 or terrain at +0x28), terrain (u16 tri count,
  a byte saying which capsule end meets it first, 0x48-byte tris with
  their plane, two barycentric edge planes, vertex and edge pointers;
  vertices 0x10 and edges 0x38 with checkcounts), trace_t (0x30,
  allsolid +0x2E, startsolid +0x2F), traceWork_t (0x118, sphere_t of
  0x24 at +0xF4), cm globals 0x14BD64C-0x14BD768 (box model 0x14BD740,
  box brush pointer 0x14BD73C, checkcount 0x14BD720), cm_noCurves
  (0x14BD64C) and cm_playerCurveClip (0x14C77C0, read by patch
  collision).
- ctest cm_trace_oracle (configure with -DCOD1_GAME_EXE; the originals
  lifted with --only and run on x86recomp's exact x87 model, SoftFloat,
  not its double runs; CoDSP.exe mapped for its constants and globals;
  `--only` needs the leaf callback 0x41A4D0 and the eight cases of
  0x429800, which fills its own jump table, listed): 300 random clip
  maps (BSP of axial and oblique planes, brushes with oblique and axial
  sides, terrain height fields with shared vertices and edges, patch
  collides with random facets, inline models, the box model; half on
  whole units) with 1000 traces each through all four entries: points,
  boxes, capsules, spheres, rotations, no-move position tests, level,
  vertical and whole-unit moves, starts in and next to brushes, surfaces
  and the box capsule, capsules on terrain vertices and edges with tiny
  fractions to beat, exactly tangent ones, and zeros, denormals,
  infinities, NaNs and huge values, the same ones at both ends now and
  then. Compared: every byte of the clip map (checkcounts, the box
  model), its globals, the results and arguments, and the registers
  above. 300000 calls (66096 hits, 33176 start solid, 2929 NaN
  fractions), 0 mismatches, about 6 s. The native code's line coverage
  under it is 99.7% (llvm-cov); only the warning's print is not run (it
  would reach the console's imports). 26 mutations (operand order and
  precision, NaN rules, branch equalities, registers, stack arguments,
  checkcount, end points, the box corner, the leaf bounds, a z not
  restored...) each fail it. Others did not and are equivalent: e.g.
  `d2 >= epsilon` right after `d2 > 0`, a NaN in a position test (it
  needs start == end), the end point of the sphere test, which only its
  start-solid branch reads. A one-ulp change in a double intermediate
  that is later rounded to float is beyond any such test; the
  transcription itself is the safeguard there.
- Captures (-DCOD1_CM_CAPTURE=ON, COD1_CM_CAPTURE=file, COD1_CM_EVERY,
  COD1_CM_MAX; tests/cm_trace_capture.cpp): the clip map walked from the
  tree, the models and the box model (nodes, planes, leaves, brushes,
  sides, shaders, patch collides, terrain with its vertices and edges,
  cvars) whenever it changes, then sampled calls with what they read and
  the game's own result, which the lifted original must reproduce from
  the record. Pass files to the test (COD1_CM_CAPTURES for ctest).
- Names and evidence: Ghidra plate comments. CM_BoxTrace and
  CM_TransformedBoxTrace: cgame syscalls 39-42 in 0x401A50 with RtCW's
  arguments and capsule 0, 0, 1, 1; the others by call-graph position
  and structure against cm_trace.c / cm_test.c and by data layout.
  CoD's own terrain functions, the transformed body and the bounds test
  stay unnamed with comments.
- Captured training sessions (the walk with turns and jumps, every
  10th and every 200th call, 20000 each, almost all CM_TransformedBox-
  Trace from cgame's entity traces): 0 mismatches, and the lifted
  originals give the game's result for every call from the record.
- Training walk, before (origin/main dc5a30a) and after, same x86recomp
  (main 4735305), three runs each, interleaved, idle machine; "Run log",
  2026-10-19: mean frame 2.85 -> 2.03 ms (-29%), p95 3.7 -> 2.3 ms, p99
  3.8 -> 2.35 ms; the traces from 32.8% of the main thread to 6.6%.
- cg_showmiss 1 on the walk with turns and jumps, both builds: no
  "Prediction miss" or "prediction error".

## Footsteps at any step length (2026-10-13, branch pmove-sound-events)
Fix 1 for "Sound events lost at high fps" (under "Frame rate"), in
src/footsteps.cpp: one native implementation replaces 0x200067C0 and its
cgame mirror 0x30005F00, with pmove_fixed 0 and 1.
- bobmove (cycle units per ms) is computed as the original does it, in
  its order of operations, with its float constants: xyspeed / (ps.speed
  x direction x move x stance scale) x K. The stance is 0x20003460 /
  0x30002C10, and the stance-change blend is 0x200053C0 / 0x30004B60.
  Both are called as the originals call them. The airborne (ladder)
  branch, the reset below xyspeed 1 and pm->xyspeed are kept.
- A step adds floor(bobmove x t1) - floor(bobmove x t0) (t1 =
  ps.commandTime, t0 = t1 - pml.msec) instead of truncating bobmove x
  msec. The footstep function 0x200065C0 / 0x30005D00 is called with the
  old and new cycle as before. The ladder gets the same advance; it was
  not tested.
- Cadence: bobmove exactly, so K at full speed. The truncated original
  at a reference frame rate was not matched, because it has no single
  value: on the same steps it gives standing 0.30 at 60 fps, 0.285 at
  125 and 0.30 at ~333, and prone 0.233 / 0.212 / 0.06. It also blurs
  the stances' constants (crouch 0.34 and stand 0.335 both come out near
  0.25-0.30). Against 60 fps, walking footsteps now come about 10%
  sooner (382 ms apart instead of ~425); against 125 fps (pmove_fixed),
  about 25% sooner. A scale on bobmove would be the place to change this.
- Verified, scripted training runs, isolated config, windowed,
  r_swapInterval 0, with a temporary env-gated trace of every server step
  (not committed). Segments of 8 s per stance and direction at
  com_maxfps 0 (300-480 fps: steps of 1-6 ms, mostly 2-3), 333, 250,
  200, 125, 62 and 59. `fixedtime` stays cheat-protected under spdevmap
  (sv_cheats reads 0), so 1 ms steps cannot be forced. They are 1-2% of
  uncapped steps. Steady full-speed steps, cycle units per ms (the
  original's truncation of the same steps in brackets):

  | pmove_fixed 0 | msec 3 (1-6 mixed) | msec 4 | msec 5 | msec 8 | msec 16 | msec 17 |
  |---|---|---|---|---|---|---|
  | stand (K 0.335) | - | - | - | 0.334-0.335 (0.285) | - | 0.334 (0.301) |
  | back (K 0.36) | 0.362 (0.303) | - | - | 0.360 (0.283) | 0.359 (0.325) | - |
  | crouch (K 0.34) | 0.340 (0.291) | 0.341 (0.253) | 0.340 (0.224) | 0.339 (0.254) | 0.339 (0.312) | 0.340 (0.300) |
  | prone (K 0.25) | 0.251 (0.060) | 0.250 (0.195) | 0.250 (0.199) | 0.250 (0.212) | 0.250 (0.235) | 0.249 (0.233) |

  (The segments alternate forward and back, so not every cell was run;
  crouch and prone use one K for both directions. The background window
  was throttled to 120 fps for part of the run, so some capped segments
  ran 8 ms steps; the column is the measured step length.) Footsteps come every
  128 / bobmove ms: measured 511-530 ms prone (512 expected), 376-382
  crouched (377), 380-383 standing (382), 353-357 back (356).
- By step length, over the steady steps of each stance: lengths with
  100+ steps (2-9, 16, 17) advanced 0.91-1.11 x bobmove x msec, most
  within 3%. Only the total is exact: with alternating lengths a unit can
  fall to either. The original's truncation on the same steps: 0 at
  msec 2 in every stance and at 3 prone, 0.57-0.99 elsewhere. Steps of
  1 ms (19-30 steady ones per stance) advanced 0.7-1.3 x, within the
  noise of so few one-unit steps; the original added 0. Events landed
  on steps of 2-19 ms. On 1 ms steps about 0.1 per stance was due, and
  none landed.
- pmove_fixed 1 (8 ms steps throughout): stand 0.334-0.336, back
  0.360, crouch 0.339 and prone 0.250 cycle units per ms in every
  segment. One prone segment ran at 0.41, with xyspeed 1.6 times the
  crawl speed (probably a slope), and its footsteps followed. The original's truncation of the same steps gives 0.25 for
  stand, back and crouch, and 0.21-0.25 prone (the fix (b) run measured
  0.187). Footsteps came every 379-381 / 355-357 / 377-382 / 512 ms. One
  run covers stand and crouch, a second covers prone (the first was
  stopped during crouch).
- Not covered: the ladder, strafing and diagonals, and pm_flags 0x80.
  They use the same advance.

## pmove_fixed without rounding (2026-10-13, branch pmove-sound-events)
Fix (b) for "Sound events lost at high fps", 2 (under "Frame rate"),
in src/pmove_fixed.cpp. It applies only with pmove_fixed 1; pmove_fixed 0
runs the originals unchanged.
- Server: the ClientThink 0x20012430 wrapper hides pmove_fixed (the vmCvar
  integer reads 0 during the call), so the command time is no longer
  rounded up. Pmove 0x20008580 then runs only the whole pmove_msec
  steps (8..33, ClientThink's clamp) that fit before the command time,
  leaving the remainder pending. It still does this through the lifted
  Pmove, with pm->pmove_fixed set and a shortened cmd.serverTime. The
  original's 1000 ms and 50 ms catch-up limits apply first.
- cgame: the same in prediction 0x3001EF70 and Pmove 0x30007C30. Steps
  begin at ps.commandTime, and each one uses the first command that
  reaches its end. The server and prediction therefore run the same
  steps with the same commands.
- A command that completes no step still updates the view angles
  (0x20006FD0 / 0x30006710), as the original fixed path did for every
  command.
- View: PmoveSingle 0x30007680 records the origin at each step end (64
  entries keyed by step). After prediction, cg.predictedPlayerState's
  origin is shown interpolated between the ends at ct - step and ct by
  pending / step, which is one step behind the newest command. The true
  origin is put back before the next prediction, because the original
  compares against it for misses. There is no interpolation across more
  than 32 units (teleports) or on the no-prediction path.
- Not interpolated: the view height during stance changes and the bob
  cycle, both 125 Hz.
- Verified, scripted training run, pmove_fixed 1, uncapped (the game loop
  ran with 1-5 ms frames):
  - 1820 of 1821 fresh snapshots replayed a command (the other one was
    at load). Before the change, 1157 of 1611 replayed none at
    500-850 fps.
  - cg_showmiss 1 printed no prediction misses.
  - Footsteps in every stance run on 8 ms steps: stand and crouch 0.25
    cycle units per ms, prone 0.187 (before fix 1: see the section above).
  - The shown origin moves every frame at the true speed: 0.190 units
    per ms walking (190 u/s), the same across step boundaries.
  - Sign beeps not yet piloted.
- pmove_fixed's default is still "0".

## 2D placement on wide screens, items 5-6 (2026-10-13, branch hud-2d)
src/screen2d.cpp places all 2D on screens wider than 4:3 (FINDINGS'
design below, with corrections). At 4:3 and 5:4 nothing changes: the
main menu at 1024x768 is pixel-identical to the build before.

How 2D reaches the renderer (corrections to the design):
- Rectangles are in pixels: StretchPic 0x4D4330 (x, y, w, h, s1, t1,
  s2, t2, shader), the gradient form 0x4D43D0 (one more colour for the
  lower edge: its handler 0x4CD540 gives vertices 0-1 the current colour
  and 2-3 the argument), the rotated form 0x4D44D0, the four-point quad
  0x4D45B0 (points, st, shader), and StretchRaw 0x4CCD10 (integers,
  drawn at once). SetColor is 0x4D42A0.
- Text is in 640x480 units: every text draw ends in 0x4D6400, which
  queues command 6 (string at +0x25). When the commands run, 0x4CDCC0
  turns each glyph into pixels through the refimport AdjustFrom640,
  which is the client's 0x40FED0 (set at 0x40E5B0): glyph widths scale
  with W/640, so HUD and menu text was stretched like the rest (the
  design's "text has one uniform scale" was wrong). The console passes a
  fixed advance of its own and was never stretched.
- Black cinematics: the screen-field function 0x410410 clears a wide
  screen outside of play (client state != 2) with one black StretchPic
  over the whole screen (return address 0x41047C), meant to leave the
  sides black under the menus. StretchRaw draws cinematic frames at
  once, before the queued commands run, so the clear painted over every
  frame. The code is the same at any wide mode, so the original should
  show it too (not checked on Windows); this build was the first to run
  one fullscreen.

Placement (by who draws; k = (4/3)/A, b = W(1-k)/2):
- Pillar, the 4:3 picture centred: uix86 vmMain 0x400079A0 (menus,
  briefing and loading screens) and the cinematic draw 0x4076F0. The
  side clear becomes the two side bars. In play (no side clear) a
  full-screen UI backdrop gets black bars too: SetColor(black), two
  white-shader StretchPics, the last colour set again.
- Center: cgamex86 vmMain 0x30018CD0 (the HUD); rectangles across the
  whole width stay full width (fades, full-screen overlays).
- Left/Right by HUD menu name in cgame's Menu_Paint 0x3003B710 (menu in
  ECX, name at +0x20): Compass, stance, tankstatus left; weaponinfo,
  Health right. The objectives list 0x3001F570 (+scores, heading
  "CGAME_MISSIONOBJECTIVES") left: at 4:3 it sits above the compass, so
  it keeps to the left edge with it. The engine's notify lines
  0x409130: left ones ("Objectives updated.") left, centred ones (fourth
  argument nonzero: bold messages such as the tank tutorial's) centred.
- Scope: the overlay 0x3000F770, centred with edge-touching rectangles
  extended. The aim point 0x3000F500 (x offset to [EDI] in 640 units)
  is divided by k in centred modes, since it is computed with the 3D
  view's real FOV.
- Stretch (as before): everything else the engine draws: the console,
  its background.
- Rectangles and quads are rewritten when queued (the arguments are the
  callee's; the quad's points are put back). Text keeps its placement by
  command address until 0x4CDCC0 runs it; 0x40FED0 then places each
  glyph. The render commands run on the main thread here.
- COD1_2D_LOG=1 logs each caller of each entry once per placement
  (entry, return address, placement, first arguments, a text's string).

Verified (pathfinder and the menus, 1920x1080 fullscreen, before =
main 8248a19; screenshots in the session's report):
- The intro cinematics play, pillarboxed (before: black).
- Main menu, briefing ("Click to Start the Mission"), loading text and
  the in-game menu stay 4:3 with black bars (before: stretched).
- HUD: compass round and at the left edge with the stance icon; weapon
  name, ammo and health at the right; "Game Saved" centred with
  unstretched glyphs; "Objectives updated." and the objectives list
  (Tab, above the compass; 2560x1440 on macOS) at the left edge.
  Console unchanged.
- At 2560x1440 (r_mode -1 0x0) the briefing, the opening text and "Game
  Saved" fit and are centred.
- Tank (tankdrivecountry): compass and tank icon left, the damage box
  right, the turret reticle and the tutorial text centred.
- Scope (pavlov, kar98k_pavlovsniper): round, centred, black to every
  edge; its circle is 508 px across at 1920x1080 as at 1440x1080 (before:
  an ellipse).
- Cheats for such runs: `+set thereisacow 1337 +devmap <map>` (devmap
  alone says "Cheats are not enabled"), then `god` in the console;
  `give` works only for weapons the level precaches (`+bind F7 "give
  kar98k_pavlovsniper"`, since scripted keys cannot type `_`). ADS is
  MOUSE2 (`toggle cl_run`).
- Not seen: reticles of other weapons. Right- or centre-aligned text
  inside a HUD group shifts by w(1 - k) against its anchor (not visible
  in these screens).

Memory (x86recomp b60f5f1, found here): loading a level peaked at 21 GB
of shared memory at 1024x768 (the build before the widescreen work too)
and ran the host out of memory at 2560x1440, then kept about 4 GB:
SDL_GPU cycled the upload buffer on every flush without bound. Now 249
MB and 329 MB.

## Widescreen and frame rate, items 1-4 (2026-10-13, branch widescreen)
Items 1-4 of "Widescreen, FOV, 2D placement, frame rate: design" below;
5-10 (the 2D transform, HUD anchors, fps measurements, the prediction
cache) are still open. The shared parts are x86recomp's (branch
widescreen; ARCHITECTURE.md "Display modes, fullscreen and
presentation", "Frame pacing"): the mode list with the desktop and wide
sizes, host fullscreen for a window covering the screen
(COD1_FULLSCREEN=window|desktop|exclusive, default desktop; window for
COD1_BACKGROUND runs), aspect-kept presentation with the pointer mapped
through it, DesktopSize/LastPresentation, FramePacer, and
COD1_FPS_REPORT=s (frame rate and interval spread every s seconds).
GL_MAX_TEXTURE_SIZE is 8192 now: the renderer refuses a mode wider than
it ("video card cannot set mode 2560x1440 because GL_MAX_TEXTURE_SIZE is
only 2048").

- Hor+ (src/fov.cpp, cgamex86 `replace` 0x30025200 and 0x30025C80):
  fov' = 2 atan(tan(fov / 2) * A * 3/4) when A = vidWidth / vidHeight >
  4/3, so cg_fov 80 gives 96.4 at 16:9. The view setup's zoom
  sensitivity is fov / cg_fov again (the original fov, from the call at
  0x30025329; the sky portal's call stores none). Underwater the original
  adds a small sine to fov_x before that division; the sensitivity does
  not follow it now.
- Native resolution (src/video_mode.cpp, `replace` 0x4A5610): `r_mode -1`
  with r_customwidth/height 0 is the desktop's size ("...setting mode -1:
  2560 1440 FS", CDS ok, fullscreen). The desktop is in points: these
  3840x2160 monitors at scale 1.5 give 2560x1440. Since x86recomp
  0cadefc the desktop is in pixels; a 5K display (macOS Retina,
  5120x2880) then failed at renderer start with "Hunk_AllocateTempMemory:
  failed on 134217744": the renderer takes a 32-bit texture of the screen
  rounded up to powers of two (8192x4096, exactly 128 MB) from the hunk's
  temporary memory, and the hunk is com_hunkMegs "128" (0x42A980,
  Com_InitHunkMemory-like: Cvar_Get("com_hunkMegs", "128", 0x21), 80 MB
  minimum, malloc once). `replace` 0x42A980 raises the cvar's value and
  integer (not its string, so the saved config is unchanged) to 128 MB
  plus that texture's size beyond its 32 MB at 2560x1440: unchanged up to
  2560x1440, 160 at 3840x2160, 224 at 5120x2880. 5120x2880 fullscreen on
  an M3 Pro: training 75-100 fps uncapped, GPU-bound (100%), main thread
  ~40%, process footprint 3.5 GB.
- Pacing (src/frame_pacing.cpp, `replace` 0x430690): FramePacer.Wait
  (com_maxfps) before the lifted body, which then sees com_maxfps 0 (its
  1 ms minimum has always passed); the integer is put back unless the
  cvar changed during the frame (modificationCount). The lifted body
  still catches the error longjmp (its try/catch is in
  f_00430690_lifted).

Verified (training, isolated XDG_DATA_HOME, scripted input, release):
- Hor+ and sensitivity: 1024x768 against 1366x768, both r_fullscreen 1
  (a popup at 0,0, so scripted mouse motion drives the virtual cursor).
  The wide frame's centre 1024 columns match the 4:3 frame before and
  after a scripted 200-count turn (RMSE 3-8%, from the stretched HUD and
  NPC animation; the views before and after the turn differ by 30%): same
  vertical FOV, same turn per count.
- Before, at 1920x1080 windowed: Hor- (fov_x 80, so 50.6 degrees vertical)
  with the HUD stretched. After: 64.0 degrees vertical, as at 4:3; the
  HUD is still stretched (items 5-6).
- Fullscreen (desktop): 1920x1080 fills the 2560x1440 screen; 1024x768 is
  pillarboxed (1920x1440 at x 320); r_mode -1 0x0 runs at 2560x1440.
  Fullscreen and windowed runs turn the same per count (after-turn frames
  0.3% RMSE apart): relative mouse motion is not scaled by the
  presentation. Exclusive: host mode 1600x900 for 1024x768 (the display's
  aspect; Wayland emulates the switch), pillarboxed; a focus loss and
  regain (COD1_TEST_FOCUS=16,21) leave it fullscreen and running. The
  d3d9 backend (COD1_GFX=d3d9) pillarboxes the same way.
- Pacing, 1280x720 windowed, r_swapInterval 0, standing at the start, 10
  s per run (every presented frame's interval; main thread CPU from
  /proc):

  | com_maxfps | build | fps | interval median / p1 / p99 ms | main thread CPU |
  |---|---|---|---|---|
  | 144 | before (1 ms spin) | 166.26 | 6.2 / 5.3 / 6.2 | 99.6% |
  | 144 | after | 143.94 | 7.1 / 6.2 / 7.6 | 39.7% |
  | 240 | before | 244.69 | 4.2 / 3.2 / 4.7 | 99.7% |
  | 240 | after | 239.99 | 4.2 / 2.7 / 4.8 | 72.5% |

  Frames start on the grid; the presents spread with each frame's work
  (the 20 Hz server frame lands on some of them), as they did before.

Harness note: windowed runs cannot script mouse motion. CoD clips the
cursor to its window rectangle, caption included, which is not inside
the client area, so the cursor is not virtual and TEST_INPUT motion (no
absolute position) lands at the client origin: the view jumps to the
upper left. The main build does the same. Runs that turn use r_fullscreen
1 (COD1_FULLSCREEN=window keeps a mode-sized window for them).

## macOS arm64 (2026-10-13, branch macos-metal)
Builds and starts on an M3 Pro with x86recomp's SDL_GPU backend on Metal
(no dxvk; CMake leaves DXVK_NATIVE_DIR empty on Apple hosts). A fresh
Ghidra 12.1.4 analysis plus EaxMan.dll lifts to the module counts in the
table below (CoDSP 5291, gamex86 1996, cgamex86 1214, uix86 769
functions; EaxMan 339). With an intact install (an earlier copy had
damaged pk3s: "17 sound file(s) are missing or in a bad format"; test
pk3s with Python's zipfile) it plays the intro cinematic and, with
`-- +map training` and a scripted click to start the mission, reaches
the training level: sky, terrain, alpha-tested foliage, models, HUD,
autosave. Not yet played by hand or compared against the d3d9 backend.
Killed (scripted) runs leave the __CoDSP safe-mode marker in the overlay,
~/Library/Application Support/cod1/install on macOS; background runs then
take safe mode's default and safemode.cfg's picmip 2 / 11 kHz end up in
main/config.cfg. Delete the marker before each run (and that config once
it has them): autoconfigure then picks 2000/512/256 (r_mode 6, picmip 0,
44 kHz) as on Linux. With the HLE sync barriers (x86recomp 0869565) the
training run is unchanged.

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
  `replace`, src/): one cinematic fix (src/cinematic.cpp), and native
  CPU skinning (src/skinning.cpp, bit-exact; "Run log" 2026-10-13).

Training plays through to completion (2026-10-12, played by hand on EAX
3). Its frame-rate drops were CPU-bound: x86recomp's GL draw path and
native skinning removed them (no frame over 25 ms in a played session;
"Run log" 2026-10-13).

Next: collision traces (about 45% of the main thread in training, on the
x87 model; native since 2026-10-18, "Collision traces"); then
widescreen/FOV, UI scaling and high frame rates (design and order:
"Widescreen, FOV, 2D placement, frame rate").

EAX works: with `mss_3d_provider "Creative Labs EAX 3 (TM)"` (a latched
cvar: takes effect on the next start, or pick it in the sound options)
the game runs Creative's EAX Manager (EaxMan.dll, lifted as a fifth
module) on x86recomp's EAX providers. See "Run log".

## Widescreen, FOV, 2D placement, frame rate: design (2026-10-13)
Investigation only (items 1-4 since implemented: "Widescreen and frame
rate, items 1-4" above), nothing renamed in Ghidra (the Quake 3 names
below are likenesses, not confirmed names). Addresses are
VAs in the unpacked CoDSP.exe, cgamex86 (0x3...), uix86 (0x4...) and
gamex86 (0x2...). Decided with the user: the RoQ cinematics and the menus
are 4:3 assets and stay pillarboxed (black bars), not stretched or
cropped.

### Today (scripted runs in training, isolated XDG_DATA_HOME)
- `+set r_mode -1 +set r_customwidth 1280 +set r_customheight 720
  +set r_fullscreen 0` works: a 1286x745 window, back buffer 1280x720.
  The view is Hor- (fov_x 80 kept, the vertical cut), the HUD is
  stretched (compass and stance icon are ellipses), text glyphs are not.
- The same with r_fullscreen 1: "calling CDS: failed, bad mode", then
  "trying next higher resolution: ok": x86recomp's ChangeDisplaySettingsA
  only knows 640x480-1600x1200 (4:3 and 5:4, up to the desktop size), so
  the virtual display becomes a larger 4:3/5:4 mode and the game opens a
  1280x720 popup in it. The host window is never fullscreen: it is a
  borderless window of that size, and present stretches the front
  buffer to the window without keeping the aspect.

### Resolution
- Renderer cvars (0x4A7A50): r_mode "3" (latched), r_fullscreen "1",
  r_customwidth "1600", r_customheight "1024", r_customaspect "1",
  r_displayRefresh "0", r_swapInterval "0" (archived).
- Mode table at 0x564A28, 13 entries {description, width, height, pixel
  aspect 1.0}, count at 0x564AF8: 0-10 are 320x240 to 2048x1536 (4:3 and
  5:4), 11 is 856x480 and 12 is 1920x1200 ("(wide)"). The graphics menu
  (options_graphics.menu) offers only 3, 4, 6, 7, 8, 9, 10; its 856x480
  entry is commented out.
- 0x4A5610 (mode in EAX; out width, height, window aspect): -1 takes
  r_customwidth/height and window aspect = r_customaspect (a window
  aspect, not a pixel aspect); a table mode gives w / (h * pixelAspect).
  The window aspect (glconfig +0x8C, 0x15347CC) is read by neither cgame
  nor ui.
- 0x4FDB60 sets the mode ("...setting mode %d:"): fullscreen does
  ChangeDisplaySettingsA (colour bits, r_displayRefresh), on failure the
  first EnumDisplaySettings mode at least as large, else windowed.
  0x4FD810 creates the "Call of Duty" window: fullscreen WS_POPUP at 0,0
  (style 0x90080000), windowed WS_CAPTION at vid_xpos/vid_ypos (style
  0x10C80000, AdjustWindowRect).
- vidWidth/vidHeight: renderer glConfig 0x15347C4/C8, client copy
  0x143942C/30, cgame 0x300FE984/88 (its glconfig at 0x300FE900, syscall
  0x50), ui 0x401DE750/54 (glconfig 0x401DE6CC, syscall 0x33).

Design:
- x86recomp, generic Windows behaviour: Modes() also lists the desktop
  mode and the common wide sizes up to it (1280x720, 1366x768, 1600x900,
  1920x1080, 1920x1200, 2560x1080, 2560x1440, 3440x1440, 3840x2160). A
  top-level popup covering the whole (virtual) screen after a mode change
  goes host fullscreen (SDL borderless desktop fullscreen, no real mode
  switch), and Show() blits with the aspect kept (black bars) instead of
  stretching, so a 4:3 mode on a 16:9 monitor is pillarboxed. An accessor
  for the desktop size for game code. DS1 (ddraw exclusive mode) shares
  the path and must keep working. On macOS SDL3 display sizes are points:
  "native" means points unless the window asks for high pixel density.
- CoD1: replace 0x4A5610 so that r_mode -1 with r_customwidth/height 0
  is the desktop size, window aspect w/h. Native resolution is then
  `seta r_mode -1; seta r_customwidth 0; seta r_customheight 0` (the menu
  cannot select it); any other size or aspect is r_mode -1 with explicit
  numbers, windowed or fullscreen.

### FOV
- 0x30025200 returns fov_x in ST0: cg_fov (vmCvar 0x301E7B40, value at
  +8; "80", archived) clamped to [1, 160]; while aiming down the sights
  it moves toward the weapon def's (0x30125668) adsZoomFov (+0x218) by
  the ADS fraction 0x3011E15C (adsZoomInFrac +0x21C, adsZoomOutFrac
  +0x220); bits 0xC000 of 0x3011E124 force 55 (state not identified).
- Callers: the view setup 0x30025C80 (refdef at 0x30125724: x, y, width
  0x3012572C, height 0x30125730, fov_x 0x30125734, fov_y 0x30125738)
  computes fov_y = atan2(height, width / tan(fov_x / 2)) * 360 / pi:
  Hor- on wider screens. It then sets the zoom sensitivity 0x301257AC =
  fov_x / cg_fov, which 0x30026B10 sends with the usercmds. The script
  camera path (syscall 0xDB, "setCameraOrigin") sets its own FOV. The sky
  portal 0x300260A0 ("CG_DrawSkyBoxPortal" errors) recomputes both from
  0x30025200 the same way.
- Hor+: replace 0x30025200: the lifted value, then, when the screen
  aspect A = vidWidth / vidHeight (cgame's glconfig) is above 4/3,
  fov' = 2 atan(tan(fov / 2) * A / (4/3)). Both callers derive fov_y from
  it, so the vertical FOV is what 4:3 gets at the same cg_fov, for hip,
  ADS and the 55 case alike. At 4:3 and below nothing changes. cg_fov 80
  gives 96.4 at 16:9, 111.5 at 21:9.
- Mouse feel: wrap 0x30025C80 and, when 0x30025200 ran inside it, write
  0x301257AC = 4:3 fov / cg_fov again (otherwise the sensitivity grows by
  fov'/fov).
- The viewmodel is drawn in the same scene and projection (only
  r_znear_depthhack differs; there is no gun FOV, cg_gun_* are offsets),
  so Hor+ keeps its vertical framing. Check the arm's cut ends at 21:9.
- Scope overlay 0x3000F770: four mirrored quarters of the weapon's
  adsOverlayShader (weapon def word 0x89) of adsOverlayWidth/Height
  (0x8B/0x8C) times the screen scales, centred on the aim point from
  0x3000F500, then black fills from it to the refdef edges in real
  pixels. 0x3000F500 (also used by the crosshair/reticle code 0x3000FF00,
  0x30010670, 0x300109A0) turns a view direction into a 640x480 offset
  with the refdef's fov_x/fov_y, so it follows Hor+ by itself; see the 2D
  placement for the overlay. 0x3000FF00 also reads fov_x/fov_y directly
  (0x3001043A, 0x30010166): check the crosshair spread under Hor+.

### 2D placement (menus, HUD)
- ui: 0x4000C1E0 (fetches the glconfig, fills the display-context table)
  sets yscale 0x401C045C = H/480, xscale 0x401C0460 = W/640 and a wide
  bias 0x401C0464 = (W - H*4/3)/2 that nothing reads. The draw helpers
  multiply by the scales inline (0x40007590, 0x40007660, 0x400076D0,
  0x40007770, 0x40007BF0, 0x40007C90; 0x40007D80 picks a font by
  yscale) and draw through 0x40018370 (DrawStretchPic syscall).
- cgame: 0x3001AF80 sets screenXScale 0x300FE9A0 = W/640 and
  screenYScale 0x300FE9A4 = H/480; some 20 functions read them inline
  (0x30012040, 0x300120E0, 0x30012200, 0x30012260, 0x3001CA20,
  0x3001DFA0, 0x3000F770, 0x3000FF00, ...) and draw through 0x30022DD0
  (syscall 0x4B) and the text syscall 0x38 (0x30022B40). There is no
  single adjust function to patch.
- Engine: 0x40FED0 has the shape of SCR_AdjustFrom640 (client glconfig
  copy); CIN_DrawCinematic-like 0x4076F0 uses it for the RoQ rectangle
  and its letterbox bars, then DrawStretchRaw.
- Renderer: the refexport is filled by 0x4A94C0 (REF_API_VERSION 14)
  and copied to the client's table at 0x1426D60 by 0x40E460. 2D enters
  through SetColor 0x4D42A0 (command 1), StretchPic 0x4D4330 (command 2:
  shader, x, y, w, h, s1, t1, s2, t2), 0x4D43D0 (command 3: the same plus
  a colour and one more word), 0x4D44D0 (0x2C-byte command), 0x4D45B0
  (0x48 bytes: four vertices; cgame's rotated quads 0x30012260, syscall
  0x4E), text 0x4D6400 (command 6: x, y, font, scale, colour, ...,
  string; also via 0x4D5D60, 0x4D6110, 0x4D6250) and StretchRaw 0x4CCD10
  (cinematics, drawn at once). The command loop 0x4CEA70 switches on ids
  1-11; the 2D setup 0x4CCC10 is an ortho of 0..vidWidth x 0..vidHeight,
  so all 2D arrives in pixels. Text has one uniform scale (0x4CDCC0),
  which is why glyphs are not stretched.
- The HUD is ui/hud.menu (localized_english_pak0) painted by cgame's
  menu code: 0x3003D3E0 ("fps: %f", Menu_PaintAll-like) calls
  0x3003B710 (Menu_Paint-like: menu in ESI, forcePaint in EAX, flags at
  +0x48 with visible = 4, fullscreen menus fill 0,0,640,480). Menus:
  "Compass" (x -25..135, y 345), "stance" and "tankstatus" (100, 434),
  "weaponinfo" (0..640 at y 420; weapon name and ammo at x 242-637),
  "Health" (488, 460), "Cursorhints" (300, 325). Outside menus: crosshair
  and reticles, the scope overlay, script hudelems, centre prints and
  subtitles (cg_subtitles), the damage direction. Game messages
  ("Objectives updated.", con_gamemessagetime) are the engine console's
  and already sit at the screen's left edge.

Design: one placement transform in pixel space where 2D enters the
renderer. With A = W/H > 4/3, k = (4/3)/A and b = W(1-k)/2 (identity
when A <= 4/3):
- stretch: unchanged (the original).
- center: x' = kx + b, w' = kw; a rectangle spanning the full width
  stays full width (fades, letterbox bars, full-screen hudelem shaders).
- left: x' = kx. right: x' = kx + 2b.
- scope: center, and rectangles touching the left or right screen edge
  are extended to it (the overlay's black fills).
Text moves x only (its scale is already uniform); the four-vertex quads
move each vertex; StretchRaw is a rectangle. Each 2D entry above becomes
a wrapper that rewrites its x/w arguments in place and calls the lifted
body. The mode comes from a save/restore stack set by wrappers of:
- uix86 vmMain: center (pillarboxed menus and loading screens).
- cgamex86 vmMain: center.
- Menu_Paint 0x3003B710: by menu name (window name pointer; offset to
  confirm, +0x20 in Q3TA's layout): Compass, stance, tankstatus left;
  weaponinfo, Health right; others center.
- 0x3000F770: scope. 0x3000F500 divides its x output by k in center and
  scope mode, since the offset is computed with the real fov_x and the
  transform would shrink it.
- 0x4076F0: center (cinematics pillarboxed; their bars are full width).
- Everything the engine draws itself (console, game messages, warning
  icons): stretch.
The UI cursor moves in 640x480 space and is drawn through the same
transform, so it stays on the items. To check: right- or centre-aligned
text shifts by w(1 - k) (positions move, glyph widths do not); whether
script hudelems at the screen edges need anchoring.

### Frame rate
- Com_Frame-like 0x430690 (it holds the error longjmp's setjmp): minMsec
  = 1000 / com_maxfps (integer; com_maxfps "0" means 1 ms), then it spins
  on 0x42F180 (event loop, timeGetTime ms) until that much has passed.
  No sleep: a cap burns a core, and only 1000/n rates exist (120 gives
  125, 144 gives 166, 240 gives 250). Then 0x430620 (fixedtime,
  timescale, clamp), the server frame 0x44CF10 and the client frame
  0x40DF80. r_swapInterval 1 reaches SDL_GPU as VSYNC, 0 as mailbox
  (x86recomp 32f0634).
- The server frame runs whole 50 ms game frames (20 Hz) from an
  accumulator (0x14F484C): AI, scripts, entities and snapshots do not
  depend on the frame rate.
- Player movement does. ClientThink-like 0x20012430: pmove_msec
  (0x20294C80, "8", clamped to 8..33) and pmove_fixed (0x202BD460, "0";
  when set, the command time is rounded up to a multiple of pmove_msec).
  Pmove-like 0x20008580 cuts each command into steps of at most 66 ms
  (pmove_msec when fixed) for 0x20007FD0 (msec clamped to 1..200,
  frametime msec * 0.001). Without pmove_fixed each frame's usercmd is
  one step of the frame's integer msec. Quake 3's per-step velocity
  snapping is not at the end of 0x20007FD0 (CoD recomputes the velocity
  there from the distance moved when blocked), so how much jump height
  and speed change with fps is unmeasured. cgame mirrors it (pmove_fixed
  0x301E6DC0, pmove_msec 0x301034E0).
- Prediction scales with fps: 0x3001EF70 (CG_PredictPlayerState-like)
  replays every usercmd newer than the latest snapshot (up to 64)
  through Pmove 0x30007C30 each frame. Snapshots come at 20 Hz, so a
  frame replays about fps x (time since the snapshot) commands: roughly
  2-3 at 60 fps, 7 at 240, and the per-second trace work grows about
  with fps squared. This is the cgame -> CM trace path that is ~45% of
  the main thread now.
- RoQ playback follows the audio clock (0x407040). Animations and
  effects use integer cg.time. Menu fades: Q3TA's Fade advances one step
  per frame when fadeCycle is shorter than a frame (hud.menu has
  fadeCycle 1), so fades would run faster at high fps; not yet located
  in CoD's code.
- Budget: the training walk averages 5.45 ms (p99 9.3 ms, run log
  2026-10-13), about 180 fps; 240 fps needs 4.17 ms.

Sound events lost at high fps (2026-10-13, macOS, measured with a
temporary trace; both are game logic and will happen on Linux too):

1. Footsteps stop when pmove steps are short. 0x200067C0 (cgame mirror
   0x30005F00) runs at the end of every pmove step and does Quake 3's
   PM_Footsteps arithmetic:
   - On the ground at xyspeed >= 10 it computes bobmove = xyspeed /
     (ps.speed (+0x44) x dir x move x stance) x K, then
     `bobCycle = _ftol2(bobCycle + bobmove * pml.msec) & 0xFF` (ps+8,
     pml.msec 0x20127048 / cgame 0x300D0528; 0x20084140 truncates).
     The old cycle is an integer, so a step adds floor(bobmove x msec).
   - 0x200065C0 (cgame 0x30005D00) emits the footstep event when
     `((old + 64) ^ (new + 64)) & 0x80`: every 128 cycle units.
   - Scales, stored at spawn (0x20014AB9-0x20014B17): dir is 1, strafe +0x344 0.8 or
     back +0x348 0.7 (diagonals blend them); move is +0x338 1.0, or
     +0x334 0.4 while pm_flags 0x80 (set by 0x20007980 on the ground,
     not prone, with pm_flags 0x20 and button 0x10); stance is prone
     +0x33C 0.15 or crouch +0x340 0.65 (blended during a stance change
     through 0x200053C0). At full speed the ratio is 1: a crawl moves at
     xyspeed 28.50 = 190 x 0.15.
   - K (cycle units per ms at full speed; with pm_flags 0x80 in brackets):
     stand 0.335 (0.305), backwards 0.36 (0.325), crouch 0.34 (0.315),
     prone 0.25.
   - Steps where K x msec < 1 add nothing, so there are no footsteps and
     no bob. The limits at full speed:

     | Stance | Silent at msec | Frame rate | Measured cycle units/ms at msec 1 / 2 / 3 / 4 / 8 / 16 / 17 |
     |---|---|---|---|
     | prone | <= 3, unreliable at 4 | > 250, unreliable > 200 | 0 / 0 / 0 / 0.186 / 0.149 / 0.248 / 0.235 |
     | crouch | <= 2 | > 333 | 0 / 0 / 0.333 / 0.25 / - / 0.312 / 0.294 |
     | stand, back | <= 2 | > 333 | 0 / 0 / 0.333 / 0.25 / 0.25 / 0.312 / 0.294 |
     | pm_flags 0x80 | <= 3 | > 250 | (not measured) |

     The prone ratio rounds a little under 1, so K x 4 and K x 8 land
     just under 1 and 2 and some steps floor to one unit less. That is
     why msec 4 gives 0.186, not 0.25, and why pmove_fixed's 8 ms steps
     give 0.149. While accelerating the ratio is below 1, so even longer
     steps add nothing until K x ratio x msec >= 1: a ratio of 0.25 is
     enough at 60 fps, but at msec 4 the crawl has to be at full speed.
     This explains why the start of a crawl is silent at high fps. Frame
     times vary, so at 300-500 fps only the occasional longer frame
     advances the cycle. Truncation also slows the cadence at normal
     rates: standing at 60 fps gives 0.29-0.31 units/ms instead of 0.335.
   - Fix (implemented: "Footsteps at any step length" at the top):
     replace 0x200067C0 and
     0x30005F00 with one native implementation. Add floor(bobmove x t1)
     - floor(bobmove x t0), where t1 = ps.commandTime (PmoveSingle sets it
     to the step's end before the call, 0x20007FD0) and t0 = t1 - msec.
     Server and prediction see the same command times, so they agree.
     For a constant bobmove the steps telescope to floor(bobmove x T1) -
     floor(bobmove x T0), the continuous cadence at any step length. When
     bobmove changes the error stays below one unit and is unbiased. The
     cadence is then K exactly, about 10% faster than the original at 60
     fps. Optionally scale bobmove to match a reference frame rate's cadence. The
     event call stays 0x200065C0 / 0x30005D00. Do not use a minimum step
     length instead: it delays the playerstate behind the commands, still
     truncates, and adds the problems in 2.

2. pmove_fixed loses server events on the player (training's sign
   beeps). `playsound` on an entity (0x20032A10) calls 0x20046610
   (G_AddEvent-like). For a client, that puts the event in the
   playerstate's predictable-event ring: events[seq & 3] (+0x78) =
   0xAC (0xAD with a notify), parms (+0x88) = alias index,
   eventSequence (+0x74)++. CoD has no externalEvent.
   - In cgame, 0x3001EF70 copies cg.snap's playerstate into the
     predicted one (0x3011E0B4), then replays each command whose
     serverTime is in (ps.commandTime, newest]. pmove_fixed rounds
     serverTime up to a multiple of pmove_msec only after that check.
     Only when at least one command ran does it compare the new state
     with the previous predicted one: damage 0x3001E510, 0x3001E460, and
     the event ring 0x3001E7C0, which fires every sequence number the old
     state lacks or whose event differs. If no command ran it returns.
     The snapshot's events are then already in the predicted state, and
     the next frame's comparison sees no difference: they are never
     played. Snapshot transitions (0x30022090) check events only when
     0x3010AC2C is set (no prediction; 0x3001EF70 then takes 0x3001ED60).
   - Server side, with pmove_fixed, ClientThink 0x20012430 rounds the
     command time up to the next multiple of 8, and Pmove runs to it.
     The snapshot's commandTime is therefore 0-7 ms ahead of the command
     the server last ran. Commands inside that bucket are ignored on the
     server (msec < 1 returns) and skipped by prediction.
   - A snapshot is used on the frame after the server built it, and the
     newest command is one frame newer than the one the server last ran
     (measured: the gap is the frame time, 3-5 ms at 270-420 fps, 16-17
     at 60). Without pmove_fixed that command always replays: 4699 of
     4700 fresh snapshots ran one, and the exception was at load. With
     pmove_fixed it replays only if the frame crossed the 8 ms boundary,
     so the chance of losing the snapshot's events is about 1 - frame
     ms / 8. Measured fresh snapshots with no replay: 102 of 920 at
     ~120 fps (mixed frame times), 1157 of 1611 at 500-850 fps.
     Piloted, 500-850 fps: all 3 sign events (0xAC, parm 54) arrived on
     such snapshots. At 60 fps the gap (16 ms) always crosses, which is
     why the beeps play there.
   - Lost the same way: damage feedback (+0xD4..+0xE0) and any other
     server-added player event. Predicted events (footsteps, weapon)
     are unaffected, because cgame generates them itself.
   - Fix options. (a) A 0x3001EF70 wrapper: when no command ran, put the
     previous predicted ring back (+0x74..+0x97), along with the damage
     count +0xD4 and +0x98. +0x98 is the sequence 0x300019A0
     (playerstate-to-entity-state) has already turned into entity
     events. 0x300019A0 loops `for (i = +0x98; i != eventSequence; i++)`,
     so restoring the ring without +0x98 hangs it for 2^32 iterations
     (a piloted test did exactly that: one 10 s stall per sign). With
     the fix, the events fire on the next frame that replays, at most
     8 ms later. Corrected version not yet piloted. (b) Fixed steps
     that do not round the command time up: run whole pmove_msec steps
     and leave the remainder pending, so ps.commandTime <= the newest
     command time and prediction always runs a command. Needs the view
     interpolation below. Chosen and implemented: see "pmove_fixed
     without rounding" at the top. A button state shorter than a step
     can still be missed, as with pmove_fixed: a step uses the command
     that completes it.

Design:
- Pacing: wrap 0x430690 so that it first sleeps until the next frame
  deadline on a host high-resolution clock (period 1/com_maxfps, not
  rounded; sleep to ~1 ms before it, then a precise wait such as SDL3's
  SDL_DelayPrecise), then runs the lifted body. Its own integer wait is
  then already over: no spinning, and 144/165/240 are exact. Check that
  the lifted body still catches the longjmp (manifest `setjmp`) when it
  is f_00430690_lifted. For VRR: r_swapInterval 0 and com_maxfps a few
  under the refresh rate; fixed refresh: r_swapInterval 1, com_maxfps 0.
- Measure before changing physics: scripted jumps and runs at com_maxfps
  60/125/250/333/500 with pmove_fixed 0 and 1 (apex and speed from a
  probe on the playerstate origin), menu fade durations, gun sway/bob at
  60 vs 240.
- If movement depends on fps: fixed pmove steps on both sides plus
  interpolating the predicted view origin between the last two steps in
  cgame, because fixed steps alone move the view at 125 Hz. Not
  pmove_fixed as it was: it loses player events and still truncates the
  prone bob (sound events, 2 above). Done for pmove_fixed 1 (remainder
  pending, see the top section); the bob (1) too.
- Prediction: keep the predicted playerstate per command number and run
  only the new commands while the snapshot is unchanged (a full replay
  on each new snapshot): a native 0x3001EF70 with an oracle test against
  the full replay. Overlaps the collision-trace work.

### Implementation order
| # | Item | Where | Size |
|---|---|---|---|
| 1 | Hor+: replace 0x30025200, wrap 0x30025C80 (zoom sensitivity); check 4:3 unchanged, 16:9/21:9 screenshots | CoD1 | S, ~60 lines |
| 2 | Display modes: desktop and wide modes, host fullscreen for full-screen popups, aspect-kept present; desktop-size accessor | x86recomp | M, ~150 lines; DS1 retest |
| 3 | Native resolution: replace 0x4A5610 (r_mode -1 with 0x0 = desktop) | CoD1 | S, ~30 lines |
| 4 | Frame pacing wrapper on 0x430690 | CoD1 | S, ~60 lines |
| 5 | 2D transform: the renderer 2D entries, mode stack, ui/cgame vmMain, 0x4076F0 | CoD1 | M, ~250 lines |
| 6 | HUD anchors: Menu_Paint by name, scope overlay, 0x3000F500 | CoD1 | S-M, ~100 lines + screenshot checks |
| 7 | Measurements: pmove vs fps, fades, sway | scripted runs | S |
| 8 | Prediction cache for 0x3001EF70 + oracle test | CoD1 | L, ~300 lines + test |
| 9 | Fixed-step pmove with view interpolation (done for pmove_fixed 1; default still 0) | CoD1 | M-L |
| 10 | Time-based menu fades, if 7 shows the need | CoD1 | S |

FOV first (small, self-contained, the biggest visible gain), then the
mode work so widescreen can be fullscreen, pacing (cheap), then the 2D
placement (the stretched HUD is the most visible remaining fault). The
measurements decide 9 and 10; 8 is what 240 fps needs in CPU time.

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

## Miles 6.5e
The game ships Miles 6.5e (mss32.dll, miles/msssoft.m3d). Their digital
master reverb, sample levels and Fast 2D are written up in x86recomp's
docs/miles.md (addresses, constants) and implemented there bit for bit;
the oracle tests take the DLLs from -DCOD1_MSS32_DLL. What the game
does with them:
- It never changes the driver's master volume (1.0), Fast 2D's "Mute at
  maximum" (on) or rolloff factor (1.0), and makes no velocity calls
  (no Doppler).
- Rooms and wet levels: see the digital reverb run-log entry.

## Run log
### 2026-10-19: pk3 directory scan and tolower native, measured
- Training's level load as in the entries below, five runs each,
  interleaved, against the jpeg-idct-float build (7a4774e; another
  session's single-threaded test was running at times): 1.619 s
  (1.614-1.623) -> 1.277 s (1.275-1.279), -21%. Launch to "Server
  Initialization": 1.18-1.20 -> 0.95-0.98 s. CL_InitCGame 0.62 ->
  0.57 s. Console output identical apart from times.
- Since main 741c785 (zlib, the IDCT, the directory scan and tolower):
  training's load 2.18 -> 1.28 s.
- Main thread over the load (perf --call-graph dwarf): unzReadCurrentFile
  34.3% (inflate 30.9%, crc32 3.1%), FS_LoadZipFile 5.8% (the directory
  read 5.2%), LoadJPG 2.1% (jpeg_idct_float 1.0%), FS_HashFileName
  1.2%, tolower 0.2%.

### 2026-10-19: jpeg_idct_float native, measured
- Training's level load, as in the zlib entry below, five runs each,
  interleaved. Another session's single-threaded test was running; the
  zlib-only build measured ~0.07 s slower than in that entry. Zlib-only
  build (host-zlib-jpeg 0dae7aa) 1.914 s (1.888-1.943) -> 1.676 s
  (1.648-1.739), -12.4%. CL_InitCGame 0.84 -> 0.65 s. Console output
  identical apart from times.
- Since main 741c785 (zlib and the IDCT): about 2.18 -> 1.67 s.

### 2026-10-19: pk3 reads and savegames on host zlib, measured
- Level load: training from `+map training`, "------ Server
  Initialization ------" to the last loading frame (over 20 ms) before
  "Click to Start the Mission", from timestamped console lines;
  uncapped, windowed, isolated XDG_DATA_HOME, systemd-run MemoryMax=6G,
  five runs each, interleaved. Base main 741c785, native this branch,
  both on x86recomp main 4735305:
  - Load: 2.181 s (2.178-2.184) -> 1.842 s (1.836-1.845), -15.5%.
  - CL_InitCGame: 1.02 -> 0.81 s. Launch to "Server Initialization":
    1.26-1.33 -> 1.20-1.24 s.
  - Console output otherwise identical.
- Hitches: the mission start's first frame 13.7-14.0 ms on both, the
  autosave frame 5.2 ms on both. A 76 s walk in training (forward held
  with turns): mean frame 1.94 ms on both, max 8.3 / 7.7 ms, no frame
  over 16 ms on either: no in-level loading hitches.
- Main thread over the native load (perf --call-graph dwarf, 999 Hz),
  inclusive: unzReadCurrentFile 22.5% (host inflate 20.5%, crc32 1.7%);
  FS_LoadZipFile 0x4237A0 14.7% (the pk3 directory scan that
  FS_Startup repeats at each map load, about 0.29 s: unzip's central
  directory read field by field through the CRT's fread, 10.5%);
  LoadJPG 13.7% (jpeg_idct_float 12.5%); the file-name hash 0x421980
  8.4%, through the CRT's locale-aware tolower 0x51B3DD (10.2% with
  other callers), which asks __getptd for the thread's locale each
  time.

### 2026-10-19: native collision traces, measured
- The scripted training walk ("Run log" 2026-10-18), uncapped, windowed,
  isolated XDG_DATA_HOME, systemd-run MemoryMax=6G; frames 12-88 s from
  the COD1_HITCH log. Base: origin/main dc5a30a; native: 8b3f006; both
  on x86recomp main 4735305. Three runs each, interleaved, idle machine,
  repeatable to 0.01 ms:
  - Mean frame: 2.85 -> 2.03 ms (-29%).
  - p95: 3.7 -> 2.3 ms. p99: 3.8 -> 2.35 ms. Max: 4.4-13.0 -> 3.3 ms.
  - Frames in the window: 26700 -> 37450.
  - The same walk with turns and jumps (cg_showmiss 1, one run each):
    2.25 -> 1.33 ms; no prediction misses on either.
- Main thread, perf --call-graph dwarf: the collision traces 32.8% ->
  6.6% (CM_TransformedBoxTrace 3.9%, CM_BoxTrace 2.4%, CM_Trace from the
  server 0.3%); cgame's 0x3001EF70 35.2% -> 18.0%. GL draw
  (DrawElements, 24.9%) leads now; next in the CM range is point
  contents (0x41A730, 2.9%, still lifted).

### 2026-10-18: collision traces still lead the main thread
- Profiled before replacing them (x86recomp main 4735305, x87 on host
  doubles; CoD1 main dc5a30a): the scripted training walk, uncapped,
  windowed, isolated XDG_DATA_HOME, COD1_TEST_INPUT (clicks at 8 and
  10 s, F7 "vstr go" = +forward at 14 s, F8 quit at 90 s), under
  systemd-run MemoryMax=6G; perf over 12-88 s. A LibreOffice build was
  using about 9 of the 16 cores meanwhile: shares, not times.
- Main thread, perf --call-graph dwarf (250 Hz): the collision code is
  32.8% inclusive (31.1% self in 0x419000-0x41DFFF), still the largest
  block; GL draw (DrawElements) is 19.4% inclusive.
  - cgame prediction through the syscall dispatcher 0x401A50:
    CM_TransformedBoxTrace 0x41D870 17.2% (from 0x3001E960) and
    CM_BoxTrace 0x41D380 11.7% (from 0x3001EC30); 0x3001EF70 is 35.2%
    inclusive. The server's 0x44E6C0 calls CM_Trace directly: 1.9%.
  - Self: CM_TraceThroughBrush 0x41B8F0 8.9%, CM_Trace 0x41CD60 7.5%,
    terrain 0x4196F0 4.5%, 0x41D3B0 4.0%, CM_TraceThroughLeaf 0x41C0A0
    2.1%, point contents 0x41A730 1.3%, CM_TraceThroughTree 0x41CA60
    0.8%. Inclusive: CM_TraceThroughLeaf 17.2%, CM_TraceThroughTree
    13.1%, CM_TraceThroughPatch 5.7% (terrain 4.8%).
- The x87 CW is 0x027F on the main thread (fldcw only in the CRT:
  __control87 0x52B587, __ctrlfp 0x5253D2, _ftol-style chop/restore;
  _controlfp 0x52B5B9 has one caller, 0x528FE1, which sets _PC_53). The
  x87 exception flags are read only by the CRT's math-error path
  (__raise_exc 0x5249E8 via _statusfp 0x5253B7).

### 2026-10-13: sound events lost at high fps (macOS, main 8248a19)
- Results are in "Frame rate", "Sound events lost at high fps". Method:
  temporary `replace` wrappers (not committed) on 0x200067C0, logging
  each step's msec, pm_flags, xyspeed and bobCycle, and on 0x3001EF70,
  logging per fresh snapshot its commandTime, the newest command time,
  whether a command replayed, and the new ring events.
- Scripted runs: training, windowed, r_swapInterval 0, com_maxfps 0,
  +forward/+back by vstr binds, goprone/gocrouch/+gostand. Background
  runs ran at 270-420 fps (one dropped to 60 fps when it was occluded).
  `fixedtime` is cheat-protected, so it cannot force msec. Piloted run:
  pmove_fixed 1, 500-850 fps, signs read.
### 2026-10-13: x87 runs on host doubles (x86recomp x87-double 79a8931)
- Under CW 0x027F, x86recomp runs x87 runs on host doubles where that
  is proven exact. Values must stay in [2^-511, 2^512); PE and C1 come
  from exact error terms. Anything else leaves for the exact model at
  that instruction. The proof is in x86recomp ARCHITECTURE.md.
- Scripted training walk, against x86recomp main 7e0dc70 built from
  the same tree. Uncapped, windowed (r_swapInterval 0, r_fullscreen
  0), isolated XDG_DATA_HOME, COD1_TEST_INPUT with F7 bound to
  "vstr go" (+forward) and F8 to quit. In-game frames 12-88 s, three
  runs each, interleaved, repeatable to 0.02 ms:
  - Mean frame: 4.50 -> 3.53 ms (-21.6%).
  - p95: 7.3 -> 5.9 ms. p99: 8.0 -> 6.35 ms. Max: ~16 -> 10.5 ms.
  - Frames in the window: 16890 -> 21560.
  - COD1_X87_DOUBLE=0 on the new build: 4.50 ms, the same as the base.
  - The base here is 4.50 ms, not 4.77 as in the 7e0dc70 entry, because
    of windowed mode and the config. Compare within one entry only.
- Lifted code: 97 -> 113 MB. The double form of each run is emitted
  next to the exact one. A full build took ~6 min.
- ctest 12/12 with COD1_GAME_EXE and COD1_MSS32_DLL. That includes
  lift_x87 (4110 forms, 82.2M checks against the CPU, with more
  double-exact states under 0x027F), skinning_oracle,
  digital_reverb_oracle and fast2d_oracle.
- Memory, measured while checking a report of "18 GB": VmSize 15.8 GB
  and VmRSS 19.1 GB on both builds. RssAnon is only ~0.2 GB.
  nvidia-smi gives the process 248 MiB of VRAM. The rest is the
  NVIDIA Vulkan driver's 8.9 GB /dev/nvidiactl mapping, which the
  kernel counts as resident (as file and as shmem). It is not an
  allocation by the port.

### 2026-10-13: faster exact x87 (x86recomp x87-fast 7e0dc70)
- The game runs its float maths at the CRT's PC=53/RC=nearest (CW
  0x027F). The only _controlfp call, 0x528FE1, sets _PC_53, and the
  GL HLE never touches the CW.
- Lifted runs had only the PC=24 arithmetic inline, so every fadd,
  fmul, fcom and rounding fst went out of line. In the scripted
  training walk the out-of-line x87 helpers were 57.7% of the main
  thread: run::ArithF32 alone 33%, run::Arith 8%, compares 9%.
- x86recomp now has these inline and exact (integer only):
  - PC=53 add/sub/mul/div of up to 53-bit significands;
  - zero operands;
  - m32/m64 loads, rounding stores and compares;
  - single-instruction runs;
  - an empty mask instead of the tag word;
  - a control-word layout that no longer misses store forwarding.
- Scripted training walk: the uncapped walk with COD1_TEST_INPUT, an
  isolated XDG_DATA_HOME, and F8 bound to quit. Measured over the
  in-game frames, 12-88 s, three runs of the final build and four of
  the baseline, repeatable to 0.02 ms:
  - Mean frame: 5.40 -> 4.77 ms (-11.7%).
  - p95: 8.5 -> 7.3 ms. p99: 9.4 -> 8.0 ms.
  - Frames in the window: 14070 -> 15930.
- Main thread, perf:
  - Out-of-line x87 helpers: 57.7% -> 1.1%.
  - The collision code (0x419000-0x41DFFF, now with the x87 work
    inline) is 60% of the main thread. Before, its self time (19%)
    plus the helpers was about 77%.
- What is left is the latency of exact integer arithmetic, about 5 ns
  per dependent operation. Host doubles would be about 5x faster on
  this code but need a change to x86recomp's rules.
- ctest 12/12 with COD1_GAME_EXE and COD1_MSS32_DLL. That includes
  lift_x87 (4110 forms, 82.2M checks against the CPU), skinning_oracle,
  digital_reverb_oracle and fast2d_oracle.

### 2026-10-13: SDL_GPU (Vulkan) is the default backend
- x86recomp 32f0634: SDL_GPU on Vulkan by default; dxvk-native optional
  (COD1_GFX=d3d9). ctest 12/12 (oracles included) on it.
- Same scripted runs on both backends (COD1_SHOT_EVERY): the training
  briefing is identical (0 pixels over 2% fuzz); in-level frames look
  the same, 35 dB PSNR at equal frame numbers from animation timing
  (the NPC and trees move). Same 15 loading hitches either way.

### 2026-10-13: native CPU skinning (src/skinning.cpp)
- Frame drops in training were CPU-bound on the main thread; after
  x86recomp's GL draw path speed-up (branch gl-draw-perf) the x87/SSE
  model was 72-84% of it, and in the user's heaviest stretch the two SSE
  skinning functions alone were 59%: 0x507D20 (weighted) 43% and
  0x5070E0 (rigid) 16%, packed SSE through the SoftFloat lanes.
- Both are slots of the surface function table at 0x564270 (5: rigid,
  10: weighted; the other slots include the non-SSE versions), called
  through it from 0x4CA987, 0x4CC6EC and 0x4CCB06. Layouts (arg block,
  surface, vertices, extra bones, matrices) are in src/skinning.cpp
  and in the Ghidra plate comments. Each: RB_CheckOverflow (0x500A60,
  named from its error strings), indexes appended by 0x480820 (+ first
  vertex), texcoords by 0x42B0A0 (a memcpy with the structure of Quake
  3's Com_Memcpy, left unnamed), then per vertex position and normal
  into the tessellator, stored with 16-byte movups at a 12-byte stride.
  The normal of a weighted vertex takes the first bone only.
- Replaced natively (manifest `replace 005070E0` / `replace 00507D20`):
  same per-lane IEEE single operations in the same order (the two
  functions sum the translation row at different points), compiled
  with -ffp-contract=off; a vertex with a NaN result is redone with
  SSE's NaN rules (first NaN operand quietened, else 0xFFC00000), which
  host arithmetic does not follow. The lifted callees are called as the
  original's call sites do (same frame depth and return addresses), and
  the argument slot gets the original's last value. Not reproduced:
  MXCSR's sticky flags (no ldmxcsr in any module; the only stmxcsr, in
  the CRT's SSE2 math entries at 0x51A7A0, 0x51AB30, 0x52E2D0 and in
  cgame/ui, mask with 0x1F80), scratch/XMM registers and EFLAGS (all
  three call sites overwrite them first).
- ctest skinning_oracle (configure with -DCOD1_GAME_EXE): the originals
  lifted with --only and run on the SSE model against the native code,
  memory and callee-saved registers compared byte for byte: 6000 random
  surfaces (zeros, denormals, infinities, quiet/signalling NaNs, overflow
  and underflow, raw bits; 245733 NaN outputs) plus captured game calls
  (-DCOD1_SKINNING_CAPTURE=ON build, COD1_SKINNING_CAPTURE=file): 3703
  calls from a training walk, 0 mismatches. Mutations (NaN operand
  order, default NaN sign, add order, argument slot, NaN redo, 12-byte
  store) each fail it. `--only` leaves out noreturn callees, so the
  test lists them too.
- Scripted training walk (uncapped, gl-draw-perf x86recomp, 3 runs each,
  repeatable to 0.1 ms): skinning was 29.5% of the main thread in the
  15-30 s stretch (12.1% over the walk), now 0.4%; that stretch's
  frame time 7.0-8.5 -> 5.9-6.4 ms, walk mean 5.86 -> 5.45 ms, p95 9.4
  -> 8.6 ms, p99 10.5 -> 9.3 ms. The SSE model is now ~0% of the main
  thread; the x87 model is still 60%, mostly collision traces (0x41B8F0
  14%, 0x41CD60 13%, 0x41D3B0 7%, 0x419EE0 6%, 0x4196F0 4%).
- The user's manual training session (perf 300 Hz, COD1_HITCH=25, own
  config: vsync 60 Hz, ~130 s in game) against the same kind of session
  on the gl-draw-perf build before it: main thread busy 72% -> 60% of
  the in-game time; the stretch that was saturated for ~25 s (98-100%
  busy at 20-48 s, skinning up to 69% of it) has no counterpart: the
  busiest 5 s is now 95%, most 50-80%. Skinning 33.8% -> 0.6%, SSE
  model 33.3% -> ~0%. No frame over 25 ms once in game (before: one of
  31.5 ms at mission start). What fills the main thread now: x87 model
  59% (leaf), collision traces from cgame (0x3001EF70 > 0x30007C30 >
  CM 0x41CD60 / 0x41C0A0) ~45% (self: 0x41B8F0 18%, 0x4196F0 15%,
  0x41CD60 7%, 0x41D3B0 5%), GL draw 6.5%.

### 2026-10-12: snd_restart and the mouse; in-game restarts crash
- snd_restart (the sound options' apply, e.g. picking another 3D
  provider) also restarts the renderer: DestroyWindow of the game window
  and a new one at the same place, without ClipCursor(NULL) first. The
  game then never clips again (its mouse is still "active"), so the new
  window got no grab: mouse look drifted and the pointer escaped. Fixed
  in x86recomp (a clip outlives its window, as on Windows). Checked
  with a scripted menu snd_restart then training: relative motion turns
  the view as before the restart.
- From inside a level the restart is "savegame internal\snd_restart"
  (same for vid_restart); once the save is written ("Game saved for
  snd_restart") the game queues "disconnect;snd_restart;loadgame
  internal\snd_restart" itself. The disconnect is an error drop
  ("ERROR: Disconnected"), which ends in the CRT's longjmp (jmp at
  0x51CAB1) back to 0x4306A6 in 0x430690: was fatal, unlifted indirect
  target.
- Fixed with x86recomp b66a6ec (setjmp/longjmp; manifest `setjmp
  0052E84C` for __setjmp3, `resume 0051CAB1` on _longjmp's jmp): its
  two callers, 0x42FDE0 and 0x430690, catch the drop and continue at
  their setjmp return points 0x42FE16 / 0x4306A6. Checked with a
  scripted F11 bound to "savegame internal\snd_restart" in training:
  disconnect, Miles re-init, training reloads from the internal save,
  and after the briefing click the level plays on. Binding the whole
  line instead disconnects before the save is written (the save
  completes after the rest of the line has run), so its loadgame finds
  no save: an artefact of that test bind, not of the menu.
- cod1 now quotes game arguments with spaces (`-- +set mss_3d_provider
  "Creative Labs EAX 3 (TM)"` reached the game as "Creative").

### 2026-10-12: Miles 6.5e Fast 2D and sample levels (x86recomp 0acb1ee)
- x86recomp's runtime/audio/miles65 implements docs/miles.md's "Miles
  6.5e sample levels and Fast 2D" for 2D samples, streams and Fast
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
  x86recomp's docs/miles.md; its runtime/audio/digital_reverb implements
  that description and matches the original bit for bit (ctest digital_reverb_oracle, which lifts the
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
