#include "PluginEditor.h"

using namespace sketch;

namespace
{
    constexpr int W = 960, H = 780;
    const juce::Rectangle<int> synArea   { 22, 76, 916, 80 };
    const juce::Rectangle<int> panelArea { 22, 168, 916, 470 };
    const juce::Rectangle<int> masterArea{ 22, 650, 916, 112 };
}

GloriaverbEditor::GloriaverbEditor (GloriaverbProcessor& p)
    : AudioProcessorEditor (p), proc (p), synoptic (p.apvts), presetBar (p.presets),
      eqCurve (p.apvts, [&p] (double hz) { return p.eqMagnitude (hz); })
{
    setLookAndFeel (&lnf);
    auto& st = proc.apvts;
    using namespace params;

    panel = juce::jlimit (0, 2, (int) st.state.getProperty ("panel", 1));
    synoptic.selected = panel;
    synoptic.onSelect = [this] (int m) { showPanel (m); };
    addAndMakeVisible (synoptic);

    onSwitch.attach (st, id::active);
    onSwitch.setTooltip ("plugin on / bypass: off stops everything at once, tails included (alt-click to reset)");
    addAndMakeVisible (onSwitch);
    addAndMakeVisible (presetBar);

    // ---- Reverb ----
    modeSel = std::make_unique<ChoiceSelector> (*st.getParameter (id::mode), modeNames());
    modeSel->fontSize = 16.0f;
    eraSel = std::make_unique<ChoiceSelector> (*st.getParameter (id::era), eraNames(), true);
    addChildComponent (*modeSel);
    addChildComponent (*eraSel);

    for (auto* f : { &timeFader, &predelayFader, &earlyFader, &gravityFader })
    {
        f->slider.onValueChange = [this] { repaint (panelArea); };
        addChildComponent (*f);
    }
    timeFader.attach (st, id::time);
    predelayFader.attach (st, id::predelay);
    earlyFader.attach (st, id::early);
    gravityFader.attach (st, id::gravity);
    gravityFader.slider.setTooltip ("+100: normal decay; going down, the tail swells after each attack (alt-click to reset)");
    // Spring / Vintage Chamber: Time can shorten the recording, not make it longer
    timeFader.slider.onValueChange = [this]
    {
        const int m = juce::roundToInt (value (params::id::mode));
        if (gv::isConvolution (m) && timeFader.slider.isMouseButtonDown())
        {
            const double tn = (double) proc.naturalTime (m);
            if (tn > 0.0 && timeFader.slider.getValue() > tn) timeFader.slider.setValue (tn, juce::sendNotificationSync);
        }
        repaint (panelArea);
    };

    depth.attach (st, id::modDepth);
    rate.attach (st, id::modRate);
    loCut.attach (st, id::loCut);
    hiCut.attach (st, id::hiCut);
    damping.attach (st, id::damping);
    width.attach (st, id::width);
    mix.attach (st, id::mix);
    for (auto* k : { &loCut, &hiCut, &eqLoCut, &eqHiCut }) k->setColour (colour::blue);
    damping.slider.setTooltip ("right: highs last longer, lows shorter; left: the opposite (alt-click to reset)");
    width.slider.setTooltip ("stereo width of the reverb, same loudness at every setting; also spreads the Destroy dropouts (alt-click to reset)");
    for (auto* k : { &depth, &rate, &loCut, &hiCut, &damping, &width, &mix })
        addChildComponent (*k);

    soloWet.attach (st, id::soloWet);
    soloWet.setTooltip ("100% reverb, the Mix knob keeps its setting (alt-click to reset)");
    addChildComponent (soloWet);
    filtersSwitch.attach (st, id::filtersOn);
    filtersSwitch.setTooltip ("Lo Cut and Hi Cut on / off, to compare (alt-click to reset)");
    addChildComponent (filtersSwitch);

    groups[0].title = "Mod";
    groups[1].title = "Filters"; groups[1].blue = true;
    groups[2].title = "Tail";
    groups[3].title = "Level";

    // ---- Delay ----
    dTypeSel   = std::make_unique<ChoiceSelector> (*st.getParameter (id::dType), dTypeNames(), true);
    dStereoSel = std::make_unique<ChoiceSelector> (*st.getParameter (id::dStereo), dStereoNames(), true);
    dSyncSel   = std::make_unique<ChoiceSelector> (*st.getParameter (id::dSync), dSyncNames(), true);
    for (auto* s : { dTypeSel.get(), dStereoSel.get(), dSyncSel.get() }) { s->fontSize = 16.0f; addChildComponent (*s); }
    dTime.attach (st, id::dTime);
    dNote.attach (st, id::dNote);
    dFeedback.attach (st, id::dFeedback);
    dMix.attach (st, id::dMix);
    for (auto* k : { &dTime, &dNote, &dFeedback, &dMix })
    {
        k->slider.onValueChange = [this, k] { k->repaint(); repaint (repeatsBox); };
        addChildComponent (*k);
    }

    // ---- EQ ----
    addChildComponent (eqCurve);
    eqLoCut.attach (st, id::loCut);
    eqHiCut.attach (st, id::hiCut);
    addChildComponent (eqLoCut);
    addChildComponent (eqHiCut);
    const char* names[3] = { "Freq", "Gain", "Q" };
    for (size_t b = 0; b < 3; ++b)
        for (size_t k = 0; k < 3; ++k)
        {
            if (k == 2 && b != 1) continue;   // bands 1 and 3 are shelves: no Q
            auto knob = std::make_unique<SketchKnob> (names[k], 44);
            knob->attach (st, k == 0 ? id::eqFreq[b] : (k == 1 ? id::eqGain[b] : id::eqQ[b]));
            addChildComponent (*knob);
            bandKnobs[b * 3 + k] = std::move (knob);
        }

    // ---- Master ----
    duck.attach (st, id::duck);
    release.attach (st, id::release);
    output.attach (st, id::output);
    destroyFader.attach (st, id::destroy);
    duck.slider.setTooltip ("your playing pushes the reverb down; it comes back when you stop (alt-click to reset)");
    release.slider.setTooltip ("how fast the reverb comes back after Duck (alt-click to reset)");
    destroyFader.slider.setTooltip ("worn cassette dropouts on the whole sound; Width spreads them (alt-click to reset)");
    destroyFader.slider.onValueChange = [this] { repaint (masterArea); };
    for (juce::Component* c : { (juce::Component*) &duck, (juce::Component*) &release, (juce::Component*) &output, (juce::Component*) &destroyFader })
        addAndMakeVisible (c);

    setSize (W, H);
    updateVisibility();
    startTimerHz (20);
}

GloriaverbEditor::~GloriaverbEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void GloriaverbEditor::showPanel (int module)
{
    panel = juce::jlimit (0, 2, module);
    synoptic.selected = panel;
    proc.apvts.state.setProperty ("panel", panel, nullptr);
    updateVisibility();
    repaint();
}

void GloriaverbEditor::updateVisibility()
{
    const bool rv = panel == 1, dl = panel == 0, eqp = panel == 2;
    const int mode = juce::roundToInt (value (params::id::mode));

    for (juce::Component* c : { (juce::Component*) modeSel.get(), (juce::Component*) eraSel.get(),
                                (juce::Component*) &timeFader, (juce::Component*) &predelayFader,
                                (juce::Component*) &depth, (juce::Component*) &rate, (juce::Component*) &loCut,
                                (juce::Component*) &hiCut, (juce::Component*) &damping, (juce::Component*) &width,
                                (juce::Component*) &mix, (juce::Component*) &soloWet, (juce::Component*) &filtersSwitch })
        c->setVisible (rv);
    const bool conv = gv::isConvolution (mode);
    earlyFader.setVisible (rv && mode != (int) gv::Mode::glorious);
    earlyFader.setEnabled (! conv);
    earlyFader.setAlpha (conv ? 0.3f : 1.0f);
    earlyFader.slider.setTooltip (conv ? "no early reflections in the recordings" : "early ref (alt-click to reset)");
    gravityFader.setVisible (rv && mode == (int) gv::Mode::glorious);

    const bool sync = value (params::id::dSync) > 0.5f;
    for (juce::Component* c : { (juce::Component*) dTypeSel.get(), (juce::Component*) dStereoSel.get(),
                                (juce::Component*) dSyncSel.get(), (juce::Component*) &dFeedback, (juce::Component*) &dMix })
        c->setVisible (dl);
    dTime.setVisible (dl && ! sync);
    dNote.setVisible (dl && sync);

    eqCurve.setVisible (eqp);
    eqLoCut.setVisible (eqp);
    eqHiCut.setVisible (eqp);
    for (auto& k : bandKnobs) if (k != nullptr) k->setVisible (eqp);

    const float cutAlpha = value (params::id::filtersOn) > 0.5f ? 1.0f : 0.45f;
    for (auto* k : { &loCut, &hiCut, &eqLoCut, &eqHiCut }) k->setAlpha (cutAlpha);
    const float eqAlpha = value (params::id::eqOn) > 0.5f ? 1.0f : 0.55f;
    for (auto& k : bandKnobs) if (k != nullptr) k->setAlpha (eqAlpha);

    // bypassed: everything but the header fades back
    const bool on = value (params::id::active) > 0.5f;
    if (on != shownOn)
    {
        shownOn = on;
        repaint();
    }
}

void GloriaverbEditor::paintOverChildren (juce::Graphics& g)
{
    // Spring / Vintage Chamber: the part of the Time fader above the recording is out of reach
    const int mode = juce::roundToInt (value (params::id::mode));
    if (panel == 1 && gv::isConvolution (mode))
    {
        const auto b = timeFader.getBounds();
        const float y = (float) b.getY() + (float) timeFader.slider.getPositionOfValue ((double) proc.naturalTime (mode));
        g.setColour (sketch::colour::paper.withAlpha (0.7f));
        g.fillRect (juce::Rectangle<float> ((float) b.getX() - 2.0f, (float) b.getY() - 2.0f, (float) b.getWidth() + 4.0f, y - (float) b.getY() - 6.0f));
    }
    if (shownOn) return;
    g.setColour (sketch::colour::paper.withAlpha (0.6f));
    g.fillRect (getLocalBounds().withTrimmedTop (synArea.getY() - 6));
}

void GloriaverbEditor::timerCallback()
{
    using namespace params;
    double sig = 0.0;
    int k = 1;
    for (const char* pid : { id::active, id::mode, id::filtersOn, id::dSync, id::order, id::delayOn, id::reverbOn, id::eqOn,
                             id::loCut, id::hiCut, id::eqFreq[0], id::eqFreq[1], id::eqFreq[2],
                             id::eqGain[0], id::eqGain[1], id::eqGain[2], id::eqQ[0], id::eqQ[1], id::eqQ[2],
                             id::dType, id::dStereo, id::dNote })
        sig += (double) value (pid) * (double) (k++ * 7919 % 1013 + 1);
    if (sig < lastSignature || sig > lastSignature)
    {
        lastSignature = sig;
        updateVisibility();
        synoptic.repaint();
        repaint (panelArea);
    }
}

void GloriaverbEditor::resized()
{
    synoptic.setBounds (synArea);
    onSwitch.setBounds (W - 22 - 80, 38, 80, 30);
    presetBar.setBounds (318, 24, 470, 32);

    // ---- Reverb ----
    const int px = panelArea.getX() + 16, py = panelArea.getY() + 12;
    modeSel->setBounds (px + 74, py, juce::jmin (620, modeSel->idealWidth()), 36);
    eraSel->setBounds (panelArea.getRight() - 16 - 230, py + 2, 230, 32);

    faderBox = { px, py + 50, 300, panelArea.getBottom() - 12 - (py + 50) };
    const int colW = (faderBox.getWidth() - 20) / 3;
    int x = faderBox.getX() + 10;
    for (auto* f : { &timeFader, &predelayFader, &earlyFader })
    {
        f->setBounds (x + 44, faderBox.getY() + 40, 36, faderBox.getHeight() - 40 - 50);
        x += colW;
    }
    gravityFader.setBounds (earlyFader.getBounds());

    const auto right = juce::Rectangle<int> (faderBox.getRight() + 16, faderBox.getY(),
                                             panelArea.getRight() - 16 - (faderBox.getRight() + 16), faderBox.getHeight());
    const int gw = (right.getWidth() - 14) / 2, gh = (right.getHeight() - 14) / 2;
    for (int i = 0; i < 4; ++i)
        groups[(size_t) i].area = { right.getX() + (i % 2) * (gw + 14), right.getY() + (i / 2) * (gh + 14), gw, gh };

    const int kh = SketchKnob::heightFor (56), kw = 84;
    auto place2 = [&] (const Group& g, SketchKnob& a, SketchKnob& b)
    {
        const int y = g.area.getY() + 48, cx = g.area.getCentreX();
        a.setBounds (cx - kw - 16, y, kw, kh);
        b.setBounds (cx + 16, y, kw, kh);
    };
    place2 (groups[0], depth, rate);
    place2 (groups[1], loCut, hiCut);
    place2 (groups[2], damping, width);
    {
        const auto& g = groups[3];
        mix.setBounds (g.area.getCentreX() - kw - 16, g.area.getY() + 48, kw, kh);
        soloWet.setBounds (g.area.getCentreX() + 6, g.area.getY() + 48 + 22, 126, 36);
    }
    filtersSwitch.setBounds (groups[1].area.getRight() - 64, groups[1].area.getY() + 10, 52, 26);

    // ---- Delay ----
    {
        int sx = panelArea.getRight() - 16;
        for (auto* s : { dSyncSel.get(), dStereoSel.get(), dTypeSel.get() })
        {
            const int w = s->idealWidth();
            sx -= w;
            s->setBounds (sx, py + 2, w, 32);
            sx -= 18;
        }
        delayKnobBox = { px, py + 54, 440, panelArea.getBottom() - 12 - (py + 54) };
        repeatsBox = { delayKnobBox.getRight() + 16, delayKnobBox.getY(), panelArea.getRight() - 16 - (delayKnobBox.getRight() + 16), delayKnobBox.getHeight() };
        const int cy = delayKnobBox.getCentreY();
        dTime.setBounds (delayKnobBox.getX() + 24, cy - SketchKnob::heightFor (80) / 2, 120, SketchKnob::heightFor (80));
        dNote.setBounds (dTime.getBounds());
        dFeedback.setBounds (delayKnobBox.getX() + 170, cy - SketchKnob::heightFor (64) / 2 + 8, 110, SketchKnob::heightFor (64));
        dMix.setBounds (delayKnobBox.getX() + 300, cy - SketchKnob::heightFor (64) / 2 + 8, 110, SketchKnob::heightFor (64));
    }

    // ---- EQ ----
    {
        eqCurve.setBounds (px, py + 46, panelArea.getWidth() - 32, 236);
        const int top = eqCurve.getBottom() + 12, h = panelArea.getBottom() - 12 - top;
        const int total = panelArea.getWidth() - 32, cutW = 104, gap = 10;
        const int bw = (total - 2 * cutW - 4 * gap) / 3;
        int cx = px;
        for (int c = 0; c < 5; ++c)
        {
            const int w = (c == 0 || c == 4) ? cutW : bw;
            eqCols[(size_t) c] = { cx, top, w, h };
            cx += w + gap;
        }
        const int kh2 = SketchKnob::heightFor (44);
        eqLoCut.setBounds (eqCols[0].getCentreX() - 35, eqCols[0].getY() + 34, 70, kh2);
        eqHiCut.setBounds (eqCols[4].getCentreX() - 35, eqCols[4].getY() + 34, 70, kh2);
        for (int b = 0; b < 3; ++b)
        {
            const auto col = eqCols[(size_t) b + 1];
            const int count = b == 1 ? 3 : 2;   // shelves: Freq and Gain only
            const int kw2 = (col.getWidth() - 8) / 3;
            const int x0 = col.getX() + (col.getWidth() - count * kw2) / 2;
            for (int k = 0; k < count; ++k)
                bandKnobs[(size_t) (b * 3 + k)]->setBounds (x0 + k * kw2, col.getY() + 34, kw2, kh2);
        }
    }

    // ---- Master ----
    {
        const int my = masterArea.getY(), kh3 = SketchKnob::heightFor (52);
        duck.setBounds (masterArea.getX() + 150, my + (masterArea.getHeight() - kh3) / 2, 80, kh3);
        release.setBounds (masterArea.getX() + 236, my + (masterArea.getHeight() - kh3) / 2, 80, kh3);
        output.setBounds (masterArea.getRight() - 16 - 84, my + (masterArea.getHeight() - kh3) / 2, 84, kh3);
        destroyFader.setBounds (masterArea.getX() + 352, my + 50, output.getX() - 40 - (masterArea.getX() + 352), 40);
    }
}

// ---------------------------------------------------------------------------

void GloriaverbEditor::valuePill (juce::Graphics& g, const juce::String& text, juce::Point<float> centre, uint32_t seed)
{
    const float tw = (float) juce::GlyphArrangement::getStringWidth (fonts->hand (15.0f), text) + 18.0f;
    const auto pill = juce::Rectangle<float> (tw, 24.0f).withCentre (centre);
    box (g, pill, seed, colour::accent, 1.6f);
    g.setColour (colour::knob);
    g.setFont (fonts->hand (15.0f));
    g.drawText (text, pill, juce::Justification::centred, false);
}

void GloriaverbEditor::drawFader (juce::Graphics& g, SketchFader& f, const juce::String& label,
                                  std::initializer_list<std::pair<float, const char*>> ticks, const juce::String& shown)
{
    const auto b = f.getBounds();
    const int cx = b.getCentreX();

    g.setColour (colour::ink);
    g.setFont (fonts->hand (17.0f));
    g.drawText (label, juce::Rectangle<int> (cx - 60, faderBox.getY() + 10, 120, 22), juce::Justification::centred, false);

    g.setColour (colour::soft);
    g.setFont (fonts->hand (13.0f));
    for (const auto& t : ticks)
    {
        const float y = (float) b.getY() + (float) f.slider.getPositionOfValue ((double) t.first);
        g.drawText (t.second, juce::Rectangle<float> ((float) b.getX() - 50.0f, y - 8.0f, 46.0f, 16.0f),
                    juce::Justification::centredRight, false);
    }
    valuePill (g, shown.isNotEmpty() ? shown : f.valueText(), { (float) cx, (float) faderBox.getBottom() - 26.0f }, seedOf (label) + 5);
}

void GloriaverbEditor::paint (juce::Graphics& g)
{
    g.fillAll (colour::paper);

    // ---- header ----
    {
        juce::Graphics::ScopedSaveState s (g);
        g.addTransform (juce::AffineTransform::rotation (-0.03f, 30.0f, 40.0f));
        g.setFont (fonts->title (58.0f));
        g.setColour (colour::ink);
        const float gw = (float) juce::GlyphArrangement::getStringWidth (fonts->title (58.0f), "Gloria");
        g.drawText ("Gloria", juce::Rectangle<float> (24.0f, 6.0f, gw + 4.0f, 62.0f), juce::Justification::centredLeft, false);
        g.setColour (colour::accent);
        g.drawText ("verb", juce::Rectangle<float> (24.0f + gw, 6.0f, 200.0f, 62.0f), juce::Justification::centredLeft, false);
    }
    g.setColour (colour::soft);
    g.setFont (fonts->hand (15.0f));
    g.drawText ("Planet Gloria", juce::Rectangle<int> (W - 22 - 250, 12, 250, 22), juce::Justification::centredRight, false);

    // ---- panel ----
    box (g, panelArea.toFloat(), 151, colour::knob, 2.0f);
    g.setColour (colour::ink.withAlpha (0.9f));
    g.fillRect (juce::Rectangle<float> (3.0f, (float) panelArea.getHeight() - 8.0f).withPosition ((float) panelArea.getRight() + 1.0f, (float) panelArea.getY() + 5.0f));
    g.fillRect (juce::Rectangle<float> ((float) panelArea.getWidth() - 8.0f, 3.0f).withPosition ((float) panelArea.getX() + 5.0f, (float) panelArea.getBottom() + 1.0f));

    if (panel == 1) paintReverb (g);
    else if (panel == 0) paintDelay (g);
    else paintEq (g);

    paintMaster (g);
}

void GloriaverbEditor::paintReverb (juce::Graphics& g)
{
    const int px = panelArea.getX() + 16, py = panelArea.getY() + 12;
    g.setColour (colour::ink);
    g.setFont (fonts->title (30.0f));
    g.drawText ("Mode", juce::Rectangle<int> (px, py, 72, 36), juce::Justification::centredLeft, false);

    box (g, faderBox.toFloat(), 101, colour::tint.withAlpha (0.45f), 2.0f);
    const int mode = juce::roundToInt (value (params::id::mode));
    if (gv::isConvolution (mode))
    {
        // the recording sets the longest Time: shown as "max"
        const float tn = proc.naturalTime (mode);
        drawFader (g, timeFader, "Time", { { tn, "max" }, { 0.5f, "0.5 s" }, { 0.1f, "0.1 s" } },
                   params::fmtTime (juce::jmin ((float) timeFader.slider.getValue(), tn), 0));
    }
    else
        drawFader (g, timeFader, mode == (int) gv::Mode::nonLin ? juce::String (juce::CharPointer_UTF8 ("Time \xc2\xb7 gate")) : "Time",
                   { { 60.0f, "60 s" }, { 12.0f, "12 s" }, { 2.4f, "2.4 s" }, { 0.5f, "0.5 s" }, { 0.1f, "0.1 s" } });
    drawFader (g, predelayFader, "Predelay", { { 250.0f, "250 ms" }, { 125.0f, "125 ms" }, { 0.0f, "0 ms" } });
    if (mode == (int) gv::Mode::glorious)
        drawFader (g, gravityFader, "Gravity", { { 100.0f, "+100" }, { 0.0f, "0" }, { -100.0f, "-100" } });
    else
        drawFader (g, earlyFader, "Early Ref", { { 100.0f, "100%" }, { 50.0f, "50%" }, { 0.0f, "0%" } },
                   gv::isConvolution (mode) ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")) : juce::String());

    for (const auto& gr : groups)
    {
        const auto r = gr.area.toFloat();
        box (g, r, seedOf (gr.title), gr.blue ? colour::blueTint : colour::paper, 2.0f);
        g.setColour (gr.blue ? colour::blue : colour::ink);
        g.setFont (fonts->title (28.0f));
        g.drawText (gr.title, r.reduced (14.0f, 6.0f).removeFromTop (32.0f), juce::Justification::centredLeft, false);
    }
    // Filters switch label
    g.setColour (colour::blue);
    g.setFont (fonts->hand (14.0f));
    g.drawText (value (params::id::filtersOn) > 0.5f ? "on" : "off",
                filtersSwitch.getBounds().translated (-34, 0).withWidth (30), juce::Justification::centredRight, false);

    g.setColour (colour::soft);
    g.setFont (fonts->hand (12.0f));
    g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x90 lows \xc2\xb7 highs \xe2\x86\x92")),
                damping.getBounds().translated (0, damping.getHeight() - 2).withHeight (16), juce::Justification::centred, false);
}

void GloriaverbEditor::paintDelay (juce::Graphics& g)
{
    const int px = panelArea.getX() + 16, py = panelArea.getY() + 12;
    g.setColour (colour::ink);
    g.setFont (fonts->title (34.0f));
    g.drawText ("Delay", juce::Rectangle<int> (px, py - 2, 120, 40), juce::Justification::centredLeft, false);

    box (g, delayKnobBox.toFloat(), 701, colour::tint.withAlpha (0.45f), 2.0f);

    // repeats sketch
    const auto r = repeatsBox.toFloat();
    {
        const float dashes[] = { 5.0f, 4.0f };
        juce::Path dashed;
        juce::PathStrokeType (1.6f).createDashedStroke (dashed, rectStroke (r, 702, 0.5f, 1.0f), dashes, 2);
        g.setColour (colour::ink.withAlpha (0.5f));
        g.fillPath (dashed);
    }
    const bool tape = value (params::id::dType) > 0.5f;
    const int stereo = juce::roundToInt (value (params::id::dStereo));
    const float fb = value (params::id::dFeedback) * 0.01f;
    g.setColour (colour::soft);
    g.setFont (fonts->hand (14.0f));
    g.drawText ("Repeats", r.reduced (14.0f, 8.0f).removeFromTop (20.0f), juce::Justification::centredLeft, false);

    const auto plot = r.reduced (24.0f, 40.0f).withTrimmedBottom (26.0f);
    const float base = plot.getBottom(), step = plot.getWidth() / 9.0f;
    stroke (g, line ({ plot.getX(), base }, { plot.getRight(), base }, 703, 0.3f), 1.8f);
    for (int k = 0; k < 9; ++k)
    {
        const float h = k == 0 ? 1.0f : std::pow (fb, (float) (k - 1)) * (stereo == 2 && k > 0 ? 0.85f : 0.75f);
        const float x = plot.getX() + step * ((float) k + 0.5f);
        // ping-pong: alternate left (up) / right (shifted)
        const float off = (stereo == 2 && k > 0) ? ((k % 2) ? -8.0f : 8.0f) : (stereo == 1 && k > 0 ? 5.0f : 0.0f);
        const auto bar = juce::Rectangle<float> (10.0f, juce::jmax (3.0f, h * plot.getHeight())).withBottomY (base).withCentre ({ x + off, 0.0f }).withBottomY (base);
        g.setColour ((k == 0 ? colour::ink : colour::accent).withAlpha (tape ? juce::jmax (0.35f, 1.0f - 0.08f * (float) k) : 1.0f));
        g.fillRoundedRectangle (bar, 3.0f);
        if (stereo == 2 && k > 0)
        {
            g.setColour (colour::soft);
            g.setFont (fonts->hand (12.0f));
            g.drawText ((k % 2) ? "L" : "R", juce::Rectangle<float> (20.0f, 14.0f).withCentre ({ x + off, base + 10.0f }), juce::Justification::centred, false);
        }
    }
    const juce::String typeNote = tape ? "Tape: repeats get darker, slightly saturated and a little wobbly."
                                       : "Digital: clean, identical repeats.";
    const juce::String stNote = stereo == 0 ? " Mono: same repeats on both sides."
                              : (stereo == 1 ? " Wide: right repeats slightly later." : " Ping-pong: repeats bounce left / right.");
    g.setColour (colour::soft);
    g.setFont (fonts->hand (14.0f));
    g.drawFittedText (typeNote + stNote, r.reduced (14.0f, 8.0f).removeFromBottom (40.0f).toNearestInt(), juce::Justification::centredLeft, 2);
}

void GloriaverbEditor::paintEq (juce::Graphics& g)
{
    const int px = panelArea.getX() + 16, py = panelArea.getY() + 12;
    g.setColour (colour::ink);
    g.setFont (fonts->title (34.0f));
    g.drawText ("EQ", juce::Rectangle<int> (px, py - 2, 80, 40), juce::Justification::centredLeft, false);

    // legend
    g.setFont (fonts->hand (14.0f));
    const int ly = py + 18;
    stroke (g, line ({ (float) px + 470.0f, (float) ly }, { (float) px + 496.0f, (float) ly }, 801, 0.2f), 3.0f, colour::accent);
    g.setColour (colour::ink);
    g.drawText ("EQ bands", juce::Rectangle<int> (px + 502, ly - 9, 80, 18), juce::Justification::centredLeft, false);
    {
        juce::Path pth; pth.startNewSubPath ((float) px + 600.0f, (float) ly); pth.lineTo ((float) px + 626.0f, (float) ly);
        juce::Path dashed; const float dashes[] = { 6.0f, 4.0f };
        juce::PathStrokeType (3.0f).createDashedStroke (dashed, pth, dashes, 2);
        g.setColour (colour::blue); g.fillPath (dashed);
    }
    g.setColour (colour::blue);
    g.drawText ("Reverb Lo / Hi Cut (Filters switch)", juce::Rectangle<int> (px + 632, ly - 9, 260, 18), juce::Justification::centredLeft, false);

    const char* titles[5] = { "Lo Cut", "Band 1", "Band 2", "Band 3", "Hi Cut" };
    for (int c = 0; c < 5; ++c)
    {
        const bool blue = c == 0 || c == 4;
        const auto r = eqCols[(size_t) c].toFloat();
        box (g, r, 820 + (uint32_t) c, blue ? colour::blueTint : colour::paper, 2.0f);
        g.setColour (blue ? colour::blue : colour::ink);
        g.setFont (fonts->title (24.0f));
        g.drawText (titles[c], r.reduced (8.0f, 4.0f).removeFromTop (30.0f), juce::Justification::centred, false);
    }
}

void GloriaverbEditor::paintMaster (juce::Graphics& g)
{
    box (g, masterArea.toFloat(), 901, colour::tint.withAlpha (0.45f), 2.0f);
    g.setColour (colour::ink);
    g.setFont (fonts->title (32.0f));
    g.drawText ("Master", juce::Rectangle<int> (masterArea.getX() + 18, masterArea.getY(), 120, masterArea.getHeight()), juce::Justification::centredLeft, false);

    const auto f = destroyFader.getBounds();
    g.setColour (colour::accent);
    g.setFont (fonts->title (32.0f));
    g.drawText ("Destroy", juce::Rectangle<int> (f.getX() - 4, masterArea.getY() + 8, 160, 36), juce::Justification::centredLeft, false);
    valuePill (g, destroyFader.valueText(), { (float) f.getRight() - 26.0f, (float) masterArea.getY() + 26.0f }, 902);
    g.setColour (colour::soft);
    g.setFont (fonts->hand (12.0f));
    const auto lab = juce::Rectangle<int> (f.getX(), f.getBottom() - 2, f.getWidth(), 16);
    g.drawText ("0", lab, juce::Justification::centredLeft, false);
    g.drawText ("worn", lab, juce::Justification::centred, false);
    g.drawText ("dying tape", lab, juce::Justification::centredRight, false);
}
