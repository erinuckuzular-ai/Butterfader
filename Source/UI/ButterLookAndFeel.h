#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Chunky cartoon-sticker look (shared with ArrowEch): cream paper, thick outlines, hard shadows.
class ButterLookAndFeel : public juce::LookAndFeel_V4
{
public:
    static inline const juce::Colour paper    { 0xfff6eddc };
    static inline const juce::Colour ink      { 0xff17131c };
    static inline const juce::Colour white    { 0xfffffdf7 };
    static inline const juce::Colour grey     { 0xffd9cfbd };

    static inline const juce::Colour tomato   { 0xffff6b57 };
    static inline const juce::Colour butter   { 0xffffd23f };
    static inline const juce::Colour mint     { 0xff5fe0b7 };
    static inline const juce::Colour lilac    { 0xffb49cff };
    static inline const juce::Colour sky      { 0xff5cc8ff };
    static inline const juce::Colour bubblegum { 0xffff8fc7 };
    static inline const juce::Colour tangerine { 0xffff9f43 };
    static inline const juce::Colour lime     { 0xffb8e986 };
    static inline const juce::Colour peach    { 0xffffc4a3 };
    static inline const juce::Colour aqua     { 0xff7ee8fa };
    static inline const juce::Colour orchid   { 0xffe7a6ff };

    static juce::Font chunky (float height)
    {
        return juce::Font (juce::FontOptions ("Arial Rounded MT Bold", height, juce::Font::plain));
    }

    // Filled rounded rectangle with outline and a hard drop shadow.
    static void drawSticker (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fill,
                             float radius = 12.0f, float shadow = 5.0f, float outline = 3.0f)
    {
        g.setColour (ink);
        g.fillRoundedRectangle (r.translated (shadow, shadow), radius);
        g.setColour (fill);
        g.fillRoundedRectangle (r, radius);
        g.setColour (ink);
        g.drawRoundedRectangle (r, radius, outline);
    }

    ButterLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, ink);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, butter);
        setColour (juce::Label::textColourId, ink);
        setColour (juce::Label::textWhenEditingColourId, ink);
        setColour (juce::TextEditor::textColourId, ink);
        setColour (juce::TextEditor::highlightColourId, butter);
        setColour (juce::CaretComponent::caretColourId, ink);
        setColour (juce::ComboBox::textColourId, ink);
        setColour (juce::PopupMenu::backgroundColourId, white);
        setColour (juce::PopupMenu::textColourId, ink);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, butter);
        setColour (juce::PopupMenu::highlightedTextColourId, ink);
        setColour (juce::TextButton::textColourOffId, ink);
        setColour (juce::TextButton::textColourOnId, ink);
        setColour (juce::TextButton::buttonColourId, white);
        setColour (juce::TextButton::buttonOnColourId, butter);
        setColour (juce::BubbleComponent::backgroundColourId, white);
        setColour (juce::BubbleComponent::outlineColourId, ink);
        setColour (juce::TooltipWindow::textColourId, ink);
    }

    //==============================================================================
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        const auto area = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
        const float size = juce::jmin (area.getWidth(), area.getHeight());
        const auto bounds = area.withSizeKeepingCentre (size, size);
        const auto centre = bounds.getCentre();
        const float radius = size * 0.5f;
        const float arcThickness = juce::jmax (4.0f, radius * 0.16f);
        const float arcRadius = radius - arcThickness * 0.5f;
        const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);
        const float alpha = slider.isEnabled() ? 1.0f : 0.35f;
        const float angle = startAngle + pos * (endAngle - startAngle);

        // Track
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
        g.setColour (ink.withAlpha (alpha));
        g.strokePath (track, { arcThickness + 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
        g.setColour (white.withAlpha (alpha));
        g.strokePath (track, { arcThickness - 1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

        // Value arc: bipolar parameters grow from the top.
        const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
        const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
        if (std::abs (angle - from) > 0.01f)
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                 juce::jmin (from, angle), juce::jmax (from, angle), true);
            g.setColour (accent.darker (0.15f).withAlpha (alpha));
            g.strokePath (value, { arcThickness - 1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
        }

        // Knob body with hard shadow
        const float bodyRadius = radius - arcThickness - 5.0f;
        const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);
        g.setColour (ink.withAlpha (alpha));
        g.fillEllipse (body.translated (2.5f, 3.0f));
        g.setColour ((slider.isMouseOverOrDragging() ? accent.brighter (0.35f) : white).withAlpha (alpha));
        g.fillEllipse (body);
        g.setColour (ink.withAlpha (alpha));
        g.drawEllipse (body, 2.5f);

        // Pointer: a stubby line with a dot at the end.
        const auto tip = centre.getPointOnCircumference (bodyRadius * 0.62f, angle);
        g.drawLine ({ centre.getPointOnCircumference (bodyRadius * 0.12f, angle), tip }, 3.5f);
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (tip));
    }

    juce::Label* createSliderTextBox (juce::Slider& slider) override
    {
        auto* label = LookAndFeel_V4::createSliderTextBox (slider);
        label->setFont (chunky (12.0f));
        label->setColour (juce::Label::textColourId, ink);
        label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        return label;
    }

    //==============================================================================
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool) override
    {
        const auto accent = button.findColour (juce::ToggleButton::tickColourId);
        const bool on = button.getToggleState();
        auto area = button.getLocalBounds().toFloat().reduced (2.0f);

        const float pillHeight = juce::jmin (area.getHeight() - 3.0f, 22.0f);
        const auto pill = area.removeFromLeft (pillHeight * 1.9f).withSizeKeepingCentre (pillHeight * 1.9f, pillHeight);

        g.setColour (ink);
        g.fillRoundedRectangle (pill.translated (2.0f, 2.5f), pillHeight * 0.5f);
        g.setColour (on ? accent : (highlighted ? grey.brighter (0.2f) : grey));
        g.fillRoundedRectangle (pill, pillHeight * 0.5f);
        g.setColour (ink);
        g.drawRoundedRectangle (pill, pillHeight * 0.5f, 2.5f);

        const float knobSize = pillHeight - 7.0f;
        const auto knob = juce::Rectangle<float> (knobSize, knobSize)
                              .withCentre ({ on ? pill.getRight() - pillHeight * 0.5f : pill.getX() + pillHeight * 0.5f, pill.getCentreY() });
        g.setColour (white);
        g.fillEllipse (knob);
        g.setColour (ink);
        g.drawEllipse (knob, 2.0f);

        area.removeFromLeft (7.0f);
        g.setFont (chunky (juce::jmin (15.0f, area.getHeight() * 0.8f)));
        g.drawFittedText (button.getButtonText(), area.toNearestInt(), juce::Justification::centredLeft, 1);
    }

    //==============================================================================
    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&, bool highlighted, bool down) override
    {
        auto r = button.getLocalBounds().toFloat().reduced (1.5f);
        r.removeFromRight (3.0f);
        r.removeFromBottom (3.0f);

        const float press = down ? 3.0f : 0.0f;
        auto fill = button.getToggleState() ? button.findColour (juce::TextButton::buttonOnColourId)
                                            : button.findColour (juce::TextButton::buttonColourId);
        if (highlighted && ! down)
            fill = fill.brighter (0.15f);

        g.setColour (ink);
        g.fillRoundedRectangle (r.translated (3.0f, 3.0f), 8.0f);
        g.setColour (fill);
        g.fillRoundedRectangle (r.translated (press, press), 8.0f);
        g.setColour (ink);
        g.drawRoundedRectangle (r.translated (press, press), 8.0f, 2.5f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool down) override
    {
        auto r = button.getLocalBounds();
        r.removeFromRight (3);
        r.removeFromBottom (3);
        if (down)
            r.translate (3, 3);

        g.setColour (ink);
        g.setFont (getTextButtonFont (button, r.getHeight()));
        g.drawFittedText (button.getButtonText(), r.reduced (4, 0), juce::Justification::centred, 1);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return chunky (juce::jmin (15.0f, buttonHeight * 0.55f));
    }

    //==============================================================================
    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width - 3.0f, (float) height - 3.0f).reduced (1.5f);
        const float alpha = box.isEnabled() ? 1.0f : 0.4f;

        g.setColour (ink.withAlpha (alpha));
        g.fillRoundedRectangle (r.translated (2.5f, 2.5f), 8.0f);
        g.setColour ((box.isMouseOver (true) ? box.findColour (juce::ComboBox::arrowColourId).brighter (0.5f) : white).withAlpha (alpha));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (ink.withAlpha (alpha));
        g.drawRoundedRectangle (r, 8.0f, 2.5f);

        const auto arrowArea = r.removeFromRight (r.getHeight()).reduced (r.getHeight() * 0.32f);
        juce::Path arrow;
        arrow.startNewSubPath (arrowArea.getX(), arrowArea.getY() + arrowArea.getHeight() * 0.3f);
        arrow.lineTo (arrowArea.getCentreX(), arrowArea.getBottom() - arrowArea.getHeight() * 0.2f);
        arrow.lineTo (arrowArea.getRight(), arrowArea.getY() + arrowArea.getHeight() * 0.3f);
        g.strokePath (arrow, { 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (8, 1, box.getWidth() - box.getHeight() - 8, box.getHeight() - 5);
        label.setFont (getComboBoxFont (box));
    }

    juce::Font getComboBoxFont (juce::ComboBox& box) override
    {
        return chunky (juce::jmin (14.0f, box.getHeight() * 0.55f));
    }

    juce::Font getPopupMenuFont() override { return chunky (15.0f); }

    void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override
    {
        g.fillAll (white);
        g.setColour (ink);
        g.drawRect (0, 0, width, height, 2);
    }

    juce::Font getLabelFont (juce::Label& label) override { return chunky (label.getFont().getHeight()); }
};
