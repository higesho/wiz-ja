/* Word wrap in the two routines that split text with wcscspn(text, L" ").
 *
 *  0x4f35b0  text controls (Controls.cpp: labels, text boxes, list items, the spell/item info popups).
 *            Splits in place: each word is ended with 0 at its separator, measured, and the separator is
 *            written back as a hard-coded 0x20 (0x4f369c); a line break turns the previous separator into '\n'.
 *  0x5d2a50  message dialogs (stMessageDialog.cpp). Copies the words into line buffers and joins them
 *            with wcscat(line, L" ").
 *
 * Japanese text uses U+E000 as its (invisible) break opportunity, so:
 *  - the separator set pushed for wcscspn becomes L" \xE000";
 *  - the separator that ended a word is remembered, written back / used as the joiner instead of 0x20;
 *  - a U+E000 separator counts as width 0 (the routines add the width of a space after every word).
 * The space width is a local of each routine; it is saved while it is non-zero and restored for spaces.
 */

static WCHAR g_sepset[3] = { 0x0020, 0xE000, 0 };
static WCHAR g_sep_a;                       /* separator after the current word (0x4f35b0) */
static DWORD g_spw_a, g_spw_d;              /* saved space widths */
static WCHAR g_jprev[2] = { 0x0020, 0 };    /* separator before the current word (0x5d2a50) */
static WCHAR g_jcur[2]  = { 0x0020, 0 };    /* separator after the current word */

static BYTE *emit32(BYTE *p, DWORD v) { *(DWORD *)p = v; return p + 4; }

static int patch_push_imm(DWORD site, DWORD oldv, DWORD newv)
{
    BYTE *p = (BYTE *)site;
    DWORD prot;
    if (p[0] != 0x68 || *(DWORD *)(p + 1) != oldv) { logf("push at %08x: unexpected bytes, skipped", site); return 0; }
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &prot)) return 0;
    *(DWORD *)(p + 1) = newv;
    VirtualProtect(p, 5, prot, &prot);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    return 1;
}

static int hook_jmp(DWORD site, int span, const char *expect, BYTE *stub)
{
    BYTE *p = (BYTE *)site;
    DWORD prot;
    int j;
    if (memcmp(p, expect, span) != 0) { logf("wrap site %08x: unexpected bytes, skipped", site); return 0; }
    if (!VirtualProtect(p, span, PAGE_EXECUTE_READWRITE, &prot)) return 0;
    p[0] = 0xe9;
    *(LONG *)(p + 1) = (LONG)((DWORD)stub - (site + 5));
    for (j = 5; j < span; j++) p[j] = 0x90;
    VirtualProtect(p, span, prot, &prot);
    FlushInstructionCache(GetCurrentProcess(), p, span);
    return 1;
}

#define SEPSET 0x00617c88   /* L" " */

static void patch_wrap(void)
{
    BYTE *cave = (BYTE *)VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    BYTE *p = cave, *stub, *j1, *j2, *j3;
    int ok = 0;
    if (!cave) { logf("wrap patch: no memory"); return; }

    /* ---- 0x4f35b0 ---- */
    ok += patch_push_imm(0x004f360c, SEPSET, (DWORD)g_sepset);
    ok += patch_push_imm(0x004f3696, SEPSET, (DWORD)g_sepset);

    /* 0x4f362f: mov word ptr [ebx], 0   (end the word at its separator); space width at [esp+0x14] */
    stub = p;
    p = emit(p, "\x50", 1);                                   /* push eax */
    p = emit(p, "\x66\x8b\x03", 3);                           /* mov ax, [ebx] */
    p = emit(p, "\x66\xa3", 2); p = emit32(p, (DWORD)&g_sep_a); /* mov [g_sep_a], ax */
    p = emit(p, "\x66\x3d\x00\xe0", 4);                       /* cmp ax, 0xe000 */
    j1 = p; p = emit(p, "\x75\x00", 2);                       /* jne L1 */
    p = emit(p, "\x8b\x44\x24\x18", 4);                       /* mov eax, [esp+0x18] */
    p = emit(p, "\x85\xc0", 2);                               /* test eax, eax */
    j2 = p; p = emit(p, "\x74\x00", 2);                       /* jz L2 */
    p = emit(p, "\xa3", 1); p = emit32(p, (DWORD)&g_spw_a);   /* mov [g_spw_a], eax */
    j2[1] = (BYTE)(p - (j2 + 2));
    p = emit(p, "\xc7\x44\x24\x18\x00\x00\x00\x00", 8);       /* L2: mov dword ptr [esp+0x18], 0 */
    j3 = p; p = emit(p, "\xeb\x00", 2);                       /* jmp L3 */
    j1[1] = (BYTE)(p - (j1 + 2));
    p = emit(p, "\x8b\x44\x24\x18", 4);                       /* L1: mov eax, [esp+0x18] */
    p = emit(p, "\x85\xc0", 2);                               /* test eax, eax */
    j1 = p; p = emit(p, "\x75\x00", 2);                       /* jnz L3 */
    p = emit(p, "\xa1", 1); p = emit32(p, (DWORD)&g_spw_a);   /* mov eax, [g_spw_a] */
    p = emit(p, "\x89\x44\x24\x18", 4);                       /* mov [esp+0x18], eax */
    j1[1] = (BYTE)(p - (j1 + 2));
    j3[1] = (BYTE)(p - (j3 + 2));
    p = emit(p, "\x58", 1);                                   /* L3: pop eax */
    p = emit(p, "\x66\xc7\x03\x00\x00", 5);                   /* mov word ptr [ebx], 0 */
    p = emit_rel32(p, "\xe9", 1, 0x004f3634);
    ok += hook_jmp(0x004f362f, 5, "\x66\xc7\x03\x00\x00", stub);

    /* 0x4f369c: mov word ptr [ebx], 0x20   -> write back the separator that was there */
    stub = p;
    p = emit(p, "\x50", 1);                                   /* push eax */
    p = emit(p, "\x66\xa1", 2); p = emit32(p, (DWORD)&g_sep_a); /* mov ax, [g_sep_a] */
    p = emit(p, "\x66\x89\x03", 3);                           /* mov [ebx], ax */
    p = emit(p, "\x58", 1);                                   /* pop eax */
    p = emit_rel32(p, "\xe9", 1, 0x004f36a1);
    ok += hook_jmp(0x004f369c, 5, "\x66\xc7\x03\x20\x00", stub);

    /* ---- 0x5d2a50 (message dialogs) ---- */
    ok += patch_push_imm(0x005d2b02, SEPSET, (DWORD)g_sepset);
    ok += patch_push_imm(0x005d2bb2, SEPSET, (DWORD)g_sepset);
    ok += patch_push_imm(0x005d2b7f, SEPSET, (DWORD)g_jprev);  /* joiner before a word inside the loop */
    ok += patch_push_imm(0x005d2c1a, SEPSET, (DWORD)g_jcur);   /* joiner before the last word */

    /* 0x5d2b2a: mov word ptr [esi+eax*2], 0; space width at [esp+0x20] */
    stub = p;
    p = emit(p, "\x51\x52", 2);                               /* push ecx; push edx */
    p = emit(p, "\x66\x8b\x0c\x46", 4);                       /* mov cx, [esi+eax*2] */
    p = emit(p, "\x66\x8b\x15", 3); p = emit32(p, (DWORD)g_jcur);  /* mov dx, [g_jcur] */
    p = emit(p, "\x66\x89\x15", 3); p = emit32(p, (DWORD)g_jprev); /* mov [g_jprev], dx */
    p = emit(p, "\x66\x89\x0d", 3); p = emit32(p, (DWORD)g_jcur);  /* mov [g_jcur], cx */
    p = emit(p, "\x66\x81\xf9\x00\xe0", 5);                   /* cmp cx, 0xe000 */
    j1 = p; p = emit(p, "\x75\x00", 2);                       /* jne L1 */
    p = emit(p, "\x8b\x54\x24\x28", 4);                       /* mov edx, [esp+0x28] */
    p = emit(p, "\x85\xd2", 2);                               /* test edx, edx */
    j2 = p; p = emit(p, "\x74\x00", 2);                       /* jz L2 */
    p = emit(p, "\x89\x15", 2); p = emit32(p, (DWORD)&g_spw_d); /* mov [g_spw_d], edx */
    j2[1] = (BYTE)(p - (j2 + 2));
    p = emit(p, "\xc7\x44\x24\x28\x00\x00\x00\x00", 8);       /* L2: mov dword ptr [esp+0x28], 0 */
    j3 = p; p = emit(p, "\xeb\x00", 2);                       /* jmp L3 */
    j1[1] = (BYTE)(p - (j1 + 2));
    p = emit(p, "\x8b\x54\x24\x28", 4);                       /* L1: mov edx, [esp+0x28] */
    p = emit(p, "\x85\xd2", 2);                               /* test edx, edx */
    j1 = p; p = emit(p, "\x75\x00", 2);                       /* jnz L3 */
    p = emit(p, "\x8b\x15", 2); p = emit32(p, (DWORD)&g_spw_d); /* mov edx, [g_spw_d] */
    p = emit(p, "\x89\x54\x24\x28", 4);                       /* mov [esp+0x28], edx */
    j1[1] = (BYTE)(p - (j1 + 2));
    j3[1] = (BYTE)(p - (j3 + 2));
    p = emit(p, "\x5a\x59", 2);                               /* L3: pop edx; pop ecx */
    p = emit(p, "\x66\xc7\x04\x46\x00\x00", 6);               /* mov word ptr [esi+eax*2], 0 */
    p = emit_rel32(p, "\xe9", 1, 0x005d2b30);
    ok += hook_jmp(0x005d2b2a, 6, "\x66\xc7\x04\x46\x00\x00", stub);

    FlushInstructionCache(GetCurrentProcess(), cave, p - cave);
    if (ok != 9) logf("wrap patch: only %d of 9 sites", ok);
}
