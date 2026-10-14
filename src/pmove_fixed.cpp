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
// Here, with pmove_fixed set, the original sees pmove_fixed 0 (no
// rounding) and Pmove (0x20008580 / 0x30007C30) runs only the whole
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

#include "runtime/lifted.h"
#include "runtime/runtime.h"

namespace recomp::gen {

void f_20012430_lifted(Ctx& c);
void f_20008580_lifted(Ctx& c);
void f_3001EF70_lifted(Ctx& c);
void f_30007C30_lifted(Ctx& c);
void f_30007680_lifted(Ctx& c);
void f_20006FD0(Ctx& c);
void f_30006710(Ctx& c);

namespace {

// pmove_t: ps at +0, cmd (serverTime first) at +4, pmove_fixed +0x108,
// pmove_msec +0x10C, trace function +0x118. playerState_t: commandTime
// +0, pm_flags +0xC, origin +0x14.
constexpr uint32_t kPmCmd = 0x4, kPmFixed = 0x108, kPmMsec = 0x10C, kPmTrace = 0x118;
constexpr uint32_t kPsFlags = 0xC, kPsOrigin = 0x14;

struct Module {
  uint32_t fixed;  // pmove_fixed vmCvar integer
  uint32_t msec;   // pmove_msec vmCvar integer
  GuestFn pmove;   // the lifted Pmove
  void (*view_angles)(Ctx& c, uint32_t ps, uint32_t cmd, uint32_t trace);
};

// Return addresses of the originals' own calls of the view-angle update
// (in PmoveSingle 0x20007FD0 / 0x30007680).
constexpr uint32_t kGameViewRet = 0x20008235, kCgViewRet = 0x300078E4;

void Push(Ctx& c, uint32_t v) {
  c.esp -= 4;
  Wr32(c.esp, v);
}

// 0x20006FD0(cmd, trace), playerstate in EAX; cdecl.
void GameViewAngles(Ctx& c, uint32_t ps, uint32_t cmd, uint32_t trace) {
  Push(c, trace);
  Push(c, cmd);
  Push(c, kGameViewRet);
  c.eax = ps;
  f_20006FD0(c);
  c.esp += 8;
}

// 0x30006710(trace), playerstate in EAX, command in EDI; cdecl.
void CgViewAngles(Ctx& c, uint32_t ps, uint32_t cmd, uint32_t trace) {
  Push(c, trace);
  Push(c, kCgViewRet);
  c.eax = ps;
  c.edi = cmd;
  f_30006710(c);
  c.esp += 4;
}

const Module kGame{0x202BD46C, 0x20294C8C, f_20008580_lifted, GameViewAngles};
const Module kCgame{0x301E6DCC, 0x301034EC, f_30007C30_lifted, CgViewAngles};

// Set while the original runs with its pmove_fixed cleared.
bool g_game_fixed = false, g_cg_fixed = false;

int32_t StepMsec(const Module& m) {
  const int32_t v = int32_t(Rd32(m.msec));
  return v < 8 ? 8 : v > 33 ? 33 : v;  // ClientThink's clamp
}

// Pmove (pm in ESI) in whole steps; the pending rest stays for later.
void FixedPmove(Ctx& c, const Module& m) {
  const uint32_t pm = c.esi, ps = Rd32(pm);
  const int32_t t = int32_t(Rd32(pm + kPmCmd));
  int32_t ct = int32_t(Rd32(ps));
  if (t < ct) return m.pmove(c);  // the original does nothing either
  // The original's catch-up limits, first, so the steps start from the
  // time it would start from.
  if (t - ct > 1000) ct = t - 1000;
  if ((Rd32(ps + kPsFlags) & 0x10000) && t - ct > 50) ct = t - 50;
  Wr32(ps, uint32_t(ct));
  const int32_t step = StepMsec(m);
  const int32_t end = ct + (t - ct) / step * step;
  const uint32_t fixed = Rd32(pm + kPmFixed), msec = Rd32(pm + kPmMsec);
  Wr32(pm + kPmFixed, 1);
  Wr32(pm + kPmMsec, uint32_t(step));
  Wr32(pm + kPmCmd, uint32_t(end));
  m.pmove(c);
  Wr32(pm + kPmCmd, uint32_t(t));
  Wr32(pm + kPmFixed, fixed);
  Wr32(pm + kPmMsec, msec);
  if (end == ct) {
    const uint32_t eax = c.eax, ebx = c.ebx, esi = c.esi, edi = c.edi, ebp = c.ebp;
    m.view_angles(c, ps, pm + kPmCmd, Rd32(pm + kPmTrace));
    c.eax = eax, c.ebx = ebx, c.esi = esi, c.edi = edi, c.ebp = ebp;
  }
}

// ---- cgame view interpolation -----------------------------------------------

constexpr uint32_t kPredicted = 0x3011E0B4;  // cg.predictedPlayerState
constexpr uint32_t kCgPm = 0x300D06B0;       // pmove_t* while Pmove runs
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

void f_20008580(Ctx& c) {
  if (!g_game_fixed) return f_20008580_lifted(c);
  FixedPmove(c, kGame);
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

void f_30007C30(Ctx& c) {
  if (!g_cg_fixed) return f_30007C30_lifted(c);
  g_replayed = true;
  FixedPmove(c, kCgame);
}

// PmoveSingle-like (one step): remembers where each step ended.
void f_30007680(Ctx& c) {
  f_30007680_lifted(c);
  if (!g_cg_fixed) return;
  const uint32_t ps = Rd32(Rd32(kCgPm));
  const int32_t t = int32_t(Rd32(ps));
  StepEnd& e = g_ends[uint32_t(t) / uint32_t(StepMsec(kCgame)) & 63];
  e.time = t;
  for (int i = 0; i < 3; ++i) e.origin[i] = Rd32(ps + kPsOrigin + i * 4);
}

}  // namespace recomp::gen
