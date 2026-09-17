#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace bf::ui
{

namespace colours
{
    inline const juce::Colour background { 0xff0f0e0c };
    inline const juce::Colour stage      { 0xff090807 };
    inline const juce::Colour rail       { 0xff17150f };
    inline const juce::Colour railDeep   { 0xff110f0b };
    inline const juce::Colour key        { 0xff2a2720 };
    inline const juce::Colour groove     { 0xff070605 };
    inline const juce::Colour grid       { 0xff1d1b16 };
    inline const juce::Colour text       { 0xfff4ecd8 };
    inline const juce::Colour textDim    { 0xff8d8472 };
    inline const juce::Colour textFaint  { 0xff5a5346 };
    inline const juce::Colour butter     { 0xfff2c14e };
    inline const juce::Colour butterDeep { 0xffc98f1c };
    inline const juce::Colour green      { 0xff8fe0a2 };
    inline const juce::Colour amber      { 0xffffb547 };
    inline const juce::Colour red        { 0xffff5a3c };
    inline const juce::Colour under      { 0xff7fb2e5 };
}

inline juce::Font font (float height, const char* style = "Regular")
{
    return juce::Font (juce::FontOptions (height).withStyle (style));
}

inline juce::String dot() { return juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")); }
inline juce::String minusSign (const juce::String& s) { return s.replace ("-", juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92"))); }

// Loudness relative to target: green on target, amber/red too loud, blue too quiet.
inline juce::Colour loudnessColour (float lufs, float target)
{
    if (lufs < -70.0f) return colours::textFaint;
    const float diff = lufs - target;
    if (diff > 3.0f)  return colours::red;
    if (diff > 1.0f)  return colours::amber;
    if (diff < -2.0f) return colours::under;
    return colours::green;
}

inline juce::Colour reductionColour (float grDb)
{
    if (grDb < -6.0f) return colours::red;
    if (grDb < -3.0f) return colours::amber;
    return colours::green;
}

//==============================================================================
// Physical surfaces: a raised key catches light on its top edge and casts a shadow;
// a recessed well is shaded from the top.
inline void drawRaised (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour base, bool lit = false)
{
    juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 8, { 0, 3 }).drawForRectangle (g, r.toNearestInt());
    g.setGradientFill (juce::ColourGradient (base.brighter (lit ? 0.25f : 0.16f), 0.0f, r.getY(),
                                             base.darker (0.35f), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
    g.setColour (juce::Colours::white.withAlpha (lit ? 0.35f : 0.10f));
    g.drawLine (r.getX() + radius, r.getY() + 0.75f, r.getRight() - radius, r.getY() + 0.75f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (r, radius, 0.8f);
}

inline void drawRecessed (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    g.setGradientFill (juce::ColourGradient (colours::groove, 0.0f, r.getY(), colours::railDeep, 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.6f), 0.0f, r.getY(),
                                             juce::Colours::transparentBlack, 0.0f, r.getY() + juce::jmin (8.0f, r.getHeight() * 0.5f), false));
    g.fillRoundedRectangle (r, radius);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawLine (r.getX() + radius, r.getBottom() - 0.5f, r.getRight() - radius, r.getBottom() - 0.5f, 1.0f);
}

// Text with a soft light halo (used for the big readout).
inline void drawGlowText (juce::Graphics& g, const juce::String& text, juce::Font f, juce::Colour c,
                          float x, float baseline, float glow = 0.45f)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText (f, text, x, baseline);
    juce::Path p;
    ga.createPath (p);
    juce::DropShadow (c.withAlpha (glow), 26, {}).drawForPath (g, p);
    g.setColour (c);
    g.fillPath (p);
}

inline void drawLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r,
                       juce::Justification just = juce::Justification::centredLeft)
{
    g.setColour (colours::textDim);
    g.setFont (font (12.5f));
    g.drawText (text, r, just, false);
}

//==============================================================================
class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        setColour (juce::ComboBox::textColourId, colours::text);
        setColour (juce::PopupMenu::backgroundColourId, colours::key);
        setColour (juce::PopupMenu::textColourId, colours::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::butter);
        setColour (juce::PopupMenu::highlightedTextColourId, colours::background);
        setColour (juce::TooltipWindow::backgroundColourId, colours::key);
        setColour (juce::TooltipWindow::textColourId, colours::text);
        setColour (juce::TooltipWindow::outlineColourId, juce::Colours::black);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (17.0f, "Semibold"); }
    juce::Font getPopupMenuFont() override               { return font (15.0f); }

    // The destination picker reads as a title with a chevron, not a form field.
    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        if (box.isMouseOver (true))
        {
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRoundedRectangle (0.0f, 0.0f, (float) width, (float) height, 8.0f);
        }
        const float textW = getComboBoxFont (box).getStringWidthFloat (box.getText());
        const float cx = juce::jmin ((float) width - 12.0f, 10.0f + textW + 14.0f), cy = height * 0.5f + 1.0f;
        juce::Path chevron;
        chevron.startNewSubPath (cx - 4.5f, cy - 2.5f);
        chevron.lineTo (cx, cy + 2.0f);
        chevron.lineTo (cx + 4.5f, cy - 2.5f);
        g.setColour (colours::butter);
        g.strokePath (chevron, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (2, 0, box.getWidth() - 20, box.getHeight());
        label.setFont (getComboBoxFont (box));
    }

    void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override
    {
        g.fillAll (colours::key);
        g.setColour (juce::Colours::black);
        g.drawRect (0, 0, width, height);
    }

    void drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override
    {
        if (isSeparator || hasSubMenu || icon != nullptr || textColour != nullptr || shortcutKeyText.isNotEmpty())
            return LookAndFeel_V4::drawPopupMenuItem (g, area, isSeparator, isActive, isHighlighted, isTicked, hasSubMenu,
                                                      text, shortcutKeyText, icon, textColour);
        auto r = area.toFloat().reduced (4.0f, 1.0f);
        if (isHighlighted)
        {
            g.setColour (colours::butter);
            g.fillRoundedRectangle (r, 6.0f);
        }
        g.setColour (isHighlighted ? colours::background : isTicked ? colours::butter : colours::text);
        g.setFont (font (15.0f, isTicked ? "Semibold" : "Regular"));
        g.drawText (text, r.withTrimmedLeft (12.0f), juce::Justification::centredLeft, true);
    }

    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardHeight, int& w, int& h) override
    {
        LookAndFeel_V4::getIdealPopupMenuItemSize (text, isSeparator, standardHeight, w, h);
        if (! isSeparator) { h = 30; w += 30; }
    }

    // Machined knob: glowing value arc in a recessed ring, shaded cap with a lit index line.
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
        const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
        const auto c = bounds.withSizeKeepingCentre (size, size).getCentre();
        const float radius = size * 0.5f;
        const float angle = startAngle + pos * (endAngle - startAngle);
        const bool enabled = slider.isEnabled();

        juce::Path ring;
        ring.addCentredArc (c.x, c.y, radius - 2.5f, radius - 2.5f, 0.0f, startAngle, endAngle, true);
        g.setColour (colours::groove);
        g.strokePath (ring, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path arc;
        arc.addCentredArc (c.x, c.y, radius - 2.5f, radius - 2.5f, 0.0f, startAngle, angle, true);
        const auto accent = enabled ? colours::butter : colours::textFaint;
        if (enabled)
        {
            juce::Path glow;
            juce::PathStrokeType (3.0f).createStrokedPath (glow, arc);
            juce::DropShadow (accent.withAlpha (0.6f), 8, {}).drawForPath (g, glow);
        }
        g.setColour (accent);
        g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float capR = radius - 9.0f;
        juce::Path cap;
        cap.addEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.7f), 10, { 0, 4 }).drawForPath (g, cap);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff45413a), c.x, c.y - capR,
                                                 juce::Colour (0xff15130f), c.x, c.y + capR, false));
        g.fillPath (cap);
        const float faceR = capR - 4.0f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2e2b25), c.x - faceR * 0.4f, c.y - faceR * 0.6f,
                                                 juce::Colour (0xff1b1914), c.x + faceR * 0.4f, c.y + faceR, true));
        g.fillEllipse (c.x - faceR, c.y - faceR, faceR * 2.0f, faceR * 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawEllipse (c.x - capR + 0.5f, c.y - capR + 0.5f, capR * 2.0f - 1.0f, capR * 2.0f - 1.0f, 1.0f);

        const auto index = c.getPointOnCircumference (faceR - 6.0f, angle);
        const auto indexDot = juce::Rectangle<float> (5.0f, 5.0f).withCentre (index);
        if (enabled) juce::DropShadow (colours::butter.withAlpha (0.7f), 6, {}).drawForRectangle (g, indexDot.toNearestInt());
        g.setColour (enabled ? juce::Colour (0xfffff3d0) : colours::textFaint);
        g.fillEllipse (indexDot);
    }
};

} // namespace bf::ui
