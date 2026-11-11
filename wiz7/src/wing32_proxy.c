/*
 * Wizardry 7 Gold 日本語化MOD - wing32.dll プロキシ
 *
 * WIZARD.EXE は LoadLibrary("wing32.dll") で WinG を読み込むため、
 * 同じフォルダに置いたこの DLL が先に読み込まれる。
 * 本物の WinG (..\WING32.DLL 等) へ処理を転送しつつ、
 * WIZARD.EXE の文字描画・幅計算・メッセージ取得をフックして日本語を表示する。
 *
 * 内部文字コード:
 *   ASCII (0x00-0x7F) はそのまま。
 *   全角文字は 2 バイト [0x80+k/127][0x80+k%127] (各 0x80-0xFE)。
 *   k は翻訳データに出現した文字の通し番号 (DLL 起動時に動的に割り当て)。
 *   両バイトとも 0x80 以上なので、ゲームの制御文字 (@ & % ] 等) と衝突しない。
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <intrin.h>
#include <ctype.h>
#pragma intrinsic(_ReturnAddress)

/* ------------------------------------------------------------------ */
/* WIZARD.EXE (Wizardry Gold 1.1b) のアドレス                          */
/* ------------------------------------------------------------------ */
#define EXE_BASE          0x400000
#define A_DRAW_CHAR       0x425d90  /* void draw_char(uchar ch, short font) */
#define A_TEXT_WIDTH      0x426cc7  /* short text_width(char *s, short font) */
#define A_MSG_FETCH       0x467e33  /* void msg_fetch(short id, char *dst) */
#define A_FONTTAB         0x4a7930  /* 24 byte/entry, +0x0c = header ptr */
#define A_CUR_X           0x4a803c  /* int, 論理座標 (320x200) */
#define A_CUR_Y           0x4a8030
#define A_COL_BG          0x4a945c  /* short */
#define A_COL_FG          0x4a9468  /* short */
#define A_OPAQUE          0x4a7118  /* short, 1 = 背景も塗る */
#define A_FRAMEBUF        0x4a7100  /* uchar*, 640x400 8bpp */
#define A_MSG_STATUS      0x4a0f78  /* short, 0 = ok */
#define A_GAME_STATE      0x4a0f94  /* short, 3 = 終了中 */

#define FB_W 640
#define FB_H 400

static DWORD g_delta;               /* 実ロードアドレス - EXE_BASE */
#define P(T, a) ((T)((a) + g_delta))

/* ------------------------------------------------------------------ */
/* ログ                                                                */
/* ------------------------------------------------------------------ */
static char g_dir[MAX_PATH];        /* jpmod データフォルダ (末尾 \) */
static FILE *g_log;

static void logf(const char *fmt, ...)
{
    va_list ap;
    if (!g_log) return;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

/* ------------------------------------------------------------------ */
/* WinG 互換実装                                                        */
/*                                                                    */
/* Steam 同梱の WING32.DLL はシステムフォルダ以外に置かれると起動を拒否  */
/* するため転送はせず、NT 系での WinG と同じく DIBSection + BitBlt で   */
/* 自前実装する (管理者権限での WinG インストールも不要になる)。        */
/* ------------------------------------------------------------------ */
static HDC g_wingdc;                /* 最後に作られた WinG DC (デバッグ用) */

HDC WINAPI WinGCreateDC_impl(void)
{
    return g_wingdc = CreateCompatibleDC(NULL);
}

BOOL WINAPI WinGRecommendDIBFormat_impl(BITMAPINFO *bi)
{
    if (!bi) return FALSE;
    bi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi->bmiHeader.biWidth = 1;
    bi->bmiHeader.biHeight = -1;           /* トップダウン */
    bi->bmiHeader.biPlanes = 1;
    bi->bmiHeader.biBitCount = 8;
    bi->bmiHeader.biCompression = BI_RGB;
    bi->bmiHeader.biSizeImage = 0;
    bi->bmiHeader.biXPelsPerMeter = 0;
    bi->bmiHeader.biYPelsPerMeter = 0;
    bi->bmiHeader.biClrUsed = 0;
    bi->bmiHeader.biClrImportant = 0;
    return TRUE;
}

HBITMAP WINAPI WinGCreateBitmap_impl(HDC dc, const BITMAPINFO *bi, void **bits)
{
    void *p = NULL;
    HBITMAP h = CreateDIBSection(dc, bi, DIB_RGB_COLORS, &p, NULL, 0);
    if (bits) *bits = p;
    logf("WinGCreateBitmap %ldx%ld %dbpp -> %p bits=%p", bi->bmiHeader.biWidth,
         bi->bmiHeader.biHeight, bi->bmiHeader.biBitCount, h, p);
    return h;
}

void *WINAPI WinGGetDIBPointer_impl(HBITMAP h, BITMAPINFO *bi)
{
    DIBSECTION ds;
    if (GetObjectA(h, sizeof ds, &ds) != sizeof ds) return NULL;
    if (bi) bi->bmiHeader = ds.dsBmih;
    return ds.dsBm.bmBits;
}

UINT WINAPI WinGGetDIBColorTable_impl(HDC dc, UINT start, UINT n, RGBQUAD *c)
{
    return GetDIBColorTable(dc, start, n, c);
}

static RGBQUAD g_pal[256];          /* デバッグ用スクリーンショットのため保持 */

UINT WINAPI WinGSetDIBColorTable_impl(HDC dc, UINT start, UINT n, const RGBQUAD *c)
{
    UINT i;
    for (i = 0; i < n && start + i < 256; i++) g_pal[start + i] = c[i];
    return SetDIBColorTable(dc, start, n, c);
}

HPALETTE WINAPI WinGCreateHalftonePalette_impl(void)
{
    HDC sdc = GetDC(NULL);
    HPALETTE p = CreateHalftonePalette(sdc);
    ReleaseDC(NULL, sdc);
    return p;
}

HBRUSH WINAPI WinGCreateHalftoneBrush_impl(HDC dc, COLORREF c, int dither)
{
    (void)dc; (void)dither;
    return CreateSolidBrush(c);
}

/* ------------------------------------------------------------------ */
/* 拡大表示                                                             */
/*  ゲームは 640x400 の画面をウィンドウ中央にそのまま転送する。            */
/*  転送 (WinGBitBlt / StretchDIBits / PatBlt) をウィンドウ中央への拡大に  */
/*  置き換え、マウスの座標は元の 640x400 の位置に戻してゲームに渡す。      */
/* ------------------------------------------------------------------ */
#define GAME_W 640
#define GAME_H 400
static int g_disp_h = 1080;   /* [display] height: 拡大後の高さ (実ピクセル)。0 = 拡大しない、9999 = ウィンドウいっぱい */
static int g_disp_smooth;     /* [display] smooth=1: なめらかに拡大 (HALFTONE) */
static HWND g_gamewnd;
static WNDPROC g_oldproc;
static int g_have_org, g_gx0, g_gy0;   /* ゲームが自分で置いている 640x400 の左上 (クライアント座標) */

typedef struct { int on; double s; int ox, oy, gx0, gy0; } DispXf;

static DispXf disp_xf(HWND h)
{
    DispXf x;
    RECT rc;
    int cw, ch, want;
    double s;
    ZeroMemory(&x, sizeof x);
    x.s = 1;
    if (!h) return x;
    GetClientRect(h, &rc);
    cw = rc.right;
    ch = rc.bottom;
    x.gx0 = g_have_org ? g_gx0 : (cw - GAME_W) / 2;
    x.gy0 = g_have_org ? g_gy0 : (ch - GAME_H) / 2;
    if (g_disp_h <= 0 || cw <= 0 || ch <= 0) return x;
    want = g_disp_h < ch ? g_disp_h : ch;
    s = (double)want / GAME_H;
    if (GAME_W * s > cw) s = (double)cw / GAME_W;
    if (s <= 1.0) return x;
    x.on = 1;
    x.s = s;
    x.ox = (cw - (int)(GAME_W * s + 0.5)) / 2;
    x.oy = (ch - (int)(GAME_H * s + 0.5)) / 2;
    return x;
}

static int disp_x(const DispXf *x, int gx) { return x->ox + (int)((gx - x->gx0) * x->s + 0.5); }
static int disp_y(const DispXf *x, int gy) { return x->oy + (int)((gy - x->gy0) * x->s + 0.5); }

static LRESULT CALLBACK disp_wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m >= WM_MOUSEFIRST && m <= WM_MBUTTONDBLCLK) {
        DispXf x = disp_xf(h);
        if (x.on) {
            int cx = (short)LOWORD(l), cy = (short)HIWORD(l);
            int gx = x.gx0 + (int)((cx - x.ox) / x.s), gy = x.gy0 + (int)((cy - x.oy) / x.s);
            l = MAKELPARAM((WORD)(short)gx, (WORD)(short)gy);
        }
    } else if (m == WM_SIZE) {
        InvalidateRect(h, NULL, TRUE);
    }
    return CallWindowProcA(g_oldproc, h, m, w, l);
}

static void disp_attach(HDC dc)
{
    HWND h = WindowFromDC(dc);
    if (!h || g_gamewnd || g_disp_h <= 0) return;   /* 最初に描かれたウィンドウだけ */
    g_gamewnd = h;
    g_oldproc = (WNDPROC)SetWindowLongA(h, GWL_WNDPROC, (LONG)disp_wndproc);
    InvalidateRect(h, NULL, TRUE);
    logf("display: window %p, height %d", h, g_disp_h);
}

static void disp_mode(HDC dc)
{
    if (g_disp_smooth) {
        SetStretchBltMode(dc, HALFTONE);
        SetBrushOrgEx(dc, 0, 0, NULL);
    } else {
        SetStretchBltMode(dc, COLORONCOLOR);
    }
}

BOOL WINAPI WinGBitBlt_impl(HDC dst, int xd, int yd, int w, int h, HDC src, int xs, int ys)
{
    DispXf x;
    disp_attach(dst);
    if (g_gamewnd && WindowFromDC(dst) == g_gamewnd) {
        g_have_org = 1;
        g_gx0 = xd - xs;
        g_gy0 = yd - ys;
        x = disp_xf(g_gamewnd);
        if (x.on) {
            int X0 = disp_x(&x, xd), Y0 = disp_y(&x, yd), X1 = disp_x(&x, xd + w), Y1 = disp_y(&x, yd + h);
            disp_mode(dst);
            return StretchBlt(dst, X0, Y0, X1 - X0, Y1 - Y0, src, xs, ys, w, h, SRCCOPY);
        }
    }
    return BitBlt(dst, xd, yd, w, h, src, xs, ys, SRCCOPY);
}

BOOL WINAPI WinGStretchBlt_impl(HDC dst, int xd, int yd, int wd, int hd,
                                HDC src, int xs, int ys, int ws, int hs)
{
    DispXf x;
    disp_attach(dst);
    x = disp_xf((g_gamewnd && WindowFromDC(dst) == g_gamewnd) ? g_gamewnd : NULL);
    if (x.on) {
        int X0 = disp_x(&x, xd), Y0 = disp_y(&x, yd), X1 = disp_x(&x, xd + wd), Y1 = disp_y(&x, yd + hd);
        disp_mode(dst);
        return StretchBlt(dst, X0, Y0, X1 - X0, Y1 - Y0, src, xs, ys, ws, hs, SRCCOPY);
    }
    SetStretchBltMode(dst, COLORONCOLOR);
    return StretchBlt(dst, xd, yd, wd, hd, src, xs, ys, ws, hs, SRCCOPY);
}

/* ゲーム領域 (640x400) の中への描画なら、拡大後の位置に直す */
static int disp_rect(HDC dc, int *x0, int *y0, int *w, int *h)
{
    DispXf x;
    int X0, Y0, X1, Y1;
    if (!g_gamewnd || WindowFromDC(dc) != g_gamewnd) return 0;
    x = disp_xf(g_gamewnd);
    if (!x.on) return 0;
    if (*x0 < x.gx0 || *y0 < x.gy0 || *x0 + *w > x.gx0 + GAME_W || *y0 + *h > x.gy0 + GAME_H) return 0;
    X0 = disp_x(&x, *x0);
    Y0 = disp_y(&x, *y0);
    X1 = disp_x(&x, *x0 + *w);
    Y1 = disp_y(&x, *y0 + *h);
    *x0 = X0;
    *y0 = Y0;
    *w = X1 - X0;
    *h = Y1 - Y0;
    return 1;
}

typedef int (WINAPI *StretchDIBits_t)(HDC, int, int, int, int, int, int, int, int, const void *, const BITMAPINFO *, UINT, DWORD);
typedef BOOL (WINAPI *PatBlt_t)(HDC, int, int, int, int, DWORD);
typedef BOOL (WINAPI *SetCursorPos_t)(int, int);
static StretchDIBits_t real_StretchDIBits;
static PatBlt_t real_PatBlt;
static SetCursorPos_t real_SetCursorPos;

static int WINAPI hk_StretchDIBits(HDC dc, int xd, int yd, int wd, int hd, int xs, int ys, int ws, int hs,
                                   const void *bits, const BITMAPINFO *bi, UINT use, DWORD rop)
{
    if (disp_rect(dc, &xd, &yd, &wd, &hd)) disp_mode(dc);
    return real_StretchDIBits(dc, xd, yd, wd, hd, xs, ys, ws, hs, bits, bi, use, rop);
}

static BOOL WINAPI hk_PatBlt(HDC dc, int x, int y, int w, int h, DWORD rop)
{
    disp_rect(dc, &x, &y, &w, &h);
    return real_PatBlt(dc, x, y, w, h, rop);
}

/* ゲームがマウスカーソルを動かすとき (ゲーム内の座標) も拡大後の位置にする */
static BOOL WINAPI hk_SetCursorPos(int X, int Y)
{
    if (g_gamewnd) {
        DispXf x = disp_xf(g_gamewnd);
        if (x.on) {
            POINT p;
            p.x = X;
            p.y = Y;
            ScreenToClient(g_gamewnd, &p);
            p.x = disp_x(&x, p.x);
            p.y = disp_y(&x, p.y);
            ClientToScreen(g_gamewnd, &p);
            X = p.x;
            Y = p.y;
        }
    }
    return real_SetCursorPos(X, Y);
}
static int g_debug;

/* 動画 (MCI の AVI 再生) の表示先も拡大する。
 * MCI_PUT の MCI_DGV_PUT_DESTINATION と、文字列コマンド "put ... destination at x y w h" を書き換える。 */
#define MCI_PUT_ 0x0842
#define MCI_DGV_RECT_ 0x00010000L
#define MCI_DGV_PUT_DESTINATION_ 0x00040000L
typedef struct { DWORD_PTR cb; RECT rc; } MciRectParms;
typedef DWORD (WINAPI *mciSendCommandA_t)(UINT, UINT, DWORD_PTR, DWORD_PTR);
typedef DWORD (WINAPI *mciSendStringA_t)(LPCSTR, LPSTR, UINT, HWND);
static mciSendCommandA_t real_mciSendCommandA;
static mciSendStringA_t real_mciSendStringA;

static void disp_mci_rect(int *x, int *y, int *w, int *h)
{
    DispXf d;
    int X0, Y0, X1, Y1;
    if (!g_gamewnd) return;
    d = disp_xf(g_gamewnd);
    if (!d.on) return;
    X0 = disp_x(&d, *x);
    Y0 = disp_y(&d, *y);
    X1 = disp_x(&d, *x + *w);
    Y1 = disp_y(&d, *y + *h);
    *x = X0;
    *y = Y0;
    *w = X1 - X0;
    *h = Y1 - Y0;
}

static DWORD WINAPI hk_mciSendCommandA(UINT dev, UINT msg, DWORD_PTR fdw, DWORD_PTR parm)
{
    if (g_debug >= 2) logf("mciSendCommand dev=%u msg=%04x flags=%08lx", dev, msg, (DWORD)fdw);
    if (msg == MCI_PUT_ && (fdw & MCI_DGV_PUT_DESTINATION_) && (fdw & MCI_DGV_RECT_) && parm) {
        MciRectParms p = *(MciRectParms *)parm;
        int x = p.rc.left, y = p.rc.top, w = p.rc.right, h = p.rc.bottom;   /* MCI_PUT の RECT は x, y, 幅, 高さ */
        disp_mci_rect(&x, &y, &w, &h);
        p.rc.left = x;
        p.rc.top = y;
        p.rc.right = w;
        p.rc.bottom = h;
        if (g_debug >= 2) logf("  put destination -> %d %d %d %d", x, y, w, h);
        return real_mciSendCommandA(dev, msg, fdw, (DWORD_PTR)&p);
    }
    return real_mciSendCommandA(dev, msg, fdw, parm);
}

/* 動画の子ウィンドウ ("status <alias> window handle" の返り値) と、その別名 */
static HWND g_movwnd;
static char g_movalias[64];

typedef BOOL (WINAPI *MoveWindow_t)(HWND, int, int, int, int, BOOL);
typedef BOOL (WINAPI *SetWindowPos_t)(HWND, HWND, int, int, int, int, UINT);
static MoveWindow_t real_MoveWindow;
static SetWindowPos_t real_SetWindowPos;

/* 動画ウィンドウを拡大した位置に置き、動画もウィンドウいっぱいに描かせる */
static int disp_movie_rect(HWND h, int *x, int *y, int *w, int *hh)
{
    char cmd[128];
    if (!h || h != g_movwnd || !g_gamewnd) return 0;
    if (!disp_xf(g_gamewnd).on) return 0;
    disp_mci_rect(x, y, w, hh);
    if (g_movalias[0] && real_mciSendStringA) {
        _snprintf(cmd, sizeof cmd, "put %s destination at 0 0 %d %d", g_movalias, *w, *hh);
        cmd[sizeof cmd - 1] = 0;
        real_mciSendStringA(cmd, NULL, 0, NULL);
    }
    if (g_debug >= 2) logf("movie window -> %d %d %d %d", *x, *y, *w, *hh);
    return 1;
}

static BOOL WINAPI hk_MoveWindow(HWND h, int x, int y, int w, int hh, BOOL rp)
{
    disp_movie_rect(h, &x, &y, &w, &hh);
    return real_MoveWindow(h, x, y, w, hh, rp);
}

static BOOL WINAPI hk_SetWindowPos(HWND h, HWND after, int x, int y, int w, int hh, UINT fl)
{
    if (!(fl & SWP_NOMOVE) && !(fl & SWP_NOSIZE)) disp_movie_rect(h, &x, &y, &w, &hh);
    return real_SetWindowPos(h, after, x, y, w, hh, fl);
}

static DWORD WINAPI hk_mciSendStringA(LPCSTR cmd, LPSTR ret, UINT n, HWND cb)
{
    char buf[512];
    const char *at;
    int x, y, w, h;
    if (g_debug >= 2) logf("mciSendString \"%s\"", cmd ? cmd : "");
    if (cmd && !_strnicmp(cmd, "put ", 4) && strstr(cmd, "destination") && (at = strstr(cmd, " at ")) != NULL &&
        sscanf(at + 4, "%d %d %d %d", &x, &y, &w, &h) == 4 && (size_t)(at - cmd) < 400) {
        disp_mci_rect(&x, &y, &w, &h);
        _snprintf(buf, sizeof buf, "%.*s at %d %d %d %d", (int)(at - cmd), cmd, x, y, w, h);
        buf[sizeof buf - 1] = 0;
        if (g_debug >= 2) logf("  -> \"%s\"", buf);
        return real_mciSendStringA(buf, ret, n, cb);
    }
    if (cmd && !_strnicmp(cmd, "open ", 5) && (at = strstr(cmd, " alias ")) != NULL) {
        sscanf(at + 7, "%63s", g_movalias);
    }
    if (cmd && !_strnicmp(cmd, "status ", 7) && strstr(cmd, "window handle")) {
        DWORD r = real_mciSendStringA(cmd, ret, n, cb);
        if (!r && ret) {
            g_movwnd = (HWND)(DWORD_PTR)strtoul(ret, NULL, 10);
            if (g_debug >= 2) logf("movie window handle %p", g_movwnd);
        }
        return r;
    }
    return real_mciSendStringA(cmd, ret, n, cb);
}

/* ------------------------------------------------------------------ */
/* 設定                                                                */
/* ------------------------------------------------------------------ */
typedef struct {
    int size;   /* グリフ描画サイズ (実ピクセル, 640x400 上) */
    int adv;    /* 送り幅 (論理ピクセル, 320x200 上) */
    int yoff;   /* セル上端からの縦オフセット (実ピクセル) */
    char bdf[MAX_PATH];  /* BDF ビットマップフォント (jpmod フォルダからの相対パス)。空なら GDI */
    int scale;           /* BDF の拡大倍率 (整数倍、最近傍) */
} FontCfg;

#define NFONT 8
static FontCfg g_fcfg[NFONT];
static wchar_t g_face[64] = L"MS Gothic";
static int g_nohooks;      /* [debug] nohooks=1: WinG だけ提供し、日本語化はしない (比較用) */

static void load_config(void)
{
    char ini[MAX_PATH], sec[16], face[64];
    int i;
    /* 既定値: font0=6x6, font1=11x9(数字), font2=12x14(本文), font3=7x4 */
    static const FontCfg def[NFONT] = {
        {12, 6, 0}, {16, 8, 1}, {20, 10, 2}, {8, 4, 0},
        {20, 10, 2}, {20, 10, 2}, {16, 8, 1}, {20, 10, 2}
    };
    _snprintf(ini, MAX_PATH, "%sjpmod.ini", g_dir);
    GetPrivateProfileStringA("font", "face", "MS Gothic", face, sizeof face, ini);
    MultiByteToWideChar(CP_UTF8, 0, face, -1, g_face, 64);
    g_debug = GetPrivateProfileIntA("debug", "log", 1, ini);
    g_nohooks = GetPrivateProfileIntA("debug", "nohooks", 0, ini);
    g_disp_h = GetPrivateProfileIntA("display", "height", 1080, ini);
    g_disp_smooth = GetPrivateProfileIntA("display", "smooth", 0, ini);
    for (i = 0; i < NFONT; i++) {
        _snprintf(sec, sizeof sec, "font%d", i);
        g_fcfg[i].size = GetPrivateProfileIntA(sec, "size", def[i].size, ini);
        g_fcfg[i].adv  = GetPrivateProfileIntA(sec, "adv",  def[i].adv,  ini);
        g_fcfg[i].yoff = GetPrivateProfileIntA(sec, "yoff", def[i].yoff, ini);
        GetPrivateProfileStringA(sec, "bdf", "", g_fcfg[i].bdf, MAX_PATH, ini);
        g_fcfg[i].scale = GetPrivateProfileIntA(sec, "scale", 1, ini);
        if (g_fcfg[i].scale < 1 || g_fcfg[i].scale > 4) g_fcfg[i].scale = 1;
    }
}

/* ------------------------------------------------------------------ */
/* 文字表 (内部コード <-> Unicode)                                      */
/* ------------------------------------------------------------------ */
#define MAXCODE (127 * 127)
static wchar_t g_code2wc[MAXCODE];
static int g_ncode;
static unsigned short *g_wc2code;   /* 0x10000 要素, 0 = 未割当, それ以外 code+1 */

static int code_for(wchar_t wc)
{
    unsigned short c = g_wc2code[wc];
    if (c) return c - 1;
    if (g_ncode >= MAXCODE) return -1;
    g_code2wc[g_ncode] = wc;
    g_wc2code[wc] = (unsigned short)(g_ncode + 1);
    return g_ncode++;
}

#define IS_HI(c) ((unsigned char)(c) >= 0x80 && (unsigned char)(c) <= 0xFE)

/* ------------------------------------------------------------------ */
/* 翻訳テーブル                                                         */
/* ------------------------------------------------------------------ */
static char *g_msg[32768];          /* id -> 内部コード文字列 (NULL = 原文) */
static int g_nmsg;

static int hexv(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* UTF-8 の翻訳文を内部コードへ変換。{xx} は生バイト。 */
static int encode_line(const char *src, char *dst, int cap)
{
    int n = 0;
    const unsigned char *s = (const unsigned char *)src;
    while (*s) {
        unsigned cp;
        int len;
        if (*s == '{' && hexv(s[1]) >= 0 && hexv(s[2]) >= 0 && s[3] == '}') {
            if (n + 1 >= cap) break;
            dst[n++] = (char)(hexv(s[1]) * 16 + hexv(s[2]));
            s += 4;
            continue;
        }
        if (*s < 0x80) {
            if (n + 1 >= cap) break;
            dst[n++] = (char)*s++;
            continue;
        }
        if ((*s & 0xE0) == 0xC0) { cp = *s & 0x1F; len = 2; }
        else if ((*s & 0xF0) == 0xE0) { cp = *s & 0x0F; len = 3; }
        else { s++; continue; }      /* BMP 外は無視 */
        {
            int k, code;
            for (k = 1; k < len; k++) {
                if ((s[k] & 0xC0) != 0x80) break;
                cp = (cp << 6) | (s[k] & 0x3F);
            }
            if (k < len) { s++; continue; }
            s += len;
            code = code_for((wchar_t)cp);
            if (code < 0 || n + 2 >= cap) continue;
            dst[n++] = (char)(0x80 + code / 127);
            dst[n++] = (char)(0x80 + code % 127);
        }
    }
    dst[n] = 0;
    return n;
}

static void load_translations(void)
{
    char path[MAX_PATH];
    FILE *f;
    char line[4096], enc[512];
    int lineno = 0;

    _snprintf(path, MAX_PATH, "%smsg_ja.txt", g_dir);
    f = fopen(path, "rb");
    if (!f) { logf("msg_ja.txt not found: %s", path); return; }
    while (fgets(line, sizeof line, f)) {
        char *tab, *e, *t;
        long id;
        int n;
        lineno++;
        e = line + strlen(line);
        while (e > line && (e[-1] == '\n' || e[-1] == '\r')) *--e = 0;
        t = line;
        if (lineno == 1 && (unsigned char)t[0] == 0xEF) t += 3;   /* BOM */
        if (!*t || *t == '#') continue;
        tab = strchr(t, '\t');
        if (!tab) continue;
        *tab = 0;
        id = strtol(t, NULL, 10);
        if (id < 0 || id >= 32768) continue;
        n = encode_line(tab + 1, enc, 256);   /* Pascal 文字列なので最大 255 */
        if (n > 255) { logf("msg %ld too long (%d bytes), truncated", id, n); enc[255] = 0; }
        free(g_msg[id]);
        g_msg[id] = _strdup(enc);
        g_nmsg++;
    }
    fclose(f);
    logf("translations: %d messages, %d glyphs", g_nmsg, g_ncode);
}

/* ------------------------------------------------------------------ */
/* グリフ生成 (GDI)                                                     */
/* ------------------------------------------------------------------ */
typedef struct { unsigned char *bits; } Glyph;   /* size*size, 0/1 */
static Glyph *g_glyph[NFONT];        /* [font][code] */
static HDC g_gdc;
static HBITMAP g_gbmp;
static DWORD *g_gpix;
static int g_gdim = 64;
static HFONT g_hfont[NFONT];

static void glyph_init(void)
{
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = g_gdim;
    bi.bmiHeader.biHeight = -g_gdim;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    g_gdc = CreateCompatibleDC(NULL);
    g_gbmp = CreateDIBSection(g_gdc, &bi, DIB_RGB_COLORS, (void **)&g_gpix, NULL, 0);
    SelectObject(g_gdc, g_gbmp);
    SetBkMode(g_gdc, OPAQUE);
    SetBkColor(g_gdc, RGB(0, 0, 0));
    SetTextColor(g_gdc, RGB(255, 255, 255));
}

static const unsigned char *get_glyph(int font, int code)
{
    int size = g_fcfg[font].size, x, y;
    Glyph *g;
    wchar_t wc;
    if (size < 4) size = 4;
    if (size > g_gdim) size = g_gdim;
    if (!g_glyph[font]) g_glyph[font] = (Glyph *)calloc(MAXCODE, sizeof(Glyph));
    g = &g_glyph[font][code];
    if (g->bits) return g->bits;

    if (!g_hfont[font])
        g_hfont[font] = CreateFontW(-size, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    NONANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, g_face);
    SelectObject(g_gdc, g_hfont[font]);
    PatBlt(g_gdc, 0, 0, g_gdim, g_gdim, BLACKNESS);
    wc = g_code2wc[code];
    TextOutW(g_gdc, 0, 0, &wc, 1);
    GdiFlush();
    g->bits = (unsigned char *)malloc(size * size);
    for (y = 0; y < size; y++)
        for (x = 0; x < size; x++)
            g->bits[y * size + x] = (g_gpix[y * g_gdim + x] & 0x00FFFFFF) ? 1 : 0;
    return g->bits;
}

/* ------------------------------------------------------------------ */
/* BDF ビットマップフォント (東雲フォントなど)                          */
/*  JIS X 0208 の BDF を読み、訳文で使う文字だけグリフ表に入れる。        */
/*  BDF に無い文字は get_glyph が GDI で作る。                            */
/* ------------------------------------------------------------------ */
static wchar_t jis_to_wc(int jis)
{
    int j1 = (jis >> 8) & 0xff, j2 = jis & 0xff;
    unsigned char sj[2];
    wchar_t wc = 0;
    if (j1 < 0x21 || j1 > 0x7e || j2 < 0x21 || j2 > 0x7e) return 0;
    /* JIS -> Shift_JIS (cp932 の対応表で Unicode にする。～ が U+FF5E になるなど訳文と一致する) */
    sj[0] = (unsigned char)(((j1 + 1) >> 1) + (j1 <= 0x5e ? 0x70 : 0xb0));
    sj[1] = (unsigned char)(j2 + ((j1 & 1) ? (j2 < 0x60 ? 0x1f : 0x20) : 0x7e));
    if (MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, (char *)sj, 2, &wc, 1) != 1) return 0;
    return wc;
}

static void load_bdf(int font)
{
    char path[MAX_PATH], line[256];
    FILE *f;
    int size = 0, fbw = 0, fbh = 0, fbx = 0, fby = 0;
    int enc = -1, bw = 0, bh = 0, bx = 0, by = 0, row = -1, n = 0, sc = g_fcfg[font].scale;
    unsigned char *bits = NULL;
    if (!g_fcfg[font].bdf[0]) return;
    _snprintf(path, MAX_PATH, "%s%s", g_dir, g_fcfg[font].bdf);
    f = fopen(path, "r");
    if (!f) { logf("font%d: bdf not found: %s", font, path); return; }
    if (!g_glyph[font]) g_glyph[font] = (Glyph *)calloc(MAXCODE, sizeof(Glyph));
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "FONTBOUNDINGBOX ", 16)) {
            sscanf(line + 16, "%d %d %d %d", &fbw, &fbh, &fbx, &fby);
            size = (fbw > fbh ? fbw : fbh) * sc;
            if (size > g_gdim) size = g_gdim;
            g_fcfg[font].size = size;
        } else if (!strncmp(line, "ENCODING ", 9)) {
            enc = atoi(line + 9);
        } else if (!strncmp(line, "BBX ", 4)) {
            sscanf(line + 4, "%d %d %d %d", &bw, &bh, &bx, &by);
        } else if (!strncmp(line, "BITMAP", 6)) {
            wchar_t wc = jis_to_wc(enc);
            unsigned short c = wc ? g_wc2code[wc] : 0;
            bits = NULL;
            row = 0;
            if (c && size && !g_glyph[font][c - 1].bits) {
                bits = (unsigned char *)calloc(size * size, 1);
                g_glyph[font][c - 1].bits = bits;
                n++;
            }
        } else if (!strncmp(line, "ENDCHAR", 7)) {
            row = -1;
            bits = NULL;
        } else if (row >= 0 && bits) {
            /* 1 行分の 16 進。セル内の位置はベースラインから求める */
            unsigned long v = strtoul(line, NULL, 16);
            int nbits = ((bw + 7) / 8) * 8, x;
            int y = (fbh + fby) - (bh + by) + row;
            for (x = 0; x < bw; x++) {
                int px = bx - fbx + x, i, j;
                if (!((v >> (nbits - 1 - x)) & 1)) continue;
                for (j = 0; j < sc; j++)
                    for (i = 0; i < sc; i++) {
                        int Y = y * sc + j, X = px * sc + i;
                        if (Y >= 0 && Y < size && X >= 0 && X < size) bits[Y * size + X] = 1;
                    }
            }
            row++;
        }
    }
    fclose(f);
    logf("font%d: %s x%d, %dpx, %d glyphs", font, g_fcfg[font].bdf, sc, size, n);
}

/* ------------------------------------------------------------------ */
/* フック本体                                                            */
/* ------------------------------------------------------------------ */
typedef void (__cdecl *draw_char_t)(int ch, int font);
typedef void (__cdecl *msg_fetch_t)(int id, char *dst);
static draw_char_t orig_draw_char;
static msg_fetch_t orig_msg_fetch;
static int g_pending = -1;

static unsigned char *font_hdr(int font)
{
    return *P(unsigned char **, A_FONTTAB + font * 24 + 0x0c);
}

static int is_text_font(int font)
{
    unsigned char *h;
    if (font < 0 || font >= NFONT) return 0;
    h = font_hdr(font);
    return h && h[2] == 0;   /* 0 = 1bpp 文字フォント, 1 = カラーアイコン */
}

static void draw_glyph(int code, int font)
{
    unsigned char *hdr = font_hdr(font);
    unsigned char *fb = *P(unsigned char **, A_FRAMEBUF);
    const FontCfg *fc = &g_fcfg[font];
    int x = *P(int *, A_CUR_X), y = *P(int *, A_CUR_Y);
    int fg = *P(short *, A_COL_FG), bg = *P(short *, A_COL_BG);
    int opaque = *P(short *, A_OPAQUE);
    int cellw = fc->adv * 2, cellh = hdr[1] * 2;
    int size = fc->size, X0 = x * 2, Y0 = y * 2, xx, yy;
    const unsigned char *bits;

    if (code < g_ncode && fb) {
        bits = get_glyph(font, code);
        for (yy = 0; yy < cellh; yy++) {
            int Y = Y0 + yy, gy = yy - fc->yoff;
            if (Y < 0 || Y >= FB_H) continue;
            for (xx = 0; xx < cellw; xx++) {
                int X = X0 + xx, on;
                if (X < 0 || X >= FB_W) continue;
                on = (gy >= 0 && gy < size && xx < size) ? bits[gy * size + xx] : 0;
                if (on) fb[Y * FB_W + X] = (unsigned char)fg;
                else if (opaque) fb[Y * FB_W + X] = (unsigned char)bg;
            }
        }
    }
    *P(int *, A_CUR_X) = x + fc->adv;
}

static void __cdecl hk_draw_char(int ch, int font)
{
    unsigned c = ch & 0xFF;
    short f = (short)font;
    if (IS_HI(c) && is_text_font(f)) {
        if (*P(short *, A_GAME_STATE) == 3) return;
        if (g_pending < 0) { g_pending = (int)c; return; }
        draw_glyph((g_pending - 0x80) * 127 + (c - 0x80), f);
        g_pending = -1;
        return;
    }
    g_pending = -1;
    orig_draw_char(ch, font);
}

static short __cdecl hk_text_width(const char *s, int font)
{
    short f = (short)font;
    unsigned char *hdr = font_hdr(f);
    int w = 0, text = is_text_font(f);
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        if (IS_HI(p[0])) {
            if (text && IS_HI(p[1])) { w += g_fcfg[f].adv; p += 2; continue; }
            p++;                /* 半端なバイトは幅 0 */
            continue;
        }
        w += hdr[0x11 + p[0] * 2] + (signed char)hdr[7];
        p++;
    }
    return (short)w;
}

/* 呼び出し箇所ごとの書き込み先バッファ容量 (tools/msgbuf_scan.py が生成)。
 * ゲームは英語原文に合わせた固定長のスタック上バッファに読み込むことがあるので、
 * 訳文がそれより長いと切り詰める (あふれるとスタックが壊れて落ちる)。 */
#include "msgbuf_sites.h"

static int site_cap(DWORD ret)
{
    int lo = 0, hi = sizeof MSGBUF_SITES / sizeof MSGBUF_SITES[0] - 1;
    ret -= g_delta;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (MSGBUF_SITES[mid].ret == ret) return MSGBUF_SITES[mid].cap;
        if (MSGBUF_SITES[mid].ret < ret) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
}

/* 全角の途中で切らずに、終端込み cap バイト以内でコピーする */
static void copy_capped(char *dst, const char *src, int cap)
{
    const unsigned char *s = (const unsigned char *)src;
    int n = 0;
    while (s[n]) {
        int w = (IS_HI(s[n]) && IS_HI(s[n + 1])) ? 2 : 1;
        if (n + w + 1 > cap) break;
        n += w;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}

static void __cdecl hk_msg_fetch(int id, char *dst)
{
    short sid = (short)id;
    if (sid >= 0 && g_msg[sid]) {
        int cap = site_cap((DWORD)_ReturnAddress());
        if (*P(short *, A_GAME_STATE) == 3) return;
        /* 旧来の本文表示 0x40f176 は、取得したメッセージを現在行 (128 バイトの行枠 0x4b5460) に
         * " " でつなぐ。行枠の残りに収まるよう切り詰める */
        if ((DWORD)_ReturnAddress() - g_delta == 0x40f192) {
            const char *slot = P(char *, 0x4b5460) + *P(short *, 0x4a0914) * 128;
            int room = 128 - (int)strlen(slot) - 1;
            if (room < 1) room = 1;
            if (!cap || room < cap) cap = room;
        }
        if (cap && (int)strlen(g_msg[sid]) + 1 > cap) {
            copy_capped(dst, g_msg[sid], cap);
            logf("msg %d truncated to %d bytes (site %08lx)", sid, cap - 1, (DWORD)_ReturnAddress());
        } else {
            strcpy(dst, g_msg[sid]);
        }
        *P(short *, A_MSG_STATUS) = 0;
        if (g_debug >= 2) logf("msg %d (ja) from %08lx", sid, (DWORD)_ReturnAddress() - g_delta);
        return;
    }
    if (g_debug >= 2) logf("msg %d (en)", sid);
    orig_msg_fetch(id, dst);
}

/* 表示上の文字数 (全角 2 バイトを 1 文字と数える)。
 * 固定幅フォント (6px) のレイアウトを strlen()*6 で計算している箇所用。 */
static size_t __cdecl hk_strlen_disp(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    size_t n = 0;
    while (*p) {
        if (IS_HI(p[0]) && IS_HI(p[1])) p += 2;
        else p++;
        n++;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* 本文の折り返し (0x40ef63 を置き換え)                                   */
/*  元の処理は空白で単語に区切り、幅 0xb0 (論理px) で折り返す。            */
/*  日本語は空白が無いので、全角 1 文字ごとを折り返し位置にする。          */
/*  行頭禁則文字 (。、」など) は直前の文字と、開き括弧は直後の文字とまとめる。*/
/*  メッセージ連結時に入る空白は、全角文字どうしの間では詰める。            */
/* ------------------------------------------------------------------ */
#define A_WRAP       0x40ef63  /* void wrap_text(char *s) */
#define A_PREFIX     0x40ebee  /* void parse_prefix(char *word, char *line): 語頭の制御記号を処理 */
#define A_SHOW_LINE  0x40e9c1  /* 現在行を表示 */
#define A_NEWLINE    0x4515f0  /* 次の行へ (スクロール) */
#define A_SLOTS      0x4b5460  /* char[4][128] テキストウィンドウの各行 */
#define A_SLOT_IDX   0x4a0914  /* short 現在行 */
#define A_SLOT_FLAG  0x4b4d98  /* short[4] */
#define WRAP_WIDTH   0xb0

static const unsigned char PROLOGUE_WRAP[] = {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x60, 0x01, 0x00, 0x00};

static int jp_code_at(const unsigned char *p)
{
    return (IS_HI(p[0]) && IS_HI(p[1])) ? (p[0] - 0x80) * 127 + (p[1] - 0x80) : -1;
}

static int no_line_start(const unsigned char *p)
{
    static const wchar_t KINSOKU[] =
        L"、。，．・：；？！ー…‥」』）］｝〕〉》】ぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮヵヶ々゛゜";
    int c = jp_code_at(p);
    if (c < 0 || c >= g_ncode) return 0;
    return wcschr(KINSOKU, g_code2wc[c]) != NULL;
}

static int no_line_end(const unsigned char *p)
{
    static const wchar_t OPEN[] = L"「『（［｛〔〈《【";
    int c = jp_code_at(p);
    if (c < 0 || c >= g_ncode) return 0;
    return wcschr(OPEN, g_code2wc[c]) != NULL;
}

static int ends_jp(const char *s)
{
    size_t n = strlen(s);
    return n >= 2 && jp_code_at((const unsigned char *)s + n - 2) >= 0;
}

/* 折り返しの単位の終わりを返す (end は語の終わり = 次の空白)。
 * 単位 = 半角の連続 + 全角 1 字 + 後続の行頭禁則文字。開き括弧は後に続く文字と同じ単位にする。
 * 全角の後、語末まで記号だけの半角 ($ ^ . ? など) が続くときはそれも含める
 * ($ ^ は行末に置く配置指定なので、単独で次の行に送られると困る)。 */
static const unsigned char *unit_end(const unsigned char *q, const unsigned char *end)
{
    for (;;) {
        const unsigned char *r;
        while (q < end && jp_code_at(q) < 0) q++;
        if (q >= end) return end;
        if (no_line_end(q) && q + 2 < end) { q += 2; continue; }
        q += 2;
        while (q < end && no_line_start(q)) q += 2;
        /* 直後の (英語) は会話キーワードの書き添えなので同じ単位にする */
        if (q < end && *q == '(') {
            for (r = q; r < end && *r != ')' && jp_code_at(r) < 0; r++) {}
            if (r < end && *r == ')') {
                q = r + 1;
                while (q < end && no_line_start(q)) q += 2;
            }
        }
        for (r = q; r < end && jp_code_at(r) < 0 && *r < 0x80 && !isalnum(*r); r++) {}
        return r == end ? end : q;
    }
}

static const unsigned char *word_end(const unsigned char *p)
{
    while (*p && *p != ' ') p++;
    return p;
}

static void __cdecl hk_wrap(char *s)
{
    typedef void (__cdecl *prefix_t)(char *, char *);
    typedef void (__cdecl *void_t)(void);
    prefix_t parse_prefix = P(prefix_t, A_PREFIX);
    void_t show_line = P(void_t, A_SHOW_LINE), next_line = P(void_t, A_NEWLINE);
    char *slots = P(char *, A_SLOTS);
    short *idx = P(short *, A_SLOT_IDX), *flag = P(short *, A_SLOT_FLAG);
    const unsigned char *p = (const unsigned char *)s;
    char line[256], word[256], tmp[512];
    int space_before = 0;

    line[0] = 0;
    while (*p) {
        const unsigned char *q;
        int n;
        if (*P(short *, A_GAME_STATE) == 3) return;
        if (*p == ' ') { space_before = 1; p++; continue; }
        q = unit_end(p, word_end(p));
        n = (int)(q - p);
        if (n > 200) n = 200;
        memcpy(word, p, n);
        word[n] = 0;
        p = q;

        parse_prefix(word, line);
        if (*P(short *, A_GAME_STATE) == 3) return;
        if (!word[0]) continue;

        strcpy(tmp, line);
        if (tmp[0] && space_before && !(ends_jp(tmp) && jp_code_at((unsigned char *)word) >= 0))
            strcat(tmp, " ");
        strcat(tmp, word);
        if (line[0] && hk_text_width(tmp, 2) > WRAP_WIDTH) {
            strcpy(slots + *idx * 128, line);
            flag[*idx] = 2;
            show_line();
            next_line();
            strcpy(line, word);
        } else {
            strcpy(line, tmp);
        }
        space_before = 0;
    }
    strcpy(slots + *idx * 128, line);
    flag[*idx] = 2;
}

/* ------------------------------------------------------------------ */
/* Gold 版の物語表示 (0x4782a6 → 0x47b169 → 0x47b210 → 0x47b7a6)          */
/*  ゲーム中の物語・会話の本文はこちらを通る (0x40ef63 は一部の旧画面のみ)。 */
/*  0x47b169: メッセージを空白で語に分け、語中の _ を空白にして 0x47b210 へ。 */
/*  0x47b210: 語頭の制御記号 (! % & ] @ # ~) を処理し、行バッファ 0x4b6130   */
/*            に空白でつなぐ。幅 0xb8 を超えたら 0x47b7a6 で 1 行描く。      */
/*  0x47b7a6: 行を中央寄せで描く。行末の $ は左寄せ、^ は前の行と同じ位置。  */
/*            4 行を超えるとクリック待ちで改ページ。                         */
/*  日本語は空白が無く 1 メッセージが 1 語になるので、0x47b169 を置き換えて  */
/*  hk_wrap と同じ単位で行に詰める。制御記号の処理と描画は元の関数を使う。  */
/* ------------------------------------------------------------------ */
#define A_GOLD_SPLIT 0x47b169  /* void split_words(char *msg) */
#define A_GOLD_WORD  0x47b210  /* void add_word(char *word) */
#define A_GOLD_FLUSH 0x47b7a6  /* void draw_line(char *line) */
#define A_GOLD_LINE  0x4b6130  /* char[] 組み立て中の行 */
#define GOLD_WIDTH   0xb8

static const unsigned char PROLOGUE_GSPLIT[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x53, 0x56, 0x57};

/* 語頭の制御記号の長さ (0x47b210 が処理する分。~ は次の 1 文字が引数) */
static int gold_ctrl_len(const unsigned char *w)
{
    int n = 0;
    while (w[n] && strchr("!%&]@#~", w[n])) {
        if (w[n] == '~' && w[n + 1]) n += 2;
        else n++;
    }
    return n;
}

static void __cdecl hk_gold_split(char *msg)
{
    typedef void (__cdecl *str_fn)(char *);
    str_fn add_word = P(str_fn, A_GOLD_WORD), draw_line = P(str_fn, A_GOLD_FLUSH);
    char *line = P(char *, A_GOLD_LINE);
    const unsigned char *p = (const unsigned char *)msg;
    char unit[256], tmp[512];
    int space_before = 1;   /* メッセージの境目は空白扱い (元の処理と同じ) */

    while (*p) {
        const unsigned char *w = p, *we = word_end(p);
        int c;
        if (*P(short *, A_GAME_STATE) == 3) return;
        if (*p == ' ') { space_before = 1; p++; continue; }
        c = gold_ctrl_len(w);
        if (c) {
            /* 制御記号だけを元の関数に渡す。最後に空の語を足すとき行末に空白が付くので取る */
            size_t before = strlen(line), n;
            memcpy(unit, w, c);
            unit[c] = 0;
            add_word(unit);
            if (*P(short *, A_GAME_STATE) == 3) return;
            n = strlen(line);
            if (n == before + 1 && line[n - 1] == ' ') line[n - 1] = 0;
            w += c;
        }
        while (w < we) {
            const unsigned char *ue = unit_end(w, we);
            int n = (int)(ue - w), i;
            if (n > 200) n = 200;
            memcpy(unit, w, n);
            unit[n] = 0;
            for (i = 0; i < n; i++) if (unit[i] == '_') unit[i] = ' ';
            strcpy(tmp, line);
            if (tmp[0] && space_before && !(ends_jp(tmp) && jp_code_at((unsigned char *)unit) >= 0))
                strcat(tmp, " ");
            strcat(tmp, unit);
            if (line[0] && hk_text_width(tmp, 2) > GOLD_WIDTH) {
                draw_line(line);          /* line を描いて空にする */
                if (*P(short *, A_GAME_STATE) == 3) return;
                strcpy(line, unit);
            } else {
                strcpy(line, tmp);
            }
            space_before = 0;
            w = ue;
        }
        p = we;
    }
}

/* ------------------------------------------------------------------ */
/* Windows 標準の画面 (起動時メニュー・設定ダイアログ・メニューバー)       */
/*  EXE のリソースにある英語を ui_ja.txt の表で置き換える。                */
/*  EXE の取り込み表 (IAT) の DialogBoxParamA などを差し替え、             */
/*  ダイアログ初期化の直後に子ウィンドウの文字列とフォントを入れ替える。    */
/* ------------------------------------------------------------------ */
typedef struct { char *en; wchar_t *ja; } UiStr;
static UiStr *g_ui;
static int g_nui;
static HFONT g_uifont;
static wchar_t g_uiface[64] = L"MS UI Gothic";
static int g_uisize = 12;

static void load_ui_table(void)
{
    char path[MAX_PATH], line[1024], ini[MAX_PATH], face[64];
    FILE *f;
    int cap = 0;
    _snprintf(ini, MAX_PATH, "%sjpmod.ini", g_dir);
    GetPrivateProfileStringA("ui", "face", "MS UI Gothic", face, sizeof face, ini);
    MultiByteToWideChar(CP_UTF8, 0, face, -1, g_uiface, 64);
    g_uisize = GetPrivateProfileIntA("ui", "size", 12, ini);
    _snprintf(path, MAX_PATH, "%sui_ja.txt", g_dir);
    f = fopen(path, "rb");
    if (!f) { logf("ui_ja.txt not found"); return; }
    while (fgets(line, sizeof line, f)) {
        char *t, *e = line + strlen(line), *p = line;
        int n;
        while (e > line && (e[-1] == '\n' || e[-1] == '\r')) *--e = 0;
        if ((unsigned char)p[0] == 0xEF) p += 3;
        if (!*p || *p == '#' || !(t = strchr(p, '\t'))) continue;
        *t++ = 0;
        if (g_nui == cap) {
            cap = cap ? cap * 2 : 64;
            g_ui = (UiStr *)realloc(g_ui, cap * sizeof(UiStr));
        }
        n = MultiByteToWideChar(CP_UTF8, 0, t, -1, NULL, 0);
        g_ui[g_nui].en = _strdup(p);
        g_ui[g_nui].ja = (wchar_t *)malloc(n * sizeof(wchar_t));
        MultiByteToWideChar(CP_UTF8, 0, t, -1, g_ui[g_nui].ja, n);
        g_nui++;
    }
    fclose(f);
    logf("ui strings: %d", g_nui);
}

/* 訳を探す。「原文\tショートカット」はタブより前で照合し、ショートカットは残す */
static const wchar_t *ui_lookup(const char *s, wchar_t *buf, int cap)
{
    const char *tab;
    size_t n;
    int i;
    if (!s || !*s) return NULL;
    tab = strchr(s, '\t');
    n = tab ? (size_t)(tab - s) : strlen(s);
    for (i = 0; i < g_nui; i++) {
        if (strlen(g_ui[i].en) == n && !strncmp(g_ui[i].en, s, n)) {
            if (!tab) return g_ui[i].ja;
            _snwprintf(buf, cap, L"%s\t%S", g_ui[i].ja, tab + 1);
            buf[cap - 1] = 0;
            return buf;
        }
    }
    return NULL;
}

static BOOL CALLBACK ui_child(HWND h, LPARAM lp)
{
    char text[512];
    wchar_t buf[512];
    const wchar_t *ja;
    (void)lp;
    if (GetWindowTextA(h, text, sizeof text) && (ja = ui_lookup(text, buf, 512)) != NULL)
        SetWindowTextW(h, ja);
    if (g_uifont) SendMessageA(h, WM_SETFONT, (WPARAM)g_uifont, TRUE);
    return TRUE;
}

static void ui_translate_window(HWND h)
{
    char text[512];
    wchar_t buf[512];
    const wchar_t *ja;
    if (!g_uifont)
        g_uifont = CreateFontW(-g_uisize, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                               DEFAULT_PITCH, g_uiface);
    if (GetWindowTextA(h, text, sizeof text) && (ja = ui_lookup(text, buf, 512)) != NULL)
        SetWindowTextW(h, ja);
    EnumChildWindows(h, ui_child, 0);
}

static void ui_translate_menu(HMENU m)
{
    int i, n = GetMenuItemCount(m);
    for (i = 0; i < n; i++) {
        char text[256];
        wchar_t buf[256];
        const wchar_t *ja;
        HMENU sub = GetSubMenu(m, i);
        if (GetMenuStringA(m, i, text, sizeof text, MF_BYPOSITION) &&
            (ja = ui_lookup(text, buf, 256)) != NULL) {
            MENUITEMINFOW mi;
            ZeroMemory(&mi, sizeof mi);
            mi.cbSize = sizeof mi;
            mi.fMask = MIIM_STRING;
            mi.dwTypeData = (wchar_t *)ja;
            SetMenuItemInfoW(m, i, TRUE, &mi);
        }
        if (sub) ui_translate_menu(sub);
    }
}

/* DialogBoxParamA: 元のダイアログ関数を包み、WM_INITDIALOG の後で訳す */
typedef INT_PTR (WINAPI *DialogBoxParamA_t)(HINSTANCE, LPCSTR, HWND, DLGPROC, LPARAM);
static DialogBoxParamA_t real_DialogBoxParamA;
static DLGPROC g_dlgstack[16];
static int g_ndlg;

static INT_PTR CALLBACK ui_dlgproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    DLGPROC orig = (DLGPROC)GetPropA(h, "jpmod.dlgproc");
    INT_PTR r;
    if (!orig && g_ndlg > 0) {
        orig = g_dlgstack[g_ndlg - 1];
        SetPropA(h, "jpmod.dlgproc", (HANDLE)orig);
    }
    r = orig ? orig(h, m, w, l) : FALSE;
    if (m == WM_INITDIALOG) ui_translate_window(h);
    else if (m == WM_NCDESTROY) RemovePropA(h, "jpmod.dlgproc");
    return r;
}

static INT_PTR WINAPI hk_DialogBoxParamA(HINSTANCE hi, LPCSTR tmpl, HWND parent, DLGPROC proc, LPARAM lp)
{
    INT_PTR r;
    if (!proc || g_ndlg >= 16) return real_DialogBoxParamA(hi, tmpl, parent, proc, lp);
    g_dlgstack[g_ndlg++] = proc;
    r = real_DialogBoxParamA(hi, tmpl, parent, ui_dlgproc, lp);
    g_ndlg--;
    return r;
}

typedef BOOL (WINAPI *SetWindowTextA_t)(HWND, LPCSTR);
typedef BOOL (WINAPI *SetDlgItemTextA_t)(HWND, int, LPCSTR);
typedef int (WINAPI *MessageBoxA_t)(HWND, LPCSTR, LPCSTR, UINT);
typedef HWND (WINAPI *CreateWindowExA_t)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
static SetWindowTextA_t real_SetWindowTextA;
static SetDlgItemTextA_t real_SetDlgItemTextA;
static MessageBoxA_t real_MessageBoxA;
static CreateWindowExA_t real_CreateWindowExA;

static BOOL WINAPI hk_SetWindowTextA(HWND h, LPCSTR s)
{
    wchar_t buf[512];
    const wchar_t *ja = ui_lookup(s, buf, 512);
    return ja ? SetWindowTextW(h, ja) : real_SetWindowTextA(h, s);
}

static BOOL WINAPI hk_SetDlgItemTextA(HWND h, int id, LPCSTR s)
{
    wchar_t buf[512];
    const wchar_t *ja = ui_lookup(s, buf, 512);
    return ja ? SetDlgItemTextW(h, id, ja) : real_SetDlgItemTextA(h, id, s);
}

static int WINAPI hk_MessageBoxA(HWND h, LPCSTR text, LPCSTR cap, UINT type)
{
    wchar_t b1[1024], b2[256];
    const wchar_t *t = ui_lookup(text, b1, 1024), *c = ui_lookup(cap, b2, 256);
    if (!t && !c) return real_MessageBoxA(h, text, cap, type);
    if (!t) { MultiByteToWideChar(CP_ACP, 0, text ? text : "", -1, b1, 1024); t = b1; }
    if (!c) { MultiByteToWideChar(CP_ACP, 0, cap ? cap : "", -1, b2, 256); c = b2; }
    return MessageBoxW(h, t, c, type);
}

static HWND WINAPI hk_CreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR name, DWORD st, int x, int y, int w, int hh,
                                      HWND parent, HMENU menu, HINSTANCE hi, LPVOID lp)
{
    HWND h = real_CreateWindowExA(ex, cls, name, st, x, y, w, hh, parent, menu, hi, lp);
    if (h && !(st & WS_CHILD) && GetMenu(h)) {
        ui_translate_menu(GetMenu(h));
        DrawMenuBar(h);
    }
    return h;
}

/* EXE の取り込み表のうち dll!name の項目を to に差し替え、元の関数を返す */
static void *patch_iat(const char *dll, const char *name, void *to)
{
    unsigned char *base = (unsigned char *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY *dd = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    IMAGE_IMPORT_DESCRIPTOR *imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + dd->VirtualAddress);
    for (; imp->Name; imp++) {
        IMAGE_THUNK_DATA *names, *addrs;
        if (_stricmp((char *)base + imp->Name, dll)) continue;
        names = (IMAGE_THUNK_DATA *)(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        addrs = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; names++, addrs++) {
            IMAGE_IMPORT_BY_NAME *ibn;
            DWORD old;
            void *prev;
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            ibn = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
            if (strcmp((char *)ibn->Name, name)) continue;
            prev = (void *)addrs->u1.Function;
            VirtualProtect(&addrs->u1.Function, sizeof(DWORD), PAGE_READWRITE, &old);
            addrs->u1.Function = (DWORD)to;
            VirtualProtect(&addrs->u1.Function, sizeof(DWORD), old, &old);
            return prev;
        }
    }
    logf("iat: %s!%s not found", dll, name);
    return NULL;
}

static BOOL CALLBACK ui_existing_window(HWND h, LPARAM lp)
{
    DWORD pid;
    (void)lp;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId() && GetMenu(h)) {
        ui_translate_menu(GetMenu(h));
        DrawMenuBar(h);
    }
    return TRUE;
}

static void ui_install(void)
{
    load_ui_table();
    if (!g_nui) return;
    real_DialogBoxParamA = (DialogBoxParamA_t)patch_iat("USER32.dll", "DialogBoxParamA", hk_DialogBoxParamA);
    real_SetWindowTextA = (SetWindowTextA_t)patch_iat("USER32.dll", "SetWindowTextA", hk_SetWindowTextA);
    real_SetDlgItemTextA = (SetDlgItemTextA_t)patch_iat("USER32.dll", "SetDlgItemTextA", hk_SetDlgItemTextA);
    real_MessageBoxA = (MessageBoxA_t)patch_iat("USER32.dll", "MessageBoxA", hk_MessageBoxA);
    real_CreateWindowExA = (CreateWindowExA_t)patch_iat("USER32.dll", "CreateWindowExA", hk_CreateWindowExA);
    if (!real_DialogBoxParamA) real_DialogBoxParamA = DialogBoxParamA;
    if (!real_SetWindowTextA) real_SetWindowTextA = SetWindowTextA;
    if (!real_SetDlgItemTextA) real_SetDlgItemTextA = SetDlgItemTextA;
    if (!real_MessageBoxA) real_MessageBoxA = MessageBoxA;
    if (!real_CreateWindowExA) real_CreateWindowExA = CreateWindowExA;
    /* 拡大表示: 描画とマウスカーソル移動 */
    real_StretchDIBits = (StretchDIBits_t)patch_iat("GDI32.dll", "StretchDIBits", hk_StretchDIBits);
    real_PatBlt = (PatBlt_t)patch_iat("GDI32.dll", "PatBlt", hk_PatBlt);
    real_SetCursorPos = (SetCursorPos_t)patch_iat("USER32.dll", "SetCursorPos", hk_SetCursorPos);
    if (!real_StretchDIBits) real_StretchDIBits = StretchDIBits;
    if (!real_PatBlt) real_PatBlt = PatBlt;
    if (!real_SetCursorPos) real_SetCursorPos = SetCursorPos;
    real_MoveWindow = (MoveWindow_t)patch_iat("USER32.dll", "MoveWindow", hk_MoveWindow);
    real_SetWindowPos = (SetWindowPos_t)patch_iat("USER32.dll", "SetWindowPos", hk_SetWindowPos);
    if (!real_MoveWindow) real_MoveWindow = MoveWindow;
    if (!real_SetWindowPos) real_SetWindowPos = SetWindowPos;
    real_mciSendCommandA = (mciSendCommandA_t)patch_iat("WINMM.dll", "mciSendCommandA", hk_mciSendCommandA);
    real_mciSendStringA = (mciSendStringA_t)patch_iat("WINMM.dll", "mciSendStringA", hk_mciSendStringA);
    if (!real_mciSendCommandA || !real_mciSendStringA) {
        HMODULE wm = GetModuleHandleA("winmm.dll");
        if (!real_mciSendCommandA) real_mciSendCommandA = (mciSendCommandA_t)GetProcAddress(wm, "mciSendCommandA");
        if (!real_mciSendStringA) real_mciSendStringA = (mciSendStringA_t)GetProcAddress(wm, "mciSendStringA");
    }
    /* WinG より先にメインウィンドウが作られている場合に備え、既存のウィンドウのメニューも訳す */
    EnumWindows(ui_existing_window, 0);
}

/* ------------------------------------------------------------------ */
/* SCENARIO.GLD の名前 (品物・怪物・人物)                               */
/*  名前は 16 バイトの欄に入っている。jpmod/scen_ja.txt の               */
/*  「ファイル位置(16進)<TAB>訳」を、ファイルを読んだバッファのその位置に  */
/*  書き込む (ReadFile / _lread / _hread を横取り)。元のファイルは変えない。*/
/* ------------------------------------------------------------------ */
typedef struct { DWORD off; char name[16]; } ScenPatch;
static ScenPatch *g_scen;
static int g_nscen;
static HANDLE g_scen_h[8];          /* SCENARIO.GLD を開いたハンドル */

static int scen_cmp(const void *a, const void *b)
{
    DWORD x = ((const ScenPatch *)a)->off, y = ((const ScenPatch *)b)->off;
    return x < y ? -1 : x > y;
}

static void load_scen_names(void)
{
    char path[MAX_PATH], line[512];
    FILE *f;
    int cap = 0;
    _snprintf(path, MAX_PATH, "%sscen_ja.txt", g_dir);
    f = fopen(path, "rb");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        char *t, *e = line + strlen(line), enc[64];
        int n;
        while (e > line && (e[-1] == '\n' || e[-1] == '\r')) *--e = 0;
        if (!line[0] || line[0] == '#' || !(t = strchr(line, '\t'))) continue;
        *t++ = 0;
        n = encode_line(t, enc, sizeof enc);
        if (n <= 0 || n >= 16) continue;      /* 欄は 16 バイト (NUL 込み) */
        if (g_nscen == cap) {
            cap = cap ? cap * 2 : 512;
            g_scen = (ScenPatch *)realloc(g_scen, cap * sizeof(ScenPatch));
        }
        g_scen[g_nscen].off = strtoul(line, NULL, 16);
        memcpy(g_scen[g_nscen].name, enc, n);
        g_scen[g_nscen].name[n] = 0;
        g_nscen++;
    }
    fclose(f);
    qsort(g_scen, g_nscen, sizeof(ScenPatch), scen_cmp);
    logf("scenario names: %d", g_nscen);
}

static int is_scen_handle(HANDLE h)
{
    int i;
    if (!h || h == INVALID_HANDLE_VALUE) return 0;
    for (i = 0; i < 8; i++) if (g_scen_h[i] == h) return 1;
    return 0;
}

static void remember_scen_handle(const char *name, HANDLE h)
{
    const char *b;
    int i;
    if (!name || !h || h == INVALID_HANDLE_VALUE || !g_nscen) return;
    b = strrchr(name, '\\');
    b = b ? b + 1 : name;
    if (_stricmp(b, "SCENARIO.GLD")) {
        for (i = 0; i < 8; i++) if (g_scen_h[i] == h) g_scen_h[i] = NULL;   /* 使い回されたハンドル */
        return;
    }
    for (i = 0; i < 8; i++) if (!g_scen_h[i] || g_scen_h[i] == h) { g_scen_h[i] = h; break; }
    if (g_debug >= 2) logf("SCENARIO.GLD opened (%p)", h);
}

/* ファイル位置 pos から n バイト読んだ buf に、名前の訳を書き込む */
static void scen_patch(DWORD pos, unsigned char *buf, DWORD n)
{
    int lo = 0, hi = g_nscen;
    if (!g_nscen || !n) return;
    while (lo < hi) {               /* off >= pos の最初 */
        int mid = (lo + hi) / 2;
        if (g_scen[mid].off < pos) lo = mid + 1; else hi = mid;
    }
    for (; lo < g_nscen && g_scen[lo].off < pos + n; lo++) {
        const char *s = g_scen[lo].name;
        DWORD k, at = g_scen[lo].off - pos, len = (DWORD)strlen(s) + 1;
        for (k = 0; k < len && at + k < n; k++) buf[at + k] = (unsigned char)s[k];
    }
}

typedef HANDLE (WINAPI *CreateFileA_t)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef BOOL (WINAPI *ReadFile_t)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef HFILE (WINAPI *lopen_t)(LPCSTR, int);
typedef HFILE (WINAPI *OpenFile_t)(LPCSTR, LPOFSTRUCT, UINT);
typedef UINT (WINAPI *lread_t)(HFILE, LPVOID, UINT);
typedef long (WINAPI *hread_t)(HFILE, LPVOID, long);
static CreateFileA_t real_CreateFileA;
static ReadFile_t real_ReadFile;
static lopen_t real_lopen;
static OpenFile_t real_OpenFile;
static lread_t real_lread;
static hread_t real_hread;

static HANDLE WINAPI hk_CreateFileA(LPCSTR name, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD fl, HANDLE tmpl)
{
    HANDLE h = real_CreateFileA(name, acc, share, sa, disp, fl, tmpl);
    remember_scen_handle(name, h);
    return h;
}

static HFILE WINAPI hk_lopen(LPCSTR name, int mode)
{
    HFILE h = real_lopen(name, mode);
    if (h != HFILE_ERROR) remember_scen_handle(name, (HANDLE)(DWORD_PTR)h);
    return h;
}

static HFILE WINAPI hk_OpenFile(LPCSTR name, LPOFSTRUCT of, UINT style)
{
    HFILE h = real_OpenFile(name, of, style);
    if (h != HFILE_ERROR) remember_scen_handle(name, (HANDLE)(DWORD_PTR)h);
    return h;
}

static BOOL WINAPI hk_ReadFile(HANDLE h, LPVOID buf, DWORD n, LPDWORD got, LPOVERLAPPED ov)
{
    DWORD pos = 0, done = 0;
    BOOL r;
    int scen = is_scen_handle(h) && !ov;
    if (scen) pos = SetFilePointer(h, 0, NULL, FILE_CURRENT);
    r = real_ReadFile(h, buf, n, got ? got : &done, ov);
    if (r && scen && pos != INVALID_SET_FILE_POINTER) scen_patch(pos, (unsigned char *)buf, got ? *got : done);
    return r;
}

static UINT WINAPI hk_lread(HFILE h, LPVOID buf, UINT n)
{
    LONG pos = is_scen_handle((HANDLE)(DWORD_PTR)h) ? _llseek(h, 0, FILE_CURRENT) : -1;
    UINT r = real_lread(h, buf, n);
    if (pos >= 0 && r != HFILE_ERROR) scen_patch((DWORD)pos, (unsigned char *)buf, r);
    return r;
}

static long WINAPI hk_hread(HFILE h, LPVOID buf, long n)
{
    LONG pos = is_scen_handle((HANDLE)(DWORD_PTR)h) ? _llseek(h, 0, FILE_CURRENT) : -1;
    long r = real_hread(h, buf, n);
    if (pos >= 0 && r > 0) scen_patch((DWORD)pos, (unsigned char *)buf, (DWORD)r);
    return r;
}

static void scen_install(void)
{
    if (!g_nscen) return;
    real_CreateFileA = (CreateFileA_t)patch_iat("KERNEL32.dll", "CreateFileA", hk_CreateFileA);
    real_ReadFile = (ReadFile_t)patch_iat("KERNEL32.dll", "ReadFile", hk_ReadFile);
    real_lopen = (lopen_t)patch_iat("KERNEL32.dll", "_lopen", hk_lopen);
    real_OpenFile = (OpenFile_t)patch_iat("KERNEL32.dll", "OpenFile", hk_OpenFile);
    real_lread = (lread_t)patch_iat("KERNEL32.dll", "_lread", hk_lread);
    real_hread = (hread_t)patch_iat("KERNEL32.dll", "_hread", hk_hread);
    if (!real_CreateFileA) real_CreateFileA = CreateFileA;
    if (!real_ReadFile) real_ReadFile = ReadFile;
    if (!real_lopen) real_lopen = _lopen;
    if (!real_OpenFile) real_OpenFile = OpenFile;
    if (!real_lread) real_lread = _lread;
    if (!real_hread) real_hread = _hread;
}

/* ------------------------------------------------------------------ */
/* パッチ                                                               */
/* ------------------------------------------------------------------ */
#include "strlen_sites.h"
#define A_STRLEN 0x494740

static int patch_call(DWORD site, DWORD expect_target, void *to)
{
    unsigned char *p = P(unsigned char *, site);
    DWORD old;
    if (p[0] != 0xE8) return 0;
    if ((DWORD)(p + 5) + *(DWORD *)(p + 1) != expect_target + g_delta) return 0;
    VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
    *(DWORD *)(p + 1) = (DWORD)to - (DWORD)(p + 5);
    VirtualProtect(p, 5, old, &old);
    return 1;
}
static const unsigned char PROLOGUE_6C[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x6C};
static const unsigned char PROLOGUE_14[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14};
static const unsigned char PROLOGUE_18[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x18};

static void *make_trampoline(unsigned char *target, int n)
{
    unsigned char *t = (unsigned char *)VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    memcpy(t, target, n);
    t[n] = 0xE9;
    *(DWORD *)(t + n + 1) = (DWORD)(target + n) - (DWORD)(t + n + 5);
    return t;
}

static void write_jmp(unsigned char *at, void *to)
{
    DWORD old;
    VirtualProtect(at, 5, PAGE_EXECUTE_READWRITE, &old);
    at[0] = 0xE9;
    *(DWORD *)(at + 1) = (DWORD)to - (DWORD)(at + 5);
    VirtualProtect(at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, 5);
}

static BOOL install_hooks(void)
{
    unsigned char *dc = P(unsigned char *, A_DRAW_CHAR);
    unsigned char *tw = P(unsigned char *, A_TEXT_WIDTH);
    unsigned char *mf = P(unsigned char *, A_MSG_FETCH);
    if (memcmp(dc, PROLOGUE_6C, 6) || memcmp(tw, PROLOGUE_14, 6) || memcmp(mf, PROLOGUE_18, 6)) {
        logf("unexpected WIZARD.EXE version; hooks not installed");
        MessageBoxA(NULL, "対応していない WIZARD.EXE です (Gold 1.1b 用)。\n日本語化は無効になります。",
                    "Wizardry 7 日本語化MOD", MB_ICONWARNING);
        return FALSE;
    }
    orig_draw_char = (draw_char_t)make_trampoline(dc, 6);
    orig_msg_fetch = (msg_fetch_t)make_trampoline(mf, 6);
    write_jmp(dc, hk_draw_char);
    write_jmp(tw, hk_text_width);
    write_jmp(mf, hk_msg_fetch);
    if (!memcmp(P(unsigned char *, A_WRAP), PROLOGUE_WRAP, sizeof PROLOGUE_WRAP))
        write_jmp(P(unsigned char *, A_WRAP), hk_wrap);
    else
        logf("wrap_text: unexpected code, not hooked");
    if (!memcmp(P(unsigned char *, A_GOLD_SPLIT), PROLOGUE_GSPLIT, sizeof PROLOGUE_GSPLIT))
        write_jmp(P(unsigned char *, A_GOLD_SPLIT), hk_gold_split);
    else
        logf("gold split_words: unexpected code, not hooked");
    {
        int i, ok = 0, n = sizeof STRLEN_SITES / sizeof STRLEN_SITES[0];
        for (i = 0; i < n; i++) {
            if (patch_call(STRLEN_SITES[i], A_STRLEN, hk_strlen_disp)) ok++;
            else logf("strlen site %08lx: unexpected code, skipped", STRLEN_SITES[i]);
        }
        FlushInstructionCache(GetCurrentProcess(), NULL, 0);
        logf("strlen sites patched: %d/%d", ok, n);
    }
    ui_install();
    scen_install();
    logf("hooks installed (delta=%08lx)", g_delta);
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* デバッグ: フレームバッファを BMP 保存 (F12 または jpmod\shot.req)     */
/* ------------------------------------------------------------------ */
static void save_shot(void)
{
    static int seq;
    char path[MAX_PATH];
    unsigned char *fb = *P(unsigned char **, A_FRAMEBUF);
    BITMAPFILEHEADER fh;
    BITMAPINFOHEADER ih;
    FILE *f;
    int y;
    if (!fb) return;
    if (g_wingdc) GetDIBColorTable(g_wingdc, 0, 256, g_pal);
    /* 起動し直しても以前のスクリーンショットを上書きしないよう、空き番号を探す */
    do {
        _snprintf(path, MAX_PATH, "%sshot_%03d.bmp", g_dir, seq++);
    } while (seq < 1000 && GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES);
    f = fopen(path, "wb");
    if (!f) return;
    ZeroMemory(&fh, sizeof fh);
    ZeroMemory(&ih, sizeof ih);
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof fh + sizeof ih + sizeof g_pal;
    fh.bfSize = fh.bfOffBits + FB_W * FB_H;
    ih.biSize = sizeof ih;
    ih.biWidth = FB_W;
    ih.biHeight = FB_H;
    ih.biPlanes = 1;
    ih.biBitCount = 8;
    ih.biClrUsed = 256;
    fwrite(&fh, sizeof fh, 1, f);
    fwrite(&ih, sizeof ih, 1, f);
    fwrite(g_pal, sizeof g_pal, 1, f);
    for (y = FB_H - 1; y >= 0; y--) fwrite(fb + y * FB_W, FB_W, 1, f);
    fclose(f);
    logf("saved %s", path);
}

static DWORD WINAPI debug_thread(LPVOID arg)
{
    char req[MAX_PATH];
    (void)arg;
    _snprintf(req, MAX_PATH, "%sshot.req", g_dir);
    for (;;) {
        Sleep(150);
        if ((GetAsyncKeyState(VK_F12) & 1) || GetFileAttributesA(req) != INVALID_FILE_ATTRIBUTES) {
            DeleteFileA(req);
            save_shot();
        }
    }
}

/* ------------------------------------------------------------------ */
/* デバッグ: 0x476842 (キャラクター作成の呼び出し元) の戻りアドレス欄への書き込みを
 * ハードウェアブレークポイントで捕まえる */
#define DBG_ARM_CODE 0xE0004A50
static DWORD g_watch;
static DWORD g_cont_create = 0x4758a4;
static int g_hits;
static void arm_watch(void) { RaiseException(DBG_ARM_CODE, 0, 0, NULL); }
/* 0x47589e (キャラクター作成) の入口: 元のプロローグ実行後に EBP を記録して監視開始 */
__declspec(naked) static void stub_create(void)
{
    __asm {
        push ebp
        mov ebp, esp
        sub esp, 0x40
        mov g_watch, ebp
        pushad
        call arm_watch
        popad
        jmp [g_cont_create]
    }
}

/* デバッグ: クラッシュ時にレジスタとスタックをログへ */
static LONG CALLBACK crash_logger(PEXCEPTION_POINTERS ep)
{
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    CONTEXT *c = ep->ContextRecord;
    const unsigned char *s;
    int i, j;
    static int done;
    if (code == DBG_ARM_CODE) {
        c->Dr0 = g_watch;          /* 保存された EBP */
        c->Dr1 = g_watch + 8;      /* 第 1 引数 (名前ポインタ) */
        c->Dr7 = 0x00DD0005;       /* DR0/DR1: write, 4 bytes */
        g_hits = 0;
        logf("watch armed at %08lx: saved ebp=%08lx ret=%08lx name=%08lx", g_watch,
             ((DWORD *)g_watch)[0], ((DWORD *)g_watch)[1], ((DWORD *)g_watch)[2]);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (code == EXCEPTION_SINGLE_STEP && (c->Dr6 & 3)) {
        DWORD bp = c->Ebp;
        logf("WATCH HIT eip=%08lx esp=%08lx ebp=%08lx new=%08lx eax=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx",
             c->Eip, c->Esp, c->Ebp, *(DWORD *)g_watch, c->Eax, c->Ecx, c->Edx, c->Esi, c->Edi);
        for (i = 0; i < 12 && bp && !IsBadReadPtr((void *)bp, 8); i++) {
            logf("  frame ebp=%08lx ret=%08lx", bp, ((DWORD *)bp)[1]);
            bp = ((DWORD *)bp)[0];
        }
        logf("  now saved ebp=%08lx name=%08lx", ((DWORD *)g_watch)[0], ((DWORD *)g_watch)[2]);
        c->Dr6 = 0;
        if (++g_hits >= 6) c->Dr7 = 0;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (done || (code != EXCEPTION_ACCESS_VIOLATION && code != 0xC0000409 && code != EXCEPTION_STACK_OVERFLOW))
        return EXCEPTION_CONTINUE_SEARCH;
    done = 1;
    logf("CRASH code=%08lx eip=%08lx esp=%08lx ebp=%08lx eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx",
         code, c->Eip, c->Esp, c->Ebp, c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi);
    s = (const unsigned char *)(c->Esp - 0x180);
    for (i = 0; i < 0x200; i += 16) {
        char hex[64], asc[20];
        if (IsBadReadPtr(s + i, 16)) break;
        for (j = 0; j < 16; j++) {
            sprintf(hex + j * 3, "%02x ", s[i + j]);
            asc[j] = (s[i + j] >= 32 && s[i + j] < 127) ? s[i + j] : '.';
        }
        asc[16] = 0;
        logf("  %08lx: %s %s", (DWORD)(s + i), hex, asc);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        char *p;
        DisableThreadLibraryCalls(inst);
        GetModuleFileNameA(inst, g_dir, MAX_PATH);
        p = strrchr(g_dir, '\\');
        if (p) p[1] = 0;
        strcat(g_dir, "jpmod\\");
        load_config();
        if (g_debug) {
            char lp[MAX_PATH];
            _snprintf(lp, MAX_PATH, "%sjpmod.log", g_dir);
            g_log = fopen(lp, "w");
        }
        g_delta = (DWORD)GetModuleHandleA(NULL) - EXE_BASE;
        g_wc2code = (unsigned short *)calloc(0x10000, sizeof(unsigned short));
        load_translations();
        load_scen_names();      /* 名前の訳もグリフ表に入れるため、BDF を読む前に */
        glyph_init();
        {
            int i;
            for (i = 0; i < NFONT; i++) load_bdf(i);
        }
        if (!g_nohooks) install_hooks(); else logf("nohooks: hooks disabled");
        if (g_debug) {
            AddVectoredExceptionHandler(1, crash_logger);
            {
                static const unsigned char pro[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x40};
                if (g_debug >= 3 && !memcmp(P(unsigned char *, 0x47589e), pro, 6))
                    write_jmp(P(unsigned char *, 0x47589e), stub_create);
            }
            CreateThread(NULL, 0, debug_thread, NULL, 0, NULL);
        }
    }
    return TRUE;
}
