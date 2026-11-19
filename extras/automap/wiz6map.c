/*
 * ウィザードリィ6 オートマップ（日本語化パッチのオプション）
 *
 * DOSBox で動いているウィザードリィ6 のメモリを読み（書き込みはしない）、
 * 歩いた所の地図を別ウインドウに描く。ゲームのファイルは書き換えない。
 *
 *  - DOSBox のプロセスから、ゲームのデータ領域（DS）を "WMAZE.OVR" の文字列で探す
 *  - DS:0x363C 地域, DS:0x4FA4/0x4FA2 座標, DS:0x4F9A 向き, DS:0x4FAA 地図データ（0xC0E バイト）
 *  - 歩いた記録は BANE\JPMAP.CUR の 1 つだけ。セーブ・ロードとは関係なく、書いたものは全部残して表示する。
 *    地図のウインドウの「この地域を消す」「全部消す」で消せる
 *  - マスのメモは BANE\JPMAP.TXT（UTF-8）
 *
 * 地図データの読み方は、KoriTama 氏の Wizardry 6 Automap Mod（GPL）と
 * martydill 氏の解析資料を参考にした（コードは使っていない）。
 *
 * build: cl /O2 /utf-8 /DUNICODE /D_UNICODE wiz6map.c /link user32.lib gdi32.lib comctl32.lib
 */
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <tlhelp32.h>
#include <wchar.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define APP_TITLE   L"ウィザードリィ6 地図"
#define GAME_TITLE  L"Wizardry 6: Bane of the Cosmic Forge"
#define NMAPS 16
#define NBLK 12
#define RECSZ 0xC0E
#define HOTKEY_VK VK_F9

/* ---------------------------------------------------------------- game data */

static const wchar_t *MAPNAME[NMAPS] = {
    L"城（入口・塔）", L"城（上層・鐘楼）", L"城（地下）", L"山・アマズールの墓所",
    L"アマズールのピラミッド", L"鉱山", L"山の峰・城の下層", L"ピラミッドの地下",
    L"ステュクス川", L"死者の間・呪われし者の墓", L"沼地", L"ラム神殿",
    L"森", L"ラム神殿の下層", L"(14)", L"(15)"
};
static const wchar_t *DIRNAME[4] = { L"北", L"東", L"南", L"西" };

static HANDLE g_proc;          /* DOSBox */
static DWORD g_pid;
static ULONG_PTR g_ds;         /* linear address of the game's DS in the DOSBox process */
static wchar_t g_bane[MAX_PATH];   /* BANE folder (save files) */

static unsigned char g_rec[NMAPS][RECSZ];   /* last seen map data */
static int g_have[NMAPS];
static unsigned char g_vis[NMAPS][NBLK * 64];  /* 0 unseen, 1 stepped, 2 seen from next square */
static int g_inmaze, g_map = -1, g_x, g_y, g_face;
static int g_dirty;            /* g_vis changed since last write of JPMAP.CUR */

typedef struct { int map, x, y; wchar_t text[80]; } Note;
static Note g_notes[1024];
static int g_nnotes;

static int bits(const unsigned char *rec, int base, int idx, int n)
{
    int bp = idx * n, o = base + bp / 8;
    unsigned w = rec[o] | ((o + 1 < RECSZ) ? rec[o + 1] << 8 : 0);
    return (w >> (bp % 8)) & ((1 << n) - 1);
}

static int blk_of(const unsigned char *rec, int x, int y, int *idx)
{
    int b;
    for (b = 0; b < NBLK; b++) {
        int ox = rec[0x1E0 + b], oy = rec[0x1EC + b];
        if (ox == 0 && oy == 0) continue;
        if (x >= ox && x <= ox + 7 && y >= oy && y <= oy + 7) {
            if (idx) *idx = b * 64 + (y - oy) * 8 + (x - ox);
            return b;
        }
    }
    return -1;
}

/* edge between (x,y) and its neighbour in direction d (0 N=y+1, 1 E=x+1, 2 S, 3 W): 0 open,1 passage,2 wall,3 door */
static int edge(int m, int x, int y, int d)
{
    const unsigned char *rec = g_rec[m];
    int idx;
    if (d == 2) { y--; d = 0; }
    else if (d == 3) { x--; d = 1; }
    if (blk_of(rec, x, y, &idx) < 0) return (m == 10 || m == 12) ? 0 : 2;
    return bits(rec, d == 0 ? 0x60 : 0x120, idx, 2);
}

static int feature(int m, int x, int y, int *dir)
{
    int idx;
    if (blk_of(g_rec[m], x, y, &idx) < 0) return 0;
    if (dir) *dir = bits(g_rec[m], 0x378, idx, 2);
    return bits(g_rec[m], 0x1F8, idx, 4);
}

static int vis_at(int m, int x, int y)
{
    int idx;
    if (!g_have[m] || blk_of(g_rec[m], x, y, &idx) < 0) return 0;
    return g_vis[m][idx];
}

static void mark(int m, int x, int y, int v)
{
    int idx;
    if (blk_of(g_rec[m], x, y, &idx) < 0) return;
    if (g_vis[m][idx] == 0 || (v == 1 && g_vis[m][idx] != 1)) { g_vis[m][idx] = (unsigned char)v; g_dirty = 1; }
}

/* ---------------------------------------------------------------- files */

static void path_in_bane(wchar_t *out, const wchar_t *name)
{
    swprintf(out, MAX_PATH, L"%s\\%s", g_bane, name);
}

static int file_exists(const wchar_t *p)
{
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static int try_bane(const wchar_t *dir)
{
    wchar_t p[MAX_PATH];
    swprintf(p, MAX_PATH, L"%s\\NEWGAME.DBS", dir);
    if (!file_exists(p)) return 0;
    wcsncpy(g_bane, dir, MAX_PATH - 1);
    return 1;
}

static void find_bane_from(const wchar_t *base)
{
    wchar_t d[MAX_PATH], *s;
    wcsncpy(d, base, MAX_PATH - 1); d[MAX_PATH - 1] = 0;
    for (int up = 0; up < 3; up++) {
        wchar_t t[MAX_PATH];
        swprintf(t, MAX_PATH, L"%s\\BANE", d);
        if (try_bane(t) || try_bane(d)) return;
        s = wcsrchr(d, L'\\');
        if (!s) break;
        *s = 0;
    }
}

/* record file: "W6MAP1" + u32 save hash + vis */
static int write_vis(const wchar_t *name, unsigned hash)
{
    wchar_t p[MAX_PATH]; FILE *f;
    if (!g_bane[0]) return 0;
    path_in_bane(p, name);
    f = _wfopen(p, L"wb");
    if (!f) return 0;
    fwrite("W6MAP1", 1, 6, f); fwrite(&hash, 4, 1, f); fwrite(g_vis, 1, sizeof g_vis, f);
    fclose(f);
    return 1;
}

static int read_vis(const wchar_t *name, unsigned *hash, int apply)
{
    wchar_t p[MAX_PATH]; FILE *f; char sig[6]; unsigned h = 0; static unsigned char tmp[NMAPS][NBLK * 64]; int ok;
    if (!g_bane[0]) return 0;
    path_in_bane(p, name);
    f = _wfopen(p, L"rb");
    if (!f) return 0;
    ok = fread(sig, 1, 6, f) == 6 && !memcmp(sig, "W6MAP1", 6) && fread(&h, 4, 1, f) == 1
         && fread(tmp, 1, sizeof tmp, f) == sizeof tmp;
    fclose(f);
    if (!ok) return 0;
    if (hash) *hash = h;
    if (apply) { memcpy(g_vis, tmp, sizeof g_vis); g_dirty = 1; }
    return 1;
}

static void save_notes(void)
{
    wchar_t p[MAX_PATH]; FILE *f;
    if (!g_bane[0]) return;
    path_in_bane(p, L"JPMAP.TXT");
    f = _wfopen(p, L"w, ccs=UTF-8");
    if (!f) return;
    for (int i = 0; i < g_nnotes; i++)
        if (g_notes[i].text[0]) fwprintf(f, L"%d\t%d\t%d\t%s\n", g_notes[i].map, g_notes[i].x, g_notes[i].y, g_notes[i].text);
    fclose(f);
}

static void load_notes(void)
{
    wchar_t p[MAX_PATH], line[256]; FILE *f;
    g_nnotes = 0;
    if (!g_bane[0]) return;
    path_in_bane(p, L"JPMAP.TXT");
    f = _wfopen(p, L"r, ccs=UTF-8");
    if (!f) return;
    while (g_nnotes < 1024 && fgetws(line, 256, f)) {
        Note *n = &g_notes[g_nnotes];
        wchar_t *t1 = wcschr(line, L'\t'), *t2, *t3;
        if (!t1) continue;
        t2 = wcschr(t1 + 1, L'\t'); if (!t2) continue;
        t3 = wcschr(t2 + 1, L'\t'); if (!t3) continue;
        n->map = _wtoi(line); n->x = _wtoi(t1 + 1); n->y = _wtoi(t2 + 1);
        wcsncpy(n->text, t3 + 1, 79); n->text[79] = 0;
        n->text[wcscspn(n->text, L"\r\n")] = 0;
        if (n->text[0]) g_nnotes++;
    }
    fclose(f);
}

static Note *note_at(int m, int x, int y)
{
    for (int i = 0; i < g_nnotes; i++)
        if (g_notes[i].map == m && g_notes[i].x == x && g_notes[i].y == y && g_notes[i].text[0]) return &g_notes[i];
    return NULL;
}

/* map data cache file, so floors seen earlier can be drawn before the game loads them again */
static void save_cache(void)
{
    wchar_t p[MAX_PATH]; FILE *f;
    if (!g_bane[0]) return;
    path_in_bane(p, L"JPMAP.DAT");
    f = _wfopen(p, L"wb");
    if (!f) return;
    fwrite(g_have, sizeof g_have, 1, f); fwrite(g_rec, sizeof g_rec, 1, f);
    fclose(f);
}

static void load_map_data(void)
{
    wchar_t p[MAX_PATH]; FILE *f; int have[NMAPS];
    /* base: the layout from NEWGAME.DBS (doors etc. may change in play; live data replaces it) */
    path_in_bane(p, L"NEWGAME.DBS");
    f = _wfopen(p, L"rb");
    if (f) {
        if (fseek(f, 0x19E, SEEK_SET) == 0 && fread(g_rec, RECSZ, NMAPS, f) == NMAPS)
            for (int m = 0; m < NMAPS; m++) g_have[m] = 1;
        fclose(f);
    }
    path_in_bane(p, L"JPMAP.DAT");
    f = _wfopen(p, L"rb");
    if (f) {
        static unsigned char rec[NMAPS][RECSZ];
        if (fread(have, sizeof have, 1, f) == 1 && fread(rec, sizeof rec, 1, f) == 1)
            for (int m = 0; m < NMAPS; m++) if (have[m] == 2) { memcpy(g_rec[m], rec[m], RECSZ); g_have[m] = 2; }
        fclose(f);
    }
}

/* ---------------------------------------------------------------- DOSBox memory */

static int rd(ULONG_PTR addr, void *buf, SIZE_T n)
{
    SIZE_T got = 0;
    return g_proc && ReadProcessMemory(g_proc, (LPCVOID)addr, buf, n, &got) && got == n;
}

static unsigned rw(unsigned off)
{
    unsigned short v = 0;
    rd(g_ds + off, &v, 2);
    return v;
}

static int g_was_maze;

static void detach(void)
{
    if (g_proc) CloseHandle(g_proc);
    g_proc = NULL; g_pid = 0; g_ds = 0; g_was_maze = 0;
}

static int check_ds(ULONG_PTR ds)
{
    char a[10], b[6];
    if (!rd(ds + 0x57C, a, 10) || memcmp(a, "WMAZE.OVR", 10)) return 0;
    if (!rd(ds + 0x600, b, 6) || memcmp(b, "WMAZE", 6)) return 0;
    return 1;
}

static ULONG_PTR scan_ds(HANDLE h)
{
    MEMORY_BASIC_INFORMATION mi;
    unsigned char *buf = (unsigned char *)VirtualAlloc(NULL, 1 << 20, MEM_COMMIT, PAGE_READWRITE);
    ULONG_PTR a = 0, found = 0;
    if (!buf) return 0;
    while (!found && VirtualQueryEx(h, (LPCVOID)a, &mi, sizeof mi) == sizeof mi) {
        ULONG_PTR base = (ULONG_PTR)mi.BaseAddress, end = base + mi.RegionSize;
        if (mi.State == MEM_COMMIT && !(mi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
            (mi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY)) && mi.RegionSize >= 0x10000) {
            for (ULONG_PTR p = base; p < end && !found; p += (1 << 20) - 16) {
                SIZE_T n = end - p < (1 << 20) ? end - p : (1 << 20), got = 0;
                if (!ReadProcessMemory(h, (LPCVOID)p, buf, n, &got) || got < 16) continue;
                for (SIZE_T i = 0; i + 10 <= got; i++) {
                    if (buf[i] == 'W' && !memcmp(buf + i, "WMAZE.OVR", 10)) {
                        ULONG_PTR ds = p + i - 0x57C;
                        HANDLE save = g_proc; g_proc = h;
                        if (check_ds(ds)) found = ds;
                        g_proc = save;
                        if (found) break;
                    }
                }
            }
        }
        if (end <= a) break;
        a = end;
    }
    VirtualFree(buf, 0, MEM_RELEASE);
    return found;
}

static void attach(void)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe;
    if (snap == INVALID_HANDLE_VALUE) return;
    pe.dwSize = sizeof pe;
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        wchar_t low[MAX_PATH]; HANDLE h; ULONG_PTR ds;
        wcsncpy(low, pe.szExeFile, MAX_PATH - 1); low[MAX_PATH - 1] = 0; _wcslwr(low);
        if (!wcsstr(low, L"dosbox")) continue;
        h = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION | SYNCHRONIZE, FALSE, pe.th32ProcessID);
        if (!h) continue;
        ds = scan_ds(h);
        if (ds) {
            g_proc = h; g_pid = pe.th32ProcessID; g_ds = ds;
            if (!g_bane[0]) {
                wchar_t img[MAX_PATH]; DWORD n = MAX_PATH;
                if (QueryFullProcessImageNameW(h, 0, img, &n)) {
                    wchar_t *s = wcsrchr(img, L'\\'); if (s) *s = 0;
                    find_bane_from(img);
                }
            }
            break;
        }
        CloseHandle(h);
    }
    CloseHandle(snap);
}

static int in_maze_state(unsigned s)
{
    switch (s) { case 5: case 10: case 11: case 12: case 13: case 14: case 19: case 20: case 22: case 24: return 1; }
    return 0;
}

/* ---------------------------------------------------------------- polling */


/* small log of what the program decided (BANE\JPMAP.LOG, kept under 64 KB) */
static void logw(const wchar_t *fmt, ...)
{
    wchar_t p[MAX_PATH], line[256]; FILE *f; va_list ap; SYSTEMTIME t; WIN32_FILE_ATTRIBUTE_DATA a;
    if (!g_bane[0]) return;
    path_in_bane(p, L"JPMAP.LOG");
    if (GetFileAttributesExW(p, GetFileExInfoStandard, &a) && a.nFileSizeLow > 65536) DeleteFileW(p);
    f = _wfopen(p, L"a, ccs=UTF-8");
    if (!f) return;
    GetLocalTime(&t);
    va_start(ap, fmt); vswprintf(line, 256, fmt, ap); va_end(ap);
    fwprintf(f, L"%02d:%02d:%02d %s\n", t.wHour, t.wMinute, t.wSecond, line);
    fclose(f);
}

/* returns 1 if the picture should be redrawn */
static int poll(void)
{
    unsigned st, m, x, y, f, ptr;
    int changed = 0;
    if (g_proc && WaitForSingleObject(g_proc, 0) == WAIT_OBJECT_0) { detach(); changed = 1; }
    if (g_proc && !check_ds(g_ds)) { detach(); changed = 1; }
    if (!g_proc) {
        static DWORD last;
        if (GetTickCount() - last < 2000) return changed;
        last = GetTickCount();
        attach();
        if (!g_proc) return changed;
        changed = 1;
    }
    st = rw(0x363A); m = rw(0x363C); x = rw(0x4FA4); y = rw(0x4FA2); f = rw(0x4F9A) & 3; ptr = rw(0x4FAA);
    {
        int maze = in_maze_state(st) && m < NMAPS && ptr;
        {
            static unsigned last_st = 0xFFFF;
            if (st != last_st) { logw(L"state %u -> %u (map %u, %u,%u)", last_st, st, m, x, y); last_st = st; }
        }
        if (maze && !g_was_maze) { logw(L"enter maze"); changed = 1; }
        g_was_maze = maze;
        if (maze) {
            unsigned char rec[RECSZ];
            if (rd(g_ds + ptr, rec, RECSZ) && blk_of(rec, x, y, NULL) >= 0) {
                if (g_have[m] != 2 || memcmp(rec, g_rec[m], RECSZ)) {
                    memcpy(g_rec[m], rec, RECSZ); g_have[m] = 2; changed = 1;
                    save_cache();
                }
                if (!g_inmaze || (int)m != g_map || (int)x != g_x || (int)y != g_y || (int)f != g_face) {
                    g_inmaze = 1; g_map = m; g_x = x; g_y = y; g_face = f; changed = 1;
                    mark(m, x, y, 1);
                    for (int d = 0; d < 4; d++)
                        if (edge(m, x, y, d) < 2)
                            mark(m, x + (d == 1) - (d == 3), y + (d == 0) - (d == 2), 2);
                }
            }
        } else if (g_inmaze) { g_inmaze = 0; changed = 1; }
    }
    if (g_dirty) {
        static DWORD last;
        if (GetTickCount() - last > 3000) { write_vis(L"JPMAP.CUR", 0); g_dirty = 0; last = GetTickCount(); }
    }
    return changed;
}

/* ---------------------------------------------------------------- drawing */

static HWND g_wnd, g_combo, g_btn_center, g_chk_top, g_status, g_btn_clr1, g_btn_clrall;
static HFONT g_font, g_font_small, g_font_mark;
static int g_view = -1;        /* map shown; -1 = follow the party */
static int g_cell = 18;
static double g_cx, g_cy;      /* view centre in map units */
static int g_follow = 1;
static int g_hover_x = -9999, g_hover_y;
#define BAR_H 58
#define LEG_H 40

static int view_map(void)
{
    if (g_view >= 0) return g_view;
    return g_map >= 0 ? g_map : -1;
}

static void cell_to_px(RECT *area, int x, int y, int *px, int *py)
{
    int w = area->right - area->left, h = area->bottom - area->top;
    *px = area->left + w / 2 + (int)((x - g_cx) * g_cell);
    *py = area->top + h / 2 - (int)((y + 1 - g_cy) * g_cell);
}

static void px_to_cell(RECT *area, int px, int py, int *x, int *y)
{
    int w = area->right - area->left, h = area->bottom - area->top;
    double fx = (px - area->left - w / 2) / (double)g_cell + g_cx;
    double fy = -(py - area->top - h / 2) / (double)g_cell + g_cy;
    *x = (int)(fx >= 0 ? fx : fx - 1);
    *y = (int)(fy >= 0 ? fy : fy - 1);
}

static void centre_on_visited(int m)
{
    int x0 = 999, x1 = -1, y0 = 999, y1 = -1;
    if (m < 0) return;
    for (int b = 0; b < NBLK; b++) {
        int ox = g_rec[m][0x1E0 + b], oy = g_rec[m][0x1EC + b];
        if (ox == 0 && oy == 0) continue;
        for (int i = 0; i < 64; i++) if (g_vis[m][b * 64 + i]) {
            int x = ox + i % 8, y = oy + i / 8;
            if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y;
        }
    }
    if (x1 < 0) return;
    g_cx = (x0 + x1 + 1) / 2.0; g_cy = (y0 + y1 + 1) / 2.0;
}

static void line(HDC dc, int x0, int y0, int x1, int y1) { MoveToEx(dc, x0, y0, NULL); LineTo(dc, x1, y1); }

static void text_c(HDC dc, int cx, int cy, const wchar_t *s, COLORREF c)
{
    RECT r = { cx - 40, cy - 20, cx + 40, cy + 20 };
    SetTextColor(dc, c);
    DrawTextW(dc, s, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

static void draw_edge(HDC dc, int kind, int x0, int y0, int x1, int y1, int dim, int portcullis)
{
    HPEN pen, old; int vert = (x0 == x1);
    COLORREF wall = dim ? RGB(140, 140, 150) : RGB(235, 235, 240);
    if (kind == 0) return;
    if (portcullis) {
        pen = CreatePen(PS_SOLID, 1, RGB(120, 170, 255)); old = (HPEN)SelectObject(dc, pen);
        for (int t = 1; t < 4; t++) {
            int px = vert ? x0 : x0 + (x1 - x0) * t / 4, py = vert ? y0 + (y1 - y0) * t / 4 : y0;
            line(dc, px - (vert ? 3 : 0), py - (vert ? 0 : 3), px + (vert ? 4 : 0), py + (vert ? 0 : 4));
        }
        line(dc, x0, y0, x1, y1);
        SelectObject(dc, old); DeleteObject(pen);
        return;
    }
    if (kind == 1) {          /* passage / archway */
        pen = CreatePen(PS_DOT, 1, dim ? RGB(90, 90, 100) : RGB(150, 150, 160));
        old = (HPEN)SelectObject(dc, pen); line(dc, x0, y0, x1, y1);
        SelectObject(dc, old); DeleteObject(pen);
        return;
    }
    pen = CreatePen(PS_SOLID, g_cell >= 14 ? 3 : 2, wall);
    old = (HPEN)SelectObject(dc, pen);
    line(dc, x0, y0, x1, y1);
    SelectObject(dc, old); DeleteObject(pen);
    if (kind == 3) {          /* door: orange bar in the middle of the wall */
        int mx = (x0 + x1) / 2, my = (y0 + y1) / 2, r = g_cell / 4 + 1;
        HBRUSH br = CreateSolidBrush(dim ? RGB(150, 95, 50) : RGB(240, 140, 50));
        RECT rr;
        if (vert) SetRect(&rr, mx - 3, my - r, mx + 4, my + r);
        else SetRect(&rr, mx - r, my - 3, mx + r, my + 4);
        FillRect(dc, &rr, br); DeleteObject(br);
    }
}

static void draw_map(HDC dc, RECT *area)
{
    int m = view_map();
    HBRUSH bg = CreateSolidBrush(RGB(18, 18, 26));
    FillRect(dc, area, bg); DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);
    if (m < 0 || !g_have[m]) {
        SelectObject(dc, g_font);
        text_c(dc, (area->left + area->right) / 2, (area->top + area->bottom) / 2,
               g_proc ? L"迷宮に入ると地図を描きます" : L"ゲームを探しています…", RGB(170, 170, 190));
        return;
    }
    HRGN clip = CreateRectRgnIndirect(area); SelectClipRgn(dc, clip);
    HBRUSH fl1 = CreateSolidBrush(RGB(52, 56, 78)), fl2 = CreateSolidBrush(RGB(34, 36, 50));
    int xa, ya, xb, yb;
    px_to_cell(area, area->left, area->bottom, &xa, &ya);
    px_to_cell(area, area->right, area->top, &xb, &yb);
    /* floors */
    for (int y = ya - 1; y <= yb + 1; y++) for (int x = xa - 1; x <= xb + 1; x++) {
        int v = vis_at(m, x, y), px, py; RECT r;
        if (!v) continue;
        cell_to_px(area, x, y, &px, &py);
        SetRect(&r, px + 1, py + 1, px + g_cell, py + g_cell);
        FillRect(dc, &r, v == 1 ? fl1 : fl2);
    }
    /* edges: north and east of each cell, plus south/west where the neighbour is not part of any block */
    for (int y = ya - 1; y <= yb + 1; y++) for (int x = xa - 1; x <= xb + 1; x++) {
        int v = vis_at(m, x, y), px, py, fd = 0, ft;
        if (blk_of(g_rec[m], x, y, NULL) < 0) continue;
        cell_to_px(area, x, y, &px, &py);
        ft = feature(m, x, y, &fd);
        {
            int vn = vis_at(m, x, y + 1);
            if (v || vn) draw_edge(dc, edge(m, x, y, 0), px, py, px + g_cell, py, !(v == 1 || vn == 1), ft == 7 && fd == 0);
            int ve = vis_at(m, x + 1, y);
            if (v || ve) draw_edge(dc, edge(m, x, y, 1), px + g_cell, py, px + g_cell, py + g_cell, !(v == 1 || ve == 1), ft == 7 && fd == 1);
            if (v && blk_of(g_rec[m], x, y - 1, NULL) < 0)
                draw_edge(dc, edge(m, x, y, 2), px, py + g_cell, px + g_cell, py + g_cell, v != 1, 0);
            if (v && blk_of(g_rec[m], x - 1, y, NULL) < 0)
                draw_edge(dc, edge(m, x, y, 3), px, py, px, py + g_cell, v != 1, 0);
        }
    }
    /* features and notes (only on squares actually stepped on) */
    SelectObject(dc, g_font_mark);
    for (int y = ya - 1; y <= yb + 1; y++) for (int x = xa - 1; x <= xb + 1; x++) {
        int v = vis_at(m, x, y), px, py, ft, fd;
        const wchar_t *s = NULL; COLORREF c = RGB(255, 220, 90);
        if (v != 1) continue;
        cell_to_px(area, x, y, &px, &py);
        ft = feature(m, x, y, &fd);
        switch (ft) {
        case 1: s = L"▲"; c = RGB(110, 230, 120); break;     /* stairs up */
        case 2: s = L"▼"; c = RGB(110, 230, 120); break;     /* stairs down */
        case 3: break;                                       /* sconce: decoration only, not drawn */
        case 4: s = L"泉"; c = RGB(110, 200, 255); break;    /* fountain */
        case 6: s = L"◇"; c = RGB(230, 170, 230); break;    /* pressure plate */
        case 8: s = L"鎖"; c = RGB(200, 200, 200); break;    /* shackles */
        case 9: case 10: case 11: case 12: s = L"窪"; c = RGB(210, 190, 150); break; /* niche */
        case 13: s = L"水"; c = RGB(110, 160, 255); break;   /* water */
        case 14: s = L"×"; c = RGB(255, 90, 90); break;     /* pit down */
        case 15: s = L"○"; c = RGB(255, 140, 90); break;    /* hole up */
        }
        if (s && g_cell >= 10) text_c(dc, px + g_cell / 2, py + g_cell / 2, s, c);
        if (note_at(m, x, y)) {
            HBRUSH nb = CreateSolidBrush(RGB(255, 230, 0)); RECT r;
            SetRect(&r, px + g_cell - 5, py + 2, px + g_cell - 1, py + 6);
            FillRect(dc, &r, nb); DeleteObject(nb);
        }
    }
    /* party */
    if (g_inmaze && g_map == m) {
        int px, py, c = g_cell / 2, r = g_cell * 2 / 5; POINT p[3];
        cell_to_px(area, g_x, g_y, &px, &py);
        px += c; py += c + 1;
        switch (g_face) {
        case 0: p[0].x = px; p[0].y = py - r; p[1].x = px - r; p[1].y = py + r; p[2].x = px + r; p[2].y = py + r; break;
        case 1: p[0].x = px + r; p[0].y = py; p[1].x = px - r; p[1].y = py - r; p[2].x = px - r; p[2].y = py + r; break;
        case 2: p[0].x = px; p[0].y = py + r; p[1].x = px - r; p[1].y = py - r; p[2].x = px + r; p[2].y = py - r; break;
        default: p[0].x = px - r; p[0].y = py; p[1].x = px + r; p[1].y = py - r; p[2].x = px + r; p[2].y = py + r; break;
        }
        HBRUSH pb = CreateSolidBrush(RGB(255, 70, 70)); HPEN pp = CreatePen(PS_SOLID, 1, RGB(255, 200, 200));
        HGDIOBJ ob = SelectObject(dc, pb), op = SelectObject(dc, pp);
        Polygon(dc, p, 3);
        SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(pb); DeleteObject(pp);
    }
    DeleteObject(fl1); DeleteObject(fl2);
    SelectClipRgn(dc, NULL); DeleteObject(clip);
}

static void draw_legend(HDC dc, RECT *r)
{
    static const struct { const wchar_t *s; COLORREF c; const wchar_t *t; } L[] = {
        { L"▲", RGB(110, 230, 120), L"上り" }, { L"▼", RGB(110, 230, 120), L"下り" },
        { L"━", RGB(240, 140, 50), L"扉" }, { L"┅", RGB(120, 170, 255), L"鉄格子" },
        { L"泉", RGB(110, 200, 255), L"" },
        { L"×", RGB(255, 90, 90), L"落とし穴" }, { L"○", RGB(255, 140, 90), L"上への穴" },
        { L"◇", RGB(230, 170, 230), L"感圧板" }, { L"窪", RGB(210, 190, 150), L"くぼみ" },
        { L"■", RGB(255, 230, 0), L"メモ" },
    };
    HBRUSH bg = CreateSolidBrush(RGB(28, 28, 38));
    FillRect(dc, r, bg); DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, g_font_small);
    int x = r->left + 6, y = r->top + 3;
    for (int i = 0; i < (int)(sizeof L / sizeof L[0]); i++) {
        SIZE sz1, sz2; wchar_t buf[32];
        GetTextExtentPoint32W(dc, L[i].s, (int)wcslen(L[i].s), &sz1);
        swprintf(buf, 32, L"%s ", L[i].t);
        GetTextExtentPoint32W(dc, buf, (int)wcslen(buf), &sz2);
        if (x + sz1.cx + sz2.cx + 4 > r->right) { x = r->left + 6; y += 17; }
        SetTextColor(dc, L[i].c); TextOutW(dc, x, y, L[i].s, (int)wcslen(L[i].s));
        SetTextColor(dc, RGB(190, 190, 200)); TextOutW(dc, x + sz1.cx + 2, y, buf, (int)wcslen(buf));
        x += sz1.cx + sz2.cx + 8;
    }
}

static void map_area(RECT *r)
{
    GetClientRect(g_wnd, r);
    r->top += BAR_H; r->bottom -= LEG_H + 22;
}

static void update_status(void)
{
    wchar_t s[256]; int m = view_map();
    if (!g_proc) wcscpy(s, L"ゲームが見つかりません（ウィザードリィ6 を起動してください）");
    else if (!g_inmaze) swprintf(s, 256, L"迷宮の外です");
    else swprintf(s, 256, L"%s　東西 %d・南北 %d　%s向き", MAPNAME[g_map], g_x, g_y, DIRNAME[g_face]);
    if (m >= 0 && g_hover_x != -9999) {
        Note *n = note_at(m, g_hover_x, g_hover_y);
        if (n) swprintf(s, 256, L"メモ: %s", n->text);
    }
    SetWindowTextW(g_status, s);
}

static void fill_combo(void)
{
    wchar_t cur[64] = L"";
    int sel = 0, n = 1;
    SendMessageW(g_combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(g_combo, CB_ADDSTRING, 0, (LPARAM)L"いる地域を表示");
    SendMessageW(g_combo, CB_SETITEMDATA, 0, (LPARAM)-1);
    for (int m = 0; m < 14; m++) {
        int any = 0;
        for (int i = 0; i < NBLK * 64 && !any; i++) any = g_vis[m][i];
        if (!any) continue;
        SendMessageW(g_combo, CB_ADDSTRING, 0, (LPARAM)MAPNAME[m]);
        SendMessageW(g_combo, CB_SETITEMDATA, n, (LPARAM)m);
        if (m == g_view) sel = n;
        n++;
    }
    (void)cur;
    SendMessageW(g_combo, CB_SETCURSEL, sel, 0);
}

/* ---------------------------------------------------------------- note dialog */

static wchar_t g_note_buf[80];
static int g_note_done;

static LRESULT CALLBACK NoteProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK || LOWORD(wp) == 3) {
            if (LOWORD(wp) == IDOK) GetDlgItemTextW(h, 10, g_note_buf, 80);
            else g_note_buf[0] = 0;
            g_note_done = 1; DestroyWindow(h); return 0;
        }
        if (LOWORD(wp) == IDCANCEL) { g_note_done = 2; DestroyWindow(h); return 0; }
        break;
    case WM_CLOSE: g_note_done = 2; DestroyWindow(h); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

/* returns 1 = OK (buffer has text, may be empty = delete), 0 = cancelled */
static int ask_note(const wchar_t *title, const wchar_t *init)
{
    RECT wr; HWND h, e; MSG msg;
    GetWindowRect(g_wnd, &wr);
    h = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, L"W6MAPNOTE", title,
                        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                        wr.left + 30, wr.top + 80, 360, 130, g_wnd, NULL, GetModuleHandleW(NULL), NULL);
    e = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", init, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                        10, 10, 330, 26, h, (HMENU)10, NULL, NULL);
    HWND b1 = CreateWindowW(L"BUTTON", L"保存", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 10, 50, 100, 28, h, (HMENU)IDOK, NULL, NULL);
    HWND b2 = CreateWindowW(L"BUTTON", L"メモを消す", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 125, 50, 100, 28, h, (HMENU)3, NULL, NULL);
    HWND b3 = CreateWindowW(L"BUTTON", L"やめる", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 240, 50, 100, 28, h, (HMENU)IDCANCEL, NULL, NULL);
    SendMessageW(e, WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(b1, WM_SETFONT, (WPARAM)g_font_small, TRUE);
    SendMessageW(b2, WM_SETFONT, (WPARAM)g_font_small, TRUE);
    SendMessageW(b3, WM_SETFONT, (WPARAM)g_font_small, TRUE);
    SendMessageW(e, EM_SETLIMITTEXT, 79, 0);
    SetFocus(e); SendMessageW(e, EM_SETSEL, 0, -1);
    EnableWindow(g_wnd, FALSE);
    g_note_done = 0;
    while (!g_note_done && GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageW(h, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    EnableWindow(g_wnd, TRUE);
    SetForegroundWindow(g_wnd);
    return g_note_done == 1;
}

static void edit_note(int m, int x, int y)
{
    Note *n = note_at(m, x, y);
    wchar_t title[96];
    swprintf(title, 96, L"メモ（東西 %d・南北 %d）", x, y);
    if (!ask_note(title, n ? n->text : L"")) return;
    if (!n) {
        if (!g_note_buf[0] || g_nnotes >= 1024) return;
        n = &g_notes[g_nnotes++];
        n->map = m; n->x = x; n->y = y;
    }
    wcsncpy(n->text, g_note_buf, 79); n->text[79] = 0;
    save_notes();
    InvalidateRect(g_wnd, NULL, FALSE);
}

/* ---------------------------------------------------------------- clearing the record */

/* m = area to clear, -1 = all areas. Asks first; the notes can be cleared with it */
static void clear_record(int m)
{
    wchar_t q[300]; int r, i, k;
    if (m < -1 || m >= NMAPS) return;
    if (m >= 0)
        swprintf(q, 300, L"「%s」の歩いた記録を消します。\n\nこの地域のメモも一緒に消しますか？\n\n"
                 L"［はい］記録とメモを消す　［いいえ］記録だけ消す　［キャンセル］やめる", MAPNAME[m]);
    else
        swprintf(q, 300, L"すべての地域の歩いた記録を消します。\n\nメモも一緒に消しますか？\n\n"
                 L"［はい］記録とメモを消す　［いいえ］記録だけ消す　［キャンセル］やめる");
    r = MessageBoxW(g_wnd, q, APP_TITLE, MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON3);
    if (r == IDCANCEL) return;
    if (m >= 0) memset(g_vis[m], 0, sizeof g_vis[m]); else memset(g_vis, 0, sizeof g_vis);
    write_vis(L"JPMAP.CUR", 0); g_dirty = 0;
    if (r == IDYES) {
        for (i = 0, k = 0; i < g_nnotes; i++)
            if (m >= 0 && g_notes[i].map != m) g_notes[k++] = g_notes[i];
        g_nnotes = k;
        save_notes();
    }
    logw(L"clear: %s%s", m >= 0 ? MAPNAME[m] : L"all", r == IDYES ? L" (with notes)" : L"");
    if (m < 0 || m == g_view) { g_view = -1; g_follow = 1; if (g_inmaze) { g_cx = g_x + 0.5; g_cy = g_y + 0.5; } }
    fill_combo(); update_status();
    InvalidateRect(g_wnd, NULL, FALSE);
}

/* ---------------------------------------------------------------- window */

static HHOOK g_hook;

static LRESULT CALLBACK KbProc(int code, WPARAM wp, LPARAM lp)
{
    /* plain F9 only: Alt+F9 (DOSBox: load state), Ctrl+F9 and Shift+F9 go to the game untouched */
    if (code == HC_ACTION && wp == WM_KEYDOWN) {
        KBDLLHOOKSTRUCT *k = (KBDLLHOOKSTRUCT *)lp;
        if (k->vkCode == HOTKEY_VK && !(k->flags & LLKHF_ALTDOWN) &&
            !(GetAsyncKeyState(VK_MENU) & 0x8000) && !(GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
            !(GetAsyncKeyState(VK_SHIFT) & 0x8000) && !(GetAsyncKeyState(VK_LWIN) & 0x8000) &&
            !(GetAsyncKeyState(VK_RWIN) & 0x8000)) {
            HWND fg = GetForegroundWindow(); DWORD pid = 0;
            GetWindowThreadProcessId(fg, &pid);
            if (fg == g_wnd || (g_pid && pid == g_pid)) {
                if (IsWindowVisible(g_wnd)) ShowWindow(g_wnd, SW_HIDE);
                else ShowWindow(g_wnd, SW_SHOWNOACTIVATE);
                return 1;
            }
        }
    }
    return CallNextHookEx(g_hook, code, wp, lp);
}

static int place_beside_game(void)
{
    HWND gw = FindWindowW(NULL, GAME_TITLE);
    RECT gr, mr, wa;
    if (!gw || !IsWindowVisible(gw)) return 0;
    GetWindowRect(gw, &gr); GetWindowRect(g_wnd, &mr);
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int w = mr.right - mr.left, h = mr.bottom - mr.top, x = gr.right + 4, y = gr.top;
    if (x + w > wa.right) x = wa.right - w;
    if (y + h > wa.bottom) y = wa.bottom - h;
    SetWindowPos(g_wnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    return 1;
}

static int g_drag, g_dragx, g_dragy, g_moved;
static double g_dragcx, g_dragcy;

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        g_combo = CreateWindowW(L"COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                4, 3, 220, 300, h, (HMENU)100, NULL, NULL);
        g_btn_center = CreateWindowW(L"BUTTON", L"現在地へ", WS_CHILD | WS_VISIBLE, 230, 3, 80, 24, h, (HMENU)101, NULL, NULL);
        g_chk_top = CreateWindowW(L"BUTTON", L"いつも手前", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 318, 5, 100, 20, h, (HMENU)102, NULL, NULL);
        g_status = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP | SS_NOPREFIX, 0, 0, 10, 10, h, (HMENU)103, NULL, NULL);
        g_btn_clr1 = CreateWindowW(L"BUTTON", L"この地域の記録を消す", WS_CHILD | WS_VISIBLE, 4, 31, 160, 24, h, (HMENU)104, NULL, NULL);
        g_btn_clrall = CreateWindowW(L"BUTTON", L"全部の記録を消す", WS_CHILD | WS_VISIBLE, 170, 31, 140, 24, h, (HMENU)105, NULL, NULL);
        SendMessageW(g_btn_clr1, WM_SETFONT, (WPARAM)g_font_small, TRUE);
        SendMessageW(g_btn_clrall, WM_SETFONT, (WPARAM)g_font_small, TRUE);
        SendMessageW(g_combo, WM_SETFONT, (WPARAM)g_font_small, TRUE);
        SendMessageW(g_btn_center, WM_SETFONT, (WPARAM)g_font_small, TRUE);
        SendMessageW(g_chk_top, WM_SETFONT, (WPARAM)g_font_small, TRUE);
        SendMessageW(g_status, WM_SETFONT, (WPARAM)g_font_small, TRUE);
        SendMessageW(g_chk_top, BM_SETCHECK, BST_CHECKED, 0);
        g_wnd = h; fill_combo();      /* areas with a record, even before the game is found */
        SetTimer(h, 1, 150, NULL);
        return 0;
    }
    case WM_SIZE: {
        RECT r; GetClientRect(h, &r);
        MoveWindow(g_status, 6, r.bottom - LEG_H - 20, r.right - 12, 18, TRUE);
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, RGB(225, 225, 235)); SetBkColor(dc, RGB(28, 28, 38));
        static HBRUSH br; if (!br) br = CreateSolidBrush(RGB(28, 28, 38));
        return (LRESULT)br;
    }
    case WM_TIMER: {
        static int placed, last_map = -2, nvis = -1;
        if (!placed && g_proc) placed = place_beside_game();
        if (poll()) {
            if (g_follow && g_view < 0 && g_inmaze) { g_cx = g_x + 0.5; g_cy = g_y + 0.5; }
            int count = 0;
            for (int m = 0; m < 14; m++) { int any = 0; for (int i = 0; i < NBLK * 64 && !any; i++) any = g_vis[m][i]; count += any; }
            if (count != nvis || last_map != g_map) { nvis = count; last_map = g_map; fill_combo(); }
            update_status();
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == 100 && HIWORD(wp) == CBN_SELCHANGE) {
            int sel = (int)SendMessageW(g_combo, CB_GETCURSEL, 0, 0);
            g_view = (int)SendMessageW(g_combo, CB_GETITEMDATA, sel, 0);
            if (g_view < 0) { g_follow = 1; if (g_inmaze) { g_cx = g_x + 0.5; g_cy = g_y + 0.5; } }
            else if (g_view == g_map && g_inmaze) { g_cx = g_x + 0.5; g_cy = g_y + 0.5; }
            else centre_on_visited(g_view);
            InvalidateRect(h, NULL, FALSE);
            SetFocus(h);
        } else if (LOWORD(wp) == 101) {
            g_view = -1; g_follow = 1; fill_combo();
            if (g_inmaze) { g_cx = g_x + 0.5; g_cy = g_y + 0.5; }
            InvalidateRect(h, NULL, FALSE);
            SetFocus(h);
        } else if (LOWORD(wp) == 104 || LOWORD(wp) == 105) {
            int vm = view_map();
            if (LOWORD(wp) == 104 && vm < 0) MessageBoxW(h, L"消す地域の地図が表示されていません。", APP_TITLE, MB_OK | MB_ICONINFORMATION);
            else clear_record(LOWORD(wp) == 104 ? vm : -1);
            SetFocus(h);
        } else if (LOWORD(wp) == 102) {
            int on = SendMessageW(g_chk_top, BM_GETCHECK, 0, 0) == BST_CHECKED;
            SetWindowPos(h, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            SetFocus(h);
        }
        return 0;
    case WM_MOUSEWHEEL: {
        int d = GET_WHEEL_DELTA_WPARAM(wp);
        g_cell += d > 0 ? 2 : -2;
        if (g_cell < 8) g_cell = 8;
        if (g_cell > 40) g_cell = 40;
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    case WM_LBUTTONDOWN:
        g_drag = 1; g_moved = 0; g_dragx = GET_X_LPARAM(lp); g_dragy = GET_Y_LPARAM(lp);
        g_dragcx = g_cx; g_dragcy = g_cy;
        SetCapture(h);
        return 0;
    case WM_MOUSEMOVE: {
        int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
        RECT a; map_area(&a);
        if (g_drag) {
            if (abs(mx - g_dragx) + abs(my - g_dragy) > 4) g_moved = 1;
            if (g_moved) {
                g_follow = 0;
                g_cx = g_dragcx - (mx - g_dragx) / (double)g_cell;
                g_cy = g_dragcy + (my - g_dragy) / (double)g_cell;
                InvalidateRect(h, NULL, FALSE);
            }
        } else {
            int x, y, ox = g_hover_x, oy = g_hover_y;
            if (my >= a.top && my < a.bottom) { px_to_cell(&a, mx, my, &x, &y); g_hover_x = x; g_hover_y = y; }
            else g_hover_x = -9999;
            if (ox != g_hover_x || oy != g_hover_y) update_status();
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp), m = view_map();
        RECT a; map_area(&a);
        ReleaseCapture();
        if (g_drag && !g_moved && m >= 0 && my >= a.top && my < a.bottom) {
            int x, y; px_to_cell(&a, mx, my, &x, &y);
            g_drag = 0;
            if (vis_at(m, x, y) == 1) edit_note(m, x, y);
        }
        g_drag = 0;
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ADD || wp == VK_OEM_PLUS) { g_cell = g_cell < 40 ? g_cell + 2 : 40; InvalidateRect(h, NULL, FALSE); }
        else if (wp == VK_SUBTRACT || wp == VK_OEM_MINUS) { g_cell = g_cell > 8 ? g_cell - 2 : 8; InvalidateRect(h, NULL, FALSE); }
        else if (wp == VK_SPACE || wp == VK_HOME) SendMessageW(h, WM_COMMAND, 101, 0);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps), mdc; RECT cr, a, lg, bar; HBITMAP bmp, ob;
        GetClientRect(h, &cr);
        mdc = CreateCompatibleDC(dc);
        bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
        ob = (HBITMAP)SelectObject(mdc, bmp);
        bar = cr; bar.bottom = BAR_H;
        { HBRUSH b = CreateSolidBrush(RGB(28, 28, 38)); FillRect(mdc, &cr, b); DeleteObject(b); }
        map_area(&a);
        draw_map(mdc, &a);
        lg = cr; lg.top = cr.bottom - LEG_H;
        draw_legend(mdc, &lg);
        BitBlt(dc, 0, BAR_H, cr.right, a.bottom - BAR_H, mdc, 0, BAR_H, SRCCOPY);
        BitBlt(dc, 0, lg.top, cr.right, LEG_H, mdc, 0, lg.top, SRCCOPY);
        SelectObject(mdc, ob); DeleteObject(bmp); DeleteDC(mdc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND: {
        RECT r; HBRUSH b = CreateSolidBrush(RGB(28, 28, 38));
        GetClientRect(h, &r); FillRect((HDC)wp, &r, b); DeleteObject(b);
        return 1;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp; mm->ptMinTrackSize.x = 440; mm->ptMinTrackSize.y = 300;
        return 0;
    }
    case WM_DESTROY:
        write_vis(L"JPMAP.CUR", 0);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmd, int show)
{
    WNDCLASSW wc; MSG msg; wchar_t exe[MAX_PATH], *s;
    HANDLE mutex;
    (void)prev; (void)show;
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    s = wcsrchr(exe, L'\\'); if (s) *s = 0;
    find_bane_from(exe);
    if (cmd && wcsstr(cmd, L"--probe")) {      /* diagnostics: write what the program can read to probe.txt */
        wchar_t p[MAX_PATH]; FILE *f;
        attach();
        swprintf(p, MAX_PATH, L"%s\\probe.txt", exe);
        f = _wfopen(p, L"w, ccs=UTF-8");
        if (f) {
            fwprintf(f, L"bane=%s\npid=%lu ds=%llx\n", g_bane, g_pid, (unsigned long long)g_ds);
            if (g_ds) fwprintf(f, L"state=%u map=%u x=%u y=%u face=%u ptr=%04x\n",
                               rw(0x363A), rw(0x363C), rw(0x4FA4), rw(0x4FA2), rw(0x4F9A), rw(0x4FAA));
            fclose(f);
        }
        return 0;
    }
    if (g_bane[0]) { load_map_data(); read_vis(L"JPMAP.CUR", NULL, 1); load_notes(); g_dirty = 0; }

    g_font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, 0, 0, 0, SHIFTJIS_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Meiryo UI");
    g_font_small = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, SHIFTJIS_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Meiryo UI");
    g_font_mark = CreateFontW(-12, 0, 0, 0, FW_BOLD, 0, 0, 0, SHIFTJIS_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Meiryo UI");
    if (cmd && wcsstr(cmd, L"--snapshot")) {   /* diagnostics: draw the current map into snapshot.bmp */
        wchar_t p[MAX_PATH]; HDC sdc = GetDC(NULL), mdc = CreateCompatibleDC(sdc);
        HBITMAP bmp = CreateCompatibleBitmap(sdc, 500, 520); RECT a = { 0, 0, 500, 480 }, lg = { 0, 480, 500, 520 };
        BITMAPINFO bi; static DWORD px[500 * 520]; BITMAPFILEHEADER fh; FILE *f;
        attach(); g_was_maze = 1; poll();
        if (g_inmaze) { g_cx = g_x + 0.5; g_cy = g_y + 0.5; }
        SelectObject(mdc, bmp);
        draw_map(mdc, &a); draw_legend(mdc, &lg);
        ZeroMemory(&bi, sizeof bi);
        bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = 500; bi.bmiHeader.biHeight = 520;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
        GetDIBits(mdc, bmp, 0, 520, px, &bi, DIB_RGB_COLORS);
        swprintf(p, MAX_PATH, L"%s\\snapshot.bmp", exe);
        f = _wfopen(p, L"wb");
        if (f) {
            ZeroMemory(&fh, sizeof fh);
            fh.bfType = 0x4D42; fh.bfOffBits = sizeof fh + sizeof bi.bmiHeader; fh.bfSize = fh.bfOffBits + sizeof px;
            fwrite(&fh, sizeof fh, 1, f); fwrite(&bi.bmiHeader, sizeof bi.bmiHeader, 1, f); fwrite(px, sizeof px, 1, f);
            fclose(f);
        }
        return 0;
    }

    mutex = CreateMutexW(NULL, TRUE, L"Wiz6JpAutomap");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND w = FindWindowW(L"W6MAP", NULL);
        if (w) { ShowWindow(w, SW_SHOWNOACTIVATE); SetForegroundWindow(w); }
        return 0;
    }

    ZeroMemory(&wc, sizeof wc);
    wc.lpfnWndProc = WndProc; wc.hInstance = inst; wc.lpszClassName = L"W6MAP";
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW); wc.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    RegisterClassW(&wc);
    wc.lpfnWndProc = NoteProc; wc.lpszClassName = L"W6MAPNOTE"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassW(&wc);

    g_wnd = CreateWindowExW(WS_EX_TOPMOST, L"W6MAP", APP_TITLE, WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT, CW_USEDEFAULT, 520, 600, NULL, NULL, inst, NULL);
    ShowWindow(g_wnd, SW_SHOWNOACTIVATE);
    update_status();
    g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, KbProc, inst, 0);
    while (GetMessageW(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    if (g_hook) UnhookWindowsHookEx(g_hook);
    detach();
    ReleaseMutex(mutex);
    return 0;
}
