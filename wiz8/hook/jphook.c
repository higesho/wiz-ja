/*
 * Wizardry 8 Japanese localization hook.
 *
 * Built as ddraw.dll (proxy, ddraw_proxy.h): Wiz8.exe imports DDRAW.DLL, which is not a KnownDLL,
 * so a ddraw.dll next to the exe is loaded instead of the system one; every export is passed on to
 * the system ddraw.dll. No game file is replaced or renamed: the Japanese data (wiz8jp.dat) is
 * applied in memory when the game reads a file (vfile.h). On load we patch Wiz8.exe so that the font
 * translation table (char -> glyph index, normally 252 entries) is extended with
 * the Japanese characters listed in wiz8jp.chr (raw UTF-16LE, next to Wiz8.exe).
 * The Japanese glyph for wiz8jp.chr[i] lives at sub-image 252 + i in every font STI.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define ADDR_CALL_CREATE_TABLE  0x00401604u  /* call CreateEnglishTransTable */
#define ADDR_CREATE_TABLE       0x00407ec0u
#define BASE_SYMBOLS            252

typedef struct {
    WORD  count;
    WORD  pad;
    WORD *chars;
} FontTransTable;

typedef FontTransTable *(__cdecl *CreateTable_t)(void);
typedef void *(__cdecl *malloc_t)(size_t);
typedef void (__cdecl *free_t)(void *);

static WCHAR g_extra[65536];
static int   g_nextra;
static char  g_dir[MAX_PATH];

/* wiz8jp.log gets errors only. Build with /DJP_DEBUG for the progress messages (dlog). */
#ifdef JP_DEBUG
#define dlog logf
#else
#define dlog(...) ((void)0)
#endif

static void logf(const char *fmt, ...)
{
    char path[MAX_PATH];
    FILE *f;
    va_list ap;
    _snprintf(path, sizeof(path), "%s\\wiz8jp.log", g_dir);
    f = fopen(path, "a");
    if (!f) return;
    {
        SYSTEMTIME t;
        GetLocalTime(&t);
        fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d  ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    }
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static void load_chars(void)
{
    char path[MAX_PATH];
    FILE *f;
    _snprintf(path, sizeof(path), "%s\\wiz8jp.chr", g_dir);
    f = fopen(path, "rb");
    if (!f) { logf("wiz8jp.chr not found"); return; }
    g_nextra = (int)fread(g_extra, 2, 65535 - BASE_SYMBOLS, f);
    fclose(f);
    if (g_nextra > 0 && g_extra[0] == 0xFEFF) { /* strip BOM if present */
        memmove(g_extra, g_extra + 1, (g_nextra - 1) * 2);
        g_nextra--;
    }
    dlog("loaded %d extra chars", g_nextra);
}

/* NPC script text (NSF) is kept as narrow char strings: the loader 0x55a140 converts each UTF-16
   line with sprintf(buf, "%S", wide) into malloc(len + 1), and the NPC code turns it back with
   swprintf(L"%S"). In the default "C" locale every character above U+00FF fails, so a Japanese line
   became empty and an NPC greeting waited forever. With the game's MSVCRT set to code page 932 both
   conversions go through Shift-JIS (the break character U+E000 maps to the EUDC byte pair F0 40), and
   the loader's buffer is doubled (patch_nsf_alloc). Called from MyCreateTable, i.e. after CRT start-up. */
static void set_cp932_locale(void)
{
    typedef char *(__cdecl *setlocale_t)(int, const char *);
    HMODULE crt = GetModuleHandleA("MSVCRT.DLL");
    setlocale_t f = crt ? (setlocale_t)GetProcAddress(crt, "setlocale") : NULL;
    const char *r = f ? f(2 /* LC_CTYPE */, ".932") : NULL;
    if (!r) logf("setlocale(LC_CTYPE, \".932\") failed: NPC dialogue will be empty");
}

static FontTransTable *__cdecl MyCreateTable(void)
{
    FontTransTable *t = ((CreateTable_t)ADDR_CREATE_TABLE)();
    HMODULE crt;
    malloc_t pmalloc;
    free_t pfree;
    WORD *arr;
    int n;

    set_cp932_locale();
    if (!t || g_nextra <= 0) return t;
    if (t->count != BASE_SYMBOLS) {
        logf("unexpected table size %d, not extending", t->count);
        return t;
    }
    /* the game frees this array with its own CRT (MSVCRT.DLL), so allocate from it */
    crt = GetModuleHandleA("MSVCRT.DLL");
    pmalloc = crt ? (malloc_t)GetProcAddress(crt, "malloc") : NULL;
    pfree = crt ? (free_t)GetProcAddress(crt, "free") : NULL;
    if (!pmalloc || !pfree) { logf("MSVCRT malloc/free not found"); return t; }

    n = t->count + g_nextra;
    arr = (WORD *)pmalloc(n * sizeof(WORD));
    if (!arr) return t;
    memcpy(arr, t->chars, t->count * sizeof(WORD));
    memcpy(arr + t->count, g_extra, g_nextra * sizeof(WORD));
    pfree(t->chars);
    t->chars = arr;
    t->count = (WORD)n;
    dlog("translation table extended: %d symbols", n);
    return t;
}

static BOOL patch_call(DWORD at, void *target, DWORD expect_old)
{
    BYTE *p = (BYTE *)at;
    DWORD old, prot;
    if (p[0] != 0xE8) { logf("no call at %08x", at); return FALSE; }
    old = at + 5 + *(LONG *)(p + 1);
    if (old != expect_old) { logf("call at %08x goes to %08x, expected %08x", at, old, expect_old); return FALSE; }
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &prot)) return FALSE;
    *(LONG *)(p + 1) = (LONG)((DWORD)target - (at + 5));
    VirtualProtect(p, 5, prot, &prot);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    return TRUE;
}

/* BinkDDSurfaceType crashes on this system: the game hands it a pointer that is not a valid
   DirectDraw surface (seen also with the original DLL and no mod). Call the original inside SEH
   and report "unknown surface type" (-1) when it faults. */
/* Bink functions are hooked in Wiz8.exe's import table (the game's own binkw32.dll stays untouched) */
#define IAT_BinkDDSurfaceType  0x005eb2c0u
#define IAT_BinkCopyToBuffer   0x005eb2c4u
#define IAT_BinkOpen           0x005eb2e0u
typedef int (__stdcall *PFN_BinkDDSurfaceType)(void *);
typedef void *(__stdcall *PFN_BinkOpen)(const char *, unsigned int);
static PFN_BinkDDSurfaceType o_BinkDDSurfaceType;
static PFN_BinkOpen          o_BinkOpen;

static int iat_hook(DWORD slot, void *fn, void **orig)
{
    DWORD prot;
    void **p = (void **)slot;
    if (!*p) return 0;
    *orig = *p;
    if (!VirtualProtect(p, 4, PAGE_READWRITE, &prot)) return 0;
    *p = fn;
    VirtualProtect(p, 4, prot, &prot);
    return 1;
}

static int __stdcall MyBinkDDSurfaceType(void *surface)
{
    if (!o_BinkDDSurfaceType) return -1;
    __try {
        return o_BinkDDSurfaceType(surface);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        logf("BinkDDSurfaceType(%p) faulted -> -1", surface);
        return -1;
    }
}

#include "fmt_spaces.h"
#include "sep_patch.h"
#include "wrap_patch.h"
#include "ddraw_proxy.h"
#include "vfile.h"

/* The space glyph is zero-width (it is the invisible line-break point of the Japanese text), so the
   spaces inside the exe's own wide display format strings ("%d-%2.2d-%2.2d %2d:%2.2d", "%s %s" ...)
   would vanish. Turn them into U+2002 (a visible blank of the original space width), in place. */
static void widen_format_spaces(void)
{
    int i, n = 0;
    for (i = 0; i < (int)(sizeof(g_fmt_va) / sizeof(g_fmt_va[0])); i++) {
        WCHAR *p = (WCHAR *)g_fmt_va[i];
        DWORD prot;
        int k, len = 0;
        while (len < 80 && p[len]) len++;
        if (len == 0 || len >= 80) { logf("format string at %08x looks wrong, skipped", g_fmt_va[i]); continue; }
        if (!VirtualProtect(p, len * 2, PAGE_READWRITE, &prot)) continue;
        for (k = 0; k < len; k++)
            if (p[k] == 0x20) { p[k] = 0x2002; n++; }
        VirtualProtect(p, len * 2, prot, &prot);
    }
    dlog("format strings: %d spaces made visible", n);
}

/* short English words the exe builds messages from ("3 kills earned"); replaced in place */
static const struct { DWORD va; const WCHAR *old, *rep; } g_words[] = {
    { 0x006174b4, L"kills", L"\x4f53\x6483\x7834" },   /* tai-gekiha (kills) */
    { 0x006174c0, L"kill",  L"\x4f53\x6483\x7834" },
    { 0x0061ec94, L"%s's",  L"%s\x306e" },           /* skill-up popup: "<name>'s" -> "<name>no" */
};

/* skill-up popup (0x5541xx): "<name>'s" + ' ' + 0xb5 + ' ' + "<skill> skill increased to <n>!".
   The two spaces after the name become U+E000 (zero width), so it reads "<name>no<skill>...". */
static void patch_skillup_spaces(void)
{
    static const DWORD sites[] = { 0x00554237, 0x00554252 };
    int i, ok = 0;
    for (i = 0; i < 2; i++) {
        BYTE *p = (BYTE *)sites[i];
        DWORD prot;
        if (memcmp(p, "\x66\xc7\x04\x47\x20\x00", 6) != 0) continue;
        if (!VirtualProtect(p, 6, PAGE_EXECUTE_READWRITE, &prot)) continue;
        *(WORD *)(p + 4) = 0xE000;
        VirtualProtect(p, 6, prot, &prot);
        ok++;
    }
    if (ok != 2) logf("skill-up spaces: only %d of 2 sites", ok);
}

static void replace_words(void)
{
    int i;
    for (i = 0; i < (int)(sizeof(g_words) / sizeof(g_words[0])); i++) {
        WCHAR *p = (WCHAR *)g_words[i].va;
        size_t n = wcslen(g_words[i].old), m = wcslen(g_words[i].rep);
        DWORD prot;
        if (m > n || wcscmp(p, g_words[i].old) != 0) { logf("word at %08x not found, skipped", g_words[i].va); continue; }
        if (!VirtualProtect(p, (n + 1) * 2, PAGE_READWRITE, &prot)) continue;
        memcpy(p, g_words[i].rep, (m + 1) * 2);
        VirtualProtect(p, (n + 1) * 2, prot, &prot);
    }
    dlog("exe words replaced");
}

/* Compound labels built as "%s %s": in Japanese the two parts join without a space
   (mental + resistance -> one word, wizardry + skill bonus -> one word). Each site is a
   "push imm32" of the format string; it is pointed at a copy without the space. */
static const WCHAR g_fmt_join2[] = L"%s%s";
static const WCHAR g_fmt_join_res[] = L"%s%s %+d%%";
static const struct { DWORD site, oldfmt; const WCHAR *fmt; } g_join_sites[] = {
    { 0x005c4bb2, 0x00617584, g_fmt_join2 },     /* <class skill> + "Skill Bonus" (traits list) */
    { 0x005c59e1, 0x0064eefc, g_fmt_join_res },  /* <realm> + "Resistance" +n% (personal effects) */
};

static void patch_join_formats(void)
{
    int i, ok = 0;
    for (i = 0; i < (int)(sizeof(g_join_sites) / sizeof(g_join_sites[0])); i++) {
        BYTE *p = (BYTE *)g_join_sites[i].site;
        DWORD prot;
        if (p[0] != 0x68 || *(DWORD *)(p + 1) != g_join_sites[i].oldfmt) { logf("join site %08x: unexpected bytes, skipped", g_join_sites[i].site); continue; }
        if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &prot)) continue;
        *(DWORD *)(p + 1) = (DWORD)g_join_sites[i].fmt;
        VirtualProtect(p, 5, prot, &prot);
        FlushInstructionCache(GetCurrentProcess(), p, 5);
        ok++;
    }
    if (ok != 2) logf("join-format patch: only %d of 2 sites", ok);
}

/* Lines built as swprintf(buf, L"%s %s?" / L"%s %s.", phrase, word): the party asking about a keyword
   ("Tell me about" + keyword, 0x574b0b) and "Proceed to" + place (0x561653). The Japanese phrases are
   suffixes ("...ni tsuite oshiete", "...e susumu"), so write word + phrase + U+FF1F / U+3002 instead. */
typedef int (__cdecl *swprintf_t)(WCHAR *, const WCHAR *, ...);
static int __cdecl MyPhraseFmt(WCHAR *buf, const WCHAR *fmt, const WCHAR *phrase, const WCHAR *word)
{
    swprintf_t sw = *(swprintf_t *)0x005eb188;   /* the game's MSVCRT swprintf (IAT) */
    size_t n = wcslen(fmt);
    const WCHAR *end = (n && fmt[n - 1] == L'?') ? L"\xff1f" : (n && fmt[n - 1] == L'.') ? L"\x3002" : L"";
    return sw(buf, L"%s%s%s", word, phrase, end);
}

static void patch_phrase_order(void)
{
    static const DWORD sites[] = { 0x00574b0b, 0x00561653 };
    int i, ok = 0;
    for (i = 0; i < 2; i++) {
        BYTE *p = (BYTE *)sites[i];
        DWORD prot;
        if (memcmp(p, "\xff\x15\x88\xb1\x5e\x00", 6) != 0) { logf("phrase site %08x: unexpected bytes, skipped", sites[i]); continue; }
        if (!VirtualProtect(p, 6, PAGE_EXECUTE_READWRITE, &prot)) continue;
        p[0] = 0xe8;
        *(LONG *)(p + 1) = (LONG)((DWORD)MyPhraseFmt - (sites[i] + 5));
        p[5] = 0x90;
        VirtualProtect(p, 6, prot, &prot);
        FlushInstructionCache(GetCurrentProcess(), p, 6);
        ok++;
    }
    if (ok != 2) logf("phrase order patch: only %d of 2 sites", ok);
}

/* Party roster entry (gather party / create party screen, 0x5be9b0): three lines in a 100 px box (game units),
   name / "%s %d %s" (Level, level, profession) / gender race. "Level 1 Gadgeteer" in Japanese runs 32 px past
   the box and leaves marks outside it when the list scrolls. Move the level to the name line
   ("Sprocket ... Lv1", narrow Latin glyphs) and draw the profession alone on line 2.
   Both lines go through the game's text draw 0x407650(x, y, fmt, ...), cdecl; the name argument is
   entry + 5 and the level is entry + 0x89, so it is read from name + 0x84. */
typedef void (__cdecl *drawtext_t)(int, int, const WCHAR *, ...);
typedef short (__cdecl *textwidth_t)(void *font, int maxlen, const WCHAR *, ...);
#define DRAWTEXT ((drawtext_t)0x00407650)
#define TEXTWIDTH ((textwidth_t)0x00406ea0)   /* width in pixels; the roster font is [0x683660] */
static void __cdecl MyRosterName(int x, int y, const WCHAR *fmt, const WCHAR *name)
{
    /* the name font has no visible blank (U+0020 is zero width, U+2002 shows as 'A'), so the level is a
       second piece of text, right-aligned to the box edge (x + 99, in 640x480 game units) or just after a long name */
    void *font = *(void **)0x00683660;
    int lv = *(const int *)((const BYTE *)name + 0x84);
    int wn = TEXTWIDTH(font, 64, L"%s", name), wl = TEXTWIDTH(font, 16, L"Lv%d", lv);
    int lx = x + 99 - wl;
    if (lx < x + wn + 5) lx = x + wn + 5;
    DRAWTEXT(x, y, L"%s", name);
    DRAWTEXT(lx, y, L"Lv%d", lv);
}
static void __cdecl MyRosterClass(int x, int y, const WCHAR *fmt, const WCHAR *label, int level, const WCHAR *cls)
{
    DRAWTEXT(x, y, L"%s", cls);
}

static void patch_roster(void)
{
    static const struct { DWORD site; void *fn; } s[] = {
        { 0x005bea76, (void *)MyRosterName },
        { 0x005beaa9, (void *)MyRosterClass },
    };
    int i, ok = 0;
    for (i = 0; i < 2; i++) {
        BYTE *p = (BYTE *)s[i].site;
        DWORD prot;
        if (p[0] != 0xe8 || (DWORD)(s[i].site + 5 + *(LONG *)(p + 1)) != 0x00407650) { logf("roster site %08x: unexpected bytes, skipped", s[i].site); continue; }
        if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &prot)) continue;
        *(LONG *)(p + 1) = (LONG)((DWORD)s[i].fn - (s[i].site + 5));
        VirtualProtect(p, 5, prot, &prot);
        FlushInstructionCache(GetCurrentProcess(), p, 5);
        ok++;
    }
    if (ok != 2) logf("roster patch: only %d of 2 sites", ok);
}

/* NSF loader 0x55a140: the narrow copy of a line is malloc(len + 1) ("and eax,0xffff; inc eax; push eax"
   at 0x55a1e3). In code page 932 a Japanese character takes two bytes, so allocate 2 * len + 1. */
static void patch_nsf_alloc(void)
{
    BYTE *cave = (BYTE *)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    BYTE *p = cave;
    if (!cave) return;
    p = emit(p, "\x25\xff\xff\x00\x00", 5);      /* and eax, 0xffff */
    p = emit(p, "\x01\xc0", 2);                  /* add eax, eax */
    p = emit_rel32(p, "\xe9", 1, 0x0055a1e8);   /* -> inc eax; push eax; call malloc */
    FlushInstructionCache(GetCurrentProcess(), cave, p - cave);
    if (hook_jmp(0x0055a1e3, 5, "\x25\xff\xff\x00\x00", cave)) dlog("NSF line buffer: 2 * len + 1");
    else logf("NSF line buffer patch failed");
}

/* Speech bubbles (0x5cf6c0, used for NPC speech and party chatter) are refused (return -1, no bubble)
   when the wrapped text makes the bubble 200px (0xc8) or taller. Japanese glyphs are about twice as
   wide as the average Latin letter, so a long line that fits in English needs more rows. Allow up to
   400px (the 3D view is ~425px). */
static void patch_bubble_height(void)
{
    {
    BYTE *p = (BYTE *)0x005cfbab;
    DWORD prot;
    if (memcmp(p, "\x66\x81\xfe\xc8\x00", 5) != 0) { logf("bubble height: unexpected bytes, skipped"); return; }
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &prot)) return;
    *(WORD *)(p + 3) = 400;
    VirtualProtect(p, 5, prot, &prot);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    dlog("bubble height limit: 200 -> 400");
    }
}

/* ---- Movies ----
   To play a movie the game closes the 3D (SR/Glide) window and switches to DirectDraw 640x480x16
   (0x423390 returns an IDirectDrawSurface2). On current Windows that switch fails and returns NULL.
   The movie player object (0xc bytes, ctor 0x5e2f90) never initializes its surface field +8, and
   0x5e31e0 does not store NULL, so the per-frame code 0x5e3020 took heap garbage for a surface and
   BinkDDSurfaceType crashed (also without the mod).
   Fix: when the Glide renderer is running, keep the Glide window open, report "no surface" (with +8
   cleared), and draw each frame ourselves in place of the software path 0x5e3070: BinkCopyToBuffer
   in RGB565, scale to the Glide screen keeping 4:3, grLfbWriteRegion to the back buffer, grBufferSwap.
   The matching "leave movie mode" 0x4234a0 (which reopens the SR window) is then skipped.
   Without Glide the original DirectDraw path runs; if that fails too the movie is cut to one frame. */
#define ADDR_ENTER_MOVIE  0x00423390u
#define ADDR_LEAVE_MOVIE  0x004234a0u
#define ADDR_DRAW_NOSURF  0x005e3070u
#define G_SR              (*(void **)0x00659634u)
#define G_PLAYER          (*(DWORD **)0x0069c258u)

enum { MOVIE_NORMAL, MOVIE_GLIDE, MOVIE_SKIP };
typedef int  (__stdcall *grLfbWriteRegion_t)(int, unsigned, unsigned, int, unsigned, unsigned, int, void *);
typedef void (__stdcall *grBufferSwap_t)(int);
typedef unsigned (__stdcall *grSstScreen_t)(void);
typedef int  (__stdcall *BinkCopyToBuffer_t)(void *, void *, int, unsigned, unsigned, unsigned, unsigned);

static grLfbWriteRegion_t p_grLfbWriteRegion;
static grBufferSwap_t     p_grBufferSwap;
static grSstScreen_t      p_grSstScreenWidth, p_grSstScreenHeight;
static BinkCopyToBuffer_t p_BinkCopyToBuffer;
static int      g_movie_mode;
static WORD    *g_frame, *g_screen;
static unsigned g_frame_cap, g_screen_cap, g_scr_w, g_scr_h;
static int      g_frames_drawn, g_glide_fault;

static int glide_ready(void)
{
    HMODULE g = GetModuleHandleA("glide2x.dll");
    if (!g || !G_SR) return 0;
    if (!p_grLfbWriteRegion) {
        p_grLfbWriteRegion  = (grLfbWriteRegion_t)GetProcAddress(g, "_grLfbWriteRegion@32");
        p_grBufferSwap      = (grBufferSwap_t)GetProcAddress(g, "_grBufferSwap@4");
        p_grSstScreenWidth  = (grSstScreen_t)GetProcAddress(g, "_grSstScreenWidth@0");
        p_grSstScreenHeight = (grSstScreen_t)GetProcAddress(g, "_grSstScreenHeight@0");
    }
    if (!p_BinkCopyToBuffer)
        p_BinkCopyToBuffer = *(BinkCopyToBuffer_t *)IAT_BinkCopyToBuffer;
    return p_grLfbWriteRegion && p_grBufferSwap && p_grSstScreenWidth && p_grSstScreenHeight
        && p_BinkCopyToBuffer && !g_glide_fault;
}

static WORD *grow(WORD *p, unsigned *cap, unsigned n)
{
    if (n > *cap) {
        free(p);
        p = (WORD *)malloc(n * 2);
        *cap = p ? n : 0;
    }
    return p;
}

/* bilinear scale of an RGB565 image into the centre of a W x H screen, black borders */
static void scale_frame(const WORD *s, unsigned w, unsigned h, WORD *d, unsigned W, unsigned H)
{
    static unsigned short xs0[4096], xs1[4096], xa[4096];
    unsigned dw, dh, ox, oy, x, y;
    if ((unsigned __int64)W * h >= (unsigned __int64)H * w) { dh = H; dw = w * H / h; }
    else { dw = W; dh = h * W / w; }
    if (dw > 4096) dw = 4096;
    ox = (W - dw) / 2; oy = (H - dh) / 2;
    memset(d, 0, W * H * 2);
    for (x = 0; x < dw; x++) {
        int f = (int)(((2 * x + 1) * (unsigned __int64)w * 256) / (2 * dw)) - 128;
        if (f < 0) f = 0;
        xs0[x] = (unsigned short)(f >> 8); xa[x] = (unsigned short)(f & 255);
        if (xs0[x] >= w) xs0[x] = (unsigned short)(w - 1);
        xs1[x] = (unsigned short)(xs0[x] + 1 < w ? xs0[x] + 1 : w - 1);
    }
    for (y = 0; y < dh; y++) {
        int f = (int)(((2 * y + 1) * (unsigned __int64)h * 256) / (2 * dh)) - 128;
        unsigned y0, y1, ay;
        const WORD *r0, *r1;
        WORD *o = d + (oy + y) * W + ox;
        if (f < 0) f = 0;
        y0 = f >> 8; ay = f & 255;
        if (y0 >= h) y0 = h - 1;
        y1 = y0 + 1 < h ? y0 + 1 : h - 1;
        r0 = s + y0 * w; r1 = s + y1 * w;
        for (x = 0; x < dw; x++) {
            unsigned a = xa[x], p00 = r0[xs0[x]], p01 = r0[xs1[x]], p10 = r1[xs0[x]], p11 = r1[xs1[x]];
            unsigned w00 = (256 - a) * (256 - ay), w01 = a * (256 - ay), w10 = (256 - a) * ay, w11 = a * ay;
            unsigned r = ((p00 >> 11) * w00 + (p01 >> 11) * w01 + (p10 >> 11) * w10 + (p11 >> 11) * w11) >> 16;
            unsigned g = (((p00 >> 5) & 63) * w00 + ((p01 >> 5) & 63) * w01 + ((p10 >> 5) & 63) * w10
                          + ((p11 >> 5) & 63) * w11) >> 16;
            unsigned b = ((p00 & 31) * w00 + (p01 & 31) * w01 + (p10 & 31) * w10 + (p11 & 31) * w11) >> 16;
            o[x] = (WORD)((r << 11) | (g << 5) | b);
        }
    }
}

static void glide_present(void)
{
    __try {
        p_grLfbWriteRegion(1 /* GR_BUFFER_BACKBUFFER */, 0, 0, 0 /* GR_LFB_SRC_FMT_565 */,
                           g_scr_w, g_scr_h, g_scr_w * 2, g_screen);
        p_grBufferSwap(1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_glide_fault = 1;
        logf("movie: Glide write faulted, movies disabled");
    }
}

/* ---- Movie subtitles: wiz8jp.sub next to Wiz8.exe (UTF-8, built by tools/movie_subs.py).
   "@name" starts a movie (Bink file name, lower case, no extension), then "start_ms end_ms text"
   lines, "|" = line break. Drawn with a Windows font (white, black outline) into the bottom of the
   frame before it is written to Glide. The time is the Bink frame number / frame rate. */
typedef struct { char key[24]; DWORD a, b; WCHAR *text; } Sub;
static Sub     *g_subs;
static int      g_nsubs, g_sub_lo, g_sub_hi, g_sub_shown = -1, g_sub_h;
static BYTE    *g_sub_fill, *g_sub_edge;
static unsigned g_sub_cap;

static void load_subs(void)
{
    char path[MAX_PATH], line[2048], key[24] = "";
    FILE *f;
    int cap = 0;
    _snprintf(path, sizeof(path), "%s\\wiz8jp.sub", g_dir);
    f = fopen(path, "rb");
    if (!f) { logf("wiz8jp.sub not found, no movie subtitles"); return; }
    while (fgets(line, sizeof(line), f)) {
        char *p = line, *t;
        size_t len = strlen(line);
        DWORD a, b;
        int n;
        WCHAR *w;
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
        if (line[0] == '@') { lstrcpynA(key, line + 1, sizeof(key)); _strlwr(key); continue; }
        if (line[0] < '0' || line[0] > '9') continue;
        a = strtoul(p, &p, 10);
        b = strtoul(p, &p, 10);
        if (*p == ' ') p++;
        for (t = p; *t; t++) if (*t == '|') *t = '\n';
        n = MultiByteToWideChar(CP_UTF8, 0, p, -1, NULL, 0);
        if (n <= 1 || b <= a) continue;
        w = (WCHAR *)malloc(n * sizeof(WCHAR));
        if (!w) break;
        MultiByteToWideChar(CP_UTF8, 0, p, -1, w, n);
        if (g_nsubs == cap) {
            Sub *ns = (Sub *)realloc(g_subs, (cap ? cap * 2 : 256) * sizeof(Sub));
            if (!ns) { free(w); break; }
            g_subs = ns; cap = cap ? cap * 2 : 256;
        }
        lstrcpynA(g_subs[g_nsubs].key, key, sizeof(g_subs[g_nsubs].key));
        g_subs[g_nsubs].a = a; g_subs[g_nsubs].b = b; g_subs[g_nsubs].text = w;
        g_nsubs++;
    }
    fclose(f);
    dlog("loaded %d movie subtitles", g_nsubs);
}

static void select_subs(const char *name)
{
    char key[MAX_PATH];
    const char *s = name, *q;
    char *dot;
    int i;
    g_sub_lo = g_sub_hi = 0; g_sub_shown = -1;
    if (!name) return;
    for (q = name; *q; q++) if (*q == '\\' || *q == '/') s = q + 1;
    lstrcpynA(key, s, sizeof(key));
    dot = strrchr(key, '.');
    if (dot) *dot = 0;
    _strlwr(key);
    for (i = 0; i < g_nsubs && strcmp(g_subs[i].key, key) != 0; i++) ;
    g_sub_lo = i;
    while (i < g_nsubs && strcmp(g_subs[i].key, key) == 0) i++;
    g_sub_hi = i;
}

/* text -> coverage maps (fill, and fill dilated by r for the outline), g_scr_w x g_sub_h */
static void render_sub(const WCHAR *text)
{
    int W = (int)g_scr_w, fh = (int)g_scr_h * 26 / 600, r = fh >= 22 ? 2 : 1, H, x, y, i;
    RECT rc;
    HDC dc;
    HFONT font;
    HGDIOBJ ofont, obm;
    HBITMAP bm;
    BITMAPINFO bi;
    DWORD *bits = NULL;
    BYTE *tmp;
    g_sub_h = 0;
    if (fh < 12) fh = 12;
    dc = CreateCompatibleDC(NULL);
    if (!dc) return;
    font = CreateFontW(-fh, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
    ofont = SelectObject(dc, font);
    SetRect(&rc, 0, 0, W - 4 * r, 0);
    DrawTextW(dc, text, -1, &rc, DT_CENTER | DT_NOPREFIX | DT_WORDBREAK | DT_CALCRECT);
    H = rc.bottom + 2 * r + 2;
    if (H > (int)g_scr_h / 2) H = (int)g_scr_h / 2;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = W; bi.bmiHeader.biHeight = -H;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    bm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    if (bm && bits && (unsigned)(W * H) <= 0x1000000) {
        if ((unsigned)(W * H) > g_sub_cap) {
            free(g_sub_fill); free(g_sub_edge);
            g_sub_fill = (BYTE *)malloc(W * H); g_sub_edge = (BYTE *)malloc(W * H);
            g_sub_cap = (g_sub_fill && g_sub_edge) ? (unsigned)(W * H) : 0;
        }
        tmp = (BYTE *)malloc(W * H);
        if (g_sub_cap && tmp) {
            obm = SelectObject(dc, bm);
            memset(bits, 0, W * H * 4);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(255, 255, 255));
            SetRect(&rc, 2 * r, r + 1, W - 2 * r, H);
            DrawTextW(dc, text, -1, &rc, DT_CENTER | DT_NOPREFIX | DT_WORDBREAK);
            GdiFlush();
            for (i = 0; i < W * H; i++) g_sub_fill[i] = (BYTE)(bits[i] >> 8);
            for (y = 0; y < H; y++)            /* square dilation, horizontal then vertical */
                for (x = 0; x < W; x++) {
                    int m = 0, k;
                    for (k = x - r; k <= x + r; k++)
                        if (k >= 0 && k < W && g_sub_fill[y * W + k] > m) m = g_sub_fill[y * W + k];
                    tmp[y * W + x] = (BYTE)m;
                }
            for (y = 0; y < H; y++)
                for (x = 0; x < W; x++) {
                    int m = 0, k;
                    for (k = y - r; k <= y + r; k++)
                        if (k >= 0 && k < H && tmp[k * W + x] > m) m = tmp[k * W + x];
                    g_sub_edge[y * W + x] = (BYTE)m;
                }
            g_sub_h = H;
            SelectObject(dc, obm);
        }
        free(tmp);
    }
    if (bm) DeleteObject(bm);
    SelectObject(dc, ofont);
    DeleteObject(font);
    DeleteDC(dc);
}

static void draw_sub(void)
{
    int W = (int)g_scr_w, y0 = (int)g_scr_h - g_sub_h - (int)g_scr_h / 30, x, y;
    if (y0 < 0) y0 = 0;
    for (y = 0; y < g_sub_h && y0 + y < (int)g_scr_h; y++) {
        WORD *row = g_screen + (y0 + y) * W;
        const BYTE *e = g_sub_edge + y * W, *f = g_sub_fill + y * W;
        for (x = 0; x < W; x++) {
            unsigned p, r, g, b, k, a;
            if (!e[x]) continue;
            p = row[x]; k = 255 - e[x]; a = f[x];
            r = (p >> 11) * k / 255; g = ((p >> 5) & 63) * k / 255; b = (p & 31) * k / 255;
            r += (31 - r) * a / 255; g += (63 - g) * a / 255; b += (31 - b) * a / 255;
            row[x] = (WORD)((r << 11) | (g << 5) | b);
        }
    }
}

static void movie_subtitle(const DWORD *bink)
{
    DWORD rate = bink[5], div = bink[6], fn = bink[3] ? bink[3] - 1 : 0, t;
    int i, cur = -1;
    if (g_sub_hi <= g_sub_lo) return;
    if (!rate || !div || rate / div > 120) { rate = 30; div = 1; }
    t = (DWORD)((unsigned __int64)fn * 1000 * div / rate);
    for (i = g_sub_lo; i < g_sub_hi; i++)
        if (t >= g_subs[i].a && t < g_subs[i].b) { cur = i; break; }
    if (cur != g_sub_shown) {
        g_sub_shown = cur;
        if (cur >= 0) render_sub(g_subs[cur].text);
    }
    if (cur >= 0 && g_sub_h) draw_sub();
}

/* replaces "call 0x423390" at 0x5ae67e (the player object is already in [0x69c258]) */
static void *__cdecl MyEnterMovie(void)
{
    DWORD *pl = G_PLAYER;
    void *s;
    if (pl) pl[2] = 0;
    g_frames_drawn = 0;
    if (glide_ready()) {
        g_scr_w = p_grSstScreenWidth();
        g_scr_h = p_grSstScreenHeight();
        if (g_scr_w >= 64 && g_scr_h >= 48 && g_scr_w <= 8192 && g_scr_h <= 8192) {
            g_movie_mode = MOVIE_GLIDE;
            dlog("movie: drawing through Glide %ux%u", g_scr_w, g_scr_h);
            return NULL;
        }
    }
    s = ((void *(__cdecl *)(void))ADDR_ENTER_MOVIE)();
    g_movie_mode = s ? MOVIE_NORMAL : MOVIE_SKIP;
    if (!s) logf("movie: no Glide and DirectDraw failed, movie skipped");
    return s;
}

/* replaces "call 0x4234a0" at 0x5ae834, 0x5ae84d, 0x5ae94a */
static void __cdecl MyLeaveMovie(void)
{
    if (g_movie_mode == MOVIE_GLIDE) {
        g_movie_mode = MOVIE_NORMAL;
        dlog("movie: done, %d frames drawn", g_frames_drawn);
        if (g_screen && !g_glide_fault) {
            memset(g_screen, 0, g_scr_w * g_scr_h * 2);
            glide_present();
            glide_present();
        }
        return;
    }
    g_movie_mode = MOVIE_NORMAL;
    ((void (__cdecl *)(void))ADDR_LEAVE_MOVIE)();
}

/* replaces "call 0x5e3070" at 0x5e304d (thiscall, no stack arguments, returns al) */
static char __fastcall MyDrawFrame(DWORD *pl)
{
    DWORD *bink;
    unsigned w, h;
    if (g_movie_mode == MOVIE_SKIP) return 1;
    if (g_movie_mode != MOVIE_GLIDE || g_glide_fault)
        return g_movie_mode == MOVIE_GLIDE ? 1 : ((char (__fastcall *)(DWORD *))ADDR_DRAW_NOSURF)(pl);
    bink = (DWORD *)pl[0];
    if (!bink) return 0;
    w = bink[0]; h = bink[1];
    if (!w || !h || w > 4096 || h > 4096) return 0;
    g_frame = grow(g_frame, &g_frame_cap, w * h);
    g_screen = grow(g_screen, &g_screen_cap, g_scr_w * g_scr_h);
    if (!g_frame || !g_screen) return 0;
    p_BinkCopyToBuffer(bink, g_frame, w * 2, h, 0, 0, 10 /* BINKSURFACE565 */);
    scale_frame(g_frame, w, h, g_screen, g_scr_w, g_scr_h);
    movie_subtitle(bink);
    glide_present();
    g_frames_drawn++;
    return 1;
}

static void patch_movies(void)
{
    int n = 0;
    n += patch_call(0x005ae67eu, (void *)MyEnterMovie, ADDR_ENTER_MOVIE);
    n += patch_call(0x005ae834u, (void *)MyLeaveMovie, ADDR_LEAVE_MOVIE);
    n += patch_call(0x005ae84du, (void *)MyLeaveMovie, ADDR_LEAVE_MOVIE);
    n += patch_call(0x005ae94au, (void *)MyLeaveMovie, ADDR_LEAVE_MOVIE);
    n += patch_call(0x005e304du, (void *)MyDrawFrame, ADDR_DRAW_NOSURF);
    if (n != 5) logf("movie patch: only %d of 5 sites", n);
}

/* BINK header: Width, Height, Frames, FrameNum, ... (u32 each). When a movie cannot be shown
   (MOVIE_SKIP) it is cut to one frame so the game does not sit on a black screen. */
static void *__stdcall MyBinkOpen(const char *name, unsigned int flags)
{
    DWORD *b;
    if (!o_BinkOpen) return NULL;
    b = (DWORD *)o_BinkOpen(name, flags);
    if (b) {
        select_subs(name);
        dlog("BinkOpen(%s): %lux%lu, %lu frames at %lu/%lu, %d subtitles%s", name ? name : "(null)",
             b[0], b[1], b[2], b[5], b[6], g_sub_hi - g_sub_lo, g_movie_mode == MOVIE_SKIP ? " -> 1" : "");
        if (g_movie_mode == MOVIE_SKIP) b[2] = 1;
    }
    return b;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        char *s;
        DisableThreadLibraryCalls(inst);
        GetModuleFileNameA(NULL, g_dir, sizeof(g_dir));
        s = strrchr(g_dir, '\\');
        if (s) *s = 0;
        dlog("---- wiz8jp hook loaded");
        load_chars();
        if (patch_call(ADDR_CALL_CREATE_TABLE, (void *)MyCreateTable, ADDR_CREATE_TABLE))
            dlog("patched CreateTransTable call");
        /* widen_format_spaces(); no longer needed: U+0020 keeps its width */
        patch_separators();
        patch_word_spaces();
        replace_words();
        patch_skillup_spaces();
        patch_join_formats();
        patch_wrap();
        patch_bubble_height();
        patch_nsf_alloc();
        patch_phrase_order();
        patch_roster();
        patch_movies();
        load_subs();
        if (!iat_hook(IAT_BinkOpen, (void *)MyBinkOpen, (void **)&o_BinkOpen)
            || !iat_hook(IAT_BinkDDSurfaceType, (void *)MyBinkDDSurfaceType, (void **)&o_BinkDDSurfaceType))
            logf("Bink import hooks failed");
        vfile_init();
    }
    return TRUE;
}
