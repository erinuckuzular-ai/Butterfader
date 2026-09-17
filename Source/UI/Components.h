#pragma once

#include "Theme.h"
#include "../PluginProcessor.h"

namespace bf::ui
{

//==============================================================================
// Text tabs for the Mic / Master switch; the active one is lit from below.
class ModeTabs : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ModeTabs (juce::RangedAudioParameter& p)
        : attachment (p, [this] (float v) { selected = juce::roundToInt (v); repaint(); })
    {
        attachment.sendInitialUpdate();
        setRepaintsOnMouseActivity (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip ("Mic: put one on each voice. Master: put one on the Mix track.");
    }

    void paint (juce::Graphics& g) override
    {
        const juce::StringArray names { "Mic", "Master" };
        auto r = getLocalBounds().toFloat();
        const float w = r.getWidth() / 2.0f;
        for (int i = 0; i < 2; ++i)
        {
            auto cell = juce::Rectangle<float> (r.getX() + i * w, r.getY(), w, r.getHeight());
            const bool on = i == selected;
            const bool hover = ! on && isMouseOver() && cell.contains (getMouseXYRelative().toFloat());
            g.setFont (font (15.0f, on ? "Semibold" : "Regular"));
            g.setColour (on ? colours::text : hover ? colours::textDim.brighter (0.3f) : colours::textDim);
            g.drawText (names[i], cell, juce::Justification::centred);
            if (on)
            {
                auto bar = cell.withSizeKeepingCentre (22.0f, 2.0f).withY (cell.getBottom() - 6.0f);
                juce::DropShadow (colours::butter.withAlpha (0.8f), 6, {}).drawForRectangle (g, bar.toNearestInt());
                g.setColour (colours::butter);
                g.fillRoundedRectangle (bar, 1.0f);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        attachment.setValueAsCompleteGesture (e.position.x < getWidth() * 0.5f ? 0.0f : 1.0f);
    }

private:
    juce::ParameterAttachment attachment;
    int selected = 1;
};

//==============================================================================
// Recessed channel with a raised key on the selected option.
class KeySelector : public juce::Component
{
public:
    KeySelector (juce::RangedAudioParameter& p, juce::StringArray opts, juce::String caption)
        : options (std::move (opts)), title (std::move (caption)),
          attachment (p, [this] (float v) { selected = juce::roundToInt (v); repaint(); })
    {
        attachment.sendInitialUpdate();
        setRepaintsOnMouseActivity (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paint (juce::Graphics& g) override
    {
        drawLabel (g, title, getLocalBounds().toFloat().removeFromTop (18.0f));
        auto r = channel();
        drawRecessed (g, r, 9.0f);

        const float w = r.getWidth() / options.size();
        for (int i = 0; i < options.size(); ++i)
        {
            auto cell = juce::Rectangle<float> (r.getX() + i * w, r.getY(), w, r.getHeight()).reduced (3.0f);
            const bool on = i == selected;
            const bool hover = ! on && isMouseOver() && cell.contains (getMouseXYRelative().toFloat());
            if (on) drawRaised (g, cell, 7.0f, colours::key, true);
            g.setFont (font (13.5f, on ? "Semibold" : "Regular"));
            g.setColour (on ? colours::text : hover ? colours::text.withAlpha (0.8f) : colours::textDim);
            g.drawText (options[i], cell.withTrimmedBottom (2.0f), juce::Justification::centred);
            if (on)
            {
                auto led = juce::Rectangle<float> (cell.getCentreX() - 7.0f, cell.getBottom() - 5.0f, 14.0f, 2.0f);
                g.setColour (colours::butter);
                g.fillRoundedRectangle (led, 1.0f);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto r = channel();
        if (! r.contains (e.position)) return;
        attachment.setValueAsCompleteGesture ((float) juce::jlimit (0, options.size() - 1, (int) ((e.position.x - r.getX()) / (r.getWidth() / options.size()))));
    }

private:
    juce::Rectangle<float> channel() const { return getLocalBounds().toFloat().withTrimmedTop (22.0f).withHeight (38.0f); }

    juce::StringArray options;
    juce::String title;
    juce::ParameterAttachment attachment;
    int selected = 0;
};

//==============================================================================
class LedToggle : public juce::Component, public juce::SettableTooltipClient
{
public:
    LedToggle (juce::RangedAudioParameter& p, juce::String labelText, juce::Colour ledColour)
        : attachment (p, [this] (float v) { on = v > 0.5f; repaint(); }), label (std::move (labelText)), colour (ledColour)
    {
        attachment.sendInitialUpdate();
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setRepaintsOnMouseActivity (true);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto well = r.removeFromLeft (18.0f).withSizeKeepingCentre (14.0f, 14.0f);
        g.setColour (colours::groove);
        g.fillEllipse (well);
        auto led = well.reduced (3.0f);
        if (on)
        {
            juce::DropShadow (colour.withAlpha (0.9f), 10, {}).drawForRectangle (g, led.toNearestInt());
            g.setGradientFill (juce::ColourGradient (colour.brighter (0.5f), led.getCentreX(), led.getY(), colour, led.getCentreX(), led.getBottom(), false));
        }
        else
            g.setColour (juce::Colour (0xff2b2822));
        g.fillEllipse (led);

        g.setFont (font (13.5f, on ? "Medium" : "Regular"));
        g.setColour (on ? colours::text : isMouseOver() ? colours::textDim.brighter (0.3f) : colours::textDim);
        g.drawText (label, r.withTrimmedLeft (8.0f), juce::Justification::centredLeft);
    }

    void mouseDown (const juce::MouseEvent&) override { attachment.setValueAsCompleteGesture (on ? 0.0f : 1.0f); }

private:
    juce::ParameterAttachment attachment;
    juce::String label;
    juce::Colour colour;
    bool on = true;
};

//==============================================================================
// Horizontal rider fader: the cap rides by itself in Auto, and is draggable in Manual.
class RiderFader : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr float minDb = -24.0f, maxDb = 36.0f;

    explicit RiderFader (juce::RangedAudioParameter& gainParam)
        : attachment (gainParam, [this] (float v) { manualDb = v; repaint(); })
    {
        attachment.sendInitialUpdate();
    }

    void setLive (float db, bool autoMode, bool active, bool learning)
    {
        if (std::abs (db - liveDb) > 0.02f || autoMode != isAuto || active != riding || learning != isLearning)
        {
            liveDb = db; isAuto = autoMode; riding = active; isLearning = learning;
            setMouseCursor (isAuto ? juce::MouseCursor::NormalCursor : juce::MouseCursor::LeftRightResizeCursor);
            setTooltip (isAuto ? "Auto: Butterfader rides the level toward the target. Turn Auto off to set it yourself."
                               : "Drag to set the gain. Double-click for 0 dB.");
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const float value = isAuto ? liveDb : manualDb;

        // readout
        auto head = r.removeFromTop (48.0f);
        drawLabel (g, "Rider", head.removeFromTop (18.0f));
        const auto valueText = minusSign ((value >= 0.0f ? "+" : "") + juce::String (value, 1));
        const auto valueFont = font (30.0f, "Light");
        g.setColour (colours::text);
        g.setFont (valueFont);
        g.drawText (valueText, head, juce::Justification::centredLeft);
        const float vw = valueFont.getStringWidthFloat (valueText);
        g.setFont (font (12.5f));
        g.setColour (isAuto ? (isLearning ? colours::butter : riding ? colours::green : colours::textDim) : colours::textDim);
        const juce::String status = ! isAuto ? "dB, manual" : isLearning ? "dB, listening" : riding ? "dB, riding" : "dB, holding";
        g.drawText (status, head.withTrimmedLeft (vw + 8.0f).withTrimmedTop (6.0f), juce::Justification::centredLeft);

        // track
        const auto t = track();
        drawRecessed (g, t, t.getHeight() * 0.5f);
        const float x0 = xFor (0.0f), xv = xFor (value);
        auto fill = juce::Rectangle<float>::leftTopRightBottom (juce::jmin (x0, xv), t.getY() + 2.0f, juce::jmax (x0, xv), t.getBottom() - 2.0f);
        g.setColour (colours::butter.withAlpha (isAuto && ! riding ? 0.45f : 1.0f));
        g.fillRoundedRectangle (fill, 2.0f);

        g.setFont (font (10.5f));
        for (float db : { -24.0f, 0.0f, 12.0f, 24.0f, 36.0f })
        {
            const float x = xFor (db);
            g.setColour (db == 0.0f ? colours::textDim : colours::textFaint);
            g.fillRect (x - 0.5f, t.getBottom() + 6.0f, 1.0f, 4.0f);
            g.drawText (minusSign ((db > 0 ? "+" : "") + juce::String ((int) db)), juce::Rectangle<float> (x - 16.0f, t.getBottom() + 11.0f, 32.0f, 12.0f), juce::Justification::centred);
        }

        // cap
        auto cap = juce::Rectangle<float> (xv - 10.0f, t.getCentreY() - 16.0f, 20.0f, 32.0f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.8f), 10, { 0, 4 }).drawForRectangle (g, cap.toNearestInt());
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffbf5e6), cap.getX(), cap.getY(), juce::Colour (0xffb9b09c), cap.getX(), cap.getBottom(), false));
        g.fillRoundedRectangle (cap, 4.0f);
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        for (int i : { -1, 1 })
            g.fillRect (cap.getX() + 5.0f, cap.getCentreY() + i * 6.0f - 0.5f, cap.getWidth() - 10.0f, 1.0f);
        g.setColour (colours::butterDeep);
        g.fillRect (cap.getX() + 4.0f, cap.getCentreY() - 1.0f, cap.getWidth() - 8.0f, 2.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (isAuto) return;
        dragStartDb = manualDb; dragStartX = e.position.x;
        attachment.beginGesture();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (isAuto) return;
        const float dbPerPixel = (maxDb - minDb) / track().getWidth() * (e.mods.isShiftDown() ? 0.2f : 1.0f);
        attachment.setValueAsPartOfGesture (std::round (juce::jlimit (-24.0f, 30.0f, dragStartDb + (e.position.x - dragStartX) * dbPerPixel) * 10.0f) / 10.0f);
    }

    void mouseUp (const juce::MouseEvent&) override          { if (! isAuto) attachment.endGesture(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { if (! isAuto) attachment.setValueAsCompleteGesture (0.0f); }

private:
    juce::Rectangle<float> track() const { return getLocalBounds().toFloat().withTrimmedTop (70.0f).withHeight (8.0f).reduced (10.0f, 0.0f); }
    float xFor (float db) const { const auto t = track(); return juce::jmap (db, minDb, maxDb, t.getX(), t.getRight()); }

    juce::ParameterAttachment attachment;
    float manualDb = 0.0f, liveDb = 0.0f, dragStartDb = 0.0f, dragStartX = 0.0f;
    bool isAuto = true, riding = false, isLearning = true;
};

//==============================================================================
class CompareKey : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit CompareKey (juce::RangedAudioParameter& p)
        : attachment (p, [this] (float v) { on = v > 0.5f; repaint(); })
    {
        attachment.sendInitialUpdate();
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setRepaintsOnMouseActivity (true);
        setTooltip ("Hear the original at the same loudness, so you judge the processing and not the volume.");
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f, 4.0f);
        drawRaised (g, r, 8.0f, on ? colours::under.darker (0.2f) : colours::key, isMouseOver());
        auto led = juce::Rectangle<float> (r.getX() + 13.0f, r.getCentreY() - 3.5f, 7.0f, 7.0f);
        if (on) juce::DropShadow (juce::Colour (0xffd9ecff), 8, {}).drawForRectangle (g, led.toNearestInt());
        g.setColour (on ? juce::Colour (0xffd9ecff) : juce::Colour (0xff3a362e));
        g.fillEllipse (led);
        g.setColour (on ? colours::background : colours::text);
        g.setFont (font (13.5f, "Semibold"));
        g.drawText (on ? "Hearing original" : "A/B original", r.withTrimmedLeft (28.0f), juce::Justification::centredLeft);
    }

    void mouseDown (const juce::MouseEvent&) override { attachment.setValueAsCompleteGesture (on ? 0.0f : 1.0f); }

private:
    juce::ParameterAttachment attachment;
    bool on = false;
};

//==============================================================================
class ClipBadge : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void()> onReset;

    void setState (int count, bool now)
    {
        if (count != clips || now != live) { clips = count; live = now; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const bool clipped = clips > 0;
        auto led = r.removeFromLeft (14.0f).withSizeKeepingCentre (8.0f, 8.0f);
        if (clipped && live) juce::DropShadow (colours::red, 10, {}).drawForRectangle (g, led.toNearestInt());
        g.setColour (clipped ? colours::red : colours::green.withAlpha (0.7f));
        g.fillEllipse (led);
        g.setFont (font (13.0f, clipped ? "Semibold" : "Regular"));
        g.setColour (clipped ? colours::red : colours::textDim);
        g.drawText (clipped ? "Source clipped " + juce::String (juce::CharPointer_UTF8 ("\xc3\x97")) + juce::String (clips) : "Source clean",
                    r.withTrimmedLeft (6.0f), juce::Justification::centredLeft);
    }

    void mouseUp (const juce::MouseEvent&) override { if (clips > 0 && onReset) onReset(); }

private:
    int clips = 0;
    bool live = false;
};

//==============================================================================
// The stage: the last few seconds of audio as a lit landscape, the big readout floating over it,
// and tall meters on the right.
class StageView : public juce::Component
{
public:
    struct Readout
    {
        float integrated = -120.0f, shortTerm = -120.0f, momentary = -120.0f, truePeak = -120.0f;
        float target = -14.0f, ceiling = -1.0f, grHeld = 0.0f, duck = 0.0f;
        juce::String status;
        juce::Colour statusColour;
    };

    explicit StageView (ButterfaderAudioProcessor& p) : processor (p)
    {
        addAndMakeVisible (clipBadge);
        clipBadge.setTooltip ("Watches the audio coming in. If the recording already clipped, no limiter can undo it. Click to clear.");
    }

    ClipBadge clipBadge;
    std::function<void()> onReset;

    void setReadout (const Readout& r) { readout = r; repaint(); }

    void resized() override
    {
        auto f = footerArea();
        clipBadge.setBounds (juce::Rectangle<float> (f.getX() + 24.0f, f.getY() + 6.0f, 200.0f, 22.0f).toNearestInt());
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (resetArea().contains (e.position) && onReset) onReset();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (resetArea().contains (e.position) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat();
        g.setColour (colours::stage);
        g.fillRect (area);

        paintGraph (g, graphArea());
        paintOverlay (g, graphArea());
        paintFooter (g, footerArea());
        paintMeters (g, meterArea());

        // edge shading makes the stage read as a window sunk into the faceplate
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.75f), 0.0f, 0.0f, juce::Colours::transparentBlack, 0.0f, 16.0f, false));
        g.fillRect (area.withHeight (16.0f));
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.fillRect (area.withTop (area.getBottom() - 1.0f));
    }

private:
    static constexpr float meterWidth = 118.0f;
    juce::Rectangle<float> meterArea() const { return getLocalBounds().toFloat().removeFromRight (meterWidth); }
    static constexpr float footerHeight = 34.0f;
    juce::Rectangle<float> graphArea() const { return getLocalBounds().toFloat().withTrimmedRight (meterWidth).withTrimmedBottom (footerHeight); }
    juce::Rectangle<float> footerArea() const { return getLocalBounds().toFloat().withTrimmedRight (meterWidth).removeFromBottom (footerHeight); }
    juce::Rectangle<float> resetArea() const { auto f = footerArea(); return { f.getRight() - 74.0f, f.getY() + 6.0f, 50.0f, 22.0f }; }

    static float yFor (juce::Rectangle<float> r, float db)
    {
        return juce::jmap (juce::jlimit (-48.0f, 0.0f, db), 0.0f, -48.0f, r.getY() + 24.0f, r.getBottom() - 6.0f);
    }

    static juce::Path strokeOf (const juce::Path& p, float width)
    {
        juce::Path out;
        juce::PathStrokeType (width).createStrokedPath (out, p);
        return out;
    }

    void paintGraph (juce::Graphics& g, juce::Rectangle<float> r)
    {
        for (float db = -12.0f; db >= -36.0f; db -= 12.0f)
        {
            g.setColour (colours::grid);
            g.fillRect (r.getX(), yFor (r, db), r.getWidth(), 1.0f);
        }

        const int points = juce::jlimit (50, ButterfaderAudioProcessor::historySize - 1, (int) (r.getWidth() / 2.5f));
        const float step = r.getWidth() / (float) (points - 1);
        const int newest = processor.historyWrite.load();

        juce::Path outPath, outRim, grPath, grRim, duckPath, stPath;
        outPath.startNewSubPath (r.getX(), r.getBottom());
        grPath.startNewSubPath (r.getX(), r.getY());
        duckPath.startNewSubPath (r.getX(), r.getY());
        bool stStarted = false;
        anyDuck = false;

        for (int i = 0; i < points; ++i)
        {
            const auto& h = processor.history[(size_t) ((newest - (points - 1 - i) + ButterfaderAudioProcessor::historySize) % ButterfaderAudioProcessor::historySize)];
            const float x = r.getX() + i * step;
            const float yo = yFor (r, h.outputDb);
            const float yg = r.getY() + juce::jlimit (0.0f, 1.0f, -h.grDb / 24.0f) * r.getHeight() * 0.9f;
            outPath.lineTo (x, yo);
            grPath.lineTo (x, yg);
            if (i == 0) { outRim.startNewSubPath (x, yo); grRim.startNewSubPath (x, yg); }
            else        { outRim.lineTo (x, yo); grRim.lineTo (x, yg); }
            duckPath.lineTo (x, r.getY() + juce::jlimit (0.0f, 1.0f, -h.duckDb / 36.0f) * r.getHeight());
            anyDuck = anyDuck || h.duckDb < -0.5f;
            if (h.shortTerm > -48.0f)
            {
                if (! stStarted) { stPath.startNewSubPath (x, yFor (r, h.shortTerm)); stStarted = true; }
                else stPath.lineTo (x, yFor (r, h.shortTerm));
            }
        }
        outPath.lineTo (r.getRight(), r.getBottom()); outPath.closeSubPath();
        for (auto* p : { &grPath, &duckPath }) { p->lineTo (r.getRight(), r.getY()); p->closeSubPath(); }

        // output: lit from above, fading into the floor, with a bright rim
        g.setGradientFill (juce::ColourGradient (colours::butter.withAlpha (0.62f), 0.0f, r.getY() + 24.0f,
                                                 colours::butterDeep.withAlpha (0.03f), 0.0f, r.getBottom(), false));
        g.fillPath (outPath);
        g.setColour (colours::butter.withAlpha (0.25f));
        g.strokePath (outRim, juce::PathStrokeType (3.0f));
        g.setColour (juce::Colour (0xfffff0c2).withAlpha (0.85f));
        g.strokePath (outRim, juce::PathStrokeType (1.0f));

        if (anyDuck)
        {
            g.setGradientFill (juce::ColourGradient (colours::under.withAlpha (0.22f), 0.0f, r.getY(), colours::under.withAlpha (0.04f), 0.0f, r.getBottom(), false));
            g.fillPath (duckPath);
        }

        // limiter reduction hanging from the top
        g.setGradientFill (juce::ColourGradient (colours::red.withAlpha (0.9f), 0.0f, r.getY(), colours::red.withAlpha (0.3f), 0.0f, r.getY() + r.getHeight() * 0.3f, false));
        g.fillPath (grPath);
        g.setColour (colours::red);
        g.strokePath (grRim, juce::PathStrokeType (1.2f));

        // target
        const float ty = yFor (r, readout.target);
        const float dashes[] = { 3.0f, 4.0f };
        g.setColour (colours::text.withAlpha (0.4f));
        g.drawDashedLine ({ r.getX(), ty, r.getRight(), ty }, dashes, 2, 1.0f);

        // short-term loudness trace, lifted off the landscape by a shadow
        juce::DropShadow (juce::Colours::black, 6, { 0, 2 }).drawForPath (g, strokeOf (stPath, 2.2f));
        g.setColour (colours::text);
        g.strokePath (stPath, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void paintOverlay (juce::Graphics& g, juce::Rectangle<float> r)
    {
        // soft-edged shade behind each readout keeps it legible over loud audio
        auto shade = [&] (juce::Rectangle<float> box)
        {
            for (int i = 12; i >= 0; --i)
            {
                g.setColour (colours::stage.withAlpha (0.14f));
                g.fillRoundedRectangle (box.expanded (i * 4.0f), 12.0f + i * 4.0f);
            }
        };
        shade (juce::Rectangle<float> (r.getX() - 20.0f, r.getY() - 20.0f, 420.0f, 170.0f));
        shade (juce::Rectangle<float> (r.getRight() - 200.0f, r.getY() + 22.0f, 184.0f, 76.0f));

        const auto c = loudnessColour (readout.integrated, readout.target);
        const bool none = readout.integrated < -70.0f;
        const juce::String big = none ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93\xe2\x80\x93.\xe2\x80\x93"))
                                      : minusSign (juce::String (readout.integrated, 1));
        drawGlowText (g, big, font (96.0f, "Light").withExtraKerningFactor (-0.03f), c, r.getX() + 22.0f, r.getY() + 108.0f, none ? 0.0f : 0.3f);

        const auto unitFont = font (14.0f);
        g.setFont (unitFont);
        g.setColour (colours::textDim);
        const juce::String unit = "LUFS integrated";
        const float unitW = unitFont.getStringWidthFloat (unit);
        g.drawText (unit, juce::Rectangle<float> (r.getX() + 28.0f, r.getY() + 120.0f, unitW + 4.0f, 20.0f), juce::Justification::centredLeft);
        g.setColour (colours::textFaint);
        g.drawText (dot(), juce::Rectangle<float> (r.getX() + 28.0f + unitW, r.getY() + 120.0f, 24.0f, 20.0f), juce::Justification::centred);
        g.setColour (readout.statusColour);
        g.setFont (font (14.0f, "Semibold"));
        g.drawText (readout.status, juce::Rectangle<float> (r.getX() + 52.0f + unitW, r.getY() + 120.0f, 420.0f, 20.0f), juce::Justification::centredLeft, true);

        // stats, top right of the graph
        auto stats = juce::Rectangle<float> (r.getRight() - 214.0f, r.getY() + 24.0f, 190.0f, 78.0f);
        auto row = [&] (const juce::String& name, const juce::String& value, juce::Colour vc)
        {
            auto line = stats.removeFromTop (26.0f);
            const auto valueFont = font (19.0f, "Medium");
            g.setFont (valueFont);
            g.setColour (vc);
            g.drawText (value, line, juce::Justification::centredRight);
            g.setFont (font (12.5f));
            g.setColour (colours::textDim);
            g.drawText (name, line.withTrimmedRight (valueFont.getStringWidthFloat (value) + 8.0f), juce::Justification::centredRight);
        };
        auto num = [] (float v) { return v < -70.0f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")) : minusSign (juce::String (v, 1)); };
        row ("short-term", num (readout.shortTerm), loudnessColour (readout.shortTerm, readout.target));
        row ("true peak", num (readout.truePeak), readout.truePeak > readout.ceiling + 0.05f ? colours::red : colours::text);
        const juce::String grWord = readout.grHeld > -0.3f ? "idle" : readout.grHeld > -3.0f ? "easy" : readout.grHeld > -6.0f ? "working" : "too hard";
        row ("limiter", grWord, reductionColour (readout.grHeld));
    }

    void paintFooter (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (juce::Colour (0xff0d0c0a));
        g.fillRect (r);
        g.setColour (juce::Colours::black);
        g.fillRect (r.withHeight (1.0f));

        g.setFont (font (12.0f));
        auto legend = juce::Rectangle<float> (r.getX() + 250.0f, r.getY() + 6.0f, 360.0f, 22.0f);
        auto item = [&] (juce::Colour col, const juce::String& t, float w, bool line)
        {
            auto a = legend.removeFromLeft (w);
            auto sw = a.removeFromLeft (14.0f).withSizeKeepingCentre (14.0f, line ? 2.0f : 8.0f);
            g.setColour (col);
            g.fillRoundedRectangle (sw, line ? 1.0f : 2.0f);
            g.setColour (colours::textDim);
            g.drawText (t, a.withTrimmedLeft (6.0f), juce::Justification::centredLeft);
        };
        item (colours::butter, "output", 76.0f, false);
        item (colours::text, "loudness", 90.0f, true);
        item (colours::red, "limiter", 74.0f, false);
        if (anyDuck) item (colours::under, "ducking", 80.0f, false);

        g.setColour (colours::textDim);
        g.drawText ("Reset", resetArea(), juce::Justification::centredRight);
    }

    void paintMeters (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0c0b09), r.getX(), 0.0f, juce::Colour (0xff13110d), r.getRight(), 0.0f, false));
        g.fillRect (r);
        g.setColour (juce::Colours::black);
        g.fillRect (r.withWidth (1.0f));
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.fillRect (r.withWidth (1.0f).translated (1.0f, 0.0f));

        auto body = r.reduced (0.0f, 24.0f).withTrimmedBottom (18.0f);
        auto lufsCol = juce::Rectangle<float> (r.getX() + 34.0f, body.getY(), 30.0f, body.getHeight());
        auto grCol   = juce::Rectangle<float> (r.getX() + 80.0f, body.getY(), 20.0f, body.getHeight());

        // loudness: short-term column, momentary tick, target notch
        drawRecessed (g, lufsCol, 5.0f);
        auto yL = [&] (float l) { return juce::jmap (juce::jlimit (-40.0f, 0.0f, l), 0.0f, -40.0f, lufsCol.getY() + 3.0f, lufsCol.getBottom() - 3.0f); };
        if (readout.shortTerm > -40.0f)
        {
            const auto c = loudnessColour (readout.shortTerm, readout.target);
            auto fill = lufsCol.reduced (3.0f).withTop (yL (readout.shortTerm));
            juce::DropShadow (c.withAlpha (0.5f), 12, {}).drawForRectangle (g, fill.toNearestInt());
            g.setGradientFill (juce::ColourGradient (c.brighter (0.35f), fill.getX(), 0.0f, c.darker (0.3f), fill.getRight(), 0.0f, false));
            g.fillRoundedRectangle (fill, 3.0f);
            g.setColour (juce::Colours::white.withAlpha (0.4f));
            g.fillRect (fill.getX() + 4.0f, fill.getY() + 2.0f, 2.0f, fill.getHeight() - 4.0f);
        }
        if (readout.momentary > -40.0f)
        {
            g.setColour (colours::text);
            g.fillRect (lufsCol.getX() + 2.0f, yL (readout.momentary) - 1.0f, lufsCol.getWidth() - 4.0f, 2.0f);
        }
        const float ty = yL (readout.target);
        juce::Path notch;
        notch.addTriangle (lufsCol.getX() - 9.0f, ty - 5.0f, lufsCol.getX() - 9.0f, ty + 5.0f, lufsCol.getX() - 2.0f, ty);
        juce::DropShadow (colours::butter.withAlpha (0.7f), 6, {}).drawForPath (g, notch);
        g.setColour (colours::butter);
        g.fillPath (notch);
        g.setFont (font (10.5f));
        for (float l = 0.0f; l >= -40.0f; l -= 10.0f)
        {
            g.setColour (colours::textFaint);
            g.drawText (minusSign (juce::String ((int) l)), juce::Rectangle<float> (r.getX() + 2.0f, yL (l) - 6.0f, 20.0f, 12.0f), juce::Justification::centredRight);
        }

        // gain reduction as lit segments
        drawRecessed (g, grCol, 5.0f);
        const int segments = 12;
        const float segH = (grCol.getHeight() - 6.0f) / segments;
        for (int s = 0; s < segments; ++s)
        {
            auto seg = juce::Rectangle<float> (grCol.getX() + 3.0f, grCol.getY() + 3.0f + s * segH, grCol.getWidth() - 6.0f, segH - 2.0f);
            const bool lit = readout.grHeld <= -(float) s - 0.3f;
            const auto c = reductionColour (-(float) s - 1.0f);
            if (lit)
            {
                juce::DropShadow (c.withAlpha (0.6f), 8, {}).drawForRectangle (g, seg.toNearestInt());
                g.setColour (c);
            }
            else
                g.setColour (c.withAlpha (0.09f));
            g.fillRoundedRectangle (seg, 1.5f);
        }

        g.setFont (font (11.5f));
        g.setColour (colours::textDim);
        g.drawText ("LUFS", juce::Rectangle<float> (lufsCol.getX() - 10.0f, body.getBottom() + 5.0f, 50.0f, 14.0f), juce::Justification::centred);
        g.drawText ("GR", juce::Rectangle<float> (grCol.getX() - 10.0f, body.getBottom() + 5.0f, 40.0f, 14.0f), juce::Justification::centred);
    }

    ButterfaderAudioProcessor& processor;
    Readout readout;
    bool anyDuck = false;
};

} // namespace bf::ui
