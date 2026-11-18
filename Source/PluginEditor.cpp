#include "PluginEditor.h"

using namespace sketch;

namespace
{
    constexpr int W = 900, H = 560;
}

GloriaverbEditor::GloriaverbEditor (GloriaverbProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    auto& st = proc.apvts;

    modeSel = std::make_unique<ChoiceSelector> (*st.getParameter (params::id::mode), params::modeNames());
    modeSel->fontSize = 17.0f;
    for (int m = 0; m < gv::numModes; ++m)
        modeSel->setItemEnabled (m, gv::modeAvailable (m), "coming in the next version");
    addAndMakeVisible (*modeSel);

    eraSel = std::make_unique<ChoiceSelector> (*st.getParameter (params::id::era), params::eraNames(), true);
    addAndMakeVisible (*eraSel);

    for (auto* f : { &timeFader, &predelayFader, &earlyFader })
    {
        f->slider.onValueChange = [this] { repaint (faderBox); };
        addAndMakeVisible (*f);
    }
    timeFader.attach (st, params::id::time);
    predelayFader.attach (st, params::id::predelay);
    earlyFader.attach (st, params::id::early);

    depth.attach (st, params::id::modDepth);
    rate.attach (st, params::id::modRate);
    loCut.attach (st, params::id::loCut);
    hiCut.attach (st, params::id::hiCut);
    damping.attach (st, params::id::damping);
    width.attach (st, params::id::width);
    mix.attach (st, params::id::mix);
    output.attach (st, params::id::output);
    loCut.setColour (colour::blue);
    hiCut.setColour (colour::blue);
    loCut.slider.setTooltip ("reverb lo cut, always on (alt-click to reset)");
    hiCut.slider.setTooltip ("reverb hi cut, always on (alt-click to reset)");
    damping.slider.setTooltip ("right: highs last longer, lows shorter; left: the opposite (alt-click to reset)");

    for (auto* k : { &depth, &rate, &loCut, &hiCut, &damping, &width, &mix, &output })
        addAndMakeVisible (*k);

    soloWet.attach (st, params::id::soloWet);
    soloWet.setTooltip ("100% reverb, the Mix knob keeps its setting (alt-click to reset)");
    addAndMakeVisible (soloWet);

    groups[0].title = "Mod";
    groups[1].title = "Filters"; groups[1].tag = "always on"; groups[1].blue = true;
    groups[2].title = "Tail";
    groups[3].title = "Level";

    setSize (W, H);
}

GloriaverbEditor::~GloriaverbEditor()
{
    setLookAndFeel (nullptr);
}

void GloriaverbEditor::resized()
{
    // header
    eraSel->setBounds (W - 22 - 250, 46, 250, 32);
    // modes
    modeSel->setBounds (104, 94, juce::jmin (W - 22 - 104, modeSel->idealWidth()), 38);

    // faders
    faderBox = { 22, 150, 320, H - 150 - 22 };
    const int colW = (faderBox.getWidth() - 20) / 3;
    int x = faderBox.getX() + 10;
    for (auto* f : { &timeFader, &predelayFader, &earlyFader })
    {
        // track sits right of the tick labels, between the label and the value pill
        f->setBounds (x + 44, faderBox.getY() + 40, 36, faderBox.getHeight() - 40 - 50);
        x += colW;
    }

    // knob groups, 2 x 2
    const auto right = juce::Rectangle<int> (faderBox.getRight() + 18, faderBox.getY(),
                                             W - 22 - (faderBox.getRight() + 18), faderBox.getHeight());
    const int gw = (right.getWidth() - 16) / 2, gh = (right.getHeight() - 16) / 2;
    for (int i = 0; i < 4; ++i)
        groups[(size_t) i].area = { right.getX() + (i % 2) * (gw + 16), right.getY() + (i / 2) * (gh + 16), gw, gh };

    const int kh = SketchKnob::heightFor (56), kw = 84;
    auto place2 = [&] (const Group& g, SketchKnob& a, SketchKnob& b)
    {
        const int y = g.area.getY() + 44;
        const int cx = g.area.getCentreX();
        a.setBounds (cx - kw - 14, y, kw, kh);
        b.setBounds (cx + 14, y, kw, kh);
    };
    place2 (groups[0], depth, rate);
    place2 (groups[1], loCut, hiCut);
    place2 (groups[2], damping, width);

    {
        const auto& g = groups[3];
        place2 (g, mix, output);
        soloWet.setBounds (g.area.getCentreX() - 62, mix.getBottom() + 8, 124, 34);
    }
}

void GloriaverbEditor::drawFader (juce::Graphics& g, SketchFader& f, const juce::String& label,
                                  std::initializer_list<std::pair<float, const char*>> ticks)
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

    // value pill
    const auto text = f.valueText();
    const float tw = (float) juce::GlyphArrangement::getStringWidth (fonts->hand (15.0f), text) + 18.0f;
    const auto pill = juce::Rectangle<float> (tw, 24.0f).withCentre ({ (float) cx, (float) faderBox.getBottom() - 26.0f });
    box (g, pill, seedOf (label) + 5, colour::accent, 1.6f);
    g.setColour (colour::knob);
    g.setFont (fonts->hand (15.0f));
    g.drawText (text, pill, juce::Justification::centred, false);
}

void GloriaverbEditor::paint (juce::Graphics& g)
{
    g.fillAll (colour::paper);

    // ---- header ----
    {
        juce::Graphics::ScopedSaveState s (g);
        g.addTransform (juce::AffineTransform::rotation (-0.03f, 30.0f, 50.0f));
        g.setFont (fonts->title (60.0f));
        g.setColour (colour::ink);
        const float gw = (float) juce::GlyphArrangement::getStringWidth (fonts->title (60.0f), "Gloria");
        g.drawText ("Gloria", juce::Rectangle<float> (24.0f, 12.0f, gw + 4.0f, 66.0f), juce::Justification::centredLeft, false);
        g.setColour (colour::accent);
        g.drawText ("verb", juce::Rectangle<float> (24.0f + gw, 12.0f, 200.0f, 66.0f), juce::Justification::centredLeft, false);
    }
    g.setColour (colour::soft);
    g.setFont (fonts->hand (15.0f));
    g.drawText ("Planet Gloria", juce::Rectangle<int> (W - 22 - 250, 16, 250, 22), juce::Justification::centredRight, false);

    g.setColour (colour::ink);
    g.setFont (fonts->title (30.0f));
    g.drawText ("Mode", juce::Rectangle<int> (22, 94, 76, 38), juce::Justification::centredLeft, false);

    // ---- faders ----
    box (g, faderBox.toFloat(), 101, colour::tint.withAlpha (0.45f), 2.0f);
    const bool nonLin = juce::roundToInt (proc.apvts.getRawParameterValue (params::id::mode)->load()) == (int) gv::Mode::nonLin;
    drawFader (g, timeFader, nonLin ? "Time · gate" : "Time",
               { { 60.0f, "60 s" }, { 12.0f, "12 s" }, { 2.4f, "2.4 s" }, { 0.5f, "0.5 s" }, { 0.1f, "0.1 s" } });
    drawFader (g, predelayFader, "Predelay", { { 250.0f, "250 ms" }, { 125.0f, "125 ms" }, { 0.0f, "0 ms" } });
    drawFader (g, earlyFader, "Early Ref", { { 100.0f, "100%" }, { 50.0f, "50%" }, { 0.0f, "0%" } });

    // ---- knob groups ----
    for (const auto& gr : groups)
    {
        const auto r = gr.area.toFloat();
        box (g, r, seedOf (gr.title), gr.blue ? colour::blueTint : colour::knob, 2.0f);
        g.setColour (gr.blue ? colour::blue : colour::ink);
        g.setFont (fonts->title (28.0f));
        g.drawText (gr.title, r.reduced (14.0f, 6.0f).removeFromTop (32.0f), juce::Justification::centredLeft, false);
        if (gr.tag.isNotEmpty())
        {
            g.setFont (fonts->hand (14.0f));
            g.drawText (gr.tag, r.reduced (14.0f, 6.0f).removeFromTop (32.0f), juce::Justification::centredRight, false);
        }
    }

    // Damping direction under its value
    g.setColour (colour::soft);
    g.setFont (fonts->hand (12.0f));
    g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x90 lows \xc2\xb7 highs \xe2\x86\x92")),
                damping.getBounds().translated (0, damping.getHeight() - 2).withHeight (16),
                juce::Justification::centred, false);
}
