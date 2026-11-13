"""Append Japanese glyphs to Wizardry 8 STI fonts.

Glyph layout: sub-images 0..251 = original glyphs (padded with blank glyphs up to 252),
then one sub-image per character in the Japanese char list (same order as wiz8jp.chr).
"""
from collections import Counter
from sti import STI
from bdfglyph import BdfFont

BASE_SYMBOLS = 252
SPACE_INDEX = 92  # position of U+0020 in the game's own translation table


def font_metrics(s):
    """line height, body colour and (optional) drop-shadow colour of an original font.

    Shadow detection: for every pixel, look at its upper-left neighbour. A colour whose
    pixels mostly sit just below/right of a *different* colour is a shadow; the colour
    they sit under is the body.
    """
    glyphs = s.subs[:92]
    hs = [g['h'] + max(0, g['offy']) for g in glyphs]
    H = Counter(hs).most_common(1)[0][0]
    count = Counter()
    inner = Counter()   # colours of pixels fully surrounded by ink (the "fill")
    under = Counter()   # (shadow_colour, body_colour) pairs
    for g in glyphs:
        w, h, p = g['w'], g['h'], g['pixels']
        for y in range(h):
            for x in range(w):
                v = p[y * w + x]
                if not v:
                    continue
                count[v] += 1
                if 0 < x < w - 1 and 0 < y < h - 1 and all(
                        p[(y + dy) * w + x + dx] for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                    inner[v] += 1
                if x > 0 and y > 0:
                    ul = p[(y - 1) * w + x - 1]
                    left = p[y * w + x - 1]
                    upv = p[(y - 1) * w + x]
                    if ul and ul != v and (left == 0 or upv == 0 or left != v):
                        under[(v, ul)] += 1
    total = sum(count.values()) or 1
    if len(count) > 6:
        # decorative multi-colour font (dark outline + bright gradient fill): use palette luminance
        pal = s.palette
        lum = lambda c: 0.299 * pal[c * 3] + 0.587 * pal[c * 3 + 1] + 0.114 * pal[c * 3 + 2]
        common = [c for c, n in count.most_common() if n > 0.02 * total]
        bright = max(common, key=lum)
        dark = min(common, key=lum)
        return H, bright, ('outline', dark)
    fill = inner if sum(inner.values()) > 0.05 * total else count
    body, shadow = (fill.most_common(1)[0][0] if fill else 1), None
    if under:
        (sv, bv), n = under.most_common(1)[0]
        # require that a good part of that colour behaves like a shadow
        if n > 0.35 * count[sv] and count[sv] > 0.1 * total and count[bv] > 0.1 * total:
            body, shadow = bv, sv
    return H, body, shadow


# the journal draws each entry on one clipped line (0x5bdd00, no word wrap): small glyphs so it fits
FONT_OVERRIDE = {'JOURNAL_FONT.STI': ('shnmk12', 1)}

# body colour as a top-to-bottom gradient of palette indices, for fonts whose Latin glyphs are shaded:
# OPT_TITLE_FONT is an ivory engraved texture (font_metrics picks its dark rim), the calligraphy fonts
# shade 4 (top) .. 1 (bottom) (FULLSHADOW's detected body 5 is black)
INK_OVERRIDE = {'OPT_TITLE_FONT.STI': (16, 24, 28, 37, 54, 61),
                'CALLIGRAPHYFONT.STI': (4, 3, 2, 1),
                'CALLIGRAPHYFONTSHADOWED.STI': (4, 3, 2, 1),
                'CALLIGRAPHYFONTFULLSHADOW.STI': (4, 3, 2, 1)}


def jp_font_for(H, name=''):
    """free bitmap fonts only (redistributable): Misaki 8, Shinonome 12 / 16, Shinonome 12 doubled = 24"""
    for k, v in FONT_OVERRIDE.items():
        if name.upper().endswith(k):
            return v
    if H <= 9:
        return ('misaki', 1)
    if H <= 14:
        return ('shnmk12', 1)
    if H <= 20:
        return ('shnmk16', 1)
    return ('shnmk12', 2)


def add_japanese(sti_bytes, chars, cache=None, name=''):
    s = STI.load(sti_bytes)
    H, ink, shadow = font_metrics(s)
    px = jp_font_for(H, name)
    grad = next((v for k, v in INK_OVERRIDE.items() if name.upper().endswith(k)), None)
    cache = {} if cache is None else cache
    if px not in cache:
        cache[px] = BdfFont(*px)
    gf = cache[px]
    # the space glyph (table index 92) keeps its width. The Japanese text breaks at U+E000, which gets a
    # zero-advance glyph below; the hook (sep_patch.h) makes the word-wrap routines accept it as a separator
    space_w = 4
    if len(s.subs) > SPACE_INDEX:
        sp = s.subs[SPACE_INDEX]
        space_w = max(1, sp['w'] + sp['offx'])
    # pad to the base table size
    while len(s.subs) < BASE_SYMBOLS:
        s.subs.append(dict(offx=0, offy=0, w=1, h=1, pixels=b'\0'))
    del s.subs[BASE_SYMBOLS:]
    offy = H - gf.height  # align bottom of the JP cell with the bottom of the Latin line
    rc = cache.setdefault(('glyphs', px), {})
    for ch in chars:
        if ch in ('\ue000', '\ue001', '\u200b'):   # break (jptext.BREAK), keyword-span mark (jptext.KW_MARK); U+200B = old break kept in saved logs
            s.subs.append(dict(offx=-1, offy=0, w=1, h=1, pixels=b'\0'))   # zero advance
            continue
        if ch in ('\u2002', '\ue002', '\u3000'):   # U+E002 = jptext.KW_SPACE
            bw = space_w if ch != '\u3000' else gf.render('\u3042')[0]
            s.subs.append(dict(offx=0, offy=0, w=bw, h=1, pixels=b'\0' * bw))
            continue
        if ch not in rc:
            rc[ch] = gf.render(ch)
        w, rows = rc[ch]
        h = len(rows)
        if grad:
            inks = [grad[min(len(grad) - 1, y * len(grad) // h)] for y in range(h)]
        else:
            inks = [ink] * h
        if isinstance(shadow, tuple):
            # 1px outline all around
            dark = shadow[1]
            W2, H2 = w + 2, h + 2
            buf = bytearray(W2 * H2)
            for y in range(h):
                for x in range(w):
                    if rows[y][x]:
                        for dy in (0, 1, 2):
                            for dx in (0, 1, 2):
                                buf[(y + dy) * W2 + x + dx] = dark
            for y in range(h):
                for x in range(w):
                    if rows[y][x]:
                        buf[(y + 1) * W2 + x + 1] = inks[y]
            s.subs.append(dict(offx=0, offy=offy - 1, w=W2, h=H2, pixels=bytes(buf)))
        elif shadow is not None:
            # grow the cell by one pixel and put the shadow at +1,+1 under the body
            W2, H2 = w + 1, h + 1
            buf = bytearray(W2 * H2)
            for y in range(h):
                for x in range(w):
                    if rows[y][x]:
                        buf[(y + 1) * W2 + x + 1] = shadow
            for y in range(h):
                for x in range(w):
                    if rows[y][x]:
                        buf[y * W2 + x] = inks[y]
            s.subs.append(dict(offx=0, offy=offy, w=W2, h=H2, pixels=bytes(buf)))
        else:
            px_bytes = bytes(inks[y] if v else 0 for y, r in enumerate(rows) for v in r)
            s.subs.append(dict(offx=0, offy=offy, w=w, h=h, pixels=px_bytes))
    return s.save(), dict(H=H, ink=grad or ink, shadow=shadow, px=px)
