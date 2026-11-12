"""wiz8jp.dat: the Japanese data as edit scripts against the player's own game files.

The hook (jphook.c, vfile) applies them in memory when the game opens a file, so the mod ships no
game data: a script is a list of "copy N bytes from offset X of the original" and "insert these bytes"
(our translated text, Japanese glyphs, and the small numbers that change with them: lengths, offsets).

File layout (little endian):
  'W8JP' u32 version(1) u32 file_count
  per file: u16 name_len, name (lower case, backslash, relative to Data\\, as in DATA.SLF / PATCH.001)
            u32 orig_size, u32 orig_crc32, u32 new_size, u32 op_count
            ops: u8 0, u32 offset, u32 length   (copy from the original)
                 u8 1, u32 length, bytes         (insert)
"""
import struct, zlib

K = 16   # block size of the original index; shorter matches are stored as inserts


def delta(orig, new):
    idx = {}
    for j in range(0, len(orig) - K + 1, K):
        idx.setdefault(orig[j:j + K], j)
    ops, ins, i, n = [], bytearray(), 0, len(new)
    while i <= n - K:
        j = idx.get(new[i:i + K])
        if j is None:
            ins.append(new[i]); i += 1
            continue
        b = 0                                      # extend backwards into the pending insert
        while b < len(ins) and j - b - 1 >= 0 and orig[j - b - 1] == new[i - b - 1]:
            b += 1
        e = K                                      # extend forwards, 4 KB at a time, then bytewise
        while i + e + 4096 <= n and j + e + 4096 <= len(orig) and orig[j + e:j + e + 4096] == new[i + e:i + e + 4096]:
            e += 4096
        while i + e < n and j + e < len(orig) and orig[j + e] == new[i + e]:
            e += 1
        if b:
            del ins[len(ins) - b:]
        if ins:
            ops.append((1, bytes(ins))); ins = bytearray()
        if ops and ops[-1][0] == 0 and ops[-1][1] + ops[-1][2] == j - b:
            ops[-1] = (0, ops[-1][1], ops[-1][2] + e + b)
        else:
            ops.append((0, j - b, e + b))
        i += e
    ins += new[i:]
    if ins:
        ops.append((1, bytes(ins)))
    return ops


def apply(orig, ops):
    out = bytearray()
    for op in ops:
        out += orig[op[1]:op[1] + op[2]] if op[0] == 0 else op[1]
    return bytes(out)


def write(path, entries):
    """entries: list of (name, orig_bytes, new_bytes). Returns stats."""
    out = bytearray(b'W8JP' + struct.pack('<II', 1, len(entries)))
    stats = dict(files=0, copy=0, insert=0)
    for name, orig, new in entries:
        ops = delta(orig, new)
        assert apply(orig, ops) == new, name
        nb = name.lower().encode('latin1')
        out += struct.pack('<H', len(nb)) + nb
        out += struct.pack('<IIII', len(orig), zlib.crc32(orig) & 0xffffffff, len(new), len(ops))
        for op in ops:
            if op[0] == 0:
                out += struct.pack('<BII', 0, op[1], op[2]); stats['copy'] += op[2]
            else:
                out += struct.pack('<BI', 1, len(op[1])) + op[1]; stats['insert'] += len(op[1])
        stats['files'] += 1
    open(path, 'wb').write(out)
    stats['size'] = len(out)
    return stats
