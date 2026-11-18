// Linux test helper: runs Gloriaverb's processor on a test signal and saves
// pictures of the window (default settings, then Solo Wet + other values).
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

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String out = argc > 1 ? argv[1] : "/tmp";

    GloriaverbProcessor p;
    p.setPlayConfigDetails (2, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);

    // Process 4 s of clicks: check the output is finite and has a tail.
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    float peak = 0.0f; bool finite = true;
    for (int b = 0; b < 375; ++b)
    {
        buf.clear();
        if (b % 94 == 0) { buf.setSample (0, 0, 0.8f); buf.setSample (1, 0, 0.8f); }
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
            {
                const float v = buf.getSample (c, i);
                if (! std::isfinite (v)) finite = false;
                if (b > 2 && b % 94 > 2) peak = std::max (peak, std::abs (v));
            }
    }
    std::printf ("processor: finite %s, tail peak %.4f\n", finite ? "yes" : "NO", peak);

    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    save (*ed, out + "/gloriaverb_default.png");

    auto set = [&] (const char* id, float v)
    {
        auto* prm = p.apvts.getParameter (id);
        prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    };
    set (params::id::mode, 0); set (params::id::era, 2); set (params::id::time, 12.0f);
    set (params::id::loCut, 120.0f); set (params::id::hiCut, 8000.0f); set (params::id::damping, 45.0f);
    set (params::id::soloWet, 1.0f); set (params::id::predelay, 40.0f);
    save (*ed, out + "/gloriaverb_changed.png");
    ed.reset();
    return finite ? 0 : 1;
}
