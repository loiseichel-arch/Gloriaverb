// ReverbEngine.h
// The Gloriaverb reverb, without JUCE (so it can be tested offline).
//
// Signal path (wet only, the processor mixes it with the dry signal):
//   era input stage (soft saturation, converter, bandwidth)
//   -> predelay -> early reflections (multi-tap)
//   -> input diffusion (4 allpasses per side)
//   -> 16-line feedback delay network (Hadamard matrix), with per-line
//      two-band decay (Time, Damping), modulation (Mod Depth / Rate) and,
//      for the 80s / Vintage eras, 16-bit words inside the loop
//   -> era output stage (bandwidth, converter, hiss)
//   -> Lo Cut / Hi Cut (always on) -> Width.
//
// Each mode is a set of line lengths, diffusion, tap pattern, modulation
// scale and natural damping. Changing mode fades the reverb out, clears it
// and fades it back in (about 20 ms each way), so there is no click.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <vector>

namespace gv
{

constexpr float pi = 3.14159265358979f;

// Order is stored in sessions: only add at the end.
enum class Mode { plate140 = 0, plate250, hall, room, nonLin, glorious };
enum class Era  { vintage = 0, eighties, clean };

constexpr int numModes = 6;
inline bool modeAvailable (int m) { return m == (int) Mode::plate140 || m == (int) Mode::plate250; }

struct Settings
{
    int   mode       = (int) Mode::plate250;
    int   era        = (int) Era::vintage;
    float timeS      = 2.0f;     // 0.1 .. 60 s (decay to -60 dB at the crossover)
    float predelayMs = 0.0f;     // 0 .. 250 ms
    float early      = 0.3f;     // 0 .. 1  early reflections level
    float modDepth   = 0.3f;     // 0 .. 1
    float modRateHz  = 0.6f;     // 0.05 .. 5 Hz
    float loCutHz    = 20.0f;    // 20 = off
    float hiCutHz    = 20000.0f; // 20000 = off
    float damping    = 0.0f;     // -1 .. +1 (+ = highs last longer, lows shorter)
    float width      = 1.0f;     // 0 .. 1.5
};

// ---------------------------------------------------------------------------
// Small building blocks

struct Rng
{
    uint32_t s = 0x12345678u;
    explicit Rng (uint32_t seed = 0x12345678u) : s (seed ? seed : 1u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float bipolar() { return (float) (int32_t) next() * (1.0f / 2147483648.0f); }   // -1 .. 1
};

// Circular buffer, power-of-two size.
struct DelayLine
{
    std::vector<float> buf;
    uint32_t mask = 0, w = 0;

    void setMaxSamples (int n)
    {
        uint32_t size = 1;
        while (size < (uint32_t) n + 4u) size <<= 1;
        buf.assign (size, 0.0f);
        mask = size - 1;
        w = 0;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    void push (float x) { buf[w] = x; w = (w + 1) & mask; }

    // Integer delay, d >= 1 (1 = the sample just pushed).
    float tap (int d) const { return buf[(w - (uint32_t) d) & mask]; }

    // Fractional delay (Hermite cubic), d >= 2.
    float tapFrac (float d) const
    {
        const int   i = (int) d;
        const float f = d - (float) i;
        const float y0 = tap (i - 1), y1 = tap (i), y2 = tap (i + 1), y3 = tap (i + 2);
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }
};

// Schroeder allpass.
struct Allpass
{
    DelayLine d;
    int len = 1;
    float g = 0.6f;
    float process (float x)
    {
        const float z = d.tap (len);
        const float v = x + g * z;
        d.push (v);
        return z - g * v;
    }
};

// Biquad, transposed direct form II.
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    bool active = false;

    void setLowpass (double fs, double fc, double q = 0.70710678)
    {
        if (fc >= 0.45 * fs) { active = false; return; }
        const double w = 2.0 * 3.14159265358979 * fc / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        const double a0 = 1.0 + al;
        b0 = (float) ((1.0 - c) * 0.5 / a0); b1 = (float) ((1.0 - c) / a0); b2 = b0;
        a1 = (float) (-2.0 * c / a0); a2 = (float) ((1.0 - al) / a0);
        active = true;
    }
    void reset() { z1 = z2 = 0; }
    float process (float x)
    {
        if (! active) return x;
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// 12 dB/oct state-variable filter (TPT), lowpass or highpass output.
struct Svf
{
    float g = 0, k = 1.41421356f, a1 = 0, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0;
    void set (double fs, double fc)
    {
        fc = std::min (fc, 0.49 * fs);
        g = (float) std::tan (3.14159265358979 * fc / fs);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void reset() { ic1 = ic2 = 0; }
    // returns lowpass, writes highpass
    float process (float x, float& hp)
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        hp = x - k * v1 - v2;
        return v2;
    }
};

// Gentle saturation with unity gain for small signals.
inline float softSat (float x, float a) { return a > 0.0f ? std::tanh (a * x) / a : x; }

// 12-bit converter with automatic gain ranging (EMT 250 style): the signal
// is boosted by up to +24 dB in 6 dB steps before the 12-bit step, then
// brought back, so the grain follows the level.
struct GainRangedConverter
{
    float env = 0.0f, att = 0.0f, rel = 0.0f;
    void prepare (double fs)
    {
        att = (float) std::exp (-1.0 / (0.0005 * fs));
        rel = (float) std::exp (-1.0 / (0.060 * fs));
    }
    void reset() { env = 0.0f; }
    void process (float& l, float& r)
    {
        const float a = std::max (std::fabs (l), std::fabs (r));
        env = a > env ? a + att * (env - a) : a + rel * (env - a);
        float gain = 1.0f;
        for (int k = 0; k < 4 && env * gain * 2.0f < 0.9f; ++k) gain *= 2.0f;
        const float q = 1.0f / 2048.0f;  // 12 bits
        l = std::round (l * gain / q) * q / gain;
        r = std::round (r * gain / q) * q / gain;
    }
};

inline float quantize16 (float x)        { return std::round (x * 32768.0f) * (1.0f / 32768.0f); }
inline float truncate16 (float x)        { return std::trunc (x * 32768.0f) * (1.0f / 32768.0f); }   // toward zero: tails still die

// ---------------------------------------------------------------------------
// What makes each mode

struct ModeDesign
{
    std::array<float, 16> lineMs;      // FDN line lengths at 48 kHz (ms)
    std::array<float, 4>  diffL, diffR;// input allpasses (ms)
    float diffG;                       // allpass coefficient
    std::array<float, 8>  erMsL, erMsR, erGain;
    float crossoverHz;                 // split between the two decay bands
    float lowMul, highMul;             // natural decay of lows / highs vs Time
    float modMs;                       // delay swing at Mod Depth 100 %
    float lateGain;                    // output level of the tail
};

inline const ModeDesign& designFor (int mode)
{
    // Plate 140: steel plate, very dense from the start, bright, short lines.
    static const ModeDesign plate140 {
        { 9.1f, 10.7f, 12.3f, 13.9f, 15.2f, 16.8f, 18.1f, 19.7f, 21.4f, 23.0f, 24.9f, 26.3f, 28.1f, 29.9f, 31.6f, 33.7f },
        { 1.1f, 1.9f, 3.1f, 4.4f }, { 1.3f, 2.2f, 2.9f, 4.9f }, 0.72f,
        { 1.9f, 3.7f, 5.8f, 8.3f, 10.9f, 13.6f, 16.9f, 20.3f },
        { 2.4f, 4.3f, 6.6f, 8.9f, 11.8f, 14.7f, 17.8f, 21.6f },
        { 0.85f, 0.75f, 0.66f, 0.57f, 0.48f, 0.40f, 0.32f, 0.25f },
        2200.0f, 1.0f, 0.85f, 0.45f, 0.55f };

    // Plate 250: EMT 250 style digital reverb, longer lines, chorus-like
    // movement, darker tail, slightly fuller lows.
    static const ModeDesign plate250 {
        { 11.3f, 14.1f, 17.2f, 19.9f, 23.1f, 25.7f, 28.9f, 31.6f, 35.2f, 38.3f, 41.9f, 44.6f, 48.4f, 51.8f, 55.2f, 58.9f },
        { 1.7f, 2.9f, 4.3f, 6.1f }, { 2.1f, 3.3f, 4.7f, 5.7f }, 0.62f,
        { 4.3f, 7.9f, 12.1f, 16.6f, 21.7f, 27.4f, 33.8f, 41.2f },
        { 5.1f, 9.2f, 13.4f, 18.3f, 23.6f, 29.9f, 36.1f, 43.7f },
        { 0.80f, 0.70f, 0.62f, 0.54f, 0.46f, 0.38f, 0.31f, 0.24f },
        1600.0f, 1.12f, 0.58f, 1.6f, 0.60f };

    return mode == (int) Mode::plate140 ? plate140 : plate250;   // other modes: step 2
}

// ---------------------------------------------------------------------------

class ReverbEngine
{
public:
    static constexpr int N = 16;

    void prepare (double sampleRate, int maxBlock)
    {
        fs = sampleRate;
        (void) maxBlock;
        const double scale = fs / 48000.0;

        for (auto& l : lines) l.setMaxSamples ((int) (0.080 * fs) + 64);   // longest line + modulation
        for (int s = 0; s < 2; ++s)
        {
            pre[s].setMaxSamples ((int) (0.320 * fs) + 8);   // predelay + early taps
            for (auto& a : diff[s]) a.d.setMaxSamples ((int) (0.010 * fs) + 8);
        }
        adc.prepare (fs);
        dac.prepare (fs);
        predelaySmooth = (float) std::exp (-1.0 / (0.050 * fs));
        fadeStep = (float) (1.0 / (0.020 * fs));
        (void) scale;

        current = -1;
        pendingMode = -1;
        fade = 1.0f;
        configure (settings.mode);
        applySettings (settings, true);
        reset();
    }

    void reset()
    {
        for (auto& l : lines) l.clear();
        for (int s = 0; s < 2; ++s)
        {
            pre[s].clear();
            for (auto& a : diff[s]) a.d.clear();
            inLp[s].reset(); outLp[s].reset(); loCut[s].reset(); hiCut[s].reset();
            hissLp[s] = 0.0f;
        }
        std::fill (std::begin (damp), std::end (damp), 0.0f);
        adc.reset(); dac.reset();
    }

    // Call once per block (cheap when nothing changed).
    void setSettings (const Settings& s)
    {
        if (s.mode != settings.mode) pendingMode = s.mode;
        applySettings (s, false);
    }

    // In-place stereo processing, wet only.
    void process (float* left, float* right, int n)
    {
        const bool eraLoop16 = era != Era::clean;

        for (int i = 0; i < n; ++i)
        {
            // ---- mode change: fade out, reconfigure, fade in ----
            if (pendingMode >= 0)
            {
                fade -= fadeStep;
                if (fade <= 0.0f)
                {
                    fade = 0.0f;
                    configure (pendingMode);
                    pendingMode = -1;
                    reset();
                    applySettings (settings, true);
                }
            }
            else if (fade < 1.0f)
                fade = std::min (1.0f, fade + fadeStep);

            float xl = left[i], xr = right[i];

            // ---- era: input stage ----
            if (satAmount > 0.0f) { xl = softSat (xl, satAmount); xr = softSat (xr, satAmount); }
            xl = inLp[0].process (xl);
            xr = inLp[1].process (xr);
            if (era == Era::vintage)       adc.process (xl, xr);
            else if (era == Era::eighties) { xl = quantize16 (xl); xr = quantize16 (xr); }

            // ---- predelay ----
            pre[0].push (xl);
            pre[1].push (xr);
            predelayCur = predelayTarget + predelaySmooth * (predelayCur - predelayTarget);
            const float pd = std::max (2.0f, predelayCur);
            const float pl = pre[0].tapFrac (pd), pr = pre[1].tapFrac (pd);

            // ---- early reflections ----
            float el = 0.0f, er = 0.0f;
            for (int k = 0; k < 8; ++k)
            {
                el += erG[k] * ((k & 1) ? pre[1].tapFrac (pd + erTapL[k]) : pre[0].tapFrac (pd + erTapL[k]));
                er += erG[k] * ((k & 1) ? pre[0].tapFrac (pd + erTapR[k]) : pre[1].tapFrac (pd + erTapR[k]));
            }

            // ---- input diffusion ----
            float dl = pl, dr = pr;
            for (auto& a : diff[0]) dl = a.process (dl);
            for (auto& a : diff[1]) dr = a.process (dr);

            // ---- modulation ----
            for (int k = 0; k < N; ++k) mod[k] = nextLfo (k);

            // ---- FDN ----
            float s[N];
            for (int k = 0; k < N; ++k)
            {
                const float d = baseLen[k] - modSamples * (1.0f + mod[k]);   // 0 .. 2 x swing below base
                s[k] = lines[k].tapFrac (d);
            }

            float outL = 0.0f, outR = 0.0f;
            for (int k = 0; k < N; ++k)
            {
                outL += outSignL[k] * s[k];
                outR += outSignR[k] * s[k];
            }

            float v[N];
            for (int k = 0; k < N; ++k)
            {
                // two-band decay: lows (one-pole below the crossover) and highs
                damp[k] += dampCoef * (s[k] - damp[k]);
                const float lo = damp[k];
                v[k] = gLow[k] * lo + gHigh[k] * (s[k] - lo);
            }
            hadamard16 (v);
            for (int k = 0; k < N; ++k)
            {
                float w = v[k] + inSign[k] * ((k & 1) ? dr : dl);
                if (eraLoop16) w = truncate16 (w);
                lines[k].push (w);
            }

            float wl = outL * lateGain + earlyGain * el;
            float wr = outR * lateGain + earlyGain * er;

            // ---- era: output stage ----
            wl = outLp[0].process (wl);
            wr = outLp[1].process (wr);
            if (era == Era::vintage)       dac.process (wl, wr);
            else if (era == Era::eighties) { wl = quantize16 (wl); wr = quantize16 (wr); }
            if (hissLevel > 0.0f)
            {
                hissLp[0] += 0.35f * (noise.bipolar() - hissLp[0]);
                hissLp[1] += 0.35f * (noise.bipolar() - hissLp[1]);
                wl += hissLevel * hissLp[0];
                wr += hissLevel * hissLp[1];
            }

            // ---- Lo / Hi Cut (always on) ----
            float hp = 0.0f;
            if (loCutOn) { loCut[0].process (wl, hp); wl = hp; loCut[1].process (wr, hp); wr = hp; }
            if (hiCutOn) { wl = hiCut[0].process (wl, hp); wr = hiCut[1].process (wr, hp); }

            // ---- Width ----
            const float m = 0.5f * (wl + wr), sd = 0.5f * (wl - wr) * width;
            left[i]  = (m + sd) * fade;
            right[i] = (m - sd) * fade;
        }
    }

    int currentMode() const { return current; }

private:
    // ---- configuration ----
    void configure (int mode)
    {
        if (! modeAvailable (mode)) mode = (int) Mode::plate250;
        current = mode;
        const ModeDesign& md = designFor (mode);
        const double scale = fs / 48000.0;

        for (int k = 0; k < N; ++k)
        {
            int len = nextPrime ((int) std::lround (md.lineMs[(size_t) k] * 0.001 * 48000.0 * scale));
            baseLen[k] = (float) len;
        }
        for (int s = 0; s < 2; ++s)
            for (int k = 0; k < 4; ++k)
            {
                auto& a = diff[s][k];
                a.len = nextPrime ((int) std::lround ((s == 0 ? md.diffL : md.diffR)[(size_t) k] * 0.001 * fs));
                a.g = md.diffG;
            }

        // early taps, normalised to unit energy
        float e2 = 0.0f;
        for (int k = 0; k < 8; ++k) e2 += md.erGain[(size_t) k] * md.erGain[(size_t) k];
        const float norm = 1.0f / std::sqrt (e2);
        for (int k = 0; k < 8; ++k)
        {
            erTapL[k] = (float) (md.erMsL[(size_t) k] * 0.001 * fs);
            erTapR[k] = (float) (md.erMsR[(size_t) k] * 0.001 * fs);
            erG[k] = md.erGain[(size_t) k] * norm * 0.7f;
        }

        // Sign patterns: rows of a 16x16 Hadamard matrix (orthogonal, so the
        // two outputs are decorrelated).
        for (int k = 0; k < N; ++k)
        {
            outSignL[k] = parity (k & 0x3) ? -0.25f : 0.25f;
            outSignR[k] = parity (k & 0x5) ? -0.25f : 0.25f;
            inSign[k]   = parity (k & 0xA) ? -0.5f : 0.5f;
        }

        dampCoef = (float) (1.0 - std::exp (-2.0 * 3.14159265358979 * md.crossoverHz / fs));
        lateGain = md.lateGain;

        // LFO phases / random states
        for (int k = 0; k < N; ++k)
        {
            lfoPhase[k] = (float) k / (float) N;
            lfoSpread[k] = 0.80f + 0.45f * (float) ((k * 7) % N) / (float) N;
            rndA[k] = rndB[k] = 0.0f;
            jitPhase[k] = jitA[k] = jitB[k] = 0.0f;
        }
    }

    void applySettings (const Settings& s, bool force)
    {
        const bool eraChanged = s.era != settings.era;
        settings = s;
        if (current < 0) return;
        const ModeDesign& md = designFor (current);

        era = (Era) std::clamp (s.era, 0, 2);

        // decay gains
        const float t = std::clamp (s.timeS, 0.1f, 60.0f);
        const float d = std::clamp (s.damping, -1.0f, 1.0f);
        const float eraHigh = era == Era::vintage ? 0.72f : (era == Era::eighties ? 0.86f : 1.0f);
        const float tLow  = t * md.lowMul  * std::pow (2.0f, -1.0f * d);
        const float tHigh = t * md.highMul * std::pow (2.0f,  1.2f * d) * eraHigh;
        for (int k = 0; k < N; ++k)
        {
            gLow[k]  = std::min (0.99999f, (float) std::exp (-6.907755 * baseLen[k] / (tLow  * fs)));
            gHigh[k] = std::min (0.99999f, (float) std::exp (-6.907755 * baseLen[k] / (tHigh * fs)));
        }

        predelayTarget = (float) (std::clamp (s.predelayMs, 0.0f, 250.0f) * 0.001 * fs);
        if (force) predelayCur = predelayTarget;
        earlyGain = std::clamp (s.early, 0.0f, 1.0f);
        modSamples = (float) (std::clamp (s.modDepth, 0.0f, 1.0f) * md.modMs * 0.001 * fs * 0.5);
        modRate = std::clamp (s.modRateHz, 0.02f, 10.0f);
        width = std::clamp (s.width, 0.0f, 1.5f);

        loCutOn = s.loCutHz > 20.5f;
        hiCutOn = s.hiCutHz < 19900.0f;
        for (int c = 0; c < 2; ++c)
        {
            if (loCutOn) loCut[c].set (fs, s.loCutHz);
            if (hiCutOn) hiCut[c].set (fs, s.hiCutHz);
        }

        if (eraChanged || force)
        {
            const double bw = era == Era::vintage ? 11000.0 : (era == Era::eighties ? 15000.0 : 1.0e9);
            for (int c = 0; c < 2; ++c) { inLp[c].setLowpass (fs, bw); outLp[c].setLowpass (fs, bw); }
            satAmount = era == Era::vintage ? 0.7f : (era == Era::eighties ? 0.35f : 0.0f);
            hissLevel = era == Era::vintage ? 4.0e-4f : (era == Era::eighties ? 6.0e-5f : 0.0f);   // about -79 / -95 dBFS
        }
    }

    // ---- modulation: shape set by the era ----
    float nextLfo (int k)
    {
        const float inc = modRate * lfoSpread[k] / (float) fs;
        lfoPhase[k] += inc;
        bool wrapped = false;
        if (lfoPhase[k] >= 1.0f) { lfoPhase[k] -= 1.0f; wrapped = true; }

        if (era == Era::clean)
            return std::sin (2.0f * pi * lfoPhase[k]);

        // smooth random: new target each cycle, cosine glide between targets
        if (wrapped) { rndA[k] = rndB[k]; rndB[k] = noise.bipolar(); }
        const float x = 0.5f - 0.5f * std::cos (pi * lfoPhase[k]);
        float y = rndA[k] + (rndB[k] - rndA[k]) * x;

        if (era == Era::vintage)
        {
            // unstable: a faster, smaller random glide on top
            jitPhase[k] += 3.7f * inc;
            if (jitPhase[k] >= 1.0f) { jitPhase[k] -= 1.0f; jitA[k] = jitB[k]; jitB[k] = noise.bipolar(); }
            const float xj = 0.5f - 0.5f * std::cos (pi * jitPhase[k]);
            y = 0.75f * y + 0.35f * (jitA[k] + (jitB[k] - jitA[k]) * xj);
        }
        return std::clamp (y, -1.0f, 1.0f);
    }

    static void hadamard16 (float* x)
    {
        for (int h = 1; h < 16; h <<= 1)
            for (int i = 0; i < 16; i += h << 1)
                for (int j = i; j < i + h; ++j)
                {
                    const float a = x[j], b = x[j + h];
                    x[j] = a + b;
                    x[j + h] = a - b;
                }
        for (int i = 0; i < 16; ++i) x[i] *= 0.25f;
    }

    static bool parity (int v) { bool p = false; while (v) { p = ! p; v &= v - 1; } return p; }

    static int nextPrime (int n)
    {
        if (n < 3) return 3;
        for (;; ++n)
        {
            bool prime = (n % 2) != 0;
            for (int d = 3; prime && d * d <= n; d += 2) if (n % d == 0) prime = false;
            if (prime) return n;
        }
    }

    // ---- state ----
    double fs = 48000.0;
    Settings settings;
    int current = -1, pendingMode = -1;
    Era era = Era::vintage;
    float fade = 1.0f, fadeStep = 0.001f;

    DelayLine lines[N];
    DelayLine pre[2];
    Allpass diff[2][4];
    float baseLen[N] {}, gLow[N] {}, gHigh[N] {}, outSignL[N] {}, outSignR[N] {}, inSign[N] {};
    float damp[N] {};
    float dampCoef = 0.2f, lateGain = 0.6f, earlyGain = 0.3f, width = 1.0f;
    float erTapL[8] {}, erTapR[8] {}, erG[8] {};
    float predelayCur = 0.0f, predelayTarget = 0.0f, predelaySmooth = 0.999f;

    float mod[N] {}, lfoPhase[N] {}, lfoSpread[N] {}, rndA[N] {}, rndB[N] {}, jitPhase[N] {}, jitA[N] {}, jitB[N] {};
    float modSamples = 0.0f, modRate = 0.6f;

    Biquad inLp[2], outLp[2];
    Svf loCut[2], hiCut[2];
    bool loCutOn = false, hiCutOn = false;
    GainRangedConverter adc, dac;
    float satAmount = 0.0f, hissLevel = 0.0f, hissLp[2] {};
    Rng noise { 0xC0FFEEu };
};

} // namespace gv
