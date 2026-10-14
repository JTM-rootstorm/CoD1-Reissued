// Footsteps and the bob cycle at any pmove step length. Replacements of
// lifted functions named with `replace` in manifest/gamex86.manifest and
// manifest/cgamex86.manifest; FINDINGS.md "Frame rate", "Sound events
// lost at high fps", 1.
//
// 0x200067C0 (gamex86) and its cgame mirror 0x30005F00 run at the end of
// every pmove step (PmoveSingle 0x20007FD0 / 0x30007680, which has set
// ps.commandTime to the step's end and pml.msec to its length). They
// store pm->xyspeed, compute the bob rate `bobmove` (cycle units per ms)
// and advance ps.bobCycle (+8, 0-255); the footstep function 0x200065C0 /
// 0x30005D00 (old cycle in EAX, new in ECX, EDX 1) then emits a footstep
// each time the cycle crosses a multiple of 128 (+64). The original adds
// `_ftol2(bobCycle + bobmove * msec) & 0xFF`: each step loses the
// fraction, so a step with bobmove * msec < 1 adds nothing (no footsteps
// and no bob prone at msec <= 3, standing and crouched at msec <= 2) and
// every frame rate has its own, slower cadence.
//
// Here bobmove is computed as the original does it, and a step adds
// floor(bobmove * t1) - floor(bobmove * t0), t1 = ps.commandTime and t0 =
// t1 - msec. For a constant rate the steps telescope: the cycle advances
// bobmove cycle units per ms at any step length. When the rate changes
// the error stays below one unit and is unbiased. The server and cgame
// prediction run the same steps with the same times, so they agree.
// The cadence is bobmove exactly (the per-stance constants below at full
// speed), not that of the truncated original at some reference frame
// rate: that has no single value (measured at full speed, standing 0.30
// units/ms at 60 fps, 0.285 at 125 and 0.30 at 333; prone 0.233, 0.212
// and 0.06), and it flattens the differences between the stances'
// constants. Walking footsteps come about 10% sooner than at 60 fps.
//
// bobmove = xyspeed / (ps.speed x direction x move x stance scale) x K:
// - On the ground and xyspeed >= 10. Below that nothing moves, and below
//   1 the cycle is reset to 0.
// - direction: strafe s' = (ps+0x344 - 1) x 0.75 + 1 alone, back ps+0x348
//   alone, forward diagonals (s' + 1) / 2, back diagonals also x (back +
//   1) / 2.
// - move: ps+0x338, or ps+0x334 with pm_flags 0x80.
// - stance: prone ps+0x33C, crouch ps+0x340, or their blend while a
//   stance change runs (fraction from 0x200053C0 / 0x30004B60, first
//   with ps+0x324, ps+0x328, then swapped).
// - K by stance (0x20003460 / 0x30002C10: 1 prone, 2 crouch) and, in
//   brackets, with pm_flags 0x80: prone 0.25 (0.24), crouch 0.34 (0.315),
//   stand 0.335 (0.305), stand with pm_flags 0x40 (backwards) 0.36
//   (0.325).
// - Airborne with pm_flags 0x10 (a ladder), 300 ms after ps+0x64:
//   velocity z / (0.5 x 190.5 x move) x 0.45 (0.35 with pm_flags 0x80),
//   and the footstep function is called on every step (the same advance;
//   not tested). On the ground it is called only with a forward or side
//   move.
// Not at all with pm_type (+4) >= 4 or ps+0x70 & 0xC000 (pm->xyspeed is
// not stored for the first).

#include <bit>
#include <cmath>
#include <cstdint>

#include "runtime/lifted.h"
#include "runtime/runtime.h"

namespace recomp::gen {

void f_20003460(Ctx& c);
void f_200053C0(Ctx& c);
void f_200065C0(Ctx& c);
void f_30002C10(Ctx& c);
void f_30004B60(Ctx& c);
void f_30005D00(Ctx& c);

namespace {

// pmove_t: ps +0, cmd.serverTime +4, cmd.forwardmove +0x18, rightmove
// +0x19 (signed bytes), xyspeed +0x104.
constexpr uint32_t kPmTime = 0x4, kPmForward = 0x18, kPmRight = 0x19, kPmXySpeed = 0x104;
// playerState_t.
constexpr uint32_t kPsType = 0x4, kPsBob = 0x8, kPsFlags = 0xC, kPsVelocity = 0x20,
                   kPsSpeed = 0x44, kPsGround = 0x54, kPsLadderTime = 0x64, kPsNoBob = 0x70,
                   kPsStanceA = 0x324, kPsStanceB = 0x328, kPsMoveSlow = 0x334,
                   kPsMove = 0x338, kPsProne = 0x33C, kPsCrouch = 0x340, kPsStrafe = 0x344,
                   kPsBack = 0x348;
constexpr uint32_t kNoGround = 0x7FF;

struct Module {
  uint32_t pm;    // pmove_t* while Pmove runs
  uint32_t msec;  // pml.msec
  GuestFn stance, blend, footstep;
  // Return addresses of the original's calls.
  uint32_t stance_ret, blend_ret[2];
};

const Module kGame{0x201271D0, 0x20127048, f_20003460, f_200053C0, f_200065C0,
                   0x20006811, {0x20006990, 0x200069E4}};
const Module kCgame{0x300D06B0, 0x300D0528, f_30002C10, f_30004B60, f_30005D00,
                    0x30005F51, {0x300060D0, 0x30006124}};

float F(uint32_t va) { return std::bit_cast<float>(Rd32(va)); }
int8_t S8(uint32_t va) { return int8_t(GPtr(va)[0]); }

void Call(Ctx& c, GuestFn fn, uint32_t ret) {
  c.esp -= 4;
  Wr32(c.esp, ret);
  fn(c);  // pops ret
}

// 1 prone, 2 crouch, else standing (playerstate in EAX).
int32_t Stance(Ctx& c, const Module& m, uint32_t ps) {
  c.eax = ps;
  Call(c, m.stance, m.stance_ret);
  return int32_t(c.eax);
}

// The stance change's blend fraction (ECX, EDX), returned in ST0.
double Blend(Ctx& c, const Module& m, int i, uint32_t a, uint32_t b) {
  c.ecx = a;
  c.edx = b;
  Call(c, m.blend, m.blend_ret[i]);
  const double f = std::bit_cast<double>(x87::StoreF64(c.fpu));
  x87::Pop(c.fpu);
  return f;
}

// The original's arithmetic, in its order (the x87 model computes in
// doubles); constants are its float constants.
double GroundBobmove(Ctx& c, const Module& m, uint32_t pm, uint32_t ps, float xy) {
  const uint32_t flags = Rd32(ps + kPsFlags);
  const bool slow = flags & 0x80;
  const int32_t stance = Stance(c, m, ps);
  const int8_t fwd = S8(pm + kPmForward), right = S8(pm + kPmRight);
  double s = double(int32_t(Rd32(ps + kPsSpeed)));
  const double strafe = (double(F(ps + kPsStrafe)) - 1.0) * double(0.75f) + 1.0;
  if (fwd && right) {
    s = s * (strafe + 1.0) * double(0.5f);
    if (fwd < 0) s = s * (double(F(ps + kPsBack)) + 1.0) * double(0.5f);
  } else if (fwd < 0) {
    s *= double(F(ps + kPsBack));
  } else if (!fwd && right) {
    s *= strafe;
  }
  s *= double(F(ps + (slow ? kPsMoveSlow : kPsMove)));
  const double base = double(float(s));  // stored as a float
  const double prone = double(F(ps + kPsProne)), crouch = double(F(ps + kPsCrouch));
  const uint32_t a = Rd32(ps + kPsStanceA), b = Rd32(ps + kPsStanceB);
  double d, f = Blend(c, m, 0, a, b);
  if (f != 0.0) {
    d = ((1.0 - f) * crouch + f * prone) * base;
  } else if ((f = Blend(c, m, 1, b, a)) != 0.0) {
    d = ((1.0 - f) * prone + f * crouch) * base;
  } else {
    d = stance == 1 ? base * prone : stance == 2 ? base * crouch : base;
  }
  float k;
  if (stance == 1)
    k = slow ? 0.24f : 0.25f;
  else if (stance == 2)
    k = slow ? 0.315f : 0.34f;
  else if (flags & 0x40)
    k = slow ? 0.325f : 0.36f;
  else
    k = slow ? 0.305f : 0.335f;
  return double(xy) / d * double(k);
}

void Footsteps(Ctx& c, const Module& m) {
  const uint32_t pm = Rd32(m.pm), ps = Rd32(pm);
  const auto ret = [&] { c.esp += 4; };
  if (int32_t(Rd32(ps + kPsType)) >= 4) return ret();
  const double vx = F(ps + kPsVelocity), vy = F(ps + kPsVelocity + 4);
  const float xy = float(std::sqrt(vx * vx + vy * vy));
  Wr32(pm + kPmXySpeed, std::bit_cast<uint32_t>(xy));
  if (Rd32(ps + kPsNoBob) & 0xC000) return ret();
  const uint32_t flags = Rd32(ps + kPsFlags);
  const bool ground = Rd32(ps + kPsGround) != kNoGround;
  double bobmove;
  if (!ground) {
    if (!(flags & 0x10)) return ret();
    if (int32_t(Rd32(pm + kPmTime)) - int32_t(Rd32(ps + kPsLadderTime)) < 300) return ret();
    const bool slow = flags & 0x80;
    bobmove = double(F(ps + kPsVelocity + 8)) /
              (double(0.5f) * double(190.5f) * double(F(ps + (slow ? kPsMoveSlow : kPsMove)))) *
              double(slow ? 0.35f : 0.45f);
  } else {
    if (xy < 10.0f) {
      if (xy < 1.0f) Wr32(ps + kPsBob, 0);
      return ret();
    }
    bobmove = GroundBobmove(c, m, pm, ps, xy);
  }
  const int32_t t1 = int32_t(Rd32(ps)), t0 = t1 - int32_t(Rd32(m.msec));
  const uint32_t old = Rd32(ps + kPsBob);
  const double steps = std::floor(bobmove * t1) - std::floor(bobmove * t0);
  const uint32_t now = (old + uint32_t(int32_t(steps))) & 0xFF;
  Wr32(ps + kPsBob, now);
  if (ground && !S8(pm + kPmForward) && !S8(pm + kPmRight)) return ret();
  c.eax = old;
  c.ecx = now;
  c.edx = 1;
  m.footstep(c);  // the original's tail call: returns to our caller
}

}  // namespace

void f_200067C0(Ctx& c) { Footsteps(c, kGame); }
void f_30005F00(Ctx& c) { Footsteps(c, kCgame); }

}  // namespace recomp::gen
