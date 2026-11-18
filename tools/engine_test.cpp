// Offline test of the Gloriaverb engine (no JUCE).
//   g++ -O2 -std=c++17 -I Source tools/engine_test.cpp -o engine_test && ./engine_test
// Measures decay times against the Time setting, the Damping tilt, the
// level of the tail, stereo correlation, noise floor per era, stability of
// 60 s tails, clicks on a mode change and CPU load.

#include "dsp/ReverbEngine.h"
#include <chrono>
#include <cstdio>
#include <vector>
#if defined(__x86_64__) || defined(_M_X64)
 #include <xmmintrin.h>
 #include <pmmintrin.h>
#endif

using namespace gv;

static const double fs = 48000.0;
static const int block = 256;

static void run (ReverbEngine& e, std::vector<float>& L, std::vector<float>& R)
{
    for (size_t i = 0; i < L.size(); i += block)
    {
        const int n = (int) std::min<size_t> (block, L.size() - i);
        e.process (L.data() + i, R.data() + i, n);
    }
}

// One-pole filters for band measurements.
static std::vector<float> lowpass (const std::vector<float>& x, double fc)
{
    std::vector<float> y (x.size());
    const float c = (float) (1.0 - std::exp (-2.0 * 3.14159265 * fc / fs));
    float s1 = 0, s2 = 0;
    for (size_t i = 0; i < x.size(); ++i) { s1 += c * (x[i] - s1); s2 += c * (s1 - s2); y[i] = s2; }
    return y;
}
static std::vector<float> highpass (const std::vector<float>& x, double fc)
{
    auto lp = lowpass (x, fc);
    std::vector<float> y (x.size());
    for (size_t i = 0; i < x.size(); ++i) y[i] = x[i] - lp[i];
    auto lp2 = lowpass (y, fc);
    for (size_t i = 0; i < x.size(); ++i) y[i] -= lp2[i];
    return y;
}

// RT60 from the Schroeder energy decay curve, fitted between -5 and -25 dB (T20 x 3).
static double rt60 (const std::vector<float>& L, const std::vector<float>& R, size_t start = 0)
{
    std::vector<double> edc (L.size() + 1, 0.0);
    for (size_t i = L.size(); i-- > start;) edc[i] = edc[i + 1] + (double) L[i] * L[i] + (double) R[i] * R[i];
    const double e0 = edc[start];
    if (e0 <= 0) return 0;
    double t5 = -1, t25 = -1;
    for (size_t i = start; i < L.size(); ++i)
    {
        const double db = 10.0 * std::log10 (edc[i] / e0 + 1e-30);
        if (t5 < 0 && db <= -5.0) t5 = (double) (i - start) / fs;
        if (t25 < 0 && db <= -25.0) { t25 = (double) (i - start) / fs; break; }
    }
    if (t5 < 0 || t25 < 0) return -1;
    return (t25 - t5) * 3.0;
}

static Settings base (int mode, float t)
{
    Settings s;
    s.mode = mode; s.era = (int) Era::clean; s.timeS = t; s.predelayMs = 0; s.early = 0;
    s.modDepth = 0.0f; s.damping = 0.0f;
    return s;
}

static void impulse (int mode, const Settings& s, double seconds, std::vector<float>& L, std::vector<float>& R)
{
    ReverbEngine e;
    Settings ss = s; ss.mode = mode;
    e.setSettings (ss);
    e.prepare (fs, block);
    e.setSettings (ss);
    L.assign ((size_t) (seconds * fs), 0.0f);
    R.assign (L.size(), 0.0f);
    L[0] = 1.0f; R[0] = 1.0f;
    run (e, L, R);
}

int main()
{
#if defined(__x86_64__) || defined(_M_X64)
    _MM_SET_FLUSH_ZERO_MODE (_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE (_MM_DENORMALS_ZERO_ON);
#endif
    const char* names[] = { "Plate 140", "Plate 250" };
    bool ok = true;

    std::printf ("== Decay time (Clean, no modulation, no damping): measured RT60 vs Time ==\n");
    std::printf ("   (broadband is pulled down by the natural high-frequency damping of each mode;\n");
    std::printf ("    the < 1 kHz band follows Time x low multiplier)\n");
    for (int m = 0; m < 2; ++m)
        for (float t : { 0.3f, 1.0f, 2.0f, 5.0f, 12.0f })
        {
            std::vector<float> L, R;
            impulse (m, base (m, t), std::max (4.0, t * 1.6), L, R);
            const double bb = rt60 (L, R);
            auto lL = lowpass (L, 400), lR = lowpass (R, 400);
            const double lo = rt60 (lL, lR);
            const double expLo = t * designFor (m).lowMul;
            std::printf ("  %-9s Time %5.1f s : broadband %6.2f s, lows %6.2f s (expected ~%5.2f)\n",
                         names[m], t, bb, lo, expLo);
            if (lo < 0 || std::fabs (lo - expLo) > 0.25 * expLo + 0.08) ok = false;
        }

    std::printf ("\n== Damping tilt (Plate 140, Time 3 s): RT60 lows (<400 Hz) / highs (>6 kHz) ==\n");
    for (float d : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f })
    {
        Settings s = base (0, 3.0f); s.damping = d;
        std::vector<float> L, R;
        impulse (0, s, 7.0, L, R);
        auto lL = lowpass (L, 400), lR = lowpass (R, 400);
        auto hL = highpass (L, 6000), hR = highpass (R, 6000);
        std::printf ("  Damping %+4.0f%% : lows %5.2f s, highs %5.2f s\n", d * 100, rt60 (lL, lR), rt60 (hL, hR));
    }

    std::printf ("\n== Era effect on the highs (Plate 250, Time 3 s, Damping 0; Vintage highs include its hiss) ==\n");
    for (int era = 0; era < 3; ++era)
    {
        Settings s = base (1, 3.0f); s.era = era;
        std::vector<float> L, R;
        impulse (1, s, 7.0, L, R);
        auto hL = highpass (L, 6000), hR = highpass (R, 6000);
        double eh = 0, ea = 0;
        for (size_t i = 0; i < L.size(); ++i) { eh += hL[i] * hL[i] + hR[i] * hR[i]; ea += L[i] * L[i] + R[i] * R[i]; }
        const char* en[] = { "Vintage", "80s", "Clean" };
        std::printf ("  %-7s : highs RT60 %5.2f s, energy above 6 kHz %5.1f dB of total\n",
                     en[era], rt60 (hL, hR), 10 * std::log10 (eh / ea));
    }

    std::printf ("\n== Tail level and stereo (pink-ish noise in, Time 2 s, wet only) ==\n");
    for (int m = 0; m < 2; ++m)
    {
        ReverbEngine e;
        Settings s = base (m, 2.0f); s.modDepth = 0.3f; s.early = 0.3f;
        e.setSettings (s); e.prepare (fs, block); e.setSettings (s);
        const size_t n = (size_t) (8 * fs);
        std::vector<float> L (n), R (n), inL (n), inR (n);
        Rng r (99), r2 (7);
        float b0 = 0, b1 = 0;
        for (size_t i = 0; i < n; ++i)
        {
            b0 += 0.05f * (r.bipolar() - b0); b1 += 0.05f * (r2.bipolar() - b1);
            inL[i] = L[i] = 0.25f * (r.bipolar() * 0.3f + b0 * 2.0f);
            inR[i] = R[i] = 0.25f * (r2.bipolar() * 0.3f + b1 * 2.0f);
        }
        run (e, L, R);
        double ei = 0, eo = 0, cLR = 0, eL = 0, eR = 0;
        for (size_t i = n / 2; i < n; ++i)
        {
            ei += inL[i] * inL[i] + inR[i] * inR[i];
            eo += L[i] * L[i] + R[i] * R[i];
            cLR += L[i] * R[i]; eL += L[i] * L[i]; eR += R[i] * R[i];
        }
        std::printf ("  %-9s : wet/dry level %+5.1f dB, L/R correlation %+.2f\n",
                     names[m], 10 * std::log10 (eo / ei), cLR / std::sqrt (eL * eR));
    }

    std::printf ("\n== Noise floor with silence in (wet only, Plate 250) ==\n");
    for (int era = 0; era < 3; ++era)
    {
        ReverbEngine e;
        Settings s = base (1, 2.0f); s.era = era;
        e.setSettings (s); e.prepare (fs, block); e.setSettings (s);
        std::vector<float> L ((size_t) (2 * fs), 0.0f), R (L.size(), 0.0f);
        run (e, L, R);
        double en = 0;
        for (size_t i = L.size() / 2; i < L.size(); ++i) en += L[i] * L[i];
        const char* nm[] = { "Vintage", "80s", "Clean" };
        std::printf ("  %-7s : %6.1f dBFS RMS\n", nm[era], 10 * std::log10 (en / (L.size() / 2) + 1e-30));
    }

    std::printf ("\n== 60 s tails: stability (120 s after an impulse, every era, full modulation) ==\n");
    for (int m = 0; m < 2; ++m)
        for (int era = 0; era < 3; ++era)
        {
            Settings s = base (m, 60.0f); s.era = era; s.modDepth = 1.0f; s.modRateHz = 5.0f; s.damping = 1.0f;
            std::vector<float> L, R;
            impulse (m, s, 120.0, L, R);
            float peakLate = 0, peakEarly = 0;
            bool finite = true;
            for (size_t i = 0; i < L.size(); ++i)
            {
                if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) finite = false;
                const float a = std::max (std::fabs (L[i]), std::fabs (R[i]));
                if (i < (size_t) fs) peakEarly = std::max (peakEarly, a);
                if (i > L.size() - (size_t) fs) peakLate = std::max (peakLate, a);
            }
            std::printf ("  %-9s era %d : finite %s, peak first s %.3f, peak last s %.4f\n",
                         names[m], era, finite ? "yes" : "NO", peakEarly, peakLate);
            if (! finite || peakLate > peakEarly) ok = false;
        }

    std::printf ("\n== Mode change while playing (Plate 140 -> Plate 250): largest sample step ==\n");
    {
        ReverbEngine e;
        Settings s = base (0, 3.0f); s.modDepth = 0.3f;
        e.setSettings (s); e.prepare (fs, block); e.setSettings (s);
        const size_t n = (size_t) (4 * fs);
        std::vector<float> L (n), R (n);
        for (size_t i = 0; i < n; ++i) L[i] = R[i] = 0.3f * std::sin (2 * 3.14159265 * 220 * (double) i / fs);
        float maxStepBefore = 0, maxStepAfter = 0, prev = 0;
        for (size_t i = 0; i < n; i += block)
        {
            if (i == (size_t) (2 * fs) / block * block) { s.mode = 1; e.setSettings (s); }
            e.process (L.data() + i, R.data() + i, block);
            for (size_t j = i; j < i + block; ++j)
            {
                const float st = std::fabs (L[j] - prev); prev = L[j];
                if (j > (size_t) fs && j < (size_t) (2 * fs)) maxStepBefore = std::max (maxStepBefore, st);
                if (j >= (size_t) (2 * fs) && j < (size_t) (2.2 * fs)) maxStepAfter = std::max (maxStepAfter, st);
            }
        }
        std::printf ("  steady %.4f, around the change %.4f, mode now %d\n", maxStepBefore, maxStepAfter, e.currentMode());
        if (maxStepAfter > 2.0f * maxStepBefore + 0.01f) ok = false;
    }

    std::printf ("\n== CPU (Plate 250, Vintage, full modulation, 48 kHz stereo) ==\n");
    {
        ReverbEngine e;
        Settings s = base (1, 4.0f); s.era = 0; s.modDepth = 1.0f; s.early = 0.5f;
        s.loCutHz = 100; s.hiCutHz = 9000;
        e.setSettings (s); e.prepare (fs, block); e.setSettings (s);
        const size_t n = (size_t) (20 * fs);
        std::vector<float> L (n), R (n);
        Rng r (3);
        for (size_t i = 0; i < n; ++i) { L[i] = 0.1f * r.bipolar(); R[i] = 0.1f * r.bipolar(); }
        auto t0 = std::chrono::steady_clock::now();
        run (e, L, R);
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("  %.1f %% of one core\n", 100.0 * sec / 20.0);
    }

    std::printf ("\n%s\n", ok ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return ok ? 0 : 1;
}
