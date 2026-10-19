// Player movement (bg_pmove and bg_slidemove), natively: one implementation
// of the code that gamex86.dll and cgamex86.dll each carry, from Pmove
// (0x20008580 / 0x30007C30) down. Replacements of lifted functions named
// with `replace` in manifest/gamex86.manifest and manifest/cgamex86.manifest;
// FINDINGS.md "Player movement". Names and evidence are in the Ghidra plate
// comments.
//
// The two modules' copies are the same code apart from their globals and a
// few game-only additions (debug drawing under a cvar, console prints, a
// weapon event and Pmove's last call), which Module describes. The game
// patches that used to wrap the lifted code are part of it: footsteps at
// any step length (PM_Footsteps) and pmove_fixed's whole steps with the
// remainder pending (Pmove).
//
// The originals are x87 code; the arithmetic is src/x87_value.h's, which
// gives the original's bits at the main thread's control word: a value the
// original keeps on the x87 stack is an St, one it stores is an F32, so
// `St(a) * b + c` is fld a / fmul b / fadd c, and each comparison keeps the
// original's branch for unordered operands. Built with -ffp-contract=off
// (CMakeLists.txt). The CRT's sqrt and atan2 and the trace callbacks are
// called lifted (sqrt only where its error path runs).
//
// Calling conventions of the lifted callees are the originals'. Values the
// originals pass by address (trace results, start and end points) are in
// guest stack frames below the caller's ESP. Not reproduced: EAX, ECX, EDX
// and EFLAGS on return (the callers of Pmove overwrite them), the x87 status
// word, the /GS cookies and the dead stack below ESP.

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <initializer_list>

#include "pmove_fixed.h"
#include "runtime/lifted.h"
#include "x87_value.h"

namespace recomp::gen {

void f_20084540(Ctx& c);  // the CRT's sqrt (ST0)
void f_2008452A(Ctx& c);  // the CRT's atan2 (ST1 y, ST0 x)
void f_2001E5E0(Ctx& c);  // prints (fmt, ...) to the console
void f_2001E580(Ctx& c);  // the error exit (level, fmt, ...): a guest longjmp
void f_30019190(Ctx& c);
void f_20019260(Ctx& c);  // debug box (EAX mins, EDX maxs; color)
void f_20019360(Ctx& c);  // debug circle (EAX normal; center, radius, color, time, ...)
void f_200194F0(Ctx& c);  // debug arc (EAX center; radius, from, to, color, time)
void f_2001DCA0(Ctx& c);  // the game's turret use (EAX ps)
void f_20073F80(Ctx& c);  // the game's record of the player's positions (ps)
void f_3004D690(Ctx& c);
void f_3004D67A(Ctx& c);
void f_300191F0(Ctx& c);

namespace {

using lift::Rd;
using lift::Wr;

template <typename T>
T& At(uint32_t va) {
  return *reinterpret_cast<T*>(g_guest_base + va);
}

constexpr F32 kZero{0};
F32 Fl(float f) { return F32{std::bit_cast<uint32_t>(f)}; }

// ---- guest structures ---------------------------------------------------------

// trace_t
struct Trace {
  F32 fraction;
  F32 endpos[3];
  F32 normal[3];
  int32_t surfaceFlags;
  int32_t contents;
  uint32_t material;
  uint16_t entityNum;
  uint16_t unknown2A;
  uint16_t unknown2C;
  uint8_t allsolid, startsolid;
};
static_assert(sizeof(Trace) == 0x30);

// usercmd_t
struct UserCmd {
  int32_t serverTime;
  uint8_t buttons, wbuttons, weapon, pad7;
  int32_t angles[3];
  int8_t forwardmove, rightmove, upmove, pad17;
  uint8_t unknown18[0x14];
};
static_assert(sizeof(UserCmd) == 0x2C);

// pmove_t
struct Pmove {
  uint32_t ps;
  UserCmd cmd, oldcmd;
  int32_t tracemask;
  int32_t debugLevel;
  int32_t numtouch;
  int32_t touchents[32];
  F32 mins[3], maxs[3];
  uint8_t watertype, waterlevel, pad102[2];
  F32 xyspeed;
  int32_t pmove_fixed, pmove_msec;
  // Traces (results, start, mins, maxs, end, passEntityNum, mask), cdecl:
  // the one of the stance (+0x110, set by PM_CheckDuck to one of the
  // other two), prone (+0x114) and upright (+0x118).
  uint32_t trace, traceProne, traceUpright;
  uint32_t pointcontents;  // +0x11C: (point, passEntityNum, mask) -> contents
};
static_assert(offsetof(Pmove, tracemask) == 0x5C && offsetof(Pmove, mins) == 0xE8 &&
              offsetof(Pmove, watertype) == 0x100 && offsetof(Pmove, trace) == 0x110 &&
              sizeof(Pmove) == 0x120);

// pml_t
struct Pml {
  F32 forward[3], right[3], up[3];
  F32 frametime;
  int32_t msec;
  int32_t walking;
  int32_t groundPlane;
  Trace groundTrace;
  F32 impactSpeed;
  F32 previous_origin[3];
  F32 previous_velocity[3];
  int32_t previous_waterlevel;
  uint32_t weaponDef;
};
static_assert(offsetof(Pml, groundTrace) == 0x34 && offsetof(Pml, previous_origin) == 0x68 &&
              sizeof(Pml) == 0x88);

// playerState_t (the fields pmove uses)
struct PlayerState {
  int32_t commandTime;  // +0x00
  int32_t pm_type;
  int32_t bobCycle;
  uint32_t pm_flags;
  int32_t pm_time;  // +0x10
  F32 origin[3];
  F32 velocity[3];  // +0x20
  int32_t weaponTime;  // +0x2C
  int32_t weaponDelay;
  int32_t grenadeTimeLeft;
  int32_t unknown38;  // the last foliage sound
  int32_t gravity;  // +0x3C
  F32 leanf;        // +0x40
  int32_t speed;
  int32_t delta_angles[3];  // +0x48
  int32_t groundEntityNum;  // +0x54
  F32 ladderNormal[3];  // +0x58
  int32_t jumpTime;     // +0x64
  F32 jumpOriginZ;      // +0x68
  int32_t movementDir;  // +0x6C
  uint32_t eFlags;      // +0x70
  uint32_t eventSequence;
  int32_t events[4];  // +0x78
  int32_t eventParms[4];  // +0x88
  uint8_t unknown98[4];
  int32_t clientNum;  // +0x9C
  int32_t weapon;     // +0xA0
  int32_t weaponstate;
  F32 weaponPosFrac;  // +0xA8: aiming down the sights, 0-1
  uint8_t unknownAC[4];
  F32 viewangles[3];  // +0xB0
  int32_t viewHeightTarget;  // +0xBC
  F32 viewHeightCurrent;
  int32_t viewHeightLerpTime;  // +0xC4
  int32_t viewHeightLerpTarget;
  int32_t viewHeightLerpDown;  // +0xCC
  F32 viewHeightLerpPosAdj;
  uint8_t unknownD4[0x20];
  int32_t ammo[64];      // +0xF4, by ammo index
  int32_t ammoclip[64];  // +0x1F4, by clip index
  uint32_t weapons[2];   // +0x2F4
  uint8_t weaponslots[8];
  uint32_t weaponrechamber[2];  // +0x304
  F32 mins[3];  // +0x30C
  F32 maxs[3];  // +0x318
  int32_t proneViewHeight;   // +0x324
  int32_t crouchViewHeight;  // +0x328
  int32_t standViewHeight;   // +0x32C
  int32_t deadViewHeight;    // +0x330
  F32 moveSlowScale;  // +0x334
  F32 moveScale;      // +0x338
  F32 proneScale;     // +0x33C
  F32 crouchScale;    // +0x340
  F32 strafeScale;    // +0x344
  F32 backScale;      // +0x348
  F32 unknown34C;
  F32 proneDirection;       // +0x350
  F32 proneDirectionPitch;  // +0x354
  F32 proneTorsoPitch;      // +0x358
  uint8_t unknown35C[0x14];
  F32 unknown370;
  uint8_t unknown374[0x48];
  F32 proneCheckHeight;      // +0x3BC
  F32 proneCheckPitch;       // +0x3C0
  F32 proneCheckTorsoPitch;  // +0x3C4
  uint8_t unknown3C8[4];
  uint32_t weapAnim;   // +0x3CC, 0x200 the toggle bit
  F32 aimSpreadScale;  // +0x3D0
};
static_assert(
    offsetof(PlayerState, groundEntityNum) == 0x54 && offsetof(PlayerState, eFlags) == 0x70 &&
    offsetof(PlayerState, weapon) == 0xA0 && offsetof(PlayerState, ammo) == 0xF4 &&
    offsetof(PlayerState, weapons) == 0x2F4 && offsetof(PlayerState, weapAnim) == 0x3CC &&
    offsetof(PlayerState, viewangles) == 0xB0 && offsetof(PlayerState, viewHeightTarget) == 0xBC &&
    offsetof(PlayerState, viewHeightLerpPosAdj) == 0xD0 && offsetof(PlayerState, mins) == 0x30C &&
    offsetof(PlayerState, proneViewHeight) == 0x324 && offsetof(PlayerState, unknown34C) == 0x34C &&
    offsetof(PlayerState, unknown370) == 0x370 && offsetof(PlayerState, proneDirection) == 0x350 &&
    offsetof(PlayerState, proneCheckHeight) == 0x3BC &&
    offsetof(PlayerState, aimSpreadScale) == 0x3D0);

constexpr int32_t kEntityNumNone = 0x7FF, kEntityNumWorld = 0x7FE;
constexpr int32_t kMaxTouch = 32;

// ---- the two modules -------------------------------------------------------------

enum Format {
  kFmtJump,        // "%i:jump base =%.1f top=%.1f vel=%.1f\n"
  kFmtLand,        // "%i:Land\n"
  kFmtSteep,       // "%i:steep\n"
  kFmtKickoff,     // "%i:kickoff\n"
  kFmtStun,        // "landing stun time: %i speed mult: %.2f\n"
  kFmtDamage,      // "falling damage: %i\n"
  kFmtLandVel,     // "landing vel: %.1f fall height: %.1f\n"
  kFmtClipPlanes,  // "%i:MAX_CLIP_PLANES\n"
  kFmtRecollided,  // "%i:recollided with plane normal (%.2f, %.2f, %.2f)\n"
  kFmtStepRoom,    // "%i:not enough step room\n"
  kFmtJumpStepTo,  // "%i:jump step to:%.2f jump peak at:%.2f\n"
  kFmtTooHigh,     // "%i:didn't use jump step results because it went too high\n"
  kFmtNoStep,      // "%i:didn't use step results\n"
  kFmtDownStep,    // "%i:did down step after not using step results\n"
  kFmtJumpVel,     // "%i:adjusted jump vel: %.1f -> %.1f\n"
  kFmtJumpStep,    // "%i:jump step %2i\n"
  kFmtStepped,     // "%i:stepped %2i\n"
  kFmtFallCvars,   // "bg_fallDamageMaxHeight and bg_fallDamageMinHeight have bad values\n" (game)
  kFormats
};

struct Module {
  bool game;
  uint32_t pm;          // pmove_t* while pmove runs
  uint32_t pml;         // pml_t
  uint32_t weaponDefs;  // weaponDef_t* by weapon number
  uint32_t cPmove;      // pmove count
  // vmCvar_t integers: the stance change times (ms) into and out of prone
  // (the original's fallbacks are 200, and 150 or 100).
  uint32_t proneTime, crouchTime;
  GuestFn sqrt, atan2;
  GuestFn printf;  // the console print
  GuestFn error;   // the error exit
  // Its messages: "All stances disallowed for player", "All stances
  // (standing & crouching) disallowed for linked player".
  uint32_t errNoStance, errNoStanceLinked;
  // Its formats for pm->debugLevel, as in Print below.
  uint32_t fmt[kFormats];
  // vmCvar_t values: bg_fallDamageMaxHeight, bg_fallDamageMinHeight.
  uint32_t fallMax, fallMin;
  // vmCvar_t integers of the foliage sound: the speeds it starts at and is
  // fastest at, its interval at those speeds, and the time it resets after.
  uint32_t foliageMinSpeed, foliageMaxSpeed, foliageSlowInterval, foliageFastInterval,
      foliageReset;
  // The view height curves (TableLerp) into prone (and the faster one with
  // pm_flags 4), into a crouch from standing and from prone, and up.
  uint32_t curveProne, curveProneFast, curveCrouchDown, curveCrouchUp, curveStand;
  // vmCvar_t integer: the view height in pm_type 2 and 3.
  uint32_t flyViewHeight;
  // vmCvar_t integers: the yaw limit on a ladder and prone (0: none), and
  // whether the prone check limits turning prone.
  uint32_t ladderYawCap, proneYawCap, proneYawCheck;
  // The weapon count, the clip sizes by clip index, a cvar that stops
  // firing, and the weapon debugging cvars (state, anim) with the last state
  // printed.
  uint32_t numWeapons, clipSizes, noFire, debugWeaponState, debugWeaponAnim, lastWeaponState,
      lastWeaponAnim;
  // The module's name for the debug prints, and how far its weapon debug
  // strings are from the game's.
  uint32_t moduleName, weaponStringsDelta;
  // The value of the weapon debugging cvars that prints from the other
  // module only (game 2, cgame 3).
  int32_t debugOtherModule;
};

const Module kGame{true, 0x201271D0, 0x20127020, 0x20125650, 0x20125654, 0x2012766C, 0x20127AEC,
                   f_20084540, f_2008452A, f_2001E5E0, f_2001E580, 0x2009892C, 0x20098950,
                   {0x20098A64, 0x20098994, 0x200989A0, 0x200989AC, 0x200989B8, 0x200989E0,
                    0x20098A3C, 0x20098918, 0x200988E4, 0x200988C8, 0x200988A0, 0x20098864,
                    0x20098848, 0x20098818, 0x200987F4, 0x200987E0, 0x200987D0, 0x200989F8},
                   0x20126F08, 0x20127548, 0x20126BAC, 0x201279CC, 0x2012730C, 0x20126DEC,
                   0x201271EC, 0x200A7040, 0x200A70C4, 0x200A6F60, 0x200A70E8, 0x200A6FD0,
                   0x2012742C, 0x2012778C, 0x201270CC, 0x20126A8C, 0x2012564C, 0x200A8E30,
                   0x20298CAC, 0x20127C0C, 0x20127D2C, 0x200A6F50, 0x200A6F54, 0x200A6334, 0, 2};
const Module kCgame{false, 0x300D06B0, 0x300D0500, 0x300CEB8C, 0x300CEB90, 0x300D0B4C, 0x300D0FCC,
                    f_3004D690, f_3004D67A, f_300191F0, f_30019190, 0x30058470, 0x30058498,
                    {0x30058564, 0x300584DC, 0x300584E8, 0x300584F4, 0x30058500, 0x30058528,
                     0x3005853C, 0x3005845C, 0x30058428, 0x3005840C, 0x300583E4, 0x300583A8,
                     0x3005838C, 0x3005835C, 0x30058338, 0x30058324, 0x30058314, 0},
                    0x300D03E8, 0x300D0A28, 0x300D008C, 0x300D0EAC, 0x300D07EC, 0x300D02CC,
                    0x300D06CC, 0x300670C8, 0x3006714C, 0x30066FE8, 0x30067170, 0x30067058,
                    0x300D090C, 0x300D0C6C, 0x300D05AC, 0x300CFF6C, 0x300CEB88, 0x30068E50,
                    0x301E87AC, 0x300D10EC, 0x300D120C, 0x30066FD8, 0x30066FDC, 0x300663B8,
                    0x0FFBFC3C, 3};

// ---- x87 helpers ------------------------------------------------------------------

St FromInt(int32_t v) { return St(double(v)); }  // FILD m32

// _ftol2: the value truncated, low 32 bits (0 for a NaN and out of range).
int32_t Ftol(St v) {
  if (!(std::fabs(v.v) < 0x1p63)) return 0;
  return int32_t(int64_t(v.v));
}

// FISTP m32 at round to nearest (the indefinite 0x80000000 out of range).
int32_t Fist(St v) {
  const double r = std::nearbyint(v.v);
  if (!(r >= -0x1p31 && r < 0x1p31)) return INT32_MIN;
  return int32_t(r);
}


// ---- calling the guest ------------------------------------------------------------

const Module* M;
Ctx* C;

Pmove& pm() { return At<Pmove>(At<uint32_t>(M->pm)); }
PlayerState& ps() { return At<PlayerState>(pm().ps); }
Pml& pml() { return At<Pml>(M->pml); }

void Push(uint32_t v) {
  C->esp -= 4;
  Wr<uint32_t>(g_guest_base, C->esp, v);
}
// The lifted function fn, called from the original's call site whose return
// address is ret. Arguments already pushed stay for the caller to pop.
void Call(GuestFn fn, uint32_t ret) {
  Push(ret);
  fn(*C);
}
// A guest function pointer, called the same way.
void CallPtr(uint32_t fn, uint32_t ret) {
  Push(ret);
  Dispatch(*C, fn, ret);
}

// The console print (format, args...) as the original calls it: each
// argument is a dword (an int) or a double (a float the original widened).
struct PrintArg {
  uint32_t lo, hi;
  bool wide;
  PrintArg(int32_t v) : lo(uint32_t(v)), hi(0), wide(false) {}
  PrintArg(St v) : wide(true) {
    const uint64_t b = std::bit_cast<uint64_t>(v.v);
    lo = uint32_t(b);
    hi = uint32_t(b >> 32);
  }
  PrintArg(F32 v) : PrintArg(St(v)) {}
};
void PrintAt(uint32_t format, std::initializer_list<PrintArg> args, uint32_t ret) {
  const uint32_t esp = C->esp;
  for (auto a = args.end(); a != args.begin();) {
    --a;
    if (a->wide) Push(a->hi);
    Push(a->lo);
  }
  Push(format);
  Call(M->printf, ret);
  C->esp = esp;
}
void Print(Format f, std::initializer_list<PrintArg> args, uint32_t ret) {
  PrintAt(M->fmt[f], args, ret);
}

// Guest memory below the caller's ESP for values passed by address; at()
// is relative to the new ESP. A guest longjmp (the error exit) unwinding through
// it has set ESP already, to its setjmp's frame above this one.
struct GuestFrame {
  uint32_t esp, lo;
  explicit GuestFrame(uint32_t size) : esp(C->esp), lo(C->esp - size) { C->esp = lo; }
  ~GuestFrame() {
    if (C->esp <= esp) C->esp = esp;
  }
  template <typename T>
  T& at(uint32_t off) {
    return At<T>(lo + off);
  }
  uint32_t va(uint32_t off) const { return lo + off; }
};

void Copy3(F32* dst, const F32* src) {
  for (int i = 0; i < 3; i++) dst[i] = src[i];
}

// ---- x87 values from the CRT and the transcendental instructions -------------------

// The CRT's sqrt (0x20084540): FSQRT for +0, positive values and +inf (it
// returns +inf as it is) and -0; its error path (errno, the math-error
// handler) for the rest, which is called lifted.
St CrtSqrt(St x, uint32_t ret) {
  const uint64_t b = std::bit_cast<uint64_t>(x.v);
  if (b == 0x8000000000000000ull || (!(b >> 63) && (b >> 52) != 0x7FF) ||
      b == 0x7FF0000000000000ull)
    return Sqrt(x);
  x87::LoadF64(C->fpu, b);
  Call(M->sqrt, ret);
  const St r(std::bit_cast<double>(x87::StoreF64(C->fpu)));
  x87::Pop(C->fpu);
  return r;
}

// atan2(y, x) * k1 / k2 (k1, k2 m64 constants) stored as a float. The CRT's
// atan2 runs FPATAN at 64-bit precision and returns its 64-bit result, so the
// products that follow are the x87 model's too.
F32 Atan2Degrees(St y, St x, double k1, double k2, uint32_t ret) {
  x87::LoadF64(C->fpu, std::bit_cast<uint64_t>(y.v));
  x87::LoadF64(C->fpu, std::bit_cast<uint64_t>(x.v));
  Call(M->atan2, ret);
  x87::ArithF64(C->fpu, x87::Op::Mul, std::bit_cast<uint64_t>(k1));
  x87::ArithF64(C->fpu, x87::Op::Div, std::bit_cast<uint64_t>(k2));
  const F32 r{x87::StoreF32(C->fpu)};
  x87::Pop(C->fpu);
  return r;
}

// FSINCOS of x, both results stored as floats: they keep 64 bits of
// significand (the precision control does not apply to FSINCOS), so they are
// rounded once, from the model's register. Out of its range (|x| >= 2^63)
// FSINCOS leaves x alone without pushing: the cosine stored is x, the sine
// the indefinite NaN of the empty register below, and the original's second
// pop moves the guest's TOP one further.
void SinCos(F32 x, F32* s, F32* c) {
  x87::State f;
  x87::Init(f);
  x87::SetControlWord(f, 0x027F);
  x87::LoadF32(f, x.bits);
  const uint8_t top = f.top;
  x87::SinCos(f);
  c->bits = x87::StoreF32(f);
  x87::Pop(f);
  s->bits = x87::StoreF32(f);
  if (f.top != top) x87::Pop(C->fpu);
}

constexpr double kDeg180 = 180.0, kDegMinus180 = -180.0;
constexpr double kPi = std::bit_cast<double>(0x400921FB60000000ull);  // M_PI as a float, 0x2009A288
constexpr float kDeg2Rad = std::bit_cast<float>(0x3C8EFA35u);

// ---- q_math -----------------------------------------------------------------------

// VectorNormalize (0x2000E030, ESI v): the length.
St VectorNormalize(F32* v) {
  const F32 sq = Fst((St(v[0]) * v[0] + St(v[1]) * v[1]) + St(v[2]) * v[2]);
  const F32 len = Fst(CrtSqrt(sq, 0x2000E070));
  if (St(len) != 0.0f) {
    const F32 inv = Fst(St(1.0f) / St(len));
    for (int i = 0; i < 3; i++) v[i] = Fst(St(inv) * v[i]);
  }
  return len;
}

// Vector2Normalize (0x2000E0C0, ESI v): x and y only; the length.
St Vector2Normalize(F32* v) {
  const F32 sq = Fst(St(v[0]) * v[0] + St(v[1]) * v[1]);
  const F32 len = Fst(CrtSqrt(sq, 0x2000E0EF));
  if (St(len) != 0.0f) {
    const F32 inv = Fst(St(1.0f) / St(len));
    v[0] = Fst(St(inv) * v[0]);
    v[1] = Fst(St(inv) * v[1]);
  }
  return len;
}

// VectorNormalize2 (0x2000E260, EDI in, ESI out): the length; out is 0 for
// a zero vector.
St VectorNormalize2(const F32* in, F32* out) {
  const F32 sq = Fst((St(in[0]) * in[0] + St(in[1]) * in[1]) + St(in[2]) * in[2]);
  const F32 len = Fst(CrtSqrt(sq, 0x2000E2A0));
  if (St(len) != 0.0f) {
    const F32 inv = Fst(St(1.0f) / St(len));
    for (int i = 0; i < 3; i++) out[i] = Fst(St(inv) * in[i]);
  } else {
    out[2] = out[1] = out[0] = kZero;
  }
  return len;
}

// vectoyaw (0x2000E6F0, ECX v).
St VecToYaw(const F32* v) {
  if (St(v[1]) == 0.0f && St(v[0]) == 0.0f) return St(0.0f);
  F32 yaw = Atan2Degrees(v[1], v[0], kDeg180, kPi, 0x2000E728);
  if (St(yaw) < 0.0f) yaw = Fst(St(yaw) + 360.0f);
  return yaw;
}

// The pitch of v (0x2000E7B0, ESI v), in [0, 360).
St VecToPitch(const F32* v) {
  if (St(v[1]) == 0.0f && St(v[0]) == 0.0f)
    return St(v[2]) > 0.0f ? St(270.0f) : St(90.0f);
  const F32 forward = Fst(CrtSqrt(St(v[0]) * v[0] + St(v[1]) * v[1], 0x2000E826));
  F32 pitch = Atan2Degrees(v[2], forward, kDegMinus180, kPi, 0x2000E836);
  if (St(pitch) < 0.0f) pitch = Fst(St(pitch) + 360.0f);
  return pitch;
}

// vectoangles (0x2000E910, ESI v, EDI angles).
void VecToAngles(const F32* v, F32* angles) {
  F32 yaw, pitch;
  if (St(v[1]) == 0.0f && St(v[0]) == 0.0f) {
    yaw = kZero;
    pitch = St(v[2]) > 0.0f ? Fl(270.0f) : Fl(90.0f);
  } else {
    yaw = Atan2Degrees(v[1], v[0], kDeg180, kPi, 0x2000E986);
    if (St(yaw) < 0.0f) yaw = Fst(St(yaw) + 360.0f);
    const F32 forward = Fst(CrtSqrt(St(v[0]) * v[0] + St(v[1]) * v[1], 0x2000E9D9));
    pitch = Atan2Degrees(v[2], forward, kDegMinus180, kPi, 0x2000E9E9);
    if (St(pitch) < 0.0f) pitch = Fst(St(pitch) + 360.0f);
  }
  angles[0] = pitch;
  angles[1] = yaw;
  angles[2] = kZero;
}

// AngleVectors (0x2000EB00, EDX angles, ESI forward, EDI right, EBX up; any
// of the three may be null).
void AngleVectors(const F32* angles, F32* forward, F32* right, F32* up) {
  F32 sy, cy, sp, cp, sr, cr;
  SinCos(Fst(St(angles[1]) * kDeg2Rad), &sy, &cy);
  SinCos(Fst(St(angles[0]) * kDeg2Rad), &sp, &cp);
  if (forward) {
    forward[0] = Fst(St(cp) * cy);
    forward[1] = Fst(St(cp) * sy);
    forward[2] = Fst(-St(sp));
  }
  if (!right && !up) return;
  SinCos(Fst(St(angles[2]) * kDeg2Rad), &sr, &cr);
  if (right) {
    const F32 t = Fst(St(sr) * sp);
    right[0] = Fst(St(cr) * sy - St(t) * cy);
    right[1] = Fst(St(cr) * cy * -1.0f - St(t) * sy);
    right[2] = Fst(St(sr) * cp * -1.0f);
  }
  if (up) {
    const F32 t = Fst(St(cr) * sp);
    up[0] = Fst(St(t) * cy + St(sr) * sy);
    up[1] = Fst(St(t) * sy - St(sr) * cy);
    up[2] = Fst(St(cr) * cp);
  }
}

// AngleDelta (0x20010680; a1, a2 on the stack): a1 - a2 in [-180, 180].
St AngleDelta(F32 a1, F32 a2) {
  F32 d = Fst(St(a1) - a2);
  while (St(d) > 180.0f) d = Fst(St(d) - 360.0f);
  while (St(d) < -180.0f) d = Fst(St(d) + 360.0f);
  return d;
}

// AngleNormalize180 (0x20010770; the angle on the stack), through a 16-bit
// angle.
St AngleNormalize180(F32 a) {
  const int32_t s = Ftol(St(a) * std::bit_cast<float>(0x43360B61u)) & 0xFFFF;
  F32 r = Fst(FromInt(s));
  r = Fst(St(r) * 0.0054931640625f);
  if (St(r) > 180.0f) r = Fst(St(r) - 360.0f);
  return r;
}

// ProjectPointOnPlane (0x20011030, EDX dst, ECX p, EAX normal).
void ProjectPointOnPlane(F32* dst, const F32* p, const F32* normal) {
  const F32 n0 = normal[0], n1 = normal[1], n2 = normal[2];
  const F32 sq = Fst((St(n0) * n0 + St(n1) * n1) + St(n2) * n2);
  const F32 inv = Fst(St(1.0f) / sq);
  const F32 d = Fst(((St(p[2]) * normal[2] + St(p[1]) * normal[1]) + St(p[0]) * normal[0]) * inv);
  const F32 n[3] = {Fst(St(inv) * normal[0]), Fst(St(inv) * normal[1]), Fst(St(inv) * normal[2])};
  for (int i = 0; i < 3; i++) dst[i] = Fst(p[i] - St(n[i]) * d);
}


// ---- events, touch entities, traces ------------------------------------------------

// BG_AddPredictableEventToPlayerstate (0x20001890, EAX ps, ECX event; parm
// on the stack).
void AddPredictableEvent(PlayerState& p, uint32_t event, uint32_t parm) {
  if (!event) return;
  p.events[p.eventSequence & 3] = int32_t(event & 0xFF);
  p.eventParms[p.eventSequence & 3] = int32_t(parm & 0xFF);
  p.eventSequence++;
}

// PM_AddEvent (0x200033A0, ECX event).
void AddEvent(uint32_t event) {
  PlayerState& p = ps();
  if (!event) return;
  p.events[p.eventSequence & 3] = int32_t(event & 0xFF);
  p.eventParms[p.eventSequence & 3] = 0;
  p.eventSequence++;
}

// PM_AddTouchEnt (0x200033D0, EDI entityNum).
void AddTouchEnt(int32_t entityNum) {
  if (entityNum == kEntityNumWorld) return;
  Pmove& m = pm();
  if (m.numtouch == kMaxTouch) return;
  for (int32_t i = 0; i < m.numtouch; i++)
    if (m.touchents[i] == entityNum) return;
  m.touchents[m.numtouch] = entityNum;
  m.numtouch++;
}

// pm->trace (+0x110; cdecl).
void PmTrace(uint32_t results, uint32_t start, uint32_t mins, uint32_t maxs, uint32_t end,
             int32_t passEntityNum, int32_t mask, uint32_t ret) {
  Push(uint32_t(mask));
  Push(uint32_t(passEntityNum));
  Push(end);
  Push(maxs);
  Push(mins);
  Push(start);
  Push(results);
  CallPtr(pm().trace, ret);
  C->esp += 0x1C;
}

// PM_TraceAll-like (0x20003310, EBX results, EAX mask; start, mins, maxs,
// end, passEntityNum on the stack): when the trace starts in a player or
// corpse, that entity is touched and the trace done again without them, and
// pm->tracemask loses them for the rest of the move.
void TraceAll(uint32_t results, uint32_t start, uint32_t mins, uint32_t maxs, uint32_t end,
              int32_t passEntityNum, int32_t mask) {
  PmTrace(results, start, mins, maxs, end, passEntityNum, mask, 0x2000333B);
  const Trace& t = At<Trace>(results);
  if (!t.startsolid || !(t.contents & 0xC000)) return;
  AddTouchEnt(t.entityNum);
  pm().tracemask &= ~0xC000;
  PmTrace(results, start, mins, maxs, end, passEntityNum, mask & ~0xC000, 0x2000338B);
}

// PM_ClipVelocity (0x20003410, EDX in, ECX normal, ESI out; overbounce on
// the stack).
void ClipVelocity(const F32* in, const F32* normal, F32* out, F32 overbounce) {
  St backoff = St(in[1]) * normal[1] + St(normal[2]) * in[2];
  backoff = backoff + St(in[0]) * normal[0];
  backoff = backoff < 0.0f ? backoff * overbounce : backoff / overbounce;
  for (int i = 0; i < 3; i++) out[i] = Fst(in[i] - backoff * normal[i]);
}

// The stance (0x20003460, EAX ps): 1 prone, 2 crouched (also while the legs
// change into those stances), 0 standing.
int32_t Stance(const PlayerState& p) {
  if (p.pm_flags & 1) return 1;
  const int32_t anim = p.viewHeightLerpTarget;
  if (anim == p.proneViewHeight) return 1;
  if (p.viewHeightLerpTime && anim == p.crouchViewHeight && !p.viewHeightLerpDown) return 1;
  if (p.pm_flags & 2) return 2;
  if (p.viewHeightLerpTime && anim == p.crouchViewHeight) return 2;
  return 0;
}

// The length of a stance change into anim (0x200052E0, ECX anim, EAX ps;
// a flag on the stack).
int32_t StanceDuration(int32_t anim, const PlayerState& p, int32_t flag) {
  if (anim == p.proneViewHeight) return p.pm_flags & 4 ? 200 : At<int32_t>(M->proneTime);
  if (anim == p.crouchViewHeight) {
    if (flag) return p.pm_flags & 4 ? 100 : 150;
    return At<int32_t>(M->crouchTime);
  }
  return 200;
}

// How far the stance change from a to b has run, 0 to 1 (0x200053C0, ECX a,
// EDX b; ST0).
St StanceBlend(int32_t a, int32_t b) {
  const Pmove& m = pm();
  const PlayerState& p = ps();
  const int32_t start = p.viewHeightLerpTime;
  if (!start) return St(0.0f);
  if (b != -1 && a != -1) {
    if (a != p.viewHeightLerpTarget) return St(0.0f);
    if (a == p.crouchViewHeight && !(b == p.proneViewHeight && !p.viewHeightLerpDown) &&
        (b != p.standViewHeight || !p.viewHeightLerpDown))
      return St(0.0f);
  }
  int32_t duration;
  const int32_t anim = p.viewHeightLerpTarget;
  if (anim == p.proneViewHeight)
    duration = p.pm_flags & 4 ? 200 : At<int32_t>(M->proneTime);
  else if (anim == p.crouchViewHeight)
    duration = p.viewHeightLerpDown ? (p.pm_flags & 4 ? 100 : 150) : At<int32_t>(M->crouchTime);
  else
    duration = 200;
  St f = FromInt(m.cmd.serverTime - start) / FromInt(duration);
  if (f < 0.0f) return St(0.0f);
  if (f > 1.0f) f = St(1.0f);
  return f;
}

// A table of {key, value, ivalue} ending at key -1 (0x20005330, EAX key, EDI
// table, EBX out): value (ST0) and ivalue (*out, as a float) at key,
// interpolated between the keys around it; the first entry for key 0 or
// past the end.
St TableLerp(int32_t key, uint32_t table, F32* out) {
  struct Entry {
    int32_t key;
    F32 value;
    int32_t ivalue;
  };
  const Entry* e = &At<Entry>(table);
  if (key) {
    for (int32_t i = 1; i == 1 || e[i].key != -1; i++) {
      if (e[i].key == key) {
        *out = Fst(FromInt(e[i].ivalue));
        return e[i].value;
      }
      if (e[i].key > key) {
        const Entry& a = e[i - 1];
        const Entry& b = e[i];
        const St t = FromInt(key - a.key) / FromInt(b.key - a.key);
        *out = Fst(FromInt(b.ivalue - a.ivalue) * t + FromInt(a.ivalue));
        return t * (St(b.value) - a.value) + a.value;
      }
    }
  }
  *out = Fst(FromInt(e[0].ivalue));
  return e[0].value;
}

// ---- surfaces ------------------------------------------------------------------------

// The material of the ground (pml.groundTrace.surfaceFlags bits 20-24), 0
// for none or with surface flag 0x2000.
int32_t GroundMaterial() {
  const int32_t sf = pml().groundTrace.surfaceFlags;
  if (sf & 0x2000) return 0;
  return (sf >> 20) & 0x1F;
}

// 0x20003BB0: 0x53 on a ladder, else the ground's material + 0x46 (0 for none).
int32_t LandEventSurface() {
  if (ps().pm_flags & 0x10) return 0x53;
  const int32_t t = GroundMaterial();
  return t ? t + 0x46 : 0;
}

// 0x20004900 (the button byte on the stack): the ground's material + 0x2F,
// + 0x18 or + 1, by the button bits 1 and 0x80 (0 for none).
int32_t StepEventSurface(uint8_t buttons) {
  const int32_t t = GroundMaterial();
  if (!t) return 0;
  if (buttons & 1) return t + 0x2F;
  if (buttons & 0x80) return t + 0x18;
  return t + 1;
}

// 0x20004930 and 0x20004950: the ground's material + 0x18 / + 1 (0 for none).
int32_t SurfaceEvent18() {
  const int32_t t = GroundMaterial();
  return t ? t + 0x18 : 0;
}
int32_t SurfaceEvent1() {
  const int32_t t = GroundMaterial();
  return t ? t + 1 : 0;
}

// 0x20004970: 0x5D + the ground's material (0x5D with surface flag 0x2000).
int32_t SurfaceEvent5D() {
  const int32_t sf = pml().groundTrace.surfaceFlags;
  if (sf & 0x2000) return 0x5D;
  return ((sf >> 20) & 0x1F) + 0x5D;
}

// ---- friction, acceleration, command scale -------------------------------------------

// PM_Friction (0x200034C0).
void Friction() {
  PlayerState& p = ps();
  F32* vel = p.velocity;
  const St vz = pml().walking ? St(0.0f) : St(vel[2]);
  const St vy(vel[1]), vx(vel[0]);
  const St speed = Sqrt((vz * vz + vy * vy) + vx * vx);
  if (speed < 1.0f) {
    vel[0] = kZero;
    vel[1] = kZero;
    return;
  }
  St drop(0.0f);
  const uint8_t waterlevel = pm().waterlevel;
  const uint32_t flags = p.pm_flags;
  if (waterlevel <= 1 && pml().walking && !(pml().groundTrace.surfaceFlags & 2) &&
      !(flags & 0x200)) {
    St control = speed < 100.0f ? St(100.0f) : speed;
    if (flags & 0x100)
      control = control * std::bit_cast<float>(0x3E99999Au);  // 0.3
    else if (flags & 0x2000)
      control = control + control;
    drop = control * (St(pml().frametime) * 5.5f);
  }
  if (waterlevel) drop = drop + FromInt(waterlevel) * 1.0f * pml().frametime * speed;
  St newspeed = speed - drop;
  if (newspeed < 0.0f) newspeed = St(0.0f);
  newspeed = newspeed / speed;
  for (int i = 0; i < 3; i++) vel[i] = Fst(newspeed * vel[i]);
}

// PM_Accelerate (0x200035E0, EDX wishdir; wishspeed, accel on the stack).
void Accelerate(const F32* wishdir, F32 wishspeed, F32 accel) {
  PlayerState& p = ps();
  F32* vel = p.velocity;
  const St current = (St(vel[2]) * wishdir[2] + St(vel[1]) * wishdir[1]) + St(wishdir[0]) * vel[0];
  const St add = wishspeed - current;
  const F32 addF = Fst(add);
  if (add <= 0.0f) return;
  St speed = St(wishspeed) < 100.0f ? St(100.0f) : St(wishspeed);
  speed = speed * pml().frametime * accel;
  if (speed > addF) speed = addF;
  if (p.groundEntityNum != kEntityNumNone) speed = speed * (St(1.0f) / p.unknown370);
  if (speed > addF) speed = addF;
  for (int i = 0; i < 3; i++) vel[i] = Fst(speed * wishdir[i] + vel[i]);
}

// PM_CmdScale (0x200036A0, EAX cmd; ST0).
St CmdScale(const UserCmd& cmd) {
  const int32_t fwd = cmd.forwardmove, right = cmd.rightmove, up = cmd.upmove;
  int32_t max = fwd < 0 ? -fwd : fwd;
  if ((right < 0 ? -right : right) > max) max = right < 0 ? -right : right;
  if ((up < 0 ? -up : up) > max) max = up < 0 ? -up : up;
  if (!max) return St(0.0f);
  const PlayerState& p = ps();
  const int32_t total = up * up + right * right + fwd * fwd;
  St scale = FromInt(p.speed) * FromInt(max) / (Sqrt(FromInt(total)) * 127.0f);
  scale = scale * (p.pm_flags & 0x80 ? p.moveSlowScale : p.moveScale);
  if (p.pm_type == 2) scale = scale * 3.0f;
  if (p.pm_type == 3) scale = scale * 6.0f;
  return scale;
}

// The command scale on the ground (0x20003760; cmd on the stack; ST0):
// backwards and sideways scaled, then the stance (blended while it
// changes), the water, the weapon and the button 0x400.
St CmdScaleWalk(const UserCmd& cmd) {
  const PlayerState& p = ps();
  const int32_t fwd = cmd.forwardmove, right = cmd.rightmove;
  St f = fwd < 0 ? FromInt(fwd) * p.backScale : FromInt(fwd);
  f = Abs(f);
  const F32 r = Fst(Abs(FromInt(right) * p.strafeScale));
  if (!(f > r)) f = r;
  if (f == 0.0f) return St(0.0f);
  const int32_t total = right * right + fwd * fwd;
  St s = FromInt(p.speed) * f / (Sqrt(FromInt(total)) * 127.0f);
  F32 scale;
  if (p.pm_flags & 0x80) {
    scale = Fst(s * p.moveSlowScale);
  } else {
    scale = Fst(s * p.moveScale);
    if (!(St(p.leanf) == 0.0f)) scale = Fst(St(scale) * p.unknown34C);
  }
  if (p.pm_type == 2) {
    s = St(scale) * 3.0f;
  } else if (p.pm_type == 3) {
    s = St(scale) * 6.0f;
  } else {
    const int32_t stance = Stance(p);
    const int32_t a = p.proneViewHeight, b = p.crouchViewHeight;
    St blend = StanceBlend(a, b);
    if (blend != 0.0f) {
      s = ((St(1.0f) - blend) * p.crouchScale + blend * p.proneScale) * scale;
    } else if ((blend = StanceBlend(b, a)) != 0.0f) {
      s = ((St(1.0f) - blend) * p.proneScale + blend * p.crouchScale) * scale;
    } else {
      s = scale;
      if (stance == 1)
        s = s * p.proneScale;
      else if (stance == 2)
        s = s * p.crouchScale;
    }
    if (const uint8_t waterlevel = pm().waterlevel)
      s = s * (1.0f - FromInt(waterlevel) * std::bit_cast<float>(0x3EAAAAABu) *
                          (St(1.0f) - 0.5f));
  }
  if (p.weapon) {
    const uint32_t wd = At<uint32_t>(At<uint32_t>(M->weaponDefs) + uint32_t(p.weapon) * 4);
    if (At<F32>(wd + 0x214) > 0.0f) s = s * At<F32>(wd + 0x214);
  }
  if (cmd.wbuttons & 4) s = s * std::bit_cast<float>(0x3ECCCCCDu);  // 0.4
  return s;
}

// ---- small helpers of PmoveSingle -----------------------------------------------------

// 0x20007960: pm_flags 0x400 while prone.
void SetProneFlag400() {
  PlayerState& p = ps();
  if (p.pm_flags & 1) p.pm_flags |= 0x400;
}

// 0x20007980: pm_flags 0x80 (the slower move) on button 0x10, alive, not
// prone, with pm_flags 0x20, outside weapon states 5-9.
void SetSlowFlag() {
  PlayerState& p = ps();
  p.pm_flags &= ~0x80u;
  if (p.pm_type >= 4 || !(pm().cmd.buttons & 0x10)) return;
  const uint32_t flags = p.pm_flags;
  if ((flags & 1) || !(flags & 0x20)) return;
  const int32_t ws = p.weaponstate;
  if (ws == 5 || ws == 7 || ws == 9 || ws == 8 || ws == 6) return;
  p.pm_flags = flags | 0x80;
}

// PM_DropTimers-like (0x20006CF0): pm_time runs out.
void DropTimers() {
  PlayerState& p = ps();
  if (!p.pm_time) return;
  const int32_t msec = pml().msec;
  if (msec >= p.pm_time) {
    p.pm_flags &= 0xFFFEDCFF;
    p.pm_time = 0;
  } else {
    p.pm_time -= msec;
  }
}

// PM_WaterEvents-like (0x20006C80): entering and leaving the water.
void WaterEvents() {
  const uint8_t waterlevel = pm().waterlevel;
  if (!pml().previous_waterlevel) {
    if (!waterlevel) return;
    AddEvent(0x90);
  } else if (!waterlevel) {
    AddEvent(0x91);
  }
}

// 0x20008650 (x on the stack): x + 2^-30 rounded to the nearest integer.
int32_t RoundUp30(F32 x) { return Fist(St(x) + St(0x1p-30)); }


// ---- moves -----------------------------------------------------------------------------

void StepSlideMove(int32_t gravity);

constexpr float kOverclip = std::bit_cast<float>(0x3F8020C5u);    // 1.001
constexpr float kUnderclip = std::bit_cast<float>(0x3F7FBE87u);   // 0.999001

// An x87 dot product scaled to clip into a plane (1.001 when it points into
// it, else 0.999001).
St Overclip(St d) { return d < 0.0f ? d * kOverclip : d * kUnderclip; }

// PM_SetMovementDir-like (0x20003980): the move's direction relative to the
// view, -90 to 90.
void SetMovementDir() {
  PlayerState& p = ps();
  const auto clamp = [](int32_t d) {
    const int32_t m = d >> 31;
    if ((d ^ m) - m > 90) d = d > 0 ? 90 : -90;
    return int32_t(int8_t(d));
  };
  const uint32_t flags = p.pm_flags;
  if ((flags & 1) && !(p.eFlags & 0xC000)) {
    p.movementDir = clamp(Ftol(AngleNormalize180(Fst(St(p.proneDirection) - p.viewangles[1]))));
    return;
  }
  if (flags & 0x10) {
    const F32 yaw = Fst(VecToYaw(p.ladderNormal) + 180.0f);
    p.movementDir = clamp(Ftol(AngleNormalize180(Fst(St(yaw) - p.viewangles[1]))));
    return;
  }
  const Pml& l = pml();
  GuestFrame f(0x18);
  F32* moved = &f.at<F32>(0);
  F32* dir = &f.at<F32>(0xC);
  for (int i = 0; i < 3; i++) moved[i] = Fst(St(p.origin[i]) - l.previous_origin[i]);
  const Pmove& m = pm();
  const int8_t fwd = m.cmd.forwardmove;
  if ((fwd || m.cmd.rightmove) && p.groundEntityNum != kEntityNumNone) {
    F32 len = Fst(CrtSqrt((St(moved[2]) * moved[2] + St(moved[1]) * moved[1]) +
                              St(moved[0]) * moved[0],
                          0x20003AA5));
    if (St(len) != 0.0f && St(l.frametime) * 5.0f < len) {
      VectorNormalize2(moved, dir);
      VecToAngles(dir, dir);
      int32_t d = Ftol(AngleNormalize180(Fst(St(dir[1]) - p.viewangles[1])));
      if (fwd < 0) d = Ftol(AngleNormalize180(Fst(St(Fst(FromInt(d))) + 180.0f)));
      p.movementDir = clamp(d);
      return;
    }
  }
  p.movementDir = 0;
}

// PM_CheckJump (0x20003BE0): 1 when the player jumps.
int32_t CheckJump() {
  Pmove& m = pm();
  PlayerState& p = ps();
  Pml& l = pml();
  if (m.cmd.serverTime - p.jumpTime < 500) return 0;
  const uint32_t flags = p.pm_flags;
  if (flags & 0x800) return 0;
  if (flags & 0x2000) return 0;
  if (Stance(p)) return 0;
  if (m.cmd.upmove < 10) return 0;
  if (flags & 8) {
    m.cmd.upmove = 0;
    return 0;
  }
  l.groundPlane = 0;
  l.walking = 0;
  p.pm_flags |= 8;
  p.groundEntityNum = kEntityNumNone;
  p.velocity[2] = Fst(Sqrt(FromInt(p.gravity) * 78.0f));
  p.jumpOriginZ = Fst(St(p.origin[2]) + 39.0f);
  if (p.pm_flags & 0x10) {
    // Off a ladder: away from it along the view.
    p.velocity[2] = Fst(St(p.velocity[2]) * 0.75f);
    GuestFrame f(0x18);
    F32* dir = &f.at<F32>(0);
    F32* out = &f.at<F32>(0xC);
    dir[0] = l.forward[0];
    dir[1] = l.forward[1];
    dir[2] = kZero;
    VectorNormalize(dir);
    const F32* n = p.ladderNormal;
    St x, y;
    if ((St(l.forward[2]) * n[2] + St(l.forward[1]) * n[1]) + St(l.forward[0]) * n[0] < 0.0f) {
      const St t = ((St(dir[2]) * n[2] + St(dir[1]) * n[1]) + St(dir[0]) * n[0]) * -2.0f;
      for (int i = 0; i < 3; i++) out[i] = Fst(t * n[i] + dir[i]);
      VectorNormalize(out);
      x = out[0];
      y = out[1];
    } else {
      x = dir[0];
      y = dir[1];
    }
    p.velocity[0] = Fst(St(128.0f) * x);
    p.velocity[1] = Fst(St(128.0f) * y);
    p.pm_flags &= ~0x10u;
  }
  AddEvent(uint32_t(LandEventSurface()));
  p.aimSpreadScale = Fst(St(p.aimSpreadScale) + 64.0f);
  if (St(p.aimSpreadScale) > 255.0f) p.aimSpreadScale = Fl(255.0f);
  if (m.debugLevel)
    Print(kFmtJump, {At<int32_t>(M->cPmove), p.origin[2], p.jumpOriginZ, p.velocity[2]},
          0x20003DD6);
  return 1;
}

// PM_AirMove (0x20003DF0).
void AirMove() {
  Friction();
  const Pmove& m = pm();
  Pml& l = pml();
  const F32 fmove = Fst(FromInt(m.cmd.forwardmove));
  const F32 smove = Fst(FromInt(m.cmd.rightmove));
  const F32 scale = Fst(CmdScale(m.cmd));
  l.forward[2] = kZero;
  l.right[2] = kZero;
  VectorNormalize(l.forward);
  VectorNormalize(l.right);
  GuestFrame f(0xC);
  F32* wishvel = &f.at<F32>(0);
  wishvel[0] = Fst(St(l.right[0]) * smove + St(l.forward[0]) * fmove);
  wishvel[1] = Fst(St(l.right[1]) * smove + St(l.forward[1]) * fmove);
  wishvel[2] = kZero;
  const F32 wishspeed = Fst(VectorNormalize(wishvel) * scale);
  Accelerate(wishvel, wishspeed, Fl(1.0f));
  if (l.groundPlane) {
    // Slide along a steep slope.
    F32* vel = ps().velocity;
    const F32* n = l.groundTrace.normal;
    const St d = Overclip((St(n[1]) * vel[1] + St(n[2]) * vel[2]) + St(n[0]) * vel[0]);
    for (int i = 0; i < 3; i++) vel[i] = Fst(vel[i] - St(n[i]) * d);
  }
  StepSlideMove(1);
  SetMovementDir();
}

// PM_WalkMove (0x20003F60).
void WalkMove() {
  Pmove& m = pm();
  Pml& l = pml();
  if (CheckJump()) {
    AirMove();
    ps().jumpTime = m.cmd.serverTime;
    return;
  }
  Friction();
  const F32 fmove = Fst(FromInt(m.cmd.forwardmove));
  const F32 smove = Fst(FromInt(m.cmd.rightmove));
  const F32 scale = Fst(CmdScaleWalk(m.cmd));
  // forward and right along the ground.
  const F32* n = l.groundTrace.normal;
  const St z = St(n[2]) * 0.0f;
  St d = Overclip((St(n[1]) * l.forward[1] + St(n[0]) * l.forward[0]) + z);
  l.forward[0] = Fst(l.forward[0] - St(n[0]) * d);
  l.forward[1] = Fst(l.forward[1] - St(n[1]) * d);
  l.forward[2] = Fst(-(St(n[2]) * d));
  d = Overclip((St(l.right[1]) * n[1] + St(l.right[0]) * n[0]) + z);
  l.right[0] = Fst(l.right[0] - St(n[0]) * d);
  l.right[1] = Fst(l.right[1] - St(n[1]) * d);
  l.right[2] = Fst(-(St(n[2]) * d));
  VectorNormalize(l.forward);
  VectorNormalize(l.right);
  GuestFrame f(0xC);
  F32* wishvel = &f.at<F32>(0);
  for (int i = 0; i < 3; i++) wishvel[i] = Fst(St(l.right[i]) * smove + St(l.forward[i]) * fmove);
  const F32 wishspeed = Fst(VectorNormalize(wishvel) * scale);
  PlayerState& p = ps();
  const int32_t stance = Stance(p);
  const bool slick = (l.groundTrace.surfaceFlags & 2) || (p.pm_flags & 0x200);
  F32 accel = Fl(slick ? 1.0f : stance == 1 ? 19.0f : stance == 2 ? 12.0f : 9.0f);
  if (p.pm_flags & 0x100) accel = Fst(St(accel) * 0.25f);
  Accelerate(wishvel, wishspeed, accel);
  F32* vel = p.velocity;
  if ((l.groundTrace.surfaceFlags & 2) || (p.pm_flags & 0x200))
    vel[2] = Fst(vel[2] - FromInt(p.gravity) * l.frametime);
  // Along the ground at the same speed, unless it turns back.
  const St v0(vel[0]), v1(vel[1]), v2(vel[2]);
  const F32 speed = Fst(Sqrt((v0 * v0 + v1 * v1) + v2 * v2));
  const St o0(vel[0]), o1(vel[1]), o2(vel[2]);
  d = Overclip((St(n[2]) * vel[2] + St(n[1]) * vel[1]) + St(n[0]) * vel[0]);
  vel[0] = Fst(vel[0] - St(n[0]) * d);
  vel[1] = Fst(vel[1] - St(n[1]) * d);
  vel[2] = Fst(vel[2] - d * n[2]);
  if ((o2 * vel[2] + o1 * vel[1]) + o0 * vel[0] > 0.0f) {
    VectorNormalize(vel);
    for (int i = 0; i < 3; i++) vel[i] = Fst(St(speed) * vel[i]);
  }
  if (!(St(vel[0]) == 0.0f && St(vel[1]) == 0.0f)) StepSlideMove(0);
  SetMovementDir();
}

// PM_DeadMove (0x20004350).
void DeadMove() {
  if (!pml().walking) return;
  F32* vel = ps().velocity;
  const St v0(vel[0]), v1(vel[1]), v2(vel[2]);
  const St forward = Sqrt((v0 * v0 + v1 * v1) + v2 * v2) - 20.0f;
  const F32 forwardF = Fst(forward);
  if (forward <= 0.0f) {
    vel[2] = kZero;
    vel[1] = kZero;
    vel[0] = kZero;
    return;
  }
  VectorNormalize(vel);
  for (int i = 0; i < 3; i++) vel[i] = Fst(St(forwardF) * vel[i]);
}

// PM_CorrectAllSolid (0x20004DC0, EAX trace): 1 when a spot nearby is free
// (the player moves there and the ground below it is traced), else 0 and
// the player is in the air.
int32_t CorrectAllSolid(uint32_t trace) {
  static constexpr float kJitter[26][3] = {
      {0, 0, 1},   {-1, 0, 1},  {0, -1, 1},  {1, 0, 1},   {0, 1, 1},   {-1, 0, 0},  {0, -1, 0},
      {1, 0, 0},   {0, 1, 0},   {0, 0, -1},  {-1, 0, -1}, {0, -1, -1}, {1, 0, -1},  {0, 1, -1},
      {-1, -1, 1}, {1, -1, 1},  {1, 1, 1},   {-1, 1, 1},  {-1, -1, 0}, {1, -1, 0},  {1, 1, 0},
      {-1, 1, 0},  {-1, -1, -1}, {1, -1, -1}, {1, 1, -1},  {-1, 1, -1}};
  GuestFrame f(0xC);
  F32* point = &f.at<F32>(0);
  const uint32_t pmVa = At<uint32_t>(M->pm);
  for (const auto& j : kJitter) {
    PlayerState& p = ps();
    for (int i = 0; i < 3; i++) point[i] = Fst(St(j[i]) + p.origin[i]);
    TraceAll(trace, f.va(0), pmVa + offsetof(Pmove, mins), pmVa + offsetof(Pmove, maxs), f.va(0),
             p.clientNum, pm().tracemask);
    if (!At<Trace>(trace).startsolid) {
      Copy3(p.origin, point);
      point[0] = p.origin[0];
      point[1] = p.origin[1];
      point[2] = Fst(St(p.origin[2]) - 1.0f);
      TraceAll(trace, pm().ps + offsetof(PlayerState, origin), pmVa + offsetof(Pmove, mins),
               pmVa + offsetof(Pmove, maxs), f.va(0), p.clientNum, pm().tracemask);
      pml().groundTrace = At<Trace>(trace);
      Copy3(p.origin, At<Trace>(trace).endpos);
      return 1;
    }
  }
  PlayerState& p = ps();
  p.groundEntityNum = kEntityNumNone;
  pml().groundPlane = 0;
  pml().walking = 0;
  p.jumpOriginZ = kZero;
  return 0;
}


// ---- the ground ----------------------------------------------------------------------

// PM_CrashLand (0x200049B0): fall damage, the landing stun and the landing
// events, from the fall height the velocity at the last step implies.
void CrashLand() {
  Pmove& m = pm();
  if (m.waterlevel == 3) return;
  PlayerState& p = ps();
  const Pml& l = pml();
  // The time to fall to the new height at gravity g, from the old vertical
  // velocity, then the velocity at impact and the height that gives.
  const St vz(l.previous_velocity[2]);
  const St g = FromInt(p.gravity);
  const F32 gF = Fst(g);
  const St ng = -g;
  const St a = St(0.5f) * ng;
  const St den = vz * vz - (St(l.previous_origin[2]) - p.origin[2]) * a * 4.0f;
  if (den < 0.0f) return;
  const St t = (-vz - Sqrt(den)) / (a + a);
  const St vel = (vz + t * ng) * -1.0f;
  const F32 height = Fst(vel * vel / (St(gF) + St(gF)));
  if (m.debugLevel) Print(kFmtLandVel, {vel, height}, 0x20004A56);

  const F32 fallMax = At<F32>(M->fallMax), fallMin = At<F32>(M->fallMin);
  int32_t damage = 0;
  if (St(fallMax) <= fallMin || St(fallMin) < 0.0f) {
    if (M->game) Print(kFmtFallCvars, {}, 0x20004B0A);
  } else if (!(St(height) <= fallMin) && !(l.groundTrace.surfaceFlags & 1) && p.pm_type < 4) {
    if (St(height) >= fallMax) {
      damage = 100;
    } else {
      damage = Ftol((St(height) - fallMin) / (St(fallMax) - fallMin) * 100.0f);
      if (damage < 0) damage = 0;
      else if (damage > 100) damage = 100;
    }
  }
  if (m.waterlevel == 2) damage = Ftol(FromInt(damage) * 0.5f);
  int32_t parm = 0;
  if (!(St(height) <= 12.0f)) {
    parm = Ftol(((St(height) - 12.0f) * std::bit_cast<float>(0x3D1D89D9u) + 1.0f) * 4.0f);
    if (parm > 24) parm = 24;
  }
  if (St(p.jumpOriginZ) < std::bit_cast<float>(0xBA83126Fu) ||
      St(p.jumpOriginZ) > std::bit_cast<float>(0x3A83126Fu)) {
    p.pm_time = 200;
    p.pm_flags |= 0x2000;
  }
  F32* v = p.velocity;
  const float kSlow = std::bit_cast<float>(0x3F2B851Fu);  // 0.67
  if (damage) {
    if (m.debugLevel) Print(kFmtDamage, {damage}, 0x20004BC1);
    if (damage < 100 && !(pml().groundTrace.surfaceFlags & 2)) {
      int32_t stun = damage * 35 + 500;
      F32 mult;
      if (stun > 2000) {
        stun = 2000;
        mult = F32{0x3E4CCCCDu};  // 0.2
      } else if (stun <= 500) {
        mult = Fl(0.5f);
      } else if (stun >= 1500) {
        mult = F32{0x3E4CCCCDu};
      } else {
        mult = Fst(0.5f - (FromInt(stun) - 500.0f) * std::bit_cast<float>(0x3A83126Fu) *
                              std::bit_cast<float>(0x3E99999Au));
      }
      if (pm().debugLevel > 1) Print(kFmtStun, {stun, mult}, 0x20004C66);
      p.pm_time = stun;
      p.pm_flags |= 0x100;
      for (int i = 0; i < 3; i++) v[i] = Fst(St(mult) * v[i]);
    } else {
      for (int i = 0; i < 3; i++) v[i] = Fst(St(v[i]) * kSlow);
    }
    const int32_t sf = pml().groundTrace.surfaceFlags;
    const int32_t event = (sf & 0x2000 ? 0 : (sf >> 20) & 0x1F) + 0x74;
    AddPredictableEvent(p, uint32_t(event), uint32_t(damage));
    return;
  }
  if (!(St(height) > 4.0f)) return;
  if (St(height) < 8.0f) return AddEvent(uint32_t(SurfaceEvent18()));
  if (St(height) < 12.0f) return AddEvent(uint32_t(SurfaceEvent1()));
  for (int i = 0; i < 3; i++) v[i] = Fst(St(v[i]) * kSlow);
  AddPredictableEvent(p, uint32_t(SurfaceEvent5D()), uint32_t(parm));
}

// PM_GroundTrace (0x20004F30).
void GroundTrace() {
  Pmove& m = pm();
  PlayerState& p = ps();
  Pml& l = pml();
  GuestFrame f(0x48);
  F32* start = &f.at<F32>(0);
  F32* end = &f.at<F32>(0xC);
  const uint32_t traceVa = f.va(0x18);
  const Trace& trace = At<Trace>(traceVa);
  const uint32_t pmVa = At<uint32_t>(M->pm);
  start[0] = end[0] = p.origin[0];
  start[1] = end[1] = p.origin[1];
  start[2] = Fst(St(p.origin[2]) + 0.25f);
  end[2] = Fst(St(p.origin[2]) - 0.25f);
  const auto down = [&] {
    TraceAll(traceVa, f.va(0), pmVa + offsetof(Pmove, mins), pmVa + offsetof(Pmove, maxs),
             f.va(0xC), ps().clientNum, pm().tracemask);
  };
  down();
  l.groundTrace = trace;
  if (trace.allsolid && !CorrectAllSolid(traceVa)) return;
  const auto air = [&](int32_t plane) {
    ps().groundEntityNum = kEntityNumNone;
    l.groundPlane = plane;
    l.walking = 0;
  };
  if (trace.startsolid) {
    start[2] = Fst(St(ps().origin[2]) - std::bit_cast<float>(0x3A83126Fu));
    down();
    if (trace.startsolid) return air(0);
    l.groundTrace = trace;
  }
  if (St(trace.fraction) == St(1.0)) return air(0);
  ps().jumpOriginZ = kZero;
  const F32* vel = ps().velocity;
  if (!(ps().pm_flags & 0x10) && St(vel[2]) > 0.0f &&
      (St(trace.normal[1]) * vel[1] + St(trace.normal[0]) * vel[0]) + St(trace.normal[2]) * vel[2] >
          10.0f) {
    if (m.debugLevel) Print(kFmtKickoff, {At<int32_t>(M->cPmove)}, 0x200050F3);
    return air(0);
  }
  if (St(trace.normal[2]) < std::bit_cast<float>(0x3F333333u)) {  // 0.7
    if (pm().debugLevel) Print(kFmtSteep, {At<int32_t>(M->cPmove)}, 0x2000513F);
    return air(1);
  }
  l.groundPlane = 1;
  l.walking = 1;
  if (ps().groundEntityNum == kEntityNumNone) {
    if (pm().debugLevel) Print(kFmtLand, {At<int32_t>(M->cPmove)}, 0x20005197);
    CrashLand();
  }
  ps().groundEntityNum = trace.entityNum;
  AddTouchEnt(trace.entityNum);
}


// ---- sliding -------------------------------------------------------------------------

// PM_ClipVelocity at 1.001 as PM_SlideMove inlines it: each product stored
// as a float.
void ClipStored(F32* v, const F32* normal, St dot) {
  F32 b = Fst(dot);
  b = Fst(St(b) < 0.0f ? St(b) * kOverclip : St(b) / kOverclip);
  for (int i = 0; i < 3; i++) v[i] = Fst(St(v[i]) - Fst(St(b) * normal[i]));
}

// PM_SlideMove (0x20008730; gravity on the stack): 1 when the move was
// clipped (or blocked), 0 when it went through in one go.
int32_t SlideMove(int32_t gravity) {
  constexpr int kMaxClipPlanes = 8;
  Pml& l = pml();
  GuestFrame f(0x3C);
  const uint32_t traceVa = f.va(0), endVa = f.va(0x30);
  const Trace& trace = At<Trace>(traceVa);
  F32* end = &f.at<F32>(0x30);
  const uint32_t pmVa = At<uint32_t>(M->pm);
  F32 primal[3], endVelocity[3], planes[kMaxClipPlanes][3];
  F32* vel = ps().velocity;
  Copy3(primal, vel);
  if (gravity) {
    Copy3(endVelocity, vel);
    const F32 g = Fst(FromInt(ps().gravity));
    endVelocity[2] = Fst(endVelocity[2] - St(g) * l.frametime);
    vel[2] = Fst((St(endVelocity[2]) + vel[2]) * 0.5f);
    primal[2] = endVelocity[2];
    if (l.groundPlane) ClipVelocity(vel, l.groundTrace.normal, vel, Fl(kOverclip));
  }
  F32 timeLeft = l.frametime;
  int32_t numplanes = 0;
  if (l.groundPlane) {
    Copy3(planes[0], l.groundTrace.normal);
    numplanes = 1;
  }
  // Never turn against the original velocity.
  VectorNormalize2(vel, planes[numplanes]);
  numplanes++;
  int32_t bumpcount;
  for (bumpcount = 0; bumpcount < 4; bumpcount++) {
    PlayerState& p = ps();
    for (int i = 0; i < 3; i++) end[i] = Fst(St(timeLeft) * vel[i] + p.origin[i]);
    TraceAll(traceVa, pm().ps + offsetof(PlayerState, origin), pmVa + offsetof(Pmove, mins),
             pmVa + offsetof(Pmove, maxs), endVa, p.clientNum, pm().tracemask);
    if (trace.allsolid) {
      ps().velocity[2] = kZero;
      return 1;
    }
    if (St(trace.fraction) > 0.0f) Copy3(ps().origin, trace.endpos);
    if (trace.fraction.bits == 0x3F800000u) break;
    AddTouchEnt(trace.entityNum);
    timeLeft = Fst(timeLeft - St(trace.fraction) * timeLeft);
    if (numplanes >= kMaxClipPlanes) {
      if (pm().debugLevel >= 2) Print(kFmtClipPlanes, {At<int32_t>(M->cPmove)}, 0x20008F89);
      vel[2] = vel[1] = vel[0] = kZero;
      return 1;
    }
    // The same plane again: nudge the velocity out along it.
    int32_t i;
    for (i = 0; i < numplanes; i++)
      if ((St(trace.normal[2]) * planes[i][2] + St(trace.normal[0]) * planes[i][0]) +
              St(trace.normal[1]) * planes[i][1] >
          std::bit_cast<float>(0x3F7FBE77u))  // 0.999
        break;
    if (i < numplanes) {
      if (pm().debugLevel >= 2)
        Print(kFmtRecollided,
              {At<int32_t>(M->cPmove), trace.normal[0], trace.normal[1], trace.normal[2]},
              0x200089EA);
      for (int k = 0; k < 3; k++) vel[k] = Fst(St(trace.normal[k]) + vel[k]);
      continue;
    }
    Copy3(planes[numplanes], trace.normal);
    numplanes++;
    // Clip the velocity into every plane it enters.
    for (i = 0; i < numplanes; i++) {
      const F32* pi = planes[i];
      F32 into = Fst((St(pi[0]) * vel[0] + St(pi[2]) * vel[2]) + St(pi[1]) * vel[1]);
      if (St(into) >= St(0.1)) continue;
      into = Fst(-St(into));
      if (St(into) > l.impactSpeed) l.impactSpeed = into;
      F32 clip[3], endClip[3];
      Copy3(clip, vel);
      ClipStored(clip, pi, (St(vel[0]) * pi[0] + St(vel[1]) * pi[1]) + St(vel[2]) * pi[2]);
      Copy3(endClip, endVelocity);
      ClipStored(endClip, pi,
                 (St(endVelocity[0]) * pi[0] + St(endVelocity[2]) * pi[2]) +
                     St(endVelocity[1]) * pi[1]);
      for (int32_t j = 0; j < numplanes; j++) {
        if (j == i) continue;
        const F32* pj = planes[j];
        const auto intoJ = [&](const F32* v) {
          return (St(v[1]) * pj[1] + St(v[0]) * pj[0]) + St(v[2]) * pj[2];
        };
        if (intoJ(clip) >= St(0.1)) continue;
        ClipStored(clip, pj, intoJ(clip));
        ClipStored(endClip, pj, intoJ(endClip));
        if ((St(clip[0]) * pi[0] + St(clip[2]) * pi[2]) + St(clip[1]) * pi[1] >= 0.0f) continue;
        // Along the crease of the two planes.
        F32 dir[3];
        dir[0] = Fst(St(pi[1]) * pj[2] - St(pj[1]) * pi[2]);
        dir[1] = Fst(St(pj[0]) * pi[2] - St(pi[0]) * pj[2]);
        dir[2] = Fst(St(pj[1]) * pi[0] - St(pj[0]) * pi[1]);
        VectorNormalize(dir);
        F32 d = Fst((St(dir[2]) * vel[2] + St(dir[1]) * vel[1]) + St(dir[0]) * vel[0]);
        for (int k = 0; k < 3; k++) clip[k] = Fst(St(dir[k]) * d);
        d = Fst((St(dir[2]) * endVelocity[2] + St(dir[1]) * endVelocity[1]) +
                St(dir[0]) * endVelocity[0]);
        for (int k = 0; k < 3; k++) endClip[k] = Fst(St(dir[k]) * d);
        // A third plane: stop dead.
        for (int32_t k = 0; k < numplanes; k++) {
          if (k == i || k == j) continue;
          const F32* pk = planes[k];
          if (!((St(clip[2]) * pk[2] + St(clip[0]) * pk[0]) + St(clip[1]) * pk[1] >= St(0.1))) {
            vel[2] = vel[1] = vel[0] = kZero;
            return 1;
          }
        }
      }
      // Every interaction fixed: try another move.
      Copy3(vel, clip);
      Copy3(endVelocity, endClip);
      break;
    }
  }
  if (gravity) Copy3(vel, endVelocity);
  if (ps().pm_time) Copy3(vel, primal);
  return bumpcount != 0;
}


// ---- prone -----------------------------------------------------------------------------

// The game's prone debugging (gamex86 only): a cvar, the time its lines
// stay, its colours, and the syscall pointer for lines.
constexpr uint32_t kProneDebug = 0x20295D6C, kProneDebugTime = 0x20295A0C;
constexpr uint32_t kColorBox = 0x2008A100, kColorTraces = 0x2008A060, kColorPoints = 0x2008A090;
constexpr uint32_t kGameSyscall = 0x200A4834;

bool ProneDebug() { return M->game && At<int32_t>(kProneDebug); }

// The debug line from a to b (syscall 0x52) and the circle of radius 6 at
// center around normal.
void DebugLine(uint32_t a, uint32_t b, uint32_t ret) {
  Push(1);
  Push(At<uint32_t>(kProneDebugTime));
  Push(kColorTraces);
  Push(b);
  Push(a);
  Push(0x52);
  CallPtr(At<uint32_t>(kGameSyscall), ret);
  C->esp += 0x18;
}
void DebugCircle(uint32_t center, uint32_t normal, uint32_t color, uint32_t ret) {
  if (!ProneDebug()) return;
  Push(1);
  Push(At<uint32_t>(kProneDebugTime));
  Push(color);
  Push(0x40C00000);  // 6.0
  Push(center);
  C->eax = normal;
  Call(f_20019360, ret);
  C->esp += 0x14;
}

// Whether the prone body fits at origin facing yaw (0x20002B60 / 0x300026C0,
// cdecl, 14 arguments; 1, always): traces down at the feet (24 units ahead
// of the origin along yaw - 180) and at the head (54 units), each from
// height above to height below (the head down to the feet's level), with a
// box of 6. Out: the height difference (0), the body's pitch and the
// torso's pitch, limited to -50..70 from the body's.
// The game's copy draws the traces under its debug cvar; its locals are laid
// out as the original's frame for those calls.
int32_t ProneCheck(int32_t passEntityNum, uint32_t originVa, F32 radius, F32 height, F32 yaw,
                   uint32_t outHeight, uint32_t outPitch, uint32_t outTorso, uint32_t traceFn,
                   int32_t corpses) {
  GuestFrame f(0xB4);
  const uint32_t E = f.va(0x10);
  const auto v = [&](uint32_t off) { return &At<F32>(E + off); };
  const F32* origin = &At<F32>(originVa);
  if (ProneDebug()) {
    F32* lo = v(0x44);
    F32* hi = v(0x5C);
    lo[0] = Fst(-St(radius) + origin[0]);
    lo[1] = Fst(-St(radius) + origin[1]);
    lo[2] = origin[2];
    hi[0] = Fst(St(radius) + origin[0]);
    hi[1] = Fst(St(radius) + origin[1]);
    hi[2] = Fst(St(height) + origin[2]);
    C->ebx = At<uint32_t>(kProneDebugTime);
    C->edi = 1;
    C->eax = E + 0x44;
    C->edx = E + 0x5C;
    Push(kColorBox);
    Call(f_20019260, 0x20002BF2);
    C->esp += 4;
  }
  const int32_t mask = (corpses ? 0x10000 : 0) + 0x810011;
  if (outHeight) At<F32>(outHeight) = kZero;
  if (outPitch) At<F32>(outPitch) = kZero;
  if (outTorso) At<F32>(outTorso) = kZero;
  F32* angles = v(0);
  F32* end = v(0);
  F32* start = v(0xC);
  F32* forward = v(0x18);
  F32* up = v(0x24);
  F32* foot = v(0x30);
  F32* mins = v(0x44);
  F32* head = v(0x50);
  F32* maxs = v(0x5C);
  F32* right = v(0x68);
  const uint32_t resultsVa = E + 0x74;
  const Trace& results = At<Trace>(resultsVa);
  angles[1] = Fst(St(yaw) - 180.0f);
  for (int i = 0; i < 3; i++) {
    mins[i] = Fl(-6.0f);
    maxs[i] = Fl(6.0f);
  }
  angles[0] = angles[2] = kZero;
  AngleVectors(angles, forward, right, up);
  const auto trace = [&](uint32_t ret) {
    Push(uint32_t(mask));
    Push(uint32_t(passEntityNum));
    Push(E);
    Push(E + 0x5C);
    Push(E + 0x44);
    Push(E + 0xC);
    Push(resultsVa);
    CallPtr(traceFn, ret);
    C->esp += 0x1C;
  };
  // A point straight below start (or where the trace stopped), 6 down.
  const auto landing = [&](F32* out) {
    St z;
    if (St(results.fraction) < 1.0f) {
      out[0] = results.endpos[0];
      out[1] = results.endpos[1];
      z = results.endpos[2];
    } else {
      out[0] = end[0];
      out[1] = end[1];
      z = end[2];
    }
    out[2] = Fst(z - 6.0f);
    if (results.startsolid || results.allsolid) out[2] = origin[2];
  };
  const auto draw = [&](uint32_t ret0, uint32_t ret1, uint32_t ret2, uint32_t ret3, uint32_t ret4) {
    if (!ProneDebug()) return;
    DebugLine(E + 0xC, E, ret0);
    DebugCircle(E + 0xC, E + 0x68, kColorTraces, ret1);
    DebugCircle(E + 0xC, E + 0x24, kColorTraces, ret2);
    DebugCircle(E, E + 0x68, kColorTraces, ret3);
    DebugCircle(E, E + 0x24, kColorTraces, ret4);
  };
  // The feet.
  F32 p = Fst(St(forward[0]) * 24.0f + origin[0]);
  end[0] = start[0] = p;
  p = Fst(St(forward[1]) * 24.0f + origin[1]);
  start[1] = end[1] = p;
  St z = St(forward[2]) * 24.0f + origin[2];
  start[2] = Fst(z + height);
  end[2] = Fst(z - height);
  trace(M->game ? 0x20002D2D : 0x3000280E);
  landing(foot);
  draw(0x20002DC5, 0x20002DF9, 0x20002E29, 0x20002E59, 0x20002E86);
  // The head, down to the feet's level.
  p = Fst(St(forward[0]) * 54.0f + origin[0]);
  start[0] = p;
  const F32 p1 = Fst(St(forward[1]) * 54.0f + origin[1]);
  start[1] = end[1] = p1;
  z = St(forward[2]) * 54.0f + origin[2];
  end[0] = p;
  start[2] = Fst(z + height);
  end[2] = Fst(St(foot[2]) - height);
  trace(M->game ? 0x20002F00 : 0x300028E1);
  landing(head);
  draw(0x20002F98, 0x20002FCC, 0x20002FFD, 0x2000302C, 0x20003059);
  F32* o = v(0x18);  // the origin, copied
  Copy3(o, origin);
  if (ProneDebug()) {
    DebugCircle(E + 0x18, E + 0x68, kColorPoints, 0x200030A2);
    DebugCircle(E + 0x18, E + 0x24, kColorPoints, 0x200030D2);
    DebugCircle(E + 0x30, E + 0x68, kColorPoints, 0x20003106);
    DebugCircle(E + 0x30, E + 0x24, kColorPoints, 0x20003137);
    DebugCircle(E + 0x50, E + 0x68, kColorPoints, 0x20003166);
    DebugCircle(E + 0x50, E + 0x24, kColorPoints, 0x20003193);
  }
  F32* d = v(0x24);
  for (int i = 0; i < 3; i++) d[i] = Fst(St(o[i]) - foot[i]);
  const F32 body = Fst(AngleNormalize180(Fst(VecToPitch(d))));
  for (int i = 0; i < 3; i++) d[i] = Fst(St(foot[i]) - head[i]);
  const F32 torso = Fst(AngleNormalize180(Fst(VecToPitch(d))));
  const St delta = AngleDelta(body, torso);
  St r;
  if (delta < -50.0f)
    r = AngleNormalize180(Fst(St(body) - -50.0f));
  else if (delta > 70.0f)
    r = AngleNormalize180(Fst(St(body) - 70.0f));
  else
    r = torso;
  if (outHeight) At<F32>(outHeight) = Fst(St(o[2]) - origin[2]);
  if (outPitch) At<F32>(outPitch) = body;
  if (outTorso) At<F32>(outTorso) = Fst(r);
  return 1;
}

// 0x20008680 (EAX origin, ESI velocity before the move): while prone, the
// prone check at the new origin; 0 (and the origin and velocity put back)
// when it fails, which it does not.
int32_t ProneFits(const F32* origin, const F32* velocity) {
  const Pmove& m = pm();
  PlayerState& p = ps();
  if (!(p.pm_flags & 1)) return 1;
  const uint32_t psVa = m.ps;
  const int32_t r = ProneCheck(
      p.clientNum, psVa + offsetof(PlayerState, origin), p.maxs[0], Fl(30.0f), p.proneDirection,
      psVa + offsetof(PlayerState, proneCheckHeight), psVa + offsetof(PlayerState, proneCheckPitch),
      psVa + offsetof(PlayerState, proneCheckTorsoPitch), m.traceUpright, 0);
  if (r) return r;
  Copy3(ps().origin, origin);
  Copy3(ps().velocity, velocity);
  return 0;
}


// ---- footsteps -----------------------------------------------------------------------

// The footstep event when the bob cycle crosses a step (0x200065C0, EAX old
// cycle, ECX new, EDX 1 for an event in the air): by the ground's material
// on the ground, by a trace into the ladder on one, by the stance in water.
void FootstepEvent(int32_t oldCycle, int32_t newCycle, int32_t air) {
  if (!(((oldCycle + 64) ^ (newCycle + 64)) & 0x80)) return;
  const Pmove& m = pm();
  const PlayerState& p = ps();
  if (m.waterlevel == 1 || m.waterlevel == 2) {
    AddEvent(p.pm_flags & 1 ? 0x43 : p.pm_flags & 0x80 ? 0x2C : 0x15);
    return;
  }
  if (m.waterlevel) return;
  if (p.groundEntityNum != kEntityNumNone) {
    if (air) AddEvent(uint32_t(StepEventSurface(uint8_t(p.pm_flags))));
    return;
  }
  if (!air || !(p.pm_flags & 0x10)) return;
  // On a ladder: the rung in front, with a narrower box.
  GuestFrame f(0x54);
  F32* maxs = &f.at<F32>(0);
  F32* mins = &f.at<F32>(0xC);
  F32* end = &f.at<F32>(0x18);
  const uint32_t resultsVa = f.va(0x24);
  mins[2] = Fl(8.0f);
  mins[0] = Fst(St(m.mins[0]) + 6.0f);
  mins[1] = Fst(St(m.mins[1]) + 6.0f);
  maxs[2] = m.maxs[2];
  maxs[0] = Fst(St(m.maxs[0]) - 6.0f);
  maxs[1] = Fst(St(m.maxs[1]) - 6.0f);
  if (St(m.maxs[2]) < 8.0f) maxs[2] = Fl(8.0f);
  for (int i = 0; i < 3; i++)
    end[i] = Fst(p.origin[i] - St(p.ladderNormal[i]) * std::bit_cast<float>(0x41F80000u));
  TraceAll(resultsVa, m.ps + offsetof(PlayerState, origin), f.va(0xC), f.va(0), f.va(0x18),
           p.clientNum, int32_t(uint32_t(m.tracemask) & 0xFDFE3FFFu));
  const Trace& t = At<Trace>(resultsVa);
  int32_t material = (t.surfaceFlags >> 20) & 0x1F;
  if (St(t.fraction) == 1.0f || !material) material = 0xD;
  AddEvent(uint32_t(material + 1));
}

// PM_Footsteps (0x200067C0 / 0x30005F00): pm->xyspeed, the bob cycle and
// footsteps, at any step length. The original adds `_ftol2(bobCycle +
// bobmove * msec) & 0xFF`, which loses the fraction at every step: a step
// with bobmove * msec < 1 adds nothing (no footsteps and no bob prone at
// msec <= 3, standing and crouched at msec <= 2), and each frame rate has
// its own, slower cadence. Here a step adds floor(bobmove * t1) -
// floor(bobmove * t0), t1 = ps.commandTime (the step's end) and t0 = t1 -
// msec: for a constant rate the steps telescope to bobmove cycle units per
// ms at any step length; when the rate changes the error stays below one
// unit and is unbiased. The server and prediction run the same steps with
// the same times, so they agree. FINDINGS.md "Footsteps at any step length".
//
// bobmove is the original's, in its order of operations: xyspeed / (speed x
// direction x move scale x stance scale (blended while the stance changes)) x
// K (by stance and pm_flags 0x80 / 0x40), on the ground at xyspeed >= 10;
// on a ladder (pm_flags 0x10, 300 ms after a jump) velocity z / (0.5 x
// 190.5 x move scale) x 0.45 (0.35), with an event on every step.
void Footsteps() {
  Pmove& m = pm();
  PlayerState& p = ps();
  if (p.pm_type >= 4) return;
  const St vy(p.velocity[1]), vx(p.velocity[0]);
  const F32 xy = Fst(Sqrt(vx * vx + vy * vy));
  m.xyspeed = xy;
  if (p.eFlags & 0xC000) return;
  const uint32_t flags = p.pm_flags;
  const bool slow = flags & 0x80;
  const bool ground = p.groundEntityNum != kEntityNumNone;
  St bobmove;
  if (!ground) {
    if (!(flags & 0x10)) return;
    if (m.cmd.serverTime - p.jumpTime < 300) return;
    bobmove = St(p.velocity[2]) / (St(0.5f) * 190.5f * (slow ? p.moveSlowScale : p.moveScale)) *
              (slow ? std::bit_cast<float>(0x3EB33333u) : std::bit_cast<float>(0x3EE66666u));
  } else {
    if (St(xy) < 10.0f) {
      if (St(xy) < 1.0f) p.bobCycle = 0;
      return;
    }
    const int32_t stance = Stance(p);
    const int8_t fwd = m.cmd.forwardmove, right = m.cmd.rightmove;
    St s = FromInt(p.speed);
    const St strafe = (St(p.strafeScale) - 1.0f) * 0.75f + 1.0f;
    if (fwd && right) {
      s = s * (strafe + 1.0f) * 0.5f;
      if (fwd < 0) s = s * (St(p.backScale) + 1.0f) * 0.5f;
    } else if (fwd < 0) {
      s = s * p.backScale;
    } else if (!fwd && right) {
      s = s * strafe;
    }
    const F32 base = Fst(s * (slow ? p.moveSlowScale : p.moveScale));
    const int32_t a = p.proneViewHeight, b = p.crouchViewHeight;
    St d, blend = StanceBlend(a, b);
    if (blend != 0.0f)
      d = ((St(1.0f) - blend) * p.crouchScale + blend * p.proneScale) * base;
    else if ((blend = StanceBlend(b, a)) != 0.0f)
      d = ((St(1.0f) - blend) * p.proneScale + blend * p.crouchScale) * base;
    else
      d = stance == 1 ? St(base) * p.proneScale : stance == 2 ? St(base) * p.crouchScale : base;
    float k;
    if (stance == 1)
      k = slow ? 0.24f : 0.25f;
    else if (stance == 2)
      k = slow ? 0.315f : 0.34f;
    else if (flags & 0x40)
      k = slow ? 0.325f : 0.36f;
    else
      k = slow ? 0.305f : 0.335f;
    bobmove = St(xy) / d * k;
  }
  const int32_t t1 = p.commandTime, t0 = t1 - pml().msec;
  const int32_t old = p.bobCycle;
  const double steps = std::floor(bobmove.v * t1) - std::floor(bobmove.v * t0);
  p.bobCycle = (old + int32_t(steps)) & 0xFF;
  if (ground && !m.cmd.forwardmove && !m.cmd.rightmove) return;
  FootstepEvent(old, p.bobCycle, 1);
}

// ---- stepping --------------------------------------------------------------------------

// PM_StepSlideMove (0x20008FF0; gravity on the stack): the slide move, then
// again from up to a step higher (18, prone 10; up to a jump's top when it
// started below it) and down again, whichever went further along the
// velocity; then steps down, the step event and its slowdown and bob.
void StepSlideMove(int32_t gravity) {
  GuestFrame f(0x54);
  const uint32_t traceVa = f.va(0), startVa = f.va(0x30), upVa = f.va(0x3C), downVa = f.va(0x48);
  const Trace& trace = At<Trace>(traceVa);
  F32* startO = &f.at<F32>(0x30);
  F32* up = &f.at<F32>(0x3C);
  F32* down = &f.at<F32>(0x48);
  const uint32_t pmVa = At<uint32_t>(M->pm);
  const uint32_t mins = pmVa + offsetof(Pmove, mins), maxs = pmVa + offsetof(Pmove, maxs);
  const auto origin = [] { return pm().ps + offsetof(PlayerState, origin); };
  const auto count = [] { return At<int32_t>(M->cPmove); };
  F32 startV[3], downO[3], downV[3];
  F32 stepSize = kZero;
  int32_t jumpStep = 0;
  const int32_t canStep = pml().groundPlane && !(ps().pm_flags & 0x10);
  Copy3(startO, ps().origin);
  Copy3(startV, ps().velocity);
  const int32_t result = SlideMove(gravity);
  PlayerState& p = ps();
  const uint32_t flags = p.pm_flags;
  F32 stepHeight = Fl(flags & 1 ? 10.0f : 18.0f);
  bool step = false;
  if (result && (St(p.jumpOriginZ) < std::bit_cast<float>(0xBA83126Fu) ||
                 St(p.jumpOriginZ) > std::bit_cast<float>(0x3A83126Fu))) {
    if (p.groundEntityNum != kEntityNumNone) {
      step = true;
    } else if (St(startO[2]) < p.jumpOriginZ) {
      // A jump that started below its top: up to the top.
      stepHeight = Fl(18.0f);
      if (St(startO[2]) + 18.0f > p.jumpOriginZ) {
        const St h = St(p.jumpOriginZ) - startO[2];
        stepHeight = Fst(h);
        if (h < 1.0f) return;
      }
      jumpStep = 1;
      step = true;
    }
  }
  if (!step && p.groundEntityNum == kEntityNumNone &&
      (!(flags & 0x10) || !(St(p.velocity[2]) > 0.0f)))
    return;

  Copy3(downO, p.origin);
  const F32 d0 = Fst(St(downO[0]) - startO[0]), d1 = Fst(St(downO[1]) - startO[1]);
  Copy3(downV, p.velocity);
  if (result) {
    up[0] = startO[0];
    up[1] = startO[1];
    up[2] = Fst(St(stepHeight) + startO[2] + 1.0f);
    TraceAll(traceVa, startVa, mins, maxs, upVa, p.clientNum, pm().tracemask);
    const St size = (St(stepHeight) + 1.0f) * trace.fraction - 1.0f;
    stepSize = Fst(size);
    if (size < 1.0f) {
      if (pm().debugLevel) Print(kFmtStepRoom, {count()}, 0x20009210);
      stepSize = kZero;
    } else {
      PlayerState& q = ps();
      q.origin[0] = up[0];
      q.origin[1] = up[1];
      q.origin[2] = Fst(St(startO[2]) + stepSize);
      Copy3(q.velocity, startV);
      if (pm().debugLevel && jumpStep)
        Print(kFmtJumpStepTo, {count(), q.origin[2], q.jumpOriginZ}, 0x20009296);
      SlideMove(gravity);
    }
  }
  // Down again.
  if (canStep || !(St(stepSize) == 0.0f)) {
    PlayerState& q = ps();
    down[0] = q.origin[0];
    down[1] = q.origin[1];
    down[2] = Fst(St(q.origin[2]) - stepSize);
    if (canStep) down[2] = Fst(down[2] - St(stepHeight) * 0.5f);
    TraceAll(traceVa, origin(), mins, maxs, downVa, q.clientNum, pm().tracemask);
    if (trace.entityNum < 4) {
      Copy3(ps().origin, downO);
      Copy3(ps().velocity, downV);
      return;
    }
    if (St(trace.fraction) < 1.0f) {
      Copy3(ps().origin, trace.endpos);
      ClipVelocity(ps().velocity, trace.normal, ps().velocity, Fl(kOverclip));
    } else if (!(St(stepSize) == 0.0f)) {
      ps().origin[2] = Fst(St(ps().origin[2]) - stepSize);
    }
  }
  // Keep the step only if it went further along the velocity.
  {
    PlayerState& q = ps();
    const St along = (St(q.origin[0]) - startO[0]) * q.velocity[0] +
                     (St(q.origin[1]) - startO[1]) * q.velocity[1];
    const St plain = St(d0) * q.velocity[0] + St(d1) * q.velocity[1] +
                     std::bit_cast<float>(0x3A83126Fu);
    if (plain >= along || (jumpStep && St(q.origin[2]) >= q.jumpOriginZ)) {
      Copy3(q.origin, downO);
      Copy3(q.velocity, downV);
      if (pm().debugLevel > 1) Print(jumpStep ? kFmtTooHigh : kFmtNoStep, {count()}, 0x200094A6);
      if (canStep) {
        down[0] = ps().origin[0];
        down[1] = ps().origin[1];
        down[2] = Fst(St(ps().origin[2]) - St(stepHeight) * 0.5f);
        TraceAll(traceVa, origin(), mins, maxs, downVa, ps().clientNum, pm().tracemask);
        if (St(trace.fraction) < 1.0f) {
          Copy3(ps().origin, trace.endpos);
          ClipVelocity(ps().velocity, trace.normal, ps().velocity, Fl(kOverclip));
          if (pm().debugLevel > 1) Print(kFmtDownStep, {count()}, 0x2000957A);
        }
      }
    }
  }
  if (jumpStep) {
    // Not faster than the jump needs to reach its top.
    PlayerState& q = ps();
    if (St(q.origin[2]) - downO[2] > 0.0f) {
      const St left = St(q.jumpOriginZ) - q.origin[2];
      if (left < std::bit_cast<float>(0x3DCCCCCDu)) {  // 0.1
        q.velocity[2] = kZero;
      } else {
        const F32 vel = Fst(Sqrt(FromInt(q.gravity) * (left + left)));
        if (St(q.velocity[2]) > vel) {
          if (pm().debugLevel) Print(kFmtJumpVel, {count(), q.velocity[2], vel}, 0x20009608);
          q.velocity[2] = vel;
        }
      }
    }
  }
  if (ps().pm_type >= 4) return;
  if (!ProneFits(startO, startV)) return;
  PlayerState& q = ps();
  const F32 delta = Fst(St(q.origin[2]) - downO[2]);
  if (!(Abs(St(q.origin[2]) - downO[2]) > St(0.5))) return;
  int32_t n = RoundUp30(delta);
  if (!n) return;
  if (pm().debugLevel) Print(jumpStep ? kFmtJumpStep : kFmtStepped, {count(), n}, 0x200096A5);
  if (n < -16)
    n = -16;
  else if (n > 24)
    n = 24;
  AddPredictableEvent(q, 0x8F, uint32_t(n + 0x80));
  F32 rise = Fst(St(q.origin[2]) - startO[2]);
  rise.bits &= 0x7FFFFFFFu;
  const St scale = (1.0f - St(rise) / stepHeight) * std::bit_cast<float>(0x3F4CCCCDu) +
                   std::bit_cast<float>(0x3E4CCCCCu);
  for (int i = 0; i < 3; i++) q.velocity[i] = Fst(scale * q.velocity[i]);
  const int32_t a = n < 0 ? -n : n;
  if (a > 3 && q.groundEntityNum != kEntityNumNone) {
    // A step bobs the view (0x200067A0, the stance check, says yes).
    int32_t bob = a / 2;
    if (bob > 4) bob = 4;
    const int32_t old = q.bobCycle;
    q.bobCycle = Ftol(FromInt(bob) * 1.25f + 7.0f + FromInt(old)) & 0xFF;
    FootstepEvent(old, ps().bobCycle, 1);
  }
}


// ---- water, foliage, view height ----------------------------------------------------------

// pm->pointcontents (+0x11C; cdecl): the contents at point.
int32_t PointContents(uint32_t point, int32_t passEntityNum, int32_t mask, uint32_t ret) {
  Push(uint32_t(mask));
  Push(uint32_t(passEntityNum));
  Push(point);
  CallPtr(pm().pointcontents, ret);
  C->esp += 0xC;
  return int32_t(C->eax);
}

// PM_SetWaterLevel (0x200051C0): 1 at the feet, 2 at the waist, 3 at the
// eyes.
void SetWaterLevel() {
  Pmove& m = pm();
  m.waterlevel = 0;
  m.watertype = 0;
  GuestFrame f(0xC);
  F32* point = &f.at<F32>(0);
  PlayerState& p = ps();
  point[0] = p.origin[0];
  point[1] = p.origin[1];
  point[2] = Fst(St(p.mins[2]) + p.origin[2] + 1.0f);
  const int32_t cont = PointContents(f.va(0), p.clientNum, 0x20, 0x20005214);
  if (!cont) return;
  const int32_t sample2 = Ftol(St(ps().viewHeightCurrent) - ps().mins[2]);
  const int32_t sample1 = sample2 / 2;
  pm().watertype = uint8_t(cont);
  pm().waterlevel = 1;
  point[2] = Fst(FromInt(sample1) + ps().mins[2] + ps().origin[2]);
  if (!PointContents(f.va(0), ps().clientNum, 0x20, 0x2000527E)) return;
  pm().waterlevel = 2;
  point[2] = Fst(St(ps().mins[2]) + ps().origin[2] + FromInt(sample2));
  if (!PointContents(f.va(0), ps().clientNum, 0x20, 0x200052BB)) return;
  pm().waterlevel = 3;
}

// The foliage sound (0x20006AF0): above the minimum speed, an event 0x8B
// whenever the player is inside foliage (contents 2) and the interval (by
// speed) has passed since the last one; below it, reset after a while.
void FoliageSound() {
  const Pmove& m = pm();
  PlayerState& p = ps();
  const int32_t minSpeed = At<int32_t>(M->foliageMinSpeed);
  if (FromInt(minSpeed) > m.xyspeed) {
    if (p.unknown38 + At<int32_t>(M->foliageReset) < m.cmd.serverTime) p.unknown38 = 0;
    return;
  }
  St f = (m.xyspeed - FromInt(minSpeed)) / FromInt(At<int32_t>(M->foliageMaxSpeed) - minSpeed);
  if (f > 1.0f) f = St(1.0f);
  const int32_t slow = At<int32_t>(M->foliageSlowInterval);
  const int32_t interval =
      Ftol(FromInt(At<int32_t>(M->foliageFastInterval) - slow) * f + FromInt(slow));
  if (interval + p.unknown38 >= m.cmd.serverTime) return;
  GuestFrame f2(0x48);
  F32* maxs = &f2.at<F32>(0);
  F32* mins = &f2.at<F32>(0xC);
  const uint32_t resultsVa = f2.va(0x18);
  const float k = std::bit_cast<float>(0x3F400000u);  // 0.75
  for (int i = 0; i < 3; i++) mins[i] = Fst(St(m.mins[i]) * k);
  for (int i = 0; i < 3; i++) maxs[i] = Fst(St(m.maxs[i]) * k);
  maxs[2] = Fst(St(m.maxs[2]) * std::bit_cast<float>(0x3F666666u));  // 0.9
  const uint32_t origin = m.ps + offsetof(PlayerState, origin);
  TraceAll(resultsVa, origin, f2.va(0xC), f2.va(0), origin, p.clientNum, 2);
  if (!At<Trace>(resultsVa).startsolid) return;
  AddEvent(0x8B);
  ps().unknown38 = pm().cmd.serverTime;
}

// PM_ViewHeightAdjust (0x200054E0): the view height toward its target; into
// and out of the stances along the curves, which also move the player
// forward by their position adjustment (a step slide, the velocity kept).
void ViewHeightAdjust() {
  Pmove& m = pm();
  PlayerState& p = ps();
  const int32_t target = p.viewHeightTarget;
  if (!target || St(p.viewHeightCurrent) == 0.0f) {
    p.viewHeightCurrent = Fst(FromInt(target));
    return;
  }
  if (FromInt(target) == p.viewHeightCurrent && !p.viewHeightLerpTime) return;
  if (target != p.proneViewHeight && target != p.crouchViewHeight && target != p.standViewHeight) {
    // Any other height: 180 units a second.
    p.viewHeightLerpTime = 0;
    const St step = St(pml().frametime) * 180.0f;
    if (FromInt(target) > p.viewHeightCurrent) {
      p.viewHeightCurrent = Fst(step + p.viewHeightCurrent);
      if (!(FromInt(target) <= p.viewHeightCurrent)) return;
    } else {
      p.viewHeightCurrent = Fst(p.viewHeightCurrent - step);
      if (!(FromInt(target) >= p.viewHeightCurrent)) return;
    }
    p.viewHeightCurrent = Fst(FromInt(target));
    return;
  }
  // The table for a lerp to lerpTarget.
  const auto curve = [&](bool fastProne) {
    const PlayerState& q = ps();
    if (q.viewHeightLerpTarget == q.proneViewHeight)
      return fastProne && (q.pm_flags & 4) ? M->curveProneFast : M->curveProne;
    if (q.viewHeightLerpTarget == q.crouchViewHeight)
      return q.viewHeightLerpDown ? M->curveCrouchDown : M->curveCrouchUp;
    return M->curveStand;
  };
  int32_t pct = 0;
  if (p.viewHeightLerpTime) {
    const int32_t duration =
        StanceDuration(p.viewHeightLerpTarget, p, p.viewHeightLerpDown);
    pct = int32_t(uint32_t(m.cmd.serverTime - p.viewHeightLerpTime) * 100u) / duration;
    if (pct >= 100) {
      p.viewHeightCurrent = Fst(FromInt(p.viewHeightLerpTarget));
      p.viewHeightLerpTime = 0;
      p.viewHeightLerpPosAdj = kZero;
    } else {
      if (pct < 0) pct = 0;
      F32 posAdj;
      p.viewHeightCurrent = Fst(TableLerp(pct, curve(true), &posAdj));
      if (Abs(St(ps().viewHeightLerpPosAdj) - posAdj) > St(0.05)) {
        // Move by the change of the position adjustment.
        PlayerState& q = ps();
        F32 saved[3];
        Copy3(saved, q.velocity);
        St speed = St(posAdj) - q.viewHeightLerpPosAdj;
        if (q.groundEntityNum == kEntityNumNone) speed = speed * 0.5f;
        const F32 speedF = Fst(speed / pml().frametime);
        F32 dir[3] = {pml().forward[0], pml().forward[1], kZero};
        VectorNormalize(dir);
        for (int i = 0; i < 3; i++) q.velocity[i] = Fst(St(dir[i]) * speedF);
        StepSlideMove(1);
        Copy3(ps().velocity, saved);
        ps().viewHeightLerpPosAdj = posAdj;
      }
    }
  }
  PlayerState& q = ps();
  if (q.viewHeightLerpTime) {
    // A change of direction during a lerp: back the other way from where it is.
    const int32_t t = q.viewHeightTarget, lt = q.viewHeightLerpTarget;
    if (t == lt) return;
    if (t < lt ? q.viewHeightLerpDown != 0 : !q.viewHeightLerpDown) return;
    q.viewHeightLerpDown ^= 1;
    const int32_t remaining = 100 - pct;
    if (q.viewHeightLerpDown) {
      if (lt == q.standViewHeight)
        q.viewHeightLerpTarget = q.crouchViewHeight;
      else if (lt == q.crouchViewHeight)
        q.viewHeightLerpTarget = q.proneViewHeight;
    } else {
      if (lt == q.proneViewHeight)
        q.viewHeightLerpTarget = q.crouchViewHeight;
      else if (lt == q.crouchViewHeight)
        q.viewHeightLerpTarget = q.standViewHeight;
    }
    if (remaining == 100) {
      q.viewHeightCurrent = Fst(FromInt(q.viewHeightLerpTarget));
      q.viewHeightLerpTime = 0;
      q.viewHeightLerpPosAdj = kZero;
      return;
    }
    const int32_t duration = StanceDuration(q.viewHeightLerpTarget, q, q.viewHeightLerpDown);
    q.viewHeightLerpTime =
        pm().cmd.serverTime - Ftol(FromInt(duration) * FromInt(remaining) *
                                   std::bit_cast<float>(0x3C23D70Au));  // 0.01
    F32 posAdj;
    TableLerp(remaining, curve(false), &posAdj);
    ps().viewHeightLerpPosAdj = posAdj;
    return;
  }
  // Start a lerp to the target's stance.
  if (FromInt(q.viewHeightTarget) == q.viewHeightCurrent) return;
  q.viewHeightLerpTime = pm().cmd.serverTime;
  const int32_t t = q.viewHeightTarget;
  if (t == q.proneViewHeight) {
    q.viewHeightLerpDown = 1;
    q.viewHeightLerpTarget =
        FromInt(q.crouchViewHeight) < q.viewHeightCurrent ? q.crouchViewHeight : q.proneViewHeight;
  } else if (t == q.crouchViewHeight) {
    q.viewHeightLerpDown = FromInt(t) < q.viewHeightCurrent;
    q.viewHeightLerpTarget = q.crouchViewHeight;
  } else if (t == q.standViewHeight) {
    q.viewHeightLerpDown = 0;
    q.viewHeightLerpTarget =
        FromInt(q.crouchViewHeight) > q.viewHeightCurrent ? q.crouchViewHeight : q.standViewHeight;
  }
}


// ---- stances --------------------------------------------------------------------------

// The pitch of the ground along yaw (0x20011860; yaw, normal on the stack;
// ST0).
St PitchForSlope(F32 yaw, const F32* normal) {
  F32 s, c;
  SinCos(Fst(St(yaw) * kDeg2Rad), &s, &c);
  const F32 dir[3] = {c, s, kZero};
  F32 projected[3];
  ProjectPointOnPlane(projected, dir, normal);
  return VecToPitch(projected);
}

// The error exit (1, message): a drop to the menu, which longjmps out of here.
void DropError(uint32_t message, uint32_t ret) {
  Push(message);
  Push(1);
  Call(M->error, ret);
  C->esp += 8;
}

// pm_flags 0x20000, 0x40000, 0x80000: standing, crouching, prone not allowed.
constexpr uint32_t kNoStand = 0x20000, kNoCrouch = 0x40000, kNoProne = 0x80000;
// The stance events.
constexpr uint32_t kEvStand = 0x8C, kEvCrouch = 0x8D, kEvProne = 0x8E;

// PM_CheckDuck (0x20005A80): the stance from the stance buttons (pm->cmd
// wbuttons 0x40 prone, 0x80 crouch) as far as there is room and it is
// allowed (pm_flags 0x20000/0x40000/0x80000 forbid standing/crouching/
// prone), its box, view height target and trace, then the view height
// (ViewHeightAdjust). Going prone takes the ground's pitch along the view.
void CheckDuck() {
  Pmove& m = pm();
  PlayerState& p = ps();
  const int32_t wasProne = p.pm_flags & 1;
  const int32_t wasStanding = !(p.pm_flags & 3);
  m.mins[0] = p.mins[0];
  m.mins[1] = p.mins[1];
  m.maxs[0] = p.maxs[0];
  m.maxs[1] = p.maxs[1];
  m.mins[2] = p.mins[2];
  const auto finish = [] {
    pm().trace = ps().pm_flags & 1 ? pm().traceProne : pm().traceUpright;
    ps().eFlags |= 0x10;
    ViewHeightAdjust();
  };
  if (p.pm_type >= 4) {
    m.maxs[2] = p.maxs[2];
    p.viewHeightTarget = p.deadViewHeight;
    return finish();
  }
  const uint32_t ef = p.eFlags;
  if ((ef & 0x20000) && !(ef & 0x80000)) {
    m.maxs[2] = p.maxs[2];
    p.viewHeightTarget = p.standViewHeight;
    p.pm_flags &= ~7u;
    AddEvent(kEvStand);
    return finish();
  }
  const uint32_t pmVa = At<uint32_t>(M->pm);
  GuestFrame f(0x58);
  F32* point = &f.at<F32>(0x10);
  F32* down = &f.at<F32>(0x1C);
  const uint32_t pointVa = f.va(0x10), downVa = f.va(0x1C), resultsVa = f.va(0x28);
  const Trace& results = At<Trace>(resultsVa);
  const uint32_t mins = pmVa + offsetof(Pmove, mins), maxs = pmVa + offsetof(Pmove, maxs);
  const auto trace = [&](uint32_t fn, uint32_t start, uint32_t end, uint32_t ret) {
    Push(uint32_t(pm().tracemask) & 0xFFFF3FFFu);
    Push(uint32_t(ps().clientNum));
    Push(end);
    Push(maxs);
    Push(mins);
    Push(start);
    Push(resultsVa);
    CallPtr(fn, ret);
    C->esp += 0x1C;
  };
  // Whether the box fits at the origin at the height maxs z.
  const auto fits = [&](F32 height, uint32_t ret) {
    pm().maxs[2] = height;
    const uint32_t origin = pm().ps + offsetof(PlayerState, origin);
    trace(pm().traceUpright, origin, origin, ret);
    return !results.allsolid;
  };
  // A stance that does not fit (feedback unless wbuttons 2).
  const auto blocked = [](uint32_t event) {
    if (!(pm().cmd.wbuttons & 2)) AddEvent(event);
  };
  // A stance that is not allowed turned into one that is: the event and the
  // buttons of the new one.
  const auto forced = [](uint32_t event, uint8_t buttons) {
    pm().cmd.wbuttons = (buttons & 0x3F) | (event == kEvProne    ? 0x40
                                            : event == kEvCrouch ? 0x80
                                                                 : 0);
    AddPredictableEvent(ps(), event, 0);
  };
  uint32_t& flags = p.pm_flags;
  if (ef & 0xC000) {
    // Set by the entity flags.
    if ((ef & 0x4000) && !(ef & 0x8000))
      flags = (flags | 1) & ~2u;
    else if ((ef & 0x8000) && !(ef & 0x4000))
      flags = (flags | 2) & ~1u;
    else
      flags &= ~3u;
  } else if (flags & 0x4000) {
  } else if (p.pm_type == 1 || (ef & 0x80000)) {
    // Linked to an entity: standing or crouched.
    const uint32_t no = flags & (kNoStand | kNoCrouch);
    if (no || (m.cmd.wbuttons & 0x40)) {
      if (no == (kNoStand | kNoCrouch)) DropError(M->errNoStanceLinked, 0x2000613F);
      const uint8_t b = pm().cmd.wbuttons;
      const uint32_t fl = ps().pm_flags;
      if (b & 0x40)
        forced(!(fl & 3) && !(fl & kNoStand) ? kEvStand : !(fl & kNoCrouch) ? kEvCrouch : kEvStand,
               b);
      else if (b & 0x80) {
        if (fl & kNoCrouch) forced(kEvStand, b);
      } else if (fl & kNoStand) {
        forced(kEvCrouch, b);
      }
    }
    flags = pm().cmd.wbuttons & 0xC0 ? (flags & ~1u) | 2 : flags & ~3u;
  } else {
    if ((flags & 0x10) && (m.cmd.wbuttons & 0xC0)) {
      // No stance changes on a ladder.
      m.cmd.wbuttons &= 0x3F;
      AddEvent(kEvStand);
    }
    if (const uint32_t no = flags & (kNoStand | kNoCrouch | kNoProne)) {
      if (no == (kNoStand | kNoCrouch | kNoProne)) DropError(M->errNoStance, 0x20005C84);
      const uint8_t b = pm().cmd.wbuttons;
      const uint32_t fl = ps().pm_flags;
      if (b & 0x40) {
        if (fl & kNoProne)
          forced(!(fl & 3) && !(fl & kNoStand) ? kEvStand
                 : !(fl & kNoCrouch)           ? kEvCrouch
                                               : kEvStand,
                 b);
      } else if (b & 0x80) {
        if (fl & kNoCrouch)
          forced(!(fl & 3) && !(fl & kNoProne) ? kEvProne : !(fl & kNoStand) ? kEvStand : kEvProne,
                 b);
      } else if (fl & kNoStand) {
        forced((fl & 1) && !(fl & kNoProne) ? kEvProne : !(fl & kNoCrouch) ? kEvCrouch : kEvProne,
               b);
      }
    }
    const uint8_t b = pm().cmd.wbuttons;
    if (b & 0x40) {
      // Prone, where the body fits (it always does).
      bool lie = true;
      if (!(flags & 1)) {
        const Pmove& n = pm();
        const uint32_t psVa = n.ps;
        lie = ProneCheck(p.clientNum, psVa + offsetof(PlayerState, origin), n.maxs[0], Fl(30.0f),
                         p.viewangles[1], psVa + offsetof(PlayerState, proneCheckHeight),
                         psVa + offsetof(PlayerState, proneCheckPitch),
                         psVa + offsetof(PlayerState, proneCheckTorsoPitch), n.traceUpright, 0);
      }
      if (lie) {
        flags |= 1;
      } else {
        flags |= 0x8000;
        flags &= ~4u;
        blocked(flags & 2 ? kEvCrouch : kEvStand);
      }
    } else if (b & 0x80) {
      flags &= ~4u;
      if (!(flags & 1)) {
        flags |= 2;
      } else if (fits(Fl(50.0f), 0x20005E82)) {
        flags = (flags & ~1u) | 2;
      } else {
        blocked(kEvProne);
      }
    } else {
      flags &= ~4u;
      if (flags & 1) {
        if (fits(p.maxs[2], 0x20005F1C))
          flags &= ~3u;
        else if (fits(Fl(50.0f), 0x20006035))
          flags = (flags & ~1u) | 2;
        else
          blocked(kEvProne);
      } else if (flags & 2) {
        if (fits(p.maxs[2], 0x200060CE))
          flags &= ~2u;
        else
          blocked(kEvCrouch);
      }
    }
  }

  if (!(flags & 1)) {
    if (flags & 2) {
      p.eFlags = (p.eFlags | 0x20) & ~0x40u;
      pm().maxs[2] = Fl(50.0f);
      p.viewHeightTarget = p.crouchViewHeight;
    } else {
      p.eFlags &= ~0x60u;
      pm().maxs[2] = p.maxs[2];
      p.viewHeightTarget = p.standViewHeight;
    }
    return finish();
  }
  // Prone.
  if (M->game && At<int32_t>(kProneDebug) == 2) {
    const Pmove& n = pm();
    ProneCheck(p.clientNum, n.ps + offsetof(PlayerState, origin), n.maxs[0], Fl(30.0f),
               p.viewangles[1], 0, 0, 0, n.traceUpright, 0);
  }
  p.eFlags = (p.eFlags | 0x40) & ~0x20u;
  p.viewHeightTarget = p.proneViewHeight;
  const F32 target = Fst(FromInt(p.viewHeightTarget));
  // The box's height: 30 when down, 50 for the first half of the way down.
  F32 height = Fl(50.0f);
  if (St(p.viewHeightCurrent) == target) {
    height = Fl(30.0f);
  } else if (p.viewHeightLerpTime) {
    const F32 elapsed = Fst(FromInt(pm().cmd.serverTime - p.viewHeightLerpTime));
    const F32 duration =
        Fst(FromInt(StanceDuration(p.viewHeightLerpTarget, p, p.viewHeightLerpDown)));
    const F32 frac = Fst(St(elapsed) / duration);
    if (!(St(frac) < 0.0f) && (St(frac) > 1.0f || St(frac) > 0.5f)) height = Fl(30.0f);
  }
  pm().maxs[2] = height;
  if (!wasProne) {
    // Just lay down: a dive when moving forward, and the ground's pitch.
    Pmove& n = pm();
    if (n.cmd.forwardmove || n.cmd.rightmove) {
      p.pm_flags &= ~0x400u;
      p.pm_flags &= ~0x20u;
    }
    if (!(p.pm_flags & 0x20) && n.cmd.forwardmove) {
      p.pm_flags |= 4;
      if (p.groundEntityNum != kEntityNumNone) {
        const F32 h = Fl(wasStanding ? 34.0f : 24.0f);
        const F32 g = Fst(FromInt(p.gravity));
        p.velocity[2] = Fst(CrtSqrt((St(h) + St(h)) * g, 0x20006313));
        pml().groundPlane = 0;
        pml().walking = 0;
        ps().groundEntityNum = kEntityNumNone;
      }
      ps().aimSpreadScale = Fl(255.0f);
    }
    // Up 10, down onto the ground again, and its normal.
    PlayerState& q = ps();
    const uint32_t origin = pm().ps + offsetof(PlayerState, origin);
    Copy3(point, q.origin);
    point[2] = Fst(St(point[2]) + 10.0f);
    trace(pm().traceProne, origin, pointVa, 0x20006395);
    Copy3(point, results.endpos);
    trace(pm().traceProne, pointVa, origin, 0x200063E7);
    Copy3(ps().origin, results.endpos);
    ps().proneDirection = ps().viewangles[1];
    Copy3(down, ps().origin);
    down[2] = Fst(St(down[2]) - 0.25f);
    trace(pm().traceProne, origin, downVa, 0x20006476);
    PlayerState& r = ps();
    if (!results.startsolid && St(results.fraction) < 1.0f)
      r.proneDirectionPitch = Fst(PitchForSlope(r.proneDirection, results.normal));
    else
      r.proneDirectionPitch = kZero;
    const F32 d = Fst(AngleNormalize180(Fst(St(r.proneDirectionPitch) - r.viewangles[0])));
    if (St(d) < -45.0f)
      r.proneTorsoPitch = Fst(St(r.viewangles[0]) - 45.0f);
    else if (St(d) > 45.0f)
      r.proneTorsoPitch = Fst(St(r.viewangles[0]) + 45.0f);
    else
      r.proneTorsoPitch = r.proneDirectionPitch;
  }
  finish();
}


// ---- ladders, prone angles, flying -----------------------------------------------------

// The narrower box of the ladder traces: 6 in from the sides, from 8 up.
void LadderBox(F32* mins, F32* maxs) {
  const Pmove& m = pm();
  mins[2] = Fl(8.0f);
  mins[0] = Fst(St(m.mins[0]) + 6.0f);
  mins[1] = Fst(St(m.mins[1]) + 6.0f);
  maxs[2] = m.maxs[2];
  maxs[0] = Fst(St(m.maxs[0]) - 6.0f);
  maxs[1] = Fst(St(m.maxs[1]) - 6.0f);
  if (St(m.maxs[2]) < 8.0f) maxs[2] = Fl(8.0f);
}

// PM_CheckLadderMove (0x200079E0): pm_flags 0x10 and the ladder's normal
// when a ladder surface (surface flag 8) is in front (8 units on the
// ground, 30 in the air; into the ladder while on one).
void CheckLadderMove() {
  if (ps().pm_time) return;
  const F32 dist = Fl(pml().walking ? 8.0f : 30.0f);
  PlayerState& p = ps();
  F32 dir[3];
  const uint32_t flags = p.pm_flags;
  if ((flags & 0x10) && p.groundEntityNum == kEntityNumNone) {
    for (int i = 0; i < 3; i++) dir[i] = Fst(-St(p.ladderNormal[i]));
  } else {
    dir[0] = pml().forward[0];
    dir[1] = pml().forward[1];
    dir[2] = kZero;
    VectorNormalize(dir);
  }
  p.pm_flags = flags & ~0x10u;
  if (p.pm_type >= 4) {
    p.groundEntityNum = kEntityNumNone;
    pml().groundPlane = 0;
    pml().walking = 0;
    return;
  }
  if (Stance(p) == 1) return;
  if (pm().cmd.serverTime - p.jumpTime < 300) return;
  GuestFrame f(0x54);
  F32* maxs = &f.at<F32>(0);
  F32* mins = &f.at<F32>(0xC);
  F32* end = &f.at<F32>(0x18);
  const uint32_t resultsVa = f.va(0x24);
  LadderBox(mins, maxs);
  for (int i = 0; i < 3; i++) end[i] = Fst(St(dir[i]) * dist + ps().origin[i]);
  TraceAll(resultsVa, pm().ps + offsetof(PlayerState, origin), f.va(0xC), f.va(0), f.va(0x18),
           ps().clientNum, pm().tracemask);
  const Trace& t = At<Trace>(resultsVa);
  if (!(St(t.fraction) < 1.0f) || !(t.surfaceFlags & 8)) return;
  if (pml().walking && pm().cmd.forwardmove <= 0) return;
  Copy3(ps().ladderNormal, t.normal);
  ps().pm_flags |= 0x10;
}

// The sign of a float's bits as a float: -1 or 1.
F32 SignOf(F32 v) { return Fst(FromInt(int32_t(v.bits) < 0 ? -1 : 1)); }

// PM_LadderMove (0x20007BF0): up and down by the view's pitch, sideways
// along the ladder, held against it and slowed when not climbing.
void LadderMove() {
  Pml& l = pml();
  if (CheckJump()) {
    AirMove();
    ps().jumpTime = pm().cmd.serverTime;
    return;
  }
  F32 upScale = Fst((St(l.forward[2]) + 0.25f) * 2.5f);
  if (St(upScale) > 1.0f)
    upScale = Fl(1.0f);
  else if (St(upScale) < -1.0f)
    upScale = Fl(-1.0f);
  l.forward[2] = kZero;
  VectorNormalize(l.forward);
  F32 right[3], wishvel[3], wishdir[3];
  l.right[2] = kZero;
  VectorNormalize2(l.right, right);
  ProjectPointOnPlane(l.right, right, ps().ladderNormal);
  const Pmove& m = pm();
  F32 scale = Fst(CmdScale(m.cmd));
  wishvel[2] = wishvel[1] = wishvel[0] = kZero;
  if (m.cmd.forwardmove)
    wishvel[2] = Fst(St(Fst(FromInt(m.cmd.forwardmove))) * 0.5f * scale * upScale);
  if (m.cmd.rightmove) {
    scale = Fst(St(Fst(FromInt(m.cmd.rightmove))) * scale * std::bit_cast<float>(0x3E4CCCCDu));
    wishvel[0] = Fst(St(scale) * l.right[0]);
    wishvel[1] = Fst(St(scale) * l.right[1]);
    wishvel[2] = Fst(St(scale) * l.right[2] + wishvel[2]);
  }
  const F32 wishspeed = Fst(VectorNormalize2(wishvel, wishdir));
  Accelerate(wishdir, wishspeed, Fl(9.0f));
  PlayerState& p = ps();
  F32* vel = p.velocity;
  if (!m.cmd.forwardmove) {
    // Not climbing: the vertical speed runs down at gravity.
    const F32 g = Fst(FromInt(p.gravity));
    if (St(vel[2]) > 0.0f) {
      vel[2] = Fst(vel[2] - St(g) * l.frametime);
      if (St(vel[2]) < 0.0f) vel[2] = kZero;
    } else {
      vel[2] = Fst(St(g) * l.frametime + vel[2]);
      if (St(vel[2]) > 0.0f) vel[2] = kZero;
    }
  }
  if (!m.cmd.rightmove) {
    // Not moving sideways: the sideways speed runs down too.
    F32 side[2] = {l.right[0], l.right[1]};
    Vector2Normalize(side);
    F32 d = Fst(St(side[1]) * vel[1] + St(side[0]) * vel[0]);
    if (!(St(d) == 0.0f)) {
      F32 n = Fst(-St(d));
      vel[0] = Fst(St(n) * side[0] + vel[0]);
      vel[1] = Fst(St(n) * side[1] + vel[1]);
      F32 step = Fst(St(16.0f) * l.frametime * d);
      const St stepAbs = Abs(St(step));
      if (Abs(St(d)) > stepAbs) {
        if (stepAbs < St(1.0)) step = SignOf(step);
        d = Fst(St(d) - step);
        vel[0] = Fst(St(d) * side[0] + vel[0]);
        vel[1] = Fst(St(d) * side[1] + vel[1]);
      }
    }
  }
  if (!l.walking) {
    // Against the ladder.
    const F32* n = p.ladderNormal;
    const F32 into = Fst(-St(Fst(St(n[1]) * vel[1] + St(n[0]) * vel[0])));
    vel[0] = Fst(St(into) * n[0] + vel[0]);
    vel[1] = Fst(St(into) * n[1] + vel[1]);
    const F32 push = Fl(St(wishvel[2]) > 0.0f ? -500.0f : -250.0f);
    vel[0] = Fst(St(push) * n[0] + vel[0]);
    vel[1] = Fst(St(push) * n[1] + vel[1]);
  }
  StepSlideMove(0);
  PlayerState& q = ps();
  const F32 yaw = Fst(VecToYaw(q.ladderNormal) + 180.0f);
  int32_t d = Ftol(AngleNormalize180(Fst(St(yaw) - q.viewangles[1])));
  const int32_t s = d >> 31;
  if ((d ^ s) - s > 75) d = d > 0 ? 75 : -75;
  q.movementDir = int8_t(d);
}

// Turning the prone body (0x200076B0): its pitch and the torso's toward the
// ground's slope along the prone direction and the view, at most 70 degrees
// a second. In the air the prone check runs (and fails, which it does not,
// with event 0x8D and pm_flags 0x8000); on steep ground, event 0x8D.
void ProneAngles() {
  PlayerState& p = ps();
  if (!(p.pm_flags & 1)) return;
  Pml& l = pml();
  if (p.groundEntityNum == kEntityNumNone) {
    const Pmove& m = pm();
    if (!ProneCheck(p.clientNum, m.ps + offsetof(PlayerState, origin), p.maxs[0], Fl(30.0f),
                    p.proneDirection, m.ps + offsetof(PlayerState, proneCheckHeight),
                    m.ps + offsetof(PlayerState, proneCheckPitch),
                    m.ps + offsetof(PlayerState, proneCheckTorsoPitch), m.traceUpright, 0)) {
      AddEvent(kEvCrouch);
      ps().pm_flags |= 0x8000;
    }
  } else if (l.groundPlane && St(l.groundTrace.normal[2]) < std::bit_cast<float>(0x3F333333u)) {
    AddEvent(kEvCrouch);
  }
  // pitch toward the slope along yaw
  const auto turn = [&](F32 yaw, F32& pitch) {
    const F32 target = l.groundPlane ? Fst(PitchForSlope(yaw, l.groundTrace.normal)) : kZero;
    const F32 delta = Fst(AngleNormalize180(Fst(St(target) - pitch)));
    if (St(delta) == 0.0f) return;
    const F32 step = Fst(St(l.frametime) * 70.0f);
    St add = delta;
    if (St(F32{delta.bits & 0x7FFFFFFFu}) > step) add = St(SignOf(delta)) * step;
    pitch = Fst(add + pitch);
    pitch = Fst(AngleNormalize180(pitch));
  };
  turn(ps().proneDirection, ps().proneDirectionPitch);
  turn(ps().viewangles[1], ps().proneTorsoPitch);
}

// The friction of noclip and of pm_type 3, as PM_Friction's.
void FlyFriction(St speed) {
  F32* vel = ps().velocity;
  const St friction = St(5.5f) * 1.5f;
  const St control = speed < 100.0f ? St(100.0f) : speed;
  St newspeed = speed - friction * (St(pml().frametime) * control);
  if (newspeed < 0.0f) newspeed = St(0.0f);
  newspeed = newspeed / speed;
  for (int i = 0; i < 3; i++) vel[i] = Fst(newspeed * vel[i]);
}

// PM_NoclipMove (0x200043F0; pm_type 2).
void NoclipMove() {
  PlayerState& p = ps();
  const int32_t height = At<int32_t>(M->flyViewHeight);
  p.viewHeightTarget = height;
  p.standViewHeight = height;
  F32* vel = p.velocity;
  const St v0(vel[0]), v1(vel[1]), v2(vel[2]);
  const St speed = Sqrt((v0 * v0 + v1 * v1) + v2 * v2);
  if (speed < 1.0f)
    vel[0] = vel[1] = vel[2] = kZero;
  else
    FlyFriction(speed);
  const Pmove& m = pm();
  const Pml& l = pml();
  const F32 scale = Fst(CmdScale(m.cmd));
  const F32 fmove = Fst(FromInt(m.cmd.forwardmove)), smove = Fst(FromInt(m.cmd.rightmove));
  F32 wishvel[3];
  wishvel[0] = Fst(St(l.right[0]) * smove + St(l.forward[0]) * fmove);
  wishvel[1] = Fst(St(l.right[1]) * smove + St(l.forward[1]) * fmove);
  wishvel[2] = Fst(FromInt(m.cmd.upmove) + (St(l.right[2]) * smove + St(l.forward[2]) * fmove));
  const F32 wishspeed = Fst(VectorNormalize(wishvel) * scale);
  Accelerate(wishvel, wishspeed, Fl(9.0f));
  PlayerState& q = ps();
  for (int i = 0; i < 3; i++) q.origin[i] = Fst(St(l.frametime) * q.velocity[i] + q.origin[i]);
}

// The move of pm_type 3 (0x200045D0): flying level, buttons 0x20 and 0x10
// for up and down.
void UfoMove() {
  PlayerState& p = ps();
  const int32_t height = At<int32_t>(M->flyViewHeight);
  p.viewHeightTarget = height;
  p.standViewHeight = height;
  Pmove& m = pm();
  // The original's byte arithmetic: + (0x20 x 0x81) and + (0x10 x 0x7F).
  m.cmd.upmove = int8_t(uint8_t(m.cmd.upmove) + uint8_t((m.cmd.wbuttons & 0x20) * 0x81));
  m.cmd.upmove = int8_t(uint8_t(m.cmd.upmove) + uint8_t((m.cmd.wbuttons & 0x10) * 0x7F));
  F32* vel = p.velocity;
  bool stop;
  F32 speed;
  if (!m.cmd.forwardmove && !m.cmd.rightmove) {
    stop = St(Fst(FromInt(m.cmd.upmove))) == 0.0f;
    if (!stop) {
      vel[1] = kZero;
      vel[0] = kZero;
      vel[2] = Fst(FromInt(m.cmd.upmove));
      speed = Fl(127.0f);
    }
  } else {
    const F32 x = vel[0], y = vel[1], z = vel[2];
    speed = Fst(CrtSqrt((St(x) * x + St(y) * y) + St(z) * z, 0x200046E5));
    stop = St(speed) < 1.0f;
  }
  if (stop) {
    vel[0] = vel[1] = vel[2] = kZero;
  } else {
    const F32 friction = Fst(St(5.5f) * 1.5f);
    const F32 control = St(speed) < 100.0f ? Fl(100.0f) : speed;
    const F32 drop = Fst(St(pml().frametime) * control * friction);
    F32 newspeed = Fst(St(speed) - drop);
    if (St(newspeed) < 0.0f) newspeed = kZero;
    newspeed = Fst(St(newspeed) / speed);
    for (int i = 0; i < 3; i++) vel[i] = Fst(St(newspeed) * vel[i]);
  }
  const Pml& l = pml();
  const F32 scale = Fst(CmdScale(pm().cmd));
  const F32 fmove = Fst(FromInt(pm().cmd.forwardmove)), smove = Fst(FromInt(pm().cmd.rightmove));
  // Forward level: right x up.
  const F32 t = Fst(St(l.right[2]) * 0.0f);
  const F32 fwd[3] = {Fst(St(t) - l.right[1]), Fst(St(l.right[0]) - t),
                      Fst(St(l.right[1]) * 0.0f - St(l.right[0]) * 0.0f)};
  F32 wishvel[3];
  for (int i = 0; i < 3; i++) wishvel[i] = Fst(St(l.right[i]) * smove + St(fwd[i]) * fmove);
  const F32 up = Fst(FromInt(pm().cmd.upmove));
  wishvel[2] = Fst(St(up) + St(up) + wishvel[2]);
  const F32 wishspeed = Fst(St(Fst(VectorNormalize(wishvel))) * scale);
  Accelerate(wishvel, wishspeed, Fl(9.0f));
  PlayerState& q = ps();
  for (int i = 0; i < 3; i++) q.origin[i] = Fst(St(l.frametime) * q.velocity[i] + q.origin[i]);
}


// ---- view angles and lean -----------------------------------------------------------------

// AddLeanToPosition (0x2004B710; EDX position; yaw, lean, leanMax, radius
// on the stack): the position moved sideways and rolled by a lean of -1..1.
void AddLeanToPosition(F32* position, F32 yaw, F32 lean, F32 leanMax, F32 radius) {
  if (St(lean) == 0.0f) return;
  const F32 t = Fst((St(2.0f) - F32{lean.bits & 0x7FFFFFFFu}) * lean);
  const F32 roll = Fst(St(t) * leanMax);
  F32 sy, cy, sp, cp, sr, cr;
  SinCos(Fst(St(yaw) * kDeg2Rad), &sy, &cy);
  SinCos(kZero, &sp, &cp);
  SinCos(Fst(St(roll) * kDeg2Rad), &sr, &cr);
  const F32 u = Fst(St(sr) * sp);
  F32 right[3];
  right[0] = Fst(St(cr) * sy - St(u) * cy);
  right[1] = Fst(St(cr) * cy * -1.0f - St(u) * sy);
  right[2] = Fst(St(sr) * cp * -1.0f);
  const F32 r = Fst(St(t) * radius);
  for (int i = 0; i < 3; i++) position[i] = Fst(St(right[i]) * r + position[i]);
}

// PM_UpdateLean (0x20006D30, ESI ps, EAX cmd; the trace on the stack): the
// lean (ps->leanf, -1..1) toward the lean buttons (wbuttons 0x10 left, 0x20
// right) at a rate by stance, limited by a trace to the leaned eye.
void UpdateLean(PlayerState& p, const UserCmd& cmd, uint32_t traceFn) {
  int32_t lean = 0;
  const uint8_t b = cmd.wbuttons;
  if ((b & 0x30) && !(p.pm_flags & 0x4000) && p.pm_type < 4 &&
      (p.groundEntityNum != kEntityNumNone || p.pm_type == 1)) {
    if ((b & 0x10) && !(p.pm_flags & 0x100000)) lean = -1;
    if ((b & 0x20) && !(p.pm_flags & 0x200000)) lean++;
  }
  if (p.eFlags & 0x2C000) lean = 0;
  const int32_t stance = Stance(p);
  const F32 rate = Fl(stance == 1 ? 0.25f : stance == 2 ? 0.5f : 1.0f);
  const St msec = FromInt(pml().msec);
  St f = p.leanf;
  if (lean == 0) {
    const float k = std::bit_cast<float>(0x3B6A0EA1u);  // 1/280
    if (f > 0.0f) {
      f = f - msec * k * rate;
      if (f < 0.0f) f = St(0.0f);
    } else if (f < 0.0f) {
      f = f + msec * k * rate;
      if (f > 0.0f) f = St(0.0f);
    }
  } else {
    const float k = std::bit_cast<float>(0x3B3B3EE7u);  // 1/350
    if (lean > 0) {
      if (f < rate) f = f + msec * k * rate;
      if (f > rate) f = rate;
    } else {
      const F32 neg = Fst(-St(rate));
      if (f > neg) f = f - msec * k * rate;
      if (f < neg) f = neg;
    }
  }
  p.leanf = Fst(f);
  if (f == 0.0f || p.pm_type == 1) return;
  // Not further than the eye can go.
  GuestFrame g(0x68);
  F32* eye = &g.at<F32>(8);
  F32* start = &g.at<F32>(0x14);
  F32* maxs = &g.at<F32>(0x20);
  F32* mins = &g.at<F32>(0x2C);
  const uint32_t resultsVa = g.va(0x38);
  const F32 leanF = Fst(f);
  start[0] = eye[0] = Fst(St(p.origin[0]));  // through the x87: an SNaN quietened
  start[1] = eye[1] = Fst(St(p.origin[1]));
  start[2] = eye[2] = Fst(St(p.origin[2]) + p.viewHeightCurrent);
  AddLeanToPosition(eye, p.viewangles[1], SignOf(leanF), Fl(16.0f), Fl(20.0f));
  for (int i = 0; i < 3; i++) {
    mins[i] = Fl(-8.0f);
    maxs[i] = Fl(8.0f);
  }
  Push(0x281C011);
  Push(uint32_t(p.clientNum));
  Push(g.va(8));
  Push(g.va(0x20));
  Push(g.va(0x2C));
  Push(g.va(0x14));
  Push(resultsVa);
  CallPtr(traceFn, M->game ? 0x20006F7B : 0x300066BB);
  C->esp += 0x1C;
  const St limit = 1.0f - Sqrt(St(1.0f) - At<Trace>(resultsVa).fraction);
  if (limit < Abs(St(p.leanf))) p.leanf = Fst(St(SignOf(p.leanf)) * limit);
}

// A turn of the view by an angle limit (ladders, prone): delta_angles[1]
// moves by the overshoot, and the yaw lands on base -/+ limit.
void ClampYaw(PlayerState& p, F32 d, int32_t limit, F32 base) {
  const F32 limitF = Fst(FromInt(limit));
  if (!(St(d) > limitF) && !(St(d) < Fst(FromInt(-limit)))) return;
  const F32 over = St(d) > limitF ? Fst(St(d) - limitF) : Fst(St(limitF) + d);
  p.delta_angles[1] += Ftol(St(over) * std::bit_cast<float>(0x43360B61u)) & 0xFFFF;
  const F32 l = Fst(FromInt(limit));
  const int32_t yaw = Ftol((St(over) > 0.0f ? St(base) - l : St(l) + base) *
                           std::bit_cast<float>(0x43360B61u)) &
                      0xFFFF;
  p.viewangles[1] = Fst(St(Fst(FromInt(yaw))) * 0.0054931640625f);
}

// PM_UpdateViewAngles (0x20006FD0 (EAX ps; cmd, trace on the stack) /
// 0x30006710 (EAX ps, EDI cmd; trace on the stack)): the view from the
// command's angles and delta_angles, pitch within -/+88 degrees (16000);
// on a ladder within its yaw limit; prone, the body turns toward the view
// as far as the prone check allows, the view stays within the prone yaw
// limit of it and within 45 degrees of the torso's pitch; then the lean.
void UpdateViewAngles(PlayerState& p, const UserCmd& cmd, uint32_t traceFn) {
  if (p.pm_type >= 4) return UpdateLean(p, cmd, traceFn);
  for (int i = 0; i < 3; i++) {
    int16_t temp = int16_t(uint16_t(p.delta_angles[i]) + uint16_t(cmd.angles[i]));
    if (i == 0) {
      if (temp > 16000) {
        p.delta_angles[0] = 16000 - cmd.angles[0];
        temp = 16000;
      } else if (temp < -16000) {
        p.delta_angles[0] = -16000 - cmd.angles[0];
        temp = -16000;
      }
    }
    p.viewangles[i] = Fst(St(Fst(FromInt(temp))) * 0.0054931640625f);
  }
  const uint32_t flags = p.pm_flags;
  if ((flags & 0x10) && p.groundEntityNum == kEntityNumNone) {
    if (const int32_t cap = At<int32_t>(M->ladderYawCap)) {
      const F32 ladderYaw = Fst(VecToYaw(p.ladderNormal) + 180.0f);
      const F32 d = Fst(AngleNormalize180(Fst(St(ladderYaw) - p.viewangles[1])));
      ClampYaw(p, d, cap, ladderYaw);
    }
  }
  if (!(flags & 1) || (p.eFlags & 0xC000)) {
    if (p.pm_type != 3 && p.pm_type != 2) UpdateLean(p, cmd, traceFn);
    return;
  }
  if (M->game && At<int32_t>(kProneDebug)) {
    // The game draws the view and the prone yaw limits.
    GuestFrame g(0x58);
    const uint32_t E = g.va(0x10);
    const auto v = [&](uint32_t off) { return &At<F32>(E + off); };
    F32* a = v(0x24);
    F32* b = v(0x30);
    F32* fwd = v(0x3C);
    const F32 height = Fst(FromInt(p.proneViewHeight));
    a[0] = p.origin[0];
    a[1] = p.origin[1];
    a[2] = Fst(St(height) + p.origin[2]);
    F32 sy, cy, sp, cp;
    SinCos(Fst(St(p.viewangles[1]) * kDeg2Rad), &sy, &cy);
    SinCos(Fst(St(p.viewangles[0]) * kDeg2Rad), &sp, &cp);
    fwd[0] = Fst(St(cp) * cy);
    fwd[1] = Fst(St(cp) * sy);
    fwd[2] = Fst(-St(sp));
    for (int i = 0; i < 3; i++) b[i] = Fst(St(fwd[i]) * 18.0f + a[i]);
    Push(1);
    Push(1);
    Push(0x2008A120);
    Push(E + 0x30);
    Push(E + 0x24);
    Push(0x52);
    CallPtr(At<uint32_t>(kGameSyscall), 0x200072DD);
    C->esp += 0x18;
    const F32 cap = At<F32>(M->proneYawCap - 4);  // the cvar's value
    Push(1);
    Push(0x2008A120);
    Push(Fst(St(cap) + p.proneDirection).bits);
    Push(Fst(St(p.proneDirection) - cap).bits);
    Push(0x41800000);  // 16
    C->eax = E + 0x24;
    C->ebx = 1;
    Call(f_200194F0, 0x2000731C);
    C->esp += 0x14;
  }
  F32 d =
      Fst(AngleNormalize180(Fst(AngleNormalize180(Fst(St(p.proneDirection) - p.viewangles[1])))));
  int32_t cap = At<int32_t>(M->proneYawCap);
  bool turn;
  if (At<int32_t>(M->proneYawCheck) &&
      (St(d) > Fst(FromInt(cap - 5)) || St(d) < Fst(FromInt(5 - cap))))
    turn = true;
  else
    turn = (cmd.forwardmove || cmd.rightmove) && !(St(d) == 0.0f);
  if (turn) {
    // The body toward the view, 55 degrees a second.
    const F32 step = Fst(St(pml().frametime) * 55.0f);
    F32 dir;
    if (St(step) > Abs(St(d)))
      dir = p.viewangles[1];
    else
      dir = St(d) > 0.0f ? Fst(St(p.proneDirection) - step) : Fst(St(step) + p.proneDirection);
    const Pmove& m = pm();
    if (ProneCheck(p.clientNum, m.ps + offsetof(PlayerState, origin), p.maxs[0], Fl(30.0f), dir,
                   m.ps + offsetof(PlayerState, proneCheckHeight),
                   m.ps + offsetof(PlayerState, proneCheckPitch),
                   m.ps + offsetof(PlayerState, proneCheckTorsoPitch), traceFn, 0)) {
      p.proneDirection = dir;
    } else {
      const F32 limit = Fst(St(Fst(FromInt(cap))) + std::bit_cast<float>(0x3DCCCCCDu));
      if (St(d) > limit || -St(limit) > d) p.pm_flags |= 0x8000;
    }
    cap = At<int32_t>(M->proneYawCap);
  }
  ClampYaw(p, d, cap, p.proneDirection);
  // The pitch within 45 of the torso's.
  const F32 pitch =
      Fst(AngleNormalize180(Fst(AngleNormalize180(Fst(St(p.proneTorsoPitch) - p.viewangles[0])))));
  if (St(pitch) > 45.0f || St(pitch) < -45.0f) {
    const F32 over = St(pitch) > 45.0f ? Fst(St(pitch) - 45.0f) : Fst(St(pitch) + 45.0f);
    p.delta_angles[0] += Ftol(St(over) * std::bit_cast<float>(0x43360B61u)) & 0xFFFF;
    const F32 a = St(over) > 0.0f ? Fst(St(p.proneTorsoPitch) - 45.0f)
                                  : Fst(St(p.proneTorsoPitch) + 45.0f);
    p.viewangles[0] = Fst(AngleNormalize180(a));
  }
  if (p.pm_type != 3 && p.pm_type != 2) UpdateLean(p, cmd, traceFn);
}


// ---- weapons ----------------------------------------------------------------------------

// weaponDef_t (the fields used here).
constexpr uint32_t kWdType = 0x70;  // 1: a grenade
constexpr uint32_t kWdSlot = 0x78;  // 1-2 primary, 3-5 the slot of their own
constexpr uint32_t kWdTakeOthers = 0x7C;
constexpr uint32_t kWdAmmoIndex = 0x1A0, kWdClipIndex = 0x1A8;
constexpr uint32_t kWdMelee = 0x1C4, kWdFireDelay = 0x1CC, kWdMeleeDelay = 0x1D0,
                   kWdFireTime = 0x1D4, kWdRechamberTime = 0x1D8, kWdRechamberBolt = 0x1DC,
                   kWdHoldFireTime = 0x1E0, kWdMeleeTime = 0x1E4, kWdReloadTime = 0x1E8,
                   kWdReloadEmptyTime = 0x1EC, kWdReloadAddTime = 0x1F0, kWdReloadStartTime = 0x1F4,
                   kWdReloadStartAddTime = 0x1F8, kWdReloadEndTime = 0x1FC, kWdDropTime = 0x200,
                   kWdRaiseTime = 0x204, kWdAltDropTime = 0x208, kWdAltRaiseTime = 0x20C,
                   kWdFuseTime = 0x210;
constexpr uint32_t kWdAimSpread = 0x24C, kWdAimSpreadFire = 0x250, kWdAimSpreadTurn = 0x254,
                   kWdAimSpreadMove = 0x258, kWdAimSpreadDuck = 0x25C, kWdAimSpreadProne = 0x260;
constexpr uint32_t kWdNoAutoFire = 0x2C4, kWdRechamber = 0x2C8, kWdAds = 0x2CC,
                   kWdCookOff = 0x2D0, kWdDropWhenEmpty = 0x2D4, kWdAdsFire = 0x2DC,
                   kWdReloadCanAdd = 0x2E8, kWdSegmentedReload = 0x2EC, kWdReloadAmmoAdd = 0x2F0,
                   kWdReloadStartAdd = 0x2F4, kWdAltWeapon = 0x2FC, kWdAdsReloadTime = 0x3D4,
                   kWdAdsTransIn = 0x414, kWdAdsTransOut = 0x418;

template <typename T = int32_t>
T& Wd(uint32_t wd, uint32_t off) {
  return At<T>(wd + off);
}
uint32_t WeaponDef(int32_t weapon) {
  return At<uint32_t>(At<uint32_t>(M->weaponDefs) + uint32_t(weapon) * 4);
}
uint32_t CurWeaponDef() { return pml().weaponDef; }
int32_t NumWeapons() { return At<int32_t>(M->numWeapons); }
int32_t ClipSize(int32_t clip) { return At<int32_t>(M->clipSizes + uint32_t(clip) * 4); }

bool HasBit(const uint32_t* bits, int32_t n) { return bits[n >> 5] & (1u << (n & 31)); }
void ClearBit(uint32_t* bits, int32_t n) { bits[n >> 5] &= ~(1u << (n & 31)); }

// weaponstate
enum : int32_t {
  kWsReady,
  kWsRaising,
  kWsDropping,
  kWsFiring,
  kWsRechambering,
  kWsReloading,
  kWsReloadingInterupt,
  kWsReloadStart,
  kWsReloadStartInterupt,
  kWsReloadEnd,
  kWsMeleeWindup,
  kWsMeleeRelax,
};
// weapAnim
enum : uint32_t {
  kAnimIdle = 0,
  kAnimAttack = 2,
  kAnimAttackLastShot,
  kAnimRechamber,
  kAnimAdsAttack,
  kAnimAdsAttackLastShot,
  kAnimAdsRechamber,
  kAnimMeleeAttack,
  kAnimDrop,
  kAnimRaise,
  kAnimReload,
  kAnimReloadEmpty,
  kAnimReloadStart,
  kAnimReloadEnd,
  kAnimAltSwitchFrom,
  kAnimAltSwitchTo,
};

// The weapon slot that holds weapon (1-5; 0 for none), following the
// alternate weapons when alt (0x2000B320, EDI ps; weapon, alt on the stack).
int32_t WeaponSlot(const PlayerState& p, int32_t weapon, int32_t alt) {
  if (!HasBit(p.weapons, weapon)) return 0;
  int32_t w = weapon;
  do {
    const uint32_t wd = WeaponDef(w);
    const int32_t slot = Wd(wd, kWdSlot);
    if (slot == 1 || slot == 2) {
      if (int8_t(p.weaponslots[1]) == w) return 1;
      if (int8_t(p.weaponslots[2]) == w) return 2;
    } else if (slot >= 3 && slot <= 5) {
      if (int8_t(p.weaponslots[slot]) == w) return slot;
    } else {
      return 0;
    }
    if (alt && Wd(wd, kWdAltWeapon)) w = Wd(wd, kWdAltWeapon);
  } while (w != weapon);
  return 0;
}

// Puts weapon in slot when it fits there (0x2000B2B0, EAX weapon, ESI
// slot, EDI ps): 1 if it did.
int32_t SetWeaponSlot(PlayerState& p, int32_t weapon, int32_t slot) {
  if (!HasBit(p.weapons, weapon)) return 0;
  const int32_t type = Wd(WeaponDef(weapon), kWdSlot);
  if (type == 1 || type == 2) {
    if (slot != 1 && slot != 2) return 0;
  } else if (type >= 3 && type <= 5) {
    if (slot != type) return 0;
  } else {
    return 0;
  }
  p.weaponslots[slot] = uint8_t(weapon);
  return 1;
}

// BG_TakePlayerWeapon-like (0x2000B150, EAX weapon, ECX ps): the weapon and
// its alternates gone, its slot given to another weapon of the kind.
int32_t TakePlayerWeapon(PlayerState& p, int32_t weapon) {
  if (!HasBit(p.weapons, weapon)) return 0;
  const uint32_t wd = WeaponDef(weapon);
  if (const int32_t slot = WeaponSlot(p, weapon, 1)) {
    // To the first weapon in no slot (the slot still holds this one).
    uint8_t other = 0;
    if (Wd(wd, kWdTakeOthers)) {
      for (int32_t w = 1; w <= NumWeapons(); w++) {
        if (HasBit(p.weapons, w) && !WeaponSlot(p, w, 1)) {
          other = uint8_t(w);
          break;
        }
      }
    }
    p.weaponslots[slot] = other;
  }
  ClearBit(p.weapons, weapon);
  for (int32_t alt = Wd(wd, kWdAltWeapon); alt && HasBit(p.weapons, alt);
       alt = Wd(WeaponDef(alt), kWdAltWeapon))
    ClearBit(p.weapons, alt);
  return 1;
}

// PM_StartWeaponAnim (0x2000BD70; anim on the stack).
void StartWeaponAnim(uint32_t anim) {
  PlayerState& p = ps();
  if (p.pm_type < 4 && pm().cmd.weapon) p.weapAnim = (~p.weapAnim & 0x200) | anim;
}
// PM_ContinueWeaponAnim (0x2000BDA0, EDX anim).
void ContinueWeaponAnim(uint32_t anim) {
  if (!pm().cmd.weapon) return;
  PlayerState& p = ps();
  if ((p.weapAnim & ~0x200u) == anim || p.pm_type >= 4) return;
  p.weapAnim = (~p.weapAnim & 0x200) | anim;
}
// The idle anim (toggled when it was not idle) and the ready state (0x2000BDE0).
void WeaponIdle() {
  PlayerState& p = ps();
  if (pm().cmd.weapon && (p.weapAnim & ~0x200u) && p.pm_type < 4) p.weapAnim = ~p.weapAnim & 0x200;
  p.weaponstate = kWsReady;
}

// ps->ammoclip of a weapon (0x2000BD20, ECX weapon) and its use (0x2000BCF0,
// ECX weapon; amount on the stack).
int32_t& ClipOf(int32_t weapon) { return ps().ammoclip[Wd(WeaponDef(weapon), kWdClipIndex)]; }

// The sights (0x2000BA10): weaponPosFrac toward 1 while aiming down them
// (pm_flags 0x20, not while reloading; or firing a weapon that fires from
// them), else toward 0.
void UpdateAimDownSight() {
  const uint32_t wd = CurWeaponDef();
  PlayerState& p = ps();
  if (!Wd(wd, kWdAds)) {
    p.weaponPosFrac = kZero;
    return;
  }
  const int32_t ws = p.weaponstate;
  const bool late = p.weaponTime - Wd(wd, kWdAdsReloadTime) > 0;
  bool blocked;
  if (!Wd(wd, kWdSegmentedReload))
    blocked = ws == kWsReloading && late;
  else
    blocked = ws == kWsReloading || ws == kWsReloadingInterupt || ws == kWsReloadStart ||
              ws == kWsReloadStartInterupt || (ws == kWsReloadEnd && late);
  bool ads = !blocked && (p.pm_flags & 0x20);
  if (Wd(wd, kWdAdsFire) && p.weaponDelay && p.weaponstate == kWsFiring) ads = true;
  const St msec = FromInt(pml().msec);
  if (ads) {
    if (p.weaponPosFrac.bits == 0x3F800000u) return;
    p.weaponPosFrac = Fst(msec * Wd<F32>(wd, kWdAdsTransIn) + p.weaponPosFrac);
  } else {
    if (St(p.weaponPosFrac) == 0.0f) return;
    p.weaponPosFrac = Fst(p.weaponPosFrac - msec * Wd<F32>(wd, kWdAdsTransOut));
  }
  if (St(p.weaponPosFrac) >= 1.0f) {
    p.weaponPosFrac = Fl(1.0f);
  } else if (St(p.weaponPosFrac) <= 0.0f) {
    p.weaponPosFrac = kZero;
  }
}

// PM_ReloadClip (0x2000BC30): ammo into the clip, all of it or the reload's
// share (segmented reloads).
void ReloadClip() {
  PlayerState& p = ps();
  const uint32_t wd = CurWeaponDef();
  const int32_t ws = p.weaponstate;
  const bool start = ws == kWsReloadStart || ws == kWsReloadStartInterupt;
  if (start && !Wd(wd, kWdReloadStartAdd)) return;
  const uint32_t weap = WeaponDef(p.weapon);
  const int32_t ammoIndex = Wd(weap, kWdAmmoIndex), clipIndex = Wd(weap, kWdClipIndex);
  const int32_t ammo = p.ammo[ammoIndex], clipSize = ClipSize(clipIndex);
  int32_t n = clipSize - p.ammoclip[clipIndex];
  if (n > ammo) n = ammo;
  const int32_t add = start ? Wd(wd, kWdReloadStartAdd) : Wd(wd, kWdReloadAmmoAdd);
  if ((start || add) && add < clipSize && n > add) n = add;
  if (!n) return;
  p.ammo[ammoIndex] = ammo - n;
  ps().ammoclip[clipIndex] += n;
}

// The rechamber flag (0x2000BE30; the weapon timers' result on the stack):
// 1 when the bolt is worked now (event 0xA3), else 0 (and back to ready
// after a rechamber, or a rechamber started: event 0xA2).
int32_t CheckRechamber(int32_t timersDone) {
  const uint32_t wd = CurWeaponDef();
  if (!Wd(wd, kWdRechamber)) return 0;
  PlayerState& p = ps();
  if (!HasBit(p.weaponrechamber, p.weapon)) return 0;
  if (p.weaponstate == kWsRechambering && timersDone) {
    ClearBit(p.weaponrechamber, p.weapon);
    AddEvent(0xA3);
    if (ps().weaponTime) return 1;
  } else if (p.weaponTime) {
    const int32_t ws = p.weaponstate;
    if (ws == kWsFiring || ws == kWsRechambering || ws == kWsMeleeWindup || ws == kWsMeleeRelax)
      return 0;
    if (p.weaponDelay) return 0;
  }
  PlayerState& q = ps();
  if (q.weaponstate == kWsRechambering) {
    WeaponIdle();
    return 0;
  }
  if (q.weaponstate != kWsReady) return 0;
  StartWeaponAnim(St(q.weaponPosFrac) > 0.75f ? kAnimAdsRechamber : kAnimRechamber);
  ps().weaponstate = kWsRechambering;
  ps().weaponTime = Wd(CurWeaponDef(), kWdRechamberTime);
  const int32_t bolt = Wd(CurWeaponDef(), kWdRechamberBolt);
  ps().weaponDelay = bolt && bolt < Wd(CurWeaponDef(), kWdRechamberTime) ? bolt : 1;
  AddEvent(0xA2);
  return 0;
}

// The delay to the clip's refill in a reload (0x2000BFD0): weaponDelay.
void SetReloadDelay() {
  PlayerState& p = ps();
  const uint32_t wd = CurWeaponDef();
  int32_t t;
  if (p.weaponstate == kWsReloadStart || p.weaponstate == kWsReloadStartInterupt) {
    t = Wd(wd, kWdReloadStartAddTime);
    if (t && t >= Wd(wd, kWdReloadStartTime)) t = Wd(wd, kWdReloadStartTime);
  } else {
    t = !p.ammoclip[Wd(WeaponDef(p.weapon), kWdClipIndex)] && !Wd(wd, kWdType)
            ? Wd(wd, kWdReloadEmptyTime)
            : Wd(wd, kWdReloadTime);
    const int32_t add = Wd(wd, kWdReloadAddTime);
    if (add && add < t) t = add;
  }
  if (Wd(wd, kWdRechamber) && HasBit(p.weaponrechamber, p.weapon)) {
    // A rechambering weapon: at the bolt time (or 1).
    if (!t) t = p.weaponTime;
    const int32_t bolt = Wd(wd, kWdRechamberBolt);
    p.weaponDelay = bolt < t && bolt ? bolt : 1;
    return;
  }
  if (t) p.weaponDelay = t;
}

// PM_BeginWeaponReload (0x2000C0B0): the reload anim, time and event.
void BeginWeaponReload() {
  PlayerState& p = ps();
  const uint32_t wd = CurWeaponDef();
  const bool empty = !p.ammoclip[Wd(WeaponDef(p.weapon), kWdClipIndex)] && !Wd(wd, kWdType);
  if (p.pm_type < 4 && pm().cmd.weapon)
    p.weapAnim = (~p.weapAnim & 0x200) | (empty ? kAnimReloadEmpty : kAnimReload);
  p.weaponTime = empty ? Wd(wd, kWdReloadEmptyTime) : Wd(wd, kWdReloadTime);
  AddEvent(empty ? 0x98 : 0x97);
  ps().weaponstate = ps().weaponstate == kWsReloadStartInterupt ? kWsReloadingInterupt
                                                                : kWsReloading;
  SetReloadDelay();
}

// The start of a reload (0x2000C1A0): segmented reloads begin with their
// start (event 0x99).
void StartReload() {
  PlayerState& p = ps();
  const int32_t ws = p.weaponstate;
  if (ws != kWsReady && ws != kWsFiring && ws != kWsRechambering) return;
  if (!p.weapon || p.weapon > NumWeapons()) return;
  const uint32_t wd = CurWeaponDef();
  if (!Wd(wd, kWdSegmentedReload) || !Wd(wd, kWdReloadStartTime)) return BeginWeaponReload();
  StartWeaponAnim(kAnimReloadStart);
  ps().weaponTime = Wd(CurWeaponDef(), kWdReloadStartTime);
  ps().weaponstate = kWsReloadStart;
  AddEvent(0x99);
  SetReloadDelay();
}

// PM_BeginWeaponChange (0x2000C250; old weapon, new weapon on the stack).
void BeginWeaponChange(int32_t oldWeapon, int32_t newWeapon) {
  if (newWeapon < 0 || newWeapon > NumWeapons()) return;
  PlayerState& p = ps();
  if (newWeapon && !HasBit(p.weapons, newWeapon)) return;
  if (p.weaponstate == kWsDropping) return;
  p.weaponDelay = 0;
  if (!oldWeapon || !HasBit(p.weapons, oldWeapon) || p.grenadeTimeLeft > 0) {
    p.weaponTime = 0;
    p.weaponstate = kWsDropping;
    p.grenadeTimeLeft = 0;
    if (p.pm_flags & 1) p.pm_flags |= 0x400;
    return;
  }
  const uint32_t wd = WeaponDef(oldWeapon);
  const bool alt = newWeapon && newWeapon == Wd(wd, kWdAltWeapon);
  const bool anim = !Wd(wd, kWdDropWhenEmpty) || p.ammoclip[Wd(wd, kWdClipIndex)];
  p.grenadeTimeLeft = 0;
  if (alt) {
    AddEvent(0x9D);
    StartWeaponAnim(kAnimAltSwitchFrom);
  } else if (anim) {
    AddEvent(0x9C);
    StartWeaponAnim(kAnimDrop);
  }
  ps().weaponstate = kWsDropping;
  SetProneFlag400();
  if (!alt) {
    ps().weaponTime = Wd(wd, kWdDropTime);
    return;
  }
  ps().weaponTime = Wd(wd, kWdAltDropTime);
  PlayerState& q = ps();
  const int32_t slot = WeaponSlot(q, oldWeapon, 1);
  if (slot) SetWeaponSlot(q, newWeapon, slot);
}

// PM_FinishWeaponChange (0x2000C3F0): 1 when it ran (dropping done: the new
// weapon raised, event 0x9B unless it is the alternate).
int32_t FinishWeaponChange() {
  Pmove& m = pm();
  PlayerState& p = ps();
  if (p.weaponstate != kWsDropping) return 0;
  int32_t weapon = m.cmd.weapon;
  if ((p.pm_flags & 0x10) || !HasBit(p.weapons, weapon) ||
      ((p.eFlags & 0x20000) && !(p.eFlags & 0x80000)) || weapon < 0 || weapon > NumWeapons())
    weapon = 0;
  if (!HasBit(p.weapons, weapon)) weapon = 0;
  const int32_t old = p.weapon;
  p.weapon = uint8_t(weapon);
  pml().weaponDef = WeaponDef(p.weapon);
  if (old == weapon) {
    p.weaponstate = kWsReady;
    if (p.pm_type < 4 && m.cmd.weapon) p.weapAnim = ~p.weapAnim & 0x200;
    return 1;
  }
  p.weaponstate = kWsRaising;
  if (!old) {
    p.weaponTime = Wd(WeaponDef(weapon), kWdRaiseTime);
    p.aimSpreadScale = Fl(255.0f);
    StartWeaponAnim(kAnimRaise);
    SetProneFlag400();
    return 1;
  }
  SetProneFlag400();
  const bool alt = weapon && weapon == Wd(WeaponDef(old), kWdAltWeapon);
  if (!alt) AddEvent(0x9B);
  ps().weaponTime = Wd(WeaponDef(weapon), alt ? kWdAltRaiseTime : kWdRaiseTime);
  if (!alt) {
    ps().aimSpreadScale = Fl(255.0f);
    StartWeaponAnim(kAnimRaise);
    return 1;
  }
  if (St(ps().aimSpreadScale) < 128.0f) ps().aimSpreadScale = Fl(128.0f);
  StartWeaponAnim(kAnimAltSwitchTo);
  return 1;
}

// Raising done (0x2000C5E0): ready, 1.
int32_t FinishRaise() {
  PlayerState& p = ps();
  if (p.weaponstate != kWsRaising) return 0;
  p.weaponstate = kWsReady;
  if (p.pm_type < 4 && pm().cmd.weapon) p.weapAnim = ~p.weapAnim & 0x200;
  return 1;
}

// Whether a reload can add ammo (0x2000C630).
int32_t CanReload() {
  const PlayerState& p = ps();
  const uint32_t weap = WeaponDef(p.weapon);
  const int32_t clipIndex = Wd(weap, kWdClipIndex);
  if (!p.ammo[Wd(weap, kWdAmmoIndex)]) return 0;
  const int32_t clip = p.ammoclip[clipIndex], size = ClipSize(clipIndex);
  if (clip >= size) return 0;
  const uint32_t wd = CurWeaponDef();
  if (!Wd(wd, kWdReloadCanAdd)) return 1;
  const int32_t add = Wd(wd, kWdReloadAmmoAdd);
  if (add && add < size) return size - clip < add ? 0 : 1;
  return clip ? 0 : 1;
}

// The reload's ammo at its moment (0x2000C6B0): a rechambering weapon works
// the bolt then (event 0xA3) and refills later; otherwise ReloadClip.
void ReloadAmmo() {
  const uint32_t wd = CurWeaponDef();
  PlayerState& p = ps();
  if (!Wd(wd, kWdRechamber) || !HasBit(p.weaponrechamber, p.weapon)) return ReloadClip();
  ClearBit(p.weaponrechamber, p.weapon);
  AddEvent(0xA3);
  PlayerState& q = ps();
  const int32_t ws = q.weaponstate;
  const bool start = ws == kWsReloadStart || ws == kWsReloadStartInterupt;
  if (start && !Wd(wd, kWdReloadStartAddTime)) return;
  if (!q.weaponTime) return ReloadClip();
  int32_t t;
  if (start) {
    t = Wd(wd, kWdReloadStartAddTime);
    if (t >= Wd(wd, kWdReloadStartTime)) t = Wd(wd, kWdReloadStartTime);
  } else {
    t = !q.ammoclip[Wd(WeaponDef(q.weapon), kWdClipIndex)] && !Wd(wd, kWdType)
            ? Wd(wd, kWdReloadEmptyTime)
            : Wd(wd, kWdReloadTime);
    const int32_t add = Wd(wd, kWdReloadAddTime);
    if (add && add < t) t = add;
  }
  int32_t bolt = Wd(wd, kWdRechamberBolt);
  if (bolt >= t) bolt = 1;
  t -= bolt;
  if (t < 1) return ReloadClip();
  q.weaponDelay = t;
}

// The reload states (0x2000C7F0; the weapon timers' result on the stack): 1
// while a reload goes on.
int32_t ReloadStates(int32_t timersDone) {
  PlayerState& p = ps();
  const int32_t ws = p.weaponstate;
  if (ws == kWsReloadStart || ws == kWsReloadStartInterupt) {
    if (timersDone) ReloadAmmo();
    PlayerState& q = ps();
    if (q.weaponTime) return 1;
    if (q.weaponstate != kWsReloadStartInterupt ||
        !q.ammoclip[Wd(WeaponDef(q.weapon), kWdClipIndex)]) {
      if (CanReload()) {
        BeginWeaponReload();
        return 1;
      }
    }
    ClearBit(q.weaponrechamber, q.weapon);
    if (Wd(CurWeaponDef(), kWdReloadEndTime)) {
      q.weaponstate = kWsReloadEnd;
      StartWeaponAnim(kAnimReloadEnd);
      ps().weaponTime = Wd(CurWeaponDef(), kWdReloadEndTime);
      AddEvent(0x9A);
      return 1;
    }
    q.weaponstate = kWsReady;
    StartWeaponAnim(kAnimIdle);
    return 0;
  }
  if (ws == kWsReloadEnd) {
    p.weaponstate = kWsReady;
    StartWeaponAnim(kAnimIdle);
    return 0;
  }
  if (ws != kWsReloading && ws != kWsReloadingInterupt) return 0;
  if (timersDone) {
    ReloadAmmo();
    if (ps().weaponTime) return 1;
  } else if (p.weaponTime) {
    return 0;
  }
  PlayerState& q = ps();
  ClearBit(q.weaponrechamber, q.weapon);
  const uint32_t wd = CurWeaponDef();
  if (Wd(wd, kWdSegmentedReload)) {
    if (q.weaponstate != kWsReloadingInterupt && CanReload()) {
      BeginWeaponReload();
      return 1;
    }
    if (Wd(wd, kWdReloadEndTime)) {
      q.weaponstate = kWsReloadEnd;
      StartWeaponAnim(kAnimReloadEnd);
      ps().weaponTime = Wd(CurWeaponDef(), kWdReloadEndTime);
      AddEvent(0x9A);
      return 1;
    }
    q.weaponstate = kWsReady;
  } else {
    q.weaponstate = kWsReady;
  }
  StartWeaponAnim(kAnimIdle);
  return timersDone ? 1 : 0;
}


// The weapon timers (0x2000CD90): weaponTime and weaponDelay run down; 1 when
// weaponDelay ran out now. A held fire button on a weapon that does not
// fire automatically keeps weaponTime at 1 (back to ready).
int32_t WeaponTimers() {
  const Pmove& m = pm();
  PlayerState& p = ps();
  if (p.weaponTime) {
    p.weaponTime -= pml().msec;
    if (p.weaponTime <= 0) {
      if (Wd(CurWeaponDef(), kWdNoAutoFire) && (m.cmd.buttons & 1) && p.weapon == m.cmd.weapon &&
          ClipOf(p.weapon)) {
        p.weaponTime = 1;
        const int32_t ws = ps().weaponstate;
        if (ws == kWsRechambering) {
          WeaponIdle();
        } else if (ws == kWsFiring || ws == kWsMeleeWindup || ws == kWsMeleeRelax) {
          ContinueWeaponAnim(kAnimIdle);
          ps().weaponstate = kWsReady;
        }
      } else {
        p.weaponTime = 0;
      }
    }
  }
  PlayerState& q = ps();
  if (q.weaponDelay) {
    q.weaponDelay -= pml().msec;
    if (q.weaponDelay <= 0) {
      q.weaponDelay = 0;
      return 1;
    }
  }
  return 0;
}

// A weapon change from the command (0x2000CE60): none on a ladder or
// linked, the weapon dropped when it is gone.
void WeaponChangeCheck() {
  const Pmove& m = pm();
  PlayerState& p = ps();
  if (p.weaponTime) {
    const int32_t ws = p.weaponstate;
    if (!(ws == kWsReloading || ws == kWsReloadStart || ws == kWsReloadEnd ||
          ws == kWsReloadStartInterupt || ws == kWsReloadingInterupt || ws == kWsRechambering)) {
      if (ws == kWsFiring || ws == kWsMeleeWindup || ws == kWsMeleeRelax) return;
      if (p.weaponDelay) return;
    }
  }
  const uint32_t flags = p.pm_flags;
  if ((flags & 0x10) || ((p.eFlags & 0x20000) && !(p.eFlags & 0x80000))) {
    if (p.weapon) BeginWeaponChange(p.weapon, 0);
    return;
  }
  const int32_t want = m.cmd.weapon, weapon = p.weapon;
  if (weapon != want && !((flags & 0x4000) && weapon)) {
    if (!want || HasBit(p.weapons, want)) return BeginWeaponChange(weapon, want);
  }
  if (!weapon) return;
  if (!HasBit(p.weapons, weapon)) BeginWeaponChange(weapon, 0);
}

// Reloading on the reload button (wbuttons 8) and with an empty clip
// (0x2000CA30); the fire button interrupts a segmented reload.
void ReloadCheck() {
  const Pmove& m = pm();
  PlayerState& p = ps();
  int32_t reload = 0;
  const uint8_t button = m.cmd.wbuttons & 8;
  if (Wd(CurWeaponDef(), kWdSegmentedReload)) {
    const int32_t ws = p.weaponstate;
    if ((ws == kWsReloadStart || ws == kWsReloading) && (m.cmd.buttons & 1) &&
        !(m.oldcmd.buttons & 1))
      p.weaponstate = ws == kWsReloadStart ? kWsReloadStartInterupt : kWsReloadingInterupt;
  }
  const int32_t ws = p.weaponstate;
  if (ws == kWsRaising || ws == kWsDropping || (ws >= kWsReloading && ws <= kWsMeleeRelax)) return;
  const uint32_t weap = WeaponDef(p.weapon);
  if (button && CanReload()) reload = 1;
  if (!p.ammoclip[Wd(weap, kWdClipIndex)] && p.ammo[Wd(weap, kWdAmmoIndex)] &&
      p.weaponstate != kWsFiring &&
      (!(p.pm_flags & 1) || (!m.cmd.forwardmove && !m.cmd.rightmove)))
    return StartReload();
  if (reload) StartReload();
}

// The end of a melee attack (0x2000D360): at least the rest of it, event
// 0xA5, relaxing.
void MeleeEnd() {
  const uint32_t wd = CurWeaponDef();
  const int32_t t = Wd(wd, kWdMeleeTime) - Wd(wd, kWdMeleeDelay);
  PlayerState& p = ps();
  if (p.weaponTime < t) p.weaponTime = t;
  AddEvent(0xA5);
  ps().weaponstate = kWsMeleeRelax;
  SetProneFlag400();
}

// The game's turret use (0x2001DCA0, EAX ps), for buttons on a turret.
void UseTurret() {
  if (!M->game) return;
  C->eax = pm().ps;
  Call(f_2001DCA0, 0x2000D3F2);
}

// A melee attack on its button (cmd buttons 0x20) when not attacking
// already (0x2000D460; the weapon timers' result on the stack). On a turret
// (pm_flags 0x400000) the button uses it instead.
void CheckMelee(int32_t timersDone) {
  const Pmove& m = pm();
  PlayerState& p = ps();
  const uint8_t button = m.cmd.buttons & 0x20;
  if (button && (p.pm_flags & 0x400000)) {
    p.grenadeTimeLeft = 0;
    return UseTurret();
  }
  if (!Wd(CurWeaponDef(), kWdMelee) || timersDone) return;
  const int32_t ws = p.weaponstate;
  if (p.weaponDelay && !(ws == kWsReloading || ws == kWsReloadStart || ws == kWsReloadEnd ||
                         ws == kWsReloadStartInterupt || ws == kWsReloadingInterupt))
    return;
  if (!button) {
    p.pm_flags &= ~0x1000u;
    return;
  }
  if (p.pm_flags & 0x1000) return;
  p.pm_flags |= 0x1000;
  if (ws == kWsRaising || ws == kWsDropping || ws == kWsMeleeWindup || ws == kWsMeleeRelax) return;
  StartWeaponAnim(kAnimMeleeAttack);
  AddEvent(0xA4);
  const uint32_t wd = CurWeaponDef();
  if (!Wd(wd, kWdMeleeDelay)) return MeleeEnd();
  ps().weaponTime = Wd(wd, kWdMeleeTime);
  ps().weaponDelay = Wd(CurWeaponDef(), kWdMeleeDelay);
  ps().weaponstate = kWsMeleeWindup;
  SetProneFlag400();
}

// The melee states (0x2000D400): 1 while one runs.
int32_t MeleeStates() {
  const int32_t ws = ps().weaponstate;
  if (ws == kWsMeleeWindup) {
    MeleeEnd();
    return 1;
  }
  if (ws != kWsMeleeRelax) return 0;
  WeaponIdle();
  return 1;
}

// Not firing (0x2000CF70; the weapon timers' result on the stack): ready, 1.
int32_t NotFiring(int32_t timersDone) {
  if ((pm().cmd.buttons & 1) || timersDone) return 0;
  if (ps().weaponstate == kWsFiring) ContinueWeaponAnim(kAnimIdle);
  ps().weaponstate = kWsReady;
  return 1;
}

// On a turret (pm_flags 0x400000; 0x2000D3D0): 1, and the game uses it.
int32_t TurretFire() {
  PlayerState& p = ps();
  if (!(p.pm_flags & 0x400000)) return 0;
  p.grenadeTimeLeft = 0;
  UseTurret();
  return 1;
}

// The start of firing (0x2000CFB0; the weapon timers' result on the stack):
// the delays; a grenade is held (its fuse, event 0x9E) until thrown.
void BeginFire(int32_t timersDone) {
  const uint32_t wd = CurWeaponDef();
  PlayerState& p = ps();
  if (Wd(wd, kWdType) == 1) {
    if (!timersDone) {
      if (p.ammoclip[Wd(WeaponDef(p.weapon), kWdClipIndex)]) {
        p.grenadeTimeLeft = Wd(wd, kWdFuseTime);
        StartWeaponAnim(0x11);
        AddEvent(0x9E);
      }
      ps().weaponDelay = Wd(CurWeaponDef(), kWdHoldFireTime);
      ps().weaponTime = 0;
    }
  } else {
    p.weaponDelay = Wd(wd, kWdFireDelay);
    p.weaponTime = Wd(wd, kWdFireTime);
    if (Wd(wd, kWdAdsFire))
      p.weaponDelay = Ftol((St(1.0f) - p.weaponPosFrac) * (St(1.0f) / Wd<F32>(wd, kWdAdsTransIn)));
    if (Wd(wd, kWdRechamber)) p.weaponrechamber[p.weapon >> 5] |= 1u << (p.weapon & 31);
  }
  ps().weaponstate = kWsFiring;
  SetProneFlag400();
}

// Ammo to fire (0x2000D100): 1 with some in the clip; else a reload when
// there is more (0), or the out-of-ammo click (event 0x95 but for
// grenades), idle and half a second's wait.
int32_t CheckAmmo() {
  PlayerState& p = ps();
  const uint32_t weap = WeaponDef(p.weapon);
  if (p.ammoclip[Wd(weap, kWdClipIndex)] >= 1) return 1;
  if (p.ammo[Wd(weap, kWdAmmoIndex)] >= 1) {
    StartReload();
    return 0;
  }
  if (Wd(CurWeaponDef(), kWdType) != 1) AddEvent(0x95);
  PlayerState& q = ps();
  if (pm().cmd.weapon && (q.weapAnim & ~0x200u) && q.pm_type < 4) q.weapAnim = ~q.weapAnim & 0x200;
  q.weaponTime += 500;
  return 0;
}

// The fire anim (0x2000D1C0): from the sights or the hip, the last shot.
void FireAnim() {
  PlayerState& p = ps();
  const bool shots = p.ammoclip[Wd(WeaponDef(p.weapon), kWdClipIndex)];
  const uint32_t anim = St(p.weaponPosFrac) > 0.75f
                            ? (shots ? kAnimAdsAttack : kAnimAdsAttackLastShot)
                            : (shots ? kAnimAttack : kAnimAttackLastShot);
  if (p.pm_type < 4 && pm().cmd.weapon) p.weapAnim = (~p.weapAnim & 0x200) | anim;
}

// The aim spread of a shot (0x2000D230), but from the sights.
void FireSpread() {
  PlayerState& p = ps();
  if (p.weaponPosFrac.bits == 0x3F800000u) return;
  p.aimSpreadScale = Fst(St(Wd<F32>(CurWeaponDef(), kWdAimSpreadFire)) * 255.0f + p.aimSpreadScale);
  if (St(p.aimSpreadScale) > 255.0f) p.aimSpreadScale = Fl(255.0f);
}

// The weapon dropped when it is out of all ammo and drops then (0x2000CB50).
void SwitchIfEmpty() {
  PlayerState& p = ps();
  const uint32_t weap = WeaponDef(p.weapon);
  if (!Wd(weap, kWdDropWhenEmpty)) return;
  if (p.ammoclip[Wd(weap, kWdClipIndex)] || p.ammo[Wd(weap, kWdAmmoIndex)]) return;
  TakePlayerWeapon(p, p.weapon);
  AddEvent(0x95);
}

// The weapon state debug prints (0x2000D790, 0x2000D8E0).
void DebugWeaponState() {
  PlayerState& p = ps();
  const int32_t ws = p.weaponstate;
  if (At<int32_t>(M->lastWeaponState) == ws) return;
  const uint32_t d = M->weaponStringsDelta;
  PrintAt(0x200971FC + d, {pm().cmd.serverTime, At<int32_t>(M->moduleName)}, 0x2000D7BF);
  At<int32_t>(M->lastWeaponState) = ws;
  PrintAt(0x200971EC + d, {}, 0x2000D7CF);
  static constexpr uint32_t kNames[12] = {0x200971DC, 0x200971CC, 0x200971B8, 0x200971A8,
                                          0x20097190, 0x2009717C, 0x20097160, 0x20097148,
                                          0x20097128, 0x20097114, 0x200970FC, 0x200970E8};
  PrintAt((uint32_t(ws) > 11 ? 0x200970DC : kNames[ws]) + d, {}, 0x2000D7EC);
}
void DebugWeaponAnim() {
  PlayerState& p = ps();
  const uint32_t d = M->weaponStringsDelta;
  const uint32_t anim = p.weapAnim & ~0x200u;
  PrintAt(0x200971FC + d, {pm().cmd.serverTime, At<int32_t>(M->moduleName)}, 0x2000D909);
  At<uint32_t>(M->lastWeaponAnim) = anim;
  PrintAt(0x200970CC + d, {}, 0x2000D919);
  static constexpr uint32_t kNames[17] = {
      0x200970C0, 0x200970DC, 0x200970B0, 0x20097098, 0x20097088, 0x20097074,
      0x20097058, 0x20097044, 0x20097030, 0x20097024, 0x20097018, 0x20097008,
      0x20096FF4, 0x20096FE0, 0x20096FCC, 0x20096FB8, 0x20096FA4};
  PrintAt((anim > 16 ? 0x200970DC : kNames[anim]) + d, {}, 0x2000D93C);
}

// PM_Weapon (0x2000D590, with its tail 0x2000D290).
void Weapon() {
  const Pmove& m = pm();
  PlayerState& p = ps();
  if (p.pm_flags & 0x800) return;
  const bool noFire = At<int32_t>(M->noFire);
  if (p.pm_type >= 4) {
    p.weapon = 0;
    return;
  }
  if (p.eFlags & 0x2C000) return;
  if (const int32_t v = At<int32_t>(M->debugWeaponState); v && v != M->debugOtherModule)
    DebugWeaponState();
  if (const int32_t v = At<int32_t>(M->debugWeaponAnim); v && v != M->debugOtherModule)
    DebugWeaponAnim();
  UpdateAimDownSight();
  const uint32_t wd = CurWeaponDef();
  if (Wd(wd, kWdType) == 1 && ps().grenadeTimeLeft > 0) {
    // A held grenade: it goes off in hand when its fuse runs out.
    PlayerState& q = ps();
    if (Wd(wd, kWdCookOff)) q.grenadeTimeLeft -= pml().msec;
    if (q.grenadeTimeLeft <= 0) {
      ClipOf(q.weapon) -= 1;
      AddEvent(0xC5);
      return;
    }
    if ((m.cmd.buttons & 1) && q.weaponDelay - pml().msec <= 0) q.weaponDelay = pml().msec + 1;
  }
  const int32_t done = WeaponTimers();
  WeaponChangeCheck();
  ReloadCheck();
  CheckMelee(done);
  if (CheckRechamber(done)) return;
  PlayerState& q = ps();
  if (((q.pm_flags & 1) && (m.cmd.forwardmove || m.cmd.rightmove)) ||
      q.weaponstate == kWsMeleeWindup || q.weaponstate == kWsMeleeRelax)
    q.aimSpreadScale = Fl(255.0f);
  if (!done && (q.weaponTime || q.weaponDelay)) return;
  if (ReloadStates(done) || MeleeStates() || FinishWeaponChange() || FinishRaise()) return;
  if (!ps().weapon) return;
  if (NotFiring(done) || noFire || TurretFire()) return;
  // Fire.
  BeginFire(done);
  if (!CheckAmmo()) return;
  PlayerState& r = ps();
  if (r.weaponDelay) return;
  int32_t& clip = ClipOf(r.weapon);
  if (clip != -1 && !(r.eFlags & 0xC000)) clip--;
  if (Wd(CurWeaponDef(), kWdType) == 1) r.weaponTime = Wd(CurWeaponDef(), kWdFireTime);
  FireAnim();
  AddEvent(ClipOf(ps().weapon) ? 0x9F : 0xA1);
  FireSpread();
  SwitchIfEmpty();
}

// PM_AdjustAimSpreadScale (0x2000CBD0): the aim spread falls by the
// weapon's rate (halved in the air, scaled prone and crouched) and rises
// with turning, moving and being in the air, unless aiming down the sights.
void AdjustAimSpreadScale() {
  const uint32_t wd = CurWeaponDef();
  const Pmove& m = pm();
  PlayerState& p = ps();
  const Pml& l = pml();
  St base = Wd<F32>(wd, kWdAimSpread);
  F32 decrease;
  St increase;
  if (base == 0.0f) {
    decrease = Fl(1.0f);
    increase = St(0.0f);
  } else {
    const bool air = p.groundEntityNum == kEntityNumNone && p.pm_type != 1;
    if (air)
      base = base * 0.5f;
    else if (p.eFlags & 0x40)
      base = base * Wd<F32>(wd, kWdAimSpreadProne);
    else if (p.eFlags & 0x20)
      base = base * Wd<F32>(wd, kWdAimSpreadDuck);
    decrease = Fst(St(l.frametime) * base);
    increase = St(0.0f);
    if (p.weaponPosFrac.bits != 0x3F800000u) {
      F32 inc = Fst(increase);
      if (!(St(Wd<F32>(wd, kWdAimSpreadTurn)) == 0.0f)) {
        // Turning: the angle turned this step, both axes.
        const F32 perSec = Fst(St(1.0f) / l.frametime);
        for (int i = 0; i < 2; i++) {
          const F32 old = Fst(FromInt(m.oldcmd.angles[i]) * 0.0054931640625f);
          const F32 now = Fst(FromInt(m.cmd.angles[i]) * 0.0054931640625f);
          increase = Abs(AngleDelta(now, old)) * Wd<F32>(wd, kWdAimSpreadTurn) *
                         std::bit_cast<float>(0x3C23D70Au) * perSec +
                     inc;
          inc = Fst(increase);
        }
      }
      if (!(St(Wd<F32>(wd, kWdAimSpreadMove)) == 0.0f) && (m.cmd.forwardmove || m.cmd.rightmove))
        increase = increase + Wd<F32>(wd, kWdAimSpreadMove);
      if (air) {
        const float k = std::bit_cast<float>(0x3FA3D70Au);  // 1.28
        increase = increase + k + k;
      }
      increase = increase * l.frametime;
    }
  }
  PlayerState& q = ps();
  q.aimSpreadScale = Fst((increase - decrease) * 255.0f + q.aimSpreadScale);
  if (St(q.aimSpreadScale) < 0.0f)
    q.aimSpreadScale = kZero;
  else if (St(q.aimSpreadScale) > 255.0f)
    q.aimSpreadScale = Fl(255.0f);
}

// Aiming down the sights (pm_flags 0x20; 0x2000B960): on button 0x10 with a
// weapon that has sights, on the ground (or linked), not raising, dropping
// or in melee; prone it is kept while moving if the button was held.
void SetAdsFlag() {
  const Pmove& m = pm();
  PlayerState& p = ps();
  const int32_t pt = p.pm_type, ws = p.weaponstate;
  if (pt >= 4 || !(m.cmd.buttons & 0x10) || !Wd(CurWeaponDef(), kWdAds) || ws == kWsDropping ||
      ws == kWsRaising || ws == kWsMeleeWindup || ws == kWsMeleeRelax ||
      (!pml().groundPlane && pt != 1)) {
    p.pm_flags &= ~0x20u;
    return;
  }
  if (!(p.pm_flags & 1)) {
    p.pm_flags |= 0x20;
    return;
  }
  if ((m.oldcmd.buttons & 0x10) && (m.cmd.forwardmove || m.cmd.rightmove)) return;
  p.pm_flags |= 0x20;
  ps().pm_flags |= 0x400;
}

// Leaving the weapon's firing and melee states (0x2000BB60): 0 while firing
// or relaxing from a melee, else 1 (and ready, from rechambering or a
// melee windup).
int32_t WeaponReset() {
  PlayerState& p = ps();
  const int32_t ws = p.weaponstate;
  if (ws == kWsReady || ws == kWsRaising || ws == kWsDropping || ws == kWsReloading ||
      ws == kWsReloadStart || ws == kWsReloadEnd || ws == kWsReloadStartInterupt ||
      ws == kWsReloadingInterupt)
    return 1;
  if (ws == kWsFiring || ws == kWsMeleeRelax) return 0;
  p.weaponTime = 0;
  p.weaponDelay = 0;
  p.weaponstate = kWsReady;
  ContinueWeaponAnim(kAnimIdle);
  return 1;
}


// ---- PmoveSingle and Pmove ---------------------------------------------------------------

// PmoveSingle (0x20007FD0 / 0x30007680; pm on the stack): one step of
// pm->cmd.serverTime - ps->commandTime ms (1-200).
void PmoveSingle(uint32_t pmVa) {
  At<int32_t>(M->cPmove)++;
  Pmove& m = At<Pmove>(pmVa);
  m.watertype = 0;
  m.waterlevel = 0;
  At<uint32_t>(M->pm) = pmVa;
  PlayerState& p = ps();
  if (p.pm_flags & 0x4000) {
    // No input.
    m.cmd.buttons = 0;
    m.cmd.wbuttons &= 0xC2;
    m.cmd.forwardmove = m.cmd.rightmove = m.cmd.upmove = 0;
  }
  p.pm_flags &= ~0x8000u;
  if (p.pm_type >= 4) m.tracemask &= int32_t(0xFDFF3FFF);
  const uint32_t flags = p.pm_flags;
  if (flags & 1) {
    // Moving harder prone ends firing and aiming down the sights.
    const auto harder = [](int8_t now, int8_t old) {
      return now != old && Abs(FromInt(old)) < Abs(FromInt(now));
    };
    if (harder(m.cmd.forwardmove, m.oldcmd.forwardmove) ||
        harder(m.cmd.rightmove, m.oldcmd.rightmove)) {
      if (WeaponReset()) {
        ps().pm_flags &= ~0x400u;
        ps().pm_flags &= ~0x20u;
      }
    } else if (!(flags & 0x20)) {
      const int32_t ws = p.weaponstate;
      if (ws == kWsReady || ws == kWsRaising || ws == kWsDropping || ws == kWsReloading)
        p.pm_flags = flags & ~0x400u;
    }
  } else {
    p.pm_flags = flags & ~0x400u;
  }
  pml().weaponDef = WeaponDef(p.weapon);
  if ((p.pm_flags & 0x20) && (p.pm_flags & 1)) {
    m.cmd.forwardmove = 0;
    m.cmd.rightmove = 0;
  }
  // eFlags 0x400: firing.
  p.eFlags &= ~0x400u;
  if (!(p.pm_flags & 0x800) && (p.weaponstate == kWsReady || p.weaponstate == kWsFiring) &&
      p.ammoclip[Wd(WeaponDef(p.weapon), kWdClipIndex)] && (m.cmd.buttons & 1) &&
      !(m.cmd.buttons & 2))
    p.eFlags |= 0x400;
  if (p.pm_type < 4 && !(m.cmd.buttons & 1)) p.pm_flags &= ~0x800u;
  Pml& l = pml();
  std::memset(&l, 0, sizeof(Pml));
  l.msec = m.cmd.serverTime - p.commandTime;
  if (l.msec < 1)
    l.msec = 1;
  else if (l.msec > 200)
    l.msec = 200;
  p.commandTime = m.cmd.serverTime;
  Copy3(l.previous_origin, p.origin);
  Copy3(l.previous_velocity, p.velocity);
  l.frametime = Fst(FromInt(l.msec) * std::bit_cast<float>(0x3A83126Fu));  // 0.001
  l.weaponDef = WeaponDef(p.weapon);
  AdjustAimSpreadScale();
  UpdateViewAngles(p, m.cmd, m.traceUpright);
  AngleVectors(ps().viewangles, l.forward, l.right, l.up);
  Pmove& n = pm();
  if (n.cmd.upmove < 10) ps().pm_flags &= ~8u;
  if (n.cmd.forwardmove < 0)
    ps().pm_flags |= 0x40;
  else if (n.cmd.forwardmove > 0 || n.cmd.rightmove)
    ps().pm_flags &= ~0x40u;
  if (ps().pm_type >= 4) n.cmd.forwardmove = n.cmd.rightmove = n.cmd.upmove = 0;
  if ((ps().pm_flags & 1) && (ps().pm_flags & 0x400)) {
    n.cmd.forwardmove = 0;
    n.cmd.rightmove = 0;
  }
  const auto inAir = [&] {
    ps().groundEntityNum = kEntityNumNone;
    l.groundPlane = 0;
    l.walking = 0;
  };
  switch (ps().pm_type) {
    case 1:
    case 5:
      // Linked to an entity.
      inAir();
      SetAdsFlag();
      SetSlowFlag();
      CheckDuck();
      DropTimers();
      Weapon();
      if (ps().eFlags & 0xC000) Footsteps();
      return;
    case 2:
      SetAdsFlag();
      SetSlowFlag();
      NoclipMove();
      return DropTimers();
    case 3:
      SetAdsFlag();
      SetSlowFlag();
      UfoMove();
      return DropTimers();
    default:
      break;
  }
  if (ps().eFlags & 0xC000) {
    inAir();
    SetAdsFlag();
    SetSlowFlag();
    CheckDuck();
    DropTimers();
    return Footsteps();
  }
  SetWaterLevel();
  l.previous_waterlevel = pm().waterlevel;
  CheckDuck();
  GroundTrace();
  SetAdsFlag();
  SetSlowFlag();
  ProneAngles();
  if (ps().pm_type == 4) DeadMove();
  CheckLadderMove();
  DropTimers();
  if (ps().pm_flags & 0x10)
    LadderMove();
  else if (l.walking)
    WalkMove();
  else
    AirMove();
  GroundTrace();
  SetWaterLevel();
  Weapon();
  Footsteps();
  FoliageSound();
  WaterEvents();
  // Blocked: the velocity becomes the move, when that is under half of it.
  PlayerState& q = ps();
  const St d0 = St(q.origin[0]) - l.previous_origin[0];
  const F32 d1 = Fst(St(q.origin[1]) - l.previous_origin[1]);
  const F32 d2 = Fst(St(q.origin[2]) - l.previous_origin[2]);
  const St v0(q.velocity[0]), v1(q.velocity[1]), v2(q.velocity[2]);
  const St moved = ((St(d2) * d2 + St(d1) * d1) + d0 * d0) / (St(l.frametime) * l.frametime);
  if (!(((v2 * v2 + v1 * v1) + v0 * v0) * 0.25f > moved)) return;
  q.velocity[0] = Fst((St(1.0f) / l.frametime) * d0);
  q.velocity[1] = Fst((St(1.0f) / l.frametime) * d1);
  q.velocity[2] = Fst((St(1.0f) / l.frametime) * d2);
}

// Pmove (0x20008580 / 0x30007C30, ESI pm): the command in steps of at most
// 66 ms (pm->pmove_msec with pm->pmove_fixed), from ps->commandTime
// (at most a second behind; 50 ms with pm_flags 0x10000).
//
// With pmove_fixed set (src/pmove_fixed.cpp, which keeps the command time
// from being rounded up), only the whole steps of pmove_msec that fit
// before the command's time run; the rest stays pending for a later
// command. Server and prediction run the same steps with the same commands
// that way. A command that completes no step still updates the view angles,
// and cgame records where each step ended for its view.
void PmoveImpl(uint32_t pmVa) {
  Pmove& m = At<Pmove>(pmVa);
  PlayerState& p = At<PlayerState>(m.ps);
  if (p.pm_flags & 0x4000) {
    const uint8_t wb = m.cmd.wbuttons & 0xC0;
    m.cmd.forwardmove = m.cmd.rightmove = m.cmd.upmove = 0;
    m.cmd.buttons = 0;
    m.cmd.wbuttons = wb;
  }
  const int32_t finalTime = m.cmd.serverTime;
  if (finalTime < p.commandTime) return;
  if (finalTime > p.commandTime + 1000) p.commandTime = finalTime - 1000;
  if ((p.pm_flags & 0x10000) && finalTime - p.commandTime > 50) p.commandTime = finalTime - 50;
  m.numtouch = 0;
  At<uint32_t>(M->pm) = pmVa;
  const bool fixed = PmoveFixedOn(M->game);
  const int32_t step = fixed ? PmoveFixedStep(M->game) : 0;
  const int32_t start = p.commandTime;
  const int32_t end = fixed ? start + (finalTime - start) / step * step : finalTime;
  while (p.commandTime != end) {
    int32_t msec = end - p.commandTime;
    if (fixed) {
      if (msec > step) msec = step;
    } else if (m.pmove_fixed) {
      if (msec > m.pmove_msec) msec = m.pmove_msec;
    } else if (msec > 66) {
      msec = 66;
    }
    m.cmd.serverTime = p.commandTime + msec;
    PmoveSingle(pmVa);
    if (fixed && !M->game) PmoveFixedStepEnd(p.commandTime, p.origin[0].bits, p.origin[1].bits,
                                             p.origin[2].bits);
    if (p.pm_flags & 8) m.cmd.upmove = 20;
  }
  m.cmd.serverTime = finalTime;
  if (M->game) {
    Push(m.ps);
    Call(f_20073F80, 0x20008642);
    C->esp += 4;
  }
  if (fixed && end == start) UpdateViewAngles(p, m.cmd, m.traceUpright);
}

// The callee-saved registers, restored on return: lifted callees called with
// the original's register arguments may leave them changed. A guest longjmp
// unwinding through here has set them already.
struct SavedRegs {
  Ctx& c;
  uint32_t ebx, ebp, esi, edi;
  int unwinding;
  explicit SavedRegs(Ctx& c)
      : c(c), ebx(c.ebx), ebp(c.ebp), esi(c.esi), edi(c.edi),
        unwinding(std::uncaught_exceptions()) {}
  ~SavedRegs() {
    if (std::uncaught_exceptions() != unwinding) return;
    c.ebx = ebx;
    c.ebp = ebp;
    c.esi = esi;
    c.edi = edi;
  }
};

// The module and context of the code running here (Pmove and the entries
// below nest only through the guest).
struct Enter {
  const Module* m;
  Ctx* c;
  Enter(Ctx& ctx, const Module& mod) : m(M), c(C) {
    M = &mod;
    C = &ctx;
  }
  ~Enter() {
    M = m;
    C = c;
  }
};

void RunPmove(Ctx& c, const Module& m) {
  SavedRegs saved(c);
  Enter enter(c, m);
  PmoveImpl(c.esi);
  c.esp += 4;
}

void RunFootsteps(Ctx& c, const Module& m) {
  SavedRegs saved(c);
  Enter enter(c, m);
  Footsteps();
  c.esp += 4;
}

}  // namespace

void f_20008580(Ctx& c) { RunPmove(c, kGame); }
void f_30007C30(Ctx& c) { RunPmove(c, kCgame); }

// PM_Footsteps as entries of its own: pmove calls it natively; these are
// for the lifted PmoveSingle of the oracle test (tests/pmove_test.cpp).
void f_200067C0(Ctx& c) { RunFootsteps(c, kGame); }
void f_30005F00(Ctx& c) { RunFootsteps(c, kCgame); }

}  // namespace recomp::gen
