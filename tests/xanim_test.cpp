// The native XAnim evaluation (src/xanim.cpp) against the original
// functions, lifted from the EXE at build time with `recomp_lift --only`
// (CMakeLists.txt) and run on x86recomp's exact x87 model (SoftFloat, not
// its host-double runs). CoDSP.exe is mapped as the game maps it, for its
// constants and globals. Each case runs 0x47CD80 (the DObj's poses from its
// anim tree), 0x4766E0 (its bone matrices), the one then the other, or
// 0x47CC90 (cgame's update of the tree) or 0x47CCD0 (the game's, once or
// twice) alone or before them, as the game does, through the lifted
// originals and the native code from the same guest memory, and both must
// leave the same bytes in everything the case built (the DObj, its
// skeleton, cached maps, models, the anim tree and its xanims, the string
// table; for an update the whole info pool), the globals (with the notes
// queue) and the stack arguments, the same ESP, EBX, ESI, EDI, EBP, x87
// stack (top, empty registers, control word) and for 0x47CCD0 EAX, and
// make the same calls of the lifted functions the originals call: the
// part-to-bone map (0x478FC0), its release (0x45D580), the part duplicates
// (0x4761F0), an info's name release (0x45D530) and the game's notes to
// script (0x47B390, 0x46A7A0, 0x4683B0), which are stubs here
// (tests/xanim_stubs.manifest) that log their register and stack
// arguments and make deterministic maps. Not compared: EAX otherwise, ECX,
// EDX, EFLAGS, the x87 status word and the stack below the return address
// (src/xanim.cpp).
//
//   xanim_test EXE [--cases N] [--seed S] [CAPTURE...]
//
// Random cases (mt19937, fixed seed): DObjs of 1-6 models (attached to a
// bone of an earlier one or not) and up to 127 bones, skeletons with bones
// done, posed and set elsewhere, part duplicates, anim trees of up to 24
// anims in up to four levels with zero, ordinary and special weights in
// either info state, xanims of 0-1000 frames whose parts have
// translations or none, full or z-only rotations or none, as one value, as
// sparse keys (byte or u16 key frames) or as a key per frame, at times from
// 0 to 1, cached maps current, stale or missing, and masks of all or some
// bones; floats now and then zeros, denormals, infinities, NaNs or huge.
// For the updates: synchronised parents or not, looping xanims or not,
// weights blending to their goals, notes (for the game's: note children,
// next notes, cycles around the anim's), with or without the game's notes
// flag and DObj +0x12, the info pool's free list, dt
// from 0 to 0.2 s, and worlds of exact values (steps ending on whole
// numbers, dt + 0.001 equal to a goal time); its times, rates and
// frequencies stay finite and small, as a huge step never ends in the
// original either. Captures (tests/xanim_capture.h) add the game's own
// calls, on its data, with what the lifted functions did in game served
// back in order.

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <initializer_list>
#include <random>
#include <string>
#include <vector>

#include "runtime/guest_memory.h"
#include "runtime/lifted.h"
#include "runtime/pe_image.h"
#include "runtime/x87.h"
#include "xanim_capture.h"

namespace recomp {
uint8_t* g_guest_base = nullptr;

namespace gen {
void f_0047CD80_lifted(Ctx& c);
void f_004766E0_lifted(Ctx& c);
void f_0047CC90_lifted(Ctx& c);
void f_0047CCD0_lifted(Ctx& c);
// The native code (tests/xanim_native.cpp renames its entries)...
void n_0047CD80(Ctx& c);
void n_004766E0(Ctx& c);
void n_0047CC90(Ctx& c);
void n_0047CCD0(Ctx& c);
// ...and the lifted originals call each other, not it.
void f_0047CD80(Ctx& c) { f_0047CD80_lifted(c); }
void f_004766E0(Ctx& c) { f_004766E0_lifted(c); }
void f_0047CC90(Ctx& c) { f_0047CC90_lifted(c); }
void f_0047CCD0(Ctx& c) { f_0047CCD0_lifted(c); }
// unzCloseCurrentFile, tolower and the memcpy 0x42B0A0, native in the game
// (src/unzip.cpp, src/crt.cpp, src/render.cpp): here only the stubbed
// functions' lifted bodies reach them.
void f_0049EAA0_lifted(Ctx& c);
void f_0049EAA0(Ctx& c) { f_0049EAA0_lifted(c); }
void f_0051B3DD_lifted(Ctx& c);
void f_0051B3DD(Ctx& c) { f_0051B3DD_lifted(c); }
void f_0042B0A0_lifted(Ctx& c);
void f_0042B0A0(Ctx& c) { f_0042B0A0_lifted(c); }
// The stubs (below).
void f_00478FC0(Ctx& c);
void f_0045D580(Ctx& c);
void f_004761F0(Ctx& c);
void f_0045D530(Ctx& c);
void f_0047B390(Ctx& c);
void f_0046A7A0(Ctx& c);
void f_004683B0(Ctx& c);
}  // namespace gen

// Nothing the cases reach calls out.
void Dispatch(Ctx&, uint32_t target, uint32_t va) {
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

using namespace xanim_capture;
using recomp::Ctx;

recomp::GuestMemory g_mem;

// The string table (its entries hold the maps and part duplicates) at the
// start of the arena, the random cases' data after it, the stack below.
constexpr uint32_t kArena = 0x30000000, kArenaSize = 0x1000000;
constexpr uint32_t kStringTable = kArena, kStringTableSize = 0x10000;
constexpr uint32_t kStack = 0x2FF00000, kStackSize = 0x100000;

constexpr uint32_t kInfo = 0xA6D010, kState = 0xA8F648;

uint32_t Rd(uint32_t va) { return g_mem.read<uint32_t>(va); }
void Wr(uint32_t va, uint32_t v) { g_mem.write<uint32_t>(va, v); }
void Wr16(uint32_t va, uint16_t v) { g_mem.write<uint16_t>(va, v); }
void Wr8(uint32_t va, uint8_t v) { g_mem.write<uint8_t>(va, v); }
void WrF(uint32_t va, float f) { g_mem.write<float>(va, f); }

struct Window {
  uint32_t va, size;
};

struct Stats {
  int cases = 0, failures = 0, by_fn[5] = {};  // anim, skel, update, game update, several
  int made = 0, freed = 0, dups = 0, names = 0;  // stub calls (made by the originals)
  int notes = 0;  // queued by the updates
  int sent = 0, ends = 0, partial = 0;  // the game's: notes to script, cycle ends, steps cut short
  int game = 0, game_differs = 0;     // captured calls with the game's result
  // What the captured 0x47CD80 calls evaluate: anims of nonzero weight
  // blended, leaves (interpolated or not), the most bones.
  int blends = 0, leaves = 0, interpolated = 0, bones = 0;
};

// ---- the stubs --------------------------------------------------------------------

// A call of a stub, as the originals make it.
struct Event {
  uint32_t fn, eax, edx, esi, arg0, arg1, arg2;
  bool operator==(const Event&) const = default;
};
// What a stub does in a captured call: what it did in game.
struct Served {
  Event in;
  uint32_t eax;
  std::vector<std::pair<Window, std::vector<uint8_t>>> writes;
};

struct Stubs {
  uint32_t next = 1;  // the next free string-table entry
  uint32_t seed = 0;
  std::vector<Event> log;
  std::deque<Served> served;  // a captured call's events, in order
  bool capture = false;
  bool failed = false;  // a captured call called differently from the game
} g_stubs;

uint32_t Arg(const Ctx& c, int n) { return Rd(c.esp + 4 + 4 * n); }

// A callee: its return, and the caller-saved registers it leaves changed.
void Return(Ctx& c, uint32_t eax) {
  c.eax = eax;
  c.ecx = 0xC1C1C1C1u ^ g_stubs.seed;
  c.edx = 0xD1D1D1D1u ^ g_stubs.seed;
  c.esp += 4;
}

uint32_t NewEntry(uint32_t bytes) {
  const uint32_t h = g_stubs.next;
  g_stubs.next += (bytes + 4 + 7) / 8;
  if (kStringTable + g_stubs.next * 8 > kStringTable + kStringTableSize) {
    std::printf("string table full\n");
    std::abort();
  }
  return h;
}

// A captured call's next event, which must be this one: its effects.
bool Serve(Ctx& c, const Event& e) {
  if (!g_stubs.capture) return false;
  if (g_stubs.served.empty() || !(g_stubs.served.front().in == e)) {
    g_stubs.failed = true;
    Return(c, 0);
    return true;
  }
  const Served& s = g_stubs.served.front();
  for (const auto& [w, bytes] : s.writes) std::memcpy(g_mem.ptr(w.va), bytes.data(), w.size);
  Return(c, s.eax);
  g_stubs.served.pop_front();
  return true;
}

// The part-to-bone map of the anim at EAX for (models, count): 128 bits of
// bones, then a bone per part (the anim's data: part names at +0xC). Here
// a random map, the same for the same calls.
void MakeMap(Ctx& c) {
  const Event e{kMakeMap, c.eax, 0, 0, Arg(c, 0), Arg(c, 1), 0};
  g_stubs.log.push_back(e);
  if (Serve(c, e)) return;
  std::mt19937 g(g_stubs.seed ^ uint32_t(g_stubs.log.size() * 7919));
  const uint32_t numParts = uint32_t(g_mem.read<int16_t>(Rd(Rd(Rd(c.eax + 4) + 4) + 0xC)));
  const uint32_t numBones = g_mem.read<uint8_t>(Rd(kGlobals2 + 0x14) + 0x17);  // the DObj's
  const uint32_t h = NewEntry(16 + numParts);
  const uint32_t map = kStringTable + h * 8 + 4;
  for (int w = 0; w < 4; w++) Wr(map + w * 4, g());
  for (uint32_t p = 0; p < numParts; p++) Wr8(map + 16 + p, uint8_t(g() % numBones));
  Return(c, (g() & 0xFFFF0000u) | h);
}
void FreeMap(Ctx& c) {
  const Event e{kFreeMap, c.eax, c.edx, 0, 0, 0, 0};
  g_stubs.log.push_back(e);
  if (Serve(c, e)) return;
  Return(c, 0);
}
// Part duplicates: 128 bits, then pairs of bone + 1, source + 1 (earlier
// bones, in order), then 0.
void Duplicates(Ctx& c) {
  const uint32_t dobj = Arg(c, 0);
  const Event e{kDuplicates, 0, 0, 0, dobj, 0, 0};
  g_stubs.log.push_back(e);
  if (Serve(c, e)) return;
  std::mt19937 g(g_stubs.seed ^ 0xD0B1u);
  const uint32_t numBones = g_mem.read<uint8_t>(dobj + 0x17);
  std::vector<uint8_t> pairs;
  uint32_t bits[4] = {};
  for (uint32_t b = 1; b < numBones; b++)
    if (g() % 6 == 0) {
      pairs.push_back(uint8_t(b + 1));
      pairs.push_back(uint8_t(g() % b + 1));
      if (g() % 4) bits[b >> 5] |= 1u << (b & 31);
    }
  const uint32_t h = NewEntry(16 + uint32_t(pairs.size()) + 1);
  const uint32_t d = kStringTable + h * 8 + 4;
  for (int w = 0; w < 4; w++) Wr(d + w * 4, bits[w]);
  for (size_t i = 0; i < pairs.size(); i++) Wr8(d + 16 + uint32_t(i), pairs[i]);
  Wr8(d + 16 + uint32_t(pairs.size()), 0);
  Wr16(dobj + 0x14, uint16_t(h));
  Return(c, g());
}

// Releases a string-table entry (an info's name): logged.
void FreeName(Ctx& c) {
  const Event e{kFreeName, 0, 0, 0, Arg(c, 0), 0, 0};
  g_stubs.log.push_back(e);
  if (Serve(c, e)) return;
  Return(c, 0);
}

// The game's notes to script: logged.
void SendNote(Ctx& c) {
  const Event e{kSendNote, 0, 0, c.esi, Arg(c, 0), Arg(c, 1), 0};
  g_stubs.log.push_back(e);
  if (Serve(c, e)) return;
  Return(c, g_stubs.seed);
}
void PushString(Ctx& c) {
  const Event e{kPushString, 0, 0, 0, Arg(c, 0), 0, 0};
  g_stubs.log.push_back(e);
  if (Serve(c, e)) return;
  Return(c, g_stubs.seed);
}
void ScriptCall(Ctx& c) {
  const Event e{kScriptCall, 0, 0, 0, Arg(c, 0), Arg(c, 1), Arg(c, 2)};
  g_stubs.log.push_back(e);
  if (Serve(c, e)) return;
  Return(c, g_stubs.seed);
}

}  // namespace

namespace recomp::gen {
void f_00478FC0(Ctx& c) { MakeMap(c); }
void f_0045D530(Ctx& c) { FreeName(c); }
void f_0045D580(Ctx& c) { FreeMap(c); }
void f_004761F0(Ctx& c) { Duplicates(c); }
void f_0047B390(Ctx& c) { SendNote(c); }
void f_0046A7A0(Ctx& c) { PushString(c); }
void f_004683B0(Ctx& c) { ScriptCall(c); }
}  // namespace recomp::gen

namespace {

// ---- comparing ----------------------------------------------------------------------

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

const char* FnName(uint32_t fn) {
  return fn == kCalcAnim ? "0x47CD80" : fn == kCalcSkel ? "0x4766E0" : fn == kUpdate ? "0x47CC90" : "0x47CCD0";
}
int FnIndex(uint32_t fn) {
  return fn == kCalcAnim ? 0 : fn == kCalcSkel ? 1 : fn == kUpdate ? 2 : 3;
}

struct Call {
  uint32_t fn, eax, ecx, edx, arg, arg2;
};

Ctx NewCtx(std::mt19937& g, uint32_t esp) {
  Ctx c{};
  c.eax = g();
  c.ecx = g();
  c.edx = g();
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

// Runs the calls in turn through the lifted originals and through the native
// code from the same memory and compares. w: everything they may write.
// served: a captured call's events. Leaves the originals' result in memory.
bool Run(const std::vector<Call>& calls, std::vector<Window> w, std::mt19937& g, Stats& s,
         const char* what, const std::deque<Served>* served = nullptr) {
  const uint32_t esp = kStack + kStackSize - 0x100;
  w.push_back({esp, 12});
  w.push_back({kGlobals1, kGlobals1Size});
  w.push_back({kGlobals2, kGlobals2Size});
  std::vector<Ctx> c0;
  for (size_t i = 0; i < calls.size(); i++) {
    c0.push_back(NewCtx(g, esp));
    c0.back().eax = calls[i].eax;
    c0.back().ecx = calls[i].ecx;
    c0.back().edx = calls[i].edx;
  }
  const std::vector<uint8_t> before = Save(w);
  const uint32_t seed = g(), next = g_stubs.next;

  using Fn = void (*)(Ctx&);
  auto run = [&](bool native, std::vector<Ctx>& out) {
    g_stubs.seed = seed;
    g_stubs.next = next;
    g_stubs.log.clear();
    g_stubs.capture = served != nullptr;
    g_stubs.failed = false;
    if (served) g_stubs.served = *served;
    for (size_t i = 0; i < calls.size(); i++) {
      Ctx c = c0[i];
      Wr(esp, 0xDEAD0000);
      Wr(esp + 4, calls[i].arg);
      Wr(esp + 8, calls[i].arg2);
      static constexpr Fn natives[] = {recomp::gen::n_0047CD80, recomp::gen::n_004766E0,
                                       recomp::gen::n_0047CC90, recomp::gen::n_0047CCD0};
      static constexpr Fn lifted[] = {
          recomp::gen::f_0047CD80_lifted, recomp::gen::f_004766E0_lifted,
          recomp::gen::f_0047CC90_lifted, recomp::gen::f_0047CCD0_lifted};
      const uint32_t f = calls[i].fn;
      const Fn fn = (native ? natives : lifted)[FnIndex(f)];
      fn(c);
      if (f == kUpdate && !native) s.notes += Rd(kNumNotes);
      if (f == kGameUpdate && !native) s.partial += c.eax == 1;
      out.push_back(c);
    }
  };
  std::vector<Ctx> cl, cn;
  run(false, cl);
  const std::vector<Event> log = g_stubs.log;
  const bool served_ok = !g_stubs.failed && g_stubs.served.empty();
  const std::vector<uint8_t> after = Save(w);
  Load(w, before);
  run(true, cn);
  const std::vector<uint8_t> got = Save(w);

  s.cases++;
  for (const Call& k : calls) s.by_fn[FnIndex(k.fn)]++;
  if (calls.size() > 1) s.by_fn[4]++;
  for (const Event& e : log) {
    s.made += e.fn == kMakeMap;
    s.freed += e.fn == kFreeMap;
    s.dups += e.fn == kDuplicates;
    s.names += e.fn == kFreeName;
    s.sent += e.fn == kSendNote;
    s.ends += e.fn == kPushString;
  }
  bool ok = true;
  auto fail = [&](const char* fmt, auto... args) {
    if (s.failures < 20) {
      std::printf("%s %s: ", what, FnName(calls.back().fn));
      std::printf(fmt, args...);
      std::printf("\n");
    }
    ok = false;
  };
  if (served && !served_ok) fail("the original's calls differ from the game's");
  if (!(g_stubs.log == log)) fail("calls differ (%zu lifted, %zu native)", log.size(), g_stubs.log.size());
  for (size_t i = 0; i < calls.size(); i++) {
    const Ctx& l = cl[i];
    const Ctx& n = cn[i];
    if (n.esp != l.esp || n.ebp != l.ebp || n.ebx != l.ebx || n.esi != l.esi || n.edi != l.edi ||
        n.fpu.top != l.fpu.top || n.fpu.empty != l.fpu.empty || n.fpu.cw != l.fpu.cw ||
        (calls[i].fn == kGameUpdate && n.eax != l.eax))
      fail("registers differ after call %zu (esp %08X/%08X ebx %08X/%08X esi %08X/%08X edi "
           "%08X/%08X ebp %08X/%08X top %d/%d eax %08X/%08X)",
           i, l.esp, n.esp, l.ebx, n.ebx, l.esi, n.esi, l.edi, n.edi, l.ebp, n.ebp, l.fpu.top,
           n.fpu.top, l.eax, n.eax);
  }
  size_t at = 0;
  for (const Window& x : w) {
    for (uint32_t i = 0; i < x.size; i++)
      if (got[at + i] != after[at + i]) {
        const uint32_t a = (x.va + i) & ~3u;
        const size_t o = at + (a - x.va);
        uint32_t lv = 0, nv = 0;
        std::memcpy(&lv, after.data() + o, std::min<size_t>(4, after.size() - o));
        std::memcpy(&nv, got.data() + o, std::min<size_t>(4, got.size() - o));
        fail("%08X lifted %08X native %08X", a, lv, nv);
        break;
      }
    at += x.size;
  }
  if (!ok) s.failures++;
  Load(w, after);
  return ok;
}

// ---- random cases -------------------------------------------------------------------

struct Arena {
  uint32_t top = kArena + kStringTableSize;
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

// A float: mostly in [lo, hi), now and then (1 in special) something else.
float Float(std::mt19937& g, float lo, float hi, uint32_t special) {
  if (special && g() % special == 0) {
    static const uint32_t bits[] = {
        0x00000000, 0x80000000, 0x00000001, 0x807FFFFF, 0x00800000, 0x7F800000,
        0xFF800000, 0x7FC00000, 0xFFC00000, 0x7F800001, 0xFFA00000, 0x7FBFFFFF,
        0x7F7FFFFF, 0xFF7FFFFF, 0x5F000000, 0x3F800000, 0x3F7FFFFF, 0x34000000};
    if (g() % 4 == 0) return std::bit_cast<float>(uint32_t(g()));
    return std::bit_cast<float>(bits[g() % std::size(bits)]);
  }
  return lo + (hi - lo) * float(g() >> 8) / float(1 << 24);
}
// A float in [lo, hi) or (1 in special) a finite one of few bits, a NaN or
// a zero: for times, rates and frequencies, whose steps the original
// loops over (a huge or infinite one never ends, in game too).
float Bounded(std::mt19937& g, float lo, float hi, uint32_t special) {
  if (special && g() % special == 0) {
    static const uint32_t bits[] = {0x00000000, 0x80000000, 0x00000001, 0x807FFFFF, 0x7FC00000,
                                    0xFFC00000, 0x7F800001, 0x3F800000, 0x3F7FFFFF, 0x3F000000};
    return std::bit_cast<float>(bits[g() % std::size(bits)]);
  }
  return Float(g, lo, hi, 0);
}
// Bounded, but no negative values (a NaN keeps its sign): the updates'
// weights, rates, frequencies and dt. With a negative weight beside a
// positive one the sum of a synchronised parent's weights can be tiny and
// its mean frequency huge, either way; with a negative step a note falls
// at a huge negative fraction of it, and the game's update cuts dt there:
// steps whose wraps never end, in the original too. The game has neither.
float NonNegative(std::mt19937& g, float lo, float hi, uint32_t special) {
  const float w = Bounded(g, lo, hi, special);
  return std::isnan(w) ? w : std::fabs(w);
}
float Weight(std::mt19937& g, uint32_t special) { return NonNegative(g, 0, 1, special ? 16 : 0); }
// One of a few values that make the update's sums and tests exact: steps
// ending on 1, a dt + 0.001 equal to a goal time, a weight blended to just
// under 1e-6 by a tiny dt.
float Exact(std::mt19937& g, std::initializer_list<uint32_t> bits) {
  return std::bit_cast<float>(bits.begin()[g() % bits.size()]);
}
constexpr uint32_t kMilliBits = 0x3A83126F, kTwoMilliBits = 0x3B03126F, kTinyBits = 0x358637BE;
// A world whose update's times, rates, frequencies and dt are all exact (so
// steps land on whole numbers).
bool g_grid = false;
// A world whose anims mostly have weights and names (the game's update
// passes notes to script only through anims with both).
bool g_lively = false;
// The same, now and then scaled down by up to 2^-40 (sums of products of
// such values round differently in another order).
float Wide(std::mt19937& g, float lo, float hi, uint32_t special) {
  const float f = Float(g, lo, hi, special);
  return g() % 4 ? f : f * std::ldexp(1.0f, -int(g() % 41));
}
int16_t Short(std::mt19937& g) {
  switch (g() % 8) {
    case 0: return int16_t(g() % 3 - 1);
    case 1: return g() % 2 ? 32767 : -32768;
    default: return int16_t(g());
  }
}

struct World {
  Arena a;
  uint32_t dobj, mask;
  uint32_t numBones;
  std::vector<uint32_t> infos;  // in use
};

// A track of the anim's frames (stride bytes a key: int16[2], int16[4] or
// float[3]): one value, sparse keys (key frames from 0 to numFrames) or a
// key per frame.
uint32_t Track(std::mt19937& g, World& w, uint32_t numFrames, uint32_t stride, bool trans,
               uint32_t special) {
  const uint32_t mode = g() % 10;
  auto value = [&](uint32_t va, int i) {
    if (trans)
      WrF(va, Wide(g, -64, 64, special));
    else
      g_mem.write<int16_t>(va, Short(g));
    (void)i;
  };
  if (mode < 3) {
    const uint32_t t = w.a.Alloc(0x10);
    for (uint32_t i = 0; i < 4; i++) Wr(t + i * 4, g());
    Wr16(t + 4, 0);
    if (trans) {
      for (uint32_t off : {0u, 8u, 12u}) value(t + off, 0);
    } else {
      for (uint32_t off : {0u, 2u, 6u, 8u}) value(t + off, 0);
    }
    return t;
  }
  const uint32_t isize = numFrames < 0x100 ? 1 : 2;
  uint32_t numKeys;
  std::vector<uint32_t> table;
  if (mode < 7 && numFrames >= 2) {
    numKeys = 1 + g() % (numFrames - 1);
    if (g() % 2) numKeys = std::min<uint32_t>(numKeys, 1 + g() % 8);
    // Key frames: 0, numKeys - 1 others in order, numFrames; now and then
    // two the same.
    std::vector<uint32_t> pool;
    for (uint32_t f = 1; f < numFrames; f++) pool.push_back(f);
    std::shuffle(pool.begin(), pool.end(), g);
    table.push_back(0);
    table.insert(table.end(), pool.begin(), pool.begin() + (numKeys - 1));
    table.push_back(numFrames);
    std::sort(table.begin(), table.end());
    if (g() % 50 == 0 && numKeys > 1) table[1 + g() % (numKeys - 1)] = table[1];
  } else {
    numKeys = std::max(numFrames, 1u) + g() % 3;
  }
  const uint32_t t = w.a.Alloc(6 + uint32_t(table.size()) * isize);
  Wr16(t + 4, uint16_t(numKeys));
  for (size_t i = 0; i < table.size(); i++) {
    if (isize == 1)
      Wr8(t + 6 + uint32_t(i), uint8_t(table[i]));
    else
      Wr16(t + 6 + uint32_t(i) * 2, uint16_t(table[i]));
  }
  const uint32_t keys = w.a.Alloc((numKeys + 1) * stride);
  for (uint32_t k = 0; k <= numKeys; k++)
    for (uint32_t i = 0; i < stride / (trans ? 4 : 2); i++)
      value(keys + k * stride + i * (trans ? 4 : 2), 0);
  Wr(t, keys);
  return t;
}

// An xanim of numParts parts.
uint32_t XAnim(std::mt19937& g, World& w, uint32_t numParts, uint32_t special) {
  const uint32_t r = g() % 10;
  const uint32_t numFrames = r == 0 ? 0 : r < 7 ? 1 + g() % 255 : 256 + g() % 745;
  const uint32_t anim = w.a.Alloc(8), data = w.a.Alloc(0x20);
  Wr(anim, 0);
  Wr(anim + 4, data);
  for (uint32_t i = 0; i < 8; i++) Wr(data + i * 4, g());
  Wr16(data, uint16_t(numFrames));
  const uint32_t names = w.a.Alloc(2 + numParts * 2);
  g_mem.write<int16_t>(names, int16_t(numParts));
  Wr(data + 0xC, names);
  const uint32_t tracks = w.a.Alloc(numParts * 8), simple = w.a.Alloc((numParts + 7) / 8);
  Wr(data + 0x10, tracks);
  Wr(data + 0x1C, simple);
  for (uint32_t i = 0; i < (numParts + 7) / 8; i++) Wr8(simple + i, uint8_t(g() & g()));
  for (uint32_t p = 0; p < numParts; p++) {
    const bool z = (g_mem.read<uint8_t>(simple + p / 8) >> (p % 8)) & 1;
    Wr(tracks + p * 8 + 4, z && g() % 5 == 0 ? 0 : Track(g, w, numFrames, z ? 4 : 8, false, special));
    Wr(tracks + p * 8, g() % 3 == 0 ? 0 : Track(g, w, numFrames, 12, true, special));
  }
  // For the update: loops or not, its frequency, its notes (u16 name, u16,
  // float time; in order, the last at 1, then a zero name; past that, notes
  // at 2 stop the original's walk of every other note).
  Wr8(data + 2, uint8_t(g() % 2));
  WrF(data + 8, g() % 10 == 0 ? 0.0f
                : g_grid || g() % 5 == 0 ? Exact(g, {0x40800000, 0x41000000, 0x41800000, 0x3F000000})
                               : NonNegative(g, 0, 40, special ? 32 : 0));
  const uint32_t numNotes = 1 + g() % 4;
  std::vector<float> times;
  // In a grid world now and then on the grid, or at 0.001 (half a step of
  // 0.002, which the game's update can cut at dt exactly).
  for (uint32_t i = 1; i < numNotes; i++)
    times.push_back(g() % 6 == 0                ? 0.0f
                    : g_grid && g() % 2 ? Exact(g, {kMilliBits, 0x3E800000, 0x3F000000, 0x3F400000})
                                        : Float(g, 0, 1, 0));
  std::sort(times.begin(), times.end());
  if (times.size() > 1 && g() % 8 == 0) times[1] = times[0];
  times.push_back(1.0f);
  const uint32_t notes = w.a.Alloc((numNotes + 5) * 8);
  Wr(data + 0x14, notes);
  for (uint32_t i = 0; i < numNotes + 5; i++) {
    const bool real = i < numNotes;
    Wr16(notes + i * 8, real ? uint16_t(1 + g() % 0xFFFF) : 0);
    Wr16(notes + i * 8 + 2, uint16_t(g()));
    WrF(notes + i * 8 + 4, real ? times[i] : i == numNotes ? Float(g, 0, 1, 0) : 2.0f);
  }
  return anim;
}

// A string-table entry made by the case (a map or part duplicates).
uint32_t Entry(uint32_t bytes) { return NewEntry(bytes); }

// update: a case for the update (no weights that are negative or huge, which
// can make the mean frequency of a synchronised parent huge).
World NewWorld(std::mt19937& g, uint32_t special, bool update) {
  World w;
  g_grid = update && g() % 4 == 0;
  g_lively = update && g() % 2;
  g_stubs.next = 1;
  Wr(kStrings, kStringTable);

  // Models.
  const uint32_t numModels = g() % 8 == 0 ? 5 + g() % 2 : 1 + g() % 4;
  struct Model {
    uint32_t va, first, numBones;
  };
  std::vector<Model> models;
  uint32_t n = 0;
  for (uint32_t k = 0; k < numModels && n < 127; k++) {
    uint32_t nb = 1 + g() % (g() % 4 == 0 ? 60 : 24);
    nb = std::min(nb, 127 - n);
    const uint32_t numRoot = std::min<uint32_t>(nb, 1 + g() % 3);
    const uint32_t model = w.a.Alloc(8), a1 = w.a.Alloc(4), b1 = w.a.Alloc(8),
                   parts = w.a.Alloc(0x14), bones = w.a.Alloc(4 + nb - numRoot),
                   names = w.a.Alloc(2 + nb * 2), quats = w.a.Alloc((nb - numRoot) * 8),
                   trans = w.a.Alloc((nb - numRoot) * 12);
    Wr(model, 0);
    Wr(model + 4, a1);
    Wr(a1, b1);
    Wr(b1, g());
    Wr(b1 + 4, parts);
    for (uint32_t i = 0; i < 5; i++) Wr(parts + i * 4, g());
    Wr(parts, bones);
    g_mem.write<int16_t>(parts + 4, int16_t(numRoot));
    Wr(parts + 0xC, quats);
    Wr(parts + 0x10, trans);
    Wr(bones, names);
    g_mem.write<int16_t>(names, int16_t(nb));
    for (uint32_t i = 0; i < nb; i++) g_mem.write<int16_t>(names + 2 + i * 2, int16_t(g()));
    for (uint32_t i = numRoot; i < nb; i++) {
      Wr8(bones + 4 + i - numRoot, uint8_t(1 + g() % i));
      for (uint32_t j = 0; j < 4; j++) g_mem.write<int16_t>(quats + (i - numRoot) * 8 + j * 2, Short(g));
      for (uint32_t j = 0; j < 3; j++) WrF(trans + (i - numRoot) * 12 + j * 4, Wide(g, -32, 32, special));
    }
    models.push_back({model, n, nb});
    n += nb;
  }
  w.numBones = n;

  // The skeleton: bits, matrices and poses from earlier calls.
  const uint32_t skel = w.a.Alloc(0x30 + n * 0x60, 16);
  // Now and then every bone posed, or with its matrix, already.
  const bool posed = g() % 10 == 0, made = g() % 10 == 0;
  for (uint32_t i = 0; i < 4; i++) {
    Wr(skel + i * 4, posed ? 0xFFFFFFFFu : g() % 3 == 0 ? 0 : g() & g() & g());
    Wr(skel + 0x10 + i * 4, g() % 2 ? 0 : g() & g() & g() & g());
    Wr(skel + 0x20 + i * 4, made ? 0xFFFFFFFFu : g() % 3 == 0 ? 0 : g() & g() & g());
  }
  for (uint32_t i = 0; i < n * 0x18; i++) WrF(skel + 0x30 + i * 4, Wide(g, -2, 2, special ? special : 64));

  // The anim tree: anims in breadth-first order, each parent's children
  // in a row.
  const uint32_t numAnims = 1 + g() % 24;
  std::vector<uint32_t> children(numAnims, 0), firstChild(numAnims, 0), depth(numAnims, 0);
  uint32_t next = 1;
  for (uint32_t i = 0; i < numAnims && next < numAnims; i++) {
    if (depth[i] >= 3 || (i > 0 && g() % 3 == 0)) continue;
    const uint32_t c = std::min<uint32_t>(numAnims - next, 1 + g() % 6);
    children[i] = c;
    firstChild[i] = next;
    for (uint32_t j = 0; j < c; j++) depth[next + j] = depth[i] + 1;
    next += c;
  }
  const uint32_t anims = w.a.Alloc(8 + numAnims * 8), tree = w.a.Alloc(8 + numAnims * 2);
  Wr(anims, g());
  Wr(anims + 4, numAnims);
  Wr(tree, anims);
  Wr(tree + 4, g());
  const uint32_t state = g() % 2;
  Wr(kState, state);
  std::vector<uint32_t> pool;
  for (uint32_t i = 1; i < 2000; i++) pool.push_back(i);  // the pool ends before 0xA8F000
  std::shuffle(pool.begin(), pool.end(), g);
  std::vector<uint32_t> leaves;
  for (uint32_t i = 0; i < numAnims; i++)
    if (!children[i]) leaves.push_back(i);
  const uint32_t cache = w.a.Alloc(numAnims * 3 + 1);
  const uint8_t generation = uint8_t(g());
  Wr8(cache + numAnims * 2, generation);
  for (uint32_t i = 0; i < numAnims; i++) {
    const uint32_t e = anims + 8 + i * 8;
    Wr(e, g());
    Wr(e + 4, g());
    Wr16(e, uint16_t(children[i]));
    Wr16(e + 6, uint16_t(firstChild[i]));
    if (children[i] && g() % 3 == 0) Wr8(e + 4, 0);  // not synchronised
    const uint32_t info = i > 0 && g() % 10 == 0 ? 0 : pool[i];
    Wr16(tree + 8 + i * 2, uint16_t(info));
    if (info) {
      w.infos.push_back(info);
      const uint32_t va = kInfo + info * 0x44;
      for (uint32_t j = 0; j < 0x11; j++) Wr(va + j * 4, g());
      // The update's: names, and for both states times, cycles, weights
      // with their goals, rates.
      Wr16(va + 4, g() % 5 < (g_lively ? 1 : 3) ? 0 : uint16_t(g()));
      Wr16(va + 6, g() % 5 < 2 ? 0 : uint16_t(1 + g() % 0xFFFF));
      // The game's: a parent's note child (a leaf) or none, the next note
      // (of at least one note and the end, then padding), now and then -1.
      Wr16(va, g() % 3 == 0 ? 0 : uint16_t(leaves[g() % leaves.size()]));
      Wr16(va + 2, g() % 20 == 0 ? 0xFFFF : uint16_t(g() % 5));
      for (uint32_t k = 0; k < 2; k++) {
        const uint32_t st = va + 0xC + k * 0x1C;
        auto time = [&] {
          const uint32_t t = g() % 10;
          return t == 0 ? 0.0f : t == 1 ? 1.0f : t == 2 ? std::bit_cast<float>(0x3F7FFFFFu)
                                                        : Bounded(g, 0, 1, special ? 16 : 0);
        };
        const bool exact = g_grid || g() % 5 == 0;
        WrF(st, exact ? Exact(g, {0, 0x3E800000, 0x3F000000, 0x3F400000, 0x3F800000}) : time());
        // The game's starts before 0 now and then (its wrap back).
        WrF(st + 4, exact && k && g() % 4 == 0 ? Exact(g, {0xBE800000, 0xBFC00000})
                    : exact                    ? Exact(g, {0, 0x3E800000, 0x3F000000, 0x3F800000})
                                               : time());
        // Cycles now and before, near each other, now and then where int16
        // wraps. The game's state (1) is never behind the time and cycle it
        // starts from, or its notes would fall at negative fractions of a
        // step, which can make the original's step back endless.
        const uint16_t old = g() % 8 == 0 ? uint16_t(0x7FFD + g() % 4) : uint16_t(g());
        uint16_t now = uint16_t(old + g() % 4 - (k ? 0 : 1));
        if (k && int16_t(now) < int16_t(old)) now = old;
        Wr16(st + 0xA, old);
        Wr16(st + 8, now);
        WrF(st + 0xC, exact ? Exact(g, {kMilliBits, kTwoMilliBits, 0x3F800000})
                      : g() % 5 < 2 ? 0.0f : Bounded(g, 0, 0.5f, special ? 16 : 0));
        WrF(st + 0x10, g() % (g_lively ? 10 : 3) == 0 ? 0.0f : Weight(g, special));
        WrF(st + 0x14, exact && g() % 2           ? Exact(g, {kTinyBits})
                       : g() % (g_lively ? 10 : 2) == 0 ? 0.0f
                                                  : Weight(g, special));
        WrF(st + 0x18, exact ? Exact(g, {0x3F000000, 0x3F800000, 0x40000000})
                       : g() % 10 == 0 ? 0.0f : NonNegative(g, 0, 2, special ? 32 : 0));
      }
      const uint32_t r = g() % 10;
      const float weight = r < (g_lively ? 1 : 3) ? 0.0f
                           : update ? Weight(g, special)
                           : r < 9  ? Wide(g, 0, 1, special)
                                    : Float(g, -1, 4, 2);
      const uint32_t t = g() % 10;
      const float time = t == 0 ? 0.0f : t == 1 ? 1.0f : t == 2 ? std::bit_cast<float>(0x3F7FFFFFu)
                                                             : Float(g, 0, 1, 0);
      WrF(va + 0xC + state * 0x1C + 0x14, weight);
      WrF(va + 0xC + state * 0x1C, time);
      // State 1 at the same cycle: its time not before the one it starts from.
      const uint32_t s1 = va + 0xC + 0x1C;
      if (Rd(s1 + 8) >> 16 == (Rd(s1 + 8) & 0xFFFF) && g_mem.read<float>(s1) < g_mem.read<float>(s1 + 4)) {
        const uint32_t t1 = Rd(s1);
        Wr(s1, Rd(s1 + 4));
        Wr(s1 + 4, t1);
      }
    }
    Wr16(cache + i * 2, uint16_t(g()));
    Wr8(cache + numAnims * 2 + 1 + i, uint8_t(g()));
    if (children[i]) continue;
    const uint32_t numParts = 1 + g() % (g() % 4 == 0 ? 60 : 20);
    Wr(e + 4, XAnim(g, w, numParts, special));
    // Its map: none yet, current or stale.
    const uint32_t m = g() % 10;
    if (m < 3) {
      Wr16(cache + i * 2, 0);
      continue;
    }
    const uint32_t h = Entry(16 + numParts);
    const uint32_t map = kStringTable + h * 8 + 4;
    for (int j = 0; j < 4; j++) Wr(map + j * 4, g());
    for (uint32_t p = 0; p < numParts; p++) Wr8(map + 16 + p, uint8_t(g() % n));
    Wr16(cache + i * 2, uint16_t(h));
    Wr8(cache + numAnims * 2 + 1 + i, m < 9 ? generation : uint8_t(generation + 1 + g() % 255));
  }

  // The DObj.
  w.dobj = w.a.Alloc(0x58);
  for (uint32_t i = 0; i < 0x58 / 4; i++) Wr(w.dobj + i * 4, g());
  Wr(w.dobj, g() % 20 == 0 ? 0 : tree);
  Wr(w.dobj + 4, skel);
  Wr(w.dobj + 0xC, cache);
  Wr8(w.dobj + 0x16, uint8_t(models.size()));
  Wr8(w.dobj + 0x17, uint8_t(n));
  Wr16(w.dobj + 0x12, g() % 5 == 0 ? 0 : uint16_t(1 + g() % 0xFFFF));  // the game's notes' target
  for (size_t k = 0; k < models.size(); k++) {
    Wr(w.dobj + 0x18 + uint32_t(k) * 4, models[k].va);
    Wr8(w.dobj + 0x48 + uint32_t(k),
        k == 0 || g() % 4 == 0 ? 0xFF : uint8_t(g() % models[k].first));
  }
  // Part duplicates, or none yet.
  Wr16(w.dobj + 0x14, 0);
  if (g() % 10 < 7) {
    std::vector<uint8_t> pairs;
    uint32_t bits[4] = {};
    for (uint32_t b = 1; b < n; b++)
      if (g() % 5 == 0) {
        pairs.push_back(uint8_t(b + 1));
        pairs.push_back(uint8_t(g() % b + 1));
        if (g() % 4) bits[b >> 5] |= 1u << (b & 31);
      }
    const uint32_t h = Entry(16 + uint32_t(pairs.size()) + 1);
    const uint32_t d = kStringTable + h * 8 + 4;
    for (int j = 0; j < 4; j++) Wr(d + j * 4, bits[j] | (g() % 4 == 0 ? g() & g() : 0));
    for (size_t i = 0; i < pairs.size(); i++) Wr8(d + 16 + uint32_t(i), pairs[i]);
    Wr8(d + 16 + uint32_t(pairs.size()), 0);
    Wr16(w.dobj + 0x14, uint16_t(h));
  }

  // The update's free list (its head in info 0), infos in use, the end note.
  Wr16(kInfo + 0xA, uint16_t(g() % 2000));
  Wr(0xA6D000, g() % 4000);
  Wr16(kEndNote, uint16_t(g()));

  // The mask: all bones, or some.
  w.mask = w.a.Alloc(16);
  for (uint32_t i = 0; i < 4; i++) Wr(w.mask + i * 4, g() % 2 ? 0xFFFFFFFFu : g() | g());
  return w;
}

void RandomCase(std::mt19937& g, uint32_t special, Stats& s) {
  const bool update = g() % 10 < 4;
  World w = NewWorld(g, special, update);
  std::vector<Call> calls;
  std::vector<Window> win{{kArena, w.a.top - kArena}};
  if (update) {
    // cgame's update or the game's (with notes or not; now and then again,
    // as after a step cut short at a note), alone or before the evaluation,
    // by 0-0.2 s.
    auto dt = [&] {
      return std::bit_cast<uint32_t>(
          g() % 10 == 0              ? 0.0f
          : g_grid || g() % 4 == 0 ? Exact(g, {0x3D800000, 0x3E000000, kMilliBits, kTwoMilliBits, 0x30800000})
                                   : NonNegative(g, 0, 0.2f, special ? 16 : 0));
    };
    if (g() % 2) {
      calls.push_back({kUpdate, uint32_t(g()), w.dobj, uint32_t(g()), dt(), uint32_t(g())});
    } else {
      for (uint32_t k = g() % 3 ? 1 : 2; k; k--) {
        const uint32_t notes = g() % 4 == 0 ? 0 : g() % 8 == 0 ? uint32_t(g()) : 1;
        calls.push_back({kGameUpdate, uint32_t(g()), w.dobj, uint32_t(g()), dt(), notes});
      }
    }
    if (g() % 10 < 4) {
      calls.push_back({kCalcAnim, w.dobj, uint32_t(g()), w.mask, uint32_t(g()), uint32_t(g())});
      calls.push_back({kCalcSkel, w.mask, uint32_t(g()), uint32_t(g()), w.dobj, uint32_t(g())});
    }
    win.push_back({kInfo, 2000 * 0x44});  // the infos, info 0 and the free list's head
  } else {
    const uint32_t r = g() % 10;
    if (r < 7) calls.push_back({kCalcAnim, w.dobj, uint32_t(g()), w.mask, uint32_t(g()), uint32_t(g())});
    if (r >= 4) calls.push_back({kCalcSkel, w.mask, uint32_t(g()), uint32_t(g()), w.dobj, uint32_t(g())});
    for (uint32_t info : w.infos) win.push_back({kInfo + info * 0x44, 0x44});
  }
  Run(calls, win, g, s, "random");
}

// ---- captures -----------------------------------------------------------------------

bool ReadRegions(FILE* f, std::vector<Window>* out, std::vector<std::vector<uint8_t>>* bytes = nullptr) {
  uint32_t n;
  if (std::fread(&n, 4, 1, f) != 1) return false;
  for (uint32_t i = 0; i < n; i++) {
    uint32_t r[2];
    if (std::fread(r, 4, 2, f) != 2) return false;
    g_mem.Commit(r[0], r[1], recomp::Prot::RW);  // keeps what is there
    std::vector<uint8_t> b(r[1]);
    if (std::fread(b.data(), 1, r[1], f) != r[1]) return false;
    if (bytes)
      bytes->push_back(std::move(b));
    else
      std::memcpy(g_mem.ptr(r[0]), b.data(), r[1]);
    out->push_back({r[0], r[1]});
  }
  return true;
}

// Counts what a 0x47CD80 call evaluates, walking the tree as it does.
void Describe(uint32_t dobj, Stats& s) {
  s.bones = std::max<int>(s.bones, g_mem.read<uint8_t>(dobj + 0x17));
  const uint32_t tree = Rd(dobj);
  if (!tree) return;
  const uint32_t anims = Rd(tree), state = Rd(kState);
  auto weight = [&](uint32_t node) {
    const uint16_t info = g_mem.read<uint16_t>(tree + 8 + node * 2);
    return info ? g_mem.read<float>(kInfo + info * 0x44u + 0x20 + state * 0x1C) : 0.0f;
  };
  std::vector<uint32_t> work{0};
  while (!work.empty()) {
    const uint32_t node = work.back(), e = anims + 8 + node * 8;
    work.pop_back();
    const uint32_t children = g_mem.read<uint16_t>(e);
    if (!children) {
      const uint16_t info = g_mem.read<uint16_t>(tree + 8 + node * 2);
      const float time = g_mem.read<float>(kInfo + info * 0x44u + 0xC + state * 0x1C);
      s.leaves++;
      s.interpolated += time != 1.0f && g_mem.read<uint16_t>(Rd(Rd(e + 4) + 4));
      continue;
    }
    int n = 0;
    for (uint32_t c = 0; c < children; c++) {
      const uint32_t child = g_mem.read<uint16_t>(e + 6) + c;
      if (!(weight(child) == 0.0f)) {
        work.push_back(child);
        n++;
      }
    }
    s.blends += n > 1;
  }
}

bool RunCaptures(const char* path, std::mt19937& g, Stats& s) {
  FILE* f = std::fopen(path, "rb");
  if (!f) {
    std::printf("%s: cannot open\n", path);
    return false;
  }
  int calls = 0;
  uint32_t tag;
  while (std::fread(&tag, 4, 1, f) == 1) {
    if (tag != kCall) {
      std::printf("%s: bad record after %d calls\n", path, calls);
      return false;
    }
    uint32_t head[7];
    if (std::fread(head, 4, 7, f) != 7) return false;
    std::deque<Served> served(head[6]);
    for (Served& e : served) {
      uint32_t ev[8];
      if (std::fread(ev, 4, 8, f) != 8) return false;
      e.in = {ev[0], ev[1], ev[2], ev[3], ev[4], ev[5], ev[6]};
      e.eax = ev[7];
      std::vector<Window> w;
      std::vector<std::vector<uint8_t>> bytes;
      if (!ReadRegions(f, &w, &bytes)) return false;
      for (size_t i = 0; i < w.size(); i++) e.writes.push_back({w[i], std::move(bytes[i])});
    }
    std::vector<Window> w;
    if (!ReadRegions(f, &w)) return false;
    const Call k{head[0], head[1], head[2], head[3], head[4], head[5]};
    if (k.fn == kCalcAnim) Describe(k.eax, s);
    // The string table's entries the events write are compared too.
    std::vector<Window> all = w;
    for (const Served& e : served)
      for (const auto& [x, b] : e.writes) all.push_back(x);
    Run({k}, all, g, s, path, &served);
    calls++;
    // The lifted original, from the record, gives the game's result: the
    // record holds all the call reads.
    std::vector<Window> after;
    std::vector<std::vector<uint8_t>> game;
    if (std::fread(&tag, 4, 1, f) != 1 || tag != kResult || !ReadRegions(f, &after, &game))
      return false;
    s.game++;
    bool same = after.size() == w.size();
    for (size_t i = 0; same && i < after.size(); i++)
      same = after[i].va == w[i].va && after[i].size == w[i].size &&
             !std::memcmp(game[i].data(), g_mem.ptr(w[i].va), w[i].size);
    if (!same && s.game_differs++ < 10)
      std::printf("%s: call %d: the lifted result differs from the game's\n", path, calls);
  }
  std::printf("%s: %d captured calls, %d results not the game's; so far %d blends, %d leaves "
              "(%d interpolated), up to %d bones\n",
              path, calls, s.game_differs, s.blends, s.leaves, s.interpolated, s.bones);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);  // messages before an abort
  if (argc < 2) {
    std::printf("usage: %s CoDSP.exe [--cases N] [--seed S] [CAPTURE...]\n", argv[0]);
    return 2;
  }
  if (!g_mem.Reserve()) return 1;
  recomp::g_guest_base = g_mem.base();
  recomp::PeImage exe;
  std::string err;
  if (!exe.Load(argv[1], g_mem, &err)) {
    std::printf("%s: %s\n", argv[1], err.c_str());
    return 1;
  }
  g_mem.Commit(kArena, kArenaSize, recomp::Prot::RW);
  g_mem.Commit(kStack, kStackSize, recomp::Prot::RW);
  // The reference: the x87 model's exact arithmetic throughout.
  recomp::x87::run::g_doubles = false;

  int cases = 20000, seed = 20261019, first_capture = 2;
  for (; first_capture + 1 < argc; first_capture += 2) {
    if (!std::strcmp(argv[first_capture], "--cases"))
      cases = std::atoi(argv[first_capture + 1]);
    else if (!std::strcmp(argv[first_capture], "--seed"))
      seed = std::atoi(argv[first_capture + 1]);
    else
      break;
  }
  std::mt19937 g(seed);
  Stats s;
  for (int i = 0; i < cases; i++) {
    // Ordinary values, then more and more special ones.
    const uint32_t special = i < cases / 3 ? 0 : i < cases * 2 / 3 ? 64 : 8;
    RandomCase(g, special, s);
  }
  const int random = s.cases;
  for (int i = first_capture; i < argc; i++)
    if (!RunCaptures(argv[i], g, s)) return 1;
  std::printf("%d random + %d captured cases (0x47CD80 %d, 0x4766E0 %d, 0x47CC90 %d, 0x47CCD0 %d, "
              "several %d; maps made %d, released %d, part duplicates %d, names released %d, "
              "notes queued %d; the game's: notes sent %d, cycle ends %d, steps cut short %d): "
              "%d mismatched\n",
              random, s.cases - random, s.by_fn[0], s.by_fn[1], s.by_fn[2], s.by_fn[3], s.by_fn[4],
              s.made, s.freed, s.dups, s.names, s.notes, s.sent, s.ends, s.partial, s.failures);
  return s.failures || s.game_differs ? 1 : 0;
}
