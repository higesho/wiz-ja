"""Render strings with patched STI fonts the same way the engine indexes glyphs
(linear search in the 252-entry table + wiz8jp.chr), to check fonts offline."""
import struct, sys
from PIL import Image
from slf import read_file, BS
from sti import STI

GAME = r'L:\SteamLibrary\steamapps\common\Wizardry8'
ORIG = list(map(ord, 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%^&*()-_+=|\\{}[]:;"\'<>,.?/ '))


def table():
    extra = open(GAME + r'\wiz8jp.chr', 'rb').read()
    t = ORIG + [0] * (252 - len(ORIG))
    return t + list(struct.unpack('<%dH' % (len(extra) // 2), extra))


def render(font_entry, text, scale=2, bg=(30, 30, 40)):
    s = STI.load(read_file(GAME + r'\Patches\PATCH.002', font_entry))
    pal = s.palette
    tbl = table()
    glyphs = []
    for ch in text:
        try:
            idx = tbl.index(ord(ch))
        except ValueError:
            idx = 0
        glyphs.append(s.subs[idx])
    W = sum(g['w'] for g in glyphs) + 8
    H = max(g['h'] + max(0, g['offy']) for g in glyphs) + 8
    top = -min(0, min(g['offy'] for g in glyphs))
    img = Image.new('RGB', (W, H + top), bg)
    x = 4
    for g in glyphs:
        for yy in range(g['h']):
            for xx in range(g['w']):
                v = g['pixels'][yy * g['w'] + xx]
                if v:
                    img.putpixel((x + xx + g['offx'], 4 + top + yy + g['offy']), tuple(pal[v * 3:v * 3 + 3]))
        x += g['w']
    return img.resize((img.width * scale, img.height * scale), Image.NEAREST)


if __name__ == '__main__':
    out = sys.argv[1]
    lines = [
        ('FONTS' + BS + 'WIZ_TEXT_FONT.STI', 'Quit Wizardry 8? ウィザードリィ8を終了しますか？'),
        ('FONTS' + BS + 'DIALOG_FONT.STI', 'Options オプション 新しいゲームのオプション'),
        ('FONTS' + BS + 'FONT10ARIAL.STI', 'Cancel キャンセル 日本語Exit (ESC,TAB)'),
        ('FONTS' + BS + 'BUTTONFONT.STI', 'Load ロード 終了 閉じる 決定'),
        ('FONTS' + BS + 'OPT_TITLE_FONT.STI', 'Options オプション'),
        ('FONTS' + BS + 'WIZFONTA.STI', 'Small 日本語テスト'),
    ]
    imgs = [render(f, t) for f, t in lines]
    W = max(i.width for i in imgs)
    canvas = Image.new('RGB', (W, sum(i.height for i in imgs)), (0, 0, 0))
    y = 0
    for i in imgs:
        canvas.paste(i, (0, y)); y += i.height
    canvas.save(out)
    print('saved', out, canvas.size)
