"""Extract every translatable string of Wizardry 8 into JSON (work/text/*.json).

Each entry: {"id": "<file>@<hex offset>" or "STRINGDATA#<n>", "en": text, "ctx": hint}
Offsets point at the length prefix (NSF/MSG/STRINGDATA) or at the first char (DBS fixed fields),
so the importer can rebuild the files from the same ids.
"""
import os, re, struct, json, glob
from slf import read_file, BS
from strdat import decode

GAME = r'L:\SteamLibrary\steamapps\common\Wizardry8'
RAW = os.path.join(GAME, 'JPMod', 'work', 'raw')
OUT = os.path.join(GAME, 'JPMod', 'work', 'text')

SPECIAL = {0x2019, 0x2018, 0x201c, 0x201d, 0x2026, 0x2014, 0x2013}
PLACEHOLDERS = {'EMPTY', 'UNKNOWN', 'CLASSIFIED', 'BLANK', 'SOUND', 'CLASSIFED', 'UKNOWN'}


def okc(c):
    return 32 <= c < 127 or c in (9, 10, 13) or 0x80 <= c < 0x100 or c in SPECIAL


def scan_prefixed(d, lw, obf):
    """length(lw bytes, in chars) + UTF-16 chars (optionally obfuscated)."""
    res, i, n = [], 0, len(d)
    while i < n - lw - 2:
        L = int.from_bytes(d[i:i + lw], 'little')
        if 2 <= L < 20000 and i + lw + 2 * L <= n:
            ws = struct.unpack_from('<%dH' % L, d, i + lw)
            cs = [((0x9696 - w) & 0xffff) for w in ws] if obf else list(ws)
            body = cs[:-1] if cs[-1] == 0 else cs
            if body and all(okc(c) for c in body) and sum(65 <= c <= 122 for c in body) >= 2:
                res.append((i, ''.join(map(chr, body))))
                i += lw + 2 * L
                continue
        i += 1
    return res


def scan_fields(d):
    """null-terminated UTF-16 strings in fixed-size DBS fields (skip stale tails of old values)."""
    res = []
    for m in re.finditer(rb'(?:(?:[\x20-\x7e]|[\xa0-\xff])\x00){2,}(?=\x00\x00)', d):
        o = m.start()
        if o % 1 == 0 and o >= 2:
            prev = struct.unpack_from('<H', d, o - 2)[0]
            if prev != 0:
                continue
            if o >= 4 and okc(struct.unpack_from('<H', d, o - 4)[0]) and struct.unpack_from('<H', d, o - 4)[0] > 32:
                continue  # tail left after a shorter string overwrote a longer one
        s = m.group().decode('utf-16le')
        if sum(c.isascii() and c.isalpha() for c in s) >= 2:
            res.append((o, s))
    return res


def scan_npc_headers(d, known):
    """NPC.DBS: the name shown in conversation sits after a [u16 2][u16 value] header, often at an odd
    offset, so scan_fields (which wants a zero u16 before the string) misses most of them."""
    res = []
    for m in re.finditer(rb'\x02\x00..((?:[\x20-\x7e]\x00){2,})\x00\x00', d, re.S):
        o = m.start(1)
        s = m.group(1).decode('utf-16le')
        if o not in known and re.search('[A-Za-z]{2}', s):
            res.append((o, s))
    return res


def scan_records(d, rec, fields, known):
    """ITEMS.DBS / MONSTERS.DBS: u32 count, then fixed-size records. scan_fields wants a zero u16 before a
    string, so it misses names whose field is preceded by 0xCDCD padding. Read those by record layout.
    fields: (offset in record, bytes). -> [(file offset, text)] not already in known."""
    n = struct.unpack_from('<I', d)[0]
    res = []
    for i in range(n):
        for fo, size in fields:
            o = 4 + i * rec + fo
            s = d[o:o + size].decode('utf-16le', 'replace').split('\0')[0]
            if o not in known and re.search('[A-Za-z]{2}', s) and all(okc(ord(c)) for c in s):
                res.append((o, s))
    return res


ITEM_REC, ITEM_FIELDS = 269, ((0, 60),)
MONSTER_REC, MONSTER_FIELDS = 663, ((0, 48), (48, 48), (96, 48), (144, 48))   # name, plural, unidentified, plural


FACT_REC = 472
FACT_FIELDS = ((0x38, 100), (0x100, 108))   # (offset in record, WCHARs incl. the terminator)


def scan_fact(d):
    """FACT.DBS: u32 count, then 472-byte records: u32 id, char name[52] (FACT_...),
    WCHAR text[100] (journal entry: the event, or the quest to do) at +0x38,
    WCHAR done[108] (shown once the quest is solved) at +0x100.
    -> [(file offset, text, fact name, field capacity in chars without the terminator)]"""
    n = struct.unpack_from('<I', d)[0]
    res = []
    for i in range(n):
        r = 4 + i * FACT_REC
        name = d[r + 4:r + 0x38].split(b'\x00')[0].decode('latin1')
        for fo, cap in FACT_FIELDS:
            raw = d[r + fo:r + fo + 2 * cap]
            s = raw.decode('utf-16le').split('\0')[0]
            if re.search('[A-Za-z]{2}', s):
                res.append((r + fo, s, name, cap - 1))
    return res


def entries_for(rel, items, ctx, skip=lambda s: False):
    out = []
    for o, s in items:
        if s in PLACEHOLDERS or skip(s):
            continue
        out.append({'id': '%s@%x' % (rel, o), 'en': s, 'ctx': ctx})
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    groups = {}

    # UI strings
    strs = decode(read_file(os.path.join(GAME, 'Patches', 'PATCH.001'), 'STRINGS' + BS + 'STRINGDATA.DAT'))
    groups['stringdata'] = [{'id': 'STRINGDATA#%d' % i, 'en': s.rstrip('\0'), 'ctx': 'UI'}
                            for i, s in enumerate(strs) if any(c.isalpha() for c in s)]

    # map / event messages
    for f in sorted(glob.glob(os.path.join(RAW, 'MESSAGES', '*.MSG'))):
        rel = 'MESSAGES/' + os.path.basename(f)
        groups.setdefault('messages', []).extend(
            entries_for(rel, scan_prefixed(open(f, 'rb').read(), 4, True), 'map message: ' + os.path.basename(f)[:-4]))

    # party member personality quotes
    for f in sorted(glob.glob(os.path.join(RAW, 'QUOTES', 'PCS', '*.MSG'))):
        b = os.path.basename(f)[:-4]
        rel = 'QUOTES/PCS/' + os.path.basename(f)
        groups['quotes_' + b] = entries_for(rel, scan_prefixed(open(f, 'rb').read(), 4, True), 'PC voice ' + b)

    # NPC dialogue
    for f in sorted(glob.glob(os.path.join(RAW, 'NPC SCRIPTS', '*.NSF'))):
        b = os.path.basename(f)[:-4]
        rel = 'NPC SCRIPTS/' + os.path.basename(f)
        groups['nsf_' + b] = entries_for(rel, scan_prefixed(open(f, 'rb').read(), 2, False), b)

    # databases (prefer the patched copies)
    for name, ctx in (('ITEMS', 'item name'), ('MONSTERS', 'monster name'), ('SPELLTABLES', 'spell name'),
                      ('NPC', 'NPC name'), ('LEVELS', 'map name')):
        p = os.path.join(RAW, 'patch001', 'DATABASES', name + '.DBS')
        if not os.path.exists(p):
            p = os.path.join(RAW, 'DATABASES', name + '.DBS')
        rel = 'DATABASES/%s.DBS' % name
        raw = open(p, 'rb').read()
        fields = scan_fields(raw)
        if name == 'NPC':
            fields = sorted(fields + scan_npc_headers(raw, {o for o, _ in fields}))
        groups['dbs_' + name.lower()] = entries_for(rel, fields, ctx, skip=lambda s: '#' in s)
        if name == 'MONSTERS':
            # singular names (and some plurals) carry a developer note after '#', e.g. "Crab# 6".
            # The game shows only the part before '#'; the importer keeps the '#...' tail.
            groups['dbs_monsters_hash'] = [
                {'id': '%s@%x' % (rel, o), 'en': s.split('#')[0].rstrip(), 'ctx': 'monster name'}
                for o, s in fields if '#' in s and re.search('[A-Za-z]', s.split('#')[0])]
        if name in ('ITEMS', 'MONSTERS'):
            rec, fl = (ITEM_REC, ITEM_FIELDS) if name == 'ITEMS' else (MONSTER_REC, MONSTER_FIELDS)
            # '#' tails are developer notes; the game shows only the part before '#' (kept by the importer)
            groups['dbs_%s_rec' % name.lower()] = [
                {'id': '%s@%x' % (rel, o), 'en': s.split('#')[0].rstrip(), 'ctx': ctx}
                for o, s in scan_records(raw, rec, fl, {o for o, _ in fields})
                if re.search('[A-Za-z]{2}', s.split('#')[0]) and s not in PLACEHOLDERS]
    d = open(os.path.join(RAW, 'DATABASES', 'FACT.DBS'), 'rb').read()
    groups["dbs_fact"] = [{"id": "DATABASES/FACT.DBS@%x" % o, "en": s, "ctx": "journal entry (%s)" % nm}
                          for o, s, nm, cap in scan_fact(d)]
    for name, ctx in (('ITEMDESC', 'item description'), ('SPELLDESC', 'spell description')):
        rel = 'DATABASES/%s.DBS' % name
        d = open(os.path.join(RAW, 'DATABASES', name + '.DBS'), 'rb').read()
        groups['dbs_' + name.lower()] = entries_for(rel, scan_prefixed(d, 4, False), ctx)

    total_w = 0
    for g, es in groups.items():
        json.dump(es, open(os.path.join(OUT, g + '.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
        total_w += sum(len(e['en'].split()) for e in es)
    print(len(groups), 'groups', sum(map(len, groups.values())), 'strings', total_w, 'words')


if __name__ == '__main__':
    main()
