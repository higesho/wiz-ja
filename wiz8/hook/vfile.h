/* ---- Virtual files: the Japanese data is applied in memory, no game file is changed ----
   wiz8jp.dat (built by tools/build_patch.py, format in tools/patchdat.py) holds, for each translated
   game file, an edit script against the original: "copy N bytes from offset X" / "insert these bytes".
   The game reads every data file through its JA2-style file manager:
     FileOpen 0x404c80, FileClose 0x404e10, FileRead 0x404ea0, FileSeek 0x405030, FileGetPos 0x4050d0,
     FileGetSize 0x405150, FileCheckEndOfFile 0x405570, FileGetFileTime 0x405630
   (handle = library id << 22 | file id). When the game opens a file that has a script, MyFileOpen reads
   the original through the real functions, checks its size and CRC-32, builds the Japanese version (cached)
   and returns a handle of our own (library id 0x3f0) that the other hooked functions serve from memory.
   A file whose original does not match (another game version) is left in English, with one log line.
   A name can have several scripts, one per known original (the Fan Patch 1.28 ships its own fonts in
   Data\FanPatch.Dat): the one whose size and CRC match is used. */

#define FM_OPEN   0x00404c80u
#define FM_CLOSE  0x00404e10u
#define FM_READ   0x00404ea0u
#define FM_SEEK   0x00405030u
#define FM_GETPOS 0x004050d0u
#define FM_SIZE   0x00405150u
#define FM_EOF    0x00405570u
#define FM_FTIME  0x00405630u

#define VF_LIB    0x3f0u
#define VF_MAX    64

typedef DWORD (__cdecl *FileOpen_t)(const char *, DWORD, DWORD);
typedef void  (__cdecl *FileClose_t)(DWORD);
typedef BYTE  (__cdecl *FileRead_t)(DWORD, void *, DWORD, DWORD *);
typedef BYTE  (__cdecl *FileSeek_t)(DWORD, DWORD, DWORD);
typedef DWORD (__cdecl *FileGetPos_t)(DWORD);
typedef DWORD (__cdecl *FileGetSize_t)(DWORD);
typedef BYTE  (__cdecl *FileEof_t)(DWORD);
typedef BYTE  (__cdecl *FileTime_t)(DWORD, void *, void *, void *);

static FileOpen_t    t_FileOpen;
static FileClose_t   t_FileClose;
static FileRead_t    t_FileRead;
static FileSeek_t    t_FileSeek;
static FileGetPos_t  t_FileGetPos;
static FileGetSize_t t_FileGetSize;
static FileEof_t     t_FileEof;
static FileTime_t    t_FileTime;

typedef struct {
    char        name[96];        /* lower case, relative to Data\ */
    DWORD       osize, ocrc, nsize, nops;
    const BYTE *ops;
    BYTE       *built;           /* Japanese version, built on first open */
    int         bad;             /* original did not match: serve the original */
} DatFile;

typedef struct { int used; DatFile *f; DWORD pos; } VHandle;

static BYTE    *g_dat;
static DatFile *g_df;
static int      g_ndf;
static VHandle  g_vh[VF_MAX];
static CRITICAL_SECTION g_vcs;

static DWORD crc32_buf(const BYTE *p, DWORD n)
{
    static DWORD tab[256];
    DWORD c = 0xffffffffu, i, k;
    if (!tab[1])
        for (i = 0; i < 256; i++) {
            DWORD v = i;
            for (k = 0; k < 8; k++) v = (v & 1) ? 0xedb88320u ^ (v >> 1) : v >> 1;
            tab[i] = v;
        }
    for (i = 0; i < n; i++) c = tab[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

static void norm_name(const char *in, char *out, int cap)
{
    int i = 0;
    const char *s = in;
    while (s[0] == '.' && (s[1] == '\\' || s[1] == '/')) s += 2;
    for (; *s && i < cap - 1; s++) out[i++] = (char)(*s == '/' ? '\\' : (*s >= 'A' && *s <= 'Z' ? *s + 32 : *s));
    out[i] = 0;
    if (strncmp(out, "data\\", 5) == 0) memmove(out, out + 5, strlen(out + 5) + 1);
}

/* the first entry with this key; *count = how many there are (consecutive) */
static DatFile *find_key(const char *n, int *count)
{
    int lo = 0, hi = g_ndf - 1;
    while (lo <= hi) {                     /* g_df is sorted by name (build_patch.py writes it sorted) */
        int m = (lo + hi) / 2, c = strcmp(n, g_df[m].name);
        if (!c) {
            int a = m, b = m;
            while (a > 0 && !strcmp(g_df[a - 1].name, n)) a--;
            while (b + 1 < g_ndf && !strcmp(g_df[b + 1].name, n)) b++;
            *count = b - a + 1;
            return &g_df[a];
        }
        if (c < 0) hi = m - 1; else lo = m + 1;
    }
    return NULL;
}

/* the first script for this game file; *count = how many there are */
static DatFile *find_dat(const char *name, int *count)
{
    char n[MAX_PATH];
    norm_name(name, n, sizeof(n));
    return find_key(n, count);
}

/* the data of an '@' entry (one insert op) */
static const BYTE *payload(const DatFile *f, DWORD *len)
{
    if (f->nops != 1 || f->ops[0] != 1) return NULL;
    *len = *(const DWORD *)(f->ops + 1);
    return f->ops + 5;
}

/* ---- Fonts: the Japanese glyphs are appended to whatever version of a font the game reads ----
   wiz8jp.dat holds glyph packs (@pack\<id>, tools/glyphpack.py) and, per font name, the pack to use
   (@glyphs\<font>, one per known original; the one made from the font being read is preferred, else the
   first). graft_font keeps sub-images 0..251 of the font (blank 1x1 glyphs if it has fewer), then appends
   the pack: for the original a pack was made from, the result is the font mkfont.add_japanese builds. */
#define BASE_GLYPHS 252
static BYTE *graft_font(const BYTE *d, DWORD size, const BYTE *pack, DWORD plen, DWORD *outsize)
{
    DWORD stored, flags, ncol, app, base, i, off = 0, n, q, keep, total, pos;
    WORD nsub;
    BYTE *out, *h;
    static const BYTE pad[2] = { 0x81, 0x00 };
    if (size < 64 || memcmp(d, "STCI", 4) || plen < 4) return NULL;
    stored = *(const DWORD *)(d + 8); flags = *(const DWORD *)(d + 16);
    ncol = *(const DWORD *)(d + 24); nsub = *(const WORD *)(d + 28); app = *(const DWORD *)(d + 48);
    if (!(flags & 0x08) || !(flags & 0x20) || ncol > 256) return NULL;
    base = 64 + ncol * 3 + nsub * 16;
    if (base > size || base + stored + app > size) return NULL;
    n = *(const DWORD *)pack;
    if (4 + n * 12 > plen) return NULL;
    keep = nsub < BASE_GLYPHS ? nsub : BASE_GLYPHS;
    total = 0;
    for (i = 0; i < keep; i++) {
        const BYTE *e = d + 64 + ncol * 3 + i * 16;
        if (*(const DWORD *)e + *(const DWORD *)(e + 4) > stored) return NULL;
        total += *(const DWORD *)(e + 4);
    }
    total += (BASE_GLYPHS - keep) * 2 + (plen - 4 - n * 12);
    *outsize = 64 + ncol * 3 + (BASE_GLYPHS + n) * 16 + total + app;
    out = (BYTE *)malloc(*outsize);
    if (!out) return NULL;
    memcpy(out, d, 64 + ncol * 3);                          /* header, palette */
    h = out + 64 + ncol * 3;                                /* sub-image headers */
    pos = 64 + ncol * 3 + (BASE_GLYPHS + n) * 16;           /* pixel data */
    for (i = 0; i < BASE_GLYPHS; i++, h += 16) {
        DWORD len;
        if (i < keep) {
            const BYTE *e = d + 64 + ncol * 3 + i * 16;
            len = *(const DWORD *)(e + 4);
            memcpy(h, e, 16);
            memcpy(out + pos, d + base + *(const DWORD *)e, len);
        } else {
            len = 2;
            memset(h, 0, 16);
            *(WORD *)(h + 12) = 1; *(WORD *)(h + 14) = 1;   /* h = 1, w = 1 */
            memcpy(out + pos, pad, 2);
        }
        *(DWORD *)h = off; *(DWORD *)(h + 4) = len;
        off += len; pos += len;
    }
    q = 4 + n * 12;
    for (i = 0; i < n; i++, h += 16) {
        const BYTE *e = pack + 4 + i * 12;                  /* i16 offx, i16 offy, u16 h, u16 w, u32 len */
        DWORD len = *(const DWORD *)(e + 8);
        if (q + len > plen) { free(out); return NULL; }
        *(DWORD *)h = off; *(DWORD *)(h + 4) = len;
        memcpy(h + 8, e, 8);
        memcpy(out + pos, pack + q, len);
        off += len; pos += len; q += len;
    }
    memcpy(out + pos, d + base + stored, app);              /* app data */
    *(DWORD *)(out + 8) = off;
    *(WORD *)(out + 28) = (WORD)(BASE_GLYPHS + n);
    return out;
}

/* g: the n @glyphs entries for this font. Reads the font, appends the pack; the result is kept in the entry */
static DatFile *build_font(DatFile *g, int nalt, const char *name, DWORD flags, DWORD del, FileOpen_t open)
{
    DWORD h, got = 0, size, crc, plen, idlen, outsize;
    BYTE *orig, *out;
    const BYTE *id, *pack;
    char key[96];
    DatFile *f = g, *pk;
    int k, np;
    h = open(name, flags, del);
    if (!h) return NULL;
    size = t_FileGetSize(h);
    orig = (BYTE *)malloc(size ? size : 1);
    if (!orig) { t_FileClose(h); return NULL; }
    if (!t_FileRead(h, orig, size, &got) || got != size) { t_FileClose(h); free(orig); return NULL; }
    t_FileClose(h);
    crc = crc32_buf(orig, size);
    for (k = 0; k < nalt; k++)
        if (g[k].osize == size && g[k].ocrc == crc) { f = &g[k]; break; }
    id = payload(f, &idlen);
    if (!id || idlen >= sizeof(key)) { free(orig); return NULL; }
    memcpy(key, id, idlen); key[idlen] = 0;
    pk = find_key(key, &np);
    pack = pk ? payload(pk, &plen) : NULL;
    out = pack ? graft_font(orig, size, pack, plen, &outsize) : NULL;
    free(orig);
    if (!out) {
        for (k = 0; k < nalt; k++) g[k].bad = 1;
        logf("%s: the Japanese glyphs could not be added to this font", g->name);
        return NULL;
    }
    if (k == nalt) dlog("%s: glyphs added to a font version made for another one", g->name);
    f->built = out;
    f->nsize = outsize;
    return f;
}

/* read the original through the game's own functions and apply the script made for it.
   g is the first of the n scripts for this name; returns the one that was built, or NULL */
static DatFile *build_file(DatFile *g, int nalt, const char *name, DWORD flags, DWORD del, FileOpen_t open)
{
    DWORD h, got = 0, i, pos = 0, size, crc;
    BYTE *orig, *out;
    const BYTE *p;
    DatFile *f = NULL;
    int k;
    h = open(name, flags, del);
    if (!h) return NULL;
    size = t_FileGetSize(h);
    orig = (BYTE *)malloc(size ? size : 1);
    if (!orig) { t_FileClose(h); return NULL; }
    if (!t_FileRead(h, orig, size, &got) || got != size) { t_FileClose(h); free(orig); return NULL; }
    t_FileClose(h);
    crc = crc32_buf(orig, size);
    for (k = 0; k < nalt; k++)
        if (g[k].osize == size && g[k].ocrc == crc) { f = &g[k]; break; }
    if (!f) {
        free(orig);
        for (k = 0; k < nalt; k++) g[k].bad = 1;
        logf("%s: the game file differs from the one the translation was made for; left in English", g->name);
        return NULL;
    }
    p = f->ops;
    out = (BYTE *)malloc(f->nsize ? f->nsize : 1);
    if (!out) { free(orig); return NULL; }
    for (i = 0; i < f->nops; i++) {
        DWORD a, b;
        if (*p++ == 0) {
            a = *(const DWORD *)p; b = *(const DWORD *)(p + 4); p += 8;
            if (a + b > f->osize || pos + b > f->nsize) break;
            memcpy(out + pos, orig + a, b);
        } else {
            b = *(const DWORD *)p; p += 4;
            if (pos + b > f->nsize) break;
            memcpy(out + pos, p, b); p += b;
        }
        pos += b;
    }
    free(orig);
    if (i != f->nops || pos != f->nsize) {
        free(out);
        for (k = 0; k < nalt; k++) g[k].bad = 1;
        logf("%s: wiz8jp.dat is damaged; left in English", f->name);
        return NULL;
    }
    f->built = out;
    return f;
}

static VHandle *vh_of(DWORD h)
{
    DWORD i = (h & 0x3fffffu) - 1;
    if ((h >> 22) != VF_LIB || i >= VF_MAX || !g_vh[i].used) return NULL;
    return &g_vh[i];
}

/* open: the function that really opens the file (the game's FileOpen, or the Fan Patch's copy of it) */
static DWORD vopen(const char *name, DWORD flags, DWORD del, FileOpen_t open)
{
    DatFile *g = NULL, *f = NULL;
    DWORD r = 0;
    int i, n = 0, font = 0;
    char key[MAX_PATH + 16];
    /* read-only opens (FILE_ACCESS_READ = 1, no write bit 2) of files we have a script or glyphs for */
    if (g_ndf && name && (flags & 3) == 1) {
        g = find_dat(name, &n);
        if (!g) {
            strcpy(key, "@glyphs\\");
            norm_name(name, key + 8, MAX_PATH);
            g = find_key(key, &n);
            font = 1;
        }
    }
    if (!g || g->bad) {
#ifdef JP_DEBUG_FILES
        dlog("open (as is) %s flags %x", name ? name : "(null)", flags);
#endif
        return open(name, flags, del);
    }
    EnterCriticalSection(&g_vcs);
    for (i = 0; i < n; i++) if (g[i].built) f = &g[i];
    if (f || (f = (font ? build_font : build_file)(g, n, name, flags, del, open)) != NULL) {
        for (i = 0; i < VF_MAX && g_vh[i].used; i++) ;
        if (i < VF_MAX) {
            g_vh[i].used = 1; g_vh[i].f = f; g_vh[i].pos = 0;
            r = (VF_LIB << 22) | (DWORD)(i + 1);
        } else logf("too many open translated files");
    }
    LeaveCriticalSection(&g_vcs);
    if (!r) r = open(name, flags, del);
    else dlog("vfile open %s", name);
    return r;
}

/* Fan Patch 1.28: its CFAgent redirects about two dozen FileOpen calls (NPC scripts, messages, quotes ...)
   to its own copy of FileOpen (Unicode file names), which our hook at the start of FileOpen never sees.
   Once that has happened (it is done at start-up, after this DLL is loaded), hook the copy as well.
   The NPC script loader's call (0x55a48e) tells where it is. */
static FileOpen_t t_AltOpen;
static LONG g_alt_state;          /* 0 = not yet, 1 = done (or given up) */
static void *inline_hook(DWORD at, const char *expect, int n, void *fn);

static DWORD __cdecl MyAltOpen(const char *name, DWORD flags, DWORD del)
{
    return vopen(name, flags, del, t_AltOpen);
}

static void try_hook_alt_open(void)
{
    BYTE *p;
    DWORD t;
    if (!g_fanpatch || g_alt_state) return;
    p = (BYTE *)A(0x0055a48eu);
    if (!p || p[0] != 0xe8) { g_alt_state = 1; return; }
    t = (DWORD)p + 5 + *(LONG *)(p + 1);
    if (t == A(FM_OPEN)) return;                   /* not redirected yet */
    if (InterlockedExchange(&g_alt_state, 1)) return;
    t_AltOpen = (FileOpen_t)inline_hook(t, "\x81\xec\x08\x04\x00\x00", 6, (void *)MyAltOpen);
    if (!t_AltOpen) logf("Fan Patch file open at %08x not hooked: NPC dialogue stays in English", t);
    else dlog("Fan Patch file open at %08x hooked", t);
}

static DWORD __cdecl MyFileOpen(const char *name, DWORD flags, DWORD del)
{
    try_hook_alt_open();
    return vopen(name, flags, del, t_FileOpen);
}

static void __cdecl MyFileClose(DWORD h)
{
    VHandle *v = vh_of(h);
    if (!v) { t_FileClose(h); return; }
    EnterCriticalSection(&g_vcs);
    v->used = 0;
    LeaveCriticalSection(&g_vcs);
}

static BYTE __cdecl MyFileRead(DWORD h, void *buf, DWORD len, DWORD *read)
{
    VHandle *v = vh_of(h);
    DWORD n;
    if (!v) return t_FileRead(h, buf, len, read);
    n = v->pos < v->f->nsize ? v->f->nsize - v->pos : 0;
    if (n > len) n = len;
    memcpy(buf, v->f->built + v->pos, n);
    v->pos += n;
    if (read) *read = n;
    return (BYTE)(n == len);
}

/* how: 1 = from start, 2 = back from the end, otherwise from the current position (as 0x405030) */
static BYTE __cdecl MyFileSeek(DWORD h, DWORD dist, DWORD how)
{
    VHandle *v = vh_of(h);
    LONG np;
    if (!v) return t_FileSeek(h, dist, how);
    how &= 0xff;
    if (how == 1) np = (LONG)dist;
    else if (how == 2) np = (LONG)v->f->nsize - ((LONG)dist > 0 ? (LONG)dist : -(LONG)dist);
    else np = (LONG)v->pos + (LONG)dist;
    if (np < 0) return 0;
    v->pos = (DWORD)np;
    return 1;
}

static DWORD __cdecl MyFileGetPos(DWORD h)
{
    VHandle *v = vh_of(h);
    return v ? v->pos : t_FileGetPos(h);
}

static DWORD __cdecl MyFileGetSize(DWORD h)
{
    VHandle *v = vh_of(h);
    return v ? v->f->nsize : t_FileGetSize(h);
}

static BYTE __cdecl MyFileEof(DWORD h)
{
    VHandle *v = vh_of(h);
    return v ? (BYTE)(v->pos >= v->f->nsize) : t_FileEof(h);
}

static BYTE __cdecl MyFileTime(DWORD h, void *c, void *a, void *w)
{
    VHandle *v = vh_of(h);
    if (!v) return t_FileTime(h, c, a, w);
    if (c) memset(c, 0, 8);
    if (a) memset(a, 0, 8);
    if (w) memset(w, 0, 8);
    return 0;
}

/* jump from the start of a game function to fn; returns a trampoline that runs the original */
static void *inline_hook(DWORD at, const char *expect, int n, void *fn)
{
    BYTE *p, *t;
    DWORD prot;
    int i;
    at = A(at);
    if (!at) return NULL;
    p = (BYTE *)at;
    if (memcmp(p, expect, n) != 0) { logf("hook at %08x: unexpected bytes, skipped", at); return NULL; }
    t = (BYTE *)VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!t) return NULL;
    memcpy(t, p, n);
    t[n] = 0xe9;
    *(LONG *)(t + n + 1) = (LONG)((at + n) - ((DWORD)t + n + 5));
    if (!VirtualProtect(p, n, PAGE_EXECUTE_READWRITE, &prot)) return NULL;
    p[0] = 0xe9;
    *(LONG *)(p + 1) = (LONG)((DWORD)fn - (at + 5));
    for (i = 5; i < n; i++) p[i] = 0x90;
    VirtualProtect(p, n, prot, &prot);
    FlushInstructionCache(GetCurrentProcess(), p, n);
    return t;
}

static int load_dat(void)
{
    char path[MAX_PATH];
    FILE *fp;
    long size;
    DWORD n, i;
    const BYTE *p, *end;
    _snprintf(path, sizeof(path), "%s\\wiz8jp.dat", g_dir);
    fp = fopen(path, "rb");
    if (!fp) { logf("wiz8jp.dat not found: the game text stays in English"); return 0; }
    fseek(fp, 0, SEEK_END); size = ftell(fp); fseek(fp, 0, SEEK_SET);
    g_dat = (BYTE *)malloc(size > 0 ? size : 1);
    if (!g_dat || fread(g_dat, 1, size, fp) != (size_t)size || size < 12 || memcmp(g_dat, "W8JP", 4) != 0
        || *(DWORD *)(g_dat + 4) != 1) {
        fclose(fp);
        logf("wiz8jp.dat cannot be read");
        return 0;
    }
    fclose(fp);
    n = *(DWORD *)(g_dat + 8);
    g_df = (DatFile *)calloc(n ? n : 1, sizeof(DatFile));
    if (!g_df) return 0;
    p = g_dat + 12; end = g_dat + size;
    for (i = 0; i < n; i++) {
        DatFile *f = &g_df[i];
        WORD L;
        DWORD k;
        if (p + 2 > end) break;
        L = *(const WORD *)p; p += 2;
        if (L >= sizeof(f->name) || p + L + 16 > end) break;
        memcpy(f->name, p, L); f->name[L] = 0; p += L;
        f->osize = ((const DWORD *)p)[0]; f->ocrc = ((const DWORD *)p)[1];
        f->nsize = ((const DWORD *)p)[2]; f->nops = ((const DWORD *)p)[3]; p += 16;
        f->ops = p;
        for (k = 0; k < f->nops && p < end; k++)
            p += (*p == 0) ? 9 : 5 + *(const DWORD *)(p + 1);
        if (p > end) break;
    }
    if (i != n) { logf("wiz8jp.dat is damaged"); free(g_df); g_df = NULL; return 0; }
    g_ndf = (int)n;
    return 1;
}

#ifdef JP_DEBUG_FILES
typedef int (__cdecl *nsfload_t)(const char *, void *);
static nsfload_t t_NsfLoad;
static int __cdecl MyNsfLoad(const char *name, void *a2)
{
    dlog("NSF load %s", name ? name : "(null)");
    return t_NsfLoad(name, a2);
}
#endif

static void vfile_init(void)
{
#ifdef JP_DEBUG_FILES
    t_NsfLoad = (nsfload_t)inline_hook(0x0055a480u, "\x51\x8b\x44\x24\x08", 5, (void *)MyNsfLoad);
#endif
    InitializeCriticalSection(&g_vcs);
    if (!load_dat()) return;
    t_FileOpen    = (FileOpen_t)inline_hook(FM_OPEN, "\x81\xec\x08\x04\x00\x00", 6, (void *)MyFileOpen);
    t_FileClose   = (FileClose_t)inline_hook(FM_CLOSE, "\x51\x8b\x54\x24\x08", 5, (void *)MyFileClose);
    t_FileRead    = (FileRead_t)inline_hook(FM_READ, "\x81\xec\x0c\x04\x00\x00", 6, (void *)MyFileRead);
    t_FileSeek    = (FileSeek_t)inline_hook(FM_SEEK, "\x51\x8b\x54\x24\x08", 5, (void *)MyFileSeek);
    t_FileGetPos  = (FileGetPos_t)inline_hook(FM_GETPOS, "\x51\x8b\x54\x24\x08", 5, (void *)MyFileGetPos);
    t_FileGetSize = (FileGetSize_t)inline_hook(FM_SIZE, "\x51\x8b\x54\x24\x08", 5, (void *)MyFileGetSize);
    t_FileEof     = (FileEof_t)inline_hook(FM_EOF, "\x51\x8b\x54\x24\x08", 5, (void *)MyFileEof);
    t_FileTime    = (FileTime_t)inline_hook(FM_FTIME, "\x83\xec\x18\x33\xc0", 5, (void *)MyFileTime);
    if (!t_FileOpen || !t_FileClose || !t_FileRead || !t_FileSeek || !t_FileGetPos || !t_FileGetSize
        || !t_FileEof || !t_FileTime) {
        logf("file hooks failed: the game text stays in English");
        g_ndf = 0;     /* MyFileOpen then passes everything through */
    }
}
