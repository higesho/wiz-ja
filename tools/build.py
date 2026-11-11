# -*- coding: utf-8 -*-
"""Build the Japanese patch:  python tools/build.py [--install]

Inputs : original/*  (pristine game files), trans/msg_ja.txt, src/jpext.asm, fonts/*.bdf
Outputs: out/WROOT.EXE out/EGA.DRV out/MSG.DBS out/MSG.HDR out/MISC.HDR  (+ copied to ../BANE with --install)
"""
import os, re, sys, struct, shutil

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))
sys.path.insert(0, HERE)
import msgtool, fontgen

ORIG = os.path.join(ROOT, 'original')
OUT = os.path.join(ROOT, 'out')
GAME = os.path.normpath(os.path.join(ROOT, '..', 'BANE'))

MOD_BASE = 0x2400          # runtime offset of module in driver segment
MOD_FILE = MOD_BASE - 0x100
CODE_BASE = 0x2480
SMALL_LIMIT = 0x2000
LARGE_LIMIT = 0x2000

VARS = dict(
    WCS=0x2410, GDS=0x2412, PEND=0x2414, INV=0x2415, COLOR=0x2416, WIDX=0x2417,
    CELLW=0x2418, LRUN=0x241a, LCY=0x241b, LWIN=0x241c, LNCX=0x241e, CONT=0x241f,
    LPX=0x2420, NEXTF=0x2422, TCOL=0x2424, TROW=0x2425, GPTR=0x2426, SH=0x2428,
    KK=0x2429, JJ=0x242a, BCOL=0x242b, BROW=0x242c, GCTRIED=0x242d, PX=0x242e,
    ROW40=0x2430, COLX=0x2432, CAND=0x2434,
    VENTRY0=0x2440, VMHIDE=0x2442, VMSHOW=0x2444, VOFF=0x2446, NSMALL=0x2448,
    NLARGE=0x244a, SFONT=0x244c, LFONT=0x244e, VTILES=0x2450, VHASH=0x2452, VUSED=0x2454,
    BGMODE=0x2436, PBIT=0x2437,
    VENTRY1=0x2456, TILEBUF=0x2458, FGTAB=0x2460, BGTAB=0x2470,
)
NTILES = 1024
HASHN = 2048

# ----------------------------------------------------------------------------- asm

INV_CC = {'ae': 'jb', 'b': 'jae', 'e': 'jne', 'ne': 'je', 'a': 'jbe', 'be': 'ja',
          'l': 'jge', 'ge': 'jl', 'g': 'jle', 'le': 'jg'}


def preprocess(src, subst):
    out = []; n = 0
    for line in src.split('\n'):
        line = line.split(';', 1)[0].rstrip()
        if not line.strip():
            continue
        for k, v in subst.items():
            line = line.replace('{%s}' % k, str(v))
        t = line.split()
        op = t[0] if t else ''
        if op in ('CALL', 'JMP'):
            n += 1
            out += [' .byte 0x%s' % ('e8' if op == 'CALL' else 'e9'),
                    ' .word %s - _m%d' % (t[1], n), '_m%d:' % n]
        elif op == 'JCCF':
            n += 1
            out += [' %s _m%d' % (INV_CC[t[1]], n), ' .byte 0xe9', ' .word %s - _n%d' % (t[2], n),
                    '_n%d:' % n, '_m%d:' % n]
        elif op == 'RET':
            out.append(' .byte 0xc3')
        elif op == 'RETF':
            out.append(' .byte 0xcb')
        else:
            out.append(line)
    return '\n'.join(out)


def assemble(src, base):
    from keystone import Ks, KS_ARCH_X86, KS_MODE_16
    import capstone
    ks = Ks(KS_ARCH_X86, KS_MODE_16)
    enc, _ = ks.asm(src, base)
    code = bytes(enc)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
    pos = base
    for ins in md.disasm(code, base):
        if ins.bytes[0] == 0x66 or ins.bytes[0] == 0x67:
            raise SystemExit('bad 32-bit prefix at %x: %s %s' % (ins.address, ins.mnemonic, ins.op_str))
        pos = ins.address + ins.size
    return code


# ----------------------------------------------------------------------------- text

def load_translations():
    import glob
    paths = sorted(glob.glob(os.path.join(ROOT, 'trans', 'parts', 'ja_*.txt'))) +         [os.path.join(ROOT, 'trans', 'msg_ja.txt')]          # msg_ja.txt wins (manual fixes)
    tr = {}
    for ln in (l for p in paths for l in open(p, encoding='utf-8')):
        ln = ln.rstrip('\n')
        if not ln or ln.startswith('#'):
            continue
        parts = ln.split('\t')
        if len(parts) < 3:
            continue
        mid, font, text = int(parts[0]), parts[1].strip(), '\t'.join(parts[2:])
        tr[mid] = (font, text)
    return tr


def icon_font_colors():
    """per icon font (1..4): (fg, bg) = 2nd / 1st most common colour in glyph 'M'."""
    import collections
    res = {}
    for n in range(1, 5):
        f = open(os.path.join(ORIG, 'WFONT%d.EGA' % n), 'rb').read()
        c = ord('M')
        cnt = collections.Counter(
            sum(((f[c * 32 + p * 8 + y] >> (7 - x)) & 1) << p for p in range(4))
            for x in range(8) for y in range(8))
        mc = [k for k, _ in cnt.most_common()]
        res[n] = (mc[1] if len(mc) > 1 else 15, mc[0])
    return res


def main():
    install = '--install' in sys.argv
    os.makedirs(OUT, exist_ok=True)
    rd = lambda n: open(os.path.join(ORIG, n), 'rb').read()
    lines = msgtool.read_all(rd('MSG.HDR'), rd('MSG.DBS'), rd('MISC.HDR'))
    tr = load_translations()
    mis = fontgen.Misaki(); shn = fontgen.Shinonome()

    # collect characters
    small, large = [], []
    seen_s, seen_l = set(), set()
    toks = {}
    for mid, s in lines:
        if mid not in tr:
            continue
        font, text = tr[mid]
        # {X} = hotkey letter X (kept as first byte for the menu code) then ZAP (code 0)
        tk = []
        for part in re.split(r'(\{[A-Za-z0-9]\})', text):
            if re.fullmatch(r'\{[A-Za-z0-9]\}', part):
                tk += [ord(part[1].upper())] + ([] if os.environ.get('NOZAP') else ['ZAP'])
            elif part:
                tk += msgtool.unesc(part)
        toks[mid] = (font, tk)
        for t in tk:
            if isinstance(t, str) and t != 'ZAP':
                if font == 'l':
                    if t not in seen_l:
                        if not shn.has(t):
                            raise SystemExit('Shinonome lacks %r (id %d)' % (t, mid))
                        seen_l.add(t); large.append(t)
                else:
                    if t not in seen_s:
                        if not mis.has(t):
                            raise SystemExit('Misaki lacks %r (id %d)' % (t, mid))
                        seen_s.add(t); small.append(t)
    import scentool
    scen_ja = scentool.load_ja()
    for key, t in scen_ja.items():
        for c in t:
            if ord(c) >= 0x80 and c not in seen_s:
                if not mis.has(c):
                    raise SystemExit('Misaki lacks %r (%s)' % (c, key))
                seen_s.add(c); small.append(c)
    if large:
        raise SystemExit('font "l" (Shinonome) is not supported by the 1/2-byte encoding')
    # --- choose the 95 single-byte glyphs: chars of race/class/sex names first, then by frequency
    import collections
    freq = collections.Counter()
    for font, tk in toks.values():
        freq.update(t for t in tk if isinstance(t, str) and t != 'ZAP')
    for t in scen_ja.values():
        freq.update(c for c in t if ord(c) >= 0x80)
    must = []
    gl = {}
    for ln in open(os.path.join(ROOT, 'trans', 'glossary.tsv'), encoding='utf-8'):
        if ln.startswith('#') or '\t' not in ln:
            continue
        en, ja = ln.rstrip('\n').split('\t')[:2]
        gl[en] = ja
    for en in ['HUMAN', 'ELF', 'DWARF', 'GNOME', 'HOBBIT', 'FAERIE', 'LIZARDMAN', 'DRACON', 'FELPURR',
               'RAWULF', 'MOOK', 'FIGHTER', 'MAGE', 'PRIEST', 'THIEF', 'RANGER', 'ALCHEMIST', 'BARD',
               'PSIONIC', 'VALKYRIE', 'BISHOP', 'LORD', 'SAMURAI', 'MONK', 'NINJA', 'MALE', 'FEMALE']:
        for c in gl.get(en, ''):
            if ord(c) >= 0x80 and c not in must and c in seen_s:
                must.append(c)
    singles = must[:95]
    for c, _ in freq.most_common():
        if len(singles) >= 95:
            break
        if c not in singles:
            singles.append(c)
    doubles = [c for c in small if c not in singles]
    assert len(doubles) <= 32 * 128
    scode = {c: i + 1 for i, c in enumerate(singles)}            # 1..95 -> byte 0x81..0xDF
    for i, c in enumerate(doubles):
        scode[c] = 96 + i
    scode['ZAP'] = 0
    small = singles + [''] * (95 - len(singles)) + doubles       # font table order (after code 0)
    print('glyphs: %d single-byte, %d double-byte' % (len(singles), len(doubles)))

    def encode(font, tk):
        b = bytearray()
        for t in tk:
            if isinstance(t, int):
                b.append(t)
            else:
                code = scode[t]
                if code < 96:
                    b.append(0x80 + code)
                else:
                    d = code - 96
                    b += bytes([0xe0 + (d >> 7), 0x80 | (d & 0x7f)])
        return bytes(b)

    newlines = []
    nbad = 0
    for mid, s in lines:
        if mid in toks:
            b = encode(*toks[mid])
            if len(b) > 79:
                print('WARN id %d: %d bytes (>79)' % (mid, len(b))); nbad += 1
            newlines.append((mid, b))
        else:
            newlines.append((mid, s))
    hdr, dbs, misc = msgtool.write_all(newlines)
    assert msgtool.read_all(hdr, dbs, misc) == newlines

    # fonts
    sfont = bytes(8) + b''.join(mis.bitmap(c) if c else bytes(8) for c in small)
    lfont = b''.join(shn.bitmap(c) for c in large)

    # module
    src = open(os.path.join(ROOT, 'src', 'jpext.asm'), encoding='utf-8').read()
    subst = dict(VARS)
    subst['PAD'] = ','.join(['0'] * (CODE_BASE - MOD_BASE - 9))
    code = assemble(preprocess(src, subst), MOD_BASE)
    end = MOD_BASE + len(code)
    end = (end + 15) & ~15
    a_sfont = end; end += len(sfont)
    a_lfont = end; end += len(lfont)
    end = (end + 15) & ~15
    a_tiles = end; end += NTILES * 8
    a_hash = end; end += HASHN * 2
    a_used = end; end += NTILES
    print('module code %d bytes; small %d glyphs, large %d glyphs; segment end 0x%x' % (
        len(code), len(small), len(large), end))
    if end > 0xFF00:
        raise SystemExit('driver segment overflow')
    mod = bytearray(code) + bytes(end - MOD_BASE - len(code))
    def put16(addr, v):
        mod[addr - MOD_BASE: addr - MOD_BASE + 2] = struct.pack('<H', v)
    put16(VARS['VENTRY1'], 0x107)
    mod[VARS['PBIT'] - MOD_BASE] = 1
    for n, (fg, bg) in icon_font_colors().items():
        mod[VARS['FGTAB'] - MOD_BASE + n] = fg
        mod[VARS['BGTAB'] - MOD_BASE + n] = bg
    put16(VARS['VENTRY0'], 0x103); put16(VARS['VMHIDE'], 0x11b); put16(VARS['VMSHOW'], 0x11f)
    mod[VARS['VOFF'] - MOD_BASE] = 2
    put16(VARS['NSMALL'], len(small) + 1); put16(VARS['NLARGE'], len(large))
    put16(VARS['SFONT'], a_sfont); put16(VARS['LFONT'], a_lfont)
    put16(VARS['VTILES'], a_tiles); put16(VARS['VHASH'], a_hash); put16(VARS['VUSED'], a_used)
    mod[a_sfont - MOD_BASE: a_sfont - MOD_BASE + len(sfont)] = sfont
    mod[a_lfont - MOD_BASE: a_lfont - MOD_BASE + len(lfont)] = lfont

    drv = bytearray(rd('EGA.DRV'))
    assert len(drv) <= MOD_FILE
    drv += bytes(MOD_FILE - len(drv))
    drv += mod

    # WROOT patches (image offsets; file = +0x200)
    ex = bytearray(rd('WROOT.EXE'))
    def patch(img, old, new):
        f = img + 0x200
        assert ex[f:f + len(old)] == bytes(old), ('mismatch at %x' % img, ex[f:f + len(old)].hex())
        ex[f:f + len(new)] = bytes(new)
    patch(0x1b82, [0x40, 0x02], [0x00, 0x10])                    # driver seg 64KB
    patch(0x1f5a, [0xb9, 0x00, 0x24], [0xb9, 0x00, 0xff])        # driver read size
    patch(0x2097, [0xb8, 0x00, 0x04], [0xb8, 0x00, 0x08])        # font0 buffer 2KB (slot 0xFF scratch)
    patch(0x20b6, [0xb8, 0x00, 0x10], [0xb8, 0x00, 0x20])        # icon fonts 8KB (slot 0xFF scratch)
    # putchar stub @0x22b7: push bp; mov bp,sp; push cs; push 22c7; push cs:[1b8c]; push 2400; retf; pop bp; ret
    patch(0x22b7, [0x55, 0x8b, 0xec, 0x83, 0xec, 0x06, 0x1e, 0x06, 0x57, 0x56, 0x2e, 0xff, 0x1e, 0xa2, 0x1b,
                   0xc7, 0x46, 0xfa],
          [0x55, 0x8b, 0xec, 0x0e, 0x68, 0xc7, 0x22, 0x2e, 0xff, 0x36, 0x8c, 0x1b, 0x68, 0x00, 0x24, 0xcb,
           0x5d, 0xc3])
    # putchar2 stub @0x23e3 -> module 0x2406, return label 0x23f3
    patch(0x23e3, [0x55, 0x8b, 0xec, 0x83, 0xec, 0x04, 0x1e, 0x06, 0x57, 0x56, 0x2e, 0xff, 0x1e, 0xa2, 0x1b,
                   0x8b, 0x76, 0x04],
          [0x55, 0x8b, 0xec, 0x0e, 0x68, 0xf3, 0x23, 0x2e, 0xff, 0x36, 0x8c, 0x1b, 0x68, 0x06, 0x24, 0xcb,
           0x5d, 0xc3])
    # refresh stub @0x2c4c: push [bp-12h]; push cs; push 2c6e; push cs:[1b8c]; push 2403; retf
    patch(0x2c4c, [0x8c, 0xc8, 0x8e, 0xd8, 0xba, 0x02, 0x2f, 0xb4, 0x09, 0xcd, 0x21, 0xb8, 0xff, 0x4c, 0xcd, 0x21],
          [0xff, 0x76, 0xee, 0x0e, 0x68, 0x6e, 0x2c, 0x2e, 0xff, 0x36, 0x8c, 0x1b, 0x68, 0x03, 0x24, 0xcb])

    # helpers (dstrlen / fixw) in the dead putchar body
    HELP_BASE = 0x22d0
    hsrc = open(os.path.join(ROOT, 'src', 'wroot_helpers.asm'), encoding='utf-8').read()
    hcode = assemble(preprocess(hsrc, {}), HELP_BASE)
    assert HELP_BASE + len(hcode) <= 0x23e3
    ex[HELP_BASE + 0x200: HELP_BASE + 0x200 + len(hcode)] = hcode
    a_dstrlen = HELP_BASE                       # first routine in the file
    a_fixw = HELP_BASE + hcode.index(bytes([0x56, 0x51, 0x52, 0x8b, 0x76, 0xaa]))

    # WROOT's own centering sites
    for m in re.finditer(re.escape(bytes([0x40, 0xb9, 0x02, 0x00, 0x99, 0xf7, 0xf9])), bytes(ex)):
        f = m.start() - 4
        i = f - 0x200
        if ex[f] == 0xe8 and ex[f + 3] == 0x59 and \
                ((i + 3 + struct.unpack('<h', ex[f + 1:f + 3])[0]) & 0xffff) == 0x400f:
            ex[f + 1:f + 3] = struct.pack('<h', ((a_dstrlen - (i + 3) + 0x8000) & 0xffff) - 0x8000)

    scen = bytearray(rd('SCENARIO.DBS'))
    nscen = scentool.apply(scen, lambda t: encode('s', msgtool.unesc(t)))
    print('scenario names replaced:', nscen)

    # party-panel class abbreviations: WFONT1.EGA glyphs 0x3A..0x55 (2 cells per class, fg 3 / bg 8)
    wf1 = bytearray(rd('WFONT1.EGA'))
    CLASS_ABBR = ['戦士', '魔術', '僧侶', '盗賊', '野伏', '錬金', '詩人', '超能', '戦乙', '司教',
                  '君主', '侍', '修道', '忍者']
    for k, txt in enumerate(CLASS_ABBR):
        rows = [0] * 8
        x0 = (16 - 8 * len(txt)) // 2
        for j, ch in enumerate(txt):
            for y, b in enumerate(mis.bitmap(ch)):
                rows[y] |= b << (8 - x0 - 8 * j)
        for half in range(2):
            c = 0x3a + 2 * k + half
            for y in range(8):
                bits = (rows[y] >> (8 * (1 - half))) & 0xff
                for p in range(4):
                    v = (bits if (3 >> p) & 1 else 0) | ((~bits & 0xff) if (8 >> p) & 1 else 0)
                    wf1[c * 32 + p * 8 + y] = v

    files = {'WROOT.EXE': ex, 'EGA.DRV': drv, 'MSG.DBS': dbs, 'MSG.HDR': hdr, 'MISC.HDR': misc,
             'SCENARIO.DBS': scen, 'WFONT1.EGA': wf1}

    # overlays: layout strlen -> dstrlen ; word-wrap width -> fixw
    OVB = 0x4564
    def rel(site, target):
        return struct.pack('<h', ((target - (site + 3) + 0x8000) & 0xffff) - 0x8000)
    nc = nw = 0
    for ovn in sorted(f for f in os.listdir(ORIG) if f.endswith('.OVR')):
        ov = bytearray(rd(ovn))
        for m in re.finditer(rb'\x40\xb9\x02\x00\x99\xf7\xf9', bytes(ov)):
            i = m.start() - 4
            if ov[i] == 0xe8 and ov[i + 3] == 0x59 and \
                    ((OVB + i + 3 + struct.unpack('<h', ov[i + 1:i + 3])[0]) & 0xffff) == 0x400f:
                ov[i + 1:i + 3] = rel(OVB + i, a_dstrlen); nc += 1
        for m in re.finditer(rb'\x59\x89\x46\xfc\x8b\x1e..\x8a\x47\x06\x2a\xe4\x8b\xc8\x8b\x46\xfc', bytes(ov), re.S):
            i = m.start() + 1
            ov[i:i + 3] = b'\xe8' + rel(OVB + i, a_fixw); nw += 1
        files[ovn] = ov
    print('overlay patches: %d centering, %d word-wrap' % (nc, nw))
    for n, d in files.items():
        open(os.path.join(OUT, n), 'wb').write(d)
    print('built:', ', '.join('%s(%d)' % (n, len(d)) for n, d in files.items()))
    if install:
        for n in files:
            shutil.copyfile(os.path.join(OUT, n), os.path.join(GAME, n))
        print('installed to', GAME)


if __name__ == '__main__':
    main()
