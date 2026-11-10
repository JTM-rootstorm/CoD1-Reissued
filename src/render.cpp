// The renderer's front end natively, and the hottest lifted code under the
// back end: the front end's culling of world surfaces, static models and
// entities against the view frustum and the cells' occluders, the dlight
// bits of a bounds and adding a world surface; the view (its culls,
// transforms, fog transition, far clip, projection and frustum); the draw
// surface list; the world's view, its static models' bounds and links into
// the cells, and a cell's models and surfaces; the cell walk (polygon
// clips, the planes through a portal, a cell's occluders, the walk through
// the portals, the coronas and the world's surfaces); the view through a
// portal (a portal surface's orientations and its view); the scene
// (RE_RenderScene, a view's render and its surfaces, R_AddEntitySurfaces;
// the brush, model and composite model entities' surfaces, the polygons'
// and the world's without the cell walk);
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
#include <utility>
#include <vector>

#include "runtime/log.h"
#include "x86/lifted.h"
#include "x87_value.h"

namespace recomp::gen {

void f_004AD7E0(Ctx& c);  // draws a debug box (ECX mins, EDX maxs, EAX colour)
void f_004DEC10(Ctx& c);  // adds a culled-in entity (ent, ent)
void f_004CAF00(Ctx& c);  // R_AddDrawSurf(surface, dlight, 0, 0; EAX, EDX shader)
void f_0042B0A0_lifted(Ctx& c);  // the memcpy, for overlapping copies
void f_004CAB80(Ctx& c);  // a portal or mirror surface's view (EBX draw surface, entity; EAX: drawn)
void f_00427E80(Ctx& c);  // a 4x4 matrix's inverse (ECX in, EAX out)
void f_00429070(Ctx& c);  // adds a point to bounds (ECX point, EDX mins, ESI maxs)
void f_004290E0(Ctx& c);  // adds bounds to bounds (ECX/ESI mins/maxs, EDX/EDI the other's)
void f_00429800(Ctx& c);  // BoxOnPlaneSide(mins, maxs, plane)
void f_004268B0(Ctx& c);  // normalises a vector (EAX)
void f_00426710(Ctx& c);  // VectorNormalize (ESI; the length in ST0)
void f_004AD630(Ctx& c);  // draws a debug polygon (EBX count, stack points; EAX colour)
void f_004AD760(Ctx& c);  // draws a debug line (EBX from, EDI to; ESI colour)
void f_004CA170(Ctx& c);  // a surface's plane (ECX surface, ESI out)
void f_004C9580(Ctx& c);  // an entity's orientation (EAX out, EBX view, EDI entity)
void f_004CA8D0(Ctx& c);  // whether a portal surface is off the screen (draw surface; EAX)
void f_004273C0(Ctx& c);  // a vector across EBX into EDX
void f_00426AB0(Ctx& c);  // rotates a point about EAX (out, point, degrees)
void f_0052E6D0(Ctx& c);  // the CRT's trigonometric function of ST0 called by the portal rotation
void f_004CB270(Ctx& c);  // renders a view (view parms)
void f_004C97D0(Ctx& c);  // the viewer's orientation (src/render_backend.cpp)
void f_004AA490(Ctx& c);  // the dlights on a brush model (model)
void f_004DF2D0(Ctx& c);  // an entity's level-of-detail distance (ESI entity; ST0)
void f_00480DD0(Ctx& c);  // a model's level of detail at a distance (ECX model, distance; EAX)
void f_00427CB0(Ctx& c);  // a 3x3 matrix's inverse (EAX in, ECX out)
void f_004ABEF0(Ctx& c);  // an entity's lighting, once (EAX entity, ECX refdef)
void f_004ABDC0(Ctx& c);  // an entity's lighting, once, the other way (EDI entity, refdef)
void f_004DA9F0(Ctx& c);  // a surface's cache entry (EAX the entity's cache, ECX surface; index, entity, inverse; EAX)
void f_00440940(Ctx& c);  // va(format, ...)
void f_004AD8F0(Ctx& c);  // draws debug text (EAX origin, EDI colour, text)
void f_00477290(Ctx& c);  // a model's surface count (EDX model, EDI levels; EAX)
void f_00477340(Ctx& c);  // a model's surfaces (model, list, levels; ESI out)
void f_00476510(Ctx& c);  // (model, the surfaces' out)
void f_00506310(Ctx& c);  // (entity, out)
void f_005065E0(Ctx& c);  // (entity, out)
void f_00506070(Ctx& c);  // adds a model's surface (model, index; EDI shader, EDX surface)
void f_005068B0(Ctx& c);  // a debug draw of the cvar 0x15346D4 (EBX entity, EDI out)
void f_004AC890(Ctx& c);  // a debug draw of the cvar 0x15344D0
void f_0051D45A(Ctx& c);  // the CRT's fprintf
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
uint16_t U16(uint32_t va) { return Rd<uint16_t>(g_guest_base, va); }
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
// occluders of the cells walked (count, array of pointers; an occluder has
// its own counter at +0x18 and planes at +0x20, count +0x1C: those of its
// shadow, 0x4D7A70), the view count
// and the debug-draw cvars (cvar_t*, integer at +0x20).
constexpr uint32_t kPlaneBits = 0x11BEC20;
constexpr uint32_t kNearPlane = 0x11BEB7C, kFarPlane = 0x11BEB80;
constexpr uint32_t kNumOccluders = 0x11BEC10, kOccluders = 0x11BEC14;
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

// The occluders: one whose counter says so (`test`) is tested, its counter
// set to kNoBits; b is culled when it is behind every plane of one.
template <typename Test>
bool OccludersCull(uint32_t b, Test test) {
  for (int32_t k = 0; k < I32(kNumOccluders); k++) {
    const uint32_t o = U32(U32(kOccluders) + uint32_t(k) * 4);
    if (!test(I32(o + 0x18))) continue;
    Put(o + 0x18, uint32_t(kNoBits));
    const uint32_t q = U32(U32(kOccluders) + uint32_t(k) * 4);
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
// below the counter), the near/far planes and the occluders (those whose
// counter is above the plane bits), then adds it.
void f_004D8280(Ctx& c) {
  const uint32_t e = c.esp, ref = Arg(c, e, 1), planes = Arg(c, e, 3);
  const int32_t numPlanes = int32_t(c.eax), planeBits = int32_t(Arg(c, e, 4));
  if (U32(ref) != U32(kViewCount)) {
    const uint32_t surf = U32(ref + 8), b = surf + 0xC;
    if (!FrustumCulls(b, planes, numPlanes, planeBits < I32(kPlaneBits)) &&
        !OccludersCull(b, [&](int32_t bits) { return planeBits < bits; })) {
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
// plane of every tested occluder (near corners) adds all its surfaces, and
// raises the counters for what is under it; otherwise its children are
// visited, or a leaf's surfaces culled one by one, with plane bits + 1.
void f_004D8420(Ctx& c) {
  const uint32_t e = c.esp, node = Arg(c, e, 1), planes = Arg(c, e, 2);
  const int32_t numPlanes = int32_t(Arg(c, e, 3)), planeBits = int32_t(Arg(c, e, 5));
  if (FrustumCulls(node, planes, numPlanes, planeBits < I32(kPlaneBits)) ||
      OccludersCull(node, [&](int32_t bits) { return planeBits < bits; })) {
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
  for (int32_t k = 0; k < I32(kNumOccluders) && inside; k++) {
    const uint32_t o = U32(U32(kOccluders) + uint32_t(k) * 4);
    if (planeBits >= I32(o + 0x18)) continue;
    for (int32_t j = 0;; j++) {
      if (j >= I32(o + 0x1C)) {
        inside = false;
        break;
      }
      const uint32_t p = U32(o + 0x20) + uint32_t(j) * 0x14;
      if (Near(node, p, 2) + Near(node, p, 1) + Near(node, p, 0) > Dist(p)) {
        Put(o + 0x18, uint32_t(planeBits + 1));
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
      OccludersCull(model, [](int32_t bits) { return bits > 0; })) {
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
      OccludersCull(b, [](int32_t bits) { return bits > 0; })) {
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

// R_AddDrawSurf's store, also inlined: the key at index n, then the surface
// at the count read again, and the count incremented.
void AddDrawSurf(uint32_t n, uint32_t key, uint32_t surface) {
  Put(U32(kDrawSurfs) + n * 8, key);
  const uint32_t m = U32(kNumDrawSurfs);
  Put(U32(kDrawSurfs) + m * 8 + 4, surface);
  Put(kNumDrawSurfs, U32(kNumDrawSurfs) + 1);
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
    AddDrawSurf(n, key, Arg(c, e, 1));
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

// A plane's (cplane_t) far corner's offsets within a bounds, from its
// normal's signs as integers (-0 counts as 0).
void SignBits(uint32_t p) {
  for (uint32_t k = 0; k < 3; k++)
    Wr<uint8_t>(g_guest_base, p + 0x10 + k, uint8_t((I32(p + 4 * k) <= 0 ? 0 : 0xC) + 4 * k));
}

// A plane nudged 0.001 back, with its offsets.
void NudgePlane(uint32_t p) {
  Put(p + 0xC, Fst(St(Ld(p + 0xC)) - Ld(0x55B58C)));
  SignBits(p);
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


// ---- the front end: the world's static models and cells ------------------------

namespace {

// The static models (0x2B8 bytes each: type at +0, 0 an XModel through
// the table 0x15364F8 by its index at +8, 1 a model scaled by its axes;
// +0x2B4 nonzero while hidden from this view) and their cell references
// (32 bytes each: model, mins, maxs, next; a cell's list at +0x3C).
constexpr uint32_t kNumStaticModels = 0x153646C, kStaticModels = 0x1536470;
constexpr uint32_t kCellRefs = 0x11BEB88, kNumCellRefs = 0x11BEB8C;

// A static model's bounds b culled by the frustum (while the plane bits
// counter is above 0), the near and far planes and the occluders.
bool ModelCulled(uint32_t b, uint32_t planes, int32_t numPlanes) {
  return FrustumCulls(b, planes, numPlanes, 0 < I32(kPlaneBits)) ||
         OccludersCull(b, [](int32_t bits) { return 0 < bits; });
}

// A vector's squared length (x*x + y*y + z*z).
St Length2(uint32_t v) {
  const St x = Ld(v), y = Ld(v + 4), z = Ld(v + 8);
  return x * x + y * y + z * z;
}

}  // namespace

// A type 1 static model's bounds (ECX model; ESI mins, EDX maxs out): its
// model's (+0x90 -> +0x18 -> +4, bounds at +0x4C) scaled by the square
// root of its largest axis's (+0x1C, +0x28, +0x34) squared length, the
// lengths compared as stored but the first kept unrounded, about its
// origin (+0x44).
void f_004D8910(Ctx& c) {
  const uint32_t m = c.ecx, mins = c.esi, maxs = c.edx;
  St r = Length2(m + 0x1C);
  const F32 l0 = Fst(r), l1 = Fst(Length2(m + 0x28)), l2 = Fst(Length2(m + 0x34));
  if (St(l0) < l1) r = l1;
  if (r < l2) r = l2;
  r = Sqrt(r);
  const uint32_t b = U32(U32(U32(m + 0x90) + 0x18) + 4) + 0x4C;
  F32 v[6];
  for (uint32_t i = 0; i < 6; i++) v[i] = Ld(b + 4 * i);
  for (uint32_t i = 0; i < 3; i++) Put(mins + 4 * i, Fst(St(v[i]) * r + Ld(m + 0x44 + 4 * i)));
  for (uint32_t i = 0; i < 3; i++) Put(maxs + 4 * i, Fst(St(v[3 + i]) * r + Ld(m + 0x44 + 4 * i)));
  c.esp += 4;
}

// An XModel static model's bounds (EDI model; EAX maxs, mins on the
// stack): its model's bounds' (+0x4C) eight corners under its axes (+0x1C,
// +0x28, +0x34) about its origin (+0x44), each stored as floats and added
// to the bounds (from +-2^18) by 0x429070.
void f_004D8A20(Ctx& c) {
  const uint32_t e = c.esp, maxs = c.eax, m = c.edi, mins = Arg(c, e, 1), esi = c.esi;
  const uint32_t b = U32(U32(0x15364F8 + 4 * U32(m + 8)) + 0x4C), pt = e - 0x18;
  for (uint32_t i = 3; i--;) Put(mins + 4 * i, 0x48800000);
  for (uint32_t i = 3; i--;) Put(maxs + 4 * i, 0xC8800000);
  for (uint32_t k = 0; k < 8; k++) {
    const F32 x = Ld(b + 12 * (k & 1)), y = Ld(b + 12 * (k >> 1 & 1) + 4), z = Ld(b + 12 * (k >> 2 & 1) + 8);
    for (uint32_t i = 0; i < 3; i++) Put(pt + 4 * i, Fst(St(x) * Ld(m + 0x1C + 4 * i) + Ld(m + 0x44 + 4 * i)));
    for (uint32_t i = 0; i < 3; i++) Put(pt + 4 * i, Fst(St(y) * Ld(m + 0x28 + 4 * i) + Ld(pt + 4 * i)));
    for (uint32_t i = 0; i < 3; i++) Put(pt + 4 * i, Fst(St(z) * Ld(m + 0x34 + 4 * i) + Ld(pt + 4 * i)));
    c.esp = e - 0x24;
    c.ecx = pt;
    c.edx = mins;
    c.esi = maxs;
    Call(c, f_00429070, 0x4D8B3A);
  }
  c.esi = esi;
  c.esp = e + 4;
}

// A static model's (EBX) reference in a cell (ECX) with bounds EDX mins,
// EAX maxs: added to the cell's bounds when it has one ("^1Max xmodel refs
// (%i) exceeded" through ri.Printf at 0x1000), else a new one at the head
// of the cell's list.
void f_004D8B50(Ctx& c) {
  const uint32_t e = c.esp, cell = c.ecx, mins = c.edx, maxs = c.eax, model = c.ebx;
  const uint32_t n = U32(kNumCellRefs);
  if (n == 0x1000) {
    c.esp = e - 8;
    Push(c, n);
    Push(c, 0x5425C8);
    Push(c, 1);
    CallPtr(c, U32(kPrintfPtr), 0x4D8B72, 0x4D8B6C);
    c.esp = e + 4;
    return;
  }
  for (uint32_t r = U32(cell + 0x3C); r; r = U32(r + 0x1C))
    if (U32(r) == model) {
      const uint32_t esi = c.esi, edi = c.edi;
      c.esp = e - 0xC;
      c.ecx = r + 4;
      c.esi = r + 0x10;
      c.edi = maxs;
      Call(c, f_004290E0, 0x4D8BDB);
      c.esi = esi;
      c.edi = edi;
      c.esp = e + 4;
      return;
    }
  const uint32_t r = U32(kCellRefs) + (n << 5);
  Put(kNumCellRefs, n + 1);
  Put(r, model);
  for (uint32_t i = 0; i < 3; i++) Put(r + 4 + 4 * i, U32(mins + 4 * i));
  for (uint32_t i = 0; i < 3; i++) Put(r + 0x10 + 4 * i, U32(maxs + 4 * i));
  Put(r + 0x1C, U32(cell + 0x3C));
  Put(cell + 0x3C, r);
  c.esp = e + 4;
}

// A static model's bounds (stack: BSP node, model, mins, maxs) linked into
// the cells of the leaves it reaches (node +8: -2 for a node, else the
// leaf's cell, -1 none; plane +0xC, children +0x10, +0x14): EAX the one
// cell, or -2 when it reaches more than one. A box across an axial plane
// is split at it: the back with its maxs clamped, then the front with its
// mins clamped only when the plane is below its maxs.
void f_004D8BE0(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, model = Arg(c, e, 2), mins = Arg(c, e, 3), maxs = Arg(c, e, 4);
  const uint32_t s = e - 0x28, lo = e - 0x18, hi = e - 0xC;
  auto link = [&](uint32_t esp, uint32_t child, uint32_t mn, uint32_t mx, uint32_t ret) {
    c.esp = esp;
    Push(c, mx);
    Push(c, mn);
    Push(c, model);
    Push(c, child);
    Call(c, f_004D8BE0, ret);
    return c.eax;
  };
  uint32_t node = Arg(c, e, 1), result;
  for (;;) {
    if (I32(node + 8) != -2) {
      if (I32(node + 8) >= 0) {
        c.esp = s;
        c.ecx = U32(U32(kWorld) + 0x124) + (U32(node + 8) << 6);
        c.edx = mins;
        c.eax = maxs;
        c.ebx = model;
        Call(c, f_004D8B50, 0x4D8C41);
      }
      result = U32(node + 8);
      break;
    }
    c.esp = s;
    Push(c, U32(node + 0xC));
    Push(c, maxs);
    Push(c, mins);
    Call(c, f_00429800, 0x4D8C0B);
    if (c.eax != 3) {
      node = U32(node + 0xC + 4 * c.eax);
      continue;
    }
    uint32_t front, back;
    if (U8(U32(node + 0xC) + 0x10) >= 3) {
      front = link(s, U32(node + 0x10), mins, maxs, 0x4D8CF5);
      back = link(s - 0x10, U32(node + 0x14), mins, maxs, 0x4D8D07);
    } else {
      const uint32_t p = U32(node + 0xC);
      for (uint32_t i = 0; i < 3; i++) Put(lo + 4 * i, U32(mins + 4 * i));
      Put(lo + 4 * U8(p + 0x10), U32(p + 0xC));
      for (uint32_t i = 0; i < 3; i++) Put(hi + 4 * i, U32(maxs + 4 * i));
      Put(hi + 4 * U8(p + 0x10), U32(p + 0xC));
      back = link(s, U32(node + 0x14), mins, hi, 0x4D8CAB);
      const uint32_t axis = 4 * U8(U32(node + 0xC) + 0x10);
      if (!(St(Ld(lo + axis)) < Ld(maxs + axis))) {
        result = back;
        break;
      }
      front = link(s, U32(node + 0x10), lo, maxs, 0x4D8CDE);
    }
    result = front == back ? front : 0xFFFFFFFE;
    break;
  }
  c.ebx = ebx;
  c.eax = result;
  c.esp = e + 4;
}

// The static models linked into the world's cells (stack: frustum planes,
// count): the cells' (world +0x124, count +0x120, 0x40 bytes each) lists
// emptied, then each model of a type the cvars allow (XModels: 0x153470C
// and 0x1534420; type 1: 0x15343A0 and 0x1534424) hidden, its bounds
// culled, and, not culled, linked through the BSP (a type 1 model only
// while two of its sizes are at most 1536: else shown); shown when it
// reaches no cell. A debug box while 0x1534550 (XModels) or 0x15344B8 is 1.
void f_004D8D80(Ctx& c) {
  const uint32_t e = c.esp, planes = Arg(c, e, 1), esi = c.esi, edi = c.edi;
  const int32_t numPlanes = int32_t(Arg(c, e, 2));
  const uint32_t s = e - 0x38, mins = e - 0x18, maxs = e - 0xC;
  const bool xmodels = CvarInt(0x153470C) && CvarInt(0x1534420);
  const bool scaled = CvarInt(0x15343A0) && CvarInt(0x1534424);
  for (int32_t i = 0; i < I32(U32(kWorld) + 0x120); i++) Put(U32(U32(kWorld) + 0x124) + 0x40 * uint32_t(i) + 0x3C, 0);
  Put(kNumCellRefs, 0);
  auto link = [&](uint32_t model, uint32_t ret) {
    c.esp = s;
    Push(c, maxs);
    Push(c, mins);
    Push(c, model);
    Push(c, U32(U32(kWorld) + 0x98));
    Call(c, f_004D8BE0, ret);
    return c.eax;
  };
  for (int32_t i = 0; i < I32(kNumStaticModels); i++) {
    const uint32_t at = 0x2B8 * uint32_t(i);
    Put(U32(kStaticModels) + at + 0x2B4, 0);
    const uint32_t type = U32(U32(kStaticModels) + at);
    uint32_t debug;
    if (type == 1) {
      if (!scaled) continue;
      Put(U32(kStaticModels) + at + 0x2B4, 2);
      c.esp = s;
      c.ecx = U32(kStaticModels) + at;
      c.edx = maxs;
      c.esi = mins;
      Call(c, f_004D8910, 0x4D8E6F);
      if (ModelCulled(mins, planes, numPlanes)) continue;
      const F32 limit = Ld(0x55B5D8);
      auto small = [&](uint32_t k) { return !(St(Ld(maxs + 4 * k)) - Ld(mins + 4 * k) > limit); };
      const bool x = small(0), y = small(1);
      if (x == y ? !x : !small(2)) Put(U32(kStaticModels) + at + 0x2B4, 0);
      else if (link(U32(kStaticModels) + at, 0x4D904C) == 0xFFFFFFFF) Put(U32(kStaticModels) + at + 0x2B4, 0);
      debug = 0x15344B8;
    } else if (type == 0) {
      if (!xmodels) continue;
      Put(U32(kStaticModels) + at + 0x2B4, 2);
      c.esp = s;
      Push(c, mins);
      c.eax = maxs;
      c.edi = U32(kStaticModels) + at;
      Call(c, f_004D8A20, 0x4D90A0);
      if (ModelCulled(mins, planes, numPlanes)) continue;
      if (link(U32(kStaticModels) + at, 0x4D9229) == 0xFFFFFFFF) Put(U32(kStaticModels) + at + 0x2B4, 0);
      debug = 0x1534550;
    } else {
      continue;
    }
    if (CvarInt(debug) == 1) {
      c.esp = s;
      DrawBounds(c, mins, maxs, 0x537890, 0x4D925B);
    }
  }
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// A cell's (EAX) static model references (+0x3C) against the frustum
// (stack: planes, count): each hidden model whose reference's bounds are
// not culled is shown.
void f_004D8770(Ctx& c) {
  const uint32_t e = c.esp, planes = Arg(c, e, 1);
  const int32_t numPlanes = int32_t(Arg(c, e, 2));
  for (uint32_t r = U32(c.eax + 0x3C); r; r = U32(r + 0x1C))
    if (U32(U32(r) + 0x2B4) && !ModelCulled(r + 4, planes, numPlanes)) Put(U32(r) + 0x2B4, 0);
  c.esp = e + 4;
}

// A cell's (ESI) world surfaces (stack: planes, count, dlight bits): its
// surface AABB trees (+0x18 -> +0x20, count +0x24) or, without, its
// surface references (+0x18, count +0x1C), culled from plane bits 0; then
// its static models while the cvar 0x15343D4 is set.
void f_004D9290(Ctx& c) {
  const uint32_t e = c.esp, cell = c.esi, planes = Arg(c, e, 1), numPlanes = Arg(c, e, 2), dl = Arg(c, e, 3);
  Put(kPlaneBits, uint32_t(kNoBits));
  if (U32(U32(cell + 0x18) + 0x24)) {
    for (int32_t i = 0; i < I32(U32(cell + 0x18) + 0x24); i++) {
      c.esp = e - 0xC;
      Push(c, 0);
      Push(c, dl);
      Push(c, numPlanes);
      Push(c, planes);
      Push(c, U32(U32(cell + 0x18) + 0x20) + 0x28 * uint32_t(i));
      Call(c, f_004D8420, 0x4D92C9);
    }
  } else {
    for (int32_t i = 0; i < I32(U32(cell + 0x18) + 0x1C); i++) {
      c.esp = e - 0xC;
      Push(c, 0);
      Push(c, planes);
      Push(c, dl);
      Push(c, U32(U32(cell + 0x18) + 0x18) + 0xC * uint32_t(i));
      c.eax = numPlanes;
      Call(c, f_004D8280, 0x4D92FE);
    }
  }
  if (CvarInt(0x15343D4)) {
    c.esp = e - 0xC;
    Push(c, numPlanes);
    Push(c, planes);
    c.eax = cell;
    Call(c, f_004D8770, 0x4D9329);
  }
  c.esp = e + 4;
}

// The world's surfaces beyond the far plane (world +0xA8, count +0xA4),
// with one: the far plane dropped, each not yet added this view whose
// bounds are wholly behind it culled by the frustum alone.
void f_004D9D80(Ctx& c) {
  const uint32_t e = c.esp, far = U32(kFarPlane);
  if (far) {
    Put(kFarPlane, 0);
    for (int32_t i = 0; i < I32(U32(kWorld) + 0xA4); i++) {
      const uint32_t ref = U32(U32(U32(kWorld) + 0xA8) + 4 * uint32_t(i));
      if (U32(ref) == U32(kViewCount) || !Behind(FarZYX(U32(ref + 8) + 0xC, far), far)) continue;
      c.esp = e - 0xC;
      Push(c, 0);
      Push(c, kCullFrustum);
      Push(c, 0);
      Push(c, ref);
      c.eax = 4;
      Call(c, f_004D8280, 0x4D9E08);
    }
  }
  c.esp = e + 4;
}

// The world's view (unless 0x564550 is clear and the cvar 0x153450C set;
// 0x564550 cleared): the frustum (0x4D6A10), the origin, the near plane
// (the view's forward axis 0x153603C through the origin, 0.1 + 0.001 on;
// a portal's 0x1536144 instead while 0x1536138), the far plane while the
// far distance is above 0 (the axis reversed, 0.001 inside it), and the
// view-projection matrix (0x4D6AB0) while the cvar 0x153452C is above 0.
void f_004D9820(Ctx& c) {
  const uint32_t e = c.esp;
  if (!U32(0x564550) && CvarInt(0x153450C)) {
    c.esp = e + 4;
    return;
  }
  Put(0x564550, 0);
  Call(c, f_004D6A10, 0x4D9848);
  constexpr uint32_t kOrigin = 0x1536030, kAxis = 0x153603C, kNear = 0x11BEB54, kFar = 0x11BEB68;
  for (uint32_t i = 0; i < 3; i++) Put(kCullOrigin + 4 * i, U32(kOrigin + 4 * i));
  for (uint32_t i = 0; i < 3; i++) Put(kNear + 4 * i, U32(kAxis + 4 * i));
  SignBits(kNear);
  Put(kNear + 0xC, Fst(St(Ld(kNear + 8)) * Ld(kOrigin + 8) + St(Ld(kNear + 4)) * Ld(kOrigin + 4) +
                       St(Ld(kNear)) * Ld(kOrigin) - Ld(0x55B71C) - Ld(0x55B58C)));
  Put(kNearPlane, U32(0x1536138) ? 0x1536144 : kNear);
  Call(c, f_004D6880, 0x4D991F);
  const St dist(std::bit_cast<double>(x87::StoreF64(c.fpu)));
  x87::Pop(c.fpu);
  if (dist > Ld(0x55B584)) {
    Put(kFarPlane, kFar);
    Put(kFar, Fst(St(Ld(kAxis)) * Ld(0x55B5BC)));
    Put(kFar + 4, Fst(St(Ld(kAxis + 4)) * Ld(0x55B5BC)));
    const St z = St(Ld(kAxis + 8)) * Ld(0x55B5BC);
    Put(kFar + 8, Fst(z));
    SignBits(kFar);
    Put(kFar + 0xC, Fst(z * Ld(kOrigin + 8) + St(Ld(kFar + 4)) * Ld(kOrigin + 4) + St(Ld(kFar)) * Ld(kOrigin) -
                        dist - Ld(0x55B58C)));
  } else {
    Put(kFarPlane, 0);
  }
  if (St(Ld(U32(0x153452C) + 0x1C)) > Ld(0x55B584)) return f_004D6AB0(c);
  c.esp = e + 4;
}


// ---- the front end: the cell walk ------------------------------------------------

namespace {

// The cull's planes (count, pointer: the walk's buffer on its stack) and
// the refimport's temporary allocation (size; EAX the block) and its free.
constexpr uint32_t kNumCullPlanes = 0x11BEC18, kCullPlanes = 0x11BEC1C;
constexpr uint32_t kAllocPtr = 0x1549610, kFreePtr = 0x154961C;

// ri.Error(ERR_DROP, msg) from the call site va (returning to ret) at ESP s.
void DropError(Ctx& c, uint32_t s, uint32_t msg, uint32_t ret, uint32_t va) {
  c.esp = s;
  Push(c, msg);
  Push(c, 1);
  CallPtr(c, U32(0x1549604), ret, va);
}

// size bytes from the refimport's temporary allocation, called from va
// (returning to ret) at ESP s.
uint32_t TempAlloc(Ctx& c, uint32_t s, uint32_t size, uint32_t ret, uint32_t va) {
  c.esp = s;
  Push(c, size);
  CallPtr(c, U32(kAllocPtr), ret, va);
  c.esp = s;
  return c.eax;
}

// The cull's flag of debug draws (the static models' bounds cvar).
uint32_t DrawFlags() { return U32(U32(kDrawModelBounds) + 0x20); }

}  // namespace

// Clips a polygon (stack: points, &count, out; 12 bytes each) by the plane
// ECX: each point's distance (z + x + y, less the plane's), behind below
// -0.001, in front above 0.001, else on it. With none in front the count
// is 0 and EAX 0; with none behind EAX the points; else EAX out, which
// gets the points on and in front and where each edge between front and
// behind crosses the plane (stopping at 0x400 points after a crossing).
void f_004D7F30(Ctx& c) {
  const uint32_t e = c.esp, plane = c.ecx, pts = Arg(c, e, 1), countp = Arg(c, e, 2), out = Arg(c, e, 3);
  c.esp = e + 4;
  const int32_t n = I32(countp);
  if (n <= 0) {
    Put(countp, 0);
    c.eax = 0;
    return;
  }
  std::vector<int32_t> sides(uint32_t(n) + 1);
  std::vector<F32> dists(uint32_t(n) + 1);
  int32_t front = 0, back = 0;
  for (int32_t i = 0; i < n; i++) {
    const uint32_t p = pts + 0xC * uint32_t(i);
    const St d = St(Ld(p + 8)) * Ld(plane + 8) + St(Ld(p)) * Ld(plane) + St(Ld(plane + 4)) * Ld(p + 4) -
                 Ld(plane + 0xC);
    dists[i] = Fst(d);
    sides[i] = 2;
    if (d < Ld(0x55B5DC)) {
      sides[i] = 1;
      back++;
    } else if (d > Ld(0x55B58C)) {
      sides[i] = 0;
      front++;
    }
  }
  if (!front) {
    Put(countp, 0);
    c.eax = 0;
    return;
  }
  c.eax = pts;
  if (!back) return;
  sides[n] = sides[0];
  dists[n] = dists[0];
  int32_t m = 0;
  uint32_t o = out;
  for (int32_t i = 0; i < n; i++) {
    const uint32_t p = pts + 0xC * uint32_t(i);
    if (sides[i] != 1) {
      MovsD(o, p, 3);
      m++;
      o += 0xC;
      if (sides[i] == 2) continue;
    }
    if (sides[i + 1] == 2 || sides[i + 1] == sides[i]) continue;
    const St t = dists[i] / (St(dists[i]) - dists[i + 1]);
    const uint32_t q = pts + 0xC * uint32_t((i + 1) % n);
    m++;
    for (uint32_t k = 0; k < 12; k += 4) Put(o + k, Fst((St(Ld(q + k)) - Ld(p + k)) * t + Ld(p + k)));
    o += 0xC;
    if (m == 0x400) break;
  }
  Put(countp, uint32_t(m));
  c.eax = out;
}

// A portal's cull planes (stack: &count, n, points; the fourth argument
// unused) in a temporary allocation, EAX: with the cvar 0x153452C above 0,
// the points projected by the view-projection matrix (the groups of four,
// then the rest, each summing in its own order), the screen rectangle
// around them (its min and max from 1 and -1) back through the inverse to
// four corners, and the planes through the view origin and each side
// unless the normal of one of the polygon's edge planes is more than the
// cvar along it; then the planes through the view origin and each of the
// polygon's edges with a normal; all normalised (0x4268B0, on the stack
// for the edges), 0.001 back, the count to *count. The corners are on the
// stack for the debug lines (the cvar 0x153443C; 2 the rejected sides).
void f_004D6BC0(Ctx& c) {
  const uint32_t e = c.esp, countp = Arg(c, e, 1), pts = Arg(c, e, 3);
  const int32_t n = I32(e + 8);
  const uint32_t ebx = c.ebx, esi = c.esi, edi = c.edi, ebp = c.ebp;
  const uint32_t s = e - 0x609C, corners = s + 0x54, normals = s + 0x309C;
  auto aperture = [] { return St(Ld(U32(0x153452C) + 0x1C)); };
  uint32_t planes;
  if (aperture() > Ld(0x55B584)) {
    planes = TempAlloc(c, s, (uint32_t(n) * 5 + 0x14) * 4, 0x4D6BFC, 0x4D6BF6);
    F32 minX{0x3F800000}, minY{0x3F800000}, maxX{0xBF800000}, maxY{0xBF800000};
    auto project = [&](uint32_t p, bool rest) {
      const F32 x = Ld(p), y = Ld(p + 4), z = Ld(p + 8);
      auto row = [&](uint32_t r) {
        return rest ? St(Ld(r + 8)) * z + St(Ld(r)) * x + St(Ld(r + 4)) * y
                    : St(Ld(r + 4)) * y + St(Ld(r + 8)) * z + St(Ld(r)) * x;
      };
      const F32 inv = Fst(St(Ld(0x55B580)) / Fst(row(kViewProj + 0x30) + Ld(kViewProj + 0x3C)));
      const F32 px = Fst((row(kViewProj) + Ld(kViewProj + 0xC)) * inv);
      const F32 py = Fst((row(kViewProj + 0x10) + Ld(kViewProj + 0x1C)) * inv);
      if (St(minX) > px) minX = px;
      if (St(maxX) < px) maxX = px;
      if (St(minY) > py) minY = py;
      if (St(maxY) < py) maxY = py;
    };
    for (int32_t i = 0; i < n; i++) project(pts + 0xC * uint32_t(i), i >= n / 4 * 4);
    for (uint32_t k = 0; k < 4; k++) {
      const F32 cx = k >= 2 ? maxX : minX, cy = k == 1 || k == 2 ? minY : maxY;
      F32 v[4];
      for (uint32_t j = 0; j < 4; j++)
        v[j] = Fst(St(Ld(kViewProjInverse + 0x10 + 4 * j)) * cy + St(Ld(kViewProjInverse + 4 * j)) * cx +
                   Ld(kViewProjInverse + 0x30 + 4 * j));
      const F32 inv = Fst(St(Ld(0x55B580)) / v[3]);
      for (uint32_t j = 0; j < 3; j++) Put(corners + 0xC * k + 4 * j, Fst(St(v[j]) * inv));
    }
    MovsD(corners + 0x30, corners, 3);
  } else {
    planes = TempAlloc(c, s, uint32_t(n) * 0x14, 0x4D720B, 0x4D7205);
  }
  // The points from the view origin, the first again at the end.
  std::vector<F32> rel(3 * (n > 0 ? uint32_t(n) + 1 : 1));
  for (int32_t i = 0; i < n; i++)
    for (uint32_t k = 0; k < 3; k++) rel[3 * i + k] = Fst(St(Ld(pts + 0xC * uint32_t(i) + 4 * k)) - Ld(kCullOrigin + 4 * k));
  if (n > 0)
    for (uint32_t k = 0; k < 3; k++) rel[3 * uint32_t(n) + k] = rel[k];
  for (int32_t i = 0; i < n; i++) {
    const F32* r0 = &rel[3 * i];
    const F32* r1 = r0 + 3;
    const uint32_t nrm = normals + 0xC * uint32_t(i);
    Put(nrm, Fst(St(r1[1]) * r0[2] - St(r1[2]) * r0[1]));
    Put(nrm + 4, Fst(St(r1[2]) * r0[0] - St(r0[2]) * r1[0]));
    Put(nrm + 8, Fst(St(r0[1]) * r1[0] - St(r1[1]) * r0[0]));
    c.esp = s;
    c.eax = nrm;
    Call(c, f_004268B0, 0x4D7326);
  }
  uint32_t m = 0;
  if (aperture() > Ld(0x55B584)) {
    F32 cr[15];
    for (uint32_t k = 0; k < 12; k++) cr[k] = Fst(St(Ld(corners + 4 * k)) - Ld(kCullOrigin + 4 * (k % 3)));
    for (uint32_t k = 0; k < 3; k++) cr[12 + k] = cr[k];
    for (uint32_t k = 0; k < 4; k++) {
      const F32* r0 = &cr[3 * k];
      const F32* r1 = r0 + 3;
      const uint32_t p = planes + 0x14 * m;
      Put(p, Fst(St(r1[1]) * r0[2] - St(r0[1]) * r1[2]));
      Put(p + 4, Fst(St(r0[0]) * r1[2] - St(r1[0]) * r0[2]));
      Put(p + 8, Fst(St(r0[1]) * r1[0] - St(r0[0]) * r1[1]));
      c.esp = s;
      c.eax = p;
      Call(c, f_004268B0, 0x4D7499);
      bool reject = false;
      for (int32_t j = 0; j < n && !reject; j++) {
        const uint32_t q = normals + 0xC * uint32_t(j);
        reject = St(Ld(q)) * Ld(p) + St(Ld(q + 8)) * Ld(p + 8) + St(Ld(q + 4)) * Ld(p + 4) > aperture();
      }
      c.ebx = corners + 0xC * k;
      c.edi = c.ebx + 0xC;
      if (reject) {
        if (DrawFlags() & 2) {
          c.esi = 0x5378B0;
          Call(c, f_004AD760, 0x4D79F8);
        }
        continue;
      }
      if (DrawFlags()) {
        c.esi = 0x5378A0;
        Call(c, f_004AD760, 0x4D74F8);
      }
      Put(p + 0xC, Fst(St(Ld(kCullOrigin + 4)) * Ld(p + 4) + St(Ld(kCullOrigin + 8)) * Ld(p + 8) +
                       St(Ld(kCullOrigin)) * Ld(p)));
      NudgePlane(p);
      m++;
    }
  }
  // The edges' planes: in the groups of four the first sums z + x + y, the
  // others and the rest z + y + x.
  const int32_t groups = n / 4 * 4;
  for (int32_t i = 0; i < n; i++) {
    const uint32_t q = normals + 0xC * uint32_t(i);
    const F32 x = Ld(q), y = Ld(q + 4), z = Ld(q + 8);
    if (St(y) * y + St(z) * z + St(x) * x == Ld(0x55B584)) continue;
    const uint32_t p = planes + 0x14 * m;
    MovsD(p, q, 3);
    const F32 ox = Ld(kCullOrigin), oy = Ld(kCullOrigin + 4), oz = Ld(kCullOrigin + 8);
    St d;
    if (i >= groups) d = St(y) * oy + St(oz) * z + St(x) * ox;
    else if (i % 4 == 0) d = St(z) * oz + St(x) * ox + St(y) * oy;
    else d = St(z) * oz + St(y) * oy + St(x) * ox;
    Put(p + 0xC, Fst(d));
    NudgePlane(p);
    m++;
  }
  Put(countp, m);
  c.eax = planes;
  c.ebx = ebx;
  c.esi = esi;
  c.edi = edi;
  c.ebp = ebp;
  c.esp = e + 4;
}

// A portal's cull planes through it (stack: portal, the plane it is seen
// through, planes; ECX &count, EDX the plane count), EAX a temporary
// allocation or 0 (count 0): its polygon (+0x18, count +0x1C) clipped by
// that plane; with the view origin in front of the portal's plane (+0) or
// less than 1 behind it a copy of the planes as they are; else the
// polygon clipped by the far plane and the planes too, drawn for the
// debug cvar, and its planes (0x4D6BC0, the fourth argument the portal's
// +0x14). The clips alternate between two buffers on the stack.
void f_004D80F0(Ctx& c) {
  const uint32_t e = c.esp, portal = Arg(c, e, 1), planes = Arg(c, e, 3), countp = c.ecx;
  const int32_t numPlanes = int32_t(c.edx);
  const uint32_t ebx = c.ebx, esi = c.esi, edi = c.edi, ebp = c.ebp;
  const uint32_t s = e - 0x6014, buf = e - 0x6000;
  auto done = [&](uint32_t eax) {
    c.eax = eax;
    c.ebx = ebx;
    c.esi = esi;
    c.edi = edi;
    c.ebp = ebp;
    c.esp = e + 4;
  };
  auto clip = [&](uint32_t plane, uint32_t pts, uint32_t out, uint32_t ret) {
    c.esp = s;
    Push(c, out);
    Push(c, countp);
    Push(c, pts);
    c.ecx = plane;
    Call(c, f_004D7F30, ret);
    c.esp = s;
    return c.eax;
  };
  auto other = [&](uint32_t pts) { return pts == buf ? buf + 0x3000 : buf; };
  Put(countp, U32(portal + 0x1C));
  uint32_t pts = clip(Arg(c, e, 2), U32(portal + 0x18), buf, 0x4D8124);
  if (!U32(countp)) return done(0);
  if (Ld(portal + 0xC) - Dot(kCullOrigin, portal) < Ld(0x55B580)) {
    Put(countp, uint32_t(numPlanes));
    const uint32_t size = uint32_t(numPlanes) * 0x14, copy = TempAlloc(c, s, size, 0x4D8170, 0x4D816A);
    MovsD(copy, planes, size / 4);
    return done(copy);
  }
  if (U32(kFarPlane)) {
    pts = clip(U32(kFarPlane), pts, other(pts), 0x4D81C0);
    if (!U32(countp)) return done(0);
  }
  for (int32_t i = 0; i < numPlanes; i++) {
    pts = clip(planes + 0x14 * uint32_t(i), pts, other(pts), 0x4D8203);
    if (!U32(countp)) return done(0);
  }
  if (DrawFlags()) {
    Push(c, pts);
    c.ebx = U32(countp);
    c.eax = 0x536548;
    Call(c, f_004AD630, 0x4D823F);
    c.esp = s;
  }
  Push(c, U32(portal + 0x14));
  Push(c, pts);
  Push(c, U32(countp));
  Push(c, countp);
  Call(c, f_004D6BC0, 0x4D8257);
  done(c.eax);
}

// A cell's occluders (stack: cell, planes, count; +0x2C, count +0x30) onto
// the cull's list (cleared, as are the cull's planes): each with
// a point in front of the near plane and, for each of the planes still
// given, in front of it (the planes given are used up, the argument slots
// advanced, by the first occluders tested), its counter set to kNoBits and
// its planes (+0x20, count +0x1C) the cull's next: its faces (+0, count +4,
// marked at +0x13) the view origin is in front of, then for each edge (+0xC,
// count +8: two faces, two points) between one of those and one not, the
// plane through the view origin and its points (normalised, 0.001 back).
// ri.Error at 0x400 occluders and 0x1800 faces.
void f_004D7A70(Ctx& c) {
  const uint32_t e = c.esp, cell = Arg(c, e, 1);
  Put(kNumOccluders, 0);
  Put(kNumCullPlanes, 0);
  auto front = [](uint32_t o, auto dist) {
    for (int32_t k = 0; k < I32(o + 0x10); k++)
      if (dist(U32(o + 0x14) + 0xC * uint32_t(k))) return true;
    return false;
  };
  for (int32_t i = 0; i < I32(cell + 0x30); i++) {
    const uint32_t o = U32(U32(cell + 0x2C) + 4 * uint32_t(i));
    if (!front(o, [](uint32_t pt) {
          const uint32_t p = U32(kNearPlane);
          return St(Ld(pt + 4)) * Ld(p + 4) + St(Ld(pt + 8)) * Ld(p + 8) + St(Ld(p)) * Ld(pt) > Ld(p + 0xC);
        }))
      continue;
    bool hidden = false;
    while (U32(e + 0xC) && !hidden) {
      const uint32_t p = U32(e + 8);
      hidden = !front(o, [&](uint32_t pt) {
        return St(Ld(pt + 8)) * Ld(p + 8) + St(Ld(pt + 4)) * Ld(p + 4) + St(Ld(p)) * Ld(pt) > Ld(p + 0xC);
      });
      if (!hidden) {
        Put(e + 0xC, U32(e + 0xC) - 1);
        Put(e + 8, p + 0x14);
      }
    }
    if (hidden) continue;
    if (I32(kNumOccluders) == 0x400) DropError(c, e - 0x30, 0x542628, 0x4D7B4F, 0x4D7B49);
    Put(U32(kOccluders) + 4 * U32(kNumOccluders), o);
    Put(kNumOccluders, U32(kNumOccluders) + 1);
    Put(o + 0x18, uint32_t(kNoBits));
    Put(o + 0x1C, U32(kNumCullPlanes));
    Put(o + 0x20, U32(kCullPlanes) + 0x14 * U32(kNumCullPlanes));
    for (int32_t j = 0; j < I32(o + 4); j++) {
      const uint32_t f = U32(o) + 0x14 * uint32_t(j);
      Wr<uint8_t>(g_guest_base, f + 0x13, 0);
      if (!(Dot(kCullOrigin, f) > Ld(f + 0xC))) continue;
      Wr<uint8_t>(g_guest_base, f + 0x13, 1);
      if (U32(kNumCullPlanes) == 0x1800) DropError(c, e - 0x30, 0x542608, 0x4D7BF4, 0x4D7BEE);
      MovsD(U32(kCullPlanes) + 0x14 * U32(kNumCullPlanes), U32(o) + 0x14 * uint32_t(j), 5);
      Put(kNumCullPlanes, U32(kNumCullPlanes) + 1);
    }
    for (int32_t j = 0; j < I32(o + 8); j++) {
      const uint32_t edge = U32(o + 0xC) + 0x10 * uint32_t(j);
      const uint8_t side = U8(U32(edge) + 0x13);
      if (side == U8(U32(edge + 4) + 0x13)) continue;
      const uint32_t p0 = U32(edge + 8 + 4 * (side == 0)), p1 = U32(edge + 8 + 4 * side);
      F32 d0[3], d1[3];
      for (uint32_t k = 0; k < 3; k++) {
        d0[k] = Fst(St(Ld(p0 + 4 * k)) - Ld(kCullOrigin + 4 * k));
        d1[k] = Fst(St(Ld(p1 + 4 * k)) - Ld(kCullOrigin + 4 * k));
      }
      const uint32_t p = U32(kCullPlanes) + 0x14 * U32(kNumCullPlanes);
      Put(p, Fst(St(d1[1]) * d0[2] - St(d1[2]) * d0[1]));
      Put(p + 4, Fst(St(d1[2]) * d0[0] - St(d1[0]) * d0[2]));
      Put(p + 8, Fst(St(d1[0]) * d0[1] - St(d1[1]) * d0[0]));
      c.esp = e - 0x30;
      c.eax = p;
      Call(c, f_004268B0, 0x4D7D1E);
      const uint32_t q = U32(kCullPlanes) + 0x14 * U32(kNumCullPlanes);
      Put(q + 0xC, Fst(Dot(kCullOrigin, q)));
      NudgePlane(q);
      Put(kNumCullPlanes, U32(kNumCullPlanes) + 1);
    }
    Put(o + 0x1C, U32(kNumCullPlanes) - U32(o + 0x1C));
  }
  c.esp = e + 4;
}

// R_RecursiveWorldNode's like for cells (stack: cell, the portal plane it
// is seen through, planes, plane count, dlight bits): its occluders
// (0x4D7A70), surfaces (0x4D9290) and static models and entities
// (0x4D9650); then each of its portals (+0x1C, 0x24 bytes each, count
// +0x20, counted down in the cell's argument slot) not already on the
// walk (+0x20), with the view origin not in front of its plane (+0), not
// wholly behind a plane (0x4D7E70) or hidden by an occluder (0x4D7ED0),
// and with planes through it (0x4D80F0): the cell beyond it (+0x14),
// walked with those planes, which are freed after.
void f_004D96E0(Ctx& c) {
  const uint32_t e = c.esp, cell = Arg(c, e, 1), planes = Arg(c, e, 3), numPlanes = Arg(c, e, 4);
  const uint32_t bits = Arg(c, e, 5);
  const uint32_t ebx = c.ebx, esi = c.esi, edi = c.edi, ebp = c.ebp;
  const uint32_t s = e - 0x14;
  c.esp = s;
  Push(c, numPlanes);
  Push(c, planes);
  Push(c, cell);
  Call(c, f_004D7A70, 0x4D96F9);
  Push(c, bits);
  Push(c, numPlanes);
  Push(c, planes);
  c.esi = cell;
  Call(c, f_004D9290, 0x4D9707);
  Push(c, planes);
  Push(c, cell);
  c.ebx = numPlanes;
  c.edi = bits;
  Call(c, f_004D9650, 0x4D9714);
  c.esp = s;
  uint32_t portal = U32(cell + 0x1C);
  if (const uint32_t n = U32(cell + 0x20)) {
    Put(e + 4, n);
    do {
      if (!U32(portal + 0x20) && !(Dot(kCullOrigin, portal) > Ld(portal + 0xC))) {
        c.eax = portal;
        c.ecx = numPlanes;
        c.edx = planes;
        Call(c, f_004D7E70, 0x4D977A);
        bool hidden = c.eax != 0;
        for (int32_t k = 0; !hidden && k < I32(kNumOccluders); k++) {
          const uint32_t o = U32(U32(kOccluders) + 4 * uint32_t(k));
          c.eax = portal;
          c.ecx = U32(o + 0x1C);
          c.edx = U32(o + 0x20);
          Call(c, f_004D7ED0, 0x4D97A5);
          hidden = c.eax != 0;
        }
        if (!hidden) {
          Push(c, planes);
          Push(c, Arg(c, e, 2));
          Push(c, portal);
          c.ecx = e - 4;
          c.edx = numPlanes;
          Call(c, f_004D80F0, 0x4D97C5);
          const uint32_t through = c.eax, count = U32(e - 4);
          c.esp = s;
          if (count) {
            Push(c, Arg(c, e, 5));
            Push(c, count);
            Push(c, through);
            Push(c, portal);
            Push(c, U32(portal + 0x14));
            Put(portal + 0x20, 1);
            Call(c, f_004D96E0, 0x4D97EA);
            Push(c, through);
            CallPtr(c, U32(kFreePtr), 0x4D97F1, 0x4D97EB);
            c.esp = s;
            Put(portal + 0x20, 0);
          }
        }
      }
      portal += 0x24;
      Put(e + 4, U32(e + 4) - 1);
    } while (U32(e + 4));
  }
  c.ebx = ebx;
  c.esi = esi;
  c.edi = edi;
  c.ebp = ebp;
  c.esp = e + 4;
}

// The world's coronas (+0xC0, 0x20 bytes each, count +0xC4: shader, origin,
// radius, two limits, colour, alpha; stack: planes, plane count): each
// whose direction to the view origin (0x1536330, normalised) has z below
// its first limit and whose cube of the radius x sqrt 3 is not culled
// (frustum while the plane bits counter is above 0, near and far planes,
// occluders), as a sprite 16 along that direction (a refentity on the
// stack, zeroed once, type 4) with the shader's handle (+0x44), the colour
// and the radius, its alpha faded by z between the limits; added by
// 0x4DEC10. The cookie check is left out.
void f_004D9A10(Ctx& c) {
  const uint32_t e = c.esp, planes = Arg(c, e, 1);
  const int32_t numPlanes = I32(e + 8);
  const uint32_t esi = c.esi;
  const uint32_t s = e - 0xE0, dir = s + 0x14, b = s + 0x24, ent = s + 0x40;
  for (uint32_t i = 0; i < 0x9C; i += 4) Put(ent + i, 0);
  Put(ent, 4);
  for (int32_t i = 0; i < I32(U32(kWorld) + 0xC4); i++) {
    const uint32_t f = U32(U32(kWorld) + 0xC0) + 0x20 * uint32_t(i);
    for (uint32_t k = 0; k < 12; k += 4) Put(dir + k, Fst(St(Ld(0x1536330 + k)) - Ld(f + 4 + k)));
    c.esp = s;
    c.esi = dir;
    Call(c, f_00426710, 0x4D9AB4);
    x87::Pop(c.fpu);
    if (St(Ld(dir + 8)) >= Ld(f + 0x14)) continue;
    const St r = St(Ld(f + 0x10)) * Ld(0x55B870);
    for (uint32_t k = 0; k < 12; k += 4) Put(b + k, Fst(St(Ld(f + 4 + k)) - r));
    for (uint32_t k = 0; k < 12; k += 4) Put(b + 0xC + k, Fst(r + Ld(f + 4 + k)));
    if (ModelCulled(b, planes, numPlanes)) continue;
    for (uint32_t k = 0; k < 12; k += 4) Put(ent + 0x44 + k, Fst(St(Ld(dir + k)) * Ld(0x55B5E4) + Ld(f + 4 + k)));
    Put(ent + 0x68, U32(U32(f) + 0x44));
    for (uint32_t k = 0; k < 3; k++) Wr<uint8_t>(g_guest_base, ent + 0x6C + k, U8(f + 0x1C + k));
    Put(ent + 0x64, Fst(St(Ld(f + 0x10))));
    Put(ent + 0x7C, Fst(St(Ld(f + 0x10))));
    uint8_t alpha = U8(f + 0x1F);
    if (St(Ld(dir + 8)) > Ld(f + 0x18))
      alpha = uint8_t(Ftol((St(Ld(f + 0x14)) - Ld(dir + 8)) * FromInt(alpha) / (St(Ld(f + 0x14)) - Ld(f + 0x18))));
    Wr<uint8_t>(g_guest_base, ent + 0x6F, alpha);
    Push(c, 0);
    Push(c, ent);
    Call(c, f_004DEC10, 0x4D9D21);
  }
  c.esi = esi;
  c.esp = e + 4;
}

// R_AddWorldSurfaces' like: with the cvar 0x1534490 set and a world
// (RDF_NOWORLDMODEL clear), the cull's occluders, planes and static model
// references in buffers on the stack, the dlight bits of tr.refdef's
// count, the world's view (0x4D9820), the static models' links (0x4D8D80)
// and coronas (0x4D9A10) by the frustum; then from the view's cell
// (0x4D7DF0) its surfaces and models alone with the far plane dropped
// (the cvar 0x1534510) or the cell walk (0x4D96E0, through the near
// plane); outside the world every cell's surfaces and models (the cvar
// 0x15346D8) or surfaces, with the world's models (+0x148, count +0x144,
// 0x24 bytes each); then the surfaces beyond the far plane (0x4D9D80)
// and, with a debug cvar at 2, the bounds of each cell's references to
// static models of its type (hidden ones in another colour).
void f_004D9E20(Ctx& c) {
  const uint32_t e = c.esp;
  if (!CvarInt(0x1534490) || (U8(0x1536364) & 1)) {
    c.esp = e + 4;
    return;
  }
  const uint32_t ebx = c.ebx, esi = c.esi, edi = c.edi, ebp = c.ebp;
  const uint32_t s = e - 0x3F014;
  Put(kOccluders, e - 0x3F000);
  const uint32_t bits = (1u << (U32(0x1536474) & 31)) - 1;
  Put(kCullPlanes, e - 0x3E000);
  Put(kCellRefs, e - 0x20000);
  Put(kNumOccluders, 0);
  Put(0x1536024, 0x7FE);
  Put(0x1536028, 0x3FF00);
  c.edi = bits;
  c.esp = s;
  Call(c, f_004D9820, 0x4D9E9D);
  c.esp = s;
  Push(c, 4);
  Push(c, kCullFrustum);
  Call(c, f_004D8D80, 0x4D9EA9);
  Push(c, 4);
  Push(c, kCullFrustum);
  Call(c, f_004D9A10, 0x4D9EB5);
  c.esp = s;
  Call(c, f_004D7DF0, 0x4D9EBD);
  const uint32_t world = U32(kWorld);
  auto surfaces = [&](uint32_t cell, uint32_t ret) {
    c.esp = s;
    Push(c, bits);
    Push(c, 4);
    Push(c, kCullFrustum);
    c.esi = cell;
    Call(c, f_004D9290, ret);
  };
  auto models = [&](uint32_t cell, uint32_t ret) {
    Push(c, kCullFrustum);
    Push(c, cell);
    c.ebx = 4;
    Call(c, f_004D9650, ret);
    c.esp = s;
  };
  if (int32_t(c.eax) >= 0) {
    const uint32_t cell = U32(world + 0x124) + (c.eax << 6);
    if (CvarInt(0x1534510)) {
      Put(kFarPlane, 0);
      surfaces(cell, 0x4D9EF2);
      models(cell, 0x4D9F02);
    } else {
      c.esp = s;
      Push(c, bits);
      Push(c, 4);
      Push(c, kCullFrustum);
      Push(c, 0x11BEB54);
      Push(c, cell);
      Call(c, f_004D96E0, 0x4D9F15);
    }
  } else if (CvarInt(0x15346D8)) {
    for (int32_t i = 0; i < I32(U32(kWorld) + 0x120); i++) {
      surfaces(U32(U32(kWorld) + 0x124) + 0x40 * uint32_t(i), 0x4D9F57);
      models(U32(U32(kWorld) + 0x124) + 0x40 * uint32_t(i), 0x4D9F75);
    }
  } else {
    for (int32_t i = 0; i < I32(U32(kWorld) + 0x120); i++) surfaces(U32(U32(kWorld) + 0x124) + 0x40 * uint32_t(i), 0x4D9FB5);
    for (int32_t i = 0; i < I32(U32(kWorld) + 0x144); i++) {
      c.esp = s;
      Push(c, bits);
      c.eax = 4;
      c.ecx = kCullFrustum;
      c.edx = U32(U32(kWorld) + 0x148) + 0x24 * uint32_t(i);
      Call(c, f_004D9330, 0x4D9FF8);
    }
  }
  c.esp = s;
  Call(c, f_004D9D80, 0x4DA013);
  if (CvarInt(0x15344B8) == 2 || CvarInt(0x1534550) == 2) {
    for (int32_t i = 0; i < I32(U32(kWorld) + 0x120); i++) {
      for (uint32_t r = U32(U32(U32(kWorld) + 0x124) + 0x40 * uint32_t(i) + 0x3C); r; r = U32(r + 0x1C)) {
        const uint32_t model = U32(r), type = U32(model);
        if ((type == 1 && CvarInt(0x15344B8) != 2) || (type == 0 && CvarInt(0x1534550) != 2)) continue;
        DrawBounds(c, r + 4, r + 0x10, U32(model + 0x2B4) ? 0x537810 : 0x537820, 0x4DA091);
      }
    }
  }
  c.ebx = ebx;
  c.esi = esi;
  c.edi = edi;
  c.ebp = ebp;
  c.esp = e + 4;
}

// ---- the front end: the view through a portal -------------------------------

// The orientations of a portal surface (ECX the entity; stack: the draw
// surface, the surface's orientation, the camera's, the PVS origin, the
// mirror flag; EAX 1 when found): the surface's plane (0x4CA170) into the
// world (for an entity other than the world's 0x7FE, its orientation into
// tr.or: 0x4C9580 for types 0-2, else the view's world copied; the normal
// by 0x4C90F0 and both planes' distances moved along tr.or's origin); the
// surface's axes from the normal (a perpendicular by 0x4273C0, their cross
// product); then the first portal entity (type 0xB) within 64 of the
// original plane. A mirror when its old origin (+0x54) is its origin
// (+0x44): the surface's origin the plane's foot, the camera the surface
// with its first axis negated. Else the surface's origin moved along the
// first axis onto the entity's origin, the camera at the old origin with
// the entity's axes (+0x1C), the first two negated and the second rotated
// about the first (0x426AB0) with an old frame (+0x60) by the time / 1000
// x the frame (+0x50), or without a frame by 0x52E6D0(time x 0.003) x 4 +
// +0x80; without an old frame by +0x80 when it is not 0; the third then
// their cross product (in each case's order). No portal entity: EAX 0.
// The draw surface's and the surface's argument slots take the original's
// temporaries; the cookie check is left out.
void f_004CA240(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, esi = c.esi, edi = c.edi, ebp = c.ebp, ent = c.ecx;
  const uint32_t surf = Arg(c, e, 2), cam = Arg(c, e, 3), pvs = Arg(c, e, 4), mirror = Arg(c, e, 5);
  const uint32_t s = e - 0x40, plane = s + 0x14, orig = s + 0x28, a0 = surf + 0xC, a1 = surf + 0x18;
  auto mul = [](uint32_t a, uint32_t b) { return St(Ld(a)) * Ld(b); };
  auto done = [&](uint32_t r) {
    c.eax = r;
    c.ebx = ebx;
    c.esi = esi;
    c.edi = edi;
    c.ebp = ebp;
    c.esp = e + 4;
  };
  c.esp = s;
  c.ebp = cam;
  c.ecx = U32(Arg(c, e, 1) + 4);
  c.esi = orig;
  c.edi = ent;
  Call(c, f_004CA170, 0x4CA26A);
  if (ent != 0x7FE) {
    Put(0x1536024, ent);
    const uint32_t ep = ent * 0x2B8 + U32(0x1536470);
    Put(0x1535D68, ep);
    if (U32(ep) <= 2) {
      c.eax = 0x153629C;
      c.ebx = 0x1536030;
      c.edi = ep;
      Call(c, f_004C9580, 0x4CA2C2);
    } else {
      MovsD(0x153629C, 0x15360AC, 0x1F);
    }
    c.ecx = plane;
    c.eax = orig;
    Call(c, f_004C90F0, 0x4CA2CF);
    Put(plane + 0xC, Fst(mul(0x15362A4, plane + 8) + mul(0x15362A0, plane + 4) + mul(0x153629C, plane) +
                         Ld(orig + 0xC)));
    Put(orig + 0xC, Fst(mul(0x15362A4, orig + 8) + mul(0x15362A0, orig + 4) + mul(0x153629C, orig) +
                        Ld(orig + 0xC)));
  } else {
    MovsD(plane, orig, 5);
  }
  MovsD(a0, plane, 3);
  c.eax = U32(plane + 8);
  c.ecx = U32(plane);
  c.ebx = a0;
  c.edx = a1;
  c.esi = a1;
  c.edi = surf;
  Call(c, f_004273C0, 0x4CA359);
  Put(surf + 0x24, Fst(mul(a0 + 4, a1 + 8) - mul(a1 + 4, a0 + 8)));
  Put(surf + 0x28, Fst(mul(a0 + 8, a1) - mul(a0, a1 + 8)));
  Put(surf + 0x2C, Fst(mul(a1 + 4, a0) - mul(a0 + 4, a1)));
  const int32_t n = I32(0x153646C);
  const uint32_t ents = U32(0x1536470);
  uint32_t p = 0;
  for (int32_t i = 0; i < n; i++) {
    const uint32_t q = ents + 0x2B8 * uint32_t(i);
    if (U32(q) != 0xB) continue;
    const F32 d = Fst(mul(orig + 8, q + 0x4C) + mul(orig, q + 0x44) + mul(orig + 4, q + 0x48) - Ld(orig + 0xC));
    Put(e + 4, d);
    if (St(d) > Ld(0x55B608) || St(d) < Ld(0x55B818)) continue;
    p = q;
    break;
  }
  if (!p) return done(0);
  MovsD(pvs, p + 0x54, 3);
  if (St(Ld(p + 0x54)) == Ld(p + 0x44) && St(Ld(p + 0x58)) == Ld(p + 0x48) &&
      St(Ld(p + 0x5C)) == Ld(p + 0x4C)) {
    const F32 x = Fst(mul(plane + 0xC, plane));
    Put(e + 4, x);
    Put(surf, x);
    Put(surf + 4, Fst(mul(plane + 0xC, plane + 4)));
    Put(surf + 8, Fst(mul(plane + 0xC, plane + 8)));
    MovsD(cam, surf, 3);
    for (uint32_t k = 0; k < 12; k += 4) Put(cam + 0xC + k, Fst(St(Ld(0x537E54 + k)) - Ld(a0 + k)));
    MovsD(cam + 0x18, surf + 0x18, 6);
    Put(mirror, 1u);
    return done(1);
  }
  const F32 d = Fst(mul(plane + 8, p + 0x4C) + mul(plane + 4, p + 0x48) + mul(plane, p + 0x44) - Ld(plane + 0xC));
  const F32 nd{d.bits ^ 0x80000000u};  // FCHS of the stored distance
  Put(e + 4, nd);
  for (uint32_t k = 0; k < 12; k += 4) Put(surf + k, Fst(St(nd) * Ld(a0 + k) + Ld(p + 0x44 + k)));
  MovsD(cam, p + 0x54, 3);
  MovsD(cam + 0xC, p + 0x1C, 9);
  for (uint32_t k = 0; k < 0x18; k += 4) Put(cam + 0xC + k, Fst(St(Ld(0x537E54 + k % 12)) - Ld(cam + 0xC + k)));
  Put(e + 8, U32(cam + 0x20));
  // Axis 0 (EDI) and axis 1 (EBX), the one rotated; the point it is
  // rotated from is a copy of it in the original's plane local.
  const uint32_t a = cam + 0xC, b = cam + 0x18, out = cam + 0x24, pt = plane;
  auto rotate = [&](F32 deg, uint32_t ret) {
    MovsD(pt, b, 3);
    c.esp = s;
    Push(c, deg.bits);
    Push(c, pt);
    Push(c, b);
    c.eax = a;
    c.ebx = b;
    c.edi = a;
    c.esi = p + 0x48;
    Call(c, f_00426AB0, ret);
    c.esp = s;
  };
  if (U32(p + 0x60)) {
    const F32 t = Fst(FromInt(I32(0x1536360)));
    Put(e + 4, t);
    if (const int32_t frame = I32(p + 0x50)) {
      const St q = St(t) / Ld(0x55B658);
      const F32 f = Fst(FromInt(frame));
      Put(e + 4, f);
      const F32 deg = Fst(q * f);
      Put(e + 4, deg);
      rotate(deg, 0x4CA636);
      Put(out, Fst(mul(b + 8, a + 4) - mul(b + 4, a + 8)));
      Put(out + 4, Fst(mul(b, a + 8) - mul(b + 8, a)));
      Put(out + 8, Fst(mul(a, b + 4) - mul(a + 4, b)));
    } else {
      x87::LoadF64(c.fpu, std::bit_cast<uint64_t>((St(t) * Ld(0x55B81C)).v));
      Call(c, f_0052E6D0, 0x4CA673);
      const F32 sn{x87::StoreF32(c.fpu)};
      x87::Pop(c.fpu);
      Put(e + 4, sn);
      const F32 deg = Fst(St(sn) * Ld(0x55B800) + Ld(p + 0x80));
      Put(e + 4, deg);
      rotate(deg, 0x4CA6AF);
      Put(out, Fst(mul(b + 8, a + 4) - mul(a + 8, b + 4)));
      Put(out + 4, Fst(mul(a + 8, b) - mul(a, b + 8)));
      Put(out + 8, Fst(mul(a, b + 4) - mul(b, a + 4)));
    }
  } else if (!(St(Ld(p + 0x80)) == Ld(0x55B584))) {
    rotate(Ld(p + 0x80), 0x4CA70D);
    Put(out, Fst(mul(a + 4, b + 8) - mul(b + 4, a + 8)));
    Put(out + 4, Fst(mul(a + 8, b) - mul(a, b + 8)));
    Put(out + 8, Fst(mul(b + 4, a) - mul(a + 4, b)));
  }
  Put(mirror, 0u);
  done(1);
}

// A portal or mirror surface's view (EBX the draw surface; stack: the
// entity; EAX 1 when drawn): none inside a portal's view (0x1536134: a
// warning), with the cvar 0x1534514 or 0x153457C set, for a surface off the
// screen (0x4CA8D0) or without a portal entity (0x4CA240); else the view
// (0x260 bytes at 0x1536030) saved and copied, the copy flagged a portal's
// (+0x104) with the mirror flag (+0x108) and PVS origin (+0xF8) 0x4CA240
// gives, its origin the view's mirrored through the surface to the camera
// (0x4C9FF0), its portal plane (+0x114) the camera's first axis negated
// through the camera's origin (nudged, 0x4D6830), its axes mirrored
// (0x4CA0C0), rendered (0x4CB270), and the view restored. The locals the
// callees are handed are at the original's (8-aligned) addresses; the
// cookie check is left out.
void f_004CAB80(Ctx& c) {
  const uint32_t e = c.esp, esi = c.esi, edi = c.edi, ebp = c.ebp, ds = c.ebx, ent = Arg(c, e, 1);
  const uint32_t s = (((e - 4) & ~7u) - 0xD28) - 8;
  const uint32_t cam = s + 8, surf = s + 0x38, parms = s + 0x68, old = s + 0x2C8, pl = parms + 0x114;
  auto done = [&](uint32_t r) {
    c.eax = r;
    c.esi = esi;
    c.edi = edi;
    c.ebp = ebp;
    c.esp = e + 4;
  };
  c.esp = s;
  c.ebp = e - 4;
  if (U32(0x1536134)) {
    Push(c, 0x5430C0);
    Push(c, 1);
    CallPtr(c, U32(kPrintfPtr), 0x4CABB3, 0x4CABAD);
    return done(0);
  }
  if (CvarInt(0x1534514) || CvarInt(0x153457C)) return done(0);
  Push(c, s + 0x528);
  Push(c, ds);
  Call(c, f_004CA8D0, 0x4CABF4);
  c.esp = s;
  if (c.eax) return done(0);
  MovsD(old, 0x1536030, 0x98);
  MovsD(parms, 0x1536030, 0x98);
  Push(c, parms + 0x108);
  Push(c, parms + 0xF8);
  Push(c, cam);
  Push(c, surf);
  Push(c, ds);
  Put(parms + 0x104, 1u);
  c.ecx = ent;
  Call(c, f_004CA240, 0x4CAC4C);
  c.esp = s;
  if (!c.eax) return done(0);
  c.eax = cam;
  c.ecx = surf;
  c.edx = parms;
  c.esi = old;
  Call(c, f_004C9FF0, 0x4CAC6F);
  for (uint32_t k = 0; k < 12; k += 4) Put(pl + k, Fst(St(Ld(0x537E54 + k)) - Ld(cam + 0xC + k)));
  Put(pl + 0xC, Fst(St(Ld(cam + 8)) * Ld(pl + 8) + St(Ld(cam + 4)) * Ld(pl + 4) + St(Ld(cam)) * Ld(pl)));
  c.eax = pl;
  Call(c, f_004D6830, 0x4CACDA);
  constexpr uint32_t kRet[3] = {0x4CACF2, 0x4CAD0D, 0x4CAD28};
  for (uint32_t i = 0; i < 3; i++) {
    c.eax = parms + 0xC + 0xC * i;
    c.ecx = cam;
    c.edx = surf;
    c.esi = old + 0xC + 0xC * i;
    Call(c, f_004CA0C0, kRet[i]);
  }
  Push(c, parms);
  Call(c, f_004CB270, 0x4CAD32);
  MovsD(0x1536030, old, 0x98);
  done(1);
}

// ---- the front end: the scene ----------------------------------------------

// The scene's polygons (0x1536488 of 16 bytes at 0x153648C: +4 the shader
// handle), each as the current entity (its index & 0x3FF at 0x1536024, << 7
// at 0x1536028): a draw surface of its shader (out of range: a warning and
// the default 0x1535AE8) with the fog 0x1549548 and dlight bits 2, the
// count read again after each.
void f_004DE980(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, esi = c.esi, edi = c.edi;
  c.esp = e - 12;
  uint32_t poly = U32(0x153648C);
  for (int32_t i = 0; i < I32(0x1536488); ++i, poly += 16) {
    const uint32_t num = uint32_t(i) & 0x3FF;
    Put(0x1536024, num);
    Put(0x1536028, num << 7);
    const int32_t h = I32(poly + 4);
    uint32_t shader;
    if (h < 0 || h >= I32(0x153C528)) {
      c.esi = poly;
      c.edi = uint32_t(i);
      Push(c, uint32_t(h));
      Push(c, 0x53C2C0);
      Push(c, 2);
      CallPtr(c, U32(kPrintfPtr), 0x4DE9D1, 0x4DE9CB);
      c.esp = e - 12;
      shader = U32(0x1535AE8);
    } else {
      shader = U32(0x153C52C + 4 * uint32_t(h));
    }
    if (const uint32_t n = U32(kNumDrawSurfs); int32_t(n) < 0x10000)
      AddDrawSurf(n, ((U32(0x1549548) << 12 | U32(shader + 0x48)) << 18 | U32(0x1536028)) | 2, poly);
  }
  c.ebx = ebx;
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// A brush model entity's surfaces (type 0; EAX the entity): none for the
// entity's +0x2B4 at 2 or without the cvar 0x1534420; else its orientation
// into 0x153629C (0x4C9580), the dlights on its model (the model handle +8
// in 0x15364F8, its brush model +0x4C) and each of its surface references
// (12 bytes at +0x18, count +0x1C read again each time) added with the
// current entity's dlight bits (0x1535D68's +0x9C).
void f_00500830(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, esi = c.esi, edi = c.edi, ent = c.eax;
  if (U32(ent + 0x2B4) != 2 && CvarInt(0x1534420)) {
    c.esp = e - 12;
    c.eax = 0x153629C;
    c.ebx = 0x1536030;
    c.edi = ent;
    Call(c, f_004C9580, 0x500859);
    const uint32_t bmodel = U32(U32(0x15364F8 + 4 * U32(ent + 8)) + 0x4C);
    c.esi = bmodel;
    Push(c, bmodel);
    Call(c, f_004AA490, 0x50086C);
    c.esp = e - 12;
    for (int32_t i = 0; i < I32(bmodel + 0x1C); ++i) {
      c.ebx = 12 * uint32_t(i);
      c.edi = uint32_t(i);
      Push(c, U32(bmodel + 0x18) + 12 * uint32_t(i));
      c.eax = U32(U32(0x1535D68) + 0x9C);
      Call(c, f_004D6970, 0x500897);
      c.esp = e - 12;
    }
  }
  c.ebx = ebx;
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// The world's surfaces and entities without the cell walk (for the debug
// cvar 0x14E43FC at 3 or -3): with the cvar 0x1534490 set and the refdef's
// flag 1 (0x1536364) clear, the entity number 0x7FE (0x1536024, << 7 at
// 0x1536028) and no dlights (0x1536474); each world surface reference (12
// bytes: view count, shader, surface; world +0xA0, count +0x9C) not yet
// marked with the view count (0x1535A28), marked and added, but not a
// surface of type 0x18 and up whose bounds (+0xC) are culled (0x4C8D50 2)
// without r_nocull (0x15344F4); its fog +4 for those types, else 0x1549548.
// Then each cell's (0x40 bytes, world +0x124 read once, count +0x120) entity list
// (+0x38: entity, next): each entity not yet marked (+0xB4), marked and
// added (0x4DEC10).
void f_00500920(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, ebp = c.ebp, esi = c.esi, edi = c.edi;
  if (CvarInt(0x1534490) && !(U8(0x1536364) & 1)) {
    c.esp = e - 16;
    Put(0x1536024, 0x7FE);
    Put(0x1536028, 0x3FF00);
    Put(0x1536474, 0u);
    for (int32_t i = 0; i < I32(U32(0x1535A34) + 0x9C); ++i) {
      const uint32_t ref = U32(U32(0x1535A34) + 0xA0) + 12 * uint32_t(i);
      const uint32_t vc = U32(kViewCount);
      if (U32(ref) == vc) continue;
      c.ebx = 12 * uint32_t(i);
      c.ebp = uint32_t(i);
      c.esi = ref;
      const uint32_t surf = U32(ref + 8);
      c.edi = surf;
      Put(ref, vc);
      if (!CvarInt(kNoCull) && I32(surf) >= 0x18) {
        c.eax = surf + 0xC;
        Call(c, f_004C8D50, 0x5009A3);
        if (c.eax == 2) continue;
      }
      c.eax = I32(surf) >= 0x18 ? U32(surf + 4) : U32(0x1549548);
      c.edx = U32(ref + 4);
      for (uint32_t v : {0u, 0u, 0u, surf}) Push(c, v);
      Call(c, f_004CAF00, 0x5009CD);
      c.esp = e - 16;
    }
    const uint32_t cells = U32(U32(0x1535A34) + 0x124);
    for (int32_t i = 0; i < I32(U32(0x1535A34) + 0x120); ++i) {
      c.ebx = cells + 0x38 + 0x40 * uint32_t(i);
      c.edi = uint32_t(i);
      for (uint32_t node = U32(c.ebx); node; node = U32(node + 4)) {
        const uint32_t ent = U32(node);
        if (U32(ent + 0xB4) == U32(kViewCount)) continue;
        Put(ent + 0xB4, U32(kViewCount));
        c.esi = node;
        Push(c, U32(node));
        Push(c, U32(node));
        Call(c, f_004DEC10, 0x500A2F);
        c.esp = e - 16;
      }
    }
  }
  c.ebx = ebx;
  c.ebp = ebp;
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// The level-of-detail distance (0x4DF2D0's ST0) as the original stores it.
uint32_t PopF32(Ctx& c) {
  const uint32_t v = x87::StoreF32(c.fpu);
  x87::Pop(c.fpu);
  return v;
}

// A model entity's surfaces (type 2; EAX the entity, its model +0x98): the
// level of detail (0x480DD0 of the model's +8 at 0x4DF2D0's distance; none
// below 0) picks a list of surfaces (the model's +0xC + 4 x level: a count,
// then 0x30 bytes each: +4 fog, +8 shader, +0xC a second shader, +0x10 a
// 16-bit triangle count), whose first index is the sum of the counts of the
// levels before. Per surface, its slot in the entity's cache (+0x2B0, at
// +0x248 + 4 x index): an empty slot with a second shader gets an entry
// (0x4DA9F0, from the inverse of the entity's axes +0x1C and its lighting
// first, once) unless the entity's +0xA0 is set. A cached entry moves to
// the front of the cache's list (0x1C8-byte entries at 0x11BEC28, head
// 0x11CD050, stamped with 0x1535A24) and is drawn with the second shader
// and fog 0x1549554, but a shader of flags 0x18 (+0x54) first lights the
// entity and, with +0xA0 set, draws the surface itself; no entry: the
// surface, and with those flags the entity lit at the end. Then with the
// cvar 0x153446C (the triangles / 3) or 0x15343B0 (the surfaces) a debug
// text at the origin (+0x44), but not for flag 8 (+4).
void f_004FA790(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, ebp = c.ebp, esi = c.esi, edi = c.edi;
  const uint32_t ent = c.eax, s = e - 0x4C, model = U32(ent + 0x98);
  auto done = [&] {
    c.ebx = ebx;
    c.ebp = ebp;
    c.esi = esi;
    c.edi = edi;
    c.esp = e + 4;
  };
  c.esp = s;
  c.esi = ent;
  c.edi = model;
  Call(c, f_004DF2D0, 0x4FA7A4);
  Push(c, PopF32(c));
  c.ecx = U32(model + 8);
  Call(c, f_00480DD0, 0x4FA7B0);
  c.esp = s;
  const int32_t level = int32_t(c.eax);
  if (level < 0) return done();
  uint32_t first = 0;
  for (int32_t k = 0; k < level; ++k) first += U32(U32(model + 0xC + 4 * uint32_t(k)));
  const uint32_t list = U32(model + 0xC + 4 * uint32_t(level));
  bool lit = false, light_after = false;
  uint32_t tris = 0;
  for (int32_t i = 0; i < I32(list); ++i) {
    const uint32_t surf = list + 4 + 0x30 * uint32_t(i), slot = (first + uint32_t(i)) * 4 + 0x248;
    c.ebx = list;
    c.ebp = slot;
    c.esi = ent;
    c.edi = surf + 0xC;
    if (!U32(U32(ent + 0x2B0) + slot) && U32(surf + 0xC)) {
      if (!lit) {
        c.eax = ent + 0x1C;
        c.ecx = s + 0x28;
        Call(c, f_00427CB0, 0x4FA835);
        c.eax = ent;
        c.ecx = 0x1536318;
        Call(c, f_004ABEF0, 0x4FA841);
        lit = true;
      }
      if (!U8(ent + 0xA0)) {
        Push(c, s + 0x28);
        Push(c, ent);
        Push(c, uint32_t(i) + first);
        c.eax = U32(ent + 0x2B0);
        c.ecx = surf;
        Call(c, f_004DA9F0, 0x4FA872);
        c.esp = s;
        Put(U32(ent + 0x2B0) + slot, c.eax);
      }
    }
    if (const uint32_t p = U32(U32(ent + 0x2B0) + slot)) {
      const uint32_t entry = uint32_t(int32_t(p - 0x11BEC28) / 0x1C8) * 0x1C8 + 0x11BEC28;
      const uint32_t stamp = U32(0x1535A24), prev = U32(entry + 4);
      Put(entry + 8, stamp);
      Put(prev, U32(entry));
      const uint32_t next = U32(entry), back = U32(entry + 4);
      Put(next + 4, back);
      Put(entry, 0x11CD050);
      Put(entry + 4, U32(0x11CD054));
      Put(0x11CD054, entry);
      Put(U32(entry + 4), entry);
      bool done_surf = false;
      if (U8(U32(surf + 8) + 0x54) & 0x18) {
        c.eax = ent;
        c.ecx = 0x1536318;
        Call(c, f_004ABEF0, 0x4FA8FC);
        if (U8(ent + 0xA0)) {
          done_surf = true;
          if (const uint32_t n = U32(kNumDrawSurfs); int32_t(n) < 0x10000)
            AddDrawSurf(n, (U32(surf + 4) << 12 | U32(U32(surf + 8) + 0x48)) << 18 | U32(0x1536028), surf);
        }
      }
      if (!done_surf) {
        const uint32_t n = U32(kNumDrawSurfs), cached = U32(U32(ent + 0x2B0) + slot);
        if (int32_t(n) < 0x10000)
          AddDrawSurf(n, (U32(U32(surf + 0xC) + 0x48) | U32(0x1549554) << 12) << 18 | U32(0x1536028), cached);
      }
    } else {
      if (const uint32_t n = U32(kNumDrawSurfs); int32_t(n) < 0x10000)
        AddDrawSurf(n, (U32(U32(surf + 8) + 0x48) | U32(surf + 4) << 12) << 18 | U32(0x1536028), surf);
      if (U8(U32(surf + 8) + 0x54) & 0x18) light_after = true;
    }
    tris += U16(surf + 0x10);
  }
  bool text = false;
  uint32_t shown = 0;
  if (CvarInt(0x153446C) && !(U8(ent + 4) & 8)) {
    text = true;
    shown = uint32_t(int32_t(tris) / 3);
  } else if (CvarInt(0x15343B0) && !(U8(ent + 4) & 8)) {
    text = true;
    shown = U32(list);
  }
  if (text) {
    Push(c, shown);
    Push(c, 0x5560E0);
    Call(c, f_00440940, 0x4FAA73);
    c.esp = s;
    Push(c, c.eax);
    c.eax = ent + 0x44;
    c.edi = 0x537890;
    Call(c, f_004AD8F0, 0x4FAA84);
    c.esp = s;
  }
  if (light_after) {
    c.eax = ent;
    c.ecx = 0x1536318;
    Call(c, f_004ABEF0, 0x4FAA9B);
  }
  done();
}

// A model's submodel handle (the 16-bit handle at the model's +0x38 + 2 x
// index): a negative one through the refimport's 0x1549688 (its negation,
// as a dword whose high half is the high half of the original's EAX then:
// eax_high), called from va with return address ret; 0 for none.
int16_t SubModel(Ctx& c, uint32_t model, int32_t a, uint32_t eax_high, uint32_t ret, uint32_t va) {
  uint16_t w = U16(model + 0x38 + 2 * uint32_t(a));
  if (int16_t(w) < 0) {
    const uint32_t esp = c.esp;
    Push(c, 0 - ((eax_high & 0xFFFF0000u) | w));
    CallPtr(c, U32(0x1549688), ret, va);
    c.esp = esp;
    w = uint16_t(c.eax);
  }
  return int16_t(w);
}

// A composite model entity's surfaces (type 1; stack the entity, its
// model +0x90: +0x16 the count of submodels, +0x18 the submodels): none
// for the entity's +0x2B4 at 2 or without the cvar 0x1534424. Each
// submodel's level of detail (0x480DD0 at 0x4DF2D0's distance) in a
// stack array; the surfaces (0x477290's count, none beyond 0x2000 with
// 0x1536498's, a warning with the cvar 0x14E43E8) listed by 0x477340 in a
// second (pairs of 16-bit submodel index and surface), with the callback
// of +0x94 (0x476510 then the refimport's 0x154968C). A submodel of the
// default model (+4 0xA8F6BC) draws instead 0x506310 and 0x5065E0 with the
// cvar 0x14E43E8. Each pair's shader (the submodel handle's model in
// 0x15364F8, its +0x50 by level, a 16-bit handle by surface; out of
// range: a warning and the default 0x1535AE8) of flags 0x18 lights the
// entity at the end (0x4ABDC0), unless it is not drawn here (flag 2 (+4)
// outside a portal's view) and the cvar 0x15343D8 is not above 1; drawn,
// its surface (the submodel's +4 by level + 1, 20 bytes each, then +4,
// +8 by surface) goes to 0x506070 and its 16-bit counts +4 and +2 are
// summed. Then the debug text of the cvar 0x153446C (the sum of +2 at 2,
// else of +4) or 0x15343B0 (the distinct shaders among the drawn
// submodels' and the pairs), but not for flags 0x18; and the debug draw
// 0x5068B0 with the cvar 0x15346D4's string set.
void f_00506980(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, ebp = c.ebp, esi = c.esi, edi = c.edi;
  const uint32_t ent = Arg(c, e, 1), bp = e - 4, local = bp - 0x40;
  auto done = [&] {
    c.ebx = ebx;
    c.ebp = ebp;
    c.esi = esi;
    c.edi = edi;
    c.esp = e + 4;
  };
  c.ebp = bp;
  const bool hidden = (U8(ent + 4) & 2) && !U32(0x1536134);
  const uint32_t model = U32(ent + 0x90);
  if (!CvarInt(0x1534424) || U32(ent + 0x2B4) == 2) return done();
  const uint32_t n = U8(model + 0x16);
  c.esp = e - 0x50 - ((n * 4 + 7) & ~7u);
  const uint32_t levels = c.esp;
  c.esi = ent;
  Call(c, f_004DF2D0, 0x5069ED);
  const uint32_t dist = PopF32(c);
  for (uint32_t i = 0; i < n; ++i) {
    c.esi = i;
    c.ebx = model + 0x18 + 4 * i;
    Push(c, dist);
    c.ecx = U32(model + 0x18 + 4 * i);
    Call(c, f_00480DD0, 0x506A0B);
    c.esp = levels;
    Put(levels + 4 * i, c.eax);
  }
  c.ebx = model;
  c.edi = levels;
  c.edx = model;
  Call(c, f_00477290, 0x506A29);
  const uint32_t count = c.eax;
  if (int32_t(U32(0x1536498) + count) > 0x2000) {
    if (CvarInt(0x14E43E8)) {
      Push(c, 0x5391EC);
      Push(c, 0);
      CallPtr(c, U32(kPrintfPtr), 0x506A5A, 0x506A54);
    }
    return done();
  }
  c.esp = levels - ((count * 4 + 7) & ~7u);
  const uint32_t pairs = c.esp;
  Push(c, levels);
  Push(c, pairs);
  Push(c, model);
  c.ebx = pairs;
  c.esi = local;
  c.edi = model;
  Call(c, f_00477340, 0x506A8C);
  c.esp = pairs;
  c.esi = ent;
  if (U32(ent + 0x94)) {
    Push(c, local);
    Push(c, model);
    Call(c, f_00476510, 0x506AA6);
    c.esp = pairs;
    Push(c, local);
    Push(c, model);
    Push(c, U32(ent + 0x94));
    CallPtr(c, U32(0x154968C), 0x506ABB, 0x506AB5);
    c.esp = pairs;
  }
  for (int32_t k = int32_t(U8(model + 0x16)) - 1; k >= 0; --k) {
    if (U32(U32(model + 0x18 + 4 * uint32_t(k)) + 4) != 0xA8F6BC) continue;
    if (CvarInt(0x14E43E8)) {
      Push(c, local);
      Push(c, ent);
      Call(c, f_00506310, 0x506B86);
      Push(c, local);
      Push(c, ent);
      Call(c, f_005065E0, 0x506B90);
    }
    return done();
  }
  auto pair = [&](int32_t i, uint32_t half) { return int16_t(U16(pairs + 4 * uint32_t(i) + half)); };
  bool light = false;
  int32_t sum4 = 0, sum2 = 0;
  for (int32_t i = 0; i < int32_t(count); ++i) {
    c.ebx = pairs;
    c.esi = uint32_t(i);
    const int32_t a = pair(i, 0);
    c.edi = uint32_t(a);
    const int16_t w = SubModel(c, model, a, count, 0x506B1C, 0x506B16);
    if (!w) continue;
    const uint32_t level = U32(levels + 4 * uint32_t(a));
    const uint32_t h = U16(U32(U32(U32(0x15364F8 + 4 * uint32_t(int32_t(w))) + 0x50) + 4 * level) + 2 * uint32_t(int32_t(pair(i, 2))));
    uint32_t shader;
    if (int32_t(h) >= I32(0x153C528)) {
      Push(c, h);
      Push(c, 0x53C2C0);
      Push(c, 2);
      CallPtr(c, U32(kPrintfPtr), 0x506B60, 0x506B5A);
      c.esp = pairs;
      shader = U32(0x1535AE8);
    } else {
      shader = U32(0x153C52C + 4 * h);
    }
    if (U8(shader + 0x54) & 0x18) light = true;
    if (hidden) continue;
    const int32_t a2 = pair(i, 0), b = pair(i, 2);
    const uint32_t level2 = U32(levels + 4 * uint32_t(a2));
    const uint32_t lods = U32(U32(model + 0x18 + 4 * uint32_t(a2)) + 4);
    const uint32_t surfs = U32(U32(U32(lods + (level2 + 1) * 20) + 4) + 8);
    const uint32_t surf = U32(surfs + 4 * uint32_t(b));
    Push(c, uint32_t(a2));
    Push(c, model);
    c.eax = surfs;
    c.edx = surf;
    c.esi = surf;
    c.edi = shader;
    Call(c, f_00506070, 0x506BF0);
    c.esp = pairs;
    sum4 += int16_t(U16(surf + 4));
    sum2 += int16_t(U16(surf + 2));
  }
  const int32_t tris = CvarInt(0x153446C);
  if (tris && !(U8(ent + 4) & 0x18)) {
    Push(c, uint32_t(tris == 2 ? sum2 : sum4));
    Push(c, 0x5560E0);
    Call(c, f_00440940, 0x506C4C);
    c.esp = pairs;
    Push(c, c.eax);
    c.eax = ent + 0x44;
    c.edi = 0x5378D0;
    Call(c, f_004AD8F0, 0x506D7F);
    c.esp = pairs;
  } else if (CvarInt(0x15343B0) && !(U8(ent + 4) & 0x18)) {
    // Distinct: each pair after the first whose submodel exists and whose
    // shader handle no earlier pair with a submodel has.
    int32_t distinct = 1;
    for (int32_t j = 1; j < int32_t(count); ++j) {
      const int32_t aj = pair(j, 0);
      const int16_t wj = SubModel(c, model, aj, uint32_t(aj), 0x506CB3, 0x506CAD);
      if (!wj) continue;
      const uint32_t mj = U32(0x15364F8 + 4 * uint32_t(int32_t(wj))), tj = U32(mj + 0x50);
      int32_t i = 0;
      for (; i < j; ++i) {
        const int32_t ai = pair(i, 0);
        const int16_t wi = SubModel(c, model, ai, i ? uint32_t(j) : mj, 0x506CF0, 0x506CEA);
        if (!wi) continue;
        const uint32_t si = U32(U32(U32(0x15364F8 + 4 * uint32_t(int32_t(wi))) + 0x50) + 4 * U32(levels + 4 * uint32_t(ai)));
        const uint32_t sj = U32(tj + 4 * U32(levels + 4 * uint32_t(aj)));
        if (U16(sj + 2 * uint32_t(int32_t(pair(j, 2)))) == U16(si + 2 * uint32_t(int32_t(pair(i, 2))))) break;
      }
      if (i == j) ++distinct;
    }
    Push(c, count);
    Push(c, uint32_t(distinct));
    Push(c, 0x5391E4);
    Call(c, f_00440940, 0x506D6B);
    c.esp = pairs;
    Push(c, c.eax);
    c.eax = ent + 0x44;
    c.edi = 0x5378D0;
    Call(c, f_004AD8F0, 0x506D7F);
    c.esp = pairs;
  }
  if (U8(U32(U32(0x15346D4) + 4))) {
    c.ebx = ent;
    c.edi = local;
    Call(c, f_005068B0, 0x506D9A);
    c.esp = pairs;
  }
  if (hidden && CvarInt(0x15343D8) <= 1) return done();
  if (light) {
    c.edi = ent;
    Push(c, 0x1536318);
    Call(c, f_004ABDC0, 0x506DC1);
  }
  done();
}

// R_AddEntitySurfaces (its "Bad reType" error): with r_drawentities
// (0x15343D4) set, each of the scene's entities (0x153646C of 0x2B8 bytes
// at 0x1536470) as the current one (number 0x1536024, reloaded after each,
// its << 7 at 0x1536028, pointer 0x1535D68, +0x9C cleared); none with flag
// 4 (+4) inside a portal's view (0x1536134). By type (+0): 0 and 2 the
// models of 0x500830 and 0x4FA790 (EAX the entity), 1 0x506980 (stack), 3
// and above 0xF ri.Error, 0xB none; the others a draw surface of the
// shader their handle (+0x68) names (out of range: a warning and the
// default shader 0x1535AE8) on the surface 0x5646C8 with the fog
// 0x1549548, but not with flag 2 outside a portal's view.
void f_004CB0C0(Ctx& c) {
  const uint32_t e = c.esp, esi = c.esi;
  if (!CvarInt(0x15343D4)) return void(c.esp = e + 4);
  c.esp = e - 4;
  Put(0x1536024, 0u);
  for (int32_t i = 0; i < I32(0x153646C);) {
    const uint32_t ep = uint32_t(i) * 0x2B8 + U32(0x1536470);
    Put(0x1535D68, ep);
    Put(ep + 0x9C, 0u);
    Put(0x1536028, U32(0x1536024) << 7);
    const uint32_t portal = U32(0x1536134), flags = U32(ep + 4), type = U32(ep);
    c.esi = portal;
    if (!((flags & 4) && portal)) {
      if (type == 0 || type == 2) {
        c.eax = ep;
        Call(c, type ? f_004FA790 : f_00500830, type ? 0x4CB1B1 : 0x4CB19F);
      } else if (type == 1) {
        Push(c, ep);
        Call(c, f_00506980, 0x4CB1A7);
      } else if (type == 3 || type > 0xF) {
        Push(c, 0x54307C);
        Push(c, 1);
        CallPtr(c, U32(0x1549604), 0x4CB1C0, 0x4CB1BA);
      } else if (type != 0xB && !((flags & 2) && !portal)) {
        const int32_t h = I32(ep + 0x68);
        uint32_t shader;
        if (h < 0 || h >= I32(0x153C528)) {
          Push(c, uint32_t(h));
          Push(c, 0x53C2C0);
          Push(c, 2);
          CallPtr(c, U32(kPrintfPtr), 0x4CB16D, 0x4CB167);
          shader = U32(0x1535AE8);
        } else {
          shader = U32(0x153C52C + 4 * uint32_t(h));
        }
        c.esp = e - 4;
        for (uint32_t v : {0u, 0u, 0u, 0x5646C8u}) Push(c, v);
        c.edx = shader;
        c.eax = U32(0x1549548);
        Call(c, f_004CAF00, 0x4CB195);
      }
      c.esp = e - 4;
    }
    i = I32(0x1536024) + 1;
    Put(0x1536024, uint32_t(i));
  }
  c.esi = esi;
  c.esp = e + 4;
}

// A view's surfaces (from 0x4CB270, ESP 8-aligned): the projection
// (0x4C9C00); the world's (0x4D9E20) with a world that has cells (+0x124)
// or the debug cvar 0x14E43FC set, but for its value 3 or -3 those of
// 0x500920 instead; the polygons' (0x4DE980), the entities'
// (R_AddEntitySurfaces) and the debug draw 0x4AC890 with its cvar
// 0x15344D0.
void f_004CB210(Ctx& c) {
  const uint32_t e = c.esp, ebp = c.ebp;
  c.ebp = e - 4;
  c.esp = (e - 4) & ~7u;
  Call(c, f_004C9C00, 0x4CB21B);
  if (const int32_t v = CvarInt(0x14E43FC)) {
    if ((v < 0 ? 0u - uint32_t(v) : uint32_t(v)) == 3)
      Call(c, f_00500920, 0x4CB236);
    else
      Call(c, f_004D9E20, 0x4CB250);
  } else if (const uint32_t w = U32(0x1535A34); w && U32(w + 0x124)) {
    Call(c, f_004D9E20, 0x4CB250);
  }
  Call(c, f_004DE980, 0x4CB255);
  Call(c, f_004CB0C0, 0x4CB25A);
  if (CvarInt(0x15344D0)) Call(c, f_004AC890, 0x4CB26C);
  c.ebp = ebp;
  c.esp = e + 4;
}

// Renders a view (stack: its parms, 0x260 bytes): none without a viewport
// (width +0x130, height +0x134); else the view count (0x1535A28) counted
// twice around the parms' copy into the view (0x1536030), the view's
// frame and scene counts (+0x10C, +0x110) from 0x1535A2C and 0x1535A24,
// four cvars' values for the back end (0xA8F800..: 0x15346FC's clamped
// to 0 with a flag of whether it was not below 0 or unordered, 0x1534528's
// and 0x153448C's clamped to 0, 0x1534678's when above 0, else 0), the
// viewer's orientation (0x4C97D0), the frustum (0x4C9D30), the surfaces
// (0x4CB210) and the sort of those added (R_SortDrawSurfs).
void f_004CB270(Ctx& c) {
  const uint32_t e = c.esp, parms = Arg(c, e, 1);
  if (I32(parms + 0x130) <= 0 || I32(parms + 0x134) <= 0) return void(c.esp = e + 4);
  const uint32_t esi = c.esi;
  const F32 zero = Ld(0x55B584);
  Put(0x1535A28, U32(0x1535A28) + 1);
  MovsD(0x1536030, parms, 0x98);
  Put(0x153613C, U32(0x1535A2C));
  Put(0x1535A28, U32(0x1535A28) + 1);
  Put(0x1536140, U32(0x1535A24));
  const uint32_t start = U32(kNumDrawSurfs);
  const F32 v0 = Ld(U32(0x15346FC) + 0x1C);
  lift::Wr<uint8_t>(g_guest_base, 0xA8F804u, uint8_t(St(v0) >= zero));
  Put(0xA8F808, Fst(St(v0) < zero ? St(zero) : St(v0)));
  for (const auto [cvar, out] : {std::pair{0x1534528u, 0xA8F81Cu}, std::pair{0x153448Cu, 0xA8F830u}}) {
    const F32 v = Ld(U32(cvar) + 0x1C);
    Put(out, Fst(St(v) < zero ? St(zero) : St(v)));
  }
  const F32 v3 = Ld(U32(0x1534678) + 0x1C);
  Put(0xA8F800, St(v3) > zero ? Fst(St(v3)).bits : 0u);
  c.esi = start;
  c.esp = e - 4;
  Call(c, f_004C97D0, 0x4CB383);
  Call(c, f_004C9D30, 0x4CB388);
  Call(c, f_004CB210, 0x4CB38D);
  Push(c, U32(kNumDrawSurfs) - start);
  Push(c, U32(kDrawSurfs) + start * 8);
  Call(c, f_004CAFC0, 0x4CB3A4);
  c.esi = esi;
  c.esp = e + 4;
}

// RE_RenderScene (its log line and errors; stack: the refdef, 0x50 bytes):
// with the renderer registered (0x1535A20) and the cvar 0x15343A4 clear,
// timed by ri.Milliseconds into 0x15364F0; ri.Error without a world
// (0x1535A34) unless the refdef has flag 1 (+0x4C); the refdef copied
// (0x1536318), flag 8 latching 0x154D734 and flag 0x10 into 0x154D708, the
// time in seconds (0x1536368); the scene's entities, dlights, polygons and
// their vertices from the back end's data (0x1549698) and the counts of
// this scene's first (0x154D72C, 0x154D718, 0x154D728, 0x154D720) and of
// all (0x154D70C, 0x154D714, 0x154D710, 0x154D730), the draw surfaces'
// first (0x154D724); with the cvar 0x1534548 at 0 no dlights, at anything
// but 1 none of the first count (and at 3 a print of that 0). Then the
// view's parms from the refdef rendered (0x4CB270): origin and axes, the
// viewport with y from the bottom of the screen (0x15347C8 its height),
// the fovs, the PVS origin, the cvar 0x1534430's value (+0x140) and the
// cvar 0x1534484's times the tangent of half the smaller fov over tan 40
// degrees (+0x144; FPTAN keeps 64 significant bits, so the x87 model's
// arithmetic as in 0x4C9C00). This scene's counts become the next one's
// first. The locals are at the original's (8-aligned) addresses; the
// cookie check is left out.
void f_004DEEC0(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, esi = c.esi, edi = c.edi, ebp = c.ebp, fd = Arg(c, e, 1);
  const uint32_t s = (((e - 4) & ~7u) - 0x26C) - 0xC, parms = s + 0x10;
  auto done = [&] {
    c.ebx = ebx;
    c.esi = esi;
    c.edi = edi;
    c.ebp = ebp;
    c.esp = e + 4;
  };
  c.ebp = e - 4;
  c.esp = s;
  if (!U32(0x1535A20)) return done();
  if (const uint32_t log = U32(0x1830CE4)) {
    Push(c, 0x541C08);
    Push(c, 0x55B2B4);
    Push(c, log);
    Call(c, f_0051D45A, 0x4DEF04);
    c.esp = s;
  }
  if (CvarInt(0x15343A4)) return done();
  CallPtr(c, U32(0x1549608), 0x4DEF1D, 0x4DEF17);
  const uint32_t t0 = c.eax;
  if (!U32(0x1535A34) && !(U8(fd + 0x4C) & 1)) {
    Push(c, 0x541BE8);
    Push(c, 1);
    CallPtr(c, U32(0x1549604), 0x4DEF40, 0x4DEF3A);
    c.esp = s;
  }
  MovsD(0x1536318, fd, 0x14);
  uint32_t height = U32(0x1536324);
  const uint32_t flags = U32(fd + 0x4C);
  if (flags & 8) Put(0x154D734, 1u);
  Put(0x154D708, (flags >> 4) & 1);
  Put(0x1536368, Fst(FromInt(I32(kTime)) * Ld(0x55B58C)));
  const uint32_t b = U32(kBackEndData);
  Put(0x153646C, U32(0x154D70C) - U32(0x154D72C));
  Put(kNumDrawSurfs, U32(0x154D724));
  Put(0x153649C, b + 0x1DD700);
  Put(0x1536470, U32(0x154D72C) * 0x2B8 + b + 0x81700);
  const uint32_t dlights = U32(0x154D714) - U32(0x154D718);
  Put(0x153647C, U32(0x154D718) * 0x88 + b + 0x80000);
  Put(0x1536484, U32(0x154D728) * 0x30 + b + 0x81100);
  Put(0x1536480, U32(0x154D710) - U32(0x154D728));
  Put(kDrawSurfs, b);
  Put(0x1536498, 0u);
  Put(0x1536474, dlights);
  Put(0x1536478, dlights);
  Put(0x1536488, U32(0x154D730) - U32(0x154D720));
  Put(0x153648C, (U32(0x154D720) + 0x1FD70) * 16 + b);
  const int32_t dl = CvarInt(0x1534548);
  if (dl == 0) Put(0x1536478, 0u);
  if (dl != 1) Put(0x1536474, 0u);
  if (dl == 3) {
    Push(c, 0);  // the count, zeroed for any value but 1
    Push(c, 0x541BCC);
    Push(c, 0);
    CallPtr(c, U32(kPrintfPtr), 0x4DF124, 0x4DF11E);
    c.esp = s;
    height = U32(0x1536324);
  }
  const uint32_t width = U32(0x1536320);
  for (uint32_t k = 0; k < 0x260; k += 4) Put(parms + k, 0u);
  Put(parms + 0x128, U32(0x1536318));
  Put(parms + 0x12C, U32(0x15347C8) - height - U32(0x153631C));
  Put(parms + 0x138, U32(kRefdefFovX));
  Put(parms + 0x13C, U32(kRefdefFovY));
  Put(parms + 0x130, width);
  Put(parms + 0x134, height);
  const F32 fx = Ld(parms + 0x138), fy = Ld(parms + 0x13C);
  x87::State& f = c.fpu;
  using x87::Op;
  x87::LoadF32(f, (St(fx) <= fy ? fx : fy).bits);
  x87::ArithF32(f, Op::Mul, U32(0x55B7B4));
  x87::Ptan(f);
  x87::Pop(f);
  x87::LoadF64(f, lift::Rd<uint64_t>(g_guest_base, 0x55B8E8u));
  x87::Ptan(f);
  x87::Pop(f);
  x87::ArithStiSt0(f, Op::Div, 1);
  x87::Pop(f);
  x87::ArithF32(f, Op::Mul, U32(U32(0x1534484) + 0x1C));
  Put(parms + 0x144, x87::StoreF32(f));
  x87::Pop(f);
  Put(parms + 0x140, U32(U32(0x1534430) + 0x1C));
  MovsD(parms, fd + 0x18, 0xC);
  MovsD(parms + 0xF8, fd + 0x18, 3);
  Push(c, parms);
  Call(c, f_004CB270, 0x4DF26A);
  c.esp = s;
  Put(0x154D724, U32(kNumDrawSurfs));
  Put(0x154D72C, U32(0x154D70C));
  Put(0x154D718, U32(0x154D714));
  Put(0x154D720, U32(0x154D730));
  CallPtr(c, U32(0x1549608), 0x4DF29F, 0x4DF299);
  Put(0x15364F0, U32(0x15364F0) + (c.eax - t0));
  done();
}

}  // namespace recomp::gen
