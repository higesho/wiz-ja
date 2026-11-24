/* ウィザードリィ7 顔グラエディタ (Wiz7Face.exe)  Gold 版・DOS 版の両方
 *
 * Gold 版 (wizardry7gold):
 *   RES\BITS\PCA\PC0..32.BMP = 55x60、RES\BITS\PCB\PC0..32.BMP = 110x120 (8 ビット BMP、下から上)。
 *   元の 33 枚とも、大きい方は小さい方をちょうど 2 倍にしたもの。なので 55x60 で描き、
 *   保存のときに大きい方を 2 倍にして書く。BMP の見出しと色の表はそのまま残す。
 * DOS 版 (DSAVANT):
 *   PORTDATA.VGA = 33 x 780 バイト。1 人分 = 26x30 ドット、1 ドット 1 バイト (上から下、左から右)。
 * 色: どちらも PALETTE.DEF (256 色、6 ビット) と同じ色の表。番号 255 は背景 (透明)。
 *   DOS 版は 256 色すべて使える。DOS 版で色の表を変えるのは、明るさ・コントラストの設定 (VINIT/VBASE が
 *   256 色すべてに同じ計算をかけて VGA.DRV の 38 番で設定)、暗転 (30・31 番)、迷宮の番号 255 (VMAZE が
 *   28 番で 1 色だけ設定) だけ。番号 255 は背景用 (迷宮で色が変わる)。
 *   Gold 版も 256 色すべて使える。WIZARD.EXE は PORTDATA.VGA を求められると pca/pcb の BMP を読み (0x426af1)、
 *   BMP の色の表は使わずに番号のまま画面の下絵に書く (0x42689a、番号 255 は描かない = 透明)。画面の色の表は
 *   winpal.bmp〜winpal6.bmp (明るさの段階、全体を同じように明るくしたもの。番号 1 はどれも白) から選ぶだけで、
 *   1 色だけ書き換える処理は無い。
 *
 * 保存すると、最初の 1 回だけ元のファイルを「.ORIG」を付けて残す。
 * 元の絵は CRC-32 で見分ける (絵そのものはこのプログラムに入れていない)。
 *
 * 診断（試験版 build.bat test だけ）: --selftest [--savetest] [--ver=gold|dos]  (selftest.txt に書く)
 *       --snapshot [--sel=N] [--ver=gold|dos]     (snapshot.bmp に描く)
 */
#include <windows.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define APP_TITLE L"ウィザードリィ7 顔グラ"
#define NFACE 33
#define MAXW 55
#define MAXH 60
#define SHEET_C 11
#define SHEET_R 3

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

/* ---------------------------------------------------------------- original data (checksums and colour sets only) */
static const DWORD CRC_DOS[NFACE] = {
    0x0491f094, 0x33aa997a, 0x968080c8, 0x55dc7475, 0x39322506, 0xa1e051af, 0xe31ed494, 0x43116591,
    0xd5d749aa, 0xe452963c, 0x322b39d1, 0x2f15d205, 0xadbf6075, 0x022bfa02, 0xe65fccab, 0x33943b99,
    0xd2e8c424, 0x47c80201, 0xd18921da, 0x6594ee8f, 0xb5941ab6, 0x86a89a9c, 0x7cc03a0a, 0x085e40d4,
    0xbbb25813, 0x9647e71a, 0x0f0f64f9, 0xe88ec13d, 0xb2d5c4ab, 0x37615fac, 0x781bceb3, 0x5d2794d2,
    0xaf2fb0af
};
static const DWORD CRC_GOLD_S[NFACE] = {
    0xdfd4d334, 0xdec61ff2, 0x9d903d0a, 0x680ddf4d, 0xa3cbd7f9, 0xb07e4701, 0xc2349e24, 0x24d9b886,
    0x75eb0295, 0xe7c8cb33, 0x1bad5ea4, 0xaca9d46a, 0xaeb067aa, 0xe39568ce, 0x33935165, 0x25d13bda,
    0xa16ab2c1, 0x8707ab1a, 0x20e91038, 0x19810772, 0x883a66b9, 0xa58854fa, 0xc94e85fc, 0xd17a021a,
    0xf4a344bf, 0x8d5deef8, 0x63ddea29, 0x0dcb6faf, 0xb3107187, 0x7bc2209c, 0x58a5788b, 0x09f3571a,
    0x8ed209de
};
#define WHITE 1
#define BG 255
#define ERASE_SIZE 3

/* ---------------------------------------------------------------- versions */
enum { V_GOLD, V_DOS };
typedef struct { int kind; wchar_t dir[MAX_PATH]; } Place;
static Place g_place[4];
static int g_nplace, g_cur = -1;
static int g_kind;                 /* V_GOLD / V_DOS of the loaded data */
static int W, H;                   /* picture size of the loaded version */

typedef BYTE Pic[MAXH][MAXW];
static Pic g_pix[NFACE], g_saved[NFACE], g_base[NFACE];
static int g_base_ok[NFACE];
static int g_large_custom[NFACE];  /* Gold: the 110x120 picture is not exactly 2x the small one */
static COLORREF PAL[256];
static BYTE g_ok[256];
static int g_loaded;
static BYTE *g_dosfile;            /* DOS: PORTDATA.VGA as read */
static long g_dossize;

static int g_sel = 0, g_colL = 0, g_colR = BG;
enum { T_PEN, T_ERASE, T_FILL, T_PICK, T_LINE, T_RECT, NTOOL };
static int g_tool = T_PEN, g_grid = 1, g_dither = 0;

/* undo: every step keeps all 33 pictures */
#define NUNDO 60
typedef struct { int sel; Pic pix[NFACE]; } Snap;
static Snap g_undo[NUNDO], g_redo[NUNDO];
static int g_nundo, g_nredo;
static Pic g_clip;
static int g_haveclip;

/* ---------------------------------------------------------------- layout */
#define LX 12
#define LY 44
#define LCOLS 6
#define LCW 64
#define LCH 78
#define CX 410
#define CY 12
#define CAW 440                       /* canvas area */
#define CAH 480
#define PVY (CY + CAH + 12)
#define RX (CX + CAW + 18)
#define SWC 13                        /* palette swatch (16 x 16) */
#define CLIENT_W (RX + 16 * SWC + 14)
#define STATUS_Y 660
#define CLIENT_H (STATUS_Y + 30)
#define EBX (CX + 300)
static int CS;                        /* canvas zoom for the loaded version */

static HWND g_wnd, g_status, g_combo;
static HFONT g_font, g_font_small;
static HWND g_btn_tool[NTOOL], g_chk_grid, g_chk_dither;
enum { ID_PEN = 100, ID_ERASE, ID_FILL, ID_PICK, ID_LINE, ID_RECT,
       ID_GRID = 120, ID_DITHER, ID_SAVE, ID_UNDO, ID_REDO, ID_COPY, ID_PASTE,
       ID_IMPORT, ID_EXPORT, ID_REVERT, ID_SWAPCOL, ID_SHEET_IN, ID_SHEET_OUT,
       ID_FLIPH, ID_FLIPV, ID_UP, ID_DOWN, ID_LEFT, ID_RIGHT, ID_COMBO };

/* ---------------------------------------------------------------- small helpers */
static DWORD crc32(const BYTE *p, int n)
{
    DWORD c = 0xffffffff; int k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320 & (0 - (c & 1)));
    }
    return ~c;
}

static DWORD pic_crc(const Pic p)
{
    static BYTE buf[MAXW * MAXH]; int y;
    for (y = 0; y < H; y++) memcpy(buf + y * W, p[y], W);
    return crc32(buf, W * H);
}

static int pic_eq(const Pic a, const Pic b)
{
    int y;
    for (y = 0; y < H; y++) if (memcmp(a[y], b[y], W)) return 0;
    return 1;
}

static int file_exists(const wchar_t *p)
{
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static BYTE *read_all(const wchar_t *p, long *size)
{
    FILE *f = _wfopen(p, L"rb"); long n; BYTE *b;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 16 * 1024 * 1024) { fclose(f); return NULL; }
    b = (BYTE *)malloc(n);
    if (b && fread(b, 1, n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    if (b) *size = n;
    return b;
}

/* write via a temporary file; keep the first original as <name>.ORIG */
static int write_file(const wchar_t *p, const BYTE *data, long n)
{
    wchar_t t[MAX_PATH], o[MAX_PATH]; FILE *f;
    swprintf(o, MAX_PATH, L"%s.ORIG", p);
    if (!file_exists(o) && !CopyFileW(p, o, TRUE)) return 0;
    swprintf(t, MAX_PATH, L"%s.TMP", p);
    f = _wfopen(t, L"wb");
    if (!f) return 0;
    if (fwrite(data, 1, n, f) != (size_t)n) { fclose(f); DeleteFileW(t); return 0; }
    fclose(f);
    if (!MoveFileExW(t, p, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { DeleteFileW(t); return 0; }
    return 1;
}

/* ---------------------------------------------------------------- BMP (8 bit, uncompressed) */
typedef struct { BYTE *file; long size; int w, h, off, stride, topdown; } Bmp;

static int bmp_open(const wchar_t *p, Bmp *b)
{
    BITMAPFILEHEADER *fh; BITMAPINFOHEADER *ih;
    ZeroMemory(b, sizeof *b);
    b->file = read_all(p, &b->size);
    if (!b->file || b->size < (long)(sizeof *fh + sizeof *ih)) return 0;
    fh = (BITMAPFILEHEADER *)b->file; ih = (BITMAPINFOHEADER *)(b->file + sizeof *fh);
    if (fh->bfType != 0x4D42 || ih->biBitCount != 8 || ih->biCompression != BI_RGB) return 0;
    b->w = ih->biWidth; b->h = ih->biHeight < 0 ? -ih->biHeight : ih->biHeight; b->topdown = ih->biHeight < 0;
    b->off = fh->bfOffBits; b->stride = (b->w + 3) & ~3;
    return b->off + (long)b->stride * b->h <= b->size;
}

static BYTE *bmp_row(Bmp *b, int y)
{
    return b->file + b->off + (long)b->stride * (b->topdown ? y : b->h - 1 - y);
}

static void gold_path(wchar_t *out, const wchar_t *dir, int large, int i)
{
    swprintf(out, MAX_PATH, L"%s\\RES\\BITS\\%s\\PC%d.BMP", dir, large ? L"PCB" : L"PCA", i);
}

/* ---------------------------------------------------------------- loading */
static int load_palette(const wchar_t *dir)
{
    wchar_t p[MAX_PATH]; long n; BYTE *d; int i;
    swprintf(p, MAX_PATH, L"%s\\PALETTE.DEF", dir);
    d = read_all(p, &n);
    if (!d || n < 768) { free(d); return 0; }
    for (i = 0; i < 256; i++) {
        int r = d[i * 3] & 63, g = d[i * 3 + 1] & 63, b = d[i * 3 + 2] & 63;
        PAL[i] = RGB(r << 2 | r >> 4, g << 2 | g >> 4, b << 2 | b >> 4);
    }
    free(d);
    return 1;
}

static void find_originals(BYTE *origfile, long origsize)
{
    int i, k;
    for (i = 0; i < NFACE; i++) {
        DWORD want = g_kind == V_GOLD ? CRC_GOLD_S[i] : CRC_DOS[i];
        g_base_ok[i] = 0;
        /* 1) the current pictures (any slot) */
        for (k = 0; k < NFACE && !g_base_ok[i]; k++)
            if (pic_crc(g_pix[k]) == want) { memcpy(g_base[i], g_pix[k], sizeof(Pic)); g_base_ok[i] = 1; }
        if (g_base_ok[i]) continue;
        /* 2) the .ORIG files */
        if (g_kind == V_DOS) {
            if (origfile && origsize >= NFACE * 780)
                for (k = 0; k < NFACE && !g_base_ok[i]; k++)
                    if (crc32(origfile + k * 780, 780) == want) {
                        int y;
                        for (y = 0; y < H; y++) memcpy(g_base[i][y], origfile + k * 780 + y * W, W);
                        g_base_ok[i] = 1;
                    }
        } else {
            for (k = 0; k < NFACE && !g_base_ok[i]; k++) {
                wchar_t p[MAX_PATH]; Bmp b; int y; Pic t;
                gold_path(p, g_place[g_cur].dir, 0, k); wcscat(p, L".ORIG");
                if (!file_exists(p)) continue;
                if (bmp_open(p, &b) && b.w == W && b.h == H) {
                    for (y = 0; y < H; y++) memcpy(t[y], bmp_row(&b, y), W);
                    if (pic_crc(t) == want) { memcpy(g_base[i], t, sizeof t); g_base_ok[i] = 1; }
                }
                free(b.file);
            }
        }
        if (!g_base_ok[i]) memset(g_base[i], BG, sizeof(Pic));
    }
}

static int load_place(int n, wchar_t *err, int errn)
{
    const Place *pl = &g_place[n]; wchar_t p[MAX_PATH]; int i, y, x;
    BYTE *orig = NULL; long origsize = 0;
    g_loaded = 0;
    if (!load_palette(pl->dir)) { swprintf(err, errn, L"PALETTE.DEF を読めませんでした。\n%s", pl->dir); return 0; }
    g_kind = pl->kind; g_cur = n;
    memset(g_pix, BG, sizeof g_pix);
    if (g_kind == V_DOS) {
        W = 26; H = 30;
        free(g_dosfile); g_dosfile = NULL;
        swprintf(p, MAX_PATH, L"%s\\PORTDATA.VGA", pl->dir);
        g_dosfile = read_all(p, &g_dossize);
        if (!g_dosfile || g_dossize < NFACE * 780) { swprintf(err, errn, L"PORTDATA.VGA を読めませんでした。\n%s", pl->dir); return 0; }
        for (i = 0; i < NFACE; i++)
            for (y = 0; y < H; y++) memcpy(g_pix[i][y], g_dosfile + i * 780 + y * W, W);
        wcscat(p, L".ORIG");
        orig = read_all(p, &origsize);
        for (i = 0; i < 256; i++) g_ok[i] = 1;   /* see the note at the top */
    } else {
        W = 55; H = 60;
        for (i = 0; i < NFACE; i++) {
            Bmp s, l;
            gold_path(p, pl->dir, 0, i);
            if (!bmp_open(p, &s) || s.w != W || s.h != H) {
                free(s.file);
                swprintf(err, errn, L"顔グラのファイルを読めませんでした。\n%s", p);
                return 0;
            }
            for (y = 0; y < H; y++) memcpy(g_pix[i][y], bmp_row(&s, y), W);
            free(s.file);
            g_large_custom[i] = 0;
            gold_path(p, pl->dir, 1, i);
            if (bmp_open(p, &l) && l.w == W * 2 && l.h == H * 2) {
                for (y = 0; y < H * 2 && !g_large_custom[i]; y++) {
                    BYTE *r = bmp_row(&l, y);
                    for (x = 0; x < W * 2; x++) if (r[x] != g_pix[i][y / 2][x / 2]) { g_large_custom[i] = 1; break; }
                }
            }
            free(l.file);
        }
        for (i = 0; i < 256; i++) g_ok[i] = 1;   /* see the note at the top */
    }
    g_ok[WHITE] = 1; g_ok[BG] = 1; g_ok[0] = 1;
    CS = CAW / W < CAH / H ? CAW / W : CAH / H;
    find_originals(orig, origsize);
    free(orig);
    memcpy(g_saved, g_pix, sizeof g_pix);
    g_nundo = g_nredo = 0;
    if (g_sel >= NFACE) g_sel = 0;
    if (!g_ok[g_colL]) g_colL = 0;
    g_colR = BG;
    g_loaded = 1;
    return 1;
}

static int any_dirty(void) { return memcmp(g_pix, g_saved, sizeof g_pix) != 0; }
static int face_dirty(int i) { return !pic_eq(g_pix[i], g_saved[i]); }
static int changed_from_original(int i) { return !g_base_ok[i] || !pic_eq(g_pix[i], g_base[i]); }

static int save_all(wchar_t *err, int errn)
{
    const wchar_t *dir = g_place[g_cur].dir; wchar_t p[MAX_PATH]; int i, y, x;
    if (g_kind == V_DOS) {
        BYTE *img;
        if (!any_dirty()) return 1;
        img = (BYTE *)malloc(g_dossize);
        if (!img) return 0;
        memcpy(img, g_dosfile, g_dossize);
        for (i = 0; i < NFACE; i++)
            for (y = 0; y < H; y++) memcpy(img + i * 780 + y * W, g_pix[i][y], W);
        swprintf(p, MAX_PATH, L"%s\\PORTDATA.VGA", dir);
        if (!write_file(p, img, g_dossize)) {
            free(img);
            swprintf(err, errn, L"PORTDATA.VGA を保存できませんでした。\nフォルダに書き込めるか、ゲームが開いていないか確かめてください。");
            return 0;
        }
        memcpy(g_dosfile, img, g_dossize);
        free(img);
        memcpy(g_saved, g_pix, sizeof g_pix);
        return 1;
    }
    for (i = 0; i < NFACE; i++) {
        Bmp b; int large;
        if (!face_dirty(i)) continue;
        for (large = 0; large < 2; large++) {
            gold_path(p, dir, large, i);
            if (!bmp_open(p, &b) || b.w != W * (large + 1) || b.h != H * (large + 1)) {
                free(b.file);
                swprintf(err, errn, L"%s を読めませんでした。", p);
                return 0;
            }
            for (y = 0; y < b.h; y++) {
                BYTE *r = bmp_row(&b, y);
                for (x = 0; x < b.w; x++) r[x] = g_pix[i][y >> large][x >> large];
            }
            if (!write_file(p, b.file, b.size)) {
                free(b.file);
                swprintf(err, errn, L"%s を保存できませんでした。\nフォルダに書き込めるか、ゲームが開いていないか確かめてください。", p);
                return 0;
            }
            free(b.file);
        }
        memcpy(g_saved[i], g_pix[i], sizeof(Pic));
        g_large_custom[i] = 0;
    }
    return 1;
}

/* where the game folders are: next to the exe, or below it */
static void add_place(int kind, const wchar_t *dir)
{
    wchar_t full[MAX_PATH], p[MAX_PATH]; int i;
    if (g_nplace >= 4) return;
    if (!GetFullPathNameW(dir, MAX_PATH, full, NULL)) wcscpy(full, dir);
    if (kind == V_GOLD) swprintf(p, MAX_PATH, L"%s\\RES\\BITS\\PCA\\PC0.BMP", full);
    else swprintf(p, MAX_PATH, L"%s\\PORTDATA.VGA", full);
    if (!file_exists(p)) return;
    if (kind == V_DOS) {   /* the Gold folder also has PORTDATA.VGA: count it as DOS only without RES */
        swprintf(p, MAX_PATH, L"%s\\RES\\BITS\\PCA\\PC0.BMP", full);
        if (file_exists(p)) return;
    }
    for (i = 0; i < g_nplace; i++) if (!_wcsicmp(g_place[i].dir, full)) return;
    g_place[g_nplace].kind = kind; wcscpy(g_place[g_nplace].dir, full); g_nplace++;
}

static void find_places(const wchar_t *exe)
{
    static const wchar_t *sub[] = { L"", L"\\wizardry7gold", L"\\DSAVANT", L"\\..\\wizardry7gold", L"\\..\\DSAVANT" };
    int i, k; wchar_t p[MAX_PATH];
    for (k = 0; k < 2; k++)
        for (i = 0; i < 5; i++) { swprintf(p, MAX_PATH, L"%s%s", exe, sub[i]); add_place(k == 0 ? V_GOLD : V_DOS, p); }
}

static int ask_place(HWND owner)
{
    OPENFILENAMEW of; wchar_t name[MAX_PATH] = L"", *s;
    ZeroMemory(&of, sizeof of);
    of.lStructSize = sizeof of; of.hwndOwner = owner;
    of.lpstrFilter = L"ゲームの本体 (WIZARD.EXE / DS.EXE)\0WIZARD.EXE;DS.EXE\0";
    of.lpstrFile = name; of.nMaxFile = MAX_PATH;
    of.lpstrTitle = L"Gold 版なら wizardry7gold の WIZARD.EXE、DOS 版なら DSAVANT の DS.EXE を選んでください";
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&of)) return 0;
    s = wcsrchr(name, L'\\'); if (s) *s = 0;
    add_place(V_GOLD, name); add_place(V_DOS, name);
    return g_nplace > 0;
}

/* ---------------------------------------------------------------- undo */
static void snap_push(Snap *st, int *n)
{
    if (*n == NUNDO) { memmove(st, st + 1, sizeof(Snap) * (NUNDO - 1)); (*n)--; }
    st[*n].sel = g_sel;
    memcpy(st[*n].pix, g_pix, sizeof g_pix);
    (*n)++;
}
static void push_undo(void) { snap_push(g_undo, &g_nundo); g_nredo = 0; }
static void undo_redo(Snap *from, int *nfrom, Snap *to, int *nto)
{
    Snap *s;
    if (!*nfrom) return;
    s = &from[--*nfrom];
    snap_push(to, nto);
    memcpy(g_pix, s->pix, sizeof g_pix);
    g_sel = s->sel;
}
static void undo_trim(void)
{
    if (g_nundo && !memcmp(g_undo[g_nundo - 1].pix, g_pix, sizeof g_pix)) g_nundo--;
}

/* ---------------------------------------------------------------- image import / export */
static int nearest(int r, int g, int b)
{
    int i, best = 0; long bd = 0x7fffffff;
    for (i = 0; i < 256; i++) {
        long dr, dg, db, d;
        if (!g_ok[i]) continue;
        dr = r - GetRValue(PAL[i]); dg = g - GetGValue(PAL[i]); db = b - GetBValue(PAL[i]);
        d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

/* region (rx, ry, w, h) of a top-down 0x00RRGGBB image -> W x H picture.
   the region is cropped to the picture's aspect, area-averaged, mapped to the usable colours */
static void region_to_pic(const DWORD *src, int pitch, int rx, int ry, int w, int h, int dither, Pic out)
{
    static float acc[MAXH][MAXW][3];
    int cw = w, ch = h, x0, y0, x, y, c;
    if ((long)w * H > (long)h * W) cw = h * W / H; else ch = w * H / W;
    if (cw < 1) cw = 1;
    if (ch < 1) ch = 1;
    x0 = rx + (w - cw) / 2; y0 = ry + (h - ch) / 2;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            int ax = x * cw / W, bx = (x + 1) * cw / W, ay = y * ch / H, by = (y + 1) * ch / H, i, j, n = 0;
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
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
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
                if (x + 1 < W) acc[y][x + 1][c] += e[c] * 7 / 16;
                if (y + 1 < H) {
                    if (x > 0) acc[y + 1][x - 1][c] += e[c] * 3 / 16;
                    acc[y + 1][x][c] += e[c] * 5 / 16;
                    if (x + 1 < W) acc[y + 1][x + 1][c] += e[c] * 1 / 16;
                }
            }
        }
}

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

static DWORD rgbx(int i)
{
    return ((DWORD)GetRValue(PAL[i]) << 16) | ((DWORD)GetGValue(PAL[i]) << 8) | GetBValue(PAL[i]);
}

/* transparent parts become the background colour */
static DWORD *load_image(const wchar_t *path, int *w, int *h)
{
    void *bmp = NULL; HBITMAP hbm = NULL; DWORD *px;
    if (GdipCreateBitmapFromFile(path, &bmp) != 0 || !bmp) return NULL;
    GdipCreateHBITMAPFromBitmap(bmp, &hbm, 0xFF000000 | rgbx(BG));
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
    region_to_pic(px, w, 0, 0, w, h, dither, out);
    free(px);
    return 1;
}

/* sheet: 11 x 3 cells of the picture's shape. 1 ok, 0 unreadable, -1 wrong shape */
static int import_sheet(const wchar_t *path, int dither, Pic *out)
{
    int w, h, i, cw, ch; DWORD *px = load_image(path, &w, &h);
    if (!px) return 0;
    if (w % (SHEET_C * W) || h % (SHEET_R * H) || w / (SHEET_C * W) != h / (SHEET_R * H) || w < SHEET_C * W) { free(px); return -1; }
    cw = w / SHEET_C; ch = h / SHEET_R;
    for (i = 0; i < NFACE; i++) region_to_pic(px, w, (i % SHEET_C) * cw, (i / SHEET_C) * ch, cw, ch, dither, out[i]);
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

static int export_png(const wchar_t *path, const Pic pix, int scale)
{
    int x, y, w = W * scale, h = H * scale, ok;
    DWORD *buf = (DWORD *)malloc((size_t)w * h * 4);
    if (!buf) return 0;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) buf[y * w + x] = rgbx(pix[y / scale][x / scale]);
    ok = save_png(path, buf, w, h);
    free(buf);
    return ok;
}

static int export_sheet(const wchar_t *path, int scale)
{
    int w = SHEET_C * W * scale, h = SHEET_R * H * scale, x, y, ok;
    DWORD *buf = (DWORD *)malloc((size_t)w * h * 4);
    if (!buf) return 0;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int px = x / scale, py = y / scale, i = (py / H) * SHEET_C + px / W;
            buf[y * w + x] = i < NFACE ? rgbx(g_pix[i][py % H][px % W]) : rgbx(BG);
        }
    ok = save_png(path, buf, w, h);
    free(buf);
    return ok;
}

static void clipboard_copy(HWND w, const Pic pix)
{
    HGLOBAL g; BITMAPINFOHEADER *bh; DWORD *px; int x, y;
    if (!OpenClipboard(w)) return;
    EmptyClipboard();
    g = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + W * H * 4);
    if (g) {
        bh = (BITMAPINFOHEADER *)GlobalLock(g);
        ZeroMemory(bh, sizeof *bh);
        bh->biSize = sizeof *bh; bh->biWidth = W; bh->biHeight = H; bh->biPlanes = 1; bh->biBitCount = 32;
        bh->biCompression = BI_RGB; bh->biSizeImage = W * H * 4;
        px = (DWORD *)(bh + 1);
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) px[y * W + x] = rgbx(pix[H - 1 - y][x]);
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
    if (px) { region_to_pic(px, iw, 0, 0, iw, ih, dither, out); free(px); ok = 1; }
    return ok;
}

/* ---------------------------------------------------------------- drawing */
static DWORD *g_bits; static int g_bw, g_bh;
static int g_listdrag = -1, g_listhover = -1;

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

/* draw a picture at any size (nearest neighbour) */
static void blit(const Pic pix, int x0, int y0, int w, int h)
{
    int x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) fill(x0 + x, y0 + y, 1, 1, PAL[pix[y * H / h][x * W / w]]);
}

static int thumb_scale(void) { return g_kind == V_DOS ? 2 : 1; }

static void list_cell(int i, int *x, int *y) { *x = LX + (i % LCOLS) * LCW; *y = LY + (i / LCOLS) * LCH; }

static int list_hit(int mx, int my)
{
    int i, x, y;
    for (i = 0; i < NFACE; i++) {
        list_cell(i, &x, &y);
        if (mx >= x - 3 && my >= y - 3 && mx < x + LCW - 4 && my < y + LCH - 2) return i;
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
    BITMAPINFO bi; HBITMAP bmp, old; HDC mdc; void *bits; int i, x, y, ts = thumb_scale(); wchar_t s[64];
    const COLORREF face = GetSysColor(COLOR_BTNFACE);
    ZeroMemory(&bi, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = cw; bi.bmiHeader.biHeight = -ch;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp) return;
    g_bits = (DWORD *)bits; g_bw = cw; g_bh = ch;
    fill(0, 0, cw, ch, face);
    if (g_loaded) {
        /* list */
        for (i = 0; i < NFACE; i++) {
            list_cell(i, &x, &y);
            if (i == g_sel) fill(x - 3, y - 3, W * ts + 6, H * ts + 6, RGB(255, 200, 0));
            if (g_listdrag >= 0 && i == g_listhover && i != g_listdrag) fill(x - 3, y - 3, W * ts + 6, H * ts + 6, RGB(0, 120, 255));
            blit(g_pix[i], x, y, W * ts, H * ts);
        }
        /* canvas */
        blit(g_pix[g_sel], CX, CY, W * CS, H * CS);
        if (g_grid && CS >= 6) {
            for (i = 1; i < W; i++) { int k; for (k = 0; k < H * CS; k += 2) fill(CX + i * CS, CY + k, 1, 1, RGB(120, 120, 120)); }
            for (i = 1; i < H; i++) { int k; for (k = 0; k < W * CS; k += 2) fill(CX + k, CY + i * CS, 1, 1, RGB(120, 120, 120)); }
        }
        frame(CX - 2, CY - 2, W * CS + 4, H * CS + 4, 2, RGB(40, 40, 40));
        /* previews */
        blit(g_pix[g_sel], CX, PVY + 22, W, H);
        blit(g_pix[g_sel], CX + W + 10, PVY + 22, W * 2, H * 2);
        if (g_kind == V_DOS) blit(g_pix[g_sel], CX + W * 3 + 30, PVY + 22, W * 2, H * 12 / 5);   /* VGA 320x200: 1.2x taller */
        /* palette */
        for (i = 0; i < 256; i++) {
            int sx = RX + (i % 16) * SWC, sy = CY + (i / 16) * SWC;
            fill(sx, sy, SWC, SWC, g_ok[i] ? PAL[i] : face);
            if (!g_ok[i]) { int k; for (k = 2; k < SWC - 2; k++) { fill(sx + k, sy + k, 1, 1, RGB(150, 150, 150)); fill(sx + SWC - 1 - k, sy + k, 1, 1, RGB(150, 150, 150)); } }
        }
        frame(RX + (g_colL % 16) * SWC - 1, CY + (g_colL / 16) * SWC - 1, SWC + 2, SWC + 2, 2, RGB(255, 0, 0));
        frame(RX + (g_colR % 16) * SWC + 2, CY + (g_colR / 16) * SWC + 2, SWC - 4, SWC - 4, 2, RGB(0, 90, 255));
        fill(RX + 2, CY + 16 * SWC + 30, 54, 30, PAL[g_colL]); frame(RX + 2, CY + 16 * SWC + 30, 54, 30, 2, RGB(255, 0, 0));
        fill(RX + 72, CY + 16 * SWC + 30, 54, 30, PAL[g_colR]); frame(RX + 72, CY + 16 * SWC + 30, 54, 30, 2, RGB(0, 90, 255));
    }
    mdc = CreateCompatibleDC(dc);
    old = (HBITMAP)SelectObject(mdc, bmp);
    if (g_loaded) {
        for (i = 0; i < NFACE; i++) {
            list_cell(i, &x, &y);
            swprintf(s, 64, L"%d", i + 1);
            text_at(mdc, x, y + H * ts + 1, s, RGB(0, 0, 0), g_font_small);
            if (changed_from_original(i)) text_at(mdc, x + 24, y + H * ts + 1, L"●", RGB(0, 90, 230), g_font_small);
            if (face_dirty(i)) text_at(mdc, x + 38, y + H * ts + 1, L"●", RGB(220, 20, 20), g_font_small);
        }
        if (g_kind == V_DOS) {
            text_at(mdc, CX, PVY, L"原寸・2倍", RGB(0, 0, 0), g_font_small);
            text_at(mdc, CX + W * 3 + 30, PVY, L"ゲーム画面", RGB(0, 0, 0), g_font_small);
        } else text_at(mdc, CX, PVY, L"原寸（小さい絵）・2倍（大きい絵）", RGB(0, 0, 0), g_font_small);
        text_at(mdc, EBX + 33, PVY + 97, L"ずらす", RGB(0, 0, 0), g_font_small);
        text_at(mdc, RX + 4, CY + 16 * SWC + 10, L"左ボタン", RGB(160, 0, 0), g_font_small);
        text_at(mdc, RX + 74, CY + 16 * SWC + 10, L"右ボタン", RGB(0, 60, 180), g_font_small);
    }
    BitBlt(dc, 0, 0, cw, ch, mdc, 0, 0, SRCCOPY);
    SelectObject(mdc, old);
    DeleteDC(mdc);
    DeleteObject(bmp);
    g_bits = NULL;
}

static void update_title(void)
{
    wchar_t s[128];
    swprintf(s, 128, L"%s%s", g_loaded && any_dirty() ? L"* " : L"", APP_TITLE);
    SetWindowTextW(g_wnd, s);
}

static const wchar_t *TOOL_HINT[NTOOL] = {
    L"ペン: 左右のボタンの色で描く", L"消しゴム: 3×3 ドットを白にする", L"塗りつぶし: 同じ色でつながった所を塗る",
    L"スポイト: クリックした点の色を拾う", L"直線: ドラッグで線を引く", L"四角: ドラッグで四角を描く（Shift で中も塗る）" };

static void update_status(void)
{
    wchar_t s[320];
    if (!g_status) return;
    if (!g_loaded) { SetWindowTextW(g_status, L""); return; }
    if (g_listdrag >= 0)
        swprintf(s, 320, L"  %d番をほかの顔グラの上で離すと、入れ替えます", g_listdrag + 1);
    else
        swprintf(s, 320, L"  %d番 %s%s　　%s　　青い●＝元の絵から変えた顔　赤い●＝保存していない顔%s",
                 g_sel + 1, changed_from_original(g_sel) ? L"元の絵から変えてあります" : L"元の絵のまま",
                 face_dirty(g_sel) ? L"（保存していない）" : L"", TOOL_HINT[g_tool],
                 L"　右下の色（255）は透明（背景）");
    SetWindowTextW(g_status, s);
}

static void refresh(void)
{
    if (!g_wnd) return;
    InvalidateRect(g_wnd, NULL, FALSE);
    update_title();
    update_status();
}

/* ---------------------------------------------------------------- editing */
static void put_px(int x, int y, int c) { if (x >= 0 && y >= 0 && x < W && y < H) g_pix[g_sel][y][x] = (BYTE)c; }

static void stamp(int x, int y, int c, int size)
{
    int i, j, h = size / 2;
    for (j = 0; j < size; j++) for (i = 0; i < size; i++) put_px(x - h + i, y - h + j, c);
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
    static short st[MAXW * MAXH * 4][2]; int n = 0, from = g_pix[g_sel][y][x];
    if (from == c) return;
    st[n][0] = (short)x; st[n][1] = (short)y; n++;
    while (n) {
        n--; x = st[n][0]; y = st[n][1];
        if (x < 0 || y < 0 || x >= W || y >= H || g_pix[g_sel][y][x] != from) continue;
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
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) g_pix[g_sel][y][x] = horiz ? t[y][W - 1 - x] : t[H - 1 - y][x];
    refresh();
}

static void shift(int dx, int dy)
{
    Pic t; int x, y;
    push_undo();
    memcpy(t, g_pix[g_sel], sizeof t);
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) g_pix[g_sel][(y + dy + H) % H][(x + dx + W) % W] = t[y][x];
    refresh();
}

static int canvas_cell(int mx, int my, int *px, int *py)
{
    *px = (mx - CX) >= 0 ? (mx - CX) / CS : -1;
    *py = (my - CY) >= 0 ? (my - CY) / CS : -1;
    return mx >= CX && my >= CY && mx < CX + W * CS && my < CY + H * CS;
}

static void set_tool(int t)
{
    int i;
    g_tool = t;
    for (i = 0; i < NTOOL; i++) SendMessageW(g_btn_tool[i], BM_SETCHECK, i == t ? BST_CHECKED : BST_UNCHECKED, 0);
    update_status();
}

static int g_drag = 0, g_dragcol, g_sx, g_sy, g_lx, g_ly, g_dragtool;
static Pic g_dragbase;

static void pick_colour(int right, int c)
{
    if (!g_ok[c]) { MessageBeep(MB_ICONASTERISK); return; }
    if (right) g_colR = c; else g_colL = c;
    refresh();
}

static void mouse_down(int mx, int my, int right, int ctrl)
{
    int px, py, i;
    if (!g_loaded) return;
    i = list_hit(mx, my);
    if (i >= 0) {
        g_sel = i;
        refresh();
        if (!right) { g_listdrag = i; g_listhover = i; SetCapture(g_wnd); return; }
        {
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
    if (mx >= RX && my >= CY && mx < RX + 16 * SWC && my < CY + 16 * SWC) {
        pick_colour(right, ((my - CY) / SWC) * 16 + (mx - RX) / SWC);
        return;
    }
    if (!canvas_cell(mx, my, &px, &py)) return;
    if (ctrl || g_tool == T_PICK) { pick_colour(right, g_pix[g_sel][py][px]); return; }
    push_undo();
    if (g_tool == T_FILL) { flood(px, py, right ? g_colR : g_colL); undo_trim(); refresh(); return; }
    g_drag = 1; g_dragtool = g_tool;
    g_dragcol = g_tool == T_ERASE ? WHITE : (right ? g_colR : g_colL);
    g_sx = g_lx = px; g_sy = g_ly = py;
    memcpy(g_dragbase, g_pix[g_sel], sizeof g_dragbase);
    if (g_tool == T_ERASE) stamp(px, py, g_dragcol, ERASE_SIZE); else put_px(px, py, g_dragcol);
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
        if (px >= W) px = W - 1; if (py >= H) py = H - 1;
        memcpy(g_pix[g_sel], g_dragbase, sizeof g_dragbase);
        if (g_dragtool == T_LINE) line_px(g_sx, g_sy, px, py, g_dragcol, 1);
        else rect_px(g_sx, g_sy, px, py, g_dragcol, shift_key);
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
    wchar_t err[600]; int i, custom = 0;
    if (!any_dirty()) { MessageBoxW(g_wnd, L"保存していない変更はありません。", APP_TITLE, MB_OK | MB_ICONINFORMATION); return; }
    if (g_kind == V_GOLD) {
        for (i = 0; i < NFACE; i++) if (face_dirty(i) && g_large_custom[i]) custom++;
        if (custom && MessageBoxW(g_wnd, L"変えた顔のなかに、大きい絵（PCB）が小さい絵のちょうど 2 倍になっていない顔があります。\n保存すると、その顔の大きい絵は小さい絵を 2 倍にしたものに置き換わります。\n保存しますか？",
                                  APP_TITLE, MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    }
    if (!save_all(err, 600)) { MessageBoxW(g_wnd, err, APP_TITLE, MB_OK | MB_ICONERROR); refresh(); return; }
    refresh();
    MessageBoxW(g_wnd, g_kind == V_GOLD
                ? L"保存しました（小さい絵 PCA と、2 倍の大きい絵 PCB）。\nゲームを起動し直すと、新しい顔グラで表示されます。\n（元のファイルは「.ORIG」を付けて残してあります）"
                : L"保存しました（PORTDATA.VGA）。\nゲームを起動し直すと、新しい顔グラで表示されます。\n（元のファイルは PORTDATA.VGA.ORIG として残してあります）",
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

static int save_dialog(wchar_t *name, const wchar_t *filter, const wchar_t *title, int *fi)
{
    OPENFILENAMEW of;
    ZeroMemory(&of, sizeof of);
    of.lStructSize = sizeof of; of.hwndOwner = g_wnd;
    of.lpstrFilter = filter; of.lpstrFile = name; of.nMaxFile = MAX_PATH; of.lpstrDefExt = L"png";
    of.lpstrTitle = title;
    of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&of)) return 0;
    *fi = of.nFilterIndex;
    return 1;
}

static void do_import(void)
{
    wchar_t name[MAX_PATH], t[160]; Pic pix;
    swprintf(t, 160, L"読み込む画像（%d×%d ドットがおすすめ。違う大きさなら縮めます）", W, H);
    if (!open_dialog(name, t)) return;
    if (!import_file(name, g_dither, pix)) { MessageBoxW(g_wnd, L"この画像は読み込めませんでした。", APP_TITLE, MB_OK | MB_ICONERROR); return; }
    push_undo();
    memcpy(g_pix[g_sel], pix, sizeof pix);
    refresh();
}

static void do_export(void)
{
    wchar_t name[MAX_PATH], filter[200]; int fi;
    swprintf(name, MAX_PATH, L"%s_face%02d.png", g_kind == V_GOLD ? L"gold" : L"dos", g_sel + 1);
    swprintf(filter, 200, L"PNG 画像 (%d×%d)%c*.png%cPNG 画像 (4倍の大きさ・%d×%d)%c*.png%c", W, H, 0, 0, W * 4, H * 4, 0, 0);
    if (!save_dialog(name, filter, L"顔グラを画像に書き出す", &fi)) return;
    if (!export_png(name, g_pix[g_sel], fi == 2 ? 4 : 1)) MessageBoxW(g_wnd, L"書き出せませんでした。", APP_TITLE, MB_OK | MB_ICONERROR);
}

static void do_sheet_out(void)
{
    wchar_t name[MAX_PATH], filter[200]; int fi;
    swprintf(name, MAX_PATH, L"%s_faces_all.png", g_kind == V_GOLD ? L"gold" : L"dos");
    swprintf(filter, 200, L"PNG 画像 (%d×%d)%c*.png%cPNG 画像 (4倍の大きさ・%d×%d)%c*.png%c",
             SHEET_C * W, SHEET_R * H, 0, 0, SHEET_C * W * 4, SHEET_R * H * 4, 0, 0);
    if (!save_dialog(name, filter, L"33 人分をまとめて 1 枚の画像に書き出す（11 列 × 3 行）", &fi)) return;
    if (!export_sheet(name, fi == 2 ? 4 : 1)) MessageBoxW(g_wnd, L"書き出せませんでした。", APP_TITLE, MB_OK | MB_ICONERROR);
}

static void do_sheet_in(void)
{
    static Pic pix[NFACE]; wchar_t name[MAX_PATH], msg[300]; int r;
    if (!open_dialog(name, L"33 人分をまとめた画像（11 列 × 3 行。「全員を書出」で作った形）")) return;
    r = import_sheet(name, g_dither, pix);
    if (r == 0) { MessageBoxW(g_wnd, L"この画像は読み込めませんでした。", APP_TITLE, MB_OK | MB_ICONERROR); return; }
    if (r < 0) {
        swprintf(msg, 300, L"画像の大きさが合いません。\n%d×%d ドット（またはその 2 倍・3 倍…）の、11 列 × 3 行に並べた画像を選んでください。\n「全員を書出」で作った画像を描き換えて使うのがおすすめです。",
                 SHEET_C * W, SHEET_R * H);
        MessageBoxW(g_wnd, msg, APP_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (MessageBoxW(g_wnd, L"33 人分の顔グラを、この画像の絵に置き換えますか？\n（Ctrl+Z で取り消せます。ファイルに書くのは保存したときです）",
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
        MessageBoxW(g_wnd, L"この顔グラの元の絵が見つかりません。\n（.ORIG のファイルがないか、このエディタを使う前にほかの方法で描き換えられています）\n\n元に戻すには、Steam でゲームのプロパティから「ゲームファイルの整合性を確認」をしてください。",
                    APP_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (pic_eq(g_pix[g_sel], g_base[g_sel])) {
        MessageBoxW(g_wnd, L"この顔グラは元の絵のままです。", APP_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (MessageBoxW(g_wnd, L"この顔グラだけを元の絵に戻しますか？\n（Ctrl+Z で取り消せます。ファイルに書くのは保存したときです）",
                    APP_TITLE, MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    push_undo();
    memcpy(g_pix[g_sel], g_base[g_sel], sizeof(Pic));
    refresh();
}

static int confirm_discard(const wchar_t *what)
{
    wchar_t m[200]; int r;
    if (!g_loaded || !any_dirty()) return 1;
    swprintf(m, 200, L"保存していない変更があります。保存してから%sか？", what);
    r = MessageBoxW(g_wnd, m, APP_TITLE, MB_YESNOCANCEL | MB_ICONQUESTION);
    if (r == IDCANCEL) return 0;
    if (r == IDYES) {
        wchar_t err[600];
        if (!save_all(err, 600)) { MessageBoxW(g_wnd, err, APP_TITLE, MB_OK | MB_ICONERROR); return 0; }
    }
    return 1;
}

static void switch_place(int n)
{
    wchar_t err[600];
    if (n == g_cur) return;
    if (!confirm_discard(L"切り替えます")) { SendMessageW(g_combo, CB_SETCURSEL, g_cur, 0); return; }
    if (!load_place(n, err, 600)) MessageBoxW(g_wnd, err, APP_TITLE, MB_OK | MB_ICONERROR);
    refresh();
}

static void command(int id, int code)
{
    if (id >= ID_PEN && id < ID_PEN + NTOOL) { set_tool(id - ID_PEN); return; }
    if (id == ID_COMBO) { if (code == CBN_SELCHANGE) switch_place((int)SendMessageW(g_combo, CB_GETCURSEL, 0, 0)); return; }
    if (!g_loaded) return;
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
    static const int col[NTOOL] = { 0, 0, 1, 1, 0, 1 }, row[NTOOL] = { 0, 1, 0, 1, 2, 2 };
    int i, y = CY + 16 * SWC + 68, bw = 101, bh = 28, x2 = RX + bw + 6;
    g_combo = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, LX, 8, LCOLS * LCW - 8, 300, ID_COMBO);
    for (i = 0; i < g_nplace; i++) {
        wchar_t s[MAX_PATH + 40];
        const wchar_t *leaf = wcsrchr(g_place[i].dir, L'\\');
        swprintf(s, MAX_PATH + 40, L"%s（%s フォルダ）", g_place[i].kind == V_GOLD ? L"Gold 版" : L"DOS 版", leaf ? leaf + 1 : g_place[i].dir);
        SendMessageW(g_combo, CB_ADDSTRING, 0, (LPARAM)s);
    }
    SendMessageW(g_combo, CB_SETCURSEL, g_cur, 0);
    for (i = 0; i < NTOOL; i++)
        g_btn_tool[i] = mk(L"BUTTON", tn[i], BS_AUTORADIOBUTTON | (i == 0 ? WS_GROUP : 0),
                           RX + col[i] * 100, y + row[i] * 24, col[i] ? 106 : 96, 24, ID_PEN + i);
    mk(L"BUTTON", L"交換", BS_PUSHBUTTON | WS_GROUP, RX + 140, CY + 16 * SWC + 30, 54, 30, ID_SWAPCOL);
    y += 3 * 24 + 4;
    g_chk_grid = mk(L"BUTTON", L"マス目", BS_AUTOCHECKBOX | WS_GROUP, RX, y, 96, 24, ID_GRID);
    g_chk_dither = mk(L"BUTTON", L"ディザ", BS_AUTOCHECKBOX, RX + 100, y, 100, 24, ID_DITHER);
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
    case WM_COMMAND: command(LOWORD(wp), HIWORD(wp)); return 0;
    case WM_CLOSE: if (confirm_discard(L"閉じます")) DestroyWindow(w); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(w, m, wp, lp);
}

#ifdef WIZTOOL_TEST   /* 診断は試験版だけ */
/* ---------------------------------------------------------------- diagnostics */
static void selftest(const wchar_t *exe)
{
    wchar_t p[MAX_PATH]; FILE *f; int i, k, n;
    swprintf(p, MAX_PATH, L"%s\\selftest.txt", exe);
    f = _wfopen(p, L"w, ccs=UTF-8");
    if (!f) return;
    fwprintf(f, L"places=%d\n", g_nplace);
    for (i = 0; i < g_nplace; i++) fwprintf(f, L"  %d %s\n", g_place[i].kind, g_place[i].dir);
    fwprintf(f, L"cur=%d kind=%d loaded=%d W=%d H=%d CS=%d\n", g_cur, g_kind, g_loaded, W, H, CS);
    if (g_loaded) {
        for (k = 0, n = 0; k < NFACE; k++) n += g_base_ok[k];
        fwprintf(f, L"originals found=%d  large_custom=", n);
        for (k = 0, n = 0; k < NFACE; k++) n += g_large_custom[k];
        fwprintf(f, L"%d  usable colours=", n);
        for (k = 0, n = 0; k < 256; k++) n += g_ok[k];
        fwprintf(f, L"%d\n", n);
        {
            int bad = 0, d; Pic back;
            for (i = 0; i < NFACE; i++)
                for (d = 0; d < 2; d++) {
                    swprintf(p, MAX_PATH, L"%s\\selftest_%02d.png", exe, i + 1);
                    if (!export_png(p, g_pix[i], d ? 4 : 1) || !import_file(p, 0, back) || !pic_eq(back, g_pix[i])) bad++;
                    DeleteFileW(p);
                }
            fwprintf(f, L"png export/import x1,x4: %d mismatches\n", bad);
        }
        {
            static Pic back[NFACE]; int d, r, same;
            for (d = 0; d < 2; d++) {
                swprintf(p, MAX_PATH, L"%s\\selftest_sheet.png", exe);
                r = export_sheet(p, d ? 4 : 1);
                r = r ? import_sheet(p, 0, back) : 0;
                for (i = 0, same = (r == 1); i < NFACE && same; i++) same = pic_eq(back[i], g_pix[i]);
                fwprintf(f, L"sheet x%d: import=%d same=%d\n", d ? 4 : 1, r, same);
                DeleteFileW(p);
            }
        }
        {
            Pic keep; int same;
            memcpy(keep, g_pix[g_sel], sizeof keep);
            flip(1); flip(1); flip(0); flip(0);
            for (k = 0; k < W; k++) shift(1, 0);
            for (k = 0; k < 3; k++) shift(0, -1);
            for (k = 0; k < 3; k++) shift(0, 1);
            same = pic_eq(keep, g_pix[g_sel]);
            memcpy(g_pix[g_sel], keep, sizeof keep); g_nundo = 0;
            flip(1); shift(1, 0); shift(0, 1);
            while (g_nundo) undo_redo(g_undo, &g_nundo, g_redo, &g_nredo);
            fwprintf(f, L"flip/shift return=%d  after undo same=%d\n", same, pic_eq(keep, g_pix[g_sel]));
        }
        fwprintf(f, L"dirty=%d\n", any_dirty());
        if (wcsstr(GetCommandLineW(), L"--savetest")) {
            wchar_t err[600] = L""; Pic want; int r, c2;
            g_pix[5][0][0] = WHITE; g_pix[5][H - 1][W - 1] = 0;
            memcpy(want, g_pix[5], sizeof want);
            r = save_all(err, 600);
            fwprintf(f, L"save=%d %s\n", r, err);
            c2 = g_cur; load_place(c2, err, 600);
            fwprintf(f, L"reload_changed=%d base_ok6=%d changed6=%d other_same=%d large_custom6=%d\n",
                     pic_eq(g_pix[5], want), g_base_ok[5], changed_from_original(5),
                     pic_eq(g_pix[4], g_base[4]), g_large_custom[5]);
        }
    }
    fclose(f);
}

static void snapshot(const wchar_t *exe)
{
    wchar_t p[MAX_PATH]; HDC sdc = GetDC(NULL), mdc = CreateCompatibleDC(sdc);
    HBITMAP bmp = CreateCompatibleBitmap(sdc, CLIENT_W, CLIENT_H); BITMAPINFO bi; DWORD *px; BITMAPFILEHEADER fh; FILE *f;
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
}

#endif /* WIZTOOL_TEST */

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmd, int show)
{
    WNDCLASSW wc; MSG msg; wchar_t exe[MAX_PATH], *s, err[600]; HANDLE mutex; HACCEL acc; RECT r; int i, want = -1;
#ifdef WIZTOOL_TEST
    int diag = cmd && (wcsstr(cmd, L"--selftest") || wcsstr(cmd, L"--snapshot"));
#else
    int diag = 0;   /* 配布版には診断のコマンドラインは無い */
#endif
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
    find_places(exe);

    if (!diag) {
        mutex = CreateMutexW(NULL, TRUE, L"Wiz7JpFaceEdit");
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            HWND w = FindWindowW(L"W7FACE", NULL);
            if (w) { ShowWindow(w, SW_RESTORE); SetForegroundWindow(w); }
            return 0;
        }
        (void)mutex;
        while (!g_nplace) {
            if (MessageBoxW(NULL, L"ゲームのフォルダが見つかりませんでした。\nGold 版なら wizardry7gold の WIZARD.EXE、DOS 版なら DSAVANT の DS.EXE を選んでください。",
                            APP_TITLE, MB_OKCANCEL | MB_ICONINFORMATION) != IDOK) return 0;
            ask_place(NULL);
        }
    }
    if (cmd && wcsstr(cmd, L"--ver=dos")) want = V_DOS;
    if (cmd && wcsstr(cmd, L"--ver=gold")) want = V_GOLD;
    for (i = 0; i < g_nplace; i++) if (want < 0 || g_place[i].kind == want) { if (load_place(i, err, 600)) break; }
    if (!g_loaded && !diag) { MessageBoxW(NULL, err, APP_TITLE, MB_OK | MB_ICONERROR); return 0; }
#ifdef WIZTOOL_TEST   /* 診断: --selftest は試験版だけ */
    if (cmd && wcsstr(cmd, L"--selftest")) { selftest(exe); GdiplusShutdown(g_gdip); return 0; }
#endif

    g_font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, SHIFTJIS_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Meiryo UI");
    g_font_small = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, SHIFTJIS_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Meiryo UI");
    ZeroMemory(&wc, sizeof wc);
    wc.lpfnWndProc = WndProc; wc.hInstance = inst; wc.lpszClassName = L"W7FACE";
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW); wc.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    RegisterClassW(&wc);
    r.left = 0; r.top = 0; r.right = CLIENT_W; r.bottom = CLIENT_H;
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    g_wnd = CreateWindowExW(0, L"W7FACE", APP_TITLE, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                            CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
#ifdef WIZTOOL_TEST   /* 診断: --snapshot は試験版だけ */
    if (cmd && wcsstr(cmd, L"--snapshot")) {
        wchar_t *a = wcsstr(cmd, L"--sel=");
        if (a) { int sel = _wtoi(a + 6) - 1; if (sel >= 0 && sel < NFACE) g_sel = sel; }
        snapshot(exe);
        DestroyWindow(g_wnd);
        GdiplusShutdown(g_gdip);
        return 0;
    }
#endif
    ShowWindow(g_wnd, SW_SHOW);
    refresh();
    acc = CreateAcceleratorTableW(ak, sizeof ak / sizeof ak[0]);
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (msg.hwnd == g_combo || GetParent(msg.hwnd) == g_combo) { TranslateMessage(&msg); DispatchMessageW(&msg); continue; }
        if (TranslateAcceleratorW(g_wnd, acc, &msg)) continue;
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    GdiplusShutdown(g_gdip);
    return 0;
}
