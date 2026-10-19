// pmove_fixed without rounding command times up. Replacements of lifted
// functions named with `replace` in manifest/gamex86.manifest and
// manifest/cgamex86.manifest; FINDINGS.md "Frame rate", "Sound events
// lost at high fps".
//
// The original pmove_fixed rounds each command's serverTime up to a
// multiple of pmove_msec (ClientThink 0x20012430 on the server, the
// prediction loop 0x3001EF70 in cgame) and runs pmove to it. The server's
// playerstate is then up to pmove_msec - 1 ms ahead of the newest
// command, so on most new snapshots at high frame rates cgame has no
// command to replay, skips its event checks, and the snapshot's player
// events (playsound on the player, damage) are never played.
//
// Here, with pmove_fixed set, the originals see pmove_fixed 0 (no
// rounding) and the native Pmove (src/pmove.cpp) runs only the whole
// pmove_msec steps that fit before the command's time; the rest stays
// pending until a later command. The playerstate never passes the newest
// command, so prediction always replays one and checks events. Steps are
// the same on both sides: they start at ps.commandTime and each uses the
// first command that covers it. A command that completes no step still
// updates the view angles (as the original's fixed path does for every
// command), so aiming follows the mouse every frame. cgame shows the
// predicted origin interpolated between the last two step ends by the
// pending time, so the view moves every frame instead of at 1000 /
// pmove_msec Hz (one step behind the newest command).

#include <bit>
#include <cmath>
#include <cstdint>

#include "pmove_fixed.h"
#include "runtime/lifted.h"
#include "runtime/runtime.h"

namespace recomp::gen {

void f_20012430_lifted(Ctx& c);
void f_3001EF70_lifted(Ctx& c);

namespace {

// playerState_t: commandTime +0, origin +0x14.
constexpr uint32_t kPsOrigin = 0x14;

struct Module {
  uint32_t fixed;  // pmove_fixed vmCvar integer
  uint32_t msec;   // pmove_msec vmCvar integer
};

const Module kGame{0x202BD46C, 0x20294C8C};
const Module kCgame{0x301E6DCC, 0x301034EC};

// Set while the original runs with its pmove_fixed cleared.
bool g_game_fixed = false, g_cg_fixed = false;

int32_t StepMsec(const Module& m) {
  const int32_t v = int32_t(Rd32(m.msec));
  return v < 8 ? 8 : v > 33 ? 33 : v;  // ClientThink's clamp
}

// ---- cgame view interpolation -----------------------------------------------

constexpr uint32_t kPredicted = 0x3011E0B4;  // cg.predictedPlayerState
constexpr uint32_t kCgCmdTime = 0x3007681C;  // cg_pmove.cmd.serverTime

// Origins at recent step ends, by step index.
struct StepEnd {
  int32_t time = INT32_MIN;
  uint32_t origin[3];
};
StepEnd g_ends[64];

// This frame's prediction ran a command (not its no-prediction path).
bool g_replayed = false;

// The true predicted origin while the interpolated one is shown.
bool g_shown = false;
uint32_t g_true_origin[3];

void Interpolate() {
  const int32_t step = StepMsec(kCgame), ct = int32_t(Rd32(kPredicted));
  const int32_t pending = int32_t(Rd32(kCgCmdTime)) - ct;
  if (pending < 0 || pending >= step) return;
  const StepEnd& prev = g_ends[uint32_t(ct - step) / uint32_t(step) & 63];
  if (prev.time != ct - step) return;
  float a[3], b[3];
  double d2 = 0;
  for (int i = 0; i < 3; ++i) {
    g_true_origin[i] = Rd32(kPredicted + kPsOrigin + i * 4);
    a[i] = std::bit_cast<float>(prev.origin[i]);
    b[i] = std::bit_cast<float>(g_true_origin[i]);
    d2 += double(b[i] - a[i]) * (b[i] - a[i]);
  }
  if (d2 > 32.0 * 32.0) return;  // a teleport or respawn, not a step
  const float f = float(pending) / float(step);
  for (int i = 0; i < 3; ++i)
    Wr32(kPredicted + kPsOrigin + i * 4, std::bit_cast<uint32_t>(a[i] + (b[i] - a[i]) * f));
  g_shown = true;
}

struct Restore {
  uint32_t va, v;
  bool* flag;
  ~Restore() {
    Wr32(va, v);
    *flag = false;
  }
};

}  // namespace

// ClientThink: the original sees pmove_fixed 0.
void f_20012430(Ctx& c) {
  const uint32_t fixed = Rd32(kGame.fixed);
  if (!fixed) return f_20012430_lifted(c);
  Restore r{kGame.fixed, fixed, &g_game_fixed};
  Wr32(kGame.fixed, 0);
  g_game_fixed = true;
  f_20012430_lifted(c);
}

// CG_PredictPlayerState-like: the original sees pmove_fixed 0; the shown
// origin is put back first (the original compares against it for
// prediction misses).
void f_3001EF70(Ctx& c) {
  if (g_shown) {
    for (int i = 0; i < 3; ++i) Wr32(kPredicted + kPsOrigin + i * 4, g_true_origin[i]);
    g_shown = false;
  }
  const uint32_t fixed = Rd32(kCgame.fixed);
  if (!fixed) return f_3001EF70_lifted(c);
  {
    Restore r{kCgame.fixed, fixed, &g_cg_fixed};
    Wr32(kCgame.fixed, 0);
    g_cg_fixed = true;
    g_replayed = false;
    f_3001EF70_lifted(c);
  }
  if (g_replayed) Interpolate();
}

bool PmoveFixedOn(bool game) {
  if (game) return g_game_fixed;
  if (g_cg_fixed) g_replayed = true;
  return g_cg_fixed;
}

int32_t PmoveFixedStep(bool game) { return StepMsec(game ? kGame : kCgame); }

void PmoveFixedStepEnd(int32_t time, uint32_t x, uint32_t y, uint32_t z) {
  StepEnd& e = g_ends[uint32_t(time) / uint32_t(StepMsec(kCgame)) & 63];
  e.time = time;
  e.origin[0] = x;
  e.origin[1] = y;
  e.origin[2] = z;
}

}  // namespace recomp::gen
