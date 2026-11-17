"""Japanese glyph packs: the glyphs mkfont.add_japanese appends to a font, kept apart from the font itself.

The hook (vfile.h, graft_font) appends a pack to whatever version of the font the game reads (Steam,
Fan Patch 1.28 FanPatch.Dat, a later update): sub-images 0..251 of the font (padded with blank glyphs),
then the pack's glyphs. For the font the pack was made from, the result is exactly add_japanese's.

pack: u32 count, count * (i16 offx, i16 offy, u16 h, u16 w, u32 length), then the ETRLE data of each glyph.
"""
import struct
from sti import STI
from mkfont import add_japanese, BASE_SYMBOLS

PAD = b'\x81\x00'      # ETRLE of a blank 1x1 glyph (add_japanese pads with these)


def make_pack(orig, chars, cache, name):
    new, info = add_japanese(orig, chars, cache, name)
    s = STI.load(new)
    subs = s.subs[BASE_SYMBOLS:]
    head = struct.pack('<I', len(subs)) + b''.join(
        struct.pack('<hhHHI', g['offx'], g['offy'], g['h'], g['w'], len(g['raw'])) for g in subs)
    return head + b''.join(g['raw'] for g in subs), new, info


def graft(orig, pack):
    """the same steps as graft_font in vfile.h"""
    d = orig
    if d[:4] != b'STCI':
        return None
    stored, flags = struct.unpack_from('<I', d, 8)[0], struct.unpack_from('<I', d, 16)[0]
    if not (flags & 0x08) or not (flags & 0x20):
        return None
    ncol, nsub = struct.unpack_from('<IH', d, 24)
    app = struct.unpack_from('<I', d, 48)[0]
    p = 64 + ncol * 3
    base = p + nsub * 16
    hdrs, blobs = b'', []
    off = 0
    keep = min(nsub, BASE_SYMBOLS)
    for i in range(keep):
        o, ln, ox, oy, h, w = struct.unpack_from('<IIhhHH', d, p + i * 16)
        hdrs += struct.pack('<IIhhHH', off, ln, ox, oy, h, w)
        blobs.append(d[base + o:base + o + ln])
        off += ln
    for i in range(keep, BASE_SYMBOLS):
        hdrs += struct.pack('<IIhhHH', off, len(PAD), 0, 0, 1, 1)
        blobs.append(PAD)
        off += len(PAD)
    n = struct.unpack_from('<I', pack, 0)[0]
    q = 4 + n * 12
    for i in range(n):
        ox, oy, h, w, ln = struct.unpack_from('<hhHHI', pack, 4 + i * 12)
        hdrs += struct.pack('<IIhhHH', off, ln, ox, oy, h, w)
        blobs.append(pack[q:q + ln])
        q += ln
        off += ln
    data = b''.join(blobs)
    hd = bytearray(d[:64])
    struct.pack_into('<I', hd, 8, len(data))
    struct.pack_into('<H', hd, 28, BASE_SYMBOLS + n)
    struct.pack_into('<I', hd, 48, app)
    appdata = d[base + stored:base + stored + app] if app else b''
    return bytes(hd) + d[64:64 + ncol * 3] + hdrs + data + appdata
