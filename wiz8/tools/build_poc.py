"""Proof of concept: build Patches/PATCH.002 with Japanese test strings + fonts,
and wiz8jp.chr (extra char table read by the hook DLL)."""
import os, sys, struct
from slf import read_slf, read_file, write_slf, BS
from strdat import decode, encode
from mkfont import add_japanese

GAME = r'L:\SteamLibrary\steamapps\common\Wizardry8'
DATA = os.path.join(GAME, 'Data', 'DATA.SLF')
PATCH1 = os.path.join(GAME, 'Patches', 'PATCH.001')
OUT_PATCH = os.path.join(GAME, 'Patches', 'PATCH.002')
OUT_CHR = os.path.join(GAME, 'wiz8jp.chr')

# characters already in the game's own table (ASCII + Latin-1 subset)
ORIG_TABLE = set(map(ord, 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%^&*()-_+=|\\{}[]:;"\'<>,.?/ ')) \
    | set(range(0xc0, 0x100)) | {0xbf, 0xa1, 0xfff0, 0xfff1, 0xfff2, 0xfff3, 0xfff4}

TRANSLATIONS = {
    16: '決定', 17: 'キャンセル', 18: '閉じる', 22: '終了',
    65: 'オプション', 1717: 'パーティをインポート', 1718: '新しいゲームのオプション',
    1738: 'キャンセル', 1773: '終了', 1776: 'セーブデータを読み込み中...',
    1913: 'ゲームを終了してメインメニューに戻りますか？',
    2098: 'ウィザードリィ8を終了しますか？', 2118: 'オプション', 2336: 'ロード', 2368: '終了',
}
PREFIX = '日本語'


def main():
    strings = decode(read_file(PATCH1, 'STRINGS' + BS + 'STRINGDATA.DAT'))
    for i, s in enumerate(strings):
        body = s.rstrip('\0')
        if i in TRANSLATIONS:
            strings[i] = TRANSLATIONS[i] + '\0'
        elif len(body) > 2 and body[0].isalpha():
            strings[i] = PREFIX + body + '\0'
    chars = sorted({c for s in strings for c in s if ord(c) not in ORIG_TABLE and ord(c) >= 0x80})
    print('extra chars:', len(chars), ''.join(chars).encode('utf-8').decode('utf-8', 'replace'))

    files = {'STRINGS' + BS + 'STRINGDATA.DAT': encode(strings)}

    h, ents = read_slf(DATA)
    f = open(DATA, 'rb')
    cache = {}
    for n, o, l, st in ents:
        up = n.upper()
        if up.endswith('.STI') and ('FONTS' + BS in up or up.endswith('_FONT.STI')) and 'PALETTE' not in up:
            f.seek(o)
            data, info = add_japanese(f.read(l), chars, cache)
            files[n] = data
            print('  font', n, info)
    write_slf(OUT_PATCH, 'PATCH.002', 'Data' + BS, files)
    open(OUT_CHR, 'wb').write(struct.pack('<%dH' % len(chars), *map(ord, chars)))
    print('wrote', OUT_PATCH, os.path.getsize(OUT_PATCH), 'bytes;', OUT_CHR)


if __name__ == '__main__':
    main()
