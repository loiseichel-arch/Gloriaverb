#include "PluginProcessor.h"
#include "PluginEditor.h"

GloriaverbProcessor::GloriaverbProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Gloriaverb", params::createLayout())
{
    using namespace params;
    auto p = [this] (const char* pid) { return apvts.getRawParameterValue (pid); };
    raw.mode = p (id::mode);         raw.era = p (id::era);
    raw.time = p (id::time);         raw.predelay = p (id::predelay);
    raw.early = p (id::early);       raw.modDepth = p (id::modDepth);
    raw.modRate = p (id::modRate);   raw.loCut = p (id::loCut);
    raw.hiCut = p (id::hiCut);       raw.damping = p (id::damping);
    raw.width = p (id::width);       raw.mix = p (id::mix);
    raw.soloWet = p (id::soloWet);   raw.output = p (id::output);
}

bool GloriaverbProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    if (in != juce::AudioChannelSet::stereo() && in != juce::AudioChannelSet::mono()) return false;
    return ! (in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::mono());
}

gv::Settings GloriaverbProcessor::readSettings() const
{
    gv::Settings s;
    s.mode       = juce::roundToInt (raw.mode->load());
    s.era        = juce::roundToInt (raw.era->load());
    s.timeS      = raw.time->load();
    s.predelayMs = raw.predelay->load();
    s.early      = raw.early->load() * 0.01f;
    s.modDepth   = raw.modDepth->load() * 0.01f;
    s.modRateHz  = raw.modRate->load();
    s.loCutHz    = raw.loCut->load();
    s.hiCutHz    = raw.hiCut->load();
    s.damping    = raw.damping->load() * 0.01f;
    s.width      = raw.width->load() * 0.01f;
    return s;
}

void GloriaverbProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    wet.setSize (2, samplesPerBlock);

    engine.setSettings (readSettings());
    engine.prepare (sampleRate, samplesPerBlock);

    for (auto* sm : { &dryGain, &wetGain, &outGain }) sm->reset (sampleRate, 0.03);
    const float mix = raw.mix->load() * 0.01f;
    const bool solo = raw.soloWet->load() > 0.5f;
    dryGain.setCurrentAndTargetValue (solo ? 0.0f : std::cos (mix * juce::MathConstants<float>::halfPi));
    wetGain.setCurrentAndTargetValue (solo ? 1.0f : std::sin (mix * juce::MathConstants<float>::halfPi));
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (raw.output->load()));
}

double GloriaverbProcessor::getTailLengthSeconds() const
{
    return (double) raw.time->load() * 1.3 + (double) raw.predelay->load() * 0.001 + 0.2;
}

void GloriaverbProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int inCh = getTotalNumInputChannels(), outCh = getTotalNumOutputChannels();
    if (n == 0) return;
    if (wet.getNumSamples() < n) wet.setSize (2, n, false, false, true);

    engine.setSettings (readSettings());

    // Wet path: always stereo inside (a mono input feeds both sides).
    wet.copyFrom (0, 0, buffer, 0, 0, n);
    wet.copyFrom (1, 0, buffer, inCh > 1 ? 1 : 0, 0, n);
    engine.process (wet.getWritePointer (0), wet.getWritePointer (1), n);

    const float mix = raw.mix->load() * 0.01f;
    const bool solo = raw.soloWet->load() > 0.5f;
    dryGain.setTargetValue (solo ? 0.0f : std::cos (mix * juce::MathConstants<float>::halfPi));
    wetGain.setTargetValue (solo ? 1.0f : std::sin (mix * juce::MathConstants<float>::halfPi));
    outGain.setTargetValue (juce::Decibels::decibelsToGain (raw.output->load()));

    auto* dl = buffer.getWritePointer (0);
    auto* dr = outCh > 1 ? buffer.getWritePointer (1) : nullptr;
    const float* inR = inCh > 1 ? buffer.getReadPointer (1) : buffer.getReadPointer (0);
    const auto* wl = wet.getReadPointer (0);
    const auto* wr = wet.getReadPointer (1);

    for (int i = 0; i < n; ++i)
    {
        const float d = dryGain.getNextValue(), w = wetGain.getNextValue(), o = outGain.getNextValue();
        const float xl = dl[i], xr = inR[i];
        if (dr != nullptr)
        {
            dl[i] = (d * xl + w * wl[i]) * o;
            dr[i] = (d * xr + w * wr[i]) * o;
        }
        else
        {
            dl[i] = (d * xl + w * 0.5f * (wl[i] + wr[i])) * o;
        }
    }
}

juce::AudioProcessorEditor* GloriaverbProcessor::createEditor() { return new GloriaverbEditor (*this); }

void GloriaverbProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void GloriaverbProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new GloriaverbProcessor(); }
