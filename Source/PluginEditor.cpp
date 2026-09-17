#include "PluginEditor.h"

using namespace bf::ui;

ButterfaderAudioProcessorEditor::ButterfaderAudioProcessorEditor (ButterfaderAudioProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      modeTabs (*p.apvts.getParameter ("mode")),
      compare (*p.apvts.getParameter ("bypass")),
      stage (p),
      rider (*p.apvts.getParameter ("inputGain")),
      autoToggle (*p.apvts.getParameter ("auto"), "Auto", colours::green),
      guardToggle (*p.apvts.getParameter ("guard"), "Noise guard", colours::green),
      character (*p.apvts.getParameter ("character"), { "Clean", "Punchy", "Smooth" }, "Character"),
      debleedSelector (*p.apvts.getParameter ("debleed"), { "Off", "Gate", "Linked" }, "Debleed"),
      leveling (*p.apvts.getParameter ("speed"), { "Gentle", "Normal", "Tight" }, "Leveling")
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (content);

    for (auto* c : std::initializer_list<juce::Component*> { &modeTabs, &platformBox, &groupBox, &compare, &stage, &rider,
                                                             &autoToggle, &guardToggle, &character, &debleedSelector, &leveling,
                                                             &targetKnob, &micTargetKnob, &ceilingKnob })
        content.addAndMakeVisible (c);

    platformBox.addItemList (bf::platformNames(), 1);
    platformAttachment = std::make_unique<juce::ComboBoxParameterAttachment> (*p.apvts.getParameter ("platform"), platformBox);
    platformBox.setTooltip ("Where is this going? Butterfader aims for that platform's loudness and peak rules.");

    groupBox.addItemList ({ "Link group A", "Link group B", "Link group C", "Link group D" }, 1);
    groupAttachment = std::make_unique<juce::ComboBoxParameterAttachment> (*p.apvts.getParameter ("group"), groupBox);
    groupBox.setTooltip ("Mics in the same group duck each other's bleed. Use different groups for separate shows or scenes.");

    autoToggle.setTooltip ("Rides the gain so the level lands on the target, even from a quiet recording.");
    guardToggle.setTooltip ("Learns the background hiss and hum and never turns it up. Between phrases it's pushed back down.");

    stage.onReset = [this] { processor.resetRequested = true; };
    stage.clipBadge.onReset = [this] { processor.resetRequested = true; };

    for (auto* knob : { &targetKnob, &micTargetKnob, &ceilingKnob })
    {
        knob->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        knob->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    }
    targetAttachment    = std::make_unique<juce::SliderParameterAttachment> (*p.apvts.getParameter ("target"), targetKnob);
    micTargetAttachment = std::make_unique<juce::SliderParameterAttachment> (*p.apvts.getParameter ("micTarget"), micTargetKnob);
    ceilingAttachment   = std::make_unique<juce::SliderParameterAttachment> (*p.apvts.getParameter ("ceiling"), ceilingKnob);
    targetKnob.setTooltip ("Your own loudness target. Pick Custom in the platform menu to use it.");
    micTargetKnob.setTooltip ("How loud each voice is levelled to. Leave headroom here and let the Master instance hit the platform.");
    ceilingKnob.setTooltip ("Highest true peak allowed. -1 dBTP keeps streaming encoders from clipping.");
    targetKnob.setDoubleClickReturnValue (true, -14.0f);
    micTargetKnob.setDoubleClickReturnValue (true, -20.0f);
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
    shortTerm     = processor.shortTermLufs.load();
    integrated    = processor.integratedLufs.load();
    target        = processor.getTargetLufs();
    ceiling       = processor.getCeilingDb();
    ceilingCapped = processor.isCeilingCappedByPlatform();
    bypassed      = processor.apvts.getRawParameterValue ("bypass")->load() > 0.5f;
    autoOn        = processor.apvts.getRawParameterValue ("auto")->load() > 0.5f;
    learning      = processor.riderLearning.load();
    clippingNow   = processor.clippingNow.load();
    duck          = processor.duckDb.load();
    talkingNow    = processor.talking.load();
    duckingBleed  = processor.duckingBleed.load();
    linkedMics    = processor.linkedMics.load();
    linkedTalking = processor.linkedTalking.load();

    const int newPlatform = (int) processor.apvts.getRawParameterValue ("platform")->load();
    const int newDebleed = processor.getDebleedMode();
    if (processor.isMicMode() != micMode || newPlatform != platform || newDebleed != debleed)
    {
        micMode = processor.isMicMode();
        platform = newPlatform;
        debleed = newDebleed;
        updateModeVisibility();
    }

    // limiter meter: instant attack, gentle fall
    const float gr = juce::jlimit (-12.0f, 0.0f, processor.grPeakDb.exchange (0.0f));
    grHeld = gr < grHeld ? gr : grHeld + (gr - grHeld) * 0.12f;

    rider.setLive (processor.autoGainDb.load(), autoOn, processor.riderActive.load(), learning);
    stage.clipBadge.setState (processor.clipCount.load(), clippingNow);

    StageView::Readout r;
    r.integrated = integrated;
    r.shortTerm = shortTerm;
    r.momentary = processor.momentaryLufs.load();
    r.truePeak = processor.outputTruePeakMaxDb.load();
    r.target = target;
    r.ceiling = ceiling;
    r.grHeld = grHeld;
    r.duck = duck;
    r.status = statusMessage (r.statusColour);
    stage.setReadout (r);

    targetKnob.setEnabled (platform == bf::customPlatform);

    const auto spec = headerSpec();
    if (spec != lastSpec)
    {
        lastSpec = spec;
        layoutHeader();
    }
    content.repaint (rail.getSmallestIntegerContainer());
}

void ButterfaderAudioProcessorEditor::updateModeVisibility()
{
    platformBox.setVisible (! micMode);
    groupBox.setVisible (micMode && debleed == ButterfaderAudioProcessor::debleedLinked);
    character.setVisible (! micMode);
    debleedSelector.setVisible (micMode);
    targetKnob.setVisible (! micMode);
    micTargetKnob.setVisible (micMode);
    lastSpec = {};
    content.repaint();
}

juce::String ButterfaderAudioProcessorEditor::headerSpec() const
{
    const auto num = [] (float v, int places) { return minusSign (juce::String (v, places)); };
    if (! micMode)
        return num (target, target == std::round (target) ? 0 : 1) + " LUFS" + dot() + num (ceiling, 1) + " dBTP";

    if (debleed == ButterfaderAudioProcessor::debleedOff)  return "Debleed off";
    if (debleed == ButterfaderAudioProcessor::debleedGate) return "Gating bleed on its own";
    if (linkedMics == 0) return "No other mics linked yet";
    const juce::String others = juce::String (linkedMics) + (linkedMics == 1 ? " other mic" : " other mics");
    if (talkingNow)        return others + dot() + "this one's talking";
    if (linkedTalking > 0) return others + dot() + "someone else talking";
    return "Linked with " + others;
}

juce::String ButterfaderAudioProcessorEditor::statusMessage (juce::Colour& colour) const
{
    const float level = integrated > -70.0f ? integrated : shortTerm;   // same number the big readout shows
    const float diff = level - target;
    colour = colours::textDim;

    if (bypassed)              { colour = colours::under; return "hearing the original, level matched"; }
    if (shortTerm < -70.0f)    return "waiting for audio";
    if (clippingNow)           { colour = colours::red; return "the source was already clipping"; }
    if (autoOn && learning)    { colour = colours::butter; return "listening, finding your level"; }
    if (micMode && duck < -6.0f)
    {
        colour = colours::under;
        return duckingBleed ? "ducking bleed from another mic" : "holding background noise down";
    }
    if (grHeld < -6.0f)        { colour = colours::red; return "limiter working hard, try Smooth"; }
    if (diff > 3.0f)           { colour = colours::red; return "too loud"; }
    if (diff > 1.0f)           { colour = colours::amber; return "a touch hot, easing down"; }
    if (diff < -2.0f)          { colour = colours::under; return autoOn ? "under target, bringing it up" : "under target, turn on Auto"; }
    colour = colours::green;
    return "on target";
}

//==============================================================================
void ButterfaderAudioProcessorEditor::layoutContent()
{
    auto area = juce::Rectangle<float> (0, 0, (float) designWidth, (float) designHeight);
    topBar = area.removeFromTop (64.0f);
    rail = area.removeFromBottom (214.0f);
    stage.setBounds (area.toNearestInt());

    modeTabs.setBounds (juce::Rectangle<float> (196.0f, 12.0f, 150.0f, 40.0f).toNearestInt());
    compare.setBounds (juce::Rectangle<float> (topBar.getRight() - 24.0f - 160.0f, 10.0f, 160.0f, 44.0f).toNearestInt());
    layoutHeader();

    const float top = rail.getY() + 24.0f;
    rider.setBounds (juce::Rectangle<float> (28.0f, top, 276.0f, 106.0f).toNearestInt());
    autoToggle.setBounds (juce::Rectangle<float> (28.0f, top + 132.0f, 76.0f, 24.0f).toNearestInt());
    guardToggle.setBounds (juce::Rectangle<float> (116.0f, top + 132.0f, 130.0f, 24.0f).toNearestInt());

    character.setBounds (juce::Rectangle<float> (352.0f, top, 250.0f, 62.0f).toNearestInt());
    debleedSelector.setBounds (character.getBounds());
    leveling.setBounds (juce::Rectangle<float> (352.0f, top + 94.0f, 250.0f, 62.0f).toNearestInt());

    const float knobSize = 68.0f;
    targetKnob.setBounds (juce::Rectangle<float> (652.0f, top + 30.0f, knobSize, knobSize).toNearestInt());
    micTargetKnob.setBounds (targetKnob.getBounds());
    ceilingKnob.setBounds (juce::Rectangle<float> (800.0f, top + 30.0f, knobSize, knobSize).toNearestInt());

    dividers = { 326.0f, 628.0f };
}

// The destination (or link group) reads as a title followed by its spec, right-aligned against A/B.
void ButterfaderAudioProcessorEditor::layoutHeader()
{
    const auto specFont = font (13.5f);
    const float specW = specFont.getStringWidthFloat (lastSpec.isEmpty() ? headerSpec() : lastSpec) + 4.0f;
    const float right = (float) compare.getX() - 24.0f;
    specArea = { right - specW, 12.0f, specW, 40.0f };

    auto& box = micMode ? groupBox : platformBox;
    const float boxW = font (17.0f, "Semibold").getStringWidthFloat (box.getText()) + 30.0f;
    const auto boxBounds = juce::Rectangle<float> (specArea.getX() - 12.0f - boxW, 12.0f, boxW, 40.0f).toNearestInt();
    platformBox.setBounds (boxBounds);
    groupBox.setBounds (boxBounds);
    if (micMode && debleed != ButterfaderAudioProcessor::debleedLinked)
        specArea.setX (boxBounds.getRight() - specW);
    content.repaint (topBar.getSmallestIntegerContainer());
}

void ButterfaderAudioProcessorEditor::paintContent (juce::Graphics& g)
{
    g.fillAll (colours::background);

    // top bar
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1a1813), 0.0f, 0.0f, juce::Colour (0xff12110d), 0.0f, topBar.getBottom(), false));
    g.fillRect (topBar);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.fillRect (topBar.withHeight (1.0f));

    {
        const auto word = font (21.0f, "Bold").withExtraKerningFactor (-0.02f);
        g.setFont (word);
        const float w1 = word.getStringWidthFloat ("butter");
        g.setColour (colours::text);
        g.drawText ("butter", juce::Rectangle<float> (28.0f, 0.0f, 120.0f, topBar.getHeight()), juce::Justification::centredLeft);
        g.setColour (colours::butter);
        g.drawText ("fader", juce::Rectangle<float> (28.0f + w1, 0.0f, 120.0f, topBar.getHeight()), juce::Justification::centredLeft);
    }

    g.setFont (font (13.5f));
    g.setColour (colours::textDim);
    g.drawText (lastSpec, specArea, juce::Justification::centredLeft);

    // rail: a faceplate lit from above, with engraved dividers between sections
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1c1a14), 0.0f, rail.getY(), juce::Colour (0xff100f0b), 0.0f, rail.getBottom(), false));
    g.fillRect (rail);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillRect (rail.withHeight (1.0f));
    for (auto x : dividers)
    {
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.fillRect (x, rail.getY() + 24.0f, 1.0f, rail.getHeight() - 48.0f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillRect (x + 1.0f, rail.getY() + 24.0f, 1.0f, rail.getHeight() - 48.0f);
    }

    // knob labels and values
    auto knobText = [&] (juce::Slider& knob, const juce::String& caption, float value, const juce::String& unit, const juce::String& hint, bool dim)
    {
        auto b = knob.getBounds().toFloat();
        drawLabel (g, caption, juce::Rectangle<float> (b.getX(), rail.getY() + 24.0f, 140.0f, 18.0f));
        const auto valueText = minusSign (juce::String (value, 1));
        const auto vf = font (26.0f, "Light");
        g.setFont (vf);
        g.setColour (dim ? colours::textDim : colours::text);
        g.drawText (valueText, juce::Rectangle<float> (b.getX(), b.getBottom() + 8.0f, 140.0f, 30.0f), juce::Justification::centredLeft);
        g.setFont (font (12.5f));
        g.setColour (colours::textDim);
        g.drawText (unit, juce::Rectangle<float> (b.getX() + vf.getStringWidthFloat (valueText) + 6.0f, b.getBottom() + 14.0f, 60.0f, 22.0f), juce::Justification::centredLeft);
        g.setColour (colours::textFaint);
        g.drawText (hint, juce::Rectangle<float> (b.getX(), b.getBottom() + 36.0f, 140.0f, 16.0f), juce::Justification::centredLeft);
    };

    if (micMode)
        knobText (micTargetKnob, "Voice level", target, "LUFS", "per mic", false);
    else
        knobText (targetKnob, "Target", target, "LUFS", platform == bf::customPlatform ? "custom" : "set by platform", platform != bf::customPlatform);
    knobText (ceilingKnob, "Ceiling", ceiling, "dBTP", ceilingCapped ? (micMode ? "mic headroom" : "platform limit") : "true peak", false);
}
