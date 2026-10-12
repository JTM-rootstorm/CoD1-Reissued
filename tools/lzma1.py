"""LZMA1 decoder after the LZMA SDK's LzmaDecode.

liblzma (and so Python's lzma module) rejects lc + lp > 4, which
PECompact's LZMA codec uses (lc=8), so this decodes the raw stream itself.
"""

_PROB_INIT = 1024
_MOVE_BITS = 5
_TOP = 1 << 24


def decode(props, src, out_size):
    """props: the LZMA properties byte; src: the range-coded stream
    (starting with its 0 byte); returns out_size bytes."""
    lc = props % 9
    props //= 9
    lp = props % 5
    pb = props // 5
    lp_mask = (1 << lp) - 1
    pb_mask = (1 << pb) - 1

    rng = 0xFFFFFFFF
    code = 0
    pos = 0
    for _ in range(5):
        code = (code << 8) | src[pos]
        pos += 1

    def bit(probs, i):
        nonlocal rng, code, pos
        p = probs[i]
        bound = (rng >> 11) * p
        if code < bound:
            rng = bound
            probs[i] = p + ((2048 - p) >> _MOVE_BITS)
            b = 0
        else:
            rng -= bound
            code -= bound
            probs[i] = p - (p >> _MOVE_BITS)
            b = 1
        if rng < _TOP:
            rng = (rng << 8) & 0xFFFFFFFF
            code = ((code << 8) | src[pos]) & 0xFFFFFFFF
            pos += 1
        return b

    def tree(probs, base, nbits):
        m = 1
        for _ in range(nbits):
            m = (m << 1) | bit(probs, base + m)
        return m - (1 << nbits)

    def reverse_tree(probs, base, nbits):
        m = 1
        sym = 0
        for i in range(nbits):
            b = bit(probs, base + m)
            m = (m << 1) | b
            sym |= b << i
        return sym

    def direct(nbits):
        nonlocal rng, code, pos
        r = 0
        for _ in range(nbits):
            rng >>= 1
            if code >= rng:
                code -= rng
                r = (r << 1) | 1
            else:
                r <<= 1
            if rng < _TOP:
                rng = (rng << 8) & 0xFFFFFFFF
                code = ((code << 8) | src[pos]) & 0xFFFFFFFF
                pos += 1
        return r

    def new(n):
        return [_PROB_INIT] * n

    is_match = new(12 << 4)
    is_rep = new(12)
    is_rep_g0 = new(12)
    is_rep_g1 = new(12)
    is_rep_g2 = new(12)
    is_rep0_long = new(12 << 4)
    pos_slot = new(4 << 6)
    spec_pos = new(115)
    align = new(16)
    literal = new(0x300 << (lc + lp))

    def len_coder():
        return [new(2), new(16 << 3), new(16 << 3), new(256)]

    len_main = len_coder()
    len_rep = len_coder()

    def decode_len(c, ps):
        choice, low, mid, high = c
        if not bit(choice, 0):
            return tree(low, ps << 3, 3)
        if not bit(choice, 1):
            return 8 + tree(mid, ps << 3, 3)
        return 16 + tree(high, 0, 8)

    out = bytearray()
    state = 0
    rep0 = rep1 = rep2 = rep3 = 0
    while len(out) < out_size:
        n = len(out)
        ps = n & pb_mask
        if not bit(is_match, (state << 4) + ps):
            prev = out[-1] if out else 0
            base = 0x300 * (((n & lp_mask) << lc) + (prev >> (8 - lc)))
            sym = 1
            if state >= 7:
                match = out[n - rep0 - 1]
                while sym < 0x100:
                    mb = (match >> 7) & 1
                    match <<= 1
                    b = bit(literal, base + ((1 + mb) << 8) + sym)
                    sym = (sym << 1) | b
                    if mb != b:
                        break
            while sym < 0x100:
                sym = (sym << 1) | bit(literal, base + sym)
            out.append(sym & 0xFF)
            state = 0 if state < 4 else state - 3 if state < 10 else state - 6
            continue

        if bit(is_rep, state):
            if not bit(is_rep_g0, state):
                if not bit(is_rep0_long, (state << 4) + ps):
                    state = 9 if state < 7 else 11
                    out.append(out[n - rep0 - 1])
                    continue
            else:
                if not bit(is_rep_g1, state):
                    dist = rep1
                else:
                    if not bit(is_rep_g2, state):
                        dist = rep2
                    else:
                        dist = rep3
                        rep3 = rep2
                    rep2 = rep1
                rep1 = rep0
                rep0 = dist
            length = decode_len(len_rep, ps)
            state = 8 if state < 7 else 11
        else:
            rep3, rep2, rep1 = rep2, rep1, rep0
            length = decode_len(len_main, ps)
            state = 7 if state < 7 else 10
            slot = tree(pos_slot, min(length, 3) << 6, 6)
            if slot < 4:
                rep0 = slot
            else:
                nd = (slot >> 1) - 1
                rep0 = (2 | (slot & 1)) << nd
                if slot < 14:
                    rep0 += reverse_tree(spec_pos, rep0 - slot - 1, nd)
                else:
                    rep0 += direct(nd - 4) << 4
                    rep0 += reverse_tree(align, 0, 4)
                    if rep0 == 0xFFFFFFFF:
                        break
        length += 2
        start = n - rep0 - 1
        if start < 0:
            raise ValueError("LZMA distance past start of output")
        for i in range(min(length, out_size - n)):
            out.append(out[start + i])
    return bytes(out)
