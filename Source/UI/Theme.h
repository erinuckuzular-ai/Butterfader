#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace bf::ui
{

namespace colours
{
    inline const juce::Colour background  { 0xff14120e };
    inline const juce::Colour panel       { 0xff1e1b16 };
    inline const juce::Colour panelRaised { 0xff272319 };
    inline const juce::Colour edge        { 0xff332e24 };
    inline const juce::Colour groove      { 0xff0e0d0a };
    inline const juce::Colour text        { 0xfff3ead3 };
    inline const juce::Colour textDim     { 0xff9c917c };
    inline const juce::Colour textFaint   { 0xff5f5748 };
    inline const juce::Colour butter      { 0xffffd45e };
    inline const juce::Colour butterDeep  { 0xfff2b632 };
    inline const juce::Colour green       { 0xff7cd992 };
    inline const juce::Colour amber       { 0xffffb547 };
    inline const juce::Colour red         { 0xffff5e4d };
    inline const juce::Colour under       { 0xff7ba7d9 };
}

inline juce::Font font (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (height).withStyle (bold ? "Bold" : "Regular"));
}

inline juce::Font mono (float height)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height, juce::Font::bold));
}

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

inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, float radius = 14.0f)
{
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, radius);
    g.setColour (colours::edge);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

inline void drawCaption (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r,
                         juce::Justification just = juce::Justification::centredLeft)
{
    g.setColour (colours::textDim);
    g.setFont (font (10.5f, true).withExtraKerningFactor (0.12f));
    g.drawText (text.toUpperCase(), r, just, false);
}

//==============================================================================
class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        setColour (juce::ComboBox::textColourId, colours::text);
        setColour (juce::PopupMenu::backgroundColourId, colours::panelRaised);
        setColour (juce::PopupMenu::textColourId, colours::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::butter);
        setColour (juce::PopupMenu::highlightedTextColourId, colours::background);
        setColour (juce::Slider::textBoxTextColourId, colours::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, colours::butter.withAlpha (0.4f));
        setColour (juce::TextEditor::textColourId, colours::text);
        setColour (juce::TextEditor::highlightColourId, colours::butter.withAlpha (0.4f));
        setColour (juce::CaretComponent::caretColourId, colours::butter);
        setColour (juce::TooltipWindow::backgroundColourId, colours::panelRaised);
        setColour (juce::TooltipWindow::textColourId, colours::text);
        setColour (juce::TooltipWindow::outlineColourId, colours::edge);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override        { return font (17.0f, true); }
    juce::Font getPopupMenuFont() override                        { return font (15.0f); }
    juce::Font getLabelFont (juce::Label&) override               { return font (13.0f, true); }

    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (0.5f);
        g.setColour (box.isMouseOver (true) ? colours::panelRaised.brighter (0.06f) : colours::panelRaised);
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (box.hasKeyboardFocus (true) ? colours::butter : colours::edge.brighter (0.2f));
        g.drawRoundedRectangle (r, 10.0f, 1.2f);

        const float cx = (float) width - 22.0f, cy = height * 0.5f;
        juce::Path chevron;
        chevron.startNewSubPath (cx - 5.0f, cy - 2.5f);
        chevron.lineTo (cx, cy + 2.5f);
        chevron.lineTo (cx + 5.0f, cy - 2.5f);
        g.setColour (colours::butter);
        g.strokePath (chevron, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (14, 1, box.getWidth() - 44, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }

    void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override
    {
        g.fillAll (colours::panelRaised);
        g.setColour (colours::edge);
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
        g.setFont (font (15.0f, isTicked));
        g.drawText (text, r.withTrimmedLeft (12.0f), juce::Justification::centredLeft, true);
    }

    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardHeight, int& w, int& h) override
    {
        LookAndFeel_V4::getIdealPopupMenuItemSize (text, isSeparator, standardHeight, w, h);
        if (! isSeparator) { h = 30; w += 30; }
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
        const auto r = bounds.withSizeKeepingCentre (size, size);
        const auto c = r.getCentre();
        const float radius = size * 0.5f;
        const float angle = startAngle + pos * (endAngle - startAngle);
        const float alpha = slider.isEnabled() ? 1.0f : 0.4f;

        juce::Path track;
        track.addCentredArc (c.x, c.y, radius - 3.0f, radius - 3.0f, 0.0f, startAngle, endAngle, true);
        g.setColour (colours::groove);
        g.strokePath (track, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path value;
        value.addCentredArc (c.x, c.y, radius - 3.0f, radius - 3.0f, 0.0f, startAngle, angle, true);
        g.setColour (colours::butter.withAlpha (alpha));
        g.strokePath (value, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float knobR = radius - 10.0f;
        g.setGradientFill (juce::ColourGradient (colours::panelRaised.brighter (0.18f), c.x, c.y - knobR,
                                                 colours::panelRaised.darker (0.3f), c.x, c.y + knobR, false));
        g.fillEllipse (c.x - knobR, c.y - knobR, knobR * 2.0f, knobR * 2.0f);
        g.setColour (colours::edge.brighter (0.3f));
        g.drawEllipse (c.x - knobR, c.y - knobR, knobR * 2.0f, knobR * 2.0f, 1.0f);

        const auto tip = c.getPointOnCircumference (knobR - 5.0f, angle);
        const auto base = c.getPointOnCircumference (knobR * 0.35f, angle);
        g.setColour (colours::text.withAlpha (alpha));
        g.drawLine ({ base, tip }, 2.5f);
    }

    juce::Label* createSliderTextBox (juce::Slider& slider) override
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (slider);
        l->setFont (font (13.0f, true));
        l->setJustificationType (juce::Justification::centred);
        return l;
    }
};

} // namespace bf::ui
