"""Write translations back into NSF / MSG / DBS files.

Formats (verified on every file, see NOTES.md):
  NSF  : serialized script tree; strings are u16 length (chars) + UTF-16, pages grouped by a u8 count.
         The file holds no file offsets (pointer-looking values are stale heap addresses), so strings may
         change length freely.
  MSG  : u32 1, u8 1, then records [8 bytes ff][u32 len][len chars, obfuscated 0x9696-c, last is \\0],
         then at P: i32 -1, u32 offset[n], u32 n, u32 P.
  DESC : (ITEMDESC/SPELLDESC.DBS) 5-byte prefix, records [8-byte header][u32 len][len chars],
         then u32 offset[n], u32 n, u32 0.
  names: (ITEMS/MONSTERS/NPC/LEVELS/SPELLTABLES.DBS) fixed-size, \\0-terminated UTF-16 fields, written in
         place. A new name may be as long as the English one, or up to the known field capacity.

  python import_text.py --roundtrip   rebuild every file with the English text and compare (must be identical)
"""
import os, re, sys, json, glob, struct

GAME = r'L:\SteamLibrary\steamapps\common\Wizardry8'
ROOT = os.path.join(GAME, 'JPMod')
RAW = os.path.join(ROOT, 'work', 'raw')
BS = chr(92)

# id prefix -> (raw path, archive entry name)
NAME_DBS = {
    'DATABASES/ITEMS.DBS': 'patch001/DATABASES/ITEMS.DBS',
    'DATABASES/SPELLTABLES.DBS': 'patch001/DATABASES/SPELLTABLES.DBS',
    'DATABASES/MONSTERS.DBS': 'DATABASES/MONSTERS.DBS',
    'DATABASES/NPC.DBS': 'DATABASES/NPC.DBS',
    'DATABASES/LEVELS.DBS': 'DATABASES/LEVELS.DBS',
    'DATABASES/FACT.DBS': 'DATABASES/FACT.DBS',      # journal entries, see textdb.scan_fact
}
CAPACITY = {'DATABASES/ITEMS.DBS': 29, 'DATABASES/MONSTERS.DBS': 23}


def files_by_kind():
    out = {}
    for p in glob.glob(os.path.join(RAW, 'NPC SCRIPTS', '*.NSF')):
        out['NPC SCRIPTS/' + os.path.basename(p)] = ('nsf', p)
    for p in glob.glob(os.path.join(RAW, 'MESSAGES', '*.MSG')):
        out['MESSAGES/' + os.path.basename(p)] = ('msg', p)
    for p in glob.glob(os.path.join(RAW, 'QUOTES', 'PCS', '*.MSG')):
        out['QUOTES/PCS/' + os.path.basename(p)] = ('msg', p)
    for n in ('ITEMDESC', 'SPELLDESC'):
        out['DATABASES/%s.DBS' % n] = ('desc', os.path.join(RAW, 'DATABASES', n + '.DBS'))
    for k, v in NAME_DBS.items():
        out[k] = ('names', os.path.join(RAW, v))
    return out


def enc(s, obf):
    ws = [ord(c) for c in s]
    if obf:
        ws = [(0x9696 - w) & 0xffff for w in ws]
    return struct.pack('<%dH' % len(ws), *ws)


def dec(b, obf):
    ws = struct.unpack('<%dH' % (len(b) // 2), b[:len(b) // 2 * 2])
    if obf:
        ws = [(0x9696 - w) & 0xffff for w in ws]
    return ''.join(map(chr, ws))


class Report:
    def __init__(self):
        self.replaced = 0
        self.warn = []


def splice(d, items, lw, obf, rep, fname):
    """items: {offset_of_length_prefix: new_text}. Returns new bytes and a function old_pos -> new_pos."""
    out = bytearray()
    pos = 0
    shifts = []   # (old_end, delta) cumulative
    delta = 0
    for o in sorted(items):
        L = int.from_bytes(d[o:o + lw], 'little')
        old = dec(d[o + lw:o + lw + 2 * L], obf)
        nul = old.endswith('\0')
        new = items[o] + ('\0' if nul else '')
        out += d[pos:o]
        out += len(new).to_bytes(lw, 'little') + enc(new, obf)
        pos = o + lw + 2 * L
        delta += 2 * (len(new) - L)
        shifts.append((pos, delta))
        rep.replaced += 1
    out += d[pos:]

    def newpos(p):
        dd = 0
        for end, dl in shifts:
            if end <= p:
                dd = dl
            else:
                break
        return p + dd
    return bytes(out), newpos


def check_prefixed(d, o, lw, obf, en):
    L = int.from_bytes(d[o:o + lw], 'little')
    s = dec(d[o + lw:o + lw + 2 * L], obf).rstrip('\0')
    return s == en


def rebuild(kind, d, items, rep, fname):
    if kind == 'nsf':
        return splice(d, items, 2, False, rep, fname)[0]
    if kind == 'msg':
        n, P = struct.unpack_from('<II', d, len(d) - 8)
        offs = struct.unpack_from('<%dI' % n, d, P + 4)
        nd, newpos = splice(d, items, 4, True, rep, fname)
        nP = newpos(P)
        body = nd[:nP]
        tab = nd[nP:nP + 4] + struct.pack('<%dI' % n, *[newpos(o) for o in offs]) + struct.pack('<II', n, nP)
        return body + tab
    if kind == 'desc':
        n = struct.unpack_from('<I', d, len(d) - 8)[0]
        P = len(d) - 8 - 4 * n
        offs = struct.unpack_from('<%dI' % n, d, P)
        nd, newpos = splice(d, items, 4, False, rep, fname)
        nP = newpos(P)
        return nd[:nP] + struct.pack('<%dI' % n, *[newpos(o) for o in offs]) + d[-8:]
    if kind == 'names':
        b = bytearray(d)
        for o, new in items.items():
            L = 0
            while d[o + 2 * L:o + 2 * L + 2] != b'\0\0':
                L += 1
            b[o:o + 2 * L + 2] = b'\0' * (2 * L + 2)
            b[o:o + 2 * len(new) + 2] = enc(new + '\0', False)
            rep.replaced += 1
        return bytes(b)
    raise ValueError(kind)


def build(tr, roundtrip=False):
    """tr: {id: text}. Returns ({archive entry name: bytes}, Report)."""
    rep = Report()
    groups = {}
    for k, v in tr.items():
        if '@' not in k:
            continue
        f, o = k.rsplit('@', 1)
        groups.setdefault(f, {})[int(o, 16)] = (k, v)
    texts = {}
    for g in glob.glob(os.path.join(ROOT, 'work', 'text', '*.json')):
        for e in json.load(open(g, encoding='utf-8')):
            texts[e['id']] = e['en']
    out = {}
    for f, (kind, path) in sorted(files_by_kind().items()):
        if f not in groups:
            continue
        d = open(path, 'rb').read()
        items = {}
        for o, (k, v) in groups[f].items():
            en = texts.get(k)
            if en is None:
                rep.warn.append((k, 'id not in extracted text')); continue
            if kind == 'names':
                L = 0
                while d[o + 2 * L:o + 2 * L + 2] != b'\0\0':
                    L += 1
                full = dec(d[o:o + 2 * L], False)
                tail = full[len(en):]
                # "Crab# 6": the game shows the part before '#'; keep the developer note after it
                if not full.startswith(en) or (tail and not tail.lstrip().startswith('#')):
                    rep.warn.append((k, 'name field does not match English')); continue
                v = v + tail
                cap = max(len(full), CAPACITY.get(f, 0))
                if f == 'DATABASES/FACT.DBS':      # two text fields per 472-byte record
                    cap = {0x38: 99, 0x100: 107}.get((o - 4) % 472, len(full))
                if len(v) > cap:
                    rep.warn.append((k, 'name too long (%d > %d): %s' % (len(v), cap, v))); continue
            else:
                lw, obf = (2, False) if kind == 'nsf' else (4, kind == 'msg')
                # extraction artifact: for a 255-char page the scanner read [count][len 0x00ff] one byte
                # early as length 0x0100 + chr(0xff); the real prefix is 2 bytes later
                if en.startswith(chr(0xff)) and check_prefixed(d, o + 2, lw, obf, en[1:]):
                    o, en = o + 2, en[1:]
                    v = v.lstrip(chr(0xff)).lstrip(' ')
                elif not check_prefixed(d, o, lw, obf, en):
                    rep.warn.append((k, 'string at offset does not match English')); continue
            items[o] = v
        nd = rebuild(kind, d, items, rep, f)
        if roundtrip and nd != d:
            rep.warn.append((f, 'ROUNDTRIP MISMATCH (%d vs %d bytes)' % (len(nd), len(d))))
        out[f.replace('/', BS)] = nd
    return out, rep


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    if '--roundtrip' in sys.argv:
        tr = {}
        for g in glob.glob(os.path.join(ROOT, 'work', 'text', '*.json')):
            for e in json.load(open(g, encoding='utf-8')):
                tr[e['id']] = e['en']
        out, rep = build(tr, roundtrip=True)
        print(len(out), 'files,', rep.replaced, 'strings rewritten,', len(rep.warn), 'warnings')
        for w in rep.warn[:40]:
            print('  ', w)
