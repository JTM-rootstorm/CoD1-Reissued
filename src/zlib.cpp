// compress2 (0x497D00) and uncompress (0x49D8A0) of the EXE's zlib 1.1.4 on
// the host's zlib: the game module's syscalls for its savegames (G_SaveGame
// compresses at level 1, loading uncompresses). Replacements of lifted
// functions named with `replace` in manifest/CoDSP.manifest; FINDINGS.md
// "zlib on the host". Names and evidence are in the Ghidra plate comments.
//
// Both keep 1.1.4's contract rather than the host's compress2/uncompress
// (whose uncompress reports a truncated stream as Z_DATA_ERROR and sets
// *destLen on errors too): one deflate or inflate with Z_FINISH over guest
// memory, Z_BUF_ERROR when it does not finish, *destLen written only on
// success. The z_stream and zlib's allocations are the host's; the
// originals' were on the guest stack and heap (zcalloc 0x49EBB0).
//
// Where the host's result is not the original's, the original runs
// (zlib_oracle): compress2 at levels 0, 2 and 5, where 1.1.4 writes other
// FLEVEL bits in the header (and, at 0, other stored blocks), and any
// uncompress that fails, where 1.1.4 leaves other partial output, or
// accepts a corrupt stream that the host's zlib rejects. uncompress
// therefore inflates into a host buffer first.

#include <zlib.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "runtime/lifted.h"

namespace recomp::gen {

void f_00497D00_lifted(Ctx& c);
void f_0049D8A0_lifted(Ctx& c);

namespace {

using lift::Rd;
using lift::Wr;

// A guest pointer for zlib: null stays null, for its argument checks.
uint8_t* Host(uint32_t va) { return va ? g_guest_base + va : nullptr; }

int32_t Compress2(uint32_t dest, uint32_t dest_len, uint32_t source, uint32_t source_len,
                  int32_t level) {
  z_stream s{};
  s.next_in = Host(source);
  s.avail_in = source_len;
  s.next_out = Host(dest);
  s.avail_out = Rd<uint32_t>(g_guest_base, dest_len);
  int32_t err = deflateInit(&s, level);
  if (err != Z_OK) return err;
  err = deflate(&s, Z_FINISH);
  if (err != Z_STREAM_END) {
    deflateEnd(&s);
    return err == Z_OK ? Z_BUF_ERROR : err;
  }
  Wr<uint32_t>(g_guest_base, dest_len, uint32_t(s.total_out));
  return deflateEnd(&s);
}

// Z_STREAM_END and the output's size, or the error.
int32_t Uncompress(uint32_t dest_len, uint32_t source, uint32_t source_len,
                   std::vector<uint8_t>& out) {
  out.resize(Rd<uint32_t>(g_guest_base, dest_len));
  z_stream s{};
  s.next_in = Host(source);
  s.avail_in = source_len;
  s.next_out = out.data();
  s.avail_out = uInt(out.size());
  int32_t err = inflateInit(&s);
  if (err != Z_OK) return err;
  err = inflate(&s, Z_FINISH);
  out.resize(s.total_out);
  inflateEnd(&s);
  return err;
}

}  // namespace

// compress2(EDX dest, EAX source, ECX sourceLen; destLen, level on the
// stack) -> EAX.
void f_00497D00(Ctx& c) {
  const uint32_t dest_len = Rd<uint32_t>(g_guest_base, c.esp + 4);
  const int32_t level = int32_t(Rd<uint32_t>(g_guest_base, c.esp + 8));
  if (level < -1 || level > 9 || level == 0 || level == 2 || level == 5) {
    f_00497D00_lifted(c);
    return;
  }
  c.eax = uint32_t(Compress2(c.edx, dest_len, c.eax, c.ecx, level));
  c.esp += 4;
}

// uncompress(EDX dest, EDI destLen, EAX source, ECX sourceLen) -> EAX.
void f_0049D8A0(Ctx& c) {
  std::vector<uint8_t> out;
  if (!c.edx || Uncompress(c.edi, c.eax, c.ecx, out) != Z_STREAM_END) {
    f_0049D8A0_lifted(c);
    return;
  }
  std::memcpy(g_guest_base + c.edx, out.data(), out.size());
  Wr<uint32_t>(g_guest_base, c.edi, uint32_t(out.size()));
  c.eax = Z_OK;
  c.esp += 4;
}

}  // namespace recomp::gen
