/* ---- ddraw.dll proxy ----
   Every export of the system ddraw.dll (exports listed in ddraw.def) jumps to the real function, which
   is looked up on first use (not in DllMain, to stay clear of the loader lock). */

static const char *g_dd_names[] = {
    "AcquireDDThreadLock", "CompleteCreateSysmemSurface", "D3DParseUnknownCommand",
    "DDGetAttachedSurfaceLcl", "DDInternalLock", "DDInternalUnlock", "DSoundHelp", "DirectDrawCreate",
    "DirectDrawCreateClipper", "DirectDrawCreateEx", "DirectDrawEnumerateA", "DirectDrawEnumerateExA",
    "DirectDrawEnumerateExW", "DirectDrawEnumerateW", "DllCanUnloadNow", "DllGetClassObject",
    "GetDDSurfaceLocal", "GetOLEThunkData", "GetSurfaceFromDC", "RegisterSpecialCase",
    "ReleaseDDThreadLock", "SetAppCompatData",
};
#define DD_COUNT (sizeof(g_dd_names) / sizeof(g_dd_names[0]))
static FARPROC g_dd[DD_COUNT];

static FARPROC __stdcall dd_get(int i)
{
    static HMODULE sys;
    if (!g_dd[i]) {
        if (!sys) {
            char p[MAX_PATH];
            UINT n = GetSystemDirectoryA(p, MAX_PATH);
            if (n && n < MAX_PATH - 12) {
                lstrcatA(p, "\\ddraw.dll");
                sys = LoadLibraryA(p);
            }
            if (!sys) { logf("system ddraw.dll could not be loaded"); ExitProcess(1); }
        }
        g_dd[i] = GetProcAddress(sys, g_dd_names[i]);
        if (!g_dd[i]) { logf("ddraw.dll has no %s", g_dd_names[i]); ExitProcess(1); }
    }
    return g_dd[i];
}

#define DD_STUB(i, name) __declspec(naked) void dd_##name(void) { __asm { push i } __asm { call dd_get } __asm { jmp eax } }
DD_STUB(0, AcquireDDThreadLock)
DD_STUB(1, CompleteCreateSysmemSurface)
DD_STUB(2, D3DParseUnknownCommand)
DD_STUB(3, DDGetAttachedSurfaceLcl)
DD_STUB(4, DDInternalLock)
DD_STUB(5, DDInternalUnlock)
DD_STUB(6, DSoundHelp)
DD_STUB(7, DirectDrawCreate)
DD_STUB(8, DirectDrawCreateClipper)
DD_STUB(9, DirectDrawCreateEx)
DD_STUB(10, DirectDrawEnumerateA)
DD_STUB(11, DirectDrawEnumerateExA)
DD_STUB(12, DirectDrawEnumerateExW)
DD_STUB(13, DirectDrawEnumerateW)
DD_STUB(14, DllCanUnloadNow)
DD_STUB(15, DllGetClassObject)
DD_STUB(16, GetDDSurfaceLocal)
DD_STUB(17, GetOLEThunkData)
DD_STUB(18, GetSurfaceFromDC)
DD_STUB(19, RegisterSpecialCase)
DD_STUB(20, ReleaseDDThreadLock)
DD_STUB(21, SetAppCompatData)
