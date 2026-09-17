#include "PluginEditor.h"

using LAF = ButterLookAndFeel;

namespace
{
const juce::Rectangle<int> eqCard { 16, 96, 780, 404 }, rideCard { 808, 96, 416, 404 };
const juce::Rectangle<int> popCard { 16, 512, 150, 272 }, sssCard { 176, 512, 200, 272 }, rulesCard { 386, 512, 280, 272 },
                           bouncerCard { 676, 512, 270, 272 }, finishCard { 956, 512, 268, 272 };
const juce::Rectangle<int> bubbleArea { 292, 12, 560, 62 };

juce::String dbText (float db, int decimals = 1)
{
    return (db > 0.05f ? "+" : "") + juce::String (db, decimals);
}

juce::String lufsText (float lufs)
{
    return lufs < -69.0f ? juce::String ("--") : juce::String (lufs, 1);
}
} // namespace

//==============================================================================
ButterfaderAudioProcessorEditor::ButterfaderAudioProcessorEditor (ButterfaderAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), eqDisplay (p)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (canvas);
    canvas.onPaint = [this] (juce::Graphics& g) { paintCanvas (g); };

    //==============================================================================
    // Header
    for (int i = 0; i < processor.getNumPrograms(); ++i)
        presetBox.addItem (processor.getProgramName (i), i + 1);
    presetBox.setSelectedItemIndex (processor.getCurrentProgram(), juce::dontSendNotification);
    presetBox.setColour (juce::ComboBox::arrowColourId, LAF::butter);
    presetBox.onChange = [this]
    {
        const int index = presetBox.getSelectedItemIndex();
        if (index >= 0 && index != processor.getCurrentProgram())
        {
            processor.setCurrentProgram (index);
            say ("spreading '" + processor.getProgramName (index) + "'. delicious choice.");
            bindBand (eqDisplay.getSelected());
        }
    };
    canvas.addAndMakeVisible (presetBox);
    addQuip (presetBox, "flavours. starting points for common podcast situations. tweak freely.");

    auto stepPreset = [this] (int delta)
    {
        const int count = processor.getNumPrograms();
        presetBox.setSelectedItemIndex ((processor.getCurrentProgram() + delta + count) % count);
    };
    prevPreset.onClick = [stepPreset] { stepPreset (-1); };
    nextPreset.onClick = [stepPreset] { stepPreset (1); };
    canvas.addAndMakeVisible (prevPreset);
    canvas.addAndMakeVisible (nextPreset);
    addQuip (prevPreset, "previous flavour.");
    addQuip (nextPreset, "next flavour.");

    //==============================================================================
    // EQ
    canvas.addAndMakeVisible (eqDisplay);
    addQuip (eqDisplay, "the tone kitchen. drag dots, scroll for width, double-click to add or toggle, right-click for shapes. shift-drag = frequency only.");
    eqDisplay.onSelect = [this] (int band) { bindBand (band); };
    addToggle ("eqOn", "EQ", LAF::butter, "turn the whole EQ on/off. for honest before/after comparisons.");

    bandTitle.setFont (LAF::chunky (15.0f));
    bandTitle.setJustificationType (juce::Justification::centredLeft);
    canvas.addAndMakeVisible (bandTitle);
    bandOn.setColour (juce::ToggleButton::tickColourId, LAF::mint);
    canvas.addAndMakeVisible (bandOn);
    bandType.addItemList (ButterfaderAudioProcessor::filterTypeNames(), 1);
    bandType.setColour (juce::ComboBox::arrowColourId, LAF::butter);
    canvas.addAndMakeVisible (bandType);
    for (auto* k : { &bandFreq, &bandGain, &bandQ })
    {
        k->slider.setSliderStyle (juce::Slider::LinearBar);
        k->slider.setColour (juce::Slider::trackColourId, LAF::butter.withAlpha (0.7f));
        k->slider.setColour (juce::Slider::textBoxTextColourId, LAF::ink);
        k->slider.setColour (juce::Slider::backgroundColourId, LAF::white);
        k->slider.setColour (juce::Slider::textBoxOutlineColourId, LAF::ink);
        k->label.setFont (LAF::chunky (12.0f));
        k->label.setJustificationType (juce::Justification::centredRight);
        canvas.addAndMakeVisible (k->slider);
        canvas.addAndMakeVisible (k->label);
    }
    bandFreq.label.setText ("freq", juce::dontSendNotification);
    bandGain.label.setText ("gain", juce::dontSendNotification);
    bandQ.label.setText ("width (Q)", juce::dontSendNotification);
    addQuip (bandType, "band shape. bells for surgery, shelves for vibes, cuts for rumble and hiss.");
    addQuip (bandOn, "this band on/off.");

    //==============================================================================
    // Ride panel
    canvas.addAndMakeVisible (rideDisplay);
    addQuip (rideDisplay, "grey = mic in, butter = what goes out, red = my fader moves, purple = bleed i refused to ride. poke me.");
    rideDisplay.onPoke = [this]
    {
        static const juce::StringArray pokes {
            "hey! i'm driving here.",
            "please keep your hands inside the fader at all times.",
            "i am 80% butterfat and 100% focused.",
            "fun fact: hard limiters can't tell a laugh from a sneeze. i can. mostly.",
            "no, i will not boost the guy in the other room.",
            "ok ok. you've poked the butter. happy?"
        };
        say (pokes[pokeCount++ % pokes.size()]);
    };
    addToggle ("rideOn", "RIDE", LAF::mint, "the rider on/off. off = no fader moves at all.");

    //==============================================================================
    // Pop stopper
    addKnob ("inputGain", "Input", LAF::sky, "trim the mic before anything else. get it roughly in the ballpark.");
    addKnob ("plosive", "Pop Stop", LAF::tangerine, "catches P-pops and mic bumps and briefly thins the lows. steady bass voices are left alone.");

    // Sss snake
    addToggle ("deessOn", "on", LAF::mint, "de-esser on/off.");
    addKnob ("deessFreq", "Hiss Zone", LAF::lilac, "where the esses live. lower for dull mics, higher for bright ones.");
    addKnob ("deessSens", "Snake Eyes", LAF::lilac, "how easily the snake gets triggered. it listens relative to the voice, so level changes don't fool it.");
    addKnob ("deessMax", "Max Bite", LAF::lilac, "most it will ever turn an 'ess' down.");

    // Ride rules
    addKnob ("target", "Sweet Spot", LAF::mint, "the loudness i steer toward (LUFS). -16 to -19 is where most podcasts live.");
    addKnob ("maxBoost", "Pump Up", LAF::mint, "the most i'm allowed to lift a quiet talker.");
    addKnob ("maxCut", "Chill Out", LAF::mint, "the most i'm allowed to pull down a loud talker.");
    addKnob ("speed", "Glide", LAF::butter, "how fast i ride. low = slow and invisible, high = keeps up with lively banter.");
    addKnob ("floor", "Hush Floor", LAF::grey, "anything quieter than this is room tone. i won't ride it (so no breathing noise swells).");

    // Bleed bouncer
    addToggle ("guardOn", "sidechain", LAF::lilac, "use the 'Other Mics' sidechain. send your co-hosts' mics there and i'll never ride their bleed.");
    addToggle ("voiceLock", "voice lock", LAF::lilac, "only ride the voice i sniffed. no sidechain needed. teach me with SNIFF first.");
    addKnob ("guard", "Hog Margin", LAF::lilac, "this mic must be this much louder than the other mics before i believe it's our talker.");
    addKnob ("strict", "Nosiness", LAF::lilac, "how closely a voice must match the sniffed one. too high and i'll ignore our own talker.");
    addKnob ("tuck", "Tuck Bleed", LAF::bubblegum, "gently turn DOWN the other person's voice in this mic. 0 = just ignore it.");

    sniffButton.setColour (juce::TextButton::buttonColourId, LAF::butter);
    sniffButton.onClick = [this]
    {
        if (processor.ride.isLearning())
        {
            processor.ride.cancelLearning();
            say ("sniffing cancelled. i kept the old fingerprint, if there was one.");
        }
        else
        {
            processor.ride.startLearning();
            say ("sniffing! play 8 seconds of JUST this talker. i'll skip the silent bits.", 150);
        }
    };
    canvas.addAndMakeVisible (sniffButton);
    addQuip (sniffButton, "learn this talker's voice fingerprint. play a solo bit of them talking. it gets saved with the session.");

    // Silk & glass
    addKnob ("silk", "Silk", LAF::butter, "a slow, wide-knee compressor. rounds off the loud words without squashing the life out.");
    addKnob ("ceiling", "Glass Ceiling", LAF::sky, "the output never goes above this. a lookahead limiter that eases in instead of slamming.");
    addKnob ("output", "Output", LAF::sky, "final volume. goes before the ceiling, so it can't clip.");

    resetLoudness.setColour (juce::TextButton::buttonColourId, LAF::white);
    resetLoudness.onClick = [this] { processor.loudness.requestReset(); say ("loudness meter reset. fresh start."); };
    canvas.addAndMakeVisible (resetLoudness);
    addQuip (resetLoudness, "reset the integrated loudness reading.");

    bindBand (0);

    addMouseListener (this, true);
    setResizable (true, true);
    setResizeLimits (baseWidth * 6 / 10, baseHeight * 6 / 10, baseWidth * 2, baseHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) baseWidth / baseHeight);
    setSize (baseWidth, baseHeight);
    startTimerHz (30);
}

ButterfaderAudioProcessorEditor::~ButterfaderAudioProcessorEditor()
{
    removeMouseListener (this);
    setLookAndFeel (nullptr);
}

//==============================================================================
ButterfaderAudioProcessorEditor::Knob& ButterfaderAudioProcessorEditor::addKnob (const juce::String& paramId, const juce::String& label,
                                                                                 juce::Colour accent, const juce::String& quip)
{
    auto knob = std::make_unique<Knob>();
    knob->slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    knob->slider.setMouseDragSensitivity (180);
    knob->slider.setColour (juce::Slider::textBoxTextColourId, LAF::ink);
    knob->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    knob->slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    knob->attachment = std::make_unique<SliderAttachment> (processor.apvts, paramId, knob->slider);
    knob->label.setText (label, juce::dontSendNotification);
    knob->label.setFont (LAF::chunky (13.0f));
    knob->label.setJustificationType (juce::Justification::centred);
    knob->label.setInterceptsMouseClicks (false, false);
    canvas.addAndMakeVisible (knob->slider);
    canvas.addAndMakeVisible (knob->label);
    addQuip (knob->slider, quip);

    auto& ref = *knob;
    knobById[paramId] = knob.get();
    knobs.push_back (std::move (knob));
    return ref;
}

ButterfaderAudioProcessorEditor::Toggle& ButterfaderAudioProcessorEditor::addToggle (const juce::String& paramId, const juce::String& text,
                                                                                     juce::Colour accent, const juce::String& quip)
{
    auto toggle = std::make_unique<Toggle>();
    toggle->button.setButtonText (text);
    toggle->button.setColour (juce::ToggleButton::tickColourId, accent);
    toggle->attachment = std::make_unique<ButtonAttachment> (processor.apvts, paramId, toggle->button);
    canvas.addAndMakeVisible (toggle->button);
    addQuip (toggle->button, quip);

    auto& ref = *toggle;
    toggleById[paramId] = toggle.get();
    toggles.push_back (std::move (toggle));
    return ref;
}

void ButterfaderAudioProcessorEditor::placeKnob (Knob& k, int x, int y)
{
    k.label.setBounds (x, y, 84, 16);
    k.slider.setBounds (x, y + 16, 84, 88);
}

void ButterfaderAudioProcessorEditor::bindBand (int band)
{
    band = juce::jlimit (0, bf::ParametricEq::numBands - 1, band);
    bandOnAttachment.reset();
    bandTypeAttachment.reset();
    bandFreq.attachment.reset();
    bandGain.attachment.reset();
    bandQ.attachment.reset();

    auto& apvts = processor.apvts;
    bandOnAttachment = std::make_unique<ButtonAttachment> (apvts, ButterfaderAudioProcessor::bandId (band, "On"), bandOn);
    bandTypeAttachment = std::make_unique<ComboAttachment> (apvts, ButterfaderAudioProcessor::bandId (band, "Type"), bandType);
    bandFreq.attachment = std::make_unique<SliderAttachment> (apvts, ButterfaderAudioProcessor::bandId (band, "Freq"), bandFreq.slider);
    bandGain.attachment = std::make_unique<SliderAttachment> (apvts, ButterfaderAudioProcessor::bandId (band, "Gain"), bandGain.slider);
    bandQ.attachment = std::make_unique<SliderAttachment> (apvts, ButterfaderAudioProcessor::bandId (band, "Q"), bandQ.slider);

    const auto colour = EqDisplay::bandColours[band];
    bandTitle.setText ("band " + juce::String (band + 1), juce::dontSendNotification);
    bandTitle.setColour (juce::Label::textColourId, LAF::ink);
    for (auto* k : { &bandFreq, &bandGain, &bandQ })
        k->slider.setColour (juce::Slider::trackColourId, colour.withAlpha (0.8f));
    bandOn.setColour (juce::ToggleButton::tickColourId, colour);
    canvas.repaint();
}

//==============================================================================
void ButterfaderAudioProcessorEditor::resized()
{
    const float scale = (float) getWidth() / baseWidth;
    canvas.setBounds (0, 0, baseWidth, baseHeight);
    canvas.setTransform (juce::AffineTransform::scale (scale));

    presetBox.setBounds (870, 24, 250, 40);
    prevPreset.setBounds (1128, 24, 44, 42);
    nextPreset.setBounds (1178, 24, 44, 42);

    // EQ card
    toggleById["eqOn"]->button.setBounds (eqCard.getRight() - 90, eqCard.getY() + 8, 80, 26);
    eqDisplay.setBounds (32, 138, 748, 298);
    bandTitle.setBounds (34, 450, 70, 32);
    bandOn.setBounds (104, 452, 70, 28);
    bandType.setBounds (178, 448, 140, 36);
    bandFreq.label.setBounds (322, 454, 42, 24);   bandFreq.slider.setBounds (368, 450, 120, 30);
    bandGain.label.setBounds (492, 454, 40, 24);   bandGain.slider.setBounds (536, 450, 90, 30);
    bandQ.label.setBounds (630, 454, 64, 24);      bandQ.slider.setBounds (698, 450, 82, 30);

    // Ride card
    toggleById["rideOn"]->button.setBounds (rideCard.getRight() - 96, rideCard.getY() + 8, 86, 26);
    rideDisplay.setBounds (822, 138, 388, 298);

    // Pop stopper
    placeKnob (*knobById["inputGain"], popCard.getX() + 33, 546);
    placeKnob (*knobById["plosive"], popCard.getX() + 33, 650);

    // Sss snake
    toggleById["deessOn"]->button.setBounds (sssCard.getRight() - 72, sssCard.getY() + 8, 64, 26);
    placeKnob (*knobById["deessFreq"], sssCard.getX() + 12, 552);
    placeKnob (*knobById["deessSens"], sssCard.getX() + 104, 552);
    placeKnob (*knobById["deessMax"], sssCard.getX() + 12, 664);

    // Ride rules
    placeKnob (*knobById["target"], rulesCard.getX() + 10, 552);
    placeKnob (*knobById["maxBoost"], rulesCard.getX() + 98, 552);
    placeKnob (*knobById["maxCut"], rulesCard.getX() + 186, 552);
    placeKnob (*knobById["speed"], rulesCard.getX() + 10, 664);
    placeKnob (*knobById["floor"], rulesCard.getX() + 98, 664);

    // Bleed bouncer
    toggleById["guardOn"]->button.setBounds (bouncerCard.getX() + 12, 546, 120, 26);
    toggleById["voiceLock"]->button.setBounds (bouncerCard.getX() + 136, 546, 128, 26);
    placeKnob (*knobById["guard"], bouncerCard.getX() + 6, 580);
    placeKnob (*knobById["strict"], bouncerCard.getX() + 93, 580);
    placeKnob (*knobById["tuck"], bouncerCard.getX() + 180, 580);
    sniffButton.setBounds (bouncerCard.getX() + 14, 700, 242, 42);

    // Silk & glass
    placeKnob (*knobById["silk"], finishCard.getX() + 6, 546);
    placeKnob (*knobById["ceiling"], finishCard.getX() + 92, 546);
    placeKnob (*knobById["output"], finishCard.getX() + 178, 546);
    resetLoudness.setBounds (finishCard.getRight() - 64, 740, 54, 30);
}

//==============================================================================
void ButterfaderAudioProcessorEditor::mouseEnter (const juce::MouseEvent& e)
{
    for (auto* c = e.eventComponent; c != nullptr; c = c->getParentComponent())
    {
        const auto it = quips.find (c);
        if (it != quips.end())
        {
            hoverQuip = it->second;
            canvas.repaint (bubbleArea);
            return;
        }
    }
}

void ButterfaderAudioProcessorEditor::mouseExit (const juce::MouseEvent&)
{
    hoverQuip = {};
    canvas.repaint (bubbleArea);
}

//==============================================================================
ButterBuddy::Mood ButterfaderAudioProcessorEditor::currentMood() const
{
    using M = ButterBuddy::Mood;
    if (processor.ride.isLearning())         return M::sniffing;
    if (limitHold > 4.0f || silkHold > 9.0f) return M::sweaty;
    if (plosiveHold > 6.0f)                  return M::shocked;
    if (rideState == bf::RideEngine::bleed)  return M::suspicious;
    if (rideState == bf::RideEngine::owner)
    {
        const float target = processor.apvts.getRawParameterValue ("target")->load();
        return std::abs (inLufs + gainDb - target) < 2.5f ? M::cool : M::riding;
    }
    if (rideState == bf::RideEngine::unsure) return M::riding;
    return M::sleepy;
}

juce::String ButterfaderAudioProcessorEditor::idleCommentary() const
{
    using M = ButterBuddy::Mood;
    const int slot = (frame / 150) % 3;
    switch (mood)
    {
        case M::sniffing:
            return "sniff... sniff... keep that one talker going. " + juce::String (juce::roundToInt (processor.ride.getLearnProgress() * 100.0f)) + "% learned.";
        case M::sweaty:
            return juce::StringArray { "the glass ceiling is sweating. maybe lower Output a touch?",
                                       "that's a lot of squashing. we're silky, not crushed. remember?",
                                       "phew. somebody laughed right into the mic." }[slot];
        case M::shocked:
            return "P-POP! caught a plosive. you're welcome.";
        case M::suspicious:
            return sidechainLive ? juce::StringArray { "the other mic is louder. that's not our talker. hands off.",
                                                        "bleed detected. not riding it. i have standards.",
                                                        "nice try, co-host. you're not in my lane." }[slot]
                                 : juce::StringArray { "that voice doesn't smell like ours. ignoring it.",
                                                        "voice lock says: stranger danger. frozen.",
                                                        "not my talker. fader stays put." }[slot];
        case M::cool:
            return juce::StringArray { "right in the sweet spot. no crushing required.",
                                       "smooth as room-temperature butter.",
                                       "this is the good stuff. zero limiter abuse." }[slot];
        case M::riding:
            return juce::StringArray { "riding the fader. hands at ten and two.",
                                       "easing them toward the sweet spot...",
                                       "gliding. not grabbing." }[slot];
        case M::sleepy:
        default:
            return juce::StringArray { "shh. waiting for someone to say something.",
                                       "room tone. not touching it. zzz.",
                                       "hover anything and i'll tell you what it does." }[slot];
    }
}

void ButterfaderAudioProcessorEditor::timerCallback()
{
    ++frame;
    auto& ride = processor.ride;

    rideState = ride.meterState.load();
    gainDb = ride.meterGainDb.load() + ride.meterTuckDb.load();
    inLufs = ride.meterInputLufs.load();
    match = ride.meterMatch.load();
    sidechainLive = ride.meterSidechain.load();
    outShort = processor.loudness.shortTerm.load();
    outMomentary = processor.loudness.momentary.load();
    outIntegrated = processor.loudness.integrated.load();

    auto hold = [] (float& held, std::atomic<float>& meter)
    {
        const float v = meter.exchange (0.0f);
        held = v > held ? v : held * 0.9f;
    };
    hold (silkHold, processor.meterSilkDb);
    hold (limitHold, processor.meterLimitDb);
    hold (deessHold, processor.meterDeessDb);
    hold (plosiveHold, processor.meterPlosiveDb);

    mood = currentMood();
    if (mood != lastMood)
        lastMood = mood;

    RideDisplay::Point point;
    point.inLufs = inLufs;
    point.outLufs = outMomentary;
    point.gainDb = gainDb;
    point.state = rideState;
    rideDisplay.mood = mood;
    rideDisplay.targetLufs = processor.apvts.getRawParameterValue ("target")->load();
    rideDisplay.push (point);

    eqDisplay.refresh();

    if (pokeFrames > 0)
        --pokeFrames;

    sniffButton.setButtonText (ride.isLearning() ? "SNIFFING... " + juce::String (juce::roundToInt (ride.getLearnProgress() * 100.0f)) + "%  (stop)"
                                                 : (ride.hasProfile() ? "RE-SNIFF VOICE" : "SNIFF MY VOICE"));

    if (presetBox.getSelectedItemIndex() != processor.getCurrentProgram())
        presetBox.setSelectedItemIndex (processor.getCurrentProgram(), juce::dontSendNotification);

    canvas.repaint();
}

//==============================================================================
void ButterfaderAudioProcessorEditor::paintCanvas (juce::Graphics& g)
{
    g.fillAll (LAF::paper);

    // Polka dots
    g.setColour (LAF::grey.withAlpha (0.45f));
    for (int y = 0; y < baseHeight; y += 28)
        for (int x = (y / 28) % 2 ? 14 : 0; x < baseWidth; x += 28)
            g.fillEllipse ((float) x, (float) y, 4.0f, 4.0f);

    // Logo
    const juce::Rectangle<float> logo (16.0f, 14.0f, 262.0f, 60.0f);
    LAF::drawSticker (g, logo, LAF::butter, 14.0f, 5.0f, 3.0f);
    g.setColour (LAF::ink);
    g.setFont (LAF::chunky (31.0f));
    g.drawText ("BUTTERFADER", logo.withTrimmedBottom (16.0f).translated (0.0f, 4.0f), juce::Justification::centred);
    g.setFont (LAF::chunky (11.0f));
    g.drawText ("silky vocal rider for talky people", logo.withTrimmedTop (40.0f), juce::Justification::centred);

    // Speech bubble
    const auto bubble = bubbleArea.toFloat();
    LAF::drawSticker (g, bubble, LAF::white, 16.0f, 4.0f, 3.0f);
    const juce::String text = hoverQuip.isNotEmpty() ? hoverQuip : (pokeFrames > 0 ? pokeText : idleCommentary());
    g.setColour (LAF::ink);
    g.setFont (LAF::chunky (15.0f));
    g.drawFittedText (text, bubble.reduced (18.0f, 6.0f).toNearestInt(), juce::Justification::centredLeft, 2);

    g.setFont (LAF::chunky (11.0f));
    g.drawText ("flavour", juce::Rectangle<float> (870.0f, 6.0f, 120.0f, 16.0f), juce::Justification::left);

    auto card = [&] (juce::Rectangle<int> r, const juce::String& title, juce::Colour colour, const juce::String& subtitle)
    {
        LAF::drawSticker (g, r.toFloat(), LAF::white, 14.0f, 5.0f, 3.0f);
        const auto tab = juce::Rectangle<float> ((float) r.getX() + 12.0f, (float) r.getY() + 9.0f,
                                                 LAF::chunky (15.0f).getStringWidthFloat (title) + 20.0f, 24.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (tab, 8.0f);
        g.setColour (LAF::ink);
        g.drawRoundedRectangle (tab, 8.0f, 2.0f);
        g.setFont (LAF::chunky (15.0f));
        g.drawText (title, tab, juce::Justification::centred);
        if (subtitle.isNotEmpty())
        {
            g.setFont (LAF::chunky (11.0f));
            g.setColour (LAF::ink.withAlpha (0.55f));
            g.drawText (subtitle, juce::Rectangle<float> (tab.getRight() + 8.0f, tab.getY(), 300.0f, tab.getHeight()), juce::Justification::centredLeft);
        }
    };

    card (eqCard, "TONE KITCHEN", LAF::butter, "6-band EQ with live spectrum");
    card (rideCard, "BUTTER RIDER", LAF::mint, "");
    card (popCard, "POP STOP", LAF::tangerine, "");
    card (sssCard, "SSS SNAKE", LAF::lilac, "");
    card (rulesCard, "RIDE RULES", LAF::mint, "");
    card (bouncerCard, "BLEED BOUNCER", LAF::bubblegum, "");
    card (finishCard, "SILK + GLASS", LAF::sky, "");

    // Band strip background
    g.setColour (EqDisplay::bandColours[eqDisplay.getSelected()]);
    g.fillRoundedRectangle (30.0f, 446.0f, 70.0f, 36.0f, 8.0f);
    g.setColour (LAF::ink);
    g.drawRoundedRectangle (30.0f, 446.0f, 70.0f, 36.0f, 8.0f, 2.0f);

    // Ride status pills
    auto pill = [&] (juce::Rectangle<float> r, juce::Colour fill, const juce::String& t)
    {
        LAF::drawSticker (g, r, fill, 10.0f, 3.0f, 2.5f);
        g.setColour (LAF::ink);
        g.setFont (LAF::chunky (13.0f));
        g.drawFittedText (t, r.reduced (6.0f, 2.0f).toNearestInt(), juce::Justification::centred, 1);
    };
    const bool rideOn = processor.apvts.getRawParameterValue ("rideOn")->load() > 0.5f;
    const juce::String stateText = ! rideOn ? "RIDER OFF"
                                 : rideState == bf::RideEngine::owner  ? "RIDING"
                                 : rideState == bf::RideEngine::bleed  ? "NOT YOU, PAL"
                                 : rideState == bf::RideEngine::unsure ? "SNIFFING IT..."
                                                                       : "SILENCE";
    pill ({ 824.0f, 448.0f, 136.0f, 34.0f }, rideOn ? RideDisplay::stateColour (rideState).brighter (rideState == 0 ? 2.5f : 0.0f) : LAF::grey, stateText);
    pill ({ 968.0f, 448.0f, 120.0f, 34.0f }, sidechainLive ? LAF::lilac : LAF::grey, sidechainLive ? "SIDECHAIN ON" : "NO SIDECHAIN");
    const bool locked = processor.apvts.getRawParameterValue ("voiceLock")->load() > 0.5f;
    pill ({ 1096.0f, 448.0f, 112.0f, 34.0f }, locked && processor.ride.hasProfile() ? LAF::butter : LAF::grey,
          ! locked ? "VOICE LOCK OFF" : (processor.ride.hasProfile() ? "MATCH " + juce::String (juce::roundToInt (juce::jmax (0.0f, match) * 100.0f)) + "%" : "NOT SNIFFED"));

    // Horizontal reduction meter
    auto meter = [&] (juce::Rectangle<float> r, float db, float maxDb, juce::Colour c, const juce::String& label)
    {
        g.setColour (LAF::ink);
        g.setFont (LAF::chunky (11.0f));
        g.drawText (label, r.removeFromLeft (44.0f), juce::Justification::centredLeft);
        g.fillRoundedRectangle (r, 5.0f);
        const auto fill = r.reduced (2.0f);
        g.setColour (c);
        g.fillRoundedRectangle (fill.withWidth (fill.getWidth() * juce::jlimit (0.0f, 1.0f, db / maxDb)), 4.0f);
        g.setColour (LAF::white);
        g.drawText (db > 0.05f ? "-" + juce::String (db, 1) : "0.0", r.reduced (6.0f, 0.0f), juce::Justification::centredRight);
    };

    meter ({ (float) sssCard.getX() + 104.0f, 690.0f, 84.0f, 20.0f }, deessHold, 12.0f, LAF::lilac, "bite");
    g.setColour (LAF::ink.withAlpha (0.55f));
    g.setFont (LAF::chunky (11.0f));
    g.drawFittedText ("hover the knobs\nfor hints", juce::Rectangle<int> (sssCard.getX() + 104, 720, 88, 40), juce::Justification::centredLeft, 2);
    meter ({ (float) popCard.getX() + 12.0f, 764.0f, 126.0f, 16.0f }, plosiveHold, 12.0f, LAF::tangerine, "pops");

    // Rider readouts in the rules card
    {
        const auto r = juce::Rectangle<float> ((float) rulesCard.getX() + 190.0f, 672.0f, 80.0f, 96.0f);
        g.setColour (LAF::ink);
        g.setFont (LAF::chunky (11.0f));
        g.drawText ("mic in", r.withHeight (16.0f), juce::Justification::centred);
        g.setFont (LAF::chunky (20.0f));
        g.drawText (lufsText (inLufs), r.withY (r.getY() + 14.0f).withHeight (24.0f), juce::Justification::centred);
        g.setFont (LAF::chunky (11.0f));
        g.drawText ("fader", r.withY (r.getY() + 44.0f).withHeight (16.0f), juce::Justification::centred);
        g.setFont (LAF::chunky (20.0f));
        g.setColour (gainDb >= 0.0f ? LAF::mint.darker (0.4f) : LAF::tomato.darker (0.2f));
        g.drawText (dbText (gainDb), r.withY (r.getY() + 58.0f).withHeight (24.0f), juce::Justification::centred);
    }

    // Bouncer hint
    g.setColour (LAF::ink.withAlpha (0.55f));
    g.setFont (LAF::chunky (11.0f));
    g.drawText (processor.ride.hasProfile() ? "voice fingerprint saved with session" : "no voice fingerprint yet",
                juce::Rectangle<int> (bouncerCard.getX() + 14, 748, 242, 20), juce::Justification::centred);

    // Crushometer
    {
        meter ({ (float) finishCard.getX() + 14.0f, 660.0f, 240.0f, 18.0f }, silkHold, 12.0f, LAF::butter, "silk");
        meter ({ (float) finishCard.getX() + 14.0f, 682.0f, 240.0f, 18.0f }, limitHold, 12.0f, LAF::sky, "glass");
        const float total = silkHold + limitHold;
        const juce::String verdict = total < 2.0f ? "crush-o-meter: silky"
                                   : total < 5.0f ? "crush-o-meter: firm but fair"
                                   : total < 9.0f ? "crush-o-meter: getting crunchy"
                                                  : "crush-o-meter: CRUSHED. that's the old way!";
        g.setColour (total >= 9.0f ? LAF::tomato.darker (0.2f) : LAF::ink);
        g.setFont (LAF::chunky (12.0f));
        g.drawText (verdict, juce::Rectangle<float> ((float) finishCard.getX() + 14.0f, 704.0f, 240.0f, 16.0f), juce::Justification::centredLeft);

        auto lr = juce::Rectangle<float> ((float) finishCard.getX() + 14.0f, 726.0f, 184.0f, 50.0f);
        g.setColour (LAF::ink);
        g.fillRoundedRectangle (lr, 8.0f);
        auto cell = [&] (juce::Rectangle<float> c, const juce::String& label, const juce::String& value, juce::Colour colour)
        {
            g.setColour (LAF::white.withAlpha (0.6f));
            g.setFont (LAF::chunky (10.0f));
            g.drawText (label, c.removeFromTop (16.0f), juce::Justification::centred);
            g.setColour (colour);
            g.setFont (LAF::chunky (17.0f));
            g.drawText (value, c, juce::Justification::centred);
        };
        const float w = lr.getWidth() / 3.0f;
        const auto inner = lr.reduced (0.0f, 4.0f);
        const bool podcastReady = outIntegrated > -17.5f && outIntegrated < -14.5f;
        cell (inner.withWidth (w), "integrated", lufsText (outIntegrated), podcastReady ? LAF::mint : LAF::butter);
        cell (inner.withX (inner.getX() + w).withWidth (w), "short", lufsText (outShort), LAF::white);
        cell (inner.withX (inner.getX() + 2.0f * w).withWidth (w), "momentary", lufsText (outMomentary), LAF::white);
    }
}
