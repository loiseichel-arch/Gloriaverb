#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include "dsp/ReverbEngine.h"
#include "dsp/Modules.h"
#include "Parameters.h"

class GloriaverbProcessor : public juce::AudioProcessor
{
public:
    GloriaverbProcessor();
    ~GloriaverbProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // For the EQ curve in the window (message thread).
    double eqMagnitude (double hz) const;
    double currentSampleRate() const { return sr; }
    double currentBpm() const { return bpm.load(); }

private:
    gv::Settings readReverb() const;
    void readModules();

    struct Raw
    {
        std::atomic<float> *mode, *era, *time, *predelay, *early, *gravity, *boing, *modDepth, *modRate,
                           *filtersOn, *loCut, *hiCut, *damping, *width, *mix, *soloWet,
                           *order, *delayOn, *reverbOn, *eqOn,
                           *dType, *dStereo, *dSync, *dTime, *dNote, *dFeedback, *dMix,
                           *duck, *release, *destroy, *output;
        std::array<std::atomic<float>*, 3> eqFreq {}, eqGain {}, eqQ {};
    } raw {};

    gv::ReverbEngine reverb;
    gv::DelayFx delay;
    gv::Eq3 eq;
    gv::Ducker ducker;
    gv::Destroyer destroyer;
    mutable juce::SpinLock eqLock;
    gv::Eq3 eqView;                  // copy of the EQ settings for drawing

    juce::AudioBuffer<float> work, wet, key, temp;
    juce::SmoothedValue<float> dryGain, wetGain, outGain;
    std::array<float, 3> moduleMix { 0.0f, 1.0f, 0.0f };   // on/off fades of Delay, Reverb, EQ
    float orderFade = 1.0f;
    int currentOrder = 0, pendingOrder = -1;
    double sr = 44100.0;
    std::atomic<double> bpm { 120.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GloriaverbProcessor)
};
