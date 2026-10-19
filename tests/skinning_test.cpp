// The native CPU skinning (src/skinning.cpp) against the original
// functions, lifted from the EXE at build time with `recomp_lift --only`
// (CMakeLists.txt) and run on x86recomp's exact SSE model. Each case runs
// both from the same guest memory and must leave the same bytes
// everywhere the originals write (tessellator counts, indexes, texture
// coordinates, positions and normals, the caller's argument slot), the
// inputs untouched, and the same ESP, EBP, EBX, ESI, EDI. Every 64th case
// compares the whole tessellator. Only MXCSR's exception flags, the
// scratch registers, XMM registers and EFLAGS are not compared (see
// src/skinning.cpp), nor the stack below the return address, which is
// dead after the call.
//
//   skinning_test [CAPTURE...]
//
// Random cases (raw mt19937 output, fixed seed): surfaces of 1-300
// vertices, rigid and with 0-3 extra bones, coordinates mostly ordinary
// values and otherwise zeros, denormals, infinities, quiet and signalling
// NaNs, huge and tiny values and raw bit patterns, also in the matrices.
// Captures (tests/skinning_capture.h) add real calls recorded in game.

#include <algorithm>
#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

#include "runtime/guest_memory.h"
#include "runtime/lifted.h"
#include "skinning_capture.h"

namespace recomp {
uint8_t* g_guest_base = nullptr;

namespace gen {
void f_005070E0(Ctx& c);
void f_005070E0_lifted(Ctx& c);
void f_00507D20(Ctx& c);
void f_00507D20_lifted(Ctx& c);
// unzCloseCurrentFile, native in the game (src/unzip.cpp): here only the
// lifted error paths reach it.
void f_0049EAA0_lifted(Ctx& c);
void f_0049EAA0(Ctx& c) { f_0049EAA0_lifted(c); }
}  // namespace gen

// Nothing the cases reach calls out: RB_CheckOverflow never overflows.
void Dispatch(Ctx&, uint32_t target, uint32_t va) {
  std::printf("unexpected call to %08X from %08X\n", target, va);
  std::abort();
}
void Resume(Ctx&, GuestResume r) {
  std::printf("unexpected resume to %08X\n", r.target);
  std::abort();
}
void Unimplemented(Ctx&, uint32_t va, const char* what) {
  std::printf("unimplemented %08X %s\n", va, what);
  std::abort();
}
void BadJump(Ctx&, uint32_t from, uint32_t to) {
  std::printf("bad jump %08X -> %08X\n", from, to);
  std::abort();
}
void Unreachable(Ctx&, uint32_t va) {
  std::printf("unreachable %08X\n", va);
  std::abort();
}
void Trap(Ctx&, uint32_t va) {
  std::printf("trap %08X\n", va);
  std::abort();
}
void RaiseDivideError(Ctx&, uint32_t va) {
  std::printf("divide error %08X\n", va);
  std::abort();
}
void Cpuid(Ctx&) { std::abort(); }
uint64_t Rdtsc(Ctx&) { std::abort(); }
void Probe(Ctx&, uint32_t, const char*) {}
void FatalF(const char* fmt, ...) {  // runtime/sse.cpp: #XM, misaligned movaps
  va_list ap;
  va_start(ap, fmt);
  std::vprintf(fmt, ap);
  va_end(ap);
  std::abort();
}

}  // namespace recomp

namespace {

using recomp::Ctx;

recomp::GuestMemory g_mem;

// The tessellator (src/skinning.cpp): counts and arrays, 0x1618C40 up to
// the counts at 0x17F0C84.
constexpr uint32_t kTess = 0x1618000, kTessEnd = 0x17F1000;
constexpr uint32_t kNumIndexes = 0x17F0C84, kNumVertexes = 0x17F0C88;
constexpr uint32_t kIndexes = 0x1618C40, kXyz = 0x1630C40, kSt = 0x1650C40,
                   kNormal = 0x1698C40;
// Random cases' data and the stack (captures pick their own stack).
constexpr uint32_t kData = 0x20000000, kDataSize = 0x100000;
constexpr uint32_t kStack = 0x30000000, kStackSize = 0x10000;

uint8_t* G(uint32_t va) { return g_mem.base() + va; }
uint32_t Rd(uint32_t va) { return g_mem.read<uint32_t>(va); }
void Wr(uint32_t va, uint32_t v) { g_mem.write<uint32_t>(va, v); }
void Wr16(uint32_t va, uint16_t v) { g_mem.write<uint16_t>(va, v); }

struct Window {
  uint32_t va, size;
};

struct Case {
  uint32_t fn;
  uint32_t arg;
  uint32_t esp;  // the return address; the argument above it
  std::vector<Window> inputs;
};

struct Stats {
  int cases = 0, failures = 0, nan_outputs = 0, full = 0;
};

Ctx NewCtx(std::mt19937& g, uint32_t esp) {
  Ctx c{};
  c.eax = g();
  c.ecx = g();
  c.edx = g();
  c.ebx = g();
  c.ebp = g();
  c.esi = g();
  c.edi = g();
  c.esp = esp;
  c.mxcsr = recomp::sse::kMxcsrDefault;
  return c;
}

void Snapshot(const std::vector<Window>& w, std::vector<uint8_t>& out) {
  out.clear();
  for (const Window& x : w) out.insert(out.end(), G(x.va), G(x.va) + x.size);
}
void Restore(const std::vector<Window>& w, const std::vector<uint8_t>& in) {
  size_t at = 0;
  for (const Window& x : w) {
    std::memcpy(G(x.va), in.data() + at, x.size);
    at += x.size;
  }
}

// Runs one case through the lifted original and the native code from the
// same memory and compares.
void Run(const Case& k, std::mt19937& g, Stats& s, const char* what) {
  const uint32_t first = Rd(kNumVertexes) & 0xFFFF;
  const uint32_t surf = Rd(k.arg + 8);
  const uint32_t verts = uint32_t(int16_t(g_mem.read<uint16_t>(surf + 2)));
  const uint32_t indexes = uint32_t(int16_t(g_mem.read<uint16_t>(surf + 4))) * 3;
  const bool full = s.cases % 64 == 0;
  std::vector<Window> w;
  if (full) {
    w.push_back({kTess, kTessEnd - kTess});
  } else {
    // Written: around each output range, 64 bytes either side.
    auto out = [&](uint32_t va, uint32_t size) { w.push_back({va - 64, size + 128}); };
    out(kNumIndexes, 8);
    out(kIndexes + Rd(kNumIndexes) * 2, indexes * 2);
    out(kSt + first * 8, verts * 8);
    out(kXyz + first * 12, verts * 12 + 4);
    out(kNormal + first * 12, verts * 12 + 4);
  }
  w.push_back({k.esp, 8});
  w.insert(w.end(), k.inputs.begin(), k.inputs.end());
  std::vector<uint8_t> before, lifted;
  Ctx c0 = NewCtx(g, k.esp);
  Wr(k.esp, 0xDEAD0000);
  Wr(k.esp + 4, k.arg);
  Snapshot(w, before);

  Ctx cl = c0;
  (k.fn == skinning::kRigid ? recomp::gen::f_005070E0_lifted
                            : recomp::gen::f_00507D20_lifted)(cl);
  Snapshot(w, lifted);
  // NaNs reaching the outputs (coverage of the NaN path).
  for (uint32_t i = 0; i < verts * 3; i++)
    for (uint32_t base : {kXyz, kNormal}) {
      uint32_t u = Rd(base + (first * 3 + i) * 4);
      if ((u & 0x7F800000) == 0x7F800000 && (u & 0x7FFFFF)) s.nan_outputs++;
    }
  Restore(w, before);
  Ctx cn = c0;
  (k.fn == skinning::kRigid ? recomp::gen::f_005070E0 : recomp::gen::f_00507D20)(cn);
  std::vector<uint8_t> native;
  Snapshot(w, native);

  s.cases++;
  s.full += full;
  bool ok = cn.esp == cl.esp && cn.ebp == cl.ebp && cn.ebx == cl.ebx && cn.esi == cl.esi &&
            cn.edi == cl.edi && (cn.mxcsr & ~0x3Fu) == (cl.mxcsr & ~0x3Fu);
  size_t at = 0;
  for (const Window& x : w) {
    for (uint32_t i = 0; i < x.size; i++)
      if (native[at + i] != lifted[at + i]) {
        if (s.failures < 20)
          std::printf("%s %s: byte %08X lifted %02X native %02X\n", what,
                      k.fn == skinning::kRigid ? "rigid" : "weighted", x.va + i, lifted[at + i],
                      native[at + i]);
        ok = false;
        break;
      }
    at += x.size;
  }
  if (!ok) {
    if (s.failures < 20 && cn.esp != cl.esp)
      std::printf("%s: esp lifted %08X native %08X\n", what, cl.esp, cn.esp);
    s.failures++;
  }
  // Leave the lifted result in place, as the next case's starting state.
  Restore(w, lifted);
}

// ---- random cases -----------------------------------------------------

float Bits(uint32_t u) {
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}

// Mostly ordinary values in [-range, range); otherwise one of the special
// classes.
uint32_t Value(std::mt19937& g, float range, uint32_t special_per_1024) {
  if (g() % 1024 >= special_per_1024) {
    float f = (float(g() >> 8) * 0x1p-24f * 2 - 1) * range;
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
  }
  const uint32_t sign = g() & 0x80000000u;
  switch (g() % 9) {
    case 0: return sign;                                     // zero
    case 1: return sign | (g() & 0x7FFFFF) | 1;              // denormal
    case 2: return sign | 0x7F800000;                        // infinity
    case 3: return sign | 0x7FC00000 | (g() & 0x3FFFFF);     // quiet NaN
    case 4: return sign | 0x7F800000 | ((g() & 0x3FFFFF) | 1);  // signalling NaN
    case 5: return sign | (0x7E000000 + (g() & 0xFFFFFF));   // huge: overflows
    case 6: return sign | (0x00800000 + (g() & 0x1FFFFFF));  // tiny: underflows
    case 7: return sign | 0x3F800000;                        // one
    default: return g();                                     // raw bits
  }
}

Case RandomCase(std::mt19937& g, bool weighted, uint32_t special) {
  std::memset(G(kData), 0, kDataSize);
  uint32_t top = kData;
  auto alloc = [&](uint32_t size, uint32_t align = 4) {
    top = (top + align - 1) & ~(align - 1);
    uint32_t va = top;
    top += size;
    return va;
  };
  const uint32_t verts = 1 + g() % 300;
  const uint32_t tris = 2 * (1 + g() % 200);  // even: the index copier's
                                              // offset path copies pairs
  const uint32_t nbones = 1 + g() % 24;
  const uint32_t arg = alloc(0x10), model = alloc(0x60), surf = alloc(0x28);
  const uint32_t mats = alloc(nbones * 64, 16);
  const uint32_t pick = g() % 4;  // the byte that selects the model's bone set
  const uint32_t key = g() % 8;
  Wr(arg + 4, model);
  Wr(arg + 8, surf);
  Wr(arg + 0xC, key);
  G(key + model + 0x50)[0] = uint8_t(pick);
  Wr(model + 4, mats - 0x30 - pick * 64);
  for (uint32_t i = 0; i < nbones * 16; i++) Wr(mats + i * 4, Value(g, 2, special / 4));

  Wr16(surf + 2, uint16_t(verts));
  Wr16(surf + 4, uint16_t(tris));
  Wr16(surf + 6, uint16_t((g() % nbones) * 64));
  const uint32_t idx = alloc(tris * 6), st = alloc(verts * 8);
  for (uint32_t i = 0; i < tris * 3; i++) Wr16(idx + i * 2, uint16_t(g() % verts));
  for (uint32_t i = 0; i < verts * 2; i++) Wr(st + i * 4, g());
  Wr(surf + 0x1C, idx);
  Wr(surf + 0x24, st);
  uint32_t vert;
  if (!weighted) {
    vert = alloc(verts * 0x18);
    for (uint32_t i = 0; i < verts * 6; i++) Wr(vert + i * 4, Value(g, 64, special));
  } else {
    // Counts first, to size the two streams.
    std::vector<uint32_t> counts(verts);
    uint32_t extras = 0;
    for (uint32_t& n : counts) extras += n = g() % 8 < 5 ? 0 : 1 + g() % 3;
    vert = alloc(verts * 0x20 + (verts - std::count(counts.begin(), counts.end(), 0u)) * 4);
    const uint32_t extra = alloc(extras * 0x14);
    Wr(surf + 0x18, extra);
    uint32_t v = vert, e = extra;
    for (uint32_t n : counts) {
      for (int k = 0; k < 3; k++) Wr(v + k * 4, Value(g, 1, special));
      Wr(v + 0xC, n);
      for (int k = 0; k < 3; k++) Wr(v + 0x10 + k * 4, Value(g, 64, special));
      Wr(v + 0x1C, (g() % nbones) * 64);
      if (n) Wr(v + 0x20, Value(g, 1, special));
      v += n ? 0x24 : 0x20;
      for (uint32_t k = 0; k < n; k++, e += 0x14) {
        for (int j = 0; j < 3; j++) Wr(e + j * 4, Value(g, 64, special));
        Wr(e + 0xC, (g() % nbones) * 64);
        Wr(e + 0x10, Value(g, 1, special));
      }
    }
  }
  Wr(surf + 0x20, vert);
  if (top > kData + kDataSize) std::abort();

  // Room in the tessellator (no overflow); sometimes empty, which takes
  // the index copier's plain-copy path.
  const bool empty = g() % 10 == 0;
  Wr(kNumVertexes, empty ? 0 : g() % (0x2000 - verts));
  Wr(kNumIndexes, empty ? 0 : g() % (0xC000 - tris * 3));
  return {weighted ? skinning::kWeighted : skinning::kRigid, arg, kStack + kStackSize - 0x100,
          {{kData, kDataSize}}};
}

// ---- captures ---------------------------------------------------------

bool RunCaptures(const char* path, std::mt19937& g, Stats& s) {
  FILE* f = std::fopen(path, "rb");
  if (!f) {
    std::printf("%s: cannot open\n", path);
    return false;
  }
  int n = 0;
  uint32_t head[4];
  while (std::fread(head, 4, 4, f) == 4) {
    if (head[0] != skinning::kMagic) {
      std::printf("%s: bad record %d\n", path, n);
      return false;
    }
    Case k{head[1], head[2], 0, {}};
    for (uint32_t i = 0; i < head[3]; i++) {
      uint32_t r[2];
      if (std::fread(r, 4, 2, f) != 2) return false;
      g_mem.Commit(r[0], r[1], recomp::Prot::RW);
      if (std::fread(G(r[0]), 1, r[1], f) != r[1]) return false;
      k.inputs.push_back({r[0], r[1]});
    }
    // A stack clear of the inputs.
    k.esp = 0x7F000000;
    for (bool clash = true; clash;) {
      clash = false;
      for (const Window& x : k.inputs)
        if (x.va < k.esp + 8 && k.esp - kStackSize < x.va + x.size) clash = true;
      if (clash) k.esp -= 0x1000000;
    }
    g_mem.Commit(k.esp - kStackSize, kStackSize + 0x1000, recomp::Prot::RW);
    // Calls that overflowed the tessellator flushed it first: run them on
    // an empty one.
    const uint32_t surf = Rd(k.arg + 8);
    const int32_t verts = int16_t(g_mem.read<uint16_t>(surf + 2));
    const int32_t indexes = int16_t(g_mem.read<uint16_t>(surf + 4)) * 3;
    if (int32_t(Rd(kNumVertexes)) + verts >= 0x2000 ||
        int32_t(Rd(kNumIndexes)) + indexes >= 0xC000) {
      Wr(kNumVertexes, 0);
      Wr(kNumIndexes, 0);
    }
    Run(k, g, s, path);
    n++;
  }
  std::printf("%s: %d captured calls\n", path, n);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);  // messages before an abort
  if (!g_mem.Reserve()) return 1;
  recomp::g_guest_base = g_mem.base();
  g_mem.Commit(kTess, kTessEnd - kTess, recomp::Prot::RW);
  g_mem.Commit(kData, kDataSize, recomp::Prot::RW);
  g_mem.Commit(kStack, kStackSize, recomp::Prot::RW);
  std::mt19937 g(20261013);
  // Fill the tessellator with noise: outputs must overwrite it the same.
  for (uint32_t va = kTess; va < kTessEnd; va += 4) Wr(va, g());

  Stats s;
  for (int i = 0; i < 6000; i++) {
    // Ordinary data, then more and more special values.
    const uint32_t special = i < 2000 ? 0 : i < 4000 ? 8 : 128;
    Run(RandomCase(g, i % 2, special), g, s, "random");
  }
  const int random = s.cases;
  for (int i = 1; i < argc; i++)
    if (!RunCaptures(argv[i], g, s)) return 1;
  std::printf("%d random + %d captured cases (%d compared in full), %d NaN outputs: "
              "%d mismatched\n",
              random, s.cases - random, s.full, s.nan_outputs, s.failures);
  return s.failures ? 1 : 0;
}
