// The native renderer functions (src/render.cpp) against the originals,
// lifted from the EXE at build time with `recomp_lift --only`
// (CMakeLists.txt) and run on SwARc's exact x87 model (SoftFloat, not
// its host-double runs). CoDSP.exe is mapped as the game maps it, for its
// constants and globals. The functions they call that src/render.cpp does
// not replace are stubs (tests/render_stubs.manifest) that record each
// call: its address, return address, ESP, the registers the callee reads
// and its stack arguments, which must be the same on both sides; they
// leave EAX, ECX and EDX changed (a light's intensity keeps EDX, as the
// original does, and pushes ST0); the debug polygon and line, an added
// entity and the merge of the lights beyond the limit also record a hash
// of what they are handed (on the caller's stack). ri.Milliseconds' time grows with the calls. The refimport's
// temporary allocation hands out blocks of a heap that starts again for
// each side. Each case runs one entry through the
// lifted original and the native code from the same guest memory, and both
// must leave the same bytes in the case's memory (the structures, the
// globals, the stack arguments), the same calls, and the same ESP, EBX,
// ESI, EDI, EBP and x87 stack (top, empty registers, control word), and
// the entries that return a value the same EAX (ST0 for 0x4D6880 and
// 0x4DF2D0). Not
// compared: EAX otherwise, ECX, EDX, EFLAGS, the x87 status word and the
// stack below the return address (refilled with one pattern before each
// side for the entries that keep locals there for their callees).
//
//   render_test EXE [CAPTURE...]
//
// Random cases (mt19937, fixed seed): small worlds of frustum planes,
// near/far planes, occluders with their counters, dlights, surface AABB
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
// that cancel and a far plane at a huge distance; polygons clipped (up to
// the 0x400 points), convex ones on portals near the view, occluders (at
// the 0x400 and 0x1800 limits too), cell graphs of three portals at most
// walked, coronas (finite: the CRT's square root of a NaN reports through
// the Windows API) and the world walk over such cells, static models,
// coronas and world models from a random view; portal surfaces (none,
// polygons, triangles; finite and not degenerate) with entities about
// their planes, mirrors and rotating portals, for the portal orientations
// and views, which the draw surface sort reaches too; entities of every
// type; scenes over the world walk's (or without a world): the view's
// surfaces, a view rendered and RE_RenderScene; the entities' lighting and
// a vertex's (point lights only where the positions are finite: the CRT's
// square root of a NaN under 0x426710 reports through the Windows API),
// over a model cache in states the game reaches. Values are now and then
// zeros, denormals, infinities, NaNs or huge. Captures
// (tests/render_capture.h) add the game's own calls.

#include <algorithm>
#include <bit>
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
// The refimport's temporary allocation and its free (0x1549610, 0x154961C):
// blocks from the heap below the stack, which starts again for each side of
// a case.
constexpr uint32_t kAllocPtr = 0x1549610, kAlloc = 0x7FFE0020;
constexpr uint32_t kFreePtr = 0x154961C, kFree = 0x7FFE0030;
// ri.Milliseconds (0x1549608): a time that grows with the calls.
constexpr uint32_t kMillisecondsPtr = 0x1549608, kMilliseconds = 0x7FFE0040;
// The refimport's 0x1549688 (a submodel's handle from the negation of
// its own) and 0x154968C (a composite model's callback).
constexpr uint32_t kSubModelPtr = 0x1549688, kSubModel = 0x7FFE0050;
constexpr uint32_t kModelCallbackPtr = 0x154968C, kModelCallback = 0x7FFE0060;
// The GL entry points a surface's cache entry calls (stdcall):
// glBindBufferARB, glBufferSubDataARB and the vertex buffer's 0x15350DC.
constexpr uint32_t kGlBindPtr = 0x1534DE4, kGlBind = 0x7FFE0070;
constexpr uint32_t kGlSubDataPtr = 0x1534B14, kGlSubData = 0x7FFE0080;
constexpr uint32_t kGlUpdatePtr = 0x15350DC, kGlUpdate = 0x7FFE0090;
constexpr uint32_t kHeap = 0x6FE00000, kHeapSize = 0x100000;
uint32_t g_heap_top = kHeap;

enum Reg { kEax = 1, kEcx = 2, kEdx = 4, kEbx = 8, kEsi = 16, kEdi = 32, kEbp = 64 };
constexpr int kSaved = kEbx | kEsi | kEdi | kEbp;

struct CallRec {
  uint32_t va, ret, esp, regs[7], args[8];
  bool operator==(const CallRec&) const = default;
};
std::vector<CallRec> g_calls;
// The composite models' surface pairs (0x477340's list): count, and how
// many submodels the model has (0x477290's EDX +0x16).
uint32_t g_pairs = 0;
// The models' cache (0x1C8-byte entries, the test's first eight, and the
// head of their list).
constexpr uint32_t kModelCache = 0x11BEC28, kModelCacheHead = 0x11CD050;
// The cache entries' vertex buffer (0x154955C): 32 x the 16 blocks of the
// eight entries, and a surface's 0x200 more.
constexpr uint32_t kVertexBuffer = 0x6FC00000, kVertexBufferSize = 24 * (8 * 16 * 32 + 0x200);
// Bounds for the bones' bounds 0x4771B0 hands out (eight of 24 bytes).
uint32_t g_bone_bounds = 0;
// The case's lights (kNumLights of 0x88 bytes, directions first), which
// the light grid's 0x4AACB0 hands out (those of the entity's +0x2B0).
constexpr uint32_t kNumLights = 12, kNumDirections = 6;
uint32_t g_lights = 0;
bool IsLight(uint32_t p) { return p >= g_lights && p < g_lights + kNumLights * 0x88 && (p - g_lights) % 0x88 == 0; }

// What each stub compares: the registers its callee reads (and for the
// texture coordinate callees the callee-saved ones, which ComputeTexCoords
// passes as they are) and its stack arguments.
struct StubInfo {
  uint32_t va;
  int regs, args;
};
constexpr StubInfo kStubs[] = {
    {0x4AD7E0, kEax | kEcx | kEdx, 0},          // debug box
    {0x4AD630, kEax | kEbx, 1},                 // debug polygon
    {0x4AD760, kEbx | kEsi | kEdi, 0},          // debug line
    {0x4CA8D0, 0, 2},                           // a portal surface off the screen
    {0x4AC890, 0, 0},                           // a debug draw
    {0x427CB0, kEax | kEcx, 0},                 // a 3x3 inverse
    {0x440940, 0, -1},                          // va (its format's arguments)
    {0x4AD8F0, kEax | kEdi, 1},                 // debug text
    {0x477290, kEdx | kEdi, 0},                 // a composite model's surfaces
    {0x477340, kEsi, 3},
    {0x476510, 0, 2},
    {0x5068B0, kEbx | kEdi, 0},
    {0x447CA0, kEax | kEcx, 1},                 // a warning icon
    {0x4771B0, kEdx, 1},                        // the bones' bounds
    {0x40F4E0, kEsi | kEdi, 4},                 // a debug line
    {0x4AA0F0, kEax | kEcx | kEdx, 0},          // the dlights into an orientation
    {0x4AA8D0, kEsi | kEdx, 0},                 // a light's intensity at a point
    {0x4AA5C0, 0, 2},                           // a debug draw of an entity's lights
    {0x4AA970, kEbx, 3},                        // the lights beyond the limit merged
    {0x4AACB0, kEsi, 4},                        // the light grid's lights at a point
    {0x51D45A, 0, 3},                           // fprintf to the log
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

// The arguments a format takes (a double for each %f).
int Conversions(uint32_t p) {
  int n = 0;
  for (;; p++) {
    const char ch = char(g_mem.read<uint8_t>(p));
    if (!ch) return n;
    if (ch != '%') continue;
    while (std::strchr("0123456789.-", char(g_mem.read<uint8_t>(p + 1)))) p++;
    const char conv = char(g_mem.read<uint8_t>(++p));
    if (conv != '%') n += conv == 'f' ? 2 : 1;
  }
}

void Stub(Ctx& c, uint32_t va, int regs, int args) {
  CallRec r{};
  r.va = va;
  r.esp = c.esp;
  r.ret = Rd(c.esp);
  const uint32_t all[7] = {c.eax, c.ecx, c.edx, c.ebx, c.esi, c.edi, c.ebp};
  for (int i = 0; i < 7; i++)
    if (regs & (1 << i)) r.regs[i] = all[i];
  if (args < 0) args = 1 + Conversions(Rd(c.esp + 4));
  for (int i = 0; i < args; i++) r.args[i] = Rd(c.esp + 4 + 4 * i);
  // What the debug draws and an added entity are handed (on the caller's
  // stack, which the cases do not compare): its bytes' hash.
  auto hash = [](uint32_t va, uint32_t size) {
    uint32_t h = 2166136261u;
    if (!size || !g_mem.IsCommitted(va & ~0xFFFu) || !g_mem.IsCommitted((va + size - 1) & ~0xFFFu)) return h;
    for (uint32_t i = 0; i < size; i++) h = (h ^ g_mem.read<uint8_t>(va + i)) * 16777619u;
    return h;
  };
  // What the merge of the lights beyond the limit reads on the caller's
  // stack: the order of the lights kept (+0xCC of them) and their values.
  if (va == 0x4AA970) {
    const int32_t n = std::clamp(int32_t(Rd(c.ebx + 0xCC)), 0, 8);
    uint32_t h = hash(r.args[2], 4 * uint32_t(n));
    for (int32_t i = 0; i < n; i++) h = h * 31 + Rd(r.args[1] + 4 * Rd(r.args[2] + 4 * uint32_t(i)));
    r.args[3] = h;
  }
  if (va == 0x40F4E0) r.args[4] = hash(r.args[0], 0xC) ^ hash(c.edi, 0xC) * 3 ^ hash(c.esi, 0x10) * 5;
  if (va == 0x4AD630 && c.ebx <= 0x1000) r.args[2] = hash(r.args[0], 0xC * c.ebx);
  if (va == 0x4AD760) r.args[2] = hash(c.ebx, 0xC) ^ hash(c.edi, 0xC) * 3;
  g_calls.push_back(r);
  const uint32_t k = uint32_t(g_calls.size());
  // What an entity's callees do: now and then change the current entity's
  // number, which R_AddEntitySurfaces reloads (the dlights' orientation,
  // under a brush model's); a light's intensity (ST0: ordinary, equal,
  // zero, negative and NaN values); the light grid's lights (from those of
  // the entity's +0x2B0, values now and then 0 or NaN); the bones' bounds.
  if (va == 0x4AA0F0 && k % 5 == 0) Wr(0x1536024, Rd(0x1536024) + 1);
  // The walk's dlight counts beyond its 32 dlights (for its bits) cut to
  // them for the brush models' dlights, which read them again.
  if (va == 0x4AA0F0 && int32_t(Rd(0x1536474)) > 40) Wr(0x1536474, Rd(0x1536474) & 31);
  if (va == 0x427CB0)
    for (uint32_t i = 0; i < 9; i++) WrF(c.ecx + 4 * i, float(int((k * 13 + i * 7) % 9) - 4) * 0.375f);
  if (va == 0x4AA8D0) {
    static const uint32_t kIntensity[] = {0x3F000000, 0x3F800000, 0x40000000, 0x3F800000, 0,
                                          0xBE800000, 0x40400000, 0x7FC00000, 0x7149F2CA, 0x3F400000,
                                          0x3DCCCCCD, 0x80000000, 0x3F800001};
    recomp::x87::LoadF32(c.fpu, kIntensity[(k * 7 + c.edx / 8) % 13]);
  }
  if (va == 0x4AACB0) {
    const uint32_t info = Rd(c.edi + 0x2B0);
    uint32_t m = g_mem.IsCommitted(info & ~0xFFFu) && g_mem.IsCommitted((info + 0x248) & ~0xFFFu) ? Rd(info + 0xB8) : 0;
    if (m > 12) m = 0;
    for (uint32_t i = 0; i < m; i++)
      if (!IsLight(Rd(info + 0x184 + 4 * i))) m = 0;
    const uint32_t n = m ? (k * 5 + m) % 14 : 0;
    static const uint32_t kValue[] = {0x3F800000, 0x3F000000, 0x40000000, 0, 0x7FC00000, 0xBF800000, 0x3E800000};
    WrF(r.args[2], k % 4 ? float(k % 3) * 0.5f : std::bit_cast<float>(kValue[k % 7]));
    for (uint32_t i = 0; i < n; i++) {
      Wr(r.args[1] + 4 * i, kValue[(k + i) % 7]);
      Wr(r.args[3] + 4 * i, Rd(info + 0x184 + 4 * ((k + 3 * i) % m)));
    }
    c.eax = n;
  }
  if (va == 0x4771B0)
    for (uint32_t i = 0; i < g_mem.read<uint8_t>(r.args[0] + 0x17); i++) Wr(c.edx + 4 * i, g_bone_bounds + 24 * ((k + i) % 8));
  // A composite model's surfaces: none without submodels, now and then
  // more than fit; the list's pairs (submodel, surface 0-3). Those that
  // fit when 0x1536498 is below 0, only while the draw surfaces stop
  // within 0x20 (they hold no more) and without the debug text of
  // distinct shaders (0x15343B0), whose count is quadratic in the pairs.
  if (va == 0x477290) {
    const uint32_t n = g_mem.read<uint8_t>(c.edx + 0x16);
    const bool room = int32_t(Rd(0x1536498)) >= 0 ||
                      (int32_t(Rd(0x1536490)) >= 0xFFF0 && !Rd(Rd(0x15343B0) + 0x20));
    g_pairs = !n ? 0 : k % 16 == 0 && room ? 0x2001 : k % 7;
    // As many as 0x10000 with room for them.
    if (n && k % 4 == 1 && int32_t(Rd(0x1536498)) < -0x20000 && room) g_pairs = 0x10003;
  }
  if (va == 0x477340) {
    // And the bones' bits (16 bytes at ESI).
    for (uint32_t i = 0; i < 16; i += 4) Wr(c.esi + i, (k + i) * 0x9E3779B9u);
    const uint32_t n = g_mem.read<uint8_t>(r.args[0] + 0x16);
    for (uint32_t i = 0; i < g_pairs && n; i++) {
      g_mem.write<uint16_t>(r.args[1] + 4 * i, uint16_t((k + i * 3) % n));
      g_mem.write<uint16_t>(r.args[1] + 4 * i + 2, uint16_t((k * 5 + i) % 4));
    }
  }
  const uint32_t junk = 0xC0DE0000u + k;
  // A portal surface now and then off the screen (EAX 1).
  c.eax = va == 0x4CA8D0   ? k % 4 == 0
          : va == 0x477290 ? g_pairs
          : va == 0x4AACB0 ? c.eax
                           : junk;
  c.ecx = junk ^ 1;
  // A light's intensity keeps EDX (its light), which its caller reads.
  if (va != 0x4AA8D0) c.edx = junk ^ 2;
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
void f_004D7F30_lifted(Ctx& c);
void f_004D6BC0_lifted(Ctx& c);
void f_004D80F0_lifted(Ctx& c);
void f_004D7A70_lifted(Ctx& c);
void f_004D96E0_lifted(Ctx& c);
void f_004D9A10_lifted(Ctx& c);
void f_004D9E20_lifted(Ctx& c);
void f_004CA240_lifted(Ctx& c);
void f_004CAB80_lifted(Ctx& c);
void f_004CB0C0_lifted(Ctx& c);
void f_004CB210_lifted(Ctx& c);
void f_004CB270_lifted(Ctx& c);
void f_004DEEC0_lifted(Ctx& c);
void f_00500830_lifted(Ctx& c);
void f_004FA790_lifted(Ctx& c);
void f_00506980_lifted(Ctx& c);
void f_004DE980_lifted(Ctx& c);
void f_00500920_lifted(Ctx& c);
void f_004DEC10_lifted(Ctx& c);
void f_004DF2D0_lifted(Ctx& c);
void f_00480DD0_lifted(Ctx& c);
void f_004DA9F0_lifted(Ctx& c);
void f_00506070_lifted(Ctx& c);
void f_00506310_lifted(Ctx& c);
void f_005065E0_lifted(Ctx& c);
void f_004AA490_lifted(Ctx& c);
void f_004AB490_lifted(Ctx& c);
void f_004ABD30_lifted(Ctx& c);
void f_004ABDC0_lifted(Ctx& c);
void f_004ABEF0_lifted(Ctx& c);
void f_004DA420_lifted(Ctx& c);
void f_004DA330_lifted(Ctx& c);
void f_004DA220_lifted(Ctx& c);
void f_004DA1B0_lifted(Ctx& c);
void f_004DA0F0_lifted(Ctx& c);
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
void f_004D7F30(Ctx& c) { f_004D7F30_lifted(c); }
void f_004D6BC0(Ctx& c) { f_004D6BC0_lifted(c); }
void f_004D80F0(Ctx& c) { f_004D80F0_lifted(c); }
void f_004D7A70(Ctx& c) { f_004D7A70_lifted(c); }
void f_004D96E0(Ctx& c) { f_004D96E0_lifted(c); }
void f_004D9A10(Ctx& c) { f_004D9A10_lifted(c); }
void f_004D9E20(Ctx& c) { f_004D9E20_lifted(c); }
void f_004CA240(Ctx& c) { f_004CA240_lifted(c); }
void f_004CAB80(Ctx& c) { f_004CAB80_lifted(c); }
void f_004CB0C0(Ctx& c) { f_004CB0C0_lifted(c); }
void f_004CB210(Ctx& c) { f_004CB210_lifted(c); }
void f_004CB270(Ctx& c) { f_004CB270_lifted(c); }
void f_004DEEC0(Ctx& c) { f_004DEEC0_lifted(c); }
void f_00500830(Ctx& c) { f_00500830_lifted(c); }
void f_004FA790(Ctx& c) { f_004FA790_lifted(c); }
void f_00506980(Ctx& c) { f_00506980_lifted(c); }
void f_004DE980(Ctx& c) { f_004DE980_lifted(c); }
void f_00500920(Ctx& c) { f_00500920_lifted(c); }
void f_004DEC10(Ctx& c) { f_004DEC10_lifted(c); }
void f_004DF2D0(Ctx& c) { f_004DF2D0_lifted(c); }
void f_00480DD0(Ctx& c) { f_00480DD0_lifted(c); }
void f_004DA9F0(Ctx& c) { f_004DA9F0_lifted(c); }
void f_00506070(Ctx& c) { f_00506070_lifted(c); }
void f_00506310(Ctx& c) { f_00506310_lifted(c); }
void f_005065E0(Ctx& c) { f_005065E0_lifted(c); }
void f_004AA490(Ctx& c) { f_004AA490_lifted(c); }
void f_004AB490(Ctx& c) { f_004AB490_lifted(c); }
void f_004ABD30(Ctx& c) { f_004ABD30_lifted(c); }
void f_004ABDC0(Ctx& c) { f_004ABDC0_lifted(c); }
void f_004ABEF0(Ctx& c) { f_004ABEF0_lifted(c); }
void f_004DA420(Ctx& c) { f_004DA420_lifted(c); }
void f_004DA330(Ctx& c) { f_004DA330_lifted(c); }
void f_004DA220(Ctx& c) { f_004DA220_lifted(c); }
void f_004DA1B0(Ctx& c) { f_004DA1B0_lifted(c); }
void f_004DA0F0(Ctx& c) { f_004DA0F0_lifted(c); }
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
// lifted renderer code reaches them.
void f_0041CD60_lifted(Ctx& c);
void f_0041CD60(Ctx& c) { f_0041CD60_lifted(c); }
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
void n_004D7F30(Ctx& c);
void n_004D6BC0(Ctx& c);
void n_004D80F0(Ctx& c);
void n_004D7A70(Ctx& c);
void n_004D96E0(Ctx& c);
void n_004D9A10(Ctx& c);
void n_004D9E20(Ctx& c);
void n_004CA240(Ctx& c);
void n_004CAB80(Ctx& c);
void n_004CB0C0(Ctx& c);
void n_004CB210(Ctx& c);
void n_004CB270(Ctx& c);
void n_004DEEC0(Ctx& c);
void n_00500830(Ctx& c);
void n_004FA790(Ctx& c);
void n_00506980(Ctx& c);
void n_004DE980(Ctx& c);
void n_00500920(Ctx& c);
void n_004DEC10(Ctx& c);
void n_004DF2D0(Ctx& c);
void n_00480DD0(Ctx& c);
void n_004DA9F0(Ctx& c);
void n_00506070(Ctx& c);
void n_00506310(Ctx& c);
void n_005065E0(Ctx& c);
void n_004AA490(Ctx& c);
void n_004AB490(Ctx& c);
void n_004ABD30(Ctx& c);
void n_004ABDC0(Ctx& c);
void n_004ABEF0(Ctx& c);
void n_004DA420(Ctx& c);
void n_004DA330(Ctx& c);
void n_004DA220(Ctx& c);
void n_004DA1B0(Ctx& c);
void n_004DA0F0(Ctx& c);
// The stubs.
#define COD1_STUB(va) \
  void f_##va(Ctx& c) { StubFor(c, 0x##va); }
COD1_STUB(004AD7E0)
COD1_STUB(004AD630)
COD1_STUB(004AD760)
COD1_STUB(004CA8D0)
COD1_STUB(004AC890)
COD1_STUB(00427CB0)
COD1_STUB(00440940)
COD1_STUB(004AD8F0)
COD1_STUB(00477290)
COD1_STUB(00477340)
COD1_STUB(00476510)
COD1_STUB(005068B0)
COD1_STUB(00447CA0)
COD1_STUB(004771B0)
COD1_STUB(0040F4E0)
COD1_STUB(004AA0F0)
COD1_STUB(004AA8D0)
COD1_STUB(004AA5C0)
COD1_STUB(004AA970)
COD1_STUB(004AACB0)
COD1_STUB(0051D45A)
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
  if (target == kAlloc || target == kFree) {
    Stub(c, target == kAlloc ? kAllocPtr : kFreePtr, 0, 1);
    if (target == kAlloc) {
      const uint32_t size = Rd(c.esp);
      if (size > kHeapSize - (g_heap_top - kHeap)) {
        std::printf("heap full (%08X bytes)\n", size);
        std::abort();
      }
      c.eax = g_heap_top;
      g_heap_top += (size + 15) & ~15u;
    }
    return;
  }
  if (target == kMilliseconds) {
    Stub(c, kMillisecondsPtr, 0, 0);
    c.eax = 1000 + 7 * uint32_t(g_calls.size());
    return;
  }
  if (target == kError || target == kPrintf) {
    // ri.Error and ri.Printf(code, fmt, ...): the arguments their formats
    // take (a double for each %f); the texmod error passes two more and
    // the registers ComputeTexCoords leaves.
    if (target == kError && Rd(c.esp + 8) == 0x53C06C) {
      Stub(c, kErrorPtr, kSaved | kEcx | kEdx, 4);
      return;
    }
    Stub(c, target == kError ? kErrorPtr : kPrintfPtr, 0, 2 + Conversions(Rd(c.esp + 8)));
    return;
  }
  if (target == kSubModel) {
    // A model handle 0-3 in AX.
    Stub(c, kSubModelPtr, 0, 1);
    c.eax = (c.eax & 0xFFFF0000u) | uint32_t(g_calls.size() % 4);
    return;
  }
  if (target == kModelCallback) {
    Stub(c, kModelCallbackPtr, 0, 3);
    return;
  }
  if (target == kGlBind || target == kGlSubData || target == kGlUpdate) {
    const int n = target == kGlBind ? 2 : target == kGlSubData ? 4 : 5;
    Stub(c, target == kGlBind ? kGlBindPtr : target == kGlSubData ? kGlSubDataPtr : kGlUpdatePtr, 0, n);
    c.esp += 4 * uint32_t(n);
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
    COD1_ENTRY(004D7F30, "polygon clip"),       COD1_ENTRY(004D6BC0, "portal planes"),
    COD1_ENTRY(004D80F0, "planes through a portal"), COD1_ENTRY(004D7A70, "cell's occluders"),
    COD1_ENTRY(004D96E0, "cell walk"),          COD1_ENTRY(004D9A10, "coronas"),
    COD1_ENTRY(004D9E20, "world walk"),
    COD1_ENTRY(004CA240, "portal orientations"), COD1_ENTRY(004CAB80, "portal view"),
    COD1_ENTRY(004CB0C0, "R_AddEntitySurfaces"), COD1_ENTRY(004CB210, "view's surfaces"),
    COD1_ENTRY(004CB270, "render view"),       COD1_ENTRY(004DEEC0, "RE_RenderScene"),
    COD1_ENTRY(00500830, "brush model entity"), COD1_ENTRY(004FA790, "model entity"),
    COD1_ENTRY(00506980, "composite model entity"), COD1_ENTRY(004DE980, "polygons"),
    COD1_ENTRY(00500920, "world without the walk"),
    COD1_ENTRY(004DEC10, "RE_AddRefEntityToScene"), COD1_ENTRY(004DF2D0, "level-of-detail distance"),
    COD1_ENTRY(00480DD0, "level of detail"),   COD1_ENTRY(004DA9F0, "surface cache entry"),
    COD1_ENTRY(00506070, "composite surface"), COD1_ENTRY(00506310, "bone boxes"),
    COD1_ENTRY(005065E0, "bone axes"),
    COD1_ENTRY(004AA490, "brush model dlights"), COD1_ENTRY(004AB490, "entity lights"),
    COD1_ENTRY(004ABD30, "grid lights"),         COD1_ENTRY(004ABDC0, "grid lighting"),
    COD1_ENTRY(004ABEF0, "entity lighting"),     COD1_ENTRY(004DA420, "vertex lighting"),
    COD1_ENTRY(004DA330, "cache block"),         COD1_ENTRY(004DA220, "cache split"),
    COD1_ENTRY(004DA1B0, "cache eviction"),      COD1_ENTRY(004DA0F0, "cache free"),
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

// The stack below the arguments that the entries keeping locals there for
// their callees use, refilled with one pattern before each side (a local
// one side leaves unwritten then differs).
uint32_t ScrubDepth(uint32_t fn) {
  switch (fn) {
    case 0x4D7F30: return 0x2100;
    case 0x4D6BC0: return 0x6200;
    case 0x4D80F0: return 0xC400;
    case 0x4D7A70: case 0x4D9A10: return 0x200;
    case 0x4D96E0: return 0x40000;
    case 0x4D9E20: return 0x80000;
    case 0x4CA240: return 0x400;
    case 0x4CAB80: return 0x4000;
    case 0x4CB210: case 0x4CB270: case 0x4DEEC0: return 0x90000;
    default: return 0;
  }
}

// Runs one call through the lifted original and the native code from the
// same memory and compares; the original's result stays (the next case's
// starting state).
void Run(const Call& k, std::vector<Window> w, std::mt19937& g, Stats& s, const char* what) {
  const Entry& e = kEntries[EntryIndex(k.fn)];
  const uint32_t esp = kStack + kStackSize - 0x1000;
  const uint32_t scrub = ScrubDepth(k.fn), pattern = g();
  auto fill = [&] {
    for (uint32_t va = esp - scrub; va < esp; va += 4) Wr(va, pattern);
  };
  // The models' cache and its counters, for whatever reaches the
  // entities' surfaces, and the heap of the cache entries' vertices.
  if (k.fn == 0x4CB0C0 || k.fn == 0x4CB210 || k.fn == 0x4CB270 || k.fn == 0x4DEEC0 ||
      k.fn == 0x4CAB80 || k.fn == 0x4CAFC0 || k.fn == 0x4FA790 || k.fn == 0x4DA9F0 ||
      k.fn == 0x4DA330 || k.fn == 0x4DA220 || k.fn == 0x4DA1B0 || k.fn == 0x4DA0F0) {
    w.push_back({kModelCache, 8 * 0x1C8});
    w.push_back({kModelCache + 0xE400, 0x38});
    w.push_back({kHeap, 0x10000});
    w.push_back({kVertexBuffer, kVertexBufferSize});
  }
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
  g_heap_top = kHeap;
  fill();
  e.lifted(cl);
  const std::vector<uint8_t> after = Save(w);
  const std::vector<CallRec> lcalls = g_calls;
  Load(w, before);
  Ctx cn = c0;
  g_calls.clear();
  g_heap_top = kHeap;
  fill();
  e.native(cn);
  const std::vector<uint8_t> got = Save(w);

  s.cases++;
  s.by_entry[EntryIndex(k.fn)]++;
  s.calls += int(lcalls.size());
  const bool eax = k.fn == 0x4D68E0 || k.fn == 0x4C8D50 || k.fn == 0x4C9070 || k.fn == 0x4D7DF0 ||
                   k.fn == 0x4D7E70 || k.fn == 0x4D7ED0 || k.fn == 0x4D7F30 || k.fn == 0x4D6BC0 ||
                   k.fn == 0x4D80F0 || k.fn == 0x4CA240 || k.fn == 0x4CAB80 || k.fn == 0x480DD0 ||
                   k.fn == 0x4DA9F0 || k.fn == 0x4DA330 || k.fn == 0x4DA220 || k.fn == 0x4DA1B0;
  // A result on the x87 stack: its register.
  const bool st0 = k.fn == 0x4D6880 || k.fn == 0x4DF2D0;
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
                   kNumOccluders = 0x11BEC10, kOccluders = 0x11BEC14, kViewCount = 0x1535A28,
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
  const uint32_t occluders = a.Alloc(4 * 4);
  for (uint32_t k = 0; k < n; k++) {
    const uint32_t p = a.Alloc(0x24);
    Wr(occluders + 4 * k, p);
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
  Wr(kNumOccluders, n);
  Wr(kOccluders, occluders);
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
  for (uint32_t k = 0; k < Rd(kNumOccluders); k++) {
    const uint32_t o = Rd(Rd(kOccluders) + 4 * k);
    for (uint32_t i = 0; i < Rd(o + 0x1C); i++) plane(Rd(o + 0x20) + 0x14 * i);
  }
}

void EntitySlots(Arena& a, std::mt19937& g, std::vector<Window>& win, bool place, uint32_t slots = 0);

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
      EntitySlots(a, g, win, true);
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
      EntitySlots(a, g, win, true);
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

// ---- the view through a portal -------------------------------------------------

// Finite axes and points only: the surface's plane, the entities' axes and
// their rotation reach VectorNormalize, whose CRT square root reports a NaN
// through the Windows API.
constexpr uint32_t kNumEntities = 0x153646C, kEntities = 0x1536470, kViewWorld = 0x15360AC;

// The scene's cvars: 0x15343D4 (the entities drawn) mostly set, the debug
// ones mostly clear, 0x14E43FC now and then 3 or -3 (0x500920's surfaces)
// or, with a world, another value (the world's surfaces without its
// cells), the back end's four values (0xA8F800..) about 0 or special and
// the view's (kViewCvars; with a world, the far clip's of its walk kept).
void SceneCvars(Arena& a, std::mt19937& g, uint32_t special, bool world) {
  auto cvar = [&](uint32_t va, uint32_t i, float f) {
    const uint32_t v = a.Alloc(0x24);
    Wr(va, v);
    Wr(v + 0x20, i);
    WrF(v + 0x1C, f);
  };
  cvar(0x15343D4, g() % 8 ? 1 : 0, 0);
  cvar(0x14E43FC, g() % 2 ? 0 : world ? uint32_t(Pick(g, {1, 3, -3, 2})) : uint32_t(Pick(g, {3, -3})), 0);
  cvar(0x15344D0, g() % 8 ? 0 : uint32_t(Pick(g, {1, -1})), 0);
  cvar(0x1534400, g() % 4 == 0, 0);
  for (uint32_t va : {0x15346FCu, 0x1534528u, 0x153448Cu, 0x1534678u})
    cvar(va, 0, g() % 4 ? Value(g, -2, 2, 256) : g() % 2 ? 0.0f : -0.0f);
  for (uint32_t va : kViewCvars)
    if (!world || va != 0x15343F0)
      cvar(va, g() % 3 ? 0 : uint32_t(Pick(g, {1, 4, 5})), g() % 4 ? Value(g, 0, 8192, special) : 0);
}

// The back end's data (the draw surface list section).
void BackEndData(std::mt19937& g, std::vector<Window>& win);

void EntityModels(Arena& a, std::mt19937& g, uint32_t ents, uint32_t n, bool dlights, bool handles);

// The scene's polygons (0x1536488 of 16 bytes at 0x153648C, the shader
// handle +4); with many, now and then more than 0x400.
void ScenePolygons(Arena& a, std::mt19937& g, bool many = false) {
  const uint32_t n = many && g() % 8 == 0 ? 0x3F0 + g() % 0x20 : g() % 4 ? g() % 5 : 0;
  const uint32_t polys = a.Alloc(16 * n + 4);
  for (uint32_t i = 0; i < 16 * n; i += 4) Wr(polys + i, g() % 2 ? g() % 4 : g());
  Wr(0x1536488, n);
  Wr(0x153648C, polys);
}

// A view rendered from a portal's: no world, the scene's polygons, cvars,
// draw surfaces and back end, and windows for what the render writes.
void PortalScene(Arena& a, std::mt19937& g, uint32_t special, std::vector<Window>& win) {
  Wr(0x1535A34, 0);
  ScenePolygons(a, g);
  Wr(kNumDrawSurfs, g() % 4);
  Wr(kNumDrawSurfs + 4, a.Alloc(0x100 * 8));
  SceneCvars(a, g, special, false);
  BackEndData(g, win);
  EntitySlots(a, g, win, false);
  win.push_back({0x1534000, 0x3000});
  win.push_back({0xA8F800, 0x40});
}

// The view's world orientation (finite axes), now and then a portal's view
// (0x1536134), the cvars 0x1534514 and 0x153457C (mostly 0), the flag
// 0x1534790, the time and n entities (0x2B8 bytes: types 0-3 or 0xB, finite
// axes, old frame (+0x60), frame (+0x50) and +0x80 for the rotation, the
// old origin now and then the origin).
void PortalGlobals(Arena& a, std::mt19937& g, uint32_t special, uint32_t n) {
  for (uint32_t k = 0; k < 0x7C; k += 4)
    WrF(kViewWorld + k, k < 0xC ? Value(g, -4096, 4096, special) : Value(g, -1, 1, 0));
  Wr(0x1536134, g() % 8 == 0);
  for (uint32_t cvar : {0x1534514u, 0x153457Cu}) {
    const uint32_t v = a.Alloc(0x24);
    Wr(cvar, v);
    Wr(v + 0x20, g() % 8 == 0 ? uint32_t(Pick(g, {1, -1})) : 0);
  }
  Wr(0x1534790, g() % 2);
  Wr(kTime, g() % 4 ? g() % 100000 : g());
  const uint32_t ents = a.Alloc(n * 0x2B8);
  Wr(kNumEntities, n);
  Wr(kEntities, ents);
  for (uint32_t i = 0; i < n; i++) {
    const uint32_t e = ents + 0x2B8 * i;
    // Model entities (types 1 and 2) only for finite views: their level of
    // detail takes the CRT's square root of the distance from the view.
    Wr(e, uint32_t(special ? Pick(g, {0, 3, 0xB, 0xB}) : Pick(g, {0, 1, 2, 3, 0xB, 0xB})));
    for (uint32_t k = 0; k < 0x24; k += 4) WrF(e + 0x1C + k, Value(g, -1, 1, 0));
    WrF(e + 0x40, g() % 2 ? 0.0f : 1.0f);
    for (uint32_t k = 0; k < 12; k += 4) WrF(e + 0x44 + k, Value(g, -4096, 4096, special));
    Wr(e + 0x50, g() % 2 ? 0 : g() % 2 ? uint32_t(Pick(g, {1, -1, 3, 100})) : g());
    for (uint32_t k = 0; k < 12; k += 4)
      Wr(e + 0x54 + k, g() % 2 ? Rd(e + 0x44 + k) : std::bit_cast<uint32_t>(Value(g, -4096, 4096, special)));
    Wr(e + 0x60, g() % 2 ? 0 : g() | 1);
    WrF(e + 0x80, g() % 3 == 0 ? 0.0f : Value(g, -360, 360, special));
  }
  EntityModels(a, g, ents, n, true, true);
}

// A portal surface: none, a polygon (type 2, its points 0x20 apart at +0xC),
// triangles (type 0x18 and up: points +0x40, indexes from the first +0x48)
// or another type; never degenerate (a zero normal's perpendicular
// divides by 0 before VectorNormalize). p0 is its first point (the default
// plane's (1, 0, 0) foot for none).
uint32_t PortalSurface(Arena& a, std::mt19937& g, float* p0) {
  float pts[3][3];
  for (auto& pt : pts)
    for (float& v : pt) v = g() % 4 ? Uniform(g, -4096, 4096) : std::round(Uniform(g, -4096, 4096));
  std::memcpy(p0, pts[0], 12);
  uint32_t surf = 0;
  switch (g() % 4) {
    case 0:
      p0[0] = 0;
      if (g() % 2) {
        surf = a.Alloc(0x50);
        Wr(surf, uint32_t(Pick(g, {0, 1, 3, 0x17, -1})));
      }
      break;
    case 1: {
      surf = a.Alloc(0x10);
      Wr(surf, 2);
      const uint32_t v = a.Alloc(0x60);
      Wr(surf + 0xC, v);
      for (uint32_t i = 0; i < 9; i++) WrF(v + 0x20 * (i / 3) + 4 * (i % 3), pts[i / 3][i % 3]);
      break;
    }
    default: {
      surf = a.Alloc(0x50);
      Wr(surf, 0x18 + g() % 4);
      const uint32_t v = a.Alloc(0x24);
      Wr(surf + 0x40, v);
      for (uint32_t i = 0; i < 9; i++) WrF(v + 4 * i, pts[i / 3][i % 3]);
      const uint16_t first = uint16_t(g() % 0xFFF0);  // the indexes do not wrap
      g_mem.write<uint16_t>(surf + 0x48, first);
      const bool swap = g() % 2;
      g_mem.write<uint16_t>(surf + 0x4A, uint16_t(first + (swap ? 2 : 1)));
      g_mem.write<uint16_t>(surf + 0x4C, uint16_t(first + (swap ? 1 : 2)));
      break;
    }
  }
  return surf;
}

// The portal entities mostly about p0 in entity ent's space (the world's
// for 0x7FE; an entity's origin for types 0-2, else the view's world
// origin): up to 40 off on each axis, now and then exactly 64 along x (the
// default plane's normal), or far.
void PortalEntities(std::mt19937& g, uint32_t ent, const float* p0) {
  const uint32_t ents = Rd(kEntities), n = Rd(kNumEntities);
  const uint32_t o = ent == 0x7FE ? 0 : Rd(ents + 0x2B8 * ent) <= 2 ? ents + 0x2B8 * ent + 0x44 : kViewWorld;
  for (uint32_t i = 0; i < n; i++) {
    const uint32_t e = ents + 0x2B8 * i;
    if (Rd(e) != 0xB || g() % 4 == 0) continue;
    const bool edge = g() % 4 == 0;
    for (uint32_t k = 0; k < 3; k++) {
      const float base = (o ? g_mem.read<float>(o + 4 * k) : 0.0f) + p0[k];
      const float off = edge ? (k ? 0.0f : g() % 2 ? 64.0f : -64.0f) : Uniform(g, -40, 40);
      WrF(e + 0x44 + 4 * k, base + off);
    }
    if (g() % 2)
      for (uint32_t k = 0; k < 12; k += 4) Wr(e + 0x54 + k, Rd(e + 0x44 + k));
  }
}

void PortalCase(Arena& a, std::mt19937& g, Stats& s, uint32_t special) {
  a.Reset();
  std::vector<Window> win = {{0x1536000, 0x500}, {0x1535D60, 8}};
  for (uint32_t va = 0x1536000; va < 0x1536500; va += 4) WrF(va, Value(g, -4096, 4096, special));
  const uint32_t n = 1 + g() % 6;
  PortalGlobals(a, g, special, n);
  float p0[3];
  const uint32_t ds = a.Alloc(8);
  Wr(ds, g());
  Wr(ds + 4, PortalSurface(a, g, p0));
  uint32_t ent = g() % 3 == 0 ? 0x7FE : g() % n;
  const uint32_t ents = Rd(kEntities), mode = g() % 3;
  float cn[3], cp[3];
  if (mode == 1) {
    // The default plane under an entity (types 0-2) whose first axis and
    // origin give a world distance that nearly cancels.
    ent = g() % n;
    const uint32_t e = ents + 0x2B8 * ent;
    Wr(e, special ? 0 : g() % 3);
    Cancel3(g, cn, cp);
    for (uint32_t k = 0; k < 3; k++) {
      WrF(e + 0x1C + 4 * k, cn[k]);
      WrF(e + 0x44 + 4 * k, cp[k]);
    }
    Wr(ds + 4, 0);
    p0[0] = 0;
  }
  PortalEntities(g, ent, p0);
  if (mode == 2) {
    // Triangles through the world's origin of normal (c, c, c) and portals
    // whose distances along it nearly cancel.
    ent = 0x7FE;
    const uint32_t surf = a.Alloc(0x50), v = a.Alloc(0x24);
    const float t = std::round(Uniform(g, 1, 4096));
    const float pts[9] = {t, -t, 0, 0, t, -t, -t, 0, t};
    Wr(surf, 0x18);
    Wr(surf + 0x40, v);
    for (uint32_t i = 0; i < 9; i++) WrF(v + 4 * i, pts[i]);
    g_mem.write<uint16_t>(surf + 0x48, 0);
    g_mem.write<uint16_t>(surf + 0x4A, 1);
    g_mem.write<uint16_t>(surf + 0x4C, 2);
    Wr(ds + 4, surf);
    for (uint32_t i = 0; i < n; i++) {
      Cancel3(g, cn, cp);
      for (uint32_t k = 0; k < 3; k++) WrF(ents + 0x2B8 * i + 0x44 + 4 * k, cn[k] * cp[k]);
      if (g() % 2) continue;
      // Or about 64 away (either side), two terms of about 2^35 that
      // cancel, so that the sum's order decides whether it is in range.
      const uint32_t a0 = g() % 3, a1 = (a0 + 1 + g() % 2) % 3, a2 = 3 - a0 - a1;
      const float X = std::ldexp(Uniform(g, 1, 2), 34 + int(g() % 3));
      float z = (g() % 2 ? 64.0f : -64.0f) / 0.57735026f;
      for (int m = int(g() % 17) - 8; m; m += m > 0 ? -1 : 1) z = std::nextafter(z, m > 0 ? 1e9f : -1e9f);
      WrF(ents + 0x2B8 * i + 0x44 + 4 * a0, X);
      WrF(ents + 0x2B8 * i + 0x44 + 4 * a1, -X);
      WrF(ents + 0x2B8 * i + 0x44 + 4 * a2, z);
    }
  }
  // Portals whose first axis and old origin nearly cancel (the view's
  // portal plane).
  for (uint32_t i = 0; mode && i < n; i++) {
    if (g() % 2) continue;
    Cancel3(g, cn, cp);
    for (uint32_t k = 0; k < 3; k++) {
      WrF(ents + 0x2B8 * i + 0x1C + 4 * k, cn[k]);
      WrF(ents + 0x2B8 * i + 0x54 + 4 * k, cp[k]);
    }
  }
  PortalScene(a, g, special, win);
  if (g() % 2) {
    const uint32_t surf = a.Alloc(0x30), cam = a.Alloc(0x30), pvs = a.Alloc(12), mirror = a.Alloc(4);
    win.push_back(a.All());
    Run({0x4CA240, uint32_t(g()), ent, uint32_t(g()), uint32_t(g()), uint32_t(g()), uint32_t(g()),
         {ds, surf, cam, pvs, mirror}},
        win, g, s, "random");
  } else {
    win.push_back(a.All());
    Run({0x4CAB80, uint32_t(g()), uint32_t(g()), uint32_t(g()), ds, uint32_t(g()), uint32_t(g()), {ent}},
        win, g, s, "random");
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

// RE_AddRefEntityToScene's state: the renderer mostly registered
// (0x1535A20), the warning icon's cvar 0x14E43EC, the back end's entity
// count 0x154D70C (small, about the limits 0x731 and 0x7FE, now and then
// negative) and the view count of the last warning (0x11CE848); with
// place the back end's data put so that the next 32 entities land in a
// buffer of the arena (slots, or a new one), else (after BackEndData) a
// window over them.
void EntitySlots(Arena& a, std::mt19937& g, std::vector<Window>& win, bool place, uint32_t slots) {
  Wr(0x1535A20, g() % 16 ? (g() % 4 ? 1 : 0x100) : 0);
  const uint32_t cv = a.Alloc(0x24);
  Wr(0x14E43EC, cv);
  Wr(cv + 0x20, g() % 2 ? 0 : uint32_t(Pick(g, {1, -1})));
  Wr(0x154D70C, g() % 4 ? g() % 8 : uint32_t(Pick(g, {0x72F, 0x730, 0x731, 0x7FC, 0x7FD, 0x7FE, 0x7FF, -1, INT32_MIN})));
  Wr(0x11CE848, g() % 2 ? Rd(0x1535A28) : g());
  const uint32_t n = Rd(0x154D70C);
  if (place) {
    if (!slots) slots = a.Alloc(0x2B8 * 32);
    Wr(0x1549698, slots - 0x81700 - 0x2B8 * n);
  } else {
    win.push_back({Rd(0x1549698) + 0x81700 + 0x2B8 * n, 0x2B8 * 32});
  }
  win.push_back({0x154D70C, 4});
  win.push_back({0x11CE848, 4});
  win.push_back({0x153646C, 4});
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
      // The portals' views: one surface, entities 0-3 (or the world's).
      PortalGlobals(a, g, special, 4);
      float p0[3];
      const uint32_t surface = PortalSurface(a, g, p0);
      PortalEntities(g, g() % 2 ? 0x7FE : g() % 4, p0);
      win.push_back({0x1535D60, 8});
      PortalScene(a, g, special, win);
      const uint32_t surfs = a.Alloc(len * 8 + 8);
      for (uint32_t i = 0; i < len; i++) {
        Wr(surfs + 8 * i, uint32_t((g() % 16) << 18 | (g() % 4) << 7 | (g() & 0x7F)));
        if (i && g() % 4 == 0) Wr(surfs + 8 * i, Rd(surfs + 8 * i - 8));
        Wr(surfs + 8 * i + 4, surface);
      }
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
  const uint32_t p1 = a.Alloc(0x1C), p2 = a.Alloc(8), model = a.Alloc(0x68);
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

void StaticCvars(Arena& a, std::mt19937& g) {
  for (uint32_t cvar : kStaticCvars) {
    const uint32_t v = a.Alloc(0x24);
    Wr(cvar, v);
    Wr(v + 0x20, g() % 4 ? g() % 2 : uint32_t(Pick(g, {2, -1})));
  }
}

void StaticCases(Arena& a, std::mt19937& g, const World& base, Stats& s) {
  // The links through the BSP only from finite values (see LinkBsp).
  const uint32_t kase = g() % 8;
  World w = base;
  if (kase == 3 || kase == 4) w.special = 0;
  StaticCvars(a, g);
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

// ---- the cell walk -----------------------------------------------------------------

constexpr uint32_t kCullOrigin = 0x11BEAF8, kViewProj = 0x11BEB90, kAperture = 0x153452C;
constexpr uint32_t kCullList = 0x11BEC10;  // occluders (count, list), planes (count, buffer)

float Dot3(const float* a, const float* b) { return float(double(a[0]) * b[0] + double(a[1]) * b[1] + double(a[2]) * b[2]); }
void RdVec(uint32_t va, float* v) {
  for (uint32_t i = 0; i < 3; i++) v[i] = g_mem.read<float>(va + 4 * i);
}
void WrVec(uint32_t va, const float* v) {
  for (uint32_t i = 0; i < 3; i++) WrF(va + 4 * i, v[i]);
}

// A unit normal (an axis now and then).
void Normal(std::mt19937& g, float* n) {
  if (g() % 4 == 0) {
    n[0] = n[1] = n[2] = 0;
    n[g() % 3] = g() % 2 ? 1.0f : -1.0f;
    return;
  }
  float l;
  do {
    for (int i = 0; i < 3; i++) n[i] = Uniform(g, -1, 1);
    l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
  } while (l < 0.1f || l > 1);
  for (int i = 0; i < 3; i++) n[i] /= l;
}

// n points (12 bytes each) at pts: a convex polygon on the plane (normal,
// dist) about the view origin's foot, of radius r, its points' distances
// rounded (now and then 0.001 off, either way); or anywhere.
void Polygon(std::mt19937& g, const World& w, uint32_t pts, uint32_t n, const float* nrm, float dist, float r) {
  float o[3], u[3], v[3];
  RdVec(kCullOrigin, o);
  if (!std::isfinite(o[0] + o[1] + o[2])) o[0] = o[1] = o[2] = 0;
  // u, v across the normal.
  const bool x = std::fabs(nrm[0]) < 0.9f;
  const float tv[3] = {x ? 1.0f : 0.0f, x ? 0.0f : 1.0f, 0};
  u[0] = nrm[1] * tv[2] - nrm[2] * tv[1];
  u[1] = nrm[2] * tv[0] - nrm[0] * tv[2];
  u[2] = nrm[0] * tv[1] - nrm[1] * tv[0];
  const float lu = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
  for (float& x : u) x /= lu;
  v[0] = nrm[1] * u[2] - nrm[2] * u[1];
  v[1] = nrm[2] * u[0] - nrm[0] * u[2];
  v[2] = nrm[0] * u[1] - nrm[1] * u[0];
  const float off = Dot3(o, nrm) - dist;
  float c[3];
  for (int i = 0; i < 3; i++) c[i] = o[i] - off * nrm[i] + Uniform(g, -r, r) * (i ? v[i] : u[i]);
  const bool any = g() % 4 == 0;
  const float a0 = Uniform(g, 0, 6.2831853f);
  for (uint32_t k = 0; k < n; k++) {
    float p[3];
    if (any) {
      for (float& x : p) x = Value(g, -w.size, w.size, w.special);
    } else {
      const float ang = a0 + 6.2831853f * (float(k) + Uniform(g, 0, 0.5f)) / float(n);
      for (int i = 0; i < 3; i++) p[i] = c[i] + r * (std::cos(ang) * u[i] + std::sin(ang) * v[i]);
      const float d = Dot3(p, nrm) - dist + (g() % 8 ? 0.0f : Pick(g, {-1, 1}) * 0.001f);
      for (int i = 0; i < 3; i++) p[i] -= d * nrm[i];
    }
    WrVec(pts + 0xC * k, p);
  }
}

// A view origin on a lattice: two axes about 2^22 (equal or 0.5 apart,
// either sign) and one short, so that it and points 0.5 and 1 off it are
// exact floats, and planes of normals (+-1, +-1, +-1) through it and
// such points have distances whose products cancel to 2^-30 of them.
void Lattice(std::mt19937& g, float* o) {
  const uint32_t j = g() % 3;
  const float A = std::round(std::ldexp(Uniform(g, 1, 2), 22) * 2) / 2;
  for (uint32_t k = 0; k < 3; k++)
    o[k] = k == j ? std::ldexp(1 + float(g() % 16) / 16, -8) * (g() % 2 ? 1 : -1)
                  : (k == (j + 1) % 3 || g() % 2 ? A : A + 0.5f) * (g() % 2 ? 1 : -1);
  WrVec(kCullOrigin, o);
}

void Aperture(Arena& a, std::mt19937& g, const World& w) {
  const uint32_t v = a.Alloc(0x24);
  Wr(kAperture, v);
  WrF(v + 0x1C, g() % 3 ? Uniform(g, 0, 1) : g() % 2 ? 0.0f : Value(g, -1, 1, w.special));
}

// The view origin (now and then huge, so that planes through it and
// points near 0 have distances that cancel) and random view-projection
// matrices.
bool CullView(std::mt19937& g, const World& w) {
  const bool huge = g() % 4 == 0;
  for (uint32_t i = 0; i < 3; i++)
    WrF(kCullOrigin + 4 * i, huge ? Wide(g, 25, 40) : Value(g, -w.size, w.size, w.special));
  for (uint32_t va = kViewProj; va < kViewProj + 0x80; va += 4) WrF(va, Value(g, -2, 2, w.special));
  return huge;
}

// An occluder (0x24 bytes): faces (+0, count +4) through points (+0x14,
// count +0x10) and edges (+0xC, count +8) between its faces (or another
// plane) with two of its points.
uint32_t Occluder(Arena& a, std::mt19937& g, const World& w, uint32_t nf) {
  const uint32_t o = a.Alloc(0x24), np = g() % 8 ? 1 + g() % 5 : 0, ne = g() % 6;
  const uint32_t faces = a.Alloc(nf * 0x14 + 4), pts = a.Alloc(np * 0xC + 4), edges = a.Alloc(ne * 0x10);
  for (uint32_t i = 0; i < nf; i++) {
    std::memcpy(g_mem.ptr<uint8_t>(faces + 0x14 * i), g_mem.ptr<uint8_t>(NewPlane(a, g, w)), 0x13);
    g_mem.write<uint8_t>(faces + 0x14 * i + 0x13, uint8_t(g() % 3));
  }
  float o3[3];
  RdVec(kCullOrigin, o3);
  for (uint32_t i = 0; i < np; i++)
    for (uint32_t k = 0; k < 3; k++)
      WrF(pts + 0xC * i + 4 * k, g() % 2 ? o3[k] + Uniform(g, -w.size, w.size) : Value(g, -w.size, w.size, w.special));
  for (uint32_t i = 0; i < ne; i++) {
    for (uint32_t k = 0; k < 2; k++) {
      uint32_t f = nf && g() % 8 ? faces + 0x14 * (g() % nf) : NewPlane(a, g, w);
      if (f != faces && (f < faces || f >= faces + 0x14 * nf)) g_mem.write<uint8_t>(f + 0x13, uint8_t(g() % 2));
      Wr(edges + 0x10 * i + 4 * k, f);
    }
    for (uint32_t k = 2; k < 4; k++) Wr(edges + 0x10 * i + 4 * k, np && g() % 8 ? pts + 0xC * (g() % np) : NewPlane(a, g, w));
  }
  Wr(o, faces);
  Wr(o + 4, nf);
  Wr(o + 8, ne);
  Wr(o + 0xC, edges);
  Wr(o + 0x10, np);
  Wr(o + 0x14, pts);
  for (uint32_t i = 0x18; i < 0x24; i += 4) Wr(o + i, g());
  return o;
}

// The cull's lists: room for n occluders and m planes.
void CullLists(Arena& a, std::mt19937& g, uint32_t n, uint32_t m) {
  Wr(kCullList, g() % 8);
  Wr(kCullList + 4, a.Alloc(4 * n));
  Wr(kCullList + 8, g() % 8);
  Wr(kCullList + 0xC, a.Alloc(0x14 * m));
}

// A portal (0x24 bytes): a plane mostly with the view origin behind it, the
// cell beyond (+0x14), a polygon on it (+0x18, count +0x1C) and the walk's
// flag (+0x20).
void Portal(Arena& a, std::mt19937& g, const World& w, uint32_t p, uint32_t beyond) {
  float n[3], o[3];
  Normal(g, n);
  RdVec(kCullOrigin, o);
  const float dist = g() % 8 ? Dot3(o, n) + Pick(g, {0, 1, 1, 2, 5}) * Uniform(g, 0, 1) * w.size / 8 +
                                   (g() % 4 ? 0.0f : Pick(g, {-1, 1}) * Uniform(g, 0.99f, 1.01f))
                             : Value(g, -w.size, w.size, w.special);
  WrVec(p, n);
  WrF(p + 0xC, dist);
  // On an axis, the view exactly 1 behind it now and then.
  const int axis = n[0] != 0 && n[1] == 0 && n[2] == 0 ? 0 : n[1] != 0 && n[0] == 0 && n[2] == 0 ? 1 : n[2] != 0 && n[0] == 0 && n[1] == 0 ? 2 : -1;
  if (axis >= 0 && g() % 2 && std::isfinite(o[axis])) {
    const float x = std::round(std::fmod(o[axis], 4096.0f));
    WrF(kCullOrigin + 4 * uint32_t(axis), x);
    WrF(p + 0xC, x * n[axis] + 1);
  }
  Wr(p + 0x10, g());
  Wr(p + 0x14, beyond);
  const uint32_t np = g() % 8 ? 3 + g() % 4 : g() % 3, pts = a.Alloc(np * 0xC + 4);
  Polygon(g, w, pts, np, n, dist, Uniform(g, 1, w.size / 2));
  Wr(p + 0x18, pts);
  Wr(p + 0x1C, np);
  Wr(p + 0x20, g() % 8 ? 0 : 1);
}

// Cells for the walk (0x40 bytes): surfaces (+0x18), portals (+0x1C,
// count +0x20; three in all at most), static models (+0x24, count +0x28),
// occluders (+0x2C, count +0x30), entities (+0x38) and references to the
// static models (+0x3C).
void WalkCells(Arena& a, std::mt19937& g, const World& w, uint32_t cells, uint32_t numCells, uint32_t models,
               uint32_t numModels) {
  uint32_t budget = 3;
  for (uint32_t i = 0; i < numCells; i++) {
    const uint32_t cell = NewCell(a, g, w, cells + 0x40 * i, models, numModels);
    const uint32_t surfs = a.Alloc(0x28);
    Wr(cell + 0x18, surfs);
    Refs(a, g, w, surfs + 0x18, g() % 4);
    if (g() % 2) {
      const uint32_t k = g() % 3, nodes = a.Alloc(k * 0x28);
      for (uint32_t j = 0; j < k; j++) NewNode(a, g, w, nodes + 0x28 * j, 2);
      Wr(surfs + 0x20, nodes);
      Wr(surfs + 0x24, k);
    }
    const uint32_t np = std::min(budget, uint32_t(g() % 3)), portals = a.Alloc(np * 0x24);
    budget -= np;
    for (uint32_t j = 0; j < np; j++) Portal(a, g, w, portals + 0x24 * j, cells + 0x40 * (g() % numCells));
    Wr(cell + 0x1C, portals);
    Wr(cell + 0x20, np);
    const uint32_t nm = g() % 3, ms = a.Alloc(4 * nm);
    for (uint32_t j = 0; j < nm; j++) Wr(ms + 4 * j, NewModel(a, g, w));
    Wr(cell + 0x24, ms);
    Wr(cell + 0x28, nm);
    const uint32_t no = g() % 3, os = a.Alloc(4 * no);
    for (uint32_t j = 0; j < no; j++) Wr(os + 4 * j, Occluder(a, g, w, g() % 4));
    Wr(cell + 0x2C, os);
    Wr(cell + 0x30, no);
    uint32_t link = 0;
    for (uint32_t j = g() % 3; j; j--) {
      const uint32_t l = a.Alloc(8);
      Wr(l, NewEntity(a, g, w));
      Wr(l + 4, link);
      link = l;
    }
    Wr(cell + 0x38, link);
  }
}

// The world's coronas (+0xC0, count +0xC4).
void Coronas(Arena& a, std::mt19937& g, const World& w, uint32_t world) {
  const uint32_t n = g() % 5, list = a.Alloc(n * 0x20);
  for (uint32_t i = 0; i < n; i++) {
    const uint32_t f = list + 0x20 * i, shader = a.Alloc(0x48);
    Wr(shader + 0x44, g());
    Wr(f, shader);
    for (uint32_t k = 0; k < 3; k++) WrF(f + 4 + 4 * k, Value(g, -w.size, w.size, w.special));
    // The view at a corona's origin now and then (a zero direction).
    if (g() % 8 == 0)
      for (uint32_t k = 0; k < 3; k++) Wr(0x1536330 + 4 * k, Rd(f + 4 + 4 * k));
    WrF(f + 0x10, Value(g, 0, w.size / 8, w.special));
    const float l0 = Value(g, -1, 1, w.special);
    WrF(f + 0x14, l0);
    WrF(f + 0x18, g() % 4 ? Uniform(g, -1, std::isfinite(l0) ? l0 : 1) : Value(g, -1, 1, w.special));
    Wr(f + 0x1C, g());
  }
  Wr(world + 0xC0, list);
  Wr(world + 0xC4, n);
}

void WalkCases(Arena& a, std::mt19937& g, const World& w, Stats& s) {
  const bool huge = CullView(g, w);
  Aperture(a, g, w);
  const uint32_t numPlanes = uint32_t(std::max(w.numPlanes, 0));
  std::vector<Window> win = {a.All(), {kPlaneBits, 4}, {kCullList, 0x10}, {kNumDrawSurfs, 8}};
  switch (g() % 6) {
    case 0: {
      // Up to the 0x400 points of a clip now and then.
      const uint32_t plane = NewPlane(a, g, w);
      const int32_t n = g() % 16 ? int32_t(g() % 9) : Pick(g, {-1, 0, 1000, 1020, 1024});
      const uint32_t len = n > 0 ? uint32_t(n) : 0;
      const uint32_t pts = a.Alloc(0xC * len + 4), out = a.Alloc(0xC * (2 * len + 2)), countp = a.Alloc(4);
      float nrm[3];
      RdVec(plane, nrm);
      const float dist = g_mem.read<float>(plane + 0xC);
      if (g() % 4 == 0 && std::isfinite(nrm[0] + nrm[1] + nrm[2])) {
        // A plane through 0 and points about 0.001 along its normal: their
        // distances (sums of products) within an ulp of +-0.001 as floats.
        WrF(plane + 0xC, 0);
        for (uint32_t i = 0; i < len; i++) {
          float c = Pick(g, {-1, 1}) * 0.001f;
          for (int u = int(g() % 7) - 3; u; u += u > 0 ? -1 : 1) c = std::nextafter(c, u > 0 ? 1.0f : -1.0f);
          for (uint32_t k = 0; k < 3; k++) WrF(pts + 0xC * i + 4 * k, c * nrm[k]);
        }
      } else if (g() % 4 == 0) {
        // An axial plane at 1..4 and points within ulps of 0.001 from it,
        // whose distances round to it as floats or not.
        const uint32_t ax = g() % 3;
        const float sign = g() % 2 ? 1.0f : -1.0f, d = Uniform(g, 1, 4);
        for (uint32_t k = 0; k < 4; k++) WrF(plane + 4 * k, k == ax ? sign : k == 3 ? d : 0.0f);
        for (uint32_t i = 0; i < len; i++) {
          float x = float((double(d) + Pick(g, {-1, 1}) * 0.001) * sign);
          for (int u = int(g() % 7) - 3; u; u += u > 0 ? -1 : 1) x = std::nextafter(x, u > 0 ? 8.0f : -8.0f);
          for (uint32_t k = 0; k < 3; k++) WrF(pts + 0xC * i + 4 * k, k == ax ? x : Uniform(g, -4, 4));
        }
      } else if (g() % 2 && std::isfinite(nrm[0] + nrm[1] + nrm[2] + dist)) {
        Polygon(g, w, pts, len, nrm, dist, Uniform(g, 0.001f, w.size));
        // Points pushed to either side, or left on it.
        for (uint32_t i = 0; i < len; i++) {
          const float d = Pick(g, {-1, 0, 1}) * (g() % 2 ? Uniform(g, 0, 0.002f) : Uniform(g, 0, w.size));
          for (uint32_t k = 0; k < 3; k++) WrF(pts + 0xC * i + 4 * k, g_mem.read<float>(pts + 0xC * i + 4 * k) + d * nrm[k]);
        }
      } else {
        for (uint32_t i = 0; i < 3 * len; i++) WrF(pts + 4 * i, Value(g, -w.size, w.size, w.special));
      }
      Wr(countp, uint32_t(n));
      win[0] = a.All();
      Run({0x4D7F30, 0, plane, 0, 0, 0, 0, {pts, countp, out}}, win, g, s, "random");
      break;
    }
    case 1: {
      const int32_t n = g() % 8 ? int32_t(g() % 8) : Pick(g, {0, 12});
      const uint32_t len = n > 0 ? uint32_t(n) : 0, pts = a.Alloc(0xC * len + 4), countp = a.Alloc(4);
      float nrm[3];
      Normal(g, nrm);
      Polygon(g, w, pts, len, nrm, Value(g, -w.size, w.size, w.special), Uniform(g, 1, w.size));
      if (huge)
        for (uint32_t i = 0; i < 3 * len; i++) WrF(pts + 4 * i, Uniform(g, -4, 4));
      if (len && g() % 3 == 0) {
        // A row of the projection that cancels on one point (in a group
        // of four or after them).
        float cn[3], cp[3];
        Cancel3(g, cn, cp);
        const uint32_t row = kViewProj + 0x10 * uint32_t(Pick(g, {0, 1, 3}));
        for (uint32_t k = 0; k < 3; k++) WrF(row + 4 * k, cn[k]);
        WrF(row + 0xC, 0);
        WrVec(pts + 0xC * (g() % len), cp);
      }
      if (len && g() % 4 == 0) {
        // Points 1 off a lattice origin in turn on two axes: every edge's
        // plane of a normal (+-1, +-1, +-1).
        float o[3];
        Lattice(g, o);
        for (uint32_t i = 0; i < len; i++) {
          const float v[2][3] = {{1, 1, 0}, {0, 1, 1}};
          for (uint32_t k = 0; k < 3; k++) WrF(pts + 0xC * i + 4 * k, o[k] + v[i % 2][k]);
        }
      } else if (!len && g() % 3 == 0) {
        // The first two corners 1 off a lattice origin on two axes (the
        // inverse's fourth row 0, 0, 1; its x column 0).
        float o[3];
        Lattice(g, o);
        const float c[3] = {0.5f, 1, 0.5f}, e[3] = {-0.5f, 0, 0.5f};
        for (uint32_t j = 0; j < 4; j++) {
          WrF(0x11BEBD0 + 4 * j, 0);
          WrF(0x11BEBE0 + 4 * j, j < 3 ? e[j] : 0.0f);
          WrF(0x11BEC00 + 4 * j, j < 3 ? o[j] + c[j] : 1.0f);
        }
      } else if (!len && g() % 2) {
        // Without points the rectangle is 1..-1 as it starts: an inverse
        // whose terms cancel at one corner, a huge one against a small.
        const uint32_t k0 = g() % 4;
        const float cx = k0 >= 2 ? -1.0f : 1.0f, cy = k0 == 1 || k0 == 2 ? 1.0f : -1.0f;
        for (uint32_t j = 0; j < 4; j++) {
          const float A = std::ldexp(Uniform(g, 1, 2), 30 + int(g() % 20)), B = Uniform(g, -2, 2);
          WrF(0x11BEBD0 + 4 * j, A);
          WrF(0x11BEBE0 + 4 * j, B);
          WrF(0x11BEC00 + 4 * j, -A * cx);
        }
      }
      win[0] = a.All();
      win.push_back({kHeap, 0x1000});
      Run({0x4D6BC0, 0, 0, 0, 0, 0, 0, {countp, uint32_t(n), pts, uint32_t(g())}}, win, g, s, "random");
      break;
    }
    case 2: {
      const uint32_t portal = a.Alloc(0x24), countp = a.Alloc(4);
      Portal(a, g, w, portal, g());
      const uint32_t from = g() % 2 ? Rd(kNearPlane) : NewPlane(a, g, w);
      win[0] = a.All();
      win.push_back({kHeap, 0x1000});
      Run({0x4D80F0, 0, countp, numPlanes, 0, 0, 0, {portal, from, w.planes}}, win, g, s, "random");
      break;
    }
    case 3: {
      // At the limits now and then: 0x401 occluders (one, again and again),
      // 0x1801 faces (one occluder's, all facing the view).
      const uint32_t cell = a.Alloc(0x40), kind = g() % 32;
      uint32_t n = g() % 4, faces = 0;
      const uint32_t list = a.Alloc(4 * 0x401);
      if (kind == 0) {
        const uint32_t o = Occluder(a, g, w, g() % 3);
        // A point in front of the near plane.
        const uint32_t near = Rd(kNearPlane), pts = Rd(o + 0x14);
        if (Rd(o + 0x10))
          for (uint32_t k = 0; k < 3; k++)
            WrF(pts + 4 * k, g_mem.read<float>(near + 4 * k) * (g_mem.read<float>(near + 0xC) + 10));
        n = g() % 2 ? 0x400 : 0x401;
        for (uint32_t i = 0; i < n; i++) Wr(list + 4 * i, o);
        faces = n * (Rd(o + 4) + Rd(o + 8));
      } else {
        for (uint32_t i = 0; i < n; i++) {
          const uint32_t o = Occluder(a, g, w, kind == 1 && i == 0 ? 0x1801 : g() % 5);
          if (kind == 1 && i == 0) {
            float o3[3];
            RdVec(kCullOrigin, o3);
            for (uint32_t j = 0; j < 0x1801; j++) {
              float nrm[3];
              Normal(g, nrm);
              const uint32_t f = Rd(o) + 0x14 * j;
              WrVec(f, nrm);
              WrF(f + 0xC, Dot3(o3, nrm) - 10);
            }
          }
          Wr(list + 4 * i, o);
          faces += Rd(o + 4) + Rd(o + 8);
        }
      }
      Wr(cell + 0x2C, list);
      Wr(cell + 0x30, n);
      CullLists(a, g, 0x410, faces + 0x10);
      uint32_t planes = w.planes;
      if (kind >= 2 && kind < 10) {
        // The near plane and the first plane through 0 with normals whose
        // products cancel on each occluder's first point.
        float cn[3], cp[3];
        Cancel3(g, cn, cp);
        planes = a.Alloc(7 * 0x14);
        std::memcpy(g_mem.ptr<uint8_t>(planes), g_mem.ptr<uint8_t>(w.planes), 7 * 0x14);
        for (uint32_t q : {Rd(kNearPlane), planes}) {
          WrVec(q, cn);
          WrF(q + 0xC, 0);
        }
        for (uint32_t i = 0; i < n; i++)
          if (Rd(Rd(list + 4 * i) + 0x10)) WrVec(Rd(Rd(list + 4 * i) + 0x14), cp);
      }
      win[0] = a.All();
      Run({0x4D7A70, 0, 0, 0, 0, 0, 0, {cell, planes, kind == 0 ? 0 : numPlanes}}, win, g, s, "random");
      break;
    }
    case 4: {
      StaticCvars(a, g);
      const uint32_t numCells = 1 + g() % 3, cells = a.Alloc(numCells * 0x40);
      WalkCells(a, g, w, cells, numCells, 0, 0);
      CullLists(a, g, 0x40, 0x100);
      EntitySlots(a, g, win, true);
      win[0] = a.All();
      win.push_back({kHeap, 0x10000});
      Run({0x4D96E0, 0, 0, 0, 0, 0, 0,
           {cells + 0x40 * uint32_t(g() % numCells), Rd(kNearPlane), w.planes, numPlanes, DlightBits(g)}},
          win, g, s, "random");
      break;
    }
    default: {
      // Finite directions (the CRT's square root of a NaN reports through
      // the Windows API, which the test does not have).
      World f = w;
      f.special = 0;
      const uint32_t world = a.Alloc(0x128);
      Wr(kWorld, world);
      for (uint32_t k = 0; k < 3; k++) WrF(0x1536330 + 4 * k, Value(g, -w.size, w.size, 0));
      Coronas(a, g, f, world);
      EntitySlots(a, g, win, true);
      win[0] = a.All();
      Run({0x4D9A10, 0, 0, 0, 0, 0, 0, {w.planes, uint32_t(w.numPlanes)}}, win, g, s, "random");
      break;
    }
  }
}

// The world walk over a world of cells, static models, coronas and world
// models, from a random view (in a cell, outside, without a world), with
// finite values (the links through the BSP).
// A world walk's view, world, cells, static models and their windows.
std::vector<Window> WorldWalkScene(Arena& a, std::mt19937& g, const World& base) {
  World w = base;
  w.special = 0;
  StaticCvars(a, g);
  CullView(g, w);
  Aperture(a, g, w);
  for (uint32_t cvar : {0x1534490u, 0x1534510u, 0x15346D8u, 0x153450Cu}) {
    const uint32_t v = a.Alloc(0x24);
    Wr(cvar, v);
    Wr(v + 0x20, cvar == 0x1534490 ? (g() % 16 ? 1 : 0) : g() % 4 == 0);
  }
  const uint32_t far = a.Alloc(0x24);
  Wr(0x15343F0, far);
  WrF(far + 0x1C, g() % 2 ? 0.0f : Uniform(g, 0, 2 * w.size));
  Wr(0x1535920, uint32_t(Pick(g, {0x800, 0x2601, 0})));
  WrF(0x1535920 + 0x24, Uniform(g, 0, 2 * w.size));
  Wr(0x1535950, g() % 2);
  Wr(0x13F9CF8, uint32_t(Pick(g, {0, 1})));
  Wr(0x1536364, g() % 16 ? 0 : 1);
  Wr(0x1536474, g() % 8 ? g() % 33 : g());
  Wr(0x564550, g() % 2);
  // The view: origin, axes, frustum planes, a portal's near plane, the
  // view and projection matrices.
  float o[3], n[3];
  for (float& x : o) x = Uniform(g, -w.size, w.size);
  Normal(g, n);
  WrVec(0x1536030, o);
  WrVec(0x153603C, n);
  for (uint32_t va = 0x1536048; va < 0x1536060; va += 4) WrF(va, Uniform(g, -1, 1));
  for (uint32_t i = 0; i < 4; i++)
    std::memcpy(g_mem.ptr<uint8_t>(0x15361F8 + 0x14 * i), g_mem.ptr<uint8_t>(NewPlane(a, g, w)), 0x14);
  Wr(0x1536138, g() % 4 ? 0 : 1);
  std::memcpy(g_mem.ptr<uint8_t>(0x1536144), g_mem.ptr<uint8_t>(NewPlane(a, g, w)), 0x14);
  for (uint32_t va = 0x15360E8; va < 0x1536128; va += 4) WrF(va, Uniform(g, -2, 2));
  for (uint32_t va = 0x1536178; va < 0x15361F8; va += 4) WrF(va, Uniform(g, -2, 2));
  // The static models and their XModels.
  for (uint32_t i = 0; i < 4; i++) {
    const uint32_t x = a.Alloc(0x50), b = a.Alloc(0x18);
    LocalBounds(g, w, b);
    Wr(x + 0x4C, b);
    Wr(kXModels + 4 * i, x);
  }
  // Room for the entities the walk adds after them (zeros, else).
  const uint32_t nm = g() % 5, models = a.Alloc(0x2B8 * (nm + 33));
  for (uint32_t i = 0; i < nm; i++) StaticModel(a, g, w, models + 0x2B8 * i);
  Wr(kNumStaticModels, nm);
  Wr(kStaticModels, models);
  // The view origin is the world's for the cells' portals.
  WrVec(kCullOrigin, o);
  const uint32_t numCells = g() % 8 ? 1 + g() % 3 : 0, cells = a.Alloc(numCells * 0x40 + 4);
  WalkCells(a, g, w, cells, numCells, models, nm);
  const uint32_t world = a.Alloc(0x150);
  Wr(kWorld, world);
  Wr(world + 0x120, numCells);
  Wr(world + 0x124, cells);
  const uint32_t box = a.Alloc(0x18);
  for (uint32_t k = 0; k < 3; k++) {
    WrF(box + 4 * k, o[k] - Uniform(g, 0, w.size));
    WrF(box + 0xC + 4 * k, o[k] + Uniform(g, 0, w.size));
  }
  Wr(world + 0x98, LinkBsp(a, g, w, numCells, box, 0));
  const uint32_t k = g() % 4, list = a.Alloc(4 * k);
  for (uint32_t i = 0; i < k; i++) Wr(list + 4 * i, NewRef(a, g, w));
  Wr(world + 0xA4, k);
  Wr(world + 0xA8, list);
  for (uint32_t i = 0; i < 3; i++) WrF(0x1536330 + 4 * i, o[i]);
  Coronas(a, g, w, world);
  const uint32_t nw = g() % 3, wm = a.Alloc(nw * 0x24);
  for (uint32_t i = 0; i < nw; i++) std::memcpy(g_mem.ptr<uint8_t>(wm + 0x24 * i), g_mem.ptr<uint8_t>(NewModel(a, g, w)), 0x24);
  Wr(world + 0x144, nw);
  Wr(world + 0x148, wm);
  EntityModels(a, g, models, nm, false, false);
  // The world's draw surfaces go on from its count (near the limit now and
  // then) into room of their own: the world's small buffer, which the
  // cases before may have filled, would take them over later allocations.
  Wr(kNumDrawSurfs + 4, a.Alloc(0x1000 * 8) - Rd(kNumDrawSurfs) * 8);
  std::vector<Window> win = {{0x11BEA00, 0x224}, {0x1535900, 0xB80}, {kNumDrawSurfs, 8}, {0x564550, 4}, {kHeap, 0x10000}};
  EntitySlots(a, g, win, true, models + 0x2B8 * nm);
  win.push_back(a.All());
  return win;
}

void WorldWalkCase(Arena& a, std::mt19937& g, const World& base, Stats& s) {
  Run({0x4D9E20, 0, 0, 0, 0, 0, 0, {}}, WorldWalkScene(a, g, base), g, s, "random");
}

// ---- the scene -------------------------------------------------------------------

// The scene's shaders: none a portal's (sort 1.0), now and then SS_BAD (0);
// all 0x1000 sorted ones, those of the entities' handles (0x153C528 of
// them at 0x153C52C, now and then none) and the default (0x1535AE8).
void SceneShaders(Arena& a, std::mt19937& g) {
  const uint32_t sh = a.Alloc(8 * 0x60);
  for (uint32_t i = 0; i < 8 * 0x60; i += 4) Wr(sh + i, g());
  for (uint32_t i = 0; i < 8; i++) WrF(sh + 0x60 * i + 0x58, float(Pick(g, {0, 2, 2, 3, 5})) / 4);
  for (uint32_t i = 0; i < 0x1000; i++) Wr(kSortedShaders + 4 * i, sh + 0x60 * (g() % 8));
  Wr(0x153C528, g() % 8 ? 4 : uint32_t(Pick(g, {0, -1})));
  for (uint32_t i = 0; i < 4; i++) Wr(0x153C52C + 4 * i, sh + 0x60 * (g() % 8));
  Wr(0x1535AE8, sh + 0x60 * (g() % 8));
}

// ---- the entities' surfaces -----------------------------------------------------

// A model's surface s's vertices for its cache entry (0x4DA9F0): counts
// (+0x12) mostly small, now and then about 32 and 0x200; texture
// coordinates (+0x14), unit normals (+0x18) and positions (+0x1C).
void ModelSurface(Arena& a, std::mt19937& g, uint32_t s) {
  const uint32_t n = g() % 8 ? g() % 9 : uint32_t(Pick(g, {0x1F, 0x20, 0x21, 0x40, 0x41, 0x1FF, 0x200, 0x201}));
  g_mem.write<uint16_t>(s + 0x12, uint16_t(n));
  const uint32_t st = a.Alloc(8 * n + 4), nv = a.Alloc(12 * n + 4), xyz = a.Alloc(12 * n + 4);
  for (uint32_t i = 0; i < 8 * n; i += 4) Wr(st + i, g());
  for (uint32_t i = 0; i < n; i++) {
    float v[3];
    Normal(g, v);
    WrVec(nv + 12 * i, v);
  }
  for (uint32_t i = 0; i < 12 * n; i += 4) WrF(xyz + i, g() % 64 ? Uniform(g, -64, 64) : Value(g, -64, 64, 1024));
  Wr(s + 0x14, st);
  Wr(s + 0x18, nv);
  Wr(s + 0x1C, xyz);
}

// A spot angle whose table position ((angle + 90) x 1024 / 360, stored)
// is a half of an even integer, where the rounding's 2^-30 decides the
// index (searched; else any angle).
float TieAngle(std::mt19937& g) {
  const float k = std::bit_cast<float>(0x40360B61u);
  for (int t = 0; t < 32; t++) {
    const float target = float(2 * (g() % 256)) + 0.5f, a = float(double(target) / k - 90.0);
    for (float c : {a, std::nextafter(a, -INFINITY), std::nextafter(a, INFINITY)})
      if (float(double(float(double(c) + 90.0)) * double(k)) == target) return c;
  }
  return Uniform(g, 0, 90);
}

// A light (0x88 bytes): a direction (+0x50 0, now and then -0) or a point
// (+0x50 not 0, now and then a NaN) with a finite origin, a spot now and
// then (+0x70 not 180: an angle, now and then special, and a spot exponent
// +0x6C of 0, below, small or NaN, never far above 0: the lighting raises
// to it by repeated products); types 0, 1, 2 and 8; intensities (+0x10)
// now and then negative or NaN; colours and attenuations with special
// values now and then.
void NewLight(std::mt19937& g, uint32_t l, bool point) {
  for (uint32_t k = 0; k < 0x88; k += 4) WrF(l + k, Value(g, -2, 2, 32));
  Wr(l, uint32_t(Pick(g, {0, 1, 2, 8, 8, 1})));
  WrF(l + 0x10, g() % 8 ? Uniform(g, 0, 2) : Value(g, -2, 2, 512));
  for (uint32_t k = 0; k < 0x20; k += 4) WrF(l + 0x14 + k, g() % 8 ? Value(g, -1, 2, 16) : 0.0f);
  float v[3];
  Normal(g, v);
  if (point)
    for (float& x : v) x = Uniform(g, -128, 128);
  else if (g() % 16 == 0)
    v[g() % 3] = Value(g, -1, 1, 1024);
  WrVec(l + 0x44, v);
  WrF(l + 0x50, point ? (g() % 8 ? 1.0f : Value(g, -2, 2, 1024)) : g() % 8 ? 0.0f : -0.0f);
  Normal(g, v);
  WrVec(l + 0x54, v);
  for (uint32_t k = 0x60; k < 0x6C; k += 4) WrF(l + k, g() % 8 ? Uniform(g, 0, 0.01f) : Value(g, 0, 1, 256));
  WrF(l + 0x6C, std::bit_cast<float>(uint32_t(Pick(g, {0, 0x3F800000, 0x40000000, 0x40600000, int32_t(0xBF800000),
                                                        0x3F000000, 0x40C00000, 0x7FC00000, int32_t(0xFF800000)}))));
  WrF(l + 0x70, g() % 2 ? 180.0f : g() % 8 ? (g() % 4 ? Uniform(g, 0, 90) : TieAngle(g)) : Value(g, -360, 360, 1024));
  if (g() % 32 == 0) WrF(l + 0x54 + 4 * (g() % 3), NAN);
  WrF(l + 0x74, Uniform(g, 0, 256));
}

// Whether a model surface's vertices (+0x12 of them at +0x1C) are free of
// infinities and NaNs: through finite axes they stay off NaNs, and so does
// a point light's direction from them (the CRT's square root of a NaN, under
// the normalisation 0x426710, reports through the Windows API).
bool FinitePositions(uint32_t s) {
  for (uint32_t i = 0; i < 3 * g_mem.read<uint16_t>(s + 0x12); i++)
    if (!std::isfinite(g_mem.read<float>(Rd(s + 0x1C) + 4 * i))) return false;
  return true;
}
bool FiniteAxes(uint32_t e) {
  for (uint32_t k = 0x1C; k < 0x50; k += 4)
    if (!std::isfinite(g_mem.read<float>(e + k))) return false;
  return true;
}
// An entity whose model's surfaces through its axes or its +0x2B0's stay
// finite: point lights may light it.
bool PointLit(uint32_t e) {
  if (!FiniteAxes(e) || !FiniteAxes(Rd(e + 0x2B0))) return false;
  for (uint32_t l = 0; l < 4; l++) {
    const uint32_t list = Rd(Rd(e + 0x98) + 0xC + 4 * l);
    for (uint32_t j = 0; j < Rd(list); j++)
      if (!FinitePositions(list + 4 + 0x30 * j)) return false;
  }
  return true;
}
uint32_t PickLight(std::mt19937& g, bool point) { return g_lights + 0x88 * (g() % (point ? kNumLights : kNumDirections)); }
// An entity's lights (+0xCC, +0xD0: light, value) directions only.
void Directional(std::mt19937& g, uint32_t e) {
  for (uint32_t i = 0; i < 8; i++)
    if (g_mem.read<float>(Rd(e + 0xD0 + 8 * i) + 0x50) != 0) Wr(e + 0xD0 + 8 * i, PickLight(g, false));
}

// The lighting of n entities at ents (EntityModels'): the case's lights;
// the world's lighting (a world of its own but a walk's): ambient +0xD8, sun colour +0xF8 and intensity +0x108 (now and
// then 0 or NaN), +0x98 its grid (a walk's its BSP), +0x118 lighting now and then
// off; the sun's direction 0x15364B0, the light limit 0x15364BC, the sun's
// mode 0x15364C0, the overbright shift 0x1536298, the refdef's dlight count
// (0x1536478; the dlights directions unless every entity is point lit; a
// walk's own 32 kept), the spot cosines 0x1544530 and the cvars. Per
// entity: lit (+0xA1) now and then, a lighting origin (+0xC), its lights
// (+0xCC, +0xD0, sun value +0x110), and the lights its +0x2B0 gives (count
// +0xB8 up to 12, the sun's value +0xBC, values +0xC0, lights +0x184; up
// to 6 in its own, for a static model, whose +0x2B0 is itself once added).
void Lighting(Arena& a, std::mt19937& g, uint32_t ents, uint32_t n, bool dlights) {
  g_lights = a.Alloc(kNumLights * 0x88);
  for (uint32_t i = 0; i < kNumLights; i++) NewLight(g, g_lights + 0x88 * i, i >= kNumDirections);
  if (dlights) Wr(kWorld, a.Alloc(0x150));
  const uint32_t w = Rd(kWorld);
  if (dlights) Wr(w + 0x98, g() % 4 ? 1 : 0);
  for (uint32_t k = 0; k < 0x10; k += 4) {
    WrF(w + 0xD8 + k, Value(g, 0, 1, 32));
    WrF(w + 0xF8 + k, Value(g, 0, 1, 32));
  }
  WrF(w + 0x108, g() % 4 ? Uniform(g, 0, 2) : float(Pick(g, {0, 0, -1, 2})));
  if (g() % 16 == 0) Wr(w + 0x108, 0x7FC00000);
  Wr(w + 0x114, g());
  Wr(w + 0x118, g() % 8 != 0);
  float v[3];
  Normal(g, v);
  WrVec(0x15364B0, v);
  Wr(0x15364BC, uint32_t(Pick(g, {0, 2, 4, 6, 8, 8, -1})));
  Wr(0x15364C0, uint32_t(Pick(g, {0, 1, 2, 2})));
  Wr(0x1536298, uint32_t(Pick(g, {0, 0, 1, 2, 31})));
  for (uint32_t i = 0; i < 0x400; i++)
    WrF(0x1544530 + 4 * i, g() % 16 ? Uniform(g, -1, 1) : std::bit_cast<float>(uint32_t(Pick(g, {int32_t(0xBF800000), 0, 0x3F800000, 0x7FC00000}))));
  auto cvar = [&](uint32_t va, uint32_t i, float f) {
    const uint32_t c = a.Alloc(0x24);
    Wr(va, c);
    Wr(c + 0x20, i);
    WrF(c + 0x1C, f);
  };
  cvar(0x15344CC, uint32_t(Pick(g, {0, 0, 0, 1, -1})), 0);
  cvar(0x1534578, 0, g() % 2 ? 0.0f : std::bit_cast<float>(uint32_t(Pick(g, {0x3E800000, int32_t(0xBF800000), 0x7FC00000, 0x40000000, 0x3F000000, 0x3F800000}))));
  cvar(0x15346C4, g() % 4 == 0, 0);
  bool all = true;
  for (uint32_t i = 0; i < n; i++) {
    const uint32_t e = ents + 0x2B8 * i, info = Rd(e + 0x2B0);
    const bool point = PointLit(e);
    all = all && point;
    g_mem.write<uint8_t>(e + 0xA1, g() % 4 == 0);
    for (uint32_t k = 0; k < 0xC; k += 4) WrF(e + 0xC + k, Value(g, -128, 128, 32));
    const uint32_t lit = g() % 8 ? g() % 9 : uint32_t(Pick(g, {-1, INT32_MIN}));
    Wr(e + 0xCC, lit);
    for (uint32_t j = 0; j < 8; j++) {
      Wr(e + 0xD0 + 8 * j, PickLight(g, point));
      WrF(e + 0xD4 + 8 * j, Value(g, 0, 2, 64));
    }
    WrF(e + 0x110, Value(g, 0, 1, 64));
    Wr(e + 0xB8, g() % 7);
    WrF(e + 0xBC, Value(g, 0, 1, 64));
    for (uint32_t j = 0; j < 6; j++) Wr(e + 0x184 + 4 * j, PickLight(g, point));
    Wr(info + 0xB8, g() % 13);
    WrF(info + 0xBC, g() % 4 ? Uniform(g, 0, 1) : Value(g, 0, 1, 1024));
    for (uint32_t j = 0; j < 12; j++) {
      WrF(info + 0xC0 + 4 * j, g() % 4 ? 1.0f : Value(g, 0, 2, 256));
      Wr(info + 0x184 + 4 * j, PickLight(g, point));
    }
  }
  if (dlights) {
    Wr(0x1536478, g() % 8 ? g() % 5 : uint32_t(Pick(g, {0, -1})));
    for (uint32_t i = 0; i < 4; i++) {
      const uint32_t dl = Rd(kRefdef) + 0x88 * i;
      for (uint32_t k = 0x60; k < 0x6C; k += 4) WrF(dl + k, Uniform(g, 0, 0.01f));
      WrF(dl + 0x6C, float(g() % 4));
      if (!all || g() % 2) WrF(dl + 0x50, 0);
    }
  } else {
    Wr(0x1536478, g() % 33);
  }
}

// The model cache (0x11BEC28) in a state the game reaches, over its first
// eight entries: each wholly free (on the list of level 0) or in use (on
// the list in use, in random order, the oldest now and then drawn this
// frame), split at random into free blocks (on their levels' lists, in
// random order) and blocks in use, each owned by one of the n entities'
// +0x2B0 (or a spare) at a slot 0-15, of a surface of random vertices. The
// entities' slots (+0x248) none or within an entry in use. The nodes of
// the entries' trees 0x4DA0F0 may be handed (entry, node, shift).
// node at the end of the list at head (next +0, previous +4).
void ListAppend(uint32_t head, uint32_t node) {
  Wr(node, head);
  Wr(node + 4, Rd(head + 4));
  Wr(head + 4, node);
  Wr(Rd(node + 4), node);
}
struct CacheNode {
  uint32_t entry, node, shift;
};
std::vector<CacheNode> g_cache_nodes;
std::vector<uint32_t> g_cache_used;
void ModelCache(Arena& a, std::mt19937& g, uint32_t ents, uint32_t n) {
  for (uint32_t l = 0; l < 6; l++) {
    const uint32_t head = kModelCache + 0xE400 + 8 * l;
    Wr(head, head);
    Wr(head + 4, head);
  }
  Wr(kModelCache + 0xE430, g());
  Wr(kModelCache + 0xE434, g());
  const uint32_t spare = a.Alloc(0x248 + 4 * 16), surf = a.Alloc(0x14);
  g_mem.write<uint16_t>(surf + 0x12, uint16_t(g()));
  std::vector<uint32_t> free[5];
  g_cache_nodes.clear();
  g_cache_used.clear();
  for (uint32_t k = 0; k < 8; k++) {
    const uint32_t entry = kModelCache + 0x1C8 * k;
    for (uint32_t i = 0; i < 0x1C8; i += 4) Wr(entry + i, g());
    for (uint32_t i = 0; i < 31; i++) {
      g_mem.write<uint16_t>(entry + 0xC + 4 * i, 0);
      g_mem.write<uint8_t>(entry + 0xE + 4 * i, 0);
    }
    if (g() % 4 == 0) {
      free[0].push_back(entry + 0x88);
      continue;
    }
    g_cache_used.push_back(entry);
    Wr(entry + 8, g() % 4 ? g() : Rd(0x1535A24));
    // Each node free, in use or split (the root never free).
    auto node = [&](auto& self, uint32_t i, uint32_t depth) -> void {
      g_cache_nodes.push_back({entry, i, 4 - depth});
      const uint32_t first = ((i + 1) << (4 - depth)) - 16, block = entry + 0x88 + 20 * first;
      const uint32_t r = g() % 8;
      if (depth && r < 3) {
        free[depth].push_back(block);
      } else if (depth == 4 || r < 5) {
        g_mem.write<uint8_t>(entry + 0xE + 4 * i, 1);
        for (int32_t j = int32_t(i); j >= 0; j = (j - 1) >> 1) {
          const uint32_t count = entry + 0xC + 4 * uint32_t(j);
          g_mem.write<uint16_t>(count, uint16_t(g_mem.read<uint16_t>(count) + (512u >> depth)));
        }
        const uint32_t owner = n && g() % 4 ? Rd(ents + 0x2B8 * (g() % n) + 0x2B0) : spare;
        Wr(block + 4, surf);
        Wr(block + 8, (k * 16 + first) * 32);
        Wr(block + 0xC, g() % 16);
        Wr(block + 0x10, owner);
      } else {
        self(self, 2 * i + 1, depth + 1);
        self(self, 2 * i + 2, depth + 1);
      }
    };
    node(node, 0, 0);
  }
  for (uint32_t l = 0; l < 5; l++) {
    std::shuffle(free[l].begin(), free[l].end(), g);
    for (uint32_t b : free[l]) ListAppend(kModelCache + 0xE400 + 8 * l, b);
  }
  std::shuffle(g_cache_used.begin(), g_cache_used.end(), g);
  for (uint32_t entry : g_cache_used) ListAppend(kModelCacheHead, entry);
  for (uint32_t i = 0; i < n; i++) {
    const uint32_t info = Rd(ents + 0x2B8 * i + 0x2B0);
    for (uint32_t q = 0; q < 16; q++)
      Wr(info + 0x248 + 4 * q, g() % 2 || g_cache_used.empty() ? 0 : g_cache_used[g() % g_cache_used.size()] + g() % 0x1C8);
  }
}

// What the entities' surfaces read, for n entities at ents (any types):
// the models 0-7 (0x15364F8; those of a walk keep their first 0x50 bytes
// and bounds): a brush model (+0x4C: bounds, surface references +0x18,
// count +0x1C) and shader handles by level (+0x50: levels -1 to 2, four
// 16-bit handles each, now and then out of range). Per entity: a model
// handle (+8) below 8; a finite origin (+0x44) and a level-of-detail scale
// (+0x40); a composite model (+0x90, 0x60 bytes: submodels +0x18, count
// +0x16, 16-bit model handles +0x38, now and then none or negative; a
// walk's first submodel kept; bones +0x17, their matrices +4 from the
// +0x50th): each submodel's +4 the default model now and then, else an
// XModel with levels of detail (as below) and by level + 1 (20 bytes each)
// a list of four surfaces (+4, +8: 0x34 bytes, +6 now and then 0xFFFF,
// fogs +0x28, +0x2C, +0x30 now and then); a model (+0x98: +8 a model whose
// XModel (+4) has up to three levels of detail (distances from +4, 0x14
// apart, now and then 0, equal or NaN; 16-bit count +0x64, now and then
// negative); by level 0-2 at +0xC a count and 0x30-byte surfaces: fog +4,
// shader +8, now and then a second shader +0xC, 16-bit triangle and
// vertex counts +0x10 and +0x12 (now and then about the limits of 32 and
// 0x200), texture coordinates +0x14, normals +0x18, positions +0x1C); its
// +0x2B0 (finite axes and origin from +0x1C, cache slots from +0x248:
// none or a byte of an entry in use); the callback +0x94 and +0xA0 now
// and then. The shaders' stages (+0x154: rgbGen +0x664, alphaGen +0x67C,
// colour +0x680) and +0x58 now and then 5.0; the model cache
// (ModelCache); the cvars of the entity types and the cache's
// (0x1534680), its vertex buffer and mode (0x1549558); the entities'
// lighting (Lighting); a finite view origin and the
// level-of-detail globals (0x1536170, 0x1536174, 0xA8F800-0xA8F848); the
// bones' bounds; when asked, dlights (count 0x1536474, 0x153647C) and the
// shaders of the handles (as SceneShaders'); 0x1536498 mostly in range,
// the entity surfaces (0x153649C) from it in a buffer.
void EntityModels(Arena& a, std::mt19937& g, uint32_t ents, uint32_t n, bool dlights, bool handles) {
  const uint32_t sh = a.Alloc(4 * 0x160), stages = a.Alloc(4 * 0x690);
  for (uint32_t i = 0; i < 4 * 0x160; i += 4) Wr(sh + i, g());
  for (uint32_t i = 0; i < 4 * 0x690; i += 4) Wr(stages + i, g());
  for (uint32_t i = 0; i < 4; i++) {
    Wr(sh + 0x160 * i + 0x154, stages + 0x690 * (g() % 4));
    if (g() % 4 == 0) WrF(sh + 0x160 * i + 0x58, 5.0f);
    if (g() % 2) Wr(sh + 0x160 * i + 0x50, (g() & 0xC00803FFu) | (g() % 2 ? 0x400 : 0));
    Wr(stages + 0x690 * i + 0x664, uint32_t(Pick(g, {1, 2, 10, 10, 11, 12, 3, 0, 13})));
    Wr(stages + 0x690 * i + 0x67C, uint32_t(Pick(g, {0, 0, 9, 9, 1})));
  }
  auto shader = [&] { return sh + 0x160 * (g() % 4); };
  auto finite = [&](uint32_t va, float lo, float hi) {
    const float f = g_mem.read<float>(va);
    if (!std::isfinite(f) || std::fabs(f) > 0x1p40f) WrF(va, Uniform(g, lo, hi));
  };
  // Levels of detail: up to three distances, increasing, now and then 0,
  // the one before or a NaN.
  auto lods = [&](uint32_t x, bool count) {
    float d = Uniform(g, -64, 512);
    for (uint32_t l = 0; l < 3; l++) {
      d += Uniform(g, 0, 1024);
      const uint32_t r = g() % 16;
      WrF(x + 4 + 0x14 * l, r == 0 ? 0.0f : r == 1 && l ? g_mem.read<float>(x + 4 + 0x14 * (l - 1)) : r == 2 ? NAN : d);
    }
    if (count) g_mem.write<uint16_t>(x + 0x64, uint16_t(g() % 8 ? g() % 4 : Pick(g, {-1, -0x8000})));
  };
  WrF(0xA8F800, g() % 4 ? 0.0f : g() % 2 ? Uniform(g, 0, 2048) : NAN);
  g_mem.write<uint8_t>(0xA8F804, g() % 4 == 0);
  lods(0xA8F804, false);
  finite(0x1536030, -4096, 4096);
  finite(0x1536034, -4096, 4096);
  finite(0x1536038, -4096, 4096);
  WrF(0x1536170, g() % 2 ? 0.0f : Uniform(g, -256, 256));
  WrF(0x1536174, g() % 2 ? 1.0f : Uniform(g, 0, 4));
  auto arena = [&](uint32_t va) { return va >= kArena && va < a.top; };
  auto bounds = [&](uint32_t va) {
    for (uint32_t k = 0; k < 0xC; k += 4) {
      const float lo = Uniform(g, -256, 256);
      WrF(va + k, lo);
      WrF(va + 0xC + k, lo + Uniform(g, 0, 128));
    }
  };
  for (uint32_t i = 0; i < 8; i++) {
    const uint32_t old = Rd(kXModels + 4 * i), m = a.Alloc(0x60), b = a.Alloc(0x20);
    if (arena(old)) std::memcpy(g_mem.ptr<uint8_t>(m), g_mem.ptr<uint8_t>(old), 0x50);
    if (arena(old) && arena(Rd(old + 0x4C)))
      std::memcpy(g_mem.ptr<uint8_t>(b), g_mem.ptr<uint8_t>(Rd(old + 0x4C)), 0x18);
    else
      bounds(b);
    const uint32_t nr = g() % 4, refs = a.Alloc(12 * nr + 4);
    for (uint32_t r = 0; r < nr; r++) {
      const uint32_t ref = refs + 12 * r, surf = a.Alloc(0x24);
      Wr(ref, g() % 4 ? g() % 16 : Rd(kViewCount));
      Wr(ref + 4, shader());
      Wr(ref + 8, surf);
      Wr(surf, uint32_t(Pick(g, {0, 0x17, 0x18, 0x19, -5})));
      Wr(surf + 4, g() % 4);
      bounds(surf + 0xC);
    }
    Wr(b + 0x18, refs);
    Wr(b + 0x1C, g() % 16 ? nr : uint32_t(Pick(g, {-1, INT32_MIN})));
    const uint32_t levels = a.Alloc(20) + 4;
    for (int32_t l = -1; l <= 2; l++) {
      const uint32_t h = a.Alloc(8);
      for (uint32_t k = 0; k < 8; k += 2)
        g_mem.write<uint16_t>(h + k, uint16_t(g() % 8 ? g() % 4 : Pick(g, {4, 5, 0xFFFF})));
      Wr(levels + 4 * uint32_t(l), h);
    }
    Wr(m + 0x4C, b);
    Wr(m + 0x50, levels);
    Wr(kXModels + 4 * i, m);
  }
  for (uint32_t i = 0; i < n; i++) {
    const uint32_t e = ents + 0x2B8 * i;
    if (Rd(e + 8) >= 8) Wr(e + 8, g() % 8);
    Wr(e + 0x94, g() % 4 ? 0 : g() | 1);
    g_mem.write<uint8_t>(e + 0xA0, g() % 4 == 0);
    for (uint32_t k = 0; k < 3; k++) finite(e + 0x44 + 4 * k, -4096, 4096);
    if (g() % 2) WrF(e + 0x40, g() % 4 ? Uniform(g, 0.25f, 4) : g() % 3 ? float(Pick(g, {0, 1})) : NAN);
    // The composite model and its bones.
    const uint32_t old = Rd(e + 0x90), p = a.Alloc(0x60), ns = g() % 8 ? 1 + g() % 4 : 0;
    const bool walk = arena(old) && arena(Rd(old + 0x18)) && arena(Rd(Rd(old + 0x18) + 4));
    if (walk) std::memcpy(g_mem.ptr<uint8_t>(p), g_mem.ptr<uint8_t>(old), 0x1C);
    g_mem.write<uint8_t>(p + 0x16, uint8_t(ns));
    const uint32_t nb = g() % 4 ? g() % 13 : g() % 40, base = g() % 3, mats = a.Alloc(0x40 * (base + nb) + 0x70);
    for (uint32_t k = 0; k < 0x40 * (base + nb) + 0x70; k += 4) WrF(mats + k, g() % 64 ? Uniform(g, -2, 2) : Value(g, -2, 2, 1024));
    g_mem.write<uint8_t>(p + 0x17, uint8_t(nb));
    g_mem.write<uint8_t>(p + 0x50, uint8_t(base));
    Wr(p + 4, mats);
    for (uint32_t j = 0; j < ns; j++) {
      const bool kept = j == 0 && walk;
      const uint32_t sub = kept ? Rd(p + 0x18) : a.Alloc(8), x = kept ? Rd(sub + 4) : a.Alloc(0x68);
      Wr(sub + 4, !kept && g() % 16 == 0 ? 0xA8F6BC : x);
      lods(x, true);
      for (uint32_t l = 0; l < 4; l++) {
        const uint32_t p20 = a.Alloc(8), p4 = a.Alloc(0xC), list = a.Alloc(16);
        for (uint32_t k = 0; k < 4; k++) {
          const uint32_t surf = a.Alloc(0x34);
          for (uint32_t q = 0; q < 0x34; q += 4) Wr(surf + q, g());
          if (g() % 4 == 0) g_mem.write<uint16_t>(surf + 6, 0xFFFF);
          for (uint32_t q = 0x28; q < 0x34; q += 4) {
            const uint32_t fog = a.Alloc(4);
            Wr(fog, g() % 4 ? g() % 8 : g());
            Wr(surf + q, g() % 3 ? 0 : fog);
          }
          Wr(list + 4 * k, surf);
        }
        Wr(p4 + 8, list);
        Wr(p20 + 4, p4);
        Wr(x + 20 * l, p20);
      }
      Wr(p + 0x18 + 4 * j, sub);
      g_mem.write<uint16_t>(p + 0x38 + 2 * j, uint16_t(g() % 8 ? 1 + g() % 7 : Pick(g, {0, -1, -5})));
    }
    Wr(e + 0x90, p);
    // The model and its cache.
    const uint32_t m = a.Alloc(0x1C), lm = a.Alloc(8), x = a.Alloc(0x68);
    Wr(m + 8, lm);
    Wr(lm + 4, x);
    lods(x, true);
    for (uint32_t l = 0; l < 4; l++) {
      const uint32_t k = g() % 4, list = a.Alloc(4 + 0x30 * k);
      Wr(list, k);
      for (uint32_t j = 0; j < k; j++) {
        const uint32_t s = list + 4 + 0x30 * j;
        for (uint32_t q = 0; q < 0x30; q += 4) Wr(s + q, g());
        Wr(s + 4, g() % 4);
        Wr(s + 8, shader());
        Wr(s + 0xC, g() % 3 ? shader() : 0);
        ModelSurface(a, g, s);
      }
      Wr(m + 0xC + 4 * l, list);
    }
    Wr(e + 0x98, m);
    const uint32_t cache = a.Alloc(0x248 + 4 * 16);
    for (uint32_t q = 0x1C; q < 0x50; q += 4) WrF(cache + q, q < 0x40 ? Uniform(g, -2, 2) : Uniform(g, -4096, 4096));
    Wr(e + 0x2B0, cache);
  }
  ModelCache(a, g, ents, n);
  auto cvar = [&](uint32_t va, uint32_t v) {
    const uint32_t c = a.Alloc(0x24);
    Wr(va, c);
    Wr(c + 0x20, v);
  };
  cvar(0x1534420, g() % 8 ? uint32_t(Pick(g, {1, 1, -1})) : 0);
  cvar(0x1534424, g() % 8 != 0);
  cvar(0x14E43E8, g() % 2);
  cvar(0x153446C, g() % 8 ? 0 : uint32_t(Pick(g, {1, 2, -1})));
  cvar(0x15343B0, g() % 8 ? 0 : uint32_t(Pick(g, {1, -1})));
  cvar(0x15343D8, g() % 4 ? g() % 3 : uint32_t(Pick(g, {3, -1, -2})));
  const uint32_t str = a.Alloc(4);
  g_mem.write<uint8_t>(str, g() % 8 ? 0 : 'x');
  cvar(0x15346D4, 0);
  Wr(Rd(0x15346D4) + 4, str);
  if (dlights) {
    const uint32_t dl = a.Alloc(0x74 + 4 * 0x88);
    for (uint32_t k = 0; k < 0x74 + 4 * 0x88; k += 4) WrF(dl + k, Uniform(g, -256, 256));
    Wr(0x1536474, g() % 5);
    Wr(kRefdef, dl);
  }
  if (handles) {
    Wr(0x153C528, g() % 8 ? 4 : uint32_t(Pick(g, {0, -1})));
    for (uint32_t i = 0; i < 4; i++) Wr(0x153C52C + 4 * i, shader());
    Wr(0x1535AE8, shader());
  }
  cvar(0x1534680, g() % 8 != 0);
  Wr(0x1549558, uint32_t(Pick(g, {12, 13, 14, 15, 12, 15, 11, 16})));
  Wr(0x154955C, kVertexBuffer);
  Wr(0x1549560, g());
  Wr(0x1536294, g());
  Wr(0x1530280, g() % 2 ? 0 : g());
  Wr(0x1535AEC, shader());
  g_bone_bounds = a.Alloc(8 * 24);
  for (uint32_t k = 0; k < 8 * 24; k += 4) WrF(g_bone_bounds + k, g() % 64 ? Uniform(g, -16, 16) : Value(g, -16, 16, 1024));
  // Mostly in range, now and then just below the limit of 0x2000 or far
  // below 0.
  Wr(0x1536498, g() % 4 ? g() % 0x2001 : uint32_t(Pick(g, {0x1FFA, 0x1FFD, int32_t(g() | 0x80000000u)})));
  const uint32_t count = Rd(0x1536498), es = a.Alloc(16 * (int32_t(count) < 0 ? 0x30010 : 0x400));
  Wr(0x153649C, es - 16 * count);
  Lighting(a, g, ents, n, dlights);
}

// A viewport (now and then empty) and fovs (with a world finite and in
// 1..179, so that the frustum the walk culls by is).
void Viewport(std::mt19937& g, uint32_t xywh, uint32_t fovs, bool world) {
  for (uint32_t k : {0u, 4u}) Wr(xywh + k, g() % 1000);
  for (uint32_t k : {8u, 0xCu}) Wr(xywh + k, g() % 8 ? 1 + g() % 1920 : uint32_t(Pick(g, {0, -1, INT32_MIN})));
  for (uint32_t k : {0u, 4u})
    WrF(fovs + k, world || g() % 8 ? Uniform(g, 1, 179) : Pick(g, {0, 180, -90, 360}));
  // Equal fovs of different bits (+0 and -0), or a NaN.
  if (!world && g() % 8 == 0) {
    const uint32_t k = 4 * (g() % 2);
    Wr(fovs + k, g() % 2 ? 0x7FC00000 : 0);
    Wr(fovs + 4 - k, g() % 2 ? 0x80000000 : std::bit_cast<uint32_t>(Uniform(g, 1, 179)));
  }
}

// An entity's (e, from EntityModels) model surfaces' entries alone, r 4 to
// 10: its level-of-detail distance; the level of detail of its model's or
// a submodel's XModel at a distance (now and then one of the levels' or
// the table's exactly, 0, -0 or a NaN); a surface's cache entry (a
// surface of its model, a finite inverse); a composite model's surface
// (one of its submodels', the shader of one of the model's); the debug
// boxes and axes of its bones (random bits); RE_AddRefEntityToScene of it.
void ModelSurfaceCase(Arena& a, std::mt19937& g, Stats& s, uint32_t e, uint32_t r, std::vector<Window> win) {
  const uint32_t p = Rd(e + 0x90), ns = g_mem.read<uint8_t>(p + 0x16), m = Rd(e + 0x98);
  // A surface of the model's (or a new one).
  uint32_t surf = 0;
  for (uint32_t t = 0; t < 8 && !surf; t++) {
    const uint32_t list = Rd(m + 0xC + 4 * (g() % 4));
    if (Rd(list)) surf = list + 4 + 0x30 * (g() % Rd(list));
  }
  if (!surf) {
    surf = a.Alloc(0x30);
    for (uint32_t q = 0; q < 0x30; q += 4) Wr(surf + q, g());
    ModelSurface(a, g, surf);
    Wr(surf + 8, Rd(0x1535AEC));
    Wr(surf + 0xC, Rd(0x1535AEC));
  }
  if (!Rd(surf + 0xC)) Wr(surf + 0xC, Rd(surf + 8));
  const uint32_t sub = ns ? Rd(p + 0x18 + 4 * (g() % ns)) : 0;
  const uint32_t rnd[6] = {uint32_t(g()), uint32_t(g()), uint32_t(g()), uint32_t(g()), uint32_t(g()), uint32_t(g())};
  switch (r) {
    case 4:
      win.push_back(a.All());
      Run({0x4DF2D0, rnd[0], rnd[1], rnd[2], rnd[3], e, rnd[5], {}}, win, g, s, "random");
      break;
    case 5: {
      const uint32_t model = sub && g() % 2 ? sub : Rd(m + 8);
      const uint32_t x = Rd(model + 4), l = g() % 3;
      const uint32_t lod = g_mem.read<uint8_t>(0xA8F804) ? 0xA8F808 + 0x14 * l : x + 4 + 0x14 * l;
      const uint32_t k = g() % 8;
      const uint32_t dist = k < 3 ? Rd(lod) : k == 3 ? uint32_t(Pick(g, {0, int32_t(0x80000000), 0x7FC00000, -0x400000}))
                                                     : std::bit_cast<uint32_t>(Uniform(g, -128, 4096));
      win.push_back(a.All());
      Run({0x480DD0, rnd[0], model, rnd[2], rnd[3], rnd[4], rnd[5], {dist}}, win, g, s, "random");
      break;
    }
    case 6: {
      const uint32_t inv = a.Alloc(36), ent = g() % 4 ? Rd(e + 0x2B0) : e;
      for (uint32_t k = 0; k < 36; k += 4) WrF(inv + k, g() % 8 ? Uniform(g, -2, 2) : float(Pick(g, {0, 1, -1})));
      // Sums that nearly cancel (where their order shows): the first
      // vertex's position against the entity's first axis column, the
      // first two normals against the inverse's first and last rows.
      if (g_mem.read<uint16_t>(surf + 0x12) >= 2 && g() % 2) {
        float cn[3], cp[3];
        Cancel3(g, cn, cp);
        for (uint32_t k = 0; k < 3; k++) WrF(ent + 0x1C + 0xC * k, cn[k]);
        WrVec(Rd(surf + 0x1C), cp);
        for (uint32_t row : {0u, 0x18u}) {
          Cancel3(g, cn, cp);
          WrVec(inv + row, cn);
          WrVec(Rd(surf + 0x18) + (row ? 12 : 0), cp);
        }
      }
      if (!FinitePositions(surf) || !FiniteAxes(ent)) Directional(g, e);
      win.push_back(a.All());
      Run({0x4DA9F0, ent, surf, rnd[2], rnd[3], rnd[4], rnd[5], {uint32_t(g() % 64), e, inv}}, win, g, s,
          "random");
      break;
    }
    case 7: {
      const uint32_t x = sub ? Rd(sub + 4) : 0;
      if (!x || x == 0xA8F6BC) return;
      const uint32_t list = Rd(Rd(Rd(x + 20 * (g() % 4)) + 4) + 8), sh = g() % 2 ? Rd(surf + 8) : Rd(surf + 0xC);
      win.push_back(a.All());
      Run({0x506070, rnd[0], rnd[1], Rd(list + 4 * (g() % 4)), rnd[3], rnd[4], sh, {p, uint32_t(g() % 4)}}, win, g, s, "random");
      break;
    }
    case 8:
    case 9: {
      // Sums that nearly cancel: the world's first coordinate (the bones'
      // matrices 0 with a translation against the entity's first axis
      // column), or the bones' first (all bounds' corners one point
      // against each matrix's first column).
      const uint32_t mats = Rd(p + 4), nb = g_mem.read<uint8_t>(p + 0x17), base = g_mem.read<uint8_t>(p + 0x50);
      float cn[3], cp[3];
      Cancel3(g, cn, cp);
      if (const uint32_t mode = g() % 3; mode == 1) {
        for (uint32_t k = 0; k < 3; k++) WrF(e + 0x1C + 0xC * k, cn[k]);
        for (uint32_t i = 0; i < nb; i++) {
          const uint32_t m = mats + 0x40 * (base + i) + 0x40;
          for (uint32_t k = 0; k < 0x30; k += 4) WrF(m - 0x10 + k, 0);
          WrVec(m + 0x20, cp);
        }
      } else if (mode == 2) {
        for (uint32_t k = 0; k < 8 * 24; k += 12) WrVec(g_bone_bounds + k, cp);
        for (uint32_t i = 0; i < nb; i++)
          for (uint32_t k = 0; k < 3; k++) WrF(mats + 0x40 * (base + i) + 0x30 + 0x10 * k, cn[k]);
      }
      const uint32_t bits = a.Alloc(16);
      for (uint32_t k = 0; k < 16; k += 4) Wr(bits + k, g() % 4 ? g() : 0xFFFFFFFF);
      win.push_back(a.All());
      Run({r == 8 ? 0x506310u : 0x5065E0u, rnd[0], rnd[1], rnd[2], rnd[3], rnd[4], rnd[5], {e, bits}}, win, g, s, "random");
      break;
    }
    default:
      EntitySlots(a, g, win, true);
      win.push_back(a.All());
      Run({0x4DEC10, rnd[0], rnd[1], rnd[2], rnd[3], rnd[4], rnd[5], {e, uint32_t(g())}}, win, g, s, "random");
      break;
  }
}

// Entities of every type (0x2B8 bytes: type, flags +4, shader handle
// +0x68, models) for R_AddEntitySurfaces, now and then inside a portal's
// view; or one entity of type 0, 1 or 2 for its surfaces alone, or its
// model surfaces' entries.
void EntityCase(Arena& a, std::mt19937& g, Stats& s) {
  a.Reset();
  for (uint32_t va = 0x1536000; va < 0x1536500; va += 4) Wr(va, g());
  SceneShaders(a, g);
  const uint32_t cvar = a.Alloc(0x24);
  Wr(0x15343D4, cvar);
  Wr(cvar + 0x20, g() % 8 ? uint32_t(Pick(g, {1, -1, 2})) : 0);
  Wr(0x1536134, g() % 4 == 0);
  const uint32_t n = g() % 16 ? g() % 8 : uint32_t(Pick(g, {0, -1, INT32_MIN})), m = n < 8 ? n : 0;
  const uint32_t ents = a.Alloc(m * 0x2B8 + 4);
  for (uint32_t i = 0; i < m * 0x2B8; i += 4) Wr(ents + i, g());
  for (uint32_t i = 0; i < m; i++) {
    const uint32_t e = ents + 0x2B8 * i;
    Wr(e, uint32_t(Pick(g, {0, 1, 2, 3, 4, 5, 10, 11, 11, 12, 15, 16, -1})));
    Wr(e + 4, g() % 4 ? g() & 7 : g());
    Wr(e + 0x68, g() % 4 ? g() % 4 : uint32_t(Pick(g, {-1, 4, 5, INT32_MAX})));
    Wr(e + 0x2B4, g() % 8 ? 0 : uint32_t(Pick(g, {1, 2})));
  }
  Wr(kNumEntities, n);
  Wr(kEntities, ents);
  Wr(0x1535A24, g());
  EntityModels(a, g, ents, m, true, false);
  const uint32_t surfs = a.Alloc(0x100 * 8);
  Wr(kNumDrawSurfs, g() % 8 ? g() % 4 : uint32_t(Pick(g, {0xFFFF, 0x10000, -1})));
  // With 0x1536498 below 0, now and then room for few draw surfaces (the
  // composite models' pairs fit then, as many as 0x10003).
  if (int32_t(Rd(0x1536498)) < 0 && g() % 2) Wr(kNumDrawSurfs, 0xFFF0 + g() % 0x20);
  Wr(kNumDrawSurfs + 4, surfs + 0x400 - Rd(kNumDrawSurfs) * 8);
  Wr(0x1549548, g());
  Wr(0x1549554, g());
  std::vector<Window> win = {{0x1536000, 0x500}, {0x1535D60, 8}, a.All()};
  const uint32_t r = g() % 11;
  if (r == 0 || !m) {
    Run({0x4CB0C0, uint32_t(g()), uint32_t(g()), uint32_t(g()), uint32_t(g()), uint32_t(g()), uint32_t(g()), {}}, win, g, s, "random");
    return;
  }
  const uint32_t e = ents + 0x2B8 * (g() % m);
  if (r >= 4) return ModelSurfaceCase(a, g, s, e, r, win);
  // One entity, as R_AddEntitySurfaces hands it over.
  Wr(e, r - 1);
  Wr(0x1535D68, e);
  Wr(e + 0x9C, 0);
  const uint32_t fn = r == 1 ? 0x500830 : r == 2 ? 0x506980 : 0x4FA790;
  Run({fn, r == 2 ? uint32_t(g()) : e, uint32_t(g()), uint32_t(g()), uint32_t(g()), uint32_t(g()), uint32_t(g()), r == 2 ? std::vector<uint32_t>{e} : std::vector<uint32_t>{}}, win, g, s, "random");
}

// A scene over a world walk's (or without a world): the view's surfaces
// (0x4CB210), the view rendered from parms (the view's, with a viewport,
// fovs, now and then a portal's) or RE_RenderScene from a refdef with the
// view's origin and axes (the walk's static models and dlights moved into
// the back end's data, as the scene's after its first ones); or the
// polygons' surfaces or the world's without the walk alone.
void SceneCase(Arena& a, std::mt19937& g, const World& base, Stats& s) {
  std::vector<Window> win = WorldWalkScene(a, g, base);
  const bool world = g() % 4 != 0;
  if (!world) Wr(kWorld, 0);
  // A world without cells now and then.
  if (world && g() % 8 == 0) Wr(Rd(kWorld) + 0x124, 0);
  // The world's surface references (+0xA0, count +0x9C) for 0x500920.
  if (world) {
    const uint32_t k = g() % 5, refs = a.Alloc(12 * k + 4);
    for (uint32_t i = 0; i < k; i++)
      std::memcpy(g_mem.ptr<uint8_t>(refs + 12 * i), g_mem.ptr<uint8_t>(NewRef(a, g, base)), 12);
    Wr(Rd(kWorld) + 0x9C, k);
    Wr(Rd(kWorld) + 0xA0, refs);
  }
  ScenePolygons(a, g, true);
  SceneCvars(a, g, 0, world);
  SceneShaders(a, g);
  Wr(0x1536134, g() % 8 == 0);
  BackEndData(g, win);
  EntitySlots(a, g, win, false);
  Wr(kNumDrawSurfs, g() % 4);
  Wr(kNumDrawSurfs + 4, a.Alloc(0x1000 * 8));
  win.push_back({0x1534000, 0x3000});
  win.push_back({0xA8F800, 0x40});
  switch (g() % 5) {
    case 0:
      win.push_back(a.All());
      Run({0x4CB210, uint32_t(g()), uint32_t(g()), uint32_t(g()), uint32_t(g()), uint32_t(g()), uint32_t(g()), {}}, win, g, s, "random");
      break;
    case 1: {
      const uint32_t parms = a.Alloc(0x260);
      std::memcpy(g_mem.ptr<uint8_t>(parms), g_mem.ptr<uint8_t>(0x1536030), 0x260);
      Viewport(g, parms + 0x128, parms + 0x138, world);
      Wr(parms + 0x104, g() % 8 == 0);
      for (uint32_t k = 0x250; k < 0x260; k += 4) Wr(parms + k, g());
      win.push_back(a.All());
      Run({0x4CB270, uint32_t(g()), uint32_t(g()), uint32_t(g()), uint32_t(g()), uint32_t(g()), uint32_t(g()), {parms}}, win, g, s, "random");
      break;
    }
    case 2:
    case 3:
      // Now and then a draw surface count near the limit or negative.
      if (g() % 4 == 0) DrawSurfs(a, g);
      win.push_back(a.All());
      Run({g() % 2 ? 0x4DE980u : 0x500920u, uint32_t(g()), uint32_t(g()), uint32_t(g()), uint32_t(g()), uint32_t(g()), uint32_t(g()), {}}, win, g, s, "random");
      break;
    default: {
      const uint32_t b = Rd(kBackEndData), n = Rd(kNumStaticModels), first = g() % 4;
      for (uint32_t k = 0; k < 8; k++) Wr(b + 0x1FD700 + 16 * k + 4, g() % 2 ? g() % 4 : g());
      const uint32_t ents = b + 0x81700 + first * 0x2B8;
      std::memcpy(g_mem.ptr<uint8_t>(ents), g_mem.ptr<uint8_t>(Rd(kStaticModels)), n * 0x2B8);
      win.push_back({ents, (n + 32) * 0x2B8});
      Wr(0x154D72C, first);
      Wr(0x154D70C, first + n);
      const uint32_t dfirst = g() % 4, dl = b + 0x80000 + dfirst * 0x88;
      std::memcpy(g_mem.ptr<uint8_t>(dl), g_mem.ptr<uint8_t>(Rd(kRefdef)), 0x74 + 32 * 0x88);
      win.push_back({dl, 0x74 + 32 * 0x88});
      Wr(0x154D718, dfirst);
      Wr(0x154D714, dfirst + g() % 33);
      const uint32_t dsfirst = g() % 4;
      Wr(0x154D724, dsfirst);
      win.push_back({b, 0x1000 * 8});
      for (uint32_t va : {0x154D728u, 0x154D720u}) Wr(va, g() % 4);
      Wr(0x154D710, Rd(0x154D728) + g() % 4);
      Wr(0x154D730, Rd(0x154D720) + g() % 4);
      Wr(0x154D734, g() % 2);
      win.push_back({0x154D700, 0x40});
      Wr(0x1535A20, g() % 16 ? (g() % 4 ? 1 : 0x100) : 0);
      Wr(0x1830CE4, g() % 8 ? 0 : g() | 1);
      auto cvar = [&](uint32_t va, uint32_t i, float f) {
        const uint32_t v = a.Alloc(0x24);
        Wr(va, v);
        Wr(v + 0x20, i);
        WrF(v + 0x1C, f);
      };
      cvar(0x15343A4, g() % 16 ? 0 : uint32_t(Pick(g, {1, -1})), 0);
      cvar(0x1534548, uint32_t(Pick(g, {0, 1, 1, 2, 3})), 0);
      cvar(0x1534484, 0, Value(g, 0, 2, 64));
      cvar(0x1534430, 0, Value(g, 0, 8192, 64));
      Wr(0x15347C8, g() % 2048);
      const uint32_t fd = a.Alloc(0x50);
      Viewport(g, fd, fd + 0x10, world);
      std::memcpy(g_mem.ptr<uint8_t>(fd + 0x18), g_mem.ptr<uint8_t>(0x1536030), 0x30);
      Wr(fd + 0x48, g() % 4 ? g() % 100000 : g());
      Wr(fd + 0x4C, g() % 4 ? g() & 0x1F : g());
      win.push_back(a.All());
      Run({0x4DEEC0, uint32_t(g()), uint32_t(g()), uint32_t(g()), uint32_t(g()), uint32_t(g()), uint32_t(g()), {fd}}, win, g, s, "random");
      break;
    }
  }
}

// ---- the entity lighting and the model cache -------------------------------------

// A refdef (+0x160 the dlight count, +0x164 the dlights) of up to 20 of
// the case's lights, now and then exactly twice their radius from p.
uint32_t LightRefdef(Arena& a, std::mt19937& g, uint32_t p) {
  const uint32_t r = a.Alloc(0x168), n = g() % 8 ? g() % 21 : uint32_t(Pick(g, {-1, 0})), dl = a.Alloc(0x88 * 21);
  for (uint32_t i = 0; i < 21; i++) {
    std::memcpy(g_mem.ptr<uint8_t>(dl + 0x88 * i), g_mem.ptr<uint8_t>(PickLight(g, g() % 2)), 0x88);
    const uint32_t d = dl + 0x88 * i;
    if (g() % 4 == 0) {
      const float radius = float(1 + g() % 8);
      WrF(d + 0x74, radius);
      for (uint32_t k = 0; k < 0xC; k += 4) WrF(d + 0x44 + k, g_mem.read<float>(p + k));
      const uint32_t k = 4 * (g() % 3);
      WrF(d + 0x44 + k, g_mem.read<float>(p + k) + 2 * radius * (g() % 2 ? 1 : -1) + float(Pick(g, {0, 0, 1, -1})) / 64);
    } else if (g() % 4 == 0 && g_mem.read<float>(p) == 0 && g_mem.read<float>(p + 4) == 0 &&
               g_mem.read<float>(p + 8) == 0) {
      // From an origin at 0: z exactly twice the radius, x and y each
      // about 0.3 of the reach's last bit squared, so that z + y + x is
      // the reach and x + y + z an ulp more.
      const float radius = Uniform(g, 1, 64);
      const double reach = double(radius) * radius * 4, t = std::sqrt(std::ldexp(0.3, std::ilogb(reach) - 52));
      const float l[3] = {-float(t), float(t), -2 * radius * (g() % 2 ? 1 : -1)};
      WrF(d + 0x74, radius);
      WrVec(d + 0x44, l);
    } else if (g() % 4 == 0) {
      // Near the reach, where the order of the squares' sum decides it
      // (searched: the sums z + y + x and x + y + z on either side of the
      // radius x 2 squared).
      float o[3];
      RdVec(p, o);
      if (!std::isfinite(o[0]) || !std::isfinite(o[1]) || !std::isfinite(o[2])) continue;
      const float radius = Uniform(g, 1, 64);
      const double reach = double(radius) * radius * 4;
      for (int t = 0; t < 64; t++) {
        float l[3], q[3];
        for (int k = 0; k < 2; k++) l[k] = o[k] - Uniform(g, -radius, radius);
        for (int k = 0; k < 2; k++) q[k] = float(double(o[k]) - l[k]);
        const double rest = reach - double(q[0]) * q[0] - double(q[1]) * q[1];
        if (rest <= 0) continue;
        l[2] = float(double(o[2]) - std::sqrt(rest));
        for (int ulp = g() % 3; ulp; ulp--) l[2] = std::nextafter(l[2], g() % 2 ? INFINITY : -INFINITY);
        q[2] = float(double(o[2]) - l[2]);
        const double zyx = (double(q[2]) * q[2] + double(q[1]) * q[1]) + double(q[0]) * q[0];
        const double xyz = (double(q[0]) * q[0] + double(q[1]) * q[1]) + double(q[2]) * q[2];
        if ((reach < zyx) != (reach < xyz)) {
          WrF(d + 0x74, radius);
          WrVec(d + 0x44, l);
          break;
        }
      }
    }
  }
  Wr(r + 0x160, n);
  Wr(r + 0x164, dl);
  return r;
}

// The lighting and the model cache's entries alone, over one to three
// entities of EntityModels: the dlights on a brush model (40 dlights of
// local origins exactly the radius off the bounds now and then, which
// are integral then); an entity's lights (the refdef's, one of the
// case's or none; its lighting origin or a point; up to 20 lights, values
// now and then 0 or NaN, the sun's value 0, -0 or NaN now and then), from
// the grid and once both ways; a vertex's colour (a position, a normal
// and its lights: directions and points whose dot products nearly
// cancel, a spot straight along the direction with the table's cosine at
// its angle equal to the spot's, or ulps off; ambient colours at the
// rounding and clamping edges); a block of each size class, a split of
// each level, the eviction and the frees of the trees' nodes.
void LightCase(Arena& a, std::mt19937& g, Stats& s) {
  a.Reset();
  for (uint32_t va = 0x1536000; va < 0x1536500; va += 4) Wr(va, g());
  const uint32_t m = 1 + g() % 3, ents = a.Alloc(m * 0x2B8 + 4);
  for (uint32_t i = 0; i < m * 0x2B8; i += 4) Wr(ents + i, g());
  for (uint32_t i = 0; i < m; i++) Wr(ents + 0x2B8 * i, uint32_t(Pick(g, {0, 1, 2})));
  Wr(kNumEntities, m);
  Wr(kEntities, ents);
  Wr(0x1535A24, g() % 64);
  EntityModels(a, g, ents, m, true, false);
  const uint32_t e = ents + 0x2B8 * (g() % m);
  std::vector<Window> win = {{0x1536000, 0x500}, {0x1535D60, 8}};
  const uint32_t rnd[6] = {uint32_t(g()), uint32_t(g()), uint32_t(g()), uint32_t(g()), uint32_t(g()), uint32_t(g())};
  auto vec = [&](float lo, float hi, uint32_t special) {
    const uint32_t v = a.Alloc(12);
    for (uint32_t k = 0; k < 12; k += 4) WrF(v + k, Value(g, lo, hi, special));
    return v;
  };
  auto refdef = [&](uint32_t p) { return g() % 4 == 0 ? 0 : g() % 2 ? 0x1536318 : LightRefdef(a, g, p); };
  switch (g() % 10) {
    case 0: {
      const uint32_t model = Rd(Rd(kXModels + 4 * Rd(e + 8)) + 0x4C), n = g() % 4 ? g() % 41 : uint32_t(Pick(g, {-1, 0}));
      const uint32_t dl = a.Alloc(0x88 * 40);
      const bool exact = g() % 2;
      if (exact)
        for (uint32_t k = 0; k < 0xC; k += 4) {
          WrF(model + k, float(int(g() % 64) - 32));
          WrF(model + 0xC + k, g_mem.read<float>(model + k) + float(g() % 16));
        }
      for (uint32_t i = 0; i < 40; i++) {
        const uint32_t d = dl + 0x88 * i;
        const float r = exact ? float(g() % 8) / 2 : Value(g, 0, 64, 32);
        WrF(d + 0x74, r);
        for (uint32_t k = 0; k < 0xC; k += 4) {
          const float lo = g_mem.read<float>(model + k), hi = g_mem.read<float>(model + 0xC + k);
          const uint32_t q = g() % 8;
          WrF(d + 0x78 + k, q == 0 ? hi + r : q == 1 ? lo - r : q == 2 ? std::nextafter(hi + r, INFINITY)
                           : q == 3 ? std::nextafter(lo - r, -INFINITY) : exact ? Uniform(g, lo, hi) : Value(g, lo - r, hi + r, 32));
        }
      }
      Wr(0x1536474, n);
      Wr(kRefdef, dl);
      Wr(0x1535D68, e);
      win.push_back(a.All());
      Run({0x4AA490, rnd[0], rnd[1], rnd[2], rnd[3], rnd[4], rnd[5], {model}}, win, g, s, "random");
      break;
    }
    case 1: {
      const uint32_t o = g() % 2 ? e + 0xC : vec(-128, 128, 64), n = g() % 21;
      if (o != e + 0xC && g() % 4 == 0)
        for (uint32_t k = 0; k < 12; k += 4) WrF(o + k, 0);
      const uint32_t values = a.Alloc(4 * 50), lights = a.Alloc(4 * 50);
      for (uint32_t i = 0; i < 50; i++) {
        WrF(values + 4 * i, g() % 4 ? 1.0f : g() % 2 ? Value(g, 0, 2, 256) : float(Pick(g, {0, 2})));
        Wr(lights + 4 * i, PickLight(g, PointLit(e)));
      }
      const float sun = g() % 4 ? Uniform(g, 0, 1) : std::bit_cast<float>(uint32_t(Pick(g, {0, int32_t(0x80000000), 0x7FC00000, 0x3F800000})));
      win.push_back(a.All());
      Run({0x4AB490, rnd[0], rnd[1], rnd[2], rnd[3], rnd[4], e,
           {refdef(o), o, n, values, std::bit_cast<uint32_t>(sun), lights, uint32_t(g() % 2)}}, win, g, s, "random");
      break;
    }
    case 2: {
      const uint32_t o = g() % 2 ? e + 0xC : vec(-128, 128, 64);
      if (g() % 8 == 0) Wr(kWorld, 0);
      win.push_back(a.All());
      Run({0x4ABD30, e, rnd[1], rnd[2], rnd[3], o, rnd[5], {refdef(o), uint32_t(g() % 2)}}, win, g, s, "random");
      break;
    }
    case 3:
      g_mem.write<uint8_t>(e + 4, uint8_t(g()));
      win.push_back(a.All());
      Run({0x4ABDC0, rnd[0], rnd[1], rnd[2], rnd[3], rnd[4], e, {refdef(e + 0xC)}}, win, g, s, "random");
      break;
    case 4:
      win.push_back(a.All());
      Run({0x4ABEF0, e, refdef(e + 0xC), rnd[2], rnd[3], rnd[4], rnd[5], {}}, win, g, s, "random");
      break;
    case 5: {
      const uint32_t pos = vec(-64, 64, 16), nrm = a.Alloc(12), out = a.Alloc(4);
      float n[3], p[3];
      Normal(g, n);
      WrVec(nrm, n);
      Wr(out, g());
      if (g() % 16 == 0) WrF(nrm + 4 * (g() % 3), Value(g, -1, 1, 1024));
      if (!PointLit(e) || !std::isfinite(g_mem.read<float>(pos)) || !std::isfinite(g_mem.read<float>(pos + 4)) ||
          !std::isfinite(g_mem.read<float>(pos + 8)))
        Directional(g, e);
      const uint32_t r = std::min(uint32_t(g() % 5), 3u);
      if (r == 0 || r == 1) {
        // A dot product that nearly cancels: a direction, or a point (the
        // vertex at 0) along it, its first light; a point's spot straight
        // along it with the table's cosine its own, or an ulp off.
        const uint32_t l = a.Alloc(0x88);
        std::memcpy(g_mem.ptr<uint8_t>(l), g_mem.ptr<uint8_t>(PickLight(g, r)), 0x88);
        Cancel3(g, n, p);
        WrVec(nrm, n);
        // Colours along the normal large enough for a dot product's last
        // bits to show.
        for (uint32_t k = 0; k < 0x10; k += 4) {
          WrF(l + 0x14 + k, g() % 2 ? 0.0f : Uniform(g, 0, 1));
          WrF(l + 0x24 + k, std::ldexp(Uniform(g, 1, 2), 40 + int(g() % 40)) * (g() % 2 ? 1 : -1));
        }
        if (r) {
          for (float& x : p) x = x * float(g() % 4 + 1);
          for (uint32_t k = 0; k < 12; k += 4) WrF(pos + k, 0);
          WrF(l + 0x50, 1.0f);
          const uint32_t axis = g() % 3;
          if (g() % 2) {
            // A spot whose axis nearly cancels the direction, against a
            // cosine of 0 at its angle; the normal along the direction.
            WrVec(l + 0x54, n);
            const double len = std::sqrt(double(p[0]) * p[0] + double(p[1]) * p[1] + double(p[2]) * p[2]);
            for (uint32_t k = 0; k < 3; k++) WrF(nrm + 4 * k, float(p[k] / len));
            for (uint32_t k = 0; k < 0x10; k += 4) WrF(l + 0x14 + k, std::ldexp(Uniform(g, 1, 2), 40 + int(g() % 20)));
            const float angle = float(g() % 90);
            WrF(l + 0x70, angle);
            const float t = float(double(float(double(angle) + 90.0) * double(std::bit_cast<float>(0x40360B61u))));
            WrF(0x1544530 + 4 * uint32_t(int32_t(std::nearbyint(double(t) + 0x1p-30)) & 0x3FF), 0.0f);
          } else if (g() % 3 == 0) {
            // Straight along an axis, a power of two away: the direction
            // normalised exactly.
            p[0] = p[1] = p[2] = 0;
            p[axis] = float(1 << (g() % 4)) * (g() % 2 ? 1 : -1);
            const float c = Uniform(g, -1, 1);
            float sp[3] = {0, 0, 0};
            sp[axis] = p[axis] > 0 ? c : -c;
            WrVec(l + 0x54, sp);
            const float angle = float(g() % 90);
            WrF(l + 0x70, angle);
            const float t = float(double(float(double(angle) + 90.0) * double(std::bit_cast<float>(0x40360B61u))));
            const int32_t idx = int32_t(std::nearbyint(double(t) + 0x1p-30)) & 0x3FF;
            WrF(0x1544530 + 4 * uint32_t(idx), g() % 2 ? c : std::nextafter(c, g() % 2 ? 2.0f : -2.0f));
          } else {
            WrF(l + 0x70, 180.0f);
          }
          WrVec(l + 0x44, p);
        } else {
          WrF(l + 0x50, 0.0f);
          WrVec(l + 0x44, p);
        }
        if (r && g() % 3 == 0) {
          // A value over a denormal attenuation: beyond a float, and
          // channels of no colour (0 x the unrounded value, 0 x the
          // stored infinity a NaN).
          WrF(l + 0x60, std::bit_cast<float>(uint32_t(1 + g() % 0x1000)));
          WrF(l + 0x64, 0);
          WrF(l + 0x68, 0);
          for (uint32_t k = 0; k < 0x10; k += 8) {
            WrF(l + 0x14 + k, 0);
            WrF(l + 0x24 + k, 0);
          }
        }
        if (r && g() % 4 == 0) {
          // The table's two positions about a tie different.
          const float angle = TieAngle(g);
          WrF(l + 0x70, angle);
          const int32_t i = int32_t(std::floor(double(float(double(float(double(angle) + 90.0)) * double(std::bit_cast<float>(0x40360B61u))))));
          WrF(0x1544530 + 4 * uint32_t(i & 0x3FF), 2.0f);
          WrF(0x1544530 + 4 * uint32_t((i + 1) & 0x3FF), -2.0f);
        }
        Wr(e + 0xD0, l);
        if (int32_t(Rd(e + 0xCC)) < 1) Wr(e + 0xCC, 1);
      } else if (r == 2) {
        // Ambient only, at the edges of rounding (n + 0.5) / 255 and of
        // the clamps.
        Wr(e + 0xCC, 0);
        WrF(e + 0x110, 0);
        for (uint32_t k = 0; k < 0x10; k += 4)
          WrF(Rd(kWorld) + 0xD8 + k, g() % 4 ? (float(g() % 258) - 1.5f + float(g() % 2)) / 255.0f
                                             : std::bit_cast<float>(uint32_t(Pick(g, {0, int32_t(0x80000000), 0x7FC00000, 0x3F800000, 0x40000000}))));
        Wr(Rd(kWorld) + 0x118, 1);
      }
      win.push_back(a.All());
      Run({0x4DA420, rnd[0], rnd[1], rnd[2], e, rnd[4], rnd[5], {pos, nrm, out}}, win, g, s, "random");
      break;
    }
    case 6:
      win.push_back(a.All());
      Run({0x4DA330, rnd[0], rnd[1], rnd[2], kModelCache, rnd[4], rnd[5], {uint32_t(5 + g() % 5)}}, win, g, s, "random");
      break;
    case 7:
      win.push_back(a.All());
      Run({0x4DA220, rnd[0], rnd[1], rnd[2], rnd[3], rnd[4], rnd[5], {kModelCache, uint32_t(g() % 5)}}, win, g, s, "random");
      break;
    case 8:
      win.push_back(a.All());
      Run({0x4DA1B0, rnd[0], rnd[1], rnd[2], rnd[3], rnd[4], kModelCache, {}}, win, g, s, "random");
      break;
    default: {
      if (g_cache_nodes.empty()) return;
      const CacheNode& c = g_cache_nodes[g() % g_cache_nodes.size()];
      win.push_back(a.All());
      Run({0x4DA0F0, rnd[0], rnd[1], rnd[2], rnd[3], rnd[4], rnd[5], {kModelCache, c.entry, c.node, c.shift}}, win, g, s, "random");
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
  g_mem.Commit(kHeap, kHeapSize, recomp::Prot::RW);
  g_mem.Commit(kVertexBuffer, (kVertexBufferSize + 0xFFF) & ~0xFFFu, recomp::Prot::RW);
  // R_CellForCamera reads its node through a null world (after ri.Error).
  g_mem.Commit(0, 0x1000, recomp::Prot::RW);
  Wr(kErrorPtr, kError);
  Wr(kPrintfPtr, kPrintf);
  Wr(kAllocPtr, kAlloc);
  Wr(kFreePtr, kFree);
  Wr(kMillisecondsPtr, kMilliseconds);
  Wr(kSubModelPtr, kSubModel);
  Wr(kModelCallbackPtr, kModelCallback);
  Wr(kGlBindPtr, kGlBind);
  Wr(kGlSubDataPtr, kGlSubData);
  Wr(kGlUpdatePtr, kGlUpdate);
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
    for (int i = 0; i < 8; i++) WalkCases(a, g, w, s);
    WorldWalkCase(a, g, w, s);
    SceneCase(a, g, w, s);
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
  for (int i = 0; i < 40000; i++) PortalCase(a, g, s, i < 20000 ? 0 : 48);
  for (int i = 0; i < 40000; i++) EntityCase(a, g, s);
  for (int i = 0; i < 30000; i++) LightCase(a, g, s);
  const int random = s.cases;
  for (int i = 2; i < argc; i++)
    if (!RunCaptures(argv[i], g, s)) return 1;
  std::printf("%d random + %d captured cases (", random, s.cases - random);
  for (int i = 0; i < kNumEntries; i++)
    std::printf("%s%s %d", i ? ", " : "", kEntries[i].name, s.by_entry[i]);
  std::printf("; %d calls out): %d mismatched\n", s.calls, s.failures);
  return s.failures ? 1 : 0;
}
