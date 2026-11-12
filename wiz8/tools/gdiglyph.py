"""Render glyphs with Windows GDI (uses embedded bitmap strikes of fonts like MS Gothic,
which look much better than outline rendering at 10-16px)."""
import ctypes
from ctypes import wintypes

gdi32 = ctypes.WinDLL('gdi32')
user32 = ctypes.WinDLL('user32')

class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [('biSize', wintypes.DWORD), ('biWidth', wintypes.LONG), ('biHeight', wintypes.LONG),
                ('biPlanes', wintypes.WORD), ('biBitCount', wintypes.WORD), ('biCompression', wintypes.DWORD),
                ('biSizeImage', wintypes.DWORD), ('biXPelsPerMeter', wintypes.LONG), ('biYPelsPerMeter', wintypes.LONG),
                ('biClrUsed', wintypes.DWORD), ('biClrImportant', wintypes.DWORD)]

class TEXTMETRICW(ctypes.Structure):
    _fields_ = [('tmHeight', wintypes.LONG), ('tmAscent', wintypes.LONG), ('tmDescent', wintypes.LONG),
                ('tmInternalLeading', wintypes.LONG), ('tmExternalLeading', wintypes.LONG),
                ('tmAveCharWidth', wintypes.LONG), ('tmMaxCharWidth', wintypes.LONG), ('tmWeight', wintypes.LONG),
                ('tmOverhang', wintypes.LONG), ('tmDigitizedAspectX', wintypes.LONG), ('tmDigitizedAspectY', wintypes.LONG),
                ('tmFirstChar', wintypes.WCHAR), ('tmLastChar', wintypes.WCHAR), ('tmDefaultChar', wintypes.WCHAR),
                ('tmBreakChar', wintypes.WCHAR), ('tmItalic', ctypes.c_byte), ('tmUnderlined', ctypes.c_byte),
                ('tmStruckOut', ctypes.c_byte), ('tmPitchAndFamily', ctypes.c_byte), ('tmCharSet', ctypes.c_byte)]

class SIZE(ctypes.Structure):
    _fields_ = [('cx', wintypes.LONG), ('cy', wintypes.LONG)]

gdi32.CreateFontW.restype = wintypes.HFONT
gdi32.CreateFontW.argtypes = [ctypes.c_int] * 5 + [wintypes.DWORD] * 8 + [wintypes.LPCWSTR]
gdi32.CreateCompatibleDC.restype = wintypes.HDC
gdi32.CreateCompatibleDC.argtypes = [wintypes.HDC]
gdi32.CreateDIBSection.restype = wintypes.HBITMAP
gdi32.CreateDIBSection.argtypes = [wintypes.HDC, ctypes.c_void_p, wintypes.UINT, ctypes.POINTER(ctypes.c_void_p), wintypes.HANDLE, wintypes.DWORD]
gdi32.SelectObject.restype = wintypes.HGDIOBJ
gdi32.SelectObject.argtypes = [wintypes.HDC, wintypes.HGDIOBJ]
gdi32.TextOutW.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int, wintypes.LPCWSTR, ctypes.c_int]
gdi32.SetTextColor.argtypes = [wintypes.HDC, wintypes.DWORD]
gdi32.SetBkColor.argtypes = [wintypes.HDC, wintypes.DWORD]
gdi32.PatBlt.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.DWORD]
gdi32.GetTextMetricsW.argtypes = [wintypes.HDC, ctypes.POINTER(TEXTMETRICW)]
gdi32.GetTextExtentPoint32W.argtypes = [wintypes.HDC, wintypes.LPCWSTR, ctypes.c_int, ctypes.POINTER(SIZE)]
gdi32.DeleteObject.argtypes = [wintypes.HGDIOBJ]
gdi32.DeleteDC.argtypes = [wintypes.HDC]

NONANTIALIASED_QUALITY = 3
SHIFTJIS_CHARSET = 128


class GdiFont:
    def __init__(self, face='ＭＳ ゴシック', px=12, bold=False):
        self.W = self.H = px * 3
        self.dc = gdi32.CreateCompatibleDC(None)
        bmi = BITMAPINFOHEADER(ctypes.sizeof(BITMAPINFOHEADER), self.W, -self.H, 1, 32, 0, 0, 0, 0, 0, 0)
        self.bits = ctypes.c_void_p()
        self.bmp = gdi32.CreateDIBSection(self.dc, ctypes.byref(bmi), 0, ctypes.byref(self.bits), None, 0)
        gdi32.SelectObject(self.dc, self.bmp)
        self.font = gdi32.CreateFontW(-px, 0, 0, 0, 700 if bold else 400, 0, 0, 0, SHIFTJIS_CHARSET, 0, 0,
                                      NONANTIALIASED_QUALITY, 0, face)
        gdi32.SelectObject(self.dc, self.font)
        gdi32.SetTextColor(self.dc, 0xFFFFFF)
        gdi32.SetBkColor(self.dc, 0)
        tm = TEXTMETRICW()
        gdi32.GetTextMetricsW(self.dc, ctypes.byref(tm))
        self.ascent, self.descent, self.height = tm.tmAscent, tm.tmDescent, tm.tmHeight

    def render(self, ch):
        """returns (advance, mask rows list[list[int]] of size height x advance)"""
        gdi32.PatBlt(self.dc, 0, 0, self.W, self.H, 0x00000042)  # BLACKNESS
        gdi32.TextOutW(self.dc, 0, 0, ch, 1)
        sz = SIZE()
        gdi32.GetTextExtentPoint32W(self.dc, ch, 1, ctypes.byref(sz))
        buf = ctypes.string_at(self.bits, self.W * self.H * 4)
        w, h = min(sz.cx, self.W), min(self.height, self.H)
        rows = [[1 if buf[(y * self.W + x) * 4] > 127 else 0 for x in range(w)] for y in range(h)]
        return w, rows

    def close(self):
        gdi32.DeleteObject(self.font)
        gdi32.DeleteObject(self.bmp)
        gdi32.DeleteDC(self.dc)


if __name__ == '__main__':
    import sys
    px = int(sys.argv[1]) if len(sys.argv) > 1 else 12
    f = GdiFont(px=px)
    print('ascent', f.ascent, 'descent', f.descent, 'height', f.height)
    for ch in (sys.argv[2] if len(sys.argv) > 2 else '終了ア'):
        w, rows = f.render(ch)
        print(ch, w)
        for r in rows:
            print(''.join('#' if v else '.' for v in r))
