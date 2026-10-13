// CPU skinning of model surfaces into the tessellator, SSE versions:
// slots 5 (rigid, 0x5070E0) and 10 (weighted, 0x507D20) of the surface
// function table at 0x564270. Replacements of lifted functions named with
// `replace` in manifest/CoDSP.manifest; FINDINGS.md "CPU skinning".
//
// The originals transform with packed SSE (movss + shufps broadcasts,
// mulps, addps). Each lane here is the same IEEE single-precision
// operation in the same order, so the results are the same bits as long
// as nothing is contracted into FMA (-ffp-contract=off, CMakeLists.txt)
// and the host runs in its default floating-point environment (round to
// nearest, no flush to zero), as it does on x86-64 and arm64. Only NaNs
// come out differently (operand order, default NaN): a vertex whose
// results contain one is redone with SSE's NaN rules.
//
// Not reproduced, and never observed: MXCSR's exception flags (the game
// never loads MXCSR and its only readers, the CRT's SSE2 math entry
// checks, test the mask bits alone), and the scratch registers, XMM
// registers and EFLAGS the originals leave behind (their callers are
// compiled C calling through the table, which discard them).

#include <bit>
#include <cstdint>
#include <initializer_list>

#include "runtime/lifted.h"

namespace recomp::gen {

void f_00500A60(Ctx& c);  // RB_CheckOverflow(verts, indexes)
void f_00480820(Ctx& c);  // appends a surface's indexes (below)
void f_0042B0A0(Ctx& c);  // copies (dst, src, size) bytes

namespace {

using lift::Rd;
using lift::Wr;

// The tessellator: counts, then arrays of uint16 indexes, float[3]
// positions, float[2] texture coordinates and float[3] normals.
constexpr uint32_t kNumIndexes = 0x17F0C84;
constexpr uint32_t kNumVertexes = 0x17F0C88;
constexpr uint32_t kIndexes = 0x1618C40;
constexpr uint32_t kXyz = 0x1630C40;
constexpr uint32_t kSt = 0x1650C40;
constexpr uint32_t kNormal = 0x1698C40;

// The surface the argument (+8) points to: int16 vertex count (+2),
// int16 triangle count (+4), and pointers to its weighted vertices'
// extra bones (+0x18), indexes (+0x1C), vertices (+0x20) and texture
// coordinates (+0x24, float[2] each).
//
// Bone matrices are 4 rows of 4 floats (16-byte aligned), the fourth row
// the translation. A vertex v becomes v.x*row0 + v.y*row1 + v.z*row2
// (+ row3 for positions), all four lanes, and is stored with a 16-byte
// write at a 12-byte stride: each vertex's lane 3 lands on the next
// vertex's x, and the last one's stays in the array after it.
using Vec4 = float[4];

// One SSE lane operation, d op s. kSse adds SSE's NaN rules for a NaN
// result: the first NaN operand quietened, else (invalid operation) the
// default NaN.
template <bool kSse>
float Nan(float r, float d, float s) {
  if (!kSse || r == r) return r;
  if (d != d) return std::bit_cast<float>(std::bit_cast<uint32_t>(d) | 0x400000u);
  if (s != s) return std::bit_cast<float>(std::bit_cast<uint32_t>(s) | 0x400000u);
  return std::bit_cast<float>(0xFFC00000u);
}
template <bool kSse>
float Mul(float d, float s) {
  return Nan<kSse>(d * s, d, s);
}
template <bool kSse>
float Add(float d, float s) {
  return Nan<kSse>(d + s, d, s);
}

bool HasNan(const Vec4& v) {
  return v[0] != v[0] || v[1] != v[1] || v[2] != v[2] || v[3] != v[3];
}

void Load(const uint8_t* B, uint32_t a, float* v, int n) {
  std::memcpy(v, B + a, n * 4);
}
void Store(uint8_t* B, uint32_t a, const Vec4& v) {
  std::memcpy(B + a, v, 16);
}

// The lifted function fn, called from the original's call site whose
// return address is ret. Arguments already pushed stay for the caller.
void Call(Ctx& c, GuestFn fn, uint32_t ret) {
  c.esp -= 4;
  Wr<uint32_t>(g_guest_base, c.esp, ret);
  fn(c);
}
void Push(Ctx& c, std::initializer_list<uint32_t> args) {
  for (uint32_t a : args) {
    c.esp -= 4;
    Wr<uint32_t>(g_guest_base, c.esp, a);
  }
}

struct Batch {
  int32_t verts;
  uint32_t surface;
  uint32_t bones;           // the model's matrices for this surface
  uint32_t xyz, normal;     // where the first vertex goes
};

// What both originals do before transforming (fn is the original's VA;
// their code up to the loop is the same): makes room in the tessellator,
// appends the surface's indexes (offset by the first vertex) and texture
// coordinates, and finds the bone matrices. ESP is left inside the
// original's frame (0x20 bytes: EBP, locals, EBX, ESI, EDI) so the calls
// see the same stack.
Batch Begin(Ctx& c, uint32_t fn) {
  uint8_t* const B = g_guest_base;
  const uint32_t arg = Rd<uint32_t>(B, c.esp + 4);
  const uint32_t model = Rd<uint32_t>(B, arg + 4);
  Batch b;
  b.surface = Rd<uint32_t>(B, arg + 8);
  b.verts = int16_t(Rd<uint16_t>(B, b.surface + 2));
  const uint32_t indexes = uint32_t(int16_t(Rd<uint16_t>(B, b.surface + 4))) * 3;

  c.esp -= 0x20;
  Push(c, {indexes, uint32_t(b.verts)});
  Call(c, f_00500A60, fn + 0x2D);
  const uint32_t first = Rd<uint32_t>(B, kNumVertexes);
  Wr<uint32_t>(B, kNumVertexes, first + b.verts);
  // EAX: destination, EDX: surface, CX: first vertex.
  c.eax = kIndexes + Rd<uint32_t>(B, kNumIndexes) * 2;
  c.edx = b.surface;
  c.ecx = first;
  Call(c, f_00480820, fn + 0x51);
  Wr<uint32_t>(B, kNumIndexes, Rd<uint32_t>(B, kNumIndexes) + indexes);
  const uint32_t at = first & 0xFFFF;
  Push(c, {uint32_t(b.verts) * 8, Rd<uint32_t>(B, b.surface + 0x24), kSt + at * 8});
  Call(c, f_0042B0A0, fn + 0x9B);
  c.esp += 0x14 + 0x20;

  b.xyz = kXyz + at * 12;
  b.normal = kNormal + at * 12;
  b.bones = Rd<uint32_t>(B, model + 4) + 0x30 +
            (uint32_t(Rd<uint8_t>(B, Rd<uint32_t>(B, arg + 0xC) + model + 0x50)) << 6);
  return b;
}

// ---- rigid: every vertex on one bone ---------------------------------
// Vertices of 0x18 bytes: normal, position. The matrix is at int16 +6 of
// the surface, a byte offset into the model's.

template <bool kSse>
void Rigid(const float (&m)[4][4], const float (&v)[6], Vec4& normal, Vec4& xyz) {
  for (int j = 0; j < 4; j++) {
    normal[j] = Add<kSse>(Add<kSse>(Mul<kSse>(v[0], m[0][j]), Mul<kSse>(v[1], m[1][j])),
                          Mul<kSse>(v[2], m[2][j]));
    xyz[j] = Add<kSse>(Add<kSse>(Add<kSse>(Mul<kSse>(v[3], m[0][j]), Mul<kSse>(v[4], m[1][j])),
                                 Mul<kSse>(v[5], m[2][j])),
                       m[3][j]);
  }
}

// ---- weighted: a first bone, then any number of weighted extra ones ---
// Vertices of 0x20 bytes: normal, uint32 extra bone count (+0xC),
// position (+0x10), matrix offset (+0x1C); with extra bones the first
// bone's weight follows (+0x20, 0x24 bytes in all). Extra bones are
// 0x14 bytes in their own stream: position, matrix offset (+0xC),
// weight (+0x10). The normal takes the first bone alone.

template <bool kSse>
void FirstBone(const float (&m)[4][4], const float (&v)[6], Vec4& normal, Vec4& xyz) {
  for (int j = 0; j < 4; j++) {
    normal[j] = Add<kSse>(Add<kSse>(Mul<kSse>(v[0], m[0][j]), Mul<kSse>(v[1], m[1][j])),
                          Mul<kSse>(v[2], m[2][j]));
    xyz[j] = Add<kSse>(Add<kSse>(Add<kSse>(Mul<kSse>(v[3], m[0][j]), Mul<kSse>(v[4], m[1][j])),
                                 m[3][j]),
                       Mul<kSse>(v[5], m[2][j]));
  }
}

// xyz (the first bone's) weighted by w, plus `count` extra bones from
// `extra` on.
template <bool kSse>
void ExtraBones(const uint8_t* B, uint32_t bones, float w, uint32_t extra, uint32_t count,
                Vec4& xyz) {
  for (int j = 0; j < 4; j++) xyz[j] = Mul<kSse>(xyz[j], w);
  do {
    float e[5], m[4][4];
    Load(B, extra, e, 3);
    Load(B, extra + 0x10, e + 3, 1);
    Load(B, bones + Rd<uint32_t>(B, extra + 0xC), m[0], 16);
    for (int j = 0; j < 4; j++) {
      const float t = Add<kSse>(Add<kSse>(Add<kSse>(Mul<kSse>(e[0], m[0][j]), Mul<kSse>(e[1], m[1][j])),
                                          Mul<kSse>(e[2], m[2][j])),
                                m[3][j]);
      xyz[j] = Add<kSse>(xyz[j], Mul<kSse>(t, e[3]));
    }
    extra += 0x14;
  } while (--count != 0);
}

}  // namespace

void f_005070E0(Ctx& c) {
  uint8_t* const B = g_guest_base;
  const Batch b = Begin(c, 0x5070E0);
  float m[4][4];
  Load(B, b.bones + uint32_t(int16_t(Rd<uint16_t>(B, b.surface + 6))), m[0], 16);
  uint32_t vert = Rd<uint32_t>(B, b.surface + 0x20);
  // The original keeps the vertex pointer in its argument slot.
  Wr<uint32_t>(B, c.esp + 4, vert);
  uint32_t normal = b.normal, xyz = b.xyz;
  uint32_t n = uint32_t(b.verts);
  do {
    float v[6];
    Vec4 nv, pv;
    Load(B, vert, v, 6);
    Rigid<false>(m, v, nv, pv);
    if (HasNan(nv) || HasNan(pv)) Rigid<true>(m, v, nv, pv);
    Store(B, normal, nv);
    Store(B, xyz, pv);
    vert += 0x18;
    normal += 12;
    xyz += 12;
  } while (--n != 0);
  c.esp += 4;
}

void f_00507D20(Ctx& c) {
  uint8_t* const B = g_guest_base;
  const Batch b = Begin(c, 0x507D20);
  uint32_t vert = Rd<uint32_t>(B, b.surface + 0x20);
  uint32_t extra = Rd<uint32_t>(B, b.surface + 0x18);
  uint32_t normal = b.normal, xyz = b.xyz;
  uint32_t n = uint32_t(b.verts);
  do {
    float v[6], m[4][4];
    Vec4 nv, pv;
    Load(B, vert, v, 3);
    Load(B, vert + 0x10, v + 3, 3);
    Load(B, b.bones + Rd<uint32_t>(B, vert + 0x1C), m[0], 16);
    FirstBone<false>(m, v, nv, pv);
    if (HasNan(nv) || HasNan(pv)) FirstBone<true>(m, v, nv, pv);
    Store(B, normal, nv);
    // Read after the normal is stored, as the original does.
    if (const uint32_t count = Rd<uint32_t>(B, vert + 0xC)) {
      const float w = std::bit_cast<float>(Rd<uint32_t>(B, vert + 0x20));
      Vec4 first;
      std::memcpy(first, pv, 16);
      ExtraBones<false>(B, b.bones, w, extra, count, pv);
      if (HasNan(pv)) {
        std::memcpy(pv, first, 16);
        ExtraBones<true>(B, b.bones, w, extra, count, pv);
      }
      extra += count * 0x14;
      vert += 4;
    }
    Store(B, xyz, pv);
    vert += 0x20;
    normal += 12;
    xyz += 12;
  } while (--n != 0);
  // The original counts the vertices down in its argument slot.
  Wr<uint32_t>(B, c.esp + 4, 0);
  c.esp += 4;
}

}  // namespace recomp::gen
