/* ---- Addresses in other builds of Wiz8.exe ----
   Every address in this hook is the one of the Steam Wiz8.exe. Other builds of the same game (the Fan
   Patch 1.28 Wiz8_v128.exe and its updates) hold the same code, moved by a few hundred bytes. When the
   running exe is not the Steam one, resolve_addresses() finds each address from its signature
   (addr_sig.h, tools/sigmap.py): the code bytes around it (call targets and absolute addresses are
   wildcards), the operand of an instruction that uses it, an import, or its text. A() then returns the
   address in the running exe, or 0 when it was not found (that patch is skipped; the patch helpers
   patch_call, patch_call_to, patch_push_imm, hook_jmp, inline_hook and iat_hook take Steam addresses and
   call A() themselves; emit_rel32 takes the real one). */
#include "addr_sig.h"
#define NSIG ((int)(sizeof(g_sig) / sizeof(g_sig[0])))
#define TS_STEAM 0x3c26eee6u      /* PE TimeDateStamp of the Steam Wiz8.exe */

static int   g_other;             /* not the Steam exe: the addresses come from g_res */
static int   g_fanpatch;          /* the Fan Patch 1.28 (its CFAgent) is installed next to the exe */
static DWORD g_res[NSIG];
static BYTE *g_img;
static DWORD g_img_size, g_img_pref;   /* g_img_pref: the exe's preferred base (= g_img in the game) */

static DWORD A(DWORD a)
{
    int lo = 0, hi = NSIG - 1;
    if (!g_other || a < 0x00401000u || a >= 0x00800000u) return a;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (g_sig[m].steam == a) return g_res[m];
        if (g_sig[m].steam < a) lo = m + 1; else hi = m - 1;
    }
    logf("address %08x has no signature (rebuild addr_sig.h)", a);
    return 0;
}

static int sig_at(const BYTE *p, const AddrSig *s)
{
    int i;
    for (i = 0; i < s->len; i++)
        if ((BYTE)s->mask[i] && p[i] != (BYTE)s->pat[i]) return 0;
    return 1;
}

/* the start of the signature closest to `expect` (searched near it first, then in the whole image) */
static DWORD sig_find(const AddrSig *s, DWORD expect)
{
    DWORD base = (DWORD)g_img, lo, hi, a, best = 0, bestd = 0xffffffffu;
    int pass;
    for (pass = 0; pass < 2 && !best; pass++) {
        if (pass == 0) {
            lo = expect > base + 0x1000 + 0x8000 ? expect - 0x8000 : base + 0x1000;
            hi = expect + 0x8000;
        } else {
            lo = base + 0x1000;
            hi = base + g_img_size;
        }
        if (hi > base + g_img_size - s->len) hi = base + g_img_size - s->len;
        __try {
            for (a = lo; a < hi; a++) {
                DWORD d;
                if (((BYTE)s->mask[0] && *(BYTE *)a != (BYTE)s->pat[0]) || !sig_at((BYTE *)a, s)) continue;
                d = a > expect ? a - expect : expect - a;
                if (d < bestd) { best = a; bestd = d; }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            /* a gap in the image: keep what was found */
        }
    }
    return best;
}

/* the import slot of "dll!function" in the running exe */
static DWORD iat_find(const char *spec)
{
    BYTE *b = g_img;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(b + ((IMAGE_DOS_HEADER *)b)->e_lfanew);
    IMAGE_DATA_DIRECTORY *dd = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    IMAGE_IMPORT_DESCRIPTOR *d;
    const char *fn = strchr(spec, '!');
    char dll[64];
    if (!fn || !dd->VirtualAddress || fn - spec >= (int)sizeof(dll)) return 0;
    memcpy(dll, spec, fn - spec); dll[fn - spec] = 0; fn++;
    /* the slots already hold the function addresses (the fan patch exe has no name list,
       OriginalFirstThunk = 0), so compare with what the DLL exports */
    for (d = (IMAGE_IMPORT_DESCRIPTOR *)(b + dd->VirtualAddress); d->Name; d++) {
        DWORD *slots, want;
        HMODULE m;
        int i;
        if (_stricmp((char *)(b + d->Name), dll)) continue;
        m = GetModuleHandleA(dll);
        want = m ? (DWORD)GetProcAddress(m, fn) : 0;
        if (!want) return 0;
        slots = (DWORD *)(b + d->FirstThunk);
        for (i = 0; slots[i]; i++)
            if (slots[i] == want) return (DWORD)&slots[i];
    }
    return 0;
}

static int text_at(DWORD a, const char *t)
{
    int n = 0;
    DWORD base = (DWORD)g_img;
    while (t[n] || t[n + 1]) n += 2;
    n += 2;
    return a >= base && a + n <= base + g_img_size && !memcmp((void *)a, t, n);
}

static void resolve_addresses(void)
{
    int i, k, bad = 0;
    long shift = 0;
    for (i = 0; i < NSIG; i++) {                       /* code first, in address order */
        const AddrSig *s = &g_sig[i];
        DWORD r;
        if (s->kind != SIG_CODE) continue;
        r = sig_find(s, (DWORD)((long)s->steam + shift) - s->back);
        if (r) { g_res[i] = r + s->back; shift = (long)g_res[i] - (long)s->steam; }
    }
    for (i = 0; i < NSIG; i++) {
        const AddrSig *s = &g_sig[i];
        if (s->kind == SIG_DATA) {
            /* the displacement of the closest resolved code address tells where to look for the reference */
            long sh = 0;
            DWORD best = 0xffffffffu, r;
            for (k = 0; k < NSIG; k++)
                if (g_sig[k].kind == SIG_CODE && g_res[k]) {
                    DWORD d = g_sig[k].steam > s->ref ? g_sig[k].steam - s->ref : s->ref - g_sig[k].steam;
                    if (d < best) { best = d; sh = (long)g_res[k] - (long)g_sig[k].steam; }
                }
            r = sig_find(s, (DWORD)((long)s->ref + sh) - s->back);
            if (r) {
                DWORD v = *(DWORD *)(r + s->back) - g_img_pref + (DWORD)g_img;
                if (!s->text || text_at(v, s->text)) g_res[i] = v;
            }
        } else if (s->kind == SIG_IAT) {
            g_res[i] = iat_find(s->pat);
        } else if (s->kind == SIG_STR) {
            DWORD base = (DWORD)g_img, a;
            for (a = base + 0x1000; a + 4 < base + g_img_size && !g_res[i]; a += 2)
                if (*(WORD *)a == *(const WORD *)s->text && text_at(a, s->text)) g_res[i] = a;
        }
    }
    for (i = 0; i < NSIG; i++)
        if (!g_res[i]) { bad++; dlog("address %08x not found in this exe", g_sig[i].steam); }
    if (bad) logf("other Wiz8.exe build: %d of %d addresses not found (those patches are skipped)", bad, NSIG);
    else dlog("other Wiz8.exe build: all %d addresses found", NSIG);
}

static int file_exists(const char *name)
{
    char p[MAX_PATH];
    _snprintf(p, sizeof(p), "%s\\%s", g_dir, name);
    p[sizeof(p) - 1] = 0;
    return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
}

static void detect_exe(void)
{
    IMAGE_NT_HEADERS *nt;
    g_img = (BYTE *)GetModuleHandleA(NULL);
    nt = (IMAGE_NT_HEADERS *)(g_img + ((IMAGE_DOS_HEADER *)g_img)->e_lfanew);
    g_img_size = nt->OptionalHeader.SizeOfImage;
    g_img_pref = nt->OptionalHeader.ImageBase;
    g_fanpatch = file_exists("Wiz8.dll") || file_exists("cfagent1.28.dll");
    g_other = nt->FileHeader.TimeDateStamp != TS_STEAM;
    if (g_other) {
        __try {
            resolve_addresses();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            memset(g_res, 0, sizeof(g_res));      /* every patch is skipped: the game runs in English */
            logf("other Wiz8.exe build: the address search failed");
        }
    }
}
