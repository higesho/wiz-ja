/* Word-break separators.
 *
 * Every word-wrap / word-scan routine in Wiz8.exe treats only U+0020 as a word separator.
 * The Japanese text uses U+E000 (a private-use character with a blank zero-advance glyph in every font) as its
 * invisible break opportunity, so that the real space keeps its normal width for English text.
 * At each site below the "cmp <x>, 0x20" is redirected to a small stub that compares with 0x20 OR
 * 0xE000 (ZF=1 if either), replays the instructions that were overwritten, and jumps back.
 */

enum { OP_CMP_AX, OP_CMP_ECX, OP_CMP_EDX, OP_CMP_EAX_EDI2, OP_RAW, OP_JE, OP_JNE, OP_JMP, OP_END };

typedef struct { int op; DWORD arg; const char *raw; int rawlen; } SepOp;
typedef struct { DWORD site; int span; const char *expect; SepOp ops[6]; } SepSite;

static const SepSite g_sep_sites[] = {
    /* message log word wrap (0x58ac00) */
    { 0x0058ae17, 6, "\x66\x3d\x20\x00\x74\x1c",
      { {OP_CMP_AX}, {OP_JE, 0x0058ae39}, {OP_JMP, 0x0058ae1d}, {OP_END} } },
    /* word renderer 0x5d0050 */
    { 0x005d00ec, 10, "\x66\x3d\x20\x00\x0f\x84\xa6\x00\x00\x00",
      { {OP_CMP_AX}, {OP_JE, 0x005d019c}, {OP_JMP, 0x005d00f6}, {OP_END} } },
    /* word renderer 0x5d0770 */
    { 0x005d0814, 6, "\x66\x3d\x20\x00\x74\x18",
      { {OP_CMP_AX}, {OP_JE, 0x005d0832}, {OP_JMP, 0x005d081a}, {OP_END} } },
    /* log keyword scanner 0x58fdd0: skip leading separators, word ends at a separator */
    { 0x0058fdee, 5, "\x66\x83\x39\x20\x57",
      { {OP_CMP_ECX}, {OP_RAW, 0, "\x57", 1}, {OP_JMP, 0x0058fdf3}, {OP_END} } },
    { 0x0058fe10, 6, "\x66\x3d\x20\x00\x74\xed",
      { {OP_CMP_AX}, {OP_JE, 0x0058fe03}, {OP_JMP, 0x0058fe16}, {OP_END} } },
    { 0x0058fe51, 6, "\x66\x3d\x20\x00\x74\x71",
      { {OP_CMP_AX}, {OP_JE, 0x0058fec8}, {OP_JMP, 0x0058fe57}, {OP_END} } },
    { 0x0058fe77, 10, "\x66\x3d\x20\x00\x89\x7c\x24\x10\x75\xdc",
      { {OP_CMP_AX}, {OP_RAW, 0, "\x89\x7c\x24\x10", 4}, {OP_JNE, 0x0058fe5d}, {OP_JMP, 0x0058fe81}, {OP_END} } },
    /* backward search for the last separator (0x5d5cxx) */
    { 0x005d5d0a, 11, "\x66\x83\x3c\x78\x20\x0f\x84\x82\x00\x00\x00",
      { {OP_CMP_EAX_EDI2}, {OP_JE, 0x005d5d97}, {OP_JMP, 0x005d5d15}, {OP_END} } },
    { 0x005d5d23, 6, "\x66\x83\x3a\x20\x74\x0a",
      { {OP_CMP_EDX}, {OP_JE, 0x005d5d33}, {OP_JMP, 0x005d5d29}, {OP_END} } },
};

static BYTE *emit(BYTE *p, const void *b, int n) { memcpy(p, b, n); return p + n; }

static BYTE *emit_rel32(BYTE *p, const char *opc, int opclen, DWORD target)
{
    p = emit(p, opc, opclen);
    *(LONG *)p = (LONG)(target - ((DWORD)p + 4));
    return p + 4;
}

/* cmp A,0x20 ; je +n ; cmp A,0xE000   -> ZF=1 when the char is either separator */
static BYTE *emit_sepcmp(BYTE *p, const char *c20, int n20, const char *cbrk, int nbrk)
{
    BYTE je[2];
    p = emit(p, c20, n20);
    je[0] = 0x74; je[1] = (BYTE)nbrk;
    p = emit(p, je, 2);
    return emit(p, cbrk, nbrk);
}

static void patch_separators(void)
{
    BYTE *cave = (BYTE *)VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    BYTE *p = cave;
    int i, j, ok = 0;
    if (!cave) { logf("separator patch: no memory"); return; }
    for (i = 0; i < (int)(sizeof(g_sep_sites) / sizeof(g_sep_sites[0])); i++) {
        const SepSite *s = &g_sep_sites[i];
        BYTE *site = (BYTE *)s->site, *stub = p;
        DWORD prot;
        if (memcmp(site, s->expect, s->span) != 0) { logf("separator site %08x: unexpected bytes, skipped", s->site); continue; }
        for (j = 0; s->ops[j].op != OP_END; j++) {
            const SepOp *o = &s->ops[j];
            switch (o->op) {
            case OP_CMP_AX:        p = emit_sepcmp(p, "\x66\x3d\x20\x00", 4, "\x66\x3d\x00\xe0", 4); break;
            case OP_CMP_ECX:       p = emit_sepcmp(p, "\x66\x83\x39\x20", 4, "\x66\x81\x39\x00\xe0", 5); break;
            case OP_CMP_EDX:       p = emit_sepcmp(p, "\x66\x83\x3a\x20", 4, "\x66\x81\x3a\x00\xe0", 5); break;
            case OP_CMP_EAX_EDI2:  p = emit_sepcmp(p, "\x66\x83\x3c\x78\x20", 5, "\x66\x81\x3c\x78\x00\xe0", 6); break;
            case OP_RAW:           p = emit(p, o->raw, o->rawlen); break;
            case OP_JE:            p = emit_rel32(p, "\x0f\x84", 2, o->arg); break;
            case OP_JNE:           p = emit_rel32(p, "\x0f\x85", 2, o->arg); break;
            case OP_JMP:           p = emit_rel32(p, "\xe9", 1, o->arg); break;
            }
        }
        if (!VirtualProtect(site, s->span, PAGE_EXECUTE_READWRITE, &prot)) continue;
        site[0] = 0xe9;
        *(LONG *)(site + 1) = (LONG)((DWORD)stub - (s->site + 5));
        for (j = 5; j < s->span; j++) site[j] = 0x90;
        VirtualProtect(site, s->span, prot, &prot);
        FlushInstructionCache(GetCurrentProcess(), site, s->span);
        ok++;
    }
    FlushInstructionCache(GetCurrentProcess(), cave, p - cave);
    if (ok != 9) logf("separator patch: only %d of 9 sites", ok);
}

/* The word renderers 0x5d0050 / 0x5d0770 rebuild each line as "word" + L' ' (a hard-coded 0x20 written
   after the word, "mov word ptr [edi], 0x20"). When the word ended at U+E000, write U+E000 instead, so
   no visible gap appears between Japanese "words". [esp+off] holds the pointer to the separator. */
typedef struct { DWORD site; BYTE off; } SpaceSite;
static const SpaceSite g_space_sites[] = {
    { 0x005d03ad, 0x28 },   /* 0x5d0050 */
    { 0x005d0d24, 0x2c },   /* 0x5d0770 */
};

static void patch_word_spaces(void)
{
    BYTE *cave = (BYTE *)VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    BYTE *p = cave;
    int i, ok = 0;
    if (!cave) return;
    for (i = 0; i < (int)(sizeof(g_space_sites) / sizeof(g_space_sites[0])); i++) {
        BYTE *site = (BYTE *)g_space_sites[i].site, *stub = p, *jne;
        DWORD back = g_space_sites[i].site + 5, prot;
        BYTE mov_ptr[4];
        if (memcmp(site, "\x66\xc7\x07\x20\x00", 5) != 0) { logf("space site %08x: unexpected bytes, skipped", g_space_sites[i].site); continue; }
        p = emit(p, "\x9c\x50", 2);                                   /* pushfd; push eax */
        mov_ptr[0] = 0x8b; mov_ptr[1] = 0x44; mov_ptr[2] = 0x24; mov_ptr[3] = (BYTE)(g_space_sites[i].off + 8);
        p = emit(p, mov_ptr, 4);                                      /* mov eax, [esp+off+8] */
        p = emit(p, "\x66\x81\x38\x00\xe0", 5);                       /* cmp word ptr [eax], 0xe000 */
        p = emit(p, "\x58", 1);                                       /* pop eax */
        jne = p;
        p = emit(p, "\x75\x00", 2);                                   /* jne L */
        p = emit(p, "\x66\xc7\x07\x00\xe0\x9d", 6);                   /* mov word ptr [edi], 0xe000; popfd */
        p = emit_rel32(p, "\xe9", 1, back);
        jne[1] = (BYTE)(p - (jne + 2));
        p = emit(p, "\x66\xc7\x07\x20\x00\x9d", 6);                   /* L: mov word ptr [edi], 0x20; popfd */
        p = emit_rel32(p, "\xe9", 1, back);
        if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &prot)) continue;
        site[0] = 0xe9;
        *(LONG *)(site + 1) = (LONG)((DWORD)stub - (g_space_sites[i].site + 5));
        VirtualProtect(site, 5, prot, &prot);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
        ok++;
    }
    FlushInstructionCache(GetCurrentProcess(), cave, p - cave);
    if (ok != 2) logf("word-space patch: only %d of 2 sites", ok);
}
