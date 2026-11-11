# -*- coding: utf-8 -*-
"""BDF loaders for Misaki (8x8, Unicode) and Shinonome (12x12, JIS X 0208 / 6x12 JIS X 0201)."""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
FONTS = os.path.join(HERE, '..', 'fonts')


def parse_bdf(path):
    """-> dict encoding -> (w, h, xoff, yoff, dwidth, rows[int]) ; plus font bbox/ascent"""
    glyphs = {}
    ascent = None
    with open(path, encoding='latin-1') as f:
        lines = f.read().split('\n')
    i = 0
    while i < len(lines):
        ln = lines[i]
        if ln.startswith('FONT_ASCENT'):
            ascent = int(ln.split()[1])
        if ln.startswith('STARTCHAR'):
            enc = None; bbx = None; dw = None; rows = []
            i += 1
            while not lines[i].startswith('ENDCHAR'):
                t = lines[i]
                if t.startswith('ENCODING'):
                    enc = int(t.split()[1])
                elif t.startswith('BBX'):
                    bbx = tuple(int(x) for x in t.split()[1:5])
                elif t.startswith('DWIDTH'):
                    dw = int(t.split()[1])
                elif t.startswith('BITMAP'):
                    i += 1
                    while not lines[i].startswith('ENDCHAR'):
                        rows.append(int(lines[i].strip(), 16)); i += 1
                    break
                i += 1
            w, h, xo, yo = bbx
            glyphs[enc] = (w, h, xo, yo, dw, rows)
        i += 1
    return glyphs, ascent


def render(g, cell_w, cell_h, ascent):
    """Render glyph into cell_h rows of cell_w-bit ints (MSB = leftmost)."""
    w, h, xo, yo, dw, rows = g
    out = [0] * cell_h
    nbytes = (w + 7) // 8
    top = ascent - (yo + h)   # row index of glyph top within cell
    for r, v in enumerate(rows):
        y = top + r
        if not (0 <= y < cell_h):
            continue
        # v is nbytes*8 bits wide, MSB-left
        for x in range(w):
            if (v >> (nbytes * 8 - 1 - x)) & 1:
                px = xo + x
                if 0 <= px < cell_w:
                    out[y] |= 1 << (cell_w - 1 - px)
    return out


def jis_to_unicode(code):
    b = bytes([0x80 | (code >> 8), 0x80 | (code & 0xFF)])
    try:
        return b.decode('euc_jp')
    except UnicodeDecodeError:
        return None


class Misaki:
    def __init__(self):
        self.g, self.asc = parse_bdf(os.path.join(FONTS, 'misaki_gothic.bdf'))

    def has(self, ch):
        return ord(ch) in self.g

    def bitmap(self, ch):
        """8 bytes."""
        rows = render(self.g[ord(ch)], 8, 8, self.asc)
        return bytes(rows)


class Shinonome:
    def __init__(self):
        g, self.asc = parse_bdf(os.path.join(FONTS, 'shnmk12.bdf'))
        self.map = {}
        for enc, gl in g.items():
            u = jis_to_unicode(enc)
            if u and len(u) == 1:
                self.map[u] = gl
        h, self.hasc = parse_bdf(os.path.join(FONTS, 'shnm6x12r.bdf'))
        self.half = {}
        for enc, gl in h.items():
            if 0x20 <= enc < 0x7F:
                self.half[chr(enc)] = gl

    def has(self, ch):
        return ch in self.map

    def bitmap(self, ch):
        """12 rows, each 16-bit big-endian (glyph left-aligned in 12 bits) -> 24 bytes."""
        rows = render(self.map[ch], 16, 12, self.asc)
        return b''.join(r.to_bytes(2, 'big') for r in rows)


if __name__ == '__main__':
    m = Misaki(); s = Shinonome()
    for ch in '日本語ヲ':
        print(ch)
        for b in m.bitmap(ch):
            print(''.join('#' if b >> (7 - x) & 1 else '.' for x in range(8)))
        bm = s.bitmap(ch)
        for y in range(12):
            v = int.from_bytes(bm[y * 2:y * 2 + 2], 'big')
            print(''.join('#' if v >> (15 - x) & 1 else '.' for x in range(16)))
    print(len(m.g), len(s.map))
