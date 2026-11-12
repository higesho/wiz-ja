"""STCI (.STI) 8-bit indexed image reader/writer (JA2 / Wizardry 8 engine).

Header (64 bytes):
  0  'STCI'
  4  u32 original size (w*h of full image for fonts: usually 640*480)
  8  u32 stored size (compressed data length)
 12  u32 transparent value
 16  u32 flags (0x08 indexed, 0x20 ETRLE compressed)
 20  u16 height, u16 width
 24  u32 number of colours, u16 number of sub images, u8 r/g/b depth, 11 unused
 44  u8 depth (8)
 45  3 unused
 48  u32 app data size
 52  12 unused
Then palette (ncol*3), then sub-image headers (16 bytes each):
  u32 data offset, u32 data length, i16 offx, i16 offy, u16 height, u16 width
Then compressed pixel data, then app data.

ETRLE per row: 0x00 = end of row; 0x80|n = n transparent pixels; n (1..127) followed by n literal pixels.
"""
import struct, sys


class STI:
    def __init__(self):
        self.header = b''
        self.palette = b''
        self.subs = []      # list of dict(offx, offy, w, h, pixels: bytes (w*h, 0 = transparent))
        self.appdata = b''
        self.flags = 0x28
        self.transp = 0
        self.orig = 0
        self.w = self.h = 0

    @classmethod
    def load(cls, path_or_bytes):
        d = path_or_bytes if isinstance(path_or_bytes, (bytes, bytearray)) else open(path_or_bytes, 'rb').read()
        assert d[:4] == b'STCI', d[:4]
        s = cls()
        s.header = d[:64]
        s.orig, stored, s.transp, s.flags, s.h, s.w = struct.unpack_from('<IIIIHH', d, 4)
        assert s.flags & 0x08, 'only indexed STI supported'
        ncol, nsub = struct.unpack_from('<IH', d, 24)
        app = struct.unpack_from('<I', d, 48)[0]
        p = 64
        s.palette = d[p:p + ncol * 3]
        p += ncol * 3
        hdrs = [struct.unpack_from('<IIhhHH', d, p + i * 16) for i in range(nsub)]
        p += nsub * 16
        base = p
        for off, ln, ox, oy, h, w in hdrs:
            raw = d[base + off: base + off + ln]
            px = etrle_decode(raw, w, h) if s.flags & 0x20 else raw
            # keep the original compressed bytes: save() reuses them, so an unchanged glyph is stored
            # byte-identical (the patch data then only needs "copy" for it)
            s.subs.append(dict(offx=ox, offy=oy, w=w, h=h, pixels=px, raw=raw))
        s.appdata = d[base + stored: base + stored + app] if app else b''
        return s

    def save(self):
        blobs = []
        hdrs = b''
        off = 0
        for g in self.subs:
            b = g.get('raw') if (g.get('raw') is not None and self.flags & 0x20) else None
            if b is None:
                b = etrle_encode(g['pixels'], g['w'], g['h']) if self.flags & 0x20 else g['pixels']
            hdrs += struct.pack('<IIhhHH', off, len(b), g['offx'], g['offy'], g['h'], g['w'])
            blobs.append(b)
            off += len(b)
        data = b''.join(blobs)
        hd = bytearray(self.header)
        struct.pack_into('<I', hd, 8, len(data))
        struct.pack_into('<H', hd, 28, len(self.subs))
        struct.pack_into('<I', hd, 48, len(self.appdata))
        return bytes(hd) + self.palette + hdrs + data + self.appdata


def etrle_decode(raw, w, h):
    out = bytearray()
    i = 0
    rows = 0
    row = bytearray()
    while i < len(raw) and rows < h:
        c = raw[i]; i += 1
        if c == 0:
            row += b'\0' * (w - len(row))
            out += row[:w]; row = bytearray(); rows += 1
        elif c & 0x80:
            row += b'\0' * (c & 0x7f)
        else:
            row += raw[i:i + c]; i += c
    while rows < h:
        out += b'\0' * w; rows += 1
    return bytes(out)


def etrle_encode(px, w, h):
    out = bytearray()
    for y in range(h):
        row = px[y * w:(y + 1) * w]
        x = 0
        while x < w:
            if row[x] == 0:
                n = 0
                while x < w and row[x] == 0 and n < 127:
                    x += 1; n += 1
                out.append(0x80 | n)
            else:
                st = x
                while x < w and row[x] != 0 and x - st < 127:
                    x += 1
                out.append(x - st); out += row[st:x]
        out.append(0)
    return bytes(out)


def ascii_art(g):
    chars = ' .:-=+*#%@ABCDEFGHIJKLMNOPQRSTUVWXYZ'
    lines = []
    for y in range(g['h']):
        lines.append(''.join(('%x' % v if v < 16 else '#') if v else '.' for v in g['pixels'][y * g['w']:(y + 1) * g['w']]))
    return '\n'.join(lines)


if __name__ == '__main__':
    s = STI.load(sys.argv[1])
    print('size', s.w, s.h, 'flags', hex(s.flags), 'glyphs', len(s.subs), 'appdata', len(s.appdata))
    idx = [int(x) for x in sys.argv[2:]] or [0]
    for i in idx:
        g = s.subs[i]
        print('#%d off=(%d,%d) %dx%d' % (i, g['offx'], g['offy'], g['w'], g['h']))
        print(ascii_art(g))
