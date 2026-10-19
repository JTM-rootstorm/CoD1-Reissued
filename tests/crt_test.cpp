// The native CRT functions (src/crt.cpp) against the originals, lifted
// from the EXE at build time with `recomp_lift --only` (CMakeLists.txt).
//
//   crt_test EXE
//
// tolower (0x51B3DD) for every int from -70000 to 70000 and 1000000 random
// ones, the result and ESP, EBX, ESI, EDI and EBP compared: in the C locale
// the game starts with, and with __ptlocinfo pointing at another locale
// (a copy of the C one), where the native code runs the original. The
// CRT's per-thread data (__getptd) is a block here whose locale is the
// global one, as it is in the game until setlocale.

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>

#include "runtime/guest_memory.h"
#include "runtime/lifted.h"
#include "runtime/pe_image.h"

namespace recomp {
uint8_t* g_guest_base = nullptr;

namespace gen {
void f_0051B3DD_lifted(Ctx& c);
// The native code (tests/crt_native.cpp renames its entry)...
void n_0051B3DD(Ctx& c);
// ...and the lifted code calls the original.
void f_0051B3DD(Ctx& c) { f_0051B3DD_lifted(c); }
}  // namespace gen

PeImage* g_exe = nullptr;
constexpr uint32_t kPtd = 0x2FE00000;
uint32_t g_last_error = 0;

// Kernel32 as __getptd uses it (stdcall: the callee pops).
bool Kernel32(Ctx& c, const std::string& name) {
  auto arg = [&](int n) { return *reinterpret_cast<uint32_t*>(g_guest_base + c.esp + 4 + 4 * n); };
  if (name == "GetLastError") {
    c.eax = g_last_error;
    c.esp += 4;
  } else if (name == "SetLastError") {
    g_last_error = arg(0);
    c.esp += 8;
  } else if (name == "TlsGetValue") {
    c.eax = kPtd;
    c.esp += 8;
  } else {
    return false;
  }
  return true;
}

void Dispatch(Ctx& c, uint32_t target, uint32_t va) {
  if (target - kThunkBase < kThunkRegionSize)
    for (const PeImport& i : g_exe->imports)
      if (i.thunk_va == target) {
        if (i.dll == "KERNEL32.dll" && Kernel32(c, i.name)) return;
        std::printf("unexpected call to %s!%s from %08X\n", i.dll.c_str(), i.name.c_str(), va);
        std::abort();
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

constexpr uint32_t kStack = 0x2FF00000, kStackSize = 0x100000;
constexpr uint32_t kPtlocinfo = 0x563404, kInitialLocinfo = 0x5633B0, kOtherLocinfo = 0x2FE01000;

struct Stats {
  long calls = 0, failures = 0;
};

void Tolower(int32_t ch, std::mt19937& g, Stats& s) {
  const uint32_t esp = kStack + kStackSize - 0x100;
  g_mem.write<uint32_t>(esp + 4, uint32_t(ch));
  g_mem.write<uint32_t>(esp, 0xDEAD0000);
  Ctx c0{};
  c0.eax = g(), c0.ecx = g(), c0.edx = g(), c0.ebx = g(), c0.ebp = g(), c0.esi = g(), c0.edi = g();
  c0.esp = esp;
  recomp::x87::Init(c0.fpu);
  Ctx a = c0, b = c0;
  recomp::gen::f_0051B3DD_lifted(a);
  recomp::gen::n_0051B3DD(b);
  s.calls++;
  if (a.eax == b.eax && a.esp == b.esp && a.ebx == b.ebx && a.esi == b.esi && a.edi == b.edi &&
      a.ebp == b.ebp)
    return;
  if (s.failures++ < 20)
    std::printf("tolower(%d): %d / %d\n", ch, int32_t(a.eax), int32_t(b.eax));
}

void AllCases(std::mt19937& g, Stats& s) {
  for (int32_t ch = -70000; ch <= 70000; ch++) Tolower(ch, g, s);
  for (int i = 0; i < 1000000; i++) Tolower(int32_t(g()), g, s);
}

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);  // messages before an abort
  if (argc < 2) {
    std::printf("usage: %s CoDSP.exe\n", argv[0]);
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
  g_mem.Commit(kStack, kStackSize, recomp::Prot::RW);
  g_mem.Commit(recomp::kPtd, 0x2000, recomp::Prot::RW);

  std::mt19937 g(20261019);
  Stats s;
  // The C locale, as the game starts: the thread's locale is the global one.
  g_mem.write<uint32_t>(recomp::kPtd + 0x64, g_mem.read<uint32_t>(kPtlocinfo));
  if (g_mem.read<uint32_t>(kPtlocinfo) != kInitialLocinfo) {
    std::printf("__ptlocinfo is not __initiallocinfo in the image\n");
    return 1;
  }
  AllCases(g, s);
  // Another locale (a copy of the C one): the native code hands over.
  std::memcpy(g_mem.ptr(kOtherLocinfo), g_mem.ptr(kInitialLocinfo), 0x100);
  g_mem.write<uint32_t>(kPtlocinfo, kOtherLocinfo);
  g_mem.write<uint32_t>(recomp::kPtd + 0x64, kOtherLocinfo);
  AllCases(g, s);
  std::printf("%ld tolower calls: %ld mismatched\n", s.calls, s.failures);
  return s.failures ? 1 : 0;
}
