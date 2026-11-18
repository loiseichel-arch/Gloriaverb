// Parameters.h
// Every automatable parameter of Gloriaverb, in one place.
// IDs are stored in sessions: never rename them, only add new ones.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>

namespace params
{

namespace id
{
    // Reverb module
    inline constexpr const char* mode     = "mode";
    inline constexpr const char* era      = "era";
    inline constexpr const char* time     = "time";
    inline constexpr const char* predelay = "predelay";
    inline constexpr const char* early    = "early";
    inline constexpr const char* modDepth = "modDepth";
    inline constexpr const char* modRate  = "modRate";
    inline constexpr const char* loCut    = "loCut";
    inline constexpr const char* hiCut    = "hiCut";
    inline constexpr const char* damping  = "damping";
    inline constexpr const char* width    = "width";
    inline constexpr const char* mix      = "mix";
    inline constexpr const char* soloWet  = "soloWet";
    // Master
    inline constexpr const char* output   = "output";
}

// Stored as indexes: only add names at the end.
inline juce::StringArray modeNames() { return { "Plate 140", "Plate 250", "Hall", "Room", "Non-lin", "Glorious" }; }
inline juce::StringArray eraNames()  { return { "Vintage", "80s", "Clean" }; }

// ---- Value formatting (English only) ----
inline juce::String fmtHzValue (float v)
{
    if (v >= 1000.0f)
    {
        const float k = v / 1000.0f;
        return juce::String (k, k < 10.0f ? 2 : 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " kHz";
    }
    return juce::String (juce::roundToInt (v)) + " Hz";
}
inline juce::String fmtLoCut (float v, int) { return v <= 20.5f ? "Off" : fmtHzValue (v); }
inline juce::String fmtHiCut (float v, int) { return v >= 19900.0f ? "Off" : fmtHzValue (v); }
inline juce::String fmtPct (float v, int)   { return juce::String (juce::roundToInt (v)) + "%"; }
inline juce::String fmtMs (float v, int)    { return juce::String (juce::roundToInt (v)) + " ms"; }
inline juce::String fmtRate (float v, int)  { return juce::String (v, v < 1.0f ? 2 : 1) + " Hz"; }
inline juce::String fmtTime (float v, int)
{
    if (v < 1.0f)  return juce::String (juce::roundToInt (v * 1000.0f)) + " ms";
    if (v < 10.0f) return juce::String (v, 1) + " s";
    return juce::String (juce::roundToInt (v)) + " s";
}
inline juce::String fmtSigned (float v, int)
{
    const int i = juce::roundToInt (v);
    return i == 0 ? "0" : ((i > 0 ? "+" : "") + juce::String (i));
}
inline juce::String fmtDb (float v, int)
{
    const float r = std::round (v * 10.0f) / 10.0f;
    return (r > 0.0f ? "+" : "") + juce::String (r, 1) + " dB";
}

// Range that moves evenly on a log scale: value = lo * (hi / lo) ^ position.
// For Time: 0.1 s * 600 ^ position (1/4 = 0.5 s, middle = 2.4 s, 3/4 = 12 s).
inline juce::NormalisableRange<float> logRange (float lo, float hi)
{
    return { lo, hi,
             [] (float s, float e, float n) { return s * std::pow (e / s, n); },
             [] (float s, float e, float v) { return std::log (v / s) / std::log (e / s); },
             [] (float s, float e, float v) { return juce::jlimit (s, e, v); } };
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto addFloat = [&] (const String& pid, const String& name, NormalisableRange<float> range, float def,
                         String (*fmt) (float, int), const char* unit)
    {
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { pid, 1 }, name, range, def,
            AudioParameterFloatAttributes().withStringFromValueFunction (fmt).withLabel (unit)));
    };

    const NormalisableRange<float> pct (0.0f, 100.0f, 0.1f);

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id::mode, 1 }, "Mode", modeNames(), 1));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id::era, 1 },  "Era",  eraNames(), 0));

    addFloat (id::time,     "Time",       logRange (0.1f, 60.0f), 2.0f, fmtTime, "s");
    addFloat (id::predelay, "Predelay",   NormalisableRange<float> (0.0f, 250.0f, 0.1f), 0.0f, fmtMs, "ms");
    addFloat (id::early,    "Early Reflections", pct, 30.0f, fmtPct, "%");
    addFloat (id::modDepth, "Mod Depth",  pct, 30.0f, fmtPct, "%");
    addFloat (id::modRate,  "Mod Rate",   logRange (0.05f, 5.0f), 0.6f, fmtRate, "Hz");
    addFloat (id::loCut,    "Lo Cut",     logRange (20.0f, 1000.0f), 20.0f, fmtLoCut, "Hz");
    addFloat (id::hiCut,    "Hi Cut",     logRange (1000.0f, 20000.0f), 20000.0f, fmtHiCut, "Hz");
    addFloat (id::damping,  "Damping",    NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f, fmtSigned, "");
    addFloat (id::width,    "Width",      NormalisableRange<float> (0.0f, 150.0f, 0.1f), 100.0f, fmtPct, "%");
    addFloat (id::mix,      "Mix",        pct, 30.0f, fmtPct, "%");
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { id::soloWet, 1 }, "Solo Wet", false));
    addFloat (id::output,   "Output",     NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, fmtDb, "dB");

    return layout;
}

} // namespace params
