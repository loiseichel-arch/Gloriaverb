#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ui/Components.h"
#include "ui/SketchLookAndFeel.h"
#include "ui/Synoptic.h"
#include "ui/EqCurve.h"
#include "ui/PresetBar.h"

// The window: synoptic at the top (Delay / Reverb / EQ, drag to reorder),
// the panel of the selected module, and the Master strip at the bottom.
class GloriaverbEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit GloriaverbEditor (GloriaverbProcessor&);
    ~GloriaverbEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    struct Group { juce::String title; juce::Rectangle<int> area; bool blue = false; };

    void timerCallback() override;
    void showPanel (int module);
    void updateVisibility();
    float value (const char* pid) const { return proc.apvts.getRawParameterValue (pid)->load(); }

    void paintReverb (juce::Graphics&);
    void paintDelay (juce::Graphics&);
    void paintEq (juce::Graphics&);
    void paintMaster (juce::Graphics&);
    void drawFader (juce::Graphics&, SketchFader&, const juce::String& label,
                    std::initializer_list<std::pair<float, const char*>> ticks, const juce::String& shown = {});
    void valuePill (juce::Graphics&, const juce::String& text, juce::Point<float> centre, uint32_t seed);

    GloriaverbProcessor& proc;
    SketchLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };
    juce::SharedResourcePointer<sketch::Fonts> fonts;

    int panel = 1;   // 0 Delay, 1 Reverb, 2 EQ
    Synoptic synoptic;
    ResetToggle onSwitch { "on", true };   // the whole plugin on / bypass
    PresetBar presetBar;
    bool shownOn = true;

    // ---- Reverb ----
    std::unique_ptr<ChoiceSelector> modeSel, eraSel;
    SketchFader timeFader { "Time" }, predelayFader { "Predelay" }, earlyFader { "Early Ref" },
                gravityFader { "Gravity" };
    SketchKnob depth { "Depth", 56 }, rate { "Rate", 56 },
               loCut { "Lo Cut", 56 }, hiCut { "Hi Cut", 56 },
               damping { "Damping", 56 }, width { "Width", 56 }, mix { "Mix", 56 };
    PillToggle soloWet { "Solo Wet", true };
    ResetToggle filtersSwitch { {}, true };
    juce::Rectangle<int> faderBox;
    std::array<Group, 4> groups;

    // ---- Delay ----
    std::unique_ptr<ChoiceSelector> dTypeSel, dStereoSel, dSyncSel;
    SketchKnob dTime { "Time", 80 }, dNote { "Time", 80 }, dFeedback { "Feedback", 64 }, dMix { "Mix", 64 };
    juce::Rectangle<int> delayKnobBox, repeatsBox;

    // ---- EQ ----
    EqCurve eqCurve;
    SketchKnob eqLoCut { "Freq", 44 }, eqHiCut { "Freq", 44 };
    std::array<std::unique_ptr<SketchKnob>, 9> bandKnobs;
    std::array<juce::Rectangle<int>, 5> eqCols;

    // ---- Master ----
    SketchKnob duck { "Duck", 52 }, release { "Release", 52 }, output { "Output", 52 };
    SketchFader destroyFader { "Destroy", false, true };

    double lastSignature = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GloriaverbEditor)
};
