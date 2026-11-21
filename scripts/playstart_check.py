#!/usr/bin/env python3
"""playstart_check.py - the first seconds after Play in a nativehost recording (#37, E571-E573).

nativehost --notes plays a C4 every whole second; with --transport T the DAW starts playing at T s, and a DAW that
processes ahead at Play (--ahead MS@T) makes the first notes come late, go missing or crackle. This prints, for the
notes from T on, the onset relative to the steady delay of the later notes, and counts clicks (sample-to-sample jumps)
around Play. Standard library only. Usage: playstart_check.py WAV PLAY_S
"""
import argparse
import array
import statistics
import sys
import wave


def load(path):
    with wave.open(path) as w:
        rate, ch = w.getframerate(), w.getnchannels()
        a = array.array("h", w.readframes(w.getnframes()))
    return rate, [v / 32768 for v in a[::ch]]  # left channel


def onset(x, rate, a, b, thr):
    for i in range(int(a * rate), min(int(b * rate), len(x))):
        if abs(x[i]) > thr:
            return i / rate
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav")
    ap.add_argument("play", type=float)
    o = ap.parse_args()
    rate, x = load(o.wav)
    end = int(len(x) / rate) - 1
    thr = 0.1 * max(abs(v) for v in x)
    p = int(o.play)
    late = [(s, onset(x, rate, s, s + 0.9, thr)) for s in range(p + 4, end)]
    late = [t - s for s, t in late if t is not None]
    if not late:
        print("no notes after Play + 4 s")
        return 1
    delay = statistics.median(late)  # the steady delay of the notes from Play + 4 s on
    print(f"steady delay {delay * 1000:.1f} ms")
    for s in range(p, p + 4):
        t = onset(x, rate, s, s + 0.9, thr)
        print(f"note {s} s: " + ("MISSING" if t is None else f"{(t - s - delay) * 1000:+.1f} ms"))
    seg = x[int((o.play - 0.5) * rate):int((o.play + 1.5) * rate)]
    d = [abs(seg[i + 1] - seg[i]) for i in range(len(seg) - 1)]
    print(f"clicks around Play (jumps > 0.25): {sum(v > 0.25 for v in d)}, largest jump {max(d):.3f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
