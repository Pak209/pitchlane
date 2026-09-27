#!/usr/bin/env python3
"""Render a piano roll of analyzed notes over the continuous pitch trace (developer tool).

usage: plot_analysis.py <prefix> <out.png> [--wav in.wav] [--from S --to S] [--segments A-B,C-D] [--title T]
Reads <prefix>.notes.csv and <prefix>.frames.csv written by pitchlane_analyze. With --wav, a
log-frequency spectrogram is drawn underneath so the trace can be judged by eye.
"""
import argparse, csv
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle

NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]

def load(prefix):
    notes = [dict(start=float(r["start"]), length=float(r["length"]), pitch=int(r["pitch"]),
                  conf=float(r["confidence"]), flags=int(r["flags"] or 0))
             for r in csv.DictReader(open(prefix + ".notes.csv"))]
    fr = np.array([[float(r["time"]), float(r["midi"]), float(r["voicedProb"]), float(r["rmsDb"])]
                   for r in csv.DictReader(open(prefix + ".frames.csv"))])
    return notes, fr

def draw(ax, notes, fr, t0, t1, wav, title, legend=True):
    sel = (fr[:, 0] >= t0) & (fr[:, 0] <= t1)
    vis = [n for n in notes if n["start"] + n["length"] >= t0 and n["start"] <= t1]
    pitches = [n["pitch"] for n in vis] + list(fr[sel & (fr[:, 1] > 0), 1])
    lo = int(np.floor(min(pitches))) - 3 if pitches else 48
    hi = int(np.ceil(max(pitches))) + 3 if pitches else 84
    ax.set_facecolor("#141a2c")
    if wav is not None:
        sr, x = wav
        seg = x[int(t0 * sr):int(t1 * sr)]
        from scipy.signal import stft
        f, t, Z = stft(seg, sr, nperseg=4096, noverlap=4096 - 512)
        m = f > 30
        mid = 69 + 12 * np.log2(f[m] / 440.0)
        S = 20 * np.log10(np.abs(Z[m]) + 1e-9)
        S = np.clip(S - S.max(), -70, 0)
        ax.pcolormesh(t + t0, mid, S, shading="auto", cmap="magma", alpha=0.55, rasterized=True)
    for p in range(lo, hi + 1):
        ax.axhline(p - 0.5, color="#ffffff", lw=0.3 if p % 12 else 0.8, alpha=0.12 if p % 12 else 0.3)
    for n in vis:
        muted = n["flags"] & 1; suspect = n["flags"] & 2
        face = "#8a8fa3" if muted else "#9d8cff"
        edge = "#f5a524" if suspect else "white"
        ax.add_patch(Rectangle((n["start"], n["pitch"] - 0.4), n["length"], 0.8, facecolor=face,
                               edgecolor=edge, lw=1.3 if suspect else 0.4, alpha=0.85))
    tr = fr[sel].copy()
    tr[tr[:, 1] <= 0, 1] = np.nan
    # Break the line at big jumps so voice flips show as gaps, not vertical bars.
    jump = np.abs(np.diff(tr[:, 1])) > 5
    y = tr[:, 1].copy(); y[1:][jump] = np.nan
    ax.plot(tr[:, 0], y, color="#5ee6f2", lw=0.9)
    ax.scatter(tr[1:, 0][jump], tr[1:, 1][jump], s=2, color="#5ee6f2")
    ax.set_xlim(t0, t1); ax.set_ylim(lo - 0.5, hi + 0.5)
    ax.set_yticks(range(lo, hi + 1))
    ax.set_yticklabels([f"{NAMES[p % 12]}{p // 12 - 1}" if NAMES[p % 12] in ("C", "E", "G", "A") else "" for p in range(lo, hi + 1)], fontsize=7)
    ax.set_xlabel("seconds"); ax.set_title(title, fontsize=10)
    if legend:
        ax.text(0.995, 0.01, "purple = note, grey = muted, orange outline = harmony suspect, cyan = f0 trace",
                transform=ax.transAxes, ha="right", va="bottom", color="white", fontsize=7)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("prefix"); ap.add_argument("out")
    ap.add_argument("--wav"); ap.add_argument("--from", dest="t0", type=float, default=None)
    ap.add_argument("--to", dest="t1", type=float, default=None); ap.add_argument("--title", default="")
    ap.add_argument("--segments", default=None, help="stack rows, e.g. 15-76,76-120,128-160")
    a = ap.parse_args()
    notes, fr = load(a.prefix)
    wav = None
    if a.wav:
        import scipy.io.wavfile as w
        sr, x = w.read(a.wav)
        x = x.astype(np.float64)
        if x.ndim > 1: x = x.mean(axis=1)
        wav = (sr, x)
    if a.segments:
        segs = [tuple(float(v) for v in s.split("-")) for s in a.segments.split(",")]
        fig, axes = plt.subplots(len(segs), 1, figsize=(24, 4.6 * len(segs)), dpi=100)
        axes = np.atleast_1d(axes)
        for i, (ax, (s0, s1)) in enumerate(zip(axes, segs)):
            draw(ax, notes, fr, s0, s1, wav, f"{a.title}  [{s0:g}-{s1:g} s]" if a.title else f"{s0:g}-{s1:g} s", legend=(i == 0))
    else:
        t0 = a.t0 if a.t0 is not None else 0.0
        t1 = a.t1 if a.t1 is not None else fr[-1, 0]
        width = max(12, min(40, (t1 - t0) * 0.9)) if a.t0 is None else 16
        fig, ax = plt.subplots(figsize=(width, 6.5), dpi=110)
        draw(ax, notes, fr, t0, t1, wav, a.title or a.prefix)
    fig.tight_layout(); fig.savefig(a.out); print("wrote", a.out)

if __name__ == "__main__":
    main()
