// Hor+ field of view on screens wider than 4:3. Replacements of lifted
// functions named with `replace` in manifest/cgamex86.manifest;
// FINDINGS.md "Widescreen, FOV, 2D placement, frame rate".
//
// 0x30025200 returns the horizontal FOV in ST0 (cg_fov clamped to
// [1, 160], moved toward the weapon's adsZoomFov while aiming, 55 in one
// state), which the original takes as is at any aspect: the view setup
// derives the vertical FOV from it and the screen size, so a wider screen
// sees less vertically (Hor-). Here the horizontal FOV is widened so
// that the vertical one is what a 4:3 screen gets at the same cg_fov,
// for hip fire, aiming and the fixed case alike. At 4:3 and narrower
// nothing changes.

#include <bit>
#include <cmath>
#include <cstdint>

#include "runtime/lifted.h"
#include "runtime/runtime.h"

namespace recomp::gen {

namespace {

// cgame's glconfig: vidWidth, vidHeight.
constexpr uint32_t kVidWidth = 0x300FE984;
constexpr uint32_t kVidHeight = 0x300FE988;
// cg_fov's value (vmCvar at 0x301E7B40, float at +8).
constexpr uint32_t kCgFov = 0x301E7B48;
// The zoom sensitivity the view setup stores (fov_x / cg_fov); the
// usercmds carry it and it scales mouse movement.
constexpr uint32_t kZoomSensitivity = 0x301257AC;
// Return address of the view setup's call (in its FOV part, 0x30025310,
// which then stores the zoom sensitivity at 0x300253CE). The sky portal
// 0x300260A0 calls 0x30025200 too and stores no sensitivity.
constexpr uint32_t kViewSetupCall = 0x3002532E;

constexpr double kPi = 3.14159265358979323846;

// Set by f_30025200 when the view setup's call returned a widened FOV:
// the original FOV, for the sensitivity.
bool g_widened = false;
double g_fov = 0;

}  // namespace

void f_30025200_lifted(Ctx& c);
void f_30025C80_lifted(Ctx& c);

// fov' = 2 atan(tan(fov / 2) * A / (4/3)) for a screen aspect A > 4/3.
void f_30025200(Ctx& c) {
  const bool view_setup = Rd32(c.esp) == kViewSetupCall;
  f_30025200_lifted(c);
  const int32_t w = int32_t(Rd32(kVidWidth)), h = int32_t(Rd32(kVidHeight));
  if (w <= 0 || h <= 0 || int64_t(w) * 3 <= int64_t(h) * 4) return;
  const double fov = std::bit_cast<double>(x87::StoreF64(c.fpu));
  const double k = double(w) / double(h) * 0.75;
  const double wide = std::atan(std::tan(fov * kPi / 360.0) * k) * 360.0 / kPi;
  x87::Pop(c.fpu);
  x87::LoadF64(c.fpu, std::bit_cast<uint64_t>(wide));
  if (view_setup) {
    g_widened = true;
    g_fov = fov;
  }
}

// The view setup: with a widened FOV its zoom sensitivity would be
// fov' / cg_fov and the mouse would turn faster than at 4:3 by fov'/fov.
// Store fov / cg_fov, the 4:3 value, instead. (Underwater the original
// adds a small oscillation to fov_x first; that wobble of the
// sensitivity is not kept.)
void f_30025C80(Ctx& c) {
  g_widened = false;
  f_30025C80_lifted(c);
  if (!g_widened) return;
  const float cg_fov = std::bit_cast<float>(Rd32(kCgFov));
  Wr32(kZoomSensitivity, std::bit_cast<uint32_t>(float(g_fov / double(cg_fov))));
}

}  // namespace recomp::gen
