#!/usr/bin/env python3
"""Unpack X-Ray (CS/CoP/CoC) .db archives. Pure Python, no dependencies.

Format, as read by src/xrCore/LocatorAPI.cpp: a sequence of chunks {u32 id, u32 size, data}.
  id 666 : INI header ([header] entry_point=..., auto_load=...)
  id 1   : file table; when bit 31 of the id is set the table is LZHuf-compressed
           (decoder ported from src/xrCore/LzHuf.cpp).
           entry: u16 hdr_size, u32 size_real, u32 size_compr, u32 crc32,
                  name[hdr_size - 16] (cp1251), u32 offset
  bodies : stored raw when size_real == size_compr, otherwise LZO1X
           (src/xrCore/Compression/rt_compressor.cpp; decoder ported from minilzo's
           lzo1x_decompress, GPL-2.0-or-later, Markus F.X.J. Oberhumer).
Each extracted file is CRC32-checked. SoC-era encrypted archives (trivial_encryptor)
are not supported. CoC 1.4.x archives need no decryption.

Archives are untrusted data: extract into a new, empty directory outside the repo,
and pass paths as arguments (run with `python -I`).

Usage: python -I tools/xrdb_unpack.py <archive.db> <outdir> [path-prefix ...]
  e.g. python -I tools/xrdb_unpack.py <coc>/database/config/scripts.db build/coc_extract scripts/
"""
import struct, sys, os, zlib

# ---------------- LZHuf (port of LzHuf.cpp Decode) ----------------
N = 4096; F = 60; TH = 2; N_CHAR = 256 - TH + F; T = N_CHAR * 2 - 1; R = T - 1; MAX_FREQ = 0x4000
d_code = [0] * 32 + [1] * 16 + [2] * 16 + [3] * 16 + [4] * 8 + [5] * 8 + [6] * 8 + [7] * 8 + [8] * 8 + [9] * 8 + [10] * 8 + [11] * 8
for v in range(12, 24):
    d_code += [v] * 4
for v in range(24, 48):
    d_code += [v] * 2
d_code += list(range(48, 64))
d_len = [3] * 32 + [4] * 48 + [5] * 64 + [6] * 48 + [7] * 48 + [8] * 16
assert len(d_code) == 256 and len(d_len) == 256


def lzhuf_decode(src):
    pos = [0]
    gb = [0, 0]  # getbuf, getlen

    def getb():
        if pos[0] >= len(src):
            return 0
        c = src[pos[0]]
        pos[0] += 1
        return c

    def fill():
        while gb[1] <= 8:
            gb[0] |= getb() << (8 - gb[1])
            gb[1] += 8

    def getbit():
        fill()
        i = gb[0]
        gb[0] = (gb[0] << 1) & 0xFFFFFFFF
        gb[1] -= 1
        return (i & 0x8000) >> 15

    def getbyte():
        fill()
        i = gb[0]
        gb[0] = (gb[0] << 8) & 0xFFFFFFFF
        gb[1] -= 8
        return (i & 0xff00) >> 8

    size = src[0] | src[1] << 8 | src[2] << 16 | src[3] << 24
    pos[0] = 4
    freq = [0] * (T + 1); prnt = [0] * (T + N_CHAR + 1); son = [0] * T
    for i in range(N_CHAR):
        freq[i] = 1; son[i] = i + T; prnt[i + T] = i
    i = 0; j = N_CHAR
    while j <= R:
        freq[j] = freq[i] + freq[i + 1]; son[j] = i; prnt[i] = prnt[i + 1] = j
        i += 2; j += 1
    freq[T] = 0xffff; prnt[R] = 0

    def reconst():
        j = 0
        for i in range(T):
            if son[i] >= T:
                freq[j] = (freq[i] + 1) // 2; son[j] = son[i]; j += 1
        i = 0; j = N_CHAR
        while j < T:
            f = freq[j] = freq[i] + freq[i + 1]
            k = j - 1
            while f < freq[k]:
                k -= 1
            k += 1
            freq[k + 1:j + 1] = freq[k:j]; freq[k] = f
            son[k + 1:j + 1] = son[k:j]; son[k] = i
            i += 2; j += 1
        for i in range(T):
            k = son[i]
            if k >= T:
                prnt[k] = i
            else:
                prnt[k] = prnt[k + 1] = i

    def update(c):
        if freq[R] == MAX_FREQ:
            reconst()
        c = prnt[c + T]
        while True:
            freq[c] += 1; k = freq[c]
            l = c + 1
            if k > freq[l]:
                l += 1
                while k > freq[l]:
                    l += 1
                l -= 1
                freq[c] = freq[l]; freq[l] = k
                i = son[c]; prnt[i] = l
                if i < T:
                    prnt[i + 1] = l
                j = son[l]; son[l] = i
                prnt[j] = c
                if j < T:
                    prnt[j + 1] = c
                son[c] = j; c = l
            c = prnt[c]
            if c == 0:
                break

    tb = bytearray(b' ' * (N + F)); r = N - F; out = bytearray()
    while len(out) < size:
        c = son[R]
        while c < T:
            c = son[c + getbit()]
        c -= T
        update(c)
        if c < 256:
            out.append(c); tb[r] = c; r = (r + 1) & (N - 1)
        else:
            i = getbyte(); cc = d_code[i] << 6; j = d_len[i] - 2
            while j:
                i = (i << 1) + getbit(); j -= 1
            p = cc | (i & 0x3f)
            i = (r - p - 1) & (N - 1)
            for k in range(c - 255 + TH):
                b = tb[(i + k) & (N - 1)]; out.append(b); tb[r] = b; r = (r + 1) & (N - 1)
    return bytes(out[:size])


# ---------------- LZO1X decompress (port of minilzo lzo1x_d) ----------------
def lzo1x_decompress(src, out_len):
    out = bytearray(); ip = 0; t = 0

    def cp(mpos, n):
        for _ in range(n):
            out.append(out[mpos]); mpos += 1

    state = 'loop'
    if src[0] > 17:
        t = src[0] - 17; ip = 1
        if t < 4:
            state = 'match_next'
        else:
            out.extend(src[ip:ip + t]); ip += t; state = 'first_lit'
    while True:
        if state == 'loop':
            t = src[ip]; ip += 1
            if t >= 16:
                state = 'match'; continue
            if t == 0:
                while src[ip] == 0:
                    t += 255; ip += 1
                t += 15 + src[ip]; ip += 1
            out.extend(src[ip:ip + t + 3]); ip += t + 3
            state = 'first_lit'; continue
        if state == 'first_lit':
            t = src[ip]; ip += 1
            if t >= 16:
                state = 'match'; continue
            m = len(out) - (1 + 0x0800) - (t >> 2) - (src[ip] << 2); ip += 1
            cp(m, 3); state = 'match_done'; continue
        if state == 'match':
            if t >= 64:
                m = len(out) - 1 - ((t >> 2) & 7) - (src[ip] << 3); ip += 1; t = (t >> 5) - 1
            elif t >= 32:
                t &= 31
                if t == 0:
                    while src[ip] == 0:
                        t += 255; ip += 1
                    t += 31 + src[ip]; ip += 1
                m = len(out) - 1 - ((src[ip] | src[ip + 1] << 8) >> 2); ip += 2
            elif t >= 16:
                m = len(out) - ((t & 8) << 11); t &= 7
                if t == 0:
                    while src[ip] == 0:
                        t += 255; ip += 1
                    t += 7 + src[ip]; ip += 1
                m -= (src[ip] | src[ip + 1] << 8) >> 2; ip += 2
                if m == len(out):
                    break
                m -= 0x4000
            else:
                m = len(out) - 1 - (t >> 2) - (src[ip] << 2); ip += 1
                cp(m, 2); state = 'match_done'; continue
            cp(m, t + 2); state = 'match_done'; continue
        if state == 'match_done':
            t = src[ip - 2] & 3
            state = 'loop' if t == 0 else 'match_next'
            continue
        if state == 'match_next':
            out.extend(src[ip:ip + t]); ip += t
            t = src[ip]; ip += 1; state = 'match'; continue
    if len(out) != out_len:
        raise ValueError('lzo size mismatch %d!=%d' % (len(out), out_len))
    return bytes(out)


def read_table(d):
    o = 0; hdr = None; ini = b''
    while o + 8 <= len(d):
        cid, sz = struct.unpack_from('<II', d, o)
        body = d[o + 8:o + 8 + sz]
        if cid & 0x7fffffff == 1:
            hdr = lzhuf_decode(body) if cid & 0x80000000 else body
        elif cid & 0x7fffffff == 666:
            ini = body
        o += 8 + sz
    ents = []; p = 0
    while p < len(hdr):
        hs, real, comp, crc = struct.unpack_from('<HIII', hdr, p); p += 14
        nl = hs - 16; name = hdr[p:p + nl].decode('cp1251'); p += nl
        ptr, = struct.unpack_from('<I', hdr, p); p += 4
        ents.append((name, real, comp, crc, ptr))
    return ini, ents


def main():
    arc, outdir = sys.argv[1], sys.argv[2]
    filt = [f.lower() for f in sys.argv[3:]]
    d = open(arc, 'rb').read()
    ini, ents = read_table(d)
    print(ini.decode('cp1251', 'replace'))
    n = 0; bad = 0
    for name, real, comp, crc, ptr in ents:
        rel = name.replace('\\', '/')
        if rel.endswith('/') or (real == 0 and comp == 0 and '.' not in rel.split('/')[-1]):
            continue
        if filt and not any(rel.lower().startswith(f) for f in filt):
            continue
        if real == 0:
            data = b''
        else:
            raw = d[ptr:ptr + comp]
            data = raw if real == comp else lzo1x_decompress(raw, real)
        if real and (zlib.crc32(data) & 0xffffffff) != crc:
            bad += 1
        if '..' in rel.split('/') or rel.startswith('/') or ':' in rel:
            raise ValueError('unsafe path ' + rel)
        dst = os.path.join(outdir, rel)
        os.makedirs(os.path.dirname(dst) or outdir, exist_ok=True)
        open(dst, 'wb').write(data); n += 1
    print('extracted', n, 'files; crc mismatches:', bad)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
