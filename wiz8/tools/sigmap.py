"""Byte signatures for every address the hook uses:  python tools/sigmap.py [other exe ...]
-> hook/addr_sig.h  (then rebuild ddraw.dll)

The hook is written against the Steam Wiz8.exe. Other builds of the same game (the Fan Patch 1.28
Wiz8_v128.exe and its updates) contain the same code, moved by a few hundred bytes. At start-up the hook
(addr_sig.c part of jphook.c, resolve_addresses) finds each address in the running exe from these signatures:

  CODE  the bytes around the address; call/jmp targets and absolute addresses are wildcards
  DATA  an instruction that uses the address (found as CODE), the address is read from its operand;
        for a string the text is checked too (a build with other text there is skipped)
  IAT   DLL!function, looked up in the running exe's import table
  STR   a string without a code reference: found by its text

Every signature must be unique in the Steam exe. The exes given on the command line (default: the Fan Patch
6709 exe at L:\\Wiz8FP) are used to choose windows that also work there, and to report what they resolve to.
"""
import collections, glob, os, re, sys
import pefile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = os.path.dirname(ROOT)
STEAM = os.path.join(GAME, 'Wiz8.exe')
OUT = os.path.join(ROOT, 'hook', 'addr_sig.h')
B = 0x400000
TEXT_END = B + 0x1ea000
LIT = re.compile(r'0x00([4-6][0-9a-fA-F]{5})u?\b')
WINDOWS = ((0, 24), (0, 32), (12, 20), (16, 16), (24, 8), (32, 0), (0, 16), (8, 16), (40, 8), (0, 48), (24, 24), (48, 0))


class Exe:
    def __init__(self, path):
        self.pe = pefile.PE(path)
        self.m = self.pe.get_memory_mapped_image()
        self.iat = {}
        for e in self.pe.DIRECTORY_ENTRY_IMPORT:
            for i in e.imports:
                self.iat[i.address] = (e.dll.decode().lower(), (i.name or b'').decode())
        self.by_name = {v: k for k, v in self.iat.items()}


def sig(m, a, bf, af):
    """(bytes, mask) of [a-bf, a+af); mask 0 = any byte"""
    b = m[a - B - bf:a - B + af]
    mask = bytearray(b'\xff' * len(b))
    i = 0
    while i < len(b):
        if b[i] in (0xe8, 0xe9) and i + 5 <= len(b):
            mask[i + 1:i + 5] = b'\0' * 4
            i += 5
            continue
        if i + 4 <= len(b) and 0x401000 <= int.from_bytes(b[i:i + 4], 'little') < 0x800000:
            mask[i:i + 4] = b'\0' * 4
            i += 4
            continue
        i += 1
    return bytes(b), bytes(mask)


def find(m, pat, mask, lo=0, hi=None):
    rx = re.compile(b''.join(re.escape(pat[i:i + 1]) if mask[i] else b'.' for i in range(len(pat))), re.S)
    return [x.start() + B for x in rx.finditer(m, lo, len(m) if hi is None else hi)]


def code_sig(s, others, a):
    """the first window unique in the Steam exe that also gives one hit in every other exe -> (bf, af, hits)"""
    best = some = None
    for bf, af in WINDOWS:
        pat, mask = sig(s.m, a, bf, af)
        if sum(mask) // 255 < 10 or find(s.m, pat, mask, 0, 0x1ea000) != [a - bf]:
            continue
        hits = [find(o.m, pat, mask, 0, 0x230000) for o in others]
        if all(len(h) == 1 for h in hits):
            return bf, af, [h[0] + bf for h in hits]
        if some is None and all(hits):
            # several hits (a function with an identical twin): the hook takes the one nearest to where the
            # neighbouring addresses moved
            some = (bf, af, [min(h, key=lambda x: abs(x + bf - a)) + bf for h in hits])
        if best is None:
            best = (bf, af, [h[0] + bf if len(h) == 1 else None for h in hits])
    if some:
        return some
    for bf, af in WINDOWS:          # not unique even in the Steam exe (twin functions): nearest hit
        pat, mask = sig(s.m, a, bf, af)
        if sum(mask) // 255 < 10 or (a - bf) not in find(s.m, pat, mask, 0, 0x1ea000):
            continue
        hits = [find(o.m, pat, mask, 0, 0x230000) for o in others]
        if all(hits):
            return bf, af, [min(h, key=lambda x: abs(x + bf - a)) + bf for h in hits]
    return best


def string_at(m, a):
    w = m[a - B:a - B + 512]
    if len(w) >= 4 and w[1] == 0 and 0x20 <= w[0] < 0x7f:
        end = 0
        while end + 1 < len(w) and (w[end] or w[end + 1]):
            end += 2
        return w[:end + 2]
    return None


def row(a, kind, back, ln, pat, mask, text, ref):
    return '    { 0x%08x, %s, %d, %d, %s, %s, %s, 0x%08x },' % (a, kind, back, ln, pat, mask, text, ref)


def cstr(b):
    return '"' + ''.join('\\x%02x' % c for c in b) + '"'


def main():
    others = [Exe(p) for p in (sys.argv[1:] or [r'L:\Wiz8FP\Wiz8_v128.exe']) if os.path.exists(p)]
    s = Exe(STEAM)
    addrs = set()
    for p in glob.glob(os.path.join(ROOT, 'hook', '*.c')) + glob.glob(os.path.join(ROOT, 'hook', '*.h')):
        if os.path.basename(p).startswith('addr_'):
            continue
        addrs |= {int(x, 16) for x in LIT.findall(open(p, encoding='utf-8', errors='replace').read())}
    rows, report = [], []
    for a in sorted(addrs):
        if a in s.iat:
            dll, fn = s.iat[a]
            res = [o.by_name.get((dll, fn)) for o in others]
            rows.append(row(a, 'SIG_IAT', 0, 0, cstr(('%s!%s' % (dll, fn)).encode()), 'NULL', 'NULL', 0))
            report.append((a, 'IAT', res))
        elif a < TEXT_END:
            r = code_sig(s, others, a)
            if not r:
                report.append((a, 'NO SIGNATURE', [])); continue
            bf, af, res = r
            pat, mask = sig(s.m, a, bf, af)
            rows.append(row(a, 'SIG_CODE', bf, bf + af, cstr(pat), cstr(mask), 'NULL', 0))
            report.append((a, 'CODE', res))
        else:
            st = string_at(s.m, a)
            # a code reference: the operand position, found as CODE
            refs = []
            for x in re.finditer(re.escape(a.to_bytes(4, 'little')), s.m[0x1000:0x1ea000]):
                r = x.start() + 0x1000 + B
                c = code_sig(s, others, r)
                if c:
                    refs.append((sum(v is not None for v in c[2]), r, c))
            refs.sort(key=lambda t: -t[0])
            check = cstr(st) if st else 'NULL'
            if refs:
                _, r, (bf, af, res) = refs[0]
                pat, mask = sig(s.m, r, bf, af)
                vals = []
                for o, v in zip(others, res):
                    v = int.from_bytes(o.m[v - B:v - B + 4], 'little') if v else None
                    if v and st and o.m[v - B:v - B + len(st)] != st:
                        v = 'text differs'
                    vals.append(v)
                rows.append(row(a, 'SIG_DATA', bf, bf + af, cstr(pat), cstr(mask), check, r))
                report.append((a, 'DATA via %08x' % r, vals))
            elif st:
                rows.append(row(a, 'SIG_STR', 0, 0, 'NULL', 'NULL', cstr(st), 0))
                res = []
                for o in others:
                    h = [x.start() + B for x in re.finditer(re.escape(st), o.m)]
                    res.append(h[0] if len(h) == 1 else None)
                report.append((a, 'STR', res))
            else:
                report.append((a, 'NO SIGNATURE (data without reference)', []))
    with open(OUT, 'w', encoding='ascii', newline='\n') as f:
        f.write('/* generated by tools/sigmap.py: signatures of the Steam Wiz8.exe addresses the hook uses */\n')
        f.write('enum { SIG_CODE, SIG_DATA, SIG_IAT, SIG_STR };\n')
        f.write('typedef struct { DWORD steam; BYTE kind; BYTE back, len; const char *pat, *mask, *text; DWORD ref; } AddrSig;\n')
        f.write('static const AddrSig g_sig[] = {\n')
        for r in rows:
            f.write(r + '\n')
        f.write('};\n')
    print('%s: %d of %d addresses' % (OUT, len(rows), len(addrs)))
    for a, k, res in report:
        bad = (not res) or any(v is None or isinstance(v, str) for v in res)
        if bad:
            print('  %08x %-30s %s' % (a, k, ['%08x' % v if isinstance(v, int) else v for v in res]))


if __name__ == '__main__':
    main()
