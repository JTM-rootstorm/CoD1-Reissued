// Dev build only (-DCOD1_PMOVE_CAPTURE=ON): records Pmove calls for
// tests/pmove_test.cpp. Linked with --wrap for the native Pmove of both
// modules (src/pmove.cpp) and for the trace and point contents functions
// the game and cgame give it (pmove_t trace..pointcontents), so the
// wrappers below append records (tests/pmove_capture.h) to the file named
// by COD1_PMOVE_CAPTURE. One call in COD1_PMOVE_EVERY (default 10) of each
// module is recorded, at most COD1_PMOVE_MAX (20000) in all. The game's calls are recorded
// only when its function after the loop (0x20073F80) returns at once (its
// event interval not over) and the player is not on a turret, whose code
// reads the game's entities.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

#include "pmove_capture.h"
#include "../src/pmove_fixed.h"
#include "runtime/lifted.h"

namespace {

using namespace pmove_capture;
using recomp::Ctx;
using recomp::g_guest_base;

uint32_t Rd(uint32_t va) {
  uint32_t v;
  std::memcpy(&v, g_guest_base + va, 4);
  return v;
}

struct Regions {
  std::map<uint32_t, uint32_t> r;  // va -> size
  void Add(uint32_t va, uint32_t size) {
    if (va && size) r[va] = std::max(r[va], size);
  }
  void Write(FILE* out) const {
    std::vector<std::pair<uint32_t, uint32_t>> merged;
    for (auto [va, size] : r) {
      if (!merged.empty() && va <= merged.back().first + merged.back().second) {
        auto& last = merged.back();
        last.second = std::max(last.second, va + size - last.first);
        continue;
      }
      merged.push_back({va, size});
    }
    const uint32_t n = uint32_t(merged.size());
    std::fwrite(&n, 4, 1, out);
    for (auto [va, size] : merged) {
      const uint32_t h[2] = {va, size};
      std::fwrite(h, 4, 2, out);
      std::fwrite(g_guest_base + va, 1, size, out);
    }
  }
};

FILE* Out() {
  static FILE* out = [] {
    const char* p = std::getenv("COD1_PMOVE_CAPTURE");
    return p ? std::fopen(p, "wb") : nullptr;
  }();
  return out;
}

bool g_recording = false;
uint32_t g_pm = 0;

void WriteWeapons(FILE* out, uint32_t module) {
  static uint32_t table[2], count[2];
  const ModuleData& d = kModuleData[module];
  const uint32_t t = Rd(d.weaponDefs), n = Rd(d.numWeapons);
  if (t == table[module] && n == count[module]) return;
  table[module] = t;
  count[module] = n;
  Regions r;
  r.Add(t, (n + 1) * 4);
  for (uint32_t w = 0; w <= n; w++) r.Add(Rd(t + w * 4), kWeaponDefSize);
  const uint32_t head[2] = {kWeapons, module};
  std::fwrite(head, 4, 2, out);
  r.Write(out);
}

// Whether this call is recorded; if so writes its record.
bool Record(Ctx& c, uint32_t module) {
  static const uint32_t every = [] {
    const char* e = std::getenv("COD1_PMOVE_EVERY");
    return e ? uint32_t(std::atoi(e)) : 10u;
  }();
  static const uint32_t max = [] {
    const char* e = std::getenv("COD1_PMOVE_MAX");
    return e ? uint32_t(std::atoi(e)) : 20000u;
  }();
  static uint32_t calls[2], records = 0;
  FILE* out = Out();
  if (!out || calls[module]++ % every || records >= max) return false;
  const ModuleData& d = kModuleData[module];
  const uint32_t pm = c.esi, ps = Rd(pm);
  if (module == 0) {
    const int32_t time = int32_t(Rd(d.levelTime));
    if (time - int32_t(Rd(d.lastEvent)) > int32_t(Rd(d.eventInterval))) return false;
    if (Rd(ps + 0xC) & 0x400000) return false;
  }
  records++;
  WriteWeapons(out, module);
  Regions r;
  r.Add(pm, kPmSize);
  r.Add(ps, kPsSize);
  r.Add(d.lo, d.hi - d.lo);
  r.Add(d.debugLo, d.debugHi - d.debugLo);
  r.Add(d.clipSizes, kClipSizesSize);
  r.Add(d.noFire, 4);
  r.Add(d.proneDebug, 4);
  r.Add(d.proneDebugTime, 4);
  r.Add(d.levelTime, 4);
  r.Add(d.eventInterval, 4);
  const bool game = module == 0;
  const uint32_t head[5] = {kCall, module, pm, recomp::gen::PmoveFixedOn(game),
                            uint32_t(recomp::gen::PmoveFixedStep(game))};
  std::fwrite(head, 4, 5, out);
  r.Write(out);
  g_recording = true;
  g_pm = pm;
  return true;
}

void Result() {
  FILE* out = Out();
  const uint32_t tag = kResult;
  std::fwrite(&tag, 4, 1, out);
  std::fwrite(g_guest_base + g_pm, 1, kPmSize, out);
  std::fwrite(g_guest_base + Rd(g_pm), 1, kPsSize, out);
  std::fflush(out);
  g_recording = false;
}

void Vec(uint32_t* out, uint32_t va) {
  for (int i = 0; i < 3; i++) out[i] = va ? Rd(va + 4 * i) : 0;
}

// A trace (cdecl: results, start, mins, maxs, end, passEntityNum, mask).
void TraceRecord(uint32_t esp) {
  uint32_t r[16] = {kTrace, 0};
  const uint32_t results = Rd(esp + 4);
  Vec(r + 2, Rd(esp + 8));
  Vec(r + 5, Rd(esp + 20));
  Vec(r + 8, Rd(esp + 12));
  Vec(r + 11, Rd(esp + 16));
  r[14] = Rd(esp + 24);
  r[15] = Rd(esp + 28);
  std::fwrite(r, 4, 16, Out());
  std::fwrite(g_guest_base + results, 1, 0x30, Out());
}

// Point contents (cdecl: point, passEntityNum, mask) -> EAX.
void ContentsRecord(const Ctx& c, uint32_t esp) {
  uint32_t r[16] = {kTrace, 3};
  Vec(r + 2, Rd(esp + 4));
  r[14] = Rd(esp + 8);
  r[15] = Rd(esp + 12);
  std::fwrite(r, 4, 16, Out());
  const uint32_t contents[12] = {c.eax};
  std::fwrite(contents, 4, 12, Out());
}

}  // namespace

namespace recomp::gen {
void __real_f_20008580(Ctx& c) asm("__real__ZN6recomp3gen10f_20008580ERNS_3CtxE");
void __real_f_30007C30(Ctx& c) asm("__real__ZN6recomp3gen10f_30007C30ERNS_3CtxE");
void __real_f_20042B00(Ctx& c) asm("__real__ZN6recomp3gen10f_20042B00ERNS_3CtxE");
void __real_f_20042C70(Ctx& c) asm("__real__ZN6recomp3gen10f_20042C70ERNS_3CtxE");
void __real_f_3001EC30(Ctx& c) asm("__real__ZN6recomp3gen10f_3001EC30ERNS_3CtxE");
void __real_f_3001ECC0(Ctx& c) asm("__real__ZN6recomp3gen10f_3001ECC0ERNS_3CtxE");
}  // namespace recomp::gen

extern "C" void __wrap__ZN6recomp3gen10f_20008580ERNS_3CtxE(Ctx& c) {
  const bool rec = Record(c, 0);
  recomp::gen::__real_f_20008580(c);
  if (rec) Result();
}
extern "C" void __wrap__ZN6recomp3gen10f_30007C30ERNS_3CtxE(Ctx& c) {
  const bool rec = Record(c, 1);
  recomp::gen::__real_f_30007C30(c);
  if (rec) Result();
}
// The game's and cgame's trace and point contents functions for pmove.
extern "C" void __wrap__ZN6recomp3gen10f_20042B00ERNS_3CtxE(Ctx& c) {
  const uint32_t esp = c.esp;
  recomp::gen::__real_f_20042B00(c);
  if (g_recording) TraceRecord(esp);
}
extern "C" void __wrap__ZN6recomp3gen10f_20042C70ERNS_3CtxE(Ctx& c) {
  const uint32_t esp = c.esp;
  recomp::gen::__real_f_20042C70(c);
  if (g_recording) ContentsRecord(c, esp);
}
extern "C" void __wrap__ZN6recomp3gen10f_3001EC30ERNS_3CtxE(Ctx& c) {
  const uint32_t esp = c.esp;
  recomp::gen::__real_f_3001EC30(c);
  if (g_recording) TraceRecord(esp);
}
extern "C" void __wrap__ZN6recomp3gen10f_3001ECC0ERNS_3CtxE(Ctx& c) {
  const uint32_t esp = c.esp;
  recomp::gen::__real_f_3001ECC0(c);
  if (g_recording) ContentsRecord(c, esp);
}
