"""Japanese glyphs from free bitmap fonts (BDF), same interface as gdiglyph.GdiFont:
.height and .render(ch) -> (advance, rows) with rows = height lists of 0/1, `advance` wide.

- Shinonome (shnmk12 / shnmk16, JIS X 0208, public domain): fonts/shinonome/
- Misaki Gothic (8x8, Unicode, free license): fonts/misaki/
Taken from the user's Wizardry 6/7 Japanese mods (same fonts, see their license files)."""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
FONTS = os.path.join(HERE, '..', 'fonts')

# characters the translations use whose JIS X 0208 form decodes to another code point (euc_jp codec)
ALIAS = {
    '～': '〜',   # FULLWIDTH TILDE -> WAVE DASH
    '―': '—', '—': '―',   # HORIZONTAL BAR / EM DASH (JIS 1-29)
    '−': '－',   # MINUS SIGN -> FULLWIDTH HYPHEN-MINUS
    '∥': '‖',   # PARALLEL TO -> DOUBLE VERTICAL LINE
    '￠': '¢', '￡': '£', '￢': '¬',
}


def parse_bdf(path):
    """-> ({encoding: (w, h, xoff, yoff, dwidth, rows)}, ascent, descent)"""
    glyphs, ascent, descent = {}, None, None
    with open(path, encoding='latin-1') as f:
        lines = f.read().split('\n')
    i = 0
    while i < len(lines):
        ln = lines[i]
        if ln.startswith('FONT_ASCENT'):
            ascent = int(ln.split()[1])
        elif ln.startswith('FONT_DESCENT'):
            descent = int(ln.split()[1])
        elif ln.startswith('STARTCHAR'):
            enc = bbx = dw = None
            rows = []
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
                        rows.append(int(lines[i].strip(), 16))
                        i += 1
                    break
                i += 1
            glyphs[enc] = bbx + (dw, rows)
        i += 1
    return glyphs, ascent, descent


def jis_to_unicode(code):
    try:
        return bytes([0x80 | (code >> 8), 0x80 | (code & 0xFF)]).decode('euc_jp')
    except UnicodeDecodeError:
        return None


def scale2x(rows):
    """Scale2x / EPX: doubles a 1-bit bitmap and rounds off the stair steps of diagonal strokes"""
    h, w = len(rows), len(rows[0]) if rows else 0
    px = lambda x, y: rows[y][x] if 0 <= x < w and 0 <= y < h else 0
    out = [[0] * (w * 2) for _ in range(h * 2)]
    for y in range(h):
        for x in range(w):
            p, a, b, c, d = px(x, y), px(x, y - 1), px(x + 1, y), px(x - 1, y), px(x, y + 1)
            e0 = a if (c == a and c != d and a != b) else p
            e1 = b if (a == b and a != c and b != d) else p
            e2 = c if (d == c and d != b and c != a) else p
            e3 = d if (b == d and b != a and d != c) else p
            out[2 * y][2 * x], out[2 * y][2 * x + 1] = e0, e1
            out[2 * y + 1][2 * x], out[2 * y + 1][2 * x + 1] = e2, e3
    return out


class BdfFont:
    """scale: integer pixel replication (e.g. 2 for a 24-dot font made from shnmk12)"""

    def __init__(self, kind, scale=1):
        if kind == 'misaki':
            g, self.asc, desc = parse_bdf(os.path.join(FONTS, 'misaki', 'misaki_gothic.bdf'))
            self.map = {chr(e): v for e, v in g.items() if e is not None and e >= 0}
        else:
            g, self.asc, desc = parse_bdf(os.path.join(FONTS, 'shinonome', kind + '.bdf'))
            self.map = {}
            for e, v in g.items():
                u = jis_to_unicode(e) if e is not None else None
                if u and len(u) == 1:
                    self.map[u] = v
        self.cell = self.asc + desc
        self.scale = scale
        self.height = self.cell * scale

    def _get(self, ch):
        return self.map.get(ch) or self.map.get(ALIAS.get(ch, ch))

    def has(self, ch):
        return self._get(ch) is not None

    def render(self, ch):
        g = self._get(ch)
        if g is None:
            g = self.map.get('□') or next(iter(self.map.values()))   # white square for a missing char
        w, h, xo, yo, dw, rows = g
        adv = dw or self.cell
        nb = (w + 7) // 8
        out = [[0] * adv for _ in range(self.cell)]
        top = self.asc - (yo + h)
        for r, v in enumerate(rows):
            y = top + r
            if 0 <= y < self.cell:
                for x in range(w):
                    if (v >> (nb * 8 - 1 - x)) & 1 and 0 <= xo + x < adv:
                        out[y][xo + x] = 1
        s = self.scale
        if s == 2:
            return adv * 2, scale2x(out)
        if s > 1:
            out = [[v for v in row for _ in range(s)] for row in out for _ in range(s)]
        return adv * s, out


if __name__ == '__main__':
    import sys
    f = BdfFont(sys.argv[1] if len(sys.argv) > 1 else 'shnmk12', int(sys.argv[2]) if len(sys.argv) > 2 else 1)
    print('height', f.height, len(f.map), 'chars')
    for ch in (sys.argv[3] if len(sys.argv) > 3 else '終了ア'):
        w, rows = f.render(ch)
        print(ch, w)
        for r in rows:
            print(''.join('#' if v else '.' for v in r))
