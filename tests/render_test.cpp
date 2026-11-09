// The native renderer functions (src/render.cpp) against the originals,
// lifted from the EXE at build time with `recomp_lift --only`
// (CMakeLists.txt) and run on SwARc's exact x87 model (SoftFloat, not
// its host-double runs). CoDSP.exe is mapped as the game maps it, for its
// constants and globals. The functions they call that src/render.cpp does
// not replace are stubs (tests/render_stubs.manifest) that record each
// call: its address, return address, ESP, the registers the callee reads
// and its stack arguments, which must be the same on both sides; they
// leave EAX, ECX and EDX changed. Each case runs one entry through the
// lifted original and the native code from the same guest memory, and both
// must leave the same bytes in the case's memory (the structures, the
// globals, the stack arguments), the same calls, and the same ESP, EBX,
// ESI, EDI, EBP and x87 stack (top, empty registers, control word), and
// 0x4D68E0 the same EAX. Not compared: EAX otherwise, ECX, EDX, EFLAGS,
// the x87 status word and the stack below the return address.
//
//   render_test EXE [CAPTURE...]
//
// Random cases (mt19937, fixed seed): small worlds of frustum planes,
// near/far planes, portals with their counters, dlights, surface AABB
// trees, surfaces, static models, cells and entities, culled from every
// entry; sky polygons clipped from every stage; shaders with random
// tcGens, vectors and texmods over a random tessellator; draw surface
// sorts of random keys and widths; matrix products, aliased too; and the
// memcpy 0x42B0A0, overlapping too; the view's globals, fogs, times and
// cvars for its culls, transforms, fog transition, far clip, projection and
// frustum, with grazing spheres and sums of products that nearly cancel
// (where the order of a sum shows), and ri.Printf's calls compared by the
// arguments their formats take; static models (unit, permuted and wide
// axes), their cells and references (at the limit too), BSPs to link them
// through (finite values only), and the world's view with huge products
// that cancel and a far plane at a huge distance. Values are now and then
// zeros, denormals, infinities, NaNs or huge. Captures
// (tests/render_capture.h) add the game's own calls.

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "render_capture.h"
#include "runtime/guest_memory.h"
#include "win32/pe_image.h"
#include "x86/lifted.h"
#include "x86/x87.h"

namespace recomp {
uint8_t* g_guest_base = nullptr;
PeImage* g_exe = nullptr;
}  // namespace recomp

// ---- the stubs ----------------------------------------------------------------

namespace {

using recomp::Ctx;

recomp::GuestMemory g_mem;

uint32_t Rd(uint32_t va) { return g_mem.read<uint32_t>(va); }
void Wr(uint32_t va, uint32_t v) { g_mem.write<uint32_t>(va, v); }
void WrF(uint32_t va, float f) { g_mem.write<float>(va, f); }

// ri.Error and ri.Printf, through the refimport table at 0x1549604 and
// 0x1549600.
constexpr uint32_t kErrorPtr = 0x1549604, kError = 0x7FFE0000;
constexpr uint32_t kPrintfPtr = 0x1549600, kPrintf = 0x7FFE0010;

enum Reg { kEax = 1, kEcx = 2, kEdx = 4, kEbx = 8, kEsi = 16, kEdi = 32, kEbp = 64 };
constexpr int kSaved = kEbx | kEsi | kEdi | kEbp;

struct CallRec {
  uint32_t va, ret, esp, regs[7], args[8];
  bool operator==(const CallRec&) const = default;
};
std::vector<CallRec> g_calls;

// What each stub compares: the registers its callee reads (and for the
// texture coordinate callees the callee-saved ones, which ComputeTexCoords
// passes as they are) and its stack arguments.
struct StubInfo {
  uint32_t va;
  int regs, args;
};
constexpr StubInfo kStubs[] = {
    {0x4AD7E0, kEax | kEcx | kEdx, 0},          // debug box
    {0x4DEC10, 0, 2},                           // adds an entity
    {0x4CAB80, 0, 1},                           // a portal's view
    {0x427E80, kEax | kEcx, 0},                 // a matrix's inverse
    {0x50A2B0, kSaved | kEax, 0},               // tcGens
    {0x50ABF0, kSaved | kEax, 0},
    {0x50A7C0, kSaved | kEax, 1},
    {0x50A3D0, kSaved | kEax | kEcx, 0},
    {0x50A380, kSaved | kEax | kEcx, 0},
    {0x50A420, kSaved | kEax | kEdx, 0},
    {0x50A4D0, kSaved | kEax | kEcx, 0},
    {0x50A5C0, kSaved | kEax, 0},
    {0x50A720, kSaved | kEax, 0},
    {0x50A820, kSaved | kEax | kEcx, 0},
    {0x50AA30, kSaved | kEax | kEcx | kEdx, 0},  // texmods
    {0x50A8D0, kSaved | kEax | kEcx | kEdx, 0},
    {0x50A9B0, kSaved | kEax | kEcx | kEdx, 0},
    {0x50A980, kSaved | kEax | kEcx | kEdx, 0},
    {0x508A50, kSaved | kEax | kEcx | kEdx, 1},
    {0x50AA80, kSaved | kEax | kEcx | kEdx, 2},
    {0x50A890, kSaved | kEax | kEcx | kEdx, 0},
    {0x50AE10, kSaved | kEax | kEcx | kEdx, 0},
};

void Stub(Ctx& c, uint32_t va, int regs, int args) {
  CallRec r{};
  r.va = va;
  r.esp = c.esp;
  r.ret = Rd(c.esp);
  const uint32_t all[7] = {c.eax, c.ecx, c.edx, c.ebx, c.esi, c.edi, c.ebp};
  for (int i = 0; i < 7; i++)
    if (regs & (1 << i)) r.regs[i] = all[i];
  for (int i = 0; i < args; i++) r.args[i] = Rd(c.esp + 4 + 4 * i);
  g_calls.push_back(r);
  const uint32_t junk = 0xC0DE0000u + uint32_t(g_calls.size());
  // A portal's view now and then not drawn (EAX 0).
  c.eax = va == 0x4CAB80 && g_calls.size() % 3 == 0 ? 0 : junk;
  c.ecx = junk ^ 1;
  c.edx = junk ^ 2;
  c.esp += 4;
}
void StubFor(Ctx& c, uint32_t va) {
  for (const StubInfo& s : kStubs)
    if (s.va == va) return Stub(c, va, s.regs, s.args);
  std::abort();
}

}  // namespace

namespace recomp {
namespace gen {
// The originals...
void f_004D68E0_lifted(Ctx& c);
void f_004D6970_lifted(Ctx& c);
void f_004D8280_lifted(Ctx& c);
void f_004D8420_lifted(Ctx& c);
void f_004D9330_lifted(Ctx& c);
void f_004D94C0_lifted(Ctx& c);
void f_004D9650_lifted(Ctx& c);
void f_004DB3E0_lifted(Ctx& c);
void f_004DB660_lifted(Ctx& c);
void f_004F5940_lifted(Ctx& c);
void f_004CADC0_lifted(Ctx& c);
void f_004C94D0_lifted(Ctx& c);
void f_0042B0A0_lifted(Ctx& c);
void f_004C8D50_lifted(Ctx& c);
void f_004C9070_lifted(Ctx& c);
void f_004C90F0_lifted(Ctx& c);
void f_004C9930_lifted(Ctx& c);
void f_004C9B90_lifted(Ctx& c);
void f_004C9C00_lifted(Ctx& c);
void f_004C9D30_lifted(Ctx& c);
void f_004C9FF0_lifted(Ctx& c);
void f_004CA0C0_lifted(Ctx& c);
void f_004CAF00_lifted(Ctx& c);
void f_004CAFC0_lifted(Ctx& c);
void f_004D4230_lifted(Ctx& c);
void f_004D6830_lifted(Ctx& c);
void f_004D6880_lifted(Ctx& c);
void f_004D6A10_lifted(Ctx& c);
void f_004D6AB0_lifted(Ctx& c);
void f_004D7DF0_lifted(Ctx& c);
void f_004D7E70_lifted(Ctx& c);
void f_004D7ED0_lifted(Ctx& c);
void f_004D8770_lifted(Ctx& c);
void f_004D8910_lifted(Ctx& c);
void f_004D8A20_lifted(Ctx& c);
void f_004D8B50_lifted(Ctx& c);
void f_004D8BE0_lifted(Ctx& c);
void f_004D8D80_lifted(Ctx& c);
void f_004D9290_lifted(Ctx& c);
void f_004D9820_lifted(Ctx& c);
void f_004D9D80_lifted(Ctx& c);
// ...call each other, not the native code (tests/render_native.cpp renames
// its entries).
void f_004D68E0(Ctx& c) { f_004D68E0_lifted(c); }
void f_004D6970(Ctx& c) { f_004D6970_lifted(c); }
void f_004D8280(Ctx& c) { f_004D8280_lifted(c); }
void f_004D8420(Ctx& c) { f_004D8420_lifted(c); }
void f_004D9330(Ctx& c) { f_004D9330_lifted(c); }
void f_004D94C0(Ctx& c) { f_004D94C0_lifted(c); }
void f_004D9650(Ctx& c) { f_004D9650_lifted(c); }
void f_004DB3E0(Ctx& c) { f_004DB3E0_lifted(c); }
void f_004DB660(Ctx& c) { f_004DB660_lifted(c); }
void f_004F5940(Ctx& c) { f_004F5940_lifted(c); }
void f_004CADC0(Ctx& c) { f_004CADC0_lifted(c); }
void f_004C94D0(Ctx& c) { f_004C94D0_lifted(c); }
void f_0042B0A0(Ctx& c) { f_0042B0A0_lifted(c); }
void f_004C8D50(Ctx& c) { f_004C8D50_lifted(c); }
void f_004C9070(Ctx& c) { f_004C9070_lifted(c); }
void f_004C90F0(Ctx& c) { f_004C90F0_lifted(c); }
void f_004C9930(Ctx& c) { f_004C9930_lifted(c); }
void f_004C9B90(Ctx& c) { f_004C9B90_lifted(c); }
void f_004C9C00(Ctx& c) { f_004C9C00_lifted(c); }
void f_004C9D30(Ctx& c) { f_004C9D30_lifted(c); }
void f_004C9FF0(Ctx& c) { f_004C9FF0_lifted(c); }
void f_004CA0C0(Ctx& c) { f_004CA0C0_lifted(c); }
void f_004CAF00(Ctx& c) { f_004CAF00_lifted(c); }
void f_004CAFC0(Ctx& c) { f_004CAFC0_lifted(c); }
void f_004D4230(Ctx& c) { f_004D4230_lifted(c); }
void f_004D6830(Ctx& c) { f_004D6830_lifted(c); }
void f_004D6880(Ctx& c) { f_004D6880_lifted(c); }
void f_004D6A10(Ctx& c) { f_004D6A10_lifted(c); }
void f_004D6AB0(Ctx& c) { f_004D6AB0_lifted(c); }
void f_004D7DF0(Ctx& c) { f_004D7DF0_lifted(c); }
void f_004D7E70(Ctx& c) { f_004D7E70_lifted(c); }
void f_004D7ED0(Ctx& c) { f_004D7ED0_lifted(c); }
void f_004D8770(Ctx& c) { f_004D8770_lifted(c); }
void f_004D8910(Ctx& c) { f_004D8910_lifted(c); }
void f_004D8A20(Ctx& c) { f_004D8A20_lifted(c); }
void f_004D8B50(Ctx& c) { f_004D8B50_lifted(c); }
void f_004D8BE0(Ctx& c) { f_004D8BE0_lifted(c); }
void f_004D8D80(Ctx& c) { f_004D8D80_lifted(c); }
void f_004D9290(Ctx& c) { f_004D9290_lifted(c); }
void f_004D9820(Ctx& c) { f_004D9820_lifted(c); }
void f_004D9D80(Ctx& c) { f_004D9D80_lifted(c); }
// unzCloseCurrentFile and tolower, native in the game (src/unzip.cpp,
// src/crt.cpp): here only lifted code the stubs stand for reaches them.
void f_0049EAA0_lifted(Ctx& c);
void f_0049EAA0(Ctx& c) { f_0049EAA0_lifted(c); }
void f_0051B3DD_lifted(Ctx& c);
void f_0051B3DD(Ctx& c) { f_0051B3DD_lifted(c); }
// TableForFunc, native in the game (src/render_backend.cpp): ComputeTexCoords
// reaches it.
void f_00508940_lifted(Ctx& c);
void f_00508940(Ctx& c) { f_00508940_lifted(c); }
// Native in the game (src/cm_trace.cpp, src/render_backend.cpp): the
// lifted portal view the stub 0x4CAB80 stands for reaches them.
void f_0041CD60_lifted(Ctx& c);
void f_0041CD60(Ctx& c) { f_0041CD60_lifted(c); }
void f_004AA0F0_lifted(Ctx& c);
void f_004AA0F0(Ctx& c) { f_004AA0F0_lifted(c); }
void f_004C9250_lifted(Ctx& c);
void f_004C9250(Ctx& c) { f_004C9250_lifted(c); }
void f_004C9580_lifted(Ctx& c);
void f_004C9580(Ctx& c) { f_004C9580_lifted(c); }
void f_004C97D0_lifted(Ctx& c);
void f_004C97D0(Ctx& c) { f_004C97D0_lifted(c); }
void f_004CA170_lifted(Ctx& c);
void f_004CA170(Ctx& c) { f_004CA170_lifted(c); }
void f_004CA770_lifted(Ctx& c);
void f_004CA770(Ctx& c) { f_004CA770_lifted(c); }
void f_004CA8D0_lifted(Ctx& c);
void f_004CA8D0(Ctx& c) { f_004CA8D0_lifted(c); }
void f_004F4A20_lifted(Ctx& c);
void f_004F4A20(Ctx& c) { f_004F4A20_lifted(c); }
void f_004F4AB0_lifted(Ctx& c);
void f_004F4AB0(Ctx& c) { f_004F4AB0_lifted(c); }
void f_005040D0_lifted(Ctx& c);
void f_005040D0(Ctx& c) { f_005040D0_lifted(c); }
void n_004D68E0(Ctx& c);
void n_004D6970(Ctx& c);
void n_004D8280(Ctx& c);
void n_004D8420(Ctx& c);
void n_004D9330(Ctx& c);
void n_004D94C0(Ctx& c);
void n_004D9650(Ctx& c);
void n_004DB3E0(Ctx& c);
void n_004DB660(Ctx& c);
void n_004F5940(Ctx& c);
void n_004CADC0(Ctx& c);
void n_004C94D0(Ctx& c);
void n_0042B0A0(Ctx& c);
void n_004C8D50(Ctx& c);
void n_004C9070(Ctx& c);
void n_004C90F0(Ctx& c);
void n_004C9930(Ctx& c);
void n_004C9B90(Ctx& c);
void n_004C9C00(Ctx& c);
void n_004C9D30(Ctx& c);
void n_004C9FF0(Ctx& c);
void n_004CA0C0(Ctx& c);
void n_004CAF00(Ctx& c);
void n_004CAFC0(Ctx& c);
void n_004D4230(Ctx& c);
void n_004D6830(Ctx& c);
void n_004D6880(Ctx& c);
void n_004D6A10(Ctx& c);
void n_004D6AB0(Ctx& c);
void n_004D7DF0(Ctx& c);
void n_004D7E70(Ctx& c);
void n_004D7ED0(Ctx& c);
void n_004D8770(Ctx& c);
void n_004D8910(Ctx& c);
void n_004D8A20(Ctx& c);
void n_004D8B50(Ctx& c);
void n_004D8BE0(Ctx& c);
void n_004D8D80(Ctx& c);
void n_004D9290(Ctx& c);
void n_004D9820(Ctx& c);
void n_004D9D80(Ctx& c);
// The stubs.
#define COD1_STUB(va) \
  void f_##va(Ctx& c) { StubFor(c, 0x##va); }
COD1_STUB(004AD7E0)
COD1_STUB(004DEC10)
COD1_STUB(004CAB80)
COD1_STUB(00427E80)
COD1_STUB(0050A2B0)
COD1_STUB(0050ABF0)
COD1_STUB(0050A7C0)
COD1_STUB(0050A3D0)
COD1_STUB(0050A380)
COD1_STUB(0050A420)
COD1_STUB(0050A4D0)
COD1_STUB(0050A5C0)
COD1_STUB(0050A720)
COD1_STUB(0050A820)
COD1_STUB(0050AA30)
COD1_STUB(0050A8D0)
COD1_STUB(0050A9B0)
COD1_STUB(0050A980)
COD1_STUB(00508A50)
COD1_STUB(0050AA80)
COD1_STUB(0050A890)
COD1_STUB(0050AE10)
#undef COD1_STUB
}  // namespace gen

void Dispatch(Ctx& c, uint32_t target, uint32_t va) {
  if (target == kError || target == kPrintf) {
    // ri.Error and ri.Printf(code, fmt, ...): the arguments their formats
    // take (a double for each %f); the texmod error passes two more and
    // the registers ComputeTexCoords leaves.
    if (target == kError && Rd(c.esp + 8) == 0x53C06C) {
      Stub(c, kErrorPtr, kSaved | kEcx | kEdx, 4);
      return;
    }
    int n = 2;
    for (uint32_t p = Rd(c.esp + 8);; p++) {
      const char ch = char(g_mem.read<uint8_t>(p));
      if (!ch) break;
      if (ch != '%') continue;
      while (std::strchr("0123456789.-", char(g_mem.read<uint8_t>(p + 1)))) p++;
      const char conv = char(g_mem.read<uint8_t>(++p));
      if (conv != '%') n += conv == 'f' ? 2 : 1;
    }
    Stub(c, target == kError ? kErrorPtr : kPrintfPtr, 0, n);
    return;
  }
  // BoxOnPlaneSide's jump through its table of cases.
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

using namespace render_capture;
using Fn = void (*)(Ctx&);

struct Entry {
  uint32_t va;
  Fn lifted, native;
  const char* name;
};
#define COD1_ENTRY(va, name) {0x##va, recomp::gen::f_##va##_lifted, recomp::gen::n_##va, name}
const Entry kEntries[] = {
    COD1_ENTRY(004D68E0, "dlight bits"),     COD1_ENTRY(004D6970, "add world surface"),
    COD1_ENTRY(004D8280, "surface cull"),    COD1_ENTRY(004D8420, "AABB tree"),
    COD1_ENTRY(004D9330, "static model"),    COD1_ENTRY(004D94C0, "entity"),
    COD1_ENTRY(004D9650, "cell"),            COD1_ENTRY(004DB3E0, "AddSkyPolygon"),
    COD1_ENTRY(004DB660, "ClipSkyPolygon"),  COD1_ENTRY(004F5940, "ComputeTexCoords"),
    COD1_ENTRY(004CADC0, "qsortFast"),       COD1_ENTRY(004C94D0, "matrix product"),
    COD1_ENTRY(0042B0A0, "memcpy"),
    COD1_ENTRY(004C8D50, "local bounds cull"), COD1_ENTRY(004C9070, "sphere cull"),
    COD1_ENTRY(004C90F0, "vector to world"),   COD1_ENTRY(004C9930, "fog transition"),
    COD1_ENTRY(004C9B90, "far clip"),          COD1_ENTRY(004C9C00, "projection"),
    COD1_ENTRY(004C9D30, "frustum"),           COD1_ENTRY(004C9FF0, "point between orientations"),
    COD1_ENTRY(004CA0C0, "vector between orientations"),
    COD1_ENTRY(004CAF00, "R_AddDrawSurf"),     COD1_ENTRY(004CAFC0, "R_SortDrawSurfs"),
    COD1_ENTRY(004D4230, "draw surfs command"), COD1_ENTRY(004D6830, "plane nudge"),
    COD1_ENTRY(004D6880, "far distance"),      COD1_ENTRY(004D6A10, "cull frustum"),
    COD1_ENTRY(004D6AB0, "view projection"),   COD1_ENTRY(004D7DF0, "R_CellForCamera"),
    COD1_ENTRY(004D7E70, "polygon behind a plane"), COD1_ENTRY(004D7ED0, "polygon behind all"),
    COD1_ENTRY(004D8910, "scaled model bounds"), COD1_ENTRY(004D8A20, "XModel bounds"),
    COD1_ENTRY(004D8B50, "cell reference"),     COD1_ENTRY(004D8BE0, "model link"),
    COD1_ENTRY(004D8D80, "static model links"), COD1_ENTRY(004D8770, "cell's models"),
    COD1_ENTRY(004D9290, "cell's surfaces"),    COD1_ENTRY(004D9D80, "beyond the far plane"),
    COD1_ENTRY(004D9820, "world view"),
};
#undef COD1_ENTRY
constexpr int kNumEntries = sizeof(kEntries) / sizeof(kEntries[0]);
int EntryIndex(uint32_t va) {
  for (int i = 0; i < kNumEntries; i++)
    if (kEntries[i].va == va) return i;
  std::printf("no entry %08X\n", va);
  std::abort();
}

// Random structures live in the arena, reset per world; the stack below.
constexpr uint32_t kArena = 0x70000000, kArenaSize = 0x1000000;
constexpr uint32_t kStack = 0x6FF00000, kStackSize = 0x100000;

struct Window {
  uint32_t va, size;
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
  uint32_t fn;
  uint32_t eax = 0, ecx = 0, edx = 0, ebx = 0, esi = 0, edi = 0;
  std::vector<uint32_t> args;
};

// Runs one call through the lifted original and the native code from the
// same memory and compares; the original's result stays (the next case's
// starting state).
void Run(const Call& k, std::vector<Window> w, std::mt19937& g, Stats& s, const char* what) {
  const Entry& e = kEntries[EntryIndex(k.fn)];
  const uint32_t esp = kStack + kStackSize - 0x1000;
  Wr(esp, 0xDEAD0000);
  for (size_t i = 0; i < k.args.size(); i++) Wr(esp + 4 + 4 * uint32_t(i), k.args[i]);
  w.push_back({esp, 4 + 4 * uint32_t(k.args.size())});
  const std::vector<uint8_t> before = Save(w);
  Ctx c0{};
  c0.eax = k.eax;
  c0.ecx = k.ecx;
  c0.edx = k.edx;
  c0.ebx = k.ebx;
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
  s.by_entry[EntryIndex(k.fn)]++;
  s.calls += int(lcalls.size());
  const bool eax = k.fn == 0x4D68E0 || k.fn == 0x4C8D50 || k.fn == 0x4C9070 || k.fn == 0x4D7DF0 ||
                   k.fn == 0x4D7E70 || k.fn == 0x4D7ED0;
  // A result on the x87 stack: its register.
  const bool st0 = k.fn == 0x4D6880;
  bool ok = cn.esp == cl.esp && cn.ebp == cl.ebp && cn.ebx == cl.ebx && cn.esi == cl.esi &&
            cn.edi == cl.edi && cn.fpu.top == cl.fpu.top && cn.fpu.empty == cl.fpu.empty &&
            cn.fpu.cw == cl.fpu.cw && (!eax || cn.eax == cl.eax) &&
            (!st0 || (cn.fpu.r[cn.fpu.top].sig == cl.fpu.r[cl.fpu.top].sig &&
                      cn.fpu.r[cn.fpu.top].se == cl.fpu.r[cl.fpu.top].se));
  if (!ok && s.failures < 20)
    std::printf("%s %s: registers differ (esp %08X/%08X ebx %08X/%08X esi %08X/%08X edi "
                "%08X/%08X ebp %08X/%08X top %d/%d eax %08X/%08X)\n",
                what, e.name, cl.esp, cn.esp, cl.ebx, cn.ebx, cl.esi, cn.esi, cl.edi, cn.edi,
                cl.ebp, cn.ebp, cl.fpu.top, cn.fpu.top, cl.eax, cn.eax);
  if (lcalls != g_calls) {
    if (s.failures < 20) {
      std::printf("%s %s: calls differ (%zu lifted, %zu native)\n", what, e.name, lcalls.size(),
                  g_calls.size());
      for (size_t i = 0; i < std::max(lcalls.size(), g_calls.size()); i++) {
        const bool have_l = i < lcalls.size(), have_n = i < g_calls.size();
        if (have_l && have_n && lcalls[i] == g_calls[i]) continue;
        for (int side = 0; side < 2; side++) {
          if (!(side ? have_n : have_l)) continue;
          const CallRec& r = side ? g_calls[i] : lcalls[i];
          std::printf("  %zu %s: %08X ret %08X esp %08X regs", i, side ? "native" : "lifted",
                      r.va, r.ret, r.esp);
          for (uint32_t v : r.regs) std::printf(" %08X", v);
          std::printf(" args");
          for (uint32_t v : r.args) std::printf(" %08X", v);
          std::printf("\n");
        }
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
          std::memcpy(&lv, after.data() + std::min(o, after.size() - 4), 4);
          std::memcpy(&nv, got.data() + std::min(o, got.size() - 4), 4);
          std::printf("%s %s: %08X lifted %08X native %08X\n", what, e.name, a, lv, nv);
        }
        ok = false;
        break;
      }
    at += x.size;
  }
  if (!ok) s.failures++;
  Load(w, after);
}

// ---- random values ----------------------------------------------------------

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
  void Reset() {
    std::memset(g_mem.ptr<uint8_t>(kArena), 0, top - kArena);
    top = kArena;
  }
  Window All() const { return {kArena, top - kArena}; }
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

int32_t Pick(std::mt19937& g, std::initializer_list<int32_t> v) {
  return *(v.begin() + g() % v.size());
}

// A float of any sign over 2^lo..2^hi, so that the x87's double sums of
// such products round (and the order of a sum matters).
float Wide(std::mt19937& g, int lo, int hi) {
  return std::ldexp(Uniform(g, 1, 2), lo + int(g() % uint32_t(hi - lo + 1))) * (g() % 2 ? 1 : -1);
}

// ---- the front end ------------------------------------------------------------

constexpr uint32_t kPlaneBits = 0x11BEC20, kNearPlane = 0x11BEB7C, kFarPlane = 0x11BEB80,
                   kNumPortals = 0x11BEC10, kPortals = 0x11BEC14, kViewCount = 0x1535A28,
                   kRefdef = 0x153647C;
constexpr uint32_t kCvars[] = {0x15347E0, 0x15344DC, 0x153443C, 0x1534568};

// The draw surfaces: a count near the limit now and then (or negative), the
// array placed so that the next one lands in a small buffer of the arena.
constexpr uint32_t kNumDrawSurfs = 0x1536490;
void DrawSurfs(Arena& a, std::mt19937& g) {
  const uint32_t buf = a.Alloc(0x20);
  for (uint32_t i = 0; i < 0x20; i += 4) Wr(buf + i, g());
  const uint32_t n = g() % 4 ? g() % 4 : uint32_t(Pick(g, {0xFFFF, 0x10000, 0x10001, -1, -0x8000}));
  Wr(kNumDrawSurfs, n);
  Wr(kNumDrawSurfs + 4, buf + 0x10 - n * 8);
  Wr(0x1536028, g() % 4 ? g() % 2 : g());
}

struct World {
  float size;
  uint32_t special;
  uint32_t planes = 0;
  int32_t numPlanes = 0;
  uint32_t viewCount = 0;
  uint32_t shaders = 0;  // four, 0x60 bytes each
};

void WrBounds(std::mt19937& g, const World& w, uint32_t va) {
  const float s = w.size;
  for (int i = 0; i < 3; i++) {
    const float c = Uniform(g, -s, s), h = Uniform(g, 0, s / 3);
    WrF(va + 4 * i, Value(g, c - h, c - h, w.special));
    WrF(va + 0xC + 4 * i, Value(g, c + h, c + h, w.special));
  }
}

// A plane through the world with CoD's far-corner offsets.
uint32_t NewPlane(Arena& a, std::mt19937& g, const World& w) {
  const uint32_t p = a.Alloc(0x14);
  float n[3];
  if (g() % 4 == 0) {
    n[0] = n[1] = n[2] = 0;
    n[g() % 3] = g() % 2 ? 1.0f : -1.0f;
  } else {
    float l;
    do {
      for (float& x : n) x = Uniform(g, -1, 1);
      l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    } while (l < 0.1f || l > 1);
    for (float& x : n) x /= l;
  }
  for (int i = 0; i < 3; i++) WrF(p + 4 * i, Value(g, n[i], n[i], w.special));
  WrF(p + 0xC, Value(g, -w.size, w.size, w.special));
  g_mem.write<uint8_t>(p + 0x10, n[0] < 0 ? 0 : 12);
  g_mem.write<uint8_t>(p + 0x11, n[1] < 0 ? 4 : 16);
  g_mem.write<uint8_t>(p + 0x12, n[2] < 0 ? 8 : 20);
  if (g() % 32 == 0) g_mem.write<uint8_t>(p + 0x10 + g() % 3, uint8_t(4 * (g() % 6)));
  return p;
}

void NewWorld(Arena& a, std::mt19937& g, World& w, uint32_t special) {
  a.Reset();
  w.size = Pick(g, {16, 512, 4096});
  w.special = special;
  w.numPlanes = g() % 8 ? int32_t(g() % 7) : Pick(g, {-1, 0});
  w.planes = a.Alloc(7 * 0x14);
  for (int i = 0; i < 7; i++) {
    const uint32_t p = NewPlane(a, g, w);
    std::memcpy(g_mem.ptr<uint8_t>(w.planes + 0x14 * i), g_mem.ptr<uint8_t>(p), 0x14);
  }
  Wr(kPlaneBits, uint32_t(Pick(g, {-3, -1, 0, 1, 2, 3, 4, 5, 0x7FFFFFFF})));
  Wr(kNearPlane, NewPlane(a, g, w));
  Wr(kFarPlane, g() % 2 ? NewPlane(a, g, w) : 0);
  const uint32_t n = g() % 4 ? g() % 4 : 0;
  const uint32_t portals = a.Alloc(4 * 4);
  for (uint32_t k = 0; k < n; k++) {
    const uint32_t p = a.Alloc(0x24);
    Wr(portals + 4 * k, p);
    Wr(p + 0x18, uint32_t(Pick(g, {-1, 0, 1, 2, 3, 5, 0x7FFFFFFF})));
    const uint32_t np = g() % 5;
    Wr(p + 0x1C, np);
    const uint32_t planes = a.Alloc(np * 0x14);
    Wr(p + 0x20, planes);
    for (uint32_t i = 0; i < np; i++) {
      const uint32_t q = NewPlane(a, g, w);
      std::memcpy(g_mem.ptr<uint8_t>(planes + 0x14 * i), g_mem.ptr<uint8_t>(q), 0x14);
    }
  }
  Wr(kNumPortals, n);
  Wr(kPortals, portals);
  w.viewCount = g() % 16;
  Wr(kViewCount, w.viewCount);
  for (uint32_t cvar : kCvars) {
    const uint32_t v = a.Alloc(0x24);
    Wr(cvar, v);
    Wr(v + 0x20, g() % 6 ? 0 : g() % 4);
  }
  Wr(0x1549548, g());
  w.shaders = a.Alloc(4 * 0x60);
  for (uint32_t i = 0; i < 4 * 0x60; i += 4) Wr(w.shaders + i, g());
  for (uint32_t i = 0; i < 4; i++) Wr(w.shaders + 0x60 * i + 0x48, g() % 4 ? g() % 0x800 : g());
  DrawSurfs(a, g);
  const uint32_t refdef = a.Alloc(0x74 + 32 * 0x88);
  Wr(kRefdef, refdef);
  for (int i = 0; i < 32; i++) {
    const uint32_t dl = refdef + 0x74 + 0x88 * i;
    WrF(dl, Value(g, 0, w.size / 2, w.special));
    for (int j = 0; j < 3; j++) WrF(dl + 4 + 4 * j, Value(g, -w.size, w.size, w.special));
  }
}

uint32_t DlightBits(std::mt19937& g) { return g() % 2 ? 0 : g() % 2 ? g() & 0xFF : g(); }

// A world surface reference and its surface.
uint32_t NewRef(Arena& a, std::mt19937& g, const World& w) {
  const uint32_t ref = a.Alloc(0xC), surf = a.Alloc(0x24);
  Wr(ref, g() % 4 ? g() % 16 : w.viewCount);
  Wr(ref + 4, w.shaders + 0x60 * (g() % 4));
  Wr(ref + 8, surf);
  Wr(surf, uint32_t(Pick(g, {0, 0x10, 0x17, 0x18, 0x19, 0x20, -5})));
  Wr(surf + 4, g());
  Wr(surf + 8, g());
  WrBounds(g, w, surf + 0xC);
  return ref;
}

void Refs(Arena& a, std::mt19937& g, const World& w, uint32_t va, uint32_t n) {
  const uint32_t refs = a.Alloc(n * 0xC);
  for (uint32_t i = 0; i < n; i++) {
    const uint32_t r = NewRef(a, g, w);
    std::memcpy(g_mem.ptr<uint8_t>(refs + 0xC * i), g_mem.ptr<uint8_t>(r), 0xC);
  }
  Wr(va, refs);
  Wr(va + 4, n);
}

void NewNode(Arena& a, std::mt19937& g, const World& w, uint32_t node, int depth) {
  WrBounds(g, w, node);
  Refs(a, g, w, node + 0x18, g() % 5);
  const uint32_t n = depth < 3 && g() % 3 ? g() % 4 : 0;
  const uint32_t children = a.Alloc(n * 0x28);
  Wr(node + 0x20, children);
  Wr(node + 0x24, n);
  for (uint32_t i = 0; i < n; i++) NewNode(a, g, w, children + i * 0x28, depth + 1);
}

uint32_t NewModel(Arena& a, std::mt19937& g, const World& w) {
  const uint32_t m = a.Alloc(0x24);
  WrBounds(g, w, m);
  Refs(a, g, w, m + 0x18, g() % 4);
  Wr(m + 0x20, g() % 4 ? g() % 16 : w.viewCount);
  return m;
}

uint32_t NewEntity(Arena& a, std::mt19937& g, const World& w) {
  const uint32_t e = a.Alloc(0xB8);
  WrBounds(g, w, e + 0x9C);
  Wr(e + 0xB4, g() % 4 ? g() % 16 : w.viewCount);
  return e;
}

// Every plane of the world through the point p (a bounds with mins ==
// maxs): a normal of +-1 on one axis and dist that coordinate exactly,
// the other two products tiny against it (about 2^-53 of it, either
// sign), so whether the point is in front, on or behind depends on how
// the sum rounds, and on its order.
void Graze(std::mt19937& g, const World& w, uint32_t b) {
  float pt[3];
  for (int i = 0; i < 3; i++) {
    pt[i] = g() % 4 ? Wide(g, -10, 10) : float(int(g() % 9) - 4);
    WrF(b + 4 * i, pt[i]);
    WrF(b + 0xC + 4 * i, pt[i]);
  }
  auto plane = [&](uint32_t p) {
    const int axis = int(g() % 3);
    const float sign = g() % 2 ? 1.0f : -1.0f, dist = pt[axis] * sign;
    for (int i = 0; i < 3; i++) {
      float n = sign;
      if (i != axis)
        n = pt[i] == 0 ? Uniform(g, -1, 1)
                       : float(double(dist == 0 ? 1.0f : dist) * std::ldexp(Uniform(g, -4, 4), -53) / pt[i]);
      WrF(p + 4 * i, n);
      g_mem.write<uint8_t>(p + 0x10 + i, uint8_t(4 * i + (n < 0 ? 0 : 12)));
    }
    WrF(p + 0xC, dist);
  };
  for (int32_t i = 0; i < 7; i++) plane(w.planes + 0x14 * uint32_t(i));
  plane(Rd(kNearPlane));
  if (Rd(kFarPlane)) plane(Rd(kFarPlane));
  for (uint32_t k = 0; k < Rd(kNumPortals); k++) {
    const uint32_t portal = Rd(Rd(kPortals) + 4 * k);
    for (uint32_t i = 0; i < Rd(portal + 0x1C); i++) plane(Rd(portal + 0x20) + 0x14 * i);
  }
}

void CullCases(Arena& a, std::mt19937& g, World& w, Stats& s) {
  const bool graze = g() % 4 == 0;
  auto planeBits = [&] { return uint32_t(Pick(g, {-1, 0, 1, 2, 3, 4, 6})); };
  std::vector<Window> win = {a.All(), {kPlaneBits, 4}, {kNumDrawSurfs, 8}};
  switch (g() % 7) {
    case 0: {
      const uint32_t node = a.Alloc(0x28);
      NewNode(a, g, w, node, 0);
      if (graze) Graze(g, w, node);
      win[0] = a.All();
      Run({0x4D8420, 0, 0, 0, 0, 0, 0,
           {node, w.planes, uint32_t(w.numPlanes), DlightBits(g), planeBits()}},
          win, g, s, "random");
      break;
    }
    case 1: {
      const uint32_t ref = NewRef(a, g, w);
      if (graze) Graze(g, w, Rd(ref + 8) + 0xC);
      win[0] = a.All();
      Run({0x4D8280, uint32_t(w.numPlanes), 0, 0, 0, 0, 0, {ref, DlightBits(g), w.planes, planeBits()}},
          win, g, s, "random");
      break;
    }
    case 2: {
      const uint32_t m = NewModel(a, g, w);
      if (graze) Graze(g, w, m);
      win[0] = a.All();
      Run({0x4D9330, uint32_t(w.numPlanes), w.planes, m, 0, 0, 0, {DlightBits(g)}}, win, g, s,
          "random");
      break;
    }
    case 3: {
      const uint32_t e = NewEntity(a, g, w);
      if (graze) Graze(g, w, e + 0x9C);
      win[0] = a.All();
      Run({0x4D94C0, uint32_t(w.numPlanes), 0, w.planes, 0, 0, 0, {e, DlightBits(g)}}, win, g, s,
          "random");
      break;
    }
    case 4: {
      const uint32_t cell = a.Alloc(0x3C);
      const uint32_t n = g() % 5, models = a.Alloc(4 * n);
      for (uint32_t i = 0; i < n; i++) Wr(models + 4 * i, NewModel(a, g, w));
      Wr(cell + 0x24, models);
      Wr(cell + 0x28, n);
      uint32_t link = 0;
      for (uint32_t i = g() % 5; i; i--) {
        const uint32_t l = a.Alloc(8);
        Wr(l, NewEntity(a, g, w));
        Wr(l + 4, link);
        link = l;
      }
      Wr(cell + 0x38, link);
      win[0] = a.All();
      Run({0x4D9650, 0, 0, 0, uint32_t(w.numPlanes), 0, DlightBits(g), {cell, w.planes}}, win, g,
          s, "random");
      break;
    }
    case 5: {
      const uint32_t ref = NewRef(a, g, w);
      win[0] = a.All();
      Run({0x4D6970, DlightBits(g), 0, 0, 0, 0, 0, {ref}}, win, g, s, "random");
      break;
    }
    default: {
      const uint32_t b = a.Alloc(0x18);
      WrBounds(g, w, b);
      win[0] = a.All();
      Run({0x4D68E0, DlightBits(g), 0, 0, b + 0xC, 0, b, {}}, win, g, s, "random");
      break;
    }
  }
}

// ---- the sky --------------------------------------------------------------------

constexpr uint32_t kSkyBounds = 0x11CD800;  // sky_mins[2][6], sky_maxs[2][6]

void SkyCase(Arena& a, std::mt19937& g, Stats& s, uint32_t special) {
  a.Reset();
  if (g() % 4 == 0) {
    for (int i = 0; i < 12; i++) {
      WrF(kSkyBounds + 4 * i, 9999);
      WrF(kSkyBounds + 0x30 + 4 * i, -9999);
    }
  } else if (g() % 8 == 0) {
    for (int i = 0; i < 24; i++) WrF(kSkyBounds + 4 * i, Value(g, -2, 2, special));
  }
  const int32_t nump = g() % 16 == 0 ? int32_t(g() % 40) : g() % 4 ? Pick(g, {3, 4}) : int32_t(g() % 10);
  const uint32_t vecs = a.Alloc(12 * uint32_t(nump + 1));
  const float scale = Pick(g, {1, 100, 10000});
  // Now and then y (or z) is a permutation of x, so that the summed
  // vertices nearly tie and the face depends on where the sums round.
  const int tie = g() % 4 == 0 ? 1 + int(g() % 2) : 0;
  const bool wide = g() % 4 == 0;
  std::vector<float> xs;
  for (int32_t i = 0; i < nump; i++) xs.push_back(wide ? Wide(g, -20, 20) : Uniform(g, -scale, scale));
  // Or y comes in pairs of large values that cancel, around small ones,
  // so that where its sum is rounded decides its size.
  std::vector<float> ys;
  if (!tie && g() % 4 == 0) {
    for (int32_t i = 0; i + 1 < nump; i += 2) {
      const float big = Wide(g, 10, 30);
      ys.push_back(big + Uniform(g, -1, 1));
      ys.push_back(-big);
    }
    ys.resize(size_t(nump), Uniform(g, -1, 1));
    std::shuffle(ys.begin(), ys.end(), g);
  }
  std::vector<float> perm = xs;
  std::shuffle(perm.begin(), perm.end(), g);
  for (int32_t i = 0; i < nump; i++) {
    float v[3];
    for (float& x : v) x = wide ? Wide(g, -20, 20) : Uniform(g, -scale, scale);
    v[0] = xs[size_t(i)];
    if (tie) v[tie] = (g() % 8 ? 1 : -1) * perm[size_t(i)];
    if (!ys.empty()) {
      v[0] = Uniform(g, -2, 2);
      v[1] = ys[size_t(i)];
      v[2] = Uniform(g, -2, 2);
    }
    // Now and then on a clip plane (|x| == |y|...) or an axis.
    if (g() % 8 == 0) v[g() % 3] = (g() % 2 ? 1 : -1) * v[g() % 3];
    if (g() % 16 == 0) v[g() % 3] = 0;
    for (int j = 0; j < 3; j++) WrF(vecs + 12 * uint32_t(i) + 4 * uint32_t(j), Value(g, v[j], v[j], special));
  }
  std::vector<Window> win = {a.All(), {kSkyBounds, 0x60}};
  if (g() % 4 == 0)
    Run({0x4DB3E0, vecs, uint32_t(nump), 0, 0, 0, 0, {}}, win, g, s, "random");
  else
    Run({0x4DB660, 0, 0, 0, 0, 0, 0, {uint32_t(nump), vecs, g() % 4 ? 0u : uint32_t(g() % 7)}}, win, g, s,
        "random");
}

// ClipSkyPolygon's error: more than 62 vertices (at the last stage, where
// the rest of the polygon is added).
void SkyErrorCase(Arena& a, std::mt19937& g, Stats& s) {
  a.Reset();
  const int32_t nump = 63;
  const uint32_t vecs = a.Alloc(12 * uint32_t(nump + 1));
  for (int32_t i = 0; i < nump * 3; i++) WrF(vecs + 4 * uint32_t(i), Uniform(g, -100, 100));
  Run({0x4DB660, 0, 0, 0, 0, 0, 0, {uint32_t(nump), vecs, 6}}, {a.All(), {kSkyBounds, 0x60}}, g,
      s, "random");
}

// ---- texture coordinates ----------------------------------------------------------

constexpr uint32_t kNumVertexes = 0x17F0C88, kTexCoordPtrs = 0x16E8C40;
constexpr uint32_t kTessArrays[] = {0x1630C40, 0x1650C40, 0x1670C40, 0x1698C40, 0x16B0C40,
                                    0x16C8C40};

void TexCoordsCase(Arena& a, std::mt19937& g, Stats& s, uint32_t special) {
  a.Reset();
  const uint32_t nv = g() % 16 == 0 ? g() % 1000 : g() % 40;
  const uint32_t stride = g() % 8 ? Pick(g, {3, 4}) : g() % 6;
  Wr(kNumVertexes, nv);
  Wr(0x17F0C60, stride);
  Wr(0x11E075C, g() % 3 == 0);
  Wr(0x17F0C68, g() % 2);
  Wr(0x17F0C6C, g() % 2);
  Wr(0x17F0C70, g());
  const uint32_t ent = a.Alloc(0x80);
  Wr(0x1549B3C, ent);
  const bool wide = g() % 4 == 0;
  for (uint32_t i = 0; i < nv * 6; i++) {
    WrF(kTessArrays[0] + 4 * i, wide ? Wide(g, -30, 30) : Value(g, -1000, 1000, special));
    // x and y equal, for vectors whose x and y terms cancel (below).
    if (wide && i % uint32_t(std::max(stride, 1u)) == 1 && g() % 2)
      WrF(kTessArrays[0] + 4 * i, g_mem.read<float>(kTessArrays[0] + 4 * i - 4));
    WrF(kTessArrays[1] + 4 * i, Value(g, -2, 2, special));
    WrF(kTessArrays[2] + 4 * i, Value(g, -2, 2, special));
    WrF(kTessArrays[3] + 4 * i, Value(g, -1, 1, special));
    WrF(kTessArrays[4] + 4 * i, Value(g, -1, 1, special));
    WrF(kTessArrays[5] + 4 * i, Value(g, -1, 1, special));
  }
  std::vector<Window> win = {{kTexCoordPtrs, 0x20}};
  for (uint32_t st = 0; st < 8; st++) {
    const uint32_t buf = kTexCoordPtrs + 0x20 + st * 0x20000;
    for (uint32_t i = 0; i < nv * 3; i++) WrF(buf + 4 * i, Value(g, -2, 2, special));
    win.push_back({buf, nv * 12 + 16});
  }
  for (uint32_t t : kTessArrays) win.push_back({t, nv * 24 + 16});
  const uint32_t shader = a.Alloc(0xAC + 8 * 0xC8);
  for (uint32_t st = 0; st < 8; st++) {
    const uint32_t stage = shader + 0xAC + st * 0xC8;
    Wr(stage - 0xC, g() % 40 == 0 ? 0 : g() % 24);
    for (int i = 0; i < 6; i++) WrF(stage - 8 + 4 * i, wide ? Wide(g, -30, 30) : Value(g, -1, 1, special));
    if (wide && g() % 2) {
      WrF(stage - 4, -g_mem.read<float>(stage - 8));
      WrF(stage + 8, -g_mem.read<float>(stage + 4));
    }
    const int32_t n = g() % 2 ? 0 : g() % 16 == 0 ? Pick(g, {-1, 6, 8}) : int32_t(g() % 4);
    Wr(stage + 0x10, uint32_t(n));
    const uint32_t mods = a.Alloc(uint32_t(std::max(n, 0)) * 0x44);
    Wr(stage + 0x14, mods);
    for (int32_t i = 0; i < n; i++) {
      for (uint32_t j = 0; j < 0x44; j += 4) Wr(mods + 0x44 * uint32_t(i) + j, g());
      // TMOD_NONE sets the index to 4 (the loop ends at 5): from index 5 on
      // the original would loop for ever.
      Wr(mods + 0x44 * uint32_t(i),
         g() % 24 == 0 ? Pick(g, {i < 5 ? 0 : 11, 11, 40}) : 1 + g() % 10);
    }
  }
  win.push_back(a.All());
  Run({0x4F5940, shader, 0, 0, 0, 0, 0, {}}, win, g, s, "random");
}

// ---- sorting, matrices, copies --------------------------------------------------

void SortCase(Arena& a, std::mt19937& g, Stats& s) {
  a.Reset();
  const uint32_t n = g() % 4 == 0 ? g() % 10 : g() % 600;
  const uint32_t width = g() % 8 ? 8 : Pick(g, {0, 4, 12, 16});
  const uint32_t keys = g() % 2 ? 0xFFFFFFFF : Pick(g, {1, 4, 64, 1000});
  const uint32_t base = a.Alloc(n * std::max(width, 8u) + 8);
  for (uint32_t i = 0; i < n * std::max(width, 8u) / 4; i++)
    Wr(base + 4 * i, i % 2 || keys == 0xFFFFFFFF ? g() : g() % keys);
  Run({0x4CADC0, n, base, 0, width, 0, 0, {}}, {a.All()}, g, s, "random");
}

void MatrixCase(Arena& a, std::mt19937& g, Stats& s, uint32_t special) {
  a.Reset();
  const uint32_t m = a.Alloc(0x100);
  for (uint32_t i = 0; i < 64; i++) WrF(m + 4 * i, Value(g, -100, 100, special));
  uint32_t A = m, B = m + 0x40, out = m + 0x80;
  switch (g() % 6) {
    case 0: out = A; break;
    case 1: out = B; break;
    case 2: B = A; break;
    case 3: out = A + 16 * (1 + g() % 3); break;
    default: break;
  }
  Run({0x4C94D0, A, B, out, 0, 0, 0, {}}, {a.All()}, g, s, "random");
}

void MemcpyCase(Arena& a, std::mt19937& g, Stats& s) {
  a.Reset();
  const uint32_t buf = a.Alloc(0x6000);
  for (uint32_t i = 0; i < 0x6000 / 4; i++) Wr(buf + 4 * i, g());
  const uint32_t count = g() % 4 == 0 ? g() % 64 : g() % 5000;
  uint32_t dst = buf + g() % 0x800, src = buf + 0x3000 + g() % 0x800;
  if (g() % 4 == 0) src = dst + int32_t(g() % 200) - 100;  // overlapping
  Run({0x42B0A0, 0, 0, 0, 0, 0, 0, {dst, src, count}}, {a.All()}, g, s, "random");
}

// ---- the view ----------------------------------------------------------------

// The view's globals (the fogs, the view and current orientations, the fov,
// frustum, far clip, projection and refdef), the time and the cvars.
constexpr uint32_t kViewGlobals = 0x1535900, kViewGlobalsEnd = 0x1536480;
constexpr uint32_t kFog = 0x1535920, kFogFrom = 0x1535960, kFogTo = 0x15359A0, kTime = 0x1536360;
constexpr uint32_t kViewCvars[] = {0x15344F4, 0x15346E0, 0x15347E4, 0x15343F0, 0x1534458};

// Values over a few units or, wide, of any sign over 2^-30..2^30 (so that
// the order of a sum of products shows in its rounding).
float ViewValue(std::mt19937& g, float size, uint32_t special, bool wide) {
  return wide ? Wide(g, -30, 30) : Value(g, -size, size, special);
}

// An orientation (origin and three axes) in the arena, or a global one.
uint32_t Orientation(Arena& a, std::mt19937& g, uint32_t special, bool wide) {
  if (g() % 8 == 0) return Pick(g, {0x153629C, 0x1536030});
  const uint32_t o = a.Alloc(0x30);
  for (uint32_t i = 0; i < 12; i++) WrF(o + 4 * i, ViewValue(g, i < 3 ? 4096 : 1, special, wide));
  return o;
}

// A sum of three products n[i] * p[i] that nearly cancels: two of about A
// whose sum is 0 or one ulp of A, and the third about 2^-52 of A, in
// random places, so that the order of the sum shows in its result.
void Cancel3(std::mt19937& g, float n[3], float p[3]) {
  const uint32_t i = g() % 3, k = (i + 1 + g() % 2) % 3, j = 3 - i - k;
  const float A = std::ldexp(Uniform(g, 1, 2), int(g() % 40) - 20), sign = g() % 2 ? 1.0f : -1.0f;
  n[i] = sign;
  n[k] = -sign;
  p[i] = A;
  p[k] = g() % 2 ? A : std::nextafter(A, 2 * A);
  n[j] = g() % 2 ? 1.0f : -1.0f;
  p[j] = std::ldexp(Uniform(g, 1, 2), std::ilogb(A) - 54 + int(g() % 5));
}

uint32_t Vector(Arena& a, std::mt19937& g, uint32_t special, float size, bool wide) {
  const uint32_t v = a.Alloc(0x18);
  for (uint32_t i = 0; i < 6; i++) WrF(v + 4 * i, ViewValue(g, size, special, wide));
  return v;
}

void ViewCase(Arena& a, std::mt19937& g, Stats& s, uint32_t special) {
  a.Reset();
  const bool wide = g() % 4 == 0, cancel = !wide && g() % 3 == 0;
  float cn[3], cp[3];
  Cancel3(g, cn, cp);
  for (uint32_t va = kViewGlobals; va < kViewGlobalsEnd; va += 4)
    WrF(va, wide ? Wide(g, -20, 20) : Value(g, -1000, 1000, special));
  // Unit-ish axes and plane normals.
  for (uint32_t va = 0x153603C; va < 0x1536060; va += 4) WrF(va, ViewValue(g, 1, special, wide));
  for (uint32_t va = 0x15362A8; va < 0x15362CC; va += 4) WrF(va, ViewValue(g, 1, special, wide));
  for (uint32_t p = 0x15361F8; p < 0x1536248; p += 0x14)
    for (uint32_t k = 0; k < 3; k++) WrF(p + 4 * k, ViewValue(g, 1, special, wide));
  for (uint32_t fov : {0x1536168u, 0x153616Cu, 0x1536328u, 0x153632Cu})
    WrF(fov, g() % 16 ? Value(g, 1, 179, special) : Pick(g, {0, 180, 360, -90}));
  // Fogs: their modes, times and flags.
  for (uint32_t f : {kFogFrom, kFogTo}) {
    Wr(f, uint32_t(g() % 8 ? Pick(g, {0x800, 0x2601}) : Pick(g, {0, 1, 0x801})));
    Wr(f + 0x38, g() % 2);
  }
  const int32_t time = g() % 8 ? int32_t(g() % 100000) : int32_t(g());
  Wr(kTime, uint32_t(time));
  const int32_t start = g() % 8 ? time - int32_t(g() % 3000) : int32_t(g());
  Wr(kFogTo + 8, uint32_t(start));
  Wr(kFogTo + 0xC, g() % 8 ? uint32_t(start + Pick(g, {0, 1, 500, 2000, 5000}) * int32_t(g() % 3)) :
                             g() % 2 ? 0 : g());
  Wr(0x15359D0, g() % 2);
  // A linear fog ending at the far clip (now and then at -0 for a far clip
  // of 0, equal but not the same bits).
  if (g() % 4 == 0) {
    if (g() % 2) Wr(0x1536248, 0);
    const uint32_t end = Rd(0x1536248) ? Rd(0x1536248) : 0x80000000;
    Wr(kFog + 0x24, end);
    Wr(kFogTo + 0x24, end);
  }
  Wr(0x1536364, g() % 4 ? 0 : g());
  for (uint32_t cvar : kViewCvars) {
    const uint32_t v = a.Alloc(0x24);
    Wr(cvar, v);
    WrF(v + 0x1C, g() % 4 ? Value(g, 0, 8192, special) : 0);
    Wr(v + 0x20, g() % 3 ? 0 : Pick(g, {1, 4, 5, 5}));
  }
  std::vector<Window> win = {{0x1534000, 0x3000}};
  switch (g() % 9) {
    case 0: {
      const uint32_t b = Vector(a, g, special, g() % 2 ? 100 : 4096, wide);
      // Ordered bounds mostly.
      if (g() % 4)
        for (uint32_t i = 0; i < 3; i++) {
          const float lo = g_mem.read<float>(b + 4 * i), hi = g_mem.read<float>(b + 0xC + 4 * i);
          if (lo > hi) {
            WrF(b + 4 * i, hi);
            WrF(b + 0xC + 4 * i, lo);
          }
        }
      if (cancel) {
        // Corners summed to near zero (the first component of each), or
        // the corners as they are (no rotation) against a plane through
        // the origin summing them to near zero.
        const bool corners = g() % 2;
        for (uint32_t i = 0; i < 3; i++) {
          WrF(b + 4 * i, cp[i]);
          WrF(b + 0xC + 4 * i, corners && g() % 2 ? cp[i] * 2 : cp[i]);
          WrF(0x153629C + 4 * i, corners && i ? Value(g, -1000, 1000, 0) : 0.0f);
          for (uint32_t ax = 0; ax < 3; ax++)
            WrF(0x15362A8 + 0xC * ax + 4 * i, corners ? (i ? Value(g, -1, 1, 0) : cn[ax])
                                                      : (ax == i ? 1.0f : 0.0f));
        }
        const uint32_t pl = 0x15361F8 + 0x14 * (g() % 4);
        for (uint32_t i = 0; i < 3; i++) WrF(pl + 4 * i, corners ? (i ? 0.0f : 1.0f) : cn[i]);
        WrF(pl + 0xC, 0);
      }
      win.push_back(a.All());
      Run({0x4C8D50, b, 0, 0, 0, 0, 0, {}}, win, g, s, "random");
      break;
    }
    case 1: {
      const uint32_t p = Vector(a, g, special, 2000, wide);
      float r = Value(g, 0, 500, special);
      // A grazing sphere: an axis-aligned plane at an integral distance,
      // the centre the radius (or minus it) from it.
      if (g() % 4 == 0) {
        r = float(g() % 64);
        const uint32_t pl = 0x15361F8 + 0x14 * (g() % 4), k = g() % 3;
        const float n = g() % 2 ? 1.0f : -1.0f, d = float(int32_t(g() % 200) - 100);
        for (uint32_t i = 0; i < 3; i++) WrF(pl + 4 * i, i == k ? n : 0.0f);
        WrF(pl + 0xC, d);
        WrF(p + 4 * k, n * (d + (g() % 2 ? r : -r)));
      } else if (cancel) {
        // A plane through the origin and a centre on it but for rounding.
        const uint32_t pl = 0x15361F8 + 0x14 * (g() % 4);
        for (uint32_t i = 0; i < 3; i++) {
          WrF(pl + 4 * i, cn[i]);
          WrF(p + 4 * i, cp[i]);
        }
        WrF(pl + 0xC, 0);
        r = 0;
      }
      win.push_back(a.All());
      uint32_t rb;
      std::memcpy(&rb, &r, 4);
      Run({0x4C9070, 0, 0, p, 0, 0, 0, {rb}}, win, g, s, "random");
      break;
    }
    case 2: {
      const uint32_t in = Vector(a, g, special, 100, wide);
      if (cancel)
        for (uint32_t i = 0; i < 3; i++) {
          WrF(in + 4 * i, cp[i]);
          WrF(0x15362A8 + 0xC * i + 4 * (g() % 3), cn[i]);
        }
      const uint32_t out = g() % 4 ? a.Alloc(12) : in + 4 * (g() % 3);
      win.push_back(a.All());
      Run({0x4C90F0, in, out, 0, 0, 0, 0, {}}, win, g, s, "random");
      break;
    }
    case 3: case 4: case 5: case 6: {
      const uint32_t fn = uint32_t(Pick(g, {0x4C9930, 0x4C9B90, 0x4C9C00, 0x4C9D30}));
      if (cancel && fn == 0x4C9D30)
        // The view's forward axis and origin summing a plane's distance to
        // near zero (the side axes zero).
        for (uint32_t i = 0; i < 3; i++) {
          WrF(0x153603C + 4 * i, cn[i]);
          WrF(0x1536048 + 4 * i, 0);
          WrF(0x1536054 + 4 * i, 0);
          WrF(0x1536030 + 4 * i, cp[i]);
        }
      win.push_back(a.All());
      Run({fn, 0, 0, 0, 0, 0, 0, {}}, win, g, s, "random");
      break;
    }
    default: {
      const uint32_t p = Vector(a, g, special, 4096, wide), A = Orientation(a, g, special, wide),
                     B = Orientation(a, g, special, wide);
      uint32_t out = a.Alloc(12);
      if (g() % 6 == 0) out = Pick(g, {int32_t(p), int32_t(B), int32_t(A + 4)});
      win.push_back(a.All());
      if (g() % 2)
        Run({0x4C9FF0, B, A, out, 0, p, 0, {}}, win, g, s, "random");
      else
        Run({0x4CA0C0, out, B, A, 0, p, 0, {}}, win, g, s, "random");
      break;
    }
  }
}

// ---- the draw surface list ---------------------------------------------------

constexpr uint32_t kSortedShaders = 0x154052C, kBackEndData = 0x1549698;

// The back end's data placed so that its command buffer's next command
// lands in a buffer of the arena (or it is full); windows for both.
void BackEndData(std::mt19937& g, std::vector<Window>& win) {
  const uint32_t cmd = kArena + 0x800000, used = g() % 8 ? g() % 0x3FC05 : 0x3FC04 + g() % 16;
  const uint32_t counter = cmd + 0x40000 - used;  // so that the command lands at cmd
  Wr(counter, used);
  Wr(kBackEndData, counter - 0x28D700);
  win.push_back({counter, 4});
  win.push_back({cmd, 0x3F8});
}

void SurfListCase(Arena& a, std::mt19937& g, Stats& s, uint32_t special) {
  a.Reset();
  std::vector<Window> win = {{0x1536000, 0x500}};
  for (uint32_t va = 0x1536000; va < 0x1536500; va += 4) Wr(va, g());
  const uint32_t shaders = a.Alloc(8 * 0x60);
  for (uint32_t i = 0; i < 8 * 0x60; i += 4) Wr(shaders + i, g());
  for (uint32_t i = 0; i < 8; i++)
    WrF(shaders + 0x60 * i + 0x58,
        g() % 4 ? float(Pick(g, {0, 1, 1, 1, 2, 3, 5})) : Value(g, 0, 4, special));
  for (uint32_t i = 0; i < 16; i++) Wr(kSortedShaders + 4 * i, shaders + 0x60 * (g() % 8));
  win.push_back({kSortedShaders, 0x40});
  for (uint32_t cvar : {0x14E43FCu, 0x1534400u}) {
    const uint32_t v = a.Alloc(0x24);
    Wr(cvar, v);
    Wr(v + 0x20, g() % 4 ? 0 : 1);
  }
  switch (g() % 3) {
    case 0:
      DrawSurfs(a, g);
      win.push_back({kNumDrawSurfs, 8});
      win.push_back(a.All());
      Run({0x4CAF00, uint32_t(g() % 4 ? g() % 64 : g()), 0, uint32_t(shaders + 0x60 * (g() % 8)), 0, 0,
           0,
           {uint32_t(g()), uint32_t(g() % 2), uint32_t(g() % 4 ? g() % 2 : g()),
            uint32_t(g() % 4 ? g() % 2 : g())}},
          win, g, s, "random");
      break;
    case 1:
      BackEndData(g, win);
      win.push_back(a.All());
      Run({0x4D4230, 0, 0, uint32_t(g() % 0x10000), 0, 0, 0, {uint32_t(g())}}, win, g, s, "random");
      break;
    default: {
      // Sort keys over the 16 shaders, entities and flags, now and then
      // equal ones.
      // Now and then more than the limit (0x10000 of them sorted).
      const uint32_t n = g() % 8 == 0 ? Pick(g, {0, -1, 0x10001}) : g() % 40;
      const uint32_t len = n == 0x10001 ? n : std::min(n, 64u);
      const uint32_t surfs = a.Alloc(len * 8 + 8);
      for (uint32_t i = 0; i < len; i++) {
        Wr(surfs + 8 * i, uint32_t((g() % 16) << 18 | (g() % 0x800) << 7 | (g() & 0x7F)));
        if (i && g() % 4 == 0) Wr(surfs + 8 * i, Rd(surfs + 8 * i - 8));
        Wr(surfs + 8 * i + 4, uint32_t(g()));
      }
      BackEndData(g, win);
      win.push_back(a.All());
      Run({0x4CAFC0, 0, 0, 0, 0, 0, 0, {surfs, n}}, win, g, s, "random");
      break;
    }
  }
}

// ---- the world's view -----------------------------------------------------------

constexpr uint32_t kCullGlobals = 0x11BEA00, kWorld = 0x1535A34;

// A random BSP of depth up to 6 over random planes; leaves have a cluster
// (not -1) and a cell.
uint32_t NewBsp(Arena& a, std::mt19937& g, uint32_t special, int depth) {
  const uint32_t node = a.Alloc(0x18);
  if (depth >= 6 || (depth && g() % 3 == 0)) {
    Wr(node, g() % 8 ? g() % 100 : g() | 1);
    Wr(node + 8, g());
    return node;
  }
  Wr(node, 0xFFFFFFFF);
  const uint32_t p = a.Alloc(0x14);
  for (uint32_t i = 0; i < 4; i++) WrF(p + 4 * i, Value(g, i < 3 ? -1 : -1000, i < 3 ? 1 : 1000, special));
  if (g() % 4 == 0) Wr(p + 0xC, 0);
  Wr(node + 0xC, p);
  Wr(node + 0x10, NewBsp(a, g, special, depth + 1));
  Wr(node + 0x14, NewBsp(a, g, special, depth + 1));
  return node;
}

void WorldViewCase(Arena& a, std::mt19937& g, Stats& s, uint32_t special) {
  a.Reset();
  for (uint32_t va = kCullGlobals; va < kCullGlobals + 0x200; va += 4) WrF(va, Value(g, -1000, 1000, special));
  for (uint32_t va = 0x1535900; va < 0x1536480; va += 4) WrF(va, Value(g, -1000, 1000, special));
  Wr(0x1535920, uint32_t(Pick(g, {0x800, 0x2601, 0x2601, 0})));
  Wr(0x1535950, g() % 2);
  Wr(0x13F9CF8, uint32_t(Pick(g, {-1, 0, 1, 2})));
  const uint32_t far = a.Alloc(0x24);
  Wr(0x15343F0, far);
  WrF(far + 0x1C, g() % 3 ? 0.0f : Value(g, 0, 8192, special));
  std::vector<Window> win = {{kCullGlobals, 0x200}, {0x1535900, 0xB80}, {0x13F9CF8, 4}};
  // A view origin at 0 now and then (for distances of exactly 0).
  if (g() % 4 == 0)
    for (uint32_t i = 0; i < 3; i++) Wr(0x11BEAF8 + 4 * i, 0);
  switch (g() % 8) {
    case 7: {
      // The world's view: its planes through an origin that nearly
      // cancels against the forward axis now and then.
      Wr(0x564550, g() % 4 ? 1 : 0);
      Wr(0x1536138, g() % 2 ? 0 : g());
      for (uint32_t cvar : {0x153450Cu, 0x153452Cu}) {
        const uint32_t v = a.Alloc(0x24);
        Wr(cvar, v);
        Wr(v + 0x20, g() % 2);
        WrF(v + 0x1C, g() % 2 ? 0.0f : Value(g, -1, 1, special));
      }
      switch (g() % 4) {
        case 0: {
          float n[3], p[3];
          Cancel3(g, n, p);
          for (uint32_t i = 0; i < 3; i++) {
            WrF(0x153603C + 4 * i, n[i]);
            WrF(0x1536030 + 4 * i, p[i]);
          }
          break;
        }
        case 1: {
          // Two huge products that cancel and a small one, first or last.
          const uint32_t i = g() % 3, k = (i + 1 + g() % 2) % 3, j = 3 - i - k;
          const float A = Wide(g, 30, 60), sign = g() % 2 ? 1.0f : -1.0f;
          for (uint32_t x : {i, k}) {
            WrF(0x153603C + 4 * x, x == i ? sign : -sign);
            WrF(0x1536030 + 4 * x, std::fabs(A));
          }
          WrF(0x153603C + 4 * j, Uniform(g, -1, 1));
          WrF(0x1536030 + 4 * j, Uniform(g, -4, 4));
          break;
        }
        case 2: {
          // A far plane through about the far distance (a huge one): its
          // distance's last bits show.
          const float d = std::fabs(Wide(g, 20, 40));
          WrF(far + 0x1C, d);
          const uint32_t x = g() % 3;
          for (uint32_t i = 0; i < 3; i++) {
            WrF(0x153603C + 4 * i, i == x ? -1.0f : 0.0f);
            WrF(0x1536030 + 4 * i, i == x ? (g() % 2 ? d : d + Uniform(g, -1, 1)) : Uniform(g, -4, 4));
          }
          break;
        }
        default:
          break;
      }
      win.push_back(a.All());
      win.push_back({0x564550, 4});
      Run({0x4D9820, 0, 0, 0, 0, 0, 0, {}}, win, g, s, "random");
      break;
    }
    case 0: {
      const uint32_t p = a.Alloc(0x14);
      for (uint32_t i = 0; i < 0x14; i += 4) WrF(p + i, Value(g, -1, 1, special));
      // Normals of -0 and +0 now and then.
      if (g() % 4 == 0) Wr(p + 4 * (g() % 3), g() % 2 ? 0x80000000 : 0);
      win.push_back(a.All());
      Run({0x4D6830, p, 0, 0, 0, 0, 0, {}}, win, g, s, "random");
      break;
    }
    case 1: {
      const uint32_t fn = uint32_t(Pick(g, {0x4D6880, 0x4D6A10, 0x4D6AB0}));
      if (fn == 0x4D6AB0 && g() % 3 == 0) {
        // A row of the view matrix by a column of the projection summing
        // to near zero (its fourth term zero).
        float n[3], p[3];
        Cancel3(g, n, p);
        const uint32_t r = g() % 4, col = g() % 4;
        for (uint32_t i = 0; i < 3; i++) {
          WrF(0x15360E8 + 16 * r + 4 * i, p[i]);
          WrF(0x1536178 + 16 * i + 4 * col, n[i]);
        }
        WrF(0x15360E8 + 16 * r + 12, 0);
      }
      win.push_back(a.All());
      Run({fn, 0, 0, 0, 0, 0, 0, {}}, win, g, s, "random");
      break;
    }
    case 2: {
      const uint32_t world = g() % 32 ? a.Alloc(0x9C) : 0;
      if (world) Wr(world + 0x98, NewBsp(a, g, special, 0));
      else Wr(0x98, NewBsp(a, g, special, 0));
      Wr(kWorld, world);
      win.push_back(a.All());
      Run({0x4D7DF0, 0, 0, 0, 0, 0, 0, {}}, win, g, s, "random");
      break;
    }
    default: {
      const uint32_t poly = a.Alloc(0x20), np = g() % 6, nplanes = g() % 6;
      const uint32_t pts = a.Alloc(np * 0xC + 4), planes = a.Alloc(nplanes * 0x14 + 4);
      for (uint32_t i = 0; i < np * 3; i++) WrF(pts + 4 * i, Value(g, -100, 100, special));
      for (uint32_t i = 0; i < nplanes; i++) {
        for (uint32_t k = 0; k < 3; k++) WrF(planes + 0x14 * i + 4 * k, Value(g, -1, 1, special));
        WrF(planes + 0x14 * i + 0xC, Value(g, -100, 100, special));
      }
      Wr(poly + 0x18, pts);
      Wr(poly + 0x1C, np);
      win.push_back(a.All());
      Run({g() % 2 ? 0x4D7E70u : 0x4D7ED0u, poly, nplanes, planes, 0, 0, 0, {}}, win, g, s, "random");
      break;
    }
  }
}

// ---- the world's static models and cells ---------------------------------------

constexpr uint32_t kCellRefs = 0x11BEB88, kNumCellRefs = 0x11BEB8C, kCullFrustum = 0x11BEB04;
constexpr uint32_t kNumStaticModels = 0x153646C, kStaticModels = 0x1536470, kXModels = 0x15364F8;
constexpr uint32_t kStaticCvars[] = {0x153470C, 0x1534420, 0x15343A0, 0x1534424,
                                     0x15344B8, 0x1534550, 0x15343D4};

// A model's local bounds: half sizes about 1536 / 2 (exactly, now and then)
// so that the scaled sizes straddle the limit of 1536.
void LocalBounds(std::mt19937& g, const World& w, uint32_t b) {
  for (int i = 0; i < 3; i++) {
    const float h = g() % 4 ? Pick(g, {8, 700, 768, 1000}) : Value(g, 0, 1000, w.special);
    const float c = g() % 2 ? 0 : Uniform(g, -h, h);
    WrF(b + 4 * i, Value(g, c - h, c - h, w.special));
    WrF(b + 0xC + 4 * i, Value(g, c + h, c + h, w.special));
  }
}

// A static model (0x2B8 bytes at m) of a random type, its axes now and
// then unit, of equal stored lengths (permuted components) or wide.
void StaticModel(Arena& a, std::mt19937& g, const World& w, uint32_t m) {
  Wr(m, uint32_t(Pick(g, {0, 0, 1, 1, 2, -1})));
  Wr(m + 8, g() % 4);
  // Unit axes and an integral origin now and then (sizes of exactly
  // 1536), an origin at 0 (where the scale's last bits show).
  const uint32_t kind = g() % 8;
  const bool wide = kind == 1, perm = kind == 2 || kind == 3;
  float v[3];
  for (float& x : v) x = wide ? Wide(g, -10, 10) : Value(g, -2, 2, w.special);
  for (uint32_t k = 0; k < 3; k++) {
    if (kind == 0) for (uint32_t i = 0; i < 3; i++) v[i] = i == k ? 1.0f : 0.0f;
    else if (!perm) for (float& x : v) x = wide ? Wide(g, -10, 10) : Value(g, -2, 2, w.special);
    else std::shuffle(v, v + 3, g);
    // The first two of equal stored lengths, the third shorter.
    if (perm && k == 2)
      for (float& x : v) x *= 0.5f;
    for (uint32_t i = 0; i < 3; i++) WrF(m + 0x1C + 0xC * k + 4 * i, v[i]);
  }
  for (uint32_t i = 0; i < 3; i++)
    WrF(m + 0x44 + 4 * i, kind == 0 ? std::round(Uniform(g, -w.size, w.size))
                          : kind == 3 || g() % 4 == 0 ? 0.0f : Value(g, -w.size, w.size, w.special));
  const uint32_t p1 = a.Alloc(0x1C), p2 = a.Alloc(8), model = a.Alloc(0x64);
  Wr(m + 0x90, p1);
  Wr(p1 + 0x18, p2);
  Wr(p2 + 4, model);
  LocalBounds(g, w, model + 0x4C);
  Wr(m + 0x2B4, g() % 2 ? 0 : g() % 3);
}

// A BSP for the static models' links: nodes (cell -2) over planes axial
// (type the axis) or not (type 3 and up; signbits from the normal), now
// and then through the model's bounds' corners; leaves with a cell or -1.
// (BoxOnPlaneSide answers 0 for NaN distances or signbits that disagree
// with the normal, and the walk then takes the plane for a node; an axial
// plane's split assumes a positive normal.)
uint32_t LinkBsp(Arena& a, std::mt19937& g, const World& w, uint32_t numCells, uint32_t b, int depth) {
  const uint32_t node = a.Alloc(0x18);
  if (depth >= 5 || (depth && g() % 3 == 0)) {
    Wr(node, g() % 100);
    Wr(node + 8, numCells && g() % 4 ? g() % numCells : 0xFFFFFFFF);
    return node;
  }
  Wr(node, 0xFFFFFFFF);
  Wr(node + 8, 0xFFFFFFFE);
  const uint32_t p = a.Alloc(0x14);
  float n[3] = {0, 0, 0};
  uint8_t type, bits = 0;
  if (g() % 2) {
    type = uint8_t(g() % 3);
    n[type] = 1;
  } else {
    type = 3;
    for (float& x : n) x = Uniform(g, -1, 1);
  }
  for (int i = 0; i < 3; i++) {
    WrF(p + 4 * i, n[i]);
    if (n[i] < 0) bits |= uint8_t(1 << i);
  }
  const int axis = type < 3 ? type : int(g() % 3);
  const float lo = g_mem.read<float>(b + 4 * uint32_t(axis)), hi = g_mem.read<float>(b + 0xC + 4 * uint32_t(axis));
  WrF(p + 0xC, g() % 4 == 0 ? (g() % 2 ? lo : hi) : g() % 2 ? Uniform(g, lo, hi) : Value(g, -w.size, w.size, w.special));
  g_mem.write<uint8_t>(p + 0x10, g() % 32 ? type : uint8_t(3 + g() % 3));
  g_mem.write<uint8_t>(p + 0x11, bits);
  Wr(node + 0xC, p);
  Wr(node + 0x10, LinkBsp(a, g, w, numCells, b, depth + 1));
  Wr(node + 0x14, LinkBsp(a, g, w, numCells, b, depth + 1));
  return node;
}

// A cell (0x40 bytes) with a list of references to the static models.
uint32_t NewCell(Arena& a, std::mt19937& g, const World& w, uint32_t cell, uint32_t models, uint32_t n) {
  uint32_t link = 0;
  for (uint32_t i = g() % 4; i; i--) {
    const uint32_t r = a.Alloc(0x20);
    Wr(r, n && g() % 4 ? models + 0x2B8 * (g() % n) : a.Alloc(0x2B8));
    Wr(Rd(r) + 0x2B4, g() % 2 ? 0 : g() % 3);
    WrBounds(g, w, r + 4);
    Wr(r + 0x1C, link);
    link = r;
  }
  Wr(cell + 0x3C, link);
  return cell;
}

void StaticCases(Arena& a, std::mt19937& g, const World& base, Stats& s) {
  // The links through the BSP only from finite values (see LinkBsp).
  const uint32_t kase = g() % 8;
  World w = base;
  if (kase == 3 || kase == 4) w.special = 0;
  for (uint32_t cvar : kStaticCvars) {
    const uint32_t v = a.Alloc(0x24);
    Wr(cvar, v);
    Wr(v + 0x20, g() % 4 ? g() % 2 : uint32_t(Pick(g, {2, -1})));
  }
  for (uint32_t i = 0; i < 4; i++) {
    const uint32_t x = a.Alloc(0x50), b = a.Alloc(0x18);
    LocalBounds(g, w, b);
    Wr(x + 0x4C, b);
    Wr(kXModels + 4 * i, x);
  }
  const uint32_t n = g() % 8 ? g() % 6 : 0, models = a.Alloc(0x2B8 * (n + 1));
  for (uint32_t i = 0; i < n; i++) StaticModel(a, g, w, models + 0x2B8 * i);
  Wr(kNumStaticModels, g() % 16 ? n : uint32_t(-1));
  Wr(kStaticModels, models);
  // The references' array, placed so that the next ones land in a buffer;
  // its count at the limit now and then.
  const uint32_t refs = a.Alloc(256 * 0x20), count = g() % 4 ? g() % 8 : uint32_t(Pick(g, {0xFFF, 0x1000}));
  Wr(kNumCellRefs, count);
  Wr(kCellRefs, refs - count * 0x20);
  const uint32_t numCells = g() % 8 ? 1 + g() % 6 : 0, cells = a.Alloc(numCells * 0x40 + 4);
  for (uint32_t i = 0; i < numCells; i++) NewCell(a, g, w, cells + 0x40 * i, models, n);
  const uint32_t world = a.Alloc(0x128);
  Wr(kWorld, world);
  Wr(world + 0x120, numCells);
  Wr(world + 0x124, cells);
  const uint32_t box = a.Alloc(0x18);
  WrBounds(g, w, box);
  Wr(world + 0x98, LinkBsp(a, g, w, numCells, box, 0));
  for (uint32_t i = 0; i < 4 * 0x14; i += 4) Wr(kCullFrustum + i, Rd(w.planes + i));
  const bool graze = g() % 4 == 0;
  const uint32_t mins = a.Alloc(0x18), maxs = mins + 0xC;
  std::vector<Window> win = {a.All(), {kPlaneBits, 4}, {kNumDrawSurfs, 8}, {0x11BEB54, 0x40}};
  switch (kase) {
    case 0: {
      const uint32_t m = models + 0x2B8 * n;
      StaticModel(a, g, w, m);
      win[0] = a.All();
      Run({0x4D8910, uint32_t(g()), m, maxs, 0, mins, 0, {}}, win, g, s, "random");
      break;
    }
    case 1: {
      const uint32_t m = models + 0x2B8 * n;
      StaticModel(a, g, w, m);
      win[0] = a.All();
      Run({0x4D8A20, maxs, 0, 0, 0, 0, m, {mins}}, win, g, s, "random");
      break;
    }
    case 2: {
      const uint32_t cell = numCells ? cells + 0x40 * (g() % numCells) : NewCell(a, g, w, a.Alloc(0x40), models, n);
      const uint32_t r = Rd(cell + 0x3C);
      const uint32_t model = r && g() % 2 ? Rd(r) : models + 0x2B8 * (n ? g() % n : 0);
      WrBounds(g, w, mins);
      win[0] = a.All();
      Run({0x4D8B50, maxs, cell, mins, model, 0, 0, {}}, win, g, s, "random");
      break;
    }
    case 3: {
      std::memcpy(g_mem.ptr<uint8_t>(mins), g_mem.ptr<uint8_t>(box), 0x18);
      // A point now and then (mins == maxs), on a plane too.
      if (g() % 8 == 0) std::memcpy(g_mem.ptr<uint8_t>(maxs), g_mem.ptr<uint8_t>(mins), 0xC);
      win[0] = a.All();
      Run({0x4D8BE0, 0, 0, 0, 0, 0, 0, {Rd(world + 0x98), models + 0x2B8 * (n ? uint32_t(g()) % n : 0), mins, maxs}},
          win, g, s, "random");
      break;
    }
    case 4:
      // The count starts again from 0.
      Wr(kCellRefs, refs);
      Run({0x4D8D80, 0, 0, 0, 0, 0, 0, {w.planes, uint32_t(w.numPlanes)}}, win, g, s, "random");
      break;
    case 5: {
      const uint32_t cell = NewCell(a, g, w, a.Alloc(0x40), models, n);
      if (graze)
        for (uint32_t r = Rd(cell + 0x3C); r; r = Rd(r + 0x1C)) Graze(g, w, r + 4);
      win[0] = a.All();
      Run({0x4D8770, cell, 0, 0, 0, 0, 0, {w.planes, uint32_t(w.numPlanes)}}, win, g, s, "random");
      break;
    }
    case 6: {
      const uint32_t cell = NewCell(a, g, w, a.Alloc(0x40), models, n), surfs = a.Alloc(0x28);
      Wr(cell + 0x18, surfs);
      // References always, trees (preferred; a negative count is neither)
      // now and then.
      Refs(a, g, w, surfs + 0x18, g() % 5);
      if (g() % 16 == 0) Wr(surfs + 0x1C, uint32_t(-1));
      if (g() % 2) {
        const uint32_t k = g() % 4, nodes = a.Alloc(k * 0x28);
        for (uint32_t i = 0; i < k; i++) NewNode(a, g, w, nodes + 0x28 * i, 1);
        Wr(surfs + 0x20, nodes);
        Wr(surfs + 0x24, g() % 8 ? k : uint32_t(-1));
      }
      win[0] = a.All();
      Run({0x4D9290, 0, 0, 0, 0, cell, 0, {w.planes, uint32_t(w.numPlanes), DlightBits(g)}}, win, g, s,
          "random");
      break;
    }
    default: {
      const uint32_t k = g() % 6, list = a.Alloc(4 * k);
      for (uint32_t i = 0; i < k; i++) {
        const uint32_t ref = NewRef(a, g, w);
        if (graze && Rd(kFarPlane)) Graze(g, w, Rd(ref + 8) + 0xC);
        Wr(list + 4 * i, ref);
      }
      Wr(world + 0xA4, k);
      Wr(world + 0xA8, list);
      win[0] = a.All();
      Run({0x4D9D80, 0, 0, 0, 0, 0, 0, {}}, win, g, s, "random");
      break;
    }
  }
}

// ---- captures ------------------------------------------------------------------

bool ReadCaptures(const char* path, std::vector<Record>& out) {
  FILE* f = std::fopen(path, "rb");
  if (!f) {
    std::printf("%s: cannot open\n", path);
    return false;
  }
  for (;;) {
    uint32_t head[9];
    if (std::fread(head, 4, 9, f) != 9) break;
    if (head[0] != kMagic) {
      std::printf("%s: bad record\n", path);
      return false;
    }
    Record r{head[1], head[2], head[3], head[4], head[5], head[6], head[7], {}, {}};
    r.args.resize(head[8]);
    uint32_t n = 0;
    if (std::fread(r.args.data(), 4, r.args.size(), f) != r.args.size() ||
        std::fread(&n, 4, 1, f) != 1)
      return false;
    for (uint32_t i = 0; i < n; i++) {
      uint32_t h[2];
      if (std::fread(h, 4, 2, f) != 2) return false;
      Region reg{h[0], std::vector<uint8_t>(h[1])};
      if (std::fread(reg.bytes.data(), 1, h[1], f) != h[1]) return false;
      r.regions.push_back(std::move(reg));
    }
    out.push_back(std::move(r));
  }
  std::fclose(f);
  return true;
}

bool RunCaptures(const char* path, std::mt19937& g, Stats& s) {
  std::vector<Record> records;
  if (!ReadCaptures(path, records)) return false;
  for (const Record& r : records) {
    std::vector<Window> win;
    for (const Region& reg : r.regions) {
      const uint32_t size = uint32_t(reg.bytes.size());
      if (reg.va < kStack + kStackSize && reg.va + size > kStack) {
        std::printf("%s: a region at %08X overlaps the test's stack\n", path, reg.va);
        return false;
      }
      for (uint32_t p = reg.va & ~0xFFFu; p < reg.va + size; p += 0x1000)
        if (!g_mem.IsCommitted(p)) g_mem.Commit(p, 0x1000, recomp::Prot::RW);
      std::memcpy(g_mem.ptr<uint8_t>(reg.va), reg.bytes.data(), size);
      win.push_back({reg.va, size});
    }
    Wr(kErrorPtr, kError);
    Run({r.fn, r.eax, r.ecx, r.edx, r.ebx, r.esi, r.edi, r.args}, win, g, s, path);
  }
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
  // R_CellForCamera reads its node through a null world (after ri.Error).
  g_mem.Commit(0, 0x1000, recomp::Prot::RW);
  Wr(kErrorPtr, kError);
  Wr(kPrintfPtr, kPrintf);
  // The reference: the x87 model's exact arithmetic throughout.
  recomp::x87::run::g_doubles = false;

  std::mt19937 g(20261019);
  Stats s;
  Arena a;
  World w;
  for (int world = 0; world < 9000; world++) {
    const uint32_t special = world < 3000 ? 0 : world < 6000 ? 4 : 48;
    NewWorld(a, g, w, special);
    for (int i = 0; i < 20; i++) CullCases(a, g, w, s);
    for (int i = 0; i < 8; i++) StaticCases(a, g, w, s);
  }
  for (int i = 0; i < 150000; i++) SkyCase(a, g, s, i < 50000 ? 0 : i < 100000 ? 4 : 48);
  for (int i = 0; i < 20; i++) SkyErrorCase(a, g, s);
  for (int i = 0; i < 15000; i++) TexCoordsCase(a, g, s, i < 5000 ? 0 : i < 10000 ? 4 : 48);
  for (int i = 0; i < 10000; i++) SortCase(a, g, s);
  for (int i = 0; i < 20000; i++) MatrixCase(a, g, s, i < 10000 ? 0 : 48);
  for (int i = 0; i < 10000; i++) MemcpyCase(a, g, s);
  for (int i = 0; i < 60000; i++) ViewCase(a, g, s, i < 20000 ? 0 : i < 40000 ? 4 : 48);
  for (int i = 0; i < 30000; i++) SurfListCase(a, g, s, i < 15000 ? 0 : 48);
  for (int i = 0; i < 40000; i++) WorldViewCase(a, g, s, i < 20000 ? 0 : i < 30000 ? 4 : 48);
  const int random = s.cases;
  for (int i = 2; i < argc; i++)
    if (!RunCaptures(argv[i], g, s)) return 1;
  std::printf("%d random + %d captured cases (", random, s.cases - random);
  for (int i = 0; i < kNumEntries; i++)
    std::printf("%s%s %d", i ? ", " : "", kEntries[i].name, s.by_entry[i]);
  std::printf("; %d calls out): %d mismatched\n", s.calls, s.failures);
  return s.failures ? 1 : 0;
}
