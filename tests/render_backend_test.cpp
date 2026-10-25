// The native renderer back end (src/render_backend.cpp) against the
// originals, lifted from the EXE at build time with `recomp_lift --only`
// (CMakeLists.txt) and run on x86recomp's exact x87 model. CoDSP.exe is
// mapped as the game maps it, for its constants and globals. The renderer's
// qgl pointers (and ri.Printf, ri.Error and the element function of the
// strips) point at recorders that log each call: the GL entry point, its
// return address, ESP and stack arguments (and the four floats glFogfv
// reads), pop the arguments as the stdcall entry points do, and leave EAX,
// ECX and EDX changed. Each case runs one entry through the lifted original
// and the native code from the same guest memory, and both must leave the
// same bytes in the renderer's globals, the case's structures and the stack
// arguments, the same calls, and the same ESP, EBX, ESI, EDI, EBP and x87
// stack (top, empty registers, control word). Not compared: EAX, ECX, EDX,
// EFLAGS, the x87 status word and the stack below the return address.
//
//   render_backend_test EXE
//
// Random cases (mt19937, fixed seed): random cached GL state (state bits,
// client arrays, cull type, units, bound textures, environments, targets),
// cvars, images, fog colours (now and then zeros, denormals, infinities,
// NaNs or huge), and index lists for the strips that continue a strip or
// start a new one at random.

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "runtime/guest_memory.h"
#include "runtime/lifted.h"
#include "runtime/pe_image.h"
#include "runtime/x87.h"

namespace recomp {
uint8_t* g_guest_base = nullptr;
PeImage* g_exe = nullptr;
}  // namespace recomp

namespace {

using recomp::Ctx;

recomp::GuestMemory g_mem;

uint32_t Rd(uint32_t va) { return g_mem.read<uint32_t>(va); }
void Wr(uint32_t va, uint32_t v) { g_mem.write<uint32_t>(va, v); }

// The entry points the functions call: the pointer they call through, its
// stdcall argument count (-1: cdecl, the caller pops), and the argument
// pointing at floats the callee reads (with their count).
struct Gl {
  uint32_t ptr;
  const char* name;
  int args;
  int ptr_arg = -1, floats = 0;
};
constexpr Gl kGl[] = {
    {0x1534F68, "glEnable", 1},
    {0x15349E0, "glDisable", 1},
    {0x153507C, "glBindTexture", 2},
    {0x153515C, "glTexEnvi", 3},
    {0x1535284, "glCullFace", 1},
    {0x1534844, "glDepthFunc", 1},
    {0x1534D68, "glDepthMask", 1},
    {0x1534D74, "glPolygonMode", 2},
    {0x1535208, "glAlphaFunc", 2},
    {0x1535050, "glColor3f", 3},
    {0x15350B0, "glPolygonOffset", 2},
    {0x1534ABC, "glBlendFunc", 2},
    {0x15349F4, "glFogfv", 2, 1, 4},
    {0x1534938, "glEnableClientState", 1},
    {0x153495C, "glDisableClientState", 1},
    {0x1534B78, "glClientActiveTextureARB", 1},
    {0x1535360, "glBegin", 1},
    {0x1534EE0, "glEnd", 0},
    {0x153513C, "glDrawElements", 4},
    {0x1535104, "glArrayElement", 1},
    {0x1549600, "ri.Printf", -1},
    {0x1549604, "ri.Error", -1},
};
constexpr int kNumGl = sizeof(kGl) / sizeof(kGl[0]);
constexpr uint32_t kGlTarget = 0x7FFD0000;  // + 0x10 * index
constexpr uint32_t kLockArrays = 0x1534DDC;  // only tested for null

struct CallRec {
  uint32_t target, ret, esp, args[4];
  uint32_t data[4];
  bool operator==(const CallRec&) const = default;
};
std::vector<CallRec> g_calls;

}  // namespace

namespace recomp {
namespace gen {
// The originals...
void f_004C89D0_lifted(Ctx& c);
void f_004C8A20_lifted(Ctx& c);
void f_004CB3B0_lifted(Ctx& c);
void f_004CB5D0_lifted(Ctx& c);
void f_004CB640_lifted(Ctx& c);
void f_004CB710_lifted(Ctx& c);
void f_004CBBE0_lifted(Ctx& c);
void f_004F3180_lifted(Ctx& c);
void f_004F32C0_lifted(Ctx& c);
// ...call each other, not the native code (tests/render_backend_native.cpp
// renames its entries).
void f_004C89D0(Ctx& c) { f_004C89D0_lifted(c); }
void f_004C8A20(Ctx& c) { f_004C8A20_lifted(c); }
void f_004CB3B0(Ctx& c) { f_004CB3B0_lifted(c); }
void f_004CB5D0(Ctx& c) { f_004CB5D0_lifted(c); }
void f_004CB640(Ctx& c) { f_004CB640_lifted(c); }
void f_004CB710(Ctx& c) { f_004CB710_lifted(c); }
void f_004CBBE0(Ctx& c) { f_004CBBE0_lifted(c); }
void f_004F3180(Ctx& c) { f_004F3180_lifted(c); }
void f_004F32C0(Ctx& c) { f_004F32C0_lifted(c); }
void n_004C89D0(Ctx& c);
void n_004C8A20(Ctx& c);
void n_004CB3B0(Ctx& c);
void n_004CB5D0(Ctx& c);
void n_004CB640(Ctx& c);
void n_004CB710(Ctx& c);
void n_004CBBE0(Ctx& c);
void n_004F3180(Ctx& c);
void n_004F32C0(Ctx& c);
}  // namespace gen

void Dispatch(Ctx& c, uint32_t target, uint32_t va) {
  const uint32_t i = (target - kGlTarget) / 0x10;
  if (target < kGlTarget || i >= uint32_t(kNumGl) || target != kGlTarget + 0x10 * i) {
    std::printf("unexpected call to %08X from %08X\n", target, va);
    std::abort();
  }
  const Gl& gl = kGl[i];
  CallRec r{};
  r.target = target;
  r.esp = c.esp;
  r.ret = Rd(c.esp);
  // ri.Printf(level, fmt) and ri.Error(code, fmt), with the environment
  // after the texture environment's message.
  const int n = gl.args >= 0 ? gl.args : Rd(c.esp + 8) == 0x54303C ? 3 : 2;
  for (int k = 0; k < n; k++) r.args[k] = Rd(c.esp + 4 + 4 * uint32_t(k));
  if (gl.ptr_arg >= 0)
    for (int k = 0; k < gl.floats; k++) r.data[k] = Rd(r.args[gl.ptr_arg] + 4 * uint32_t(k));
  g_calls.push_back(r);
  const uint32_t junk = 0xC0DE0000u + uint32_t(g_calls.size());
  c.eax = junk;
  c.ecx = junk ^ 1;
  c.edx = junk ^ 2;
  c.esp += 4 + 4 * uint32_t(gl.args < 0 ? 0 : gl.args);
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

using Fn = void (*)(Ctx&);

struct Entry {
  uint32_t va;
  Fn lifted, native;
  const char* name;
};
#define COD1_ENTRY(va, name) {0x##va, recomp::gen::f_##va##_lifted, recomp::gen::n_##va, name}
const Entry kEntries[] = {
    COD1_ENTRY(004C89D0, "fog on"),        COD1_ENTRY(004C8A20, "fog colour"),
    COD1_ENTRY(004CB3B0, "GL_Bind"),       COD1_ENTRY(004CB5D0, "cull"),
    COD1_ENTRY(004CB640, "texture env"),   COD1_ENTRY(004CB710, "state bits"),
    COD1_ENTRY(004CBBE0, "client arrays"), COD1_ENTRY(004F3180, "strips"),
    COD1_ENTRY(004F32C0, "draw elements"),
};
#undef COD1_ENTRY
constexpr int kNumEntries = sizeof(kEntries) / sizeof(kEntries[0]);

// Random structures live in the arena, reset per case; the stack below.
constexpr uint32_t kArena = 0x70000000, kArenaSize = 0x100000;
constexpr uint32_t kStack = 0x6FF00000, kStackSize = 0x100000;

struct Window {
  uint32_t va, size;
};
// The globals the functions read and write: the renderer's GL state and
// qgl pointers, the refdef and counters, the world's fog, the strip
// counters, and the first page (a null image's fields).
const Window kGlobals[] = {
    {0x1534000, 0x3000}, {0x1549000, 0x1000}, {0x13F9000, 0x1000},
    {0x11E0000, 0x1000}, {0x0, 0x100},
};

struct Stats {
  int cases = 0, failures = 0, calls = 0, by_entry[kNumEntries] = {};
};

std::vector<uint8_t> Save(const std::vector<Window>& w) {
  std::vector<uint8_t> out;
  for (const Window& x : w) out.insert(out.end(), g_mem.ptr<uint8_t>(x.va), g_mem.ptr<uint8_t>(x.va) + x.size);
  return out;
}
void Load(const std::vector<Window>& w, const std::vector<uint8_t>& in) {
  size_t at = 0;
  for (const Window& x : w) {
    std::memcpy(g_mem.ptr<uint8_t>(x.va), in.data() + at, x.size);
    at += x.size;
  }
}

struct Call {
  int entry;
  uint32_t eax = 0, edx = 0, esi = 0, edi = 0;
  std::vector<uint32_t> args;
};

void PrintCall(const char* side, size_t i, const CallRec& r) {
  const uint32_t k = (r.target - kGlTarget) / 0x10;
  std::printf("  %zu %s: %s ret %08X esp %08X args %08X %08X %08X %08X data %08X %08X %08X %08X\n",
              i, side, k < uint32_t(kNumGl) ? kGl[k].name : "?", r.ret, r.esp, r.args[0],
              r.args[1], r.args[2], r.args[3], r.data[0], r.data[1], r.data[2], r.data[3]);
}

void Run(const Call& k, std::mt19937& g, Stats& s) {
  const Entry& e = kEntries[k.entry];
  std::vector<Window> w(std::begin(kGlobals), std::end(kGlobals));
  w.push_back({kArena, kArenaSize});
  const uint32_t esp = kStack + kStackSize - 0x1000;
  Wr(esp, 0xDEAD0000);
  for (size_t i = 0; i < k.args.size(); i++) Wr(esp + 4 + 4 * uint32_t(i), k.args[i]);
  w.push_back({esp, 4 + 4 * uint32_t(k.args.size())});
  const std::vector<uint8_t> before = Save(w);
  Ctx c0{};
  c0.eax = k.eax;
  c0.ecx = g();
  c0.edx = k.edx;
  c0.ebx = g();
  c0.esi = k.esi;
  c0.edi = k.edi;
  c0.ebp = g();
  c0.esp = esp;
  recomp::x87::Init(c0.fpu);
  recomp::x87::SetControlWord(c0.fpu, 0x027F);
  for (uint32_t i = g() % 3; i; i--) recomp::x87::LoadF32(c0.fpu, g());
  c0.mxcsr = recomp::sse::kMxcsrDefault;

  Ctx cl = c0;
  g_calls.clear();
  e.lifted(cl);
  const std::vector<uint8_t> after = Save(w);
  const std::vector<CallRec> lcalls = g_calls;
  Load(w, before);
  Ctx cn = c0;
  g_calls.clear();
  e.native(cn);
  const std::vector<uint8_t> got = Save(w);

  s.cases++;
  s.by_entry[k.entry]++;
  s.calls += int(lcalls.size());
  bool ok = cn.esp == cl.esp && cn.ebp == cl.ebp && cn.ebx == cl.ebx && cn.esi == cl.esi &&
            cn.edi == cl.edi && cn.fpu.top == cl.fpu.top && cn.fpu.empty == cl.fpu.empty &&
            cn.fpu.cw == cl.fpu.cw;
  if (!ok && s.failures < 20)
    std::printf("%s: registers differ (esp %08X/%08X ebx %08X/%08X esi %08X/%08X edi "
                "%08X/%08X ebp %08X/%08X top %d/%d)\n",
                e.name, cl.esp, cn.esp, cl.ebx, cn.ebx, cl.esi, cn.esi, cl.edi, cn.edi, cl.ebp,
                cn.ebp, cl.fpu.top, cn.fpu.top);
  if (lcalls != g_calls) {
    if (s.failures < 20) {
      std::printf("%s: calls differ (%zu lifted, %zu native)\n", e.name, lcalls.size(),
                  g_calls.size());
      for (size_t i = 0; i < std::max(lcalls.size(), g_calls.size()); i++) {
        const bool have_l = i < lcalls.size(), have_n = i < g_calls.size();
        if (have_l && have_n && lcalls[i] == g_calls[i]) continue;
        if (have_l) PrintCall("lifted", i, lcalls[i]);
        if (have_n) PrintCall("native", i, g_calls[i]);
        break;
      }
    }
    ok = false;
  }
  size_t at = 0;
  for (const Window& x : w) {
    for (uint32_t i = 0; i < x.size; i++)
      if (got[at + i] != after[at + i]) {
        if (s.failures < 20) {
          const uint32_t a = (x.va + i) & ~3u;
          const size_t o = at + (a - x.va);
          uint32_t lv = 0, nv = 0;
          std::memcpy(&lv, after.data() + o, 4);
          std::memcpy(&nv, got.data() + o, 4);
          std::printf("%s: %08X lifted %08X native %08X\n", e.name, a, lv, nv);
        }
        ok = false;
        break;
      }
    at += x.size;
  }
  if (!ok) s.failures++;
}

// ---- random state -------------------------------------------------------------

struct Arena {
  uint32_t top = kArena;
  uint32_t Alloc(uint32_t size) {
    const uint32_t va = top;
    top += (size + 15) & ~15u;
    return va;
  }
  void Reset() {
    std::memset(g_mem.ptr<uint8_t>(kArena), 0, top - kArena);
    top = kArena;
  }
};

uint32_t Pick(std::mt19937& g, std::initializer_list<uint32_t> v) {
  return v.begin()[g() % v.size()];
}

// A float's bits: ordinary values, now and then special ones.
uint32_t RandFloat(std::mt19937& g) {
  switch (g() % 16) {
    case 0: return 0;
    case 1: return 0x80000000;
    case 2: return g() & 0x807FFFFF;               // denormal
    case 3: return 0x7F800000 | (g() & 0x80000000);  // infinity
    case 4: return 0x7FC00000 | (g() & 0x803FFFFF);  // QNaN
    case 5: return 0x7F800001 | (g() & 0x803FFFFE);  // SNaN
    case 6: return 0x7F000000 | (g() & 0x80FFFFFF);  // huge
    default: {
      float f = std::ldexp(float(g() % 2001) / 1000.0f - 1.0f, int(g() % 9) - 4);
      uint32_t b;
      std::memcpy(&b, &f, 4);
      return b;
    }
  }
}

uint32_t Cvar(Arena& a, std::mt19937& g, uint32_t value) {
  const uint32_t v = a.Alloc(0x40);
  Wr(v + 0x1C, RandFloat(g));
  Wr(v + 0x20, value);
  return v;
}

uint32_t Image(Arena& a, std::mt19937& g) {
  const uint32_t im = a.Alloc(0x60);
  Wr(im + 0x54, Pick(g, {0xDE1, 0xDE1, 0x8513, 0, uint32_t(g())}));
  Wr(im + 0x58, g() % 6);
  Wr(im + 0x5C, g());
  return im;
}

// State bits: mostly the meaningful fields, now and then anything.
uint32_t StateBits(std::mt19937& g) {
  if (g() % 8 == 0) return g();
  uint32_t b = 0;
  if (g() % 2) b |= (g() % 11) | ((g() % 10) << 4);
  b |= g() & 0x37F100;  // depth mask, line mode, depth, lighting, fog, ext
  b |= (g() % 4 == 0 ? g() & 0xE000 : 0);
  b |= (g() % 8) << 28;
  b |= g() & 0x3C00000;
  return b;
}

void Randomize(Arena& a, std::mt19937& g) {
  a.Reset();
  for (int i = 0; i < kNumGl; i++) Wr(kGl[i].ptr, kGlTarget + 0x10 * uint32_t(i));
  Wr(kLockArrays, g() % 3 ? 0x7FFC0000 : 0);
  Wr(0x1534630, g() % 4 ? StateBits(g) : 0);
  Wr(0x1534634, g() % 4 ? g() & 0x7FF : g());
  Wr(0x153462C, g() % 4);
  Wr(0x15345A0, g() % 8);
  Wr(0x15345A4, g() % 8);
  for (uint32_t u = 0; u < 8; u++) {
    Wr(0x15345A8 + 4 * u, g() % 6);
    Wr(0x15345C8 + 4 * u, Pick(g, {0x2100, 0x104, 0x1E01, 0x2101, 0, uint32_t(g())}));
    Wr(0x15345E8 + 4 * u, Pick(g, {0xDE1, 0x8513, 0, uint32_t(g())}));
  }
  Wr(0x1534460, Cvar(a, g, g() % 4 == 0));       // r_nobind
  Wr(0x15347F4, Cvar(a, g, g() % 3 != 0));       // fog
  Wr(0x15346C4, Cvar(a, g, g() % 3 == 0));       // lighting off
  Wr(0x15346A0, Cvar(a, g, Pick(g, {0, 0, 1, 2, 3})));  // r_primitives
  Wr(0x1534530, Cvar(a, g, 0));                  // polygon offset units
  Wr(0x153447C, Cvar(a, g, 0));                  // polygon offset factor
  const uint32_t lights = a.Alloc(0x120);
  Wr(lights + 0x118, g() % 2);
  Wr(0x1535A34, lights);
  Wr(0x1535A38, Image(a, g));
  Wr(0x1535ABC, g() % 2 ? Image(a, g) : 0);
  Wr(0x1535A24, g());
  Wr(0x1549930, g() % 2);
  Wr(0x1549B50, g() % 4 == 0);
  Wr(0x15496EC, g() & 0xFF);
  Wr(0x1535890, g() % 3 ? a.Alloc(16) : 0);
  Wr(0x13F9CF8, g() % 3 ? a.Alloc(16) : 0);
  for (uint32_t i = 0; i < 4; i++) Wr(0x1534640 + 4 * i, RandFloat(g));
  Wr(0x1536290, RandFloat(g));
  Wr(0x11E0760, g());
  Wr(0x11E0764, g());
  Wr(0x1549B14, g());
  Wr(0x1549B18, g());
}

// Triangles that continue a strip (the next triangle on the shared edge,
// in the winding a strip alternates) or start a new one.
uint32_t Indexes(Arena& a, std::mt19937& g, int tris) {
  const uint32_t p = a.Alloc(6 * uint32_t(tris) + 16);
  std::vector<uint16_t> v;
  uint16_t next = uint16_t(g() % 100);
  for (int t = 0; t < tris; t++) {
    const size_t n = v.size();
    if (t && g() % 3) {
      // Continue: (odd) a b c -> c b d, (even) a b c -> a c d; the strip
      // check takes whichever the parity says, so either may break it.
      if (g() % 2) v.insert(v.end(), {v[n - 1], v[n - 2], next++});
      else v.insert(v.end(), {v[n - 3], v[n - 1], next++});
    } else {
      v.insert(v.end(), {next, uint16_t(next + 1), uint16_t(next + 2)});
      next += 3;
    }
    if (g() % 16 == 0) next = uint16_t(g());
  }
  for (size_t i = 0; i < v.size(); i++) g_mem.write<uint16_t>(p + 2 * uint32_t(i), v[i]);
  return p;
}

void Cases(Arena& a, std::mt19937& g, Stats& s) {
  Randomize(a, g);
  Call k{};
  k.entry = int(g() % kNumEntries);
  switch (kEntries[k.entry].va) {
    case 0x4CB3B0:
      k.esi = g() % 32 ? Image(a, g) : 0;
      break;
    case 0x4CB5D0:
      k.esi = g() % 8 ? g() % 3 : g();
      break;
    case 0x4CB640:
      k.eax = Pick(g, {0x2100, 0x104, 0x1E01, 0x2101, 0x2100, 0x1E01, uint32_t(g() % 0x3000), uint32_t(g())});
      break;
    case 0x4CB710:
      k.eax = StateBits(g);
      break;
    case 0x4CBBE0:
      k.args = {uint32_t(g() % 4 ? g() & 0x7FF : g())};
      break;
    case 0x4F3180:
    case 0x4F32C0: {
      const int tris = int(g() % 24);
      k.eax = Indexes(a, g, tris);
      int n = 3 * tris;
      if (g() % 8 == 0) n = int(g() % 80) - 8;
      k.edi = kGlTarget + 0x10 * 19;  // glArrayElement
      if (kEntries[k.entry].va == 0x4F3180) k.args = {uint32_t(n)};
      else k.edx = uint32_t(n);
      break;
    }
  }
  Run(k, g, s);
}

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);  // messages before an abort
  if (argc < 2) {
    std::printf("usage: %s CoDSP.exe\n", argv[0]);
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
  // A null image's fields (GL_Bind reads them after its warning).
  g_mem.Commit(0, 0x1000, recomp::Prot::RW);
  recomp::x87::run::g_doubles = false;

  std::mt19937 g(20261025);
  Stats s;
  Arena a;
  for (int i = 0; i < 200000; i++) Cases(a, g, s);
  std::printf("%d random cases (", s.cases);
  for (int i = 0; i < kNumEntries; i++)
    std::printf("%s%s %d", i ? ", " : "", kEntries[i].name, s.by_entry[i]);
  std::printf("; %d calls out): %d mismatched\n", s.calls, s.failures);
  return s.failures ? 1 : 0;
}
