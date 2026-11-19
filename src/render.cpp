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
// and the world's without the cell walk; RE_AddRefEntityToScene, the
// model surfaces' levels of detail, cache entries and entity surfaces and
// the composite models' debug bone draws; the entity lighting: the dlights
// on a brush model, an entity's lights from its own or the light grid (a
// light's intensity, the grid's lights at a point, the lights beyond the
// limit merged, their debug draw; R_PointInLeaf, a grid corner's lights
// and their cache (the bits reversed, the corners' sorted list and an
// insertion into it, the lights found by traces, whether a point light
// reaches a corner, the sun's traces), whether a light reaches a point), a
// vertex's colour; the model cache's blocks, splits, eviction and frees;
// the debug draws (a polygon, a line, a box, text and its queue, the
// client's line, the light grid's corners, r_xdebug's) and the client's
// warning icon (StatMon_UpdateEntry)); the refexport's per-frame entries
// (the scene's polygons, dlights, coronas and fogs; RE_BeginFrame, the
// frame's end, its counters and commands) and the back end's draws of the
// debug queues; the back end's sky clipping (ClipSkyPolygon,
// AddSkyPolygon), texture coordinates (ComputeTexCoords); the draw surface
// sort (qsortFast), the 4x4 matrix product and the memcpy 0x42B0A0 under
// both; and the vector helpers the whole game calls (the normalisations, a
// perpendicular, a rotation about a vector, the 3x3 and 4x4 inverses,
// bounds, BoxOnPlaneSide, a plane from points, the squared distance, a
// point projected onto a plane, the 3x3 matrix product). Replacements of lifted
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

void f_004AD7E0(Ctx& c);  // draws a debug box (ECX mins, EDX maxs, EAX colour), below
void f_004DEC10(Ctx& c);  // RE_AddRefEntityToScene (refentity, its +0x2B0), below
void f_004CAF00(Ctx& c);  // R_AddDrawSurf(surface, dlight, 0, 0; EAX, EDX shader)
void f_0042B0A0_lifted(Ctx& c);  // the memcpy, for overlapping copies
void f_004CAB80(Ctx& c);  // a portal or mirror surface's view (EBX draw surface, entity; EAX: drawn)
void f_00427E80(Ctx& c);  // a 4x4 matrix's inverse (ECX in, EAX out), below
void f_00429070(Ctx& c);  // adds a point to bounds (ECX point, EDX mins, ESI maxs), below
void f_004290E0(Ctx& c);  // adds bounds to bounds (ECX/ESI mins/maxs, EDX/EDI the other's), below
void f_00429800(Ctx& c);  // BoxOnPlaneSide(mins, maxs, plane), below
void f_004268B0(Ctx& c);  // normalises a vector (EAX), below
void f_00426710(Ctx& c);  // VectorNormalize (ESI; the length in ST0), below
void f_004AD630(Ctx& c);  // draws a debug polygon (EBX count, stack points; EAX colour), below
void f_004AD760(Ctx& c);  // draws a debug line (EBX from, EDI to; ESI colour), below
void f_004CA170(Ctx& c);  // a surface's plane (ECX surface, ESI out)
void f_004C9580(Ctx& c);  // an entity's orientation (EAX out, EBX view, EDI entity)
void f_004CA8D0(Ctx& c);  // whether a portal surface is off the screen (draw surface; EAX)
void f_004273C0(Ctx& c);  // a vector across EBX into EDX, below
void f_00426AB0(Ctx& c);  // rotates a point about EAX (out, point, degrees), below
void f_00429710(Ctx& c);  // a point (ECX) projected onto the plane of the normal EAX into EDX, below
void f_00427590(Ctx& c);  // a 3x3 matrix product (ECX by EAX into EDX), below
void f_0052E6D0(Ctx& c);  // the CRT's trigonometric function of ST0 called by the portal rotation
void f_004CB270(Ctx& c);  // renders a view (view parms)
void f_004C97D0(Ctx& c);  // the viewer's orientation (src/render_backend.cpp)
void f_004AA490(Ctx& c);  // the dlights on a brush model (model), below
void f_00427CB0(Ctx& c);  // a 3x3 matrix's inverse (EAX in, ECX out), below
void f_004ABEF0(Ctx& c);  // an entity's lighting, once (EAX entity, ECX refdef), below
void f_004ABDC0(Ctx& c);  // an entity's lighting from the grid, once (EDI entity, refdef), below
void f_004ABD30(Ctx& c);  // an entity's lights from the grid (EAX entity, ESI origin; refdef, flag), below
void f_004AB490(Ctx& c);  // an entity's lights (EDI entity; refdef, origin, count, scales, sun, lights, flag), below
void f_004AA0F0(Ctx& c);  // the dlights into an orientation (EAX count, EDX dlights, ECX orientation)
void f_004AA8D0(Ctx& c);  // a light's (EDX) intensity at a point (ESI; ST0), below
void f_004AA5C0(Ctx& c);  // a debug draw of an entity's lights (origin, entity), below
void f_004AA970(Ctx& c);  // the lights beyond the limit merged into one (EBX entity; origin, values, order), below
void f_004AACB0(Ctx& c);  // the light grid's lights at a point (origin, scales, &sun, lights; EAX count), below
void f_00440940(Ctx& c);  // va(format, ...)
void f_004AD8F0(Ctx& c);  // draws debug text (EAX origin, EDI colour, text), below
void f_00477290(Ctx& c);  // a DObj's surface count (EDX the DObj, EDI levels; EAX; src/xanim.cpp)
void f_00477340(Ctx& c);  // a DObj's surfaces and their bones (DObj, list, levels; ESI bits; src/xanim.cpp)
void f_00476510(Ctx& c);  // a DObj's bones' parents added to a set of its bones (DObj, bits; src/xanim.cpp)
void f_005068B0(Ctx& c);  // the composite models' debug draws by r_xdebug (EBX entity, EDI), below
void f_004AC890(Ctx& c);  // a debug draw of the light grid's corners near the view, below
void f_0051D45A(Ctx& c);  // the CRT's fprintf
void f_0052E610(Ctx& c);  // the CRT's square root (ST0)
void f_00447CA0(Ctx& c);  // StatMon_UpdateEntry: a warning icon (ECX entry, EAX time; its image's name), below
void f_004DA330(Ctx& c);  // a model cache entry of a size class (EBX the cache, class; EAX), below
void f_004DA220(Ctx& c);  // a free block of a level in the model cache (cache, level; EAX), below
void f_004DA1B0(Ctx& c);  // the model cache's oldest entry freed (EDI the cache; EAX), below
void f_004DA0F0(Ctx& c);  // a model cache entry's blocks freed (cache, entry, node, shift), below
void f_004DA420(Ctx& c);  // a vertex's lighting (EBX the refentity; position, normal, colour out), below
void f_004771B0(Ctx& c);  // a DObj's bones' bounds (EDX out, DObj; src/xanim.cpp)
void f_0040F4E0(Ctx& c);  // the client's debug line (from, value, dword, flag; ESI colour, EDI to), below
void f_005008B0(Ctx& c);  // R_PointInLeaf: the world's leaf at ESI (EAX), below
void f_004A9EC0(Ctx& c);  // a grid corner's lights (EAX, ECX; x, z, count, lights, origin, &byte; EAX bits), below
void f_00426610(Ctx& c);  // the squared distance between EAX and ECX (ST0), below
void f_004A99C0(Ctx& c);  // whether the light at EAX reaches ECX (AL 2), below
void f_0041CD60(Ctx& c);  // CM_Trace (src/cm_trace.cpp)
void f_004A9880(Ctx& c);  // each byte of a dword's bits reversed (EAX), below
void f_004AC7E0(Ctx& c);  // a grid corner in the sorted list 0xC952B4 (EAX z; x, y, add; EAX index), below
void f_004AC780(Ctx& c);  // an entry inserted into the sorted list (ESI index, EDI entry; EAX), below
void f_004A9C30(Ctx& c);  // a grid corner's lights found (EAX corner; entry, count, lights, origin; EAX leaf), below
void f_004A9A30(Ctx& c);  // whether the point light at EAX reaches ECX (EAX 1), below
void f_004A9A90(Ctx& c);  // the sun's traces about a grid corner (EAX leaf, EDI corner, ESI entry), below
void f_0041E960(Ctx& c);  // a collision trace (EAX, ECX the box, EDX the start; stack ..., the end; EAX)
void f_0051ACF0(Ctx& c);  // the CRT's memmove
void f_0042A480(Ctx& c);  // a zeroed allocation (size; EAX)
void f_004AD860(Ctx& c);  // queues debug text (EBX point, EDI colour; scale, text), below
void f_0051AA00(Ctx& c);  // the CRT's strncpy
void f_0042E660(Ctx& c);  // the error routine (code, format, ...; longjmps for a drop)
void f_0052AB6F(Ctx& c);  // _stricmp
void f_00431CF0(Ctx& c);  // sets a cvar (name, value; EAX force)
void f_0042E300(Ctx& c);  // prints (format, ...)
void f_004CEA70(Ctx& c);  // the back end's commands run (EAX the list; src/render_backend.cpp)
void f_004CB3B0(Ctx& c);  // GL_Bind (ESI the image; src/render_backend.cpp)
void f_004CB710(Ctx& c);  // GL_State (EAX the state bits; src/render_backend.cpp)
void f_004CBBE0(Ctx& c);  // the client arrays (bits; src/render_backend.cpp)
void f_004ADC20(Ctx& c);  // the immediate-mode batch begun (src/render_backend.cpp)
void f_004ADEC0(Ctx& c);  // a vertex of the batch (x, y, z; src/render_backend.cpp)
void f_004ACF50(Ctx& c);  // debug text drawn (EAX the count; the text; src/render_backend.cpp)
void f_004D6400(Ctx& c);  // the text command queued (src/screen2d.cpp)
void f_004406F0(Ctx& c);  // Q_stricmpn (EAX the count, ECX, EDX; EAX)
void f_004D0700(Ctx& c);  // a world surface's vertices (ESI the surface; buffer, size)
void f_004815E0(Ctx& c);  // a model surface's vertices (ESI the surface; ...)
void f_004FC1B0(Ctx& c);  // the per-frame vertex buffer work (3)
void f_004E59D0(Ctx& c);  // r_gamma applied
void f_004DF810(Ctx& c);  // the frame's texture memory (three outs), below
void f_004D3ED0(Ctx& c);  // the frame's counters printed and cleared, below
void f_004D41A0(Ctx& c);  // the commands so far run, below
void f_004DF740(Ctx& c);  // r_textureMode applied (EAX its string), below
void f_004A97B0(Ctx& c);  // the light grid's settings for the frame, below
void f_004A70A0(Ctx& c);  // r_nv_fogdist_mode applied, below
void f_004D0870(Ctx& c);  // one world surface of type 0x1A a frame, below
void f_004F8D50(Ctx& c);  // one model surface a frame (passed on), below
void f_004AD480(Ctx& c);  // the numbers at points drawn and aged, below
void f_004ACB20(Ctx& c);  // the debug polygons drawn, below
void f_004ACDD0(Ctx& c);  // debug lines drawn (EAX the count; the lines), below
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
// Cvars (cvar_t*: float at +0x1C, integer at +0x20): r_nocull, r_speeds
// (the fog and far clip print at 5), r_znear, the far clip's and a depth
// one.
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

// RE_AddRefEntityToScene (its "bad reType" error; stack: the refentity and
// the pointer for its +0x2B0): none before the renderer is registered
// (0x1535A20); the warning icon 0x447CA0 with the cvar 0x14E43EC from
// 0x731 entities (0x154D70C) on; from 0x7FE none, ri.Printf's "too many
// visible models" once per view count (0x11CE848 the one it was printed
// at). Else, after ri.Error for a type outside 0-15, the refentity's 0x9C
// bytes copied to the next of the back end's entities (+0x81700, 0x2B8
// bytes each), its +0xA1, +0x2B4 and +0xCC cleared and +0x2B0 the second
// argument; that count and the scene's (0x153646C) incremented.
void f_004DEC10(Ctx& c) {
  const uint32_t e = c.esp, esi = c.esi, edi = c.edi;
  c.esp = e + 4;
  if (!U32(0x1535A20)) return;
  if (CvarInt(0x14E43EC) && I32(0x154D70C) >= 0x731) {
    c.esp = e;
    Push(c, 0x541CB8);
    c.eax = 0xBB8;
    c.ecx = 9;
    Call(c, f_00447CA0, 0x4DEC49);
    c.esp = e + 4;
  }
  if (I32(0x154D70C) >= 0x7FE) {
    if (const uint32_t vc = U32(kViewCount); U32(0x11CE848) != vc) {
      c.esp = e;
      Push(c, 0x7FD);
      Push(c, 0x541C90);
      Push(c, 2);
      Put(0x11CE848, vc);
      CallPtr(c, U32(kPrintfPtr), 0x4DEC80, 0x4DEC7A);
      c.esp = e + 4;
    }
    return;
  }
  const uint32_t ref = Arg(c, e, 1);
  if (const int32_t type = I32(ref); type < 0 || type >= 0x10) {
    c.esp = e - 8;
    c.esi = ref;
    Push(c, uint32_t(type));
    Push(c, 0x541C68);
    Push(c, 1);
    CallPtr(c, U32(0x1549604), 0x4DECA3, 0x4DEC9D);
  }
  const uint32_t n = U32(0x154D70C), ent = U32(kBackEndData) + n * 0x2B8;
  MovsD(ent + 0x81700, ref, 0x27);
  Wr<uint8_t>(g_guest_base, ent + 0x817A1, 0);
  Put(ent + 0x819B4, 0u);
  Put(ent + 0x819B0, Arg(c, e, 2));
  Put(ent + 0x817CC, 0u);
  Put(0x154D70C, n + 1);
  Put(0x153646C, U32(0x153646C) + 1);
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

// ST0 popped as the float the original stores.
uint32_t PopF32(Ctx& c) {
  const uint32_t v = x87::StoreF32(c.fpu);
  x87::Pop(c.fpu);
  return v;
}

// An entity's (ESI) level-of-detail distance, pushed on the x87 stack: the
// view origin's (0x1536030) less the entity's (+0x44) stored as floats,
// the CRT's square root of their squares summed z + y + x, times
// 0x1536174 plus 0x1536170, over the entity's +0x40 unless that is 0.
void f_004DF2D0(Ctx& c) {
  const uint32_t e = c.esp, ent = c.esi;
  const F32 dx = Fst(St(Ld(0x1536030)) - Ld(ent + 0x44));
  const F32 dy = Fst(St(Ld(0x1536034)) - Ld(ent + 0x48));
  const F32 dz = Fst(St(Ld(0x1536038)) - Ld(ent + 0x4C));
  const St sum = (St(dz) * dz + St(dy) * dy) + St(dx) * dx;
  x87::LoadF64(c.fpu, std::bit_cast<uint64_t>(sum.v));
  c.esp = e - 0x10;
  Call(c, f_0052E610, 0x4DF31B);
  F32 d{PopF32(c)};
  d = Fst(St(Ld(0x1536174)) * d + Ld(0x1536170));
  if (!(St(Ld(ent + 0x40)) == 0.0f)) d = Fst(St(d) / Ld(ent + 0x40));
  x87::LoadF32(c.fpu, d.bits);
  c.esp = e + 4;
}

// A model's level of detail at a distance (ECX the model, its XModel +4;
// cdecl the distance): the first of the XModel's levels (16-bit count
// +0x64, 0x14 bytes each from +4, a distance first) whose distance is 0 or
// beyond the distance, else -1. While the byte 0xA8F804 is set the levels'
// distances are those at 0xA8F808 instead, and a distance 0xA8F800 other
// than 0 replaces the argument (in its slot).
void f_00480DD0(Ctx& c) {
  const uint32_t e = c.esp;
  uint32_t lod = U8(0xA8F804) ? 0xA8F808 : U32(c.ecx + 4) + 4;
  if (!(St(Ld(0xA8F800)) == 0.0f)) Put(e + 4, U32(0xA8F800));
  const int32_t n = int16_t(U16(U32(c.ecx + 4) + 0x64));
  c.eax = 0xFFFFFFFF;
  for (int32_t i = 0; i < n; ++i, lod += 0x14)
    if (St(Ld(lod)) == 0.0f || St(Ld(e + 4)) < Ld(lod)) {
      c.eax = uint32_t(i);
      break;
    }
  c.esp = e + 4;
}

// A model surface's cache entry (EAX the entity, ECX the surface; stack
// its index, the refentity, the inverse of the entity's axes; EAX the
// entry or 0): none without the cvar 0x1534680 or above 0x200 vertices
// (+0x12). An entry of the size class (32 vertices up, by powers of two)
// from 0x4DA330 (the cache 0x11BEC28 in EBX), counted (0x11CD058 the
// vertices of the classes, 0x11CD05C the surfaces'), with the mode
// 0x1549558, the surface, the index and the entity. Its vertices (24
// bytes: texture coordinates from +0x14, a colour, the position from +0x1C
// into the world by the entity's axes and origin) in a temporary block; the
// colours by the surface's shader's (+0xC) stage +0x154 rgbGen (+0x664: 1
// the identity light byte 0x1536294 in all four, 2 white, 10 each normal
// (+0x18) by the inverse, normalised (0x426710), lit by 0x4DA420, 11 the
// refentity's +0x6C, 12 the stage's +0x680) and alphaGen (+0x67C: 0 opaque
// unless rgbGen is 2, 9 the stage's +0x683 unless rgbGen is 12). Then into
// the vertex buffer at the entry's +8 vertices by mode: 12 and 15 copied
// to 0x154955C, 13 through the buffer object 0x154955C (glBindBufferARB
// 0x1534DE4, glBufferSubDataARB 0x1534B14), 14 by 0x15350DC with
// 0x1549560; the block freed.
void f_004DA9F0(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, ebp = c.ebp, esi = c.esi, edi = c.edi;
  const uint32_t ent = c.eax, surf = c.ecx, inv = Arg(c, e, 3), s = e - 0x30;
  auto done = [&](uint32_t r) {
    c.eax = r;
    c.ebx = ebx;
    c.ebp = ebp;
    c.esi = esi;
    c.edi = edi;
    c.esp = e + 4;
  };
  if (!CvarInt(0x1534680) || U16(surf + 0x12) > 0x200) return done(0);
  uint32_t size = 5;
  if (const int32_t n = U16(surf + 0x12); n > 0x20)
    do ++size;
    while (n > 1 << size);
  c.esp = s;
  Push(c, size);
  c.ebx = 0x11BEC28;
  c.ebp = inv;
  c.esi = ent;
  c.edi = surf;
  Call(c, f_004DA330, 0x4DAA41);
  c.esp = s;
  const uint32_t entry = c.eax;
  if (!entry) return done(0);
  const uint32_t surfs = U32(0x11CD05C);
  Put(0x11CD058, U32(0x11CD058) + (1u << size));
  Put(0x11CD05C, surfs + U16(surf + 0x12));
  Put(entry, U32(0x1549558));
  Put(entry + 0x10, ent);
  Put(entry + 4, surf);
  Put(entry + 0xC, Arg(c, e, 1));
  const uint32_t buf = TempAlloc(c, s, U16(surf + 0x12) * 24, 0x4DAAA9, 0x4DAAA3);
  auto count = [&] { return int32_t(U16(surf + 0x12)); };
  for (int32_t i = 0; i < count(); ++i) {
    const uint32_t v = buf + 24 * uint32_t(i);
    Put(v, U32(U32(surf + 0x14) + 8 * uint32_t(i)));
    Put(v + 4, U32(U32(surf + 0x14) + 8 * uint32_t(i) + 4));
    auto at = [&](uint32_t axis) {
      const uint32_t p = U32(surf + 0x1C) + 12 * uint32_t(i);
      return Fst(((St(Ld(p + 8)) * Ld(ent + 0x34 + axis) + St(Ld(p + 4)) * Ld(ent + 0x28 + axis)) +
                  St(Ld(p)) * Ld(ent + 0x1C + axis)) +
                 Ld(ent + 0x44 + axis));
    };
    Put(v + 0xC, at(0));
    Put(v + 0x10, at(4));
    Put(v + 0x14, at(8));
  }
  auto colours = [&](uint32_t rgba) {
    for (int32_t i = 0; i < count(); ++i) Put(buf + 24 * uint32_t(i) + 8, rgba);
  };
  uint32_t stage = U32(U32(surf + 0xC) + 0x154);
  switch (U32(stage + 0x664)) {
    case 1:
      colours(U32(0x1536294) * 0x1010101u);
      break;
    case 2:
      colours(0xFFFFFFFF);
      break;
    case 10:
      for (int32_t i = 0; i < count(); ++i) {
        const uint32_t nv = U32(surf + 0x18) + 12 * uint32_t(i), normal = s + 0x24;
        Put(normal, Fst((St(Ld(nv + 4)) * Ld(inv + 4) + St(Ld(nv)) * Ld(inv)) + St(Ld(inv + 8)) * Ld(nv + 8)));
        Put(normal + 4, Fst((St(Ld(inv + 0x14)) * Ld(nv + 8) + St(Ld(nv)) * Ld(inv + 0xC)) +
                            St(Ld(nv + 4)) * Ld(inv + 0x10)));
        Put(normal + 8, Fst((St(Ld(nv)) * Ld(inv + 0x18) + St(Ld(inv + 0x20)) * Ld(nv + 8)) +
                            St(Ld(nv + 4)) * Ld(inv + 0x1C)));
        c.esi = normal;
        Call(c, f_00426710, 0x4DABFE);
        c.esp = s;
        x87::Pop(c.fpu);
        const uint32_t v = buf + 24 * uint32_t(i);
        Push(c, v + 8);
        Push(c, normal);
        Push(c, v + 0xC);
        c.ebx = Arg(c, e, 2);
        Call(c, f_004DA420, 0x4DAC13);
        c.esp = s;
      }
      break;
    case 11:
      colours(U32(Arg(c, e, 2) + 0x6C));
      break;
    case 12:
      colours(U32(stage + 0x680));
      break;
  }
  stage = U32(U32(surf + 0xC) + 0x154);
  auto alphas = [&](uint8_t a) {
    for (int32_t i = 0; i < count(); ++i) Wr<uint8_t>(g_guest_base, buf + 24 * uint32_t(i) + 0xB, a);
  };
  if (const uint32_t alpha = U32(stage + 0x67C); !alpha) {
    if (U32(stage + 0x664) != 2) alphas(0xFF);
  } else if (alpha == 9 && U32(stage + 0x664) != 12) {
    alphas(U8(stage + 0x683));
  }
  const uint32_t bytes = U16(surf + 0x12) * 24;
  c.esp = s;
  switch (U32(0x1549558)) {
    case 12:
    case 15:
      MovsD(U32(0x154955C) + U32(entry + 8) * 24, buf, bytes >> 2);
      break;
    case 13:
      Push(c, U32(0x154955C));
      Push(c, 0x8892);
      CallPtr(c, U32(0x1534DE4), 0x4DADB4, 0x4DADAE);
      Push(c, buf);
      Push(c, bytes);
      Push(c, U32(entry + 8) * 24);
      Push(c, 0x8892);
      CallPtr(c, U32(0x1534B14), 0x4DADD9, 0x4DADD3);
      Push(c, 0);
      Push(c, 0x8892);
      CallPtr(c, U32(0x1534DE4), 0x4DADE6, 0x4DADE0);
      break;
    case 14:
      Push(c, 0x8762);
      Push(c, buf);
      Push(c, bytes);
      Push(c, U32(0x1549560) + U32(entry + 8) * 24);
      Push(c, U32(0x154955C));
      CallPtr(c, U32(0x15350DC), 0x4DAE1A, 0x4DAE14);
      break;
  }
  Push(c, buf);
  CallPtr(c, U32(kFreePtr), 0x4DAE51, 0x4DAE4B);
  done(entry);
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

// Adds a composite model's surface (EDX the surface, EDI its shader; stack
// the model and the submodel index): the next entity surface (16 bytes at
// 0x153649C, count 0x1536498): its type 9 for a surface whose +6 is 0xFFFF,
// else 4, either one more while 0x1530280 is set; for a shader without
// the flags 0x3FF7FC00 (+0x50) 6 with the surface's +0x28, else 8 or 7
// with the fog at +0x30 or +0x2C; then the model, the surface and the
// index. A draw surface for it (the fog 0x1549548 unless one of those,
// the view's flag, 4), and with the cvar 0x15343D8 at 2 and the shader's
// +0x58 at 5.0 a second, of the shader 0x1535AEC, by R_AddDrawSurf; the
// count incremented after each.
void f_00506070(Ctx& c) {
  const uint32_t e = c.esp, surf = c.edx, shader = c.edi;
  const uint32_t es = U32(0x153649C) + (U32(0x1536498) << 4), lit = U32(0x1530280) != 0;
  uint32_t fog = U32(0x1549548);
  if (U16(surf + 6) == 0xFFFF) {
    Put(es, lit + 9);
  } else {
    Put(es, lit + 4);
    if (!(U32(shader + 0x50) & 0x3FF7FC00)) {
      if (U32(surf + 0x28)) {
        Put(es, 6u);
        fog = U32(0x1549548);
      } else if (U32(surf + 0x30)) {
        Put(es, 8u);
        fog = U32(U32(surf + 0x30));
      } else if (U32(surf + 0x2C)) {
        Put(es, 7u);
        fog = U32(U32(surf + 0x2C));
      }
    }
  }
  Put(es + 8, surf);
  Put(es + 4, Arg(c, e, 1));
  Put(es + 0xC, Arg(c, e, 2));
  if (const uint32_t n = U32(kNumDrawSurfs); int32_t(n) < 0x10000)
    AddDrawSurf(n, (U32(shader + 0x48) | fog << 12) << 18 | U32(0x1536028) | 4, es);
  Put(0x1536498, U32(0x1536498) + 1);
  if (CvarInt(0x15343D8) == 2 && U32(shader + 0x58) == 0x40A00000) {
    c.esp = e - 8;
    Push(c, 0);
    Push(c, 0);
    Push(c, 0);
    Push(c, es);
    c.eax = fog;
    c.edx = U32(0x1535AEC);
    Call(c, f_004CAF00, 0x506178);
    Put(0x1536498, U32(0x1536498) + 1);
  }
  c.esp = e + 4;
}

// A point p through a bone's matrix (rows m - 0x10, m and m + 0x10, the
// translation at m + 0x20) and then the entity's axes (+0x1C, 0xC apart)
// into out, each world coordinate stored and then the origin (+0x44) added
// to it: row r of each sums the products of the coordinates bone[r] and
// world[r] name, in that order (as the debug draws' x87 code does; each
// product loads the coordinate and multiplies by the matrix's or axis'
// element, but for some of 0x5065E0's, whose points are never NaNs).
struct Sums {
  int bone[3][3], world[3][3];
};
void BoneToWorld(uint32_t out, uint32_t ent, uint32_t m, const F32 p[3], const Sums& o) {
  F32 q[3];
  for (uint32_t r = 0; r < 3; ++r) {
    auto t = [&](int k) { return St(p[k]) * Ld(m - 0x10 + 0x10 * uint32_t(k) + 4 * r); };
    q[r] = Fst(((t(o.bone[r][0]) + t(o.bone[r][1])) + t(o.bone[r][2])) + Ld(m + 0x20 + 4 * r));
  }
  for (uint32_t r = 0; r < 3; ++r) {
    auto t = [&](int k) { return St(q[k]) * Ld(ent + 0x1C + 0xC * uint32_t(k) + 4 * r); };
    Put(out + 4 * r, Fst((t(o.world[r][0]) + t(o.world[r][1])) + t(o.world[r][2])));
  }
  for (uint32_t r = 0; r < 3; ++r) Put(out + 4 * r, Fst(St(Ld(out + 4 * r)) + Ld(ent + 0x44 + 4 * r)));
}

// The debug boxes of a composite model's bones (stack: the entity, the
// bones' bit array; r_xdebug): the bones' bounds (0x4771B0 of the model
// +0x90 into an _alloca array of pointers, EDX), and for each bone (count
// +0x17) whose bit is set the twelve edges of its bounds (corner indices
// in the table 0x564150, six per edge) through the bone's matrix (the
// model's +4, 0x40 bytes each from its +0x50th) into the world, drawn
// white by 0x40F4E0.
void f_00506310(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, ebp = c.ebp, esi = c.esi, edi = c.edi;
  const uint32_t bp = e - 4, ent = Arg(c, e, 1), out = Arg(c, e, 2);
  const uint32_t model = U32(ent + 0x90), n = U8(model + 0x17);
  c.ebp = bp;
  c.esp = e - 0x68 - ((n * 4 + 3) & ~3u);
  const uint32_t bones = c.esp;
  Push(c, model);
  c.ebx = model;
  c.edx = bones;
  c.esi = ent;
  c.edi = n;
  Call(c, f_004771B0, 0x506346);
  c.esp = bones;
  const uint32_t m0 = U32(ent + 0x90);
  uint32_t m = (U8(m0 + 0x50) << 6) + U32(m0 + 4) + 0x40;
  Put(bp - 0x58, 0x3F800000u);
  Put(bp - 0x54, 0x3F800000u);
  Put(bp - 0x50, 0x3F800000u);
  Put(bp - 0x4C, 0u);
  static constexpr Sums kFrom = {{{2, 1, 0}, {2, 1, 0}, {2, 1, 0}}, {{2, 1, 0}, {2, 1, 0}, {2, 1, 0}}};
  static constexpr Sums kTo = {{{0, 2, 1}, {0, 2, 1}, {0, 2, 1}}, {{0, 2, 1}, {0, 2, 1}, {0, 2, 1}}};
  for (uint32_t i = 0; int32_t(i) < int32_t(n); ++i, m += 0x40) {
    if (!(int8_t(U8(out + (i >> 3))) & 1 << (i & 7))) continue;
    const uint32_t b = U32(bones + 4 * i);
    for (uint32_t k = 0; k < 12; ++k) {
      const uint32_t t = 0x564150 + 0x18 * k;
      const F32 from[3] = {Ld(b + 12 * U32(t)), Ld(b + 12 * U32(t + 4) + 4), Ld(b + 12 * U32(t + 8) + 8)};
      BoneToWorld(bp - 0x3C, ent, m, from, kFrom);
      const F32 to[3] = {Ld(b + 12 * U32(t + 0xC)), Ld(b + 12 * U32(t + 0x10) + 4), Ld(b + 12 * U32(t + 0x14) + 8)};
      BoneToWorld(bp - 0x48, ent, m, to, kTo);
      Push(c, 0);
      Push(c, 0);
      Push(c, 0);
      Push(c, bp - 0x3C);
      c.ebx = m;
      c.esi = bp - 0x58;
      c.edi = bp - 0x48;
      Call(c, f_0040F4E0, 0x50659B);
      c.esp = bones;
    }
  }
  c.ebx = ebx;
  c.ebp = ebp;
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// The debug axes of a composite model's bones (stack: the entity, the
// bones' bit array; r_xdebug): for each bone (count +0x17) whose bit is
// set, from the bone's origin (the point 0x537E54 through its matrix, as
// 0x506310's) a line 6 along each of its axes (a diagonal 3x3 of 6.0 in
// the frame), coloured red, green and blue, by 0x40F4E0. The axis' index
// is kept in the entity's argument slot.
void f_005065E0(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, ebp = c.ebp, esi = c.esi, edi = c.edi;
  const uint32_t ent = Arg(c, e, 1), out = Arg(c, e, 2), model = U32(ent + 0x90);
  for (uint32_t k = 0; k < 9; ++k) Put(e - 0x24 + 4 * k, k % 4 ? 0u : 0x40C00000u);
  const uint32_t n = U8(model + 0x17);
  uint32_t m = (U8(model + 0x50) << 6) + U32(model + 4) + 0x40;
  static constexpr Sums kFrom = {{{1, 2, 0}, {1, 2, 0}, {1, 2, 0}}, {{2, 1, 0}, {2, 1, 0}, {2, 1, 0}}};
  static constexpr Sums kTo = {{{1, 2, 0}, {1, 0, 2}, {0, 1, 2}}, {{2, 1, 0}, {0, 2, 1}, {0, 2, 1}}};
  for (uint32_t i = 0; int32_t(i) < int32_t(n); ++i, m += 0x40) {
    if (!(int8_t(U8(out + (i >> 3))) & 1 << (i & 7))) continue;
    for (uint32_t j = 0; j < 3; ++j) {
      Put(e + 4, j);
      for (uint32_t k = 0; k < 4; ++k) Put(e - 0x34 + 4 * k, k == j ? 0x3F800000u : 0u);
      const F32 o[3] = {Ld(0x537E54), Ld(0x537E58), Ld(0x537E5C)};
      BoneToWorld(e - 0x4C, ent, m, o, kFrom);
      const uint32_t row = e - 0x24 + 0xC * j;
      const F32 a[3] = {Ld(row), Ld(row + 4), Ld(row + 8)};
      BoneToWorld(e - 0x40, ent, m, a, kTo);
      c.esp = e - 0x74;
      Push(c, 0);
      Push(c, 0);
      Push(c, 0);
      Push(c, e - 0x4C);
      c.ebx = m;
      c.ebp = ent;
      c.esi = e - 0x34;
      c.edi = e - 0x40;
      Call(c, f_0040F4E0, 0x506865);
    }
    Put(e + 4, 3u);
  }
  c.ebx = ebx;
  c.ebp = ebp;
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
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

// ---- the front end: the model cache ------------------------------------------
//
// The model cache 0x11BEC28 (EBX or the first argument): 0x1C8-byte
// entries (+0/+4 the list of entries in use, oldest first from the head
// at +0xE428, +8 the frame 0x1535A24 last drawn; a tree of 31 nodes from
// +0xC, 4 bytes each: a 16-bit count of the vertices in use below and a
// byte at +2 set for a block in use; from +0x88 sixteen 20-byte blocks of
// 32 vertices: +0/+4 a free list's links, +8 the first vertex, then a
// surface's cache entry's +0xC slot and +0x10 owner) and five free lists
// by level (+0xE400 + 8 x level, 512 vertices at level 0 down to 32 at 4;
// the block of a node is the first of the blocks below it); +0xE430 and
// +0xE434 the vertices of the blocks in use and of their surfaces.

namespace {

void Unlink(uint32_t node) {
  Put(U32(node + 4), U32(node));
  Put(U32(node) + 4, U32(node + 4));
}
// node at the end of the list at head.
void Append(uint32_t head, uint32_t node) {
  Put(node, head);
  Put(node + 4, U32(head + 4));
  Put(head + 4, node);
  Put(U32(node + 4), node);
}
uint32_t EntryOf(uint32_t cache, uint32_t block) {
  return cache + uint32_t(int32_t(block - cache) / 0x1C8) * 0x1C8;
}

}  // namespace

// Frees an entry's blocks (stack: the cache, the entry, a node and the
// shift of its first block: its block is (node + 1) << shift): a node in
// use (byte +0xE) clears its block's owner's slot (+0x10 +0x248 + 4 x
// +0xC) and takes its vertices (1 << (shift + 5)) and its surface's (+4,
// +0x12) off the counts; a node with vertices below has its count cleared
// and its children freed (the right one in the loop); a node without its
// block taken off its free list.
void f_004DA0F0(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, ebp = c.ebp, esi = c.esi, edi = c.edi;
  const uint32_t cache = Arg(c, e, 1), entry = Arg(c, e, 2);
  uint32_t node = Arg(c, e, 3), shift = Arg(c, e, 4);
  auto done = [&] {
    c.ebx = ebx;
    c.ebp = ebp;
    c.esi = esi;
    c.edi = edi;
    c.esp = e + 4;
  };
  auto block = [&] { return entry + 20 * ((node + 1) << (shift & 31)) - 0xB8; };
  c.esp = e - 16;
  while (U16(entry + node * 4 + 0xC)) {
    const uint8_t used = U8(entry + node * 4 + 0xE);
    Wr<uint16_t>(g_guest_base, entry + node * 4 + 0xC, 0);
    if (used) {
      const uint32_t b = block();
      Put(U32(b + 0x10) + U32(b + 0xC) * 4 + 0x248, 0u);
      Put(cache + 0xE430, U32(cache + 0xE430) + (0xFFFFFFFFu << ((shift + 5) & 31)));
      Put(cache + 0xE434, U32(cache + 0xE434) - U16(U32(b + 4) + 0x12));
      Wr<uint8_t>(g_guest_base, entry + node * 4 + 0xE, 0);
      return done();
    }
    c.ebx = shift - 1;
    c.ebp = cache;
    c.esi = node;
    c.edi = entry;
    for (uint32_t v : {shift - 1, 2 * node + 1, entry, cache}) Push(c, v);
    Call(c, f_004DA0F0, 0x4DA12F);
    c.esp = e - 16;
    node = 2 * node + 2;
    shift -= 1;
  }
  Unlink(block());
  done();
}

// The cache's (EDI) oldest entry freed for reuse: none (EAX 0) when it was
// drawn this frame; else its blocks freed (0x4DA0F0), the entry off the
// list in use and its whole block at the end of the free list of level 0
// (EAX 1).
void f_004DA1B0(Ctx& c) {
  const uint32_t e = c.esp, esi = c.esi, cache = c.edi, entry = U32(cache + 0xE428);
  if (U32(entry + 8) != U32(0x1535A24)) {
    c.esp = e - 4;
    c.esi = entry;
    for (uint32_t v : {4u, 0u, entry, cache}) Push(c, v);
    Call(c, f_004DA0F0, 0x4DA1D3);
    Unlink(entry);
    Append(cache + 0xE400, entry + 0x88);
    c.eax = 1;
  } else {
    c.eax = 0;
  }
  c.esi = esi;
  c.esp = e + 4;
}

// A free block of a level (stack: the cache, the level 0-4; EAX 1, or 0
// when there is none): level 0 frees the oldest entry (0x4DA1B0); else, with
// the level's list empty, a block of the level above (recursively), whose
// last free block is split: an entry whose whole block it was goes on the
// list in use, and both halves go at the end of the level's list.
void f_004DA220(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, ebp = c.ebp, esi = c.esi, edi = c.edi;
  const uint32_t cache = Arg(c, e, 1), level = Arg(c, e, 2);
  auto done = [&](uint32_t r) {
    c.eax = r;
    c.ebx = ebx;
    c.ebp = ebp;
    c.esi = esi;
    c.edi = edi;
    c.esp = e + 4;
  };
  c.ebx = level;
  if (!level) {
    c.esp = e - 8;
    c.edi = cache;
    Call(c, f_004DA1B0, 0x4DA233);
    return done(c.eax);
  }
  const uint32_t above = cache + 0xE3F8 + level * 8;
  if (U32(above + 4) == above) {
    c.esp = e - 12;
    c.ebp = cache;
    Push(c, level - 1);
    Push(c, cache);
    Call(c, f_004DA220, 0x4DA257);
    if (!c.eax) return done(0);
  }
  const uint32_t b = U32(above + 4);
  Unlink(b);
  const uint32_t entry = EntryOf(cache, b);
  if (level == 1) Append(cache + 0xE428, entry);
  const uint32_t list = cache + 0xE400 + Arg(c, e, 2) * 8;
  Append(list, b);
  Append(list, entry + (uint32_t(int32_t(b - entry - 0x88) / 20) + (1u << ((4 - level) & 31))) * 20 + 0x88);
  done(1);
}

// A block of a size class (EBX the cache; stack the class 5-9, 32 to 512
// vertices; EAX the block or 0): the last free block of level 9 - class
// (0x4DA220 first when there is none), off its list; an entry whose whole
// block it is goes on the list in use; its node marked in use and the
// class's vertices counted in it and the nodes above; its +8 its first
// vertex, 32 x the block's index in the cache.
void f_004DA330(Ctx& c) {
  const uint32_t e = c.esp, ebp = c.ebp, esi = c.esi, edi = c.edi, cache = c.ebx;
  const uint32_t cls = Arg(c, e, 1), level = 9 - cls, list = cache + 0xE400 + level * 8;
  auto done = [&](uint32_t r) {
    c.eax = r;
    c.ebp = ebp;
    c.esi = esi;
    c.edi = edi;
    c.esp = e + 4;
  };
  if (U32(list + 4) == list) {
    c.esp = e - 4;
    c.edi = level;
    Push(c, level);
    Push(c, cache);
    Call(c, f_004DA220, 0x4DA355);
    if (!c.eax) return done(0);
  }
  const uint32_t b = U32(list + 4);
  Unlink(b);
  const uint32_t index = uint32_t(int32_t(b - cache) / 0x1C8), entry = cache + index * 0x1C8;
  if (!level) Append(cache + 0xE428, entry);
  const uint32_t k = uint32_t(int32_t(b - entry - 0x88) / 20);
  const int32_t node = int32_t(k + 16) >> ((4 - level) & 31);
  Wr<uint8_t>(g_guest_base, entry + uint32_t(node) * 4 + 0xA, 1);
  for (int32_t i = node - 1; i >= 0; i = (i - 1) >> 1) {
    const uint32_t count = entry + uint32_t(i) * 4 + 0xC;
    Wr<uint16_t>(g_guest_base, count, uint16_t(U16(count) + (1u << (cls & 31))));
  }
  Put(entry + k * 20 + 0x90, ((index << 4) + k) << 5);
  done(entry + k * 20 + 0x88);
}

// ---- the front end: the entity lighting -----------------------------------------
//
// A light (0x88 bytes; the dlights 0x153647C, count 0x1536474 for the
// surfaces and the refdef's +0x160 for the entities, and an entity's own):
// type +0, colour +4 (intensity +0x10), two colours +0x14 and +0x24
// (constant and along the normal), origin or direction +0x44, +0x50 0 for
// a direction, a spot's axis +0x54, attenuation +0x60/+0x64/+0x68, spot
// exponent +0x6C and angle +0x70 (180 for none), radius +0x74, the origin
// in an entity's orientation +0x78. A refentity's lighting: +0xA0 a dlight
// reached it, +0xA1 lit, +0xA4 the sun's direction in its axes (with the
// cvar 0x15343D8 at 2), +0xCC up to eight lights (+0xD0: light, value), the
// sun's value +0x110, and its sun lights +0x19C and +0x224. The world
// (0x1535A34): +0x98 its light grid, ambient +0xD8, sun colour +0xF8, sun
// +0x108, +0x118 lighting.

namespace {

// The light at l from the world's sun (colour +0xF8, intensity +0x108):
// type 8, the colour, the colour by k as the constant one and by k2 along
// the normal, pointing up.
void SunLight(uint32_t l, uint32_t world, float k, float k2) {
  Put(l, 8u);
  for (uint32_t i = 0; i < 4; ++i) Put(l + 4 + 4 * i, U32(world + 0xF8 + 4 * i));
  Put(l + 0x10, U32(world + 0x108));
  for (uint32_t i = 0; i < 4; ++i) Put(l + 0x14 + 4 * i, Fst(St(Ld(world + 0xF8 + 4 * i)) * k));
  for (uint32_t i = 0; i < 4; ++i) Put(l + 0x24 + 4 * i, Fst(St(Ld(world + 0xF8 + 4 * i)) * k2));
  for (uint32_t o = 0x34; o < 0x4C; o += 4) Put(l + o, 0u);
  Put(l + 0x4C, 0x3F800000u);
  Put(l + 0x50, 0u);
  Put(l + 0x70, 0x43340000u);
}

// The sun's direction (0x15364B0) into the entity's axes (+0x1C) at +0xA4,
// by their inverse (0x427CB0 into m, from the call site ret).
void SunAxes(Ctx& c, uint32_t ent, uint32_t m, uint32_t ret) {
  c.eax = ent + 0x1C;
  c.ecx = m;
  Call(c, f_00427CB0, ret);
  for (uint32_t row = 0; row < 3; ++row)
    for (uint32_t i = 0; i < 3; ++i) {
      const St v = St(Ld(0x15364B0 + 4 * row)) * Ld(m + 0xC * row + 4 * i);
      Put(ent + 0xA4 + 4 * i, row ? Fst(v + Ld(ent + 0xA4 + 4 * i)) : Fst(v));
    }
}

}  // namespace

// The dlights on a brush model (stack: the model, mins +0, maxs +0xC,
// surface references +0x18, count +0x1C): the dlights into the current
// orientation 0x153629C (0x4AA0F0), then a bit for each whose origin
// there (+0x78) is no more than its radius outside the bounds on any axis
// (in the argument's slot); the bits the current entity's +0x9C and each
// referenced surface's (+8) of type 0x18 and up its +8.
void f_004AA490(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, ebp = c.ebp, esi = c.esi, edi = c.edi;
  const uint32_t model = Arg(c, e, 1);
  c.esp = e - 8;
  c.ebp = model;
  c.edx = U32(0x153647C);
  c.eax = U32(0x1536474);
  c.ecx = 0x153629C;
  Call(c, f_004AA0F0, 0x4AA4AB);
  Put(e + 4, 0u);
  const uint32_t dlights = U32(0x153647C);
  for (int32_t i = 0; i < I32(0x1536474); ++i) {
    const uint32_t dl = dlights + 0x88 * uint32_t(i);
    bool lit = true;
    for (uint32_t k = 0; k < 0xC && lit; k += 4)
      lit = !(St(Ld(dl + 0x78 + k)) - Ld(model + 0xC + k) > Ld(dl + 0x74)) &&
            !(St(Ld(model + k)) - Ld(dl + 0x78 + k) > Ld(dl + 0x74));
    if (lit) Put(e + 4, Arg(c, e, 1) | 1u << (i & 31));
  }
  Put(U32(0x1535D68) + 0x9C, Arg(c, e, 1));
  for (int32_t i = 0; i < I32(model + 0x1C); ++i) {
    const uint32_t surf = U32(U32(model + 0x18) + 12 * uint32_t(i) + 8);
    if (I32(surf) >= 0x18) Put(surf + 8, Arg(c, e, 1));
  }
  c.ebx = ebx;
  c.ebp = ebp;
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// A light's (EDX) intensity at a point (ESI), pushed on the x87 stack: its
// intensity (+0x10) x 1 << 0x1536298 (FIMUL, not for the shift 0); for a
// point (+0x50 not 0) over its attenuation +0x60, with +0x64 or +0x68 not
// 0 plus d2 x +0x68 (d2 the squares of the unrounded differences summed
// z + y + x), and with +0x64 not 0 plus sqrt(d2) x +0x64.
void f_004AA8D0(Ctx& c) {
  const uint32_t light = c.edx, p = c.esi, shift = U32(0x1536298);
  St r = Ld(light + 0x10);
  if (shift) r = r * FromInt(int32_t(1u << (shift & 31)));
  if (!(St(Ld(light + 0x50)) == 0.0f)) {
    const St a = Ld(light + 0x60);
    if (St(Ld(light + 0x64)) == 0.0f && St(Ld(light + 0x68)) == 0.0f) {
      r = r / a;
    } else {
      const St dx = St(Ld(light + 0x44)) - Ld(p), dy = St(Ld(light + 0x48)) - Ld(p + 4),
               dz = St(Ld(light + 0x4C)) - Ld(p + 8);
      const St d2 = (dz * dz + dy * dy) + dx * dx;
      St att = a + d2 * Ld(light + 0x68);
      if (!(St(Ld(light + 0x64)) == 0.0f)) att = att + Sqrt(d2) * Ld(light + 0x64);
      r = r / att;
    }
  }
  x87::LoadF64(c.fpu, std::bit_cast<uint64_t>(r.v));
  c.esp += 4;
}

// The lights beyond the limit 0x15364BC merged into one (EBX the
// refentity; stack the origin and 0x4AB490's values and order of the
// lights kept, +0xCC of them at +0xD0): unless there are no more than the
// limit, those kept from the first (the second when the first's value is
// 1e19) while their values are above its value x the cvar 0x153440C, up
// to the limit less one; the rest's directions (a direction's own, its
// value less the sun's share 1 << 0x1536298 x the world's +0x108 x the
// sun's value +0x110 for types 1 and 8; a point's from the origin,
// normalised by 0x426710) summed by their values into a direction
// (normalised). The merged light +0x114 (its type left) points along it,
// each light adding its colour +4 x the scale 0x1536290 x its value x the
// scale at +0x24 when its dot product with the direction is above 0 (that
// x 0.8 but for a type 1 or the merged light's own type 8), and x 0.5 or
// (1 - dot) / 2 at +0x14; it takes the first merged one's place, value 1.
void f_004AA970(Ctx& c) {
  const uint32_t e = c.esp, esi = c.esi, ent = c.ebx, s = e - 0x80, sum = e - 0x6C, dirs = e - 0x60;
  const uint32_t origin = Arg(c, e, 1), values = Arg(c, e, 2), order = Arg(c, e, 3);
  const int32_t n = I32(ent + 0xCC), limit = I32(0x15364BC);
  if (n <= limit) {
    c.esp = e + 4;
    return;
  }
  auto value = [&](int32_t i) { return values + 4 * U32(order + 4 * uint32_t(i)); };
  int32_t k = St(Ld(value(0))) == F32{0x5F0AC723u} ? 1 : 0;
  const F32 least = Fst(St(Ld(value(k))) * Ld(U32(0x153440C) + 0x1C));
  for (++k; k < limit - 1 && !(St(Ld(value(k))) <= least); ++k) {
  }
  for (uint32_t o = 0; o < 0xC; o += 4) Put(sum + o, 0u);
  if (k < n) {
    int32_t i = k;
    do {
      const uint32_t light = U32(ent + 0xD0 + 8 * uint32_t(i)), d = dirs + 12 * uint32_t(i);
      if (St(Ld(light + 0x50)) == 0.0f) {
        for (uint32_t o = 0; o < 0xC; o += 4) Put(d + o, U32(light + 0x44 + o));
        if (U32(light) == 1 || U32(light) == 8) {
          const uint32_t v = value(i);
          Put(v, Fst(Ld(v) - FromInt(int32_t(1u << (U32(0x1536298) & 31))) * Ld(U32(0x1535A34) + 0x108) *
                                 Ld(ent + 0x110)));
        }
      } else {
        for (uint32_t o = 0; o < 0xC; o += 4) Put(d + o, Fst(St(Ld(light + 0x44 + o)) - Ld(origin + o)));
        c.esp = s;
        c.esi = d;
        Call(c, f_00426710, 0x4AAAB2);
        x87::Pop(c.fpu);
      }
      const St v = Ld(value(i));
      for (uint32_t o = 0; o < 0xC; o += 4) Put(sum + o, Fst(v * Ld(d + o) + Ld(sum + o)));
    } while (++i < I32(ent + 0xCC));
  }
  c.esp = s;
  c.esi = sum;
  Call(c, f_00426710, 0x4AAB12);
  x87::Pop(c.fpu);
  const uint32_t m = ent + 0x114;
  for (uint32_t o = 0x14; o < 0x40; o += 0x10)
    for (uint32_t j = 0; j < 0xC; j += 4) Put(m + o + j, 0u);
  for (int32_t i = k; i < I32(ent + 0xCC); ++i) {
    const uint32_t light = U32(ent + 0xD0 + 8 * uint32_t(i)), d = dirs + 12 * uint32_t(i);
    St dot = (St(Ld(sum)) * Ld(d) + St(Ld(sum + 8)) * Ld(d + 8)) + St(Ld(sum + 4)) * Ld(d + 4);
    St w(0.5f);
    if (!(dot <= 0.0f)) {
      if (U32(light) != 1 && U32(m) != 8) dot = dot * 0.8f;
      const St t = St(Ld(0x1536290)) * Ld(value(i)) * dot;
      for (uint32_t o = 0; o < 0xC; o += 4) Put(m + 0x24 + o, Fst(t * Ld(light + 4 + o) + Ld(m + 0x24 + o)));
      w = (1.0f - dot) * 0.5f;
    }
    const St u = w * (St(Ld(0x1536290)) * Ld(value(i)));
    for (uint32_t o = 0; o < 0xC; o += 4) Put(m + 0x14 + o, Fst(u * Ld(light + 4 + o) + Ld(m + 0x14 + o)));
  }
  for (uint32_t o = 0; o < 0xC; o += 4) Put(m + 0x44 + o, U32(sum + o));
  Put(m + 0x64, 0u);
  Put(m + 0x68, 0u);
  for (uint32_t o : {0x20u, 0x30u, 0x40u, 0x60u}) Put(m + o, 0x3F800000u);
  Put(m + 0x70, 0x43340000u);
  Put(ent + 0xCC, uint32_t(k + 1));
  Put(ent + 0xD0 + 8 * uint32_t(k), m);
  Put(ent + 0xD4 + 8 * uint32_t(k), 0x3F800000u);
  c.esi = esi;
  c.esp = e + 4;
}

// A debug draw of an entity's lights at an origin (stack the origin, the
// refentity), with a world with a node tree (+0x98): not for the cvar
// 0x15344CC at 3 when the direction from the view origin 0x1536330
// (normalised by 0x4268B0) is less than 0.95 along the view's forward
// axis 0x153633C, nor at 4 beyond 96 (0x426610's squared distance); a
// line from the origin to each of the lights of the leaf there
// (R_PointInLeaf 0x5008B0: count +0x18, the world's 16-bit indexes +0x11C
// from +0x14 into its lights +0x110), a direction's 1024 along it, yellow
// or green when the entity has it (+0xD0, +0xCC); with the cvar at 4 or
// more and the leaf's +0x10
// a grid of traces (0x4A99C0) down from the origin's height + 0.1, 32768
// long, the grid's step 32768 (16384 above 3 of 0x15364C4) over half the
// step x (0x15364C4 - 1) each way: one that reaches (AL) drawn to its end,
// one that does not to the original's uninitialised local at e - 0x2C.
void f_004AA5C0(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, esi = c.esi, edi = c.edi, s = e - 0x6C;
  const uint32_t origin = Arg(c, e, 1), ent = Arg(c, e, 2), world = U32(0x1535A34);
  [&] {
    if (!world || !U32(world + 0x98)) return;
    const int32_t mode = CvarInt(0x15344CC);
    if (mode == 3) {
      const uint32_t v = e - 0x48;
      for (uint32_t o = 0; o < 0xC; o += 4) Put(v + o, Fst(St(Ld(origin + o)) - Ld(0x1536330 + o)));
      c.esp = s;
      c.eax = v;
      Call(c, f_004268B0, 0x4AA623);
      if ((St(Ld(0x1536344)) * Ld(v + 8) + St(Ld(0x1536340)) * Ld(v + 4)) + St(Ld(0x153633C)) * Ld(v) < 0.95f)
        return;
    }
    if (mode == 4) {
      c.esp = s;
      c.eax = 0x1536330;
      c.ecx = origin;
      Call(c, f_00426610, 0x4AA667);
      const St d(std::bit_cast<double>(x87::StoreF64(c.fpu)));
      x87::Pop(c.fpu);
      if (d > 9216.0f) return;
    }
    c.esp = s;
    c.esi = origin;
    Call(c, f_005008B0, 0x4AA67F);
    const uint32_t point = c.eax;
    if (!point) return;
    for (int32_t i = 0; i < I32(point + 0x18); ++i) {
      const uint32_t w = U32(0x1535A34);
      const int16_t index = int16_t(U16(U32(w + 0x11C) + 2 * (U32(point + 0x14) + uint32_t(i))));
      const uint32_t light = U32(w + 0x110) + uint32_t(index) * 0x88;
      uint32_t colour = 0x537810, to = light + 0x44;
      for (int32_t j = 0, n = I32(ent + 0xCC); j < n; ++j)
        if (U32(ent + 0xD0 + 8 * uint32_t(j)) == light) {
          colour = 0x537820;
          break;
        }
      if (St(Ld(light + 0x50)) == 0.0f) {
        colour = colour == 0x537820 ? 0x537850 : 0x537840;
        to = e - 0x3C;
        for (uint32_t o = 0; o < 0xC; o += 4) Put(to + o, Fst(St(Ld(light + 0x44 + o)) * 1024.0f + Ld(origin + o)));
      }
      c.esp = s;
      c.ebx = origin;
      c.edi = to;
      c.esi = colour;
      Call(c, f_004AD760, 0x4AA756);
    }
    if (CvarInt(0x15344CC) < 4 || !U32(point + 0x10)) return;
    const int32_t size = I32(0x15364C4);
    const F32 step{size > 3 ? 0x46800000u : 0x47000000u};
    const F32 r = Fst(FromInt(size - 1) * (St(step) * 0.5f));
    const uint32_t from = e - 0x48, to = e - 0x3C;
    Put(from + 8, Fst(St(Ld(origin + 8)) + 0.1f));
    Put(to + 8, Fst(St(Ld(origin + 8)) + 32768.0f));
    const F32 low = Fst(-St(r));
    if (!(St(r) >= low)) return;
    F32 y = low;
    do {
      F32 x = low;
      Put(from + 4, Fst(St(y) * Ld(0x55B894) + Ld(origin + 4)));
      Put(to + 4, Fst(St(y) + Ld(origin + 4)));
      if (St(x) <= r) do {
          Put(from, Fst(St(x) * Ld(0x55B894) + Ld(origin)));
          Put(to, Fst(St(x) + Ld(origin)));
          c.esp = s;
          c.eax = to;
          c.ecx = from;
          Call(c, f_004A99C0, 0x4AA869);
          const bool reaches = c.eax & 0xFF;
          c.ebx = from;
          c.esi = reaches ? 0x537910 : 0x5378E0;
          c.edi = reaches ? to : e - 0x2C;
          Call(c, f_004AD760, 0x4AA88A);
          x = Fst(St(x) + step);
        } while (St(x) <= r);
      y = Fst(St(y) + step);
    } while (St(y) <= r);
  }();
  c.ebx = ebx;
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// R_PointInLeaf (its error string "R_PointInLeaf: bad model"): the leaf of
// the world's node tree (+0x98) at the point ESI (EAX and ECX the leaf).
// Without a world ri.Error, then the walk as before. A node (+0 -1) goes
// to its child +0x10 when the point is in front of its plane (+0xC: the
// products summed z + y + x, less the distance, above 0), else to +0x14.
void f_005008B0(Ctx& c) {
  const uint32_t e = c.esp, p = c.esi;
  if (!U32(0x1535A34)) {
    Push(c, 0x539394);
    Push(c, 1);
    CallPtr(c, U32(0x1549604), 0x5008C6, 0x5008C0);
    c.esp = e;
  }
  uint32_t node = U32(U32(0x1535A34) + 0x98);
  while (U32(node) == 0xFFFFFFFF) {
    const uint32_t plane = U32(node + 0xC);
    const St d = (St(Ld(plane + 8)) * Ld(p + 8) + St(Ld(plane + 4)) * Ld(p + 4)) + St(Ld(plane)) * Ld(p) -
                 Ld(plane + 0xC);
    node = U32(node + (d > 0.0f ? 0x10 : 0x14));
  }
  c.eax = c.ecx = node;
  c.esp = e + 4;
}

// Whether the light at EAX reaches the point ECX (AL 2) or not (AL 0):
// the collision trace 0x41CD60 (results in the frame at e - 0x30, fraction
// 1 to beat; start ECX, end EAX, model 0, the zero vector 0x537E54 as
// origin, mins and maxs, brush mask 0x2001, no capsule or sphere). From
// inside a solid (+0x2F) the contents' bit 0x800 (+0x20) says; else a trace
// that went the whole way (fraction exactly 1) or stopped on a surface
// with flag 4 (+0x1C).
void f_004A99C0(Ctx& c) {
  const uint32_t e = c.esp, from = c.ecx, to = c.eax, results = e - 0x30;
  c.esp = results;
  for (uint32_t v : {0u, 0x2001u, 0x537E54u, 0u, to, from, results}) Push(c, v);
  Put(results, 0x3F800000u);
  c.eax = 0;
  c.ecx = c.edx = 0x537E54;
  Call(c, f_0041CD60, 0x4A99EE);
  if (U8(results + 0x2F))
    c.eax = U32(results + 0x20) >> 11 & 1 ? 2 : 0;
  else
    c.eax = St(Ld(results)) == 1.0f || U8(results + 0x1C) & 4 ? 2 : 0;
  c.esp = e + 4;
}

// A grid corner's lights (EAX the leaf's +0xC, ECX y; stack x, z, count,
// lights, origin, &byte; EAX the 16-bit set of the lights seen, or -1),
// cached: the key (x & 0x3FF | y << 10) << 12 | EAX & 0xFFF in the bucket
// (z with each byte's bits reversed (0x4A9880) + y x 0xC41 - x x 0xC3D) &
// 0x1FFF of 32 entries (key, state byte, the byte, the bits) at 0xC952C8.
// A hit gives its byte; a miss (the first empty entry, or the bucket shifted
// down one by memmove when full: 0xC952C0 counted) first records the corner
// and origin (24 bytes) at 0xC952B0 while 0xC952B8 counts below 0x400000
// and adds it to the sorted list 0x4AC7E0, counts 0xC952C4 (and 0xC951A8,
// the deepest entry 0xC951A4 when not full), then fills the entry
// (0x4A9C30: the corner at (x - 0x1000) x 32, (y - 0x1000) x 32, (z - 0x800)
// x 64 in the frame) and gives its byte. The bits only for state 1. The
// argument slot of x keeps what the original leaves there.
void f_004A9EC0(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, ebp = c.ebp, esi = c.esi, edi = c.edi;
  const uint32_t x = Arg(c, e, 1), y = c.ecx, z = Arg(c, e, 2), out = Arg(c, e, 6);
  const uint32_t key = ((x & 0x3FF) | y << 10) << 12 | (c.eax & 0xFFF);
  c.esp = e - 0x24;
  Push(c, z);
  Call(c, f_004A9880, 0x4A9EEE);
  c.esp = e - 0x24;
  const uint32_t first = 0xC952C8 + ((c.eax + (y * 0xC41 - x * 0xC3D)) & 0x1FFF) * 0x100;
  uint32_t entry = first, left = 0x20;
  bool hit = false;
  while (U8(entry + 4)) {
    if (U32(entry) == key) {
      hit = true;
      break;
    }
    entry += 8;
    if (!--left) break;
  }
  Put(e + 4, left);
  if (hit) {
    Wr<uint8_t>(g_guest_base, out, U8(entry + 5));
  } else {
    if (U32(0xC952B0) && I32(0xC952B8) < 0x400000) {
      auto record = [](uint32_t o, uint32_t v) { Put(U32(0xC952B0) + U32(0xC952B8) * 24 + o, v); };
      const uint32_t origin = Arg(c, e, 5);
      record(0, x);
      record(4, y);
      record(8, Arg(c, e, 2));
      for (uint32_t o = 0; o < 0xC; o += 4) record(0xC + o, U32(origin + o));
      Push(c, 1);
      Push(c, y);
      Push(c, x);
      c.eax = Arg(c, e, 2);
      c.ecx = U32(0xC952B8) * 3;
      c.edx = U32(0xC952B0);
      Call(c, f_004AC7E0, 0x4A9FE1);
      c.esp = e - 0x24;
      Put(0xC952B8, U32(0xC952B8) + 1);
    }
    Put(0xC952C4, U32(0xC952C4) + 1);
    if (const uint32_t n = U32(e + 4)) {
      Put(0xC951A8, U32(0xC951A8) + 1);
      if (I32(0xC951A4) < int32_t(0x21 - n)) Put(0xC951A4, 0x21 - n);
    } else {
      Push(c, 0xF8);
      Push(c, first);
      Push(c, first + 8);
      Call(c, f_0051ACF0, 0x4AA01E);
      c.esp = e - 0x24;
      Put(0xC952C0, U32(0xC952C0) + 1);
      entry = first;
    }
    const uint32_t xs = (x - 0x1000) << 5, ys = (y - 0x1000) << 5, zs = (Arg(c, e, 2) - 0x800) << 6;
    Put(e - 0xC, Fst(FromInt(int32_t(xs))));
    Put(e - 8, Fst(FromInt(int32_t(ys))));
    Put(e - 4, Fst(FromInt(int32_t(zs))));
    Put(e + 4, zs);
    for (uint32_t v : {Arg(c, e, 5), Arg(c, e, 4), Arg(c, e, 3), entry}) Push(c, v);
    Put(entry, key);
    c.eax = e - 0xC;
    Call(c, f_004A9C30, 0x4AA0BC);
    Wr<uint8_t>(g_guest_base, out, U8(entry + 5));
  }
  c.eax = U8(entry + 4) == 1 ? U16(entry + 6) : 0xFFFFFFFFu;
  c.ebx = ebx;
  c.ebp = ebp;
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// Each byte of the dword on the stack with its bits reversed (EAX), by the
// table at 0xC951B0, which is made (and 0x564A20 cleared) while 0x564A20
// is set; the lookups read the table as it is.
void f_004A9880(Ctx& c) {
  const uint32_t e = c.esp;
  if (U32(0x564A20)) {
    Put(0x564A20, 0u);
    for (uint32_t i = 0; i < 0x100; i++) {
      uint8_t r = 0;
      for (uint32_t b = 0; b < 8; b++)
        if (i >> b & 1) r |= uint8_t(0x80 >> b);
      Wr<uint8_t>(g_guest_base, 0xC951B0 + i, r);
    }
  }
  const uint32_t v = Arg(c, e, 1);
  c.eax = 0;
  for (uint32_t k = 0; k < 32; k += 8) c.eax |= uint32_t(U8(0xC951B0 + (v >> k & 0xFF))) << k;
  c.esp = e + 4;
}

// A grid corner's lights found (EAX the corner; stack the cache entry, the
// count, the lights and the origin; EAX the corner's leaf): the entry's
// byte (+5) and bits (+6) cleared, the leaf R_PointInLeaf 0x5008B0. A leaf
// with bit 0 of its +0 or off the grid (+0xC below 0) is not used: state
// (+4) 2. Else the trace 0x41E960 (EAX and ECX the zero vector 0x537E54,
// EDX the corner moved 0.01 along the direction to the origin (0x4268B0)
// stored in the frame; stack 0, the origin, 0, 0x537E54, 0x2001, 0, 0):
// nonzero is state 2 as well. Else state 1, each light's bit (1 << its
// index, 16 bits) for a directional one (+0x50 0) that 0x4A99C0 sees reach
// from 0.1 along its direction (+0x44) to 32768 along it, and for a point
// one (+0x50 not 0, NaN too) that 0x4A9A30 sees reach 0.1 along the
// direction to its origin (+0x44; 0x4268B0) from there; then the sun's
// traces 0x4A9A90. The entry's argument slot ends 0, or 1 for state 2, as
// the original leaves it.
void f_004A9C30(Ctx& c) {
  const uint32_t e = c.esp, esi = c.esi, edi = c.edi;
  const uint32_t corner = c.eax, entry = Arg(c, e, 1), a = e - 0x18, b = e - 0xC;
  Wr<uint16_t>(g_guest_base, entry + 6, 0);
  Wr<uint8_t>(g_guest_base, entry + 5, 0);
  c.esp = e - 0x2C;
  c.esi = corner;
  Call(c, f_005008B0, 0x4A9C4B);
  const uint32_t leaf = c.eax;
  [&] {
    if (U8(leaf) & 1 || I32(leaf + 0xC) < 0) return Put(e + 4, 1u);
    Put(e + 4, 0u);
    const uint32_t origin = Arg(c, e, 4);
    for (uint32_t o = 0; o < 0xC; o += 4) Put(a + o, Fst(St(Ld(origin + o)) - Ld(corner + o)));
    c.eax = a;
    Call(c, f_004268B0, 0x4A9C90);
    for (uint32_t o = 0; o < 0xC; o += 4) Put(a + o, Fst(St(Ld(a + o)) * 0.01f + Ld(corner + o)));
    for (uint32_t v : {0u, 0u, 0x2001u, 0x537E54u, 0u, origin, 0u}) Push(c, v);
    c.eax = c.ecx = 0x537E54;
    c.edx = a;
    Call(c, f_0041E960, 0x4A9CE5);
    c.esp = e - 0x2C;
    if (c.eax) return Put(e + 4, 1u);
    for (uint32_t i = 0; int32_t(i) < I32(e + 8); i++) {
      const uint32_t l = U32(Arg(c, e, 3) + 4 * i), bit = 1u << (i & 31);
      if (St(Ld(l + 0x50)) == 0.0f) {
        for (uint32_t o = 0; o < 0xC; o += 4) Put(a + o, Fst(St(Ld(l + 0x44 + o)) * 0.1f + Ld(corner + o)));
        for (uint32_t o = 0; o < 0xC; o += 4) Put(b + o, Fst(St(Ld(l + 0x44 + o)) * 32768.0f + Ld(corner + o)));
        c.eax = b;
        c.ecx = a;
        Call(c, f_004A99C0, 0x4A9D96);
        if ((c.eax & 0xFF) == 2) Wr<uint16_t>(g_guest_base, entry + 6, uint16_t(U16(entry + 6) | bit));
      } else {
        for (uint32_t o = 0; o < 0xC; o += 4) Put(a + o, Fst(St(Ld(l + 0x44 + o)) - Ld(corner + o)));
        c.eax = a;
        Call(c, f_004268B0, 0x4A9DD6);
        for (uint32_t o = 0; o < 0xC; o += 4) Put(a + o, Fst(St(Ld(a + o)) * 0.1f + Ld(corner + o)));
        c.eax = l + 0x44;
        c.ecx = a;
        Call(c, f_004A9A30, 0x4A9E13);
        if (c.eax) Wr<uint16_t>(g_guest_base, entry + 6, uint16_t(U16(entry + 6) | bit));
      }
    }
    c.eax = leaf;
    c.edi = corner;
    Push(c, origin);
    c.esi = entry;
    Call(c, f_004A9A90, 0x4A9E43);
  }();
  Wr<uint8_t>(g_guest_base, entry + 4, U32(e + 4) ? 2 : 1);
  c.eax = leaf;
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// Whether the point light at EAX reaches the point ECX (EAX 1) or not (0):
// the collision trace 0x41CD60 from ECX to EAX as 0x4A99C0 makes it; not
// from inside a solid (+0x2F) and with a fraction not below 1 (NaN too).
void f_004A9A30(Ctx& c) {
  const uint32_t e = c.esp, from = c.ecx, to = c.eax, results = e - 0x30;
  c.esp = results;
  for (uint32_t v : {0u, 0x2001u, 0x537E54u, 0u, to, from, results}) Push(c, v);
  Put(results, 0x3F800000u);
  c.eax = 0;
  c.ecx = c.edx = 0x537E54;
  Call(c, f_0041CD60, 0x4A9A5E);
  c.eax = !U8(results + 0x2F) && !(St(Ld(results)) < 1.0f);
  c.esp = e + 4;
}

// The sun's traces about a grid corner (EAX the leaf, EDI the corner, ESI
// the cache entry; an unused stack argument), for a leaf with sun (+0x10):
// the grid of 0x4AA5C0's lights' debug draw (the step 32768, 16384 above 3
// of 0x15364C4, (0x15364C4 - 1) half steps each way, x and y from -r,
// compared unrounded with r) of traces 0x41CD60 from the corner + 0.1 up
// (x and y by 2^-21 x the offset) to the corner + 32768 up (x and y by the
// offset); each that reaches (0x4A99C0's test) adds 2 to the entry's byte
// (+5) and sets its bit 0x8000.
void f_004A9A90(Ctx& c) {
  const uint32_t e = c.esp, corner = c.edi, entry = c.esi, t = e - 0x64;
  const uint32_t to = e - 0x48, from = e - 0x3C, results = e - 0x30;
  [&] {
    if (!U32(c.eax + 0x10)) return;
    const int32_t size = I32(0x15364C4);
    const F32 step{size > 3 ? 0x46800000u : 0x47000000u};
    const F32 r = Fst(FromInt(size - 1) * (St(step) * 0.5f));
    Put(from + 8, Fst(St(Ld(corner + 8)) + 0.1f));
    Put(to + 8, Fst(St(Ld(corner + 8)) + 32768.0f));
    const F32 low = Fst(-St(r));
    if (!(St(r) >= low)) return;
    F32 y = low;
    St ny;
    do {
      F32 x = low;
      Put(from + 4, Fst(St(y) * Ld(0x55B894) + Ld(corner + 4)));
      Put(to + 4, Fst(St(y) + Ld(corner + 4)));
      if (St(x) <= r) {
        St nx;
        do {
          Put(from, Fst(St(x) * Ld(0x55B894) + Ld(corner)));
          Put(to, Fst(St(x) + Ld(corner)));
          c.esp = t;
          for (uint32_t v : {0u, 0x2001u, 0x537E54u, 0u, to, from, results}) Push(c, v);
          Put(results, 0x3F800000u);
          c.eax = 0;
          c.ecx = c.edx = 0x537E54;
          Call(c, f_0041CD60, 0x4A9BAD);
          c.esp = t;
          const bool reaches = U8(results + 0x2F) ? U32(results + 0x20) >> 11 & 1
                                                  : St(Ld(results)) == 1.0f || U8(results + 0x1C) & 4;
          if (reaches) {
            const uint8_t n = U8(entry + 5);
            Wr<uint16_t>(g_guest_base, entry + 6, uint16_t(U16(entry + 6) | 0x8000));
            Wr<uint8_t>(g_guest_base, entry + 5, uint8_t(n + 2));
          }
          nx = St(x) + step;
          x = Fst(nx);
        } while (nx <= r);
      }
      ny = St(y) + step;
      y = Fst(ny);
    } while (ny <= r);
  }();
  c.esp = e + 4;
}

// The light grid's lights at a point (stack: the origin, the values, the
// sun's value and the lights out; EAX their count). The sun's value 0;
// with the world's node tree (+0x98) the leaf at the origin (R_PointInLeaf
// 0x5008B0): a leaf off the grid (+0xC below 0) gives the world's default light +0x114 (if any) at 1
// with the sun's value 1; one without lights (+0x18) nor sun (+0x10)
// none. Else its lights (the world's 16-bit indexes +0x11C from +0x14 into
// its lights +0x110) at 0, and for each of the eight corners of the grid
// cell at the origin (32 x 32 x 64 units from -131072: FISTP of the
// distance less 0.5 - 2^-30, shifted; the fractions' weights 1 - f and f
// stored) the lights 0x4A9EC0 sees from it (a bit each; -1 none) take its
// weight (the three weights' product stored, unrounded into the total)
// and the sun's value its byte x the weight x 0x15364C8. With a total
// below 0.98: none (0) sets them all and the sun to 1 off the grid; else
// the sun and the lights / total but the default light, which takes
// 1 - total more. A sun of +0x10 under 0.25 becomes 0.25. The cvar
// 0x15344CC draws the cell (at 2 or below 0, at 3 when the view looks
// along it) and below 0 each corner and its lines to its lights.
void f_004AACB0(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, ebp = c.ebp, esi = c.esi, edi = c.edi, s = e - 0x8C;
  const uint32_t origin = Arg(c, e, 1), values = Arg(c, e, 2), sun = Arg(c, e, 3), lights = Arg(c, e, 4);
  c.eax = [&]() -> uint32_t {
    Put(sun, 0u);
    if (!U32(U32(0x1535A34) + 0x98)) return 0;
    c.esp = s;
    c.esi = origin;
    Call(c, f_005008B0, 0x4AACDF);
    const uint32_t point = c.eax;
    if (I32(point + 0xC) < 0) {
      const uint32_t l = U32(U32(0x1535A34) + 0x114);
      if (!l) return 0;
      Put(lights, l);
      Put(values, 0x3F800000u);
      Put(sun, 0x3F800000u);
      return 1;
    }
    if (!U32(point + 0x18) && !U32(point + 0x10)) return 0;
    int32_t n = 0;
    for (; n < I32(point + 0x18); ++n) {
      const uint32_t w = U32(0x1535A34);
      const int16_t index = int16_t(U16(U32(w + 0x11C) + 2 * (U32(point + 0x14) + uint32_t(n))));
      Put(lights + 4 * uint32_t(n), U32(w + 0x110) + uint32_t(index) * 0x88);
      Put(values + 4 * uint32_t(n), 0u);
    }
    // The cell's index and the two weights 1 - f and f at weight.
    const St half(std::bit_cast<double>(0x3FDFFFFFFF000000ull));
    auto cell = [&](uint32_t o, int shift, float scale, uint32_t weight) {
      const int32_t i = Fistp(St(Fst(St(Ld(origin + o)) - -131072.0f)) - half) >> shift;
      const F32 f = Fst((St(Ld(origin + o)) - -131072.0f) * scale - FromInt(i));
      Put(weight + 4, f);
      Put(weight, Fst(St(1.0f) - f));
      return i;
    };
    const int32_t ix = cell(0, 5, 0.03125f, e - 0x18), iy = cell(4, 5, 0.03125f, e - 0x10),
                  iz = cell(8, 6, 0.015625f, e - 8);
    const int32_t mode = CvarInt(0x15344CC);
    bool draw = mode == 2 || mode < 0;
    if (mode == 3) {
      const uint32_t v = e - 0x48;
      for (uint32_t o = 0; o < 0xC; o += 4) Put(v + o, Fst(St(Ld(origin + o)) - Ld(0x1536330 + o)));
      c.esp = s;
      c.eax = v;
      Call(c, f_004268B0, 0x4AAF0A);
      draw = !((St(Ld(0x1536344)) * Ld(v + 8) + St(Ld(0x1536340)) * Ld(v + 4)) + St(Ld(0x153633C)) * Ld(v) < 0.95f);
    }
    if (draw) {
      const uint32_t mins = e - 0x48, maxs = e - 0x30;
      Put(mins, Fst(FromInt(int32_t(uint32_t(ix - 0x1000) << 5))));
      Put(mins + 4, Fst(FromInt(int32_t(uint32_t(iy - 0x1000) << 5))));
      Put(mins + 8, Fst(FromInt(int32_t(uint32_t(iz - 0x800) << 6))));
      Put(maxs, Fst(St(Ld(mins)) + 32.0f));
      Put(maxs + 4, Fst(St(Ld(mins + 4)) + 32.0f));
      Put(maxs + 8, Fst(St(Ld(mins + 8)) + 64.0f));
      c.esp = s;
      c.eax = 0x5378D0;
      c.ecx = mins;
      c.edx = maxs;
      Call(c, f_004AD7E0, 0x4AAFB4);
    }
    const uint32_t total = e - 0x78, corner = e - 0x48, mins = e - 0x30, maxs = e - 0x3C, seen = e - 0x79;
    Put(total, 0u);
    for (int32_t k = 0; k < 8; ++k) {
      const int32_t bx = k & 1, by = k >> 1 & 1, bz = k >> 2 & 1;
      c.esp = s;
      for (uint32_t v : {seen, origin, lights, uint32_t(n), uint32_t(iz + bz), uint32_t(ix + bx)}) Push(c, v);
      c.eax = U32(point + 0xC);
      c.ecx = uint32_t(iy + by);
      c.edx = point;
      c.ebx = uint32_t(bx);
      c.esi = uint32_t(bz);
      c.edi = uint32_t(by);
      c.ebp = uint32_t(ix + bx);
      Call(c, f_004A9EC0, 0x4AB01A);
      const uint32_t bits = c.eax;
      c.esp = s;
      if (CvarInt(0x15344CC) < 0) {
        Put(corner, Fst(FromInt(int32_t(uint32_t(ix + bx) << 5)) - 131072.0f));
        Put(corner + 4, Fst(FromInt(int32_t(uint32_t(iy + by) << 5)) - 131072.0f));
        Put(corner + 8, Fst(FromInt(int32_t(uint32_t(iz + bz) << 6)) - 131072.0f));
        for (uint32_t o = 0; o < 0xC; o += 4) Put(mins + o, Fst(St(Ld(corner + o)) - 1.0f));
        for (uint32_t o = 0; o < 0xC; o += 4) Put(maxs + o, Fst(St(Ld(corner + o)) + 1.0f));
      }
      auto box = [&](uint32_t colour, uint32_t ret) {
        if (CvarInt(0x15344CC) >= 0) return;
        c.eax = colour;
        c.ecx = mins;
        c.edx = maxs;
        Call(c, f_004AD7E0, ret);
      };
      if (int32_t(bits) < 0) {
        box(0x537810, 0x4AB100);
        continue;
      }
      const St product = St(Ld(e - 0x10 + 4 * uint32_t(by))) * Ld(e - 8 + 4 * uint32_t(bz)) * Ld(e - 0x18 + 4 * uint32_t(bx));
      const F32 weight = Fst(product);
      Put(total, Fst(product + Ld(total)));
      Put(sun, Fst(FromInt(U8(seen)) * weight * Ld(0x15364C8) + Ld(sun)));
      if (!bits) {
        box(0x537840, 0x4AB169);
        continue;
      }
      for (int32_t i = 0; i < n; ++i) {
        if (!(bits & 1u << (i & 31))) continue;
        Put(values + 4 * uint32_t(i), Fst(St(weight) + Ld(values + 4 * uint32_t(i))));
        if (CvarInt(0x15344CC) >= 0) continue;
        const uint32_t light = U32(lights + 4 * uint32_t(i));
        uint32_t to = light + 0x44, colour = 0x537820;
        if (St(Ld(light + 0x50)) == 0.0f) {
          to = e - 0x24;
          colour = 0x537850;
          for (uint32_t o = 0; o < 0xC; o += 4) Put(to + o, Fst(St(Ld(light + 0x44 + o)) * 32768.0f + Ld(corner + o)));
        }
        c.ebx = corner;
        c.edi = to;
        c.esi = colour;
        Call(c, f_004AD760, 0x4AB224);
      }
    }
    if (St(Ld(total)) < 0.98f) {
      if (St(Ld(total)) == 0.0f) {
        if (I32(point + 0xC) < 0) {
          for (int32_t i = 0; i < n; ++i) Put(values + 4 * uint32_t(i), 0x3F800000u);
          Put(sun, 0x3F800000u);
        }
      } else {
        const F32 inv = Fst(St(1.0f) / Ld(total));
        if (!(St(Ld(sun)) == 0.0f)) Put(sun, Fst(St(inv) * Ld(sun)));
        for (int32_t i = 0; i < n; ++i) {
          const uint32_t v = values + 4 * uint32_t(i);
          if (St(Ld(v)) == 0.0f) continue;
          if (U32(lights + 4 * uint32_t(i)) == U32(U32(0x1535A34) + 0x114))
            Put(v, Fst((St(1.0f) - Ld(total)) + Ld(v)));
          else
            Put(v, Fst(St(inv) * Ld(v)));
        }
      }
    }
    if (U32(point + 0x10) && St(Ld(sun)) < 0.25f) Put(sun, 0x3E800000u);
    return uint32_t(n);
  }();
  c.ebx = ebx;
  c.ebp = ebp;
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// An entity's lights (EDI the refentity; stack: refdef, origin, count,
// values, the sun's value, lights, flag; the count and the sun's value in
// their slots change): +0xA0 cleared, then the refdef's dlights (+0x164,
// count +0x160) whose radius x 2 reaches the origin added with the value
// 1.0, setting +0xA0; the world's sun (unless the sun's value or the
// world's +0x108 is 0, or 0x15364C0 is 0) added as one light at +0x19C
// (0x15364C0 1 or more lights than 0x15364BC) or two, +0x19C and +0x224,
// each with the sun's value, which then becomes 0. Of the lights with a
// value other than 0: those of a negative or NaN intensity first (1e19),
// the rest by 0x4AA8D0's intensity x the value (types 1 and 8 plus 1 <<
// 0x1536298 x the sun's +0x108 x the sun's value), dropped below the cvar
// 0x1534578; the eight greatest (ties: the first) in order at +0xD0 with
// their values, their count +0xCC, the sun's value +0x110; with the cvar
// 0x15344CC above 0 a debug draw (0x4AA5C0), with the flag those beyond
// 0x15364BC merged (0x4AA970, from the values and the order on the stack).
void f_004AB490(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, ebp = c.ebp, esi = c.esi, ent = c.edi, s = e - 0x110;
  const uint32_t refdef = Arg(c, e, 1), origin = Arg(c, e, 2), values = Arg(c, e, 4), lights = Arg(c, e, 6);
  const uint32_t order = s + 0x2C, value = s + 0x4C;
  Wr<uint8_t>(g_guest_base, ent + 0xA0, 0);
  uint32_t n = Arg(c, e, 3);
  if (refdef && I32(refdef + 0x160) > 0) {
    for (int32_t i = 0; i < I32(refdef + 0x160); ++i) {
      const uint32_t dl = U32(refdef + 0x164) + 0x88 * uint32_t(i);
      const F32 dx = Fst(St(Ld(origin)) - Ld(dl + 0x44));
      const F32 dy = Fst(St(Ld(origin + 4)) - Ld(dl + 0x48));
      const F32 dz = Fst(St(Ld(origin + 8)) - Ld(dl + 0x4C));
      const St d2 = (St(dz) * dz + St(dy) * dy) + St(dx) * dx;
      if (St(Ld(dl + 0x74)) * Ld(dl + 0x74) * 4.0f < d2) continue;
      Put(lights + 4 * n, dl);
      Put(values + 4 * n, 0x3F800000u);
      ++n;
      Wr<uint8_t>(g_guest_base, ent + 0xA0, 1);
    }
    Put(e + 0xC, n);
  }
  const uint32_t world = U32(0x1535A34);
  if (!(St(Ld(e + 0x14)) == 0.0f) && !(St(Ld(world + 0x108)) == 0.0f) && U32(0x15364C0)) {
    const uint32_t sun = Arg(c, e, 5);
    const bool one = U32(0x15364C0) == 1 || int32_t(n) > I32(0x15364BC);
    SunLight(ent + 0x19C, world, one ? 0.5f : 0.75f, one ? 0.5f : 0.25f);
    Put(values + 4 * n, sun);
    Put(lights + 4 * n, ent + 0x19C);
    ++n;
    if (!one) {
      // The first's colour, the sun's intensity and colour by -0.25 along
      // the normal, pointing down.
      const uint32_t l = ent + 0x224;
      Put(l, 8u);
      for (uint32_t i = 0; i < 3; ++i) Put(l + 4 + 4 * i, U32(ent + 0x1A0 + 4 * i));
      Put(l + 0x10, Fst(St(Ld(world + 0x108)) * -0.25f));
      for (uint32_t i = 0; i < 4; ++i) Put(l + 0x14 + 4 * i, 0u);
      for (uint32_t i = 0; i < 4; ++i) Put(l + 0x24 + 4 * i, Fst(St(Ld(world + 0xF8 + 4 * i)) * -0.25f));
      for (uint32_t o = 0x34; o < 0x4C; o += 4) Put(l + o, 0u);
      Put(l + 0x4C, 0xBF800000u);
      Put(l + 0x50, 0u);
      Put(l + 0x70, 0x43340000u);
      Put(values + 4 * n, sun);
      Put(lights + 4 * n, l);
      ++n;
    }
    Put(e + 0xC, n);
    Put(e + 0x14, 0u);
  }
  int32_t picked = 0;
  for (int32_t i = 0; i < I32(e + 0xC); ++i) {
    const uint32_t v = value + 4 * uint32_t(i);
    if (St(Ld(values + 4 * uint32_t(i))) == 0.0f) continue;
    const uint32_t light = U32(lights + 4 * uint32_t(i));
    if (!(St(Ld(light + 0x10)) >= 0.0f)) {
      Put(v, 0x5F0AC723u);
    } else {
      c.esp = s;
      c.ebx = uint32_t(picked);
      c.ebp = v;
      c.esi = origin;
      c.edx = light;
      Call(c, f_004AA8D0, 0x4ABA99);
      const St r(std::bit_cast<double>(x87::StoreF64(c.fpu)));
      x87::Pop(c.fpu);
      const F32 rv = Fst(r * Ld(values + 4 * uint32_t(i)));
      Put(v, rv);
      if (const uint32_t type = U32(light); type == 1 || type == 8) {
        const F32 one = Fst(FromInt(int32_t(1u << (U32(0x1536298) & 31))));
        Put(v, Fst(St(one) * Ld(U32(0x1535A34) + 0x108) * Ld(e + 0x14) + rv));
      }
      if (St(Ld(v)) < Ld(U32(0x1534578) + 0x1C)) continue;
    }
    int32_t at = 0;
    while (at < picked && St(Ld(value + 4 * U32(order + 4 * uint32_t(at)))) > Ld(v)) ++at;
    if (at >= 8) continue;
    if (picked == 8) picked = 7;
    for (int32_t k = picked; k > at; --k) Put(order + 4 * uint32_t(k), U32(order + 4 * uint32_t(k) - 4));
    Put(order + 4 * uint32_t(at), uint32_t(i));
    ++picked;
  }
  Put(ent + 0xCC, uint32_t(picked));
  for (int32_t k = 0; k < picked; ++k) {
    const uint32_t i = U32(order + 4 * uint32_t(k));
    Put(ent + 0xD4 + 8 * uint32_t(k), U32(values + 4 * i));
    Put(ent + 0xD0 + 8 * uint32_t(k), U32(lights + 4 * i));
  }
  Put(ent + 0x110, U32(e + 0x14));
  c.ebx = uint32_t(picked);
  c.esi = 0;
  if (CvarInt(0x15344CC) > 0) {
    c.esp = s;
    Push(c, ent);
    Push(c, origin);
    Call(c, f_004AA5C0, 0x4ABCFE);
  }
  if (Arg(c, e, 7)) {
    c.esp = s;
    Push(c, order);
    Push(c, value);
    Push(c, origin);
    c.ebx = ent;
    Call(c, f_004AA970, 0x4ABD23);
  }
  c.ebx = ebx;
  c.ebp = ebp;
  c.esi = esi;
  c.esp = e + 4;
}

// An entity's lights from the light grid (EAX the refentity, ESI the
// origin; stack refdef, flag): with a world with a grid (+0x98) and the
// cvar 0x15346C4 clear, the grid's lights at the origin (0x4AACB0: values,
// the sun's value and lights in this frame) through 0x4AB490; else none
// (+0xCC).
void f_004ABD30(Ctx& c) {
  const uint32_t e = c.esp, edi = c.edi, ent = c.eax, s = e - 0x19C, world = U32(0x1535A34);
  if (world && U32(world + 0x98) && !CvarInt(0x15346C4)) {
    const uint32_t lights = s + 0xC, sun = s + 8, values = s + 0xD4;
    c.esp = s;
    c.edi = ent;
    for (uint32_t v : {lights, sun, values, c.esi}) Push(c, v);
    c.eax = values;
    c.ecx = lights;
    c.edx = sun;
    Call(c, f_004AACB0, 0x4ABD70);
    c.esp = s;
    for (uint32_t v : {Arg(c, e, 2), lights, U32(sun), values, c.eax, c.esi, Arg(c, e, 1)}) Push(c, v);
    Call(c, f_004AB490, 0x4ABD9C);
  } else {
    Put(ent + 0xCC, 0u);
  }
  c.edi = edi;
  c.esp = e + 4;
}

// An entity's lighting from the grid, once (EDI the refentity; stack the
// refdef): unless lit (+0xA1, then set), its lights at its lighting origin
// (+0xC with flag 0x80 at +4, else its origin +0x44) by 0x4ABD30, and the
// sun's direction in its axes with the cvar 0x15343D8 at 2.
void f_004ABDC0(Ctx& c) {
  const uint32_t e = c.esp, esi = c.esi, ent = c.edi, s = e - 0x38;
  if (!U8(ent + 0xA1)) {
    const uint32_t o = U8(ent + 4) & 0x80 ? ent + 0xC : ent + 0x44;
    Wr<uint8_t>(g_guest_base, ent + 0xA1, 1);
    for (uint32_t k = 0; k < 0xC; k += 4) Put(s + 8 + k, U32(o + k));
    c.esp = s;
    Push(c, 1);
    Push(c, Arg(c, e, 1));
    c.eax = ent;
    c.esi = s + 8;
    Call(c, f_004ABD30, 0x4ABE12);
    c.esp = s;
    if (CvarInt(0x15343D8) == 2) SunAxes(c, ent, s + 0x14, 0x4ABE33);
  }
  c.esi = esi;
  c.esp = e + 4;
}

// An entity's lighting, once (EAX the refentity, ECX the refdef): with the
// cvar 0x15344CC below 0 from the grid (0x4ABDC0); else, unless lit (+0xA1,
// then set), its lights at its lighting origin (+0xC) from those its +0x2B0
// gives (count +0xB8, the sun's value +0xBC, values +0xC0, lights +0x184)
// by 0x4AB490, and the sun's direction in its axes with the cvar 0x15343D8
// at 2.
void f_004ABEF0(Ctx& c) {
  const uint32_t e = c.esp, edi = c.edi, ent = c.eax, s = e - 0x2C;
  c.edi = ent;
  if (CvarInt(0x15344CC) < 0) {
    c.esp = s;
    Push(c, c.ecx);
    Call(c, f_004ABDC0, 0x4ABF08);
  } else if (!U8(ent + 0xA1)) {
    const uint32_t info = U32(ent + 0x2B0);
    c.esp = s;
    Push(c, 1);
    Push(c, info + 0x184);
    Wr<uint8_t>(g_guest_base, ent + 0xA1, 1);
    for (uint32_t v : {U32(info + 0xBC), info + 0xC0, U32(info + 0xB8), ent + 0xC, c.ecx}) Push(c, v);
    Call(c, f_004AB490, 0x4ABF53);
    c.esp = s;
    if (CvarInt(0x15343D8) == 2) SunAxes(c, ent, s + 8, 0x4ABF73);
  }
  c.edi = edi;
  c.esp = e + 4;
}

// A vertex's colour (EBX the refentity; stack position, normal, the colour
// out): without the world's lighting (+0x118) the identity light byte
// 0x1536294 and alpha 255. Else the world's ambient (+0xD8) plus its sun
// colour (+0xF8) x the entity's sun value (+0x110), then each of the
// entity's lights (+0xCC, +0xD0: light, value): a direction (+0x50 == 0)
// its dot product with the normal at its value; a point its direction
// from the vertex, normalised (0x426710, the length l), the dot product,
// the value over (l x +0x68 + +0x64) x l + +0x60, and for a spot (+0x70
// not 180) 0 when the direction is not beyond the cosine of the table
// 0x1544530 at the angle, else that by the cosine +0x6C times (at least
// once while above 0); the dot product (0 below) x +0x24 + +0x14 by that
// added to the colour, each sum stored. Each channel x 255 rounded
// (+2^-30), clamped to 0-255.
void f_004DA420(Ctx& c) {
  const uint32_t e = c.esp, esi = c.esi, ref = c.ebx, world = U32(0x1535A34);
  const uint32_t pos = Arg(c, e, 1), nrm = Arg(c, e, 2), out = Arg(c, e, 3);
  if (!U32(world + 0x118)) {
    for (uint32_t k = 0; k < 3; ++k) Wr<uint8_t>(g_guest_base, out + k, U8(0x1536294));
    Wr<uint8_t>(g_guest_base, out + 3, 0xFF);
    c.esp = e + 4;
    return;
  }
  F32 colour[4];
  for (uint32_t k = 0; k < 4; ++k)
    colour[k] = Fst(St(Ld(world + 0xF8 + 4 * k)) * Ld(ref + 0x110) + Ld(world + 0xD8 + 4 * k));
  for (int32_t i = 0; i < I32(ref + 0xCC); ++i) {
    const uint32_t light = U32(ref + 0xD0 + 8 * uint32_t(i));
    const F32 value = Fst(St(Ld(ref + 0xD4 + 8 * uint32_t(i))));
    F32 dot;
    St scale;
    if (St(Ld(light + 0x50)) == 0.0f) {
      const St d = (St(Ld(light + 0x4C)) * Ld(nrm + 8) + St(Ld(light + 0x44)) * Ld(nrm)) +
                   St(Ld(light + 0x48)) * Ld(nrm + 4);
      dot = d < 0.0f ? F32{0} : Fst(d);
      scale = St(value);
    } else {
      const uint32_t dir = e - 0x1C;
      for (uint32_t k = 0; k < 0xC; k += 4) Put(dir + k, Fst(St(Ld(light + 0x44 + k)) - Ld(pos + k)));
      c.esp = e - 0x58;
      c.esi = dir;
      Call(c, f_00426710, 0x4DA55D);
      const St len(std::bit_cast<double>(x87::StoreF64(c.fpu)));
      x87::Pop(c.fpu);
      const St d =
          (St(Ld(dir + 8)) * Ld(nrm + 8) + St(Ld(dir + 4)) * Ld(nrm + 4)) + St(Ld(dir)) * Ld(nrm);
      dot = d < 0.0f ? F32{0} : Fst(d);
      scale = value / ((len * Ld(light + 0x68) + Ld(light + 0x64)) * len + Ld(light + 0x60));
      const F32 stored = Fst(scale);
      if (U32(light + 0x70) != 0x43340000) {
        const F32 cosine = Fst((St(Ld(dir + 8)) * Ld(light + 0x5C) + St(Ld(dir + 4)) * Ld(light + 0x58)) +
                               St(Ld(dir)) * Ld(light + 0x54));
        const F32 angle = Fst((St(Ld(light + 0x70)) + 90.0f) * Ld(0x55B76C));
        const uint32_t edge = 0x1544530 + 4 * (uint32_t(Fistp(St(angle) + St(0x1p-30))) & 0x3FF);
        if (St(cosine) <= Ld(edge)) {
          scale = St(0.0f);
        } else if (!(St(0.0f) < Ld(light + 0x6C))) {
          scale = St(stored);
        } else {
          const F32 exponent = Ld(light + 0x6C);
          scale = St(stored);
          int32_t n = 0;
          do scale = scale * cosine;
          while (FromInt(++n) < exponent);
        }
      }
    }
    for (uint32_t k = 0; k < 4; ++k)
      colour[k] = Fst((St(dot) * Ld(light + 0x24 + 4 * k) + Ld(light + 0x14 + 4 * k)) * scale + colour[k]);
  }
  for (uint32_t k = 0; k < 4; ++k) {
    const int32_t v = Fistp(St(Fst(St(colour[k]) * 255.0f)) + St(0x1p-30));
    Wr<uint8_t>(g_guest_base, out + k, uint8_t(v < 0 ? 0 : v > 0xFF ? 0xFF : v));
  }
  c.esi = esi;
  c.esp = e + 4;
}

// ---- the front end's debug draws --------------------------------------------
//
// Queues the back end draws (each a count and its limit, read signed, and a
// block from 0x42A480, the zeroed allocation, made at the limit's size when
// it is missing): polygons 0x1549580/0x1549584/0x1549588 (24 bytes: colour,
// first point, count) with their points 0x1549574/0x1549578/0x154957C (12
// bytes), text 0x154958C/0x1549590/0x1549594 (0x4AD860) and lines
// 0x15495A4/0x15495A8/0x15495AC (0x2C bytes: from, to, colour, 0); and the
// client's lines (0x40F4E0).

// A debug polygon (EBX the count, stack the points, EAX the colour): unless
// the points or the polygons would pass their limits, both blocks made
// when either is missing, the polygon (its first point the point count),
// then the points copied a dword at a time and counted.
void f_004AD630(Ctx& c) {
  const uint32_t e = c.esp, n = c.ebx, colour = c.eax;
  if (int32_t(U32(0x1549578) + n) > I32(0x1549574) || int32_t(U32(0x1549584) + 1) > I32(0x1549580)) {
    c.esp = e + 4;
    return;
  }
  if (!U32(0x1549588) || !U32(0x154957C)) {
    c.esp = e - 4;
    Push(c, U32(0x1549580) * 24);
    Call(c, f_0042A480, 0x4AD67E);
    Put(0x1549588, c.eax);
    Push(c, U32(0x1549574) * 12);
    Call(c, f_0042A480, 0x4AD694);
    Put(0x154957C, c.eax);
  }
  auto poly = [](uint32_t o, uint32_t v) { Put(U32(0x1549588) + U32(0x1549584) * 24 + o, v); };
  poly(0x10, U32(0x1549578));
  poly(0x14, n);
  for (uint32_t o = 0; o < 0x10; o += 4) poly(o, U32(colour + o));
  const uint32_t to = U32(0x154957C) + U32(0x1549578) * 12, from = Arg(c, e, 1);
  Put(0x1549584, U32(0x1549584) + 1);
  for (uint32_t o = 0; o < n * 12; o += 4) Put(to + o, U32(from + o));
  Put(0x1549578, U32(0x1549578) + n);
  c.esp = e + 4;
}

// A debug line (EBX from, EDI to, ESI the colour) unless the lines would
// pass their limit; the block made when missing.
void f_004AD760(Ctx& c) {
  const uint32_t e = c.esp;
  int32_t n = I32(0x15495A8);
  if (int32_t(uint32_t(n) + 1) > I32(0x15495A4)) {
    c.esp = e + 4;
    return;
  }
  uint32_t lines = U32(0x15495AC);
  if (!lines) {
    Push(c, U32(0x15495A4) * 0x2C);
    Call(c, f_0042A480, 0x4AD785);
    lines = c.eax;
    n = I32(0x15495A8);
    Put(0x15495AC, lines);
  }
  const uint32_t line = uint32_t(n) * 0x2C + lines;
  for (uint32_t o = 0; o < 0xC; o += 4) Put(line + o, U32(c.ebx + o));
  for (uint32_t o = 0; o < 0xC; o += 4) Put(line + 0xC + o, U32(c.edi + o));
  for (uint32_t o = 0; o < 0x10; o += 4) Put(line + 0x18 + o, U32(c.esi + o));
  Put(line + 0x28, 0u);
  Put(0x15495A8, U32(0x15495A8) + 1);
  c.esp = e + 4;
}

// A debug box (ECX mins, EDX maxs, EAX the colour): its eight corners (bit
// 0 x, 1 y, 2 z from the maxs, else the mins; through the x87, an SNaN
// quietened) in the frame, and its twelve edges (the corner pairs of the
// table 0x536578) as lines.
void f_004AD7E0(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, ebp = c.ebp, esi = c.esi, edi = c.edi;
  const uint32_t mins = c.ecx, maxs = c.edx, corners = e - 0x60;
  for (uint32_t i = 0; i < 8; ++i)
    for (uint32_t k = 0; k < 3; ++k)
      Put(corners + 12 * i + 4 * k, Fst(St(Ld((i >> k & 1 ? maxs : mins) + 4 * k))));
  c.esi = c.eax;
  for (uint32_t k = 0; k < 0x60; k += 8) {
    c.esp = e - 0x70;
    c.ebx = corners + 12 * U32(0x536578 + k);
    c.edi = corners + 12 * U32(0x53657C + k);
    Call(c, f_004AD760, 0x4AD84F);
  }
  c.ebx = ebx;
  c.ebp = ebp;
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// Debug text at a point (EAX the point, EDI the colour; stack the text):
// its scale the direction from the view origin 0x1536030 (stored,
// normalised by 0x426710) along the view's forward axis 0x153603C (z + y +
// x) less 0.995, times the distance, stored, at least 1; queued by 0x4AD860
// (EBX the point).
void f_004AD8F0(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, esi = c.esi, origin = c.eax, dir = e - 0xC;
  for (uint32_t o = 0; o < 0xC; o += 4) Put(dir + o, Fst(St(Ld(origin + o)) - Ld(0x1536030 + o)));
  c.esp = e - 0x18;
  c.ebx = origin;
  c.esi = dir;
  Call(c, f_00426710, 0x4AD926);
  const St len(std::bit_cast<double>(x87::StoreF64(c.fpu)));
  x87::Pop(c.fpu);
  F32 scale = Fst(
      ((St(Ld(0x1536044)) * Ld(dir + 8) + St(Ld(0x1536040)) * Ld(dir + 4)) + St(Ld(0x153603C)) * Ld(dir) - 0.995f) *
      len);
  if (St(scale) < 1.0f) scale = F32{0x3F800000u};
  Push(c, Arg(c, e, 1));
  Push(c, scale.bits);
  Call(c, f_004AD860, 0x4AD97E);
  c.ebx = ebx;
  c.esi = esi;
  c.esp = e + 4;
}

// Queues debug text (EBX the point, EDI the colour; stack the scale and
// the text) unless the count 0x1549590 would pass the limit 0x154958C: the
// block 0x1549594 made when missing (0x42A480, the limit x 128 bytes), the
// entry (128 bytes: the point, the colour, the scale, then the text by
// strncpy, 0x5F characters at most, and a 0 at +0x7F) and the count.
void f_004AD860(Ctx& c) {
  const uint32_t e = c.esp, limit = U32(0x154958C);
  uint32_t n = U32(0x1549590);
  if (int32_t(n + 1) <= int32_t(limit)) {
    uint32_t block = U32(0x1549594);
    if (!block) {
      c.esp = e - 4;
      Push(c, limit << 7);
      Call(c, f_0042A480, 0x4AD886);
      n = U32(0x1549590);
      Put(0x1549594, block = c.eax);
    }
    const uint32_t q = (n << 7) + block;
    for (uint32_t o = 0; o < 0xC; o += 4) Put(q + o, U32(c.ebx + o));
    for (uint32_t o = 0; o < 0x10; o += 4) Put(q + 0xC + o, U32(c.edi + o));
    const uint32_t scale = Arg(c, e, 1), text = Arg(c, e, 2);
    c.esp = e - 4;
    Push(c, 0x5F);
    Push(c, text);
    Push(c, q + 0x20);
    Put(q + 0x1C, scale);
    Call(c, f_0051AA00, 0x4AD8D7);
    Wr<uint8_t>(g_guest_base, q + 0x7F, 0);
    Put(0x1549590, U32(0x1549590) + 1);
  }
  c.esp = e + 4;
}

// The client's debug line (stack from, a value kept with it, a dword and a
// flag; ESI the colour, EDI to): with 0x1439370 set, the limit 0x1439460
// set to 0x1000 and unless the count 0x1439464 would pass it: the blocks
// made when missing (0x2C000 bytes of lines, a byte and a dword per line;
// the count then 0), the line (from, to, colour, the value), its byte
// (0x1439474, 2 without the flag) and dword.
void f_0040F4E0(Ctx& c) {
  const uint32_t e = c.esp, from = Arg(c, e, 1);
  [&] {
    if (!U32(0x1439370)) return;
    int32_t n = I32(0x1439464);
    Put(0x1439460, 0x1000u);
    if (int32_t(uint32_t(n) + 1) > 0x1000) return;
    const uint8_t flag = Arg(c, e, 4) ? U8(0x1439474) : 2;
    if (!U32(0x1439468)) {
      c.esp = e - 8;
      Push(c, 0x2C000);
      Call(c, f_0042A480, 0x40F535);
      Push(c, U32(0x1439460));
      Put(0x1439468, c.eax);
      Call(c, f_0042A480, 0x40F546);
      Put(0x143946C, c.eax);
      Push(c, U32(0x1439460) * 4);
      Call(c, f_0042A480, 0x40F55D);
      Put(0x1439470, c.eax);
      n = 0;
      Put(0x1439464, 0u);
    }
    const uint32_t line = uint32_t(n) * 0x2C + U32(0x1439468);
    for (uint32_t o = 0; o < 0xC; o += 4) Put(line + o, U32(from + o));
    for (uint32_t o = 0; o < 0xC; o += 4) Put(line + 0xC + o, U32(c.edi + o));
    for (uint32_t o = 0; o < 0x10; o += 4) Put(line + 0x18 + o, U32(c.esi + o));
    Put(line + 0x28, Arg(c, e, 2));
    Wr<uint8_t>(g_guest_base, U32(0x143946C) + U32(0x1439464), flag);
    Put(U32(0x1439470) + 4 * U32(0x1439464), Arg(c, e, 3));
    Put(0x1439464, U32(0x1439464) + 1);
  }();
  c.esp = e + 4;
}

// StatMon_UpdateEntry (its error "StatMon_UpdateEntry: invalid entry '%i'"),
// the client's warning icons (ECX the entry, EAX how long in ms; stack its
// image's name), with the cvar 0x14E43EC on: an entry outside 0-63 raises
// the error routine 0x42E660 (code 1: it longjmps in game, and the rest
// runs on here after a stub); the time base 0x15342A0 taken from
// timeGetTime the first time (0x13F9DB0 then set); the entry's end
// timeGetTime + EAX - base at 0x8D5898 + 8 x entry; its image (+4)
// registered through the client's refexport 0x1426D74 (name, 1) when
// missing and 0x1439370 is set; the entries in use 0x8D5A98 at least the
// entry + 1.
void f_00447CA0(Ctx& c) {
  const uint32_t e = c.esp, n = c.ecx, time = c.eax;
  [&] {
    if (!CvarInt(0x14E43EC)) return;
    if (n >= 0x40) {
      c.esp = e - 8;
      Push(c, n);
      Push(c, 0x553464);
      Push(c, 1);
      Call(c, f_0042E660, 0x447CCC);
    }
    const uint32_t clock = U32(0x53331C);
    c.esp = e - 0xC;
    if (!U32(0x13F9DB0)) {
      CallPtr(c, clock, 0x447CE1, 0x447CDF);
      Put(0x15342A0, c.eax);
      Put(0x13F9DB0, 1u);
    }
    c.esp = e - 0xC;
    CallPtr(c, clock, 0x447CF2, 0x447CF0);
    Put(0x8D5898 + n * 8, c.eax + (time - U32(0x15342A0)));
    if (!U32(0x8D589C + n * 8) && U32(0x1439370)) {
      c.esp = e - 8;
      Push(c, 1);
      Push(c, Arg(c, e, 1));
      CallPtr(c, U32(0x1426D74), 0x447D23, 0x447D1D);
      Put(0x8D589C + n * 8, c.eax);
    }
    if (int32_t(n) >= I32(0x8D5A98)) Put(0x8D5A98, n + 1);
  }();
  c.esp = e + 4;
}

// A debug draw of the light grid's corners near the view with the cvar
// 0x15346E4 above 0 and the record 0xC952B0 on: for the view's corner
// (FISTP of the stored distances from -131072, x and y + 2^-30 over 32, z
// less 0.5 - 2^-30 over 64) and those up to the cvar away in x and y and
// one in z, each inside the grid (0 to 0x80000 units) and not in the
// sorted list (0x4AC7E0 below 0) nor culled (0x4C9070 of the point, radius
// 0), a "." at it (0x4AD860, scale 1; 0x537850 for the layer below, else
// 0x537820).
void f_004AC890(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, ebp = c.ebp, esi = c.esi, edi = c.edi, point = e - 0xC;
  [&] {
    if (!U32(0xC952B0)) return;
    const int32_t r = CvarInt(0x15346E4);
    if (r <= 0) return;
    auto stored = [](uint32_t va) { return St(Fst(St(Ld(va)) - -131072.0f)); };
    const int32_t cx = Fistp(stored(0x1536030) + St(0x1p-30)) >> 5, cy = Fistp(stored(0x1536034) + St(0x1p-30)) >> 5;
    const int32_t cz = Fistp(stored(0x1536038) - St(std::bit_cast<double>(0x3FDFFFFFFF000000ull))) >> 6;
    const uint32_t lo = 0u - uint32_t(r), count = uint32_t(r) - lo + 1;
    for (int32_t dz = -1; dz <= 1; ++dz) {
      const uint32_t z = uint32_t(dz) + uint32_t(cz);
      if (int32_t(z) < 0 || int32_t(z << 6) > 0x80000) continue;
      uint32_t y = lo + uint32_t(cy), ny = count;
      do {
        if (int32_t(y) >= 0 && int32_t(y << 5) <= 0x80000) {
          uint32_t x = lo + uint32_t(cx), xs = (x - 0x1000) << 5, nx = count;
          do {
            if (int32_t(x) >= 0 && int32_t(x << 5) <= 0x80000) {
              c.esp = e - 0x54;
              Push(c, 0);
              Push(c, y);
              Push(c, x);
              c.eax = z;
              Call(c, f_004AC7E0, 0x4ACA3A);
              if (int32_t(c.eax) < 0) {
                Put(point, Fst(FromInt(int32_t(xs))));
                Put(point + 4, Fst(FromInt(int32_t((y - 0x1000) << 5))));
                Put(point + 8, Fst(FromInt(int32_t((z - 0x800) << 6))));
                c.esp = e - 0x54;
                Push(c, 0);
                c.edx = point;
                Call(c, f_004C9070, 0x4ACA85);
                if (!c.eax) {
                  c.esp = e - 0x54;
                  Push(c, 0x55504C);
                  Push(c, 0x3F800000);
                  c.ebx = point;
                  c.edi = dz == -1 ? 0x537850 : 0x537820;
                  Call(c, f_004AD860, 0x4ACAB6);
                }
              }
            }
            xs += 0x20;
            ++x;
          } while (--nx);
        }
        ++y;
      } while (--ny);
    }
  }();
  c.ebx = ebx;
  c.ebp = ebp;
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// A grid corner in the sorted list 0xC952B4 (count 0xC952BC) of 8-byte
// entries (x and y as 16 bits, z as 16 bits and two bytes the caller's
// stack held, never read): EAX z; stack x, y and whether to add it (0 no,
// 1 when missing, 2 also when found, before the one found); EAX its index,
// or -1 (missing and not added, or the list full). The key is made in the
// frame (x and y at e - 8, z at e - 4; e - 2 untouched) and searched for
// by halves (lo + hi) >> 1 from 0 to the count less 1: the dword's
// difference (wrapping), when 0 z's, its sign deciding. An addition is
// 0x4AC780 at lo (or at the one found).
void f_004AC7E0(Ctx& c) {
  const uint32_t e = c.esp, esi = c.esi, edi = c.edi, key = e - 8, z = c.eax;
  Wr<uint16_t>(g_guest_base, e - 6, uint16_t(Arg(c, e, 2)));
  int32_t hi = int32_t(U32(0xC952BC) - 1), lo = 0;
  Wr<uint16_t>(g_guest_base, key, uint16_t(Arg(c, e, 1)));
  Wr<uint16_t>(g_guest_base, e - 4, uint16_t(z));
  c.esp = e - 0x18;
  c.edi = key;
  c.eax = [&]() -> uint32_t {
    if (hi >= 0) {
      const uint32_t list = U32(0xC952B4);
      do {
        const uint32_t mid = uint32_t(int32_t(uint32_t(lo) + uint32_t(hi)) >> 1);
        int32_t d = int32_t(U32(key) - U32(list + mid * 8));
        if (!d && !(d = int32_t((z & 0xFFFF) - U16(list + mid * 8 + 4)))) {
          if (Arg(c, e, 3) == 2) {
            c.esi = mid;
            Call(c, f_004AC780, 0x4AC870);
          }
          return mid;
        }
        if (d < 0)
          hi = int32_t(mid) - 1;
        else
          lo = int32_t(mid) + 1;
      } while (lo <= hi);
    }
    if (!Arg(c, e, 3)) return 0xFFFFFFFFu;
    c.esi = uint32_t(lo);
    Call(c, f_004AC780, 0x4AC852);
    return c.eax ? uint32_t(lo) : 0xFFFFFFFFu;
  }();
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// An entry (EDI, 8 bytes) inserted into the sorted list at ESI unless the
// count 0xC952BC is 0x400000 or more (EAX 0): the entries from ESI moved up
// one (memmove 0x51ACF0, (count - ESI) x 8 bytes), the entry copied, the
// count incremented (EAX 1).
void f_004AC780(Ctx& c) {
  const uint32_t e = c.esp, at = c.esi, entry = c.edi, n = U32(0xC952BC);
  c.eax = 0;
  if (int32_t(n) < 0x400000) {
    const uint32_t from = U32(0xC952B4) + at * 8;
    Push(c, (n - at) << 3);
    Push(c, from);
    Push(c, from + 8);
    Call(c, f_0051ACF0, 0x4AC7A8);
    const uint32_t lo = U32(entry);
    Put(U32(0xC952B4) + at * 8, lo);
    const uint32_t hi = U32(entry + 4);
    Put(U32(0xC952B4) + at * 8 + 4, hi);
    Put(0xC952BC, U32(0xC952BC) + 1);
    c.eax = 1;
  }
  c.esi = at;
  c.edi = entry;
  c.esp = e + 4;
}

// The composite models' debug draws by the string of r_xdebug (0x15346D4;
// EBX the entity, EDI passed on): "boxes" or "both" the bones' boxes
// (0x506310), "axes" or "both" their axes (0x5065E0), compared by _stricmp;
// anything else sets r_xdebug to "" and prints the three choices.
void f_005068B0(Ctx& c) {
  const uint32_t e = c.esp;
  auto is = [&](uint32_t name, uint32_t ret) {
    c.esp = e - 4;
    Push(c, name);
    Push(c, U32(U32(0x15346D4) + 4));
    Call(c, f_0052AB6F, ret);
    c.esp = e - 4;
    return c.eax == 0;
  };
  bool boxes = false;
  if (is(0x539278, 0x5068C6) || is(0x539270, 0x5068E1)) {
    Push(c, c.edi);
    Push(c, c.ebx);
    boxes = true;
    Call(c, f_00506310, 0x5068F4);
  }
  if (is(0x539268, 0x50690B) || is(0x539270, 0x506925)) {
    Push(c, c.edi);
    Push(c, c.ebx);
    Call(c, f_005065E0, 0x506933);
  } else if (!boxes) {
    Push(c, 0x54ED00);
    Push(c, 0x54D79C);
    c.eax = 1;
    Call(c, f_00431CF0, 0x506950);
    for (uint32_t s : {0x539248u, 0x539234u, 0x53920Cu}) {
      Push(c, s);
      Call(c, f_0042E300, s == 0x539248 ? 0x50695A : s == 0x539234 ? 0x506964 : 0x50696E);
    }
  }
  c.esp = e + 4;
}

// The numbers shown at points (0x4ADB10's) drawn and aged (0x4AD480): each
// whose time (0x1536360 less its +0x20, signed) is past its duration or
// negative is replaced by the last (rep movs, the count less 1, the same
// index next); the others get an alpha (+0x18) of 1, 2 - 2t/d over their
// second half, and are queued as text (0x4AD860, scale 0.5, va("%i")) at
// their point moved by 4 sin(t x 0x55B890 + index) (FSIN on the x87
// model) along 0x1536348 and up by t x 0.064, in the frame at e - 0xC.
void f_004AD480(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, ebp = c.ebp, esi = c.esi, edi = c.edi, pt = e - 0xC;
  if (I32(0x15495B8) > 0) {
    uint32_t i = 0, off = 0;
    do {
      const uint32_t entry = U32(0x15495C0) + off;
      const int32_t t = int32_t(U32(kTime) - U32(entry + 0x20));
      if (t < 0 || t > I32(entry + 0x24)) {
        const uint32_t n = U32(0x15495B8) - 1;
        Put(0x15495B8, n);
        MovsD(entry, U32(0x15495C0) + n * 40, 10);
        continue;
      }
      Put(entry + 0x18, 0x3F800000u);
      const uint32_t p = U32(0x15495C0) + off;
      if (const int32_t d = I32(p + 0x24); int32_t(uint32_t(t) * 2) > d)
        Put(p + 0x18, Fst(Ld(0x55B624) - (FromInt(t) + FromInt(t)) / FromInt(d)));
      const St x = St(Ld(0x55B890)) * FromInt(t) + FromInt(int32_t(i));
      x87::LoadF64(c.fpu, std::bit_cast<uint64_t>(x.v));
      x87::Sin(c.fpu);
      x87::ArithF32(c.fpu, x87::Op::Mul, Ld(0x55B800).bits);
      const St w(std::bit_cast<double>(x87::StoreF64(c.fpu)));
      x87::Pop(c.fpu);
      Put(pt, Fst(St(Ld(0x1536348)) * w + Ld(p)));
      Put(pt + 4, Fst(St(Ld(0x153634C)) * w + Ld(p + 4)));
      Put(pt + 8, Fst(St(Ld(0x1536350)) * w + Ld(p + 8) + FromInt(t) * Ld(0x55B88C)));
      c.esp = e - 0x28;
      Push(c, U32(p + 0x1C));
      Push(c, 0x5560E0);
      Call(c, f_00440940, 0x4AD55F);
      Push(c, c.eax);
      Push(c, 0x3F000000);
      c.edi = U32(0x15495C0) + off + 0xC;
      c.ebx = pt;
      Call(c, f_004AD860, 0x4AD577);
      i++;
      off += 40;
    } while (int32_t(i) < I32(0x15495B8));
  }
  c.ebx = ebx;
  c.ebp = ebp;
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

namespace {

// A GL entry point called through its qgl pointer (stdcall) with the
// arguments pushed in the order given, from the call site returning to ret.
void Gl(Ctx& c, uint32_t ptr, std::initializer_list<uint32_t> args, uint32_t ret) {
  for (uint32_t v : args) Push(c, v);
  CallPtr(c, U32(ptr), ret, ret - 6);
}

// The view's matrices loaded (glLoadMatrixf, glMatrixMode), from the call
// sites returning to rets.
void ViewMatrices(Ctx& c, const uint32_t rets[4]) {
  Gl(c, 0x1534D00, {0x15360E8}, rets[0]);
  Gl(c, 0x1534D6C, {0x1701}, rets[1]);
  Gl(c, 0x1534D00, {0x1536178}, rets[2]);
  Gl(c, 0x1534D6C, {0x1700}, rets[3]);
}

// The fog enabled again after a debug draw (glEnable from the call site
// returning to ret): unless 0x1549B50 or the fog's state bit 0x200000
// (0x1534630) is set, with r_fog (0x15347F4) on and a fog (0x1535890 for
// 0x15496EC bit 8, else 0x13F9CF8).
void FogAgain(Ctx& c, uint32_t ret) {
  if (U32(0x1549B50) || U32(0x1534630) & 0x200000 || !CvarInt(0x15347F4)) return;
  if (!U32(U8(0x15496EC) & 8 ? 0x1535890 : 0x13F9CF8)) return;
  Gl(c, 0x1534F68, {0xB60}, ret);
  Put(0x1534630, U32(0x1534630) | 0x200000);
}

}  // namespace

// The debug polygons drawn (0x4ACB20), when there are any (0x1549584): the
// client arrays to plain ones (0x153465C 3) as the immediate-mode batch's
// begin sets them; the white image bound (GL_Bind 0x1535AC4), the client
// arrays 0x400 (0x4CBBE0), the points as the vertices, the view's
// matrices; the fog off (GL_State's bit 0x200000), the points locked
// (0x1534DDC) and culling off (0x153462C 2); each polygon (24 bytes from
// 0x1549588) as GL_POLYGON (state 0x165, its colour through 0x15357B8),
// then again in 0x1535114 at state 0x1100 and depth range 0..0; the counts
// cleared, the points unlocked (0x1534C2C) and the fog back.
void f_004ACB20(Ctx& c) {
  const uint32_t e = c.esp, esi = c.esi, edi = c.edi;
  c.eax = U32(0x1549584);
  if (!c.eax) {
    c.esp = e + 4;
    return;
  }
  c.esp = e - 8;
  if (U32(0x153465C) != 3) {
    if (const uint32_t range = U32(0x153479C)) {
      Gl(c, 0x153495C, {range == 1 ? 0x851Du : 0x8533u}, 0x4ACB61);
      Gl(c, 0x15354F0, {0x1630C40, 0, 0x1406, U32(0x17F0C60)}, 0x4ACB79);
      Gl(c, 0x153503C, {0x1698C40, 0, 0x1406}, 0x4ACB8B);
      Gl(c, 0x15349A0, {U32(0x16E8C40), 0, 0x1406, 2}, 0x4ACBA1);
      Gl(c, 0x153542C, {0x16E0C40, 0, 0x1401, 4}, 0x4ACBB5);
    } else if (U32(0x15347B8)) {
      Gl(c, 0x15354F0, {0x1630C40, 0, 0x1406, U32(0x17F0C60)}, 0x4ACBD9);
      Gl(c, 0x153503C, {0x1698C40, 0xC, 0x1406}, 0x4ACBEB);
      Gl(c, 0x15349A0, {U32(0x16E8C40), 0, 0x1406, 2}, 0x4ACC00);
    }
    Put(0x153465C, 3u);
  }
  c.edi = 3;
  c.esi = U32(0x1535AC4);
  Call(c, f_004CB3B0, 0x4ACC11);
  Push(c, 0x400);
  Call(c, f_004CBBE0, 0x4ACC1B);
  c.esp = e - 8;
  Gl(c, 0x15354F0, {U32(0x154957C), 0, 0x1406, 3}, 0x4ACC33);
  static constexpr uint32_t kRets[4] = {0x4ACC3E, 0x4ACC49, 0x4ACC54, 0x4ACC5F};
  ViewMatrices(c, kRets);
  c.esi = 0x200000;
  if (U32(0x1534630) & 0x200000) {
    Gl(c, 0x15349E0, {0xB60}, 0x4ACC78);
    Put(0x1534630, U32(0x1534630) & 0xFFDFFFFF);
  }
  if (const uint32_t lock = U32(0x1534DDC)) {
    Push(c, U32(0x1549578));
    Push(c, 0);
    CallPtr(c, lock, 0x4ACC96, 0x4ACC94);
  }
  if (U32(0x153462C) != 2) {
    Gl(c, 0x15349E0, {0xB44}, 0x4ACCAA);
    Put(0x153462C, 2u);
  }
  for (int32_t i = 0; i < I32(0x1549584); i++) {
    const uint32_t o = 0x18 * uint32_t(i);
    c.edi = uint32_t(i);
    c.esi = o;
    c.eax = 0x165;
    Call(c, f_004CB710, 0x4ACCCF);
    Gl(c, 0x15357B8, {U32(0x1549588) + o}, 0x4ACCDE);
    Gl(c, 0x153501C, {U32(U32(0x1549588) + o + 0x14), U32(U32(0x1549588) + o + 0x10), 9}, 0x4ACCF5);
    c.eax = 0x1100;
    Call(c, f_004CB710, 0x4ACCFF);
    Gl(c, 0x153539C, {0, 0, 0, 0}, 0x4ACD0D);
    Gl(c, 0x1535114, {U32(0x1549588) + o}, 0x4ACD1D);
    Gl(c, 0x153501C, {U32(U32(0x1549588) + o + 0x14), U32(U32(0x1549588) + o + 0x10), 9}, 0x4ACD34);
    Gl(c, 0x153539C, {0x3FF00000, 0, 0, 0}, 0x4ACD45);
  }
  c.esi = 0x200000;
  const uint32_t unlock = U32(0x1534C2C);
  Put(0x1549584, 0u);
  Put(0x1549578, 0u);
  if (unlock) CallPtr(c, unlock, 0x4ACD7A, 0x4ACD78);
  FogAgain(c, 0x4ACDB9);
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// Debug lines drawn (0x4ACDD0; EAX the count, stack the lines, 0x2C bytes:
// from, to, colour, a flag), none for a count of 0: the white image bound,
// the immediate-mode batch begun (0x4ADC20), the view's matrices; each line
// at state 0x100 (with the flag) or 0x10100, the batch's mode 1 (lines)
// and colour bytes the components x 255 truncated (_ftol, the low byte),
// its two ends (0x4ADEC0) drawn (glDrawArrays of the batch's vertices, its
// count and mode then 0); the batch ended (0x15495C4 0), the fog back and
// the tessellator's positions as the vertices again.
void f_004ACDD0(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, esi = c.esi, edi = c.edi, n = c.eax;
  if (n) {
    c.esp = e - 0xC;
    c.edi = n;
    c.ebx = 0;
    c.esi = U32(0x1535AC4);
    Call(c, f_004CB3B0, 0x4ACDEA);
    Call(c, f_004ADC20, 0x4ACDEF);
    static constexpr uint32_t kRets[4] = {0x4ACDFA, 0x4ACE05, 0x4ACE10, 0x4ACE1B};
    ViewMatrices(c, kRets);
    if (int32_t(n) > 0) {
      for (uint32_t left = n, line = Arg(c, e, 1); left; left--, line += 0x2C) {
        c.edi = left;
        c.esi = line + 0x20;
        c.eax = U32(line + 0x28) ? 0x100 : 0x10100;
        Call(c, f_004CB710, 0x4ACE46);
        Put(0x15495C8, 1u);
        for (uint32_t k = 0; k < 4; k++)
          Wr<uint8_t>(g_guest_base, 0x15495D8 + k, uint8_t(Ftol(St(Ld(line + 0x18 + 4 * k)) * Ld(0x55B5FC))));
        for (uint32_t k : {8u, 4u, 0u}) Push(c, U32(line + k));
        Call(c, f_004ADEC0, 0x4ACEAC);
        for (uint32_t k : {0x14u, 0x10u, 0xCu}) Push(c, U32(line + k));
        Call(c, f_004ADEC0, 0x4ACEBD);
        c.esp = e - 0xC;
        Gl(c, 0x153501C, {U32(0x15495E8), 0, U32(0x15495C8)}, 0x4ACED4);
        Put(0x15495E8, 0u);
        Put(0x15495C8, 0u);
      }
    }
    Put(0x15495C4, 0u);
    c.esi = 0x200000;
    FogAgain(c, 0x4ACF33);
    Gl(c, 0x15354F0, {0x1630C40, 0, 0x1406, 3}, 0x4ACF4C);
  }
  c.ebx = ebx;
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// The back end's debug draws (0x4AD5C0, from RB_SwapBuffers): the numbers
// at points (0x4AD480), the polygons (0x4ACB20), the lines (0x4ACDD0) and
// the client's (0x15495B0/0x15495B4), the text (0x4ACF50) and the
// client's (0x154959C/0x15495A0); the line and text counts then 0.
void f_004AD5C0(Ctx& c) {
  const uint32_t e = c.esp;
  Call(c, f_004AD480, 0x4AD5C5);
  Call(c, f_004ACB20, 0x4AD5CA);
  Push(c, U32(0x15495AC));
  c.eax = U32(0x15495A8);
  Call(c, f_004ACDD0, 0x4AD5DA);
  c.ecx = U32(0x15495B4);
  c.eax = U32(0x15495B0);
  Push(c, c.ecx);
  Call(c, f_004ACDD0, 0x4AD5EB);
  c.edx = U32(0x1549594);
  c.eax = U32(0x1549590);
  Push(c, c.edx);
  Put(0x15495A8, 0u);
  Call(c, f_004ACF50, 0x4AD606);
  Push(c, U32(0x15495A0));
  c.eax = U32(0x154959C);
  Call(c, f_004ACF50, 0x4AD616);
  Put(0x1549590, 0u);
  c.esp = e + 4;
}

// ---- the front end's vector helpers ----------------------------------------
//
// Called throughout the game (collision, game code, the back end), so
// beyond the conventions above: the registers each original leaves
// unchanged stay unchanged (no caller reads those it changes; each call
// site checked), EAX and ST0 results are the original's, and the x87 stack
// below is untouched.

// VectorNormalize (ESI): the squares of the vector summed x + y + z and
// stored, the CRT's square root of that stored as the length (pushed on
// the x87 stack); unless it is 0 each component times 1 / length stored.
void f_00426710(Ctx& c) {
  const uint32_t e = c.esp, v = c.esi;
  const F32 x = Ld(v), y = Ld(v + 4), z = Ld(v + 8);
  const F32 sum = Fst((St(x) * x + St(y) * y) + St(z) * z);
  x87::LoadF32(c.fpu, sum.bits);
  c.esp = e - 0xC;
  Call(c, f_0052E610, 0x426750);
  const F32 len{PopF32(c)};
  if (!(St(len) == 0.0f)) {
    const F32 inv = Fst(St(1.0f) / St(len));
    for (uint32_t k = 0; k < 0xC; k += 4) Put(v + k, Fst(St(inv) * Ld(v + k)));
  }
  x87::LoadF32(c.fpu, len.bits);
  c.esp = e + 4;
}

// Normalises the vector at EAX (EAX unchanged): its squares summed x + y +
// z and stored, the inverse square root by one Newton step from the bits
// 0x5F3759DF less half the sum's (each value stored), times each component.
void f_004268B0(Ctx& c) {
  const uint32_t v = c.eax;
  const F32 x = Ld(v), y = Ld(v + 4), z = Ld(v + 8);
  const F32 sum = Fst((St(x) * x + St(y) * y) + St(z) * z);
  const F32 half = Fst(St(sum) * 0.5f), i{0x5F3759DFu - uint32_t(int32_t(sum.bits) >> 1)};
  const F32 r = Fst((1.5f - St(i) * i * half) * i);
  for (uint32_t k = 0; k < 0xC; k += 4) Put(v + k, Fst(St(r) * Ld(v + k)));
  c.esp += 4;
}

// A vector perpendicular to the unit vector at EBX into EDX: the axis of
// its component of least magnitude (the first of equal ones; |x| compared
// as a double against 1 and the least so far stored), projected onto the
// plane of EBX (0x429710) and normalised (0x426710), in an 8-aligned frame.
void f_004273C0(Ctx& c) {
  const uint32_t e = c.esp, ebp = c.ebp, esi = c.esi, edi = c.edi, src = c.ebx;
  const uint32_t a = (e - 4) & ~7u, s = a - 0x20, axis = a - 0x10;
  F32 least{0x3F800000u};
  uint32_t pos = 0;
  for (uint32_t i = 0; i < 3; ++i) {
    const St m = Abs(St(Ld(src + 4 * i)));
    if (St(least) > m) {
      pos = i;
      least = Fst(m);
    }
  }
  for (uint32_t k = 0; k < 0xC; k += 4) Put(axis + k, 0u);
  Put(axis + 4 * pos, 0x3F800000u);
  c.esp = s;
  c.eax = src;
  c.ecx = axis;
  Call(c, f_00429710, 0x427427);
  c.esi = c.edx;
  Call(c, f_00426710, 0x42742E);
  x87::Pop(c.fpu);
  c.ebp = ebp;
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// Rotates a point about the unit vector EAX by degrees (stack: out, the
// point, the degrees): the frame (a perpendicular r (0x4273C0), the cross
// product u of r and the vector f, f) as the columns of a matrix m, its
// transpose im, and the rotation about z by the angle (deg x pi / 180,
// stored, through FSINCOS on the x87 model): m x z (0x427590), then by im,
// times the point (each row's sum (1, 2) + 0 or (1, 0) + 2).
void f_00426AB0(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, ebp = c.ebp, esi = c.esi, edi = c.edi, s = e - 0xD0;
  const uint32_t dir = c.eax, out = Arg(c, e, 1), p = Arg(c, e, 2);
  const uint32_t f = s + 0x10, r = s + 0x1C, u = s + 0x28, m = s + 0x34, z = s + 0x64, im = s + 0x88, t = s + 0xAC;
  for (uint32_t k = 0; k < 0xC; k += 4) Put(f + k, U32(dir + k));
  c.esp = s;
  c.ebx = dir;
  c.edx = r;
  Call(c, f_004273C0, 0x426AE0);
  Put(u, Fst(St(Ld(r + 4)) * Ld(f + 8) - St(Ld(r + 8)) * Ld(f + 4)));
  Put(u + 4, Fst(St(Ld(r + 8)) * Ld(f) - St(Ld(r)) * Ld(f + 8)));
  Put(u + 8, Fst(St(Ld(r)) * Ld(f + 4) - St(Ld(r + 4)) * Ld(f)));
  for (uint32_t row = 0; row < 3; ++row)
    for (uint32_t col = 0; col < 3; ++col) {
      const uint32_t v = U32((col == 0 ? r : col == 1 ? u : f) + 4 * row);
      Put(m + 0xC * row + 4 * col, v);
      Put(im + 0xC * col + 4 * row, v);
    }
  const F32 angle = Fst(St(Ld(e + 0xC)) * Ld(0x55B700) / 180.0f);
  for (uint32_t k = 0; k < 0x24; k += 4) Put(z + k, 0u);
  x87::LoadF32(c.fpu, angle.bits);
  x87::SinCos(c.fpu);
  Put(z, PopF32(c));
  Put(z + 4, PopF32(c));
  Put(z + 0xC, Fst(-St(Ld(z + 4))));
  Put(z + 0x10, U32(z));
  Put(z + 0x20, 0x3F800000u);
  c.esp = s;
  c.edx = t;
  c.eax = z;
  c.ecx = m;
  Call(c, f_00427590, 0x426C2E);
  c.edx = m;
  c.eax = im;
  c.ecx = t;
  Call(c, f_00427590, 0x426C45);
  Put(out, Fst((St(Ld(m + 8)) * Ld(p + 8) + St(Ld(m + 4)) * Ld(p + 4)) + St(Ld(m)) * Ld(p)));
  Put(out + 4, Fst((St(Ld(m + 0x10)) * Ld(p + 4) + St(Ld(m + 0xC)) * Ld(p)) + St(Ld(m + 0x14)) * Ld(p + 8)));
  Put(out + 8, Fst((St(Ld(m + 0x1C)) * Ld(p + 4) + St(Ld(m + 0x18)) * Ld(p)) + St(Ld(m + 0x20)) * Ld(p + 8)));
  c.ebx = ebx;
  c.ebp = ebp;
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// A 3x3 matrix's inverse (EAX in, ECX out; no register changed): the
// adjugate over the determinant (its first cofactor stored, the
// determinant expanded down the first column and stored, its inverse
// stored), each element read when the original reads it.
void f_00427CB0(Ctx& c) {
  const uint32_t a = c.eax, o = c.ecx;
  auto minor = [&](uint32_t i, uint32_t j, uint32_t k, uint32_t l) {
    return St(Ld(a + i)) * Ld(a + j) - St(Ld(a + k)) * Ld(a + l);
  };
  const F32 c0 = Fst(minor(0x10, 0x20, 0x1C, 0x14));
  const F32 det = Fst((St(c0) * Ld(a) - minor(4, 0x20, 0x1C, 8) * Ld(a + 0xC)) + minor(4, 0x14, 8, 0x10) * Ld(a + 0x18));
  const F32 inv = Fst(St(1.0f) / det);
  Put(o, Fst(St(c0) * inv));
  Put(o + 4, Fst(-(minor(4, 0x20, 0x1C, 8) * inv)));
  Put(o + 8, Fst(minor(4, 0x14, 8, 0x10) * inv));
  Put(o + 0xC, Fst(-(minor(0xC, 0x20, 0x18, 0x14) * inv)));
  Put(o + 0x10, Fst(minor(0, 0x20, 8, 0x18) * inv));
  Put(o + 0x14, Fst(-(minor(0, 0x14, 8, 0xC) * inv)));
  Put(o + 0x18, Fst(minor(0x1C, 0xC, 0x18, 0x10) * inv));
  Put(o + 0x1C, Fst(-(minor(0x1C, 0, 4, 0x18) * inv)));
  Put(o + 0x20, Fst(minor(0x10, 0, 4, 0xC) * inv));
  c.esp += 4;
}

// A 4x4 matrix's inverse (ECX in, EAX out; EAX unchanged) by cofactors:
// the input transposed into s first (the bits copied), the products of
// pairs of its last two rows (stored), the first two rows' cofactors,
// then those of the first two rows' pairs for the last two, each a
// difference of two sums of three products in the original's operand
// order; the determinant (s's first row by the out's first, its sum
// ((2, 3) + 0) + 1) stored, its inverse stored, times each of the 16.
void f_00427E80(Ctx& c) {
  const uint32_t in = c.ecx, o = c.eax;
  F32 s[16], t[12];
  for (uint32_t i = 0; i < 4; ++i)
    for (uint32_t j = 0; j < 4; ++j) s[4 * i + j] = Ld(in + 16 * j + 4 * i);
  // (a x b + c x d) + e x f, a, c and e on the stack.
  auto sum = [](F32 a, F32 b, F32 c, F32 d, F32 e, F32 f) { return (St(a) * b + St(c) * d) + St(e) * f; };
  auto put = [&](uint32_t k, St v) { Put(o + 4 * k, Fst(v)); };
  const int kPairs[12][2] = {{15, 10}, {14, 11}, {9, 15}, {13, 11}, {9, 14}, {13, 10},
                             {8, 15},  {12, 11}, {8, 14}, {12, 10}, {8, 13}, {12, 9}};
  for (int k = 0; k < 12; ++k) t[k] = Fst(St(s[kPairs[k][0]]) * s[kPairs[k][1]]);
  put(0, sum(s[7], t[4], s[6], t[3], s[5], t[0]) - sum(s[7], t[5], s[6], t[2], s[5], t[1]));
  put(1, sum(s[7], t[9], s[6], t[6], s[4], t[1]) - sum(s[7], t[8], s[6], t[7], s[4], t[0]));
  put(2, sum(s[7], t[10], s[5], t[7], s[4], t[2]) - sum(s[7], t[11], s[5], t[6], s[4], t[3]));
  put(3, sum(s[6], t[11], s[5], t[8], s[4], t[5]) - sum(s[6], t[10], s[5], t[9], s[4], t[4]));
  put(4, sum(s[3], t[5], s[2], t[2], s[1], t[1]) - sum(s[3], t[4], s[2], t[3], s[1], t[0]));
  put(5, sum(s[3], t[8], s[0], t[0], s[2], t[7]) - sum(s[3], t[9], s[0], t[1], s[2], t[6]));
  put(6, sum(s[3], t[11], s[0], t[3], s[1], t[6]) - sum(s[3], t[10], s[0], t[2], s[1], t[7]));
  put(7, sum(s[0], t[4], s[2], t[10], s[1], t[9]) - sum(s[0], t[5], s[2], t[11], s[1], t[8]));
  const int kPairs2[12][2] = {{2, 7}, {3, 6}, {1, 7}, {3, 5}, {1, 6}, {2, 5},
                              {0, 7}, {3, 4}, {0, 6}, {2, 4}, {0, 5}, {1, 4}};
  for (int k = 0; k < 12; ++k) t[k] = Fst(St(s[kPairs2[k][0]]) * s[kPairs2[k][1]]);
  put(8, sum(t[3], s[14], s[13], t[0], t[4], s[15]) - sum(s[13], t[1], t[2], s[14], t[5], s[15]));
  put(9, sum(s[12], t[1], t[6], s[14], t[9], s[15]) - sum(t[7], s[14], s[12], t[0], t[8], s[15]));
  put(10, sum(t[7], s[13], s[12], t[2], t[10], s[15]) - sum(s[12], t[3], t[6], s[13], t[11], s[15]));
  put(11, sum(s[12], t[5], t[8], s[13], t[11], s[14]) - sum(s[12], t[4], t[9], s[13], t[10], s[14]));
  put(12, sum(t[5], s[11], s[9], t[1], t[2], s[10]) - sum(t[4], s[11], s[9], t[0], t[3], s[10]));
  put(13, sum(t[8], s[11], s[8], t[0], t[7], s[10]) - sum(t[9], s[11], s[8], t[1], t[6], s[10]));
  put(14, sum(s[8], t[3], t[6], s[9], t[11], s[11]) - sum(t[7], s[9], s[8], t[2], t[10], s[11]));
  put(15, sum(s[8], t[4], t[9], s[9], t[10], s[10]) - sum(s[8], t[5], t[8], s[9], t[11], s[10]));
  const F32 det = Fst(((St(s[2]) * Ld(o + 8) + St(s[3]) * Ld(o + 0xC)) + St(s[0]) * Ld(o)) + St(s[1]) * Ld(o + 4));
  const F32 inv = Fst(St(1.0f) / det);
  for (uint32_t k = 0; k < 0x40; k += 4) Put(o + k, Fst(St(inv) * Ld(o + k)));
  c.esp += 4;
}

// Adds the point ECX to the bounds EDX (mins) and ESI (maxs; EDX
// unchanged): per axis a coordinate below the min or above the max (not
// for NaN) replaces it.
void f_00429070(Ctx& c) {
  const uint32_t p = c.ecx, mins = c.edx, maxs = c.esi;
  for (uint32_t k = 0; k < 0xC; k += 4) {
    if (St(Ld(p + k)) < Ld(mins + k)) Put(mins + k, U32(p + k));
    if (St(Ld(p + k)) > Ld(maxs + k)) Put(maxs + k, U32(p + k));
  }
  c.esp += 4;
}

// Adds the bounds EDX/EDI to the bounds ECX/ESI (ECX unchanged): per axis
// a min of the other's below or a max above (not for NaN) replaces it.
void f_004290E0(Ctx& c) {
  const uint32_t mins = c.ecx, maxs = c.esi, mins2 = c.edx, maxs2 = c.edi;
  for (uint32_t k = 0; k < 0xC; k += 4) {
    if (St(Ld(mins + k)) > Ld(mins2 + k)) Put(mins + k, U32(mins2 + k));
    if (St(Ld(maxs + k)) < Ld(maxs2 + k)) Put(maxs + k, U32(maxs2 + k));
  }
  c.esp += 4;
}

// BoxOnPlaneSide (stack: mins, maxs, the plane; EAX the sides): the
// self-filling table of the original's cases (0x885E40 set, its eight
// addresses after) written as it writes it; the corners nearest and
// farthest along the normal by its sign bits (+0x11; 8 and above trap,
// int3), their distances (x + y) + z against +0xC: 1 for a far corner not
// behind, plus 2 for a near one behind (or unordered).
void f_00429800(Ctx& c) {
  const uint32_t e = c.esp, mins = Arg(c, e, 1), maxs = Arg(c, e, 2), p = Arg(c, e, 3);
  if (U32(0x885E40) != 1) {
    Put(0x885E40, 1u);
    const uint32_t kCases[8] = {0x429888, 0x4298BB, 0x4298EE, 0x429921, 0x429954, 0x429987, 0x4299B7, 0x4299E7};
    for (uint32_t i = 0; i < 8; ++i) Put(0x885E44 + 4 * i, kCases[i]);
  }
  const uint32_t bits = U8(p + 0x11);
  if (bits >= 8) {
    c.esp = e - 4;
    Put(c.esp, c.ebx);
    Trap(c, 0x429A36u);
    return;
  }
  auto dist = [&](bool near) {
    auto corner = [&](uint32_t k) { return (bits >> k & 1) != near ? mins + 4 * k : maxs + 4 * k; };
    return (St(Ld(p)) * Ld(corner(0)) + St(Ld(p + 4)) * Ld(corner(1))) + St(Ld(p + 8)) * Ld(corner(2));
  };
  const St far = dist(false), near = dist(true);
  c.eax = (far >= Ld(p + 0xC) ? 1u : 0u) + (near >= Ld(p + 0xC) ? 0u : 2u);
  c.esp = e + 4;
}

// A plane through three points (EDI, ECX, EAX) into EDX: the normal the
// cross product of the third's and the second's differences from the
// first (stored), normalised by 0x426710; EAX 0 for a zero length, else 1
// with the distance (n.x p.x + p.y n.y) + n.z p.z at +0xC.
void f_00429650(Ctx& c) {
  const uint32_t e = c.esp, esi = c.esi, p0 = c.edi, p1 = c.ecx, p2 = c.eax, n = c.edx, s = e - 0x1C;
  F32 u[3], v[3];
  for (uint32_t k = 0; k < 3; ++k) u[k] = Fst(St(Ld(p1 + 4 * k)) - Ld(p0 + 4 * k));
  for (uint32_t k = 0; k < 3; ++k) v[k] = Fst(St(Ld(p2 + 4 * k)) - Ld(p0 + 4 * k));
  Put(n, Fst(St(v[1]) * u[2] - St(v[2]) * u[1]));
  Put(n + 4, Fst(St(v[2]) * u[0] - St(v[0]) * u[2]));
  Put(n + 8, Fst(St(v[0]) * u[1] - St(v[1]) * u[0]));
  c.esp = s;
  c.esi = n;
  Call(c, f_00426710, 0x4296D1);
  const St len(std::bit_cast<double>(x87::StoreF64(c.fpu)));
  x87::Pop(c.fpu);
  c.eax = 0;
  if (!(len == 0.0f)) {
    Put(n + 0xC, Fst((St(Ld(n)) * Ld(p0) + St(Ld(p0 + 4)) * Ld(n + 4)) + St(Ld(n + 8)) * Ld(p0 + 8)));
    c.eax = 1;
  }
  c.esi = esi;
  c.esp = e + 4;
}

// The squared distance between EAX and ECX, pushed on the x87 stack (no
// register changed): the differences stored, their squares summed z + y + x.
void f_00426610(Ctx& c) {
  const uint32_t a = c.eax, b = c.ecx;
  const F32 dx = Fst(St(Ld(a)) - Ld(b)), dy = Fst(St(Ld(a + 4)) - Ld(b + 4)), dz = Fst(St(Ld(a + 8)) - Ld(b + 8));
  const St d2 = (St(dz) * dz + St(dy) * dy) + St(dx) * dx;
  x87::LoadF64(c.fpu, std::bit_cast<uint64_t>(d2.v));
  c.esp += 4;
}

// The point ECX projected onto the plane through the origin of the normal
// EAX, into EDX (no register changed): 1 over the normal's squares (x + y)
// + z stored, the dot product (z + y) + x of the point and the normal times
// that stored, the normal times it stored, and each coordinate less its
// product with the dot; the normal read before the first write.
void f_00429710(Ctx& c) {
  const uint32_t n = c.eax, p = c.ecx, out = c.edx;
  const F32 x = Ld(n), y = Ld(n + 4), z = Ld(n + 8);
  const F32 inv = Fst(St(1.0f) / Fst((St(x) * x + St(y) * y) + St(z) * z));
  const F32 d = Fst(((St(Ld(p + 8)) * Ld(n + 8) + St(Ld(p + 4)) * Ld(n + 4)) + St(Ld(p)) * Ld(n)) * inv);
  F32 t[3];
  for (uint32_t k = 0; k < 3; ++k) t[k] = Fst(St(inv) * Ld(n + 4 * k));
  for (uint32_t k = 0; k < 3; ++k) Put(out + 4 * k, Fst(Ld(p + 4 * k) - St(t[k]) * d));
  c.esp += 4;
}

// The 3x3 matrix product ECX x EAX into EDX (no register changed), each
// element's three products summed in the original's order and stored as it
// is made (later elements read an output on an input).
void f_00427590(Ctx& c) {
  const uint32_t a = c.eax, m = c.ecx, o = c.edx;
  auto p = [](uint32_t i, uint32_t j) { return St(Ld(i)) * Ld(j); };
  Put(o, Fst((p(m, a) + p(m + 4, a + 0xC)) + p(a + 0x18, m + 8)));
  Put(o + 4, Fst((p(m + 4, a + 0x10) + p(a + 0x1C, m + 8)) + p(a + 4, m)));
  Put(o + 8, Fst((p(m + 4, a + 0x14) + p(a + 0x20, m + 8)) + p(a + 8, m)));
  Put(o + 0xC, Fst((p(m + 0x10, a + 0xC) + p(m + 0xC, a)) + p(m + 0x14, a + 0x18)));
  Put(o + 0x10, Fst((p(m + 0x14, a + 0x1C) + p(m + 0x10, a + 0x10)) + p(m + 0xC, a + 4)));
  Put(o + 0x14, Fst((p(m + 0x14, a + 0x20) + p(m + 0x10, a + 0x14)) + p(m + 0xC, a + 8)));
  Put(o + 0x18, Fst((p(m + 0x18, a) + p(a + 0x18, m + 0x20)) + p(m + 0x1C, a + 0xC)));
  Put(o + 0x1C, Fst((p(a + 0x1C, m + 0x20) + p(a + 0x10, m + 0x1C)) + p(a + 4, m + 0x18)));
  Put(o + 0x20, Fst((p(a + 0x20, m + 0x20) + p(a + 0x14, m + 0x1C)) + p(a + 8, m + 0x18)));
  c.esp += 4;
}


// ---- the front end: the scene's and the frame's entry points -------------------
//
// The refexport's per-frame entries (the table 0xC950B8, filled by
// 0x4A94C0; the client's copy at 0x1426D60) and what they call. The scene's
// counts: entities 0x154D70C, coronas 0x154D710, dlights 0x154D714, polygon
// vertices 0x154D71C, polygons 0x154D730 (each with its first of the scene
// at 0x154D72C, 0x154D728, 0x154D718, 0x154D720); the back end's buffer
// 0x1549698 holds dlights from +0x80000 (0x88 bytes), coronas from +0x81100
// (0x30), polygons from +0x1FD700 (16) and their vertices from +0x20D700
// (32), and the commands from +0x24D700 (used +0x28D700). Registered: the
// renderer's 0x1535A20.

namespace {

constexpr uint32_t kRegistered = 0x1535A20, kFrameCount = 0x1535A24;
constexpr uint32_t kNumEntities = 0x154D70C, kNumCoronas = 0x154D710, kNumDlights = 0x154D714,
                   kNumPolyVerts = 0x154D71C, kNumPolys = 0x154D730;
constexpr uint32_t kCmds = 0x24D700, kCmdsUsed = 0x28D700;
constexpr uint32_t kErrorPtr = 0x1549604, kCvarSetPtr = 0x1549628;

// The x87 compare `fld m32; fcomp m32; fnstsw ax` on the guest's x87 stack:
// AX the status word (the rest of EAX kept).
void CompareSw(Ctx& c, F32 a, F32 b) {
  x87::LoadF32(c.fpu, a.bits);
  x87::CompareF32(c.fpu, b.bits);
  x87::Pop(c.fpu);
  c.eax = (c.eax & 0xFFFF0000u) | x87::StatusWord(c.fpu);
}

// R_GetCommandBuffer inlined: size bytes of the command buffer (none once
// it would pass 0x3FFF8 bytes, unsigned), 0 for none; EAX and ECX as the
// original leaves them.
uint32_t CommandBuffer(Ctx& c, uint32_t size) {
  const uint32_t bed = U32(kBackEndData), used = U32(bed + kCmdsUsed) + size;
  c.ecx = bed;
  c.eax = used;
  if (used > 0x3FFF8) return 0;
  Put(bed + kCmdsUsed, used);
  return c.eax = used + bed + kCmds - size;
}

// The end of the command list (0 at the used offset, the count 0) and the
// commands run by the back end (0x4CEA70, EAX the list) without the cvar
// 0x153455C, from the call site returning to ret.
void IssueCommands(Ctx& c, uint32_t ret) {
  const uint32_t cmds = U32(kBackEndData) + kCmds;
  Put(cmds + U32(cmds + 0x40000), 0u);
  Put(cmds + 0x40000, 0u);
  c.eax = cmds;
  c.edx = U32(0x153455C);
  if (!CvarInt(0x153455C)) Call(c, f_004CEA70, ret);
}

// Q_stricmpn(EAX 99999, ECX, EDX) from the call site returning to ret: 0 when
// the strings match but for case.
bool SameName(Ctx& c, uint32_t a, uint32_t b, uint32_t ret) {
  c.eax = 99999;
  c.ecx = a;
  c.edx = b;
  Call(c, f_004406F0, ret);
  return !c.eax;
}

}  // namespace

// The scene's firsts set to the counts (dlights, coronas, entities,
// polygons): EAX, ECX and EDX the polygons', coronas' and entities' counts.
void f_004DE950(Ctx& c) {
  c.eax = U32(kNumDlights);
  c.ecx = U32(kNumCoronas);
  c.edx = U32(kNumEntities);
  Put(0x154D718, c.eax);
  c.eax = U32(kNumPolys);
  Put(0x154D728, c.ecx);
  Put(0x154D72C, c.edx);
  Put(0x154D720, c.eax);
  c.esp += 4;
}

// A polygon added to the scene (shader, 0 for the default shader's index
// (0x1535AE8 +0x44); the vertex count; the vertices, 32 bytes each): when
// registered, unless the vertices would pass 0x15344F8 or the polygons
// reach 0x1534504 (signed): the polygon (type 2, the shader, the count,
// its vertices' place) and its vertices copied (rep movs of count << 5
// bytes), both counts.
void f_004DEA40(Ctx& c) {
  const uint32_t e = c.esp;
  c.esp = e + 4;
  c.eax = U32(kRegistered);
  if (!c.eax) return;
  uint32_t shader = Arg(c, e, 1);
  if (!shader) shader = U32(U32(0x1535AE8) + 0x44);
  const uint32_t first = U32(kNumPolyVerts), n = Arg(c, e, 2), total = first + n;
  c.ecx = n;
  c.eax = U32(0x15344F8);
  if (int32_t(total) > int32_t(c.eax)) return;
  const uint32_t polys = U32(kNumPolys);
  c.edx = polys;
  if (int32_t(polys) >= I32(0x1534504)) return;
  const uint32_t bed = U32(kBackEndData), poly = ((polys + 0x1FD70) << 4) + bed;
  const uint32_t verts = ((first + 0x106B8) << 5) + bed, bytes = n << 5;
  Put(poly + 8, n);
  Put(poly, 2u);
  Put(poly + 4, shader);
  Put(poly + 0xC, verts);
  RepMovs(verts, Arg(c, e, 3), bytes >> 2, bytes & 3);
  c.eax = bytes;
  c.ecx = 0;
  c.edx = polys + 1;
  Put(kNumPolys, c.edx);
  Put(kNumPolyVerts, total);
}

// RE_AddPolysToScene (its warning for a null shader; shader, vertex count,
// vertices, polygon count): when registered and the shader not 0, each
// polygon as 0x4DEA40 adds one (the vertex limit checked against the
// counts at the call plus the vertices so far), until a limit or the count.
// EAX the place of the next polygon's count, ECX the vertex count, EDX the
// next vertices' place.
void f_004DEAE0(Ctx& c) {
  const uint32_t e = c.esp;
  c.esp = e + 4;
  c.eax = U32(kRegistered);
  if (!c.eax) return;
  const uint32_t shader = Arg(c, e, 1);
  c.eax = shader;
  if (!shader) {
    c.esp = e - 0xC;
    Push(c, 0x541CD4);
    Push(c, 2);
    CallPtr(c, U32(kPrintfPtr), 0x4DEB05, 0x4DEAFF);
    c.esp = e + 4;
    return;
  }
  const int32_t count = I32(e + 0x10);
  c.eax = uint32_t(count);
  if (count <= 0) return;
  const uint32_t bed = U32(kBackEndData), n = Arg(c, e, 2), bytes = n << 5;
  uint32_t total = U32(kNumPolyVerts) + n, from = Arg(c, e, 3);
  c.ecx = n;
  c.edx = ((U32(kNumPolyVerts) + 0x106B8) << 5) + bed;
  c.eax = (U32(kNumPolys) << 4) + bed + 0x1FD708;
  for (int32_t j = 0; j < count; j++) {
    if (int32_t(total) > I32(0x15344F8) || I32(kNumPolys) >= I32(0x1534504)) break;
    Put(c.eax, c.ecx);
    Put(c.eax - 4, shader);
    Put(c.eax + 4, c.edx);
    Put(c.eax - 8, 2u);
    RepMovs(c.edx, from, bytes >> 2, bytes & 3);
    Put(kNumPolys, U32(kNumPolys) + 1);
    c.ecx = Arg(c, e, 2);
    total += c.ecx;
    const uint32_t verts = U32(kNumPolyVerts) + c.ecx;
    c.eax += 0x10;
    c.edx += bytes;
    Put(kNumPolyVerts, verts);
    from += bytes;
  }
}

// A dlight added to the scene (origin, intensity, r, g, b): when
// registered, below 32 dlights and with an intensity not at or below 0
// (NaN passes): the next of the back end's dlights (type 2, the colour,
// the intensity's bits at +0x74, its square x 1/32 at +0x10 and that by the
// scale 0x1536290 by each component at +0x24, the origin, an attenuation of
// 1, a cone of 180 at +0x70 and the fields between) and the count. EAX the
// dlight (the status word in AX when refused), ECX 0, EDX the origin's z.
void f_004DED10(Ctx& c) {
  const uint32_t e = c.esp;
  c.esp = e + 4;
  c.eax = U32(kRegistered);
  c.ecx = 0;
  if (!c.eax) return;
  const uint32_t n = U32(kNumDlights);
  c.edx = n;
  if (int32_t(n) >= 0x20) return;
  const F32 intensity = Ld(e + 8);
  CompareSw(c, intensity, Ld(0x55B584));
  if (St(intensity) < 0.0f || St(intensity) == 0.0f) return;
  const St radius = St(intensity) * intensity * Ld(0x55B5C0);
  const uint32_t l = n * 0x88 + U32(kBackEndData) + 0x80000, org = Arg(c, e, 1);
  Put(kNumDlights, n + 1);
  Put(l + 0x74, intensity);
  Put(l + 0x10, Fst(radius));
  for (uint32_t k = 0; k < 0xC; k += 4) Put(l + 4 + k, U32(e + 0xC + k));
  Put(l + 0x20, 0x3F800000u);
  Put(l, 2u);
  for (uint32_t k = 0x14; k < 0x20; k += 4) Put(l + k, 0u);
  Put(l + 0x24, Fst(St(Ld(0x1536290)) * radius * Ld(e + 0xC)));
  Put(l + 0x28, Fst(St(Ld(0x1536290)) * radius * Ld(e + 0x10)));
  Put(l + 0x30, 0x3F800000u);
  Put(l + 0x40, 0x3F800000u);
  for (uint32_t k = 0x34; k < 0x40; k += 4) Put(l + k, 0u);
  Put(l + 0x2C, Fst(St(Ld(0x1536290)) * radius * Ld(e + 0x14)));
  Put(l + 0x44, U32(org));
  Put(l + 0x48, U32(org + 4));
  c.edx = U32(org + 8);
  Put(l + 0x50, 0x3F800000u);
  Put(l + 0x68, 0x3F800000u);
  Put(l + 0x4C, c.edx);
  Put(l + 0x60, 0x3A83126Fu);
  Put(l + 0x64, 0u);
  Put(l + 0x6C, 0u);
  Put(l + 0x70, 0x43340000u);
  c.eax = l;
}

// A corona added to the scene (its warning for an id outside 0-0x7FF;
// origin, r, g, b, scale, id, flags): when registered and below 32: the
// next of the back end's coronas (origin, colour, scale +0x24, id +0x28,
// flags +0x2C) and the count, or ri.Printf's warning (code 1). EAX the
// corona, ECX the scale, EDX the flags.
void f_004DEE20(Ctx& c) {
  const uint32_t e = c.esp;
  c.esp = e + 4;
  c.eax = U32(kRegistered);
  if (!c.eax) return;
  const uint32_t n = U32(kNumCoronas);
  c.ecx = n;
  if (int32_t(n) >= 0x20) return;
  const uint32_t id = Arg(c, e, 6);
  c.edx = id;
  if (int32_t(id) < 0 || int32_t(id) >= 0x800) {
    c.esp = e;
    for (uint32_t v : {0x800u, id, 0x541C28u, 1u}) Push(c, v);
    CallPtr(c, U32(kPrintfPtr), 0x4DEEB2, 0x4DEEAC);
    c.esp = e + 4;
    return;
  }
  const uint32_t q = n * 0x30 + U32(kBackEndData) + 0x81100, org = Arg(c, e, 1);
  Put(kNumCoronas, n + 1);
  for (uint32_t k = 0; k < 0xC; k += 4) Put(q + k, U32(org + k));
  for (uint32_t k = 0; k < 0xC; k += 4) Put(q + 0xC + k, U32(e + 8 + k));
  c.ecx = Arg(c, e, 5);
  Put(q + 0x28, id);
  c.edx = Arg(c, e, 7);
  Put(q + 0x24, c.ecx);
  Put(q + 0x2C, c.edx);
  c.eax = q;
}

// The cull's far distance 0x11BEB84 set: the argument when above 0 (NaN
// too), else 0 (EAX then the status word in AX).
void f_004DA0C0(Ctx& c) {
  const F32 d = Ld(c.esp + 4);
  CompareSw(c, d, Ld(0x55B584));
  if (St(d) > 0.0f || !(St(d) == St(d))) {
    c.eax = d.bits;
    Put(kCullFar, d);
  } else {
    Put(kCullFar, 0u);
  }
  c.esp += 4;
}

// A fog set (fog, then start and end times (or for the fog 8 the mode),
// colour, density): fogs 0x15357E0 (64 bytes each: mode, hint 0x1100,
// colour, density +0x2C (1 for linear), start and end as floats, the set
// flag +0x30, 0 at +0x38); a start and end of 0 clears the set flag; a
// density below 1 (NaN too) is exponential fog (0x800, its density), else
// linear (0x2601). The fog 8 changes the fog now (0x1535920, its target
// 0x15359A0, its start 0x1535960): mode 3 clears the fog now and its target
// (the fog now copied to the start while 0x1535950 is set) and 0x13F9CF8;
// another fog that is set becomes 0x13F9CF8 and the target (the start the
// fog now, or that fog without one), at once without a time (the fog now's
// and the target's +0x3C 1, the times 0) or over it from 0x1536360. The
// float conversions and the compare are on the guest's x87 stack (AX the
// status word, which no caller reads).
void f_004C8AA0(Ctx& c) {
  const uint32_t e = c.esp, fog = Arg(c, e, 1);
  c.esp = e + 4;
  if (fog != 8) {
    c.edx = 0;
    const uint32_t f = (fog << 6) + 0x15357E0;
    c.ecx = fog << 6;
    if (!Arg(c, e, 2) && !Arg(c, e, 3)) {
      c.eax = 0;
      Put(f + 0x30, 0u);
      return;
    }
    auto toFloat = [&](uint32_t v) {
      x87::LoadI32(c.fpu, int32_t(v));
      const uint32_t b = x87::StoreF32(c.fpu);
      x87::Pop(c.fpu);
      return b;
    };
    Put(f + 0x10, Arg(c, e, 4));
    Put(f + 0x20, toFloat(Arg(c, e, 2)));
    Put(f + 0x14, Arg(c, e, 5));
    Put(f + 0x18, Arg(c, e, 6));
    Put(f + 0x24, toFloat(Arg(c, e, 3)));
    c.eax = Arg(c, e, 6);
    const F32 density = Ld(e + 0x1C);
    Put(f + 0x1C, 0x3F800000u);
    Put(f + 0x38, 0u);
    Put(f + 0x34, 1u);
    CompareSw(c, density, Ld(0x55B580));
    if (!(St(density) >= 1.0f)) {
      c.edx = density.bits;
      Put(f, 0x800u);
      Put(f + 0x2C, c.edx);
    } else {
      Put(f, 0x2601u);
      Put(f + 0x2C, 0x3F800000u);
    }
    Put(f + 0x30, 1u);
    Put(f + 4, 0x1100u);
    return;
  }
  const uint32_t mode = Arg(c, e, 2);
  c.ecx = mode;
  if (mode == 3) {
    if (U32(kFog + 0x30)) MovsD(kFogFrom, kFog, 0x10);
    for (uint32_t k = 0; k < 0x40; k += 4) Put(0x15358A0 + k, 0u);
    for (uint32_t k = 0; k < 0x40; k += 4) Put(kFogTo + k, 0u);
    c.eax = c.ecx = c.edx = 0;
    Put(0x13F9CF8, 0u);
    return;
  }
  c.eax = mode << 6;
  c.edx = U32(c.eax + 0x1535810);
  if (c.edx != 1) return;
  Put(0x13F9CF8, mode);
  c.edx = 0;
  MovsD(kFogFrom, U32(kFog + 0x30) ? kFog : c.eax + 0x15357E0, 0x10);
  MovsD(kFogTo, c.eax + 0x15357E0, 0x10);
  c.ecx = 0;
  c.eax = Arg(c, e, 3);
  if (!c.eax) {
    Put(kFogTo + 0x3C, 1u);
    Put(kFog + 0x3C, 1u);
    Put(kFogTo + 8, 0u);
    Put(kFogTo + 0xC, 0u);
    return;
  }
  c.ecx = U32(kTime);
  Put(kFogTo + 8, c.ecx);
  c.ecx += c.eax;
  Put(kFogTo + 0xC, c.ecx);
}

// The fogs saved (its error for a buffer below 0x244 bytes; buffer, size):
// the fogs' 0x240 bytes, then 0x13F9CF8. EAX 0x244, ECX that fog.
void f_004C8C40(Ctx& c) {
  const uint32_t e = c.esp;
  if (const uint32_t size = Arg(c, e, 2); size < 0x244) {
    for (uint32_t v : {0x244u, size, 0x543200u, 1u}) Push(c, v);
    CallPtr(c, U32(kErrorPtr), 0x4C8C5E, 0x4C8C58);
    c.esp = e;
  }
  const uint32_t buf = Arg(c, e, 1);
  MovsD(buf, 0x15357E0, 0x90);
  c.ecx = U32(0x13F9CF8);
  Put(buf + 0x240, c.ecx);
  c.eax = 0x244;
  c.esp = e + 4;
}

// The fogs restored (its error for a buffer below 0x244 bytes; buffer,
// size): the fogs' 0x240 bytes, then 0x13F9CF8 from the buffer's +0x240.
// EAX 0x244, ECX 0.
void f_004C8C90(Ctx& c) {
  const uint32_t e = c.esp;
  if (Arg(c, e, 2) < 0x244) {
    Push(c, 0x5431B0);
    Push(c, 1);
    CallPtr(c, U32(kErrorPtr), 0x4C8CA7, 0x4C8CA1);
    c.esp = e;
  }
  const uint32_t buf = Arg(c, e, 1);
  MovsD(0x15357E0, buf, 0x90);
  Put(0x13F9CF8, U32(buf + 0x240));
  c.eax = 0x244;
  c.ecx = 0;
  c.esp = e + 4;
}

// 128 entries of 0x4C bytes at 0x154B100 cleared and linked into a free
// list (each to the one before, the head 0x154D700 the last), 0x154B0EC,
// 0x13F9CF4 and 0x11CE828-0x11CE830 cleared. EAX the end, ECX the last.
void f_004CEC10(Ctx& c) {
  for (uint32_t k = 0; k < 0x2600; k += 4) Put(0x154B100 + k, 0u);
  for (uint32_t va : {0x154B0ECu, 0x13F9CF4u, 0x11CE828u, 0x11CE82Cu, 0x11CE830u}) Put(va, 0u);
  c.ecx = 0;
  for (c.eax = 0x154B100; int32_t(c.eax) < 0x154D700; c.eax += 0x4C) {
    Put(c.eax, c.ecx);
    c.ecx = c.eax;
  }
  Put(0x154D700, c.ecx);
  c.esp += 4;
}

// The command r_smc_flush (its registration): each model cache entry in use
// (from the list's last, 0x11CD054) has its blocks freed (0x4DA0F0 from the
// tree's root), the list's last unlinked and the entry's first block
// (+0x88) appended to the free list of level 0 (0x11CD028).
void f_004DB370(Ctx& c) {
  const uint32_t e = c.esp, esi = c.esi;
  for (uint32_t entry = U32(0x11CD054); entry != 0x11CD050; entry = U32(0x11CD054)) {
    c.esi = entry;
    c.esp = e - 4;
    for (uint32_t v : {4u, 0u, entry, 0x11BEC28u}) Push(c, v);
    Call(c, f_004DA0F0, 0x4DB38F);
    Put(U32(U32(0x11CD054) + 4), U32(U32(0x11CD054)));
    const uint32_t last = U32(0x11CD054);
    Put(U32(last) + 4, U32(last + 4));
    c.eax = entry + 0x88;
    Put(c.eax, 0x11CD028u);
    c.ecx = U32(0x11CD02C);
    Put(entry + 0x8C, c.ecx);
    Put(0x11CD02C, c.eax);
    c.edx = U32(entry + 0x8C);
    Put(c.edx, c.eax);
  }
  c.esi = esi;
  c.esp = e + 4;
}

// The pending frees cleared (0x13F9CE8).
void f_004DF5F0(Ctx& c) {
  Put(0x13F9CE8, 0u);
  c.esp += 4;
}

// The pending frees done, last first (0x13F9CE8 of 8 bytes from 0x11D0A58:
// a kind, a block): kind 0 through the refimport's 0x1549650, 1 its
// 0x154961C, others none (ECX then the kind less 1); the count stored
// before each. EAX the count left (as read after a free).
void f_004DF600(Ctx& c) {
  const uint32_t e = c.esp;
  c.eax = U32(0x13F9CE8);
  while (int32_t(c.eax) > 0) {
    const uint32_t kind = U32(c.eax * 8 + 0x11D0A50);
    c.eax--;
    Put(0x13F9CE8, c.eax);
    if (kind > 1) {
      c.ecx = kind - 1;
      continue;
    }
    c.esp = e;
    const uint32_t block = U32(c.eax * 8 + 0x11D0A5C);
    Push(c, block);
    if (kind) {
      c.eax = block;
      c.ecx = 0;
      CallPtr(c, U32(kFreePtr), 0x4DF633, 0x4DF62D);
    } else {
      c.ecx = block;
      CallPtr(c, U32(0x1549650), 0x4DF643, 0x4DF63D);
    }
    c.eax = U32(0x13F9CE8);
  }
  c.esp = e + 4;
}

// A model's bounds (handle, mins, maxs): its brush model's (+0x4C, read
// again for each component), zeros without one. EAX the brush model, ECX
// maxs, EDX the last value.
void f_004F8C20(Ctx& c) {
  const uint32_t e = c.esp, model = U32(0x15364F8 + 4 * Arg(c, e, 1)), mins = Arg(c, e, 2), maxs = Arg(c, e, 3);
  c.esp = e + 4;
  if (!U32(model + 0x4C)) {
    c.ecx = c.edx = 0;
    for (uint32_t k : {8u, 4u, 0u}) Put(mins + k, 0u);
    for (uint32_t k : {8u, 4u, 0u}) Put(maxs + k, 0u);
    c.eax = maxs;
    return;
  }
  for (uint32_t k = 0; k < 0xC; k += 4) Put(mins + k, U32(U32(model + 0x4C) + k));
  for (uint32_t k = 0; k < 8; k += 4) Put(maxs + k, c.edx = U32(U32(model + 0x4C) + 0xC + k));
  c.eax = U32(model + 0x4C);
  c.ecx = maxs;
  Put(maxs + 8, c.edx = U32(c.eax + 0x14));
}

// The frame's texture memory (0x4DF810; stack: three outs, any of them 0):
// once a frame (0x11D0C5C the frame 0x1535A24 counted at), over the images
// (0x153A51C of them at 0x153A520) used this frame or the last (+0x5C):
// their sizes +0x48 summed (0x11D4D60; those of type 3 (+0x50) apart,
// 0x11D0C58) and their +0x4C (0x11D4D68). Above 0x1400000 with the warning
// icons' cvar 0x14E43EC on, StatMon_UpdateEntry's entry 6 inlined: shown
// 0xBB8 ms (timeGetTime against the base 0x15342A0, taken once), its image
// "gfx/2d/warning@textures.jpg" registered when missing and 0x1439370 set,
// the count 0x8D5A98 at least 7.
void f_004DF810(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, esi = c.esi, edi = c.edi;
  const uint32_t frame = U32(kFrameCount);
  if (U32(0x11D0C5C) != frame) {
    Put(0x11D0C5C, frame);
    Put(0x11D4D60, 0u);
    Put(0x11D0C58, 0u);
    Put(0x11D4D68, 0u);
    uint32_t total = 0, other = 0;
    if (I32(0x153A51C) > 0) {
      for (int32_t i = 0; i < I32(0x153A51C); i++) {
        const uint32_t img = U32(0x153A520 + 4 * uint32_t(i)), used = U32(img + 0x5C);
        if (used != frame && used != frame - 1) continue;
        const uint32_t size = U32(img + 0x48);
        total += size;
        if (U32(img + 0x50) == 3) Put(0x11D0C58, U32(0x11D0C58) + size);
        other += U32(img + 0x4C);
      }
      Put(0x11D4D68, other);
      Put(0x11D4D60, total);
    }
    if (CvarInt(0x14E43EC) && int32_t(other) > 0x1400000) {
      const uint32_t time = U32(0x53331C);
      c.esp = e - 0xC;
      if (!U32(0x13F9DB0)) {
        CallPtr(c, time, 0x4DF8BF, 0x4DF8BD);
        Put(0x15342A0, c.eax);
        Put(0x13F9DB0, 1u);
      }
      CallPtr(c, time, 0x4DF8D0, 0x4DF8CE);
      Put(0x8D58C8, c.eax + (0xBB8 - U32(0x15342A0)));
      if (!U32(0x8D58CC) && U32(0x1439370)) {
        Push(c, 1);
        Push(c, 0x541A48);
        CallPtr(c, U32(0x1426D74), 0x4DF903, 0x4DF8FD);
        c.esp = e - 0xC;
        Put(0x8D58CC, c.eax);
      }
      if (I32(0x8D5A98) <= 6) Put(0x8D5A98, 7u);
    }
  }
  const uint32_t total = U32(0x11D4D60), other = U32(0x11D4D68);
  if (const uint32_t p = Arg(c, e, 1)) Put(p, total);
  if (const uint32_t p = Arg(c, e, 2)) Put(p, U32(0x11D0C58));
  if (const uint32_t p = Arg(c, e, 3)) Put(p, other);
  c.ebx = ebx;
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// The frame's counters printed by r_speeds (0x15346E0) and cleared
// (0x15364CC-0x15364EC, 0x1549B04-0x1549B34): with the warning icons' cvar
// 0x14E43EC the texture memory's check (0x4DF810); with the stats 0x15364F4
// (0x4D4BB0's) its counts, texture memory and overdraw per pixel (the sum
// 0x1549B1C over the screen 0x15347C8 x 0x15347C4) and entities; r_speeds 1
// the counts, triangles (/3), texture memory in MB (by 2^-20) and the
// overdraw, 2 the patches', 3 the view cluster, 4 the dlights' (with any
// surfaces), 6 the flares', through ri.Printf. EAX and ECX 0.
void f_004D3ED0(Ctx& c) {
  const uint32_t e = c.esp, esi = c.esi, edi = c.edi;
  auto print = [&](std::initializer_list<uint32_t> args, uint32_t ret, uint32_t va) {
    c.esp = e - 0x18;
    for (uint32_t v : args) Push(c, v);
    CallPtr(c, U32(kPrintfPtr), ret, va);
  };
  auto pixels = [] { return FromInt(I32(0x15347C8) * I32(0x15347C4)); };
  c.esp = e - 0x10;
  if (CvarInt(0x14E43EC)) {
    for (uint32_t v : {0u, 0u, 0u}) Push(c, v);
    Call(c, f_004DF810, 0x4D3EEA);
    c.esp = e - 0x10;
  }
  if (U32(0x15364F4)) {
    Put(U32(0x15364F4), U32(0x1549B10));
    Put(U32(0x15364F4) + 4, U32(0x1549B14));
    Put(U32(0x15364F4) + 8, U32(0x1549B0C));
    Put(U32(0x15364F4) + 0xC, U32(0x1549B18));
    const uint32_t s = U32(0x15364F4);
    for (uint32_t v : {s + 0x18, s + 0x14, s + 0x10}) Push(c, v);
    Call(c, f_004DF810, 0x4D3F3F);
    c.esp = e - 0x10;
    const St pp = pixels();
    c.edx = U32(kNumEntities);
    Put(U32(0x15364F4) + 0x20, Fst(Ld(0x1549B1C) / pp));
    Put(U32(0x15364F4) + 0x1C, c.edx);
  }
  switch (CvarInt(kFogDebug)) {
    case 0: break;
    case 1: {
      c.esp = e - 0x18;
      for (uint32_t v : {e - 4, e - 8, e - 0x10}) Push(c, v);
      Call(c, f_004DF810, 0x4D3FA3);
      const St pp = pixels();
      const uint32_t total = U32(e - 0x10), diff = U32(e - 0x10) - U32(e - 8), other = U32(e - 4);
      auto put = [&](uint32_t va, St v) {
        const uint64_t d = std::bit_cast<uint64_t>(v.v);
        Put(va, uint32_t(d));
        Put(va + 4, uint32_t(d >> 32));
      };
      put(e - 0x20, Ld(0x1549B1C) / pp);
      put(e - 0x28, FromInt(int32_t(total)) * Ld(0x55B5F8));
      put(e - 0x30, FromInt(int32_t(diff)) * Ld(0x55B5F8));
      put(e - 0x38, FromInt(int32_t(other)) * Ld(0x55B5F8));
      c.esp = e - 0x38;
      for (uint32_t v : {uint32_t(I32(0x1549B14) / 3), uint32_t(I32(0x1549B10) / 3), U32(0x1549B0C), U32(0x15364E4),
                         U32(0x1549B04), U32(0x1549B08), 0x542940u, 0u})
        Push(c, v);
      CallPtr(c, U32(kPrintfPtr), 0x4D404E, 0x4D4048);
      break;
    }
    case 2:
      print({U32(0x15364E0), U32(0x15364DC), U32(0x15364D8), U32(0x15364D4), U32(0x15364D0), U32(0x15364CC),
             0x542904u, 0u},
            0x4D4090, 0x4D408A);
      break;
    case 3: print({U32(0x15364A0), 0x5428F0u, 0u}, 0x4D40B0, 0x4D40AA); break;
    case 4:
      if (const uint32_t surfs = U32(0x1549B20))
        print({uint32_t(I32(0x1549B24) / 3), surfs, U32(0x15364EC), U32(0x15364E8), 0x5428C0u, 0u}, 0x4D40F4,
              0x4D40EE);
      break;
    case 6: print({U32(0x1549B30), U32(0x1549B2C), U32(0x1549B28), 0x54289Cu, 0u}, 0x4D411F, 0x4D4119); break;
  }
  for (uint32_t k = 0; k < 0x24; k += 4) Put(0x15364CC + k, 0u);
  for (uint32_t k = 0; k < 0x34; k += 4) Put(0x1549B04 + k, 0u);
  c.eax = c.ecx = 0;
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// The frame's end (0x4D4A60; stack: where to put the front end's and the
// back end's times, either 0): when registered, the swap command (id 11 at
// the used offset, unchecked) and the list's end, the counters (0x4D3ED0),
// the commands run (0x4CEA70) without the cvar 0x153455C, then the used
// offset and the scene's counts and firsts cleared; the times 0x15364F0 and
// 0x1549B34 handed out and cleared.
void f_004D4A60(Ctx& c) {
  const uint32_t e = c.esp, esi = c.esi, edi = c.edi;
  c.esp = e + 4;
  c.eax = U32(kRegistered);
  if (!c.eax) return;
  const uint32_t bed = U32(kBackEndData), cmds = bed + kCmds;
  Put(U32(bed + kCmdsUsed) + bed + kCmds, 0xBu);
  Put(bed + kCmdsUsed, U32(bed + kCmdsUsed) + 4);
  Put(cmds + U32(cmds + 0x40000), 0u);
  Put(cmds + 0x40000, 0u);
  c.esi = cmds;
  c.edi = 0;
  c.esp = e - 8;
  Call(c, f_004D3ED0, 0x4D4AB0);
  if (!CvarInt(0x153455C)) {
    c.eax = cmds;
    Call(c, f_004CEA70, 0x4D4AC1);
  }
  Put(U32(kBackEndData) + kCmdsUsed, 0u);
  for (uint32_t va : {0x154D724u, kNumDlights, 0x154D718u, kNumCoronas, 0x154D728u, kNumEntities, 0x154D72Cu,
                      kNumPolys, 0x154D720u, kNumPolyVerts})
    Put(va, 0u);
  if (const uint32_t p = Arg(c, e, 1)) Put(p, U32(0x15364F0));
  Put(0x15364F0, 0u);
  if (const uint32_t p = Arg(c, e, 2)) Put(p, U32(0x1549B34));
  Put(0x1549B34, 0u);
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// Command 9 queued (4 bytes). EAX and ECX as the original leaves them.
void f_004D4B40(Ctx& c) {
  if (const uint32_t cmd = CommandBuffer(c, 4)) Put(cmd, 9u);
  c.esp += 4;
}

// Command 10 queued with its argument (8 bytes) for an argument above 0.
void f_004D4B70(Ctx& c) {
  c.edx = Arg(c, c.esp, 1);
  if (int32_t(c.edx) > 0)
    if (const uint32_t cmd = CommandBuffer(c, 8)) {
      Put(cmd, 0xAu);
      Put(cmd + 4, c.edx);
    }
  c.esp += 4;
}

// The stats 0x15364F4 that 0x4D3ED0 fills (EAX the argument).
void f_004D4BB0(Ctx& c) {
  c.eax = Arg(c, c.esp, 1);
  Put(0x15364F4, c.eax);
  c.esp += 4;
}

// What a trace from a point (start; dir: its end start + dir x 262144,
// stored) hits (CM_Trace 0x41CD60, mask 0xF83FFF7, the zero box; results at
// e - 0x30 with the fraction 1 to beat): EAX 0 for none (from inside a solid
// (+0x2F), +0x2E set, a fraction of exactly 1 or no surface name (+0x24)),
// else 1 with the outs filled (each size bytes, its last byte and first
// cleared; EAX 0 when one ends full): the surface's name copied, its
// material ((flags +0x1C >> 20) & 0x1F, 1-0x16: the names at 0x5646C0, else
// "^1default^7"), "solid" or "^3nonsolid^7" by the contents' (+0x20) bit 1,
// then each surface parameter of the table 0x564830 (name, surface flag
// +8, content +0xC) whose flag the surface or whose content the contents
// has, after a space. The first out's length is kept in the dir's argument
// slot.
void f_004D4BC0(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, ebp = c.ebp, esi = c.esi, edi = c.edi;
  const uint32_t start = Arg(c, e, 1), dir = Arg(c, e, 2), results = e - 0x30, end = e - 0x3C;
  auto done = [&](uint32_t eax) {
    c.eax = eax;
    c.ebx = ebx;
    c.ebp = ebp;
    c.esi = esi;
    c.edi = edi;
    c.esp = e + 4;
  };
  Put(end, Fst(St(Ld(dir)) * Ld(0x55B884) + Ld(start)));
  Put(results, 0x3F800000u);
  Put(end + 4, Fst(St(Ld(dir + 4)) * Ld(0x55B884) + Ld(start + 4)));
  Put(end + 8, Fst(St(Ld(dir + 8)) * Ld(0x55B884) + Ld(start + 8)));
  c.esp = e - 0x4C;
  for (uint32_t v : {0u, 0xF83FFF7u, 0x537E54u, 0u, end, start, results}) Push(c, v);
  c.eax = c.ecx = c.edx = 0;
  Call(c, f_0041CD60, 0x4D4C29);
  c.esp = e - 0x4C;
  if (U8(results + 0x2F) || U8(results + 0x2E) || St(Ld(results)) == 1.0f || !U32(results + 0x24)) return done(0);
  const uint32_t out0 = Arg(c, e, 3), out1 = Arg(c, e, 4), out2 = Arg(c, e, 5), size = Arg(c, e, 6);
  const uint32_t flags = U32(results + 0x1C), contents = U32(results + 0x20);
  for (uint32_t s = U32(results + 0x24), d = out0;; s++, d++) {
    const uint8_t ch = U8(s);
    Wr<uint8_t>(g_guest_base, d, ch);
    if (!ch) break;
  }
  auto strncpy = [&](uint32_t to, uint32_t from, uint32_t n, uint32_t ret) {
    for (uint32_t v : {n, from, to}) Push(c, v);
    Call(c, f_0051AA00, ret);
    c.esp = e - 0x4C;
  };
  auto len = [](uint32_t s) {
    uint32_t n = 0;
    while (U8(s + n)) n++;
    return n;
  };
  c.esi = size;
  c.edi = out1;
  c.ebp = out2;
  Wr<uint8_t>(g_guest_base, out1, 0);
  Wr<uint8_t>(g_guest_base, out1 + size - 1, 0);
  const int32_t material = int32_t(flags) >> 20 & 0x1F;
  Wr<uint8_t>(g_guest_base, out2, 0);
  Wr<uint8_t>(g_guest_base, out2 + size - 1, 0);
  strncpy(out1, material > 0 && material < 0x17 ? U32(0x5646C0 + 16 * uint32_t(material)) : 0x542730, size,
          0x4D4CB5);
  if (U8(out1 + size - 1)) return done(0);
  uint32_t len1 = len(out1);
  Put(e + 8, len1);
  strncpy(out2, contents & 1 ? 0x542728 : 0x542718, size, 0x4D4CF7);
  if (U8(out2 + size - 1)) return done(0);
  uint32_t len2 = len(out2);
  for (uint32_t p = 0x564830; U32(p); p += 0x10) {
    c.ebx = p;
    if (U32(p + 8) & flags) {
      Wr<uint8_t>(g_guest_base, out1 + len1, ' ');
      len1++;
      Put(e + 8, len1);
      strncpy(out1 + len1, U32(p), size - len1, 0x4D4D58);
      if (U8(out1 + size - 1)) return done(0);
      len1 += len(out1 + len1);
      Put(e + 8, len1);
    }
    if (U32(p + 0xC) & contents) {
      Wr<uint8_t>(g_guest_base, out2 + len2, ' ');
      len2++;
      strncpy(out2 + len2, U32(p), size - len2, 0x4D4DAA);
      if (U8(out2 + size - 1)) return done(0);
      len2 += len(out2 + len2);
    }
  }
  done(1);
}

// A font's height at a scale (font, scale): the refimport's 0x1549694 with
// both, its +0x5004 times the scale, truncated (_ftol); EAX.
void f_004D5D30(Ctx& c) {
  const uint32_t e = c.esp;
  Push(c, Arg(c, e, 2));
  Push(c, Arg(c, e, 1));
  CallPtr(c, U32(0x1549694), 0x4D5D40, 0x4D5D3A);
  c.eax = uint32_t(Ftol(St(Ld(e + 8)) * Ld(c.eax + 0x5004)));
  c.esp = e + 4;
}

// Text drawn (nine arguments) as 0x4D6400 with -1 and 0 after the sixth.
void f_004D5D60(Ctx& c) {
  const uint32_t e = c.esp;
  for (int n : {9, 8, 7})
    Push(c, Arg(c, e, n));
  Push(c, 0);
  Push(c, 0xFFFFFFFF);
  for (int n : {6, 5, 4, 3, 2, 1}) Push(c, Arg(c, e, n));
  Call(c, f_004D6400, 0x4D5D96);
  c.esp = e + 4;
}

// The client's debug text (its block and count; 0x15495A0, 0x154959C) and
// lines (0x15495B4, 0x15495B0) for the back end's debug draws (0x4AD5C0).
// EAX and ECX the arguments.
void f_004ADAD0(Ctx& c) {
  c.eax = Arg(c, c.esp, 1);
  c.ecx = Arg(c, c.esp, 2);
  Put(0x15495A0, c.eax);
  Put(0x154959C, c.ecx);
  c.esp += 4;
}
void f_004ADAF0(Ctx& c) {
  c.eax = Arg(c, c.esp, 1);
  c.ecx = Arg(c, c.esp, 2);
  Put(0x15495B4, c.eax);
  Put(0x15495B0, c.ecx);
  c.esp += 4;
}

// A number shown at a point for a while (point, number, colour, duration
// in ms; 0x4AD480 draws them): 40-byte entries at 0x15495C0 (made at the
// limit 0x15495BC's size by 0x42A480 when missing, the count 0x15495B8
// then 0), unless the count is the limit: the point, the colour's three
// components (+0xC), the number (+0x1C), the time 0x1536360 (+0x20) and the
// duration (+0x24), the count and block read again for each. EAX the block,
// ECX the count x 5, EDX the duration.
void f_004ADB10(Ctx& c) {
  const uint32_t e = c.esp;
  c.eax = U32(0x15495C0);
  if (!c.eax) {
    Push(c, U32(0x15495BC) * 40);
    Call(c, f_0042A480, 0x4ADB2A);
    c.esp = e;
    c.ecx = 0;
    Put(0x15495C0, c.eax);
    Put(0x15495B8, 0u);
  } else {
    c.ecx = U32(0x15495B8);
  }
  c.esp = e + 4;
  if (c.ecx == U32(0x15495BC)) return;
  auto field = [](uint32_t o, uint32_t v) { Put(U32(0x15495C0) + U32(0x15495B8) * 40 + o, v); };
  const uint32_t org = Arg(c, e, 1), colour = Arg(c, e, 3);
  Put(c.eax + c.ecx * 40, U32(org));
  field(4, U32(org + 4));
  field(8, U32(org + 8));
  for (uint32_t k = 0; k < 0xC; k += 4) field(0xC + k, U32(colour + k));
  field(0x1C, Arg(c, e, 2));
  field(0x20, U32(kTime));
  c.edx = Arg(c, e, 4);
  field(0x24, c.edx);
  c.eax = U32(0x15495C0);
  c.ecx = U32(0x15495B8) * 5;
  Put(0x15495B8, U32(0x15495B8) + 1);
}

// The commands so far run (0x4D41A0): when registered, the list's end and
// the commands run (0x4CEA70) without the cvar 0x153455C. ECX unchanged.
void f_004D41A0(Ctx& c) {
  const uint32_t e = c.esp, ecx = c.ecx;
  c.eax = U32(kRegistered);
  if (c.eax) {
    const uint32_t cmds = U32(kBackEndData) + kCmds;
    c.edx = U32(0x153455C);
    Put(cmds + U32(cmds + 0x40000), 0u);
    Put(cmds + 0x40000, 0u);
    c.eax = cmds;
    c.esp = e - 4;
    if (!CvarInt(0x153455C)) Call(c, f_004CEA70, 0x4D41DD);
  }
  c.ecx = ecx;
  c.esp = e + 4;
}

// The light grid's settings for the frame (0x4A97B0): 0x15364C0 the cvar
// 0x15344D4 within 0-2, the grid's size 0x15364C4 the cvar 0x153471C within
// 1-5 and 0x15364C8 0.5 over its square, the light limit 0x15364BC the cvar
// 0x1534480 within 0x15364C0 + 1 and 0x153475C; the size's cvar modified
// (+0x14) clears the grid corners' cache (0xC952C8, 2 MB) and counters.
// ECX the size's square (left in the slot of the ECX it saves).
void f_004A97B0(Ctx& c) {
  int32_t mode = CvarInt(0x15344D4);
  Put(0x15364C0, uint32_t(mode));
  if (mode < 0) Put(0x15364C0, uint32_t(mode = 0));
  else if (mode > 2) Put(0x15364C0, uint32_t(mode = 2));
  const uint32_t cvar = U32(0x153471C);
  int32_t n = I32(cvar + 0x20);
  Put(0x15364C4, uint32_t(n));
  if (n < 1) Put(0x15364C4, uint32_t(n = 1));
  else if (n > 5) Put(0x15364C4, uint32_t(n = 5));
  c.ecx = uint32_t(n * n);
  const uint32_t limit = U32(0x1534480);
  Put(0x15364C8, Fst(Ld(0x55B590) / FromInt(int32_t(c.ecx))));
  c.eax = U32(limit + 0x20);
  Put(0x15364BC, c.eax);
  if (int32_t(c.eax) < mode + 1) Put(0x15364BC, c.eax = uint32_t(mode + 1));
  if (int32_t(c.eax) > I32(0x153475C)) Put(0x15364BC, U32(0x153475C));
  c.edx = cvar;
  if (U32(cvar + 0x14)) {
    Put(cvar + 0x14, 0u);
    std::memset(g_guest_base + 0xC952C8, 0, 0x200000);
    for (uint32_t va : {0xC951A4u, 0xC951A8u, 0xC952C0u, 0xC952C4u}) Put(va, 0u);
    c.eax = 0;
  }
  c.esp += 4;
}

// One world surface of type 0x1A a frame (0x4D0870): from the cursor
// 0x1538514 on (wrapping at the world's surface count +0x9C, stopping at
// the cursor), the next whose reference (12 bytes from +0xA0) points to
// one has 0x4D0700 called (ESI the surface; its reference's +4 +0x18C, and
// the least of its +0x44 16-bit indexes from +0x48 (signed) x 32).
void f_004D0870(Ctx& c) {
  const uint32_t e = c.esp, esi = c.esi, world = U32(kWorld);
  if (world) {
    const uint32_t from = U32(0x1538514);
    for (uint32_t i = from + 1;; i++) {
      Put(0x1538514, i);
      if (i == U32(world + 0x9C)) Put(0x1538514, i = 0);
      if (i == from) break;
      const uint32_t surf = U32(U32(world + 0xA0) + i * 12 + 8);
      if (U32(surf) != 0x1A) continue;
      const int32_t n = I32(surf + 0x44);
      uint32_t least = U16(surf + 0x48);
      for (int32_t k = 1; k < n; k++)
        if (int32_t(least) > int32_t(U16(surf + 0x48 + 2 * uint32_t(k)))) least = U16(surf + 0x48 + 2 * uint32_t(k));
      c.esi = surf;
      c.esp = e - 8;
      Push(c, least << 5);
      Push(c, U32(U32(U32(world + 0xA0) + i * 12 + 4) + 0x18C));
      Call(c, f_004D0700, 0x4D08F0);
      break;
    }
  }
  c.esi = esi;
  c.esp = e + 4;
}

// One model surface a frame (0x4F8D50; stack passed on): from the cursors
// 0x15384FC (model), 0x1538500 (level) and 0x1538504 (surface) the next
// surface of a model with an XModel (+0x54; its levels' surfaces as the
// DObj accessors read them, 16-bit counts) has 0x4815E0 called (ESI the
// surface); the models wrap at 0x15384F8 (signed), stopping at the model
// the search started from.
void f_004F8D50(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, ebp = c.ebp, esi = c.esi, edi = c.edi;
  const int32_t models = I32(0x15384F8);
  uint32_t level = U32(0x1538500), model = U32(0x15384FC), surf = U32(0x1538504);
  auto xmodel = [&] { return U32(U32(0x15364F8 + 4 * model) + 0x54); };
  for (;;) {
    if (xmodel()) {
      const uint32_t s = U32(U32(U32(xmodel() + 4) + 0x14 + 0x14 * level) + 4);
      Put(0x1538504, ++surf);
      if (int32_t(surf) < int32_t(int16_t(U16(s + 4)))) {
        c.esi = U32(U32(s + 8) + 4 * surf);
        c.esp = e - 0x10;
        Push(c, Arg(c, e, 1));
        Call(c, f_004815E0, 0x4F8E05);
        break;
      }
      Put(0x1538504, surf = 0);
      Put(0x1538500, ++level);
      if (int32_t(level) < int32_t(int16_t(U16(U32(xmodel() + 4) + 0x64)))) continue;
    }
    Put(0x1538500, level = 0);
    const uint32_t from = model;
    do {
      Put(0x15384FC, ++model);
      if (int32_t(model) >= models) Put(0x15384FC, model = 0);
    } while (model != from && !xmodel());
    if (model == from) break;
  }
  c.ebx = ebx;
  c.ebp = ebp;
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// r_textureMode applied (0x4DF740; EAX its string): the mode of that name
// (the table 0x564308 of six: name, minification and magnification
// filters; Q_stricmpn) into 0x5642FC and 0x564300, and each mipmapped image
// (+0x64 bit 0) bound (GL_Bind) and given both through glTexParameter
// (0x15356BC, its target +0x54); "bad filter name" through ri.Printf for
// none.
void f_004DF740(Ctx& c) {
  const uint32_t e = c.esp, ebx = c.ebx, esi = c.esi, edi = c.edi, name = c.eax;
  c.esp = e - 0xC;
  uint32_t i = 0;
  for (; i < 6; i++) {
    const uint32_t mode = 0x564308 + 12 * i;
    c.esi = mode;
    c.edi = i;
    if (U32(mode) && name && SameName(c, name, U32(mode), 0x4DF766)) break;
  }
  if (i == 6) {
    Push(c, 0x541A64);
    Push(c, 0);
    CallPtr(c, U32(kPrintfPtr), 0x4DF788, 0x4DF782);
  } else {
    Put(0x5642FC, U32(0x56430C + 12 * i));
    Put(0x564300, U32(0x564310 + 12 * i));
    c.ebx = (name & 0xFFFFFF00u) | 1;
    for (int32_t k = 0; k < I32(0x153A51C); k++) {
      const uint32_t img = U32(0x153A520 + 4 * uint32_t(k));
      c.edi = uint32_t(k);
      c.esi = img;
      if (!(U8(img + 0x64) & 1)) continue;
      Call(c, f_004CB3B0, 0x4DF7D1);
      for (uint32_t v : {U32(0x5642FC), 0x2801u, U32(img + 0x54)}) Push(c, v);
      CallPtr(c, U32(0x15356BC), 0x4DF7E6, 0x4DF7E0);
      for (uint32_t v : {U32(0x564300), 0x2800u, U32(img + 0x54)}) Push(c, v);
      CallPtr(c, U32(0x15356BC), 0x4DF7FC, 0x4DF7F6);
    }
  }
  c.ebx = ebx;
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

// r_nv_fogdist_mode applied (0x4A70A0): GL_EYE_PLANE_ABSOLUTE_NV,
// GL_EYE_PLANE or GL_EYE_RADIAL_NV (Q_stricmpn) into 0x1534798 and glFogi
// (GL_FOG_DISTANCE_MODE_NV); another name (or none) is set to
// GL_EYE_RADIAL_NV through ri.Cvar_Set first.
void f_004A70A0(Ctx& c) {
  const uint32_t e = c.esp, esi = c.esi, name = U32(U32(0x1534558) + 4);
  static constexpr uint32_t kModes[][4] = {{0x54E4BC, 0x855C, 0x4A70C2, 0x4A70E2},
                                           {0x54E4AC, 0x2502, 0x4A70F9, 0x4A7119},
                                           {0x54E498, 0x855B, 0x4A7130, 0x4A7150}};
  c.esi = name;
  c.esp = e - 4;
  auto fogi = [&](uint32_t ret) {
    Push(c, U32(0x1534798));
    Push(c, 0x855A);
    CallPtr(c, U32(0x1534C84), ret, ret - 6);
  };
  for (const auto& m : kModes) {
    if (!name) break;
    if (SameName(c, m[0], name, m[2])) {
      Put(0x1534798, m[1]);
      fogi(m[3]);
      c.esi = esi;
      c.esp = e + 4;
      return;
    }
  }
  Push(c, 0x54E498);
  Push(c, 0x54E484);
  Put(0x1534798, 0x855Bu);
  CallPtr(c, U32(kCvarSetPtr), 0x4A716C, 0x4A7166);
  c.esp = e - 4;
  fogi(0x4A7181);
  c.esi = esi;
  c.esp = e + 4;
}

// RE_BeginFrame (its error strings; stereo frame): when registered, the
// frame count 0x1535A24 incremented, 0x1534628 and 0x1535A2C cleared;
// r_measureOverdraw on: off again (ri.Printf's warning, ri.Cvar_Set) with
// fewer than 4 stencil bits (0x1534768) or cg_shadows 2, else the commands
// so far run (0x4D41A0) and the stencil set to count (glEnable, 0x15355D0
// -1, 0x1535754 0, glStencilFunc, glStencilOp); off but modified: the
// commands run and the stencil test disabled; its modified flag cleared.
// r_textureMode modified: the commands run, 0x4DF740. The light grid's
// settings (0x4A97B0). With the PN triangles entry 0x15348C0: each of
// r_ati_truform_tess (above the maximum 0x15347AC lowered to it through
// va("%d")), _pointmode (LINEAR or CUBIC into 0x15347B4; another set to
// LINEAR, CUBIC kept) and _normalmode (LINEAR or QUADRATIC into 0x15347B0;
// another set to LINEAR, LINEAR kept) that is modified is applied. With
// 0x1534794, r_nv_fogdist_mode modified (0x4A70A0); r_gamma modified: the
// commands run, 0x4E59D0; with 0x1534788 and r_vbo_paranoia, 0x4D0870,
// 0x4FC1B0(3) and 0x4F8D50(3). Without r_ignoreGLErrors: the commands run
// and glGetError, an error through ri.Error. Then the draw buffer command
// (id 8, 8 bytes): with stereo (0x15347D8) GL_BACK_LEFT or GL_BACK_RIGHT
// for the frame 1 or 2 (else ri.Error, the buffer not set); without, an
// error for a frame other than 0, then GL_FRONT for r_drawBuffer
// "GL_FRONT" (Q_stricmpn), else GL_BACK.
void f_004D4680(Ctx& c) {
  const uint32_t e = c.esp, esi = c.esi, edi = c.edi;
  auto done = [&] {
    c.esi = esi;
    c.edi = edi;
    c.esp = e + 4;
  };
  c.esp = e - 4;
  c.edi = 0;
  if (!U32(kRegistered)) return done();
  Put(0x1534628, 0u);
  Put(kFrameCount, U32(kFrameCount) + 1);
  Put(0x1535A2C, 0u);
  auto cvarSet = [&](uint32_t name, uint32_t value, uint32_t ret) {
    Push(c, value);
    Push(c, name);
    CallPtr(c, U32(kCvarSetPtr), ret, ret - 6);
  };
  auto gl = [&](std::initializer_list<uint32_t> args, uint32_t ptr, uint32_t ret) {
    for (uint32_t v : args) Push(c, v);
    CallPtr(c, U32(ptr), ret, ret - 6);
  };
  if (CvarInt(0x15343EC)) {
    if (const int32_t bits = I32(0x1534768); bits < 4) {
      for (uint32_t v : {uint32_t(bits), 0x54283Cu, 0u}) Push(c, v);
      CallPtr(c, U32(kPrintfPtr), 0x4D46CE, 0x4D46C8);
      cvarSet(0x54D9E4, 0x55B190, 0x4D46DE);
      c.esp = e - 4;
      Put(U32(0x15343EC) + 0x14, 0u);
    } else if (CvarInt(0x15343D8) == 2) {
      Push(c, 0x5427F0);
      Push(c, 0);
      CallPtr(c, U32(kPrintfPtr), 0x4D4706, 0x4D4700);
      cvarSet(0x54D9E4, 0x55B190, 0x4D4716);
      c.esp = e - 4;
      Put(U32(0x15343EC) + 0x14, 0u);
    } else {
      Call(c, f_004D41A0, 0x4D4729);
      gl({0xB90}, 0x1534F68, 0x4D4734);
      gl({0xFFFFFFFF}, 0x15355D0, 0x4D473C);
      gl({0}, 0x1535754, 0x4D4743);
      gl({0xFFFFFFFF, 0, 0x207}, 0x1534E7C, 0x4D4751);
      gl({0x1E02, 0x1E02, 0x1E00}, 0x1535428, 0x4D4766);
    }
  } else if (U32(U32(0x15343EC) + 0x14)) {
    Call(c, f_004D41A0, 0x4D4772);
    gl({0xB90}, 0x15349E0, 0x4D477D);
  }
  Put(U32(0x15343EC) + 0x14, 0u);
  if (U32(U32(0x1534414) + 0x14)) {
    Call(c, f_004D41A0, 0x4D4795);
    c.eax = U32(U32(0x1534414) + 4);
    Call(c, f_004DF740, 0x4D47A3);
    Put(U32(0x1534414) + 0x14, 0u);
  }
  c.esp = e - 8;
  Call(c, f_004A97B0, 0x4D47B1);
  if (U32(0x15348C0)) {
    if (const uint32_t tess = U32(0x1534428); U32(tess + 0x14)) {
      Put(tess + 0x14, 0u);
      if (FromInt(I32(0x15347AC)) < Ld(tess + 0x1C)) {
        Push(c, U32(0x15347AC));
        Push(c, 0x55AE18);
        Call(c, f_00440940, 0x4D47EC);
        Push(c, c.eax);
        Push(c, 0x54E470);
        CallPtr(c, U32(kCvarSetPtr), 0x4D47F8, 0x4D47F2);
        c.esp = e - 8;
      }
      gl({U32(U32(0x1534428) + 0x20), 0x87F4}, 0x15348C0, 0x4D4810);
    }
    // The point and normal modes: their cvars, names, values, the global
    // they go to, the PN triangles enum and the return addresses.
    static constexpr uint32_t kModes[][9] = {
        {0x15344BC, 0x54DFE4, 0x87F5, 0x87F6, 0x15347B4, 0x54DFCC, 0x87F6, 0x87F2, 0},
        {0x15344B4, 0x54E008, 0x87F7, 0x87F8, 0x15347B0, 0x54DFEC, 0x87F7, 0x87F3, 1}};
    static constexpr uint32_t kRets[][4] = {{0x4D4835, 0x4D485A, 0x4D4884, 0x4D4898},
                                            {0x4D48BD, 0x4D48E2, 0x4D490C, 0x4D4921}};
    for (const auto& m : kModes) {
      const uint32_t* r = kRets[m[8]];
      const uint32_t cvar = U32(m[0]);
      if (!U32(cvar + 0x14)) continue;
      Put(cvar + 0x14, 0u);
      const uint32_t name = U32(cvar + 4);
      c.esi = name;
      if (name && SameName(c, 0x5427E4, name, r[0]))
        Put(m[4], m[2]);
      else if (name && SameName(c, m[1], name, r[1]))
        Put(m[4], m[3]);
      else {
        Push(c, 0x5427E4);
        Push(c, m[5]);
        Put(m[4], m[6]);
        CallPtr(c, U32(kCvarSetPtr), r[2], r[2] - 6);
        c.esp = e - 8;
      }
      gl({U32(m[4]), m[7]}, 0x15348C0, r[3]);
    }
  }
  if (U32(0x1534794) && U32(U32(0x1534558) + 0x14)) {
    Put(U32(0x1534558) + 0x14, 0u);
    Call(c, f_004A70A0, 0x4D493B);
  }
  if (U32(U32(0x15343F8) + 0x14)) {
    Put(U32(0x15343F8) + 0x14, 0u);
    Call(c, f_004D41A0, 0x4D494D);
    Call(c, f_004E59D0, 0x4D4952);
  }
  if (U32(0x1534788) && CvarInt(0x15346BC)) {
    Call(c, f_004D0870, 0x4D496A);
    Push(c, 3);
    Call(c, f_004FC1B0, 0x4D4971);
    Push(c, 3);
    Call(c, f_004F8D50, 0x4D4978);
    c.esp = e - 8;
  }
  if (!CvarInt(0x1534690)) {
    Call(c, f_004D41A0, 0x4D498A);
    CallPtr(c, U32(0x153483C), 0x4D4990, 0x4D498A);
    if (c.eax) {
      for (uint32_t v : {c.eax, 0x5427B4u, 0u}) Push(c, v);
      CallPtr(c, U32(kErrorPtr), 0x4D49A1, 0x4D499B);
      c.esp = e - 8;
    }
  }
  const uint32_t cmd = CommandBuffer(c, 8);
  if (!cmd) return done();
  c.esi = cmd;
  Put(cmd, 8u);
  const uint32_t frame = Arg(c, e, 1);
  if (U32(0x15347D8)) {
    if (frame == 1 || frame == 2) {
      Put(cmd + 4, frame == 1 ? 0x402u : 0x403u);
    } else {
      for (uint32_t v : {frame, 0x542778u, 0u}) Push(c, v);
      CallPtr(c, U32(kErrorPtr), 0x4D4A10, 0x4D4A0A);
    }
    return done();
  }
  if (frame) {
    for (uint32_t v : {frame, 0x54273Cu, 0u}) Push(c, v);
    CallPtr(c, U32(kErrorPtr), 0x4D4A27, 0x4D4A21);
    c.esp = e - 8;
  }
  const uint32_t buffer = U32(U32(0x15344E8) + 4);
  Put(cmd + 4, buffer && SameName(c, 0x54C9C4, buffer, 0x4D4A46) ? 0x404u : 0x405u);
  done();
}

// An image of 0x1535A3C (by the sixth argument) uploaded from data (stack:
// two unused, columns, rows, data, the image, whether it changed): bound
// (GL_Bind); at its size (16-bit +0x40, +0x42) and changed, glTexSubImage2D;
// at another, the size into +0x44/+0x40 and +0x46/+0x42 (the image read
// again for each), glTexImage2D (RGBA, 3 components) and glTexParameter's
// linear filters and clamping to the edge.
void f_004CD1F0(Ctx& c) {
  const uint32_t e = c.esp, esi = c.esi, edi = c.edi, n = Arg(c, e, 6);
  auto img = [&] { return U32(0x1535A3C + 4 * n); };
  auto gl = [&](std::initializer_list<uint32_t> args, uint32_t ptr, uint32_t ret) {
    for (uint32_t v : args) Push(c, v);
    CallPtr(c, U32(ptr), ret, ret - 6);
  };
  c.esp = e - 8;
  c.esi = img();
  c.edi = n;
  Call(c, f_004CB3B0, 0x4CD202);
  const uint32_t cols = Arg(c, e, 3), rows = Arg(c, e, 4), data = Arg(c, e, 5);
  c.eax = cols;
  c.ecx = rows;
  c.edx = img();
  if (cols == U16(c.edx + 0x40) && rows == U16(c.edx + 0x42)) {
    c.edx = Arg(c, e, 7);
    if (c.edx) gl({data, 0x1401, 0x1908, rows, cols, 0, 0, 0, 0xDE1}, 0x1534E4C, 0x4CD24F);
  } else {
    Wr<uint16_t>(g_guest_base, img() + 0x44, uint16_t(cols));
    Wr<uint16_t>(g_guest_base, img() + 0x40, uint16_t(cols));
    Wr<uint16_t>(g_guest_base, img() + 0x46, uint16_t(rows));
    Wr<uint16_t>(g_guest_base, img() + 0x42, uint16_t(rows));
    gl({data, 0x1401, 0x1908, 0, rows, cols, 3, 0, 0xDE1}, 0x1534CC4, 0x4CD299);
    gl({0x2601, 0x2801, 0xDE1}, 0x15356BC, 0x4CD2AE);
    gl({0x2601, 0x2800, 0xDE1}, 0x15356BC, 0x4CD2C3);
    gl({0x812F, 0x2802, 0xDE1}, 0x15356BC, 0x4CD2D8);
    gl({0x812F, 0x2803, 0xDE1}, 0x15356BC, 0x4CD2ED);
  }
  c.esi = esi;
  c.edi = edi;
  c.esp = e + 4;
}

}  // namespace recomp::gen
