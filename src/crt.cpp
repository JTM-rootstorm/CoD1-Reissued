// Functions of the EXE's MSVC 7 CRT, natively. Replacements of lifted
// functions named with `replace` in manifest/CoDSP.manifest; FINDINGS.md
// "The pk3 directory scan". Names and evidence are in the Ghidra plate
// comments.

#include <cstdint>

#include "runtime/lifted.h"

namespace recomp::gen {

void f_0051B3DD_lifted(Ctx& c);

namespace {

using lift::Rd;

// __ptlocinfo, the global locale, and the C locale it starts as
// (__initiallocinfo), whose LC_CTYPE handle (+0x14) is 0.
constexpr uint32_t kPtlocinfo = 0x563404, kInitialLocinfo = 0x5633B0;

}  // namespace

// tolower(c) -> EAX (0x51B3DD). It looks up the thread's locale (__getptd,
// through TlsGetValue) and, if it lags the global one, updates it; in the C
// locale _tolower_l (0x51B315) maps 'A'-'Z' (signed compares) and leaves
// everything else. The file system lowers every character of every file
// name it hashes with it. While the global locale is still the initial C
// locale, no thread can have another (setlocale replaces __ptlocinfo), so
// this is the original's result; otherwise the original runs.
void f_0051B3DD(Ctx& c) {
  const uint32_t locinfo = Rd<uint32_t>(g_guest_base, kPtlocinfo);
  if (locinfo != kInitialLocinfo || Rd<uint32_t>(g_guest_base, locinfo + 0x14) != 0) {
    f_0051B3DD_lifted(c);
    return;
  }
  const int32_t ch = int32_t(Rd<uint32_t>(g_guest_base, c.esp + 4));
  c.eax = uint32_t(ch >= 'A' && ch <= 'Z' ? ch + 0x20 : ch);
  c.esp += 4;
}

}  // namespace recomp::gen
