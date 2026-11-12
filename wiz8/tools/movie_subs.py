"""Build the movie subtitle file wiz8jp.sub (next to Wiz8.exe) from work/movies/cues.json (timing,
from movie_cues.py) and work/movies/ja.json (Japanese text; see its _note for null / "+" / overrides).
Format (UTF-8): "@<bink name, lower case, no extension>" starts a movie, then one cue per line:
"<start ms> <end ms> <text>" with "|" for a line break. Read by the hook (jphook.c, load_subs)."""
import os, json
from jptext import NO_START, NO_END

ROOT = os.path.join(os.path.dirname(__file__), '..')
GAME = os.path.abspath(os.path.join(ROOT, '..'))
KEY = {'TRang': "t'rang"}          # cue file name -> bink name (the apostrophe cannot be in a wav name)
MAXCH = 26                         # full-width characters per line at 800x600
TAIL, MINDUR = 0.3, 1.2


def wrap(s):
    out = []
    for part in s.split('\n'):
        while len(part) > MAXCH:
            n = len(part)
            lines = -(-n // MAXCH)
            target = -(-n // lines)
            best = None
            for i in range(max(1, n - MAXCH), min(n, MAXCH) + 1):
                a, b = part[i - 1], part[i] if i < n else ''
                if b in NO_START or a in NO_END:
                    continue
                score = abs(i - target) - (8 if a in '、。!?」' or b == '「' or a == ' ' else 0)
                if best is None or score < best[0]:
                    best = (score, i)
            i = best[1] if best else MAXCH
            out.append(part[:i].rstrip()); part = part[i:].lstrip()
        out.append(part)
    return out


def main():
    cues = json.load(open(os.path.join(ROOT, 'work', 'movies', 'cues.json'), encoding='utf-8'))
    ja = json.load(open(os.path.join(ROOT, 'work', 'movies', 'ja.json'), encoding='utf-8'))
    lines = ['# Wizardry 8 JP movie subtitles (built by tools/movie_subs.py)']
    total, missing = 0, []
    for movie, cs in cues.items():
        subs = []
        for c in cs:
            v = ja.get(c['id'], 'MISSING')
            if v == 'MISSING':
                missing.append(c['id']); continue
            if v is None:
                continue
            if v == '+':
                if subs: subs[-1][1] = c['end']
                continue
            if isinstance(v, dict):
                subs.append([v.get('start', c['start']), v.get('end', c['end']), v['ja']])
            else:
                subs.append([c['start'], c['end'], v])
        subs.sort()
        for i, s in enumerate(subs):
            nxt = subs[i + 1][0] - 0.05 if i + 1 < len(subs) else 1e9
            s[1] = min(max(s[1] + TAIL, s[0] + MINDUR), nxt)
        lines.append('@' + KEY.get(movie, movie.lower()))
        for a, b, t in subs:
            lines.append('%d %d %s' % (round(a * 1000), round(b * 1000), '|'.join(wrap(t))))
        total += len(subs)
    if missing:
        print('no translation for', missing)
    open(os.path.join(GAME, 'wiz8jp.sub'), 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')
    print('wrote wiz8jp.sub:', len(cues), 'movies,', total, 'cues')


if __name__ == '__main__':
    main()
