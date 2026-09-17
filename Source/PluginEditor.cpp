#include "PluginEditor.h"

using namespace bf::ui;

namespace
{
    juce::String lufsText (float v) { return v < -70.0f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")) : juce::String (v, 1); }
}

ButterfaderAudioProcessorEditor::ButterfaderAudioProcessorEditor (ButterfaderAudioProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      compare (*p.apvts.getParameter ("bypass")),
      autoSwitch (*p.apvts.getParameter ("auto")),
      rider (*p.apvts.getParameter ("inputGain")),
      historyView (p),
      character (*p.apvts.getParameter ("character"), { "Clean", "Punchy", "Smooth" }, "Character"),
      leveling (*p.apvts.getParameter ("speed"), { "Gentle", "Normal", "Tight" }, "Leveling")
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (content);

    for (auto* c : std::initializer_list<juce::Component*> { &logo, &platformBox, &compare, &autoSwitch, &rider, &historyView,
                                                             &clipBadge, &resetButton, &loudnessBar, &reductionBar,
                                                             &character, &leveling, &targetKnob, &ceilingKnob })
        content.addAndMakeVisible (c);

    platformBox.addItemList (bf::platformNames(), 1);
    platformBox.setJustificationType (juce::Justification::centredLeft);
    platformAttachment = std::make_unique<juce::ComboBoxParameterAttachment> (*p.apvts.getParameter ("platform"), platformBox);
    platformBox.setTooltip ("Where is this going? Butterfader aims for that platform's loudness and peak rules.");

    clipBadge.setTooltip ("Watches the audio coming IN. If the recording itself clipped, no limiter can undo it. Click to clear.");
    clipBadge.onReset = [this] { processor.resetRequested = true; };

    resetButton.setTooltip ("Start the integrated loudness and true-peak readings again");
    resetButton.onClick = [this] { processor.resetRequested = true; };
    resetButton.setColour (juce::TextButton::buttonColourId, colours::panelRaised);
    resetButton.setColour (juce::TextButton::textColourOffId, colours::textDim);
    resetButton.setColour (juce::ComboBox::outlineColourId, colours::edge);

    for (auto* knob : { &targetKnob, &ceilingKnob })
    {
        knob->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        knob->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    }
    targetAttachment  = std::make_unique<juce::SliderParameterAttachment> (*p.apvts.getParameter ("target"), targetKnob);
    ceilingAttachment = std::make_unique<juce::SliderParameterAttachment> (*p.apvts.getParameter ("ceiling"), ceilingKnob);
    targetKnob.setTooltip ("Your own loudness target. Pick Custom in the platform menu to use it.");
    ceilingKnob.setTooltip ("Highest true peak allowed. -1 dBTP keeps streaming encoders from clipping.");
    targetKnob.setDoubleClickReturnValue (true, -14.0f);
    ceilingKnob.setDoubleClickReturnValue (true, -1.0f);

    auto* constrainer = getConstrainer();
    constrainer->setFixedAspectRatio ((double) designWidth / designHeight);
    constrainer->setSizeLimits (designWidth * 6 / 10, designHeight * 6 / 10, designWidth * 2, designHeight * 2);
    setResizable (true, true);
    setSize (designWidth, designHeight);

    timerCallback();
    startTimerHz (30);
}

ButterfaderAudioProcessorEditor::~ButterfaderAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void ButterfaderAudioProcessorEditor::resized()
{
    content.setBounds (0, 0, designWidth, designHeight);
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / designWidth, (float) getHeight() / designHeight));
}

//==============================================================================
void ButterfaderAudioProcessorEditor::timerCallback()
{
    shortTerm   = processor.shortTermLufs.load();
    momentary   = processor.momentaryLufs.load();
    integrated  = processor.integratedLufs.load();
    truePeak    = processor.outputTruePeakMaxDb.load();
    target      = processor.getTargetLufs();
    ceiling     = processor.getCeilingDb();
    ceilingCapped = processor.isCeilingCappedByPlatform();
    bypassed    = processor.apvts.getRawParameterValue ("bypass")->load() > 0.5f;
    autoOn      = processor.apvts.getRawParameterValue ("auto")->load() > 0.5f;
    platform    = (int) processor.apvts.getRawParameterValue ("platform")->load();
    learning    = processor.riderLearning.load();
    riding      = processor.riderActive.load();
    clippingNow = processor.clippingNow.load();

    rider.setLive (processor.autoGainDb.load(), autoOn, riding, learning);
    loudnessBar.setValues (shortTerm, momentary, target);
    reductionBar.setValue (processor.grPeakDb.exchange (0.0f));
    grHeld = reductionBar.getHeld();
    logo.setMelt (-grHeld / 9.0f);
    clipBadge.setState (processor.clipCount.load(), clippingNow);
    historyView.setTarget (target);
    historyView.repaint();

    targetKnob.setEnabled (platform == bf::customPlatform);
    content.repaint (headerArea.getSmallestIntegerContainer());
    content.repaint (readoutArea.getSmallestIntegerContainer());
    content.repaint (statusArea.getSmallestIntegerContainer());
    content.repaint (metersPanel.getSmallestIntegerContainer().removeFromBottom (60));
    content.repaint (controlsPanel.getSmallestIntegerContainer());
}

juce::String ButterfaderAudioProcessorEditor::statusMessage (juce::Colour& colour) const
{
    const float diff = shortTerm - target;
    colour = colours::textDim;

    if (bypassed)              { colour = colours::under; return "Hearing the original, level matched"; }
    if (shortTerm < -70.0f)    return "Waiting for audio. Press play.";
    if (clippingNow)           { colour = colours::red; return "Your source was already clipping before it got here"; }
    if (autoOn && learning)    { colour = colours::butter; return "Listening... finding your level"; }
    if (grHeld < -6.0f)        { colour = colours::red; return "Limiter working hard. Try Smooth, or Gentle leveling"; }
    if (diff > 3.0f)           { colour = colours::red; return "Too loud for " + juce::String (bf::platforms[platform].name); }
    if (diff > 1.0f)           { colour = colours::amber; return "A touch hot, easing it down"; }
    if (diff < -2.0f)          { colour = colours::under; return autoOn ? "Quiet passage, bringing it up" : "Under target. Turn on Auto, or add gain"; }
    colour = colours::green;
    return "Sitting nicely on target";
}

//==============================================================================
void ButterfaderAudioProcessorEditor::layoutContent()
{
    const float pad = 20.0f, gap = 12.0f;
    auto area = juce::Rectangle<float> (0, 0, (float) designWidth, (float) designHeight).reduced (pad, 0.0f);

    headerArea = area.removeFromTop (104.0f).withTrimmedTop (14.0f);
    controlsPanel = area.removeFromBottom (92.0f).translated (0.0f, -pad);
    area.removeFromBottom (pad + gap);

    riderPanel  = area.removeFromLeft (150.0f);
    area.removeFromLeft (gap);
    metersPanel = area.removeFromRight (196.0f);
    area.removeFromRight (gap);
    centrePanel = area;

    // header
    auto h = headerArea;
    logo.setBounds (h.removeFromLeft (46.0f).withSizeKeepingCentre (46.0f, 42.0f).toNearestInt());
    auto compareBounds = h.removeFromRight (200.0f).withSizeKeepingCentre (200.0f, 48.0f).translated (0.0f, -4.0f);
    compare.setBounds (compareBounds.toNearestInt());
    h.removeFromRight (16.0f);
    auto platformArea = h.removeFromRight (300.0f);
    platformBox.setBounds (platformArea.withSizeKeepingCentre (300.0f, 44.0f).translated (0.0f, 2.0f).toNearestInt());

    // rider
    auto rp = riderPanel.reduced (14.0f);
    rp.removeFromTop (22.0f);
    autoSwitch.setBounds (rp.removeFromTop (28.0f).toNearestInt());
    rp.removeFromTop (6.0f);
    rider.setBounds (rp.toNearestInt());

    // centre
    auto cp = centrePanel.reduced (14.0f);
    auto top = cp.removeFromTop (28.0f);
    clipBadge.setBounds (top.removeFromRight (176.0f).toNearestInt());
    statusArea = top.withTrimmedRight (8.0f).translated (14.0f, 14.0f).withX (centrePanel.getX() + 14.0f);
    cp.removeFromTop (10.0f);
    readoutArea = cp.removeFromBottom (74.0f);
    cp.removeFromBottom (8.0f);
    historyView.setBounds (cp.toNearestInt());
    resetButton.setBounds (readoutArea.getRight() - 54, (int) readoutArea.getY() + 8, 54, 22);

    // meters
    auto mp = metersPanel.reduced (10.0f, 14.0f);
    mp.removeFromBottom (44.0f);
    loudnessBar.setBounds (mp.removeFromLeft (mp.getWidth() * 0.54f).toNearestInt());
    reductionBar.setBounds (mp.toNearestInt());

    // controls
    auto ctl = controlsPanel.reduced (18.0f, 14.0f);
    character.setBounds (ctl.removeFromLeft (250.0f).toNearestInt());
    ctl.removeFromLeft (20.0f);
    leveling.setBounds (ctl.removeFromLeft (250.0f).toNearestInt());
    ctl.removeFromLeft (20.0f);
    auto knobs = ctl;
    const float kw = knobs.getWidth() / 2.0f;
    targetKnob.setBounds (knobs.removeFromLeft (kw).removeFromLeft (60.0f).withSizeKeepingCentre (60.0f, 60.0f).toNearestInt());
    ceilingKnob.setBounds (knobs.removeFromLeft (60.0f).withSizeKeepingCentre (60.0f, 60.0f).toNearestInt());
}

void ButterfaderAudioProcessorEditor::paintContent (juce::Graphics& g)
{
    g.fillAll (colours::background);

    // header
    {
        auto h = headerArea;
        h.removeFromLeft (58.0f);
        auto wordmark = h.removeFromLeft (260.0f);
        g.setColour (colours::text);
        g.setFont (font (28.0f, true).withExtraKerningFactor (-0.02f));
        const float midY = logo.getBounds().toFloat().getCentreY();
        g.drawText ("butterfader", wordmark.withY (midY - 26.0f).withHeight (32.0f), juce::Justification::centredLeft);
        g.setColour (colours::textDim);
        g.setFont (font (12.5f));
        g.drawText ("smooth, loud, never too loud", wordmark.withY (midY + 7.0f).withHeight (18.0f), juce::Justification::centredLeft);

        auto pb = platformBox.getBounds().toFloat();
        drawCaption (g, "Deliver to", pb.withY (pb.getY() - 18.0f).withHeight (16.0f));
        const auto& plat = bf::platforms[platform];
        g.setColour (colours::textDim);
        g.setFont (font (11.5f));
        const juce::String spec = juce::String (target, target == std::round (target) ? 0 : 1) + " LUFS  \xc2\xb7  " + juce::String (ceiling, 1) + " dBTP  \xc2\xb7  " + plat.note;
        g.drawText (juce::String (juce::CharPointer_UTF8 (spec.toRawUTF8())), pb.withY (pb.getBottom() + 3.0f).withHeight (16.0f).withTrimmedLeft (2.0f).withWidth (420.0f),
                    juce::Justification::centredLeft, true);
    }

    drawPanel (g, riderPanel);
    drawPanel (g, centrePanel);
    drawPanel (g, metersPanel);
    drawPanel (g, controlsPanel);

    drawCaption (g, "Rider", riderPanel.reduced (14.0f).removeFromTop (16.0f));

    // status line
    {
        juce::Colour c;
        const auto msg = statusMessage (c);
        auto s = centrePanel.reduced (14.0f).removeFromTop (28.0f).withTrimmedRight (184.0f);
        g.setColour (c);
        g.fillEllipse (s.getX(), s.getCentreY() - 4.0f, 8.0f, 8.0f);
        g.setColour (c == colours::textDim ? colours::textDim : colours::text);
        g.setFont (font (15.0f, true));
        g.drawText (msg, s.withTrimmedLeft (16.0f), juce::Justification::centredLeft, true);
    }

    // readouts
    {
        auto r = readoutArea;
        auto cell = [&] (juce::Rectangle<float> a, const juce::String& caption, const juce::String& value, const juce::String& unit,
                         juce::Colour colour, float size)
        {
            drawCaption (g, caption, a.removeFromTop (16.0f));
            g.setColour (colour);
            g.setFont (mono (size));
            const float valueWidth = mono (size).getStringWidthFloat (value);
            auto line = a.withTrimmedTop (2.0f);
            g.drawText (value, line, juce::Justification::topLeft);
            g.setColour (colours::textDim);
            g.setFont (font (12.0f, true));
            g.drawText (unit, line.withTrimmedLeft (valueWidth + 5.0f).withHeight (size * 0.95f), juce::Justification::bottomLeft);
        };

        const bool tpOver = truePeak > ceiling + 0.05f;
        cell (r.removeFromLeft (170.0f), "Integrated", lufsText (integrated), "LUFS", loudnessColour (integrated, target), 38.0f);
        cell (r.removeFromLeft (112.0f), "Short-term", lufsText (shortTerm), "LUFS", loudnessColour (shortTerm, target), 22.0f);
        cell (r.removeFromLeft (112.0f), "True peak", truePeak < -70.0f ? lufsText (truePeak) : juce::String (truePeak, 1), "dBTP",
              tpOver ? colours::red : colours::text, 22.0f);
    }

    // reduction verdict
    {
        auto v = metersPanel.reduced (12.0f, 14.0f).removeFromBottom (40.0f);
        const auto c = reductionColour (grHeld);
        const juce::String word = grHeld > -0.3f ? "Relaxed" : grHeld > -3.0f ? "Easy" : grHeld > -6.0f ? "Working" : "Too hard";
        g.setColour (c.withAlpha (0.14f));
        g.fillRoundedRectangle (v, 10.0f);
        g.setColour (c);
        g.setFont (font (14.0f, true));
        g.drawText (word, v.removeFromTop (22.0f).withTrimmedTop (4.0f), juce::Justification::centred);
        g.setColour (colours::textDim);
        g.setFont (font (11.0f));
        g.drawText (juce::String (grHeld, 1) + " dB reduction", v, juce::Justification::centred);
    }

    // knob captions and values
    auto knobText = [&] (juce::Slider& knob, const juce::String& caption, const juce::String& value, const juce::String& hint, bool dim)
    {
        auto b = knob.getBounds().toFloat();
        auto text = juce::Rectangle<float> (b.getRight() + 8.0f, b.getY() + 2.0f, 130.0f, b.getHeight() - 4.0f);
        drawCaption (g, caption, text.removeFromTop (16.0f));
        g.setColour (dim ? colours::textDim : colours::text);
        g.setFont (mono (18.0f));
        g.drawText (value, text.removeFromTop (24.0f), juce::Justification::centredLeft);
        g.setColour (colours::textFaint);
        g.setFont (font (10.5f));
        g.drawText (hint, text, juce::Justification::topLeft, true);
    };
    knobText (targetKnob, "Target", juce::String (target, 1),
              platform == bf::customPlatform ? "LUFS, custom" : "LUFS, set by platform", platform != bf::customPlatform);
    knobText (ceilingKnob, "Ceiling", juce::String (ceiling, 1),
              ceilingCapped ? "dBTP, platform limit" : "dBTP", false);
}
