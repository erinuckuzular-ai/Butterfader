#pragma once

#include "Theme.h"
#include "../PluginProcessor.h"

namespace bf::ui
{

//==============================================================================
// Row of pill buttons bound to a choice parameter.
class SegmentedControl : public juce::Component
{
public:
    SegmentedControl (juce::RangedAudioParameter& p, juce::StringArray opts, juce::String caption)
        : param (p), options (std::move (opts)), title (std::move (caption)),
          attachment (p, [this] (float v) { selected = juce::roundToInt (v); repaint(); })
    {
        attachment.sendInitialUpdate();
        setRepaintsOnMouseActivity (true);
    }

    std::function<juce::String (int)> tooltipFor;

    void paint (juce::Graphics& g) override
    {
        drawCaption (g, title, getLocalBounds().removeFromTop (16).toFloat());
        auto r = track();
        g.setColour (colours::groove);
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);

        const float w = r.getWidth() / options.size();
        for (int i = 0; i < options.size(); ++i)
        {
            auto cell = juce::Rectangle<float> (r.getX() + i * w, r.getY(), w, r.getHeight()).reduced (3.0f);
            const bool on = i == selected;
            const bool hover = ! on && isMouseOver() && cell.contains (getMouseXYRelative().toFloat());
            if (on)
            {
                g.setColour (colours::butter);
                g.fillRoundedRectangle (cell, cell.getHeight() * 0.5f);
            }
            else if (hover)
            {
                g.setColour (colours::panelRaised);
                g.fillRoundedRectangle (cell, cell.getHeight() * 0.5f);
            }
            g.setColour (on ? colours::background : hover ? colours::text : colours::textDim);
            g.setFont (font (13.0f, true));
            g.drawText (options[i], cell, juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto r = track();
        if (! r.contains (e.position)) return;
        const int i = juce::jlimit (0, options.size() - 1, (int) ((e.position.x - r.getX()) / (r.getWidth() / options.size())));
        attachment.setValueAsCompleteGesture ((float) i);
    }

private:
    juce::Rectangle<float> track() const { return getLocalBounds().toFloat().withTrimmedTop (20.0f).withHeight (34.0f); }

    juce::RangedAudioParameter& param;
    juce::StringArray options;
    juce::String title;
    juce::ParameterAttachment attachment;
    int selected = 0;
};

//==============================================================================
class AutoSwitch : public juce::Component
{
public:
    explicit AutoSwitch (juce::RangedAudioParameter& p)
        : attachment (p, [this] (float v) { on = v > 0.5f; repaint(); })
    {
        attachment.sendInitialUpdate();
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTitle ("Auto level");
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto pill = r.removeFromLeft (44.0f).withSizeKeepingCentre (44.0f, 24.0f);
        g.setColour (on ? colours::butter : colours::groove);
        g.fillRoundedRectangle (pill, 12.0f);
        const float knobX = on ? pill.getRight() - 21.0f : pill.getX() + 3.0f;
        g.setColour (on ? colours::background : colours::textDim);
        g.fillEllipse (knobX, pill.getY() + 3.0f, 18.0f, 18.0f);

        g.setColour (on ? colours::text : colours::textDim);
        g.setFont (font (13.0f, true));
        g.drawText (on ? "Auto" : "Manual", r.withTrimmedLeft (8.0f), juce::Justification::centredLeft);
    }

    void mouseDown (const juce::MouseEvent&) override { attachment.setValueAsCompleteGesture (on ? 0.0f : 1.0f); }

private:
    juce::ParameterAttachment attachment;
    bool on = true;
};

//==============================================================================
// The "rider": a fader whose butter cap moves by itself in auto mode, draggable in manual mode.
class RiderFader : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr float minDb = -24.0f, maxDb = 36.0f;

    RiderFader (juce::RangedAudioParameter& gainParam)
        : param (gainParam), attachment (gainParam, [this] (float v) { manualDb = v; repaint(); })
    {
        attachment.sendInitialUpdate();
    }

    void setLive (float db, bool autoMode, bool active, bool learning)
    {
        if (std::abs (db - liveDb) > 0.02f || autoMode != isAuto || active != riding || learning != isLearning)
        {
            liveDb = db; isAuto = autoMode; riding = active; isLearning = learning;
            setMouseCursor (isAuto ? juce::MouseCursor::NormalCursor : juce::MouseCursor::UpDownResizeCursor);
            setTooltip (isAuto ? "Auto gain: Butterfader rides your level toward the target. Switch to Manual to set it yourself."
                               : "Drag to set input gain. Double-click for 0 dB.");
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const auto trackArea = slot();

        // scale
        g.setFont (font (10.0f, true));
        for (float db : { 36.0f, 24.0f, 12.0f, 0.0f, -12.0f, -24.0f })
        {
            const float y = yFor (db);
            g.setColour (db == 0.0f ? colours::textDim : colours::textFaint);
            g.drawText (juce::String (db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (r.getX(), y - 7.0f, 26.0f, 14.0f),
                        juce::Justification::centredRight);
            g.fillRect (trackArea.getX() - 8.0f, y - 0.5f, 5.0f, 1.0f);
        }

        g.setColour (colours::groove);
        g.fillRoundedRectangle (trackArea, 3.0f);

        const float value = isAuto ? liveDb : manualDb;
        const float y0 = yFor (0.0f), yv = yFor (value);
        g.setColour (colours::butter.withAlpha (isAuto && ! riding ? 0.35f : 0.8f));
        g.fillRect (trackArea.getX(), juce::jmin (y0, yv), trackArea.getWidth(), std::abs (yv - y0));

        // butter-pat cap
        auto cap = juce::Rectangle<float> (trackArea.getCentreX() - 24.0f, yv - 13.0f, 48.0f, 26.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (cap.translated (0.0f, 3.0f), 7.0f);
        g.setGradientFill (juce::ColourGradient (colours::butter.brighter (0.25f), cap.getX(), cap.getY(),
                                                 colours::butterDeep, cap.getX(), cap.getBottom(), false));
        g.fillRoundedRectangle (cap, 7.0f);
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.fillRoundedRectangle (cap.reduced (6.0f, 0.0f).withHeight (3.0f).translated (0.0f, 4.0f), 1.5f);
        g.setColour (colours::background);
        g.setFont (mono (12.0f));
        g.drawText ((value >= 0.0f ? "+" : "") + juce::String (value, 1), cap.translated (0.0f, 1.0f), juce::Justification::centred);

        // status
        g.setFont (font (11.0f, true));
        g.setColour (isAuto ? (isLearning ? colours::butter : riding ? colours::green : colours::textDim) : colours::textDim);
        const juce::String status = ! isAuto ? "manual gain" : isLearning ? "listening..." : riding ? "riding" : "holding";
        g.drawText (status, r.removeFromBottom (16.0f), juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (isAuto) return;
        dragStartDb = manualDb;
        dragStartY = e.position.y;
        attachment.beginGesture();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (isAuto) return;
        const float dbPerPixel = (maxDb - minDb) / slot().getHeight() * (e.mods.isShiftDown() ? 0.2f : 1.0f);
        const float db = juce::jlimit (-24.0f, 30.0f, dragStartDb - (e.position.y - dragStartY) * dbPerPixel);
        attachment.setValueAsPartOfGesture (std::round (db * 10.0f) / 10.0f);
    }

    void mouseUp (const juce::MouseEvent&) override       { if (! isAuto) attachment.endGesture(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { if (! isAuto) attachment.setValueAsCompleteGesture (0.0f); }

private:
    juce::Rectangle<float> slot() const
    {
        auto r = getLocalBounds().toFloat().reduced (0.0f, 16.0f).withTrimmedBottom (14.0f);
        return juce::Rectangle<float> (r.getX() + 52.0f, r.getY(), 6.0f, r.getHeight());
    }

    float yFor (float db) const
    {
        const auto s = slot();
        return juce::jmap (db, maxDb, minDb, s.getY(), s.getBottom());
    }

    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    float manualDb = 0.0f, liveDb = 0.0f, dragStartDb = 0.0f, dragStartY = 0.0f;
    bool isAuto = true, riding = false, isLearning = true;
};

//==============================================================================
// Vertical loudness meter: short-term fill coloured against the target, momentary tick, target band.
class LoudnessBar : public juce::Component
{
public:
    static constexpr float top = 0.0f, bottom = -40.0f;

    void setValues (float shortTermLufs, float momentaryLufs, float targetLufs)
    {
        const float st = juce::jmax (bottom - 1.0f, shortTermLufs), m = juce::jmax (bottom - 1.0f, momentaryLufs);
        if (std::abs (st - shortTerm) > 0.01f || std::abs (m - momentary) > 0.01f || targetLufs != target)
        {
            shortTerm = st; momentary = m; target = targetLufs;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        drawCaption (g, "Loudness", r.removeFromTop (16.0f), juce::Justification::centred);
        r.removeFromTop (12.0f);
        r.removeFromBottom (8.0f);
        auto scaleArea = r.removeFromLeft (30.0f);
        r.removeFromLeft (10.0f);
        auto bar = r.removeFromLeft (26.0f);

        g.setColour (colours::groove);
        g.fillRoundedRectangle (bar, 6.0f);

        auto yFor = [&] (float l) { return juce::jmap (juce::jlimit (bottom, top, l), top, bottom, bar.getY(), bar.getBottom()); };

        // zones beside the bar
        auto zone = [&] (float from, float to, juce::Colour c)
        {
            g.setColour (c.withAlpha (0.55f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (bar.getRight() + 4.0f, yFor (to), bar.getRight() + 7.0f, yFor (from)));
        };
        zone (target - 2.0f, target + 1.0f, colours::green);
        zone (target + 1.0f, target + 3.0f, colours::amber);
        zone (target + 3.0f, top, colours::red);
        zone (bottom, target - 2.0f, colours::under.withAlpha (0.5f));

        // fill
        if (shortTerm > bottom)
        {
            const auto colour = loudnessColour (shortTerm, target);
            auto fill = bar.withTop (yFor (shortTerm));
            g.setGradientFill (juce::ColourGradient (colour, fill.getX(), fill.getY(), colour.withAlpha (0.35f), fill.getX(), bar.getBottom(), false));
            g.fillRoundedRectangle (fill, 6.0f);
        }

        // momentary tick
        if (momentary > bottom)
        {
            g.setColour (colours::text.withAlpha (0.8f));
            g.fillRect (bar.getX() + 3.0f, yFor (momentary) - 1.0f, bar.getWidth() - 6.0f, 2.0f);
        }

        // target marker
        const float ty = yFor (target);
        juce::Path arrow;
        arrow.addTriangle (bar.getX() - 7.0f, ty - 5.0f, bar.getX() - 7.0f, ty + 5.0f, bar.getX() - 1.0f, ty);
        g.setColour (colours::butter);
        g.fillPath (arrow);
        g.fillRect (bar.getX(), ty - 0.75f, bar.getWidth(), 1.5f);

        g.setFont (font (10.0f, true));
        for (float l = top; l >= bottom; l -= 10.0f)
        {
            g.setColour (colours::textFaint);
            g.drawText (juce::String ((int) l), scaleArea.withY (yFor (l) - 7.0f).withHeight (14.0f).withTrimmedRight (2.0f), juce::Justification::centredRight);
        }
    }

private:
    float shortTerm = -120.0f, momentary = -120.0f, target = -14.0f;
};

//==============================================================================
class ReductionBar : public juce::Component
{
public:
    static constexpr float range = 12.0f;

    void setValue (float grDb)
    {
        const float v = juce::jlimit (-range, 0.0f, grDb);
        held = v < held ? v : held + (v - held) * 0.12f;
        if (std::abs (held - shown) > 0.01f) { shown = held; repaint(); }
    }

    float getHeld() const { return held; }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        drawCaption (g, "Reduction", r.removeFromTop (16.0f), juce::Justification::centred);
        r.removeFromTop (12.0f);
        r.removeFromBottom (8.0f);
        auto bar = r.withSizeKeepingCentre (22.0f, r.getHeight()).translated (-8.0f, 0.0f);
        auto scaleArea = r.withLeft (bar.getRight() + 4.0f);

        g.setColour (colours::groove);
        g.fillRoundedRectangle (bar, 6.0f);
        auto yFor = [&] (float db) { return juce::jmap (-db, 0.0f, range, bar.getY(), bar.getBottom()); };

        if (shown < -0.05f)
        {
            auto fill = bar.withBottom (yFor (shown));
            g.setGradientFill (juce::ColourGradient (colours::green, 0.0f, yFor (0.0f), colours::red, 0.0f, yFor (-8.0f), false));
            g.fillRoundedRectangle (fill, 6.0f);
        }

        g.setFont (font (10.0f, true));
        for (float db : { 0.0f, 3.0f, 6.0f, 9.0f, 12.0f })
        {
            g.setColour (db == 3.0f || db == 6.0f ? colours::textDim : colours::textFaint);
            g.drawText (db == 0.0f ? "0" : "-" + juce::String ((int) db), scaleArea.withY (yFor (-db) - 7.0f).withHeight (14.0f).withTrimmedLeft (6.0f),
                        juce::Justification::centredLeft);
        }
    }

private:
    float held = 0.0f, shown = 0.0f;
};

//==============================================================================
// Scrolling picture of the last few seconds: level going in, level coming out, reduction from the top.
class HistoryView : public juce::Component
{
public:
    explicit HistoryView (ButterfaderAudioProcessor& p) : processor (p) {}

    void setTarget (float t) { target = t; }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (colours::groove);
        g.fillRoundedRectangle (r, 10.0f);

        g.saveState();
        juce::Path clip;
        clip.addRoundedRectangle (r, 10.0f);
        g.reduceClipRegion (clip);

        auto yFor = [&] (float db) { return juce::jmap (juce::jlimit (-48.0f, 0.0f, db), 0.0f, -48.0f, r.getY() + 6.0f, r.getBottom()); };

        // grid
        for (float db = -6.0f; db >= -42.0f; db -= 6.0f)
        {
            g.setColour (colours::edge.withAlpha (0.45f));
            g.fillRect (r.getX(), yFor (db), r.getWidth(), 1.0f);
        }

        const int points = juce::jlimit (50, ButterfaderAudioProcessor::historySize - 1, (int) (r.getWidth() / 2.0f));
        const float step = r.getWidth() / (float) (points - 1);
        const int newest = processor.historyWrite.load();

        juce::Path inPath, outPath, grPath, stPath;
        inPath.startNewSubPath (r.getX(), r.getBottom());
        outPath.startNewSubPath (r.getX(), r.getBottom());
        grPath.startNewSubPath (r.getX(), r.getY());
        bool stStarted = false;

        for (int i = 0; i < points; ++i)
        {
            const auto& h = processor.history[(size_t) ((newest - (points - 1 - i) + ButterfaderAudioProcessor::historySize) % ButterfaderAudioProcessor::historySize)];
            const float x = r.getX() + i * step;
            inPath.lineTo (x, yFor (h.inputDb));
            outPath.lineTo (x, yFor (h.outputDb));
            grPath.lineTo (x, r.getY() + juce::jlimit (0.0f, 1.0f, -h.grDb / 24.0f) * r.getHeight());
            if (h.shortTerm > -48.0f)
            {
                if (! stStarted) { stPath.startNewSubPath (x, yFor (h.shortTerm)); stStarted = true; }
                else stPath.lineTo (x, yFor (h.shortTerm));
            }
        }
        inPath.lineTo (r.getRight(), r.getBottom());   inPath.closeSubPath();
        outPath.lineTo (r.getRight(), r.getBottom());  outPath.closeSubPath();
        grPath.lineTo (r.getRight(), r.getY());        grPath.closeSubPath();

        g.setColour (colours::butter.withAlpha (0.16f));
        g.fillPath (inPath);
        g.setGradientFill (juce::ColourGradient (colours::butter.withAlpha (0.85f), 0.0f, r.getY(), colours::butterDeep.withAlpha (0.35f), 0.0f, r.getBottom(), false));
        g.fillPath (outPath);
        g.setColour (colours::red.withAlpha (0.75f));
        g.fillPath (grPath);

        // target and loudness trace
        const float ty = yFor (target);
        g.setColour (colours::green.withAlpha (0.8f));
        const float dashes[] = { 6.0f, 5.0f };
        g.drawDashedLine ({ r.getX(), ty, r.getRight(), ty }, dashes, 2, 1.2f);
        g.setFont (font (10.0f, true));
        auto tag = juce::Rectangle<float> (r.getX() + 8.0f, ty - 18.0f, 52.0f, 15.0f);
        g.setColour (colours::groove.withAlpha (0.8f));
        g.fillRoundedRectangle (tag, 4.0f);
        g.setColour (colours::green);
        g.drawText ("TARGET", tag, juce::Justification::centred);

        g.setFont (font (10.0f, true));
        for (float db = -12.0f; db >= -42.0f; db -= 12.0f)
        {
            auto label = juce::Rectangle<float> (r.getRight() - 34.0f, yFor (db) - 8.0f, 28.0f, 16.0f);
            g.setColour (colours::groove.withAlpha (0.75f));
            g.fillRoundedRectangle (label, 4.0f);
            g.setColour (colours::textDim);
            g.drawText (juce::String ((int) db), label, juce::Justification::centred);
        }

        g.setColour (colours::text.withAlpha (0.9f));
        g.strokePath (stPath, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.restoreState();
    }

private:
    ButterfaderAudioProcessor& processor;
    float target = -14.0f;
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
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        const bool clipped = clips > 0;
        const auto colour = clipped ? colours::red : colours::green;

        g.setColour (clipped ? colours::red.withAlpha (live ? 0.28f : 0.14f) : colours::panelRaised);
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (colour.withAlpha (clipped ? 0.9f : 0.35f));
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);

        auto dot = r.removeFromLeft (r.getHeight()).withSizeKeepingCentre (8.0f, 8.0f);
        if (live) { g.setColour (colours::red.withAlpha (0.35f)); g.fillEllipse (dot.expanded (4.0f)); }
        g.setColour (colour);
        g.fillEllipse (dot);

        g.setColour (clipped ? colours::text : colours::textDim);
        g.setFont (font (12.0f, true));
        g.drawText (clipped ? "Source clipped x" + juce::String (clips) : "Source clean", r.withTrimmedRight (10.0f), juce::Justification::centredLeft);
    }

    void mouseUp (const juce::MouseEvent&) override { if (clips > 0 && onReset) onReset(); }

private:
    int clips = 0;
    bool live = false;
};

//==============================================================================
class CompareButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit CompareButton (juce::RangedAudioParameter& p)
        : attachment (p, [this] (float v) { on = v > 0.5f; repaint(); })
    {
        attachment.sendInitialUpdate();
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setRepaintsOnMouseActivity (true);
        setTooltip ("Hear the original at the same loudness, so you judge the processing and not the volume.");
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (on ? colours::under : isMouseOver() ? colours::panelRaised.brighter (0.06f) : colours::panelRaised);
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (on ? colours::under : colours::edge.brighter (0.2f));
        g.drawRoundedRectangle (r, 10.0f, 1.2f);

        auto badge = r.removeFromLeft (54.0f).withSizeKeepingCentre (38.0f, 24.0f);
        g.setColour (on ? colours::background.withAlpha (0.25f) : colours::groove);
        g.fillRoundedRectangle (badge, 6.0f);
        g.setFont (font (13.0f, true));
        g.setColour (on ? colours::background : colours::butter);
        g.drawText ("A/B", badge, juce::Justification::centred);

        g.setColour (on ? colours::background : colours::text);
        g.setFont (font (14.0f, true));
        g.drawText (on ? "Hearing original" : "Compare", r.removeFromTop (r.getHeight() * 0.54f), juce::Justification::bottomLeft);
        g.setColour (on ? colours::background.withAlpha (0.75f) : colours::textDim);
        g.setFont (font (11.5f));
        g.drawText (on ? "level matched, click to exit" : "original, level matched", r.withTrimmedTop (1.0f), juce::Justification::topLeft, true);
    }

    void mouseDown (const juce::MouseEvent&) override { attachment.setValueAsCompleteGesture (on ? 0.0f : 1.0f); }

private:
    juce::ParameterAttachment attachment;
    bool on = false;
};

//==============================================================================
// Pat of butter. It melts when the limiter is working hard.
class ButterLogo : public juce::Component
{
public:
    void setMelt (float amount)
    {
        amount = juce::jlimit (0.0f, 1.0f, amount);
        if (std::abs (amount - melt) > 0.01f) { melt = amount; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto plate = r.removeFromBottom (8.0f).withSizeKeepingCentre (r.getWidth(), 6.0f);
        g.setColour (colours::edge.brighter (0.4f));
        g.fillEllipse (plate);

        auto pat = r.withSizeKeepingCentre (r.getWidth() * 0.78f, r.getHeight() * 0.62f).withBottom (plate.getCentreY() + 1.0f);
        const float sag = melt * pat.getHeight() * 0.25f;
        pat = pat.withTrimmedTop (sag);

        juce::Path body;
        body.addRoundedRectangle (pat.getX(), pat.getY(), pat.getWidth(), pat.getHeight(), 4.0f + melt * 5.0f);
        if (melt > 0.05f)
        {
            // drips
            const float d = melt * 7.0f;
            body.addEllipse (pat.getX() - d * 0.6f, pat.getBottom() - 5.0f, d * 1.6f + 4.0f, 5.0f + d * 0.3f);
            body.addEllipse (pat.getRight() - d * 1.0f - 4.0f, pat.getBottom() - 4.0f, d * 1.8f + 4.0f, 4.0f + d * 0.3f);
        }
        g.setGradientFill (juce::ColourGradient (colours::butter.brighter (0.3f), pat.getX(), pat.getY(), colours::butterDeep, pat.getX(), pat.getBottom(), false));
        g.fillPath (body);
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.fillRoundedRectangle (pat.getX() + 4.0f, pat.getY() + 3.0f, pat.getWidth() * 0.4f, 2.5f, 1.2f);
    }

private:
    float melt = 0.0f;
};

} // namespace bf::ui
