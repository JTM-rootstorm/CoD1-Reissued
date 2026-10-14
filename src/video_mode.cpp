// Native resolution: `r_mode -1` with r_customwidth and r_customheight 0
// is the desktop's size. Replacement of a lifted function named with
// `replace` in manifest/CoDSP.manifest; FINDINGS.md "Widescreen, FOV, 2D
// placement, frame rate".

#include <bit>
#include <cstdint>

#include "runtime/display.h"
#include "runtime/lifted.h"
#include "runtime/runtime.h"

namespace recomp::gen {

namespace {

// cvar_t pointers; the integer is at +0x20.
constexpr uint32_t kCustomWidth = 0x1534444;   // r_customwidth
constexpr uint32_t kCustomHeight = 0x15346EC;  // r_customheight

}  // namespace

void f_004A5610_lifted(Ctx& c);

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

}  // namespace recomp::gen
