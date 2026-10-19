// Dev build only (-DCOD1_CM_CAPTURE=ON): records collision traces for
// tests/cm_trace_test.cpp. Linked with --wrap for the six entry points
// (src/cm_trace.cpp), so every call from lifted code reaches the wrappers
// below, which append records (tests/cm_trace_capture.h) to the file named
// by COD1_CM_CAPTURE and call the native function. The clip map is
// written when it changes (its node array moves); one call in
// COD1_CM_EVERY (default 20) is recorded, at most COD1_CM_MAX (20000).

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

#include "cm_trace_capture.h"
#include "runtime/lifted.h"

namespace {

using namespace cm_capture;
using recomp::Ctx;
using recomp::g_guest_base;

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

// The clip map's globals (src/cm_trace.cpp).
constexpr uint32_t kNoCurves = 0x14BD64C, kShaders = 0x14BD6AC, kNodes = 0x14BD6C4,
                   kLeafs = 0x14BD6CC, kLeafBrushes = 0x14BD6D4, kLeafSurfaces = 0x14BD6DC,
                   kNumSubModels = 0x14BD6E0, kCModels = 0x14BD6E4, kBrushes = 0x14BD6EC,
                   kPatches = 0x14BD718, kBoxBrush = 0x14BD73C, kBoxModel = 0x14BD740,
                   kPlayerCurveClip = 0x14C77C0;

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

void AddBrush(Regions& m, uint32_t b) {
  m.Add(b, 0x34);
  const uint32_t shaders = Rd(kShaders);
  for (int i = 0; i < 6; i++) m.Add(shaders + uint32_t(int16_t(Rd16(b + 0x28 + i * 2))) * 0x48, 0x48);
  const uint32_t n = Rd(b + 0x1C), sides = Rd(b + 0x20);
  m.Add(sides, n * 8);
  for (uint32_t i = 0; i < n; i++) {
    m.Add(Rd(sides + i * 8), 0x14);
    m.Add(shaders + Rd(sides + i * 8 + 4) * 0x48, 0x48);
  }
}

void AddSurface(Regions& m, uint32_t p) {
  m.Add(p, 0x2C);
  m.Add(Rd(kShaders) + Rd(p + 4) * 0x48, 0x48);
  if (const uint32_t pc = Rd(p + 0x24)) {
    // Patch collide: planes, facets (0x140 bytes; border planes at +8).
    const uint32_t numFacets = Rd(pc + 8), facets = Rd(pc + 0xC);
    m.Add(pc, 0x10);
    m.Add(facets, numFacets * 0x140);
    uint32_t planes = Rd(pc);
    for (uint32_t f = 0; f < numFacets; f++) {
      const uint32_t fa = facets + f * 0x140;
      planes = std::max(planes, Rd(fa) + 1);
      for (uint32_t b = 0; b < std::min(Rd(fa + 4), 26u); b++)
        planes = std::max(planes, Rd(fa + 8 + b * 4) + 1);
    }
    m.Add(Rd(pc + 4), planes * 0x14);
  }
  if (const uint32_t t = Rd(p + 0x28)) {
    const uint32_t n = Rd16(t);
    m.Add(t, 4 + n * 0x48);
    for (uint32_t i = 0; i < n; i++) {
      const uint32_t tri = t + 4 + i * 0x48;
      for (int j = 0; j < 3; j++) {
        m.Add(Rd(tri + 0x30 + j * 4), 0x10);
        m.Add(Rd(tri + 0x3C + j * 4), 0x38);
      }
    }
  }
}

void AddLeaf(Regions& m, uint32_t leaf) {
  m.Add(leaf, 0x10);
  const uint32_t fb = Rd(leaf + 4), nb = Rd16(leaf + 8);
  const uint32_t fs = Rd16(leaf + 0xA), ns = Rd16(leaf + 0xC);
  m.Add(Rd(kLeafBrushes) + fb * 4, nb * 4);
  for (uint32_t i = 0; i < nb; i++) AddBrush(m, Rd(kBrushes) + Rd(Rd(kLeafBrushes) + (fb + i) * 4) * 0x34);
  m.Add(Rd(kLeafSurfaces) + fs * 4, ns * 4);
  for (uint32_t i = 0; i < ns; i++)
    AddSurface(m, Rd(kPatches) + Rd(Rd(kLeafSurfaces) + (fs + i) * 4) * 0x2C);
}

// Everything a trace can reach from the tree, the models and the cvars.
void WriteMap(FILE* out) {
  Regions m;
  m.Add(kGlobals, kGlobalsSize);
  m.Add(Rd(kNoCurves), 0x40);
  m.Add(kPlayerCurveClip, 4);
  m.Add(Rd(kPlayerCurveClip), 0x40);
  std::vector<int32_t> work{0};
  std::vector<bool> seen;
  while (!work.empty()) {
    const int32_t n = work.back();
    work.pop_back();
    if (n < 0) {
      AddLeaf(m, Rd(kLeafs) + uint32_t(-1 - n) * 16);
      continue;
    }
    if (size_t(n) >= seen.size()) seen.resize(n + 1);
    if (seen[n]) continue;
    seen[n] = true;
    const uint32_t node = Rd(kNodes) + uint32_t(n) * 8;
    m.Add(node, 8);
    m.Add(Rd(node), 0x14);
    work.push_back(int16_t(Rd16(node + 4)));
    work.push_back(int16_t(Rd16(node + 6)));
  }
  for (uint32_t i = 0; i < Rd(kNumSubModels); i++) {
    m.Add(Rd(kCModels) + i * 0x28, 0x28);
    AddLeaf(m, Rd(kCModels) + i * 0x28 + 0x18);
  }
  AddLeaf(m, kBoxModel + 0x18);
  const uint32_t tag = kMap;
  std::fwrite(&tag, 4, 1, out);
  m.Write(out);
}

FILE* g_out = nullptr;
uint32_t g_results = 0;  // of the call being recorded (1: point contents)

void Record(Ctx& c, uint32_t fn) {
  static FILE* out = [] {
    const char* p = std::getenv("COD1_CM_CAPTURE");
    return p ? std::fopen(p, "wb") : nullptr;
  }();
  static const uint32_t every = [] {
    const char* e = std::getenv("COD1_CM_EVERY");
    return e ? uint32_t(std::atoi(e)) : 20u;
  }();
  static const uint32_t max = [] {
    const char* e = std::getenv("COD1_CM_MAX");
    return e ? uint32_t(std::atoi(e)) : 20000u;
  }();
  static uint32_t calls = 0, records = 0, nodes = 0;
  g_results = 0;
  if (!out || calls++ % every || records >= max) return;
  g_out = out;
  g_results = fn == kPointContents || fn == kTransformedPointContents ? 1
              : fn == kBoxTrace || fn == kTransformedBoxTrace          ? c.eax
                                                                       : Rd(c.esp + 4);
  if (Rd(kNodes) != nodes) {
    nodes = Rd(kNodes);
    WriteMap(out);
  }
  records++;
  uint32_t args[kArgs];
  for (int i = 0; i < kArgs; i++) args[i] = Rd(c.esp + 4 + i * 4);
  Regions r;
  r.Add(kGlobals, kGlobalsSize);
  AddBrush(r, Rd(kBoxBrush));
  auto vec = [&](uint32_t va) { r.Add(va, 12); };
  if (fn == kTrace) {
    r.Add(c.eax, 0x24);
    vec(c.edx);
    vec(c.ecx);
    r.Add(args[0], 0x30);
    vec(args[1]);
    vec(args[2]);
    vec(args[4]);
  } else if (fn == kBoxTrace) {
    r.Add(c.eax, 0x30);
    for (int i = 0; i < 4; i++) vec(args[i]);
  } else if (fn == kTransformed) {
    vec(c.eax);
    vec(c.ecx);
    vec(c.edx);
    r.Add(args[0], 0x30);
    vec(args[1]);
    vec(args[2]);
    vec(args[5]);
  } else if (fn == kTransformedBoxTrace) {
    r.Add(c.eax, 0x30);
    vec(c.edx);
    for (int i = 0; i < 4; i++) vec(args[i]);
    vec(args[6]);
  } else {
    vec(c.ecx);
    if (fn == kTransformedPointContents) {
      vec(c.eax);
      vec(c.edx);
    }
  }
  const uint32_t head[5] = {kCall, fn, c.eax, c.ecx, c.edx};
  std::fwrite(head, 4, 5, out);
  std::fwrite(args, 4, kArgs, out);
  r.Write(out);
}

// The game's result of the recorded call.
void Result(const Ctx& c) {
  if (!g_results) return;
  const uint32_t tag = kResult;
  std::fwrite(&tag, 4, 1, g_out);
  if (g_results == 1) {
    const uint32_t contents[12] = {c.eax};
    std::fwrite(contents, 4, 12, g_out);
  } else {
    std::fwrite(g_guest_base + g_results, 1, 0x30, g_out);
  }
  std::fflush(g_out);
  g_results = 0;
}

}  // namespace

namespace recomp::gen {
void __real_f_0041CD60(Ctx& c) asm("__real__ZN6recomp3gen10f_0041CD60ERNS_3CtxE");
void __real_f_0041D380(Ctx& c) asm("__real__ZN6recomp3gen10f_0041D380ERNS_3CtxE");
void __real_f_0041D3B0(Ctx& c) asm("__real__ZN6recomp3gen10f_0041D3B0ERNS_3CtxE");
void __real_f_0041D870(Ctx& c) asm("__real__ZN6recomp3gen10f_0041D870ERNS_3CtxE");
void __real_f_0041A730(Ctx& c) asm("__real__ZN6recomp3gen10f_0041A730ERNS_3CtxE");
void __real_f_0041A820(Ctx& c) asm("__real__ZN6recomp3gen10f_0041A820ERNS_3CtxE");
}  // namespace recomp::gen

extern "C" void __wrap__ZN6recomp3gen10f_0041CD60ERNS_3CtxE(Ctx& c) {
  Record(c, kTrace);
  recomp::gen::__real_f_0041CD60(c);
  Result(c);
}
extern "C" void __wrap__ZN6recomp3gen10f_0041D380ERNS_3CtxE(Ctx& c) {
  Record(c, kBoxTrace);
  recomp::gen::__real_f_0041D380(c);
  Result(c);
}
extern "C" void __wrap__ZN6recomp3gen10f_0041D3B0ERNS_3CtxE(Ctx& c) {
  Record(c, kTransformed);
  recomp::gen::__real_f_0041D3B0(c);
  Result(c);
}
extern "C" void __wrap__ZN6recomp3gen10f_0041D870ERNS_3CtxE(Ctx& c) {
  Record(c, kTransformedBoxTrace);
  recomp::gen::__real_f_0041D870(c);
  Result(c);
}
extern "C" void __wrap__ZN6recomp3gen10f_0041A730ERNS_3CtxE(Ctx& c) {
  Record(c, kPointContents);
  recomp::gen::__real_f_0041A730(c);
  Result(c);
}
extern "C" void __wrap__ZN6recomp3gen10f_0041A820ERNS_3CtxE(Ctx& c) {
  Record(c, kTransformedPointContents);
  recomp::gen::__real_f_0041A820(c);
  Result(c);
}
