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
// argument count (popped by the callee unless cdecl), and the argument
// pointing at floats the callee reads (with their count).
struct Gl {
  uint32_t ptr;
  const char* name;
  int args;
  bool cdecl = false;
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
    {0x15349F4, "glFogfv", 2, false, 1, 4},
    {0x1534938, "glEnableClientState", 1},
    {0x153495C, "glDisableClientState", 1},
    {0x1534B78, "glClientActiveTextureARB", 1},
    {0x1535360, "glBegin", 1},
    {0x1534EE0, "glEnd", 0},
    {0x153513C, "glDrawElements", 4},
    {0x1535104, "glArrayElement", 1},
    {0x1534C14, "glActiveTextureARB", 1},
    {0x1534A10, "glTexEnvfv", 3, false, 2, 4},
    {0x15349B8, "glTexEnvf", 3},
    {0x15349A0, "glTexCoordPointer", 4},
    {0x1534C84, "glFogi", 2},
    {0x1535598, "glFogf", 2},
    {0x15351A0, "glHint", 2},
    {0x1535014, "qgl 0x1535014", 1},
    {0x1549600, "ri.Printf", 2, true},
    {0x1549604, "ri.Error", 2, true},
    {0x1549684, "ri 0x1549684", 1, true},
    {0x154967C, "ri 0x154967C", 1, true},
};
constexpr int kNumGl = sizeof(kGl) / sizeof(kGl[0]);
constexpr uint32_t kGlTarget = 0x7FFD0000;  // + 0x10 * index
constexpr uint32_t kLockArrays = 0x1534DDC;  // only tested for null

struct CallRec {
  uint32_t target, ret, esp, args[4];
  uint32_t data[4];
  uint32_t regs[7];  // stubs: the registers their callee reads
  bool operator==(const CallRec&) const = default;
};
std::vector<CallRec> g_calls;

enum Reg { kEax = 1, kEcx = 2, kEdx = 4, kEbx = 8, kEsi = 16, kEdi = 32, kEbp = 64 };
constexpr int kAll = 127;

// The lifted functions the native ones call (tests/render_backend_stubs.manifest),
// recorded as the GL calls are, with the registers each reads.
void Stub(Ctx& c, uint32_t va, int regs, int args) {
  CallRec r{};
  r.target = va;
  r.esp = c.esp;
  r.ret = Rd(c.esp);
  for (int k = 0; k < args; k++) r.args[k] = Rd(c.esp + 4 + 4 * uint32_t(k));
  const uint32_t all[7] = {c.eax, c.ecx, c.edx, c.ebx, c.esi, c.edi, c.ebp};
  for (int i = 0; i < 7; i++)
    if (regs & (1 << i)) r.regs[i] = all[i];
  g_calls.push_back(r);
  const uint32_t junk = 0xC0DE0000u + uint32_t(g_calls.size());
  c.eax = junk;
  c.ecx = junk ^ 1;
  c.edx = junk ^ 2;
  c.esp += 4;
}

}  // namespace

namespace recomp {
namespace gen {
// The stubs: the video generator's update (ECX; time, unit), the register
// combiners (combiners), the vertex program (ESI), the vertex deforms
// (every register: the dispatch passes them as the original leaves them),
// and the colour and alpha generators.
void f_004FCE80(Ctx& c) { Stub(c, 0x4FCE80, kEcx, 2); }
void f_004F3A70(Ctx& c) { Stub(c, 0x4F3A70, 0, 1); }
void f_004F3C70(Ctx& c) { Stub(c, 0x4F3C70, kEsi, 0); }
#define COD1_STUB(va, regs, args) \
  void f_##va(Ctx& c) { Stub(c, 0x##va, regs, args); }
COD1_STUB(00508AE0, kAll, 0)
COD1_STUB(00508C50, kAll, 0)
COD1_STUB(00508F00, kAll, 0)
COD1_STUB(00508DE0, kAll, 0)
COD1_STUB(00509040, kAll, 1)
COD1_STUB(005090E0, kAll, 0)
COD1_STUB(00508800, kAll, 0)
COD1_STUB(005095F0, kAll, 0)
COD1_STUB(005098A0, kAll, 0)
COD1_STUB(00509190, kAll, 0)
COD1_STUB(0050A030, kEcx, 0)
COD1_STUB(0050A070, kEcx, 0)
COD1_STUB(0050A170, kEax | kEsi, 0)
COD1_STUB(0050B040, 0, 1)
COD1_STUB(004F2E60, kEax | kEdi, 0)
COD1_STUB(0050A0E0, kEcx, 0)
COD1_STUB(0050A120, kEcx, 0)
COD1_STUB(0050AEC0, kEax, 0)
COD1_STUB(0050A270, kEax, 1)
#undef COD1_STUB
// unzCloseCurrentFile, tolower and the memcpy 0x42B0A0, native in the
// game (src/unzip.cpp, src/crt.cpp, src/render.cpp): here only the stubbed
// functions' lifted bodies reach them.
void f_0049EAA0_lifted(Ctx& c);
void f_0049EAA0(Ctx& c) { f_0049EAA0_lifted(c); }
void f_0051B3DD_lifted(Ctx& c);
void f_0051B3DD(Ctx& c) { f_0051B3DD_lifted(c); }
void f_0042B0A0_lifted(Ctx& c);
void f_0042B0A0(Ctx& c) { f_0042B0A0_lifted(c); }
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
void f_004F3330_lifted(Ctx& c);
void f_004F3410_lifted(Ctx& c);
void f_004F3810_lifted(Ctx& c);
void f_004F38B0_lifted(Ctx& c);
void f_004F3920_lifted(Ctx& c);
void f_004F3D80_lifted(Ctx& c);
void f_004C8830_lifted(Ctx& c);
void f_004C89A0_lifted(Ctx& c);
void f_004F5D50_lifted(Ctx& c);
void f_004F4090_lifted(Ctx& c);
void f_00509F20_lifted(Ctx& c);
void f_004F4FA0_lifted(Ctx& c);
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
void f_004F3330(Ctx& c) { f_004F3330_lifted(c); }
void f_004F3410(Ctx& c) { f_004F3410_lifted(c); }
void f_004F3810(Ctx& c) { f_004F3810_lifted(c); }
void f_004F38B0(Ctx& c) { f_004F38B0_lifted(c); }
void f_004F3920(Ctx& c) { f_004F3920_lifted(c); }
void f_004F3D80(Ctx& c) { f_004F3D80_lifted(c); }
void f_004C8830(Ctx& c) { f_004C8830_lifted(c); }
void f_004C89A0(Ctx& c) { f_004C89A0_lifted(c); }
void f_004F5D50(Ctx& c) { f_004F5D50_lifted(c); }
void f_004F4090(Ctx& c) { f_004F4090_lifted(c); }
void f_00509F20(Ctx& c) { f_00509F20_lifted(c); }
void f_004F4FA0(Ctx& c) { f_004F4FA0_lifted(c); }
void n_004C89D0(Ctx& c);
void n_004C8A20(Ctx& c);
void n_004CB3B0(Ctx& c);
void n_004CB5D0(Ctx& c);
void n_004CB640(Ctx& c);
void n_004CB710(Ctx& c);
void n_004CBBE0(Ctx& c);
void n_004F3180(Ctx& c);
void n_004F32C0(Ctx& c);
void n_004F3330(Ctx& c);
void n_004F3410(Ctx& c);
void n_004F3810(Ctx& c);
void n_004F38B0(Ctx& c);
void n_004F3920(Ctx& c);
void n_004F3D80(Ctx& c);
void n_004C8830(Ctx& c);
void n_004C89A0(Ctx& c);
void n_004F5D50(Ctx& c);
void n_004F4090(Ctx& c);
void n_00509F20(Ctx& c);
void n_004F4FA0(Ctx& c);
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
  // ri.Error's texture environment message has the environment after it.
  const int n = gl.ptr == 0x1549604 && Rd(c.esp + 8) == 0x54303C ? 3 : gl.args;
  for (int k = 0; k < n; k++) r.args[k] = Rd(c.esp + 4 + 4 * uint32_t(k));
  if (gl.ptr_arg >= 0)
    for (int k = 0; k < gl.floats; k++) r.data[k] = Rd(r.args[gl.ptr_arg] + 4 * uint32_t(k));
  g_calls.push_back(r);
  const uint32_t junk = 0xC0DE0000u + uint32_t(g_calls.size());
  c.eax = junk;
  c.ecx = junk ^ 1;
  c.edx = junk ^ 2;
  c.esp += 4 + (gl.cdecl ? 0 : 4 * uint32_t(gl.args));
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
    COD1_ENTRY(004F32C0, "draw elements"), COD1_ENTRY(004F3330, "bundle image"),
    COD1_ENTRY(004F3410, "bind bundle"),   COD1_ENTRY(004F3810, "unit off"),
    COD1_ENTRY(004F38B0, "coords on"),     COD1_ENTRY(004F3920, "coords off"),
    COD1_ENTRY(004F3D80, "stage textures"), COD1_ENTRY(004C8830, "fog params"),
    COD1_ENTRY(004C89A0, "fog off"),       COD1_ENTRY(004F5D50, "flush fog"),
    COD1_ENTRY(004F4090, "units reset"),   COD1_ENTRY(00509F20, "deforms"),
    COD1_ENTRY(004F4FA0, "stage colours"),
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
    {0x11E0000, 0x1000}, {0x17F0000, 0x1000}, {0x154D000, 0x1000}, {0x0, 0x100},
    // The tessellator's positions, vertex colours and normals, and the
    // stage colours (as many as the cases' vertices use).
    {0x1630C40, 0x800}, {0x1690C40, 0x100}, {0x1698C40, 0x200}, {0x16E0C40, 0x100},
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
  uint32_t eax = 0, ecx = 0, edx = 0, esi = 0, edi = 0;
  std::vector<uint32_t> args;
};

void PrintCall(const char* side, size_t i, const CallRec& r) {
  const uint32_t k = (r.target - kGlTarget) / 0x10;
  std::printf("  %zu %s: %s ret %08X esp %08X args %08X %08X %08X %08X data %08X %08X %08X %08X\n",
              i, side, k < uint32_t(kNumGl) ? kGl[k].name : "?", r.ret, r.esp, r.args[0],
              r.args[1], r.args[2], r.args[3], r.data[0], r.data[1], r.data[2], r.data[3]);
}

void Run(const Call& k, uint32_t arena_used, std::mt19937& g, Stats& s) {
  const Entry& e = kEntries[k.entry];
  std::vector<Window> w(std::begin(kGlobals), std::end(kGlobals));
  w.push_back({kArena, arena_used});
  const uint32_t esp = kStack + kStackSize - 0x1000;
  Wr(esp, 0xDEAD0000);
  for (size_t i = 0; i < k.args.size(); i++) Wr(esp + 4 + 4 * uint32_t(i), k.args[i]);
  w.push_back({esp, 4 + 4 * uint32_t(k.args.size())});
  const std::vector<uint8_t> before = Save(w);
  Ctx c0{};
  c0.eax = k.eax;
  c0.ecx = k.ecx ? k.ecx : uint32_t(g());
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
            cn.fpu.cw == cl.fpu.cw && (e.va != 0x4F3330 || cn.eax == cl.eax);
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
  Wr(0x1534758, g() % 8 ? g() % 9 : 0);  // texture units in use
  for (uint32_t u = 0; u < 8; u++) Wr(0x1534608 + 4 * u, g() % 3 == 0);
  Wr(0x1534670, g() % 3);
  Wr(0x15343B4, Cvar(a, g, g() % 4 == 0));       // r_lightmap
  // The shader time: seconds, now and then anything.
  float time = float(g() % 100000) / 100.0f;
  uint32_t time_bits;
  std::memcpy(&time_bits, &time, 4);
  Wr(0x17F0C78, g() % 4 ? time_bits : RandFloat(g));
  Wr(0x1536360, g());
  for (uint32_t i = 0; i < 4; i++) Wr(0x1535A3C + 4 * i, Image(a, g));
  // The tessellator: up to 40 vertices (positions 4 or 8 floats apart).
  Wr(0x17F0C88, g() % 16 ? g() % 41 : 0);
  Wr(0x17F0C60, g() % 4 ? 4 : 8);
  for (uint32_t i = 0; i < 0x800; i += 4) Wr(0x1630C40 + i, RandFloat(g));
  for (uint32_t i = 0; i < 0x200; i += 4) Wr(0x1698C40 + i, RandFloat(g));
  for (uint32_t i = 0; i < 0x100; i += 4) {
    Wr(0x1690C40 + i, uint32_t(g()));
    Wr(0x16E0C40 + i, uint32_t(g()));
  }
  const uint32_t shader = a.Alloc(0x700);
  Wr(shader + 0xA4, RandFloat(g));
  Wr(shader + 0xAC, g() % 8 ? g() % 5 : g() % 9);
  for (uint32_t d = 0; d < 8; d++) Wr(shader + 0xB0 + 0x34 * d, g() % 8 ? 1 + g() % 18 : g() % 24);
  Wr(0x17F0C70, shader);
  Wr(0x17F0C84, uint32_t(g()));
  if (g() % 2) Wr(0x1536290, 0x3F800000);  // tr.identityLight 1
  Wr(0x1536294, g() & 0xFF);
  Wr(0x1536298, g() % 4 ? g() % 3 : uint32_t(g()));  // overbright bits
  for (uint32_t i = 0; i < 12; i += 4) Wr(0x1549828 + i, RandFloat(g));  // view origin
  if (g() % 2) {
    Wr(0x1549B3C, 0x1535D6C);  // the world entity
  } else {
    const uint32_t ent = a.Alloc(0x60);
    for (uint32_t i = 0x1C; i < 0x50; i += 4) Wr(ent + i, RandFloat(g));
    Wr(0x1549B3C, ent);
  }
  Wr(0x154D734, g() % 2);
  Wr(0x15343F0, Cvar(a, g, 0));                  // the far clip
  if (g() % 2) Wr(Rd(0x15343F0) + 0x1C, 0);
  Wr(0x15343E8, Cvar(a, g, g() % 8));
  // The sky's, the view's and the world's fogs, and the values last sent.
  for (uint32_t fog : {0x1535820u, 0x1535860u, 0x1535920u}) {
    Wr(fog, Pick(g, {0, 0x2601, 0x800, 0x801, uint32_t(g())}));
    Wr(fog + 4, Pick(g, {0, 0x1100, 0x1101, 0x1102}));
    for (uint32_t i = 0x10; i < 0x30; i += 4) Wr(fog + i, RandFloat(g));
    if (g() % 4 == 0) Wr(fog + 0x2C, 0);
    Wr(fog + 0x30, g() % 3 != 0);
  }
  Wr(0x13F9CF8, g() % 3 ? g() % 3 : uint32_t(g()));
  const uint32_t sent = 0x1535820 + 0x40 * (g() % 2);
  Wr(0x1534638, g() % 2 ? Rd(sent) : uint32_t(g()));
  Wr(0x153463C, g() % 2 ? Rd(sent + 4) : uint32_t(g()));
  Wr(0x1534650, g() % 2 ? Rd(sent + 0x20) : RandFloat(g));
  Wr(0x1534654, g() % 2 ? Rd(sent + 0x24) : RandFloat(g));
  Wr(0x1534658, g() % 2 ? Rd(sent + 0x2C) : RandFloat(g));
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

// A stage: 8 bundles of 200 bytes from +4, then the combiners, vertex
// program and fragment shader at +0x644..+0x64C.
uint32_t Bundle(Arena& a, std::mt19937& g, uint32_t b) {
  // Up to 32 images (+0..+0x7C), from a few distinct ones; the count now
  // and then negative.
  uint32_t pool[4];
  for (uint32_t& im : pool) im = Image(a, g);
  for (uint32_t i = 0; i < 32; i++) Wr(b + 4 * i, pool[g() % 4]);
  Wr(b + 0x80, g() % 4 ? g() % 6 : g() % 2 ? g() % 33 : uint32_t(g()) | 0x80000000u);
  Wr(b + 0x84, RandFloat(g));
  Wr(b + 0x88, g() % 8 == 0 ? a.Alloc(16) : 0);
  Wr(b + 0x8C, 2 + g() % 3);
  Wr(b + 0x90, g() % 5 == 0 ? 0 : Pick(g, {0x2100, 0x1E01, 0x104, 0x8570, uint32_t(g())}));
  const uint32_t combine = a.Alloc(0x50);
  for (uint32_t i = 0; i < 0x50; i += 4) Wr(combine + i, i == 0x2C || i == 0x4C ? RandFloat(g) : uint32_t(g()));
  Wr(b + 0x94, combine);
  uint32_t shader = 0;
  if (g() % 4 == 0) {
    shader = a.Alloc(0x20);
    Wr(shader, g() % 4 ? 0x86E7 + g() % 14 : g() % 3 ? 0 : uint32_t(g()));
    for (uint32_t i = 4; i < 0x20; i += 4) Wr(shader + i, uint32_t(g()));
  }
  Wr(b + 0x98, shader);
  Wr(b + 0xC0, g() % 4);
  g_mem.write<uint8_t>(b + 0xC4, g() % 4 == 0);
  g_mem.write<uint8_t>(b + 0xC5, g() % 8 == 0);
  g_mem.write<uint8_t>(b + 0xC6, g() % 2);
  return b;
}
uint32_t Stage(Arena& a, std::mt19937& g) {
  const uint32_t s = a.Alloc(0x650);
  for (uint32_t u = 0; u < 8; u++) Bundle(a, g, s + 4 + 0xC8 * u);
  Wr(s + 0x644, g() % 4 ? Rd(0x1534670) : g() % 3);
  Wr(s + 0x648, g() % 8 == 0 ? g() : 0);
  Wr(s + 0x64C, g() % 8 == 0 ? g() : 0);
  return s;
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
    case 0x4F3330:
      k.esi = Bundle(a, g, a.Alloc(0xC8));
      k.args = {uint32_t(g() % 8)};
      break;
    case 0x4F3410:
      k.eax = Bundle(a, g, a.Alloc(0xC8));
      k.edi = g() % 8;
      break;
    case 0x4F3810:
      k.eax = g() % 8;
      break;
    case 0x4F38B0:
      k.esi = g() % 8;
      k.args = {Bundle(a, g, a.Alloc(0xC8)), uint32_t(g()), uint32_t(g() % 64)};
      break;
    case 0x4F3920:
      k.edi = g() % 8;
      break;
    case 0x4C8830:
      k.esi = Pick(g, {0x1535820, 0x1535860, 0x1535920});
      break;
    case 0x4F4FA0: {
      const uint32_t stage = a.Alloc(0x700);
      Wr(stage + 0x664, g() % 8 ? g() % 15 : uint32_t(g()));
      Wr(stage + 0x67C, g() % 8 ? g() % 15 : uint32_t(g()));
      Wr(stage + 0x680, uint32_t(g()));
      k.args = {stage};
      break;
    }
    case 0x4F3D80: {
      const uint32_t coords = a.Alloc(32);
      for (uint32_t u = 0; u < 8; u++) Wr(coords + 4 * u, uint32_t(g()));
      k.args = {Stage(a, g), coords, uint32_t(g() % 64)};
      break;
    }
  }
  Run(k, a.top - kArena, g, s);
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
