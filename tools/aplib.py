"""aPLib decompressor (the raw, headerless stream used by PECompact 2)."""


def depack(src, offset=0):
    """Decompress the aPLib stream at src[offset:]. Returns (data, end)."""
    out = bytearray()
    pos = offset
    tag = 0
    bits = 0

    def getbit():
        nonlocal tag, bits, pos
        if bits == 0:
            tag = src[pos]
            pos += 1
            bits = 8
        bits -= 1
        return (tag >> bits) & 1

    def getgamma():
        v = 1
        while True:
            v = (v << 1) | getbit()
            if not getbit():
                return v

    def copy(dist, length):
        for _ in range(length):
            out.append(out[-dist])

    out.append(src[pos])
    pos += 1
    last = 0
    lwm = 0
    while True:
        if not getbit():
            out.append(src[pos])
            pos += 1
            lwm = 0
        elif not getbit():
            dist = getgamma()
            if lwm == 0 and dist == 2:
                copy(last, getgamma())
            else:
                dist -= 3 if lwm == 0 else 2
                dist = (dist << 8) | src[pos]
                pos += 1
                length = getgamma()
                if dist >= 32000:
                    length += 1
                if dist >= 1280:
                    length += 1
                if dist < 128:
                    length += 2
                copy(dist, length)
                last = dist
            lwm = 1
        elif not getbit():
            b = src[pos]
            pos += 1
            dist = b >> 1
            if dist == 0:
                return bytes(out), pos
            copy(dist, 2 + (b & 1))
            last = dist
            lwm = 1
        else:
            dist = 0
            for _ in range(4):
                dist = (dist << 1) | getbit()
            out.append(out[-dist] if dist else 0)
            lwm = 0
