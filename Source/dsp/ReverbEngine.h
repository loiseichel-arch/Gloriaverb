// ReverbEngine.h
// The Gloriaverb reverb, without JUCE (so it can be tested offline).
//
// Signal path (wet only, the processor mixes it with the dry signal):
//   era input stage (soft saturation, converter, bandwidth)
//   -> predelay -> early reflections (multi-tap)
//   -> input diffusion (4 allpasses per side)
//   -> 16-line feedback delay network (Hadamard matrix), with per-line
//      two-band decay (Time, Damping), modulation (Mod Depth / Rate) and,
//      for the Vintage era, 16-bit words inside the loop
//      (Spring and Vintage Chamber: convolution with Loic's own recordings
//      instead, Time and Damping shorten them, see Convolver.h)
//   -> Non-lin gate / Glorious gravity envelope
//   -> tail vibrato (Mod Depth) -> era output stage (bandwidth, converter)
//   -> Lo Cut / Hi Cut (on/off switch) -> Width (constant loudness).
//
// Each mode is a set of line lengths, diffusion, tap pattern, modulation
// scale and natural damping. Changing mode fades the reverb out, clears it
// and fades it back in (about 20 ms each way), so there is no click.

#pragma once

#include "Blocks.h"
#include "Convolver.h"

namespace gv
{

// Order is stored in sessions: only add at the end.
enum class Mode { plate140 = 0, plate250, hall, room, nonLin, glorious, spring, vintageChamber };
enum class Era  { vintage = 0, clean };   // (0.3 had Vintage / 80s / Clean: 80s sessions open in Clean)

constexpr int numModes = 8;
inline bool modeAvailable (int m) { return m >= 0 && m < numModes; }
inline bool isConvolution (int m) { return m == (int) Mode::spring || m == (int) Mode::vintageChamber; }
inline int  impulseIndex (int m)  { return m == (int) Mode::vintageChamber ? 1 : 0; }   // 0 Spring, 1 Vintage Chamber

struct Settings
{
    int   mode       = (int) Mode::plate250;
    int   era        = (int) Era::vintage;
    float timeS      = 2.0f;     // 0.1 .. 60 s (decay to -60 dB; Non-lin: gate length; Spring / Chamber: up to the recording)
    float predelayMs = 0.0f;     // 0 .. 250 ms
    float early      = 0.3f;     // 0 .. 1  early reflections level
    float gravity    = 1.0f;     // -1 .. +1 (Glorious) + = normal decay, - = the tail swells
    float modDepth   = 0.3f;     // 0 .. 1
    float modRateHz  = 0.6f;     // 0.05 .. 5 Hz
    bool  filtersOn  = true;
    float loCutHz    = 20.0f;    // 20 = off
    float hiCutHz    = 20000.0f; // 20000 = off
    float damping    = 0.0f;     // -1 .. +1 (+ = highs last longer, lows shorter)
    float width      = 1.0f;     // 0 .. 1.5
};

// ---------------------------------------------------------------------------
// What makes each FDN mode

struct ModeDesign
{
    std::array<float, 16> lineMs;      // FDN line lengths at 48 kHz (ms)
    std::array<float, 4>  diffL, diffR;// input allpasses (ms)
    float diffG;                       // allpass coefficient
    std::array<float, 8>  erMsL, erMsR, erGain;
    float crossoverHz;                 // split between the two decay bands
    float lowMul, highMul;             // natural decay of lows / highs vs Time
    float modMs;                       // (unused since 0.3: modulation has one scale for every mode)
    float modRateMul;                  // LFO speed vs Mod Rate
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
        2200.0f, 1.0f, 0.85f, 0.45f, 1.0f, 0.55f };

    // Plate 250: EMT 250 style digital reverb, longer lines, chorus-like
    // movement, darker tail, slightly fuller lows.
    static const ModeDesign plate250 {
        { 11.3f, 14.1f, 17.2f, 19.9f, 23.1f, 25.7f, 28.9f, 31.6f, 35.2f, 38.3f, 41.9f, 44.6f, 48.4f, 51.8f, 55.2f, 58.9f },
        { 1.7f, 2.9f, 4.3f, 6.1f }, { 2.1f, 3.3f, 4.7f, 5.7f }, 0.62f,
        { 4.3f, 7.9f, 12.1f, 16.6f, 21.7f, 27.4f, 33.8f, 41.2f },
        { 5.1f, 9.2f, 13.4f, 18.3f, 23.6f, 29.9f, 36.1f, 43.7f },
        { 0.80f, 0.70f, 0.62f, 0.54f, 0.46f, 0.38f, 0.31f, 0.24f },
        1600.0f, 1.12f, 0.58f, 1.6f, 1.0f, 0.60f };

    // Hall: big space, long lines, spread early reflections, warm tail.
    static const ModeDesign hall {
        { 29.3f, 33.1f, 37.7f, 41.9f, 45.2f, 49.8f, 53.3f, 57.9f, 61.4f, 66.1f, 70.3f, 74.8f, 79.6f, 84.1f, 89.3f, 94.7f },
        { 2.3f, 3.9f, 5.7f, 8.1f }, { 2.7f, 4.3f, 6.1f, 7.7f }, 0.68f,
        { 7.1f, 11.9f, 17.3f, 23.8f, 31.2f, 39.7f, 49.1f, 61.3f },
        { 8.3f, 13.1f, 19.4f, 26.2f, 33.9f, 42.6f, 52.8f, 64.5f },
        { 0.75f, 0.68f, 0.60f, 0.52f, 0.44f, 0.36f, 0.28f, 0.20f },
        1400.0f, 1.2f, 0.55f, 1.0f, 1.0f, 0.70f };

    // Room: small space, short lines, strong early reflections.
    static const ModeDesign room {
        { 5.3f, 6.1f, 7.0f, 7.9f, 8.8f, 9.9f, 10.7f, 11.8f, 12.9f, 13.8f, 15.1f, 16.3f, 17.6f, 18.9f, 20.3f, 21.8f },
        { 0.7f, 1.3f, 2.1f, 3.0f }, { 0.9f, 1.5f, 2.3f, 2.8f }, 0.6f,
        { 2.1f, 3.9f, 5.6f, 7.8f, 10.3f, 13.1f, 16.4f, 20.2f },
        { 2.7f, 4.6f, 6.4f, 8.7f, 11.5f, 14.2f, 17.9f, 22.1f },
        { 1.0f, 0.9f, 0.8f, 0.7f, 0.6f, 0.5f, 0.4f, 0.3f },
        1800.0f, 1.0f, 0.6f, 0.25f, 1.0f, 0.50f };

    // Non-lin: a dense, bright room kept full until the gate closes.
    static const ModeDesign nonLin {
        { 7.4f, 8.6f, 9.8f, 11.1f, 12.3f, 13.9f, 15.0f, 16.5f, 18.1f, 19.3f, 21.1f, 22.8f, 24.6f, 26.5f, 28.4f, 30.5f },
        { 0.9f, 1.6f, 2.6f, 3.7f }, { 1.1f, 1.9f, 2.8f, 3.4f }, 0.75f,
        { 1.3f, 2.9f, 4.4f, 6.2f, 8.1f, 10.4f, 12.9f, 15.8f },
        { 1.7f, 3.3f, 4.9f, 6.8f, 8.9f, 11.2f, 13.8f, 16.9f },
        { 0.8f, 0.75f, 0.7f, 0.65f, 0.6f, 0.55f, 0.5f, 0.45f },
        2500.0f, 1.0f, 0.8f, 0.2f, 1.0f, 0.45f };

    // Glorious: huge, very dense, deep slow movement.
    static const ModeDesign glorious {
        { 61.0f, 68.9f, 76.4f, 83.7f, 91.6f, 99.2f, 107.9f, 115.3f, 124.1f, 132.6f, 141.8f, 150.3f, 159.7f, 168.2f, 177.9f, 187.4f },
        { 3.1f, 5.3f, 8.7f, 12.9f }, { 3.7f, 6.1f, 9.4f, 13.8f }, 0.75f,
        { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f },    // unused (the fader is Gravity)
        { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        1200.0f, 1.0f, 0.75f, 3.5f, 0.5f, 0.90f };

    switch ((Mode) mode)
    {
        case Mode::plate140: return plate140;
        case Mode::hall:     return hall;
        case Mode::room:     return room;
        case Mode::nonLin:   return nonLin;
        case Mode::glorious: return glorious;
        case Mode::plate250:
        case Mode::spring:
        case Mode::vintageChamber:
        default:             return plate250;   // also used to size things in the convolution modes
    }
}

// ---------------------------------------------------------------------------

class ReverbEngine
{
public:
    static constexpr int N = 16;

    // The recordings behind Spring (0) and Vintage Chamber (1). Set them
    // before prepare(); without them those two modes are silent.
    void setImpulse (int which, std::shared_ptr<const ImpulseSource> src)
    {
        if (which < 0 || which > 1) return;
        sources[which] = std::move (src);
        kernels[which].reset();
    }

    // Length of the recording (decay to -60 dB): the longest Time of that mode.
    float naturalTime (int mode) const
    {
        if (! isConvolution (mode)) return 60.0f;
        const auto& src = sources[impulseIndex (mode)];
        return src != nullptr ? src->naturalTime : 0.0f;
    }

    void prepare (double sampleRate, int maxBlock)
    {
        fs = sampleRate;
        (void) maxBlock;

        // the impulses at this rate (heavy, only when the rate changes)
        int maxTaps = 0;
        for (int k = 0; k < 2; ++k)
        {
            if (sources[k] != nullptr && (kernels[k] == nullptr || std::fabs (kernels[k]->fs - fs) > 0.5))
                kernels[k] = sharedKernel (sources[k], fs);
            if (kernels[k] != nullptr && kernels[k]->valid) maxTaps = std::max (maxTaps, kernels[k]->length);
        }
        conv.prepare (maxTaps);

        for (auto& l : lines) l.setMaxSamples ((int) (0.215 * fs) + 64);   // longest Glorious line + modulation
        for (int s = 0; s < 2; ++s)
        {
            pre[s].setMaxSamples ((int) (0.340 * fs) + 8);   // predelay + early taps
            for (auto& a : diff[s]) a.d.setMaxSamples ((int) (0.020 * fs) + 8);
        }
        for (auto& v : vib) v.setMaxSamples ((int) (0.020 * fs) + 16);
        modSmooth = 1.0f - timeCoef (fs, 0.03);
        adc.prepare (fs);
        dac.prepare (fs);
        keyFast.set (fs, 0.001, 0.030);
        gateKey.set (fs, 0.0005, 0.005);
        keySlow.set (fs, 0.050, 0.400);
        gateAtt = 1.0f - timeCoef (fs, 0.001);
        gateRel = 1.0f - timeCoef (fs, 0.005);
        predelaySmooth = timeCoef (fs, 0.050);
        fadeStep = (float) (1.0 / (0.020 * fs));

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
        }
        std::fill (std::begin (damp), std::end (damp), 0.0f);
        conv.reset();
        vib[0].clear(); vib[1].clear();
        for (int k = 0; k < N; ++k) modA[k] = modAT[k];
        vibA = vibAT;
        gateArmed = true; gateRearm = 0.0f;
        adc.reset(); dac.reset();
        keyFast.env = keySlow.env = gateKey.env = 0.0f;
        gateHold = 0.0f; gate = 0.0f; swell = 1.0f;
        filterMix = settings.filtersOn ? 1.0f : 0.0f;
    }

    // Call once per block (cheap when nothing changed).
    void setSettings (const Settings& s)
    {
        if (s.mode != settings.mode && current >= 0) pendingMode = s.mode;
        applySettings (s, false);
    }

    // In-place stereo processing, wet only.
    void process (float* left, float* right, int n)
    {
        const bool eraLoop16 = era == Era::vintage;
        const bool isConv = isConvolution (current);
        const bool isNonLin = current == (int) Mode::nonLin;
        const bool isGlorious = current == (int) Mode::glorious;
        const float filterStep = 1.0f / (0.010f * (float) fs);

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
                    process (left + i, right + i, n - i);   // the rest of the block with the new mode
                    return;
                }
            }
            else if (fade < 1.0f)
                fade = std::min (1.0f, fade + fadeStep);

            float xl = left[i], xr = right[i];

            // ---- era: input stage ----
            if (satAmount > 0.0f) { xl = softSat (xl, satAmount); xr = softSat (xr, satAmount); }
            xl = inLp[0].process (xl);
            xr = inLp[1].process (xr);
            if (era == Era::vintage) adc.process (xl, xr);

            // ---- predelay ----
            pre[0].push (xl);
            pre[1].push (xr);
            predelayCur = predelayTarget + predelaySmooth * (predelayCur - predelayTarget);
            const float pd = std::max (2.0f, predelayCur);
            const float pl = pre[0].tapFrac (pd), pr = pre[1].tapFrac (pd);

            float wl = 0.0f, wr = 0.0f;

            if (isConv)
            {
                conv.process (0.5f * (pl + pr), wl, wr);   // mono in, stereo out
            }
            else
            {
                // ---- early reflections ----
                float el = 0.0f, er = 0.0f;
                if (earlyGain > 0.0f)
                    for (int k = 0; k < 8; ++k)
                    {
                        el += erG[k] * ((k & 1) ? pre[1].tapFrac (pd + erTapL[k]) : pre[0].tapFrac (pd + erTapL[k]));
                        er += erG[k] * ((k & 1) ? pre[0].tapFrac (pd + erTapR[k]) : pre[1].tapFrac (pd + erTapR[k]));
                    }

                // ---- input diffusion ----
                float dl = pl, dr = pr;
                for (auto& a : diff[0]) dl = a.process (dl);
                for (auto& a : diff[1]) dr = a.process (dr);

                // ---- FDN ----
                float s[N];
                for (int k = 0; k < N; ++k)
                {
                    modA[k] += modSmooth * (modAT[k] - modA[k]);
                    const float d = baseLen[k] - modA[k] * (1.0f + nextLfo (k));   // swings 0 .. 2 x modA below the base length
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

                float late = lateGain;

                // ---- Glorious: gravity (the tail swells after each attack) ----
                if (isGlorious)
                {
                    const float key = std::max (std::fabs (pl), std::fabs (pr));
                    const float f = keyFast.process (key), sl = keySlow.process (key);
                    if (f > 0.003f && f > 2.5f * sl) swell = std::min (swell, 1.0f - 0.97f * swellDepth);
                    swell += swellRate * (1.0f - swell);
                    late *= std::pow (swell, swellPower);
                }

                wl = outL * late + earlyGain * el;
                wr = outR * late + earlyGain * er;

                // ---- Non-lin: gate (open while there is sound, then Time, then shut) ----
                if (isNonLin)
                {
                    // each attack opens the gate for exactly Time, however long the note
                    const float a = std::max (std::fabs (pl), std::fabs (pr));
                    const float f = gateKey.process (a), sl = keySlow.process (a);
                    if (gateArmed && f > 0.0056f && f > 2.0f * sl + 0.001f)   // attack above about -45 dBFS
                    {
                        gateHold = gateLen;
                        gateArmed = false;
                        gateRearm = (float) (0.040 * fs);
                    }
                    else if (gateHold > 0.0f) gateHold -= 1.0f;
                    if (! gateArmed && (gateRearm -= 1.0f) <= 0.0f && f < 1.3f * sl + 0.001f) gateArmed = true;
                    const float target = gateHold > 0.0f ? 1.0f : 0.0f;
                    gate += (target > gate ? gateAtt : gateRel) * (target - gate);
                    wl *= gate;
                    wr *= gate;
                }
            }

            // ---- modulation of the whole tail (the "sea-sick" part of Mod Depth) ----
            vibA += modSmooth * (vibAT - vibA);
            vib[0].push (wl);
            vib[1].push (wr);
            if (vibA > 0.01f || vibAT > 0.0f)
            {
                wl = vib[0].tapFrac (2.0f + vibA * (1.0f + nextLfo (N)));
                wr = vib[1].tapFrac (2.0f + vibA * (1.0f + nextLfo (N + 1)));
            }
            else
            {
                wl = vib[0].tapFrac (2.0f);
                wr = vib[1].tapFrac (2.0f);
            }

            // ---- era: output stage ----
            wl = outLp[0].process (wl);
            wr = outLp[1].process (wr);
            if (era == Era::vintage) dac.process (wl, wr);

            // ---- Lo / Hi Cut, with an on/off switch (10 ms crossfade) ----
            const float ft = filtersOnTarget ? 1.0f : 0.0f;
            if (filterMix < ft || filterMix > ft) filterMix = ft > filterMix ? std::min (ft, filterMix + filterStep) : std::max (ft, filterMix - filterStep);
            {
                float fl = wl, fr = wr, hp = 0.0f;
                if (loCutOn) { loCut[0].process (fl, hp); fl = hp; loCut[1].process (fr, hp); fr = hp; }
                if (hiCutOn) { fl = hiCut[0].process (fl, hp); fr = hiCut[1].process (fr, hp); }
                wl += filterMix * (fl - wl);
                wr += filterMix * (fr - wr);
            }

            // ---- Width, same loudness at every setting ----
            const float m = 0.5f * (wl + wr), sd = 0.5f * (wl - wr) * width;
            left[i]  = (m + sd) * widthGain * fade;
            right[i] = (m - sd) * widthGain * fade;
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
            baseLen[k] = (float) nextPrime ((int) std::lround (md.lineMs[(size_t) k] * 0.001 * 48000.0 * scale));
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

        dampCoef = onePoleCoef (fs, md.crossoverHz);
        lateGain = md.lateGain;
        conv.setKernel (isConvolution (mode) ? kernels[impulseIndex (mode)].get() : nullptr);

        for (int k = 0; k < N + 2; ++k)
        {
            lfoPhase[k] = (float) k / (float) N;
            lfoSpread[k] = k >= N ? (k == N ? 1.0f : 1.07f) : 0.80f + 0.45f * (float) ((k * 7) % N) / (float) N;
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

        era = (Era) std::clamp (s.era, 0, 1);
        const bool isNonLin = current == (int) Mode::nonLin;
        const bool isGlorious = current == (int) Mode::glorious;

        // decay gains (Non-lin: the room rings well past the gate)
        float t = std::clamp (s.timeS, 0.1f, 60.0f);
        if (isNonLin)
        {
            gateLen = (float) (t * fs);
            t = std::min (60.0f, t * 3.0f + 1.0f);
        }
        const float d = std::clamp (s.damping, -1.0f, 1.0f);
        const float eraHigh = era == Era::vintage ? 0.72f : 1.0f;
        const float tLow  = t * md.lowMul  * std::pow (2.0f, -1.0f * d);
        const float tHigh = t * md.highMul * std::pow (2.0f,  1.2f * d) * eraHigh;
        for (int k = 0; k < N; ++k)
        {
            gLow[k]  = std::min (0.99999f, (float) std::exp (-6.907755 * baseLen[k] / (tLow  * fs)));
            gHigh[k] = std::min (0.99999f, (float) std::exp (-6.907755 * baseLen[k] / (tHigh * fs)));
        }

        predelayTarget = (float) (std::clamp (s.predelayMs, 0.0f, 250.0f) * 0.001 * fs);
        if (force) predelayCur = predelayTarget;
        const bool hasEarly = ! isGlorious && ! isConvolution (current);
        earlyGain = hasEarly ? std::clamp (s.early, 0.0f, 1.0f) : 0.0f;
        // Modulation, same scale in every mode. Inside the tank each line swings
        // by up to 4 ms (less in small rooms, and capped so the pitch never
        // runs away at high rates); on top, the whole tail gets a vibrato that
        // grows with the square of Depth: 30 % = a clear chorus, 100 % = sea-sick.
        modRate = std::clamp (s.modRateHz, 0.02f, 10.0f) * md.modRateMul;
        const float depth = std::clamp (s.modDepth, 0.0f, 1.0f);
        const float w = 2.0f * pi * modRate;
        const float loopAmp = std::min (0.004f, 0.06f / w);
        // (short lines are passed through more often, so they swing less)
        const float depthCurve = depth * std::sqrt (depth);
        for (int k = 0; k < N; ++k)
        {
            const float lenFactor = std::sqrt (std::min (1.0f, baseLen[k] / (0.030f * (float) fs)));
            modAT[k] = depthCurve * lenFactor * std::min (loopAmp * (float) fs, 0.4f * baseLen[k]);
        }
        vibAT = depth * depth * std::min (0.007f, 0.09f / w) * (float) fs;
        if (force) { for (int k = 0; k < N; ++k) modA[k] = modAT[k]; vibA = vibAT; }
        width = std::clamp (s.width, 0.0f, 1.5f);
        widthGain = std::sqrt (2.0f / (1.0f + width * width));   // decorrelated sides: keep the loudness

        // Glorious gravity: +1 = normal decay; going down, each attack dips the
        // tail and it swells back over a time that follows Time.
        const float gv = std::clamp (s.gravity, -1.0f, 1.0f);
        swellPower = (1.0f - gv) * 1.5f;                              // 0 .. 3
        swellDepth = std::clamp ((1.0f - gv) * 0.5f, 0.0f, 1.0f);    // 0 .. 1
        const float swellTime = std::clamp (t * (gv < 0.0f ? 0.25f : 0.12f), 0.15f, 10.0f);
        swellRate = 1.0f - timeCoef (fs, swellTime);

        // Spring / Vintage Chamber: Time shortens the recording (it cannot make
        // it longer); Damping shortens the lows (right) or the highs (left).
        if (isConvolution (current))
        {
            const float tn = naturalTime (current);
            if (tn > 0.0f)
            {
                const float te = std::min (t, tn);
                const float tl = std::min (tn, te * std::pow (2.0f, -1.0f * d));
                const float th = std::min (tn, te * std::pow (2.0f,  1.2f * d));
                conv.setDecay (6.907755f * (1.0f / tl - 1.0f / tn), 6.907755f * (1.0f / th - 1.0f / tn));
            }
        }

        filtersOnTarget = s.filtersOn;
        loCutOn = s.loCutHz > 20.5f;
        hiCutOn = s.hiCutHz < 19900.0f;
        for (int c = 0; c < 2; ++c)
        {
            if (loCutOn) loCut[c].set (fs, s.loCutHz);
            if (hiCutOn) hiCut[c].set (fs, s.hiCutHz);
        }

        if (eraChanged || force)
        {
            const double bw = era == Era::vintage ? 11000.0 : 1.0e9;
            for (int c = 0; c < 2; ++c) { inLp[c].setLowpass (fs, bw); outLp[c].setLowpass (fs, bw); }
            satAmount = era == Era::vintage ? 0.7f : 0.0f;
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
    std::shared_ptr<const ImpulseSource> sources[2];
    std::shared_ptr<const ConvKernel> kernels[2];
    Convolver conv;
    float baseLen[N] {}, gLow[N] {}, gHigh[N] {}, outSignL[N] {}, outSignR[N] {}, inSign[N] {};
    float damp[N] {};
    float dampCoef = 0.2f, lateGain = 0.6f, earlyGain = 0.3f, width = 1.0f, widthGain = 1.0f;
    float erTapL[8] {}, erTapR[8] {}, erG[8] {};
    float predelayCur = 0.0f, predelayTarget = 0.0f, predelaySmooth = 0.999f;

    // LFOs: one per line, plus two for the vibrato of the tail
    float lfoPhase[N + 2] {}, lfoSpread[N + 2] {}, rndA[N + 2] {}, rndB[N + 2] {}, jitPhase[N + 2] {}, jitA[N + 2] {}, jitB[N + 2] {};
    float modA[N] {}, modAT[N] {}, modSmooth = 0.001f, modRate = 0.6f;
    DelayLine vib[2];
    float vibA = 0.0f, vibAT = 0.0f;

    // Non-lin gate, Glorious gravity
    EnvFollower keyFast, keySlow, gateKey;
    float gateLen = 0.0f, gateHold = 0.0f, gate = 0.0f, gateAtt = 0.1f, gateRel = 0.01f, gateRearm = 0.0f;
    bool gateArmed = true;
    float swell = 1.0f, swellRate = 0.001f, swellPower = 0.0f, swellDepth = 0.0f;

    Biquad inLp[2], outLp[2];
    Svf loCut[2], hiCut[2];
    bool loCutOn = false, hiCutOn = false, filtersOnTarget = true;
    float filterMix = 1.0f;
    GainRangedConverter adc, dac;
    float satAmount = 0.0f;
    Rng noise { 0xC0FFEEu };
};

} // namespace gv
