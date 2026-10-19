// Collision model traces: CM_BoxTrace (0x41D380), CM_TransformedBoxTrace
// (0x41D870) and the transformed body 0x41D3B0, CM_Trace (0x41CD60) and
// the trace code under them, and point contents (0x41A730, 0x41A820), read
// from the clip map in guest memory.
// Replacements of lifted functions named with `replace` in
// manifest/CoDSP.manifest; FINDINGS.md "Collision traces". Names and
// evidence are in the Ghidra plate comments.
//
// The originals are x87 code; the arithmetic is src/x87_value.h's, which
// gives the original's bits at the main thread's control word. Each
// expression keeps the original's order of operations and operands: a
// value the original keeps on the x87 stack is an St, one it stores is an
// F32, so `St(a) * b + c` is fld a / fmul b / fadd c, and the comparisons
// keep the original's branch for unordered operands. Built with
// -ffp-contract=off (CMakeLists.txt).
//
// Calling conventions are the originals' (whole-program optimisation gave
// most of them register arguments; see each function). The trace work
// (0x118 bytes) stays where the original keeps it, in CM_Trace's guest
// stack frame, so the lifted functions called from here (CM_ClipHandle-
// ToModel, the vector normalisation and angle vectors, patch collision,
// CM_BoxLeafnums_r, the console print) see the same pointers; guest frames are
// made only where such a call needs guest memory below them. Not
// reproduced: EAX, ECX, EDX and EFLAGS on return (every caller overwrites
// them first), the x87 status word and stale registers (x87_value.h), the
// /GS cookies and the dead stack below ESP.

#include <cstdint>
#include <cstring>
#include <exception>

#include "runtime/lifted.h"
#include "x87_value.h"

namespace recomp::gen {

void f_00412AD0(Ctx& c);  // CM_ClipHandleToModel(EAX handle) -> EAX cmodel
void f_00426940(Ctx& c);  // normalizes EDI's vector into ESI -> ST0 its length
void f_004271E0(Ctx& c);  // forward, right, up (ESI, EDI, EBX) of EDX's angles
void f_00415A40(Ctx& c);  // CM_TraceThroughPatchCollide(tw, pc)
void f_0042E370(Ctx& c);  // prints (fmt, ...) to the console
void f_004163C0(Ctx& c);  // CM_PositionTestInPatchCollide(ECX tw, EAX pc) -> EAX
void f_0041A5D0(Ctx& c);  // CM_BoxLeafnums_r(ll, nodenum)

namespace {

using lift::Rd;
using lift::Wr;

// ---- the clip map -------------------------------------------------------

// cplane_t
struct Plane {
  F32 normal[3];
  F32 dist;
  uint8_t type, signbits, pad[2];
};
// cbrushside_t
struct BrushSide {
  uint32_t plane;
  int32_t shader;
};
// cbrush_t. axial: the shader of each bounding plane, -x -y -z +x +y +z.
struct Brush {
  int32_t contents;
  F32 bounds[2][3];
  int32_t numsides;
  uint32_t sides;
  int32_t checkcount;
  int16_t axial[2][3];
};
// cLeaf_t (CoD's: 16-bit counts)
struct Leaf {
  uint32_t unknown0;
  int32_t firstLeafBrush;
  uint16_t numLeafBrushes, firstLeafSurface, numLeafSurfaces, unknownE;
};
// cNode_t
struct Node {
  uint32_t plane;
  int16_t children[2];
};
// A leaf surface: a patch (patch collide) or a piece of terrain.
struct Patch {
  int32_t checkcount;
  int32_t shader;
  int32_t contents;
  F32 bounds[2][3];
  uint32_t pc;
  uint32_t terrain;
};
// Terrain collision (built by CM_GenerateTerrainCollide, 0x418860): a
// triangle count, a byte that says which end of a capsule touches it
// first, then the triangles. A triangle has its plane, two edge planes
// that give it barycentric coordinates, and its shared vertices and
// edges (null where none), each tested once per trace (checkcount).
struct TerrainTri {
  F32 plane[4];
  F32 edge[2][4];
  uint32_t verts[3], edges[3];
};
struct TerrainVert {
  int32_t checkcount;
  F32 xyz[3];
};
// An edge as a cylinder: origin, two axes across it, its direction (the
// third axis) and its length along it.
struct TerrainEdge {
  int32_t checkcount;
  F32 origin[3];
  F32 axis[3][3];
  F32 length;
};
struct Terrain {
  uint16_t numTris;
  uint8_t fromBelow, pad;
  TerrainTri tris[1];
};
static_assert(sizeof(Plane) == 0x14 && sizeof(Brush) == 0x34 && sizeof(Leaf) == 0x10 &&
              sizeof(Patch) == 0x2C && sizeof(TerrainTri) == 0x48 &&
              sizeof(TerrainEdge) == 0x38);

// trace_t
struct Trace {
  F32 fraction;
  F32 endpos[3];
  F32 normal[3];
  int32_t surfaceFlags;
  int32_t contents;
  uint32_t material;  // the shader
  uint32_t unknown28;
  uint16_t unknown2C;
  uint8_t allsolid, startsolid;
};
// The capsule of a trace: use, radius, half height, the offset of the
// upper sphere's centre, and per axis |offset| + radius.
struct Sphere {
  int32_t use;
  F32 radius, halfheight;
  F32 offset[3];
  F32 extents[3];
};
// traceWork_t: start, end and box are relative to the box's centre.
struct TraceWork {
  F32 start[3], end[3], delta[3];
  F32 deltaLenSq;
  F32 size[2][3];
  F32 offsets[8][3];  // box corners by plane sign bits
  F32 extentSum;      // maxs y + z + x
  F32 bounds[2][3];   // of the whole move
  int32_t contents;
  int32_t isPoint;
  Trace trace;
  Sphere sphere;
};
static_assert(sizeof(Trace) == 0x30 && sizeof(Sphere) == 0x24 && sizeof(TraceWork) == 0x118);

// The clip map (cm) and its temporary box model.
constexpr uint32_t kNoCurves = 0x14BD64C;  // cvar_t* cm_noCurves (->integer at +0x20)
constexpr uint32_t kShaders = 0x14BD6AC;   // 0x48 each: name[64], surfaceFlags, contentFlags
constexpr uint32_t kNodes = 0x14BD6C4;
constexpr uint32_t kLeafs = 0x14BD6CC;
constexpr uint32_t kLeafBrushes = 0x14BD6D4;
constexpr uint32_t kLeafSurfaces = 0x14BD6DC;
constexpr uint32_t kBrushes = 0x14BD6EC;
constexpr uint32_t kPatches = 0x14BD718;
constexpr uint32_t kCheckcount = 0x14BD720;
constexpr uint32_t kBoxBrush = 0x14BD73C;  // cbrush_t*
constexpr uint32_t kBoxModel = 0x14BD740;  // cmodel_t: mins, maxs, leaf
constexpr uint32_t kVec3Origin = 0x537E54;
constexpr uint32_t kBoxWarned = 0x13F9E54;
constexpr uint32_t kBoxWarning = 0x5587B0;  // "^1Box collision on terrain ..."

constexpr int32_t kCapsuleModel = 0x1FE;
constexpr int32_t kBoxModelHandle = 0x1FF;

constexpr float kClipEpsilon = 0.125f;  // 0x55B70C
// Terrain (0x55B5DC, 0x55B718, 0x55B714): barycentric slack, and how far
// a hit stops short.
constexpr float kBaryMin = std::bit_cast<float>(0xBA83126Fu);  // -0.001
constexpr float kBaryMax = std::bit_cast<float>(0x3F8020C5u);  // 1.001
constexpr float kTerrainBackoff = std::bit_cast<float>(0x3727C5ACu);  // 1e-5

template <typename T>
T& At(uint32_t va) {
  return *reinterpret_cast<T*>(g_guest_base + va);
}
uint32_t Va(const void* p) {
  return uint32_t(static_cast<const uint8_t*>(p) - g_guest_base);
}
uint32_t Global(uint32_t va) { return At<uint32_t>(va); }
int32_t Checkcount() { return At<int32_t>(kCheckcount); }
Brush& BoxBrush() { return At<Brush>(Global(kBoxBrush)); }
uint32_t ShaderVa(int32_t index) { return Global(kShaders) + uint32_t(index) * 0x48; }

void Copy3(F32* dst, const F32* src) {
  for (int i = 0; i < 3; i++) dst[i] = src[i];
}

// The lifted function fn, called from the original's call site whose
// return address is ret. Arguments already pushed stay for the caller.
void Call(Ctx& c, GuestFn fn, uint32_t ret) {
  c.esp -= 4;
  Wr<uint32_t>(g_guest_base, c.esp, ret);
  fn(c);
}
void Push(Ctx& c, uint32_t v) {
  c.esp -= 4;
  Wr<uint32_t>(g_guest_base, c.esp, v);
}
// ST0 as a double, popped (a lifted callee's return value).
St PopSt0(Ctx& c) {
  St v(std::bit_cast<double>(x87::StoreF64(c.fpu)));
  x87::Pop(c.fpu);
  return v;
}
// 0x426940's normalisation of in into out, both in guest memory (lifted:
// its square root is the CRT's, with errno on a NaN): the length.
St Normalize(Ctx& c, const F32* in, F32* out, uint32_t ret) {
  const uint32_t esi = c.esi, edi = c.edi;
  c.edi = Va(in);
  c.esi = Va(out);
  Call(c, f_00426940, ret);
  c.esi = esi;
  c.edi = edi;
  return PopSt0(c);
}

// The original's locals, at the same place below the caller's ESP. A guest
// longjmp (the error exit) unwinding through it has set ESP already, to its
// setjmp's frame above this one.
struct GuestFrame {
  Ctx& c;
  uint32_t esp;
  GuestFrame(Ctx& c, uint32_t size) : c(c), esp(c.esp) { c.esp -= size; }
  ~GuestFrame() {
    if (c.esp <= esp) c.esp = esp;
  }
  template <typename T>
  T& at(uint32_t off) {
    return At<T>(c.esp + off);
  }
};

// ---- brushes ------------------------------------------------------------

// CM_TraceThroughBrush (0x41B8F0; ECX tw, brush on the stack). The
// brush's bounds are six axial planes, -1 then +1, before its sides. An
// enterFrac starts at 0 and leaveFrac at the trace's fraction (not -1 and
// 1); both are clipped as each plane is crossed, and the brush is out
// once enterFrac reaches leaveFrac.
void TraceThroughBrush(TraceWork& tw, Brush& b) {
  F32 enterFrac{0}, leaveFrac = tw.trace.fraction;
  bool allsolid = true;  // no plane with the end outside
  // The plane the trace enters last: a side, or an axial plane, which the
  // original builds as a side on its stack.
  uint32_t leadSide = 0;
  bool leadAxial = false;
  F32 axialNormal[3];
  int32_t axialShader = 0;

  // One plane, d1 and d2 the start and end distances: false when the
  // brush is missed.
  auto plane = [&](F32 d1, St d2, auto&& lead) {
    const bool haveLead = leadSide || leadAxial;
    if (St(d1) > 0.0f) {
      const F32 den = Fst(St(d1) - d2);
      if (d2 > 0.0f) {
        if (St(den) <= 0.0f) return false;
        if (d2 >= kClipEpsilon) return false;
        allsolid = false;
      }
      const St a = St(d1) - kClipEpsilon;
      if (a > St(den) * enterFrac) {
        const St f = a / den;
        enterFrac = Fst(f);
        if (f >= leaveFrac) return false;
        lead();
      } else if (!haveLead) {
        lead();
      }
    } else if (d2 > 0.0f) {
      const St den = d1 - d2;
      allsolid = false;
      if (St(leaveFrac) * den < d1) {
        leaveFrac = Fst(St(d1) / den);
        if (St(enterFrac) >= leaveFrac) return false;
      }
    }
    return true;
  };

  F32 sign{0xBF800000u};  // -1, then 1
  for (int side = 0; side < 2; side++) {
    for (int i = 0; i < 3; i++) {
      const F32 ext = tw.sphere.use ? tw.sphere.extents[i] : tw.size[1][i];
      const F32 d1 = Fst((St(tw.start[i]) - b.bounds[side][i]) * sign - ext);
      const St d2 = (St(tw.end[i]) - b.bounds[side][i]) * sign - ext;
      if (!plane(d1, d2, [&] {
            axialShader = b.axial[side][i];
            axialNormal[0] = axialNormal[1] = axialNormal[2] = F32{0};
            axialNormal[i] = sign;
            leadAxial = true;
            leadSide = 0;
          }))
        return;
    }
    sign = F32{0x3F800000u};
  }

  const BrushSide* sides = &At<BrushSide>(b.sides);
  for (int32_t k = 0; k < b.numsides; k++) {
    const BrushSide& s = sides[k];
    const Plane& p = At<Plane>(s.plane);
    F32 d1;
    St d2;
    if (tw.sphere.use) {
      // The sphere centre nearest the plane.
      const F32* off = tw.sphere.offset;
      const St r = St(p.dist) + tw.sphere.radius;
      const St t = (St(p.normal[2]) * off[2] + St(off[0]) * p.normal[0]) +
                   St(p.normal[1]) * off[1];
      St sx, sy, sz;
      F32 e[3];
      if (t > 0.0f) {
        sx = St(tw.start[0]) - off[0];
        sy = St(tw.start[1]) - off[1];
        sz = St(tw.start[2]) - off[2];
        for (int i = 0; i < 3; i++) e[i] = Fst(St(tw.end[i]) - off[i]);
      } else {
        sx = St(off[0]) + tw.start[0];
        sy = St(tw.start[1]) + off[1];
        sz = St(tw.start[2]) + off[2];
        for (int i = 0; i < 3; i++) e[i] = Fst(St(tw.end[i]) + off[i]);
      }
      d1 = Fst(((sz * p.normal[2] + sy * p.normal[1]) + sx * p.normal[0]) - r);
      d2 = ((St(e[2]) * p.normal[2] + St(e[1]) * p.normal[1]) + St(e[0]) * p.normal[0]) - r;
    } else {
      // The box corner nearest the plane.
      const F32* o = tw.offsets[p.signbits];
      const St dist =
          p.dist - ((St(o[2]) * p.normal[2] + St(o[0]) * p.normal[0]) + St(o[1]) * p.normal[1]);
      d1 = Fst(((St(tw.start[1]) * p.normal[1] + St(p.normal[0]) * tw.start[0]) +
                St(p.normal[2]) * tw.start[2]) -
               dist);
      d2 = ((St(tw.end[1]) * p.normal[1] + St(p.normal[2]) * tw.end[2]) +
            St(tw.end[0]) * p.normal[0]) -
           dist;
    }
    if (!plane(d1, d2, [&] {
          leadSide = Va(&s);
          leadAxial = false;
        }))
      return;
  }

  tw.trace.contents = b.contents;
  if (!leadSide && !leadAxial) {
    tw.trace.startsolid = 1;
    if (allsolid) {
      tw.trace.fraction = F32{0};
      tw.trace.allsolid = 1;
    }
    return;
  }
  tw.trace.fraction = enterFrac;
  int32_t shader;
  if (leadAxial) {
    Copy3(tw.trace.normal, axialNormal);
    shader = axialShader;
  } else {
    const BrushSide& s = At<BrushSide>(leadSide);
    Copy3(tw.trace.normal, At<Plane>(s.plane).normal);
    shader = s.shader;
  }
  tw.trace.surfaceFlags = At<int32_t>(ShaderVa(shader) + 0x40);
  tw.trace.material = ShaderVa(shader);
}

// ---- terrain --------------------------------------------------------------

// A hit the trace started in: no fraction, the triangle's normal.
void TerrainStartSolid(TraceWork& tw, const TerrainTri& t) {
  Copy3(tw.trace.normal, t.plane);
  tw.trace.fraction = F32{0};
  tw.trace.startsolid = 1;
}

// A point against terrain (0x419580; EDI terrain, EDX tw): the first
// crossing of a triangle's plane, front to back, inside its edges.
void TraceThroughTerrainPoint(TraceWork& tw, const Terrain& terrain) {
  for (uint32_t i = 0; i < terrain.numTris; i++) {
    const TerrainTri& t = terrain.tris[i];
    const St d2s = ((St(t.plane[1]) * tw.end[1] + St(t.plane[0]) * tw.end[0]) +
                    St(t.plane[2]) * tw.end[2]) -
                   t.plane[3];
    const F32 d2 = Fst(d2s);
    if (d2s >= 0.0f) continue;
    const St d1s = ((St(t.plane[2]) * tw.start[2] + St(t.plane[1]) * tw.start[1]) +
                    St(t.plane[0]) * tw.start[0]) -
                   t.plane[3];
    const F32 d1 = Fst(d1s);
    if (d1s <= 0.0f) continue;
    const St den = St(d1) - d2;
    const St fs = (St(d1) - kClipEpsilon) / den;
    const F32 frac = Fst(fs);
    if (fs >= tw.trace.fraction) continue;
    const St f = d1 / den;
    const F32 px = Fst(f * tw.delta[0] + tw.start[0]);
    const F32 py = Fst(f * tw.delta[1] + tw.start[1]);
    const St pzs = f * tw.delta[2] + tw.start[2];
    const F32 pz = Fst(pzs);
    const St b1s =
        ((pzs * t.edge[0][2] + St(py) * t.edge[0][1]) + St(px) * t.edge[0][0]) - t.edge[0][3];
    const F32 b1 = Fst(b1s);
    if (b1s < kBaryMin) continue;
    if (St(b1) > kBaryMax) continue;
    const St b2 =
        ((St(pz) * t.edge[1][2] + St(py) * t.edge[1][1]) + St(px) * t.edge[1][0]) - t.edge[1][3];
    if (b2 < kBaryMin) continue;
    if (St(b1) + b2 > kBaryMax) continue;
    tw.trace.fraction = frac;
    Copy3(tw.trace.normal, t.plane);
  }
}

// Moves the trace to the end sphere of its capsule that meets the terrain
// first (start and end z; the callers restore them). Returns the distance
// from it to the other end's centre along z.
F32 ToTerrainEnd(TraceWork& tw, const Terrain& terrain) {
  const St h = St(tw.sphere.halfheight) - tw.sphere.radius;
  if (terrain.fromBelow) {
    const F32 h2 = Fst(-2.0f * h);
    tw.start[2] = Fst(h + tw.start[2]);
    tw.end[2] = Fst(h + tw.end[2]);
    return h2;
  }
  const F32 h2 = Fst(h + h);
  tw.start[2] = Fst(St(tw.start[2]) - h);
  tw.end[2] = Fst(St(tw.end[2]) - h);
  return h2;
}

// A capsule whose end sphere starts behind a triangle's plane (d1, its
// distance, at most -r; h2 takes it to the other end along z): in the
// terrain if the other end is not behind the plane too and the triangle
// is under the capsule where it would meet the plane, moved along z.
bool InsideFromBehind(const TraceWork& tw, const TerrainTri& t, F32 d1, F32 h2, F32 r,
                      F32 negR) {
  const St x = St(h2) * t.plane[2] + d1;
  if (x <= negR) return false;
  const F32 invNz = Fst(St(1.0f) / t.plane[2]);
  const F32 e1 = Fst(((St(t.edge[0][1]) * tw.start[1] + St(t.edge[0][0]) * tw.start[0]) +
                      St(t.edge[0][2]) * tw.start[2]) -
                     t.edge[0][3]);
  const F32 e2 = Fst(((St(t.edge[1][1]) * tw.start[1] + St(t.edge[1][0]) * tw.start[0]) +
                      St(t.edge[1][2]) * tw.start[2]) -
                     t.edge[1][3]);
  const St tt = (St(negR) - d1) * invNz;
  const St b1s = tt * t.edge[0][2] + e1;
  const F32 b1 = Fst(b1s);
  if (b1s >= 0.0f) {
    const St b2 = tt * t.edge[1][2] + e2;
    if (b2 >= 0.0f && St(b1) + b2 <= 1.0f) return true;
  }
  const St t2 = x < r ? St(h2) : (St(r) - d1) * invNz;
  const St b1s2 = t2 * t.edge[0][2] + e1;
  const F32 b1b = Fst(b1s2);
  if (b1s2 < 0.0f) return false;
  const St b2b = t2 * t.edge[1][2] + e2;
  if (b2b < 0.0f) return false;
  return !(St(b1b) + b2b > 1.0f);
}

// A sphere against terrain (0x4196F0; EDX tw, which it keeps, terrain on
// the stack). The capsule is moved to the end that meets the terrain
// first (the caller restores start and end z), its radius grows by the
// clip epsilon, and each triangle is tried as a plane, then, where the
// contact point is outside it, against the edges (cylinders) and
// vertices (spheres) outside which it lies.
void TraceThroughTerrainSphere(TraceWork& tw, const Terrain& terrain) {
  const F32 h2 = ToTerrainEnd(tw, terrain);
  const St rs = St(tw.sphere.radius) + kClipEpsilon;
  const F32 r = Fst(rs), negR = Fst(-rs);

  for (uint32_t i = 0; i < terrain.numTris; i++) {
    const TerrainTri& t = terrain.tris[i];
    const St d2 = ((St(t.plane[2]) * tw.end[2] + St(t.plane[1]) * tw.end[1]) +
                   St(t.plane[0]) * tw.end[0]) -
                  t.plane[3];
    if (d2 >= r) continue;
    const St d1s = ((St(t.plane[2]) * tw.start[2] + St(t.plane[1]) * tw.start[1]) +
                    St(t.plane[0]) * tw.start[0]) -
                   t.plane[3];
    const F32 d1 = Fst(d1s);
    const F32 den = Fst(d1s - d2);
    if (St(den) <= 0.0f) continue;

    if (St(d1) <= negR) {
      if (InsideFromBehind(tw, t, d1, h2, r, negR)) {
        TerrainStartSolid(tw, t);
        return;
      }
      continue;
    }

    // Where the sphere meets the plane.
    St f, px, py, pz;
    if (St(d1) < r) {
      f = St(0.0f);
      px = St(tw.start[0]);
      py = St(tw.start[1]);
      pz = St(tw.start[2]);
    } else {
      f = (St(d1) - r) / den;
      if (f > tw.trace.fraction) continue;
      px = f * tw.delta[0] + tw.start[0];
      py = f * tw.delta[1] + tw.start[1];
      pz = f * tw.delta[2] + tw.start[2];
    }
    const F32 b1 = Fst(((py * t.edge[0][1] + px * t.edge[0][0]) + pz * t.edge[0][2]) - t.edge[0][3]);
    const St b2 = ((pz * t.edge[1][2] + py * t.edge[1][1]) + px * t.edge[1][0]) - t.edge[1][3];
    // Bit k: outside edge k (0: the third, 1 and 2: the edge planes'),
    // whose edge or vertex is tried below.
    const uint32_t outside =
        (St(b1) + b2 > 1.0f ? 1u : 0u) | (St(b1) < 0.0f ? 2u : 0u) | (b2 < 0.0f ? 4u : 0u);
    if (!outside) {
      Copy3(tw.trace.normal, t.plane);
      if (f <= kTerrainBackoff) {
        tw.trace.fraction = F32{0};
        tw.trace.startsolid = 1;
        return;
      }
      tw.trace.fraction = Fst(f - kTerrainBackoff);
      continue;
    }

    for (int k = 0; k < 3; k++) {
      if (outside & (1u << k)) {
        if (!t.edges[k]) continue;
        TerrainEdge& e = At<TerrainEdge>(t.edges[k]);
        if (e.checkcount == Checkcount()) continue;
        e.checkcount = Checkcount();
        // The start in the edge's axes.
        const St vx = St(tw.start[0]) - e.origin[0], vy = St(tw.start[1]) - e.origin[1],
                 vz = St(tw.start[2]) - e.origin[2];
        const F32 p0 = Fst((vx * e.axis[0][0] + vz * e.axis[0][2]) + vy * e.axis[0][1]);
        const F32 p1 = Fst((vx * e.axis[1][0] + vz * e.axis[1][2]) + vy * e.axis[1][1]);
        const F32 p2 = Fst((vz * e.axis[2][2] + vy * e.axis[2][1]) + vx * e.axis[2][0]);
        const St c0s = (St(p1) * p1 + St(p0) * p0) - St(r) * r;
        const F32 c0 = Fst(c0s);
        if (c0s <= 0.0f) {
          if (St(p2) < 0.0f) continue;
          if (St(p2) > e.length) continue;
          TerrainStartSolid(tw, t);
          return;
        }
        const F32 dd0 = Fst((St(e.axis[0][0]) * tw.delta[0] + St(tw.delta[1]) * e.axis[0][1]) +
                            St(e.axis[0][2]) * tw.delta[2]);
        const F32 dd1 = Fst((St(tw.delta[1]) * e.axis[1][1] + St(tw.delta[0]) * e.axis[1][0]) +
                            St(e.axis[1][2]) * tw.delta[2]);
        const St bs = St(p1) * dd1 + St(p0) * dd0;
        const F32 b = Fst(bs);
        if (bs >= 0.0f) continue;
        const St a = St(dd1) * dd1 + St(dd0) * dd0;
        const St disc = St(b) * b - a * c0;
        if (disc <= 0.0f) continue;
        const St ts = (-Sqrt(disc) - b) / a;
        const F32 tf = Fst(ts);
        if (!(ts < tw.trace.fraction)) continue;
        const St along = ((St(e.axis[2][2]) * tw.delta[2] + St(tw.delta[0]) * e.axis[2][0]) +
                          St(e.axis[2][1]) * tw.delta[1]) *
                             tf +
                         p2;
        if (along < 0.0f) continue;
        if (along > e.length) continue;
        const St inv = St(1.0f) / r;
        const St q0 = (St(dd0) * tf + p0) * inv;
        const F32 q1 = Fst((St(dd1) * tf + p1) * inv);
        for (int j = 0; j < 3; j++) tw.trace.normal[j] = Fst(q0 * e.axis[0][j]);
        for (int j = 0; j < 3; j++)
          tw.trace.normal[j] = Fst(St(q1) * e.axis[1][j] + tw.trace.normal[j]);
        if (St(tw.trace.fraction) <= kTerrainBackoff) {
          tw.trace.fraction = F32{0};
          tw.trace.startsolid = 1;
          return;
        }
        tw.trace.fraction = Fst(St(tf) - kTerrainBackoff);
      } else {
        if (!t.verts[k]) continue;
        TerrainVert& v = At<TerrainVert>(t.verts[k]);
        if (v.checkcount == Checkcount()) continue;
        v.checkcount = Checkcount();
        const F32 vx = Fst(St(tw.start[0]) - v.xyz[0]);
        const F32 vy = Fst(St(tw.start[1]) - v.xyz[1]);
        const St vzs = St(tw.start[2]) - v.xyz[2];
        const F32 vz = Fst(vzs);
        const St c0s = ((vzs * vz + St(vy) * vy) + St(vx) * vx) - St(r) * r;
        const F32 c0 = Fst(c0s);
        if (c0s <= 0.0f) {
          TerrainStartSolid(tw, t);
          return;
        }
        const St bs = (St(vy) * tw.delta[1] + St(vx) * tw.delta[0]) + St(vz) * tw.delta[2];
        const F32 b = Fst(bs);
        if (bs >= 0.0f) continue;
        const St a = St(tw.deltaLenSq);
        const St disc = St(b) * b - a * c0;
        if (disc < 0.0f) continue;
        const St ts = (-Sqrt(disc) - b) / a;
        if (!(ts < tw.trace.fraction)) continue;
        const St inv = St(1.0f) / r;
        tw.trace.normal[0] = Fst((ts * tw.delta[0] + vx) * inv);
        tw.trace.normal[1] = Fst((ts * tw.delta[1] + vy) * inv);
        tw.trace.normal[2] = Fst((ts * tw.delta[2] + vz) * inv);
        if (St(tw.trace.fraction) <= kTerrainBackoff) {
          tw.trace.fraction = F32{0};
          tw.trace.startsolid = 1;
          return;
        }
        tw.trace.fraction = Fst(ts - kTerrainBackoff);
      }
    }
  }
}

// A box against terrain (0x41A2D0; ESI tw, terrain on the stack): a
// capsule as wide as the box's x extent, with a warning the first time.
void TraceThroughTerrainBox(Ctx& c, TraceWork& tw, const Terrain& terrain) {
  if (!At<int32_t>(kBoxWarned)) {
    At<int32_t>(kBoxWarned) = 1;
    Push(c, kBoxWarning);
    Call(c, f_0042E370, 0x41A2F0);
    c.esp += 4;
  }
  const F32 sz = tw.start[2], ez = tw.end[2], radius = tw.sphere.radius;
  tw.sphere.use = 1;
  tw.sphere.radius = tw.size[1][0];
  TraceThroughTerrainSphere(tw, terrain);
  tw.sphere.use = 0;
  tw.start[2] = sz;
  tw.end[2] = ez;
  tw.sphere.radius = radius;
}

// 0x41A350 (EAX terrain, EDX tw).
void TraceThroughTerrain(Ctx& c, TraceWork& tw, const Terrain& terrain) {
  if (tw.isPoint) {
    TraceThroughTerrainPoint(tw, terrain);
  } else if (tw.sphere.use) {
    const F32 sz = tw.start[2], ez = tw.end[2];
    TraceThroughTerrainSphere(tw, terrain);
    tw.start[2] = sz;
    tw.end[2] = ez;
  } else {
    TraceThroughTerrainBox(c, tw, terrain);
  }
}

// ---- leaves ---------------------------------------------------------------

// Whether the move can touch a leaf surface's bounds (0x41B690; ECX tw,
// ESI mins, EDX maxs): its bounds overlap, and for a point, the segment
// passes the separating-axis tests across the three axes.
bool TraceMayTouch(const TraceWork& tw, const F32* mins, const F32* maxs) {
  for (int i = 0; i < 3; i++) {
    if (St(tw.bounds[0][i]) > maxs[i]) return false;
    if (St(tw.bounds[1][i]) < mins[i]) return false;
  }
  if (!tw.isPoint) return true;
  F32 size[3], extent[3];
  for (int i = 0; i < 3; i++) size[i] = Fst(St(maxs[i]) - mins[i]);
  // Twice the segment's centre minus twice the box's.
  const St ax = St(mins[0]) + maxs[0];
  const St ay = St(maxs[1]) + mins[1];
  const F32 az = Fst(St(maxs[2]) + mins[2]);
  const St bx = St(tw.end[0]) + tw.start[0];
  const F32 by = Fst(St(tw.end[1]) + tw.start[1]);
  const F32 bz = Fst(St(tw.end[2]) + tw.start[2]);
  const F32 dx = Fst(bx - ax);
  const F32 dy = Fst(St(by) - ay);
  const St dz = St(bz) - az;
  for (int i = 0; i < 3; i++) extent[i] = Fst(St(tw.bounds[1][i]) - tw.bounds[0][i]);
  const St c1 = dz * tw.delta[1] - St(dy) * tw.delta[2];
  const St r1 = St(extent[1]) * size[2] + St(extent[2]) * size[1];
  if (r1 * r1 < c1 * c1) return false;
  const St c2 = St(dx) * tw.delta[2] - dz * tw.delta[0];
  const St r2 = St(extent[0]) * size[2] + St(extent[2]) * size[0];
  if (r2 * r2 < c2 * c2) return false;
  const St c3 = St(dy) * tw.delta[0] - St(dx) * tw.delta[1];
  const St r3 = St(extent[0]) * size[1] + St(extent[1]) * size[0];
  if (r3 * r3 < c3 * c3) return false;
  return true;
}

// CM_TraceThroughPatch (0x41B860; EAX patch, EDI tw): patch collision
// (lifted) or terrain, and the surface's shader and contents for a hit.
void TraceThroughPatch(Ctx& c, TraceWork& tw, const Patch& p) {
  if (!TraceMayTouch(tw, p.bounds[0], p.bounds[1])) return;
  const F32 old = tw.trace.fraction;
  if (p.pc) {
    Push(c, p.pc);
    Push(c, Va(&tw));
    Call(c, f_00415A40, 0x41B88E);
    c.esp += 8;
  } else {
    TraceThroughTerrain(c, tw, At<Terrain>(p.terrain));
  }
  if (St(tw.trace.fraction) < old) {
    tw.trace.surfaceFlags = At<int32_t>(ShaderVa(p.shader) + 0x40);
    tw.trace.contents = p.contents;
    tw.trace.material = ShaderVa(p.shader);
  }
}

// CM_TraceThroughLeaf (0x41C0A0; EAX tw, ESI leaf): its brushes, then
// unless cm_noCurves its surfaces, each once per trace, until the trace
// is stopped at its start.
void TraceThroughLeaf(Ctx& c, TraceWork& tw, const Leaf& leaf) {
  for (uint32_t k = 0; k < leaf.numLeafBrushes; k++) {
    const int32_t index = At<int32_t>(Global(kLeafBrushes) + (leaf.firstLeafBrush + k) * 4);
    Brush& b = At<Brush>(Global(kBrushes) + uint32_t(index) * 0x34);
    if (b.checkcount == Checkcount()) continue;
    b.checkcount = Checkcount();
    if (!(b.contents & tw.contents)) continue;
    TraceThroughBrush(tw, b);
    if (St(tw.trace.fraction) == 0.0f) return;
  }
  if (At<int32_t>(Global(kNoCurves) + 0x20)) return;
  for (uint32_t k = 0; k < leaf.numLeafSurfaces; k++) {
    const int32_t index =
        At<int32_t>(Global(kLeafSurfaces) + (leaf.firstLeafSurface + k) * 4);
    Patch& p = At<Patch>(Global(kPatches) + uint32_t(index) * 0x2C);
    if (p.checkcount == Checkcount()) continue;
    p.checkcount = Checkcount();
    if (!(p.contents & tw.contents)) continue;
    TraceThroughPatch(c, tw, p);
    if (St(tw.trace.fraction) == 0.0f) return;
  }
}

// CM_TraceThroughTree (0x41CA60; tw, num, p1f, p2f, p1, p2 on the stack).
// t2 and frac2 are floats in the original (argument slots), t1 and frac
// stay on the x87 stack.
void TraceThroughTree(Ctx& c, TraceWork& tw, int32_t num, F32 p1f, F32 p2f, const F32* p1,
                      const F32* p2) {
  if (St(tw.trace.fraction) <= p1f) return;
  if (num < 0) {
    TraceThroughLeaf(c, tw, At<Leaf>(Global(kLeafs) - uint32_t(num + 1) * 16));
    return;
  }
  const Node& node = At<Node>(Global(kNodes) + uint32_t(num) * 8);
  const Plane& plane = At<Plane>(node.plane);
  St t1, offset;
  F32 t2;
  if (plane.type < 3) {
    t1 = St(p1[plane.type]) - plane.dist;
    t2 = Fst(St(p2[plane.type]) - plane.dist);
    offset = St(tw.size[1][plane.type]);
  } else {
    t1 = ((St(p1[1]) * plane.normal[1] + St(p1[2]) * plane.normal[2]) +
          St(p1[0]) * plane.normal[0]) -
         plane.dist;
    t2 = Fst(((St(p2[1]) * plane.normal[1] + St(p2[2]) * plane.normal[2]) +
              St(p2[0]) * plane.normal[0]) -
             plane.dist);
    offset = St(tw.isPoint ? 0.0f : 2048.0f);
  }
  const St front = 1.0f + offset;
  if (t1 >= front && t2 >= front) {
    TraceThroughTree(c, tw, node.children[0], p1f, p2f, p1, p2);
    return;
  }
  const St back = -offset - 1.0f;
  if (t1 < back && t2 < back) {
    TraceThroughTree(c, tw, node.children[1], p1f, p2f, p1, p2);
    return;
  }

  // Both sides: the near side up to frac, the far one from frac2.
  int side;
  F32 frac2;
  St frac;
  if (t1 < t2) {
    const St idist = 1.0f / (t1 - t2);
    side = 1;
    frac2 = Fst(((offset + t1) + kClipEpsilon) * idist);
    frac = ((t1 - offset) + kClipEpsilon) * idist;
  } else if (t1 > t2) {
    const St idist = 1.0f / (t1 - t2);
    side = 0;
    frac2 = Fst(((t1 - offset) - kClipEpsilon) * idist);
    frac = ((t1 + offset) + kClipEpsilon) * idist;
  } else {
    side = 0;
    frac2 = F32{0};
    frac = St(1.0f);
  }
  if (frac < 0.0f) frac = St(0.0f);
  else if (frac > 1.0f) frac = St(1.0f);
  F32 mid[3];
  for (int i = 0; i < 3; i++) mid[i] = Fst((St(p2[i]) - p1[i]) * frac + p1[i]);
  const F32 span = Fst(St(p2f) - p1f);
  TraceThroughTree(c, tw, node.children[side], p1f, Fst(St(span) * frac + p1f), p1, mid);

  St f2 = St(frac2);
  if (f2 < 0.0f) f2 = St(0.0f);
  else if (f2 > 1.0f) f2 = St(1.0f);
  for (int i = 0; i < 3; i++) mid[i] = Fst((St(p2[i]) - p1[i]) * f2 + p1[i]);
  TraceThroughTree(c, tw, node.children[side ^ 1], Fst(St(span) * f2 + p1f), p2f, mid, p2);
}

// ---- capsules ---------------------------------------------------------------

// CM_TraceThroughSphere (0x41C180; EAX start, EBX origin; tw, end and the
// sphere's radius on the stack): false for a hit.
bool TraceThroughSphere(Ctx& c, TraceWork& tw, const F32* start, const F32* origin,
                        const F32* end, F32 radius) {
  GuestFrame f(c, 0x2C);  // dir at +0x10, normal at +0x1C
  F32* dir = &f.at<F32>(0x10);
  F32* normal = &f.at<F32>(0x1C);
  for (int i = 0; i < 3; i++) dir[i] = Fst(St(start[i]) - origin[i]);
  const St rs = St(radius) + tw.sphere.radius;
  const F32 r2 = Fst(rs * rs);
  const St cs = ((St(dir[2]) * dir[2] + St(dir[1]) * dir[1]) + St(dir[0]) * dir[0]) - r2;
  const F32 cf = Fst(cs);
  if (cs <= 0.0f) {
    tw.trace.fraction = F32{0};
    tw.trace.startsolid = 1;
    Normalize(c, dir, tw.trace.normal, 0x41C20B);
    tw.trace.contents = BoxBrush().contents;
    const St wx = St(end[0]) - origin[0], wy = St(end[1]) - origin[1],
             wz = St(end[2]) - origin[2];
    if ((wz * wz + wy * wy) + wx * wx <= r2) tw.trace.allsolid = 1;
    return false;
  }
  const St bs = (St(dir[2]) * tw.delta[2] + St(dir[1]) * tw.delta[1]) + St(dir[0]) * tw.delta[0];
  const F32 b = Fst(bs);
  if (bs >= 0.0f) return true;
  const F32 a = tw.deltaLenSq;
  const St ds = St(b) * b - St(a) * cf;
  const F32 disc = Fst(ds);
  if (ds < 0.0f) return true;
  const St q = (Normalize(c, dir, normal, 0x41C2C8) * kClipEpsilon) / b;
  St t = q + ((-St(b)) - Sqrt(St(disc))) / a;
  if (!(t < tw.trace.fraction)) return true;
  if (!(t > 0.0f)) t = St(0.0f);
  tw.trace.fraction = Fst(t);
  Copy3(tw.trace.normal, normal);
  tw.trace.contents = BoxBrush().contents;
  return false;
}

// CM_TraceThroughVerticalCylinder (0x41C360; EBX origin; tw, half height
// and radius on the stack): false for a hit.
bool TraceThroughVerticalCylinder(Ctx& c, TraceWork& tw, const F32* origin, F32 halfheight,
                                  F32 radius) {
  GuestFrame f(c, 0x30);  // dir at +0x14, normal at +0x20
  F32* dir = &f.at<F32>(0x14);
  F32* normal = &f.at<F32>(0x20);
  dir[0] = Fst(St(tw.start[0]) - origin[0]);
  dir[1] = Fst(St(tw.start[1]) - origin[1]);
  const St dz = St(tw.start[2]) - origin[2];
  const St rs = St(radius) + tw.sphere.radius;
  const F32 cf = Fst((St(dir[1]) * dir[1] + St(dir[0]) * dir[0]) - rs * rs);
  if (St(cf) <= 0.0f) {
    const F32 h = Fst((St(tw.sphere.halfheight) - tw.sphere.radius) + halfheight);
    if (dz > h) return true;
    const F32 negH = Fst(-St(h));
    if (dz < negH) return true;
    tw.trace.fraction = F32{0};
    tw.trace.startsolid = 1;
    dir[2] = F32{0};
    Normalize(c, dir, tw.trace.normal, 0x41C424);
    tw.trace.contents = BoxBrush().contents;
    const St ez = St(tw.end[2]) - origin[2];
    if (ez <= h && ez >= negH) tw.trace.allsolid = 1;
    return false;
  }
  const St bs = St(dir[1]) * tw.delta[1] + St(dir[0]) * tw.delta[0];
  const F32 b = Fst(bs);
  if (bs >= 0.0f) return true;
  const St dx = St(tw.delta[0]), dy = St(tw.delta[1]);
  const F32 a = Fst(dx * dx + dy * dy);
  const St ds = St(b) * b - St(a) * cf;
  const F32 disc = Fst(ds);
  if (ds < 0.0f) return true;
  dir[2] = F32{0};
  const F32 q = Fst((Normalize(c, dir, normal, 0x41C4E5) * kClipEpsilon) / b);
  const St frs = ((-St(b)) - Sqrt(St(disc))) / a + q;
  const F32 fr = Fst(frs);
  if (!(frs < tw.trace.fraction)) return true;
  const St h2 = (St(tw.sphere.halfheight) - tw.sphere.radius) + halfheight;
  const St z = ((St(fr) - q) * tw.delta[2] + tw.start[2]) - origin[2];
  if (z > h2) return true;
  if (z < -h2) return true;
  tw.trace.fraction = St(fr) > 0.0f ? fr : F32{0};
  Copy3(tw.trace.normal, normal);
  tw.trace.contents = BoxBrush().contents;
  return false;
}

// CM_TraceCapsuleThroughCapsule (0x41C5C0; ESI tw), against the box
// model's capsule: the end spheres where they can lead, then the side.
void TraceCapsuleThroughCapsule(Ctx& c, TraceWork& tw) {
  const F32* mins = &At<F32>(kBoxModel);
  const F32* maxs = mins + 3;
  for (int i = 0; i < 3; i++)
    if (St(maxs[i]) + 1.0f < tw.bounds[0][i]) return;
  for (int i = 0; i < 3; i++)
    if (St(mins[i]) - 1.0f > tw.bounds[1][i]) return;
  // The trace's capsule's upper and lower sphere centres.
  const F32* off = tw.sphere.offset;
  F32 topStart[3], bottomStart[3], topEnd[3], bottomEnd[3];
  topStart[0] = Fst(St(off[0]) + tw.start[0]);
  topStart[1] = Fst(St(tw.start[1]) + off[1]);
  topStart[2] = Fst(St(off[2]) + tw.start[2]);
  for (int i = 0; i < 3; i++) bottomStart[i] = Fst(St(tw.start[i]) - off[i]);
  const St bottomStartZ = St(tw.start[2]) - off[2];
  topEnd[0] = Fst(St(off[0]) + tw.end[0]);
  topEnd[1] = Fst(St(tw.end[1]) + off[1]);
  topEnd[2] = Fst(St(off[2]) + tw.end[2]);
  for (int i = 0; i < 3; i++) bottomEnd[i] = Fst(St(tw.end[i]) - off[i]);
  // The box model's capsule: centre, radius, half the distance between
  // its sphere centres.
  const St cx = (St(mins[0]) + maxs[0]) * 0.5f;
  const St rx = St(maxs[0]) - cx;
  F32 center[3] = {Fst(cx), Fst((St(mins[1]) + maxs[1]) * 0.5f),
                   Fst((St(mins[2]) + maxs[2]) * 0.5f)};
  const F32 hz = Fst(St(maxs[2]) - center[2]);
  const F32 radius = rx > hz ? hz : Fst(rx);
  const F32 halfheight = Fst(St(hz) - radius);
  const F32 top[3] = {center[0], center[1], Fst(St(center[2]) + halfheight)};
  const F32 bottom[3] = {center[0], center[1], Fst(St(center[2]) - halfheight)};

  if (bottomStartZ > top[2]) {
    if (TraceThroughSphere(c, tw, bottomStart, top, bottomEnd, radius)) {
      if (St(tw.delta[2]) >= 0.0f) return;
    } else {
      return;
    }
  } else if (St(topStart[2]) < bottom[2]) {
    if (TraceThroughSphere(c, tw, topStart, bottom, topEnd, radius)) {
      if (St(tw.delta[2]) <= 0.0f) return;
    } else {
      return;
    }
  }
  if (!TraceThroughVerticalCylinder(c, tw, center, halfheight, radius)) return;
  if (St(bottomEnd[2]) > top[2]) {
    if (St(bottomStart[2]) <= top[2])
      TraceThroughSphere(c, tw, bottomStart, top, bottomEnd, radius);
    return;
  }
  if (!(St(topEnd[2]) < bottom[2])) return;
  if (!(St(topStart[2]) >= bottom[2])) return;
  TraceThroughSphere(c, tw, topStart, bottom, topEnd, radius);
}

// CM_TraceBoundingBoxThroughCapsule (0x41C8D0; ECX tw): the roles
// swapped, in the capsule's frame. The trace becomes the capsule's, the
// box model becomes the trace's box, and the box brush is traced.
void TraceBoundingBoxThroughCapsule(TraceWork& tw) {
  F32* mins = &At<F32>(kBoxModel);
  F32* maxs = mins + 3;
  const St cx = (St(maxs[0]) + mins[0]) * 0.5f;
  const F32 rx = Fst(St(maxs[0]) - cx);
  tw.start[0] = Fst(St(tw.start[0]) - cx);
  tw.end[0] = Fst(St(tw.end[0]) - cx);
  const St cy = (St(maxs[1]) + mins[1]) * 0.5f;
  tw.start[1] = Fst(St(tw.start[1]) - cy);
  tw.end[1] = Fst(St(tw.end[1]) - cy);
  const F32 cz = Fst((St(maxs[2]) + mins[2]) * 0.5f);
  const St hz = St(maxs[2]) - cz;
  tw.start[2] = Fst(St(tw.start[2]) - cz);
  tw.end[2] = Fst(St(tw.end[2]) - cz);
  tw.sphere.use = 1;
  const St r = St(rx) > hz ? hz : St(rx);
  tw.sphere.radius = Fst(r);
  tw.sphere.halfheight = Fst(hz);
  tw.sphere.offset[0] = tw.sphere.offset[1] = F32{0};
  tw.sphere.offset[2] = Fst(hz - r);
  tw.sphere.extents[0] = tw.sphere.extents[1] = Fst(r);
  tw.sphere.extents[2] = Fst(hz);
  const int32_t contents = BoxBrush().contents;
  Copy3(mins, tw.size[0]);
  Copy3(maxs, tw.size[1]);
  Brush& box = BoxBrush();
  Copy3(box.bounds[0], tw.size[0]);
  Copy3(box.bounds[1], tw.size[1]);
  box.contents = contents;
  TraceThroughBrush(tw, box);
}

// ---- position tests ----------------------------------------------------------

// CM_TestBoxInBrush (0x41AE60; ECX tw, brush on the stack): inside every
// plane, the bounds' included, at the start.
void TestBoxInBrush(TraceWork& tw, const Brush& b) {
  for (int i = 0; i < 3; i++)
    if (St(tw.bounds[0][i]) > b.bounds[1][i]) return;
  for (int i = 0; i < 3; i++)
    if (St(tw.bounds[1][i]) < b.bounds[0][i]) return;
  const BrushSide* sides = &At<BrushSide>(b.sides);
  for (int32_t k = 0; k < b.numsides; k++) {
    const Plane& p = At<Plane>(sides[k].plane);
    St d;
    if (tw.sphere.use) {
      const F32* off = tw.sphere.offset;
      const St r = St(p.dist) + tw.sphere.radius;
      const St t = (St(off[1]) * p.normal[1] + St(p.normal[0]) * off[0]) + St(off[2]) * p.normal[2];
      St px, py, pz;
      if (t > 0.0f) {
        px = St(tw.start[0]) - off[0];
        py = St(tw.start[1]) - off[1];
        pz = St(tw.start[2]) - off[2];
      } else {
        px = St(off[0]) + tw.start[0];
        py = St(tw.start[1]) + off[1];
        pz = St(tw.start[2]) + off[2];
      }
      d = ((px * p.normal[0] + pz * p.normal[2]) + py * p.normal[1]) - r;
    } else {
      const F32* o = tw.offsets[p.signbits];
      const St dot = (St(tw.start[1]) * p.normal[1] + St(p.normal[0]) * tw.start[0]) +
                     St(tw.start[2]) * p.normal[2];
      d = dot - (p.dist - ((St(o[2]) * p.normal[2] + St(o[0]) * p.normal[0]) +
                           St(o[1]) * p.normal[1]));
    }
    if (d > 0.0f) return;
  }
  tw.trace.allsolid = tw.trace.startsolid = 1;
  tw.trace.fraction = F32{0};
  tw.trace.contents = b.contents;
}

// A capsule against terrain at its start (0x419EE0; ESI tw, terrain on
// the stack): the same moves and triangle tests as the sweep (with the
// radius alone), then whether an edge or vertex outside which the start
// lies is inside the capsule's end sphere. No checkcounts.
bool TerrainInSolid(TraceWork& tw, const Terrain& terrain) {
  const F32 h2 = ToTerrainEnd(tw, terrain);
  const F32 r = tw.sphere.radius, negR = Fst(-St(r));
  for (uint32_t i = 0; i < terrain.numTris; i++) {
    const TerrainTri& t = terrain.tris[i];
    const St d1s = ((St(t.plane[2]) * tw.start[2] + St(tw.start[1]) * t.plane[1]) +
                    St(tw.start[0]) * t.plane[0]) -
                   t.plane[3];
    const F32 d1 = Fst(d1s);
    if (d1s >= r) continue;
    if (St(d1) <= negR) {
      if (InsideFromBehind(tw, t, d1, h2, r, negR)) return true;
      continue;
    }
    // The start projected onto the plane.
    const St nd = -St(d1);
    const F32 px = Fst(nd * t.plane[0] + tw.start[0]);
    const F32 py = Fst(nd * t.plane[1] + tw.start[1]);
    const St pzs = nd * t.plane[2] + tw.start[2];
    const F32 pz = Fst(pzs);
    const St b1 = ((pzs * t.edge[0][2] + St(py) * t.edge[0][1]) + St(px) * t.edge[0][0]) - t.edge[0][3];
    const St b2s =
        ((St(pz) * t.edge[1][2] + St(py) * t.edge[1][1]) + St(px) * t.edge[1][0]) - t.edge[1][3];
    const F32 b2 = Fst(b2s);
    const uint32_t outside =
        (b2s + b1 > 1.0f ? 1u : 0u) | (b1 < 0.0f ? 2u : 0u) | (St(b2) < 0.0f ? 4u : 0u);
    if (!outside) return true;
    for (int k = 0; k < 3; k++) {
      if (outside & (1u << k)) {
        if (!t.edges[k]) continue;
        const TerrainEdge& e = At<TerrainEdge>(t.edges[k]);
        const F32 vx = Fst(St(tw.start[0]) - e.origin[0]);
        const F32 vy = Fst(St(tw.start[1]) - e.origin[1]);
        const St vzs = St(tw.start[2]) - e.origin[2];
        const F32 vz = Fst(vzs);
        const St p2s = (vzs * e.axis[2][2] + St(vy) * e.axis[2][1]) + St(vx) * e.axis[2][0];
        const F32 p2 = Fst(p2s);
        if (!(p2s >= 0.0f)) continue;
        if (!(St(p2) <= e.length)) continue;
        // The start's offset from the edge, across it.
        const St np = -St(p2);
        const St qx = np * e.axis[2][0] + vx, qy = np * e.axis[2][1] + vy,
                 qz = np * e.axis[2][2] + vz;
        const St rr = St(tw.sphere.radius);
        if (rr * rr > (qz * qz + qy * qy) + qx * qx) return true;
      } else {
        if (!t.verts[k]) continue;
        const TerrainVert& v = At<TerrainVert>(t.verts[k]);
        const St dx = St(tw.start[0]) - v.xyz[0], dy = St(tw.start[1]) - v.xyz[1],
                 dz = St(tw.start[2]) - v.xyz[2];
        const St rr = St(tw.sphere.radius);
        if (rr * rr > (dz * dz + dy * dy) + dx * dx) return true;
      }
    }
  }
  return false;
}

// CM_TestInLeaf (0x41B020; EAX tw, leaf on the stack): its brushes, then
// unless cm_noCurves its surfaces (patch collision, lifted, or terrain for
// all but points), each once per position test, until all solid.
void TestInLeaf(Ctx& c, TraceWork& tw, const Leaf& leaf) {
  for (uint32_t k = 0; k < leaf.numLeafBrushes; k++) {
    const int32_t index = At<int32_t>(Global(kLeafBrushes) + (leaf.firstLeafBrush + k) * 4);
    Brush& b = At<Brush>(Global(kBrushes) + uint32_t(index) * 0x34);
    if (b.checkcount == Checkcount()) continue;
    b.checkcount = Checkcount();
    if (!(b.contents & tw.contents)) continue;
    TestBoxInBrush(tw, b);
    if (tw.trace.allsolid) return;
  }
  if (At<int32_t>(Global(kNoCurves) + 0x20)) return;
  for (uint32_t k = 0; k < leaf.numLeafSurfaces; k++) {
    const int32_t index =
        At<int32_t>(Global(kLeafSurfaces) + (leaf.firstLeafSurface + k) * 4);
    Patch& p = At<Patch>(Global(kPatches) + uint32_t(index) * 0x2C);
    if (p.checkcount == Checkcount()) continue;
    p.checkcount = Checkcount();
    if (!(p.contents & tw.contents)) continue;
    bool solid;
    if (p.pc) {
      c.ecx = Va(&tw);
      c.eax = p.pc;
      Call(c, f_004163C0, 0x41B0E4);
      solid = c.eax != 0;
    } else {
      if (tw.isPoint) continue;
      const F32 sz = tw.start[2], ez = tw.end[2];
      solid = TerrainInSolid(tw, At<Terrain>(p.terrain));
      tw.start[2] = sz;
      tw.end[2] = ez;
    }
    if (solid) {
      tw.trace.allsolid = tw.trace.startsolid = 1;
      tw.trace.fraction = F32{0};
      return;
    }
  }
}

// CM_PositionTest (0x41B560; ESI tw): the leaves the bounds (+1) touch,
// from CM_BoxLeafnums_r (lifted) into a list in its guest frame, then
// each leaf until all solid.
void PositionTest(Ctx& c, TraceWork& tw) {
  GuestFrame f(c, 0x1038);  // the leaf list at +4 (count, max, overflowed, list, bounds, ...)
  const uint32_t ll = c.esp + 4, list = c.esp + 0x34;
  F32* bounds = &f.at<F32>(0x14);
  const St mx = St(tw.size[0][0]) + tw.start[0], my = St(tw.size[0][1]) + tw.start[1];
  bounds[2] = Fst(St(tw.size[0][2]) + tw.start[2]);
  const St bx = St(tw.size[1][0]) + tw.start[0];
  bounds[4] = Fst(St(tw.size[1][1]) + tw.start[1]);
  bounds[5] = Fst(St(tw.size[1][2]) + tw.start[2]);
  At<int32_t>(kCheckcount)++;
  bounds[0] = Fst(mx - 1.0f);
  bounds[3] = Fst(bx + 1.0f);
  bounds[1] = Fst(my - 1.0f);
  bounds[4] = Fst(St(bounds[4]) + 1.0f);
  bounds[2] = Fst(St(bounds[2]) - 1.0f);
  bounds[5] = Fst(St(bounds[5]) + 1.0f);
  Wr<uint32_t>(g_guest_base, ll, 0);           // count
  Wr<uint32_t>(g_guest_base, ll + 4, 0x400);   // maxcount
  Wr<uint32_t>(g_guest_base, ll + 8, 0);       // overflowed
  Wr<uint32_t>(g_guest_base, ll + 0xC, list);
  Wr<uint32_t>(g_guest_base, ll + 0x28, 0);    // lastLeaf
  Wr<uint32_t>(g_guest_base, ll + 0x2C, 0x41A4D0);  // CM_StoreLeafs
  Push(c, 0);
  Push(c, ll);
  c.eax = list;
  c.ecx = ll;
  Call(c, f_0041A5D0, 0x41B62E);
  c.esp += 8;
  At<int32_t>(kCheckcount)++;
  for (uint32_t i = 0; i < Rd<uint32_t>(g_guest_base, ll); i++) {
    const uint32_t leaf = Global(kLeafs) + (Rd<uint32_t>(g_guest_base, list + i * 4) << 4);
    TestInLeaf(c, tw, At<Leaf>(leaf));
    if (tw.trace.allsolid) break;
  }
}

// CM_TestCapsuleInCapsule (0x41B150; ECX tw), against the box model's
// capsule: each pair of end spheres, then the side.
void TestCapsuleInCapsule(TraceWork& tw) {
  const F32* mins = &At<F32>(kBoxModel);
  const F32* maxs = mins + 3;
  const F32* off = tw.sphere.offset;
  const F32 top[3] = {Fst(St(tw.start[0]) + off[0]), Fst(St(off[1]) + tw.start[1]),
                      Fst(St(off[2]) + tw.start[2])};
  const St bottomX = St(tw.start[0]) - off[0];
  const F32 bottom[2] = {Fst(St(tw.start[1]) - off[1]), Fst(St(tw.start[2]) - off[2])};  // y, z
  const F32 cx = Fst((St(mins[0]) + maxs[0]) * 0.5f);
  const St rx = St(maxs[0]) - cx;
  const F32 cy = Fst((St(maxs[1]) + mins[1]) * 0.5f);
  const F32 cz = Fst((St(maxs[2]) + mins[2]) * 0.5f);
  const F32 hz = Fst(St(maxs[2]) - cz);
  const St radius = rx > hz ? St(hz) : rx;
  const F32 halfheight = Fst(St(hz) - radius);
  const St rs = radius + tw.sphere.radius;
  const F32 r2 = Fst(rs * rs);
  auto solid = [&] {
    tw.trace.allsolid = tw.trace.startsolid = 1;
    tw.trace.fraction = F32{0};
  };
  // The box capsule's upper sphere against the trace's upper, then lower.
  F32 z = Fst(St(cz) + halfheight);
  const St dx = St(cx) - top[0], dy = St(cy) - top[1];
  const F32 dxf = Fst(dx), dyf = Fst(dy);
  {
    const St dz = St(z) - top[2];
    if ((dz * dz + dy * dy) + dx * dx < r2) solid();
  }
  const St bx = cx - bottomX;
  const F32 bxf = Fst(bx);
  const St by = St(cy) - bottom[0];
  {
    const St dz = St(z) - bottom[1];
    if ((dz * dz + by * by) + St(bxf) * bxf < r2) solid();
  }
  // Its lower sphere against both.
  z = Fst(St(cz) - halfheight);
  {
    const St dz = St(z) - top[2];
    const St fx = St(dxf), fy = St(dyf);
    if ((dz * dz + fy * fy) + fx * fx < r2) solid();
  }
  {
    const St dz = St(z) - bottom[1];
    if ((dz * dz + by * by) + St(bxf) * bxf < r2) solid();
  }
  // The side: within the height of both, then the horizontal distance.
  const St zc = St(tw.start[2]) - cz;
  const St h = (St(halfheight) + tw.sphere.halfheight) - tw.sphere.radius;
  if (!(zc <= h)) return;
  if (!(zc >= -h)) return;
  const St ex = St(top[0]) - cx, ey = St(top[1]) - cy;
  if (!(ey * ey + ex * ex < r2)) return;
  tw.trace.fraction = F32{0};
  tw.trace.allsolid = tw.trace.startsolid = 1;
}

// CM_TestBoundingBoxInCapsule (0x41B3E0; ECX tw): as the trace (0x41C8D0)
// but without the extents, and the box brush tested at the start.
void TestBoundingBoxInCapsule(TraceWork& tw) {
  F32* mins = &At<F32>(kBoxModel);
  F32* maxs = mins + 3;
  const St cx = (St(maxs[0]) + mins[0]) * 0.5f;
  const F32 rx = Fst(St(maxs[0]) - cx);
  tw.start[0] = Fst(St(tw.start[0]) - cx);
  tw.end[0] = Fst(St(tw.end[0]) - cx);
  const St cy = (St(maxs[1]) + mins[1]) * 0.5f;
  tw.start[1] = Fst(St(tw.start[1]) - cy);
  tw.end[1] = Fst(St(tw.end[1]) - cy);
  const St cz = (St(maxs[2]) + mins[2]) * 0.5f;
  const F32 hz = Fst(St(maxs[2]) - cz);
  tw.start[2] = Fst(St(tw.start[2]) - cz);
  tw.end[2] = Fst(St(tw.end[2]) - cz);
  tw.sphere.use = 1;
  const F32 r = St(rx) > hz ? hz : rx;
  tw.sphere.radius = r;
  tw.sphere.halfheight = hz;
  tw.sphere.offset[0] = tw.sphere.offset[1] = F32{0};
  tw.sphere.offset[2] = Fst(St(hz) - r);
  const int32_t contents = BoxBrush().contents;
  Copy3(mins, tw.size[0]);
  Copy3(maxs, tw.size[1]);
  Brush& box = BoxBrush();
  Copy3(box.bounds[0], tw.size[0]);
  Copy3(box.bounds[1], tw.size[1]);
  box.contents = contents;
  TestBoxInBrush(tw, box);
}

// ---- traces -----------------------------------------------------------------

// CM_Trace (0x41CD60; EAX sphere or null, EDX mins, ECX maxs; results,
// start, end, model, origin (unused), brushmask, capsule on the stack).
// results->fraction is the fraction to beat (the callers set it). Guest
// ESP: as at the original's call instruction; the frame (0x134 bytes
// below the return address) keeps the trace work at +0x1C.
void CmTrace(Ctx& c, uint32_t sphere, uint32_t minsVa, uint32_t maxsVa, uint32_t results,
           uint32_t startVa, uint32_t endVa, int32_t model, int32_t brushmask, int32_t capsule) {
  GuestFrame f(c, 0x138);
  c.eax = uint32_t(model);
  Call(c, f_00412AD0, 0x41CD83);
  const uint32_t cmodel = c.eax;
  At<int32_t>(kCheckcount)++;
  TraceWork& tw = f.at<TraceWork>(0x1C);
  std::memset(&tw, 0, sizeof tw);
  tw.trace.fraction = At<F32>(results);
  tw.contents = brushmask;
  const F32* mins = &At<F32>(minsVa ? minsVa : kVec3Origin);
  const F32* maxs = &At<F32>(maxsVa ? maxsVa : kVec3Origin);
  const F32* start = &At<F32>(startVa);
  const F32* end = &At<F32>(endVa);

  // Centre the box on the origin.
  St dz;
  for (int i = 0; i < 3; i++) {
    const St off = (St(mins[i]) + maxs[i]) * 0.5f;
    tw.size[0][i] = Fst(St(mins[i]) - off);
    tw.size[1][i] = Fst(St(maxs[i]) - off);
    tw.start[i] = Fst(off + start[i]);
    const St e = off + end[i];
    tw.end[i] = Fst(e);
    dz = e - tw.start[i];
    tw.delta[i] = Fst(dz);
  }
  tw.deltaLenSq = Fst((dz * tw.delta[2] + St(tw.delta[1]) * tw.delta[1]) +
                      St(tw.delta[0]) * tw.delta[0]);

  if (sphere) {
    tw.sphere = At<Sphere>(sphere);
  } else {
    tw.sphere.use = capsule;
    tw.sphere.radius = St(tw.size[1][0]) > tw.size[1][2] ? tw.size[1][2] : tw.size[1][0];
    tw.sphere.halfheight = tw.size[1][2];
    tw.sphere.offset[0] = tw.sphere.offset[1] = F32{0};
    tw.sphere.offset[2] = Fst(St(tw.size[1][2]) - tw.sphere.radius);
  }
  const int32_t use = tw.sphere.use;
  tw.extentSum = Fst((St(tw.size[1][1]) + tw.size[1][2]) + tw.size[1][0]);
  for (int k = 0; k < 8; k++)
    for (int i = 0; i < 3; i++) tw.offsets[k][i] = tw.size[(k >> i) & 1][i];

  // The bounds of the whole move.
  if (use) {
    for (int i = 0; i < 3; i++) {
      const St off = Abs(St(tw.sphere.offset[i]));
      if (St(tw.start[i]) < tw.end[i]) {
        tw.bounds[0][i] = Fst((tw.start[i] - off) - tw.sphere.radius);
        tw.bounds[1][i] = Fst((off + tw.end[i]) + tw.sphere.radius);
      } else {
        tw.bounds[0][i] = Fst((tw.end[i] - off) - tw.sphere.radius);
        tw.bounds[1][i] = Fst((off + tw.start[i]) + tw.sphere.radius);
      }
    }
  } else {
    for (int i = 0; i < 3; i++) {
      if (St(tw.start[i]) < tw.end[i]) {
        tw.bounds[0][i] = Fst(St(tw.start[i]) + tw.size[0][i]);
        tw.bounds[1][i] = Fst(St(tw.end[i]) + tw.size[1][i]);
      } else {
        tw.bounds[0][i] = Fst(St(tw.end[i]) + tw.size[0][i]);
        tw.bounds[1][i] = Fst(St(tw.start[i]) + tw.size[1][i]);
      }
    }
  }

  if (St(start[0]) == end[0] && St(start[1]) == end[1] && St(start[2]) == end[2]) {
    // No move: the position tests.
    if (model == 0) {
      PositionTest(c, tw);
    } else if (model == kCapsuleModel) {
      if (BoxBrush().contents & tw.contents) {
        if (use)
          TestCapsuleInCapsule(tw);
        else
          TestBoundingBoxInCapsule(tw);
      }
    } else {
      TestInLeaf(c, tw, At<Leaf>(cmodel + 0x18));
    }
  } else {
    tw.isPoint = (St(tw.size[1][1]) + tw.size[1][2]) + tw.size[1][0] == 0.0f;
    if (use)
      for (int i = 0; i < 3; i++)
        tw.sphere.extents[i] =
            Fst(St(F32{tw.sphere.offset[i].bits & 0x7FFFFFFFu}) + tw.sphere.radius);
    if (model == 0) {
      TraceThroughTree(c, tw, 0, F32{0}, tw.trace.fraction, tw.start, tw.end);
    } else if (model == kCapsuleModel) {
      if (BoxBrush().contents & tw.contents) {
        if (use)
          TraceCapsuleThroughCapsule(c, tw);
        else
          TraceBoundingBoxThroughCapsule(tw);
      }
    } else {
      TraceThroughLeaf(c, tw, At<Leaf>(cmodel + 0x18));
    }
  }

  for (int i = 0; i < 3; i++)
    tw.trace.endpos[i] = Fst(St(tw.delta[i]) * tw.trace.fraction + start[i]);
  At<Trace>(results) = tw.trace;
}

// 0x41D3B0, the body of CM_TransformedBoxTrace (EAX mins, ECX maxs, EDX
// angles; results, start, end, model, brushmask, origin, capsule on the
// stack): the move in the model's frame, CM_Trace, and the normal back.
// Guest ESP as at the call; its locals (0xCC bytes below the return
// address) hold the angle vectors and CM_Trace's arguments.
void TransformedTrace(Ctx& c, uint32_t minsVa, uint32_t maxsVa, uint32_t anglesVa,
                      uint32_t results, uint32_t startVa, uint32_t endVa, int32_t model,
                      int32_t brushmask, uint32_t originVa, int32_t capsule) {
  GuestFrame f(c, 0xD0);
  const F32* mins = &At<F32>(minsVa ? minsVa : kVec3Origin);
  const F32* maxs = &At<F32>(maxsVa ? maxsVa : kVec3Origin);
  const F32* start = &At<F32>(startVa);
  const F32* end = &At<F32>(endVa);
  const F32* origin = &At<F32>(originVa);
  const F32* angles = &At<F32>(anglesVa);
  F32* localEnd = &f.at<F32>(0x14);
  F32* localStart = &f.at<F32>(0x20);
  F32* axis = &f.at<F32>(0x3C);  // forward, right (negated: left), up
  Sphere& sphere = f.at<Sphere>(0x60);
  F32* size = &f.at<F32>(0x84);  // mins, maxs about the centre
  Trace& trace = f.at<Trace>(0x9C);

  for (int i = 0; i < 3; i++) {
    const F32 off = Fst((St(mins[i]) + maxs[i]) * 0.5f);
    size[i] = Fst(St(mins[i]) - off);
    size[3 + i] = Fst(St(maxs[i]) - off);
    localStart[i] = Fst(St(off) + start[i]);
    localEnd[i] = Fst(St(off) + end[i]);
  }
  for (int i = 0; i < 3; i++) localStart[i] = Fst(St(localStart[i]) - origin[i]);
  for (int i = 0; i < 3; i++) localEnd[i] = Fst(St(localEnd[i]) - origin[i]);
  const bool rotated =
      model != kBoxModelHandle &&
      (St(angles[0]) != 0.0f || St(angles[1]) != 0.0f || St(angles[2]) != 0.0f);

  sphere.use = capsule;
  sphere.radius = St(size[3]) > size[5] ? size[5] : size[3];
  sphere.halfheight = size[5];
  const F32 t = Fst(St(size[5]) - sphere.radius);
  if (rotated) {
    const uint32_t ebx = c.ebx, esi = c.esi, edi = c.edi;
    c.edx = anglesVa;
    c.esi = Va(axis);
    c.edi = Va(axis + 3);
    c.ebx = Va(axis + 6);
    Call(c, f_004271E0, 0x41D598);
    c.ebx = ebx;
    c.esi = esi;
    c.edi = edi;
    for (int i = 0; i < 3; i++) axis[3 + i] = Fst(-St(axis[3 + i]));
    const F32* a0 = axis;
    const F32* a1 = axis + 3;
    const F32* a2 = axis + 6;
    F32* s = localStart;
    const F32 sx = s[0], sy = s[1];
    s[0] = Fst((St(a0[1]) * s[1] + St(a0[0]) * s[0]) + St(a0[2]) * s[2]);
    s[1] = Fst((St(a1[0]) * sx + St(a1[1]) * s[1]) + St(a1[2]) * s[2]);
    s[2] = Fst((St(a2[1]) * sy + St(a2[0]) * sx) + St(a2[2]) * s[2]);
    F32* e = localEnd;
    const F32 ex = e[0], ey = e[1];
    e[0] = Fst((St(a0[1]) * e[1] + St(a0[0]) * e[0]) + St(a0[2]) * e[2]);
    e[1] = Fst((St(a1[0]) * ex + St(a1[1]) * e[1]) + St(a1[2]) * e[2]);
    e[2] = Fst((St(a2[1]) * ey + St(a2[0]) * ex) + St(a2[2]) * e[2]);
    sphere.offset[0] = Fst(St(a0[2]) * t);
    sphere.offset[1] = Fst(-(St(a1[2]) * t));
    sphere.offset[2] = Fst(St(a2[2]) * t);
  } else {
    sphere.offset[0] = sphere.offset[1] = F32{0};
    sphere.offset[2] = t;
  }

  trace.fraction = At<F32>(results);
  c.esp = f.esp - 0xD0 - 0x1C;  // CM_Trace's seven arguments
  CmTrace(c, Va(&sphere), Va(size), Va(size + 3), Va(&trace), Va(localStart), Va(localEnd), model,
        brushmask, capsule);
  c.esp = f.esp - 0xD0;

  if (rotated && St(trace.fraction) < At<F32>(results)) {
    // The normal back to the world: by the transposed axes.
    const F32* a0 = axis;
    const F32* a1 = axis + 3;
    const F32* a2 = axis + 6;
    F32* n = trace.normal;
    const F32 nx = n[0], ny = n[1];
    n[0] = Fst((St(a2[0]) * n[2] + St(a1[0]) * n[1]) + St(a0[0]) * n[0]);
    n[1] = Fst((St(a2[1]) * n[2] + St(a1[1]) * n[1]) + St(a0[1]) * nx);
    n[2] = Fst((St(a2[2]) * n[2] + St(a1[2]) * ny) + St(a0[2]) * nx);
  }
  for (int i = 0; i < 3; i++)
    trace.endpos[i] = Fst((St(end[i]) - start[i]) * trace.fraction + start[i]);
  At<Trace>(results) = trace;
}

// ---- point contents -----------------------------------------------------

// 0x41A730 (ECX p, EAX model): the contents of the brushes of the model's
// leaf, or of the world's leaf at p (model 0), that hold p. Guest ESP as
// at the call (the return address on top).
int32_t PointContents(Ctx& c, const F32* p, int32_t model) {
  const Leaf* leaf;
  if (model) {
    // Its two locals and EDI are pushed when it calls CM_ClipHandleToModel.
    c.esp -= 0xC;
    c.eax = uint32_t(model);
    Call(c, f_00412AD0, 0x41A73F);
    c.esp += 0xC;
    leaf = &At<Leaf>(c.eax + 0x18);
  } else {
    // 0x41A460 from node 0 (ECX node, EDI p): the leaf at p.
    int32_t num = 0;
    do {
      const Node& node = At<Node>(Global(kNodes) + uint32_t(num) * 8);
      const Plane& plane = At<Plane>(node.plane);
      const St d = plane.type < 3 ? St(p[plane.type])
                                  : (St(plane.normal[2]) * p[2] + St(plane.normal[1]) * p[1]) +
                                        St(plane.normal[0]) * p[0];
      num = d - plane.dist < 0.0f ? node.children[1] : node.children[0];
    } while (num >= 0);
    leaf = &At<Leaf>(Global(kLeafs) + uint32_t(-1 - num) * 0x10);
  }
  int32_t contents = 0;
  const int32_t* leafBrushes = &At<int32_t>(Global(kLeafBrushes)) + leaf->firstLeafBrush;
  for (int k = 0; k < leaf->numLeafBrushes; k++) {
    const Brush& b = At<Brush>(Global(kBrushes) + uint32_t(leafBrushes[k]) * 0x34);
    bool inside = true;
    for (int i = 0; i < 3 && inside; i++)
      inside = !(St(p[i]) < b.bounds[0][i]) && !(St(p[i]) > b.bounds[1][i]);
    const BrushSide* side = &At<BrushSide>(b.sides);
    for (int32_t n = b.numsides; inside && n; n--, side++) {
      const Plane& plane = At<Plane>(side->plane);
      const St d = (St(plane.normal[2]) * p[2] + St(plane.normal[1]) * p[1]) +
                   St(plane.normal[0]) * p[0];
      inside = !(d > plane.dist);
    }
    if (inside) contents |= b.contents;
  }
  return contents;
}

// 0x41A820 (ECX p, EAX origin, EDX angles; model on the stack): point
// contents in the model's frame. Its locals (0x34 bytes below the return
// address, EBP saved at the bottom) hold p there and the angle vectors.
int32_t TransformedPointContents(Ctx& c, const F32* p, const F32* origin, uint32_t anglesVa,
                                 int32_t model) {
  GuestFrame f(c, 0x34);
  F32* local = &f.at<F32>(0x4);
  F32* forward = &f.at<F32>(0x10);
  F32* right = &f.at<F32>(0x1C);
  F32* up = &f.at<F32>(0x28);
  f.at<uint32_t>(0) = c.ebp;
  for (int i = 0; i < 3; i++) local[i] = Fst(St(p[i]) - origin[i]);
  const F32* angles = &At<F32>(anglesVa);
  // Rotated unless every angle is 0 (a NaN angle rotates).
  if (model != kBoxModelHandle &&
      !(St(angles[0]) == 0.0f && St(angles[1]) == 0.0f && St(angles[2]) == 0.0f)) {
    const uint32_t ebx = c.ebx, esi = c.esi, edi = c.edi;
    c.esp -= 0xC;
    Wr<uint32_t>(g_guest_base, c.esp + 8, ebx);
    Wr<uint32_t>(g_guest_base, c.esp + 4, esi);
    Wr<uint32_t>(g_guest_base, c.esp, edi);
    c.edx = anglesVa;
    c.esi = Va(forward);
    c.edi = Va(right);
    c.ebx = Va(up);
    Call(c, f_004271E0, 0x41A893);
    c.esp += 0xC;
    c.ebx = ebx;
    c.esi = esi;
    c.edi = edi;
    const F32 x = local[0], y = local[1];
    local[0] = Fst((St(forward[2]) * local[2] + St(forward[1]) * y) + St(forward[0]) * x);
    local[1] = Fst(-((St(right[0]) * St(x) + St(right[2]) * local[2]) + St(right[1]) * y));
    local[2] = Fst((St(up[1]) * St(y) + St(up[0]) * St(x)) + St(up[2]) * local[2]);
  }
  c.esp -= 4;  // its call of 0x41A730
  const int32_t contents = PointContents(c, local, model);
  c.esp += 4;
  return contents;
}

// The callee-saved registers, restored on return: lifted callees called
// with the original's register arguments may leave them changed. A guest
// longjmp unwinding through here has set them already.
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

uint32_t Arg(const Ctx& c, int n) { return Rd<uint32_t>(g_guest_base, c.esp + 4 * n); }

}  // namespace

// 0x41A730 (see PointContents).
void f_0041A730(Ctx& c) {
  SavedRegs saved(c);
  c.eax = uint32_t(PointContents(c, &At<F32>(c.ecx), int32_t(c.eax)));
  c.esp += 4;
}

// 0x41A820 (see TransformedPointContents).
void f_0041A820(Ctx& c) {
  SavedRegs saved(c);
  c.eax = uint32_t(TransformedPointContents(c, &At<F32>(c.ecx), &At<F32>(c.eax), c.edx,
                                            int32_t(Arg(c, 1))));
  c.esp += 4;
}

// CM_Trace (see CmTrace).
void f_0041CD60(Ctx& c) {
  SavedRegs saved(c);
  c.esp += 4;  // as at the call
  CmTrace(c, c.eax, c.edx, c.ecx, Arg(c, 0), Arg(c, 1), Arg(c, 2), int32_t(Arg(c, 3)),
        int32_t(Arg(c, 5)), int32_t(Arg(c, 6)));
}

// CM_BoxTrace (EAX results, EDX brushmask, ECX capsule; start, end, mins,
// maxs, model on the stack).
void f_0041D380(Ctx& c) {
  SavedRegs saved(c);
  const uint32_t results = c.eax;
  At<F32>(results) = F32{0x3F800000u};
  const uint32_t start = Arg(c, 1), end = Arg(c, 2), mins = Arg(c, 3), maxs = Arg(c, 4);
  const int32_t model = int32_t(Arg(c, 5)), brushmask = int32_t(c.edx), capsule = int32_t(c.ecx);
  c.esp -= 0x1C;  // its seven pushes, as at its call
  CmTrace(c, 0, mins, maxs, results, start, end, model, brushmask, capsule);
  c.esp += 0x1C + 4;
}

// 0x41D3B0 (see TransformedTrace).
void f_0041D3B0(Ctx& c) {
  SavedRegs saved(c);
  c.esp += 4;
  TransformedTrace(c, c.eax, c.ecx, c.edx, Arg(c, 0), Arg(c, 1), Arg(c, 2), int32_t(Arg(c, 3)),
                   int32_t(Arg(c, 4)), Arg(c, 5), int32_t(Arg(c, 6)));
}

// CM_TransformedBoxTrace (EAX results, EDX origin, ECX capsule; start,
// end, mins, maxs, model, brushmask, angles on the stack).
void f_0041D870(Ctx& c) {
  SavedRegs saved(c);
  const uint32_t results = c.eax;
  At<F32>(results) = F32{0x3F800000u};
  const uint32_t start = Arg(c, 1), end = Arg(c, 2), mins = Arg(c, 3), maxs = Arg(c, 4),
                 angles = Arg(c, 7);
  const int32_t model = int32_t(Arg(c, 5)), brushmask = int32_t(Arg(c, 6));
  const uint32_t origin = c.edx;
  const int32_t capsule = int32_t(c.ecx);
  c.esp -= 0x1C;
  TransformedTrace(c, mins, maxs, angles, results, start, end, model, brushmask, origin, capsule);
  c.esp += 0x1C + 4;
}

}  // namespace recomp::gen
