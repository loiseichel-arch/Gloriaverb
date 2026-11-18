// EqCurve.h
// The EQ panel's curve: the three bands in brick red (grey when the EQ is
// off), the reverb Lo / Hi Cut as a dashed blue line (grey when the Filters
// switch is off). Drag a numbered point: frequency and gain of that band;
// drag a small blue square: the Lo / Hi Cut. Alt-click: back to the default.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include "Sketch.h"
#include "Components.h"
#include "../Parameters.h"

class EqCurve : public juce::Component, public juce::SettableTooltipClient
{
public:
    EqCurve (juce::AudioProcessorValueTreeState& s, std::function<double (double)> eqMagnitude)
        : magnitude (std::move (eqMagnitude))
    {
        for (size_t b = 0; b < 3; ++b)
        {
            freq[b] = s.getParameter (params::id::eqFreq[b]);
            gain[b] = s.getParameter (params::id::eqGain[b]);
        }
        loCut = s.getParameter (params::id::loCut);
        hiCut = s.getParameter (params::id::hiCut);
        eqOn = s.getParameter (params::id::eqOn);
        filtersOn = s.getParameter (params::id::filtersOn);
        setTooltip ("drag a point: frequency and gain of a band; drag a blue square: reverb lo / hi cut (alt-click to reset)");
    }

    void paint (juce::Graphics& g) override
    {
        using namespace sketch;
        const auto r = getLocalBounds().toFloat();
        g.setColour (colour::paper);
        g.fillRoundedRectangle (r.reduced (1.0f), 6.0f);
        stroke (g, rectStroke (r.reduced (1.0f), 611, 0.6f, 1.0f), 2.0f);

        // grid
        g.setFont (fonts->hand (12.0f));
        for (float f : { 100.0f, 1000.0f, 10000.0f })
        {
            const float x = xOf (f);
            g.setColour (colour::ink.withAlpha (0.18f));
            g.drawVerticalLine ((int) x, r.getY() + 4.0f, r.getBottom() - 4.0f);
            g.setColour (colour::soft);
            g.drawText (params::fmtHzValue (f), juce::Rectangle<float> (x + 4.0f, r.getBottom() - 18.0f, 60.0f, 14.0f), juce::Justification::centredLeft, false);
        }
        for (float db : { -12.0f, 0.0f, 12.0f })
        {
            const float y = yOf (db);
            g.setColour (colour::ink.withAlpha (std::abs (db) < 0.1f ? 0.35f : 0.15f));
            g.drawHorizontalLine ((int) y, r.getX() + 4.0f, r.getRight() - 4.0f);
            g.setColour (colour::soft);
            g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db) + " dB", juce::Rectangle<float> (r.getX() + 6.0f, y - 15.0f, 50.0f, 14.0f), juce::Justification::centredLeft, false);
        }

        const bool eqActive = paramValue (eqOn) > 0.5f, cutActive = paramValue (filtersOn) > 0.5f;

        // reverb Lo / Hi Cut (12 dB/oct)
        {
            const float lo = paramValue (loCut), hi = paramValue (hiCut);
            juce::Path p;
            for (int i = 0; i <= 200; ++i)
            {
                const float f = 20.0f * std::pow (1000.0f, (float) i / 200.0f);
                double m = 1.0;
                if (lo > 20.5f) m /= std::sqrt (1.0 + std::pow (lo / f, 4.0));
                if (hi < 19900.0f) m /= std::sqrt (1.0 + std::pow (f / hi, 4.0));
                const float y = yOf ((float) juce::jmax (-30.0, 20.0 * std::log10 (m)));
                if (i == 0) p.startNewSubPath (xOf (f), y); else p.lineTo (xOf (f), y);
            }
            juce::Path dashed;
            const float dashes[] = { 8.0f, 6.0f };
            juce::PathStrokeType (2.6f).createDashedStroke (dashed, p, dashes, 2);
            g.setColour (cutActive ? colour::blue : colour::soft.withAlpha (0.5f));
            g.fillPath (dashed);
        }

        // the three bands
        {
            juce::Path p;
            for (int i = 0; i <= 240; ++i)
            {
                const float f = 20.0f * std::pow (1000.0f, (float) i / 240.0f);
                const float y = yOf ((float) (20.0 * std::log10 (juce::jmax (1.0e-6, magnitude ((double) f)))));
                if (i == 0) p.startNewSubPath (xOf (f), y); else p.lineTo (xOf (f), y);
            }
            stroke (g, p, 3.2f, eqActive ? colour::accent : colour::soft.withAlpha (0.6f));
        }

        // handles
        for (int b = 0; b < 3; ++b)
        {
            const auto c = bandPoint (b);
            const auto e = ellipse (juce::Rectangle<float> (26.0f, 26.0f).withCentre (c), 620 + (uint32_t) b, 0.4f);
            g.setColour (eqActive ? colour::accent : colour::soft);
            g.fillPath (e);
            stroke (g, e, 1.8f);
            g.setColour (colour::knob);
            g.setFont (fonts->hand (15.0f));
            g.drawText (juce::String (b + 1), juce::Rectangle<float> (26.0f, 26.0f).withCentre (c), juce::Justification::centred, false);
        }
        for (int k = 0; k < 2; ++k)
        {
            const auto sq = juce::Rectangle<float> (16.0f, 16.0f).withCentre (cutPoint (k));
            g.setColour (colour::paper);
            g.fillRect (sq);
            stroke (g, rectStroke (sq, 630 + (uint32_t) k, 0.3f, 0.5f), 2.0f, cutActive ? colour::blue : colour::soft);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        drag = -1;
        float best = 18.0f;
        for (int b = 0; b < 3; ++b)
        {
            const float d = bandPoint (b).getDistanceFrom (e.position);
            if (d < best) { best = d; drag = b; }
        }
        for (int k = 0; k < 2; ++k)
        {
            const float d = cutPoint (k).getDistanceFrom (e.position);
            if (d < best) { best = d; drag = 3 + k; }
        }
        if (drag < 0) return;

        if (e.mods.isAltDown())
        {
            if (drag < 3) { setParam (freq[(size_t) drag], paramDefault (freq[(size_t) drag])); setParam (gain[(size_t) drag], paramDefault (gain[(size_t) drag])); }
            else { auto* p = drag == 3 ? loCut : hiCut; setParam (p, paramDefault (p)); }
            drag = -1;
            repaint();
            return;
        }
        if (drag < 3) { freq[(size_t) drag]->beginChangeGesture(); gain[(size_t) drag]->beginChangeGesture(); }
        else (drag == 3 ? loCut : hiCut)->beginChangeGesture();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag < 0) return;
        const float f = fOf (e.position.x);
        if (drag < 3)
        {
            auto* fp = freq[(size_t) drag];
            auto* gp = gain[(size_t) drag];
            fp->setValueNotifyingHost (fp->convertTo0to1 (juce::jlimit (20.0f, 20000.0f, f)));
            gp->setValueNotifyingHost (gp->convertTo0to1 (juce::jlimit (-18.0f, 18.0f, dbOf (e.position.y))));
        }
        else
        {
            auto* p = drag == 3 ? loCut : hiCut;
            const auto range = p->getNormalisableRange();
            p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (range.start, range.end, f)));
        }
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (drag < 0) return;
        if (drag < 3) { freq[(size_t) drag]->endChangeGesture(); gain[(size_t) drag]->endChangeGesture(); }
        else (drag == 3 ? loCut : hiCut)->endChangeGesture();
        drag = -1;
    }

private:
    float xOf (float f) const { return 6.0f + (float) (getWidth() - 12) * std::log10 (f / 20.0f) / 3.0f; }
    float fOf (float x) const { return 20.0f * std::pow (1000.0f, (x - 6.0f) / (float) (getWidth() - 12)); }
    float yOf (float db) const { return (float) getHeight() * 0.5f - db / 18.0f * ((float) getHeight() * 0.5f - 12.0f); }
    float dbOf (float y) const { return ((float) getHeight() * 0.5f - y) / ((float) getHeight() * 0.5f - 12.0f) * 18.0f; }

    juce::Point<float> bandPoint (int b) const
    {
        return { xOf (paramValue (freq[(size_t) b])), yOf (paramValue (gain[(size_t) b])) };
    }
    juce::Point<float> cutPoint (int k) const
    {
        const float f = k == 0 ? juce::jmax (20.0f, paramValue (loCut)) : juce::jmin (20000.0f, paramValue (hiCut));
        return { xOf (f), yOf (-3.0f) };
    }

    std::function<double (double)> magnitude;
    std::array<juce::RangedAudioParameter*, 3> freq {}, gain {};
    juce::RangedAudioParameter *loCut = nullptr, *hiCut = nullptr, *eqOn = nullptr, *filtersOn = nullptr;
    juce::SharedResourcePointer<sketch::Fonts> fonts;
    int drag = -1;
};
