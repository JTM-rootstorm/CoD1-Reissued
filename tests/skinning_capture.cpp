// Dev build only (-DCOD1_SKINNING_CAPTURE=ON): records sampled calls of
// the CPU skinning functions for tests/skinning_test.cpp. Linked with
// --wrap for both functions, so every call reaches the wrappers below,
// which append a record (tests/skinning_capture.h) to the file named by
// COD1_SKINNING_CAPTURE and call the real function. One call in
// COD1_SKINNING_EVERY (default 50) is recorded, at most 3000.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

#include "runtime/lifted.h"
#include "skinning_capture.h"

namespace {

using recomp::Ctx;
using recomp::g_guest_base;

uint32_t Rd32(uint32_t va) {
  uint32_t v;
  std::memcpy(&v, g_guest_base + va, 4);
  return v;
}
int32_t RdS16(uint32_t va) {
  int16_t v;
  std::memcpy(&v, g_guest_base + va, 2);
  return v;
}

void Record(Ctx& c, uint32_t fn) {
  static FILE* out = [] {
    const char* p = std::getenv("COD1_SKINNING_CAPTURE");
    return p ? std::fopen(p, "wb") : nullptr;
  }();
  static const uint32_t every = [] {
    const char* e = std::getenv("COD1_SKINNING_EVERY");
    return e ? uint32_t(std::atoi(e)) : 50u;
  }();
  static uint32_t calls = 0, records = 0;
  if (!out || calls++ % every || records >= 3000) return;
  records++;

  std::map<uint32_t, uint32_t> regions;  // va -> size (merged on write)
  auto add = [&](uint32_t va, uint32_t size) {
    if (size) regions[va] = std::max(regions[va], size);
  };
  const uint32_t arg = Rd32(c.esp + 4);
  const uint32_t model = Rd32(arg + 4), surf = Rd32(arg + 8);
  const int32_t verts = RdS16(surf + 2), tris = RdS16(surf + 4);
  add(arg, 0x10);
  add(model + 4, 4);
  add(Rd32(arg + 0xC) + model + 0x50, 1);
  add(surf, 0x28);
  add(0x17F0C84, 8);
  add(Rd32(surf + 0x1C), uint32_t(tris) * 6);
  add(Rd32(surf + 0x24), uint32_t(verts) * 8);
  const uint32_t bones = Rd32(model + 4) + 0x30 +
                         (uint32_t(g_guest_base[Rd32(arg + 0xC) + model + 0x50]) << 6);
  uint32_t vert = Rd32(surf + 0x20);
  if (fn == skinning::kRigid) {
    add(vert, uint32_t(verts) * 0x18);
    add(bones + uint32_t(RdS16(surf + 6)), 64);
  } else {
    uint32_t extra = Rd32(surf + 0x18);
    for (int32_t i = 0; i < verts; i++) {
      const uint32_t count = Rd32(vert + 0xC);
      add(vert, count ? 0x24 : 0x20);
      add(bones + Rd32(vert + 0x1C), 64);
      for (uint32_t k = 0; k < count; k++, extra += 0x14) {
        add(extra, 0x14);
        add(bones + Rd32(extra + 0xC), 64);
      }
      vert += count ? 0x24 : 0x20;
    }
  }
  // Coalesce overlapping and adjacent regions.
  std::map<uint32_t, uint32_t> merged;
  for (auto [va, size] : regions) {
    if (!merged.empty()) {
      auto last = std::prev(merged.end());
      if (va <= last->first + last->second) {
        last->second = std::max(last->second, va + size - last->first);
        continue;
      }
    }
    merged[va] = size;
  }
  const uint32_t head[4] = {skinning::kMagic, fn, arg, uint32_t(merged.size())};
  std::fwrite(head, 4, 4, out);
  for (auto [va, size] : merged) {
    const uint32_t r[2] = {va, size};
    std::fwrite(r, 4, 2, out);
    std::fwrite(g_guest_base + va, 1, size, out);
  }
  std::fflush(out);
}

}  // namespace

namespace recomp::gen {
void __real_f_005070E0(Ctx& c) asm("__real__ZN6recomp3gen10f_005070E0ERNS_3CtxE");
void __real_f_00507D20(Ctx& c) asm("__real__ZN6recomp3gen10f_00507D20ERNS_3CtxE");
}  // namespace recomp::gen

extern "C" void __wrap__ZN6recomp3gen10f_005070E0ERNS_3CtxE(Ctx& c) {
  Record(c, skinning::kRigid);
  recomp::gen::__real_f_005070E0(c);
}
extern "C" void __wrap__ZN6recomp3gen10f_00507D20ERNS_3CtxE(Ctx& c) {
  Record(c, skinning::kWeighted);
  recomp::gen::__real_f_00507D20(c);
}
