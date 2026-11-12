"""Rebuild every file from wiz8jp.dat and the game's own originals (as the hook does) and check the
rewritten NSF/MSG/DBS files.

  * the original matches the size and CRC-32 recorded in wiz8jp.dat

  * MSG / DESC: offset table is consistent (records contiguous, table position right)
  * every translated id: the text found at its (shifted) place equals the expected translation
  * untouched bytes: the file minus the strings is identical to the original (structure preserved)
"""
import os, sys, struct, json, glob
sys.path.insert(0, os.path.dirname(__file__))
from slf import read_slf
from import_text import files_by_kind, dec, BS, check_prefixed
from build_patch import load_translations, OUT_DAT, DATA, PATCH1
from datstat import read as read_dat
from patchdat import apply as apply_ops
import zlib


def strings_and_skeleton(kind, d):
    """returns (list of decoded strings in file order, skeleton bytes without string bodies)"""
    from textdb import scan_prefixed
    if kind == 'names':
        return None, None
    if kind == 'nsf':
        raise NotImplementedError
    lw, obf = 4, kind == 'msg'
    if kind == 'msg':
        n, P = struct.unpack_from('<II', d, len(d) - 8)
        offs = struct.unpack_from('<%dI' % n, d, P + 4)
        assert P + 4 + 4 * n + 8 == len(d), 'table size'
    else:
        n = struct.unpack_from('<I', d, len(d) - 8)[0]
        P = len(d) - 8 - 4 * n
        offs = struct.unpack_from('<%dI' % n, d, P)
    pos = offs[0]
    strs, skel = [], bytearray(d[:offs[0]])
    for o in offs:
        assert o == pos, 'records not contiguous at %d' % o
        L = struct.unpack_from('<I', d, o + 8)[0]
        strs.append(dec(d[o + 12:o + 12 + 2 * L], obf).rstrip('\0'))
        skel += d[o:o + 8]
        pos = o + 12 + 2 * L
    assert pos == P, 'table position'
    return strs, bytes(skel)


def main():
    sys.stdout.reconfigure(encoding='utf-8')
    tr = load_translations()
    src = {}
    for lib in (DATA, PATCH1):                 # PATCH.001 overrides DATA.SLF, as in the game
        for n, o, l, st in read_slf(lib)[1]:
            src[n.upper()] = (lib, o, l)
    patch = {}
    bad = 0
    for name, osz, crc, nsz, ops in read_dat(OUT_DAT):
        lib, o, l = src[name.upper()]
        with open(lib, 'rb') as f:
            f.seek(o)
            orig = f.read(l)
        if len(orig) != osz or zlib.crc32(orig) & 0xffffffff != crc:
            print('ORIGINAL DIFFERS', name); bad += 1; continue
        new = apply_ops(orig, ops)
        if len(new) != nsz:
            print('SIZE', name); bad += 1; continue
        patch[name.upper()] = new
    kinds = files_by_kind()
    checked = 0
    for name, (kind, path) in sorted(kinds.items()):
        key = name.replace('/', BS).upper()
        if key not in patch:
            print('missing in patch:', name); bad += 1; continue
        new = patch[key]
        old = open(path, 'rb').read()
        ids = {int(k.rsplit('@', 1)[1], 16): v for k, v in tr.items() if k.startswith(name + '@')}
        if kind in ('msg', 'desc'):
            try:
                so, ko = strings_and_skeleton(kind, old)
                sn, kn = strings_and_skeleton(kind, new)
            except AssertionError as e:
                print('BROKEN', name, e); bad += 1; continue
            if ko != kn or len(so) != len(sn):
                print('STRUCTURE CHANGED', name); bad += 1; continue
            exp = set(v.lstrip(chr(0xff)).lstrip(' ').rstrip('\0') for v in ids.values())
            got = set(sn)
            miss = [v for v in exp if v not in got]
            if miss:
                print('NOT FOUND', name, miss[:2]); bad += 1
            checked += len(exp)
        elif kind == 'nsf':
            # walk original strings in order; in the new file each must be at the shifted position
            from textdb import scan_prefixed
            pos_old = 0; pos_new = 0; ok = True
            for o, s in scan_prefixed(old, 2, False):
                key_o = o
                if s.startswith(chr(0xff)) and check_prefixed(old, o + 2, 2, False, s[1:]):
                    o, s = o + 2, s[1:]
                L = struct.unpack_from('<H', old, o)[0]
                gap = old[pos_old:o]
                if new[pos_new:pos_new + len(gap)] != gap:
                    ok = False; break
                pn = pos_new + len(gap)
                Ln = struct.unpack_from('<H', new, pn)[0]
                txt = dec(new[pn + 2:pn + 2 + 2 * Ln], False).rstrip('\0')
                want = ids.get(key_o)
                if want is not None:
                    want = want.lstrip(chr(0xff)).lstrip(' ')
                if want is not None:
                    checked += 1
                    if txt != want.rstrip('\0'):
                        ok = False; break
                elif txt != s:
                    ok = False; break
                pos_old = o + 2 + 2 * L
                pos_new = pn + 2 + 2 * Ln
            if ok and new[pos_new:] != old[pos_old:]:
                ok = False
            if not ok:
                print('NSF MISMATCH', name, hex(o)); bad += 1
        else:
            for o, v in ids.items():
                L = 0
                while new[o + 2 * L:o + 2 * L + 2] != b'\0\0':
                    L += 1
                got = dec(new[o:o + 2 * L], False)
                if not (got == v or (got.startswith(v) and got[len(v):].lstrip().startswith('#'))):
                    print('NAME MISMATCH', name, hex(o), v); bad += 1
                checked += 1
            if len(new) != len(old):
                print('SIZE CHANGED', name); bad += 1
    print('checked %d strings in %d files, %d problems' % (checked, len(kinds), bad))


if __name__ == '__main__':
    main()
