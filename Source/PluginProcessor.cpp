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
    raw.mode = p (id::mode);           raw.era = p (id::era);
    raw.time = p (id::time);           raw.predelay = p (id::predelay);
    raw.early = p (id::early);         raw.gravity = p (id::gravity);
    raw.boing = p (id::boing);         raw.modDepth = p (id::modDepth);
    raw.modRate = p (id::modRate);     raw.filtersOn = p (id::filtersOn);
    raw.loCut = p (id::loCut);         raw.hiCut = p (id::hiCut);
    raw.damping = p (id::damping);     raw.width = p (id::width);
    raw.mix = p (id::mix);             raw.soloWet = p (id::soloWet);
    raw.order = p (id::order);         raw.delayOn = p (id::delayOn);
    raw.reverbOn = p (id::reverbOn);   raw.eqOn = p (id::eqOn);
    raw.dType = p (id::dType);         raw.dStereo = p (id::dStereo);
    raw.dSync = p (id::dSync);         raw.dTime = p (id::dTime);
    raw.dNote = p (id::dNote);         raw.dFeedback = p (id::dFeedback);
    raw.dMix = p (id::dMix);           raw.duck = p (id::duck);
    raw.release = p (id::release);     raw.destroy = p (id::destroy);
    raw.output = p (id::output);
    for (size_t b = 0; b < 3; ++b)
    {
        raw.eqFreq[b] = p (id::eqFreq[b]);
        raw.eqGain[b] = p (id::eqGain[b]);
        raw.eqQ[b]    = p (id::eqQ[b]);
    }
    eqView.prepare (48000.0);
}

bool GloriaverbProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    if (in != juce::AudioChannelSet::stereo() && in != juce::AudioChannelSet::mono()) return false;
    return ! (in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::mono());
}

gv::Settings GloriaverbProcessor::readReverb() const
{
    gv::Settings s;
    s.mode       = juce::roundToInt (raw.mode->load());
    s.era        = juce::roundToInt (raw.era->load());
    s.timeS      = raw.time->load();
    s.predelayMs = raw.predelay->load();
    s.early      = raw.early->load() * 0.01f;
    s.gravity    = raw.gravity->load() * 0.01f;
    s.boing      = raw.boing->load() * 0.01f;
    s.modDepth   = raw.modDepth->load() * 0.01f;
    s.modRateHz  = raw.modRate->load();
    s.filtersOn  = raw.filtersOn->load() > 0.5f;
    s.loCutHz    = raw.loCut->load();
    s.hiCutHz    = raw.hiCut->load();
    s.damping    = raw.damping->load() * 0.01f;
    s.width      = raw.width->load() * 0.01f;
    return s;
}

void GloriaverbProcessor::readModules()
{
    gv::DelayFx::Params d;
    d.type     = juce::roundToInt (raw.dType->load());
    d.stereo   = juce::roundToInt (raw.dStereo->load());
    d.sync     = raw.dSync->load() > 0.5f;
    d.timeMs   = raw.dTime->load();
    d.note     = juce::roundToInt (raw.dNote->load());
    d.feedback = raw.dFeedback->load() * 0.01f;
    d.mix      = raw.dMix->load() * 0.01f;
    delay.set (d, bpm.load());

    gv::Eq3::Params e;
    for (size_t b = 0; b < 3; ++b)
    {
        e.freq[b]   = raw.eqFreq[b]->load();
        e.gainDb[b] = raw.eqGain[b]->load();
        e.q[b]      = raw.eqQ[b]->load();
    }
    eq.set (e);
    {
        const juce::SpinLock::ScopedTryLockType lock (eqLock);
        if (lock.isLocked()) eqView.set (e);
    }

    ducker.set (raw.duck->load() * 0.01f, raw.release->load());
    destroyer.set (raw.destroy->load() * 0.01f, raw.width->load() * 0.01f);
}

double GloriaverbProcessor::eqMagnitude (double hz) const
{
    const juce::SpinLock::ScopedLockType lock (eqLock);
    return eqView.magnitude (hz);
}

void GloriaverbProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    for (auto* b : { &work, &wet, &key, &temp }) b->setSize (2, samplesPerBlock);

    reverb.setSettings (readReverb());
    reverb.prepare (sampleRate, samplesPerBlock);
    delay.prepare (sampleRate);
    eq.prepare (sampleRate);
    {
        const juce::SpinLock::ScopedLockType lock (eqLock);
        eqView.prepare (sampleRate);
    }
    ducker.prepare (sampleRate);
    destroyer.prepare (sampleRate);
    readModules();
    delay.reset();

    for (auto* sm : { &dryGain, &wetGain, &outGain }) sm->reset (sampleRate, 0.03);
    const float mix = raw.mix->load() * 0.01f;
    const bool solo = raw.soloWet->load() > 0.5f;
    dryGain.setCurrentAndTargetValue (solo ? 0.0f : gv::mixDryGain (mix));
    wetGain.setCurrentAndTargetValue (solo ? 1.0f : gv::mixWetGain (mix));
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (raw.output->load()));

    moduleMix = { raw.delayOn->load() > 0.5f ? 1.0f : 0.0f,
                  raw.reverbOn->load() > 0.5f ? 1.0f : 0.0f,
                  raw.eqOn->load() > 0.5f ? 1.0f : 0.0f };
    currentOrder = juce::jlimit (0, 5, juce::roundToInt (raw.order->load()));
    pendingOrder = -1;
    orderFade = 1.0f;
}

double GloriaverbProcessor::getTailLengthSeconds() const
{
    double t = (double) raw.time->load() * 1.3 + (double) raw.predelay->load() * 0.001 + 0.2;
    if (raw.delayOn->load() > 0.5f) t += 8.0;
    return juce::jmin (90.0, t);
}

void GloriaverbProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int inCh = getTotalNumInputChannels(), outCh = getTotalNumOutputChannels();
    if (n == 0) return;
    if (work.getNumSamples() < n)
        for (auto* b : { &work, &wet, &key, &temp }) b->setSize (2, n, false, false, true);

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) bpm.store (*b);

    reverb.setSettings (readReverb());
    readModules();

    // stereo inside (a mono input feeds both sides)
    for (int c = 0; c < 2; ++c)
    {
        work.copyFrom (c, 0, buffer, (c == 1 && inCh > 1) ? 1 : 0, 0, n);
        key.copyFrom (c, 0, work, c, 0, n);   // clean input: drives the Duck
    }

    const float mix = raw.mix->load() * 0.01f;
    const bool solo = raw.soloWet->load() > 0.5f;
    dryGain.setTargetValue (solo ? 0.0f : gv::mixDryGain (mix));
    wetGain.setTargetValue (solo ? 1.0f : gv::mixWetGain (mix));
    outGain.setTargetValue (juce::Decibels::decibelsToGain (raw.output->load()));

    // order change: fade the output out, swap, fade back in (10 ms each)
    const int wantedOrder = juce::jlimit (0, 5, juce::roundToInt (raw.order->load()));
    if (wantedOrder != currentOrder && pendingOrder < 0) pendingOrder = wantedOrder;

    const bool onTarget[3] = { raw.delayOn->load() > 0.5f, raw.reverbOn->load() > 0.5f, raw.eqOn->load() > 0.5f };
    const float fadeStep = (float) (1.0 / (0.010 * sr));

    auto* L = work.getWritePointer (0);
    auto* R = work.getWritePointer (1);

    for (int slot = 0; slot < 3; ++slot)
    {
        const int m = params::orders()[(size_t) currentOrder][(size_t) slot];
        float& fm = moduleMix[(size_t) m];
        const float target = onTarget[m] ? 1.0f : 0.0f;
        if (fm <= 0.0f && target <= 0.0f) continue;   // off and faded out: skip

        // processed copy in temp, then crossfade with the input of the module
        temp.copyFrom (0, 0, work, 0, 0, n);
        temp.copyFrom (1, 0, work, 1, 0, n);
        auto* tl = temp.getWritePointer (0);
        auto* tr = temp.getWritePointer (1);

        if (m == 0)
        {
            delay.process (tl, tr, n);
        }
        else if (m == 1)
        {
            wet.copyFrom (0, 0, temp, 0, 0, n);
            wet.copyFrom (1, 0, temp, 1, 0, n);
            auto* wl = wet.getWritePointer (0);
            auto* wr = wet.getWritePointer (1);
            reverb.process (wl, wr, n);
            ducker.process (key.getReadPointer (0), key.getReadPointer (1), wl, wr, n);
            for (int i = 0; i < n; ++i)
            {
                const float d = dryGain.getNextValue(), w = wetGain.getNextValue();
                tl[i] = d * tl[i] + w * wl[i];
                tr[i] = d * tr[i] + w * wr[i];
            }
        }
        else
        {
            eq.process (tl, tr, n);
        }

        for (int i = 0; i < n; ++i)
        {
            if (fm < target || fm > target) fm = target > fm ? juce::jmin (target, fm + fadeStep) : juce::jmax (target, fm - fadeStep);
            L[i] += fm * (tl[i] - L[i]);
            R[i] += fm * (tr[i] - R[i]);
        }
        if (m == 1 && fm <= 0.0f) reverb.reset();   // the tail is gone: start clean next time
        if (m == 0 && fm <= 0.0f) delay.reset();
    }
    if (moduleMix[1] <= 0.0f) { dryGain.skip (n); wetGain.skip (n); }

    // ---- Master: Destroy, Output ----
    destroyer.process (L, R, n);

    auto* outL = buffer.getWritePointer (0);
    auto* outR = outCh > 1 ? buffer.getWritePointer (1) : nullptr;
    for (int i = 0; i < n; ++i)
    {
        if (pendingOrder >= 0)
        {
            orderFade -= fadeStep;
            if (orderFade <= 0.0f) { orderFade = 0.0f; currentOrder = pendingOrder; pendingOrder = -1; }
        }
        else if (orderFade < 1.0f) orderFade = juce::jmin (1.0f, orderFade + fadeStep);

        const float g = outGain.getNextValue() * orderFade;
        if (outR != nullptr) { outL[i] = L[i] * g; outR[i] = R[i] * g; }
        else                 { outL[i] = 0.5f * (L[i] + R[i]) * g; }
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
