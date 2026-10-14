// Exact frame pacing for com_maxfps. Replacement of a lifted function
// named with `replace` in manifest/CoDSP.manifest; FINDINGS.md
// "Widescreen, FOV, 2D placement, frame rate".
//
// The frame function 0x430690 (Com_Frame-like) limits the rate by
// spinning on its event loop until 1000 / com_maxfps whole milliseconds
// (timeGetTime) have passed since the last frame: a cap burns a core, and
// only 1000/n rates exist (144 gives 166, 240 gives 250). Here the frame
// first sleeps until its start on an exact grid of 1/com_maxfps on the
// host clock (recomp::FramePacer), and the original's own wait is then
// cut to its minimum of 1 ms, which has always passed.

#include <cstdint>

#include "runtime/lifted.h"
#include "runtime/pacing.h"
#include "runtime/runtime.h"

namespace recomp::gen {

namespace {

// cvar_t* com_maxfps; modificationCount at +0x18, integer at +0x20. The
// frame function is the only reader of the integer (the rest of the game
// reads cvars by name, through the value and string).
constexpr uint32_t kComMaxfps = 0x14E55C8;

}  // namespace

void f_00430690_lifted(Ctx& c);

void f_00430690(Ctx& c) {
  static FramePacer pacer;
  const uint32_t cvar = Rd32(kComMaxfps);
  const int32_t fps = cvar ? int32_t(Rd32(cvar + 0x20)) : 0;
  pacer.Wait(fps);
  if (fps <= 0) return f_00430690_lifted(c);
  // The original's wait sees com_maxfps 0 (1 ms) for this frame. A
  // com_maxfps set during the frame (console, menu) is kept.
  const uint32_t mod = Rd32(cvar + 0x18);
  struct Restore {
    uint32_t cvar, mod;
    int32_t fps;
    ~Restore() {
      if (Rd32(cvar + 0x18) == mod) Wr32(cvar + 0x20, uint32_t(fps));
    }
  } restore{cvar, mod, fps};
  Wr32(cvar + 0x20, 0);
  f_00430690_lifted(c);
}

}  // namespace recomp::gen
