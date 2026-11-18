#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ui/Components.h"
#include "ui/SketchLookAndFeel.h"

// Step 1 window: the Reverb panel alone (the synoptic with Delay and EQ, and
// the Master strip with Duck and Destroy, come in the next steps).
class GloriaverbEditor : public juce::AudioProcessorEditor
{
public:
    explicit GloriaverbEditor (GloriaverbProcessor&);
    ~GloriaverbEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Group { juce::String title, tag; juce::Rectangle<int> area; bool blue = false; };

    GloriaverbProcessor& proc;
    SketchLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };
    juce::SharedResourcePointer<sketch::Fonts> fonts;

    std::unique_ptr<ChoiceSelector> modeSel, eraSel;

    SketchFader timeFader { "Time" }, predelayFader { "Predelay" }, earlyFader { "Early Ref" };
    SketchKnob depth { "Depth", 56 }, rate { "Rate", 56 },
               loCut { "Lo Cut", 56 }, hiCut { "Hi Cut", 56 },
               damping { "Damping", 56 }, width { "Width", 56 },
               mix { "Mix", 56 }, output { "Output", 56 };
    PillToggle soloWet { "Solo Wet", false };

    juce::Rectangle<int> faderBox;
    std::array<Group, 4> groups;

    void drawFader (juce::Graphics&, SketchFader&, const juce::String& label,
                    std::initializer_list<std::pair<float, const char*>> ticks);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GloriaverbEditor)
};
