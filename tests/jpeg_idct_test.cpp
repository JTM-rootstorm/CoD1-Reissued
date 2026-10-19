// The native jpeg_idct_float (src/jpeg_idct.cpp) against the original
// (0x519140, and the CRT conversion 0x52E218 it calls), lifted from the EXE
// at build time with `recomp_lift --only` (CMakeLists.txt) and run on
// x86recomp's exact x87 model (SoftFloat, not its host-double runs) at the
// game's CW 0x027F.
//
//   jpeg_idct_test EXE [PK3...]
//
// Each block runs through both from the same memory; the eight output rows
// (and a guard byte on each side), ESP, EBX, ESI, EDI, EBP and the x87
// stack (top, empty registers, control word) must agree. Not compared: EAX,
// ECX, EDX, EFLAGS, the x87 status word and the stack below the return
// address (src/jpeg_idct.cpp).
//
// Random blocks (mt19937, fixed seed): DC only, sparse, dense, extreme
// (-32768..32767) coefficients; multipliers as jpeg-6's jddctmgr makes them
// from random quantisation tables, or random floats; a random range-limit
// table, so a wrong index shows. Then every 8x8 block of every .jpg in the
// given pk3s with its own quantisation table: the coefficients as libjpeg's
// entropy decoder leaves them (read here with the host's libjpeg,
// jpeg_read_coefficients, which is lossless), the multipliers as jpeg-6
// computes them.

#include <cstddef>
#include <cstdio>

#include <jpeglib.h>
#include <zlib.h>

#include <algorithm>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "runtime/guest_memory.h"
#include "runtime/lifted.h"
#include "runtime/pe_image.h"
#include "runtime/x87.h"

namespace recomp {
uint8_t* g_guest_base = nullptr;

namespace gen {
void f_00519140_lifted(Ctx& c);
// The native code (tests/jpeg_idct_native.cpp renames its entry)...
void n_00519140(Ctx& c);
// ...and the lifted table lists the original.
void f_00519140(Ctx& c) { f_00519140_lifted(c); }
}  // namespace gen

PeImage* g_exe = nullptr;

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
void FatalF(const char* fmt, ...) {
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

// The call's structures: cinfo (sample_range_limit at +0x11C), compptr
// (dct_table at +0x50), the block, the multipliers, the range-limit
// table, the row pointers and the rows (with a guard byte each side).
constexpr uint32_t kArena = 0x30000000, kArenaSize = 0x10000;
constexpr uint32_t kCinfo = kArena, kCompptr = kArena + 0x200, kCoef = kArena + 0x300,
                   kTable = kArena + 0x400, kRange = kArena + 0x500, kRows = kArena + 0xA00,
                   kOut = kArena + 0xB00, kRowStride = 0x40;
constexpr uint32_t kStack = 0x2FF00000, kStackSize = 0x100000;

void Wr(uint32_t va, uint32_t v) { g_mem.write<uint32_t>(va, v); }

struct Stats {
  long blocks = 0, failures = 0, dc_only = 0;
};

// Runs the block now in guest memory through both, from the same state.
void Run(uint32_t output_col, std::mt19937& g, Stats& s, const char* what) {
  const uint32_t esp = kStack + kStackSize - 0x100;
  const uint32_t args[] = {kCinfo, kCompptr, kCoef, kRows, output_col};
  for (int i = 0; i < 5; i++) Wr(esp + 4 + 4 * i, args[i]);
  Wr(esp, 0xDEAD0000);
  for (int r = 0; r < 8; r++) Wr(kRows + 4 * r, kOut + r * kRowStride);
  for (uint32_t i = 0; i < 8 * kRowStride; i++) g_mem.write<uint8_t>(kOut + i, uint8_t(g()));
  std::vector<uint8_t> before(g_mem.ptr(kOut), g_mem.ptr(kOut) + 8 * kRowStride);

  Ctx c0{};
  c0.eax = g(), c0.ecx = g(), c0.edx = g(), c0.ebx = g(), c0.ebp = g(), c0.esi = g(), c0.edi = g();
  c0.esp = esp;
  recomp::x87::Init(c0.fpu);
  recomp::x87::SetControlWord(c0.fpu, 0x027F);
  c0.mxcsr = recomp::sse::kMxcsrDefault;

  Ctx cl = c0;
  recomp::gen::f_00519140_lifted(cl);
  const std::vector<uint8_t> lifted(g_mem.ptr(kOut), g_mem.ptr(kOut) + 8 * kRowStride);
  std::memcpy(g_mem.ptr(kOut), before.data(), before.size());
  Ctx cn = c0;
  recomp::gen::n_00519140(cn);
  const std::vector<uint8_t> native(g_mem.ptr(kOut), g_mem.ptr(kOut) + 8 * kRowStride);

  s.blocks++;
  const bool ok = cn.esp == cl.esp && cn.ebp == cl.ebp && cn.ebx == cl.ebx && cn.esi == cl.esi &&
                  cn.edi == cl.edi && cn.fpu.top == cl.fpu.top && cn.fpu.empty == cl.fpu.empty &&
                  cn.fpu.cw == cl.fpu.cw && native == lifted;
  if (ok) return;
  if (s.failures++ < 20) {
    std::printf("%s: block %ld differs (esp %08X/%08X top %d/%d)\n", what, s.blocks, cl.esp,
                cn.esp, cl.fpu.top, cn.fpu.top);
    for (int r = 0; r < 8; r++) {
      std::printf("  row %d:", r);
      for (uint32_t k = 0; k < 8; k++) {
        const uint32_t i = r * kRowStride + output_col + k;
        std::printf(" %02X/%02X", lifted[i], native[i]);
      }
      std::printf("\n");
    }
  }
}

// jpeg-6's jddctmgr for JDCT_FLOAT: quantval[i] * aanscalefactor[row] *
// aanscalefactor[col] in doubles, stored as float.
void Multipliers(const uint16_t* quantval) {
  static const double aan[8] = {1.0, 1.387039845, 1.306562965, 1.175875602,
                                1.0, 0.785694958, 0.541196100, 0.275899379};
  for (int i = 0; i < 64; i++)
    g_mem.write<float>(kTable + 4 * i, float(double(quantval[i]) * aan[i / 8] * aan[i % 8]));
}

void RandomBlocks(std::mt19937& g, Stats& s) {
  for (int t = 0; t < 4000; t++) {
    // A table, a range-limit table, then blocks under them.
    if (t % 4 == 3) {
      for (int i = 0; i < 64; i++) {
        float f = std::ldexp(float(g() % 0x1000000) / 0x1000000, int(g() % 24) - 4);
        if (g() % 2) f = -f;
        g_mem.write<float>(kTable + 4 * i, f);
      }
    } else {
      uint16_t q[64];
      const uint32_t top = t % 4 == 0 ? 256 : t % 4 == 1 ? 32 : 65536;
      for (uint16_t& x : q) x = uint16_t(1 + g() % (top - 1));
      Multipliers(q);
    }
    for (uint32_t i = 0; i < 0x500; i++) g_mem.write<uint8_t>(kRange + i, uint8_t(g()));
    for (int b = 0; b < 50; b++) {
      int16_t coef[64] = {};
      const uint32_t kind = g() % 5;
      const int32_t range = kind == 4 ? 65536 : g() % 2 ? 2048 : 128;
      if (kind == 0) {
        coef[0] = int16_t(int32_t(g() % range) - range / 2);
        s.dc_only++;
      } else {
        const int n = kind == 1 ? 1 + g() % 4 : kind == 2 ? 1 + g() % 16 : 64;
        for (int i = 0; i < n; i++)
          coef[kind == 3 || kind == 4 ? i : g() % 64] = int16_t(int32_t(g() % range) - range / 2);
      }
      std::memcpy(g_mem.ptr(kCoef), coef, sizeof coef);
      Run(g() % (kRowStride - 9) + 1, g, s, "random");
    }
  }
}

// ---- the pk3s' JPEGs -------------------------------------------------------

std::vector<std::vector<uint8_t>> ZipJpegs(const char* path) {
  std::vector<std::vector<uint8_t>> out;
  std::FILE* f = std::fopen(path, "rb");
  if (!f) return out;
  std::fseek(f, 0, SEEK_END);
  std::vector<uint8_t> zip(size_t(std::ftell(f)));
  std::fseek(f, 0, SEEK_SET);
  const bool ok = std::fread(zip.data(), 1, zip.size(), f) == zip.size();
  std::fclose(f);
  auto u16 = [&](size_t a) { return uint32_t(zip[a] | zip[a + 1] << 8); };
  auto u32 = [&](size_t a) { return u16(a) | u16(a + 2) << 16; };
  size_t end = zip.size() >= 22 ? zip.size() - 22 : 0;
  while (end > 0 && u32(end) != 0x06054B50) end--;
  if (!ok || u32(end) != 0x06054B50) return out;
  for (size_t p = u32(end + 16), i = 0; i < u16(end + 10); i++) {
    const uint32_t method = u16(p + 10), csize = u32(p + 20), usize = u32(p + 24);
    const uint32_t local = u32(p + 42), name_len = u16(p + 28);
    std::string name(reinterpret_cast<const char*>(&zip[p + 46]), name_len);
    for (char& ch : name) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".jpg") == 0 &&
        (method == 0 || method == 8)) {
      const size_t data = local + 30 + u16(local + 26) + u16(local + 28);
      std::vector<uint8_t> v(usize);
      if (method == 0) {
        std::memcpy(v.data(), zip.data() + data, usize);
      } else {
        z_stream z{};
        inflateInit2(&z, -MAX_WBITS);
        z.next_in = zip.data() + data;
        z.avail_in = csize;
        z.next_out = v.data();
        z.avail_out = usize;
        inflate(&z, Z_FINISH);
        inflateEnd(&z);
      }
      out.push_back(std::move(v));
    }
    p += 46 + u16(p + 28) + u16(p + 30) + u16(p + 32);
  }
  return out;
}

// Every block of the image, with its component's table.
void JpegBlocks(const std::vector<uint8_t>& file, std::mt19937& g, Stats& s) {
  jpeg_decompress_struct ci;
  jpeg_error_mgr err;
  ci.err = jpeg_std_error(&err);
  jpeg_create_decompress(&ci);
  jpeg_mem_src(&ci, file.data(), static_cast<unsigned long>(file.size()));
  jpeg_read_header(&ci, TRUE);
  jvirt_barray_ptr* coefs = jpeg_read_coefficients(&ci);
  for (int ci_n = 0; ci_n < ci.num_components; ci_n++) {
    const jpeg_component_info& comp = ci.comp_info[ci_n];
    Multipliers(comp.quant_table->quantval);
    for (JDIMENSION row = 0; row < comp.height_in_blocks; row++) {
      JBLOCKARRAY blocks = (*ci.mem->access_virt_barray)(reinterpret_cast<j_common_ptr>(&ci),
                                                         coefs[ci_n], row, 1, FALSE);
      for (JDIMENSION col = 0; col < comp.width_in_blocks; col++) {
        int16_t coef[64];
        for (int i = 0; i < 64; i++) coef[i] = int16_t(blocks[0][col][i]);
        s.dc_only += std::all_of(coef + 1, coef + 64, [](int16_t x) { return x == 0; });
        std::memcpy(g_mem.ptr(kCoef), coef, sizeof coef);
        Run(1, g, s, "pk3 jpeg");
      }
    }
  }
  jpeg_finish_decompress(&ci);
  jpeg_destroy_decompress(&ci);
}

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);  // messages before an abort
  if (argc < 2) {
    std::printf("usage: %s CoDSP.exe [PK3...]\n", argv[0]);
    return 2;
  }
  if (!g_mem.Reserve()) return 1;
  recomp::g_guest_base = g_mem.base();
  recomp::PeImage exe;
  recomp::g_exe = &exe;
  std::string err;
  if (!exe.Load(argv[1], g_mem, &err)) {
    std::printf("%s: %s\n", argv[1], err.c_str());
    return 1;
  }
  g_mem.Commit(kArena, kArenaSize, recomp::Prot::RW);
  g_mem.Commit(kStack, kStackSize, recomp::Prot::RW);
  Wr(kCinfo + 0x11C, kRange);
  Wr(kCompptr + 0x50, kTable);
  // The reference: the x87 model's exact arithmetic throughout.
  recomp::x87::run::g_doubles = false;

  std::mt19937 g(20261019);
  Stats s;
  RandomBlocks(g, s);
  const long random = s.blocks;
  // Real images: jpeg-6's own range-limit table from sample_range_limit
  // on (prepare_range_limit_table): 0-255, then 255 up to 640, zeros up to
  // 1024, then 0-127 again.
  for (int j = 0; j < 0x500; j++)
    g_mem.write<uint8_t>(kRange + j, uint8_t(j < 256 ? j : j < 640 ? 255 : j < 1024 ? 0 : j - 1024));
  long jpegs = 0;
  for (int i = 2; i < argc; i++)
    for (const auto& file : ZipJpegs(argv[i])) {
      JpegBlocks(file, g, s);
      jpegs++;
    }
  std::printf("%ld random + %ld blocks of %ld pk3 JPEGs (%ld DC only): %ld mismatched\n", random,
              s.blocks - random, jpegs, s.dc_only, s.failures);
  return s.failures ? 1 : 0;
}
