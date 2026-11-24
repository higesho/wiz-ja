/*
 * ウィザードリィ7 オートマップ（日本語化パッチのオプション、Wizardry Gold 版）
 *
 * Wizardry Gold（WIZARD.EXE 1.1b）のメモリを読み（書き込みはしない）、歩いた所の地図を
 * 別ウインドウに描く。ゲームのファイルは書き換えない。
 *
 *  - 現在地: 0x4a0ff0 地域, 0x4a0ff4 東西, 0x4a0ff8 南北, 0x4a0ffc/0x4a1000 区画内の x/y,
 *            0x4a1004 区画, 0x4a1008 向き（0 北 1 東 2 南 3 西）
 *  - 地図: いる地域は [0x4add04]（0xad0 バイト）、ほかの地域は SCENARIO.GLD の 0xac40 + 地域*0xad0
 *  - 歩いたマス: このプログラムが現在地から付ける記録（ゲームのフォルダの JPMAP.VIS）。
 *    セーブ・ロードとは関係なく、書いたものは全部残して表示する。「この地域を消す」「全部消す」で消せる
 *    （ゲーム自身の記録 0x4ad830 / [0x4b099c] + 地域*0x80 はセーブごとに変わるので使わない）
 *  - マスのメモは、ゲームのフォルダの JPMAP.TXT（UTF-8）
 *
 * 地図データの読み方は、tamanegitaro 氏の Wizardry 7 CDS PC-98 External Automap と、
 * その元の KoriTama 氏の Automap Mod（GPL）を参考にした（コードは使っていない）。
 *
 * build: cl /O2 /utf-8 /DUNICODE /D_UNICODE wiz7map.c /link user32.lib gdi32.lib
 */
#include <windows.h>
#include <windowsx.h>
#include <tlhelp32.h>
#include <wchar.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#define APP_TITLE   L"ウィザードリィ7 地図"
#define NMAPS 32
#define NQ 16
#define RECSZ 0xAD0
#define SCEN_OFS 0xAC40
#define HOTKEY_VK VK_F9

/* WIZARD.EXE (Gold 1.1b) */
#define A_LEVEL   0x4a0ff0
#define A_X       0x4a0ff4
#define A_Y       0x4a0ff8
#define A_FACE    0x4a1008
#define A_RECPTR  0x4add04
#define A_VISCUR  0x4ad830
#define A_VISALL  0x4b099c
#define A_SIG     0x486705   /* abs -> quadrant function, used to recognise the version */
static const unsigned char SIG[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x53, 0x56, 0x57 };

/* ---------------------------------------------------------------- game data */

static const wchar_t *MAPNAME[NMAPS] = {
    L"ニューシティの西・蘭の野", L"ニクタリンスへの道", L"ニクタリンス・ラットキンの廃墟・オーコーグルの森を結ぶ道",
    L"オーコーグルの森", L"ニューシティの東・マンクハラマ橋", L"マンクハラマからウクピアへの道",
    L"ウクピアから山への道", L"山の石段", L"エアン川", L"悲しみの海（中央）", L"悲しみの海（西）",
    L"悲しみの海（東）", L"悲しみの海（南）", L"ミュルミドンの森・荒れ地", L"ニューシティ", L"デインの塔",
    L"ニクタリンス", L"初心者の迷宮・オーコーグル城", L"ラットキンの廃墟", L"マンクハラマ・夢の国",
    L"天空都市", L"ウクピア", L"魔女の洞窟・巨人の洞窟・スフィンクスの洞窟", L"マンクハラマの地下神殿",
    L"墓所（死者の間）", L"ブロンバデグの水の洞窟", L"アストラル・ドミナエの墓", L"高い山々",
    L"ニクタリンスの地下", L"見世物小屋", L"旧市街・修道院の地下・デインの塔の上の階",
    L"ニューシティ（トランとウンパニの建物ほか）"
};
static const wchar_t *DIRNAME[4] = { L"北", L"東", L"南", L"西" };

/* floor kinds */
enum { T_NONE = -1, T_DARK = -2, T_FLOOR = 0, T_WATER, T_SLABS, T_GRASS, T_ROAD, T_FOG };
/* tileset of each map, and per tileset: default floor, floor where layer 2 is set, layer 0, layer 1 */
static const int MAP_TS[NMAPS] = { 2,2,2,2,2,2,2,4,6,6,6,6,6,2,3,1,3,1,3,3,3,3,5,1,1,5,1,4,5,1,1,3 };
static const int TS_TILE[7][4] = {
    { T_FLOOR, T_FLOOR, T_NONE, T_NONE },
    { T_FLOOR, T_WATER, T_DARK, T_NONE },     /* dungeon */
    { T_ROAD, T_GRASS, T_SLABS, T_NONE },     /* forest and roads */
    { T_GRASS, T_SLABS, T_NONE, T_WATER },    /* city */
    { T_FLOOR, T_GRASS, T_DARK, T_NONE },     /* mountain */
    { T_FLOOR, T_WATER, T_NONE, T_FOG },      /* cave */
    { T_ROAD, T_GRASS, T_WATER, T_FOG },      /* river and sea */
};

static HANDLE g_proc;
static DWORD g_pid;
static int g_badver;                 /* WIZARD.EXE found but not the supported version */
static wchar_t g_gamedir[MAX_PATH];  /* folder of WIZARD.EXE */

static unsigned char g_rec[NMAPS][RECSZ];
static int g_have;                   /* map data of all areas was read (SCENARIO.GLD, DOS SCENARIO.DBS or the copy) */
static int g_lvl[NMAPS];             /* 1 from a file, 2 read from the running game */
static unsigned char g_vis[NMAPS][128];   /* squares stepped on (bit = quadrant*64 + y*8 + x) */
static int g_dirty;                       /* g_vis changed since JPMAP.VIS was written */
static void read_vis(void);
static int g_inmaze, g_map = -1, g_x, g_y, g_face;

typedef struct { int map, x, y; wchar_t text[80]; } Note;
static Note g_notes[1024];
static int g_nnotes;

static int bits(const unsigned char *rec, int base, int idx, int n)
{
    int bp = idx * n, o = base + bp / 8;
    unsigned w = rec[o] | ((o + 1 < RECSZ) ? rec[o + 1] << 8 : 0);
    return (w >> (bp % 8)) & ((1 << n) - 1);
}

static int q_of(const unsigned char *rec, int x, int y, int *idx)
{
    for (int q = 0; q < NQ; q++) {
        int ox = rec[0x490 + q], oy = rec[0x4A0 + q];
        if (ox == 0 && oy == 0) continue;
        if (x >= ox && x <= ox + 7 && y >= oy && y <= oy + 7) {
            if (idx) *idx = q * 64 + (y - oy) * 8 + (x - ox);
            return q;
        }
    }
    return -1;
}

/* edge between (x,y) and its neighbour in direction d (0 N=y+1, 1 E=x+1, 2 S, 3 W)
   0 open, 1 passage, 2 wall, 3 door, 4 trees/hedge, 5 cave passage, 6 portcullis, 7 window */
static int edge(int m, int x, int y, int d)
{
    int idx;
    if (d == 2) { y--; d = 0; }
    else if (d == 3) { x--; d = 1; }
    if (q_of(g_rec[m], x, y, &idx) < 0) return 2;
    return bits(g_rec[m], d == 0 ? 0x90 : 0x290, idx, 4);
}

static int see_through(int e) { return e == 0 || e == 1 || e == 5; }

static int feature(int m, int x, int y)
{
    int idx;
    if (q_of(g_rec[m], x, y, &idx) < 0) return 0;
    return bits(g_rec[m], 0x4B0, idx, 6);
}

static int floor_kind(int m, int x, int y)
{
    int idx, ts = MAP_TS[m], k;
    if (q_of(g_rec[m], x, y, &idx) < 0) return T_NONE;
    k = bits(g_rec[m], 0x9B0, idx, 1) ? TS_TILE[ts][1] : TS_TILE[ts][0];
    if (TS_TILE[ts][2] != T_NONE && bits(g_rec[m], 0x8B0, idx, 1)) k = TS_TILE[ts][2];
    if (TS_TILE[ts][3] != T_NONE && bits(g_rec[m], 0x930, idx, 1)) k = TS_TILE[ts][3];
    if (bits(g_rec[m], 0x4B0, idx, 6) == 13) k = T_WATER;
    return k;
}

static int stepped(int m, int x, int y)
{
    int idx;
    if (!g_lvl[m] || q_of(g_rec[m], x, y, &idx) < 0) return 0;
    return (g_vis[m][idx >> 3] >> (idx & 7)) & 1;
}

/* 1 stepped on, 2 seen from a square next to it, 0 not known */
static int vis_at(int m, int x, int y)
{
    if (!g_lvl[m] || q_of(g_rec[m], x, y, NULL) < 0) return 0;
    if (stepped(m, x, y)) return 1;
    for (int d = 0; d < 4; d++) {
        int nx = x + (d == 1) - (d == 3), ny = y + (d == 0) - (d == 2);
        if (stepped(m, nx, ny) && see_through(edge(m, x, y, d))) return 2;
    }
    return 0;
}

/* ---------------------------------------------------------------- files */

static void path_in_game(wchar_t *out, const wchar_t *name)
{
    swprintf(out, MAX_PATH, L"%s\\%s", g_gamedir, name);
}

static int file_exists(const wchar_t *p)
{
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static int try_gamedir(const wchar_t *dir)
{
    wchar_t p[MAX_PATH];
    swprintf(p, MAX_PATH, L"%s\\SCENARIO.GLD", dir);
    if (!file_exists(p)) return 0;
    wcsncpy(g_gamedir, dir, MAX_PATH - 1);
    return 1;
}

static void find_gamedir_from(const wchar_t *base)
{
    wchar_t d[MAX_PATH], *s;
    wcsncpy(d, base, MAX_PATH - 1); d[MAX_PATH - 1] = 0;
    for (int up = 0; up < 3; up++) {
        wchar_t t[MAX_PATH];
        swprintf(t, MAX_PATH, L"%s\\wizardry7gold", d);
        if (try_gamedir(d) || try_gamedir(t)) return;
        s = wcsrchr(d, L'\\');
        if (!s) break;
        *s = 0;
    }
}

/* SCENARIO.GLD is kept open (not shared) by the running game, so a copy is kept in JPMAP.DAT;
   the DOS version's SCENARIO.DBS has the same map data and can be read while Gold runs */
static int read_maps(const wchar_t *path, long ofs)
{
    static unsigned char tmp[NMAPS][RECSZ];
    FILE *f = _wfopen(path, L"rb");
    int ok;
    if (!f) return 0;
    ok = fseek(f, ofs, SEEK_SET) == 0 && fread(tmp, RECSZ, NMAPS, f) == NMAPS;
    fclose(f);
    if (!ok || tmp[14][0x490] == 0) return 0;          /* New City must have its first block */
    memcpy(g_rec, tmp, sizeof g_rec);
    for (int m = 0; m < NMAPS; m++) g_lvl[m] = 1;
    g_have = 1;
    return 1;
}

static void load_scenario(void)
{
    wchar_t p[MAX_PATH], c[MAX_PATH];
    if (!g_gamedir[0] || g_have) return;
    path_in_game(p, L"SCENARIO.GLD");
    path_in_game(c, L"JPMAP.DAT");
    if (read_maps(p, SCEN_OFS)) {
        FILE *f = _wfopen(c, L"wb");
        if (f) { fwrite(g_rec, RECSZ, NMAPS, f); fclose(f); }
        return;
    }
    if (read_maps(c, 0)) return;
    swprintf(p, MAX_PATH, L"%s\\..\\DSAVANT\\SCENARIO.DBS", g_gamedir);
    read_maps(p, SCEN_OFS);
}

static void save_notes(void)
{
    wchar_t p[MAX_PATH]; FILE *f;
    if (!g_gamedir[0]) return;
    path_in_game(p, L"JPMAP.TXT");
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
    if (!g_gamedir[0]) return;
    path_in_game(p, L"JPMAP.TXT");
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
        if (n->text[0] && n->map >= 0 && n->map < NMAPS) g_nnotes++;
    }
    fclose(f);
}

static Note *note_at(int m, int x, int y)
{
    for (int i = 0; i < g_nnotes; i++)
        if (g_notes[i].map == m && g_notes[i].x == x && g_notes[i].y == y && g_notes[i].text[0]) return &g_notes[i];
    return NULL;
}

/* 動きの記録 JPMAP.LOG（調べるとき用）は試験版（build.bat test）だけ。配布版では何もしない */
static void logw(const wchar_t *fmt, ...)
{
#ifndef WIZTOOL_TEST
    (void)fmt;
#else
    wchar_t p[MAX_PATH], line[256]; FILE *f; va_list ap; SYSTEMTIME t; WIN32_FILE_ATTRIBUTE_DATA a;
    if (!g_gamedir[0]) return;
    path_in_game(p, L"JPMAP.LOG");
    if (GetFileAttributesExW(p, GetFileExInfoStandard, &a) && a.nFileSizeLow > 65536) DeleteFileW(p);
    f = _wfopen(p, L"a, ccs=UTF-8");
    if (!f) return;
    GetLocalTime(&t);
    va_start(ap, fmt); vswprintf(line, 256, fmt, ap); va_end(ap);
    fwprintf(f, L"%02d:%02d:%02d %s\n", t.wHour, t.wMinute, t.wSecond, line);
    fclose(f);
#endif
}

/* ---------------------------------------------------------------- game memory */

static int rd(DWORD addr, void *buf, SIZE_T n)
{
    SIZE_T got = 0;
    return g_proc && ReadProcessMemory(g_proc, (LPCVOID)(ULONG_PTR)addr, buf, n, &got) && got == n;
}

static unsigned rw(DWORD a) { unsigned short v = 0xFFFF; rd(a, &v, 2); return v; }
static DWORD rdw(DWORD a) { DWORD v = 0; rd(a, &v, 4); return v; }

static void detach(void)
{
    if (g_proc) CloseHandle(g_proc);
    g_proc = NULL; g_pid = 0;
}

static void attach(void)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe;
    if (snap == INVALID_HANDLE_VALUE) return;
    pe.dwSize = sizeof pe;
    g_badver = 0;
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        HANDLE h; unsigned char sig[sizeof SIG]; SIZE_T got = 0;
        if (_wcsicmp(pe.szExeFile, L"WIZARD.EXE")) continue;
        h = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pe.th32ProcessID);
        if (!h) continue;
        if (!ReadProcessMemory(h, (LPCVOID)(ULONG_PTR)A_SIG, sig, sizeof sig, &got) || got != sizeof sig || memcmp(sig, SIG, sizeof SIG)) {
            g_badver = 1; CloseHandle(h); continue;
        }
        g_proc = h; g_pid = pe.th32ProcessID;
        {
            wchar_t img[MAX_PATH]; DWORD n = MAX_PATH;
            if (QueryFullProcessImageNameW(h, 0, img, &n)) {
                wchar_t *s = wcsrchr(img, L'\\'); if (s) *s = 0;
                if (!g_gamedir[0] && try_gamedir(img)) read_vis();
            }
        }
        load_scenario();
        logw(L"attached to WIZARD.EXE pid %lu", g_pid);
        break;
    }
    CloseHandle(snap);
}

/* ---------------------------------------------------------------- the record (JPMAP.VIS) */

static void write_vis(void)
{
    wchar_t p[MAX_PATH], t[MAX_PATH]; FILE *f;
    if (!g_gamedir[0]) return;
    path_in_game(p, L"JPMAP.VIS"); path_in_game(t, L"JPMAP.VIS.TMP");
    f = _wfopen(t, L"wb");
    if (!f) return;
    fwrite("W7MAP1", 1, 6, f); fwrite(g_vis, 1, sizeof g_vis, f);
    if (fclose(f) == 0) MoveFileExW(t, p, MOVEFILE_REPLACE_EXISTING);
    g_dirty = 0;
}

static void read_vis(void)
{
    wchar_t p[MAX_PATH]; FILE *f; char sig[6]; static unsigned char tmp[NMAPS][128];
    if (!g_gamedir[0]) return;
    path_in_game(p, L"JPMAP.VIS");
    f = _wfopen(p, L"rb");
    if (!f) return;
    if (fread(sig, 1, 6, f) == 6 && !memcmp(sig, "W7MAP1", 6) && fread(tmp, 1, sizeof tmp, f) == sizeof tmp)
        memcpy(g_vis, tmp, sizeof g_vis);
    fclose(f);
}

static void mark_here(int m, int x, int y)
{
    int idx;
    if (q_of(g_rec[m], x, y, &idx) < 0) return;
    if (!((g_vis[m][idx >> 3] >> (idx & 7)) & 1)) { g_vis[m][idx >> 3] |= (unsigned char)(1 << (idx & 7)); g_dirty = 1; }
}

/* ---------------------------------------------------------------- polling */

/* returns 1 if the picture should be redrawn */
static int poll(void)
{
    unsigned m, x, y, f;
    DWORD recp, allp;
    int changed = 0;
    if (g_proc && WaitForSingleObject(g_proc, 0) == WAIT_OBJECT_0) { detach(); changed = 1; if (g_inmaze) { g_inmaze = 0; } }
    if (!g_proc) {
        static DWORD last;
        if (GetTickCount() - last < 2000) return changed;
        last = GetTickCount();
        attach();
        if (!g_proc) return changed;
        changed = 1;
    }
    m = rw(A_LEVEL); x = rw(A_X); y = rw(A_Y); f = rw(A_FACE) & 3;
    recp = rdw(A_RECPTR); allp = rdw(A_VISALL);
    {
        int ok = m < NMAPS && recp && allp;     /* allp: only to tell that a game is running */
        unsigned char rec[RECSZ];
        if (ok && rd(recp, rec, RECSZ) && (g_lvl[m] ? !memcmp(rec + 0x490, g_rec[m] + 0x490, 0x20)
                                                     : q_of(rec, x, y, NULL) >= 0)) {
            if (!g_lvl[m] || memcmp(rec, g_rec[m], RECSZ)) { memcpy(g_rec[m], rec, RECSZ); if (!g_lvl[m]) g_lvl[m] = 2; changed = 1; }
        }
        ok = ok && g_lvl[m];
        ok = ok && q_of(g_rec[m], x, y, NULL) >= 0;
        if (ok) {
            if (!g_inmaze || (int)m != g_map || (int)x != g_x || (int)y != g_y || (int)f != g_face) {
                if (!g_inmaze || (int)m != g_map) logw(L"in map %u at (%u,%u)", m, x, y);
                g_inmaze = 1; g_map = m; g_x = x; g_y = y; g_face = f; changed = 1;
                mark_here(m, x, y);
            }
        } else if (g_inmaze) { g_inmaze = 0; changed = 1; }
    }
    if (g_dirty) {
        static DWORD last;
        if (GetTickCount() - last > 3000) { write_vis(); last = GetTickCount(); }
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

static int any_visited(int m)
{
    for (int i = 0; i < 128; i++) if (g_vis[m][i]) return 1;
    return 0;
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
    for (int q = 0; q < NQ; q++) {
        int ox = g_rec[m][0x490 + q], oy = g_rec[m][0x4A0 + q];
        if (ox == 0 && oy == 0) continue;
        for (int i = 0; i < 64; i++) {
            int idx = q * 64 + i;
            if (!((g_vis[m][idx >> 3] >> (idx & 7)) & 1)) continue;
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

static COLORREF dimc(COLORREF c, int dim)
{
    if (!dim) return c;
    return RGB(GetRValue(c) * 3 / 5, GetGValue(c) * 3 / 5, GetBValue(c) * 3 / 5);
}

static void pen_line(HDC dc, int style, int w, COLORREF c, int x0, int y0, int x1, int y1)
{
    HPEN pen = CreatePen(style, w, c), old = (HPEN)SelectObject(dc, pen);
    line(dc, x0, y0, x1, y1);
    SelectObject(dc, old); DeleteObject(pen);
}

static void draw_edge(HDC dc, int kind, int x0, int y0, int x1, int y1, int dim)
{
    int vert = (x0 == x1), w = g_cell >= 14 ? 3 : 2;
    switch (kind) {
    case 0: return;
    case 1: pen_line(dc, PS_DOT, 1, dim ? RGB(90, 90, 100) : RGB(150, 150, 160), x0, y0, x1, y1); return;
    case 5: pen_line(dc, PS_DOT, 1, dim ? RGB(100, 85, 60) : RGB(170, 140, 100), x0, y0, x1, y1); return;
    case 4: pen_line(dc, PS_SOLID, w, dimc(RGB(90, 190, 90), dim), x0, y0, x1, y1); return;
    case 7: pen_line(dc, PS_SOLID, w, dimc(RGB(235, 235, 240), dim), x0, y0, x1, y1);
            pen_line(dc, PS_SOLID, 1, dimc(RGB(150, 210, 255), dim),
                     vert ? x0 : x0 + (x1 - x0) / 4, vert ? y0 + (y1 - y0) / 4 : y0,
                     vert ? x1 : x1 - (x1 - x0) / 4, vert ? y1 - (y1 - y0) / 4 : y1);
            return;
    case 6: {
        HPEN pen = CreatePen(PS_SOLID, 1, dimc(RGB(120, 170, 255), dim)), old = (HPEN)SelectObject(dc, pen);
        for (int t = 1; t < 4; t++) {
            int px = vert ? x0 : x0 + (x1 - x0) * t / 4, py = vert ? y0 + (y1 - y0) * t / 4 : y0;
            line(dc, px - (vert ? 3 : 0), py - (vert ? 0 : 3), px + (vert ? 4 : 0), py + (vert ? 0 : 4));
        }
        line(dc, x0, y0, x1, y1);
        SelectObject(dc, old); DeleteObject(pen);
        return;
    }
    }
    pen_line(dc, PS_SOLID, w, dim ? RGB(140, 140, 150) : RGB(235, 235, 240), x0, y0, x1, y1);
    if (kind == 3) {          /* door: orange bar in the middle of the wall */
        int mx = (x0 + x1) / 2, my = (y0 + y1) / 2, r = g_cell / 4 + 1;
        HBRUSH br = CreateSolidBrush(dim ? RGB(150, 95, 50) : RGB(240, 140, 50));
        RECT rr;
        if (vert) SetRect(&rr, mx - 3, my - r, mx + 4, my + r);
        else SetRect(&rr, mx - r, my - 3, mx + r, my + 4);
        FillRect(dc, &rr, br); DeleteObject(br);
    }
}

static COLORREF floor_color(int k)
{
    switch (k) {
    case T_WATER: return RGB(38, 70, 140);
    case T_SLABS: return RGB(84, 72, 100);
    case T_GRASS: return RGB(44, 84, 46);
    case T_ROAD:  return RGB(92, 82, 62);
    case T_FOG:   return RGB(78, 88, 100);
    case T_DARK:  return RGB(30, 30, 40);
    }
    return RGB(52, 56, 78);
}

static void draw_map(HDC dc, RECT *area)
{
    int m = view_map();
    HBRUSH bg = CreateSolidBrush(RGB(18, 18, 26));
    FillRect(dc, area, bg); DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);
    if (m < 0 || !g_lvl[m]) {
        SelectObject(dc, g_font);
        text_c(dc, (area->left + area->right) / 2, (area->top + area->bottom) / 2,
               g_proc ? L"ゲームを始めると地図を描きます" : L"ゲームを探しています…", RGB(170, 170, 190));
        return;
    }
    HRGN clip = CreateRectRgnIndirect(area); SelectClipRgn(dc, clip);
    int xa, ya, xb, yb;
    px_to_cell(area, area->left, area->bottom, &xa, &ya);
    px_to_cell(area, area->right, area->top, &xb, &yb);
    /* floors */
    for (int y = ya - 1; y <= yb + 1; y++) for (int x = xa - 1; x <= xb + 1; x++) {
        int v = vis_at(m, x, y), px, py; RECT r; HBRUSH b;
        if (!v) continue;
        cell_to_px(area, x, y, &px, &py);
        SetRect(&r, px + 1, py + 1, px + g_cell, py + g_cell);
        b = CreateSolidBrush(dimc(floor_color(floor_kind(m, x, y)), v != 1));
        FillRect(dc, &r, b); DeleteObject(b);
    }
    /* edges: north and east of each square, plus south/west where the neighbour is not part of the map */
    for (int y = ya - 1; y <= yb + 1; y++) for (int x = xa - 1; x <= xb + 1; x++) {
        int v = vis_at(m, x, y), px, py;
        if (q_of(g_rec[m], x, y, NULL) < 0) continue;
        cell_to_px(area, x, y, &px, &py);
        int vn = vis_at(m, x, y + 1);
        if (v || vn) draw_edge(dc, edge(m, x, y, 0), px, py, px + g_cell, py, !(v == 1 || vn == 1));
        int ve = vis_at(m, x + 1, y);
        if (v || ve) draw_edge(dc, edge(m, x, y, 1), px + g_cell, py, px + g_cell, py + g_cell, !(v == 1 || ve == 1));
        if (v && q_of(g_rec[m], x, y - 1, NULL) < 0)
            draw_edge(dc, 2, px, py + g_cell, px + g_cell, py + g_cell, v != 1);
        if (v && q_of(g_rec[m], x - 1, y, NULL) < 0)
            draw_edge(dc, 2, px, py, px, py + g_cell, v != 1);
    }
    /* features and notes (only on squares actually stepped on) */
    SelectObject(dc, g_font_mark);
    for (int y = ya - 1; y <= yb + 1; y++) for (int x = xa - 1; x <= xb + 1; x++) {
        int v = vis_at(m, x, y), px, py;
        const wchar_t *s = NULL; COLORREF c = RGB(255, 220, 90);
        if (v != 1) continue;
        cell_to_px(area, x, y, &px, &py);
        switch (feature(m, x, y)) {
        case 1: s = L"▲"; c = RGB(110, 230, 120); break;     /* stairs up */
        case 2: s = L"▼"; c = RGB(110, 230, 120); break;     /* stairs down */
        case 4: s = L"泉"; c = RGB(110, 200, 255); break;    /* fountain */
        case 8: s = L"▽"; c = RGB(220, 200, 120); break;     /* ladder down */
        case 9: s = L"△"; c = RGB(220, 200, 120); break;     /* ladder up */
        case 14: s = L"×"; c = RGB(255, 90, 90); break;     /* pit down */
        case 15: s = L"○"; c = RGB(255, 140, 90); break;    /* hole up */
        case 17: s = L"◎"; c = RGB(255, 120, 90); break;    /* hole up and down */
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
    SelectClipRgn(dc, NULL); DeleteObject(clip);
}

static void draw_legend(HDC dc, RECT *r)
{
    static const struct { const wchar_t *s; COLORREF c; const wchar_t *t; } L[] = {
        { L"▲", RGB(110, 230, 120), L"上り" }, { L"▼", RGB(110, 230, 120), L"下り" },
        { L"△", RGB(220, 200, 120), L"はしご上" }, { L"▽", RGB(220, 200, 120), L"はしご下" },
        { L"━", RGB(240, 140, 50), L"扉" }, { L"┅", RGB(120, 170, 255), L"鉄格子" },
        { L"━", RGB(90, 190, 90), L"木・生け垣" }, { L"泉", RGB(110, 200, 255), L"" },
        { L"×", RGB(255, 90, 90), L"落とし穴" }, { L"○", RGB(255, 140, 90), L"上への穴" },
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
    if (!g_proc) wcscpy(s, g_badver ? L"この版の WIZARD.EXE には対応していません（Gold 1.1b 用）"
                                    : L"ゲームが見つかりません（Wizardry Gold を起動してください）");
    else if (!g_inmaze) swprintf(s, 256, L"ゲームを始めると現在地を表示します");
    else swprintf(s, 256, L"%s　東西 %d・南北 %d　%s向き", MAPNAME[g_map], g_x, g_y, DIRNAME[g_face]);
    if (m >= 0 && g_hover_x != -9999) {
        Note *n = note_at(m, g_hover_x, g_hover_y);
        if (n) swprintf(s, 256, L"メモ: %s", n->text);
    }
    SetWindowTextW(g_status, s);
}

static void fill_combo(void)
{
    int sel = 0, n = 1;
    SendMessageW(g_combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(g_combo, CB_ADDSTRING, 0, (LPARAM)L"いる地域を表示");
    SendMessageW(g_combo, CB_SETITEMDATA, 0, (LPARAM)-1);
    for (int m = 0; m < NMAPS; m++) {
        if (!any_visited(m)) continue;
        SendMessageW(g_combo, CB_ADDSTRING, 0, (LPARAM)MAPNAME[m]);
        SendMessageW(g_combo, CB_SETITEMDATA, n, (LPARAM)m);
        if (m == g_view) sel = n;
        n++;
    }
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

static int ask_note(const wchar_t *title, const wchar_t *init)
{
    RECT wr; HWND h, e; MSG msg;
    GetWindowRect(g_wnd, &wr);
    h = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, L"W7MAPNOTE", title,
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
    write_vis();
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

static HWND g_found;
static BOOL CALLBACK find_game_wnd(HWND w, LPARAM lp)
{
    DWORD pid = 0; RECT r;
    (void)lp;
    GetWindowThreadProcessId(w, &pid);
    if (pid != g_pid || !IsWindowVisible(w) || GetWindow(w, GW_OWNER)) return TRUE;
    GetWindowRect(w, &r);
    if (r.right - r.left < 300) return TRUE;
    g_found = w;
    return FALSE;
}

static int place_beside_game(void)
{
    RECT gr, mr, wa;
    if (!g_pid) return 0;
    g_found = NULL;
    EnumWindows(find_game_wnd, 0);
    if (!g_found) return 0;
    GetWindowRect(g_found, &gr); GetWindowRect(g_wnd, &mr);
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int w = mr.right - mr.left, h = mr.bottom - mr.top, x = gr.right + 4, y = gr.top;
    if (x + w > wa.right) x = wa.right - w;
    if (y + h > wa.bottom) y = wa.bottom - h;
    if (y < wa.top) y = wa.top;
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
                                4, 3, 220, 400, h, (HMENU)100, NULL, NULL);
        g_btn_center = CreateWindowW(L"BUTTON", L"現在地へ", WS_CHILD | WS_VISIBLE, 230, 3, 80, 24, h, (HMENU)101, NULL, NULL);
        g_chk_top = CreateWindowW(L"BUTTON", L"いつも手前", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 318, 5, 100, 20, h, (HMENU)102, NULL, NULL);
        g_status = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP | SS_NOPREFIX, 0, 0, 10, 10, h, (HMENU)103, NULL, NULL);
        g_btn_clr1 = CreateWindowW(L"BUTTON", L"この地域の記録を消す", WS_CHILD | WS_VISIBLE, 4, 31, 160, 24, h, (HMENU)104, NULL, NULL);
        g_btn_clrall = CreateWindowW(L"BUTTON", L"全部の記録を消す", WS_CHILD | WS_VISIBLE, 170, 31, 140, 24, h, (HMENU)105, NULL, NULL);
        SendMessageW(g_btn_clr1, WM_SETFONT, (WPARAM)g_font_small, TRUE);
        SendMessageW(g_btn_clrall, WM_SETFONT, (WPARAM)g_font_small, TRUE);
        SendMessageW(g_combo, WM_SETFONT, (WPARAM)g_font_small, TRUE);
        SendMessageW(g_combo, CB_SETDROPPEDWIDTH, 360, 0);
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
        if (!g_proc) placed = 0;
        if (poll()) {
            if (g_follow && g_view < 0 && g_inmaze) { g_cx = g_x + 0.5; g_cy = g_y + 0.5; }
            int count = 0;
            for (int m = 0; m < NMAPS; m++) count += any_visited(m);
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
        if (g_cell < 6) g_cell = 6;
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
        else if (wp == VK_SUBTRACT || wp == VK_OEM_MINUS) { g_cell = g_cell > 6 ? g_cell - 2 : 6; InvalidateRect(h, NULL, FALSE); }
        else if (wp == VK_SPACE || wp == VK_HOME) SendMessageW(h, WM_COMMAND, 101, 0);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps), mdc; RECT cr, a, lg; HBITMAP bmp, ob;
        GetClientRect(h, &cr);
        mdc = CreateCompatibleDC(dc);
        bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
        ob = (HBITMAP)SelectObject(mdc, bmp);
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
        if (g_dirty) write_vis();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void write_bmp(const wchar_t *path, HDC mdc, HBITMAP bmp, int w, int hgt)
{
    BITMAPINFO bi; BITMAPFILEHEADER fh; FILE *f; DWORD *px = (DWORD *)malloc((size_t)w * hgt * 4);
    if (!px) return;
    ZeroMemory(&bi, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = hgt;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    GetDIBits(mdc, bmp, 0, hgt, px, &bi, DIB_RGB_COLORS);
    f = _wfopen(path, L"wb");
    if (f) {
        ZeroMemory(&fh, sizeof fh);
        fh.bfType = 0x4D42; fh.bfOffBits = sizeof fh + sizeof bi.bmiHeader; fh.bfSize = fh.bfOffBits + w * hgt * 4;
        fwrite(&fh, sizeof fh, 1, f); fwrite(&bi.bmiHeader, sizeof bi.bmiHeader, 1, f); fwrite(px, 4, (size_t)w * hgt, f);
        fclose(f);
    }
    free(px);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmd, int show)
{
    WNDCLASSW wc; MSG msg; wchar_t exe[MAX_PATH], *s;
    HANDLE mutex;
    (void)prev; (void)show;
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    s = wcsrchr(exe, L'\\'); if (s) *s = 0;
    find_gamedir_from(exe);
    load_scenario();
    read_vis();

    g_font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, 0, 0, 0, SHIFTJIS_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Meiryo UI");
    g_font_small = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, SHIFTJIS_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Meiryo UI");
    g_font_mark = CreateFontW(-12, 0, 0, 0, FW_BOLD, 0, 0, 0, SHIFTJIS_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Meiryo UI");

#ifdef WIZTOOL_TEST   /* 診断: --probe は試験版だけ */
    if (cmd && wcsstr(cmd, L"--probe")) {      /* diagnostics: write what the program can read to probe.txt */
        wchar_t p[MAX_PATH]; FILE *f;
        attach();
        if (g_proc) poll();
        swprintf(p, MAX_PATH, L"%s\\probe.txt", exe);
        f = _wfopen(p, L"w, ccs=UTF-8");
        if (f) {
            fwprintf(f, L"gamedir=%s have=%d pid=%lu badver=%d\n", g_gamedir, g_have, g_pid, g_badver);
            if (g_proc) {
                fwprintf(f, L"level=%u x=%u y=%u qx=%u qy=%u q=%u face=%u recp=%08lx allp=%08lx inmaze=%d\n",
                         rw(A_LEVEL), rw(A_X), rw(A_Y), rw(0x4a0ffc), rw(0x4a1000), rw(0x4a1004), rw(A_FACE),
                         rdw(A_RECPTR), rdw(A_VISALL), g_inmaze);
                for (int m = 0; m < NMAPS; m++) {
                    int n = 0;
                    for (int i = 0; i < 1024; i++) n += (g_vis[m][i >> 3] >> (i & 7)) & 1;
                    if (n) fwprintf(f, L"map %d: %d squares\n", m, n);
                }
            }
            fclose(f);
        }
        return 0;
    }
#endif
#ifdef WIZTOOL_TEST   /* 診断: --snapshot は試験版だけ */
    if (cmd && wcsstr(cmd, L"--snapshot")) {   /* diagnostics: draw the current map into snapshot.bmp */
        wchar_t p[MAX_PATH]; HDC sdc = GetDC(NULL), mdc = CreateCompatibleDC(sdc);
        HBITMAP bmp = CreateCompatibleBitmap(sdc, 500, 520); RECT a = { 0, 0, 500, 480 }, lg = { 0, 480, 500, 520 };
        int want = -1; const wchar_t *mp = wcsstr(cmd, L"--map=");
        if (mp) want = _wtoi(mp + 6);
        attach(); poll(); load_notes();
        if (want >= 0 && want < NMAPS) { g_view = want; centre_on_visited(want); }
        else if (g_inmaze) { g_cx = g_x + 0.5; g_cy = g_y + 0.5; }
        if (wcsstr(cmd, L"--all")) memset(g_vis, 0xFF, sizeof g_vis);   /* show the whole map (for checking the drawing) */
        if (wcsstr(cmd, L"--all") && want >= 0) centre_on_visited(want);
        if (wcsstr(cmd, L"--cell=")) g_cell = _wtoi(wcsstr(cmd, L"--cell=") + 7);
        SelectObject(mdc, bmp);
        draw_map(mdc, &a); draw_legend(mdc, &lg);
        swprintf(p, MAX_PATH, L"%s\\snapshot.bmp", exe);
        write_bmp(p, mdc, bmp, 500, 520);
        return 0;
    }
#endif
    load_notes();

    mutex = CreateMutexW(NULL, TRUE, L"Wiz7JpAutomap");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND w = FindWindowW(L"W7MAP", NULL);
        if (w) { ShowWindow(w, SW_SHOWNOACTIVATE); SetForegroundWindow(w); }
        return 0;
    }

    ZeroMemory(&wc, sizeof wc);
    wc.lpfnWndProc = WndProc; wc.hInstance = inst; wc.lpszClassName = L"W7MAP";
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW); wc.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    RegisterClassW(&wc);
    wc.lpfnWndProc = NoteProc; wc.lpszClassName = L"W7MAPNOTE"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassW(&wc);

    g_wnd = CreateWindowExW(WS_EX_TOPMOST, L"W7MAP", APP_TITLE, WS_OVERLAPPEDWINDOW,
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
