// Synoptic.h
// The strip at the top: In -> [three modules] -> Master -> Out.
// Click a module to show its panel, click its round button to turn it on or
// off (alt-click: back to the default), drag it left or right to change the
// order. Cmd / Ctrl are not needed: the order snaps to the three slots.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include "Sketch.h"
#include "Components.h"
#include "../Parameters.h"

class Synoptic : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Synoptic (juce::AudioProcessorValueTreeState& s) : state (s)
    {
        orderParam = s.getParameter (params::id::order);
        onParams = { s.getParameter (params::id::delayOn), s.getParameter (params::id::reverbOn), s.getParameter (params::id::eqOn) };
        setTooltip ("click a module to show it, drag it to change the order");
    }

    std::function<void (int)> onSelect;   // module index 0 Delay, 1 Reverb, 2 EQ
    int selected = 1;

    std::array<int, 3> order() const
    {
        return params::orders()[(size_t) juce::jlimit (0, 5, juce::roundToInt (paramValue (orderParam)))];
    }

    void paint (juce::Graphics& g) override
    {
        using namespace sketch;
        const auto r = getLocalBounds().toFloat();
        box (g, r.reduced (1.0f), 211, colour::tint.withAlpha (0.45f), 2.0f);

        g.setFont (fonts->hand (16.0f));
        g.setColour (colour::soft);
        g.drawText ("In", inArea(), juce::Justification::centred, false);
        g.drawText ("Out", outArea(), juce::Justification::centred, false);

        // arrows between the boxes
        const float cy = r.getCentreY();
        auto arrow = [&] (float x0, float x1, uint32_t seed)
        {
            stroke (g, line ({ x0, cy }, { x1, cy }, seed, 0.3f), 1.8f);
            juce::Path head;
            head.startNewSubPath (x1 - 7.0f, cy - 5.0f); head.lineTo (x1, cy); head.lineTo (x1 - 7.0f, cy + 5.0f);
            stroke (g, head, 1.8f);
        };
        arrow ((float) inArea().getRight() - 4.0f, (float) slotArea (0).getX() - 6.0f, 301);
        for (int i = 0; i < 2; ++i) arrow ((float) slotArea (i).getRight() + 6.0f, (float) slotArea (i + 1).getX() - 6.0f, 302 + (uint32_t) i);
        arrow ((float) slotArea (2).getRight() + 6.0f, (float) masterArea().getX() - 6.0f, 305);
        arrow ((float) masterArea().getRight() + 6.0f, (float) outArea().getX() + 4.0f, 306);

        // Master (fixed, always last)
        {
            const auto m = masterArea().toFloat();
            const float dashes[] = { 5.0f, 4.0f };
            juce::Path dashed;
            juce::PathStrokeType (1.8f).createDashedStroke (dashed, rectStroke (m, 77, 0.6f, 1.0f), dashes, 2);
            g.setColour (colour::ink);
            g.fillPath (dashed);
            g.setFont (fonts->title (26.0f));
            g.drawText ("Master", m, juce::Justification::centred, false);
        }

        const auto o = order();
        for (int slot = 0; slot < 3; ++slot)
            if (! (dragging && o[(size_t) slot] == dragModule))
                drawModule (g, o[(size_t) slot], slotArea (slot).toFloat());
        if (dragging)
            drawModule (g, dragModule, slotArea (dragFromSlot).toFloat().translated ((float) dragDx, -3.0f), true);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = false;
        dragModule = -1;
        const auto o = order();
        for (int slot = 0; slot < 3; ++slot)
            if (slotArea (slot).contains (e.getPosition()))
            {
                dragModule = o[(size_t) slot];
                dragFromSlot = slot;
                if (ledArea (slotArea (slot)).expanded (4).contains (e.getPosition()))
                {
                    auto* p = onParams[(size_t) dragModule];
                    const bool alt = e.mods.isAltDown();
                    setParam (p, alt ? paramDefault (p) : (paramValue (p) > 0.5f ? 0.0f : 1.0f));
                    dragModule = -1;
                    repaint();
                }
                return;
            }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragModule < 0) return;
        if (! dragging && std::abs (e.getDistanceFromDragStartX()) > 5) dragging = true;
        if (dragging) { dragDx = e.getDistanceFromDragStartX(); repaint(); }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragModule < 0) return;
        if (dragging)
        {
            const float centre = (float) slotArea (dragFromSlot).getCentreX() + (float) dragDx;
            int target = 0;
            float best = 1.0e9f;
            for (int slot = 0; slot < 3; ++slot)
            {
                const float d = std::abs ((float) slotArea (slot).getCentreX() - centre);
                if (d < best) { best = d; target = slot; }
            }
            auto o = order();
            if (target != dragFromSlot)
            {
                std::array<int, 3> n {};
                int k = 0;
                std::array<int, 2> rest {};
                for (int s = 0; s < 3; ++s) if (s != dragFromSlot) rest[(size_t) k++] = o[(size_t) s];
                k = 0;
                for (int s = 0; s < 3; ++s) n[(size_t) s] = (s == target) ? dragModule : rest[(size_t) k++];
                setParam (orderParam, (float) params::orderIndex (n));
            }
        }
        else if (onSelect)
        {
            selected = dragModule;
            onSelect (dragModule);
        }
        dragging = false;
        dragModule = -1;
        dragDx = 0;
        juce::ignoreUnused (e);
        repaint();
    }

private:
    juce::Rectangle<int> inArea() const     { return { 10, 0, 34, getHeight() }; }
    juce::Rectangle<int> outArea() const    { return { getWidth() - 46, 0, 36, getHeight() }; }
    juce::Rectangle<int> masterArea() const { return { getWidth() - 46 - 30 - 116, 14, 116, getHeight() - 28 }; }
    juce::Rectangle<int> slotArea (int slot) const
    {
        const int x0 = inArea().getRight() + 30, x1 = masterArea().getX() - 30;
        const int w = (x1 - x0 - 2 * 36) / 3;
        return { x0 + slot * (w + 36), 12, w, getHeight() - 24 };
    }
    static juce::Rectangle<int> ledArea (juce::Rectangle<int> block)
    {
        return { block.getRight() - 42, block.getCentreY() - 15, 30, 30 };
    }

    void drawModule (juce::Graphics& g, int m, juce::Rectangle<float> r, bool lifted = false)
    {
        using namespace sketch;
        const bool sel = (m == selected);
        const bool on = paramValue (onParams[(size_t) m]) > 0.5f;
        if (lifted)
        {
            g.setColour (colour::ink.withAlpha (0.25f));
            g.fillRoundedRectangle (r.translated (4.0f, 5.0f), 6.0f);
        }
        box (g, r, 400 + (uint32_t) m, sel ? colour::accent : colour::knob, 2.0f);
        if (! on)
        {
            g.setColour (colour::paper.withAlpha (0.45f));
            g.fillRoundedRectangle (r.reduced (2.0f), 4.0f);
        }

        const auto txt = sel ? colour::knob : colour::ink;
        g.setColour (txt.withAlpha (on ? 1.0f : 0.6f));
        g.setFont (fonts->hand (18.0f));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x8b\xae\xe2\x8b\xae")), r.withWidth (30.0f).translated (8.0f, 0.0f), juce::Justification::centred, false);
        g.setFont (fonts->title (30.0f));
        g.drawText (params::moduleNames()[m], r.withTrimmedLeft (30.0f).withTrimmedRight (44.0f), juce::Justification::centred, false);

        // on / off button
        const auto led = ledArea (r.toNearestInt()).toFloat();
        const auto circle = ellipse (led, 500 + (uint32_t) m, 0.5f);
        g.setColour (on ? (sel ? colour::knob : colour::ink) : (sel ? colour::accent : colour::knob));
        g.fillPath (circle);
        stroke (g, circle, 1.7f, sel ? colour::knob : colour::ink);
        g.setColour (on ? (sel ? colour::accent : colour::knob) : (sel ? colour::knob : colour::ink));
        g.setFont (fonts->hand (13.0f));
        g.drawText (on ? "on" : "off", led, juce::Justification::centred, false);
    }

    juce::AudioProcessorValueTreeState& state;
    juce::RangedAudioParameter* orderParam = nullptr;
    std::array<juce::RangedAudioParameter*, 3> onParams {};
    juce::SharedResourcePointer<sketch::Fonts> fonts;
    bool dragging = false;
    int dragModule = -1, dragFromSlot = 0, dragDx = 0;
};
