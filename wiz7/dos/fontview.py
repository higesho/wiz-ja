"""DOS 版のフォント (VBFONT*.VGA / VCFONT*.VGA) を調べて PNG に並べる
ヘッダ 16 バイト: [0]=幅 [1]=高さ [2]=? [3]=1 行のバイト数 [4]=? [5]=字数 ...
続いて字数 x 2 バイトの表、その後に字形
"""
import sys
from pathlib import Path
from PIL import Image

D = Path(__file__).resolve().parents[2] / "DSAVANT"


def info(fn):
    d = (D / fn).read_bytes()
    h = d[:16]
    n = h[5]
    tab = [(d[16 + 2 * i], d[17 + 2 * i]) for i in range(n)]
    body = d[16 + 2 * n:]
    return d, h, n, tab, body


def render(fn, out):
    d, h, n, tab, body = info(fn)
    w, ht = h[0], h[1]
    per = len(body) // n
    print(fn, "hdr", h.hex(" "), "n", n, "per", per, "rem", len(body) - per * n, "tab", tab[:8], tab[-4:])
    cols = 16
    img = Image.new("RGB", (cols * (w + 2) * 3, ((n + cols - 1) // cols) * (ht + 2) * 3), (40, 40, 80))
    pal = [(0, 0, 0)] + [((i * 53) % 256, (i * 97) % 256, (i * 31 + 128) % 256) for i in range(1, 256)]
    for c in range(n):
        g = body[c * per:(c + 1) * per]
        ox, oy = (c % cols) * (w + 2) * 3, (c // cols) * (ht + 2) * 3
        for y in range(ht):
            for x in range(w):
                if per == w * ht:                 # 1 バイト 1 画素
                    v = g[y * w + x]
                else:
                    bpr = per // ht
                    if bpr * 8 >= w and per % ht == 0:
                        byte = g[y * bpr + x // 8]
                        v = 15 if byte & (0x80 >> (x % 8)) else 0
                    else:
                        v = 0
                if v:
                    col = (255, 255, 255) if v == 15 else pal[v]
                    for a in range(3):
                        for b in range(3):
                            img.putpixel((ox + x * 3 + a, oy + y * 3 + b), col)
    img.save(out)


if __name__ == "__main__":
    outdir = Path(sys.argv[1])
    for fn in ["VBFONT0.VGA", "VBFONT1.VGA", "VBFONT2.VGA", "VBFONT3.VGA", "VCFONT1.VGA", "VCFONT2.VGA", "VCFONT3.VGA"]:
        render(fn, outdir / (fn + ".png"))
