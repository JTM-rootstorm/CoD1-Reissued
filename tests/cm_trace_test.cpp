// The native collision traces (src/cm_trace.cpp) against the original
// functions, lifted from the EXE at build time with `recomp_lift --only`
// (CMakeLists.txt) and run on x86recomp's exact x87 model (SoftFloat, not
// its host-double runs). CoDSP.exe is mapped as the game maps it, for its
// constants and globals. Each case runs CM_BoxTrace, CM_TransformedBoxTrace,
// CM_Trace or 0x41D3B0 through the lifted original and the native code from
// the same guest memory, and both must leave the same bytes in the whole
// clip map (checkcounts, the temporary box model), the clip map's globals,
// the results and the arguments, and the same ESP, EBX, ESI, EDI, EBP and
// x87 stack (top, empty registers, control word). Not compared: EAX, ECX,
// EDX, EFLAGS, the x87 status word and the stack below the return address
// (src/cm_trace.cpp).
//
//   cm_trace_test EXE [CAPTURE...]
//
// Random clip maps (mt19937, fixed seed): a BSP tree of axial and oblique
// planes over leaves of brushes (oblique sides) and leaf surfaces, which
// are terrain (height fields with shared vertices and edges) or patch
// collides (random facets), inline models and the temporary box model;
// traces with points, boxes, capsules and spheres, rotations, positions
// (no move), every entry and its register arguments, and coordinates that
// are now and then zeros, denormals, infinities, NaNs or huge. Captures
// (tests/cm_trace_capture.h) add the game's own calls, on its clip map.

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "cm_trace_capture.h"
#include "runtime/guest_memory.h"
#include "runtime/lifted.h"
#include "runtime/pe_image.h"
#include "runtime/x87.h"

namespace recomp {
uint8_t* g_guest_base = nullptr;

namespace gen {
void f_0041CD60_lifted(Ctx& c);
void f_0041D380_lifted(Ctx& c);
void f_0041D3B0_lifted(Ctx& c);
void f_0041D870_lifted(Ctx& c);
// The native code (tests/cm_trace_native.cpp renames its entries)...
void n_0041CD60(Ctx& c);
void n_0041D380(Ctx& c);
void n_0041D3B0(Ctx& c);
void n_0041D870(Ctx& c);
// ...and the lifted originals call each other, not it.
void f_0041CD60(Ctx& c) { f_0041CD60_lifted(c); }
void f_0041D380(Ctx& c) { f_0041D380_lifted(c); }
void f_0041D3B0(Ctx& c) { f_0041D3B0_lifted(c); }
void f_0041D870(Ctx& c) { f_0041D870_lifted(c); }
// unzCloseCurrentFile, native in the game (src/unzip.cpp): here only the
// lifted error paths reach it.
void f_0049EAA0_lifted(Ctx& c);
void f_0049EAA0(Ctx& c) { f_0049EAA0_lifted(c); }
// tolower, native in the game (src/crt.cpp): here only lifted code
// reaches it.
void f_0051B3DD_lifted(Ctx& c);
void f_0051B3DD(Ctx& c) { f_0051B3DD_lifted(c); }
}  // namespace gen

PeImage* g_exe = nullptr;
// The CRT's per-thread data (_getptd), for errno from its math-error path
// (the square root of a NaN in 0x426940, which both sides call lifted).
constexpr uint32_t kPtd = 0x2FE00000;
uint32_t g_last_error = 0;

// Kernel32 as the CRT's _getptd uses it (stdcall: the callee pops).
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

// The leaf callback of CM_BoxLeafnums_r is called through a pointer.
void Dispatch(Ctx& c, uint32_t target, uint32_t va) {
  if (target - kThunkBase < kThunkRegionSize)
    for (const PeImport& i : g_exe->imports)
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

using namespace cm_capture;
using recomp::Ctx;

recomp::GuestMemory g_mem;

// The clip map's globals (src/cm_trace.cpp).
constexpr uint32_t kNoCurves = 0x14BD64C, kShaders = 0x14BD6AC, kNodes = 0x14BD6C4,
                   kLeafs = 0x14BD6CC, kLeafBrushes = 0x14BD6D4, kNumSubModels = 0x14BD6E0,
                   kCModels = 0x14BD6E4, kLeafSurfaces = 0x14BD6DC, kBrushes = 0x14BD6EC,
                   kPatches = 0x14BD718, kCheckcount = 0x14BD720, kBoxBrush = 0x14BD73C,
                   kBoxModel = 0x14BD740, kBoxWarned = 0x13F9E54,
                   kPlayerCurveClip = 0x14C77C0;  // cvar_t*, read by patch collision

// Random maps and cases live in the arena; the stack below it.
constexpr uint32_t kArena = 0x30000000, kArenaSize = 0x1000000;
constexpr uint32_t kStack = 0x2FF00000, kStackSize = 0x100000;

uint32_t Rd(uint32_t va) { return g_mem.read<uint32_t>(va); }
void Wr(uint32_t va, uint32_t v) { g_mem.write<uint32_t>(va, v); }
void WrF(uint32_t va, float f) { g_mem.write<float>(va, f); }
float RdF(uint32_t va) { return g_mem.read<float>(va); }

struct Window {
  uint32_t va, size;
};

struct Stats {
  int cases = 0, failures = 0, by_fn[4] = {}, hits = 0, startsolid = 0, nan = 0;
  int game = 0, game_differs = 0;  // captured calls with the game's result
};

// ---- comparing -------------------------------------------------------------

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

int FnIndex(uint32_t fn) {
  return fn == kTrace ? 0 : fn == kBoxTrace ? 1 : fn == kTransformed ? 2 : 3;
}
const char* FnName(uint32_t fn) {
  static const char* names[] = {"CM_Trace", "CM_BoxTrace", "0x41D3B0", "CM_TransformedBoxTrace"};
  return names[FnIndex(fn)];
}

struct Call {
  uint32_t fn, eax, ecx, edx;
  uint32_t args[kArgs];
  uint32_t results;  // where the trace_t goes
};

Ctx NewCtx(std::mt19937& g, const Call& k, uint32_t esp) {
  Ctx c{};
  c.eax = k.eax;
  c.ecx = k.ecx;
  c.edx = k.edx;
  c.ebx = g();
  c.ebp = g();
  c.esi = g();
  c.edi = g();
  c.esp = esp;
  recomp::x87::Init(c.fpu);
  recomp::x87::SetControlWord(c.fpu, 0x027F);
  c.mxcsr = recomp::sse::kMxcsrDefault;
  return c;
}

// Runs one call through the lifted original and the native code from the
// same memory and compares. w: everything the call may write.
void Run(const Call& k, std::vector<Window> w, std::mt19937& g, Stats& s, const char* what) {
  const uint32_t esp = kStack + kStackSize - 0x100;
  for (int i = 0; i < kArgs; i++) Wr(esp + 4 + i * 4, k.args[i]);
  Wr(esp, 0xDEAD0000);
  w.push_back({esp, 4 + kArgs * 4});
  w.push_back({kGlobals, kGlobalsSize});
  w.push_back({kBoxWarned, 4});
  const std::vector<uint8_t> before = Save(w);
  const Ctx c0 = NewCtx(g, k, esp);

  using Fn = void (*)(Ctx&);
  static const Fn lifted[] = {recomp::gen::f_0041CD60_lifted, recomp::gen::f_0041D380_lifted,
                              recomp::gen::f_0041D3B0_lifted, recomp::gen::f_0041D870_lifted};
  static const Fn native[] = {recomp::gen::n_0041CD60, recomp::gen::n_0041D380,
                              recomp::gen::n_0041D3B0, recomp::gen::n_0041D870};
  Ctx cl = c0;
  lifted[FnIndex(k.fn)](cl);
  const std::vector<uint8_t> after = Save(w);
  Load(w, before);
  Ctx cn = c0;
  native[FnIndex(k.fn)](cn);
  const std::vector<uint8_t> got = Save(w);

  s.cases++;
  s.by_fn[FnIndex(k.fn)]++;
  const float frac = RdF(k.results);
  s.hits += frac < 1.0f;
  s.startsolid += g_mem.read<uint8_t>(k.results + 0x2F) != 0;
  s.nan += frac != frac;
  bool ok = cn.esp == cl.esp && cn.ebp == cl.ebp && cn.ebx == cl.ebx && cn.esi == cl.esi &&
            cn.edi == cl.edi && cn.fpu.top == cl.fpu.top && cn.fpu.empty == cl.fpu.empty &&
            cn.fpu.cw == cl.fpu.cw;
  if (!ok && s.failures < 20)
    std::printf("%s %s: registers differ (esp %08X/%08X ebx %08X/%08X esi %08X/%08X edi "
                "%08X/%08X ebp %08X/%08X top %d/%d)\n",
                what, FnName(k.fn), cl.esp, cn.esp, cl.ebx, cn.ebx, cl.esi, cn.esi, cl.edi,
                cn.edi, cl.ebp, cn.ebp, cl.fpu.top, cn.fpu.top);
  size_t at = 0;
  for (const Window& x : w) {
    for (uint32_t i = 0; i < x.size; i++)
      if (got[at + i] != after[at + i]) {
        if (s.failures < 20) {
          const uint32_t a = (x.va + i) & ~3u;
          const size_t o = at + (a - x.va);
          uint32_t lv, nv;
          std::memcpy(&lv, after.data() + o, 4);
          std::memcpy(&nv, got.data() + o, 4);
          std::printf("%s %s: %08X lifted %08X native %08X (results at %08X)\n", what,
                      FnName(k.fn), a, lv, nv, k.results);
        }
        ok = false;
        break;
      }
    at += x.size;
  }
  if (!ok) s.failures++;
  // Keep the original's result, the next case's starting state.
  Load(w, after);
}

// ---- random clip maps --------------------------------------------------------

struct Arena {
  uint32_t top = kArena;
  uint32_t Alloc(uint32_t size, uint32_t align = 4) {
    top = (top + align - 1) & ~(align - 1);
    const uint32_t va = top;
    top += size;
    if (top > kArena + kArenaSize) {
      std::printf("arena full\n");
      std::abort();
    }
    return va;
  }
};

float Uniform(std::mt19937& g, float lo, float hi) {
  return lo + (hi - lo) * (float(g() >> 8) * 0x1p-24f);
}

// Mostly ordinary values; otherwise one of the special classes.
float Value(std::mt19937& g, float lo, float hi, uint32_t special_per_1024) {
  if (g() % 1024 >= special_per_1024) return Uniform(g, lo, hi);
  const uint32_t sign = g() & 0x80000000u;
  uint32_t u;
  switch (g() % 9) {
    case 0: u = sign; break;                                        // zero
    case 1: u = sign | ((g() & 0x7FFFFF) | 1); break;               // denormal
    case 2: u = sign | 0x7F800000; break;                           // infinity
    case 3: u = sign | 0x7FC00000 | (g() & 0x3FFFFF); break;        // quiet NaN
    case 4: u = sign | 0x7F800000 | ((g() & 0x3FFFFF) | 1); break;  // signalling NaN
    case 5: u = sign | (0x7E000000 + (g() & 0xFFFFFF)); break;      // huge
    case 6: u = sign | (0x00800000 + (g() & 0x1FFFFFF)); break;     // tiny
    case 7: return std::round(Uniform(g, lo, hi));                  // integral
    default: u = g(); break;                                        // raw bits
  }
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}

struct V3 {
  float x, y, z;
};
V3 Sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3 Scale(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
V3 Normalized(V3 a) {
  const float l = std::sqrt(Dot(a, a));
  return l > 0 ? Scale(a, 1 / l) : V3{0, 0, 1};
}
V3 RandomDir(std::mt19937& g) {
  for (;;) {
    V3 v{Uniform(g, -1, 1), Uniform(g, -1, 1), Uniform(g, -1, 1)};
    if (Dot(v, v) > 0.01f && Dot(v, v) <= 1) return Normalized(v);
  }
}
void WrV(uint32_t va, V3 v) {
  WrF(va, v.x);
  WrF(va + 4, v.y);
  WrF(va + 8, v.z);
}

struct Map {
  float world;          // half size
  bool integral;        // brush bounds, plane distances and terrain on whole units
  uint32_t numBrushes = 0, numPatches = 0, numShaders = 0, numModels = 0;
  uint32_t leafBrushes = 0, leafSurfaces = 0;  // array VAs
  std::vector<uint32_t> planes;                // all VAs, for side planes
};

// A plane (0x14 bytes); type 0-2 for the positive axes.
uint32_t NewPlane(Arena& a, std::mt19937& g, V3 n, float dist, uint32_t special) {
  const uint32_t p = a.Alloc(0x14);
  WrV(p, n);
  WrF(p + 0xC, dist);
  uint8_t type = 3;
  if (n.x == 1 && n.y == 0 && n.z == 0) type = 0;
  if (n.x == 0 && n.y == 1 && n.z == 0) type = 1;
  if (n.x == 0 && n.y == 0 && n.z == 1) type = 2;
  if (g() % 64 == 0) type = uint8_t(g() % 5);  // inconsistent now and then
  const uint8_t signbits = (n.x < 0) | (n.y < 0) << 1 | (n.z < 0) << 2;
  g_mem.write<uint8_t>(p + 0x10, type);
  g_mem.write<uint8_t>(p + 0x11, signbits);
  if (special && g() % 1024 < special) {
    WrF(p + (g() % 4) * 4, Value(g, -1, 1, 1024));
  }
  return p;
}

V3 RandomPoint(std::mt19937& g, float w) {
  return {Uniform(g, -w, w), Uniform(g, -w, w), Uniform(g, -w, w)};
}

// A brush with its bounds and oblique sides near them.
void NewBrush(Arena& a, std::mt19937& g, Map& m, uint32_t b, uint32_t special) {
  V3 c = RandomPoint(g, m.world);
  V3 h{Uniform(g, 2, 400), Uniform(g, 2, 400), Uniform(g, 2, 400)};
  if (m.integral) {
    c = {std::round(c.x), std::round(c.y), std::round(c.z)};
    h = {std::round(h.x), std::round(h.y), std::round(h.z)};
  }
  static const uint32_t kContents[] = {1, 0x10000, 0x2000000, 0x4000000, 0x20, 0x8, 0x2010001};
  Wr(b, kContents[g() % 7] | (g() % 8 == 0 ? g() : 0));
  WrV(b + 4, Sub(c, h));
  WrV(b + 0x10, {c.x + h.x, c.y + h.y, c.z + h.z});
  const uint32_t n = g() % 9;
  const uint32_t sides = a.Alloc(n * 8 + 8);
  for (uint32_t i = 0; i < n; i++) {
    const V3 dir = RandomDir(g);
    const float extent = std::fabs(dir.x) * h.x + std::fabs(dir.y) * h.y + std::fabs(dir.z) * h.z;
    float dist = Dot(dir, c) + extent * Uniform(g, 0.3f, 1.1f);
    V3 n = dir;
    if (m.integral) {
      // Axial bevels and whole-unit distances, as maps have them.
      if (g() % 3 == 0) {
        const int axis = g() % 3;
        const float sgn = g() % 2 ? 1.f : -1.f;
        n = {axis == 0 ? sgn : 0.f, axis == 1 ? sgn : 0.f, axis == 2 ? sgn : 0.f};
        dist = sgn * (&c.x)[axis] + (&h.x)[axis];
      }
      dist = std::round(dist);
    }
    const uint32_t p = NewPlane(a, g, n, dist, special);
    m.planes.push_back(p);
    Wr(sides + i * 8, p);
    Wr(sides + i * 8 + 4, g() % m.numShaders);
  }
  Wr(b + 0x1C, n);
  Wr(b + 0x20, sides);
  Wr(b + 0x24, 0);
  for (int i = 0; i < 6; i++) g_mem.write<int16_t>(b + 0x28 + i * 2, int16_t(g() % m.numShaders));
}

// Terrain: a height field of tris with shared vertices and edges.
uint32_t NewTerrain(Arena& a, std::mt19937& g, const Map& m, V3* lo, V3* hi, uint32_t special) {
  const int n = 1 + g() % 5;  // cells per side
  const float cell = Uniform(g, 16, 256);
  const V3 o = RandomPoint(g, m.world);
  const float slope = Uniform(g, -1, 1);
  std::vector<V3> p((n + 1) * (n + 1));
  std::vector<uint32_t> verts(p.size());
  *lo = {1e30f, 1e30f, 1e30f};
  *hi = {-1e30f, -1e30f, -1e30f};
  for (int y = 0; y <= n; y++)
    for (int x = 0; x <= n; x++) {
      V3 v{o.x + x * cell, o.y + y * cell, o.z + slope * x * cell + Uniform(g, -cell, cell) * 0.5f};
      if (m.integral) v = {std::round(v.x), std::round(v.y), std::round(v.z)};
      if (special && g() % 1024 < special) v.z = Value(g, -1, 1, 1024);
      p[y * (n + 1) + x] = v;
      const uint32_t va = a.Alloc(0x10);
      Wr(va, 0);
      WrV(va + 4, v);
      verts[y * (n + 1) + x] = g() % 16 ? va : 0;
      lo->x = std::min(lo->x, v.x), lo->y = std::min(lo->y, v.y), lo->z = std::min(lo->z, v.z);
      hi->x = std::max(hi->x, v.x), hi->y = std::max(hi->y, v.y), hi->z = std::max(hi->z, v.z);
    }
  // One edge per vertex pair, shared by the tris on both sides.
  std::vector<std::pair<std::pair<int, int>, uint32_t>> edges;
  auto edge = [&](int i, int j) -> uint32_t {
    if (i > j) std::swap(i, j);
    for (auto& e : edges)
      if (e.first == std::make_pair(i, j)) return e.second;
    uint32_t va = 0;
    if (g() % 16) {
      va = a.Alloc(0x38);
      const V3 d = Sub(p[j], p[i]);
      const V3 axis2 = Normalized(d);
      const V3 axis0 = Normalized(Cross(axis2, std::fabs(axis2.z) < 0.9f ? V3{0, 0, 1} : V3{1, 0, 0}));
      const V3 axis1 = Cross(axis2, axis0);
      Wr(va, 0);
      WrV(va + 4, p[i]);
      WrV(va + 0x10, axis0);
      WrV(va + 0x1C, axis1);
      WrV(va + 0x28, axis2);
      WrF(va + 0x34, std::sqrt(Dot(d, d)));
    }
    edges.push_back({{i, j}, va});
    return va;
  };
  const uint32_t tris = 2 * n * n;
  const uint32_t t = a.Alloc(4 + tris * 0x48);
  g_mem.write<uint16_t>(t, uint16_t(tris));
  g_mem.write<uint8_t>(t + 2, g() % 4 == 0);
  int k = 0;
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++)
      for (int half = 0; half < 2; half++, k++) {
        const int i00 = y * (n + 1) + x, i10 = i00 + 1, i01 = i00 + n + 1, i11 = i01 + 1;
        const int v[3] = {half ? i10 : i00, half ? i11 : i10, half ? i01 : i01};
        const V3 a0 = p[v[0]], e1 = Sub(p[v[1]], a0), e2 = Sub(p[v[2]], a0);
        V3 nrm = Normalized(Cross(e1, e2));
        if (g() % 2) nrm = Scale(nrm, -1);
        const uint32_t tri = t + 4 + k * 0x48;
        WrV(tri, nrm);
        WrF(tri + 0xC, Dot(nrm, a0));
        // Barycentric planes: b1 = 1 at v1, 0 at v0 and v2; b2 likewise.
        const V3 c1 = Cross(nrm, e2), c2 = Cross(nrm, e1);
        const V3 n1 = Scale(c1, 1 / Dot(c1, e1)), n2 = Scale(c2, 1 / Dot(c2, e2));
        WrV(tri + 0x10, n1);
        WrF(tri + 0x1C, Dot(n1, a0));
        WrV(tri + 0x20, n2);
        WrF(tri + 0x2C, Dot(n2, a0));
        for (int j = 0; j < 3; j++) Wr(tri + 0x30 + j * 4, verts[v[j]]);
        Wr(tri + 0x3C, edge(v[1], v[2]));
        Wr(tri + 0x40, edge(v[0], v[2]));
        Wr(tri + 0x44, edge(v[0], v[1]));
      }
  return t;
}

// A patch collide with random facets (it is lifted on both sides).
uint32_t NewPatchCollide(Arena& a, std::mt19937& g, const Map& m, V3* lo, V3* hi) {
  const V3 c = RandomPoint(g, m.world);
  const V3 h{Uniform(g, 8, 300), Uniform(g, 8, 300), Uniform(g, 8, 100)};
  *lo = Sub(c, h);
  *hi = {c.x + h.x, c.y + h.y, c.z + h.z};
  const uint32_t numPlanes = 4 + g() % 20, numFacets = g() % 8;
  const uint32_t planes = a.Alloc(numPlanes * 0x14);
  for (uint32_t i = 0; i < numPlanes; i++) {
    const V3 n = RandomDir(g);
    WrV(planes + i * 0x14, n);
    WrF(planes + i * 0x14 + 0xC, Dot(n, c) + Uniform(g, -h.x, h.x));
    Wr(planes + i * 0x14 + 0x10, (n.x < 0) | (n.y < 0) << 1 | (n.z < 0) << 2);
  }
  const uint32_t facets = a.Alloc(numFacets * 0x140 + 4);
  for (uint32_t f = 0; f < numFacets; f++) {
    const uint32_t fa = facets + f * 0x140;
    Wr(fa, g() % numPlanes);
    const uint32_t borders = g() % 9;
    Wr(fa + 4, borders);
    for (uint32_t b = 0; b < 26; b++) {
      Wr(fa + 8 + b * 4, g() % numPlanes);
      Wr(fa + 0x70 + b * 4, g() % 2);
      Wr(fa + 0xD8 + b * 4, g() % 2);
    }
  }
  const uint32_t pc = a.Alloc(0x10);
  Wr(pc, numPlanes);
  Wr(pc + 4, planes);
  Wr(pc + 8, numFacets);
  Wr(pc + 0xC, facets);
  return pc;
}

uint32_t NewLeaf(Arena& a, std::mt19937& g, Map& m, std::vector<int32_t>& lb,
                 std::vector<int32_t>& ls) {
  const uint32_t leaf = a.Alloc(0x10);
  Wr(leaf, g());
  Wr(leaf + 4, uint32_t(lb.size()));
  const uint32_t nb = g() % 10;
  for (uint32_t i = 0; i < nb; i++) lb.push_back(g() % m.numBrushes);
  g_mem.write<uint16_t>(leaf + 8, uint16_t(nb));
  g_mem.write<uint16_t>(leaf + 0xA, uint16_t(ls.size()));
  const uint32_t ns = m.numPatches ? g() % 4 : 0;
  for (uint32_t i = 0; i < ns; i++) ls.push_back(g() % m.numPatches);
  g_mem.write<uint16_t>(leaf + 0xC, uint16_t(ns));
  g_mem.write<uint16_t>(leaf + 0xE, uint16_t(g()));
  return leaf;
}

void NewMap(std::mt19937& g, Map& m, uint32_t special) {
  std::memset(g_mem.ptr(kArena), 0, kArenaSize);
  Arena a;
  m = Map{};
  m.world = Uniform(g, 256, 4096);
  m.integral = g() % 2;
  m.numShaders = 4 + g() % 28;
  m.numBrushes = 8 + g() % 120;
  m.numPatches = g() % 24;
  const uint32_t numLeafs = 2 + g() % 40;
  m.numModels = 1 + g() % 5;

  const uint32_t shaders = a.Alloc(m.numShaders * 0x48);
  for (uint32_t i = 0; i < m.numShaders; i++) {
    Wr(shaders + i * 0x48 + 0x40, g());
    Wr(shaders + i * 0x48 + 0x44, g());
  }
  Wr(kShaders, shaders);
  // The box brush comes after the map's.
  const uint32_t brushes = a.Alloc((m.numBrushes + 1) * 0x34);
  for (uint32_t i = 0; i <= m.numBrushes; i++) NewBrush(a, g, m, brushes + i * 0x34, special);
  Wr(kBrushes, brushes);
  Wr(kBoxBrush, brushes + m.numBrushes * 0x34);

  const uint32_t patches = a.Alloc(m.numPatches * 0x2C + 4);
  for (uint32_t i = 0; i < m.numPatches; i++) {
    const uint32_t p = patches + i * 0x2C;
    V3 lo, hi;
    Wr(p, 0);
    Wr(p + 4, g() % m.numShaders);
    Wr(p + 8, g() % 4 ? 1 : g());
    if (g() % 3) {
      Wr(p + 0x28, NewTerrain(a, g, m, &lo, &hi, special));
    } else {
      Wr(p + 0x24, NewPatchCollide(a, g, m, &lo, &hi));
    }
    if (g() % 8 == 0) {  // bounds that miss now and then
      lo.z += 1000;
      hi.z += 1000;
    }
    WrV(p + 0xC, lo);
    WrV(p + 0x18, hi);
  }
  Wr(kPatches, patches);

  std::vector<int32_t> lb, ls;
  const uint32_t leafs = a.Alloc(numLeafs * 0x10);
  for (uint32_t i = 0; i < numLeafs; i++) {
    const uint32_t l = NewLeaf(a, g, m, lb, ls);
    std::memcpy(g_mem.ptr(leafs + i * 0x10), g_mem.ptr(l), 0x10);
  }
  Wr(kLeafs, leafs);
  // Inline models and the box model's leaf: brushes and surfaces too.
  const uint32_t models = a.Alloc(m.numModels * 0x28);
  for (uint32_t i = 0; i < m.numModels; i++) {
    const uint32_t md = models + i * 0x28;
    WrV(md, {-m.world, -m.world, -m.world});
    WrV(md + 0xC, {m.world, m.world, m.world});
    const uint32_t l = NewLeaf(a, g, m, lb, ls);
    std::memcpy(g_mem.ptr(md + 0x18), g_mem.ptr(l), 0x10);
  }
  Wr(kNumSubModels, m.numModels);
  Wr(kCModels, models);
  Wr(kBoxModel + 0x18 + 4, uint32_t(lb.size()));
  lb.push_back(int32_t(m.numBrushes));
  g_mem.write<uint16_t>(kBoxModel + 0x18 + 8, 1);
  g_mem.write<uint16_t>(kBoxModel + 0x18 + 0xC, 0);

  const uint32_t lbva = a.Alloc(uint32_t(lb.size()) * 4 + 4);
  for (size_t i = 0; i < lb.size(); i++) Wr(lbva + i * 4, uint32_t(lb[i]));
  const uint32_t lsva = a.Alloc(uint32_t(ls.size()) * 4 + 4);
  for (size_t i = 0; i < ls.size(); i++) Wr(lsva + i * 4, uint32_t(ls[i]));
  Wr(kLeafBrushes, lbva);
  Wr(kLeafSurfaces, lsva);

  // The tree: each node splits a range of leaves.
  const uint32_t nodes = a.Alloc(numLeafs * 8);
  uint32_t numNodes = 0;
  auto build = [&](auto&& self, uint32_t first, uint32_t count) -> int32_t {
    if (count == 1) return -1 - int32_t(first);
    const uint32_t n = numNodes++;
    V3 dir;
    if (g() % 3) {
      const int axis = g() % 3;
      dir = {axis == 0 ? 1.f : 0.f, axis == 1 ? 1.f : 0.f, axis == 2 ? 1.f : 0.f};
    } else {
      dir = RandomDir(g);
    }
    const float dist = Uniform(g, -m.world, m.world);
    Wr(nodes + n * 8, NewPlane(a, g, dir, m.integral ? std::round(dist) : dist, special));
    const uint32_t left = 1 + g() % (count - 1);
    const int32_t c0 = self(self, first, left), c1 = self(self, first + left, count - left);
    g_mem.write<int16_t>(nodes + n * 8 + 4, int16_t(c0));
    g_mem.write<int16_t>(nodes + n * 8 + 6, int16_t(c1));
    return int32_t(n);
  };
  build(build, 0, numLeafs);
  Wr(kNodes, nodes);

  const uint32_t cvar = a.Alloc(0x40);
  Wr(cvar + 0x20, g() % 8 == 0);
  Wr(kNoCurves, cvar);
  const uint32_t clip = a.Alloc(0x40);
  Wr(clip + 0x20, g() % 4 != 0);
  Wr(kPlayerCurveClip, clip);
  Wr(kCheckcount, g() % 1000);
  Wr(kBoxWarned, 1);  // the warning would print through the console (imports)
  m.leafBrushes = lbva;
  m.leafSurfaces = lsva;
  // Scratch for the cases from here on.
  Wr(kArena + kArenaSize - 4, a.top);
}

uint32_t ArenaTop() { return Rd(kArena + kArenaSize - 4); }

// A vector for a case: a point in the world, a box size, or (now and then)
// special values.
uint32_t NewVec(Arena& a, std::mt19937& g, V3 v, uint32_t special) {
  const uint32_t va = a.Alloc(12);
  WrV(va, v);
  for (int i = 0; i < 3; i++)
    if (special && g() % 1024 < special) WrF(va + i * 4, Value(g, -1, 1, 1024));
  return va;
}

// A point in or next to something the model can hit: the box model, or a
// brush or surface of its leaf (the world's: any).
V3 InBounds(std::mt19937& g, uint32_t bounds) {
  V3 p;
  float* out = &p.x;
  for (int i = 0; i < 3; i++) {
    const float lo = RdF(bounds + i * 4), hi = RdF(bounds + 0xC + i * 4), pad = (hi - lo) * 0.2f;
    out[i] = Uniform(g, lo - pad, hi + pad);
  }
  return p;
}
V3 Near(std::mt19937& g, const Map& m, int32_t model) {
  if (model == 0x1FE || model == 0x1FF) return InBounds(g, kBoxModel);
  uint32_t numBrushes, firstBrush, numSurfaces, firstSurface;
  if (model) {
    const uint32_t leaf = Rd(kCModels) + uint32_t(model) * 0x28 + 0x18;
    firstBrush = Rd(leaf + 4);
    numBrushes = g_mem.read<uint16_t>(leaf + 8);
    firstSurface = g_mem.read<uint16_t>(leaf + 0xA);
    numSurfaces = g_mem.read<uint16_t>(leaf + 0xC);
  } else {
    numBrushes = m.numBrushes;
    numSurfaces = m.numPatches;
    firstBrush = firstSurface = 0;
  }
  if (numSurfaces && (g() % 2 || !numBrushes)) {
    const uint32_t i = model ? Rd(m.leafSurfaces + (firstSurface + g() % numSurfaces) * 4)
                             : g() % numSurfaces;
    return InBounds(g, Rd(kPatches) + i * 0x2C + 0xC);
  }
  if (numBrushes) {
    const uint32_t i = model ? Rd(m.leafBrushes + (firstBrush + g() % numBrushes) * 4)
                             : g() % numBrushes;
    return InBounds(g, Rd(kBrushes) + i * 0x34 + 4);
  }
  return RandomPoint(g, m.world);
}

// A capsule (a cube's: no offset) starting on a terrain vertex's sphere or
// an edge's cylinder and moving into it, with a small fraction to beat:
// the edge and vertex hits of a fraction already near 0. Or not moving,
// about a radius from it.
bool GrazeCase(std::mt19937& g, const Map& m, Stats& s) {
  for (uint32_t model = 1; model < m.numModels; model++) {
    const uint32_t leaf = Rd(kCModels) + model * 0x28 + 0x18;
    const uint32_t first = g_mem.read<uint16_t>(leaf + 0xA), n = g_mem.read<uint16_t>(leaf + 0xC);
    for (uint32_t i = 0; i < n; i++) {
      const uint32_t p = Rd(kPatches) + Rd(m.leafSurfaces + (first + i) * 4) * 0x2C;
      const uint32_t t = Rd(p + 0x28);
      if (!t || g() % 2) continue;
      const uint32_t tri = t + 4 + (g() % g_mem.read<uint16_t>(t)) * 0x48;
      const int j = g() % 3;
      const bool edge = g() % 2;
      const uint32_t at = Rd(tri + (edge ? 0x3C : 0x30) + j * 4);
      if (!at) continue;
      Arena a;
      a.top = ArenaTop();
      const float r = Uniform(g, 4, 24), big = r + 0.125f;
      const V3 dir = RandomDir(g);
      V3 c{RdF(at + 4), RdF(at + 8), RdF(at + 0xC)};
      V3 out = dir;
      if (edge) {  // across the edge, from a point along it
        const V3 axis{RdF(at + 0x28), RdF(at + 0x2C), RdF(at + 0x30)};
        const float along = Uniform(g, 0, RdF(at + 0x34));
        c = {c.x + axis.x * along, c.y + axis.y * along, c.z + axis.z * along};
        out = Normalized(Sub(dir, Scale(axis, Dot(dir, axis))));
      }
      const float d = big * (1 + Uniform(g, -4e-7f, 4e-7f));
      V3 st{c.x + out.x * d, c.y + out.y * d, c.z + out.z * d};
      const float len = Uniform(g, 16, 2048);
      V3 en{st.x - out.x * len, st.y - out.y * len, st.z - out.z * len};
      float rr = r;
      if (m.integral && !edge && g() % 2) {
        // Straight down past the vertex's sphere, touching it: an exact
        // zero discriminant.
        const float bigR = float(3 + g() % 20), up = float(1 + g() % 64);
        rr = bigR - 0.125f;
        st = {c.x + bigR, c.y, c.z + up};
        en = {st.x, st.y, st.z - up - float(1 + g() % 64)};
      }
      if (g() % 3 == 0) {
        // No move, about a radius from it: the terrain position test.
        const float near = rr * Uniform(g, 0.5f, 1.5f);
        st = {c.x + out.x * near, c.y + out.y * near, c.z + out.z * near};
        en = st;
      }
      Call k{};
      k.fn = kTrace;
      k.results = a.Alloc(0x30);
      static const float kFracs[] = {1e-6f, 5e-6f, 1e-5f, 2e-5f, 1.0f};
      WrF(k.results, kFracs[g() % 5]);
      const uint32_t args[] = {k.results,    NewVec(a, g, st, 0), NewVec(a, g, en, 0), model,
                               0x537E54, 0x2000001,          1};
      std::copy(std::begin(args), std::end(args), k.args);
      k.edx = NewVec(a, g, {-rr, -rr, -rr}, 0);
      k.ecx = NewVec(a, g, {rr, rr, rr}, 0);
      Run(k, {{kArena, a.top - kArena}}, g, s, "graze");
      return true;
    }
  }
  return false;
}

void RandomCase(std::mt19937& g, const Map& m, uint32_t special, Stats& s) {
  Arena a;
  a.top = ArenaTop();
  // The temporary box model, as CM_TempBoxModel leaves it.
  V3 bc = RandomPoint(g, m.world);
  V3 bh{Uniform(g, 1, 64), Uniform(g, 1, 64), Uniform(g, 1, 96)};
  if (g() % 3 == 0) {
    bc = {std::round(bc.x), std::round(bc.y), std::round(bc.z)};
    bh = {std::round(bh.x), std::round(bh.y), std::round(bh.z)};
  }
  WrV(kBoxModel, Sub(bc, bh));
  WrV(kBoxModel + 0xC, {bc.x + bh.x, bc.y + bh.y, bc.z + bh.z});
  const uint32_t box = Rd(kBoxBrush);
  std::memcpy(g_mem.ptr(box + 4), g_mem.ptr(kBoxModel), 24);
  Wr(box, g() % 4 ? 1 : g());

  Call k{};
  const uint32_t pick = g() % 16;
  k.fn = pick < 6 ? kBoxTrace : pick < 10 ? kTransformedBoxTrace : pick < 13 ? kTrace : kTransformed;
  const uint32_t mp = g() % 16;
  int32_t model = mp < 7 ? 0 : mp < 11 ? 0x1FE : mp < 12 ? 0x1FF : 0;
  if (mp >= 12 && m.numModels > 1) model = int32_t(1 + g() % (m.numModels - 1));
  V3 st = g() % 5 < 3 ? Near(g, m, model) : RandomPoint(g, m.world);
  V3 en = st;
  if (g() % 8) en = g() % 3 ? RandomPoint(g, m.world)
                            : V3{st.x + Uniform(g, -64, 64), st.y + Uniform(g, -64, 64),
                                 st.z + Uniform(g, -64, 64)};
  // As the game's often are: on whole units, level, or straight down.
  const bool integral = g() % 3 == 0;
  if (integral) {
    st = {std::round(st.x), std::round(st.y), std::round(st.z)};
    en = {std::round(en.x), std::round(en.y), std::round(en.z)};
  }
  const uint32_t dir = g() % 10;
  if (dir < 2) en.z = st.z;
  if (dir == 2) en = {st.x, st.y, en.z};
  const uint32_t start = NewVec(a, g, st, special), end = NewVec(a, g, en, special);
  if (special && g() % 8 == 0) {  // the same special value at both ends (inf - inf)
    const int i = g() % 3;
    const float v = Value(g, -1, 1, 1024);
    WrF(start + i * 4, v);
    WrF(end + i * 4, v);
  }
  uint32_t mins = 0, maxs = 0;
  const uint32_t size = g() % 4;
  if (size) {
    V3 h{Uniform(g, 1, 48), Uniform(g, 1, 48), Uniform(g, 1, 72)};
    float lift = g() % 2 ? Uniform(g, -h.z, h.z) : 0;
    if (integral) {
      h = {std::round(h.x), std::round(h.y), std::round(h.z)};
      lift = std::round(lift);
    }
    mins = NewVec(a, g, size == 1 ? V3{0, 0, 0} : V3{-h.x, -h.y, -h.z + lift}, special);
    maxs = NewVec(a, g, size == 1 ? V3{0, 0, 0} : V3{h.x, h.y, h.z + lift}, special);
    if (g() % 8 == 0) mins = 0;
  }
  const uint32_t brushmask = g() % 4 ? 0x2000001 | (g() % 2 ? 0x10000 : 0) : g();
  const uint32_t capsule = g() % 3 == 0;
  k.results = a.Alloc(0x30);
  for (int i = 0; i < 12; i++) Wr(k.results + i * 4, g());
  WrF(k.results, g() % 4 ? 1.0f : Value(g, 0, 1.2f, 32));

  if (k.fn == kBoxTrace) {
    k.eax = k.results;
    k.edx = brushmask;
    k.ecx = capsule;
    const uint32_t args[] = {start, end, mins, maxs, uint32_t(model)};
    std::copy(std::begin(args), std::end(args), k.args);
  } else if (k.fn == kTrace) {
    uint32_t sphere = 0;
    if (g() % 3 == 0) {
      sphere = a.Alloc(0x24);
      Wr(sphere, g() % 4 != 0);
      const float r = Value(g, 1, 32, special), hh = Value(g, 1, 64, special);
      WrF(sphere + 4, r);
      WrF(sphere + 8, hh);
      WrV(sphere + 0xC, {Value(g, -8, 8, special), Value(g, -8, 8, special), hh - r});
      for (int i = 0; i < 3; i++) Wr(sphere + 0x18 + i * 4, g());
    }
    k.eax = sphere;
    k.edx = mins;
    k.ecx = maxs;
    const uint32_t args[] = {k.results, start, end, uint32_t(model), 0x537E54, brushmask, capsule};
    std::copy(std::begin(args), std::end(args), k.args);
  } else {
    const uint32_t origin = NewVec(a, g, g() % 2 ? V3{0, 0, 0} : RandomPoint(g, m.world), special);
    const uint32_t angles =
        NewVec(a, g, g() % 3 ? V3{0, 0, 0} : V3{Uniform(g, -360, 360), Uniform(g, -360, 360),
                                                 Uniform(g, -360, 360)},
               special);
    if (k.fn == kTransformedBoxTrace) {
      k.eax = k.results;
      k.edx = origin;
      k.ecx = capsule;
      const uint32_t args[] = {start, end, mins, maxs, uint32_t(model), brushmask, angles};
      std::copy(std::begin(args), std::end(args), k.args);
    } else {
      k.eax = mins;
      k.ecx = maxs;
      k.edx = angles;
      const uint32_t args[] = {k.results, start, end, uint32_t(model), brushmask, origin, capsule};
      std::copy(std::begin(args), std::end(args), k.args);
    }
  }
  // Everything a trace may write: the whole arena (map and case).
  Run(k, {{kArena, a.top - kArena}}, g, s, "random");
}

// ---- captures ------------------------------------------------------------------

bool ReadRegions(FILE* f, std::vector<Window>* out) {
  uint32_t n;
  if (std::fread(&n, 4, 1, f) != 1) return false;
  for (uint32_t i = 0; i < n; i++) {
    uint32_t r[2];
    if (std::fread(r, 4, 2, f) != 2) return false;
    if (!g_mem.IsCommitted(r[0]) || !g_mem.IsCommitted(r[0] + r[1] - 1))
      g_mem.Commit(r[0], r[1], recomp::Prot::RW);
    if (std::fread(g_mem.ptr(r[0]), 1, r[1], f) != r[1]) return false;
    out->push_back({r[0], r[1]});
  }
  return true;
}

bool RunCaptures(const char* path, std::mt19937& g, Stats& s) {
  FILE* f = std::fopen(path, "rb");
  if (!f) {
    std::printf("%s: cannot open\n", path);
    return false;
  }
  std::vector<Window> map;
  int calls = 0, maps = 0;
  uint32_t tag;
  while (std::fread(&tag, 4, 1, f) == 1) {
    if (tag == kMap) {
      map.clear();
      if (!ReadRegions(f, &map)) return false;
      maps++;
      Wr(kBoxWarned, 1);
    } else if (tag == kCall) {
      Call k{};
      uint32_t head[4];
      if (std::fread(head, 4, 4, f) != 4 || std::fread(k.args, 4, kArgs, f) != kArgs) return false;
      k.fn = head[0];
      k.eax = head[1];
      k.ecx = head[2];
      k.edx = head[3];
      k.results = k.fn == kBoxTrace || k.fn == kTransformedBoxTrace ? k.eax : k.args[0];
      std::vector<Window> w = map;
      if (!ReadRegions(f, &w)) return false;
      Run(k, w, g, s, path);
      calls++;
      uint8_t game[0x30];
      if (std::fread(&tag, 4, 1, f) != 1 || tag != kResult || std::fread(game, 1, 0x30, f) != 0x30)
        return false;
      // The lifted original, on the recorded clip map, gives the game's
      // result: the record holds all the call reads.
      s.game++;
      if (std::memcmp(game, g_mem.ptr(k.results), 0x30)) {
        if (s.game_differs++ < 10)
          std::printf("%s: call %d: the lifted result differs from the game's\n", path, calls);
      }
    } else {
      std::printf("%s: bad record after %d calls\n", path, calls);
      return false;
    }
  }
  std::printf("%s: %d clip maps, %d captured calls, %d results not the game's\n", path, maps,
              calls, s.game_differs);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);  // messages before an abort
  if (argc < 2) {
    std::printf("usage: %s CoDSP.exe [CAPTURE...]\n", argv[0]);
    return 2;
  }
  if (!g_mem.Reserve()) return 1;
  recomp::g_guest_base = g_mem.base();
  recomp::PeImage exe;
  recomp::g_exe = &exe;
  std::string err;
  if (!exe.Load(argv[1], g_mem, &err)) {
    std::printf("%s: %s\n", argv[1], err.c_str());
    return 1;
  }
  g_mem.Commit(kArena, kArenaSize, recomp::Prot::RW);
  g_mem.Commit(kStack, kStackSize, recomp::Prot::RW);
  g_mem.Commit(recomp::kPtd, 0x1000, recomp::Prot::RW);
  // The reference: the x87 model's exact arithmetic throughout.
  recomp::x87::run::g_doubles = false;

  std::mt19937 g(20261018);
  Stats s;
  Map m;
  for (int map = 0; map < 300; map++) {
    // Ordinary maps and traces, then more and more special values.
    const uint32_t special = map < 100 ? 0 : map < 200 ? 4 : 48;
    NewMap(g, m, special);
    for (int i = 0; i < 1000; i++)
      if (i % 8 || !GrazeCase(g, m, s)) RandomCase(g, m, special, s);
  }
  const int random = s.cases;
  for (int i = 2; i < argc; i++)
    if (!RunCaptures(argv[i], g, s)) return 1;
  std::printf("%d random + %d captured calls (CM_Trace %d, CM_BoxTrace %d, 0x41D3B0 %d, "
              "CM_TransformedBoxTrace %d; %d hits, %d start solid, %d NaN fractions): "
              "%d mismatched\n",
              random, s.cases - random, s.by_fn[0], s.by_fn[1], s.by_fn[2], s.by_fn[3], s.hits,
              s.startsolid, s.nan, s.failures);
  return s.failures || s.game_differs ? 1 : 0;
}
