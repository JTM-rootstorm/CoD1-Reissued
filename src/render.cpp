// The renderer's front end natively, and the hottest lifted code under the
// back end: the front end's culling of world surfaces, static models and
// entities against the view frustum and the cell portals, the dlight bits
// of a bounds and adding a world surface; the view (its culls, transforms,
// fog transition, far clip, projection and frustum);
// the back end's sky clipping (ClipSkyPolygon, AddSkyPolygon), texture
// coordinates (ComputeTexCoords); and the draw surface sort (qsortFast),
// the 4x4 matrix product and the memcpy 0x42B0A0 under both. Replacements of lifted
// functions named with `replace` in manifest/CoDSP.manifest; FINDINGS.md
// "Renderer". Names and evidence are in the Ghidra plate comments.
//
// The originals are x87 code; the arithmetic is src/x87_value.h's, which
// gives the original's bits at the main thread's control word: a value the
// original keeps on the x87 stack is an St, one it stores is an F32, and
// each expression keeps the original's order of operations and operands.
// Comparisons keep the original's branch for unordered operands: `d < dist`
// is fcomp + `test ah,5` + jnp (taken only for less), `!(d > dist)` is
// `test ah,41h` + jne. Built with -ffp-contract=off (CMakeLists.txt).
//
// Calling conventions are the originals' (register arguments from whole-
// program optimisation; see each function). Lifted callees are called as
// the original's call sites call them: the same arguments on the guest
// stack at the same ESP, the same return address and the registers they
// read; argument slots the original writes get the same values. Not
// reproduced: EAX, ECX and EDX on return (no caller reads them), the
// saved registers and locals in the original's frame below ESP, EFLAGS,
// the x87 status word and stale registers (x87_value.h).

#include <bit>
#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "runtime/log.h"
#include "x86/lifted.h"
#include "x87_value.h"

namespace recomp::gen {

void f_004AD7E0(Ctx& c);  // draws a debug box (ECX mins, EDX maxs, EAX colour)
void f_004DEC10(Ctx& c);  // adds a culled-in entity (ent, ent)
void f_004CAF00(Ctx& c);  // R_AddDrawSurf(surface, dlight, 0, 0; EAX, EDX shader)
void f_0042B0A0_lifted(Ctx& c);  // the memcpy, for overlapping copies
void f_004CAB80(Ctx& c);  // a portal or mirror surface's view (entity; EAX: drawn)
void f_00427E80(Ctx& c);  // a 4x4 matrix's inverse (ECX in, EAX out)
// ComputeTexCoords' texture coordinate generators and modifiers.
void f_0050A2B0(Ctx& c);
void f_0050ABF0(Ctx& c);
void f_0050A7C0(Ctx& c);
void f_0050A3D0(Ctx& c);
void f_0050A380(Ctx& c);
void f_0050A420(Ctx& c);
void f_0050A4D0(Ctx& c);
void f_0050A5C0(Ctx& c);
void f_0050A720(Ctx& c);
void f_0050A820(Ctx& c);
void f_0050A890(Ctx& c);
void f_0050A8D0(Ctx& c);
void f_0050A9B0(Ctx& c);
void f_0050A980(Ctx& c);
void f_00508A50(Ctx& c);
void f_0050AA30(Ctx& c);
void f_0050AA80(Ctx& c);
void f_0050AE10(Ctx& c);

namespace {

using lift::Rd;
using lift::Wr;

// ---- guest memory and calls ----------------------------------------------

uint32_t U32(uint32_t va) { return Rd<uint32_t>(g_guest_base, va); }
uint8_t U8(uint32_t va) { return Rd<uint8_t>(g_guest_base, va); }
void Put(uint32_t va, uint32_t v) { Wr<uint32_t>(g_guest_base, va, v); }
F32 Ld(uint32_t va) { return {U32(va)}; }
void Put(uint32_t va, F32 v) { Put(va, v.bits); }

void Push(Ctx& c, uint32_t v) {
  c.esp -= 4;
  Put(c.esp, v);
}
// The lifted function fn, called from the original's call site whose
// return address is ret. Arguments already pushed stay for the caller.
void Call(Ctx& c, GuestFn fn, uint32_t ret) {
  Push(c, ret);
  fn(c);
}
// An indirect call (the refimport table's ri.Error) from site va.
void CallPtr(Ctx& c, uint32_t target, uint32_t ret, uint32_t va) {
  Push(c, ret);
  Dispatch(c, target, va);
}
uint32_t Arg(const Ctx& c, uint32_t e, int n) { return U32(e + 4 * n); }

// ---- the front end: culling --------------------------------------------

// Globals of the cull: a plane-bits counter (the depth from which the
// frustum test is still needed), the near and an optional far plane, the
// portals of the current cell (count, array of pointers; a portal has its
// own counter at +0x18 and planes at +0x20, count +0x1C), the view count
// and the debug-draw cvars (cvar_t*, integer at +0x20).
constexpr uint32_t kPlaneBits = 0x11BEC20;
constexpr uint32_t kNearPlane = 0x11BEB7C, kFarPlane = 0x11BEB80;
constexpr uint32_t kNumPortals = 0x11BEC10, kPortals = 0x11BEC14;
constexpr uint32_t kViewCount = 0x1535A28;
constexpr uint32_t kDrawEntityBounds = 0x15347E0, kDrawSurfBounds = 0x15344DC,
                   kDrawModelBounds = 0x153443C, kCullEntities = 0x1534568;
constexpr int32_t kNoBits = 0x7FFFFFFF;

int32_t I32(uint32_t va) { return int32_t(U32(va)); }

// A plane (cplane_t: normal, dist, type, then CoD's byte offsets of the far
// corner's x, y and z within a bounds' mins/maxs at +0x10..+0x12) against
// a bounds b: the far corner's distance along the normal. The sums run
// x + y + z or z + y + x, as each site has them.
St Corner(uint32_t b, uint32_t p, int axis) {
  return St(Ld(b + U8(p + 0x10 + axis))) * Ld(p + 4 * axis);
}
St FarXYZ(uint32_t b, uint32_t p) { return Corner(b, p, 0) + Corner(b, p, 1) + Corner(b, p, 2); }
St FarZYX(uint32_t b, uint32_t p) { return Corner(b, p, 2) + Corner(b, p, 1) + Corner(b, p, 0); }
F32 Dist(uint32_t p) { return Ld(p + 0xC); }
bool Behind(St d, uint32_t p) { return d < Dist(p); }

// The near corner: the other end of each axis (mins at +0, maxs at +0xC).
St Near(uint32_t b, uint32_t p, int axis) {
  return St(Ld(b + 0xC + 8 * axis - U8(p + 0x10 + axis))) * Ld(p + 4 * axis);
}

// What every cull starts with: the frustum (when `test` says so; the
// counter is set to kNoBits first), the near plane and the far one.
bool FrustumCulls(uint32_t b, uint32_t planes, int32_t numPlanes, bool test) {
  if (test) {
    Put(kPlaneBits, uint32_t(kNoBits));
    for (int32_t i = 0; i < numPlanes; i++) {
      const uint32_t p = planes + uint32_t(i) * 0x14;
      if (Behind(FarXYZ(b, p), p)) return true;
    }
  }
  const uint32_t n = U32(kNearPlane);
  if (Behind(FarZYX(b, n), n)) return true;
  const uint32_t f = U32(kFarPlane);
  return f && Behind(FarZYX(b, f), f);
}

// The portals: one whose counter says so (`test`) is tested, its counter
// set to kNoBits; b is culled when it is behind every plane of one.
template <typename Test>
bool PortalsCull(uint32_t b, Test test) {
  for (int32_t k = 0; k < I32(kNumPortals); k++) {
    const uint32_t portal = U32(U32(kPortals) + uint32_t(k) * 4);
    if (!test(I32(portal + 0x18))) continue;
    Put(portal + 0x18, uint32_t(kNoBits));
    const uint32_t q = U32(U32(kPortals) + uint32_t(k) * 4);
    for (int32_t j = 0;; j++) {
      if (j >= I32(q + 0x1C)) return true;
      const uint32_t p = U32(q + 0x20) + uint32_t(j) * 0x14;
      if (!Behind(FarZYX(b, p), p)) break;
    }
  }
  return false;
}

bool DebugDraw(uint32_t cvar) { return U32(U32(cvar) + 0x20) != 0; }

// 0x4AD7E0's debug box of bounds b from the call site ret (ESP: the
// original's at the call).
void DrawBounds(Ctx& c, uint32_t b, uint32_t maxs, uint32_t colour, uint32_t ret) {
  c.eax = colour;
  c.ecx = b;
  c.edx = maxs;
  Call(c, f_004AD7E0, ret);
}

}  // namespace

// The dlight bits of a bounds (EAX bits, EDI mins, EBX maxs) -> EAX: each
// dlight of tr.refdef's (radius then origin, 0x88 bytes each, from +0x74
// of 0x153647C's) whose cube around its origin overlaps the bounds.
void f_004D68E0(Ctx& c) {
  uint32_t bits = c.eax, dl = U32(0x153647C) + 0x74, bit = 1, result = 0;
  const uint32_t mins = c.edi, maxs = c.ebx;
  while (bits) {
    if (bits & bit) {
      bits &= ~bit;
      const F32 r = Ld(dl);
      bool hit = true;
      for (int i = 0; i < 3 && hit; i++) {
        const F32 o = Ld(dl + 4 + 4 * i);
        hit = !(St(r) + o <= Ld(mins + 4 * i)) && !(St(o) - r >= Ld(maxs + 4 * i));
      }
      if (hit) result |= bit;
    }
    bit += bit;
    dl += 0x88;
  }
  c.eax = result;
  c.ecx = dl;
  c.edx = bit;
  c.esp += 4;
}

// Adds a world surface reference (12 bytes: view count, shader, surface;
// stack) with dlight bits EAX: marks it, recomputes the bits of a surface
// of type 0x18 and up from its bounds (+0xC, +0x18) into the surface (+8),
// and adds it to the draw surfaces.
void f_004D6970(Ctx& c) {
  const uint32_t ebx = c.ebx, edi = c.edi;
  const uint32_t e = c.esp, ref = Arg(c, e, 1), surf = U32(ref + 8);
  Put(ref, U32(kViewCount));
  uint32_t bits = c.eax;
  if (I32(surf) >= 0x18) {
    if (bits) {
      c.esp = e - 0x10;
      c.ebx = surf + 0x18;
      c.edi = surf + 0xC;
      Call(c, f_004D68E0, 0x4D6998);
      Put(surf + 8, c.eax);
      bits = c.eax != 0;
      c.ebx = ebx;
      c.edi = edi;
    }
    c.esp = e - 8;
    c.edx = U32(ref + 4);
    Push(c, 0);
    Push(c, 0);
    Push(c, bits);
    Push(c, U32(ref + 8));
    c.eax = U32(surf + 4);
    Call(c, f_004CAF00, 0x4D69BA);
  } else {
    c.esp = e - 8;
    c.edx = U32(ref + 4);
    Push(c, 0);
    Push(c, 0);
    Push(c, 0);
    Push(c, surf);
    c.eax = U32(0x1549548);
    Call(c, f_004CAF00, 0x4D69D9);
  }
  c.esp = e + 4;
}

// A world surface reference (stack: ref, dlight bits, planes, plane bits;
// EAX the plane count): unless already added this view, culls the
// surface's bounds (+0xC) against the frustum (when the plane bits are
// below the counter), the near/far planes and the portals (those whose
// counter is above the plane bits), then adds it.
void f_004D8280(Ctx& c) {
  const uint32_t e = c.esp, ref = Arg(c, e, 1), planes = Arg(c, e, 3);
  const int32_t numPlanes = int32_t(c.eax), planeBits = int32_t(Arg(c, e, 4));
  if (U32(ref) != U32(kViewCount)) {
    const uint32_t surf = U32(ref + 8), b = surf + 0xC;
    if (!FrustumCulls(b, planes, numPlanes, planeBits < I32(kPlaneBits)) &&
        !PortalsCull(b, [&](int32_t bits) { return planeBits < bits; })) {
      c.esp = e - 0x14;
      if (U32(U32(kDrawSurfBounds) + 0x20) & 2) DrawBounds(c, b, surf + 0x18, 0x537820, 0x4D8407);
      Push(c, ref);
      c.eax = Arg(c, e, 2);
      Call(c, f_004D6970, 0x4D8415);
    }
  }
  c.esp = e + 4;
}

// A node of a surface AABB tree (stack: node, planes, plane count, dlight
// bits, plane bits): mins/maxs, surface references (+0x18, count +0x1C, 12
// bytes each), children (+0x20, count +0x24, 0x28 bytes each). Culled like
// a surface (far corners); then the dlight bits are narrowed to its
// bounds. A node entirely in front of the frustum planes and inside some
// plane of every tested portal (near corners) adds all its surfaces, and
// raises the counters for what is under it; otherwise its children are
// visited, or a leaf's surfaces culled one by one, with plane bits + 1.
void f_004D8420(Ctx& c) {
  const uint32_t e = c.esp, node = Arg(c, e, 1), planes = Arg(c, e, 2);
  const int32_t numPlanes = int32_t(Arg(c, e, 3)), planeBits = int32_t(Arg(c, e, 5));
  if (FrustumCulls(node, planes, numPlanes, planeBits < I32(kPlaneBits)) ||
      PortalsCull(node, [&](int32_t bits) { return planeBits < bits; })) {
    c.esp = e + 4;
    return;
  }
  c.esp = e - 0x14;
  if (Arg(c, e, 4)) {
    const uint32_t ebx = c.ebx, edi = c.edi;
    c.eax = Arg(c, e, 4);
    c.ebx = node + 0xC;
    c.edi = node;
    Call(c, f_004D68E0, 0x4D8583);
    Put(e + 0x10, c.eax);
    c.ebx = ebx;
    c.edi = edi;
  }
  bool inside = true;
  if (planeBits < I32(kPlaneBits)) {
    for (int32_t i = 0; i < numPlanes && inside; i++) {
      const uint32_t p = planes + uint32_t(i) * 0x14;
      inside = Near(node, p, 1) + Near(node, p, 0) + Near(node, p, 2) > Dist(p);
    }
    if (inside) Put(kPlaneBits, uint32_t(planeBits + 1));
  }
  for (int32_t k = 0; k < I32(kNumPortals) && inside; k++) {
    const uint32_t portal = U32(U32(kPortals) + uint32_t(k) * 4);
    if (planeBits >= I32(portal + 0x18)) continue;
    for (int32_t j = 0;; j++) {
      if (j >= I32(portal + 0x1C)) {
        inside = false;
        break;
      }
      const uint32_t p = U32(portal + 0x20) + uint32_t(j) * 0x14;
      if (Near(node, p, 2) + Near(node, p, 1) + Near(node, p, 0) > Dist(p)) {
        Put(portal + 0x18, uint32_t(planeBits + 1));
        break;
      }
    }
  }
  if (inside) {
    if (DebugDraw(kDrawSurfBounds)) DrawBounds(c, node, node + 0xC, 0x537850, 0x4D8696);
    for (int32_t j = 0; j < I32(node + 0x1C); j++) {
      const uint32_t ref = U32(node + 0x18) + uint32_t(j) * 0xC;
      if (U32(ref) == U32(kViewCount)) continue;
      Push(c, ref);
      c.eax = Arg(c, e, 4);
      Call(c, f_004D6970, 0x4D86BC);
      c.esp += 4;
    }
  } else if (U32(node + 0x24)) {
    for (int32_t j = 0; j < I32(node + 0x24); j++) {
      Push(c, uint32_t(planeBits + 1));
      Push(c, Arg(c, e, 4));
      Push(c, uint32_t(numPlanes));
      Push(c, planes);
      Push(c, U32(node + 0x20) + uint32_t(j) * 0x28);
      Call(c, f_004D8420, 0x4D86FB);
      c.esp += 0x14;
    }
  } else {
    if (DebugDraw(kDrawSurfBounds)) DrawBounds(c, node, node + 0xC, 0x537910, 0x4D872B);
    for (int32_t j = 0; j < I32(node + 0x1C); j++) {
      Push(c, uint32_t(planeBits + 1));
      Push(c, planes);
      Push(c, Arg(c, e, 4));
      Push(c, U32(node + 0x18) + uint32_t(j) * 0xC);
      c.eax = uint32_t(numPlanes);
      Call(c, f_004D8280, 0x4D875A);
      c.esp += 0x10;
    }
  }
  c.esp = e + 4;
}

// A static model (EDX: bounds at +0, surface references +0x18, count
// +0x1C, view count +0x20; EAX the plane count, ECX the planes; stack:
// dlight bits): culled with the counters above zero, then marked and its
// surfaces added.
void f_004D9330(Ctx& c) {
  const uint32_t e = c.esp, model = c.edx;
  if (FrustumCulls(model, c.ecx, int32_t(c.eax), I32(kPlaneBits) > 0) ||
      PortalsCull(model, [](int32_t bits) { return bits > 0; })) {
    c.esp = e + 4;
    return;
  }
  c.esp = e - 0x10;
  if (U32(U32(kDrawModelBounds) + 0x20) & 1) DrawBounds(c, model, model + 0xC, 0x537860, 0x4D948A);
  Put(model + 0x20, U32(kViewCount));
  uint32_t ref = U32(model + 0x18);
  for (uint32_t n = U32(model + 0x1C); n; n--, ref += 0xC) {
    Push(c, ref);
    c.eax = Arg(c, e, 1);
    Call(c, f_004D6970, 0x4D94AA);
    c.esp += 4;
  }
  c.esp = e + 4;
}

// An entity in a cell (stack: entity, dlight bits; EAX the plane count, EDX
// the planes): its bounds (+0x9C) culled like a static model's, then
// marked (+0xB4) and added.
void f_004D94C0(Ctx& c) {
  const uint32_t e = c.esp, ent = Arg(c, e, 1), b = ent + 0x9C;
  if (FrustumCulls(b, c.edx, int32_t(c.eax), I32(kPlaneBits) > 0) ||
      PortalsCull(b, [](int32_t bits) { return bits > 0; })) {
    c.esp = e + 4;
    return;
  }
  c.esp = e - 0x10;
  if (DebugDraw(kDrawEntityBounds)) DrawBounds(c, b, ent + 0xA8, 0x5378E0, 0x4D9629);
  Push(c, ent);
  Push(c, ent);
  Put(ent + 0xB4, U32(kViewCount));
  Call(c, f_004DEC10, 0x4D963C);
  c.esp = e + 4;
}

// A cell's static models (+0x24, count +0x28) and, when entities are
// culled per cell, its entities (list at +0x38) not yet seen this view
// (stack: cell, planes; EBX the plane count, EDI the dlight bits).
void f_004D9650(Ctx& c) {
  const uint32_t e = c.esp, cell = Arg(c, e, 1), planes = Arg(c, e, 2);
  c.esp = e - 0xC;
  uint32_t models = U32(cell + 0x24);
  if (uint32_t n = U32(cell + 0x28)) {
    for (; n; n--, models += 4) {
      const uint32_t model = U32(models);
      if (U32(model + 0x20) == U32(kViewCount)) continue;
      Push(c, c.edi);
      c.eax = c.ebx;
      c.ecx = planes;
      c.edx = model;
      Call(c, f_004D9330, 0x4D9686);
      c.esp += 4;
    }
  }
  if (DebugDraw(kCullEntities)) {
    for (uint32_t link = U32(cell + 0x38); link; link = U32(link + 4)) {
      const uint32_t ent = U32(link);
      if (U32(ent + 0xB4) == U32(kViewCount)) continue;
      Push(c, c.edi);
      Push(c, ent);
      c.eax = c.ebx;
      c.edx = planes;
      Call(c, f_004D94C0, 0x4D96CB);
      c.esp += 8;
    }
  }
  c.esp = e + 4;
}

// ---- the back end: the sky ------------------------------------------------

namespace {

constexpr uint32_t kSkyMins = 0x11CD800, kSkyMaxs = 0x11CD830;  // float[2][6] each
constexpr uint32_t kSkyClip = 0x564478;  // vec3_t[6]
constexpr uint32_t kVecToSt = 0x5644C0;  // int[6][3]
constexpr uint32_t kOrigin = 0x537E54;   // vec3_origin
constexpr int kMaxClipVerts = 64;

// vecs[i][j], 1-based and signed as vec_to_st has them (negative: negated).
F32 Coord(const F32* v, int32_t k) { return v[k > 0 ? k - 1 : -k - 1]; }

// AddSkyPolygon (ECX nump, EAX vecs): the cube face the polygon's summed
// vertices point at, and the face's s/t bounds grown by its vertices. The
// sum keeps x and y on the x87 stack across stretches of the original's
// four-at-a-time loop and z in memory; each Fst is a store of the original.
void AddSkyPolygon(int32_t nump, const F32* vecs) {
  St r0 = St(Ld(kOrigin)), r1 = St(Ld(kOrigin + 4));
  F32 m0{}, m1 = Fst(r1), m2 = Ld(kOrigin + 8);
  int32_t i = 0;
  if (nump >= 4) {
    for (;;) {
      const F32* v = vecs + 3 * i;
      r0 = r0 + v[0];
      r1 = St(m1) + v[1];
      m2 = Fst(St(m2) + v[2]);
      m0 = Fst(r0 + v[3]);
      r1 = r1 + v[4];
      St r2 = St(m2) + v[5];
      m0 = Fst(St(m0) + v[6]);
      m1 = Fst(r1 + v[7]);
      m2 = Fst(r2 + v[8]);
      r0 = St(m0) + v[9];
      r1 = St(m1) + v[10];
      m1 = Fst(r1);
      m2 = Fst(St(m2) + v[11]);
      i += 4;
      if (!(i + 3 < nump)) break;
    }
  }
  if (i < nump) {
    for (; i < nump; i++) {
      const F32* v = vecs + 3 * i;
      r0 = r0 + v[0];
      r1 = r1 + v[1];
      m2 = Fst(St(m2) + v[2]);
    }
    m1 = Fst(r1);
  }
  const F32 a0 = Fst(Abs(r0)), a1 = Fst(Abs(r1)), a2 = Fst(Abs(St(m2)));
  int axis;
  if (St(a0) > a1 && St(a0) > a2)
    axis = r0 < 0.0f ? 1 : 0;
  else if (St(a1) > a2 && St(a1) > a0)
    axis = St(m1) < 0.0f ? 3 : 2;
  else
    axis = St(m2) < 0.0f ? 5 : 4;

  const uint32_t st = kVecToSt + uint32_t(axis) * 12;
  const int32_t k0 = I32(st), k1 = I32(st + 4), k2 = I32(st + 8);
  const uint32_t mins0 = kSkyMins + 4 * axis, mins1 = mins0 + 0x18;
  const uint32_t maxs0 = kSkyMaxs + 4 * axis, maxs1 = maxs0 + 0x18;
  for (i = 0; i < nump; i++) {
    const F32* v = vecs + 3 * i;
    const St dv = k2 > 0 ? St(v[k2 - 1]) : -St(v[-k2 - 1]);
    if (dv < St(0.001)) continue;
    const F32 s = Fst(k0 < 0 ? St(-1.0f) / dv * Coord(v, k0) : St(Coord(v, k0)) / dv);
    const St t = k1 < 0 ? St(-1.0f) / dv * Coord(v, k1) : Coord(v, k1) / dv;
    if (St(s) < Ld(mins0)) Put(mins0, s);
    if (t < Ld(mins1)) Put(mins1, Fst(t));
    if (St(s) > Ld(maxs0)) Put(maxs0, s);
    if (t > Ld(maxs1)) Put(maxs1, Fst(t));
  }
}

// ClipSkyPolygon (stack: nump, vecs, stage): clips the polygon by the sky
// cube's planes from `stage` on (front and back pieces, recursively) and
// adds the pieces to their faces. vecs has room for one more vertex,
// where vertex 0 is copied. ESP follows the original's frames (0x828
// bytes and the call's 0x10) for the error call at its depth.
void ClipSkyPolygon(Ctx& c, int32_t nump, F32* vecs, int32_t stage, uint32_t e) {
  if (nump > kMaxClipVerts - 2) {
    c.esp = e - 0x828 + 0x10 - 8;
    Push(c, 0x541F50);  // "ClipSkyPolygon: MAX_CLIP_VERTS"
    Push(c, 1);
    CallPtr(c, U32(0x1549604), 0x4DB681, 0x4DB67B);
    if (nump > kMaxClipVerts - 1) FatalF("ClipSkyPolygon: %d vertices\n", nump);
  }
  if (stage == 6) {
    AddSkyPolygon(nump, vecs);
    return;
  }
  const uint32_t e1 = e - 0x828 - 0x10;
  if (nump <= 0) {
    ClipSkyPolygon(c, nump, vecs, stage + 1, e1);
    return;
  }
  const uint32_t norm = kSkyClip + uint32_t(stage) * 12;
  const F32 n[3] = {Ld(norm), Ld(norm + 4), Ld(norm + 8)};
  enum { kFront, kBack, kOn };
  int sides[kMaxClipVerts];
  F32 dists[kMaxClipVerts];
  bool front = false, back = false;
  for (int32_t i = 0; i < nump; i++) {
    const F32* v = vecs + 3 * i;
    const St d = St(v[2]) * n[2] + St(v[0]) * n[0] + St(n[1]) * v[1];
    if (d > 0.1f) {
      front = true;
      sides[i] = kFront;
    } else if (d < -0.1f) {
      back = true;
      sides[i] = kBack;
    } else {
      sides[i] = kOn;
    }
    dists[i] = Fst(d);
  }
  if (!front || !back) {
    ClipSkyPolygon(c, nump, vecs, stage + 1, e1);
    return;
  }
  sides[nump] = sides[0];
  dists[nump] = dists[0];
  std::memcpy(vecs + 3 * nump, vecs, 12);
  F32 newv[2][kMaxClipVerts][3];
  int32_t newc[2] = {0, 0};
  for (int32_t i = 0; i < nump; i++) {
    const F32* v = vecs + 3 * i;
    switch (sides[i]) {
      case kFront:
        std::memcpy(newv[0][newc[0]++], v, 12);
        break;
      case kOn:
        std::memcpy(newv[0][newc[0]++], v, 12);
        [[fallthrough]];
      case kBack:
        std::memcpy(newv[1][newc[1]++], v, 12);
        break;
    }
    if (sides[i] == kOn || sides[i + 1] == kOn || sides[i + 1] == sides[i]) continue;
    const St d = dists[i] / (St(dists[i]) - dists[i + 1]);
    for (int j = 0; j < 3; j++) {
      const F32 x = Fst((St(v[3 + j]) - v[j]) * d + v[j]);
      newv[0][newc[0]][j] = x;
      newv[1][newc[1]][j] = x;
    }
    newc[0]++;
    newc[1]++;
  }
  ClipSkyPolygon(c, newc[0], newv[0][0], stage + 1, e1);
  ClipSkyPolygon(c, newc[1], newv[1][0], stage + 1, e1 - 0xC);
}

}  // namespace

// AddSkyPolygon (ECX nump, EAX vecs).
void f_004DB3E0(Ctx& c) {
  AddSkyPolygon(int32_t(c.ecx), reinterpret_cast<F32*>(g_guest_base + c.eax));
  c.esp += 4;
}

// ClipSkyPolygon (stack: nump, vecs, stage).
void f_004DB660(Ctx& c) {
  const uint32_t e = c.esp;
  ClipSkyPolygon(c, int32_t(Arg(c, e, 1)), reinterpret_cast<F32*>(g_guest_base + Arg(c, e, 2)),
                 int32_t(Arg(c, e, 3)), e);
  c.esp = e + 4;
}

// ---- the back end: texture coordinates ------------------------------------

namespace {

// The tessellator's vertex count and the arrays the generators copy, the
// per-stage texture coordinates (0x20000 bytes each) and their pointers.
constexpr uint32_t kNumVertexes = 0x17F0C88;
constexpr uint32_t kXyzStride = 0x17F0C60;  // floats from one position to the next
constexpr uint32_t kTexCoords = 0x16E8C60, kTexCoordsEnd = 0x17E8C60;
constexpr uint32_t kTexCoordPtrs = 0x16E8C40;

// rep movsd / movsb (forward, as the original's direction flag has it).
void RepMovs(uint32_t dst, uint32_t src, uint32_t dwords, uint32_t bytes) {
  uint8_t* const B = g_guest_base;
  const uint32_t n = dwords * 4 + bytes;
  if (dst + n <= src || src + n <= dst) {
    std::memmove(B + dst, B + src, n);
    return;
  }
  for (uint32_t i = 0; i < dwords; i++) Put(dst + 4 * i, U32(src + 4 * i));
  for (uint32_t i = dwords * 4; i < n; i++) B[dst + i] = B[src + i];
}

// A copied generator: the source itself when nothing modifies it, else a
// copy into dst (2 floats a vertex in whole dwords, 3 in dwords and bytes).
void Source(uint32_t ptr, uint32_t dst, uint32_t src, bool direct, uint32_t floats) {
  if (direct) {
    Put(ptr, src);
    return;
  }
  const uint32_t bytes = U32(kNumVertexes) * floats * 4;
  RepMovs(dst, src, bytes >> 2, floats == 2 ? 0 : bytes & 3);
}

}  // namespace

// ComputeTexCoords (EAX: the shader; its stages are 0xC8 bytes from +0x94
// on, tcGen at +0xA0 of each, the two tcGen vectors at +0xA4, texMods at
// +0xC0, count +0xBC, 0x44 bytes each): each of the eight stages' texture
// coordinates, generated into its own buffer (or pointing at the
// tessellator's when nothing modifies them), then modified. A tcGen of 0
// ends it all.
void f_004F5940(Ctx& c) {
  const uint32_t e = c.esp, locSt = e - 4, locStage = e - 8;
  // The callee-saved registers, restored on return (a guest longjmp
  // through here sets them itself).
  const uint32_t ebx = c.ebx, esi = c.esi, edi0 = c.edi, ebp = c.ebp;
  auto restore = [&] {
    c.ebx = ebx;
    c.esi = esi;
    c.edi = edi0;
    c.ebp = ebp;
    c.esp = e + 4;
  };
  c.esp = e - 0x18;
  uint32_t edi = c.edi;
  uint32_t tc = kTexCoords, stage = c.eax + 0xAC, ptr = kTexCoordPtrs;
  Put(locSt, tc);
  Put(locStage, stage);
  // The registers a call site leaves for its callee (EBX and ESI unless
  // the site passes something in them), and the call.
  auto call = [&](GuestFn fn, uint32_t ret, uint32_t ebx = 0, uint32_t esi = 0) {
    c.ebx = ebx ? ebx : tc;
    c.esi = esi ? esi : stage;
    c.edi = edi;
    c.ebp = ptr;
    Call(c, fn, ret);
  };
  do {
    Put(ptr, tc);
    const uint32_t gen = U32(stage - 0xC);
    const bool unmodified = I32(stage + 0x10) == 0;
    c.eax = gen;
    switch (gen) {
      case 0:
        restore();
        return;
      case 1: {
        const uint32_t n = U32(kNumVertexes) * 2;
        for (uint32_t i = 0; i < n; i++) Put(tc + 4 * i, 0);
        break;
      }
      case 2:
        Source(ptr, tc, 0x1670C40, U32(0x11E075C) == 0 && unmodified, 2);
        break;
      case 3:
        Source(ptr, tc, 0x1650C40, U32(0x11E075C) == 0 && unmodified, 2);
        break;
      case 4:
        c.eax = tc;
        call(f_0050A2B0, 0x4F5A65);
        break;
      case 6: {
        // TCGEN_VECTOR: each position against the two vectors.
        uint32_t xyz = 0x1630C40;
        for (int32_t i = 0; i < I32(kNumVertexes);) {
          const F32 x = Ld(xyz), y = Ld(xyz + 4), z = Ld(xyz + 8);
          const uint32_t out = U32(ptr) + uint32_t(i) * 8;
          Put(out, Fst(St(Ld(stage - 4)) * y + St(Ld(stage - 8)) * x + St(z) * Ld(stage)));
          Put(U32(ptr) + uint32_t(i) * 8 + 4,
              Fst(St(Ld(stage + 4)) * x + St(z) * Ld(stage + 0xC) + St(Ld(stage + 8)) * y));
          i++;
          xyz += U32(kXyzStride) * 4;
        }
        break;
      }
      case 7:
        Source(ptr, tc, 0x1698C40, unmodified, 3);  // into EBX
        break;
      case 8:
        if (U32(0x17F0C68) == 0) {
          c.eax = 0;
          call(f_0050ABF0, 0x4F5AB4);
        }
        Source(ptr, U32(ptr), 0x16C8C40, I32(stage + 0x10) == 0, 3);
        break;
      case 9:
        if (U32(0x17F0C6C) == 0) {
          c.eax = 0;
          call(f_0050ABF0, 0x4F5AFF);
        }
        Source(ptr, U32(ptr), 0x16B0C40, I32(stage + 0x10) == 0, 3);
        break;
      case 10:
      case 11:
      case 12:
        Push(c, gen - 10);
        c.eax = tc;
        call(f_0050A7C0, gen == 10 ? 0x4F5B28 : gen == 11 ? 0x4F5B36 : 0x4F5B44);
        c.esp += 4;
        break;
      case 13:
        c.ecx = tc;
        call(f_0050A3D0, 0x4F5B50);
        break;
      case 14:
        c.ecx = tc;
        call(f_0050A380, 0x4F5B59);
        break;
      case 15:
        c.edx = tc;
        call(f_0050A420, 0x4F5B62);
        break;
      case 16:
        c.ecx = tc;
        call(f_0050A4D0, 0x4F5B6B);
        break;
      case 17:
        c.eax = tc;
        call(f_0050A5C0, 0x4F5B74);
        break;
      case 18:
        c.eax = tc;
        call(f_0050A720, 0x4F5B7D);
        break;
      case 19:
      case 20:
      case 21:
        c.eax = gen - 19;
        c.ecx = tc;
        call(f_0050A820, 0x4F5B96);
        break;
      default:  // 5 and above 21: nothing
        break;
    }
    edi = 0;
    for (; int32_t(edi) < I32(stage + 0x10); edi++) {
      const uint32_t tm = U32(stage + 0x14) + edi * 0x44, type = U32(tm);
      c.eax = tm;
      c.ecx = type;
      c.edx = U32(stage + 0x14);
      switch (type) {
        case 0:  // TMOD_NONE: the loop stops at 5
          edi = 4;
          break;
        case 1:
          c.ecx = U32(ptr);
          call(f_0050AA30, 0x4F5C3E);
          break;
        case 2:
          c.eax = U32(ptr);
          call(f_0050A8D0, 0x4F5BE2, tm + 4);
          tc = U32(locSt);
          stage = U32(locStage);
          break;
        case 3:
          c.ecx = U32(ptr);
          c.eax = tm + 0x38;
          call(f_0050A9B0, 0x4F5C0F);
          break;
        case 4:
          c.edx = tm + 0x30;
          c.eax = U32(ptr);
          call(f_0050A980, 0x4F5C1F);
          break;
        case 5:
          c.ecx = U32(ptr);
          Push(c, c.ecx);
          call(f_00508A50, 0x4F5C2D, 0, tm + 4);
          stage = U32(locStage);
          c.esp += 4;
          break;
        case 6:
          c.edx = U32(ptr);
          c.eax = U32(tm + 0x40);
          Push(c, c.edx);
          Push(c, c.eax);
          call(f_0050AA80, 0x4F5C4D);
          c.esp += 8;
          break;
        case 7:
          c.eax = U32(0x1549B3C) + 0x70;
          c.ecx = U32(ptr);
          call(f_0050A9B0, 0x4F5BFF);
          break;
        case 8:
          c.eax = U32(ptr);
          call(f_0050A890, 0x4F5BD2);
          break;
        case 9: {
          // Negated, three floats per vertex.
          const uint32_t st = U32(ptr);
          for (int32_t i = 0, n = I32(kNumVertexes) * 3; i < n; i++)
            Put(st + 4 * uint32_t(i), Fst(-St(Ld(st + 4 * uint32_t(i)))));
          break;
        }
        case 10:
          c.eax = U32(ptr);
          call(f_0050AE10, 0x4F5C79);
          break;
        default:
          c.edx = U32(0x17F0C70);
          Push(c, c.edx);
          Push(c, type);
          Push(c, 0x53C06C);  // "ERROR: unknown texmod '%d' in shader '%s'\n"
          Push(c, 1);
          c.ebx = tc;
          c.esi = stage;
          c.edi = edi;
          c.ebp = ptr;
          CallPtr(c, U32(0x1549604), 0x4F5C90, 0x4F5C8A);
          c.esp += 0x10;
          break;
      }
    }
    tc += 0x20000;
    stage += 0xC8;
    ptr += 4;
    Put(locSt, tc);
    Put(locStage, stage);
  } while (int32_t(tc) < int32_t(kTexCoordsEnd));
  restore();
}

// ---- sorting, matrices, copies ------------------------------------------------

namespace {

// The 8-byte swap of qsortFast's elements, in the original's order of
// reads and writes.
void Swap8(uint32_t a, uint32_t b) {
  const uint32_t a0 = U32(a), b0 = U32(b);
  Put(b, a0);
  const uint32_t a1 = U32(a + 4);
  Put(a, b0);
  const uint32_t b1 = U32(b + 4);
  Put(b + 4, a1);
  Put(a + 4, b1);
}

// The selection sort of up to eight elements (lo to hi inclusive, 8 bytes
// each, unsigned keys first).
void ShortSort(uint32_t lo, uint32_t hi) {
  while (hi > lo) {
    uint32_t max = lo;
    for (uint32_t p = lo + 8; p <= hi; p += 8)
      if (U32(p) > U32(max)) max = p;
    // The original's order: [hi] to [max] first.
    const uint32_t h0 = U32(hi), m0 = U32(max);
    Put(max, h0);
    const uint32_t h1 = U32(hi + 4);
    Put(hi, m0);
    const uint32_t m1 = U32(max + 4);
    Put(max + 4, h1);
    Put(hi + 4, m1);
    hi -= 8;
  }
}

}  // namespace

// qsortFast (ECX base, EAX count, EBX width): the draw surfaces' sort on
// their unsigned keys; swaps move 8 bytes whatever the width.
void f_004CADC0(Ctx& c) {
  const uint32_t width = c.ebx;
  uint32_t lo = c.ecx, num = c.eax;
  if (num >= 2 && width) {
    uint32_t hi = lo + (num - 1) * width;
    uint32_t lostk[32], histk[32];
    int32_t stkptr = 0;
    for (;;) {
      const uint32_t size = (hi - lo) / width + 1;
      if (size <= 8) {
        ShortSort(lo, hi);
      } else {
        Swap8(lo, lo + (size >> 1) * width);
        uint32_t loguy = lo, higuy = hi + width;
        for (;;) {
          do loguy += width;
          while (loguy <= hi && U32(loguy) <= U32(lo));
          do higuy -= width;
          while (higuy > lo && U32(higuy) >= U32(lo));
          if (higuy < loguy) break;
          Swap8(higuy, loguy);
        }
        Swap8(higuy, lo);
        if (int32_t(higuy - lo - 1) >= int32_t(hi - loguy)) {
          if (lo + width < higuy) {
            lostk[stkptr] = lo;
            histk[stkptr++] = higuy - width;
          }
          if (loguy < hi) {
            lo = loguy;
            continue;
          }
        } else {
          if (loguy < hi) {
            lostk[stkptr] = loguy;
            histk[stkptr++] = hi;
          }
          if (lo + width < higuy) {
            hi = higuy - width;
            continue;
          }
        }
      }
      if (--stkptr < 0) break;
      lo = lostk[stkptr];
      hi = histk[stkptr];
    }
  }
  c.esp += 4;
}

// The 4x4 matrix product out = a * b (EAX a, ECX b, EDX out; row-major
// floats), each element summed from the last term on and stored before the
// next is read, as the original does.
void f_004C94D0(Ctx& c) {
  const uint32_t a = c.eax, b = c.ecx, out = c.edx;
  for (uint32_t i = 0; i < 4; i++) {
    const uint32_t ar = a + 16 * i;
    for (uint32_t j = 0; j < 4; j++) {
      // The last product loads a's element for column 0, b's for the others.
      const St last = j ? St(Ld(b + 4 * j)) * Ld(ar) : St(Ld(ar)) * Ld(b);
      const St s = St(Ld(b + 0x30 + 4 * j)) * Ld(ar + 0xC) + St(Ld(b + 0x20 + 4 * j)) * Ld(ar + 8) +
                   St(Ld(b + 0x10 + 4 * j)) * Ld(ar + 4) + last;
      Put(out + 16 * i + 4 * j, Fst(s));
    }
  }
  c.eax = a + 0x48;
  c.edx = out + 0x48;
  c.esp += 4;
}

// memcpy(dst, src, count; cdecl) with Quake 3's Com_Memcpy structure:
// 32-byte blocks from the end down, then the rest upwards. That is memmove
// unless the two overlap or count is negative, which the original handles.
void f_0042B0A0(Ctx& c) {
  const uint32_t dst = Arg(c, c.esp, 1), src = Arg(c, c.esp, 2);
  const int32_t count = int32_t(Arg(c, c.esp, 3));
  const uint32_t n = uint32_t(count);
  if (count < 0 || (dst != src && dst < src + n && src < dst + n)) {
    f_0042B0A0_lifted(c);
    return;
  }
  std::memmove(g_guest_base + dst, g_guest_base + src, n);
  c.esp += 4;
}

// ---- the front end: the view ------------------------------------------------

namespace {

// The current orientation (origin, then three axes; the view's or an
// entity's), the view's own (origin 0x1536030, axes 0x153603C), its fov
// x and y (0x1536168, 0x153616C), the four frustum planes (cplane_t, 0x14
// bytes), the far clip, the projection matrix and its copy, and the
// refdef's fov x and y and time.
constexpr uint32_t kOr = 0x153629C, kViewOrigin = 0x1536030, kViewAxis = 0x153603C;
constexpr uint32_t kFovX = 0x1536168, kFovY = 0x153616C, kFrustum = 0x15361F8;
constexpr uint32_t kZFar = 0x1536248, kProjection = 0x1536178, kProjectionCopy = 0x15361B8;
constexpr uint32_t kRefdefFovX = 0x1536328, kRefdefFovY = 0x153632C, kTime = 0x1536360;
// Cvars (cvar_t*: float at +0x1C, integer at +0x20): r_nocull, the debug
// level the fog and far clip print at (5), r_znear, the far clip's and a
// depth one.
constexpr uint32_t kNoCull = 0x15344F4, kFogDebug = 0x15346E0, kZNear = 0x15347E4,
                   kFarClip = 0x15343F0, kDepthCvar = 0x1534458;
// ri.Printf, through the refimport table.
constexpr uint32_t kPrintfPtr = 0x1549600;
// The fog now, at the start of a transition and at its target (16 dwords
// each: mode (GL_EXP 0x800, GL_LINEAR 0x2601), start and end times, colour,
// start, end, density, ...).
constexpr uint32_t kFog = 0x1535920, kFogFrom = 0x1535960, kFogTo = 0x15359A0;

int32_t CvarInt(uint32_t cvar) { return I32(U32(cvar) + 0x20); }

// A float pushed as the double a varargs call takes (FLD m32, FSTP m64).
void PushDouble(Ctx& c, F32 f) {
  const uint64_t d = std::bit_cast<uint64_t>(St(f).v);
  c.esp -= 8;
  Put(c.esp, uint32_t(d));
  Put(c.esp + 4, uint32_t(d >> 32));
}
// ri.Printf(PRINT_ALL, fmt, ...) with the doubles already pushed, from the
// call site at va (returning to ret); the caller's add esp follows.
void Printf(Ctx& c, uint32_t fmt, uint32_t ret, uint32_t va) {
  Push(c, fmt);
  Push(c, 0);
  CallPtr(c, U32(kPrintfPtr), ret, va);
}

// FSINCOS of x on the guest's x87 stack, the cosine stored first, then the
// sine (out of range FSINCOS pushes nothing: x is stored as the cosine and
// the register below it as the sine, which the second pop takes too).
void SinCos(Ctx& c, F32 x, F32& cos, F32& sin) {
  x87::LoadF32(c.fpu, x.bits);
  x87::SinCos(c.fpu);
  cos.bits = x87::StoreF32(c.fpu);
  x87::Pop(c.fpu);
  sin.bits = x87::StoreF32(c.fpu);
  x87::Pop(c.fpu);
}

}  // namespace

// The cull of a local bounds (EAX: mins, maxs) under the current
// orientation against the four frustum planes: 0 all in front of every
// plane, 2 all behind one, else 1; 1 with r_nocull. Its eight corners are
// transformed to floats first; a plane's test stops at a corner in front
// once one has been behind.
void f_004C8D50(Ctx& c) {
  const uint32_t b = c.eax;
  c.esp += 4;
  if (CvarInt(kNoCull)) {
    c.eax = 1;
    return;
  }
  F32 v[8][3];
  for (uint32_t i = 0; i < 8; i++) {
    const St x = St(Ld(b + 0xC * (i & 1))), y = St(Ld(b + 4 + 0xC * ((i >> 1) & 1))),
             z = St(Ld(b + 8 + 0xC * ((i >> 2) & 1)));
    for (uint32_t k = 0; k < 3; k++)
      v[i][k] = Fst(St(Ld(kOr + 0x24 + 4 * k)) * z + St(Ld(kOr + 0x18 + 4 * k)) * y +
                    St(Ld(kOr + 0xC + 4 * k)) * x + Ld(kOr + 4 * k));
  }
  bool clipped = false;
  for (uint32_t p = kFrustum; p < kFrustum + 4 * 0x14; p += 0x14) {
    bool back = false, front = false;
    for (uint32_t i = 0; i < 8; i++) {
      const St d = St(v[i][2]) * Ld(p + 8) + St(v[i][1]) * Ld(p + 4) + St(v[i][0]) * Ld(p);
      if (!(d > Ld(p + 0xC))) {
        back = true;
      } else {
        front = true;
        if (back) break;
      }
    }
    if (!front) {
      c.eax = 2;
      return;
    }
    clipped |= back;
  }
  c.eax = clipped;
}

// The cull of a sphere (EDX centre; radius on the stack, cdecl) against
// the four frustum planes: 2 behind one by more than the radius, else 1
// when one is within it, else 0; 1 with r_nocull.
void f_004C9070(Ctx& c) {
  const uint32_t e = c.esp, pt = c.edx;
  const F32 r = Ld(e + 4);
  c.esp = e + 4;
  if (CvarInt(kNoCull)) {
    c.eax = 1;
    return;
  }
  const F32 nr = Fst(-St(r));
  bool clipped = false;
  for (uint32_t p = kFrustum; p < kFrustum + 4 * 0x14; p += 0x14) {
    const St d =
        St(Ld(p)) * Ld(pt) + St(Ld(p + 8)) * Ld(pt + 8) + St(Ld(p + 4)) * Ld(pt + 4) - Ld(p + 0xC);
    if (d < nr) {
      c.eax = 2;
      return;
    }
    if (d <= r) clipped = true;
  }
  c.eax = clipped;
}

// A local vector (EAX) rotated into the world by the current orientation's
// axes, into ECX (each component stored before the next is read).
void f_004C90F0(Ctx& c) {
  const uint32_t in = c.eax, out = c.ecx;
  for (uint32_t k = 0; k < 3; k++)
    Put(out + 4 * k, Fst(St(Ld(kOr + 0xC + 4 * k)) * Ld(in) + St(Ld(kOr + 0x24 + 4 * k)) * Ld(in + 8) +
                         St(Ld(kOr + 0x18 + 4 * k)) * Ld(in + 4)));
  c.esp += 4;
}

// The fog for this frame: the target's (the transition copied whole, its
// flag 0x153595C clear) once the transition has ended or with none; with
// one between GL_EXP and GL_LINEAR (either way) the target's at once (its
// end time cleared); else the start's colour, start, end and density moved
// towards the target's by the time passed (at most all of it). A linear
// fog's end nearer than the far clip becomes the far clip. The debug level
// 5 prints the far clip (and does nothing else) while the target's flag
// 0x15359D0 is clear, else the fog's end, the far clip and density.
void f_004C9930(Ctx& c) {
  const uint32_t e = c.esp;
  // push ebp; and esp, -8; sub esp, 8; four registers pushed.
  const uint32_t sp = (((e - 4) & ~7u) - 8) - 16;
  if (CvarInt(kFogDebug) == 5 && !U32(0x15359D0)) {
    c.esp = sp;
    PushDouble(c, Ld(kZFar));
    Printf(c, 0x543190, 0x4C996C, 0x4C9966);
    c.esp = e + 4;
    return;
  }
  const int32_t end = I32(kFogTo + 0xC), time = I32(kTime);
  const uint32_t from = U32(kFogFrom), to = U32(kFogTo);
  if (!end || end < time) {
    std::memmove(g_guest_base + kFog, g_guest_base + kFogTo, 0x40);
    Put(kFog + 0x3C, 0);
  } else if ((from == 0x800 && to == 0x2601) || (from == 0x2601 && to == 0x800)) {
    std::memmove(g_guest_base + kFog, g_guest_base + kFogTo, 0x40);
    Put(kFogTo + 0xC, 0);
    Put(kFog + 0x3C, 1);
  } else {
    const int32_t start = I32(kFogTo + 8), span = end - start > 0 ? end - start : 1;
    St f = FromInt(time - start) / FromInt(span);
    if (f > Ld(0x55B580)) f = St(Ld(0x55B580));
    Put(kFog, to);
    Put(kFog + 0x30, 1);
    for (uint32_t k : {0x20u, 0x24u, 0x2Cu, 0x10u, 0x14u, 0x18u})
      Put(kFog + k, Fst((St(Ld(kFogTo + k)) - Ld(kFogFrom + k)) * f + Ld(kFogFrom + k)));
    Put(kFog + 0x38, U32(kFogTo + 0x38) || U32(kFogFrom + 0x38));
    Put(kFog + 0x3C, 1);
  }
  const uint32_t mode = U32(kFog);
  if (mode == 0x2601 && St(Ld(kFog + 0x24)) < Ld(kZFar)) Put(kZFar, U32(kFog + 0x24));
  if (CvarInt(kFogDebug) == 5) {
    c.esp = sp;
    PushDouble(c, Ld(kFog + 0x24));
    PushDouble(c, Ld(kZFar));
    PushDouble(c, Ld(kFog + 0x2C));
    if (mode == 0x2601)
      Printf(c, 0x543150, 0x4C9B6E, 0x4C9B68);
    else
      Printf(c, 0x543110, 0x4C9B85, 0x4C9B7F);
  }
  c.esp = e + 4;
}

// The far clip: 2048 for a refdef flag 1; else the far clip cvar's when
// it is not zero (printed at the debug level 5 after the fog), else 2^19;
// then the fog's (f_004C9930, a tail call in the last case).
void f_004C9B90(Ctx& c) {
  const uint32_t e = c.esp;
  if (U8(0x1536364) & 1) {
    Put(kZFar, 0x45000000);
    c.esp = e + 4;
    return;
  }
  const uint32_t far = U32(kFarClip);
  if (St(Ld(far + 0x1C)) == Ld(0x55B584)) {
    Put(kZFar, 0x49000000);
    f_004C9930(c);
    return;
  }
  Put(kZFar, U32(far + 0x1C));
  Call(c, f_004C9930, 0x4C9BC7);
  if (CvarInt(kFogDebug) == 5) {
    PushDouble(c, Ld(kZFar));
    Printf(c, 0x5430E8, 0x4C9BEC, 0x4C9BE6);
    c.esp = e;
  }
  c.esp = e + 4;
}

// The projection matrix (column-major, 16 floats) from the refdef's fov
// and r_znear, after the far clip (f_004C9B90), and its copy, whose
// element 14 then takes the depth cvar's value instead. FPTAN keeps 64
// significant bits, so the arithmetic is the x87 model's, on the guest's
// stack as the original has it.
void f_004C9C00(Ctx& c) {
  const uint32_t e = c.esp;
  c.esp = e - 0xC;  // ECX, ESI and EDI pushed
  Call(c, f_004C9B90, 0x4C9C08);
  x87::State& f = c.fpu;
  using x87::Op;
  auto m = [](int i) { return kProjection + 4 * uint32_t(i); };
  auto tan = [&](uint32_t fov) {
    x87::LoadF32(f, U32(fov));
    x87::ArithF32(f, Op::Mul, U32(0x55B7B4));
    x87::Ptan(f);
    x87::Pop(f);
  };
  auto store = [&](uint32_t va) {
    Put(va, x87::StoreF32(f));
    x87::Pop(f);
  };
  x87::LoadF32(f, U32(U32(kZNear) + 0x1C));
  tan(kRefdefFovY);
  for (int i : {4, 12, 1, 13, 2, 6, 3, 7, 15}) Put(m(i), 0);
  Put(m(10), 0xBF800000);
  Put(m(11), 0xBF800000);
  x87::LoadSt(f, 0);
  x87::Chs(f);  // -tan y
  tan(kRefdefFovX);
  x87::LoadSt(f, 0);
  x87::Chs(f);  // -tan x
  x87::LoadSt(f, 1);
  x87::ArithSt0Sti(f, Op::Sub, 1);  // width
  x87::LoadSt(f, 4);
  x87::ArithSt0Sti(f, Op::Sub, 4);  // height
  const uint32_t height = x87::StoreF32(f);
  x87::Pop(f);
  x87::ArithF32(f, Op::DivR, U32(0x55B580));
  x87::LoadSt(f, 0);
  x87::ArithSt0Sti(f, Op::Add, 1);
  store(m(0));
  x87::Xch(f, 1);
  x87::ArithSt0Sti(f, Op::Add, 2);
  x87::ArithSt0Sti(f, Op::Mul, 1);
  store(m(8));
  x87::Pop(f);
  x87::Pop(f);
  x87::LoadF32(f, U32(0x55B580));
  x87::ArithF32(f, Op::Div, height);
  x87::LoadSt(f, 0);
  x87::ArithSt0Sti(f, Op::Add, 1);
  store(m(5));
  x87::Xch(f, 1);
  x87::ArithSt0Sti(f, Op::Add, 2);
  x87::ArithSt0Sti(f, Op::Mul, 1);
  store(m(9));
  x87::Pop(f);
  x87::Pop(f);
  x87::ArithF32(f, Op::Mul, U32(0x55B710));
  store(m(14));
  x87::LoadF32(f, U32(U32(kDepthCvar) + 0x1C));
  x87::ArithF32(f, Op::Mul, U32(0x55B710));
  std::memmove(g_guest_base + kProjectionCopy, g_guest_base + kProjection, 0x40);
  store(kProjectionCopy + 0x38);
  c.esp = e + 4;
}

// The frustum's four side planes from the view's axes and fov: left and
// right from the x fov, bottom and top from the y one (each normal the
// forward axis by the sine plus or minus the side axis by the cosine),
// non-axial, with their distances from the view origin and sign bits.
void f_004C9D30(Ctx& c) {
  for (uint32_t side = 0; side < 2; side++) {
    const uint32_t fov = side ? kFovY : kFovX, axis = kViewAxis + 0xC * (side + 1);
    const uint32_t p0 = kFrustum + 0x28 * side, p1 = p0 + 0x14;
    const F32 a = Fst(St(Ld(fov)) / Ld(0x55B6D8) * Ld(0x55B7AC));
    F32 cs, sn;
    SinCos(c, a, cs, sn);
    for (uint32_t k = 0; k < 3; k++) {
      const F32 v = Fst(St(Ld(kViewAxis + 4 * k)) * sn);
      Put(p0 + 4 * k, v);
      Put(p1 + 4 * k, v);
    }
    for (uint32_t k = 0; k < 3; k++) Put(p0 + 4 * k, Fst(St(Ld(axis + 4 * k)) * cs + Ld(p0 + 4 * k)));
    const F32 ncs = Fst(-St(cs));
    for (uint32_t k = 0; k < 3; k++) Put(p1 + 4 * k, Fst(St(ncs) * Ld(axis + 4 * k) + Ld(p1 + 4 * k)));
  }
  for (uint32_t p = kFrustum; p < kFrustum + 4 * 0x14; p += 0x14) {
    Wr<uint8_t>(g_guest_base, p + 0x10, 3);
    Put(p + 0xC, Fst(St(Ld(kViewOrigin + 8)) * Ld(p + 8) + St(Ld(kViewOrigin + 4)) * Ld(p + 4) +
                     St(Ld(kViewOrigin)) * Ld(p)));
    uint8_t bits = 0;
    for (uint32_t k = 0; k < 3; k++)
      if (St(Ld(p + 4 * k)) < Ld(0x55B584)) bits |= uint8_t(1 << k);
    Wr<uint8_t>(g_guest_base, p + 0x11, bits);
  }
  c.esp += 4;
}

// A point (ESI) from one orientation (ECX: origin, axes) into another
// (EAX), into EDX: the offset from ECX's origin along each of its axes,
// then along EAX's axes, summed with partial sums stored as floats where
// the original stores them, plus EAX's origin.
void f_004C9FF0(Ctx& c) {
  const uint32_t p = c.esi, a = c.ecx, b = c.eax, out = c.edx;
  const St dx = St(Ld(p)) - Ld(a), dy = St(Ld(p + 4)) - Ld(a + 4), dz = St(Ld(p + 8)) - Ld(a + 8);
  const St l0 = dz * Ld(a + 0x14) + dy * Ld(a + 0x10) + dx * Ld(a + 0xC);
  const St x0 = l0 * Ld(b + 0xC);
  F32 y = Fst(l0 * Ld(b + 0x10)), z = Fst(l0 * Ld(b + 0x14));
  const St l1 = dz * Ld(a + 0x20) + dy * Ld(a + 0x1C) + dx * Ld(a + 0x18);
  const F32 l1f = Fst(l1), x = Fst(l1 * Ld(b + 0x18) + x0);
  y = Fst(St(l1f) * Ld(b + 0x1C) + y);
  z = Fst(St(l1f) * Ld(b + 0x20) + z);
  const St l2 = dz * Ld(a + 0x2C) + dy * Ld(a + 0x28) + dx * Ld(a + 0x24);
  const St x2 = l2 * Ld(b + 0x24) + x;
  y = Fst(l2 * Ld(b + 0x28) + y);
  z = Fst(l2 * Ld(b + 0x2C) + z);
  Put(out, Fst(x2 + Ld(b)));
  Put(out + 4, Fst(St(y) + Ld(b + 4)));
  Put(out + 8, Fst(St(z) + Ld(b + 8)));
  c.esp += 4;
}

// A vector (ESI) along one orientation's axes (EDX) into another's (ECX),
// into EAX: zeroed, then each of EDX's components added along ECX's axes.
void f_004CA0C0(Ctx& c) {
  const uint32_t out = c.eax, a = c.edx, v = c.esi, b = c.ecx;
  for (uint32_t k = 0; k < 3; k++) Put(out + 8 - 4 * k, 0);
  for (uint32_t i = 0; i < 3; i++) {
    const uint32_t ax = a + 0xC + 0xC * i, bx = b + 0xC + 0xC * i;
    const St l = St(Ld(ax + 8)) * Ld(v + 8) + St(Ld(ax + 4)) * Ld(v + 4) + St(Ld(ax)) * Ld(v);
    for (uint32_t k = 0; k < 3; k++)
      Put(out + 4 * k, i ? Fst(l * Ld(bx + 4 * k) + Ld(out + 4 * k)) : Fst(l * Ld(bx + 4 * k)));
  }
  c.esp += 4;
}

// ---- the front end: the draw surface list ------------------------------------

namespace {

// The draw surfaces (count, array of 8 bytes: sort key, surface), the
// shaders by sorted index, and the back end's data (its command buffer at
// +0x24D30C, the bytes used at +0x28D700).
constexpr uint32_t kNumDrawSurfs = 0x1536490, kDrawSurfs = 0x1536494, kSortedShaders = 0x154052C;
constexpr uint32_t kBackEndData = 0x1549698;

// rep movs of n dwords.
void MovsD(uint32_t dst, uint32_t src, uint32_t n) {
  for (; n; n--, dst += 4, src += 4) Put(dst, U32(src));
}

}  // namespace

// R_AddDrawSurf(surface, dlight bits, a, b; cdecl; EAX fog, EDX shader):
// the sort key is the shader's sorted index (+0x48) with the fog above it,
// then a and b, the view's flag 0x1536028 and the dlight bits; nothing
// once there are 0x10000 (a signed count). The count and array are read
// again for the surface after the key is stored.
void f_004CAF00(Ctx& c) {
  const uint32_t e = c.esp, n = U32(kNumDrawSurfs);
  if (int32_t(n) < 0x10000) {
    uint32_t key = U32(c.edx + 0x48) | c.eax << 12;
    key = key << 16 | Arg(c, e, 3);
    key = key << 1 | Arg(c, e, 4);
    key = (key << 1 | U32(0x1536028)) | Arg(c, e, 2);
    Put(U32(kDrawSurfs) + n * 8, key);
    const uint32_t m = U32(kNumDrawSurfs);
    Put(U32(kDrawSurfs) + m * 8 + 4, Arg(c, e, 1));
    Put(kNumDrawSurfs, U32(kNumDrawSurfs) + 1);
  }
  c.esp = e + 4;
}

// R_AddDrawSurfCmd(draw surfaces; EDX their count): a command of 0x3F4
// bytes from the back end's buffer (none once it would pass 0x3FFF8 bytes,
// unsigned): its id 7, the surfaces and count, the refdef and the view.
void f_004D4230(Ctx& c) {
  const uint32_t bed = U32(kBackEndData), used = U32(bed + 0x28D700) + 0x3F4;
  if (used <= 0x3FFF8) {
    Put(bed + 0x28D700, used);
    const uint32_t cmd = used + bed + 0x24D30C;
    if (cmd) {
      Put(cmd + 0x3EC, Arg(c, c.esp, 1));
      Put(cmd, 7);
      Put(cmd + 0x3F0, c.edx);
      MovsD(cmd + 4, 0x1536318, 0x62);
      MovsD(cmd + 0x18C, 0x1536030, 0x98);
    }
  }
  c.esp += 4;
}

// R_SortDrawSurfs(draw surfaces, count; cdecl): sorted (qsortFast, at most
// 0x10000), then up to the first shader sorted after 1.0 (or all of them
// with the cvar 0x14E43FC) each opaque one checked: ri.Error for a sort of
// 0 (SS_BAD), and for a portal (exactly 1.0) the view through it
// (f_004CAB80 with the entity, 0x7FE for bit 1), which ends it without the
// command when drawn and the cvar 0x1534400 is set; then the command
// (f_004D4230). The loop's counter is in the count's argument slot. The
// registers are the original's for the callees.
void f_004CAFC0(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, esi = c.esi, edi = c.edi, ebp = c.ebp;
  int32_t num = I32(e + 8);
  if (num < 1) {
    c.esp = e - 4;
    Push(c, Arg(c, e, 1));
    c.edx = uint32_t(num);
    Call(c, f_004D4230, 0x4CAFD6);
    c.esp = e + 4;
    return;
  }
  if (num > 0x10000) num = 0x10000;
  const uint32_t surfs = Arg(c, e, 1);
  c.esp = e - 0x10;
  c.ebx = 8;
  c.eax = uint32_t(num);
  c.ecx = surfs;
  Call(c, f_004CADC0, 0x4CAFFD);
  Put(e + 8, 0);
  for (uint32_t p = surfs;; p += 8) {
    const uint32_t sort = U32(p);
    uint32_t ent = (sort >> 7) & 0x7FF;
    const uint32_t shader = U32(kSortedShaders + 4 * ((sort >> 18) & 0xFFF));
    if (sort & 2) ent = 0x7FE;
    if (St(Ld(shader + 0x58)) > Ld(0x55B580)) break;
    if (St(Ld(shader + 0x58)) == Ld(0x55B584)) {
      c.esp = e - 0x10;
      Push(c, shader);
      Push(c, 0x5430A0);
      Push(c, 1);
      CallPtr(c, U32(0x1549604), 0x4CB066, 0x4CB060);
    }
    if (CvarInt(0x14E43FC)) break;
    if (U32(shader + 0x58) == 0x3F800000) {
      c.esp = e - 0x10;
      c.ebx = p;
      c.esi = ent;
      c.edi = shader;
      c.ebp = uint32_t(num);
      Push(c, ent);
      Call(c, f_004CAB80, 0x4CB085);
      c.ebx = ebx;
      c.esi = esi;
      c.edi = edi;
      c.ebp = ebp;
      if (c.eax && CvarInt(0x1534400)) {
        c.esp = e + 4;
        return;
      }
    }
    const int32_t i = I32(e + 8) + 1;
    Put(e + 8, uint32_t(i));
    if (i >= num) break;
  }
  c.esp = e - 0x10;
  Push(c, Arg(c, e, 1));
  c.edx = uint32_t(num);
  Call(c, f_004D4230, 0x4CB0B8);
  c.ebx = ebx;
  c.esp = e + 4;
}

// ---- the front end: the world's view -------------------------------------------

namespace {

// The world's copies of the view: its origin, the frustum planes (with the
// far corners' offsets, nudged 0.001 back), the far plane's distance, the
// view-projection matrix (transposed) and its inverse; the world.
constexpr uint32_t kCullOrigin = 0x11BEAF8, kCullFrustum = 0x11BEB04, kCullFar = 0x11BEB84;
constexpr uint32_t kViewProj = 0x11BEB90, kViewProjInverse = 0x11BEBD0, kWorld = 0x1535A34;

// A plane (cplane_t) nudged 0.001 back, with its far corner's offsets
// within a bounds from its normal's signs as integers (-0 counts as 0).
void NudgePlane(uint32_t p) {
  Put(p + 0xC, Fst(St(Ld(p + 0xC)) - Ld(0x55B58C)));
  for (uint32_t k = 0; k < 3; k++)
    Wr<uint8_t>(g_guest_base, p + 0x10 + k, uint8_t((I32(p + 4 * k) <= 0 ? 0 : 0xC) + 4 * k));
}

// A point's distance along a plane's normal (z + y + x).
St Dot(uint32_t pt, uint32_t p) {
  return St(Ld(pt + 8)) * Ld(p + 8) + St(Ld(pt + 4)) * Ld(p + 4) + St(Ld(pt)) * Ld(p);
}

}  // namespace

// The nudge of a plane (EAX).
void f_004D6830(Ctx& c) {
  NudgePlane(c.eax);
  c.esp += 4;
}

// The far distance, returned on the x87 stack: the far clip cvar's, or
// with it 0 the linear fog's end while there is fog (0x13F9CF8 > 0 and
// 0x1535950); at least the far plane's.
void f_004D6880(Ctx& c) {
  uint32_t v = U32(U32(kFarClip) + 0x1C);
  if (St(F32{v}) == Ld(0x55B584) && I32(0x13F9CF8) > 0 && U32(0x1535950) && U32(kFog) == 0x2601)
    v = U32(kFog + 0x24);
  if (St(F32{v}) < Ld(kCullFar)) v = U32(kCullFar);
  x87::LoadF32(c.fpu, v);
  c.esp += 4;
}

// The frustum's four planes copied for the world's culls, each nudged.
void f_004D6A10(Ctx& c) {
  for (uint32_t i = 0; i < 4; i++) {
    MovsD(kCullFrustum + 0x14 * i, kFrustum + 0x14 * i, 4);
    NudgePlane(kCullFrustum + 0x14 * i);
  }
  c.esp += 4;
}

// The view-projection matrix: the view's matrix 0x15360E8 by the
// projection into a local, its inverse (0x427E80, from the local), and the
// local transposed.
void f_004D6AB0(Ctx& c) {
  const uint32_t e = c.esp, m = e - 0x40;
  for (uint32_t col = 0; col < 4; col++)
    for (uint32_t r = 0; r < 4; r++) {
      const uint32_t q = 0x15360F0 + 16 * r, pc = kProjection + 4 * col;
      Put(m + 16 * r + 4 * col, Fst(St(Ld(q + 4)) * Ld(pc + 0x30) + St(Ld(q - 8)) * Ld(pc) +
                                    St(Ld(q - 4)) * Ld(pc + 0x10) + St(Ld(pc + 0x20)) * Ld(q)));
    }
  c.esp = m;
  c.eax = kViewProjInverse;
  c.ecx = m;
  Call(c, f_00427E80, 0x4D6B20);
  for (uint32_t r = 0; r < 4; r++)
    for (uint32_t col = 0; col < 4; col++) Put(kViewProj + 16 * col + 4 * r, U32(m + 16 * r + 4 * col));
  c.esp = e + 4;
}

// R_CellForCamera: the world's BSP walked from its node (+0x98) by the
// view origin (in front of a node's plane: its first child) down to a leaf
// (cluster not -1); EAX its cell (+8). ri.Error without a world.
void f_004D7DF0(Ctx& c) {
  const uint32_t e = c.esp;
  if (!U32(kWorld)) {
    Push(c, 0x5425EC);
    Push(c, 1);
    CallPtr(c, U32(0x1549604), 0x4D7E06, 0x4D7E00);
    c.esp = e;
  }
  uint32_t node = U32(U32(kWorld) + 0x98);
  while (U32(node) == 0xFFFFFFFF) {
    const uint32_t p = U32(node + 0xC);
    node = U32(node + (Dot(kCullOrigin, p) - Ld(p + 0xC) > Ld(0x55B584) ? 0x10 : 0x14));
  }
  c.eax = U32(node + 8);
  c.esp = e + 4;
}

// Whether a polygon (EAX: points at +0x18, 12 bytes each, count at +0x1C)
// lies wholly behind (or on) one of ECX planes (EDX, 0x14 bytes each): 1 at
// the first plane none of its points is in front of (at once without
// points), else 0.
void f_004D7E70(Ctx& c) {
  const uint32_t pts = U32(c.eax + 0x18), n = U32(c.eax + 0x1C);
  uint32_t p = c.edx;
  c.eax = 0;
  for (uint32_t planes = c.ecx; planes; planes--, p += 0x14) {
    bool front = false;
    for (uint32_t k = n, pt = pts; k && !front; k--, pt += 0xC) front = Dot(pt, p) > Ld(p + 0xC);
    if (!front) {
      c.eax = 1;
      break;
    }
  }
  c.esp += 4;
}

// Whether no point of a polygon (EAX, as above) is in front of any of ECX
// planes (EDX): 0 at the first point in front of one, else 1.
void f_004D7ED0(Ctx& c) {
  const uint32_t pts = U32(c.eax + 0x18), n = U32(c.eax + 0x1C);
  uint32_t p = c.edx;
  c.eax = 1;
  for (uint32_t planes = c.ecx; planes && c.eax; planes--, p += 0x14)
    for (uint32_t k = n, pt = pts; k; k--, pt += 0xC)
      if (Dot(pt, p) > Ld(p + 0xC)) {
        c.eax = 0;
        break;
      }
  c.esp += 4;
}

}  // namespace recomp::gen
