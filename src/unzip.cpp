// Reading files out of pk3s (zip) on the host's zlib: unzOpenCurrentFile
// (0x49E6D0), unzReadCurrentFile (0x49E7F0) and unzCloseCurrentFile
// (0x49EAA0) of the EXE's unzip 0.15 (Quake 3's), whose inflate and crc32
// are zlib 1.1.4 linked in statically; and reading the central directory,
// unzlocal_GetCurrentFileInfoInternal (0x49DE20). Replacements of lifted
// functions named with `replace` in manifest/CoDSP.manifest; FINDINGS.md
// "zlib on the host" and "The pk3 directory scan". Names and evidence are
// in the Ghidra plate comments.
//
// The unzip state stays the original's, in guest memory: unz_s and the
// file_in_zip_read_info_s these allocate with the game's malloc, the 16 KB
// read buffer filled through the game's CRT (fseek/fread on its FILE), and
// the output written straight into the caller's buffer. Only the inflate
// state is the host's: a z_stream per open file, kept here under the read
// info's address (also what the guest z_stream's `state` holds), its input
// and output pointing into guest memory.
//
// What the engine reads back is exact: return values, the output, and the
// read info's total_out (unztell, inlined in FS_Seek), crc32 and remaining
// sizes. Not reproduced: where zlib 1.1.4 is between calls. It decodes
// ahead into its 32 KB window and so consumes input sooner, which changes
// next_in, avail_in and total_in, and with them when the read buffer is
// refilled (pos_in_zipfile, rest_read_compressed); its msg strings and its
// allocations from the game's heap (zcalloc 0x49EBB0, zcfree 0x49EBD0),
// which the host's own replace.

#include <zlib.h>

#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "runtime/lifted.h"

namespace recomp::gen {

void f_0051BBA7(Ctx& c);  // malloc(size)
void f_0051B768(Ctx& c);  // free(p)
void f_0051C3A6(Ctx& c);  // fseek(file, offset, origin)
void f_0051BFD5(Ctx& c);  // fread(buf, size, count, file)
void f_0049E4D0(Ctx& c);  // unzlocal_CheckCurrentFileCoherencyHeader(ESI s; piSizeVar,
                          // poffset_local_extrafield, psize_local_extrafield)
void f_0049DE20_lifted(Ctx& c);

namespace {

using lift::Rd;
using lift::Wr;

// unz_s (0x80 bytes); cur_file_info is unz_file_info, then its internal
// part (the local header's offset).
struct UnzS {
  uint32_t file;  // FILE*
  uint32_t number_entry, size_comment;
  uint32_t byte_before_the_zipfile;
  uint32_t num_file, pos_in_central_dir, current_file_ok;
  uint32_t central_pos, size_central_dir, offset_central_dir;
  uint32_t version, version_needed, flag, compression_method, dosDate, crc, compressed_size,
      uncompressed_size, size_filename, size_file_extra, size_file_comment, disk_num_start,
      internal_fa, external_fa, tmu_date[6];
  uint32_t offset_curfile;
  uint32_t pfile_in_zip_read;
};
// file_in_zip_read_info_s (0x6C bytes), its z_stream at +4.
struct ReadInfo {
  uint32_t read_buffer;
  uint32_t next_in, avail_in, total_in, next_out, avail_out, total_out, msg, state, zalloc, zfree,
      opaque, data_type, adler, reserved;
  uint32_t pos_in_zipfile, stream_initialised, offset_local_extrafield, size_local_extrafield,
      pos_local_extrafield, crc32, crc32_wait, rest_read_compressed, rest_read_uncompressed;
  uint32_t file;  // FILE*
  uint32_t compression_method, byte_before_the_zipfile;
};
static_assert(sizeof(UnzS) == 0x80 && sizeof(ReadInfo) == 0x6C);

// Swap_Init's LittleShort and LittleLong, and the functions that do not
// swap, which it installs on this host.
constexpr uint32_t kLittleShort = 0x8CC718, kLittleLong = 0x8C49A0, kShortNoSwap = 0x4403D0,
                   kLongNoSwap = 0x440410;
constexpr int32_t kParamError = -102, kEndOfList = -100, kBadZipFile = -103,
                  kInternalError = -104, kCrcError = -105;
// zlib 1.1.4's default allocators, which its inflateInit2_ stores in the
// stream.
constexpr uint32_t kZcalloc = 0x49EBB0, kZcfree = 0x49EBD0;

template <typename T>
T& At(uint32_t va) {
  return *reinterpret_cast<T*>(g_guest_base + va);
}

std::mutex g_lock;
std::unordered_map<uint32_t, z_stream> g_streams;  // by read info

// A cdecl CRT function of the EXE, called from the original's call site
// whose return address is ret; returns EAX.
template <typename... A>
uint32_t CallCdecl(Ctx& c, GuestFn fn, uint32_t ret, A... args) {
  const uint32_t a[] = {uint32_t(args)...};
  for (size_t i = sizeof...(A); i-- > 0;) {
    c.esp -= 4;
    Wr<uint32_t>(g_guest_base, c.esp, a[i]);
  }
  c.esp -= 4;
  Wr<uint32_t>(g_guest_base, c.esp, ret);
  fn(c);
  c.esp += 4 * sizeof...(A);
  return c.eax;
}

// crc32 (0x497DC0) over guest memory; a null buffer gives 0.
uint32_t Crc(uint32_t crc, uint32_t buf, uint32_t len) {
  return buf ? uint32_t(crc32(crc, g_guest_base + buf, len)) : 0;
}

int32_t CloseCurrentFile(Ctx& c, uint32_t s) {
  if (!s) return kParamError;
  const uint32_t info_va = At<UnzS>(s).pfile_in_zip_read;
  if (!info_va) return kParamError;
  ReadInfo& info = At<ReadInfo>(info_va);
  int32_t err = 0;
  if (info.rest_read_uncompressed == 0 && info.crc32 != info.crc32_wait) err = kCrcError;
  CallCdecl(c, f_0051B768, 0x49EADA, info.read_buffer);
  info.read_buffer = 0;
  if (info.stream_initialised) {
    std::lock_guard<std::mutex> lock(g_lock);
    auto it = g_streams.find(info_va);
    if (it != g_streams.end()) {
      inflateEnd(&it->second);
      g_streams.erase(it);
    }
    info.state = 0;
  }
  info.stream_initialised = 0;
  CallCdecl(c, f_0051B768, 0x49EAFF, info_va);
  At<UnzS>(s).pfile_in_zip_read = 0;
  return err;
}

int32_t OpenCurrentFile(Ctx& c, uint32_t s) {
  if (!s || !At<UnzS>(s).current_file_ok) return kParamError;
  if (At<UnzS>(s).pfile_in_zip_read) CloseCurrentFile(c, s);
  // The local header's checks, lifted; its three results in the
  // original's locals.
  const uint32_t esp = c.esp, esi = c.esi;
  c.esp -= 0x20;
  const uint32_t size_var = c.esp + 0x14, offset_extra = c.esp + 0x18,
                 size_extra = c.esp + 0x1C;
  c.esi = s;
  const uint32_t bad = CallCdecl(c, f_0049E4D0, 0x49E70F, size_var, offset_extra, size_extra);
  const uint32_t header_extra = At<uint32_t>(size_var), local_extra_offset = At<uint32_t>(offset_extra),
                 local_extra_size = At<uint32_t>(size_extra);
  c.esi = esi;
  c.esp = esp;
  if (bad) return kBadZipFile;
  const uint32_t info_va = CallCdecl(c, f_0051BBA7, 0x49E72A, 0x6C);
  if (!info_va) return kInternalError;
  ReadInfo& info = At<ReadInfo>(info_va);
  info.read_buffer = CallCdecl(c, f_0051BBA7, 0x49E73D, 0x4000);
  info.offset_local_extrafield = local_extra_offset;
  info.size_local_extrafield = local_extra_size;
  info.pos_local_extrafield = 0;
  if (!info.read_buffer) {
    CallCdecl(c, f_0051B768, 0x49E75D, info_va);
    return kInternalError;
  }
  const UnzS& z = At<UnzS>(s);
  info.stream_initialised = 0;
  info.crc32_wait = z.crc;
  info.crc32 = 0;
  info.compression_method = z.compression_method;
  info.file = z.file;
  info.byte_before_the_zipfile = z.byte_before_the_zipfile;
  info.total_out = 0;
  if (z.compression_method != 0) {
    // inflateInit2_(-MAX_WBITS): raw deflate, as zip stores it.
    std::lock_guard<std::mutex> lock(g_lock);
    z_stream& h = g_streams[info_va];
    h = z_stream{};
    if (inflateInit2(&h, -MAX_WBITS) == Z_OK) {
      info.total_in = 0;
      info.msg = 0;
      info.state = info_va;
      info.zalloc = kZcalloc;
      info.zfree = kZcfree;
      info.opaque = 0;
      info.stream_initialised = 1;
    } else {
      g_streams.erase(info_va);
      info.zalloc = info.zfree = info.opaque = 0;
    }
  }
  info.rest_read_compressed = z.compressed_size;
  info.rest_read_uncompressed = z.uncompressed_size;
  info.pos_in_zipfile = z.offset_curfile + 0x1E + header_extra;
  info.avail_in = 0;
  At<UnzS>(s).pfile_in_zip_read = info_va;
  return 0;
}

// inflate(Z_SYNC_FLUSH) on the read info's stream, its guest fields
// advanced as zlib advances the host's.
int32_t Inflate(uint32_t info_va) {
  ReadInfo& info = At<ReadInfo>(info_va);
  z_stream* h;
  {
    std::lock_guard<std::mutex> lock(g_lock);
    auto it = g_streams.find(info_va);
    if (it == g_streams.end()) return Z_STREAM_ERROR;
    h = &it->second;
  }
  h->next_in = g_guest_base + info.next_in;
  h->avail_in = info.avail_in;
  h->next_out = g_guest_base + info.next_out;
  h->avail_out = info.avail_out;
  const int32_t err = inflate(h, Z_SYNC_FLUSH);
  const uint32_t in = uint32_t(h->next_in - (g_guest_base + info.next_in));
  const uint32_t out = uint32_t(h->next_out - (g_guest_base + info.next_out));
  info.next_in += in;
  info.avail_in = h->avail_in;
  info.total_in += in;
  info.next_out += out;
  info.avail_out = h->avail_out;
  info.total_out += out;
  return err;
}

int32_t ReadCurrentFile(Ctx& c, uint32_t s, uint32_t buf, uint32_t len) {
  if (!s) return kParamError;
  const uint32_t info_va = At<UnzS>(s).pfile_in_zip_read;
  if (!info_va) return kParamError;
  ReadInfo& info = At<ReadInfo>(info_va);
  if (!info.read_buffer) return kEndOfList;
  if (!len) return 0;
  info.next_out = buf;
  info.avail_out = len < info.rest_read_uncompressed ? len : info.rest_read_uncompressed;
  uint32_t read = 0;
  while (info.avail_out > 0) {
    if (info.avail_in == 0 && info.rest_read_compressed > 0) {
      const uint32_t n = info.rest_read_compressed < 0x4000 ? info.rest_read_compressed : 0x4000;
      if (CallCdecl(c, f_0051C3A6, 0x49E898, info.file,
                    info.pos_in_zipfile + info.byte_before_the_zipfile, 0) != 0)
        return -1;
      if (CallCdecl(c, f_0051BFD5, 0x49E8B2, info.read_buffer, n, 1, info.file) != 1) return -1;
      info.pos_in_zipfile += n;
      info.rest_read_compressed -= n;
      info.next_in = info.read_buffer;
      info.avail_in = n;
    }
    if (info.compression_method == 0) {
      const uint32_t n = info.avail_out < info.avail_in ? info.avail_out : info.avail_in;
      std::memmove(g_guest_base + info.next_out, g_guest_base + info.next_in, n);
      info.crc32 = Crc(info.crc32, info.next_out, n);
      info.rest_read_uncompressed -= n;
      info.avail_in -= n;
      info.avail_out -= n;
      info.next_out += n;
      info.next_in += n;
      info.total_out += n;
      read += n;
    } else {
      const uint32_t before = info.total_out, out = info.next_out;
      const int32_t err = Inflate(info_va);
      const uint32_t n = info.total_out - before;
      info.crc32 = Crc(info.crc32, out, n);
      info.rest_read_uncompressed -= n;
      read += n;
      if (err == Z_STREAM_END) return int32_t(read);
      if (err != Z_OK) return err;
    }
  }
  return int32_t(read);
}

// The central directory entry at pos_in_central_dir, as the original reads
// it, but with one fread of its 46-byte fixed part where the original
// reads each field with an fread of its own (and LittleShort/LittleLong):
// the same bytes from the same FILE, which ends in the same state. Returns
// false, having changed nothing the original would not redo, when the
// original must run: the fseek fails (it then reads on from wherever the
// FILE is, without the signature), the fixed part is short (it keeps stale
// values), or the swap functions are not the no-swap ones.
bool GetCurrentFileInfo(Ctx& c, uint32_t s, int32_t* result) {
  auto arg = [&](int n) { return Rd<uint32_t>(g_guest_base, c.esp + 4 + 4 * n); };
  const uint32_t info = arg(0), info_internal = arg(1), name = arg(2), name_size = arg(3),
                 extra = arg(4), extra_size = arg(5), comment = arg(6), comment_size = arg(7);
  if (At<uint32_t>(kLittleShort) != kShortNoSwap || At<uint32_t>(kLittleLong) != kLongNoSwap)
    return false;
  const UnzS& z = At<UnzS>(s);
  const uint32_t file = z.file;
  const uint32_t esp = c.esp;
  c.esp -= 0x58;  // the original's frame; the fixed part goes in it
  const uint32_t fixed = c.esp + 0x18;
  const bool read = CallCdecl(c, f_0051C3A6, 0x49DE52, file,
                              z.pos_in_central_dir + z.byte_before_the_zipfile, 0) == 0 &&
                    CallCdecl(c, f_0051BFD5, 0x49DE74, fixed, 46, 1, file) == 1;
  if (!read) {
    c.esp = esp;
    return false;
  }
  const uint8_t* h = g_guest_base + fixed;
  // Shorts sign-extended (LittleShort returns a short), longs as read.
  auto i16 = [&](int o) { return uint32_t(int32_t(int16_t(h[o] | h[o + 1] << 8))); };
  auto u32 = [&](int o) {
    return uint32_t(h[o] | h[o + 1] << 8 | h[o + 2] << 16) | uint32_t(h[o + 3]) << 24;
  };
  int32_t err = u32(0) == 0x02014B50 ? 0 : kBadZipFile;
  // unz_file_info: version, version_needed, flag, compression_method,
  // dosDate, crc, compressed_size, uncompressed_size, size_filename,
  // size_file_extra, size_file_comment, disk_num_start, internal_fa,
  // external_fa, then tmu_date from dosDate.
  uint32_t fi[20] = {i16(4),  i16(6),  i16(8),  i16(10), u32(12), u32(16), u32(20),
                     u32(24), i16(28), i16(30), i16(32), i16(34), i16(36), u32(38)};
  const uint32_t dos = fi[4], date = dos >> 16;
  fi[14] = (dos & 0x1F) << 1;
  fi[15] = dos >> 5 & 0x3F;
  fi[16] = dos >> 11 & 0x1F;
  fi[17] = date & 0x1F;
  fi[18] = (date >> 5 & 0xF) - 1;
  fi[19] = (date >> 9 & 0x7F) + 1980;
  const uint32_t offset = u32(42);

  // The variable part, sizes compared unsigned as the original does.
  uint32_t seek = fi[8], extra_left = fi[9];
  const uint32_t comment_len = fi[10];
  if (err == 0 && name) {
    uint32_t n = name_size;
    if (seek < name_size) {
      At<uint8_t>(name + seek) = 0;
      n = seek;
    }
    if (seek && name_size && CallCdecl(c, f_0051BFD5, 0x49E127, name, n, 1, file) != 1) err = -1;
    seek -= n;
  }
  if (err == 0 && extra) {
    const uint32_t n = extra_left < extra_size ? extra_left : extra_size;
    if (seek) {
      if (CallCdecl(c, f_0051C3A6, 0x49E169, file, seek, 1) == 0)
        seek = 0;
      else
        err = -1;
    }
    if (extra_left && extra_size && CallCdecl(c, f_0051BFD5, 0x49E19B, extra, n, 1, file) != 1)
      err = -1;
    extra_left -= n;
  }
  seek += extra_left;
  if (err == 0 && comment) {
    uint32_t n = comment_size;
    if (comment_len < comment_size) {
      At<uint8_t>(comment + comment_len) = 0;
      n = comment_len;
    }
    if (seek && CallCdecl(c, f_0051C3A6, 0x49E1EA, file, seek, 1) != 0) err = -1;
    if (comment_len && comment_size &&
        CallCdecl(c, f_0051BFD5, 0x49E21B, comment, n, 1, file) != 1) {
      c.esp = esp;
      *result = -1;
      return true;
    }
  }
  if (err == 0) {
    if (info) std::memcpy(g_guest_base + info, fi, sizeof fi);
    if (info_internal) At<uint32_t>(info_internal) = offset;
  }
  c.esp = esp;
  *result = err;
  return true;
}

}  // namespace

// unzlocal_GetCurrentFileInfoInternal(EAX s; pfile_info,
// pfile_info_internal, szFileName, fileNameBufferSize, extraField,
// extraFieldBufferSize, szComment, commentBufferSize) -> EAX.
void f_0049DE20(Ctx& c) {
  const uint32_t s = c.eax;
  int32_t err = kParamError;
  if (s && !GetCurrentFileInfo(c, s, &err)) {
    c.eax = s;
    f_0049DE20_lifted(c);
    return;
  }
  c.eax = uint32_t(err);
  c.esp += 4;
}

// unzOpenCurrentFile(EAX s) -> EAX.
void f_0049E6D0(Ctx& c) {
  c.eax = uint32_t(OpenCurrentFile(c, c.eax));
  c.esp += 4;
}

// unzReadCurrentFile(EAX s, ECX len; buf on the stack) -> EAX, the bytes
// read or an error.
void f_0049E7F0(Ctx& c) {
  const uint32_t buf = Rd<uint32_t>(g_guest_base, c.esp + 4);
  c.eax = uint32_t(ReadCurrentFile(c, c.eax, buf, c.ecx));
  c.esp += 4;
}

// unzCloseCurrentFile(EDI s) -> EAX.
void f_0049EAA0(Ctx& c) {
  c.eax = uint32_t(CloseCurrentFile(c, c.edi));
  c.esp += 4;
}

}  // namespace recomp::gen
