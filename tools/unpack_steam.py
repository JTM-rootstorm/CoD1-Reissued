#!/usr/bin/env python3
"""Statically unpack the Steam DRM wrapper (PECompact 2) of CoDSP/CoDMP.exe.

The Steam release of Call of Duty wraps both EXEs in Valve's 2004 Steam
loader, which is PECompact 2 with a steam.dll subscription check. Nothing
is executed here: the steps of the wrapper's loader are replayed in Python
and the result is written as a normal PE with the original entry point,
import directory and a reconstructed section table.

  entry stub (in .rsrc)   aPLib-unpacks the loader into a VirtualAlloc
                          buffer and calls it
  loader                  LZMA-unpacks the image, moves blocks into place,
                          undoes an E8/E9 call filter, patches words,
                          resolves imports, calls steam.dll, jumps to OEP

Usage: python3 -I tools/unpack_steam.py CoDSP.exe -o unpacked/CoDSP.exe
"""

import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import aplib  # noqa: E402
import lzma1  # noqa: E402

# Loader section record flags (records are 0x1c bytes at ctx+0x50).
F_MOVE = 0x0002
F_FILTER = 0x0008
F_COMPRESSED = 0x0010
F_PATCH_WORDS = 0x0200

TEXT = 0x60000020
RDATA = 0x40000040
DATA = 0xC0000040


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


class Image:
    def __init__(self, data):
        self.file = data
        self.pe = u32(data, 0x3C)
        if data[self.pe:self.pe + 4] != b"PE\0\0":
            sys.exit("not a PE file")
        self.nsec = u16(data, self.pe + 6)
        self.optsize = u16(data, self.pe + 0x14)
        self.opt = self.pe + 0x18
        self.base = u32(data, self.opt + 0x1C)
        self.size = u32(data, self.opt + 0x38)
        self.entry = u32(data, self.opt + 0x10)
        self.mem = bytearray(self.size)
        hdr = u32(data, self.opt + 0x3C)
        self.mem[:hdr] = data[:hdr]
        for i in range(self.nsec):
            s = self.opt + self.optsize + i * 0x28
            va, raw_size, raw_ptr = u32(data, s + 12), u32(data, s + 16), u32(data, s + 20)
            self.mem[va:va + raw_size] = data[raw_ptr:raw_ptr + raw_size]

    def rva(self, va):
        return va - self.base


def find_stub_context(img):
    """Entry is `mov eax, stub; jmp eax`; the stub starts with
    `mov eax, K; push x6; lea ebx, [eax + D]` and ebx is its context."""
    e = img.entry
    m = img.mem
    if m[e] != 0xB8 or m[e + 5:e + 7] != b"\xff\xe0":
        sys.exit("entry is not the Steam/PECompact stub (already unpacked?)")
    stub = img.rva(u32(m, e + 1))
    if m[stub] != 0xB8 or m[stub + 11:stub + 13] != b"\x8d\x98":
        sys.exit("unrecognised stub at RVA %#x" % stub)
    return img.rva(u32(m, stub + 1) + u32(m, stub + 13))


def lzma_codec(src):
    """PECompact LZMA codec block: u32 unpacked size, then a 13-byte .lzma
    header (props, u32 dictionary size, u64 size) and the stream."""
    return lzma1.decode(src[4], src[17:], u32(src, 0))


def unfilter_calls(mem, start, length, marker_e8, marker_e9):
    """Undo the E8/E9 filter: a call/jmp whose displacement's first byte
    is the marker stores a big-endian 24-bit absolute target in the next
    three bytes. Returns the loader's checksum over the region."""
    total = 0
    i = 0
    p = start
    while i < length:
        op = mem[p]
        p += 1
        total += op
        marker = marker_e8 if op == 0xE8 else marker_e9 if op == 0xE9 else None
        if marker is not None:
            v = u32(mem, p)
            if v & 0xFF == marker:
                v = (((v >> 8) & 0xFF) << 16 | ((v >> 16) & 0xFF) << 8 | (v >> 24)) - i
                v &= 0xFFFFFFFF
                struct.pack_into("<I", mem, p, v)
            total += v
            p += 4
            i += 4
        i += 1
    return total & 0xFFFFFFFF


def unpack(img):
    m = img.mem
    sctx = find_stub_context(img)
    loader_rva, loader_size, loader_entry = u32(m, sctx), u32(m, sctx + 4), u32(m, sctx + 8)
    loader, _ = aplib.depack(m, loader_rva)
    if len(loader) != loader_size:
        sys.exit("loader: got %#x bytes, expected %#x" % (len(loader), loader_size))
    if b"SteamIsAppSubscribed" not in loader:
        print("warning: loader has no steam.dll check; not the Steam wrapper?")

    ctx = loader_entry - (u32(loader, loader_entry - 4) + 4)
    oep = u32(loader, ctx + 0x0C)
    ncodecs = u16(loader, ctx + 0x02)
    import_rva = u32(loader, ctx + 0x34)
    nrec = u32(loader, ctx + 0x3C)
    patch_at, patch_from = u32(loader, ctx + 0x40), u32(loader, ctx + 0x44)
    jmp_mode = u32(loader, ctx + 0x48)
    table = ctx - u32(loader, ctx + 0x30)
    if ncodecs != 1 or u32(loader, table + 4) != 0:
        sys.exit("expected exactly one codec, got %d" % ncodecs)
    codec = table + u32(loader, table)
    # LzmaDecode's literal-table setup: mov eax, 0x300; shl eax, cl
    if b"\xb8\x00\x03\x00\x00\xd3\xe0" not in loader[codec:ctx]:
        sys.exit("codec at loader+%#x is not LZMA" % codec)

    recs = [struct.unpack_from("<IIIIHHI", loader, ctx + 0x50 + i * 0x1C)
            for i in range(nrec)]

    # The entry stub overwrote the first bytes of the packed data.
    if jmp_mode != 1 and patch_from:
        m[patch_at:patch_at + 7] = m[patch_from:patch_from + 7]

    # Records: (r0, r4, size, _, flags, word, r14). Compressed: packed at
    # r0 (size - r14 bytes), unpacked to r4. Moves: r4 -> r0.
    for packed, out_rva, size, _, flags, _, tail in recs:
        if flags & F_COMPRESSED:
            out = lzma_codec(m[packed:packed + size - tail])
            m[out_rva:out_rva + len(out)] = out
            end = out_rva + len(out)
            m[end:(end + 0xFFF) & ~0xFFF] = bytes(((end + 0xFFF) & ~0xFFF) - end)

    for dst, src, size, _, flags, _, _ in recs:
        if flags & F_MOVE:
            block = bytes(m[src:src + size])
            m[src:src + size] = bytes(size)
            m[dst:dst + size] = block

    for dst, src, size, _, flags, word, check in recs:
        if flags & F_PATCH_WORDS:
            p = dst
            while u32(m, p):
                struct.pack_into("<H", m, u32(m, p), word)
                p += 4
        elif flags & F_FILTER:
            got = unfilter_calls(m, dst, size, word & 0xFF, word >> 8)
            if check and got != check:
                sys.exit("call filter checksum %#x != %#x" % (got, check))

    if jmp_mode == 1:
        sys.exit("loader expects a jmp to OEP at the entry; not handled")
    return oep, import_rva, sections(img, recs, import_rva)


def sections(img, recs, import_rva):
    """Rebuild the section table, which the wrapper merged into one. The
    packed blob is the original sections back to back: the call filter
    covers the code, and every other section's data is moved to its RVA.
    The section holding the import directory is taken as .rdata, the
    rest as writable data; the original .rsrc is kept."""
    code = [r for r in recs if r[4] & F_FILTER]
    if len(code) != 1:
        sys.exit("expected one filtered (code) record, got %d" % len(code))
    starts = sorted([code[0][0]] + [r[0] for r in recs if r[4] & F_MOVE])
    rsrc = u32(img.file, img.opt + 0x60 + 2 * 8)
    out = []
    ndata = 0
    for i, va in enumerate(starts):
        end = starts[i + 1] if i + 1 < len(starts) else rsrc
        if va == code[0][0]:
            out.append((b".text", va, end, TEXT))
        elif va <= import_rva < end:
            out.append((b".rdata", va, end, RDATA))
        else:
            ndata += 1
            name = b".data" if ndata == 1 else b".data%d" % ndata
            out.append((name, va, end, DATA))
    out.append((b".rsrc", rsrc, img.size, RDATA))
    return out


def import_dir_size(m, rva):
    n = 0
    while any(m[rva + n * 20:rva + n * 20 + 20]):
        n += 1
    return (n + 1) * 20


def write_pe(img, oep, import_rva, secs, path):
    m = img.mem
    falign = 0x200
    hdr = bytearray(m[:0x1000])
    opt, optsize = img.opt, img.optsize
    struct.pack_into("<H", hdr, img.pe + 6, len(secs))
    struct.pack_into("<I", hdr, opt + 0x10, oep)
    struct.pack_into("<I", hdr, opt + 0x24, falign)
    # Import directory (index 1); clear IAT and bound-import entries.
    dd = opt + 0x60
    struct.pack_into("<II", hdr, dd + 1 * 8, import_rva, import_dir_size(m, import_rva))
    struct.pack_into("<II", hdr, dd + 11 * 8, 0, 0)
    struct.pack_into("<II", hdr, dd + 12 * 8, 0, 0)

    hdr_size = (opt + optsize + len(secs) * 0x28 + falign - 1) & ~(falign - 1)
    struct.pack_into("<I", hdr, opt + 0x3C, hdr_size)
    body = bytearray()
    raw = hdr_size
    for i, (name, va, end, chars) in enumerate(secs):
        data = bytes(m[va:end]).rstrip(b"\0")
        data += bytes(-len(data) % falign)
        s = opt + optsize + i * 0x28
        hdr[s:s + 0x28] = struct.pack("<8sIIIIIIHHI", name.ljust(8, b"\0"),
                                      end - va, va, len(data), raw if data else 0,
                                      0, 0, 0, 0, chars)
        body += data
        raw += len(data)
    with open(path, "wb") as f:
        f.write(hdr[:hdr_size])
        f.write(body)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("exe")
    ap.add_argument("-o", "--out", required=True)
    args = ap.parse_args()
    with open(args.exe, "rb") as f:
        img = Image(f.read())
    oep, import_rva, secs = unpack(img)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    write_pe(img, oep, import_rva, secs, args.out)
    print("OEP %#x, imports at RVA %#x -> %s" % (img.base + oep, import_rva, args.out))


if __name__ == "__main__":
    main()
