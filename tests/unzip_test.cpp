// The native pk3 reads (src/unzip.cpp, on the host's zlib) against the
// original unzip 0.15 and zlib 1.1.4, lifted from the EXE at build time
// with `recomp_lift --only` (CMakeLists.txt). CoDSP.exe is mapped as the
// game maps it; the CRT calls the unzip code makes (fopen, fseek, ftell,
// fread, fclose, malloc, free) and the allocator under zlib's zcalloc
// (0x42A480) are stubs here on host stdio and a bump arena.
//
//   unzip_test EXE PK3...
//
// Each zip is opened and walked with the lifted unzOpen / unzGoToFirstFile
// / unzGoToNextFile, and every entry is read by the lifted original and by
// the native code from the same memory: unzOpenCurrentFile, reads in three
// patterns (the whole file in one call and once more at the end; random
// chunk sizes; for small files, half of it then a reopen from the start,
// as FS_Seek does), unzCloseCurrentFile. After every call both must have
// returned the same, written the same bytes into the caller's buffer
// (padded with a guard pattern), left the same unz_s and the same read info
// fields the engine and unzip read back (crc32 and its expected value,
// total_out, the remaining uncompressed size, next_out/avail_out and the
// rest), and the same ESP, EBX, ESI, EDI and EBP. Not compared: zlib 1.1.4's
// position (next_in, avail_in, total_in, msg, state, adler, data_type) and
// the read buffer refills it leads to (pos_in_zipfile,
// rest_read_compressed); see src/unzip.cpp. A zip made here adds stored
// entries (the pk3s store only directories) and the host's own deflate
// output.

#include <zlib.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "runtime/guest_memory.h"
#include "runtime/lifted.h"
#include "runtime/pe_image.h"

namespace recomp {
uint8_t* g_guest_base = nullptr;

namespace gen {
void f_0049E6D0_lifted(Ctx& c);
void f_0049E7F0_lifted(Ctx& c);
void f_0049EAA0_lifted(Ctx& c);
// The native code (tests/unzip_native.cpp renames its entries)...
void n_0049E6D0(Ctx& c);
void n_0049E7F0(Ctx& c);
void n_0049EAA0(Ctx& c);
// ...and the lifted originals call each other, not it.
void f_0049E6D0(Ctx& c) { f_0049E6D0_lifted(c); }
void f_0049E7F0(Ctx& c) { f_0049E7F0_lifted(c); }
void f_0049EAA0(Ctx& c) { f_0049EAA0_lifted(c); }
void f_0049DB90(Ctx& c);  // unzOpen(EAX path) -> EAX unz_s
void f_0049E2A0(Ctx& c);  // unzGoToFirstFile(ESI s)
void f_0049E2E0(Ctx& c);  // unzGoToNextFile(ESI s)
}  // namespace gen

PeImage* g_exe = nullptr;

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

// malloc's arena, the callers' read buffers and the stack.
constexpr uint32_t kArena = 0x30000000, kArenaSize = 0x4000000;
constexpr uint32_t kBuffer = 0x40000000, kBufferSize = 0x4000000;
constexpr uint32_t kStack = 0x2FF00000, kStackSize = 0x100000;
constexpr uint32_t kPath = 0x2FE00000;
constexpr uint32_t kGuard = 16;  // pattern bytes after each read's buffer

uint32_t Rd(uint32_t va) { return g_mem.read<uint32_t>(va); }
void Wr(uint32_t va, uint32_t v) { g_mem.write<uint32_t>(va, v); }
uint32_t Arg(const Ctx& c, int n) { return Rd(c.esp + 4 + 4 * n); }

// malloc's half of the arena, and the other half for zlib 1.1.4's own
// allocations, which the native side doesn't make: the read infos and read
// buffers then get the same addresses on both sides.
constexpr uint32_t kZlibArena = kArena + kArenaSize / 2;
uint32_t g_top = kArena, g_zlib_top = kZlibArena;
uint32_t Alloc(uint32_t& top, uint32_t size, uint32_t end) {
  const uint32_t va = top;
  top = (top + size + 15) & ~15u;
  if (top > end) {
    std::printf("arena full\n");
    std::abort();
  }
  return va;
}

std::vector<std::FILE*> g_files;  // guest FILE* i + 1
std::FILE* File(uint32_t f) { return g_files.at(f - 1); }

}  // namespace

// The CRT and allocator stubs (cdecl: the caller pops).
namespace recomp::gen {
void f_0051BBA7(Ctx& c) {  // malloc: the arena, filled with a pattern
  const uint32_t size = Arg(c, 0);
  c.eax = Alloc(g_top, size, kZlibArena);
  std::memset(g_mem.ptr(c.eax), 0xCD, size);
  c.esp += 4;
}
void f_0051B768(Ctx& c) { c.esp += 4; }  // free
void f_0042A480(Ctx& c) {                // zcalloc's allocation, zeroed
  const uint32_t size = Arg(c, 0);
  c.eax = Alloc(g_zlib_top, size, kArena + kArenaSize);
  std::memset(g_mem.ptr(c.eax), 0, size);
  c.esp += 4;
}
void f_0051BDFD(Ctx& c) {  // fopen(path, mode)
  std::FILE* f = std::fopen(reinterpret_cast<const char*>(g_mem.ptr(Arg(c, 0))), "rb");
  c.eax = 0;
  if (f) {
    g_files.push_back(f);
    c.eax = uint32_t(g_files.size());
  }
  c.esp += 4;
}
void f_0051C43B(Ctx& c) {  // fclose(file)
  std::fclose(File(Arg(c, 0)));
  c.eax = 0;
  c.esp += 4;
}
void f_0051C3A6(Ctx& c) {  // fseek(file, offset, origin)
  c.eax = uint32_t(std::fseek(File(Arg(c, 0)), int32_t(Arg(c, 1)), int(Arg(c, 2))));
  c.esp += 4;
}
void f_0051C2D6(Ctx& c) {  // ftell(file)
  c.eax = uint32_t(std::ftell(File(Arg(c, 0))));
  c.esp += 4;
}
void f_0051BFD5(Ctx& c) {  // fread(buf, size, count, file)
  c.eax = uint32_t(std::fread(g_mem.ptr(Arg(c, 0)), Arg(c, 1), Arg(c, 2), File(Arg(c, 3))));
  c.esp += 4;
}
}  // namespace recomp::gen

namespace {

Ctx NewCtx(std::mt19937& g) {
  Ctx c{};
  c.eax = g();
  c.ecx = g();
  c.edx = g();
  c.ebx = g();
  c.ebp = g();
  c.esi = g();
  c.edi = g();
  c.esp = kStack + kStackSize - 0x100;
  recomp::x87::Init(c.fpu);
  c.mxcsr = recomp::sse::kMxcsrDefault;
  return c;
}

// A call of an entry with its register argument; returns EAX.
uint32_t Call(Ctx& c, recomp::GuestFn fn, uint32_t eax) {
  c.eax = eax;
  c.esp -= 4;
  Wr(c.esp, 0xDEAD0000);
  fn(c);
  return c.eax;
}

// Everything one side's call left that must agree.
struct State {
  uint32_t ret, esp, ebx, esi, edi, ebp;
  uint8_t unz[0x80];
  uint32_t info[27];
  std::vector<uint8_t> out;
  bool operator==(const State& o) const {
    return ret == o.ret && esp == o.esp && ebx == o.ebx && esi == o.esi && edi == o.edi &&
           ebp == o.ebp && !std::memcmp(unz, o.unz, sizeof unz) &&
           !std::memcmp(info, o.info, sizeof info) && out == o.out;
  }
};

// ReadInfo's words not compared: next_in, avail_in, total_in, msg, state,
// data_type, adler, reserved, pos_in_zipfile, rest_read_compressed.
constexpr uint32_t kSkipWords = (1u << 1) | (1u << 2) | (1u << 3) | (1u << 7) | (1u << 8) |
                                (1u << 12) | (1u << 13) | (1u << 14) | (1u << 15) | (1u << 22);

State Snapshot(const Ctx& c, uint32_t s, uint32_t buf, uint32_t len) {
  State t{};
  t.ret = c.eax;
  t.esp = c.esp;
  t.ebx = c.ebx;
  t.esi = c.esi;
  t.edi = c.edi;
  t.ebp = c.ebp;
  std::memcpy(t.unz, g_mem.ptr(s), 0x80);
  const uint32_t info = Rd(s + 0x7C);
  if (info)
    for (int i = 0; i < 27; i++) t.info[i] = (kSkipWords >> i & 1) ? 0 : Rd(info + 4 * i);
  if (len) t.out.assign(g_mem.ptr(buf), g_mem.ptr(buf) + len + kGuard);
  return t;
}

// One step of a read pattern: open, read len bytes, close.
struct Step {
  enum { kOpen, kRead, kClose } op;
  uint32_t len;
};

struct Side {
  recomp::GuestFn open, read, close;
};
const Side kLifted{recomp::gen::f_0049E6D0_lifted, recomp::gen::f_0049E7F0_lifted,
                   recomp::gen::f_0049EAA0_lifted};
const Side kNative{recomp::gen::n_0049E6D0, recomp::gen::n_0049E7F0, recomp::gen::n_0049EAA0};

std::vector<State> RunSide(const Side& side, const std::vector<Step>& steps, uint32_t s,
                           uint32_t seed) {
  std::mt19937 g(seed);
  std::vector<State> out;
  uint32_t pos = 0;
  for (const Step& k : steps) {
    Ctx c = NewCtx(g);
    uint32_t buf = 0, len = 0;
    switch (k.op) {
      case Step::kOpen:
        Call(c, side.open, s);
        pos = 0;
        break;
      case Step::kRead:
        buf = kBuffer + pos;
        len = k.len;
        std::memset(g_mem.ptr(buf), 0xA5, len + kGuard);
        c.ecx = len;
        c.esp -= 4;
        Wr(c.esp, buf);
        Call(c, side.read, s);
        c.esp += 4;
        if (int32_t(c.eax) > 0) pos += c.eax;
        break;
      case Step::kClose:
        c.edi = s;
        Call(c, side.close, g());
        break;
    }
    out.push_back(Snapshot(c, s, buf, len));
  }
  return out;
}

struct Stats {
  long entries = 0, deflated = 0, stored = 0, calls = 0, failures = 0;
  long long bytes = 0;
};

// Runs the steps through both sides from the same memory and compares.
void Compare(const std::vector<Step>& steps, uint32_t s, const std::string& what, Stats& st) {
  const uint32_t top = g_top;
  uint8_t unz[0x80];
  std::memcpy(unz, g_mem.ptr(s), sizeof unz);
  const std::vector<State> a = RunSide(kLifted, steps, s, st.calls);
  g_top = top;
  std::memcpy(g_mem.ptr(s), unz, sizeof unz);
  const std::vector<State> b = RunSide(kNative, steps, s, st.calls);
  g_top = top;
  g_zlib_top = kZlibArena;
  st.calls += long(steps.size());
  for (size_t i = 0; i < steps.size(); i++)
    if (!(a[i] == b[i])) {
      if (st.failures < 20) {
        std::printf("%s: step %zu (op %d len %u) differs: ret %d/%d", what.c_str(), i,
                    int(steps[i].op), steps[i].len, int32_t(a[i].ret), int32_t(b[i].ret));
        for (int w = 0; w < 27; w++)
          if (a[i].info[w] != b[i].info[w])
            std::printf(" info+%02X %08X/%08X", w * 4, a[i].info[w], b[i].info[w]);
        if (a[i].out != b[i].out) std::printf(" output");
        if (std::memcmp(a[i].unz, b[i].unz, 0x80)) std::printf(" unz_s");
        std::printf("\n");
      }
      st.failures++;
      return;
    }
}

void TestEntry(uint32_t s, const std::string& what, Stats& st) {
  const uint32_t method = Rd(s + 0x34), size = Rd(s + 0x44);
  if (size + kGuard > kBufferSize) {
    std::printf("%s: %u bytes, too large for the test\n", what.c_str(), size);
    st.failures++;
    return;
  }
  st.entries++;
  (method ? st.deflated : st.stored)++;
  st.bytes += size;
  using S = Step;
  // The whole file at once, and once more at the end.
  Compare({{S::kOpen, 0}, {S::kRead, size + 1}, {S::kRead, 16}, {S::kClose, 0}}, s, what, st);
  // Random chunks.
  std::mt19937 g(uint32_t(st.entries));
  std::vector<Step> steps{{S::kOpen, 0}};
  for (uint32_t left = size; left;) {
    const uint32_t r = g() % 4;
    uint32_t n = 1 + g() % (r == 0 ? 32 : r == 1 ? 4096 : 100000);
    n = std::min(n, left);
    steps.push_back({S::kRead, n});
    left -= n;
  }
  steps.push_back({S::kRead, 1});
  steps.push_back({S::kClose, 0});
  Compare(steps, s, what + " (chunks)", st);
  // Half, then a reopen (which closes first) and the whole file.
  if (size <= 0x40000)
    Compare({{S::kOpen, 0}, {S::kRead, size / 2}, {S::kOpen, 0}, {S::kRead, size}, {S::kClose, 0}},
            s, what + " (reopen)", st);
}

bool TestZip(const std::string& path, Stats& st) {
  std::mt19937 g(1);
  Ctx c = NewCtx(g);
  std::memcpy(g_mem.ptr(kPath), path.c_str(), path.size() + 1);
  const uint32_t s = Call(c, recomp::gen::f_0049DB90, kPath);
  if (!s) {
    std::printf("%s: unzOpen failed\n", path.c_str());
    return false;
  }
  const long before = st.entries;
  c.esi = s;
  for (int32_t err = int32_t(Call(c, recomp::gen::f_0049E2A0, 0)); err == 0;
       err = int32_t(Call(c, recomp::gen::f_0049E2E0, 0))) {
    TestEntry(s, path + " #" + std::to_string(Rd(s + 0x10)), st);
    c.esi = s;
  }
  std::printf("%s: %ld entries\n", path.c_str(), st.entries - before);
  g_top = kArena;
  g_zlib_top = kZlibArena;
  return true;
}

// ---- a zip of our own ------------------------------------------------------

void Put16(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(uint8_t(x));
  v.push_back(uint8_t(x >> 8));
}
void Put32(std::vector<uint8_t>& v, uint32_t x) {
  Put16(v, x & 0xFFFF);
  Put16(v, x >> 16);
}

// Stored entries of random sizes and contents, and the same deflated by
// the host's zlib at several levels.
std::string MakeZip(const char* dir) {
  std::mt19937 g(20261019);
  std::vector<uint8_t> zip, central;
  int count = 0;
  for (int i = 0; i < 64; i++) {
    const uint32_t r = g() % 4;
    std::vector<uint8_t> data(r == 0 ? g() % 64 : r == 1 ? g() % 70000 : g() % 400000);
    // Text-like runs for the compressor, random bytes now and then.
    for (size_t j = 0; j < data.size(); j++)
      data[j] = (g() % 16) ? uint8_t('a' + (j / (1 + g() % 64)) % 23) : uint8_t(g());
    const uint32_t crc = uint32_t(crc32(0, data.data(), uInt(data.size())));
    for (int method : {0, 8}) {
      std::vector<uint8_t> body = data;
      if (method == 8) {
        z_stream z{};
        deflateInit2(&z, i % 10, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
        body.resize(deflateBound(&z, uLong(data.size())));
        z.next_in = data.data();
        z.avail_in = uInt(data.size());
        z.next_out = body.data();
        z.avail_out = uInt(body.size());
        deflate(&z, Z_FINISH);
        body.resize(z.total_out);
        deflateEnd(&z);
      }
      const std::string name = "f" + std::to_string(count++) + (method ? ".z" : ".s");
      const uint32_t offset = uint32_t(zip.size());
      Put32(zip, 0x04034B50);
      for (uint32_t x : {20u, 0u, uint32_t(method), 0u, 0u}) Put16(zip, x);
      Put32(zip, crc);
      Put32(zip, uint32_t(body.size()));
      Put32(zip, uint32_t(data.size()));
      Put16(zip, uint32_t(name.size()));
      Put16(zip, 0);
      zip.insert(zip.end(), name.begin(), name.end());
      zip.insert(zip.end(), body.begin(), body.end());
      Put32(central, 0x02014B50);
      for (uint32_t x : {20u, 20u, 0u, uint32_t(method), 0u, 0u}) Put16(central, x);
      Put32(central, crc);
      Put32(central, uint32_t(body.size()));
      Put32(central, uint32_t(data.size()));
      for (uint32_t x : {uint32_t(name.size()), 0u, 0u, 0u, 0u}) Put16(central, x);
      Put32(central, 0);
      Put32(central, offset);
      central.insert(central.end(), name.begin(), name.end());
    }
  }
  const uint32_t at = uint32_t(zip.size());
  zip.insert(zip.end(), central.begin(), central.end());
  Put32(zip, 0x06054B50);
  for (uint32_t x : {0u, 0u, uint32_t(count), uint32_t(count)}) Put16(zip, x);
  Put32(zip, uint32_t(central.size()));
  Put32(zip, at);
  Put16(zip, 0);
  const std::string path = std::string(dir) + "/unzip_test.zip";
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return {};
  std::fwrite(zip.data(), 1, zip.size(), f);
  std::fclose(f);
  return path;
}

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);  // messages before an abort
  if (argc < 2) {
    std::printf("usage: %s CoDSP.exe [ZIP...]\n", argv[0]);
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
  g_mem.Commit(kBuffer, kBufferSize, recomp::Prot::RW);
  g_mem.Commit(kStack, kStackSize, recomp::Prot::RW);
  g_mem.Commit(kPath, 0x1000, recomp::Prot::RW);
  // Swap_Init on a little-endian machine: LittleShort and LittleLong
  // don't swap.
  Wr(0x8CC718, 0x4403D0);
  Wr(0x8C49A0, 0x440410);

  Stats st;
  const char* tmp = std::getenv("TMPDIR");
  const std::string own = MakeZip(tmp ? tmp : "/tmp");
  if (own.empty() || !TestZip(own, st)) return 1;
  std::remove(own.c_str());
  for (int i = 2; i < argc; i++)
    if (!TestZip(argv[i], st)) return 1;
  std::printf("%ld entries (%ld deflated, %ld stored, %lld bytes), %ld calls: %ld mismatched\n",
              st.entries, st.deflated, st.stored, st.bytes, st.calls, st.failures);
  return st.failures ? 1 : 0;
}
