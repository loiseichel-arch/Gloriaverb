// Offline test of the Gloriaverb DSP (no JUCE). Run it from the repository
// root (it reads the impulses in Assets/Impulses):
//   g++ -O2 -std=c++17 -I Source tools/engine_test.cpp -o engine_test && ./engine_test
// Reverb: decay times vs Time, Damping tilt, level and stereo of each mode,
// Width loudness, the Filters switch, noise floor per era, 60 s stability,
// Non-lin gate, Glorious gravity, convolution (exactness against a direct
// convolution, no latency, Time / Damping on the recordings), mode change
// clicks, CPU. Modules: Delay, EQ (shelves and bell), Duck, Destroy.

#include "dsp/ReverbEngine.h"
#include "dsp/Modules.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <algorithm>
#include <utility>
#include <vector>
#if defined(__x86_64__) || defined(_M_X64)
 #include <xmmintrin.h>
 #include <pmmintrin.h>
#endif

using namespace gv;

static const double fs = 48000.0;
static const int block = 256;
static bool ok = true;
static void check (bool cond, const char* what) { if (! cond) { ok = false; std::printf ("  !! FAILED: %s\n", what); } }

static const char* modeName (int m) { static const char* n[] = { "Plate 140", "Plate 250", "Hall", "Room", "Non-lin", "Glorious", "Spring", "V.Chamber" }; return n[m]; }

static std::shared_ptr<const ImpulseSource> impulses[2];

static std::shared_ptr<const ImpulseSource> loadImpulse (const char* path, int channels)
{
    std::ifstream f (path, std::ios::binary | std::ios::ate);
    if (! f) { std::printf ("  !! cannot read %s (run from the repository root)\n", path); return {}; }
    const size_t bytes = (size_t) f.tellg();
    std::vector<float> data (bytes / sizeof (float));
    f.seekg (0);
    f.read (reinterpret_cast<char*> (data.data()), (std::streamsize) bytes);
    return std::make_shared<const ImpulseSource> (makeImpulseSource (data.data(), (int) (data.size() / (size_t) channels), channels, 96000.0));
}

static void attachImpulses (ReverbEngine& e) { for (int k = 0; k < 2; ++k) e.setImpulse (k, impulses[k]); }

static void run (ReverbEngine& e, std::vector<float>& L, std::vector<float>& R)
{
    for (size_t i = 0; i < L.size(); i += block)
        e.process (L.data() + i, R.data() + i, (int) std::min<size_t> (block, L.size() - i));
}

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

// RT60 from the Schroeder energy decay curve, fitted between -5 and -25 dB.
static double rt60 (const std::vector<float>& L, const std::vector<float>& R)
{
    std::vector<double> edc (L.size() + 1, 0.0);
    for (size_t i = L.size(); i-- > 0;) edc[i] = edc[i + 1] + (double) L[i] * L[i] + (double) R[i] * R[i];
    const double e0 = edc[0];
    if (e0 <= 0) return 0;
    double t5 = -1, t25 = -1;
    for (size_t i = 0; i < L.size(); ++i)
    {
        const double db = 10.0 * std::log10 (edc[i] / e0 + 1e-30);
        if (t5 < 0 && db <= -5.0) t5 = (double) i / fs;
        if (t25 < 0 && db <= -25.0) { t25 = (double) i / fs; break; }
    }
    if (t5 < 0 || t25 < 0) return -1;
    return (t25 - t5) * 3.0;
}

static Settings base (int mode, float t)
{
    Settings s;
    s.mode = mode; s.era = (int) Era::clean; s.timeS = t; s.predelayMs = 0; s.early = 0;
    s.modDepth = 0.0f; s.damping = 0.0f; s.gravity = 1.0f;
    return s;
}

static void render (const Settings& s, std::vector<float>& L, std::vector<float>& R)
{
    static ReverbEngine e;   // reused: the impulses are only built once
    static bool first = true;
    if (first) { attachImpulses (e); first = false; }
    e.setSettings (s);
    e.prepare (fs, block);
    e.setSettings (s);
    run (e, L, R);
}

static void impulse (const Settings& s, double seconds, std::vector<float>& L, std::vector<float>& R)
{
    L.assign ((size_t) (seconds * fs), 0.0f);
    R.assign (L.size(), 0.0f);
    L[0] = 1.0f; R[0] = 1.0f;
    render (s, L, R);
}

static void pinkish (std::vector<float>& L, std::vector<float>& R, size_t n, uint32_t seed = 99)
{
    L.resize (n); R.resize (n);
    Rng r (seed), r2 (seed * 7 + 1);
    float b0 = 0, b1 = 0;
    for (size_t i = 0; i < n; ++i)
    {
        b0 += 0.05f * (r.bipolar() - b0); b1 += 0.05f * (r2.bipolar() - b1);
        L[i] = 0.25f * (r.bipolar() * 0.3f + b0 * 2.0f);
        R[i] = 0.25f * (r2.bipolar() * 0.3f + b1 * 2.0f);
    }
}

static double energy (const std::vector<float>& x, size_t from, size_t to)
{
    double e = 0; for (size_t i = from; i < to && i < x.size(); ++i) e += (double) x[i] * x[i]; return e;
}

int main()
{
#if defined(__x86_64__) || defined(_M_X64)
    _MM_SET_FLUSH_ZERO_MODE (_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE (_MM_DENORMALS_ZERO_ON);
#endif
    impulses[0] = loadImpulse ("Assets/Impulses/spring.f32", 1);
    impulses[1] = loadImpulse ("Assets/Impulses/chamber.f32", 2);

    std::printf ("== Decay time (Clean, no modulation, no damping): RT60 of the lows (< 400 Hz) vs Time x low multiplier ==\n");
    for (int m : { 0, 1, 2, 3, 5 })
        for (float t : { 0.5f, 2.0f, 8.0f })
        {
            std::vector<float> L, R;
            impulse (base (m, t), std::max (4.0, t * 1.6), L, R);
            auto lL = lowpass (L, 400), lR = lowpass (R, 400);
            const double lo = rt60 (lL, lR), expLo = t * designFor (m).lowMul;
            std::printf ("  %-9s Time %4.1f s : broadband %6.2f s, lows %6.2f s (expected %5.2f)\n", modeName (m), t, rt60 (L, R), lo, expLo);
            check (lo > 0 && std::fabs (lo - expLo) < 0.2 * expLo + 0.1, "decay time");
        }

    std::printf ("\n== Damping tilt (Plate 140, Time 3 s): RT60 lows (<400 Hz) / highs (>6 kHz) ==\n");
    for (float d : { -1.0f, 0.0f, 1.0f })
    {
        Settings s = base (0, 3.0f); s.damping = d;
        std::vector<float> L, R;
        impulse (s, 7.0, L, R);
        std::printf ("  Damping %+4.0f%% : lows %5.2f s, highs %5.2f s\n", d * 100,
                     rt60 (lowpass (L, 400), lowpass (R, 400)), rt60 (highpass (L, 6000), highpass (R, 6000)));
    }

    std::printf ("\n== Level and stereo of every mode (noise in, Time 2 s, Early 30 %%, wet only) ==\n");
    for (int m = 0; m < numModes; ++m)
    {
        Settings s = base (m, 2.0f); s.modDepth = 0.3f; s.early = 0.3f;
        std::vector<float> L, R, iL, iR;
        pinkish (L, R, (size_t) (8 * fs));
        iL = L; iR = R;
        render (s, L, R);
        // Non-lin: a held sound only opens the gate once, so measure inside it
        const size_t a = m == 4 ? (size_t) (0.3 * fs) : L.size() / 2, b = m == 4 ? (size_t) (1.9 * fs) : L.size();
        double cLR = 0; for (size_t i = a; i < b; ++i) cLR += (double) L[i] * R[i];
        const double eo = energy (L, a, b) + energy (R, a, b), ei = energy (iL, a, b) + energy (iR, a, b);
        const double lvl = 10 * std::log10 (eo / ei);
        std::printf ("  %-9s : wet/dry %+5.1f dB, L/R correlation %+.2f\n", modeName (m), lvl, cLR / std::sqrt (energy (L, a, b) * energy (R, a, b)));
        check (lvl > -12 && lvl < 3, "mode level");
    }

    std::printf ("\n== Width keeps the loudness (Hall, noise in) ==\n");
    {
        double ref = 0;
        for (float w : { 1.0f, 0.0f, 0.5f, 1.5f })
        {
            Settings s = base (2, 2.0f); s.width = w;
            std::vector<float> L, R;
            pinkish (L, R, (size_t) (6 * fs));
            render (s, L, R);
            const size_t a = L.size() / 2;
            const double e = energy (L, a, L.size()) + energy (R, a, R.size());
            if (w == 1.0f) ref = e;
            std::printf ("  Width %3.0f%% : %+5.2f dB vs 100%%\n", w * 100, 10 * std::log10 (e / ref));
            check (std::fabs (10 * std::log10 (e / ref)) < 0.6, "width loudness");
        }
    }

    std::printf ("\n== Filters switch (Plate 250, Lo Cut 400 Hz, Hi Cut 3 kHz) ==\n");
    {
        double eOn = 0, eOff = 0;
        for (bool on : { true, false })
        {
            Settings s = base (1, 2.0f); s.loCutHz = 400; s.hiCutHz = 3000; s.filtersOn = on;
            std::vector<float> L, R;
            pinkish (L, R, (size_t) (4 * fs));
            render (s, L, R);
            (on ? eOn : eOff) = energy (L, L.size() / 2, L.size());
        }
        std::printf ("  on vs off: %+5.1f dB\n", 10 * std::log10 (eOn / eOff));
        check (eOn < eOff * 0.7, "filters switch");
    }

    std::printf ("\n== Noise floor with silence in (wet only, Plate 250) ==\n");
    for (int era = 0; era < 2; ++era)
    {
        Settings s = base (1, 2.0f); s.era = era;
        std::vector<float> L ((size_t) (2 * fs), 0.0f), R (L.size(), 0.0f);
        render (s, L, R);
        const char* nm[] = { "Vintage", "Clean" };
        const double db = 10 * std::log10 (energy (L, L.size() / 2, L.size()) / (L.size() / 2) + 1e-30);
        std::printf ("  %-7s : %6.1f dBFS RMS\n", nm[era], db);
        check (db < -150.0, "no hiss: silence in, silence out");
    }

    std::printf ("\n== 60 s tails: stability (100 s after an impulse, every mode and era, full modulation) ==\n");
    for (int m = 0; m < numModes; ++m)
        for (int era = 0; era < 2; ++era)
        {
            Settings s = base (m, 60.0f); s.era = era; s.modDepth = 1.0f; s.modRateHz = 5.0f; s.damping = 1.0f;
            s.gravity = -1.0f;
            std::vector<float> L, R;
            impulse (s, 100.0, L, R);
            float peakLate = 0, peakEarly = 0; bool finite = true;
            for (size_t i = 0; i < L.size(); ++i)
            {
                if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) finite = false;
                const float a = std::max (std::fabs (L[i]), std::fabs (R[i]));
                if (i < (size_t) (2 * fs)) peakEarly = std::max (peakEarly, a);
                if (i > L.size() - (size_t) fs) peakLate = std::max (peakLate, a);
            }
            if (era == 0 || ! finite || peakLate > peakEarly)
                std::printf ("  %-9s era %d : finite %s, peak first 2 s %.3f, peak last s %.4f\n", modeName (m), era, finite ? "yes" : "NO", peakEarly, peakLate);
            check (finite && peakLate <= peakEarly, "stability");
        }

    std::printf ("\n== Non-lin: the gate closes Time after the sound stops ==\n");
    for (float t : { 0.25f, 0.6f, 2.0f })
    {
        Settings s = base (4, t);
        std::vector<float> L ((size_t) (5 * fs), 0.0f), R (L.size(), 0.0f);
        for (size_t i = 0; i < (size_t) (0.05 * fs); ++i)
            L[i] = R[i] = 0.5f * (float) std::sin (2 * 3.14159265 * 180 * (double) i / fs) * (1.0f - (float) i / (float) (0.05 * fs));
        render (s, L, R);
        size_t last = 0;
        for (size_t i = 0; i < L.size(); ++i) if (std::fabs (L[i]) > 0.001f) last = i;
        const double before = std::sqrt (energy (L, (size_t) ((t * 0.6) * fs), (size_t) ((t * 0.8) * fs)) / (0.2 * t * fs));
        std::printf ("  Time %4.2f s : sound until %.3f s (burst ends at 0.050 s), level before the cut %.1f dBFS\n", t, (double) last / fs, 20 * std::log10 (before + 1e-12));
        check (std::fabs ((double) last / fs - 0.05 - t) < 0.06, "non-lin gate length");
    }

    std::printf ("\n== Non-lin: Time counts from the attack (held note 1.5 s, then a second hit at 2.5 s) ==\n");
    for (float t : { 0.4f, 1.0f })
    {
        Settings s = base (4, t);
        std::vector<float> L ((size_t) (5 * fs), 0.0f), R (L.size(), 0.0f);
        for (size_t i = 0; i < (size_t) (1.5 * fs); ++i)
            L[i] = R[i] = 0.4f * (float) std::sin (2 * 3.14159265 * 220 * (double) i / fs) * std::exp (-(float) i / (float) (0.8 * fs));
        for (size_t i = (size_t) (2.5 * fs); i < (size_t) (2.55 * fs); ++i)
            L[i] = R[i] = 0.4f * (float) std::sin (2 * 3.14159265 * 220 * (double) i / fs);
        std::vector<float> dryL = L;
        render (s, L, R);
        // the wet signal includes the dry note's diffusion: find where the tail stops after each hit
        auto lastAbove = [&] (size_t from, size_t to) { size_t last = from; for (size_t i = from; i < to; ++i) if (std::fabs (L[i]) > 0.001f) last = i; return (double) last / fs; };
        const double end1 = lastAbove (0, (size_t) (2.45 * fs)), end2 = lastAbove ((size_t) (2.45 * fs), L.size());
        std::printf ("  Time %.1f s : first gate ends at %.3f s (note lasts 1.5 s), second at %.3f s (hit at 2.5 s)\n", t, end1, end2);
        check (std::fabs (end1 - t) < 0.06, "non-lin: gate length from the attack");
        check (std::fabs (end2 - 2.5 - t) < 0.06, "non-lin: retrigger");
    }

    std::printf ("\n== Glorious: Gravity (Time 8 s): tail 1.5-3 s vs 0.1-0.5 s after a burst ==\n");
    for (float gr : { 1.0f, 0.0f, -1.0f })
    {
        Settings s = base (5, 8.0f); s.gravity = gr; s.modDepth = 0.5f;
        std::vector<float> L ((size_t) (5 * fs), 0.0f), R (L.size(), 0.0f);
        Rng r (5);
        for (size_t i = 0; i < (size_t) (0.05 * fs); ++i) { L[i] = 0.5f * r.bipolar(); R[i] = 0.5f * r.bipolar(); }
        render (s, L, R);
        const double early = energy (L, (size_t) (0.1 * fs), (size_t) (0.5 * fs)) / 0.4;
        const double later = energy (L, (size_t) (1.5 * fs), (size_t) (3.0 * fs)) / 1.5;
        std::printf ("  Gravity %+4.0f : later vs early %+5.1f dB%s\n", gr * 100, 10 * std::log10 (later / early), later > early ? "  (swells)" : "");
        if (gr == 1.0f) check (later < early, "gravity +100 decays");
        if (gr == -1.0f) check (later > early, "gravity -100 swells");
    }

    std::printf ("\n== Convolution engine: against a direct convolution (synthetic 48 kHz impulse, 1.2 s, stereo) ==\n");
    {
        const int len = (int) (1.2 * fs);
        std::vector<float> data ((size_t) len * 2);
        Rng r (11);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < len; ++i)
                data[(size_t) (c * len + i)] = r.bipolar() * std::exp (-6.9f * (float) i / (float) len) * 0.05f;
        ImpulseSource src = makeImpulseSource (data.data(), len, 2, fs);
        ConvKernel k; buildKernel (k, src, fs);
        Convolver cv; cv.prepare (k.length); cv.setKernel (&k);
        const int n = (int) (1.6 * fs);
        std::vector<float> x ((size_t) n), yl ((size_t) n), yr ((size_t) n);
        Rng r2 (12);
        for (int i = 0; i < n; ++i) x[(size_t) i] = i < (int) (0.3 * fs) ? 0.5f * r2.bipolar() : 0.0f;
        for (int i = 0; i < n; ++i) cv.process (x[(size_t) i], yl[(size_t) i], yr[(size_t) i]);
        double err = 0, ref = 0;
        for (int i = 0; i < n; i += 7)   // a sample every 7 is plenty
        {
            double dl = 0, dr = 0;
            for (int j = 0; j < len && j <= i; ++j)
            {
                if (i - j >= (int) (0.3 * fs)) continue;
                dl += (double) data[(size_t) j] * x[(size_t) (i - j)];
                dr += (double) data[(size_t) (len + j)] * x[(size_t) (i - j)];
            }
            err += (dl - yl[(size_t) i]) * (dl - yl[(size_t) i]) + (dr - yr[(size_t) i]) * (dr - yr[(size_t) i]);
            ref += dl * dl + dr * dr;
        }
        const double edb = 10 * std::log10 (err / ref + 1e-30);
        std::printf ("  error vs direct convolution: %.1f dB (taps %d, natural length %.2f s)\n", edb, k.length, k.naturalTime);
        check (edb < -80.0, "convolution exact");
        // no latency: an impulse comes out on the very first sample
        Convolver c2; c2.prepare (k.length); c2.setKernel (&k);
        float l0, r0; c2.process (1.0f, l0, r0);
        std::printf ("  first output sample %.5f (first tap %.5f)\n", l0, data[0]);
        check (std::fabs (l0 - data[0]) < 1e-6f, "convolution: no latency");
    }

    std::printf ("\n== Spring / Vintage Chamber: the recordings, Time and Damping ==\n");
    for (int m : { 6, 7 })
    {
        ReverbEngine probe; attachImpulses (probe);
        const float tn = probe.naturalTime (m);
        std::printf ("  %-9s : natural length %.2f s\n", modeName (m), tn);
        check (tn > 1.0f && tn < 10.0f, "impulse loaded");
        for (float t : { 60.0f, 2.0f, 1.0f, 0.3f })
        {
            std::vector<float> L, R;
            impulse (base (m, t), tn + 1.5, L, R);
            // time to -60 dB on the energy decay curve
            std::vector<double> edc (L.size() + 1, 0.0);
            for (size_t i = L.size(); i-- > 0;) edc[i] = edc[i + 1] + (double) L[i] * L[i] + (double) R[i] * R[i];
            size_t i60 = 0; while (i60 < L.size() && edc[i60] > edc[0] * 1e-6) ++i60;
            const double t60 = (double) i60 / fs, want = std::min (t, tn);
            double c = 0; for (size_t i = 0; i < L.size(); ++i) c += (double) L[i] * R[i];
            std::printf ("      Time %4.1f s : -60 dB at %.2f s (expected about %.2f), L/R correlation %+.2f\n", t, t60, want, c / std::sqrt (energy (L, 0, L.size()) * energy (R, 0, R.size())));
            check (std::fabs (t60 - want) < 0.2 * want + 0.05, "convolution: Time");
        }
        for (float d : { -1.0f, 1.0f })
        {
            Settings s = base (m, 1.5f); s.damping = d;
            std::vector<float> L, R;
            impulse (s, tn + 1.0, L, R);
            // narrow bands (RBJ band-pass, Q 4): 200 Hz and 2.5 kHz
            auto bandpass = [&] (const std::vector<float>& x, double f0)
            {
                const double w = 2 * 3.14159265 * f0 / fs, al = std::sin (w) / 8.0, a0 = 1 + al;
                const double b0 = al / a0, b2 = -al / a0, a1 = -2 * std::cos (w) / a0, a2 = (1 - al) / a0;
                std::vector<float> y (x.size()); double z1 = 0, z2 = 0;
                for (size_t i = 0; i < x.size(); ++i) { const double o = b0 * x[i] + z1; z1 = -a1 * o + z2; z2 = b2 * x[i] - a2 * o; y[i] = (float) o; }
                return y;
            };
            // early decay (0 to -10 dB, x 6): the narrow bands still leak a little of the other band later on
            auto edt = [&] (const std::vector<float>& a, const std::vector<float>& b)
            {
                std::vector<double> e (a.size() + 1, 0.0);
                for (size_t i = a.size(); i-- > 0;) e[i] = e[i + 1] + (double) a[i] * a[i] + (double) b[i] * b[i];
                size_t i10 = 0; while (i10 < a.size() && e[i10] > e[0] * 0.1) ++i10;
                return 6.0 * (double) i10 / fs;
            };
            const double lo = edt (bandpass (L, 200), bandpass (R, 200)), hi = edt (bandpass (L, 2500), bandpass (R, 2500));
            std::printf ("      Time 1.5 s, Damping %+4.0f : lows %.2f s, highs %.2f s (early decay)\n", d * 100, lo, hi);
            check (d > 0 ? lo < hi : hi < lo, "convolution: Damping");
        }
    }

    std::printf ("\n== Modulation: pitch swing of a 1 kHz tone through the reverb (Clean, Rate 0.6 Hz, wet only) ==\n");
    for (int m : { 0, 3, 5 })
        for (float dpt : { 0.0f, 0.3f, 1.0f })
        {
            Settings s = base (m, 3.0f); s.modDepth = dpt; s.modRateHz = 0.6f;
            const size_t n = (size_t) (8 * fs);
            std::vector<float> L (n), R (n);
            for (size_t i = 0; i < n; ++i) L[i] = R[i] = 0.3f * (float) std::sin (2 * 3.14159265 * 1000.0 * (double) i / fs);
            render (s, L, R);
            // frequency per 40 ms window from zero crossings (after 3 s)
            double fmin = 1e9, fmax = 0;
            const size_t w = (size_t) (0.04 * fs);
            for (size_t a = (size_t) (3 * fs); a + w < n; a += w)
            {
                int zc = 0; double first = -1, last = -1;
                for (size_t i = a + 1; i < a + w; ++i)
                    if (L[i - 1] < 0 && L[i] >= 0)
                    {
                        const double t = (double) (i - 1) + L[i - 1] / (L[i - 1] - L[i]);
                        if (first < 0) first = t;
                        last = t;
                        ++zc;
                    }
                if (zc > 3) { const double f = (zc - 1) * fs / (last - first); fmin = std::min (fmin, f); fmax = std::max (fmax, f); }
            }
            const double cents = 1200.0 * std::log2 (fmax / fmin) / 2.0;
            std::printf ("  %-9s Depth %3.0f%% : pitch swing about +/- %.0f cents\n", modeName (m), dpt * 100, cents);
            if (dpt == 1.0f) check (cents > 25.0, "strong modulation at 100 %");
        }

    std::printf ("\n== Mode changes while playing (every mode in turn): largest sample step ==\n");
    {
        ReverbEngine e; attachImpulses (e);
        Settings s = base (0, 3.0f); s.modDepth = 0.3f;
        e.setSettings (s); e.prepare (fs, block); e.setSettings (s);
        const size_t n = (size_t) (8 * fs);
        std::vector<float> L (n), R (n);
        for (size_t i = 0; i < n; ++i) L[i] = R[i] = 0.3f * (float) std::sin (2 * 3.14159265 * 220 * (double) i / fs);
        float maxSteady = 0, maxChange = 0, prev = 0;
        for (size_t i = 0; i < n; i += block)
        {
            if (i % (size_t) fs < block && i > 0) { s.mode = (s.mode + 1) % numModes; e.setSettings (s); }
            e.process (L.data() + i, R.data() + i, block);
            for (size_t j = i; j < i + block; ++j)
            {
                const float st = std::fabs (L[j] - prev); prev = L[j];
                if (j % (size_t) fs < (size_t) (0.1 * fs)) maxChange = std::max (maxChange, st);
                else maxSteady = std::max (maxSteady, st);
            }
        }
        std::printf ("  steady %.4f, around the changes %.4f\n", maxSteady, maxChange);
        check (maxChange < 2.0f * maxSteady + 0.01f, "mode change click");
    }

    // ------------------------------------------------------------------ modules
    std::printf ("\n== Delay (250 ms, feedback 50 %%, impulse on the left) ==\n");
    {
        for (int st = 0; st < 3; ++st)
        {
            DelayFx d; d.prepare (fs);
            DelayFx::Params p; p.type = 0; p.stereo = st; p.timeMs = 250; p.feedback = 0.5f; p.mix = 1.0f;
            d.set (p, 120.0); d.reset();
            const size_t n = (size_t) (2 * fs);
            std::vector<float> L (n, 0.0f), R (n, 0.0f);
            L[0] = 1.0f;
            for (size_t i = 0; i < n; i += block) d.process (L.data() + i, R.data() + i, block);
            auto firstPeak = [&] (const std::vector<float>& x)
            {
                size_t best = 0; float bv = 0;
                for (size_t i = 10; i < (size_t) (0.6 * fs); ++i) if (std::fabs (x[i]) > bv) { bv = std::fabs (x[i]); best = i; }
                return std::make_pair ((double) best / fs * 1000.0, bv);
            };
            auto pl = firstPeak (L), pr = firstPeak (R);
            const char* nm[] = { "Mono", "Wide", "Ping-pong" };
            std::printf ("  %-9s : first repeat L %.0f ms (%.2f), R %.0f ms (%.2f)\n", nm[st], pl.first, pl.second, pr.first, pr.second);
        }
        DelayFx d; d.prepare (fs);
        DelayFx::Params p; p.type = 1; p.stereo = 2; p.timeMs = 120; p.feedback = 1.0f; p.mix = 1.0f;
        d.set (p, 120.0); d.reset();
        std::vector<float> L ((size_t) (30 * fs)), R (L.size());
        Rng r (2); for (size_t i = 0; i < L.size(); ++i) { L[i] = i < (size_t) fs ? 0.5f * r.bipolar() : 0.0f; R[i] = L[i]; }
        for (size_t i = 0; i < L.size(); i += block) d.process (L.data() + i, R.data() + i, block);
        float pk = 0; bool fin = true; for (float v : L) { pk = std::max (pk, std::fabs (v)); fin = fin && std::isfinite (v); }
        std::printf ("  Tape, feedback 100 %%, 30 s : peak %.2f, finite %s\n", pk, fin ? "yes" : "NO");
        check (fin && pk < 4.0f, "delay feedback bounded");
        std::printf ("  Sync 1/8 dotted at 120 BPM : %.1f ms\n", DelayFx::noteMs (DelayFx::noteIndex ("1/8."), 120.0));
        check (std::fabs (DelayFx::noteMs (DelayFx::noteIndex ("1/8."), 120.0) - 375.0) < 0.01, "sync time");
    }

    std::printf ("\n== EQ: band 1 low shelf +6 dB at 200 Hz, band 2 bell -6 dB at 2 kHz (Q 1), band 3 high shelf +6 dB at 8 kHz ==\n");
    {
        Eq3 eq; eq.prepare (fs);
        Eq3::Params p; p.freq = { 200.0f, 2000.0f, 8000.0f }; p.gainDb = { 6.0f, -6.0f, 6.0f }; p.q = { 5.0f, 1.0f, 5.0f };   // Q of the shelves is ignored
        eq.set (p);
        struct Pt { double f, want; } pts[] = { { 30.0, 6.0 }, { 200.0, 3.0 }, { 2000.0, -6.0 }, { 8000.0, 3.0 }, { 20000.0, 6.0 } };
        for (const auto& pt : pts)
        {
            const size_t n = (size_t) fs;
            std::vector<float> L (n), R (n);
            for (size_t i = 0; i < n; ++i) L[i] = R[i] = (float) std::sin (2 * 3.14159265 * pt.f * (double) i / fs) * 0.25f;
            eq.reset();
            eq.process (L.data(), R.data(), (int) n);
            const double g = 20 * std::log10 (std::sqrt (energy (L, n / 2, n) / (n / 2)) / (0.25 / std::sqrt (2.0)));
            std::printf ("  %6.0f Hz : %+5.2f dB (curve says %+5.2f, expected about %+4.1f)\n", pt.f, g, 20 * std::log10 (eq.magnitude (pt.f)), pt.want);
            check (std::fabs (g - pt.want) < 0.8, "eq bands");
        }
    }

    std::printf ("\n== Duck (100 %%, Release 300 ms): reverb level while playing and after ==\n");
    {
        Ducker dk; dk.prepare (fs); dk.set (1.0f, 300.0f);
        const size_t n = (size_t) (3 * fs);
        std::vector<float> key (n, 0.0f), wl (n, 0.2f), wr (n, 0.2f);
        for (size_t i = 0; i < (size_t) (1.0 * fs); ++i) key[i] = 0.5f * (float) std::sin (2 * 3.14159265 * 200 * (double) i / fs);
        for (size_t i = 0; i < n; i += block) dk.process (key.data() + i, key.data() + i, wl.data() + i, wr.data() + i, (int) std::min<size_t> (block, n - i));
        auto db = [&] (double t) { return 20 * std::log10 (std::fabs (wl[(size_t) (t * fs)]) / 0.2); };
        std::printf ("  while playing %+5.1f dB, 0.2 s after %+5.1f dB, 1.5 s after %+5.1f dB\n", db (0.8), db (1.2), db (2.5));
        check (db (0.8) < -15 && db (2.5) > -1.0, "duck");
    }

    std::printf ("\n== Destroy (dropouts, 440 Hz tone, 20 s) ==\n");
    for (float amt : { 0.0f, 0.3f, 1.0f })
        for (float w : { 0.0f, 1.0f, 1.5f })
        {
            if (amt == 0.0f && w > 0.0f) continue;
            Destroyer ds; ds.prepare (fs); ds.set (amt, w);
            const size_t n = (size_t) (20 * fs);
            std::vector<float> L (n), R (n);
            for (size_t i = 0; i < n; ++i) L[i] = R[i] = 0.3f * (float) std::sin (2 * 3.14159265 * 440 * (double) i / fs);
            std::vector<float> ref = L;
            for (size_t i = 0; i < n; i += block) ds.process (L.data() + i, R.data() + i, block);
            int dropsL = 0, dropsR = 0, both = 0; const size_t win = (size_t) (0.01 * fs);
            for (size_t i = 0; i + win < n; i += win)
            {
                const double rl = std::sqrt (energy (L, i, i + win) / win), rr = std::sqrt (energy (R, i, i + win) / win);
                const bool dl = rl < 0.106, dr = rr < 0.106;
                dropsL += dl; dropsR += dr; both += dl && dr;
            }
            // count separate dropouts on the left (1 ms windows) and their median length
            {
                const size_t w1 = (size_t) (0.001 * fs);
                int count = 0; std::vector<int> lens; int cur = 0;
                for (size_t i = 0; i + w1 < n; i += w1)
                {
                    const bool d = std::sqrt (energy (L, i, i + w1) / w1) < 0.106;
                    if (d) ++cur; else if (cur > 0) { lens.push_back (cur); ++count; cur = 0; }
                }
                std::sort (lens.begin(), lens.end());
                if (amt > 0.0f)
                    std::printf ("      %4.1f dropouts per second, median %d ms\n", count / 20.0, lens.empty() ? 0 : lens[lens.size() / 2]);
            }
            std::printf ("  Destroy %3.0f%%, Width %3.0f%% : dropped %4.1f%% of the time (L), %4.1f%% (R), together in %3.0f%% of drops\n",
                         amt * 100, w * 100, 100.0 * dropsL / (double) (n / win), 100.0 * dropsR / (double) (n / win), dropsL ? 100.0 * both / dropsL : 0.0);
            if (amt == 0.0f) check (L == ref, "destroy 0 is an exact bypass");
            if (amt == 1.0f) check (dropsL > 0, "destroy drops");
        }

    std::printf ("\n== CPU (48 kHz stereo, reverb) ==\n");
    for (int m : { 1, 5, 6, 7 })
    {
        ReverbEngine e; attachImpulses (e);
        Settings s = base (m, 4.0f); s.era = 0; s.modDepth = 1.0f; s.early = 0.5f; s.loCutHz = 100; s.hiCutHz = 9000;
        e.setSettings (s); e.prepare (fs, block); e.setSettings (s);
        const size_t n = (size_t) (10 * fs);
        std::vector<float> L (n), R (n);
        Rng r (3); for (size_t i = 0; i < n; ++i) { L[i] = 0.1f * r.bipolar(); R[i] = 0.1f * r.bipolar(); }
        auto t0 = std::chrono::steady_clock::now();
        run (e, L, R);
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("  %-9s : %.1f %% of one core\n", modeName (m), 100.0 * sec / 10.0);
    }

    std::printf ("\n%s\n", ok ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return ok ? 0 : 1;
}
