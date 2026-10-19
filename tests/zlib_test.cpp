// The native compress2 and uncompress (src/zlib.cpp, on the host's zlib)
// against the EXE's zlib 1.1.4, lifted at build time with `recomp_lift
// --only` (CMakeLists.txt). CoDSP.exe is mapped as the game maps it; zlib's
// allocator (0x42A480 under zcalloc) and free are stubs here on a bump arena
// (tests/crt_stubs.manifest).
//
//   zlib_test EXE [PK3...]
//
// Inputs: generated buffers (empty to 1 MB: zeros, random bytes, text-like
// runs, repeats at every distance up to the window, small integers as in
// savegames) and the contents of pk3 entries (every entry up to 256 KB of
// each zip, read with the host's zlib). compress2 runs on each with the
// game's level (1, G_SaveGame's) and the destination G_SaveGame gives it
// (sourceLen + 0.1% + 13 bytes), the generated ones also at every other
// level and with destinations too small by one byte and by half.
// uncompress runs on each result with the exact destination size, a larger
// and a too small one, and on corrupt copies (truncated, a byte changed,
// the Adler-32 changed). Both sides must return the same, write the same
// *destLen and the same destination bytes (plus a guard pattern after them),
// and leave the same ESP, EBX, ESI, EDI and EBP.

#include <zlib.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "runtime/guest_memory.h"
#include "runtime/lifted.h"
#include "runtime/pe_image.h"

namespace recomp {
uint8_t* g_guest_base = nullptr;

namespace gen {
void f_00497D00_lifted(Ctx& c);
void f_0049D8A0_lifted(Ctx& c);
// The native code (tests/zlib_native.cpp renames its entries)...
void n_00497D00(Ctx& c);
void n_0049D8A0(Ctx& c);
// ...and the lifted code calls the originals, not it.
void f_00497D00(Ctx& c) { f_00497D00_lifted(c); }
void f_0049D8A0(Ctx& c) { f_0049D8A0_lifted(c); }
}  // namespace gen

PeImage* g_exe = nullptr;

// zlib calls deflate_stored/fast/slow and its allocators through pointers.
void Dispatch(Ctx& c, uint32_t target, uint32_t va) {
  const LiftedFn* lo = kLiftedFunctions;
  const LiftedFn* hi = kLiftedFunctions + kLiftedFunctionCount;
  const LiftedFn* f =
      std::lower_bound(lo, hi, target, [](const LiftedFn& a, uint32_t t) { return a.va < t; });
  if (f != hi && f->va == target) {
    f->fn(c);
    return;
  }
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

// zlib's arena; the input, the output and *destLen; the stack.
constexpr uint32_t kArena = 0x30000000, kArenaSize = 0x1000000;
constexpr uint32_t kInput = 0x40000000, kOutput = 0x48000000, kRegionSize = 0x2000000;
constexpr uint32_t kDestLen = 0x2FE00000;
constexpr uint32_t kStack = 0x2FF00000, kStackSize = 0x100000;
constexpr uint32_t kGuard = 16;

uint32_t Rd(uint32_t va) { return g_mem.read<uint32_t>(va); }
void Wr(uint32_t va, uint32_t v) { g_mem.write<uint32_t>(va, v); }
uint32_t Arg(const Ctx& c, int n) { return Rd(c.esp + 4 + 4 * n); }

uint32_t g_top = kArena;

}  // namespace

// The stubs (cdecl: the caller pops).
namespace recomp::gen {
void f_0042A480(Ctx& c) {  // zcalloc's allocation, zeroed
  const uint32_t size = Arg(c, 0);
  c.eax = g_top;
  g_top = (g_top + size + 15) & ~15u;
  if (g_top > kArena + kArenaSize) {
    std::printf("arena full\n");
    std::abort();
  }
  std::memset(g_mem.ptr(c.eax), 0, size);
  c.esp += 4;
}
void f_0051B768(Ctx& c) { c.esp += 4; }  // free
// Reached only from the lifted body of the allocator (its error path):
// never called here.
void f_0049EAA0(Ctx&) { std::abort(); }
void f_0051BBA7(Ctx&) { std::abort(); }
void f_0051BDFD(Ctx&) { std::abort(); }
void f_0051C43B(Ctx&) { std::abort(); }
}  // namespace recomp::gen

namespace {

Ctx NewCtx(std::mt19937& g) {
  Ctx c{};
  c.ebx = g();
  c.ebp = g();
  c.esi = g();
  c.edi = g();
  c.esp = kStack + kStackSize - 0x100;
  recomp::x87::Init(c.fpu);
  c.mxcsr = recomp::sse::kMxcsrDefault;
  return c;
}

struct Result {
  uint32_t ret, dest_len, esp, ebx, esi, edi, ebp;
  std::vector<uint8_t> out;
  bool operator==(const Result& o) const {
    return ret == o.ret && dest_len == o.dest_len && esp == o.esp && ebx == o.ebx &&
           esi == o.esi && edi == o.edi && ebp == o.ebp && out == o.out;
  }
};

enum Fn { kCompress2, kUncompress };

// One call from the given input (already at kInput) into a destination of
// dest_size bytes.
Result Run(bool native, Fn fn, uint32_t size, uint32_t dest_size, int32_t level, uint32_t seed) {
  std::mt19937 g(seed);
  Ctx c = NewCtx(g);
  g_top = kArena;
  std::memset(g_mem.ptr(kOutput), 0xA5, dest_size + kGuard);
  Wr(kDestLen, dest_size);
  c.eax = kInput;
  c.ecx = size;
  c.edx = kOutput;
  if (fn == kCompress2) {
    c.esp -= 8;
    Wr(c.esp, kDestLen);
    Wr(c.esp + 4, uint32_t(level));
  } else {
    c.edi = kDestLen;
  }
  c.esp -= 4;
  Wr(c.esp, 0xDEAD0000);
  if (fn == kCompress2)
    (native ? recomp::gen::n_00497D00 : recomp::gen::f_00497D00_lifted)(c);
  else
    (native ? recomp::gen::n_0049D8A0 : recomp::gen::f_0049D8A0_lifted)(c);
  if (fn == kCompress2) c.esp += 8;
  Result r{c.eax, Rd(kDestLen), c.esp, c.ebx, c.esi, c.edi, c.ebp, {}};
  r.out.assign(g_mem.ptr(kOutput), g_mem.ptr(kOutput) + dest_size + kGuard);
  return r;
}

struct Stats {
  long cases = 0, failures = 0, errors = 0, inputs = 0;
  long long bytes = 0;
  std::map<std::string, std::pair<long, long>> kinds;  // calls, mismatches
};

// Both sides on the input at kInput; returns the original's result.
Result Compare(Fn fn, uint32_t size, uint32_t dest_size, int32_t level, const std::string& what,
               Stats& st, const char* variant = "") {
  const uint32_t seed = uint32_t(st.cases);
  const Result a = Run(false, fn, size, dest_size, level, seed);
  const Result b = Run(true, fn, size, dest_size, level, seed);
  st.cases++;
  st.errors += a.ret != 0;
  auto& kind = st.kinds[std::string(fn == kCompress2 ? "compress2" : "uncompress") +
                        (fn == kCompress2 ? " level " + std::to_string(level) : "") + variant +
                        (a.ret ? " (error " + std::to_string(int32_t(a.ret)) + ")" : "")];
  kind.first++;
  kind.second += !(a == b);
  if (!(a == b)) {
    if (st.failures < 20)
      std::printf("%s: %s level %d, %u -> %u bytes: ret %d/%d destLen %u/%u%s\n", what.c_str(),
                  fn == kCompress2 ? "compress2" : "uncompress", level, size, dest_size,
                  int32_t(a.ret), int32_t(b.ret), a.dest_len, b.dest_len,
                  a.out != b.out ? ", output differs" : "");
    st.failures++;
  }
  return a;
}

void Load(uint32_t va, const std::vector<uint8_t>& v) {
  if (!v.empty()) std::memcpy(g_mem.ptr(va), v.data(), v.size());
}

// G_SaveGame's destination for size bytes.
uint32_t Bound(uint32_t size) { return size + size / 1000 + 13; }

// compress2 of data at the given levels (the first with too small
// destinations too), then uncompress of each result.
void TestInput(const std::vector<uint8_t>& data, const std::vector<int32_t>& levels,
               const std::string& what, std::mt19937& g, Stats& st) {
  st.inputs++;
  st.bytes += long(data.size());
  const uint32_t size = uint32_t(data.size());
  for (int32_t level : levels) {
    Load(kInput, data);
    const Result c = Compare(kCompress2, size, Bound(size), level, what, st);
    if (c.ret != 0) continue;
    const uint32_t n = c.dest_len;
    if (level == levels.front() && levels.size() > 1) {
      Compare(kCompress2, size, n - 1, level, what + " (short)", st, " short");
      Compare(kCompress2, size, n / 2, level, what + " (half)", st, " half");
    }
    // uncompress of the result, and of corrupt copies.
    std::vector<uint8_t> z(c.out.begin(), c.out.begin() + n);
    Load(kInput, z);
    Compare(kUncompress, n, size, 0, what, st);
    Compare(kUncompress, n, size + 100, 0, what + " (larger)", st, " larger");
    if (size) Compare(kUncompress, n, size - 1, 0, what + " (short)", st, " short");
    std::vector<uint8_t> bad = z;
    bad[g() % n] ^= uint8_t(1 + g() % 255);
    Load(kInput, bad);
    Compare(kUncompress, n, size, 0, what + " (byte)", st, " byte");
    bad = z;
    bad[n - 1] ^= 1;
    Load(kInput, bad);
    Compare(kUncompress, n, size, 0, what + " (adler)", st, " adler");
    Load(kInput, z);
    Compare(kUncompress, n / 2, size, 0, what + " (truncated)", st, " truncated");
  }
}

std::vector<uint8_t> Generate(std::mt19937& g, int kind, uint32_t size) {
  std::vector<uint8_t> v(size);
  for (uint32_t i = 0; i < size; i++) {
    switch (kind) {
      case 0: v[i] = 0; break;
      case 1: v[i] = uint8_t(g()); break;
      case 2: v[i] = (g() % 16) ? uint8_t('a' + (i / (1 + g() % 64)) % 23) : uint8_t(g()); break;
      case 3: {  // a copy from up to the whole window back, or a literal
        const uint32_t back = 1 + g() % 40000;
        v[i] = (i >= back && g() % 8) ? v[i - back] : uint8_t(g() % 4);
        break;
      }
      default:  // little-endian words, mostly small
        v[i] = (i % 4 == 0) ? uint8_t(g() % 8 ? g() % 16 : g()) : (g() % 32 ? 0 : uint8_t(g()));
        break;
    }
  }
  return v;
}

// The entries of a zip up to max bytes, read with the host's zlib.
std::vector<std::vector<uint8_t>> ZipEntries(const char* path, uint32_t max) {
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
    const uint32_t local = u32(p + 42);
    if (usize <= max && (method == 0 || method == 8)) {
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
  g_mem.Commit(kInput, kRegionSize, recomp::Prot::RW);
  g_mem.Commit(kOutput, kRegionSize, recomp::Prot::RW);
  g_mem.Commit(kDestLen, 0x1000, recomp::Prot::RW);
  g_mem.Commit(kStack, kStackSize, recomp::Prot::RW);

  std::mt19937 g(20261019);
  Stats st;
  const std::vector<int32_t> all{1, -1, 0, 2, 3, 4, 5, 6, 7, 8, 9};
  for (int i = 0; i < 400; i++) {
    const int kind = i % 5;
    const uint32_t r = g() % 8;
    const uint32_t size = r == 0 ? g() % 4 : r < 4 ? g() % 4096 : r < 7 ? g() % 100000 : g() % 1100000;
    TestInput(Generate(g, kind, size), all, "generated " + std::to_string(i), g, st);
  }
  const long generated = st.inputs;
  for (int i = 2; i < argc; i++) {
    const long before = st.inputs;
    for (const auto& v : ZipEntries(argv[i], 0x40000))
      TestInput(v, {1}, std::string(argv[i]) + " entry " + std::to_string(st.inputs - before), g,
                st);
    std::printf("%s: %ld entries\n", argv[i], st.inputs - before);
  }
  for (const auto& [kind, n] : st.kinds)
    std::printf("  %-40s %7ld calls, %ld mismatched\n", kind.c_str(), n.first, n.second);
  std::printf("%ld generated + %ld pk3 inputs (%lld bytes), %ld calls (%ld returned errors): "
              "%ld mismatched\n",
              generated, st.inputs - generated, st.bytes, st.cases, st.errors, st.failures);
  return st.failures ? 1 : 0;
}
