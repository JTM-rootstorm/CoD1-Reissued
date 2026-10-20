// Dev build only (-DCOD1_XANIM_CAPTURE=ON): records XAnim evaluation calls
// for tests/xanim_test.cpp. Linked with --wrap for the four entry points
// (src/xanim.cpp) and the lifted functions they call, so every call
// reaches the wrappers below, which append records
// (tests/xanim_capture.h) to the file named by COD1_XANIM_CAPTURE and call
// the real function. One call in COD1_XANIM_EVERY (default 20) of each
// function is recorded, at most COD1_XANIM_MAX (5000) in all.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

#include "runtime/lifted.h"
#include "xanim_capture.h"

namespace {

using namespace xanim_capture;
using recomp::Ctx;
using recomp::g_guest_base;

constexpr uint32_t kInfo = 0xA6D010;

uint32_t Rd(uint32_t va) {
  uint32_t v;
  std::memcpy(&v, g_guest_base + va, 4);
  return v;
}
uint16_t Rd16(uint32_t va) {
  uint16_t v;
  std::memcpy(&v, g_guest_base + va, 2);
  return v;
}
uint8_t Rd8(uint32_t va) { return g_guest_base[va]; }

struct Regions {
  std::map<uint32_t, uint32_t> r;  // va -> size
  void Add(uint32_t va, uint32_t size) {
    if (va && size) r[va] = std::max(r[va], size);
  }
  std::vector<std::pair<uint32_t, uint32_t>> Merged() const {
    std::vector<std::pair<uint32_t, uint32_t>> merged;
    for (auto [va, size] : r) {
      if (!merged.empty() && va <= merged.back().first + merged.back().second) {
        auto& last = merged.back();
        last.second = std::max(last.second, va + size - last.first);
        continue;
      }
      merged.push_back({va, size});
    }
    return merged;
  }
  // The regions' bytes as they are now.
  void Write(std::vector<uint8_t>& out) const {
    auto put = [&](const void* p, size_t n) {
      out.insert(out.end(), static_cast<const uint8_t*>(p), static_cast<const uint8_t*>(p) + n);
    };
    const auto merged = Merged();
    const uint32_t n = uint32_t(merged.size());
    put(&n, 4);
    for (auto [va, size] : merged) {
      const uint32_t h[2] = {va, size};
      put(h, 8);
      put(g_guest_base + va, size);
    }
  }
};

// The models' parts (src/xanim.cpp).
void AddModels(Regions& m, uint32_t dobj) {
  for (uint32_t k = 0; k < Rd8(dobj + 0x16); k++) {
    const uint32_t model = Rd(dobj + 0x18 + k * 4);
    m.Add(model, 8);
    m.Add(Rd(model + 4), 4);
    m.Add(Rd(Rd(model + 4)), 8);
    const uint32_t parts = Rd(Rd(Rd(model + 4)) + 4);
    m.Add(parts, 0x14);
    const uint32_t bones = Rd(parts);
    const int32_t numBones = int16_t(Rd16(Rd(bones)));
    const int32_t nonRoot = std::max(0, numBones - int16_t(Rd16(parts + 4)));
    m.Add(bones, 4 + nonRoot);
    m.Add(Rd(bones), 2);
    m.Add(Rd(parts + 0xC), nonRoot * 8);
    m.Add(Rd(parts + 0x10), nonRoot * 12);
  }
}

// Part duplicates: 128 bits, pairs up to a zero.
void AddDuplicates(Regions& m, uint32_t d) {
  uint32_t n = 16;
  while (Rd8(d + n)) n += 2;
  m.Add(d, n + 1);
}

uint32_t Entry(uint16_t h) { return Rd(kStrings) + h * 8u + 4; }
uint32_t NumParts(uint32_t anim) { return uint32_t(int16_t(Rd16(Rd(Rd(anim + 4) + 0xC)))); }

void AddTrack(Regions& m, uint32_t t, uint32_t numFrames, uint32_t stride) {
  if (!t) return;
  const uint32_t numKeys = Rd16(t + 4);
  if (!numKeys) {
    m.Add(t, 0x10);
    return;
  }
  m.Add(t, 6 + (numKeys + 1) * (numFrames < 0x100 ? 1 : 2));
  m.Add(Rd(t), (numKeys + 1) * stride);
}

// The anim tree as the call walks it: its anims and their infos, and below
// each anim of nonzero weight its children, the leaves' xanims and maps.
void AddTree(Regions& m, uint32_t dobj) {
  const uint32_t tree = Rd(dobj);
  if (!tree) return;
  const uint32_t anims = Rd(tree), count = Rd(anims + 4);
  m.Add(tree, 8 + count * 2);
  m.Add(anims, 8 + count * 8);
  const uint32_t cache = Rd(dobj + 0xC);
  m.Add(cache, count * 3 + 1);
  auto info = [&](uint32_t node) {
    const uint16_t i = Rd16(tree + 8 + node * 2);
    if (i) m.Add(kInfo + i * 0x44u, 0x44);
    return i;
  };
  const uint32_t state = Rd(kGlobals2 + 0x648);
  std::vector<uint32_t> work{0};
  info(0);
  while (!work.empty()) {
    const uint32_t node = work.back(), e = anims + 8 + node * 8;
    work.pop_back();
    if (const uint32_t children = Rd16(e)) {
      for (uint32_t c = 0; c < children; c++) {
        const uint32_t child = Rd16(e + 6) + c;
        const uint16_t i = info(child);
        float w;
        std::memcpy(&w, g_guest_base + kInfo + i * 0x44u + 0x20 + state * 0x1C, 4);
        if (i && !(w == 0.0f)) work.push_back(child);
      }
      continue;
    }
    const uint32_t anim = Rd(e + 4), data = Rd(anim + 4), numParts = NumParts(anim);
    const uint32_t numFrames = Rd16(data);
    m.Add(anim, 8);
    m.Add(data, 0x20);
    m.Add(Rd(data + 0xC), 2);
    m.Add(Rd(data + 0x10), numParts * 8);
    m.Add(Rd(data + 0x1C), (numParts + 7) / 8);
    for (uint32_t p = 0; p < numParts; p++) {
      const bool z = (Rd8(Rd(data + 0x1C) + p / 8) >> (p % 8)) & 1;
      AddTrack(m, Rd(Rd(data + 0x10) + p * 8 + 4), numFrames, z ? 4 : 8);
      AddTrack(m, Rd(Rd(data + 0x10) + p * 8), numFrames, 12);
    }
    if (const uint16_t h = Rd16(cache + node * 2)) m.Add(Entry(h), 16 + numParts);
  }
}

// What an update (cgame's or the game's) reads: the tree's anims, all their infos with info 0 and
// the free list's head, and the leaves' xanims with their notes (past the
// end too: the original reads every other note after a wrap until one is
// late enough).
void AddUpdate(Regions& m, uint32_t dobj) {
  m.Add(kEndNote, 4);
  m.Add(kInfo, 0x44);
  m.Add(kInfo + Rd16(kInfo + 0xA) * 0x44u, 0x44);
  const uint32_t tree = Rd(dobj);
  if (!tree) return;
  const uint32_t anims = Rd(tree), count = Rd(anims + 4);
  m.Add(tree, 8 + count * 2);
  m.Add(anims, 8 + count * 8);
  for (uint32_t i = 0; i < count; i++) {
    if (const uint16_t info = Rd16(tree + 8 + i * 2)) m.Add(kInfo + info * 0x44u, 0x44);
    const uint32_t e = anims + 8 + i * 8;
    if (Rd16(e)) continue;
    const uint32_t anim = Rd(e + 4), data = Rd(anim + 4), notes = Rd(data + 0x14);
    m.Add(anim, 8);
    m.Add(data, 0x20);
    uint32_t n = 0;
    while (Rd16(notes + n)) n += 8;
    m.Add(notes, n + 0x100);
  }
}

// What 0x47CD80 and 0x4766E0 read.
void AddCalc(Regions& r, const Ctx& c, uint32_t fn, uint32_t arg) {
  const uint32_t dobj = fn == kCalcAnim ? c.eax : arg;
  r.Add(fn == kCalcAnim ? c.edx : c.eax, 16);
  r.Add(dobj, 0x58);
  r.Add(Rd(dobj + 4), 0x30 + Rd8(dobj + 0x17) * 0x60u);
  AddModels(r, dobj);
  if (const uint16_t h = Rd16(dobj + 0x14)) AddDuplicates(r, Entry(h));
  if (fn == kCalcAnim) AddTree(r, dobj);
}

// The call being recorded: its record so far, and its events.
struct Recording {
  bool on = false;
  std::vector<uint8_t> head, events;
  uint32_t numEvents = 0;
} g_rec;

FILE* Out() {
  static FILE* out = [] {
    const char* p = std::getenv("COD1_XANIM_CAPTURE");
    return p ? std::fopen(p, "wb") : nullptr;
  }();
  return out;
}

// Starts recording the call (fn, EAX, ECX, EDX, two stack arguments) if it is
// one in COD1_XANIM_EVERY: returns the regions it reads.
bool Start(const Ctx& c, uint32_t fn, Regions& r) {
  static const uint32_t every = [] {
    const char* e = std::getenv("COD1_XANIM_EVERY");
    return e ? uint32_t(std::atoi(e)) : 20u;
  }();
  static const uint32_t max = [] {
    const char* e = std::getenv("COD1_XANIM_MAX");
    return e ? uint32_t(std::atoi(e)) : 5000u;
  }();
  static uint32_t calls[4] = {}, records = 0;  // per function
  const int k = fn == kCalcAnim ? 0 : fn == kCalcSkel ? 1 : fn == kUpdate ? 2 : 3;
  if (!Out() || g_rec.on || calls[k]++ % every || records >= max) return false;
  records++;
  const uint32_t arg = Rd(c.esp + 4), arg2 = Rd(c.esp + 8);
  r.Add(kGlobals1, kGlobals1Size);
  r.Add(kGlobals2, kGlobals2Size);
  r.Add(kStrings, 4);
  if (fn == kUpdate || fn == kGameUpdate) {
    r.Add(c.ecx, 0x58);
    AddUpdate(r, c.ecx);
  } else {
    AddCalc(r, c, fn, arg);
  }
  g_rec = {};
  g_rec.on = true;
  const uint32_t head[7] = {kCall, fn, c.eax, c.ecx, c.edx, arg, arg2};
  g_rec.head.insert(g_rec.head.end(), reinterpret_cast<const uint8_t*>(head),
                    reinterpret_cast<const uint8_t*>(head + 7));
  r.Write(g_rec.head);  // the regions before the call, after the events
  return true;
}

void Finish(const Regions& r) {
  FILE* out = Out();
  const uint32_t tag = kCall;
  std::fwrite(&tag, 4, 1, out);
  std::fwrite(g_rec.head.data() + 4, 1, 24, out);
  std::fwrite(&g_rec.numEvents, 4, 1, out);
  std::fwrite(g_rec.events.data(), 1, g_rec.events.size(), out);
  std::fwrite(g_rec.head.data() + 28, 1, g_rec.head.size() - 28, out);
  std::vector<uint8_t> after;
  r.Write(after);
  const uint32_t result = kResult;
  std::fwrite(&result, 4, 1, out);
  std::fwrite(after.data(), 1, after.size(), out);
  std::fflush(out);
  g_rec.on = false;
}

// Calls of the lifted callees under way: an event is the call's own, not
// one made inside another callee (by script a note runs, say).
int g_depth = 0;

// An event of the call being recorded: the callee's arguments, its EAX and
// the regions it wrote that the call reads back.
void Event(uint32_t fn, uint32_t eax, uint32_t edx, uint32_t esi, uint32_t a0, uint32_t a1,
           uint32_t a2, uint32_t ret, const Regions& wrote) {
  const uint32_t ev[8] = {fn, eax, edx, esi, a0, a1, a2, ret};
  g_rec.events.insert(g_rec.events.end(), reinterpret_cast<const uint8_t*>(ev),
                      reinterpret_cast<const uint8_t*>(ev + 8));
  wrote.Write(g_rec.events);
  g_rec.numEvents++;
}

}  // namespace

namespace recomp::gen {
void __real_f_0047CD80(Ctx& c) asm("__real__ZN6recomp3gen10f_0047CD80ERNS_3CtxE");
void __real_f_004766E0(Ctx& c) asm("__real__ZN6recomp3gen10f_004766E0ERNS_3CtxE");
void __real_f_0047CC90(Ctx& c) asm("__real__ZN6recomp3gen10f_0047CC90ERNS_3CtxE");
void __real_f_0045D530(Ctx& c) asm("__real__ZN6recomp3gen10f_0045D530ERNS_3CtxE");
void __real_f_00478FC0(Ctx& c) asm("__real__ZN6recomp3gen10f_00478FC0ERNS_3CtxE");
void __real_f_0045D580(Ctx& c) asm("__real__ZN6recomp3gen10f_0045D580ERNS_3CtxE");
void __real_f_004761F0(Ctx& c) asm("__real__ZN6recomp3gen10f_004761F0ERNS_3CtxE");
void __real_f_0047CCD0(Ctx& c) asm("__real__ZN6recomp3gen10f_0047CCD0ERNS_3CtxE");
void __real_f_0047B390(Ctx& c) asm("__real__ZN6recomp3gen10f_0047B390ERNS_3CtxE");
void __real_f_0046A7A0(Ctx& c) asm("__real__ZN6recomp3gen10f_0046A7A0ERNS_3CtxE");
void __real_f_004683B0(Ctx& c) asm("__real__ZN6recomp3gen10f_004683B0ERNS_3CtxE");
}  // namespace recomp::gen

extern "C" void __wrap__ZN6recomp3gen10f_0047CD80ERNS_3CtxE(Ctx& c) {
  Regions r;
  const bool rec = Start(c, kCalcAnim, r);
  recomp::gen::__real_f_0047CD80(c);
  if (rec) Finish(r);
}
extern "C" void __wrap__ZN6recomp3gen10f_004766E0ERNS_3CtxE(Ctx& c) {
  Regions r;
  const bool rec = Start(c, kCalcSkel, r);
  recomp::gen::__real_f_004766E0(c);
  if (rec) Finish(r);
}
extern "C" void __wrap__ZN6recomp3gen10f_0047CC90ERNS_3CtxE(Ctx& c) {
  Regions r;
  const bool rec = Start(c, kUpdate, r);
  recomp::gen::__real_f_0047CC90(c);
  if (rec) Finish(r);
}
extern "C" void __wrap__ZN6recomp3gen10f_0045D530ERNS_3CtxE(Ctx& c) {
  const uint32_t name = Rd(c.esp + 4);
  const bool top = g_rec.on && !g_depth;
  g_depth++;
  recomp::gen::__real_f_0045D530(c);
  g_depth--;
  if (top) Event(kFreeName, 0, 0, 0, name, 0, 0, c.eax, Regions{});
}
extern "C" void __wrap__ZN6recomp3gen10f_00478FC0ERNS_3CtxE(Ctx& c) {
  const uint32_t anim = c.eax, a0 = Rd(c.esp + 4), a1 = Rd(c.esp + 8);
  const bool top = g_rec.on && !g_depth;
  g_depth++;
  recomp::gen::__real_f_00478FC0(c);
  g_depth--;
  if (!top) return;
  Regions w;
  w.Add(Entry(uint16_t(c.eax)), 16 + NumParts(anim));
  Event(kMakeMap, anim, 0, 0, a0, a1, 0, c.eax, w);
}
extern "C" void __wrap__ZN6recomp3gen10f_0045D580ERNS_3CtxE(Ctx& c) {
  const uint32_t eax = c.eax, edx = c.edx;
  const bool top = g_rec.on && !g_depth;
  g_depth++;
  recomp::gen::__real_f_0045D580(c);
  g_depth--;
  if (top) Event(kFreeMap, eax, edx, 0, 0, 0, 0, c.eax, Regions{});
}
extern "C" void __wrap__ZN6recomp3gen10f_004761F0ERNS_3CtxE(Ctx& c) {
  const uint32_t dobj = Rd(c.esp + 4);
  const bool top = g_rec.on && !g_depth;
  g_depth++;
  recomp::gen::__real_f_004761F0(c);
  g_depth--;
  if (!top) return;
  Regions w;
  w.Add(dobj + 0x14, 2);
  if (const uint16_t h = Rd16(dobj + 0x14)) AddDuplicates(w, Entry(h));
  Event(kDuplicates, 0, 0, 0, dobj, 0, 0, c.eax, w);
}
extern "C" void __wrap__ZN6recomp3gen10f_0047CCD0ERNS_3CtxE(Ctx& c) {
  Regions r;
  const bool rec = Start(c, kGameUpdate, r);
  recomp::gen::__real_f_0047CCD0(c);
  if (rec) Finish(r);
}
// The game's notes to script. What script does then is not recorded: the
// replay's result differs from the game's if it changed what the update
// reads.
extern "C" void __wrap__ZN6recomp3gen10f_0047B390ERNS_3CtxE(Ctx& c) {
  const uint32_t esi = c.esi, a0 = Rd(c.esp + 4), a1 = Rd(c.esp + 8);
  const bool top = g_rec.on && !g_depth;
  g_depth++;
  recomp::gen::__real_f_0047B390(c);
  g_depth--;
  if (top) Event(kSendNote, 0, 0, esi, a0, a1, 0, c.eax, Regions{});
}
extern "C" void __wrap__ZN6recomp3gen10f_0046A7A0ERNS_3CtxE(Ctx& c) {
  const uint32_t a0 = Rd(c.esp + 4);
  const bool top = g_rec.on && !g_depth;
  g_depth++;
  recomp::gen::__real_f_0046A7A0(c);
  g_depth--;
  if (top) Event(kPushString, 0, 0, 0, a0, 0, 0, c.eax, Regions{});
}
extern "C" void __wrap__ZN6recomp3gen10f_004683B0ERNS_3CtxE(Ctx& c) {
  const uint32_t a0 = Rd(c.esp + 4), a1 = Rd(c.esp + 8), a2 = Rd(c.esp + 12);
  const bool top = g_rec.on && !g_depth;
  g_depth++;
  recomp::gen::__real_f_004683B0(c);
  g_depth--;
  if (top) Event(kScriptCall, 0, 0, 0, a0, a1, a2, c.eax, Regions{});
}
