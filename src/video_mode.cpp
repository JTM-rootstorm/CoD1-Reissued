// Native resolution: `r_mode -1` with r_customwidth and r_customheight 0
// is the desktop's size, and the hunk is made large enough for it.
// Replacements of lifted functions named with `replace` in
// manifest/CoDSP.manifest; FINDINGS.md "Widescreen, FOV, 2D placement,
// frame rate".

#include <bit>
#include <cstdint>
#include <cstdio>

#include "runtime/display.h"
#include "runtime/lifted.h"
#include "runtime/runtime.h"

namespace recomp::gen {

namespace {

// cvar_t pointers; the value (float) is at +0x1C, the integer at +0x20.
constexpr uint32_t kCustomWidth = 0x1534444;   // r_customwidth
constexpr uint32_t kCustomHeight = 0x15346EC;  // r_customheight

// Com_InitHunkMemory-like 0x42A980: Cvar_Get("com_hunkMegs", "128",
// CVAR_ARCHIVE | CVAR_LATCH), returning to 0x42A9A9.
constexpr uint32_t kHunkMegsName = 0x5579EC, kHunkMegsDefault = 0x5579FC;
constexpr uint32_t kCvarGetRet = 0x42A9A9;

uint32_t Pow2(uint32_t v) {
  uint32_t p = 1;
  while (p < v) p <<= 1;
  return p;
}

// The renderer takes a 32-bit texture of the screen rounded up to powers
// of two from the hunk's temporary memory while it starts: 32 MB at
// 2560x1440, 128 MB at 5120x2880, the whole of the default 128 MB hunk
// ("Hunk_AllocateTempMemory: failed on 134217744"). The hunk gets what it
// would have at 2560x1440 on top of that texture: 128 MB up to there, 160
// at 3840x2160, 224 at 5120x2880.
int32_t HunkMegsFor(int w, int h) {
  const uint64_t tex = uint64_t(Pow2(uint32_t(w))) * Pow2(uint32_t(h)) * 4;
  const int32_t tex_mb = int32_t(tex >> 20);
  return 128 + (tex_mb > 32 ? tex_mb - 32 : 0);
}

}  // namespace

void f_004A5610_lifted(Ctx& c);
void f_0042A980_lifted(Ctx& c);
void f_004319F0(Ctx& c);

// Mode (EAX) -> width ([esp+4] points to it), height (EDX), window aspect
// (ECX, float); returns 1, or 0 for a mode that does not exist. Mode -1
// takes r_customwidth/height and r_customaspect; when both sizes are 0
// (not a size the original could use) this is the desktop's, with its
// aspect. Everything else is the original.
void f_004A5610(Ctx& c) {
  if (c.eax != 0xFFFFFFFFu || Rd32(Rd32(kCustomWidth) + 0x20) ||
      Rd32(Rd32(kCustomHeight) + 0x20))
    return f_004A5610_lifted(c);
  int w, h;
  DesktopSize(&w, &h);
  Wr32(Rd32(c.esp + 4), uint32_t(w));
  Wr32(c.edx, uint32_t(h));
  Wr32(c.ecx, std::bit_cast<uint32_t>(float(w) / float(h)));
  c.eax = 1;
  c.esp += 4;  // ret
}

// Hunk setup: com_hunkMegs below what the desktop's size needs is raised
// for this run (value and integer; the string, and so the saved config,
// keep what was set), then the original sizes and allocates the hunk.
void f_0042A980(Ctx& c) {
  const uint32_t esp = c.esp;
  c.esp -= 16;
  Wr32(c.esp + 0, kCvarGetRet);
  Wr32(c.esp + 4, kHunkMegsName);
  Wr32(c.esp + 8, kHunkMegsDefault);
  Wr32(c.esp + 12, 0x21);
  f_004319F0(c);  // pops the return address
  c.esp = esp;
  if (const uint32_t var = c.eax) {
    int w, h;
    DesktopSize(&w, &h);
    const int32_t need = HunkMegsFor(w, h);
    if (int32_t(Rd32(var + 0x20)) < need) {
      Wr32(var + 0x20, uint32_t(need));
      Wr32(var + 0x1C, std::bit_cast<uint32_t>(float(need)));
      std::fprintf(stderr, "com_hunkMegs raised to %d for a %dx%d desktop\n", need, w, h);
    }
  }
  f_0042A980_lifted(c);
}

}  // namespace recomp::gen
