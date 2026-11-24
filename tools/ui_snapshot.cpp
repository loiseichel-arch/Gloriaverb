// Linux test helper: runs Gloriaverb's processor (all modules, every module
// order) on a test signal and saves pictures of the three panels.
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

static void save (juce::Component& c, const juce::String& path)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    juce::File f (path);
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("saved %s\n", path.toRawUTF8());
}

static void set (GloriaverbProcessor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

// Runs `seconds` of clicks + a tone through the processor, returns peak and whether all is finite.
static std::pair<float, bool> runAudio (GloriaverbProcessor& p, double seconds, int blockSize = 512)
{
    juce::AudioBuffer<float> buf (2, blockSize);
    juce::MidiBuffer midi;
    float peak = 0.0f; bool finite = true;
    const int blocks = (int) (seconds * 48000.0 / blockSize);
    for (int b = 0; b < blocks; ++b)
    {
        buf.clear();
        if (b % 40 < 4)
            for (int i = 0; i < blockSize; ++i)
            {
                const float v = 0.3f * (float) std::sin (2.0 * 3.14159265 * 330.0 * (double) (b * blockSize + i) / 48000.0);
                buf.setSample (0, i, v); buf.setSample (1, i, v);
            }
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < blockSize; ++i)
            {
                const float v = buf.getSample (c, i);
                if (! std::isfinite (v)) finite = false;
                peak = std::max (peak, std::abs (v));
            }
    }
    return { peak, finite };
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String out = argc > 1 ? argv[1] : "/tmp";
    bool ok = true;

    GloriaverbProcessor p;
    p.setPlayConfigDetails (2, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);

    auto r = runAudio (p, 3.0);
    std::printf ("defaults (Reverb only, Solo Wet on): peak %.3f, finite %s\n", r.first, r.second ? "yes" : "NO");
    ok = ok && r.second;

    // everything on, every module order, every mode
    set (p, params::id::delayOn, 1); set (p, params::id::eqOn, 1);
    set (p, params::id::soloWet, 0); set (p, params::id::mix, 40);
    set (p, params::id::duck, 60); set (p, params::id::destroy, 50);
    set (p, params::id::eqGain[1], 6); set (p, params::id::dFeedback, 70);
    for (int o = 0; o < 6; ++o)
        for (int m = 0; m < gv::numModes; ++m)
        {
            set (p, params::id::order, (float) o);
            set (p, params::id::mode, (float) m);
            r = runAudio (p, 0.6, o % 2 ? 256 : 480);
            if (! r.second || r.first > 4.0f) { ok = false; std::printf ("order %d mode %d: peak %.3f finite %s\n", o, m, r.first, r.second ? "yes" : "NO"); }
        }
    std::printf ("all orders x all modes, everything on: %s\n", ok ? "finite, bounded" : "PROBLEM");

    // sync delay with a tempo-less host: 120 BPM
    set (p, params::id::dSync, 1);
    r = runAudio (p, 1.0);
    std::printf ("delay synced: peak %.3f, finite %s\n", r.first, r.second ? "yes" : "NO");
    ok = ok && r.second;

    // On / bypass: a hard stop. After a sound, switch off: the output is the
    // clean input (here silence) within a few ms, and switching back on
    // brings nothing of the old tail back.
    {
        GloriaverbProcessor b;
        b.setPlayConfigDetails (2, 2, 48000.0, 256);
        b.prepareToPlay (48000.0, 256);
        set (b, params::id::mode, 7); set (b, params::id::soloWet, 1); set (b, params::id::delayOn, 1);
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        auto blockPeak = [&] { return std::max (buf.getMagnitude (0, 0, 256), buf.getMagnitude (1, 0, 256)); };
        for (int k = 0; k < 40; ++k)
        {
            buf.clear();
            if (k < 4) for (int i = 0; i < 256; ++i) buf.setSample (0, i, 0.5f), buf.setSample (1, i, 0.5f);
            b.processBlock (buf, midi);
        }
        const float before = blockPeak();
        set (b, params::id::active, 0);
        float afterOff = 0.0f;
        for (int k = 0; k < 4; ++k) { buf.clear(); b.processBlock (buf, midi); if (k >= 1) afterOff = std::max (afterOff, blockPeak()); }
        // a tone while off comes out untouched
        for (int i = 0; i < 256; ++i) { buf.setSample (0, i, 0.25f * (float) std::sin (0.05 * i)); buf.setSample (1, i, -0.1f); }
        juce::AudioBuffer<float> ref (buf);
        b.processBlock (buf, midi);
        float diff = 0.0f;
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) diff = std::max (diff, std::abs (buf.getSample (c, i) - ref.getSample (c, i)));
        set (b, params::id::active, 1);
        float afterOn = 0.0f;
        for (int k = 0; k < 20; ++k) { buf.clear(); b.processBlock (buf, midi); afterOn = std::max (afterOn, blockPeak()); }
        std::printf ("on / bypass: tail before %.4f, after off %.6f, off = clean input (max diff %.6f), back on %.6f\n",
                     before, afterOff, diff, afterOn);
        ok = ok && before > 0.001f && afterOff == 0.0f && diff == 0.0f && afterOn == 0.0f;
    }

    // pictures: defaults of each panel, then a few values moved
    GloriaverbProcessor q;
    q.setPlayConfigDetails (2, 2, 48000.0, 512);
    q.prepareToPlay (48000.0, 512);
    const char* names[3] = { "delay", "reverb", "eq" };
    for (int panel : { 1, 0, 2 })
    {
        q.apvts.state.setProperty ("panel", panel, nullptr);
        std::unique_ptr<juce::AudioProcessorEditor> ed (q.createEditor());
        save (*ed, out + "/gv_" + names[panel] + "_default.png");
    }
    set (q, params::id::mode, 5); set (q, params::id::gravity, -40); set (q, params::id::filtersOn, 0);
    set (q, params::id::loCut, 150); set (q, params::id::hiCut, 7000); set (q, params::id::destroy, 35);
    set (q, params::id::delayOn, 1); set (q, params::id::eqOn, 1); set (q, params::id::order, 4);
    set (q, params::id::dStereo, 2); set (q, params::id::dSync, 1);
    set (q, params::id::eqGain[0], 4); set (q, params::id::eqGain[1], -5); set (q, params::id::eqGain[2], 3);
    runAudio (q, 0.1);
    for (int panel : { 1, 0, 2 })
    {
        q.apvts.state.setProperty ("panel", panel, nullptr);
        std::unique_ptr<juce::AudioProcessorEditor> ed (q.createEditor());
        save (*ed, out + "/gv_" + names[panel] + "_changed.png");
    }
    // Vintage Chamber with Time above the recording, then bypassed
    set (q, params::id::mode, 7); set (q, params::id::time, 10.0f);
    q.apvts.state.setProperty ("panel", 1, nullptr);
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (q.createEditor());
        save (*ed, out + "/gv_reverb_chamber.png");
    }
    set (q, params::id::active, 0);
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (q.createEditor());
        save (*ed, out + "/gv_bypassed.png");
    }
    return ok ? 0 : 1;
}
