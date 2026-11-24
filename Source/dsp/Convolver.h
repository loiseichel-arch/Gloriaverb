// Convolver.h
// Zero-latency convolution for the modes made from Loic's own recordings
// (Spring, Vintage Chamber). No JUCE, so it can be tested offline.
//
// Partitioning (taps of the impulse at the session rate):
//   0 .. 63          direct FIR (sample by sample: no latency)
//   64 .. 1023       blocks of 64,  computed when each 64-sample block is in
//   1024 .. 8191     blocks of 512, work spread over the next 512 samples
//   8192 .. end      blocks of 4096, work spread over the next 4096 samples
// so the CPU load stays even (no spike every 4096 samples).
//
// Time and Damping shorten the impulse without rebuilding anything: the
// impulse is split in two bands (lows / highs, linear phase, the two add up
// to the original exactly), and each band gets its own exponential decay
// envelope. Inside every partition the envelope is drawn as a straight line
// between its two ends: for that, each partition keeps two spectra, the
// taps as they are and the taps multiplied by a ramp 0 -> 1. The envelope
// can then change at any time, at the cost of two multiply-adds per band.
//
// The impulse is mono in, stereo out. A mono impulse (Spring) gets its right
// side from a short chain of allpasses (decorrelated, same tone).

#pragma once

#include "Blocks.h"
#include <complex>
#include <memory>
#include <mutex>

namespace gv
{

// ---------------------------------------------------------------------------
// Radix-2 complex FFT (in place).
class Fft
{
public:
    void prepare (int size)
    {
        n = size;
        bits = 0;
        while ((1 << bits) < n) ++bits;
        rev.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
        tw.resize ((size_t) (n / 2));
        for (int i = 0; i < n / 2; ++i)
            tw[(size_t) i] = std::polar (1.0f, (float) (-2.0 * 3.14159265358979323846 * i / n));
    }

    int size() const { return n; }

    // inverse: conjugate twiddles, no 1/n scaling
    void run (std::complex<float>* x, bool inverse) const
    {
        for (int i = 0; i < n; ++i)
        {
            const int r = rev[(size_t) i];
            if (r > i) std::swap (x[i], x[r]);
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len >> 1, step = n / len;
            for (int i = 0; i < n; i += len)
                for (int j = 0; j < half; ++j)
                {
                    std::complex<float> w = tw[(size_t) (j * step)];
                    if (inverse) w = std::conj (w);
                    const std::complex<float> a = x[i + j], b = x[i + j + half] * w;
                    x[i + j] = a + b;
                    x[i + j + half] = a - b;
                }
        }
    }

private:
    int n = 0, bits = 0;
    std::vector<int> rev;
    std::vector<std::complex<float>> tw;
};

// ---------------------------------------------------------------------------
// One impulse, ready to play at one sample rate.
struct ConvKernel
{
    static constexpr int headLen = 64, numStages = 3;
    static constexpr int stageB[numStages] = { 64, 512, 4096 };
    static constexpr int stageO[numStages] = { 64, 1024, 8192 };
    static constexpr int stageEnd[numStages - 1] = { 1024, 8192 };

    struct Stage
    {
        int B = 0, O = 0, P = 0;
        // spectra, split real / imaginary, B + 1 bins each, indexed by
        // ((p * 2 + channel) * 2 + band) * 2 + kind   (kind 0 = taps, 1 = taps x ramp)
        std::vector<float> re, im;
        const float* r (int p, int c, int b, int k) const { return re.data() + (size_t) ((((p * 2 + c) * 2 + b) * 2 + k) * (B + 1)); }
        const float* i (int p, int c, int b, int k) const { return im.data() + (size_t) ((((p * 2 + c) * 2 + b) * 2 + k) * (B + 1)); }
    };

    double fs = 0.0;
    int length = 0;                    // taps
    float naturalTime = 0.0f;          // s, -60 dB on the energy decay curve
    float head[2][2][headLen] {};      // [channel][band][tap]
    Stage stages[numStages];
    bool valid = false;
};

// The recordings as they are stored (96 kHz).
struct ImpulseSource
{
    std::vector<float> ch[2];          // ch[1] empty = mono
    double rate = 96000.0;
    float naturalTime = 0.0f;
    bool empty() const { return ch[0].empty(); }
};

// Energy decay curve -> time where it falls to -60 dB.
inline float naturalTimeOf (const std::vector<double>* chans, int numCh, double fs)
{
    const size_t n = chans[0].size();
    std::vector<double> e (n + 1, 0.0);
    for (size_t i = n; i-- > 0;)
    {
        double s = 0.0;
        for (int c = 0; c < numCh; ++c) s += chans[c][i] * chans[c][i];
        e[i] = e[i + 1] + s;
    }
    if (e[0] <= 0.0) return 0.0f;
    for (size_t i = 0; i < n; ++i)
        if (e[i] < e[0] * 1.0e-6) return (float) ((double) i / fs);
    return (float) ((double) n / fs);
}

inline ImpulseSource makeImpulseSource (const float* data, int frames, int channels, double rate)
{
    ImpulseSource s;
    s.rate = rate;
    for (int c = 0; c < std::min (2, channels); ++c)
        s.ch[c].assign (data + (size_t) c * (size_t) frames, data + (size_t) (c + 1) * (size_t) frames);
    std::vector<double> d[2];
    for (int c = 0; c < std::min (2, channels); ++c) d[c].assign (s.ch[c].begin(), s.ch[c].end());
    s.naturalTime = frames > 0 ? naturalTimeOf (d, std::min (2, channels), rate) : 0.0f;
    return s;
}

namespace convdetail
{
    // Windowed-sinc resampling (Blackman), keeps the gain of the impulse.
    // The usual rates are ratios of small integers (44.1 / 96 = 147 / 320):
    // the filter is then a table, one row per output phase.
    inline std::vector<double> resample (const std::vector<float>& x, double from, double to)
    {
        if (std::fabs (from - to) < 0.5) return std::vector<double> (x.begin(), x.end());
        const double ratio = to / from;
        const double fc = 0.94 * std::min (1.0, ratio);              // of the input Nyquist
        const int half = (int) std::ceil (24.0 / std::min (1.0, ratio));
        const size_t outN = (size_t) std::ceil ((double) x.size() * ratio);
        std::vector<double> y (outN, 0.0);
        const double gain = from / to;                               // the sum of the taps follows the rate
        auto weight = [&] (double u)
        {
            const double wpos = 0.5 + 0.5 * u / (double) half;      // 0 .. 1 across the window
            if (wpos <= 0.0 || wpos >= 1.0) return 0.0;
            const double a = 3.14159265358979323846 * fc * u;
            const double sinc = std::fabs (u) < 1e-9 ? 1.0 : std::sin (a) / a;
            const double w = 0.42 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * wpos) + 0.08 * std::cos (4.0 * 3.14159265358979323846 * wpos);
            return fc * sinc * w * ratio * gain;
        };

        // rational ratio L / M (output phase n * M mod L)
        long long L = 0, M = 0;
        if (std::fabs (from - std::round (from)) < 1e-6 && std::fabs (to - std::round (to)) < 1e-6)
        {
            long long a = (long long) std::llround (to), b = (long long) std::llround (from);
            long long g = a, h = b;
            while (h != 0) { const long long r = g % h; g = h; h = r; }
            L = a / g; M = b / g;
        }
        const int taps = 2 * half;
        if (L > 0 && L <= 4096)
        {
            std::vector<double> table ((size_t) (L * taps));
            for (long long ph = 0; ph < L; ++ph)
            {
                const double frac = (double) ph / (double) L;          // t - floor (t)
                for (int j = 0; j < taps; ++j)
                    table[(size_t) (ph * taps + j)] = weight (frac + (double) (half - 1 - j));   // k = c - half + 1 + j
            }
            for (size_t n = 0; n < outN; ++n)
            {
                const long long num = (long long) n * M;
                const long long c = num / L, ph = num % L;
                const double* w = table.data() + (size_t) (ph * taps);
                double acc = 0.0;
                const long long k0 = c - half + 1;
                for (int j = 0; j < taps; ++j)
                {
                    const long long k = k0 + j;
                    if (k >= 0 && k < (long long) x.size()) acc += x[(size_t) k] * w[j];
                }
                y[n] = acc;
            }
            return y;
        }

        for (size_t n = 0; n < outN; ++n)
        {
            const double t = (double) n / ratio;
            const long long c = (long long) std::floor (t);
            double acc = 0.0;
            for (long long k = c - half + 1; k <= c + half; ++k)
                if (k >= 0 && k < (long long) x.size()) acc += x[(size_t) k] * weight (t - (double) k);
            y[n] = acc;
        }
        return y;
    }

    // Zero-phase lowpass (Butterworth, forward then backward).
    inline std::vector<double> zeroPhaseLowpass (const std::vector<double>& x, double fs, double fc)
    {
        const double w = 2.0 * 3.14159265358979323846 * fc / fs, c = std::cos (w), al = std::sin (w) / (2.0 * 0.70710678);
        const double a0 = 1.0 + al;
        const double b0 = (1.0 - c) * 0.5 / a0, b1 = (1.0 - c) / a0, b2 = b0, a1 = -2.0 * c / a0, a2 = (1.0 - al) / a0;
        std::vector<double> y (x.size());
        auto pass = [&] (const std::vector<double>& in, std::vector<double>& out, bool backward)
        {
            double z1 = 0.0, z2 = 0.0;
            const size_t n = in.size();
            for (size_t j = 0; j < n; ++j)
            {
                const size_t i = backward ? n - 1 - j : j;
                const double o = b0 * in[i] + z1;
                z1 = b1 * in[i] - a1 * o + z2;
                z2 = b2 * in[i] - a2 * o;
                out[i] = o;
            }
        };
        std::vector<double> tmp (x.size());
        pass (x, tmp, false);
        pass (tmp, y, true);
        return y;
    }

    // Right side for a mono impulse: allpasses (same tone, decorrelated).
    inline std::vector<double> decorrelate (const std::vector<double>& x, double fs)
    {
        std::vector<double> y = x;
        const double ms[4] = { 1.7, 3.1, 4.9, 7.3 };
        for (double m : ms)
        {
            const size_t d = (size_t) std::lround (m * 0.001 * fs);
            const double g = 0.55;
            std::vector<double> buf (d, 0.0);
            size_t p = 0;
            for (size_t i = 0; i < y.size(); ++i)
            {
                const double z = buf[p];
                const double v = y[i] + g * z;
                buf[p] = v;
                p = (p + 1) % d;
                y[i] = z - g * v;
            }
        }
        double el = 0.0, er = 0.0;
        for (size_t i = 0; i < x.size(); ++i) { el += x[i] * x[i]; er += y[i] * y[i]; }
        if (er > 0.0) { const double k = std::sqrt (el / er); for (auto& v : y) v *= k; }
        return y;
    }
}

// Builds the kernel of an impulse for a sample rate (heavy: call from prepare).
inline void buildKernel (ConvKernel& k, const ImpulseSource& src, double fs)
{
    using namespace convdetail;
    k = ConvKernel();
    k.fs = fs;
    if (src.empty()) return;

    std::vector<double> h[2];
    h[0] = resample (src.ch[0], src.rate, fs);
    h[1] = src.ch[1].empty() ? decorrelate (h[0], fs) : resample (src.ch[1], src.rate, fs);
    const size_t n = std::min (h[0].size(), h[1].size());
    h[0].resize (n); h[1].resize (n);
    k.length = (int) n;
    k.naturalTime = naturalTimeOf (h, 2, fs);

    // two bands: lows (zero-phase lowpass) and highs = the rest
    std::vector<double> band[2][2];
    for (int c = 0; c < 2; ++c)
    {
        band[c][0] = zeroPhaseLowpass (h[c], fs, 700.0);
        band[c][1].resize (n);
        for (size_t i = 0; i < n; ++i) band[c][1][i] = h[c][i] - band[c][0][i];
    }
    auto tap = [&] (int c, int b, long i) { return i >= 0 && i < (long) n ? band[c][b][(size_t) i] : 0.0; };

    for (int c = 0; c < 2; ++c)
        for (int b = 0; b < 2; ++b)
            for (int i = 0; i < ConvKernel::headLen; ++i)
                k.head[c][b][i] = (float) tap (c, b, i);

    for (int s = 0; s < ConvKernel::numStages; ++s)
    {
        auto& st = k.stages[s];
        st.B = ConvKernel::stageB[s];
        st.O = ConvKernel::stageO[s];
        const long end = s < ConvKernel::numStages - 1 ? std::min ((long) ConvKernel::stageEnd[s], (long) n) : (long) n;
        st.P = end > st.O ? (int) ((end - st.O + st.B - 1) / st.B) : 0;
        const int bins = st.B + 1;
        st.re.assign ((size_t) st.P * 8 * (size_t) bins, 0.0f);
        st.im.assign (st.re.size(), 0.0f);
        Fft fft; fft.prepare (2 * st.B);
        std::vector<std::complex<float>> buf ((size_t) (2 * st.B));
        for (int p = 0; p < st.P; ++p)
            for (int c = 0; c < 2; ++c)
                for (int b = 0; b < 2; ++b)
                    for (int kind = 0; kind < 2; ++kind)
                    {
                        const long start = st.O + (long) p * st.B;
                        const long stop = s < ConvKernel::numStages - 1 ? std::min (start + st.B, (long) ConvKernel::stageEnd[s]) : start + st.B;
                        std::fill (buf.begin(), buf.end(), std::complex<float> {});
                        for (long i = start; i < stop; ++i)
                        {
                            const double ramp = kind == 0 ? 1.0 : (double) (i - start) / (double) st.B;
                            buf[(size_t) (i - start)] = (float) (tap (c, b, i) * ramp);
                        }
                        fft.run (buf.data(), false);
                        const size_t off = (size_t) ((((p * 2 + c) * 2 + b) * 2 + kind) * bins);
                        for (int j = 0; j < bins; ++j)
                        {
                            st.re[off + (size_t) j] = buf[(size_t) j].real();
                            st.im[off + (size_t) j] = buf[(size_t) j].imag();
                        }
                    }
    }
    k.valid = true;
}

// The same impulse at the same rate is built once for every instance of the
// plugin (several Gloriaverbs in a session share it).
inline std::shared_ptr<const ConvKernel> sharedKernel (const std::shared_ptr<const ImpulseSource>& src, double fs)
{
    if (src == nullptr) return {};
    struct Entry { std::weak_ptr<const ImpulseSource> src; double fs; std::weak_ptr<const ConvKernel> kernel; };
    static std::mutex lock;
    static std::vector<Entry> cache;
    const std::lock_guard<std::mutex> g (lock);
    cache.erase (std::remove_if (cache.begin(), cache.end(), [] (const Entry& e) { return e.src.expired() || e.kernel.expired(); }), cache.end());
    for (const auto& e : cache)
        if (e.src.lock() == src && std::fabs (e.fs - fs) < 0.5)
            if (auto k = e.kernel.lock()) return k;
    auto k = std::make_shared<ConvKernel>();
    buildKernel (*k, *src, fs);
    cache.push_back ({ src, fs, k });
    return k;
}

// ---------------------------------------------------------------------------
// The running convolution (one at a time: the engine swaps kernels on a
// mode change, with its own fade).
class Convolver
{
public:
    // Allocates everything for kernels of up to maxTaps at this rate.
    void prepare (int maxTaps)
    {
        hist.assign (histSize, 0.0f);
        ring.assign (2 * ringSize, 0.0f);
        for (int s = 0; s < ConvKernel::numStages; ++s)
        {
            auto& st = state[s];
            const int B = ConvKernel::stageB[s], O = ConvKernel::stageO[s];
            const int maxP = s < ConvKernel::numStages - 1 ? (ConvKernel::stageEnd[s] - O) / B
                                                            : std::max (0, (maxTaps - O + B - 1) / B);
            st.maxP = maxP;
            st.fdlRe.assign ((size_t) std::max (1, maxP) * (size_t) (B + 1), 0.0f);
            st.fdlIm.assign (st.fdlRe.size(), 0.0f);
            for (int c = 0; c < 2; ++c) { st.accRe[c].assign ((size_t) (B + 1), 0.0f); st.accIm[c].assign ((size_t) (B + 1), 0.0f); }
            st.g0.assign ((size_t) std::max (1, maxP) * 2, 0.0f);
            st.g1.assign (st.g0.size(), 0.0f);
            st.fft.prepare (2 * B);
            st.buf.assign ((size_t) (2 * B), {});
        }
        kernel = nullptr;
        reset();
    }

    void setKernel (const ConvKernel* k)
    {
        kernel = (k != nullptr && k->valid) ? k : nullptr;
        if (kernel != nullptr)
            for (int s = 0; s < ConvKernel::numStages; ++s)
                if (kernel->stages[s].P > state[s].maxP) kernel = nullptr;   // should not happen: prepared for the longest
        reset();
        updateHead();
    }

    void reset()
    {
        std::fill (hist.begin(), hist.end(), 0.0f);
        std::fill (ring.begin(), ring.end(), 0.0f);
        for (auto& st : state)
        {
            std::fill (st.fdlRe.begin(), st.fdlRe.end(), 0.0f);
            std::fill (st.fdlIm.begin(), st.fdlIm.end(), 0.0f);
            st.j = 0;
        }
        pos = 0;
    }

    // Extra decay, in nepers per second, of the lows and of the highs (0 = the
    // impulse as recorded).
    void setDecay (float lowsPerS, float highsPerS)
    {
        const float fs = kernel != nullptr ? (float) kernel->fs : 48000.0f;
        const float a0 = std::max (0.0f, lowsPerS) / fs, a1 = std::max (0.0f, highsPerS) / fs;
        if (! (a0 < alpha[0] || a0 > alpha[0] || a1 < alpha[1] || a1 > alpha[1])) return;
        alpha[0] = a0; alpha[1] = a1;
        updateHead();
    }

    bool active() const { return kernel != nullptr; }

    void process (float in, float& outL, float& outR)
    {
        const uint32_t hp = (uint32_t) pos & (histSize - 1);
        hist[hp] = in;
        if (kernel == nullptr) { outL = outR = 0.0f; ++pos; return; }

        // direct taps
        float yl = 0.0f, yr = 0.0f;
        for (int i = 0; i < ConvKernel::headLen; ++i)
        {
            const float x = hist[(hp - (uint32_t) i) & (histSize - 1)];
            yl += headEff[0][i] * x;
            yr += headEff[1][i] * x;
        }
        const uint32_t rp = ((uint32_t) pos & (ringSize - 1)) * 2;
        outL = yl + ring[rp];
        outR = yr + ring[rp + 1];
        ring[rp] = ring[rp + 1] = 0.0f;
        ++pos;
        if ((pos & 63) == 0) tick();
    }

private:
    static constexpr uint32_t histSize = 16384;     // >= 2 x the largest block
    static constexpr uint32_t ringSize = 16384;     // output to come (stereo, interleaved)

    struct StageState
    {
        int maxP = 0, j = 0;
        std::vector<float> fdlRe, fdlIm;            // input spectra, one per partition, circular
        std::vector<float> accRe[2], accIm[2];
        std::vector<float> g0, g1;                  // envelope at both ends of each partition, per band
        Fft fft;
        std::vector<std::complex<float>> buf;
        int64_t jobStart = 0;                          // time the current block completed
    };

    float gainAt (int band, double tapIndex) const { return (float) std::exp (-(double) alpha[band] * tapIndex); }

    void updateHead()
    {
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < ConvKernel::headLen; ++i)
                headEff[c][i] = kernel == nullptr ? 0.0f
                              : kernel->head[c][0][i] * gainAt (0, i) + kernel->head[c][1][i] * gainAt (1, i);
    }

    void tick()
    {
        const int64_t t = (int64_t) pos;   // the block that just completed ends here
        for (int s = 0; s < ConvKernel::numStages; ++s)
        {
            const auto& ks = kernel->stages[s];
            if (ks.P == 0) continue;
            auto& st = state[s];
            const int B = ks.B, T = B / 64;
            const int r = (int) ((t / 64) % (int64_t) T);

            if (r == 0) startJob (s, t);
            if (T == 1) { macRange (s, 0, ks.P); finishJob (s); continue; }

            // MAC spread over ticks 0 .. T-2, inverse FFT on the last tick
            if (r <= T - 2)
            {
                const int slices = T - 1;
                const int from = ks.P * r / slices, to = ks.P * (r + 1) / slices;
                macRange (s, from, to);
            }
            if (r == T - 1) finishJob (s);
            (void) st;
        }
    }

    void startJob (int s, int64_t t)
    {
        const auto& ks = kernel->stages[s];
        auto& st = state[s];
        const int B = ks.B, N = 2 * B;
        st.jobStart = t;
        // input frame: the last 2B samples
        for (int i = 0; i < N; ++i)
            st.buf[(size_t) i] = hist[(uint32_t) (t - N + i) & (histSize - 1)];
        st.fft.run (st.buf.data(), false);
        st.j = (st.j + 1) % ks.P;
        float* xr = st.fdlRe.data() + (size_t) st.j * (size_t) (B + 1);
        float* xi = st.fdlIm.data() + (size_t) st.j * (size_t) (B + 1);
        for (int k = 0; k <= B; ++k) { xr[k] = st.buf[(size_t) k].real(); xi[k] = st.buf[(size_t) k].imag(); }
        for (int c = 0; c < 2; ++c)
        {
            std::fill (st.accRe[c].begin(), st.accRe[c].end(), 0.0f);
            std::fill (st.accIm[c].begin(), st.accIm[c].end(), 0.0f);
        }
        // envelope snapshot for this block
        for (int p = 0; p < ks.P; ++p)
            for (int b = 0; b < 2; ++b)
            {
                const double k0 = (double) ks.O + (double) p * B;
                st.g0[(size_t) (p * 2 + b)] = gainAt (b, k0);
                st.g1[(size_t) (p * 2 + b)] = gainAt (b, k0 + B);
            }
    }

    void macRange (int s, int from, int to)
    {
        const auto& ks = kernel->stages[s];
        auto& st = state[s];
        const int bins = ks.B + 1;
        for (int p = from; p < to; ++p)
        {
            const int slot = ((st.j - p) % ks.P + ks.P) % ks.P;
            const float* xr = st.fdlRe.data() + (size_t) slot * (size_t) bins;
            const float* xi = st.fdlIm.data() + (size_t) slot * (size_t) bins;
            const float gl = st.g0[(size_t) (p * 2)], dl = st.g1[(size_t) (p * 2)] - gl;
            const float gh = st.g0[(size_t) (p * 2 + 1)], dh = st.g1[(size_t) (p * 2 + 1)] - gh;
            if (gl < 1.0e-7f && gh < 1.0e-7f) continue;   // fully decayed
            for (int c = 0; c < 2; ++c)
            {
                const float *hlr = ks.r (p, c, 0, 0), *hli = ks.i (p, c, 0, 0), *rlr = ks.r (p, c, 0, 1), *rli = ks.i (p, c, 0, 1);
                const float *hhr = ks.r (p, c, 1, 0), *hhi = ks.i (p, c, 1, 0), *rhr = ks.r (p, c, 1, 1), *rhi = ks.i (p, c, 1, 1);
                float* ar = st.accRe[c].data();
                float* ai = st.accIm[c].data();
                for (int k = 0; k < bins; ++k)
                {
                    const float kr = gl * hlr[k] + dl * rlr[k] + gh * hhr[k] + dh * rhr[k];
                    const float ki = gl * hli[k] + dl * rli[k] + gh * hhi[k] + dh * rhi[k];
                    ar[k] += xr[k] * kr - xi[k] * ki;
                    ai[k] += xr[k] * ki + xi[k] * kr;
                }
            }
        }
    }

    void finishJob (int s)
    {
        const auto& ks = kernel->stages[s];
        auto& st = state[s];
        const int B = ks.B, N = 2 * B;
        // both channels in one inverse FFT: z = yL + i yR
        auto& z = st.buf;
        const float* lr = st.accRe[0].data(); const float* li = st.accIm[0].data();
        const float* rr = st.accRe[1].data(); const float* ri = st.accIm[1].data();
        for (int k = 0; k <= B; ++k)
            z[(size_t) k] = { lr[k] - ri[k], li[k] + rr[k] };
        for (int k = B + 1; k < N; ++k)
        {
            const int m = N - k;   // conj of the mirrored bins
            z[(size_t) k] = { lr[m] + ri[m], -li[m] + rr[m] };
        }
        st.fft.run (z.data(), true);
        const float scale = 1.0f / (float) N;
        // valid output: the second half, for times jobStart - B .. jobStart - 1, plus the stage offset
        const int64_t t0 = st.jobStart - B + ks.O;
        for (int i = 0; i < B; ++i)
        {
            const uint32_t rp = ((uint32_t) (t0 + i) & (ringSize - 1)) * 2;
            ring[rp]     += z[(size_t) (B + i)].real() * scale;
            ring[rp + 1] += z[(size_t) (B + i)].imag() * scale;
        }
    }

    const ConvKernel* kernel = nullptr;
    std::vector<float> hist, ring;
    StageState state[ConvKernel::numStages];
    float headEff[2][ConvKernel::headLen] {};
    float alpha[2] { 0.0f, 0.0f };
    uint64_t pos = 0;
};

} // namespace gv
