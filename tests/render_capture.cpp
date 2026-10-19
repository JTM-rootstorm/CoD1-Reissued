// Dev build only (-DCOD1_RENDER_CAPTURE=ON): records sampled calls of the
// renderer entry points src/render.cpp replaces, for tests/render_test.cpp.
// Linked with --wrap for each, so every call from lifted code reaches the
// wrappers below, which append a record (tests/render_capture.h) to the
// file named by COD1_RENDER_CAPTURE and call the real function. One call
// in COD1_RENDER_EVERY (default 20) of each entry is recorded, at most
// COD1_RENDER_MAX (default 3000) of each.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

#include "render_capture.h"
#include "runtime/lifted.h"

namespace {

using namespace render_capture;
using recomp::Ctx;
using recomp::g_guest_base;

uint32_t Rd32(uint32_t va) {
  uint32_t v;
  std::memcpy(&v, g_guest_base + va, 4);
  return v;
}
int32_t I32(uint32_t va) { return int32_t(Rd32(va)); }
// A count read from the game, clamped for the capture's sake.
uint32_t Count(uint32_t va, int32_t max) { return uint32_t(std::clamp(I32(va), 0, max)); }

struct Regions {
  std::map<uint32_t, uint32_t> map;  // va -> size
  void Add(uint32_t va, uint32_t size) {
    if (va && size) map[va] = std::max(map[va], size);
  }
  void AddPtr(uint32_t pva, uint32_t size) {
    Add(pva, 4);
    Add(Rd32(pva), size);
  }
};

void CullGlobals(Regions& r, uint32_t planes, int32_t numPlanes) {
  r.Add(planes, uint32_t(std::clamp(numPlanes, 0, 64)) * 0x14);
  r.Add(0x11BEC20, 4);
  r.Add(0x11BEB7C, 8);
  r.Add(Rd32(0x11BEB7C), 0x14);
  r.Add(Rd32(0x11BEB80), 0x14);
  r.Add(0x11BEC10, 8);
  const uint32_t portals = Rd32(0x11BEC14), n = Count(0x11BEC10, 64);
  r.Add(portals, n * 4);
  for (uint32_t k = 0; k < n; k++) {
    const uint32_t p = Rd32(portals + 4 * k);
    r.Add(p, 0x24);
    r.Add(Rd32(p + 0x20), Count(p + 0x1C, 64) * 0x14);
  }
  r.Add(0x1535A28, 4);
  for (uint32_t cvar : {0x15347E0u, 0x15344DCu, 0x153443Cu, 0x1534568u}) r.AddPtr(cvar, 0x24);
  r.Add(0x1549548, 4);
  r.Add(0x153647C, 4);
  r.Add(Rd32(0x153647C) + 0x74, 32 * 0x88);
}

void SurfaceRefs(Regions& r, uint32_t refs, uint32_t n) {
  r.Add(refs, n * 0xC);
  for (uint32_t i = 0; i < n; i++) r.Add(Rd32(refs + i * 0xC + 8), 0x24);
}

void AabbTree(Regions& r, uint32_t node, int depth, int& budget) {
  if (depth > 32 || --budget < 0) return;
  r.Add(node, 0x28);
  SurfaceRefs(r, Rd32(node + 0x18), Count(node + 0x1C, 4096));
  const uint32_t children = Rd32(node + 0x20);
  for (uint32_t j = 0, n = Count(node + 0x24, 64); j < n; j++)
    AabbTree(r, children + j * 0x28, depth + 1, budget);
}

void Write(Ctx& c, uint32_t fn, std::initializer_list<uint32_t> args, Regions& r) {
  static FILE* out = [] {
    const char* p = std::getenv("COD1_RENDER_CAPTURE");
    return p ? std::fopen(p, "wb") : nullptr;
  }();
  // Coalesce overlapping and adjacent regions.
  std::map<uint32_t, uint32_t> merged;
  for (auto [va, size] : r.map) {
    if (!merged.empty()) {
      auto last = std::prev(merged.end());
      if (va <= last->first + last->second) {
        last->second = std::max(last->second, va + size - last->first);
        continue;
      }
    }
    merged[va] = size;
  }
  const uint32_t head[9] = {kMagic, fn, c.eax, c.ecx, c.edx, c.ebx, c.esi, c.edi,
                            uint32_t(args.size())};
  std::fwrite(head, 4, 9, out);
  for (uint32_t a : args) std::fwrite(&a, 4, 1, out);
  const uint32_t n = uint32_t(merged.size());
  std::fwrite(&n, 4, 1, out);
  for (auto [va, size] : merged) {
    const uint32_t h[2] = {va, size};
    std::fwrite(h, 4, 2, out);
    std::fwrite(g_guest_base + va, 1, size, out);
  }
  std::fflush(out);
}

bool Sample(uint32_t fn) {
  static const bool on = std::getenv("COD1_RENDER_CAPTURE") != nullptr;
  static const uint32_t every = [] {
    const char* e = std::getenv("COD1_RENDER_EVERY");
    return e ? uint32_t(std::atoi(e)) : 20u;
  }();
  static const uint32_t max = [] {
    const char* e = std::getenv("COD1_RENDER_MAX");
    return e ? uint32_t(std::atoi(e)) : 3000u;
  }();
  static std::map<uint32_t, std::pair<uint32_t, uint32_t>> counts;  // calls, records
  if (!on) return false;
  auto& [calls, records] = counts[fn];
  if (calls++ % every || records >= max) return false;
  records++;
  return true;
}

uint32_t Arg(const Ctx& c, int n) { return Rd32(c.esp + 4 * n); }

void Capture(Ctx& c, uint32_t fn) {
  if (!Sample(fn)) return;
  Regions r;
  switch (fn) {
    case kCell: {
      const uint32_t cell = Arg(c, 1);
      CullGlobals(r, Arg(c, 2), int32_t(c.ebx));
      r.Add(cell, 0x3C);
      const uint32_t models = Rd32(cell + 0x24), n = std::min(Rd32(cell + 0x28), 4096u);
      r.Add(models, n * 4);
      for (uint32_t i = 0; i < n; i++) {
        const uint32_t m = Rd32(models + 4 * i);
        r.Add(m, 0x24);
        SurfaceRefs(r, Rd32(m + 0x18), std::min(Rd32(m + 0x1C), 4096u));
      }
      int budget = 4096;
      for (uint32_t link = Rd32(cell + 0x38); link && --budget >= 0; link = Rd32(link + 4)) {
        r.Add(link, 8);
        r.Add(Rd32(link) + 0x9C, 0x1C);
      }
      Write(c, fn, {Arg(c, 1), Arg(c, 2)}, r);
      break;
    }
    case kAabbTree: {
      CullGlobals(r, Arg(c, 2), int32_t(Arg(c, 3)));
      int budget = 20000;
      AabbTree(r, Arg(c, 1), 0, budget);
      Write(c, fn, {Arg(c, 1), Arg(c, 2), Arg(c, 3), Arg(c, 4), Arg(c, 5)}, r);
      break;
    }
    case kSurface:
      CullGlobals(r, Arg(c, 3), int32_t(c.eax));
      SurfaceRefs(r, Arg(c, 1), 1);
      Write(c, fn, {Arg(c, 1), Arg(c, 2), Arg(c, 3), Arg(c, 4)}, r);
      break;
    case kClipSky:
      r.Add(Arg(c, 2), (uint32_t(std::clamp(int32_t(Arg(c, 1)), 0, 63)) + 1) * 12);
      r.Add(0x11CD800, 0x60);
      r.Add(0x564478, 0x48);
      r.Add(0x5644C0, 0x48);
      r.Add(0x537E54, 12);
      Write(c, fn, {Arg(c, 1), Arg(c, 2), Arg(c, 3)}, r);
      break;
    case kTexCoords: {
      const uint32_t shader = c.eax;
      r.Add(shader, 0xAC + 8 * 0xC8);
      for (uint32_t s = 0; s < 8; s++) {
        const uint32_t stage = shader + 0xAC + s * 0xC8;
        r.Add(Rd32(stage + 0x14), Count(stage + 0x10, 32) * 0x44);
      }
      r.Add(0x17F0C60, 0x30);
      r.Add(0x11E075C, 4);
      r.Add(0x1549B3C, 4);
      if (Rd32(0x1549B3C)) r.Add(Rd32(0x1549B3C) + 0x70, 12);
      const uint32_t nv = Count(0x17F0C88, 0x4000), stride = Count(0x17F0C60, 16);
      r.Add(0x1630C40, nv * stride * 4);
      r.Add(0x1650C40, nv * 8);
      r.Add(0x1670C40, nv * 8);
      r.Add(0x1698C40, nv * 12);
      r.Add(0x16B0C40, nv * 12);
      r.Add(0x16C8C40, nv * 12);
      r.Add(0x16E8C40, 0x20);
      for (uint32_t s = 0; s < 8; s++) r.Add(0x16E8C60 + s * 0x20000, nv * 12);
      Write(c, fn, {}, r);
      break;
    }
    case kSort:
      r.Add(c.ecx, std::min(c.eax * c.ebx, 0x100000u));
      Write(c, fn, {}, r);
      break;
    case kMatrix:
      r.Add(c.eax, 64);
      r.Add(c.ecx, 64);
      r.Add(c.edx, 64);
      Write(c, fn, {}, r);
      break;
  }
}

}  // namespace

#define COD1_RENDER_WRAP(va)                                                       \
  namespace recomp::gen {                                                          \
  void __real_f_##va(Ctx& c) asm("__real__ZN6recomp3gen10f_" #va "ERNS_3CtxE");    \
  }                                                                                \
  extern "C" void __wrap__ZN6recomp3gen10f_##va##ERNS_3CtxE(Ctx& c) {              \
    Capture(c, 0x##va);                                                            \
    recomp::gen::__real_f_##va(c);                                                 \
  }
COD1_RENDER_WRAP(004D9650)
COD1_RENDER_WRAP(004D8420)
COD1_RENDER_WRAP(004D8280)
COD1_RENDER_WRAP(004DB660)
COD1_RENDER_WRAP(004F5940)
COD1_RENDER_WRAP(004CADC0)
COD1_RENDER_WRAP(004C94D0)
