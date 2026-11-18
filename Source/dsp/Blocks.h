// Blocks.h
// Small DSP building blocks shared by the Gloriaverb modules (no JUCE).

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
    // Bell (RBJ peaking), gain in dB.
    void setPeak (double fs, double fc, double gainDb, double q)
    {
        fc = std::min (fc, 0.45 * fs);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * 3.14159265358979 * fc / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        const double a0 = 1.0 + al / A;
        b0 = (float) ((1.0 + al * A) / a0); b1 = (float) (-2.0 * c / a0); b2 = (float) ((1.0 - al * A) / a0);
        a1 = b1; a2 = (float) ((1.0 - al / A) / a0);
        active = std::abs (gainDb) > 0.01;
    }
    // Magnitude (linear) at frequency f, for drawing curves.
    double magnitude (double fs, double f) const
    {
        if (! active) return 1.0;
        const double w = 2.0 * 3.14159265358979 * f / fs;
        const double cr = std::cos (w), ci = -std::sin (w), c2r = std::cos (2 * w), c2i = -std::sin (2 * w);
        const double nr = b0 + b1 * cr + b2 * c2r, ni = b1 * ci + b2 * c2i;
        const double dr = 1.0 + a1 * cr + a2 * c2r, di = a1 * ci + a2 * c2i;
        return std::sqrt ((nr * nr + ni * ni) / (dr * dr + di * di));
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

// First-order allpass (a + z^-1) / (1 + a z^-1): delays frequencies by
// different amounts. A chain of them gives the spring "chirp".
struct Allpass1
{
    float z = 0.0f;
    float process (float x, float a) { const float y = a * x + z; z = x - a * y; return y; }
};

// One-pole smoothing / filters.
inline float onePoleCoef (double fs, double fc) { return (float) (1.0 - std::exp (-2.0 * 3.14159265358979 * fc / fs)); }
inline float timeCoef (double fs, double seconds) { return (float) std::exp (-1.0 / (std::max (1.0e-5, seconds) * fs)); }

// Peak follower with separate attack and release.
struct EnvFollower
{
    float env = 0.0f, att = 0.0f, rel = 0.0f;
    void set (double fs, double attackS, double releaseS) { att = timeCoef (fs, attackS); rel = timeCoef (fs, releaseS); }
    float process (float x)
    {
        const float a = std::fabs (x);
        env = a > env ? a + att * (env - a) : a + rel * (env - a);
        return env;
    }
};

} // namespace gv
