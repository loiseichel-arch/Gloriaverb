#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "dsp/ReverbEngine.h"
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

private:
    gv::Settings readSettings() const;

    struct Raw
    {
        std::atomic<float> *mode, *era, *time, *predelay, *early, *modDepth, *modRate,
                           *loCut, *hiCut, *damping, *width, *mix, *soloWet, *output;
    } raw {};

    gv::ReverbEngine engine;
    juce::AudioBuffer<float> wet;
    juce::SmoothedValue<float> dryGain, wetGain, outGain;
    double sr = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GloriaverbProcessor)
};
