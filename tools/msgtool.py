# -*- coding: utf-8 -*-
"""Wizardry 6 MSG.DBS / MSG.HDR / MISC.HDR (Huffman tree) codec.

MSG.HDR : u16 count, then count * (u16 first_id, u16 offset_in_block, u8 last_index, u8 block)
          (lookup = binary search, id in [first_id, first_id+last_index]); capacity 850 entries.
MSG.DBS : 1024-byte blocks. Record = u8 (nbytes+1), u8 nchars, nbytes of Huffman bitstream (MSB first).
MISC.HDR: Huffman tree, 256 nodes * (i16 left, i16 right); <0 -> -node, >=0 -> leaf byte.
"""
import struct, heapq, itertools

HDR_CAP = 850
BLOCK = 1024


def load_tree(misc):
    return struct.unpack("<512h", misc[:1024])


def decode_bits(tree, data, n):
    out = bytearray(); bit = 0
    for _ in range(n):
        node = 0
        while True:
            b = (data[bit >> 3] >> (7 - (bit & 7))) & 1
            bit += 1
            v = tree[node * 2 + b]
            if v < 0:
                node = -v
            else:
                out.append(v & 0xFF)
                break
    return bytes(out)


def read_all(hdr, dbs, misc):
    """Return list of (msg_id, bytes) in file order."""
    tree = load_tree(misc)
    n = struct.unpack("<H", hdr[:2])[0]
    res = []
    for i in range(n):
        fid, off, last, blk = struct.unpack("<HHBB", hdr[2 + i * 6: 8 + i * 6])
        p = blk * BLOCK + off
        for k in range(last + 1):
            ln = dbs[p]; nch = dbs[p + 1]
            res.append((fid + k, decode_bits(tree, dbs[p + 2: p + 1 + ln], nch)))
            p += 1 + ln
    return res


def build_tree(freq):
    """freq: dict byte->count. Returns (tree tuple of 512 i16, codes dict byte->bitstring)."""
    syms = [s for s in freq if freq[s] > 0]
    if len(syms) < 2:
        syms = syms + [0x20, 0x21][: 2 - len(syms)]
    cnt = itertools.count()
    heap = [(freq.get(s, 1), next(cnt), ('L', s)) for s in syms]
    heapq.heapify(heap)
    while len(heap) > 1:
        a = heapq.heappop(heap); b = heapq.heappop(heap)
        heapq.heappush(heap, (a[0] + b[0], next(cnt), ('N', a[2], b[2])))
    root = heap[0][2]
    nodes = []  # list of [left,right]
    codes = {}

    def assign(t, prefix):
        # returns reference value for parent
        if t[0] == 'L':
            codes[t[1]] = prefix or '0'
            return t[1]
        idx = len(nodes); nodes.append([0, 0])
        nodes[idx][0] = assign(t[1], prefix + '0')
        nodes[idx][1] = assign(t[2], prefix + '1')
        return -idx
    assign(root, '')
    assert len(nodes) <= 256, len(nodes)
    flat = []
    for l, r in nodes:
        flat += [l, r]
    flat += [0] * (512 - len(flat))
    return tuple(flat), codes


def encode_bits(codes, data):
    bits = ''.join(codes[b] for b in data)
    bits += '0' * (-len(bits) % 8)
    return bytes(int(bits[i:i + 8], 2) for i in range(0, len(bits), 8))


def write_all(lines, tree_codes=None):
    """lines: list of (msg_id, bytes) in order, ids ascending.
    Returns (hdr, dbs, misc)."""
    if tree_codes is None:
        freq = {}
        for _, s in lines:
            for b in s:
                freq[b] = freq.get(b, 0) + 1
        tree, codes = build_tree(freq)
    else:
        tree, codes = tree_codes
    dbs = bytearray()
    entries = []  # [first_id, off, last, blk]
    prev_id = None
    for mid, s in lines:
        assert len(s) <= 255, (mid, len(s))
        enc = encode_bits(codes, s)
        rec = bytes([len(enc) + 1, len(s)]) + enc
        assert len(enc) + 1 <= 255, (mid, 'record too long')
        pos = len(dbs)
        blk, off = divmod(pos, BLOCK)
        cur = entries[-1] if entries else None
        contiguous = cur is not None and prev_id is not None and mid == prev_id + 1 \
            and cur[3] == blk and cur[2] < 255
        if contiguous:
            cur[2] += 1
        else:
            entries.append([mid, off, 0, blk])
        dbs += rec
        prev_id = mid
    dbs += b'\0' * (-len(dbs) % BLOCK)
    assert len(entries) <= HDR_CAP, ('MSG.HDR overflow', len(entries))
    assert len(dbs) // BLOCK <= 255
    hdr = struct.pack("<H", len(entries))
    for e in entries:
        hdr += struct.pack("<HHBB", *e)
    hdr += b'\0' * (2 + HDR_CAP * 6 - len(hdr))
    misc = struct.pack("<512h", *tree)
    return hdr, bytes(dbs), misc


BS = chr(92)  # backslash


def esc(b):
    """bytes -> editable text (backslash-xNN for non-printables and backslash)."""
    return ''.join(chr(c) if 32 <= c < 127 and c != 92 else BS + 'x%02x' % c for c in b)


def unesc(s):
    """editable text -> list of tokens: ints (raw bytes) and str (non-ASCII chars)."""
    out = []; i = 0
    while i < len(s):
        if s[i] == BS and s[i + 1:i + 2] == 'x':
            out.append(int(s[i + 2:i + 4], 16)); i += 4
        elif ord(s[i]) < 0x80:
            out.append(ord(s[i])); i += 1
        else:
            out.append(s[i]); i += 1
    return out


def load_en(root):
    """英語の原文 {id: 編集用テキスト}。trans/msg_en.txt があればそれを、なければ original/ のゲームデータから取り出す。"""
    import os
    path = os.path.join(root, 'trans', 'msg_en.txt')
    en = {}
    if os.path.exists(path):
        for ln in open(path, encoding='utf-8'):
            i, t = ln.rstrip(chr(13) + chr(10)).split(chr(9), 1)
            en[int(i)] = t
        return en
    rd = lambda n: open(os.path.join(root, 'original', n), 'rb').read()
    for mid, b in read_all(rd('MSG.HDR'), rd('MSG.DBS'), rd('MISC.HDR')):
        en[mid] = esc(b)
    return en


if __name__ == '__main__':
    import sys, os
    d = sys.argv[1]
    hdr = open(os.path.join(d, 'MSG.HDR'), 'rb').read()
    dbs = open(os.path.join(d, 'MSG.DBS'), 'rb').read()
    misc = open(os.path.join(d, 'MISC.HDR'), 'rb').read()
    L = read_all(hdr, dbs, misc)
    print(len(L), 'lines; ids', L[0][0], '..', L[-1][0])
    h2, d2, m2 = write_all(L)
    L2 = read_all(h2, d2, m2)
    print('roundtrip ok:', L == L2, 'dbs', len(dbs), '->', len(d2), 'entries', struct.unpack('<H', h2[:2])[0])
