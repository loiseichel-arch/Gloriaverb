#!/usr/bin/env python3
"""Turn Loic's sweep recordings into the impulses built into Gloriaverb.

    python3 tools/make_impulses.py chamber.wav spring.wav Assets/Impulses

The recordings are the Gloriaverb sweep (96 kHz, 20 Hz -> 40 kHz, 15 s at
-12 dBFS after 1 s of silence) played through the machine. For each one:
deconvolution (regularised spectral division, smooth band edges), the delay
before the first sound is removed (0.5 ms kept, faded in), the end is cut
where the energy is 70 dB down (50 ms fade), and every channel is scaled to
the same energy, so all modes play at the same level.

Output: raw float32 little-endian, 96 kHz, channels one after the other
(<name>.f32), plus a line per impulse on stdout.
"""
import sys
import numpy as np
import scipy.io.wavfile as wf

FS = 96000
TARGET_DB = -2.5          # energy of each channel (sum of squares) in dB


def sweep():
    t = np.arange(int(15.0 * FS)) / FS
    L = 15.0 / np.log(40000.0 / 20.0)
    x = np.sin(2 * np.pi * 20.0 * L * (np.exp(t / L) - 1))
    fi, fo = int(0.05 * FS), int(0.02 * FS)
    x[:fi] *= 0.5 - 0.5 * np.cos(np.pi * np.arange(fi) / fi)
    x[-fo:] *= 0.5 + 0.5 * np.cos(np.pi * np.arange(fo) / fo)
    x *= 10 ** (-12 / 20)
    return np.concatenate([np.zeros(FS), x, np.zeros(15 * FS)])


def band_mask(f):
    m = np.ones_like(f)
    lo = (f < 30.0)
    m[lo] = 0.5 - 0.5 * np.cos(np.pi * np.clip((f[lo] - 15.0) / 15.0, 0, 1))
    hi = (f > 34000.0)
    m[hi] = 0.5 + 0.5 * np.cos(np.pi * np.clip((f[hi] - 34000.0) / 4000.0, 0, 1))
    return m


def deconvolve(path, mono):
    fs, y = wf.read(path)
    assert fs == FS, f"{path}: {fs} Hz, expected {FS}"
    y = y.astype(np.float64)           # the scale does not matter: every impulse is normalised
    if y.ndim == 1:
        y = y[:, None]
    if mono:
        y = y[:, :1]
    s = sweep()
    n = 1 << int(np.ceil(np.log2(len(y) + len(s))))
    S = np.fft.rfft(s, n)
    f = np.fft.rfftfreq(n, 1 / FS)
    H = np.conj(S) / (np.abs(S) ** 2 + 1e-3 * np.abs(S).max() ** 2) * band_mask(f)
    return np.array([np.fft.irfft(np.fft.rfft(y[:, c], n) * H, n) for c in range(y.shape[1])])


def clean(h):
    a = np.abs(h).max(0)
    search = a[:2 * FS]
    onset = int(np.argmax(search > search.max() * 0.01))
    start = max(0, onset - int(0.0005 * FS))
    h = h[:, start:start + 10 * FS].copy()
    fi = onset - start
    if fi > 0:
        h[:, :fi] *= 0.5 - 0.5 * np.cos(np.pi * np.arange(fi) / fi)
    e = (h ** 2).sum(0)
    edc = np.cumsum(e[::-1])[::-1]
    edc = 10 * np.log10(edc / edc[0] + 1e-30)
    end = int(np.argmax(edc < -70.0))
    fo = int(0.05 * FS)
    h = h[:, :end]
    h[:, -fo:] *= 0.5 + 0.5 * np.cos(np.pi * np.arange(fo) / fo)
    # same energy for every channel and every impulse
    for c in range(h.shape[0]):
        h[c] *= np.sqrt(10 ** (TARGET_DB / 10) / (h[c] ** 2).sum())
    t60 = int(np.argmax(edc < -60.0)) / FS
    return h.astype(np.float32), onset / FS * 1000, t60


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        sys.exit(1)
    out = sys.argv[3]
    for name, path, mono in (("chamber", sys.argv[1], False), ("spring", sys.argv[2], True)):
        h, removed, t60 = clean(deconvolve(path, mono))
        h.astype('<f4').tofile(f"{out}/{name}.f32")
        print(f"{name}: {h.shape[0]} ch, {h.shape[1] / FS:.2f} s, -60 dB at {t60:.2f} s, "
              f"removed {removed:.1f} ms before the first sound")


if __name__ == "__main__":
    main()
