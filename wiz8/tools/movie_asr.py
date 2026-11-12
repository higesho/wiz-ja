"""Transcribe the movie soundtracks (work/movies/*.wav, extracted with ffmpeg) with faster-whisper
large-v3 and write work/movies/<name>.en.json: [{"start", "end", "en", "words": [[start, end, word], ...]}, ...]."""
import os, sys, json, glob
from faster_whisper import WhisperModel

D = os.path.join(os.path.dirname(__file__), '..', 'work', 'movies')
PROMPT = ("Wizardry 8. Dominus, Cosmic Circle, Ascension Peak, Dark Savant, Destinae Dominus, "
          "Phoonzang, Marten, Arnika, Trynton, Umpani, T'Rang, Rapax, Mook, Higardi, Bela, Aletheides.")

def main():
    m = WhisperModel('large-v3', device='cuda', compute_type='float16')
    for w in sorted(glob.glob(os.path.join(D, '*.wav'))):
        segs, info = m.transcribe(w, language='en', initial_prompt=PROMPT, vad_filter=True,
                                  word_timestamps=True, beam_size=5)
        out = [{'start': round(s.start, 2), 'end': round(s.end, 2), 'en': s.text.strip(),
                'words': [[round(x.start, 2), round(x.end, 2), x.word] for x in s.words]} for s in segs]
        json.dump(out, open(w[:-4] + '.en.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
        print(os.path.basename(w), len(out), 'segments')

if __name__ == '__main__':
    main()
