// Modules.h
// Delay, EQ, Duck and Destroy of Gloriaverb, without JUCE.

#pragma once

#include "Blocks.h"
#include <cstring>
#include <string>

namespace gv
{

inline float mixDryGain (float mix) { return std::cos (mix * 0.5f * pi); }   // equal power
inline float mixWetGain (float mix) { return std::sin (mix * 0.5f * pi); }

// ===========================================================================
// Delay: Digital (clean) or Tape (darker, saturating, wobbling repeats);
// Mono, Wide (right side a little later) or Ping-pong (repeats bounce);
// time in ms or as a note value synced to the tempo.

class DelayFx
{
public:
    struct Params
    {
        int   type     = 1;      // 0 Digital, 1 Tape
        int   stereo   = 1;      // 0 Mono, 1 Wide, 2 Ping-pong
        bool  sync     = false;
        float timeMs   = 375.0f; // 1 .. 2000 ms
        int   note     = 6;      // index in noteNames() (1/8.)
        float feedback = 0.35f;  // 0 .. 1
        float mix      = 0.25f;  // 0 .. 1
    };

    static constexpr int numNotes = 14;
    static const char* noteName (int i)
    {
        static const char* n[numNotes] = { "1/32", "1/16T", "1/16", "1/16.", "1/8T", "1/8", "1/8.",
                                           "1/4T", "1/4", "1/4.", "1/2T", "1/2", "1/2.", "1/1" };
        return n[std::clamp (i, 0, numNotes - 1)];
    }
    static int noteIndex (const char* name)
    {
        for (int i = 0; i < numNotes; ++i) if (std::strcmp (noteName (i), name) == 0) return i;
        return 6;
    }
    // Length in ms of a note value at a tempo (quarter note = 60000 / bpm).
    static double noteMs (int i, double bpm)
    {
        static const double quarters[numNotes] = { 0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75,
                                                   2.0 / 3.0, 1.0, 1.5, 4.0 / 3.0, 2.0, 3.0, 4.0 };
        return quarters[std::clamp (i, 0, numNotes - 1)] * 60000.0 / std::max (20.0, bpm);
    }

    static constexpr double maxMs = 4000.0;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& l : line) l.setMaxSamples ((int) ((maxMs * 1.1 + 20.0) * 0.001 * fs));
        smooth = timeCoef (fs, 0.08);
        lpCoef = onePoleCoef (fs, 3800.0);
        hpCoef = onePoleCoef (fs, 70.0);
        dcCoef = onePoleCoef (fs, 25.0);
        reset();
    }

    void reset()
    {
        for (auto& l : line) l.clear();
        for (int c = 0; c < 2; ++c) { lp[c] = hp[c] = dc[c] = 0.0f; }
        curL = targetL; curR = targetR;
        wowPhase = flutPhase = 0.0f;
    }

    void set (const Params& p, double bpm)
    {
        prm = p;
        double ms = p.sync ? noteMs (p.note, bpm) : (double) std::clamp (p.timeMs, 1.0f, 2000.0f);
        ms = std::clamp (ms, 1.0, maxMs);
        targetL = (float) (ms * 0.001 * fs);
        targetR = p.stereo == 1 ? (float) (ms * 1.08 * 0.001 * fs + 0.007 * fs) : targetL;   // Wide: right a bit later
        fb = p.type == 1 ? std::clamp (p.feedback, 0.0f, 1.0f) * 1.02f : std::clamp (p.feedback, 0.0f, 1.0f) * 0.98f;
        dryG = mixDryGain (std::clamp (p.mix, 0.0f, 1.0f));
        wetG = mixWetGain (std::clamp (p.mix, 0.0f, 1.0f));
        wowDepth = (float) std::min (0.0016 * fs, ms * 0.002 * 0.001 * fs + 0.0004 * fs);
    }

    void process (float* L, float* R, int n)
    {
        const bool tape = prm.type == 1;
        for (int i = 0; i < n; ++i)
        {
            curL = targetL + smooth * (curL - targetL);
            curR = targetR + smooth * (curR - targetR);

            float mod = 0.0f;
            if (tape)
            {
                wowPhase += 0.55f / (float) fs;  if (wowPhase >= 1.0f) wowPhase -= 1.0f;
                flutPhase += 6.3f / (float) fs;  if (flutPhase >= 1.0f) flutPhase -= 1.0f;
                mod = wowDepth * (0.5f + 0.5f * std::sin (2.0f * pi * wowPhase))
                    + 0.00006f * (float) fs * (0.5f + 0.5f * std::sin (2.0f * pi * flutPhase));
            }

            float yl = line[0].tapFrac (std::max (2.0f, curL + mod));
            float yr = line[1].tapFrac (std::max (2.0f, curR + mod * 0.9f));

            if (tape)
            {
                // the loop gets darker, thinner and saturates
                lp[0] += lpCoef * (yl - lp[0]);  lp[1] += lpCoef * (yr - lp[1]);
                hp[0] += hpCoef * (lp[0] - hp[0]); hp[1] += hpCoef * (lp[1] - hp[1]);
                yl = softSat (lp[0] - hp[0], 0.9f);
                yr = softSat (lp[1] - hp[1], 0.9f);
            }
            else
            {
                dc[0] += dcCoef * (yl - dc[0]); dc[1] += dcCoef * (yr - dc[1]);
                yl -= dc[0]; yr -= dc[1];
            }

            const float xl = L[i], xr = R[i], xm = 0.5f * (xl + xr);
            switch (prm.stereo)
            {
                case 0:  line[0].push (xm + fb * yl); line[1].push (xm + fb * yl); yr = yl; break;   // Mono
                case 2:  line[0].push (xm + fb * yr); line[1].push (fb * yl); break;                 // Ping-pong
                default: line[0].push (xl + fb * yl); line[1].push (xr + fb * yr); break;            // Wide
            }

            L[i] = dryG * xl + wetG * yl;
            R[i] = dryG * xr + wetG * yr;
        }
    }

private:
    double fs = 48000.0;
    Params prm;
    DelayLine line[2];
    float targetL = 18000.0f, targetR = 18000.0f, curL = 18000.0f, curR = 18000.0f, smooth = 0.999f;
    float fb = 0.35f, dryG = 1.0f, wetG = 0.0f;
    float lp[2] {}, hp[2] {}, dc[2] {}, lpCoef = 0.3f, hpCoef = 0.01f, dcCoef = 0.003f;
    float wowPhase = 0.0f, flutPhase = 0.0f, wowDepth = 0.0f;
};

// ===========================================================================
// EQ: three bells (Freq, Gain, Q).

class Eq3
{
public:
    struct Params
    {
        std::array<float, 3> freq   { 120.0f, 1200.0f, 6000.0f };
        std::array<float, 3> gainDb { 0.0f, 0.0f, 0.0f };
        std::array<float, 3> q      { 0.9f, 0.9f, 0.9f };
    };

    void prepare (double sampleRate) { fs = sampleRate; last.gainDb[0] = 999.0f; set (prm); reset(); }
    void reset() { for (auto& c : bq) for (auto& b : c) b.reset(); }

    void set (const Params& p)
    {
        prm = p;
        for (int b = 0; b < 3; ++b)
        {
            const auto i = (size_t) b;
            if (same (p.freq[i], last.freq[i]) && same (p.gainDb[i], last.gainDb[i]) && same (p.q[i], last.q[i])) continue;
            for (int c = 0; c < 2; ++c)
            {
                // keep the filter state: only the coefficients change
                const float z1 = bq[c][b].z1, z2 = bq[c][b].z2;
                bq[c][b].setPeak (fs, p.freq[i], p.gainDb[i], std::max (0.1f, p.q[i]));
                bq[c][b].z1 = z1; bq[c][b].z2 = z2;
            }
        }
        last = p;
    }

    void process (float* L, float* R, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R[i];
            for (int b = 0; b < 3; ++b) { l = bq[0][b].process (l); r = bq[1][b].process (r); }
            L[i] = l; R[i] = r;
        }
    }

    // Linear magnitude at f (all three bells), for the curve.
    double magnitude (double f) const
    {
        double m = 1.0;
        for (int b = 0; b < 3; ++b) m *= bq[0][b].magnitude (fs, f);
        return m;
    }

private:
    static bool same (float x, float y) { return ! (x < y || x > y); }
    double fs = 48000.0;
    Params prm, last;
    Biquad bq[2][3];
};

// ===========================================================================
// Duck: the clean input pushes the reverb down while you play; Release sets
// how fast it comes back. Amount 100 % = about -20 dB on loud playing.

class Ducker
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        env.set (fs, 0.002, 0.040);          // detection
        att = timeCoef (fs, 0.004);
        rel = timeCoef (fs, 0.3);
        gain = 1.0f;
    }
    void set (float amount, float releaseMs)
    {
        amt = std::clamp (amount, 0.0f, 1.0f);
        rel = timeCoef (fs, std::clamp (releaseMs, 20.0f, 3000.0f) * 0.001);
    }
    void process (const float* keyL, const float* keyR, float* wl, float* wr, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float e = env.process (std::max (std::fabs (keyL[i]), std::fabs (keyR[i])));
            float target = 1.0f;
            if (amt > 0.0f)
            {
                const float db = 20.0f * std::log10 (e + 1.0e-9f);
                const float depth = std::clamp ((db + 48.0f) / 36.0f, 0.0f, 1.0f);   // -48 dBFS .. -12 dBFS
                target = std::pow (10.0f, -20.0f * amt * depth / 20.0f);
            }
            gain = target + (target < gain ? att : rel) * (gain - target);
            wl[i] *= gain;
            wr[i] *= gain;
        }
    }

private:
    double fs = 48000.0;
    EnvFollower env;
    float amt = 0.0f, gain = 1.0f, att = 0.9f, rel = 0.999f;
};

// ===========================================================================
// Destroy: worn-cassette dropouts on the whole signal. Each dropout pulls
// the level down, closes the highs and slows the "tape" a little, then it
// recovers. More Destroy = more frequent, deeper and longer dropouts.
// Width sets how often both sides drop together (0 = always, 100 % = about
// half the time, 150 % = independent).

class Destroyer
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& b : buf) b.assign (1024, 0.0f);
        att = 1.0f - timeCoef (fs, 0.004);
        rel = 1.0f - timeCoef (fs, 0.035);
        reset();
    }

    void reset()
    {
        for (int c = 0; c < 2; ++c) { g[c] = tg[c] = 1.0f; left[c] = 0; lp[c] = 0.0f; std::fill (buf[c].begin(), buf[c].end(), 0.0f); }
        w = 0;
    }

    void set (float amount, float widthSetting)
    {
        const float a = std::clamp (amount, 0.0f, 1.0f);
        if (a <= 0.0f && amt > 0.0f) reset();
        amt = a;
        rate = 0.15f + 3.85f * a * a;                                       // dropouts per second
        const float wd = std::clamp (widthSetting, 0.0f, 1.5f);
        together = wd <= 1.0f ? 1.0f - 0.5f * wd : 1.5f - wd;               // 1 .. 0.5 .. 0
    }

    void process (float* L, float* R, int n)
    {
        if (amt <= 0.0f) return;   // exact bypass
        float* io[2] = { L, R };
        const float p = rate / (float) fs;
        for (int i = 0; i < n; ++i)
        {
            if (rng.uniform() < p)
            {
                const bool both = rng.uniform() < together;
                const int first = rng.uniform() < 0.5f ? 0 : 1;
                const float depthDb = (6.0f + 34.0f * amt) * (0.4f + 0.6f * rng.uniform());
                const int length = (int) ((0.015f + 0.235f * amt * rng.uniform()) * (float) fs);
                for (int c = 0; c < 2; ++c)
                {
                    if (! both && c != first) continue;
                    tg[c] = std::pow (10.0f, -depthDb / 20.0f);
                    left[c] = length;
                }
            }

            w = (w + 1) & 1023;
            for (int c = 0; c < 2; ++c)
            {
                if (left[c] > 0 && --left[c] == 0) tg[c] = 1.0f;
                g[c] += (tg[c] < g[c] ? att : rel) * (tg[c] - g[c]);

                // tape slows a little in the dropout: read slightly later
                buf[c][(size_t) w] = io[c][i];
                const float d = (1.0f - g[c]) * 0.0008f * (float) fs;
                const int di = (int) d;
                const float f = d - (float) di;
                const float a0 = buf[c][(size_t) ((w - di) & 1023)], a1 = buf[c][(size_t) ((w - di - 1) & 1023)];
                float y = a0 + f * (a1 - a0);

                // highs close with the level
                const float fc = 1500.0f + 18500.0f * g[c] * g[c];
                const float k = 1.0f - std::exp (-2.0f * pi * fc / (float) fs);
                lp[c] += k * (y - lp[c]);
                io[c][i] = lp[c] * g[c];
            }
        }
    }

private:
    struct Uni { uint32_t s = 0x9E3779B9u; float uniform() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s >> 8) * (1.0f / 16777216.0f); } };
    double fs = 48000.0;
    std::vector<float> buf[2];
    int w = 0;
    float amt = 0.0f, rate = 0.0f, together = 0.5f;
    float g[2] { 1.0f, 1.0f }, tg[2] { 1.0f, 1.0f }, lp[2] {}, att = 0.01f, rel = 0.001f;
    int left[2] {};
    Uni rng;
};

} // namespace gv
