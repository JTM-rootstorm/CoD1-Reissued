// The native player movement (src/pmove.cpp) against the original Pmove of
// gamex86.dll and cgamex86.dll, lifted from the DLLs at build time with
// `recomp_lift --only` (CMakeLists.txt) and run on x86recomp's exact x87
// model (SoftFloat, not its host-double runs). Both DLLs are mapped as the
// game maps them, for their constants, tables and globals.
//
//   pmove_oracle_test GAME_DIR [CAPTURE...]
//
// Each case runs Pmove (ESI pm) through the lifted original and the native
// code from the same guest memory, and both must leave the same bytes in
// pmove_t, playerState_t, the module's pmove globals (pml, the pmove
// pointer and count, the weapon debug state) and the stack above ESP, the
// same ESP, EBX, ESI, EDI and EBP, x87 stack and control word, and make the
// same trace and point contents calls. The reference is the lifted code as
// the game ran it before: with PM_Footsteps native (an intended change,
// src/pmove.cpp) and, for pmove_fixed, the earlier wrapper that ran the
// original Pmove on whole steps (its copy is below, PmoveFixedReference).
// Not compared: EAX, ECX, EDX, EFLAGS, the x87 status word, the stack below
// ESP.
//
// The traces and point contents go to a world of brushes here: a floor,
// steps, walls, a ramp, a ladder, water, bodies and corpses of other
// players, with surface flags (slick, ladder, materials, no fall damage),
// swept as boxes. Player states and commands are random: every pm_type,
// stance, the flags pmove reads, weapons in every state with random
// weapon definitions, view heights mid-change, ladders, water, debug
// output now and then (the prints and the game's prone debug drawing go to
// stubs), and in a share of the cases special floats (zeros, denormals,
// infinities, NaNs, huge values) in the state.

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "pmove_capture.h"
#include "runtime/guest_memory.h"
#include "runtime/lifted.h"
#include "runtime/pe_image.h"
#include "runtime/x87.h"

namespace {
void TraceCall(recomp::Ctx& c, int which);
void PointContentsCall(recomp::Ctx& c);
}  // namespace

namespace recomp {
uint8_t* g_guest_base = nullptr;

namespace gen {
void f_20008580_lifted(Ctx& c);
void f_30007C30_lifted(Ctx& c);
void f_20006FD0(Ctx& c);
void f_30006710(Ctx& c);
void f_20078B38(Ctx& c);  // the CRT's float formatting (_cfltcvt_init)
void f_300407BC(Ctx& c);
// The native code (tests/pmove_native.cpp renames its entries)...
void n_20008580(Ctx& c);
void n_30007C30(Ctx& c);
void n_200067C0(Ctx& c);
void n_30005F00(Ctx& c);
// ...whose PM_Footsteps the lifted originals call, as in the game.
void f_200067C0(Ctx& c) { n_200067C0(c); }
void f_30005F00(Ctx& c) { n_30005F00(c); }
void f_20008580(Ctx& c) { n_20008580(c); }
void f_30007C30(Ctx& c) { n_30007C30(c); }
// The CRT's _exit (after a fatal runtime error), not lifted on its own.
void f_20078D58(Ctx&) {
  std::printf("_exit\n");
  std::abort();
}
void f_30041EAC(Ctx&) {
  std::printf("_exit\n");
  std::abort();
}

// pmove_fixed's state (src/pmove_fixed.h), set by the test.
bool g_test_fixed = false;
int32_t g_test_step = 8;
bool PmoveFixedOn(bool) { return g_test_fixed; }
int32_t PmoveFixedStep(bool) { return g_test_step; }
void PmoveFixedStepEnd(int32_t, uint32_t, uint32_t, uint32_t) {}
}  // namespace gen

PeImage* g_images[2] = {};
constexpr uint32_t kPtd = 0x6DE00000;
uint32_t g_last_error = 0;

// The trace and point contents functions and the syscall pointer of the
// game, at addresses of their own.
constexpr uint32_t kTraceFns[3] = {0x6C000000, 0x6C000010, 0x6C000020};
constexpr uint32_t kPointContentsFn = 0x6C000030, kSyscallFn = 0x6C000040;

bool Kernel32(Ctx& c, const std::string& name) {
  auto arg = [&](int n) { return *reinterpret_cast<uint32_t*>(g_guest_base + c.esp + 4 + 4 * n); };
  if (name == "GetLastError") {
    c.eax = g_last_error;
    c.esp += 4;
  } else if (name == "SetLastError") {
    g_last_error = arg(0);
    c.esp += 8;
  } else if (name == "TlsGetValue") {
    c.eax = kPtd;
    c.esp += 8;
  } else {
    return false;
  }
  return true;
}

void Dispatch(Ctx& c, uint32_t target, uint32_t va) {
  for (int i = 0; i < 3; i++)
    if (target == kTraceFns[i]) return TraceCall(c, i);
  if (target == kPointContentsFn) return PointContentsCall(c);
  if (target == kSyscallFn) {
    c.eax = 0;
    c.esp += 4;
    return;
  }
  if (target - kThunkBase < kThunkRegionSize)
    for (PeImage* image : g_images)
      for (const PeImport& i : image->imports)
        if (i.thunk_va == target) {
          if (i.dll == "KERNEL32.dll" && Kernel32(c, i.name)) return;
          std::printf("unexpected call to %s!%s from %08X\n", i.dll.c_str(), i.name.c_str(), va);
          std::abort();
        }
  const LiftedFn* lo = kLiftedFunctions;
  const LiftedFn* hi = kLiftedFunctions + kLiftedFunctionCount;
  const LiftedFn* f =
      std::lower_bound(lo, hi, target, [](const LiftedFn& a, uint32_t t) { return a.va < t; });
  if (f != hi && f->va == target) {
    f->fn(c);
    return;
  }
  std::printf("unexpected call to %08X from %08X\n", target, va);
  std::abort();
}
void Resume(Ctx&, GuestResume r) {
  std::printf("unexpected resume to %08X\n", r.target);
  std::abort();
}
void Unimplemented(Ctx&, uint32_t va, const char* what) {
  std::printf("unimplemented %08X %s\n", va, what);
  std::abort();
}
void BadJump(Ctx&, uint32_t from, uint32_t to) {
  std::printf("bad jump %08X -> %08X\n", from, to);
  std::abort();
}
void Unreachable(Ctx&, uint32_t va) {
  std::printf("unreachable %08X\n", va);
  std::abort();
}
void Trap(Ctx&, uint32_t va) {
  std::printf("trap %08X\n", va);
  std::abort();
}
void RaiseDivideError(Ctx&, uint32_t va) {
  std::printf("divide error %08X\n", va);
  std::abort();
}
void Cpuid(Ctx&) { std::abort(); }
uint64_t Rdtsc(Ctx&) { std::abort(); }
void Probe(Ctx&, uint32_t, const char*) {}
void FatalF(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::vprintf(fmt, ap);
  va_end(ap);
  std::abort();
}

}  // namespace recomp

namespace {

using recomp::Ctx;

recomp::GuestMemory g_mem;

uint32_t Rd(uint32_t va) { return g_mem.read<uint32_t>(va); }
void Wr(uint32_t va, uint32_t v) { g_mem.write<uint32_t>(va, v); }
float RdF(uint32_t va) { return g_mem.read<float>(va); }
void WrF(uint32_t va, float f) { g_mem.write<float>(va, f); }
void Wr8(uint32_t va, uint8_t v) { g_mem.write<uint8_t>(va, v); }

// Guest memory of the test: player states, weapon definitions; the stack.
// Above what captures hold (the game's heap and the DLLs).
constexpr uint32_t kArena = 0x6E000000, kArenaSize = 0x100000;
constexpr uint32_t kStack = 0x6D000000, kStackSize = 0x100000;
constexpr uint32_t kPm = kArena, kPs = kArena + 0x1000, kWeaponTable = kArena + 0x2000,
                   kWeaponDefs = kArena + 0x3000;
constexpr uint32_t kPmSize = 0x120, kPsSize = 0x400, kWeaponDefSize = 0x420;
constexpr int kNumWeapons = 10;

// What differs between the two modules for the test.
struct Module {
  const char* name;
  uint32_t lo, hi;  // the pmove globals (pml, pmove pointer and count, cvars)
  uint32_t debugLo, debugHi;  // the weapon debug state
  uint32_t numWeapons, weaponDefs, clipSizes;
  uint32_t proneTime, crouchTime, fallMax, fallMin, foliageMinSpeed, foliageMaxSpeed,
      foliageSlowInterval, foliageFastInterval, foliageReset, flyViewHeight, ladderYawCap,
      proneYawCap, proneYawCheck, noFire, debugWeaponState, debugWeaponAnim;
  uint32_t proneDebug, proneDebugTime;  // game only
  uint32_t syscall;
  recomp::GuestFn lifted, native, viewAngles;
};

const Module kModules[2] = {
    {"gamex86", 0x20125600, 0x20128000, 0x200A6F50, 0x200A6F58, 0x2012564C, 0x20125650,
     0x200A8E30, 0x2012766C, 0x20127AEC, 0x20126F08, 0x20127548, 0x20126BAC, 0x201279CC,
     0x2012730C, 0x20126DEC, 0x201271EC, 0x2012742C, 0x2012778C, 0x201270CC, 0x20126A8C,
     0x20298CAC, 0x20127C0C, 0x20127D2C, 0x20295D6C, 0x20295A0C, 0x200A4834,
     recomp::gen::f_20008580_lifted, recomp::gen::n_20008580, recomp::gen::f_20006FD0},
    {"cgamex86", 0x300CEB00, 0x300D1400, 0x30066FD8, 0x30066FE0, 0x300CEB88, 0x300CEB8C,
     0x30068E50, 0x300D0B4C, 0x300D0FCC, 0x300D03E8, 0x300D0A28, 0x300D008C, 0x300D0EAC,
     0x300D07EC, 0x300D02CC, 0x300D06CC, 0x300D090C, 0x300D0C6C, 0x300D05AC, 0x300CFF6C,
     0x301E87AC, 0x300D10EC, 0x300D120C, 0, 0, 0x300657E8, recomp::gen::f_30007C30_lifted,
     recomp::gen::n_30007C30, recomp::gen::f_30006710},
};

// ---- the world ---------------------------------------------------------------------------

struct Brush {
  double lo[3], hi[3];
  int numExtra = 0;
  double extra[2][4];  // n . x <= d
  int32_t contents;
  int32_t surfaceFlags;
  uint16_t entity;
};
std::vector<Brush> g_world;
// Where the world has a ladder (its x; NAN: none), for players facing it.
double g_ladderX = NAN;

struct TraceOut {
  double fraction = 1;
  double normal[3] = {0, 0, 0};
  const Brush* brush = nullptr;
  bool startsolid = false, allsolid = false;
};

constexpr double kEpsilon = 0.125;

// A box (mins, maxs about the path) swept from start to end through b, as
// Quake 3's CM_TraceThroughBrush.
void TraceBrush(const Brush& b, const double* start, const double* end, const double* mins,
                const double* maxs, TraceOut& t) {
  double enter = -1, leave = 1, normal[3] = {0, 0, 0};
  bool startout = false, getout = false;
  const auto plane = [&](const double* n, double dist) {
    // Expanded by the box's corner farthest against the normal.
    double off = 0;
    for (int i = 0; i < 3; i++) off += n[i] < 0 ? n[i] * maxs[i] : n[i] * mins[i];
    const double d = dist - off;
    const double d1 = n[0] * start[0] + n[1] * start[1] + n[2] * start[2] - d;
    const double d2 = n[0] * end[0] + n[1] * end[1] + n[2] * end[2] - d;
    if (d2 > 0) getout = true;
    if (d1 > 0) startout = true;
    if (d1 > 0 && (d2 >= kEpsilon || d2 >= d1)) return false;
    if (d1 <= 0 && d2 <= 0) return true;
    if (d1 > d2) {
      double f = (d1 - kEpsilon) / (d1 - d2);
      if (f < 0) f = 0;
      if (f > enter) {
        enter = f;
        std::memcpy(normal, n, sizeof(normal));
      }
    } else {
      double f = (d1 + kEpsilon) / (d1 - d2);
      if (f > 1) f = 1;
      if (f < leave) leave = f;
    }
    return true;
  };
  for (int i = 0; i < 3; i++) {
    double n[3] = {0, 0, 0};
    n[i] = 1;
    if (!plane(n, b.hi[i])) return;
    n[i] = -1;
    if (!plane(n, -b.lo[i])) return;
  }
  for (int k = 0; k < b.numExtra; k++)
    if (!plane(b.extra[k], b.extra[k][3])) return;
  if (!startout) {
    t.startsolid = true;
    t.brush = &b;
    if (!getout) {
      t.allsolid = true;
      t.fraction = 0;
    }
    return;
  }
  if (enter < leave && enter > -1 && enter < t.fraction) {
    t.fraction = enter < 0 ? 0 : enter;
    std::memcpy(t.normal, normal, sizeof(normal));
    t.brush = &b;
  }
}

// The calls the code made, to compare.
struct CallRecord {
  int fn;
  uint32_t args[7];
  bool operator==(const CallRecord&) const = default;
};
std::vector<CallRecord>* g_calls = nullptr;

uint32_t Arg(const Ctx& c, int n) { return Rd(c.esp + 4 + 4 * n); }

// A captured call's traces and point contents (tests/pmove_capture.h): served
// in order instead of the world's; a call that is not the next one recorded
// (another kind or other arguments) is counted.
struct Recorded {
  uint32_t which;
  uint32_t args[14];  // start, end, mins, maxs, passEntityNum, mask
  uint8_t result[0x30];
};
const std::vector<Recorded>* g_replay = nullptr;
size_t g_replayAt = 0;
int g_replayOff = 0;  // calls not as recorded

const Recorded* Replay(uint32_t which, const uint32_t* args) {
  if (g_replayAt >= g_replay->size()) {
    g_replayOff++;
    return nullptr;
  }
  const Recorded& r = (*g_replay)[g_replayAt++];
  if ((r.which == 3) != (which == 3) || std::memcmp(r.args, args, sizeof(r.args))) g_replayOff++;
  return &r;
}

void TraceCall(Ctx& c, int which) {
  const uint32_t results = Arg(c, 0), startVa = Arg(c, 1), minsVa = Arg(c, 2), maxsVa = Arg(c, 3),
                 endVa = Arg(c, 4);
  const int32_t pass = int32_t(Arg(c, 5)), mask = int32_t(Arg(c, 6));
  if (g_calls) {
    CallRecord r{which, {}};
    for (int i = 0; i < 3; i++) {
      r.args[i] = Rd(startVa + 4 * i);
      r.args[3 + i] = Rd(endVa + 4 * i);
    }
    r.args[6] = uint32_t(mask) ^ Rd(minsVa) ^ Rd(maxsVa + 8) ^ uint32_t(pass);
    g_calls->push_back(r);
  }
  if (g_replay) {
    uint32_t args[14];
    for (int i = 0; i < 3; i++) {
      args[i] = Rd(startVa + 4 * i);
      args[3 + i] = Rd(endVa + 4 * i);
      args[6 + i] = minsVa ? Rd(minsVa + 4 * i) : 0;
      args[9 + i] = maxsVa ? Rd(maxsVa + 4 * i) : 0;
    }
    args[12] = uint32_t(pass);
    args[13] = uint32_t(mask);
    if (const Recorded* r = Replay(uint32_t(which), args))
      std::memcpy(g_mem.ptr(results), r->result, 0x30);
    c.esp += 4;
    return;
  }
  double start[3], end[3], mins[3], maxs[3];
  for (int i = 0; i < 3; i++) {
    start[i] = RdF(startVa + 4 * i);
    end[i] = RdF(endVa + 4 * i);
    mins[i] = RdF(minsVa + 4 * i);
    maxs[i] = RdF(maxsVa + 4 * i);
  }
  TraceOut t;
  for (const Brush& b : g_world) {
    if (!(b.contents & mask)) continue;
    if (b.entity == uint16_t(pass)) continue;
    // The prone trace (1) passes through bodies, as a different trace would.
    if (which == 1 && (b.contents & 0xC000)) continue;
    TraceBrush(b, start, end, mins, maxs, t);
    if (t.allsolid) break;
  }
  WrF(results, float(t.fraction));
  for (int i = 0; i < 3; i++) {
    WrF(results + 4 + 4 * i, float(start[i] + t.fraction * (end[i] - start[i])));
    WrF(results + 0x10 + 4 * i, float(t.normal[i]));
  }
  Wr(results + 0x1C, t.brush ? uint32_t(t.brush->surfaceFlags) : 0);
  Wr(results + 0x20, t.brush ? uint32_t(t.brush->contents) : 0);
  Wr(results + 0x24, 0);
  g_mem.write<uint16_t>(results + 0x28, t.brush ? t.brush->entity : 0x7FF);
  g_mem.write<uint16_t>(results + 0x2A, 0);
  g_mem.write<uint16_t>(results + 0x2C, 0);
  Wr8(results + 0x2E, t.allsolid);
  Wr8(results + 0x2F, t.startsolid);
  c.esp += 4;
}

void PointContentsCall(Ctx& c) {
  const uint32_t pointVa = Arg(c, 0);
  if (g_calls) {
    CallRecord r{3, {Rd(pointVa), Rd(pointVa + 4), Rd(pointVa + 8), Arg(c, 1), Arg(c, 2)}};
    g_calls->push_back(r);
  }
  if (g_replay) {
    uint32_t args[14] = {Rd(pointVa), Rd(pointVa + 4), Rd(pointVa + 8)};
    args[12] = Arg(c, 1);
    args[13] = Arg(c, 2);
    const Recorded* r = Replay(3, args);
    c.eax = 0;
    if (r) std::memcpy(&c.eax, r->result, 4);
    c.esp += 4;
    return;
  }
  double p[3];
  for (int i = 0; i < 3; i++) p[i] = RdF(pointVa + 4 * i);
  uint32_t contents = 0;
  for (const Brush& b : g_world)
    if (p[0] >= b.lo[0] && p[0] <= b.hi[0] && p[1] >= b.lo[1] && p[1] <= b.hi[1] &&
        p[2] >= b.lo[2] && p[2] <= b.hi[2])
      contents |= uint32_t(b.contents);
  c.eax = contents & Arg(c, 2);
  c.esp += 4;
}

// ---- random values -------------------------------------------------------------------------

float Uniform(std::mt19937& g, float lo, float hi) {
  return lo + (hi - lo) * (float(g() >> 8) * 0x1p-24f);
}
int32_t Int(std::mt19937& g, int32_t lo, int32_t hi) {
  return lo + int32_t(g() % uint32_t(hi - lo + 1));
}
bool Chance(std::mt19937& g, int per1024) { return int(g() % 1024) < per1024; }

// A float that is now and then special (special per 1024).
float Value(std::mt19937& g, float lo, float hi, int special) {
  if (!Chance(g, special)) return Uniform(g, lo, hi);
  const uint32_t sign = g() & 0x80000000u;
  uint32_t u;
  switch (g() % 8) {
    case 0: u = sign; break;
    case 1: u = sign | ((g() & 0x7FFFFF) | 1); break;
    case 2: u = sign | 0x7F800000; break;
    case 3: u = sign | 0x7FC00000 | (g() & 0x3FFFFF); break;
    case 4: u = sign | 0x7F800000 | ((g() & 0x3FFFFF) | 1); break;
    case 5: u = sign | (0x7E000000 + (g() & 0xFFFFFF)); break;
    case 6: return std::round(Uniform(g, lo, hi));
    default: u = g(); break;
  }
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}

// ---- the case ---------------------------------------------------------------------------------

void NewWorld(std::mt19937& g) {
  g_world.clear();
  const auto box = [&](double x0, double y0, double z0, double x1, double y1, double z1,
                       int32_t contents, int32_t sf, uint16_t entity) {
    Brush b{};
    b.lo[0] = x0, b.lo[1] = y0, b.lo[2] = z0;
    b.hi[0] = x1, b.hi[1] = y1, b.hi[2] = z1;
    b.contents = contents;
    b.surfaceFlags = sf;
    b.entity = entity;
    g_world.push_back(b);
    return &g_world.back();
  };
  const auto material = [&] {
    int32_t sf = Int(g, 0, 22) << 20;
    if (Chance(g, 100)) sf |= 2;     // slick
    if (Chance(g, 60)) sf |= 1;      // no fall damage
    if (Chance(g, 60)) sf |= 0x2000;  // no steps
    return sf;
  };
  // The floor.
  box(-2048, -2048, -64, 2048, 2048, 0, 1, material(), 0x7FE);
  // Steps and blocks near the origin.
  for (int i = 0, n = Int(g, 0, 8); i < n; i++) {
    const double x = Uniform(g, -200, 200), y = Uniform(g, -200, 200);
    const double w = Uniform(g, 8, 96), d = Uniform(g, 8, 96);
    const double h = Chance(g, 512) ? std::round(Uniform(g, 2, 24)) : Uniform(g, 2, 120);
    box(x, y, 0, x + w, y + d, h, Chance(g, 900) ? 1 : 0x10000, material(),
        Chance(g, 900) ? 0x7FE : uint16_t(Int(g, 4, 900)));
  }
  // Walls.
  for (int i = 0, n = Int(g, 0, 3); i < n; i++) {
    const double x = Uniform(g, -150, 150);
    box(x, -1000, 0, x + 16, 1000, 400, 1, material(), 0x7FE);
  }
  // A ramp.
  if (Chance(g, 600)) {
    Brush* b = box(-100, -300, 0, 100, 300, 120, 1, material(), 0x7FE);
    const double a = Uniform(g, 0.1, 1.2);
    b->numExtra = 1;
    b->extra[0][0] = 0;
    b->extra[0][1] = std::sin(a);
    b->extra[0][2] = std::cos(a);
    b->extra[0][3] = std::cos(a) * 60 + std::sin(a) * Uniform(g, -100, 100);
  }
  // Walls and posts close by, for bodies lying down.
  for (int i = 0, n = Int(g, 0, 4); i < n; i++) {
    const double x = Uniform(g, -60, 60), y = Uniform(g, -60, 60);
    const double w = Chance(g, 512) ? 8 : 64;
    box(x, y, Chance(g, 512) ? 0 : Uniform(g, 0, 40), x + w, y + 72 - w, Uniform(g, 20, 200), 1,
        material(), 0x7FE);
  }
  // Ceilings, at whole units (a head touching one).
  for (int i = 0, n = Chance(g, 400) ? Int(g, 1, 3) : 0; i < n; i++) {
    const double z = Int(g, 40, 120);
    box(Uniform(g, -300, 0), Uniform(g, -300, 0), z, Uniform(g, 0, 300), Uniform(g, 0, 300), z + 16, 1,
        material(), 0x7FE);
  }
  // Foliage (contents 2).
  if (Chance(g, 400))
    box(Uniform(g, -200, 0), Uniform(g, -200, 0), 0, Uniform(g, 0, 200), Uniform(g, 0, 200),
        Uniform(g, 10, 80), 2, 0, 0x7FE);
  // A ladder.
  g_ladderX = NAN;
  if (Chance(g, 500)) {
    const double x = Uniform(g, -40, 40);
    box(x, -64, 0, x + 4, 64, 300, 1, 8 | material(), 0x7FE);
    g_ladderX = x;
  }
  // Water.
  if (Chance(g, 300)) box(-500, -500, -64, 500, 500, Uniform(g, 0, 90), 0x20, 0, 0x7FE);
  // Others: bodies (0x4000) and corpses (0x8000).
  for (int i = 0, n = Int(g, 0, 3); i < n; i++) {
    const double x = Uniform(g, -80, 80), y = Uniform(g, -80, 80);
    box(x - 15, y - 15, 0, x + 15, y + 15, 70, Chance(g, 512) ? 0x4000 : 0x8000, 0,
        uint16_t(Int(g, 0, 3)));
  }
}

void NewWeapons(std::mt19937& g, const Module& m, int special) {
  Wr(m.numWeapons, kNumWeapons);
  Wr(m.weaponDefs, kWeaponTable);
  for (int w = 0; w <= kNumWeapons; w++) {
    const uint32_t wd = kWeaponDefs + uint32_t(w) * kWeaponDefSize;
    Wr(kWeaponTable + uint32_t(w) * 4, wd);
    for (uint32_t i = 0; i < kWeaponDefSize; i += 4) Wr(wd + i, Chance(g, 512) ? g() % 2 : g());
    Wr(wd + 0x70, Chance(g, 150));
    Wr(wd + 0x78, Int(g, 0, 6));
    Wr(wd + 0x7C, Chance(g, 512));
    Wr(wd + 0x1A0, Int(g, 0, 7));
    Wr(wd + 0x1A8, Int(g, 0, 7));
    Wr(wd + 0x1C4, Chance(g, 512));
    for (uint32_t off = 0x1CC; off <= 0x210; off += 4)
      Wr(wd + off, Chance(g, 200) ? 0 : Int(g, 1, 900));
    WrF(wd + 0x214, Value(g, 0.3f, 1.3f, special));
    for (uint32_t off = 0x24C; off <= 0x260; off += 4)
      WrF(wd + off, Chance(g, 200) ? 0.0f : Value(g, 0, 3, special));
    for (uint32_t off = 0x2C4; off <= 0x2EC; off += 4) Wr(wd + off, Chance(g, 512));
    Wr(wd + 0x2F0, Chance(g, 300) ? 0 : Int(g, 1, 10));
    Wr(wd + 0x2F4, Chance(g, 300) ? 0 : Int(g, 1, 10));
    Wr(wd + 0x2FC, 0);
    Wr(wd + 0x3D4, Int(g, 0, 600));
    WrF(wd + 0x414, Value(g, 0.001f, 0.02f, special));
    WrF(wd + 0x418, Value(g, 0.001f, 0.02f, special));
  }
  // Alternate weapons in cycles (pairs, as the game's are, now and then
  // three): the slot search walks them until it is back where it started.
  int order[kNumWeapons];
  for (int i = 0; i < kNumWeapons; i++) order[i] = i + 1;
  std::shuffle(order, order + kNumWeapons, g);
  for (int i = 0; i + 1 < kNumWeapons;) {
    const int n = Chance(g, 300) ? 0 : Chance(g, 800) ? 2 : 3;
    if (n == 0 || i + n > kNumWeapons) {
      i++;
      continue;
    }
    for (int k = 0; k < n; k++)
      Wr(kWeaponDefs + uint32_t(order[i + k]) * kWeaponDefSize + 0x2FC, uint32_t(order[i + (k + 1) % n]));
    i += n;
  }
  for (int i = 0; i < 8; i++) Wr(m.clipSizes + uint32_t(i) * 4, Int(g, 1, 30));
}

void NewCvars(std::mt19937& g, const Module& m, int special) {
  Wr(m.proneTime, Int(g, 100, 600));
  Wr(m.crouchTime, Int(g, 100, 400));
  // Fall heights: ordinary, small (most landings hurt), special, or equal
  // (bad values).
  const bool low = Chance(g, 100);
  WrF(m.fallMax, Chance(g, 30) ? Value(g, 0, 300, 1024) : low ? Uniform(g, 0, 8) : Uniform(g, 250, 500));
  WrF(m.fallMin, Chance(g, 30)  ? Value(g, -50, 300, 1024)
                 : low          ? Uniform(g, 0, 4)
                                : Uniform(g, 64, 200));
  if (Chance(g, 20)) WrF(m.fallMin, RdF(m.fallMax));
  if (Chance(g, 20)) Wr(m.fallMin, g() & 1 ? 0x7FC00001 : 0xFF800001);  // a NaN, landings hurt
  if (Chance(g, 20)) WrF(m.fallMax, Uniform(g, 0, 8));
  Wr(m.foliageMinSpeed, Int(g, 0, 60));
  Wr(m.foliageMaxSpeed, Int(g, 100, 250));
  Wr(m.foliageSlowInterval, Int(g, 300, 1500));
  Wr(m.foliageFastInterval, Int(g, 100, 600));
  Wr(m.foliageReset, Int(g, 0, 3000));
  Wr(m.flyViewHeight, Int(g, 20, 70));
  Wr(m.ladderYawCap, Chance(g, 512) ? 0 : Int(g, 20, 90));
  Wr(m.proneYawCap, Int(g, 0, 90));
  WrF(m.proneYawCap - 4, float(int32_t(Rd(m.proneYawCap))));
  Wr(m.proneYawCheck, Chance(g, 512));
  Wr(m.noFire, Chance(g, 60));
  Wr(m.debugWeaponState, Chance(g, 30) ? Int(g, 1, 3) : 0);
  Wr(m.debugWeaponAnim, Chance(g, 30) ? Int(g, 1, 3) : 0);
  if (m.proneDebug) {
    Wr(m.proneDebug, Chance(g, 30) ? Int(g, 1, 2) : 0);
    Wr(m.proneDebugTime, Int(g, 0, 100));
  }
  Wr(m.syscall, recomp::kSyscallFn);
  (void)special;
}

// The flags pmove reads, each now and then.
uint32_t RandomFlags(std::mt19937& g, const uint32_t* bits, int n, int per1024) {
  uint32_t f = 0;
  for (int i = 0; i < n; i++)
    if (Chance(g, per1024)) f |= bits[i];
  return f;
}

void RandomCmd(std::mt19937& g, uint32_t cmd, int32_t time) {
  Wr(cmd, uint32_t(time));
  Wr8(cmd + 4, uint8_t(Chance(g, 300) ? g() : RandomFlags(g, (const uint32_t[]){1, 2, 0x10, 0x20}, 4, 300)));
  Wr8(cmd + 5, uint8_t(Chance(g, 300) ? g() : RandomFlags(g, (const uint32_t[]){2, 8, 0x10, 0x20, 0x40, 0x80}, 6, 200)));
  Wr8(cmd + 6, uint8_t(Chance(g, 900) ? Int(g, 0, kNumWeapons) : g()));
  for (int i = 0; i < 3; i++) Wr(cmd + 8 + 4 * i, Chance(g, 900) ? Int(g, -32768, 32767) : g());
  const int8_t moves[] = {0, 0, 127, -127, 64, -64, 1, -1, -128};
  for (int i = 0; i < 3; i++)
    Wr8(cmd + 0x14 + i, uint8_t(Chance(g, 700) ? moves[g() % 9] : int8_t(g())));
}

void NewState(std::mt19937& g, const Module& m, int special) {
  for (uint32_t i = 0; i < kPmSize; i += 4) Wr(kPm + i, 0);
  for (uint32_t i = 0; i < kPsSize; i += 4) Wr(kPs + i, Chance(g, 100) ? g() : 0);
  const uint32_t ps = kPs;
  const int32_t time = Int(g, 1000, 1000000);
  Wr(ps + 0x0, uint32_t(time));
  Wr(ps + 0x4, Chance(g, 800) ? 0 : Int(g, 0, 6));
  Wr(ps + 0x8, Int(g, 0, 255));
  static const uint32_t kPmFlags[] = {1,      2,       4,       8,       0x10,     0x20,
                                      0x40,   0x80,    0x100,   0x200,   0x400,    0x800,
                                      0x1000, 0x2000,  0x4000,  0x8000,  0x10000,  0x20000,
                                      0x40000, 0x80000, 0x100000, 0x200000};
  uint32_t flags = RandomFlags(g, kPmFlags, 22, 120);
  if (Chance(g, 200)) flags |= 4;
  // On a turret: only cgame's (the game's calls the game's turret code).
  if (&m == &kModules[1] && Chance(g, 30)) flags |= 0x400000;
  if ((flags & 0xE0000) == 0xE0000) flags &= ~0x20000u;  // all stances out: a drop
  if (Chance(g, 900)) flags &= ~0x4000u;
  Wr(ps + 0xC, flags);
  Wr(ps + 0x10, Chance(g, 600) ? 0 : Int(g, 1, 600));
  WrF(ps + 0x14, Value(g, -100, 100, special));
  WrF(ps + 0x18, Value(g, -100, 100, special));
  WrF(ps + 0x1C, Chance(g, 300)   ? float(Int(g, 0, 80))
                 : Chance(g, 500) ? Uniform(g, 0, 2)
                                  : Value(g, -2, 140, special));
  for (int i = 0; i < 3; i++)
    WrF(ps + 0x20 + 4 * i, Chance(g, 150) ? 0.0f : Value(g, -400, 400, special));
  Wr(ps + 0x2C, Chance(g, 500) ? 0 : Int(g, -50, 900));
  Wr(ps + 0x30, Chance(g, 500) ? 0 : Int(g, -50, 900));
  Wr(ps + 0x34, Chance(g, 800) ? 0 : Int(g, -50, 3000));
  Wr(ps + 0x38, Int(g, 0, time));
  Wr(ps + 0x3C, Chance(g, 900) ? 800 : Int(g, 0, 2000));
  WrF(ps + 0x40, Chance(g, 600) ? 0.0f : Value(g, -1, 1, special));
  Wr(ps + 0x44, Chance(g, 900) ? 190 : Int(g, 0, 400));
  for (int i = 0; i < 3; i++) Wr(ps + 0x48 + 4 * i, Chance(g, 700) ? 0 : g());
  Wr(ps + 0x54, Chance(g, 500) ? 0x7FF : Chance(g, 800) ? 0x7FE : Int(g, 0, 0x7FF));
  const float yaw = Uniform(g, 0, 6.2831853f);
  WrF(ps + 0x58, std::cos(yaw));
  WrF(ps + 0x5C, std::sin(yaw));
  WrF(ps + 0x60, Chance(g, 900) ? 0.0f : Value(g, -1, 1, special));
  Wr(ps + 0x64, uint32_t(time - Int(g, -100, 2000)));
  WrF(ps + 0x68, Chance(g, 500) ? 0.0f : Value(g, -50, 100, special));
  Wr(ps + 0x6C, Int(g, -90, 90));
  static const uint32_t kEFlags[] = {0x10, 0x20, 0x40, 0x400, 0x4000, 0x8000, 0x20000, 0x80000};
  Wr(ps + 0x70, RandomFlags(g, kEFlags, 8, 60));
  Wr(ps + 0x74, g());
  Wr(ps + 0x9C, Int(g, 0, 3));
  Wr(ps + 0xA0, Int(g, 0, kNumWeapons));
  Wr(ps + 0xA4, Chance(g, 900) ? Int(g, 0, 11) : g());
  WrF(ps + 0xA8, Chance(g, 500) ? 0.0f : Chance(g, 500) ? 1.0f : Value(g, 0, 1, special));
  for (int i = 0; i < 3; i++) WrF(ps + 0xB0 + 4 * i, Value(g, -180, 360, special));
  // View heights: prone 11, crouch 40, standing 60, dead 8, or another.
  const int32_t heights[] = {11, 40, 60, 8, Int(g, 0, 80)};
  Wr(ps + 0x324, 11);
  Wr(ps + 0x328, 40);
  Wr(ps + 0x32C, 60);
  Wr(ps + 0x330, 8);
  Wr(ps + 0xBC, uint32_t(heights[g() % 5]));
  WrF(ps + 0xC0, Chance(g, 300) ? float(heights[g() % 5]) : Value(g, 0, 70, special));
  Wr(ps + 0xC4, Chance(g, 500) ? 0 : uint32_t(time - Int(g, -20, Chance(g, 512) ? 250 : 700)));
  Wr(ps + 0xC8, uint32_t(heights[g() % 5]));
  Wr(ps + 0xCC, Chance(g, 512));
  WrF(ps + 0xD0, Value(g, -5, 5, special));
  for (int i = 0; i < 64; i++) Wr(ps + 0xF4 + 4 * i, Chance(g, 300) ? 0 : Int(g, 0, 90));
  for (int i = 0; i < 64; i++) Wr(ps + 0x1F4 + 4 * i, Chance(g, 300) ? 0 : Int(g, -1, 30));
  Wr(ps + 0x2F4, g() & 0x7FE);
  Wr(ps + 0x2F8, 0);
  for (int i = 0; i < 8; i++) Wr8(ps + 0x2FC + i, uint8_t(Int(g, 0, kNumWeapons)));
  if (Chance(g, 300)) Wr8(ps + 0x2FC + Int(g, 1, 2), uint8_t(Rd(ps + 0xA0)));  // in a slot
  Wr(ps + 0x304, g() & 0x7FE);
  WrF(ps + 0x30C, -15);
  WrF(ps + 0x310, -15);
  WrF(ps + 0x314, 0);
  WrF(ps + 0x318, 15);
  WrF(ps + 0x31C, 15);
  WrF(ps + 0x320, 70);
  WrF(ps + 0x334, 0.4f);
  WrF(ps + 0x338, 1.0f);
  WrF(ps + 0x33C, 0.15f);
  WrF(ps + 0x340, 0.65f);
  WrF(ps + 0x344, 0.8f);
  WrF(ps + 0x348, 0.7f);
  WrF(ps + 0x34C, Value(g, 0.5f, 1.2f, special));
  for (int i = 0; i < 3; i++) WrF(ps + 0x350 + 4 * i, Value(g, -90, 360, special));
  WrF(ps + 0x370, Chance(g, 900) ? 1.0f : Value(g, 0.2f, 3, special));
  for (int i = 0; i < 3; i++) WrF(ps + 0x3BC + 4 * i, Value(g, -40, 40, special));
  Wr(ps + 0x3CC, Int(g, 0, 17) | (g() & 0x200));
  WrF(ps + 0x3D0, Value(g, 0, 255, special));

  // pmove_t
  Wr(kPm, ps);
  const int32_t dt = Chance(g, 900) ? Int(g, 0, 70) : Chance(g, 500) ? Int(g, -30, 0) : Int(g, 70, 2500);
  RandomCmd(g, kPm + 4, time + dt);
  RandomCmd(g, kPm + 0x30, time);
  if (Chance(g, 500))
    for (uint32_t i = 0; i < 0x2C; i++) Wr8(kPm + 0x30 + i, g_mem.read<uint8_t>(kPm + 4 + i));
  Wr(kPm + 0x5C, Chance(g, 900) ? 0x2810011 | 0xC000 : g());
  Wr(kPm + 0x60, Chance(g, 50) ? Int(g, 1, 2) : 0);
  Wr(kPm + 0x64, Int(g, 0, 32));
  for (uint32_t i = 0; i < 32; i++) Wr(kPm + 0x68 + i * 4, Int(g, 0, 40));
  Wr8(kPm + 0x100, uint8_t(g()));
  Wr8(kPm + 0x101, uint8_t(Int(g, 0, 3)));
  WrF(kPm + 0x104, Value(g, 0, 300, special));
  Wr(kPm + 0x108, Chance(g, 300));
  Wr(kPm + 0x10C, Int(g, 8, 33));
  Wr(kPm + 0x110, recomp::kTraceFns[0]);
  Wr(kPm + 0x114, recomp::kTraceFns[1]);
  Wr(kPm + 0x118, recomp::kTraceFns[2]);
  Wr(kPm + 0x11C, recomp::kPointContentsFn);
  // Now and then at the ladder, facing it (yaw 0), standing or in the air.
  if (!std::isnan(g_ladderX) && Chance(g, 300)) {
    WrF(ps + 0x14, float(g_ladderX - 15 - Uniform(g, 0, 3)));
    WrF(ps + 0x18, Uniform(g, -50, 50));
    WrF(ps + 0x1C, Chance(g, 512) ? 0.0f : Uniform(g, 0, 200));
    Wr(ps + 0xC, Rd(ps + 0xC) & ~3u);
    Wr(ps + 0x4C, 0);
    Wr(kPm + 4 + 0xC, Chance(g, 800) ? 0 : Int(g, -3000, 3000));
    if (Chance(g, 512)) Wr8(kPm + 4 + 0x16, 127);  // jumping off it
    WrF(ps + 0x58, -1.0f);
    WrF(ps + 0x5C, 0.0f);
    WrF(ps + 0x60, 0.0f);
  }
}

// ---- running and comparing -------------------------------------------------------------------

struct Window {
  uint32_t va, size;
};

std::vector<uint8_t> Save(const std::vector<Window>& w) {
  std::vector<uint8_t> out;
  for (const Window& x : w) out.insert(out.end(), g_mem.ptr(x.va), g_mem.ptr(x.va) + x.size);
  return out;
}
void Load(const std::vector<Window>& w, const std::vector<uint8_t>& in) {
  size_t at = 0;
  for (const Window& x : w) {
    std::memcpy(g_mem.ptr(x.va), in.data() + at, x.size);
    at += x.size;
  }
}

Ctx NewCtx(std::mt19937& g, uint32_t esp) {
  Ctx c{};
  c.eax = g();
  c.ecx = g();
  c.edx = g();
  c.ebx = g();
  c.ebp = g();
  c.esi = kPm;
  c.edi = g();
  c.esp = esp;
  recomp::x87::Init(c.fpu);
  recomp::x87::SetControlWord(c.fpu, 0x027F);
  c.mxcsr = recomp::sse::kMxcsrDefault;
  return c;
}

// The earlier pmove_fixed wrapper around the original Pmove (the reference
// for the fixed steps of the native Pmove).
void PmoveFixedReference(Ctx& c, const Module& m) {
  const uint32_t pm = c.esi, ps = Rd(pm);
  const int32_t t = int32_t(Rd(pm + 4));
  int32_t ct = int32_t(Rd(ps));
  if (t < ct) return m.lifted(c);
  if (t - ct > 1000) ct = t - 1000;
  if ((Rd(ps + 0xC) & 0x10000) && t - ct > 50) ct = t - 50;
  Wr(ps, uint32_t(ct));
  const int32_t step = recomp::gen::g_test_step;
  const int32_t end = ct + (t - ct) / step * step;
  const uint32_t fixed = Rd(pm + 0x108), msec = Rd(pm + 0x10C);
  Wr(pm + 0x108, 1);
  Wr(pm + 0x10C, uint32_t(step));
  Wr(pm + 4, uint32_t(end));
  m.lifted(c);
  Wr(pm + 4, uint32_t(t));
  Wr(pm + 0x108, fixed);
  Wr(pm + 0x10C, msec);
  if (end != ct) return;
  // The view angles (game: EAX ps; cmd, trace; cgame: EAX ps, EDI cmd; trace).
  const uint32_t eax = c.eax, ebx = c.ebx, esi = c.esi, edi = c.edi, ebp = c.ebp;
  const bool game = &m == &kModules[0];
  c.esp -= 4;
  Wr(c.esp, Rd(pm + 0x118));
  if (game) {
    c.esp -= 4;
    Wr(c.esp, pm + 4);
  }
  c.esp -= 4;
  Wr(c.esp, game ? 0x20008235 : 0x300078E4);
  c.eax = ps;
  c.edi = pm + 4;
  m.viewAngles(c);
  c.esp += game ? 8 : 4;
  c.eax = eax, c.ebx = ebx, c.esi = esi, c.edi = edi, c.ebp = ebp;
}

struct Stats {
  int cases = 0, failures = 0, fixed = 0, steps = 0, calls = 0;
  int captured = 0, game_differs = 0;  // captured calls; the lifted one's result not the game's
  int captured_by[2] = {}, captured_fixed = 0, captured_traces = 0;
};

// The names of the bytes that differ, for the report.
std::string Where(uint32_t va, uint32_t pm) {
  char buf[64];
  const uint32_t ps = Rd(pm);
  if (va >= pm && va < pm + kPmSize)
    std::snprintf(buf, sizeof(buf), "pm+%X", va - pm);
  else if (va >= ps && va < ps + kPsSize)
    std::snprintf(buf, sizeof(buf), "ps+%X", va - ps);
  else
    std::snprintf(buf, sizeof(buf), "%08X", va);
  return buf;
}

// Runs Pmove (ESI pm) through the lifted original and the native code and
// compares; lifted: the original's pmove_t and playerState_t after it.
bool RunCase(std::mt19937& g, const Module& m, bool fixed, int32_t step, uint32_t pm, Stats& s,
             const char* what, std::vector<uint8_t>* lifted = nullptr) {
  const uint32_t esp = kStack + kStackSize - 0x100;
  Wr(esp, 0xDEAD0000);
  std::vector<Window> w = {{pm, kPmSize},        {Rd(pm), kPsSize},
                           {m.lo, m.hi - m.lo},  {m.debugLo, m.debugHi - m.debugLo},
                           {esp + 4, 0x40}};
  const std::vector<uint8_t> before = Save(w);
  Ctx c0 = NewCtx(g, esp);
  c0.esi = pm;
  recomp::gen::g_test_fixed = fixed;
  recomp::gen::g_test_step = step;
  std::vector<CallRecord> lc, nc;
  Ctx cl = c0;
  g_calls = &lc;
  g_replayAt = 0;
  if (fixed)
    PmoveFixedReference(cl, m);
  else
    m.lifted(cl);
  const std::vector<uint8_t> after = Save(w);
  if (lifted) lifted->assign(after.begin(), after.begin() + kPmSize + kPsSize);
  if (g_replay && (g_replayAt != g_replay->size() || g_replayOff)) {
    // The original did not make the game's calls: the record is incomplete.
    if (s.game_differs++ < 10)
      std::printf("%s %s: the lifted Pmove made %zu of the %zu recorded calls, %d others\n", what,
                  m.name, g_replayAt, g_replay->size(), g_replayOff);
  }
  Load(w, before);
  Ctx cn = c0;
  g_calls = &nc;
  g_replayAt = 0;
  m.native(cn);
  g_calls = nullptr;
  const std::vector<uint8_t> got = Save(w);

  s.cases++;
  s.fixed += fixed;
  s.calls += int(lc.size());
  bool ok = cn.esp == cl.esp && cn.ebp == cl.ebp && cn.ebx == cl.ebx && cn.esi == cl.esi &&
            cn.edi == cl.edi && cn.fpu.top == cl.fpu.top && cn.fpu.empty == cl.fpu.empty &&
            cn.fpu.cw == cl.fpu.cw;
  if (!ok && s.failures < 20)
    std::printf("%s %s: registers differ (esp %08X/%08X ebx %08X/%08X esi %08X/%08X edi "
                "%08X/%08X ebp %08X/%08X top %d/%d)\n",
                what, m.name, cl.esp, cn.esp, cl.ebx, cn.ebx, cl.esi, cn.esi, cl.edi, cn.edi,
                cl.ebp, cn.ebp, cl.fpu.top, cn.fpu.top);
  if (lc != nc) {
    if (s.failures < 20) {
      size_t i = 0;
      while (i < lc.size() && i < nc.size() && lc[i] == nc[i]) i++;
      std::printf("%s %s: calls differ at %zu of %zu/%zu (fn %d/%d)\n", what, m.name, i,
                  lc.size(), nc.size(), i < lc.size() ? lc[i].fn : -1,
                  i < nc.size() ? nc[i].fn : -1);
      for (int k = 0; k < 7; k++)
        std::printf("  %08X %08X\n", i < lc.size() ? lc[i].args[k] : 0,
                    i < nc.size() ? nc[i].args[k] : 0);
    }
    ok = false;
  }
  size_t at = 0;
  int printed = 0;
  for (const Window& x : w) {
    for (uint32_t i = 0; i < x.size; i += 4) {
      uint32_t lv, nv;
      std::memcpy(&lv, after.data() + at + i, 4);
      std::memcpy(&nv, got.data() + at + i, 4);
      if (lv == nv) continue;
      if (s.failures < 20 && printed++ < 8)
        std::printf("%s %s%s: %s lifted %08X native %08X\n", what, m.name, fixed ? " fixed" : "",
                    Where(x.va + i, pm).c_str(), lv, nv);
      ok = false;
    }
    at += x.size;
  }
  if (!ok) s.failures++;
  return ok;
}

// ---- captures ------------------------------------------------------------------------------

bool ReadRegions(FILE* f) {
  uint32_t n;
  if (std::fread(&n, 4, 1, f) != 1) return false;
  for (uint32_t i = 0; i < n; i++) {
    uint32_t r[2];
    if (std::fread(r, 4, 2, f) != 2) return false;
    if (r[0] < kArena + kArenaSize && r[0] + r[1] > kStack) {
      std::printf("a captured region (%08X, %X bytes) overlaps the test's memory\n", r[0], r[1]);
      return false;
    }
    if (!g_mem.IsCommitted(r[0]) || !g_mem.IsCommitted(r[0] + r[1] - 1))
      g_mem.Commit(r[0], r[1], recomp::Prot::RW);
    if (std::fread(g_mem.ptr(r[0]), 1, r[1], f) != r[1]) return false;
  }
  return true;
}

// The game's own calls (tests/pmove_capture.h): each through both, with the
// recorded traces, and the lifted one must give the game's result.
bool RunCaptures(const char* path, std::mt19937& g, Stats& s) {
  using namespace pmove_capture;
  FILE* f = std::fopen(path, "rb");
  if (!f) {
    std::printf("%s: cannot open\n", path);
    return false;
  }
  int calls = 0;
  const Stats before = s;
  uint32_t tag;
  const auto bad = [&] {
    std::printf("%s: bad record after %d calls\n", path, calls);
    std::fclose(f);
    return false;
  };
  while (std::fread(&tag, 4, 1, f) == 1) {
    if (tag == kWeapons) {
      uint32_t module;
      if (std::fread(&module, 4, 1, f) != 1 || !ReadRegions(f)) return bad();
      continue;
    }
    if (tag != kCall) return bad();
    uint32_t head[4];  // module, pm, pmove_fixed, step
    if (std::fread(head, 4, 4, f) != 4 || head[0] > 1 || !ReadRegions(f)) return bad();
    std::vector<Recorded> traces;
    std::vector<uint8_t> game(kPmSize + kPsSize);
    for (;;) {
      if (std::fread(&tag, 4, 1, f) != 1) return bad();
      if (tag == kResult) {
        if (std::fread(game.data(), 1, game.size(), f) != game.size()) return bad();
        break;
      }
      Recorded r;
      if (tag != kTrace || std::fread(&r.which, 4, 1, f) != 1 ||
          std::fread(r.args, 4, 14, f) != 14 || std::fread(r.result, 1, 0x30, f) != 0x30)
        return bad();
      traces.push_back(r);
    }
    const Module& m = kModules[head[0]];
    const uint32_t pm = head[1];
    // Its traces and prints to the test's.
    for (int i = 0; i < 3; i++) Wr(pm + 0x110 + 4 * i, recomp::kTraceFns[i]);
    Wr(pm + 0x11C, recomp::kPointContentsFn);
    Wr(m.syscall, recomp::kSyscallFn);
    g_replay = &traces;
    g_replayOff = 0;
    char what[64];
    std::snprintf(what, sizeof(what), "%s call %d", path, calls);
    std::vector<uint8_t> lifted;
    RunCase(g, m, head[2], int32_t(head[3]), pm, s, what, &lifted);
    g_replay = nullptr;
    s.captured++;
    s.fixed -= head[2] != 0;
    s.captured_by[head[0]]++;
    s.captured_fixed += head[2] != 0;
    s.captured_traces += int(traces.size());
    // The trace pointers are the test's (the native code's are compared).
    std::memcpy(game.data() + 0x110, lifted.data() + 0x110, 0x10);
    if (lifted != game) {
      size_t i = 0;
      while (lifted[i] == game[i]) i++;
      if (s.game_differs++ < 10)
        std::printf("%s: the lifted result differs from the game's at %s+%zX\n", what,
                    i < kPmSize ? "pm" : "ps", i < kPmSize ? i : i - kPmSize);
    }
    calls++;
  }
  std::fclose(f);
  std::printf("%s: %d captured calls (game %d, cgame %d, %d with pmove_fixed, %d traces and "
              "point contents)\n",
              path, calls, s.captured_by[0] - before.captured_by[0],
              s.captured_by[1] - before.captured_by[1], s.captured_fixed - before.captured_fixed,
              s.captured_traces - before.captured_traces);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  if (argc < 2) {
    std::printf("usage: %s GAME_DIR [-n CASES] [-s SEED] [CAPTURE...]\n", argv[0]);
    return 2;
  }
  int cases = 100000;
  uint32_t seed = 20261019;
  std::vector<const char*> captures;
  for (int i = 2; i < argc; i++) {
    if (!std::strcmp(argv[i], "-n") && i + 1 < argc)
      cases = std::atoi(argv[++i]);
    else if (!std::strcmp(argv[i], "-s") && i + 1 < argc)
      seed = uint32_t(std::strtoul(argv[++i], nullptr, 0));
    else
      captures.push_back(argv[i]);
  }
  if (!g_mem.Reserve()) return 1;
  recomp::g_guest_base = g_mem.base();
  static recomp::PeImage images[2];
  const char* names[2] = {"gamex86.dll", "cgamex86.dll"};
  for (int i = 0; i < 2; i++) {
    std::string err;
    uint32_t next = recomp::kThunkBase + uint32_t(i) * 0x8000;
    const auto bind = [&](const std::string&, const std::string&) {
      next += recomp::kThunkStride;
      return next;
    };
    if (!images[i].Load(std::string(argv[1]) + "/" + names[i], g_mem, &err, 0, bind)) {
      std::printf("%s: %s\n", names[i], err.c_str());
      return 1;
    }
    recomp::g_images[i] = &images[i];
  }
  g_mem.Commit(kArena, kArenaSize, recomp::Prot::RW);
  g_mem.Commit(kStack, kStackSize, recomp::Prot::RW);
  g_mem.Commit(recomp::kPtd, 0x1000, recomp::Prot::RW);
  // The reference: the x87 model's exact arithmetic throughout.
  recomp::x87::run::g_doubles = false;
  // What the DLLs' CRT startup does that Pmove depends on.
  for (recomp::GuestFn init : {recomp::gen::f_20078B38, recomp::gen::f_300407BC}) {
    Ctx c{};
    c.esp = kStack + kStackSize - 0x100;
    recomp::x87::Init(c.fpu);
    init(c);
  }

  std::mt19937 g(seed);
  Stats s;
  for (int i = 0; i < cases; i++) {
    const Module& m = kModules[i % 2];
    const int special = i % 4 < 2 ? 0 : (i % 7 == 0 ? 64 : 4);
    if (i % 64 < 2) NewWorld(g);
    if (i % 256 < 2) {
      NewWeapons(g, m, special);
    }
    NewCvars(g, m, special);
    NewState(g, m, special);
    char what[32];
    std::snprintf(what, sizeof(what), "case %d", i);
    const bool fixed = Chance(g, 250);
    RunCase(g, m, fixed, Int(g, 8, 33), kPm, s, what);
  }
  const int random = s.cases;
  for (const char* c : captures)
    if (!RunCaptures(c, g, s)) return 1;
  std::printf("%d random + %d captured calls (%d random with pmove_fixed, %d calls of traces and "
              "contents): %d mismatched, %d captured results not the game's\n",
              random, s.captured, s.fixed, s.calls, s.failures, s.game_differs);
  return s.failures || s.game_differs ? 1 : 0;
}
