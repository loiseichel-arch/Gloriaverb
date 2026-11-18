// Parameters.h
// Every automatable parameter of Gloriaverb, in one place.
// IDs are stored in sessions: never rename them, only add new ones.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <cmath>
#include "dsp/Modules.h"

namespace params
{

namespace id
{
    // Routing
    inline constexpr const char* order    = "order";
    inline constexpr const char* delayOn  = "delayOn";
    inline constexpr const char* reverbOn = "reverbOn";
    inline constexpr const char* eqOn     = "eqOn";
    // Reverb module
    inline constexpr const char* mode     = "mode";
    inline constexpr const char* era      = "era";
    inline constexpr const char* time     = "time";
    inline constexpr const char* predelay = "predelay";
    inline constexpr const char* early    = "early";
    inline constexpr const char* gravity  = "gravity";   // Glorious
    inline constexpr const char* boing    = "boing";     // Spring ("Boiiing!!")
    inline constexpr const char* modDepth = "modDepth";
    inline constexpr const char* modRate  = "modRate";
    inline constexpr const char* filtersOn= "filtersOn";
    inline constexpr const char* loCut    = "loCut";
    inline constexpr const char* hiCut    = "hiCut";
    inline constexpr const char* damping  = "damping";
    inline constexpr const char* width    = "width";
    inline constexpr const char* mix      = "mix";
    inline constexpr const char* soloWet  = "soloWet";
    // Delay module
    inline constexpr const char* dType    = "delayType";
    inline constexpr const char* dStereo  = "delayStereo";
    inline constexpr const char* dSync    = "delaySync";
    inline constexpr const char* dTime    = "delayTime";
    inline constexpr const char* dNote    = "delayNote";
    inline constexpr const char* dFeedback= "delayFeedback";
    inline constexpr const char* dMix     = "delayMix";
    // EQ module
    inline constexpr std::array<const char*, 3> eqFreq { "eq1Freq", "eq2Freq", "eq3Freq" };
    inline constexpr std::array<const char*, 3> eqGain { "eq1Gain", "eq2Gain", "eq3Gain" };
    inline constexpr std::array<const char*, 3> eqQ    { "eq1Q", "eq2Q", "eq3Q" };
    // Master
    inline constexpr const char* duck     = "duck";
    inline constexpr const char* release  = "release";
    inline constexpr const char* destroy  = "destroy";
    inline constexpr const char* output   = "output";
}

// Stored as indexes: only add names at the end.
inline juce::StringArray modeNames()   { return { "Plate 140", "Plate 250", "Hall", "Room", "Non-lin", "Glorious", "Spring" }; }
inline juce::StringArray eraNames()    { return { "Vintage", "80s", "Clean" }; }
inline juce::StringArray dTypeNames()  { return { "Digital", "Tape" }; }
inline juce::StringArray dStereoNames(){ return { "Mono", "Wide", "Ping-pong" }; }
inline juce::StringArray dSyncNames()  { return { "ms", "Sync" }; }
inline juce::StringArray noteNames()
{
    juce::StringArray s;
    for (int i = 0; i < gv::DelayFx::numNotes; ++i) s.add (gv::DelayFx::noteName (i));
    return s;
}

// Module order: 0 = Delay, 1 = Reverb, 2 = EQ. The six possible orders.
inline const std::array<std::array<int, 3>, 6>& orders()
{
    static const std::array<std::array<int, 3>, 6> o { { { 0, 1, 2 }, { 0, 2, 1 }, { 1, 0, 2 }, { 1, 2, 0 }, { 2, 0, 1 }, { 2, 1, 0 } } };
    return o;
}
inline int orderIndex (const std::array<int, 3>& o)
{
    for (int i = 0; i < 6; ++i) if (orders()[(size_t) i] == o) return i;
    return 0;
}
inline juce::StringArray moduleNames() { return { "Delay", "Reverb", "EQ" }; }
inline juce::StringArray orderNames()
{
    juce::StringArray s;
    for (const auto& o : orders())
        s.add (moduleNames()[o[0]] + " > " + moduleNames()[o[1]] + " > " + moduleNames()[o[2]]);
    return s;
}

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
inline juce::String fmtHz (float v, int)    { return fmtHzValue (v); }
inline juce::String fmtLoCut (float v, int) { return v <= 20.5f ? "Off" : fmtHzValue (v); }
inline juce::String fmtHiCut (float v, int) { return v >= 19900.0f ? "Off" : fmtHzValue (v); }
inline juce::String fmtPct (float v, int)   { return juce::String (juce::roundToInt (v)) + "%"; }
inline juce::String fmtDuck (float v, int)  { return v < 0.5f ? "Off" : juce::String (juce::roundToInt (v)) + "%"; }
inline juce::String fmtMs (float v, int)    { return juce::String (juce::roundToInt (v)) + " ms"; }
inline juce::String fmtRate (float v, int)  { return juce::String (v, v < 1.0f ? 2 : 1) + " Hz"; }
inline juce::String fmtQ (float v, int)     { return juce::String (v, v < 10.0f ? 2 : 1); }
inline juce::String fmtTime (float v, int)
{
    if (v < 1.0f)  return juce::String (juce::roundToInt (v * 1000.0f)) + " ms";
    if (v < 10.0f) return juce::String (v, 1) + " s";
    return juce::String (juce::roundToInt (v)) + " s";
}
inline juce::String fmtLongMs (float v, int)
{
    if (v < 1000.0f) return juce::String (juce::roundToInt (v)) + " ms";
    return juce::String (v / 1000.0f, 2) + " s";
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
    auto addBool = [&] (const String& pid, const String& name, bool def)
    {
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { pid, 1 }, name, def));
    };
    auto addChoice = [&] (const String& pid, const String& name, const StringArray& items, int def)
    {
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid, 1 }, name, items, def));
    };

    const NormalisableRange<float> pct (0.0f, 100.0f, 0.1f);

    // ---- Reverb (v0.1 parameters first, same IDs) ----
    addChoice (id::mode, "Mode", modeNames(), 1);
    addChoice (id::era,  "Era",  eraNames(), 0);
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
    addBool  (id::soloWet,  "Solo Wet",   true);
    addFloat (id::output,   "Output",     NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, fmtDb, "dB");

    // ---- new in 0.2 ----
    addFloat (id::gravity,  "Gravity",    NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 100.0f, fmtSigned, "");
    addFloat (id::boing,    "Boiiing!!",  pct, 50.0f, fmtPct, "%");
    addBool  (id::filtersOn,"Filters On", true);

    addChoice (id::order,   "Order", orderNames(), 0);
    addBool   (id::delayOn, "Delay On",  false);
    addBool   (id::reverbOn,"Reverb On", true);
    addBool   (id::eqOn,    "EQ On",     false);

    addChoice (id::dType,   "Delay Type",   dTypeNames(), 1);
    addChoice (id::dStereo, "Delay Stereo", dStereoNames(), 1);
    addChoice (id::dSync,   "Delay Sync",   dSyncNames(), 0);
    addFloat  (id::dTime,   "Delay Time",   logRange (1.0f, 2000.0f), 375.0f, fmtLongMs, "ms");
    addChoice (id::dNote,   "Delay Note",   noteNames(), 6);
    addFloat  (id::dFeedback, "Delay Feedback", pct, 35.0f, fmtPct, "%");
    addFloat  (id::dMix,    "Delay Mix",    pct, 25.0f, fmtPct, "%");

    const std::array<float, 3> eqDef { 120.0f, 1200.0f, 6000.0f };
    for (int b = 0; b < 3; ++b)
    {
        const String n = "EQ Band " + String (b + 1) + " ";
        addFloat (id::eqFreq[(size_t) b], n + "Freq", logRange (20.0f, 20000.0f), eqDef[(size_t) b], fmtHz, "Hz");
        addFloat (id::eqGain[(size_t) b], n + "Gain", NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 0.0f, fmtDb, "dB");
        addFloat (id::eqQ[(size_t) b],    n + "Q",    logRange (0.2f, 10.0f), 0.9f, fmtQ, "");
    }

    addFloat (id::duck,    "Duck",    pct, 0.0f, fmtDuck, "%");
    addFloat (id::release, "Release", logRange (20.0f, 3000.0f), 300.0f, fmtLongMs, "ms");
    addFloat (id::destroy, "Destroy", pct, 0.0f, fmtPct, "%");

    return layout;
}

} // namespace params
