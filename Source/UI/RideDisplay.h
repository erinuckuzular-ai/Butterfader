#pragma once

#include "ButterLookAndFeel.h"
#include "ButterBuddy.h"

// Scrolling history of what the rider did, with the butter buddy riding the actual gain fader.
class RideDisplay : public juce::Component
{
public:
    struct Point
    {
        float inLufs = -120.0f, outLufs = -120.0f, gainDb = 0.0f;
        int state = 0;
    };

    ButterBuddy::Mood mood = ButterBuddy::Mood::sleepy;
    float targetLufs = -18.0f, maxBoost = 9.0f, maxCut = 9.0f;
    std::function<void()> onPoke;

    void push (const Point& p)
    {
        history[(size_t) writePos] = p;
        writePos = (writePos + 1) % historySize;
        buddy.tick();
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (faderArea().expanded (10.0f).contains (e.position) && onPoke)
            onPoke();
    }

    void paint (juce::Graphics& g) override
    {
        using LAF = ButterLookAndFeel;
        const auto graph = graphArea();

        // --- Fader rail with buddy -------------------------------------------------------
        const auto rail = faderArea();
        g.setColour (LAF::ink);
        g.fillRoundedRectangle (rail.withSizeKeepingCentre (10.0f, rail.getHeight()), 5.0f);
        g.setFont (LAF::chunky (10.0f));
        for (float db : { -18.0f, -12.0f, -6.0f, 0.0f, 6.0f, 12.0f, 18.0f })
        {
            const float y = yForGainDb (db, rail);
            g.drawHorizontalLine ((int) y, rail.getX() + 6.0f, rail.getX() + 14.0f);
            g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (rail.getX() - 34.0f, y - 6.0f, 30.0f, 12.0f),
                        juce::Justification::centredRight);
        }

        const float gainNow = latest().gainDb;
        const float capY = yForGainDb (gainNow, rail);
        const auto cap = juce::Rectangle<float> (46.0f, 16.0f).withCentre ({ rail.getCentreX(), capY });
        LAF::drawSticker (g, cap, LAF::white, 4.0f, 3.0f, 2.5f);
        g.setColour (LAF::ink);
        g.drawHorizontalLine ((int) cap.getCentreY(), cap.getX() + 6.0f, cap.getRight() - 6.0f);
        buddy.draw (g, { rail.getCentreX() + 2.0f, cap.getY() + 1.0f }, 62.0f, mood);

        g.setFont (LAF::chunky (13.0f));
        g.drawText ((gainNow >= 0 ? "+" : "") + juce::String (gainNow, 1) + " dB",
                    juce::Rectangle<float> (rail.getX() - 20.0f, rail.getBottom() + 6.0f, rail.getWidth() + 40.0f, 16.0f), juce::Justification::centred);

        // --- Graph ------------------------------------------------------------------------
        g.setColour (LAF::ink);
        g.fillRoundedRectangle (graph, 10.0f);

        const float w = graph.getWidth(), step = w / (historySize - 1);
        // State bands along the bottom and bleed shading
        for (int i = 0; i < historySize; ++i)
        {
            const auto& p = at (i);
            const float x = graph.getX() + i * step;
            if (p.state == 3)
            {
                g.setColour (LAF::lilac.withAlpha (0.22f));
                g.fillRect (x, graph.getY(), step + 0.5f, graph.getHeight());
            }
            g.setColour (stateColour (p.state));
            g.fillRect (x, graph.getBottom() - 6.0f, step + 0.5f, 4.0f);
        }

        // Target zone
        const float ty = yForLufs (targetLufs, graph);
        g.setColour (LAF::mint.withAlpha (0.12f));
        g.fillRect (juce::Rectangle<float> (graph.getX(), yForLufs (targetLufs + 2.0f, graph), w, yForLufs (targetLufs - 2.0f, graph) - yForLufs (targetLufs + 2.0f, graph)));
        g.setColour (LAF::mint.withAlpha (0.8f));
        const float dashes[] { 6.0f, 5.0f };
        g.drawDashedLine ({ graph.getX(), ty, graph.getRight(), ty }, dashes, 2, 1.5f);
        g.setFont (LAF::chunky (10.0f));
        g.drawText ("sweet spot " + juce::String (targetLufs, 0), juce::Rectangle<float> (graph.getX() + 8.0f, ty - 15.0f, 140.0f, 13.0f), juce::Justification::left);

        auto line = [&] (std::function<float (const Point&)> value, juce::Colour c, float thickness, bool skipQuiet)
        {
            juce::Path path;
            bool started = false;
            for (int i = 0; i < historySize; ++i)
            {
                const auto& p = at (i);
                if (skipQuiet && p.inLufs < -60.0f) { started = false; continue; }
                const juce::Point<float> pt (graph.getX() + i * step, value (p));
                if (! started) { path.startNewSubPath (pt); started = true; } else path.lineTo (pt);
            }
            g.setColour (c);
            g.strokePath (path, { thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
        };

        line ([&] (const Point& p) { return yForLufs (p.inLufs, graph); }, LAF::white.withAlpha (0.35f), 1.5f, true);
        line ([&] (const Point& p) { return yForLufs (p.outLufs, graph); }, LAF::butter, 2.5f, true);
        line ([&] (const Point& p) { return juce::jmap (p.gainDb, -18.0f, 18.0f, graph.getBottom() - 12.0f, graph.getY() + 12.0f); },
              LAF::tomato, 2.0f, false);

        // Legend
        g.setFont (LAF::chunky (10.0f));
        auto legend = juce::Rectangle<float> (graph.getRight() - 190.0f, graph.getY() + 6.0f, 184.0f, 12.0f);
        auto item = [&] (const juce::String& text, juce::Colour c, float width)
        {
            auto r = legend.removeFromLeft (width);
            g.setColour (c);
            g.fillRect (r.removeFromLeft (10.0f).withSizeKeepingCentre (10.0f, 3.0f));
            g.drawText (text, r.withTrimmedLeft (3.0f), juce::Justification::left);
        };
        item ("in", LAF::white.withAlpha (0.6f), 34.0f);
        item ("out", LAF::butter, 42.0f);
        item ("ride", LAF::tomato, 44.0f);
        item ("bleed", LAF::lilac, 60.0f);
    }

    static juce::Colour stateColour (int state)
    {
        switch (state)
        {
            case 1:  return ButterLookAndFeel::grey;
            case 2:  return ButterLookAndFeel::mint;
            case 3:  return ButterLookAndFeel::lilac;
            default: return ButterLookAndFeel::ink.brighter (0.25f);
        }
    }

private:
    static constexpr int historySize = 240;

    juce::Rectangle<float> faderArea() const { return { 30.0f, 40.0f, 50.0f, getHeight() - 70.0f }; }
    juce::Rectangle<float> graphArea() const { return getLocalBounds().toFloat().withTrimmedLeft (100.0f); }

    const Point& at (int i) const { return history[(size_t) ((writePos + i) % historySize)]; }
    const Point& latest() const { return history[(size_t) ((writePos - 1 + historySize) % historySize)]; }

    static float yForGainDb (float db, juce::Rectangle<float> rail) { return juce::jmap (db, -18.0f, 18.0f, rail.getBottom(), rail.getY()); }
    static float yForLufs (float lufs, juce::Rectangle<float> r)
    {
        return juce::jmap (juce::jlimit (-60.0f, 0.0f, lufs), -60.0f, 0.0f, r.getBottom() - 10.0f, r.getY() + 10.0f);
    }

    std::array<Point, historySize> history {};
    int writePos = 0;
    ButterBuddy buddy;
};
