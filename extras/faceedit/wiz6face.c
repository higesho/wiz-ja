/* ウィザードリィ6 顔グラエディタ (Wiz6Face.exe)
 *
 * BANE\WPORT1.EGA / WPORT2.EGA / WPORT3.EGA (EGA 用の顔グラ) を編集する。
 *   1 ファイル = 14 人分。1 人分 = 24x24 ドット = 3x3 タイル (左上から右へ、上から下へ)。
 *   1 タイル = 32 バイト = 4 枚のビットプレーン x 8 行 (プレーン 0 が先、上位ビットが左)。
 *   14 x 288 = 4032 バイトのあとに 64 バイトの空き (そのまま残す)。
 *   キャラクターの顔グラの番号 0..41 = ファイル (番号 / 14) の (番号 % 14) 人目。
 * 形式は martydill 氏の Wizardry-6-reverse-engineering (WPORT_EGA.md) による。
 *
 * 保存すると、最初の 1 回だけ元のファイルを WPORTn.EGA.ORIG として残す。
 * 画像の読み込み・書き出しは GDI+、貼り付けはクリップボードの画像。
 * 全員まとめての画像 = 7 列 x 6 行に 24x24 を隙間なく並べた 168x144 (またはその整数倍)。
 *
 * 診断（試験版 build.bat test だけ）: --selftest [--savetest] (読み書きの往復などを確かめて selftest.txt に書く)
 *       --snapshot [--sel=N]    (画面を snapshot.bmp に描く)
 */
#include <windows.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define APP_TITLE L"ウィザードリィ6 顔グラ"
#define NFILE 3
#define PER_FILE 14
#define NPORT (NFILE * PER_FILE)
#define PW 24
#define FRAME 288
#define FILESZ 4096
#define SHEET_C 7
#define SHEET_R 6

/* ---------------------------------------------------------------- GDI+ (flat API) */
typedef struct { UINT32 GdiplusVersion; void *DebugEventCallback; BOOL SuppressBackgroundThread; BOOL SuppressExternalCodecs; } GpStartupInput;
int __stdcall GdiplusStartup(ULONG_PTR *token, const GpStartupInput *in, void *out);
void __stdcall GdiplusShutdown(ULONG_PTR token);
int __stdcall GdipCreateBitmapFromFile(const WCHAR *name, void **bmp);
int __stdcall GdipCreateHBITMAPFromBitmap(void *bmp, HBITMAP *hbm, DWORD background);
int __stdcall GdipDisposeImage(void *img);
int __stdcall GdipCreateBitmapFromScan0(INT w, INT h, INT stride, INT fmt, BYTE *scan0, void **bmp);
int __stdcall GdipSaveImageToFile(void *img, const WCHAR *name, const CLSID *enc, const void *params);
static const CLSID CLSID_PNG = { 0x557cf406, 0x1a04, 0x11d3, { 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };
#define PixelFormat32bppRGB 0x00022009
static ULONG_PTR g_gdip;

/* ---------------------------------------------------------------- data */
/* game palette (DOSBox capture of the running game) */
static const COLORREF PAL[16] = {
    RGB(0, 0, 0), RGB(255, 255, 255), RGB(85, 85, 255), RGB(255, 85, 255),
    RGB(255, 85, 85), RGB(255, 255, 85), RGB(85, 255, 85), RGB(85, 255, 255),
    RGB(85, 85, 85), RGB(170, 170, 170), RGB(0, 0, 170), RGB(170, 0, 170),
    RGB(170, 0, 0), RGB(170, 85, 0), RGB(0, 170, 0), RGB(0, 170, 170) };
#define BG_INDEX 15       /* the cyan background of the portraits */
#define ERASE_INDEX 1     /* the eraser paints white */
#define ERASE_SIZE 3      /* 3x3 dots */

/* CRC-32 of each original 288-byte frame (Steam release). Only the checksums are kept here, not the
   pictures: they let the editor recognise the original picture wherever it is (.ORIG, or any slot). */
static const DWORD DEF_CRC[NPORT] = {
    0x573a0551, 0xea4ec870, 0x31c87aa7, 0xe0ad769e, 0x49e137b0, 0x896547ea, 0x02f977cc, 0xc3b6f78e,
    0x46ab12e3, 0x7dcf2c39, 0x0500c757, 0xbf984600, 0x1591f820, 0x19df9b53, 0xc8769b85, 0xff93429c,
    0xb701b4d7, 0xc207465f, 0x67c2f3a2, 0x2c161517, 0x299d2fcd, 0x509e5c34, 0x1d188bc5, 0xe951b7ae,
    0x8f190d99, 0xa958b872, 0xad6f4f00, 0xf4615432, 0xbecb0022, 0x1e0fc5ba, 0x003773bc, 0x717d49a4,
    0x84e103cf, 0xd363222f, 0x42faea8e, 0x162a10ac, 0xdbb889cd, 0xd0727ceb, 0x03ad89ad, 0xf241102e,
    0xbb7c4425, 0x88f062bc
};

typedef BYTE Pic[PW][PW];
static wchar_t g_dir[MAX_PATH];                  /* folder that holds WPORT1.EGA */
static BYTE g_file[NFILE][FILESZ];               /* file images (tail bytes kept) */
static Pic g_pix[NPORT];                         /* current pictures */
static Pic g_saved[NPORT];                       /* as on disk */
static Pic g_base[NPORT];                        /* the original picture */
static int g_base_ok[NPORT];                     /* 1 = the original picture was found */
static int g_loaded;

static int g_sel = 0;                            /* selected portrait */
static int g_colL = 0, g_colR = BG_INDEX;        /* left / right button colours */
enum { T_PEN, T_ERASE, T_FILL, T_PICK, T_LINE, T_RECT, NTOOL };
static int g_tool = T_PEN;
static int g_grid = 1, g_dither = 0;

/* undo: every step keeps all 42 pictures (24 KB) */
#define NUNDO 100
typedef struct { int sel; Pic pix[NPORT]; } Snap;
static Snap g_undo[NUNDO], g_redo[NUNDO];
static int g_nundo, g_nredo;

/* clipboard inside the program (whole portrait) */
static Pic g_clip;
static int g_haveclip;

/* ---------------------------------------------------------------- layout */
#define LX 12
#define LY 12
#define LCOLS 7
#define LCW 58
#define LCH 66
#define CX 432
#define CY 12
#define CS 15                                     /* canvas cell size */
#define PVY (CY + PW * CS + 14)                    /* previews */
#define RX (CX + PW * CS + 22)                     /* right panel */
#define SW 38                                     /* palette swatch */
#define CLIENT_W (RX + 4 * SW + 16)
#define STATUS_Y 548
#define CLIENT_H (STATUS_Y + 30)
#define GVX (CX + 166)                             /* game-look preview: 3x wide, 3.6x tall */
#define GVW (PW * 3)
#define GVH 86
#define EBX (CX + 256)                             /* flip / shift buttons */

static HWND g_wnd, g_status;
static HFONT g_font, g_font_small;
static HWND g_btn_tool[NTOOL], g_chk_grid, g_chk_dither;
enum { ID_PEN = 100, ID_ERASE, ID_FILL, ID_PICK, ID_LINE, ID_RECT,
       ID_GRID = 120, ID_DITHER, ID_SAVE, ID_UNDO, ID_REDO, ID_COPY, ID_PASTE,
       ID_IMPORT, ID_EXPORT, ID_REVERT, ID_SWAPCOL, ID_SHEET_IN, ID_SHEET_OUT,
       ID_FLIPH, ID_FLIPV, ID_UP, ID_DOWN, ID_LEFT, ID_RIGHT };

/* ---------------------------------------------------------------- file format */
static void decode(const BYTE *f, Pic pix)
{
    int x, y, p;
    for (y = 0; y < PW; y++)
        for (x = 0; x < PW; x++) {
            const BYTE *t = f + ((y >> 3) * 3 + (x >> 3)) * 32;
            int c = 0;
            for (p = 0; p < 4; p++) c |= ((t[p * 8 + (y & 7)] >> (7 - (x & 7))) & 1) << p;
            pix[y][x] = (BYTE)c;
        }
}

static void encode(const Pic pix, BYTE *f)
{
    int x, y, p;
    memset(f, 0, FRAME);
    for (y = 0; y < PW; y++)
        for (x = 0; x < PW; x++) {
            BYTE *t = f + ((y >> 3) * 3 + (x >> 3)) * 32;
            for (p = 0; p < 4; p++)
                if ((pix[y][x] >> p) & 1) t[p * 8 + (y & 7)] |= (BYTE)(0x80 >> (x & 7));
        }
}

static void fpath(wchar_t *out, int n, const wchar_t *ext)
{
    swprintf(out, MAX_PATH, L"%s\\WPORT%d.EGA%s", g_dir, n + 1, ext);
}

static int read_file(const wchar_t *p, BYTE *buf)
{
    FILE *f = _wfopen(p, L"rb"); size_t n;
    if (!f) return 0;
    n = fread(buf, 1, FILESZ, f);
    fclose(f);
    return n == FILESZ;
}

static int file_exists(const wchar_t *p)
{
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static DWORD crc32(const BYTE *p, int n)
{
    DWORD c = 0xffffffff; int k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320 & (0 - (c & 1)));
    }
    return ~c;
}

/* look for the original picture of every face among the frames of the .ORIG files and the current files */
static void find_originals(void)
{
    static BYTE orig[NFILE][FILESZ];
    int n, k, i, have[NFILE]; wchar_t p[MAX_PATH];
    for (n = 0; n < NFILE; n++) { fpath(p, n, L".ORIG"); have[n] = read_file(p, orig[n]); }
    for (i = 0; i < NPORT; i++) {
        g_base_ok[i] = 0;
        for (n = 0; n < NFILE * 2 && !g_base_ok[i]; n++) {
            const BYTE *f = n < NFILE ? (have[n] ? orig[n] : NULL) : g_file[n - NFILE];
            if (!f) continue;
            for (k = 0; k < PER_FILE; k++)
                if (crc32(f + k * FRAME, FRAME) == DEF_CRC[i]) { decode(f + k * FRAME, g_base[i]); g_base_ok[i] = 1; break; }
        }
        if (!g_base_ok[i]) memset(g_base[i], BG_INDEX, sizeof(Pic));
    }
}

static int load_all(void)
{
    int n, k; wchar_t p[MAX_PATH];
    for (n = 0; n < NFILE; n++) {
        fpath(p, n, L"");
        if (!read_file(p, g_file[n])) return 0;
    }
    for (n = 0; n < NFILE; n++)
        for (k = 0; k < PER_FILE; k++) decode(g_file[n] + k * FRAME, g_pix[n * PER_FILE + k]);
    find_originals();
    memcpy(g_saved, g_pix, sizeof g_pix);
    g_loaded = 1;
    return 1;
}

static int changed_from_original(int i)
{
    return !g_base_ok[i] || memcmp(g_pix[i], g_base[i], sizeof(Pic)) != 0;
}

static int file_dirty(int n)
{
    return memcmp(g_pix[n * PER_FILE], g_saved[n * PER_FILE], sizeof(Pic) * PER_FILE) != 0;
}

static int any_dirty(void)
{
    return memcmp(g_pix, g_saved, sizeof g_pix) != 0;
}

/* returns 1 on success; err gets a message */
static int save_all(wchar_t *err, int errn)
{
    int n, k; wchar_t p[MAX_PATH], b[MAX_PATH], t[MAX_PATH];
    for (n = 0; n < NFILE; n++) {
        static BYTE img[FILESZ]; FILE *f;
        if (!file_dirty(n)) continue;
        memcpy(img, g_file[n], FILESZ);
        for (k = 0; k < PER_FILE; k++) encode(g_pix[n * PER_FILE + k], img + k * FRAME);
        fpath(p, n, L""); fpath(b, n, L".ORIG"); fpath(t, n, L".TMP");
        if (!file_exists(b) && !CopyFileW(p, b, TRUE)) {
            swprintf(err, errn, L"元のファイルの控え（WPORT%d.EGA.ORIG）を作れませんでした。\nフォルダに書き込めるか確かめてください。", n + 1);
            return 0;
        }
        f = _wfopen(t, L"wb");
        if (!f || fwrite(img, 1, FILESZ, f) != FILESZ) {
            if (f) fclose(f);
            DeleteFileW(t);
            swprintf(err, errn, L"WPORT%d.EGA を保存できませんでした。\nフォルダに書き込めるか確かめてください。", n + 1);
            return 0;
        }
        fclose(f);
        if (!MoveFileExW(t, p, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(t);
            swprintf(err, errn, L"WPORT%d.EGA を書き換えられませんでした。\nゲームやほかのプログラムが開いていないか確かめてください。", n + 1);
            return 0;
        }
        memcpy(g_file[n], img, FILESZ);
        memcpy(g_saved[n * PER_FILE], g_pix[n * PER_FILE], sizeof(Pic) * PER_FILE);
    }
    return 1;
}

/* find the folder: <exe>\BANE, <exe>, <exe>\..\BANE */
static int find_dir(const wchar_t *exe)
{
    static const wchar_t *sub[] = { L"\\BANE", L"", L"\\..\\BANE" };
    int i; wchar_t p[MAX_PATH], full[MAX_PATH];
    for (i = 0; i < 3; i++) {
        swprintf(p, MAX_PATH, L"%s%s\\WPORT1.EGA", exe, sub[i]);
        if (file_exists(p)) {
            swprintf(p, MAX_PATH, L"%s%s", exe, sub[i]);
            if (GetFullPathNameW(p, MAX_PATH, full, NULL)) wcscpy(g_dir, full); else wcscpy(g_dir, p);
            return 1;
        }
    }
    return 0;
}

static int ask_dir(HWND owner)
{
    OPENFILENAMEW of; wchar_t name[MAX_PATH] = L"WPORT1.EGA", *s;
    ZeroMemory(&of, sizeof of);
    of.lStructSize = sizeof of; of.hwndOwner = owner;
    of.lpstrFilter = L"顔グラのファイル (WPORT1.EGA)\0WPORT1.EGA\0";
    of.lpstrFile = name; of.nMaxFile = MAX_PATH;
    of.lpstrTitle = L"ゲームの BANE フォルダにある WPORT1.EGA を選んでください";
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&of)) return 0;
    s = wcsrchr(name, L'\\'); if (s) *s = 0;
    wcscpy(g_dir, name);
    return 1;
}

/* ---------------------------------------------------------------- undo */
static void snap_push(Snap *st, int *n)
{
    if (*n == NUNDO) { memmove(st, st + 1, sizeof(Snap) * (NUNDO - 1)); (*n)--; }
    st[*n].sel = g_sel;
    memcpy(st[*n].pix, g_pix, sizeof g_pix);
    (*n)++;
}

static void push_undo(void)
{
    snap_push(g_undo, &g_nundo);
    g_nredo = 0;
}

static void undo_redo(Snap *from, int *nfrom, Snap *to, int *nto)
{
    Snap *s;
    if (!*nfrom) return;
    s = &from[--*nfrom];
    snap_push(to, nto);
    memcpy(g_pix, s->pix, sizeof g_pix);
    g_sel = s->sel;
}

/* drop the last undo step if it changed nothing */
static void undo_trim(void)
{
    if (g_nundo && !memcmp(g_undo[g_nundo - 1].pix, g_pix, sizeof g_pix)) g_nundo--;
}

/* ---------------------------------------------------------------- image import / export */
static int nearest(int r, int g, int b)
{
    int i, best = 0; long bd = 0x7fffffff;
    for (i = 0; i < 16; i++) {
        long dr = r - GetRValue(PAL[i]), dg = g - GetGValue(PAL[i]), db = b - GetBValue(PAL[i]);
        long d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

/* src: 0x00RRGGBB, top-down, row pitch = pitch pixels; region (rx, ry, w, h).
   center-crop to a square, area-average to 24x24, map to the palette */
static void region_to_portrait(const DWORD *src, int pitch, int rx, int ry, int w, int h, int dither, Pic out)
{
    static float acc[PW][PW][3];
    int s = w < h ? w : h, x0 = rx + (w - s) / 2, y0 = ry + (h - s) / 2, x, y, c;
    for (y = 0; y < PW; y++)
        for (x = 0; x < PW; x++) {
            int ax = x * s / PW, bx = (x + 1) * s / PW, ay = y * s / PW, by = (y + 1) * s / PW, i, j, n = 0;
            double r = 0, g = 0, b = 0;
            if (bx <= ax) bx = ax + 1;
            if (by <= ay) by = ay + 1;
            for (j = ay; j < by; j++)
                for (i = ax; i < bx; i++) {
                    DWORD p = src[(size_t)(y0 + j) * pitch + x0 + i];
                    r += (p >> 16) & 255; g += (p >> 8) & 255; b += p & 255; n++;
                }
            acc[y][x][0] = (float)(r / n); acc[y][x][1] = (float)(g / n); acc[y][x][2] = (float)(b / n);
        }
    for (y = 0; y < PW; y++)
        for (x = 0; x < PW; x++) {
            int r = (int)(acc[y][x][0] + 0.5f), g = (int)(acc[y][x][1] + 0.5f), b = (int)(acc[y][x][2] + 0.5f), k;
            float e[3];
            if (r < 0) r = 0; if (r > 255) r = 255;
            if (g < 0) g = 0; if (g > 255) g = 255;
            if (b < 0) b = 0; if (b > 255) b = 255;
            k = nearest(r, g, b);
            out[y][x] = (BYTE)k;
            if (!dither) continue;
            e[0] = acc[y][x][0] - GetRValue(PAL[k]); e[1] = acc[y][x][1] - GetGValue(PAL[k]); e[2] = acc[y][x][2] - GetBValue(PAL[k]);
            for (c = 0; c < 3; c++) {
                if (x + 1 < PW) acc[y][x + 1][c] += e[c] * 7 / 16;
                if (y + 1 < PW) {
                    if (x > 0) acc[y + 1][x - 1][c] += e[c] * 3 / 16;
                    acc[y + 1][x][c] += e[c] * 5 / 16;
                    if (x + 1 < PW) acc[y + 1][x + 1][c] += e[c] * 1 / 16;
                }
            }
        }
}

/* HBITMAP -> top-down 0x00RRGGBB pixels (caller frees) */
static DWORD *bitmap_pixels(HBITMAP hbm, int *w, int *h)
{
    BITMAP bm; BITMAPINFO bi; DWORD *px; HDC dc;
    if (!GetObjectW(hbm, sizeof bm, &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0) return NULL;
    if (bm.bmWidth > 8192 || bm.bmHeight > 8192) return NULL;
    px = (DWORD *)malloc((size_t)bm.bmWidth * bm.bmHeight * 4);
    if (!px) return NULL;
    ZeroMemory(&bi, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = bm.bmWidth; bi.bmiHeader.biHeight = -bm.bmHeight;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    dc = GetDC(NULL);
    if (!GetDIBits(dc, hbm, 0, bm.bmHeight, px, &bi, DIB_RGB_COLORS)) { ReleaseDC(NULL, dc); free(px); return NULL; }
    ReleaseDC(NULL, dc);
    *w = bm.bmWidth; *h = bm.bmHeight;
    return px;
}

/* image file -> pixels (transparent parts become the cyan background) */
static DWORD *load_image(const wchar_t *path, int *w, int *h)
{
    void *bmp = NULL; HBITMAP hbm = NULL; DWORD *px;
    if (GdipCreateBitmapFromFile(path, &bmp) != 0 || !bmp) return NULL;
    GdipCreateHBITMAPFromBitmap(bmp, &hbm, 0xFF000000 | ((DWORD)GetRValue(PAL[BG_INDEX]) << 16) |
                                ((DWORD)GetGValue(PAL[BG_INDEX]) << 8) | GetBValue(PAL[BG_INDEX]));
    GdipDisposeImage(bmp);
    if (!hbm) return NULL;
    px = bitmap_pixels(hbm, w, h);
    DeleteObject(hbm);
    return px;
}

static int import_file(const wchar_t *path, int dither, Pic out)
{
    int w, h; DWORD *px = load_image(path, &w, &h);
    if (!px) return 0;
    region_to_portrait(px, w, 0, 0, w, h, dither, out);
    free(px);
    return 1;
}

/* sheet: 7 x 6 square cells. returns 1 ok, 0 unreadable, -1 wrong shape */
static int import_sheet(const wchar_t *path, int dither, Pic out[NPORT])
{
    int w, h, i, cw; DWORD *px = load_image(path, &w, &h);
    if (!px) return 0;
    if (w % SHEET_C || h % SHEET_R || w / SHEET_C != h / SHEET_R || w / SHEET_C < PW) { free(px); return -1; }
    cw = w / SHEET_C;
    for (i = 0; i < NPORT; i++)
        region_to_portrait(px, w, (i % SHEET_C) * cw, (i / SHEET_C) * cw, cw, cw, dither, out[i]);
    free(px);
    return 1;
}

static int save_png(const wchar_t *path, const DWORD *buf, int w, int h)
{
    void *bmp = NULL; int ok;
    if (GdipCreateBitmapFromScan0(w, h, w * 4, PixelFormat32bppRGB, (BYTE *)buf, &bmp) != 0) return 0;
    ok = GdipSaveImageToFile(bmp, path, &CLSID_PNG, NULL) == 0;
    GdipDisposeImage(bmp);
    return ok;
}

static DWORD rgbx(int i)
{
    return ((DWORD)GetRValue(PAL[i]) << 16) | ((DWORD)GetGValue(PAL[i]) << 8) | GetBValue(PAL[i]);
}

static int export_png(const wchar_t *path, const Pic pix, int scale)
{
    static DWORD buf[PW * 8 * PW * 8];
    int x, y, n = PW * scale;
    for (y = 0; y < n; y++)
        for (x = 0; x < n; x++) buf[y * n + x] = rgbx(pix[y / scale][x / scale]);
    return save_png(path, buf, n, n);
}

static int export_sheet(const wchar_t *path, int scale)
{
    int w = SHEET_C * PW * scale, h = SHEET_R * PW * scale, x, y, ok;
    DWORD *buf = (DWORD *)malloc((size_t)w * h * 4);
    if (!buf) return 0;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int px = x / scale, py = y / scale, i = (py / PW) * SHEET_C + px / PW;
            buf[y * w + x] = rgbx(g_pix[i][py % PW][px % PW]);
        }
    ok = save_png(path, buf, w, h);
    free(buf);
    return ok;
}

/* system clipboard: copy as a 24x24 bitmap */
static void clipboard_copy(HWND w, const Pic pix)
{
    HGLOBAL g; BITMAPINFOHEADER *bh; DWORD *px; int x, y;
    if (!OpenClipboard(w)) return;
    EmptyClipboard();
    g = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + PW * PW * 4);
    if (g) {
        bh = (BITMAPINFOHEADER *)GlobalLock(g);
        ZeroMemory(bh, sizeof *bh);
        bh->biSize = sizeof *bh; bh->biWidth = PW; bh->biHeight = PW; bh->biPlanes = 1; bh->biBitCount = 32;
        bh->biCompression = BI_RGB; bh->biSizeImage = PW * PW * 4;
        px = (DWORD *)(bh + 1);
        for (y = 0; y < PW; y++)
            for (x = 0; x < PW; x++) px[y * PW + x] = rgbx(pix[PW - 1 - y][x]);
        GlobalUnlock(g);
        if (!SetClipboardData(CF_DIB, g)) GlobalFree(g);
    }
    CloseClipboard();
}

static int clipboard_paste(HWND w, int dither, Pic out)
{
    HBITMAP hbm; DWORD *px = NULL; int iw, ih, ok = 0;
    if (!IsClipboardFormatAvailable(CF_BITMAP) && !IsClipboardFormatAvailable(CF_DIB)) return 0;
    if (!OpenClipboard(w)) return 0;
    hbm = (HBITMAP)GetClipboardData(CF_BITMAP);
    if (hbm) px = bitmap_pixels(hbm, &iw, &ih);
    CloseClipboard();
    if (px) { region_to_portrait(px, iw, 0, 0, iw, ih, dither, out); free(px); ok = 1; }
    return ok;
}

/* ---------------------------------------------------------------- drawing */
static DWORD *g_bits; static int g_bw, g_bh;     /* back buffer (DIB section) */
static int g_listdrag = -1, g_listhover = -1;     /* swapping portraits in the list */

static void fill(int x, int y, int w, int h, COLORREF c)
{
    int i, j; DWORD v = ((DWORD)GetRValue(c) << 16) | ((DWORD)GetGValue(c) << 8) | GetBValue(c);
    for (j = y < 0 ? 0 : y; j < y + h && j < g_bh; j++)
        for (i = x < 0 ? 0 : x; i < x + w && i < g_bw; i++) g_bits[j * g_bw + i] = v;
}

static void frame(int x, int y, int w, int h, int t, COLORREF c)
{
    fill(x, y, w, t, c); fill(x, y + h - t, w, t, c); fill(x, y, t, h, c); fill(x + w - t, y, t, h, c);
}

static void blit_portrait(const Pic pix, int x0, int y0, int s)
{
    int x, y;
    for (y = 0; y < PW; y++)
        for (x = 0; x < PW; x++) fill(x0 + x * s, y0 + y * s, s, s, PAL[pix[y][x]]);
}

/* any size (nearest neighbour): used for the game-look preview */
static void blit_scaled(const Pic pix, int x0, int y0, int w, int h)
{
    int x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) fill(x0 + x, y0 + y, 1, 1, PAL[pix[y * PW / h][x * PW / w]]);
}

static void list_cell(int i, int *x, int *y)
{
    *x = LX + (i % LCOLS) * LCW; *y = LY + (i / LCOLS) * LCH;
}

static int list_hit(int mx, int my)
{
    int i, x, y;
    for (i = 0; i < NPORT; i++) {
        list_cell(i, &x, &y);
        if (mx >= x - 3 && my >= y - 3 && mx < x + 51 && my < y + 64) return i;
    }
    return -1;
}

static void text_at(HDC dc, int x, int y, const wchar_t *s, COLORREF c, HFONT f)
{
    SelectObject(dc, f); SetTextColor(dc, c); SetBkMode(dc, TRANSPARENT);
    TextOutW(dc, x, y, s, (int)wcslen(s));
}

static void render(HDC dc, int cw, int ch)
{
    BITMAPINFO bi; HBITMAP bmp, old; HDC mdc; void *bits; int i, x, y; wchar_t s[64];
    const COLORREF face = GetSysColor(COLOR_BTNFACE);
    ZeroMemory(&bi, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = cw; bi.bmiHeader.biHeight = -ch;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp) return;
    g_bits = (DWORD *)bits; g_bw = cw; g_bh = ch;
    fill(0, 0, cw, ch, face);

    /* list */
    for (i = 0; i < NPORT; i++) {
        list_cell(i, &x, &y);
        if (i == g_sel) fill(x - 3, y - 3, 54, 54, RGB(255, 200, 0));
        if (g_listdrag >= 0 && i == g_listhover && i != g_listdrag) fill(x - 3, y - 3, 54, 54, RGB(0, 120, 255));
        blit_portrait(g_pix[i], x, y, 2);
    }
    /* canvas */
    blit_portrait(g_pix[g_sel], CX, CY, CS);
    if (g_grid) {
        for (i = 0; i <= PW; i++) {
            COLORREF c = (i % 8 == 0) ? RGB(60, 60, 60) : RGB(120, 120, 120);
            if (i % 8 == 0 || i == PW) { fill(CX + i * CS - (i == PW), CY, 1, PW * CS, c); fill(CX, CY + i * CS - (i == PW), PW * CS, 1, c); }
            else {
                int k;
                for (k = 0; k < PW * CS; k += 2) { fill(CX + i * CS, CY + k, 1, 1, c); fill(CX + k, CY + i * CS, 1, 1, c); }
            }
        }
    }
    frame(CX - 2, CY - 2, PW * CS + 4, PW * CS + 4, 2, RGB(40, 40, 40));
    /* previews: 1x, 2x, 3x, and as DOSBox shows it (pixels 1.2x taller) */
    blit_portrait(g_pix[g_sel], CX, PVY + 22, 1);
    blit_portrait(g_pix[g_sel], CX + 30, PVY + 22, 2);
    blit_portrait(g_pix[g_sel], CX + 84, PVY + 22, 3);
    blit_scaled(g_pix[g_sel], GVX, PVY + 22, GVW, GVH);
    /* palette */
    for (i = 0; i < 16; i++) {
        int sx = RX + (i % 4) * SW, sy = CY + (i / 4) * SW;
        fill(sx + 2, sy + 2, SW - 4, SW - 4, PAL[i]);
        frame(sx + 2, sy + 2, SW - 4, SW - 4, 1, RGB(0, 0, 0));
        if (i == g_colL) frame(sx, sy, SW, SW, 2, RGB(255, 0, 0));
        if (i == g_colR) frame(sx + 4, sy + 4, SW - 8, SW - 8, 2, RGB(0, 90, 255));
    }
    /* current colours */
    fill(RX + 2, CY + 4 * SW + 26, 50, 30, PAL[g_colL]); frame(RX + 2, CY + 4 * SW + 26, 50, 30, 2, RGB(255, 0, 0));
    fill(RX + 66, CY + 4 * SW + 26, 50, 30, PAL[g_colR]); frame(RX + 66, CY + 4 * SW + 26, 50, 30, 2, RGB(0, 90, 255));

    mdc = CreateCompatibleDC(dc);
    old = (HBITMAP)SelectObject(mdc, bmp);
    for (i = 0; i < NPORT; i++) {
        list_cell(i, &x, &y);
        swprintf(s, 64, L"%d", i + 1);
        text_at(mdc, x, y + 49, s, RGB(0, 0, 0), g_font_small);
        if (changed_from_original(i)) text_at(mdc, x + 26, y + 49, L"●", RGB(0, 90, 230), g_font_small);
        if (memcmp(g_pix[i], g_saved[i], sizeof(Pic))) text_at(mdc, x + 40, y + 49, L"●", RGB(220, 20, 20), g_font_small);
    }
    text_at(mdc, CX, PVY, L"原寸・2倍・3倍", RGB(0, 0, 0), g_font_small);
    text_at(mdc, GVX, PVY, L"ゲーム画面", RGB(0, 0, 0), g_font_small);
    text_at(mdc, EBX + 33, PVY + 97, L"ずらす", RGB(0, 0, 0), g_font_small);
    text_at(mdc, RX + 4, CY + 4 * SW + 6, L"左ボタン", RGB(160, 0, 0), g_font_small);
    text_at(mdc, RX + 68, CY + 4 * SW + 6, L"右ボタン", RGB(0, 60, 180), g_font_small);
    BitBlt(dc, 0, 0, cw, ch, mdc, 0, 0, SRCCOPY);
    SelectObject(mdc, old);
    DeleteDC(mdc);
    DeleteObject(bmp);
    g_bits = NULL;
}

static void update_title(void)
{
    wchar_t s[128];
    swprintf(s, 128, L"%s%s", any_dirty() ? L"* " : L"", APP_TITLE);
    SetWindowTextW(g_wnd, s);
}

static const wchar_t *TOOL_HINT[NTOOL] = {
    L"ペン: 左右のボタンの色で描く",
    L"消しゴム: 3×3 ドットを白にする",
    L"塗りつぶし: 同じ色でつながった所を塗る",
    L"スポイト: クリックした点の色を拾う",
    L"直線: ドラッグで線を引く",
    L"四角: ドラッグで四角を描く（Shift で中も塗る）" };

static void update_status(void)
{
    wchar_t s[300];
    int f = g_sel / PER_FILE, k = g_sel % PER_FILE;
    if (g_listdrag >= 0)
        swprintf(s, 300, L"  %d番をほかの顔グラの上で離すと、入れ替えます", g_listdrag + 1);
    else
        swprintf(s, 300, L"  %d番（WPORT%d.EGA の %d 人目）%s%s　　%s　　青い●＝元の絵から変えた顔　赤い●＝保存していない顔",
                 g_sel + 1, f + 1, k + 1,
                 changed_from_original(g_sel) ? L"　元の絵から変えてあります" : L"　元の絵のまま",
                 memcmp(g_pix[g_sel], g_saved[g_sel], sizeof(Pic)) ? L"（保存していない）" : L"",
                 TOOL_HINT[g_tool]);
    if (g_status) SetWindowTextW(g_status, s);
}

static void refresh(void)
{
    if (!g_wnd) return;
    InvalidateRect(g_wnd, NULL, FALSE);
    update_title();
    update_status();
}

/* ---------------------------------------------------------------- editing */
static void put_px(int x, int y, int c)
{
    if (x >= 0 && y >= 0 && x < PW && y < PW) g_pix[g_sel][y][x] = (BYTE)c;
}

static void stamp(int x, int y, int c, int size)
{
    int i, j, h = size / 2;
    for (j = 0; j < size; j++)
        for (i = 0; i < size; i++) put_px(x - h + i, y - h + j, c);
}

static void line_px(int x0, int y0, int x1, int y1, int c, int size)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) {
        stamp(x0, y0, c, size);
        if (x0 == x1 && y0 == y1) break;
        if (2 * e >= dy) { e += dy; x0 += sx; }
        if (2 * e <= dx) { e += dx; y0 += sy; }
    }
}

static void rect_px(int x0, int y0, int x1, int y1, int c, int filled)
{
    int x, y, t;
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
            if (filled || y == y0 || y == y1 || x == x0 || x == x1) put_px(x, y, c);
}

static void flood(int x, int y, int c)
{
    static short st[PW * PW * 4][2]; int n = 0, from = g_pix[g_sel][y][x];
    if (from == c) return;
    st[n][0] = (short)x; st[n][1] = (short)y; n++;
    while (n) {
        n--; x = st[n][0]; y = st[n][1];
        if (x < 0 || y < 0 || x >= PW || y >= PW || g_pix[g_sel][y][x] != from) continue;
        g_pix[g_sel][y][x] = (BYTE)c;
        st[n][0] = (short)(x + 1); st[n][1] = (short)y; n++;
        st[n][0] = (short)(x - 1); st[n][1] = (short)y; n++;
        st[n][0] = (short)x; st[n][1] = (short)(y + 1); n++;
        st[n][0] = (short)x; st[n][1] = (short)(y - 1); n++;
    }
}

static void flip(int horiz)
{
    Pic t; int x, y;
    push_undo();
    memcpy(t, g_pix[g_sel], sizeof t);
    for (y = 0; y < PW; y++)
        for (x = 0; x < PW; x++) g_pix[g_sel][y][x] = horiz ? t[y][PW - 1 - x] : t[PW - 1 - y][x];
    refresh();
}

/* move the picture by one dot; what goes off one edge comes back on the other */
static void shift(int dx, int dy)
{
    Pic t; int x, y;
    push_undo();
    memcpy(t, g_pix[g_sel], sizeof t);
    for (y = 0; y < PW; y++)
        for (x = 0; x < PW; x++) g_pix[g_sel][(y + dy + PW) % PW][(x + dx + PW) % PW] = t[y][x];
    refresh();
}

static int canvas_cell(int mx, int my, int *px, int *py)
{
    *px = (mx - CX) >= 0 ? (mx - CX) / CS : -1;
    *py = (my - CY) >= 0 ? (my - CY) / CS : -1;
    return mx >= CX && my >= CY && mx < CX + PW * CS && my < CY + PW * CS;
}

static void set_tool(int t)
{
    int i;
    g_tool = t;
    for (i = 0; i < NTOOL; i++) SendMessageW(g_btn_tool[i], BM_SETCHECK, i == t ? BST_CHECKED : BST_UNCHECKED, 0);
    update_status();
}

/* canvas drag state */
static int g_drag = 0, g_dragcol, g_sx, g_sy, g_lx, g_ly, g_dragtool;
static Pic g_dragbase;

static void mouse_down(int mx, int my, int right, int ctrl)
{
    int px, py, i;
    /* list: select; dragging onto another one swaps them */
    i = list_hit(mx, my);
    if (i >= 0) {
        g_sel = i;
        refresh();
        if (!right) { g_listdrag = i; g_listhover = i; SetCapture(g_wnd); return; }
        {   /* right click: menu for this face */
            HMENU m = CreatePopupMenu(); POINT pt; pt.x = mx; pt.y = my;
            AppendMenuW(m, MF_STRING | (changed_from_original(i) ? 0 : MF_GRAYED), ID_REVERT, L"この顔を元の絵に戻す");
            AppendMenuW(m, MF_SEPARATOR, 0, NULL);
            AppendMenuW(m, MF_STRING, ID_COPY, L"コピー");
            AppendMenuW(m, MF_STRING, ID_PASTE, L"貼り付け");
            AppendMenuW(m, MF_STRING, ID_EXPORT, L"画像に書き出す…");
            AppendMenuW(m, MF_STRING, ID_IMPORT, L"画像を読み込む…");
            ClientToScreen(g_wnd, &pt);
            TrackPopupMenu(m, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_wnd, NULL);
            DestroyMenu(m);
        }
        return;
    }
    /* palette */
    for (i = 0; i < 16; i++) {
        int sx = RX + (i % 4) * SW, sy = CY + (i / 4) * SW;
        if (mx >= sx && my >= sy && mx < sx + SW && my < sy + SW) {
            if (right) g_colR = i; else g_colL = i;
            refresh(); return;
        }
    }
    if (!canvas_cell(mx, my, &px, &py)) return;
    if (ctrl || g_tool == T_PICK) {
        if (right) g_colR = g_pix[g_sel][py][px]; else g_colL = g_pix[g_sel][py][px];
        refresh(); return;
    }
    push_undo();
    if (g_tool == T_FILL) { flood(px, py, right ? g_colR : g_colL); undo_trim(); refresh(); return; }
    g_drag = 1; g_dragtool = g_tool;
    g_dragcol = g_tool == T_ERASE ? ERASE_INDEX : (right ? g_colR : g_colL);
    g_sx = g_lx = px; g_sy = g_ly = py;
    memcpy(g_dragbase, g_pix[g_sel], sizeof g_dragbase);
    if (g_tool == T_PEN) put_px(px, py, g_dragcol);
    else if (g_tool == T_ERASE) stamp(px, py, g_dragcol, ERASE_SIZE);
    else put_px(px, py, g_dragcol);
    SetCapture(g_wnd);
    refresh();
}

static void mouse_move(int mx, int my, int shift_key)
{
    int px, py;
    if (g_listdrag >= 0) {
        int h = list_hit(mx, my);
        if (h != g_listhover) { g_listhover = h; InvalidateRect(g_wnd, NULL, FALSE); }
        return;
    }
    if (!g_drag) return;
    canvas_cell(mx, my, &px, &py);
    if (g_dragtool == T_LINE || g_dragtool == T_RECT) {
        if (px < 0) px = 0; if (py < 0) py = 0;
        if (px >= PW) px = PW - 1; if (py >= PW) py = PW - 1;
        memcpy(g_pix[g_sel], g_dragbase, sizeof g_dragbase);
        if (g_dragtool == T_LINE) line_px(g_sx, g_sy, px, py, g_dragcol, 1);
        else rect_px(g_sx, g_sy, px, py, g_dragcol, shift_key);
        g_lx = px; g_ly = py;
        InvalidateRect(g_wnd, NULL, FALSE);
        return;
    }
    if (px == g_lx && py == g_ly) return;
    line_px(g_lx, g_ly, px, py, g_dragcol, g_dragtool == T_ERASE ? ERASE_SIZE : 1);
    g_lx = px; g_ly = py;
    InvalidateRect(g_wnd, NULL, FALSE);
}

static void mouse_up(int mx, int my)
{
    if (g_listdrag >= 0) {
        int a = g_listdrag, b = list_hit(mx, my);
        g_listdrag = -1; g_listhover = -1;
        ReleaseCapture();
        if (b >= 0 && b != a) {
            Pic t;
            push_undo();
            memcpy(t, g_pix[a], sizeof t); memcpy(g_pix[a], g_pix[b], sizeof t); memcpy(g_pix[b], t, sizeof t);
            g_sel = b;
        }
        refresh();
        return;
    }
    if (!g_drag) return;
    g_drag = 0;
    ReleaseCapture();
    undo_trim();
    refresh();
}

static void do_save(void)
{
    wchar_t err[512];
    if (!any_dirty()) { MessageBoxW(g_wnd, L"保存していない変更はありません。", APP_TITLE, MB_OK | MB_ICONINFORMATION); return; }
    if (!save_all(err, 512)) { MessageBoxW(g_wnd, err, APP_TITLE, MB_OK | MB_ICONERROR); refresh(); return; }
    refresh();
    MessageBoxW(g_wnd, L"保存しました。\nゲームを起動し直すと、新しい顔グラで表示されます。\n（最初の保存のときの元のファイルは WPORT1〜3.EGA.ORIG として残してあります）",
                APP_TITLE, MB_OK | MB_ICONINFORMATION);
}

static int open_dialog(wchar_t *name, const wchar_t *title)
{
    OPENFILENAMEW of;
    name[0] = 0;
    ZeroMemory(&of, sizeof of);
    of.lStructSize = sizeof of; of.hwndOwner = g_wnd;
    of.lpstrFilter = L"画像 (PNG, BMP, GIF, JPEG)\0*.png;*.bmp;*.gif;*.jpg;*.jpeg\0すべてのファイル\0*.*\0";
    of.lpstrFile = name; of.nMaxFile = MAX_PATH; of.lpstrTitle = title;
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&of);
}

static int save_dialog(wchar_t *name, const wchar_t *filter, const wchar_t *title, int *filter_index)
{
    OPENFILENAMEW of;
    ZeroMemory(&of, sizeof of);
    of.lStructSize = sizeof of; of.hwndOwner = g_wnd;
    of.lpstrFilter = filter; of.lpstrFile = name; of.nMaxFile = MAX_PATH; of.lpstrDefExt = L"png";
    of.lpstrTitle = title;
    of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&of)) return 0;
    *filter_index = of.nFilterIndex;
    return 1;
}

static void do_import(void)
{
    wchar_t name[MAX_PATH]; Pic pix;
    if (!open_dialog(name, L"読み込む画像（24×24 ドットがおすすめ。違う大きさなら縮めます）")) return;
    if (!import_file(name, g_dither, pix)) { MessageBoxW(g_wnd, L"この画像は読み込めませんでした。", APP_TITLE, MB_OK | MB_ICONERROR); return; }
    push_undo();
    memcpy(g_pix[g_sel], pix, sizeof pix);
    refresh();
}

static void do_export(void)
{
    wchar_t name[MAX_PATH]; int fi;
    swprintf(name, MAX_PATH, L"face%02d.png", g_sel + 1);
    if (!save_dialog(name, L"PNG 画像 (24×24)\0*.png\0PNG 画像 (8倍の大きさ・192×192)\0*.png\0", L"顔グラを画像に書き出す", &fi)) return;
    if (!export_png(name, g_pix[g_sel], fi == 2 ? 8 : 1))
        MessageBoxW(g_wnd, L"書き出せませんでした。", APP_TITLE, MB_OK | MB_ICONERROR);
}

static void do_sheet_out(void)
{
    wchar_t name[MAX_PATH] = L"faces_all.png"; int fi;
    if (!save_dialog(name, L"PNG 画像 (168×144)\0*.png\0PNG 画像 (4倍の大きさ・672×576)\0*.png\0",
                     L"42 人分をまとめて 1 枚の画像に書き出す（7 列 × 6 行）", &fi)) return;
    if (!export_sheet(name, fi == 2 ? 4 : 1))
        MessageBoxW(g_wnd, L"書き出せませんでした。", APP_TITLE, MB_OK | MB_ICONERROR);
}

static void do_sheet_in(void)
{
    static Pic pix[NPORT]; wchar_t name[MAX_PATH]; int r;
    if (!open_dialog(name, L"42 人分をまとめた画像（7 列 × 6 行。「全員を書出」で作った形）")) return;
    r = import_sheet(name, g_dither, pix);
    if (r == 0) { MessageBoxW(g_wnd, L"この画像は読み込めませんでした。", APP_TITLE, MB_OK | MB_ICONERROR); return; }
    if (r < 0) {
        MessageBoxW(g_wnd, L"画像の大きさが合いません。\n168×144 ドット（またはその 2 倍・3 倍…）の、7 列 × 6 行に並べた画像を選んでください。\n「全員を書出」で作った画像を描き換えて使うのがおすすめです。",
                    APP_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (MessageBoxW(g_wnd, L"42 人分の顔グラを、この画像の絵に置き換えますか？\n（Ctrl+Z で取り消せます。ファイルに書くのは保存したときです）",
                    APP_TITLE, MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    push_undo();
    memcpy(g_pix, pix, sizeof g_pix);
    undo_trim();
    refresh();
}

static void do_paste(void)
{
    Pic pix;
    if (clipboard_paste(g_wnd, g_dither, pix)) { push_undo(); memcpy(g_pix[g_sel], pix, sizeof pix); refresh(); return; }
    if (g_haveclip) { push_undo(); memcpy(g_pix[g_sel], g_clip, sizeof g_clip); refresh(); return; }
    MessageBoxW(g_wnd, L"貼り付けられる画像がありません。", APP_TITLE, MB_OK | MB_ICONINFORMATION);
}

static void do_revert(void)
{
    if (!g_base_ok[g_sel]) {
        MessageBoxW(g_wnd, L"この顔グラの元の絵が見つかりません。\n（WPORT1〜3.EGA.ORIG がないか、このエディタを使う前にほかの方法で描き換えられています）\n\n元に戻すには、Steam でゲームのプロパティから「ゲームファイルの整合性を確認」をしてください。",
                    APP_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (!memcmp(g_pix[g_sel], g_base[g_sel], sizeof(Pic))) {
        MessageBoxW(g_wnd, L"この顔グラは元の絵のままです。", APP_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (MessageBoxW(g_wnd, L"この顔グラだけを元の絵に戻しますか？\n（Ctrl+Z で取り消せます。ファイルに書くのは保存したときです）",
                    APP_TITLE, MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    push_undo();
    memcpy(g_pix[g_sel], g_base[g_sel], sizeof(Pic));
    refresh();
}

static void command(int id)
{
    if (id >= ID_PEN && id < ID_PEN + NTOOL) { set_tool(id - ID_PEN); return; }
    switch (id) {
    case ID_GRID: g_grid = SendMessageW(g_chk_grid, BM_GETCHECK, 0, 0) == BST_CHECKED; refresh(); break;
    case ID_DITHER: g_dither = SendMessageW(g_chk_dither, BM_GETCHECK, 0, 0) == BST_CHECKED; break;
    case ID_SAVE: do_save(); break;
    case ID_UNDO: undo_redo(g_undo, &g_nundo, g_redo, &g_nredo); refresh(); break;
    case ID_REDO: undo_redo(g_redo, &g_nredo, g_undo, &g_nundo); refresh(); break;
    case ID_COPY: memcpy(g_clip, g_pix[g_sel], sizeof g_clip); g_haveclip = 1; clipboard_copy(g_wnd, g_pix[g_sel]); break;
    case ID_PASTE: do_paste(); break;
    case ID_IMPORT: do_import(); break;
    case ID_EXPORT: do_export(); break;
    case ID_SHEET_IN: do_sheet_in(); break;
    case ID_SHEET_OUT: do_sheet_out(); break;
    case ID_REVERT: do_revert(); break;
    case ID_SWAPCOL: { int t = g_colL; g_colL = g_colR; g_colR = t; refresh(); } break;
    case ID_FLIPH: flip(1); break;
    case ID_FLIPV: flip(0); break;
    case ID_UP: shift(0, -1); break;
    case ID_DOWN: shift(0, 1); break;
    case ID_LEFT: shift(-1, 0); break;
    case ID_RIGHT: shift(1, 0); break;
    }
}

/* ---------------------------------------------------------------- window */
static HWND mk(const wchar_t *cls, const wchar_t *txt, DWORD st, int x, int y, int w, int h, int id)
{
    HWND c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | st, x, y, w, h, g_wnd, (HMENU)(INT_PTR)id,
                             GetModuleHandleW(NULL), NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

static void create_controls(void)
{
    static const wchar_t *tn[NTOOL] = { L"ペン", L"消しゴム", L"塗りつぶし", L"スポイト", L"直線", L"四角" };
    int i, y = 228, bw = 74, bh = 28, x2 = RX + bw + 6;
    /* screen position of each tool: column (0/1), row */
    static const int col[NTOOL] = { 0, 0, 1, 1, 0, 1 }, row[NTOOL] = { 0, 1, 0, 1, 2, 2 };
    for (i = 0; i < NTOOL; i++)
        g_btn_tool[i] = mk(L"BUTTON", tn[i], BS_AUTORADIOBUTTON | (i == 0 ? WS_GROUP : 0),
                           RX + col[i] * 72, y + row[i] * 24, col[i] ? 96 : 70, 24, ID_PEN + i);
    mk(L"BUTTON", L"交換", BS_PUSHBUTTON | WS_GROUP, RX + 122, CY + 4 * SW + 26, 44, 30, ID_SWAPCOL);
    y += 3 * 24 + 4;
    g_chk_grid = mk(L"BUTTON", L"マス目", BS_AUTOCHECKBOX | WS_GROUP, RX, y, 70, 24, ID_GRID);
    g_chk_dither = mk(L"BUTTON", L"ディザ", BS_AUTOCHECKBOX, RX + 74, y, 80, 24, ID_DITHER);
    SendMessageW(g_chk_grid, BM_SETCHECK, BST_CHECKED, 0);
    y += 30;
    mk(L"BUTTON", L"元に戻す", BS_PUSHBUTTON, RX, y, bw, bh, ID_UNDO);
    mk(L"BUTTON", L"やり直す", BS_PUSHBUTTON, x2, y, bw, bh, ID_REDO); y += bh + 4;
    mk(L"BUTTON", L"コピー", BS_PUSHBUTTON, RX, y, bw, bh, ID_COPY);
    mk(L"BUTTON", L"貼り付け", BS_PUSHBUTTON, x2, y, bw, bh, ID_PASTE); y += bh + 4;
    mk(L"BUTTON", L"画像を読込", BS_PUSHBUTTON, RX, y, bw, bh, ID_IMPORT);
    mk(L"BUTTON", L"画像に書出", BS_PUSHBUTTON, x2, y, bw, bh, ID_EXPORT); y += bh + 4;
    mk(L"BUTTON", L"全員を読込", BS_PUSHBUTTON, RX, y, bw, bh, ID_SHEET_IN);
    mk(L"BUTTON", L"全員を書出", BS_PUSHBUTTON, x2, y, bw, bh, ID_SHEET_OUT); y += bh + 4;
    mk(L"BUTTON", L"この顔を元の絵に戻す", BS_PUSHBUTTON, RX, y, bw * 2 + 6, bh, ID_REVERT); y += bh + 8;
    mk(L"BUTTON", L"保存", BS_PUSHBUTTON, RX, y, bw * 2 + 6, bh + 4, ID_SAVE);
    /* under the canvas: flip and shift */
    mk(L"BUTTON", L"左右反転", BS_PUSHBUTTON, EBX, PVY, 104, 26, ID_FLIPH);
    mk(L"BUTTON", L"上下反転", BS_PUSHBUTTON, EBX, PVY + 30, 104, 26, ID_FLIPV);
    mk(L"BUTTON", L"↑", BS_PUSHBUTTON, EBX + 37, PVY + 64, 30, 26, ID_UP);
    mk(L"BUTTON", L"←", BS_PUSHBUTTON, EBX, PVY + 92, 30, 26, ID_LEFT);
    mk(L"BUTTON", L"→", BS_PUSHBUTTON, EBX + 74, PVY + 92, 30, 26, ID_RIGHT);
    mk(L"BUTTON", L"↓", BS_PUSHBUTTON, EBX + 37, PVY + 120, 30, 26, ID_DOWN);
    g_status = mk(L"STATIC", L"", SS_LEFT | SS_SUNKEN | SS_CENTERIMAGE, 0, STATUS_Y, CLIENT_W, 26, 0);
    SendMessageW(g_status, WM_SETFONT, (WPARAM)g_font_small, TRUE);
    set_tool(T_PEN);
}

static int confirm_close(void)
{
    int r;
    if (!any_dirty()) return 1;
    r = MessageBoxW(g_wnd, L"保存していない変更があります。保存してから閉じますか？", APP_TITLE, MB_YESNOCANCEL | MB_ICONQUESTION);
    if (r == IDCANCEL) return 0;
    if (r == IDYES) {
        wchar_t err[512];
        if (!save_all(err, 512)) { MessageBoxW(g_wnd, err, APP_TITLE, MB_OK | MB_ICONERROR); return 0; }
    }
    return 1;
}

static LRESULT CALLBACK WndProc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_CREATE: g_wnd = w; create_controls(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; RECT r; HDC dc = BeginPaint(w, &ps);
        GetClientRect(w, &r);
        render(dc, r.right, STATUS_Y);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN:
        SetFocus(w);
        mouse_down((short)LOWORD(lp), (short)HIWORD(lp), m == WM_RBUTTONDOWN, (wp & MK_CONTROL) != 0);
        return 0;
    case WM_MOUSEMOVE: mouse_move((short)LOWORD(lp), (short)HIWORD(lp), (wp & MK_SHIFT) != 0); return 0;
    case WM_LBUTTONUP: case WM_RBUTTONUP: mouse_up((short)LOWORD(lp), (short)HIWORD(lp)); return 0;
    case WM_CAPTURECHANGED:
        if (g_drag || g_listdrag >= 0) { g_drag = 0; g_listdrag = -1; g_listhover = -1; undo_trim(); refresh(); }
        return 0;
    case WM_COMMAND: if (HIWORD(wp) == BN_CLICKED || HIWORD(wp) == 1) command(LOWORD(wp)); return 0;
    case WM_CLOSE: if (confirm_close()) DestroyWindow(w); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(w, m, wp, lp);
}

#ifdef WIZTOOL_TEST   /* 診断は試験版だけ */
/* ---------------------------------------------------------------- diagnostics */
static void selftest(const wchar_t *exe)
{
    wchar_t p[MAX_PATH]; FILE *f; int n, k, ok = 1;
    swprintf(p, MAX_PATH, L"%s\\selftest.txt", exe);
    f = _wfopen(p, L"w, ccs=UTF-8");
    if (!f) return;
    fwprintf(f, L"dir=%s\nloaded=%d\n", g_dir, g_loaded);
    if (g_loaded) {
        /* 1. decode -> encode gives the same bytes */
        for (n = 0; n < NFILE; n++)
            for (k = 0; k < PER_FILE; k++) {
                BYTE fr[FRAME];
                encode(g_pix[n * PER_FILE + k], fr);
                if (memcmp(fr, g_file[n] + k * FRAME, FRAME)) { ok = 0; fwprintf(f, L"roundtrip mismatch file %d slot %d\n", n + 1, k); }
            }
        fwprintf(f, L"roundtrip=%s\n", ok ? L"ok" : L"NG");
        for (k = 0, n = 0; k < NPORT; k++) n += g_base_ok[k];
        fwprintf(f, L"originals found=%d  changed(1,42)=%d,%d\n", n, changed_from_original(0), changed_from_original(41));
        /* 2. export every portrait as PNG and read it back */
        {
            int bad = 0, i, d;
            for (i = 0; i < NPORT; i++) {
                Pic back;
                for (d = 0; d < 2; d++) {
                    swprintf(p, MAX_PATH, L"%s\\selftest_%02d.png", exe, i + 1);
                    if (!export_png(p, g_pix[i], d ? 8 : 1) || !import_file(p, 0, back) ||
                        memcmp(back, g_pix[i], sizeof back)) bad++;
                    DeleteFileW(p);
                }
            }
            fwprintf(f, L"png export/import x1,x8: %d mismatches\n", bad);
        }
        /* 3. sheet out and back in, at 1x and 4x */
        {
            static Pic back[NPORT]; int d, r;
            for (d = 0; d < 2; d++) {
                swprintf(p, MAX_PATH, L"%s\\selftest_sheet.png", exe);
                r = export_sheet(p, d ? 4 : 1);
                r = r ? import_sheet(p, 0, back) : 0;
                fwprintf(f, L"sheet x%d: import=%d same=%d\n", d ? 4 : 1, r, r == 1 && !memcmp(back, g_pix, sizeof back));
                DeleteFileW(p);
            }
        }
        /* 4. flip twice, shift around: back to the same picture */
        {
            Pic keep; int same;
            memcpy(keep, g_pix[g_sel], sizeof keep);
            flip(1); flip(1); flip(0); flip(0);
            for (k = 0; k < PW; k++) shift(1, 0);
            for (k = 0; k < 3; k++) shift(0, -1);
            for (k = 0; k < 3; k++) shift(0, 1);
            same = !memcmp(keep, g_pix[g_sel], sizeof keep);
            flip(1);
            fwprintf(f, L"flip/shift return=%d  flipH changes=%d  undo steps=%d\n", same, memcmp(keep, g_pix[g_sel], sizeof keep) != 0, g_nundo);
            while (g_nundo) undo_redo(g_undo, &g_nundo, g_redo, &g_nredo);
            fwprintf(f, L"after undo all: same=%d redo=%d\n", !memcmp(keep, g_pix[g_sel], sizeof keep), g_nredo);
        }
        fwprintf(f, L"dirty=%d\n", any_dirty());
        /* 5. (--savetest only: writes the files) change portrait 16, save, reload, check */
        if (wcsstr(GetCommandLineW(), L"--savetest")) {
            wchar_t err[512] = L""; Pic want; int r;
            g_pix[15][0][0] ^= 1; g_pix[15][23][23] ^= 3;
            memcpy(want, g_pix[15], sizeof want);
            r = save_all(err, 512);
            fwprintf(f, L"save=%d %s\n", r, err);
            fpath(p, 1, L".ORIG"); fwprintf(f, L"orig2=%d", file_exists(p));
            fpath(p, 0, L".ORIG"); fwprintf(f, L" orig1=%d\n", file_exists(p));
            load_all();
            fwprintf(f, L"base_ok16=%d changed16=%d\n", g_base_ok[15], changed_from_original(15));
            fwprintf(f, L"reload_changed=%d base_is_orig=%d other_same=%d\n",
                     !memcmp(g_pix[15], want, sizeof want), memcmp(g_base[15], want, sizeof want) != 0,
                     !memcmp(g_pix[14], g_base[14], sizeof want));
        }
    }
    fclose(f);
}

#endif /* WIZTOOL_TEST */

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmd, int show)
{
    WNDCLASSW wc; MSG msg; wchar_t exe[MAX_PATH], *s; HANDLE mutex; HACCEL acc; RECT r;
    GpStartupInput gsi = { 1, NULL, FALSE, FALSE };
    ACCEL ak[] = {
        { FCONTROL | FVIRTKEY, 'Z', ID_UNDO }, { FCONTROL | FVIRTKEY, 'Y', ID_REDO }, { FCONTROL | FSHIFT | FVIRTKEY, 'Z', ID_REDO },
        { FCONTROL | FVIRTKEY, 'S', ID_SAVE }, { FCONTROL | FVIRTKEY, 'C', ID_COPY }, { FCONTROL | FVIRTKEY, 'V', ID_PASTE },
        { FCONTROL | FVIRTKEY, 'O', ID_IMPORT }, { FVIRTKEY, 'X', ID_SWAPCOL },
        { FVIRTKEY, 'B', ID_PEN }, { FVIRTKEY, 'E', ID_ERASE }, { FVIRTKEY, 'G', ID_FILL }, { FVIRTKEY, 'I', ID_PICK },
        { FVIRTKEY, 'L', ID_LINE }, { FVIRTKEY, 'R', ID_RECT }, { FVIRTKEY, 'H', ID_FLIPH }, { FVIRTKEY, 'V', ID_FLIPV },
        { FVIRTKEY, VK_UP, ID_UP }, { FVIRTKEY, VK_DOWN, ID_DOWN }, { FVIRTKEY, VK_LEFT, ID_LEFT }, { FVIRTKEY, VK_RIGHT, ID_RIGHT } };
    (void)prev; (void)show;
    GdiplusStartup(&g_gdip, &gsi, NULL);
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    s = wcsrchr(exe, L'\\'); if (s) *s = 0;
    find_dir(exe);
    if (g_dir[0]) load_all();
#ifdef WIZTOOL_TEST   /* 診断: --selftest は試験版だけ */
    if (cmd && wcsstr(cmd, L"--selftest")) { selftest(exe); GdiplusShutdown(g_gdip); return 0; }
#endif

    g_font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, SHIFTJIS_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Meiryo UI");
    g_font_small = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, SHIFTJIS_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Meiryo UI");

#ifdef WIZTOOL_TEST
    if (!(cmd && wcsstr(cmd, L"--snapshot"))) {
#else
    (void)cmd;
    {
#endif
        mutex = CreateMutexW(NULL, TRUE, L"Wiz6JpFaceEdit");
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            HWND w = FindWindowW(L"W6FACE", NULL);
            if (w) { ShowWindow(w, SW_RESTORE); SetForegroundWindow(w); }
            return 0;
        }
        (void)mutex;
        while (!g_loaded) {
            if (MessageBoxW(NULL, g_dir[0] ? L"顔グラのファイル（WPORT1〜3.EGA）を読めませんでした。\n場所を選びますか？"
                                           : L"ゲームのフォルダが見つかりませんでした。\nBANE フォルダの WPORT1.EGA を選んでください。",
                            APP_TITLE, MB_OKCANCEL | MB_ICONINFORMATION) != IDOK) return 0;
            if (!ask_dir(NULL)) return 0;
            load_all();
        }
    }

    ZeroMemory(&wc, sizeof wc);
    wc.lpfnWndProc = WndProc; wc.hInstance = inst; wc.lpszClassName = L"W6FACE";
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW); wc.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    RegisterClassW(&wc);
    r.left = 0; r.top = 0; r.right = CLIENT_W; r.bottom = CLIENT_H;
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    g_wnd = CreateWindowExW(0, L"W6FACE", APP_TITLE, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                            CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);

#ifdef WIZTOOL_TEST   /* 診断: --snapshot は試験版だけ */
    if (cmd && wcsstr(cmd, L"--snapshot")) {   /* draw the window into snapshot.bmp */
        wchar_t p[MAX_PATH]; HDC sdc = GetDC(NULL), mdc = CreateCompatibleDC(sdc);
        HBITMAP bmp = CreateCompatibleBitmap(sdc, CLIENT_W, CLIENT_H); BITMAPINFO bi; DWORD *px; BITMAPFILEHEADER fh; FILE *f;
        int sel = 0; wchar_t *a = wcsstr(cmd, L"--sel=");
        if (a) sel = _wtoi(a + 6) - 1;
        if (sel >= 0 && sel < NPORT) g_sel = sel;
        SelectObject(mdc, bmp);
        ShowWindow(g_wnd, SW_SHOWNOACTIVATE); UpdateWindow(g_wnd);
        update_title(); update_status();
        PrintWindow(g_wnd, mdc, PW_CLIENTONLY);
        px = (DWORD *)malloc(CLIENT_W * CLIENT_H * 4);
        ZeroMemory(&bi, sizeof bi);
        bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = CLIENT_W; bi.bmiHeader.biHeight = CLIENT_H;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
        GetDIBits(mdc, bmp, 0, CLIENT_H, px, &bi, DIB_RGB_COLORS);
        swprintf(p, MAX_PATH, L"%s\\snapshot.bmp", exe);
        f = _wfopen(p, L"wb");
        if (f && px) {
            ZeroMemory(&fh, sizeof fh);
            fh.bfType = 0x4D42; fh.bfOffBits = sizeof fh + sizeof bi.bmiHeader; fh.bfSize = fh.bfOffBits + CLIENT_W * CLIENT_H * 4;
            fwrite(&fh, sizeof fh, 1, f); fwrite(&bi.bmiHeader, sizeof bi.bmiHeader, 1, f); fwrite(px, CLIENT_W * CLIENT_H * 4, 1, f);
        }
        if (f) fclose(f);
        free(px);
        DestroyWindow(g_wnd);
        GdiplusShutdown(g_gdip);
        return 0;
    }
#endif

    ShowWindow(g_wnd, SW_SHOW);
    refresh();
    acc = CreateAcceleratorTableW(ak, sizeof ak / sizeof ak[0]);
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (TranslateAcceleratorW(g_wnd, acc, &msg)) continue;
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    GdiplusShutdown(g_gdip);
    return 0;
}
