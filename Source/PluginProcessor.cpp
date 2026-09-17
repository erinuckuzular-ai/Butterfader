#include "PluginProcessor.h"
#if ! BUTTERFADER_NO_EDITOR
 #include "PluginEditor.h"
#endif

namespace
{
    juce::String dbText (float v, int) { return juce::String (v, 1) + " dB"; }
}

ButterfaderAudioProcessor::ButterfaderAudioProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Butterfader", createLayout())
{
    bypassParam    = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter ("bypass"));
    platformParam  = apvts.getRawParameterValue ("platform");
    targetParam    = apvts.getRawParameterValue ("target");
    ceilingParam   = apvts.getRawParameterValue ("ceiling");
    autoParam      = apvts.getRawParameterValue ("auto");
    inputGainParam = apvts.getRawParameterValue ("inputGain");
    speedParam     = apvts.getRawParameterValue ("speed");
    characterParam = apvts.getRawParameterValue ("character");
    modeParam      = apvts.getRawParameterValue ("mode");
    micTargetParam = apvts.getRawParameterValue ("micTarget");
    debleedParam   = apvts.getRawParameterValue ("debleed");
    groupParam     = apvts.getRawParameterValue ("group");
    guardParam     = apvts.getRawParameterValue ("guard");

    linkSlot = bf::MicLink::acquire();
}

ButterfaderAudioProcessor::~ButterfaderAudioProcessor()
{
    bf::MicLink::release (linkSlot);
}

juce::AudioProcessorValueTreeState::ParameterLayout ButterfaderAudioProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode", StringArray { "Mic", "Master" }, masterMode));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "platform", 1 }, "Platform", bf::platformNames(), 0));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "target", 1 }, "Custom target",
        NormalisableRange<float> (-30.0f, -6.0f, 0.5f), -14.0f,
        AudioParameterFloatAttributes().withLabel ("LUFS").withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " LUFS"; })));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "micTarget", 1 }, "Voice level",
        NormalisableRange<float> (-30.0f, -12.0f, 0.5f), -20.0f,
        AudioParameterFloatAttributes().withLabel ("LUFS").withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " LUFS"; })));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "ceiling", 1 }, "Ceiling",
        NormalisableRange<float> (-6.0f, 0.0f, 0.1f), -1.0f,
        AudioParameterFloatAttributes().withLabel ("dBTP").withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dBTP"; })));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "auto", 1 }, "Auto level", true));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "inputGain", 1 }, "Input gain",
        NormalisableRange<float> (-24.0f, 30.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText)));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "speed", 1 }, "Leveling", StringArray { "Gentle", "Normal", "Tight" }, 1));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "character", 1 }, "Character", StringArray { "Clean", "Punchy", "Smooth" }, 0));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "guard", 1 }, "Noise guard", true));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "debleed", 1 }, "Debleed", StringArray { "Off", "Gate", "Linked" }, debleedLinked));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "group", 1 }, "Link group", StringArray { "A", "B", "C", "D" }, 0));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass (level matched)", false));

    return { p.begin(), p.end() };
}

float ButterfaderAudioProcessor::getTargetLufs() const
{
    if (isMicMode()) return micTargetParam->load();
    const int platform = juce::jlimit (0, bf::numPlatforms - 1, (int) platformParam->load());
    return platform == bf::customPlatform ? targetParam->load() : bf::platforms[platform].targetLufs;
}

float ButterfaderAudioProcessor::getCeilingDb() const
{
    if (isMicMode()) return juce::jmin (ceilingParam->load(), micCeilingDb);
    const int platform = juce::jlimit (0, bf::numPlatforms - 1, (int) platformParam->load());
    return juce::jmin (ceilingParam->load(), bf::platforms[platform].maxTruePeak);
}

bool ButterfaderAudioProcessor::isCeilingCappedByPlatform() const
{
    if (isMicMode()) return micCeilingDb < ceilingParam->load() - 0.05f;
    const int platform = juce::jlimit (0, bf::numPlatforms - 1, (int) platformParam->load());
    return bf::platforms[platform].maxTruePeak < ceilingParam->load() - 0.05f;
}

bool ButterfaderAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

//==============================================================================
void ButterfaderAudioProcessor::prepareToPlay (double newSampleRate, int)
{
    const bool rateChanged = newSampleRate != sampleRate;
    sampleRate = newSampleRate;
    numChannels = juce::jmax (1, getTotalNumOutputChannels());

    inputMeter.prepare (sampleRate, numChannels);
    outputMeter.prepare (sampleRate, numChannels);
    rider.prepare (sampleRate);
    if (rateChanged)
    {
        rider.resetLearning();
        noiseFloor.reset();
    }

    limiter.prepare (sampleRate, numChannels);
    outputPeak.assign ((size_t) numChannels, {});
    setLatencySamples (limiter.getLatencySamples());

    dryDelay.assign ((size_t) numChannels, std::vector<float> ((size_t) limiter.getLatencySamples(), 0.0f));
    dryPos = 0;

    controlDt = controlInterval / sampleRate;
    duckOpenCoef  = 1.0f - std::exp (-1.0f / (float) (sampleRate * 0.004));
    duckCloseCoef = 1.0f - std::exp (-1.0f / (float) (sampleRate * 0.12));
    controlCounter = 0; tickEnergy = 0.0;

    matchCoef = 1.0 - std::exp (-1.0 / (sampleRate * 3.0));
    bypassCoef = 1.0f - std::exp (-1.0f / (float) (sampleRate * 0.015));
    gainSmoothCoef = 1.0f - std::exp (-1.0f / (float) (sampleRate * 0.02));
    bypassMix = bypassParam->get() ? 1.0f : 0.0f;
    manualGainDb = inputGainParam->load();

    lastSample.assign ((size_t) numChannels, 0.0f);
    flatRun.assign ((size_t) numChannels, 0);
    historyInterval = juce::jmax (1, (int) (sampleRate / 50.0));
    resetMeters();
}

void ButterfaderAudioProcessor::resetMeters()
{
    inputMeter.reset();
    outputMeter.reset();
    clipCount = 0;
    clippingNow = false;
    clipHoldSamples = 0;
    tpMax = 0.0f;
    outputTruePeakMaxDb = -120.0f;
    integratedLufs = -120.0f;
}

//==============================================================================
// Runs every 32 samples: noise floor, who is talking, and how far to duck (bleed / noise).
void ButterfaderAudioProcessor::updateControl (float riderGainDb, bool guardOn, int debleed, bool micModeOn,
                                               float target, const bf::MicLink::Others& others)
{
    const double tick = tickEnergy / controlInterval;
    tickEnergy = 0.0;

    const double attack = 1.0 - std::exp (-controlDt / 0.005), release = 1.0 - std::exp (-controlDt / 0.08);
    fastEnergy += (tick - fastEnergy) * (tick > fastEnergy ? attack : release);

    const double smooth = 1.0 - std::exp (-controlDt / 0.2);
    slowEnergy += (tick - slowEnergy) * smooth;

    const float levelDb = (float) bf::energyToLufs (fastEnergy);           // raw input level, fast
    const float steadyDb = (float) bf::energyToLufs (slowEnergy);          // raw input level, smoothed
    const float floorDb = noiseFloor.update (steadyDb, rider.getSpeechLufs(), rider.hasEstimate(), controlDt);
    const float levelledDb = levelDb + riderGainDb;                         // what it sounds like after the rider
    const bool aboveNoise = levelDb > floorDb + 12.0f && levelDb > -65.0f;

    // --- mic link: share of the room's (levelled) speech energy that is ours
    const bool linked = micModeOn && debleed == debleedLinked && linkSlot >= 0;
    const double myEnergy = aboveNoise ? bf::lufsToEnergy (levelledDb) : 0.0;
    const bool haveOthers = linked && others.count > 0;
    const double share = haveOthers ? myEnergy / std::max (1.0e-12, myEnergy + others.energy) : 1.0;

    isTalking = aboveNoise && share > 0.35;
    if (linkSlot >= 0)
    {
        auto& slot = bf::MicLink::slots()[(size_t) linkSlot];
        slot.linked = linked;
        slot.group = (int) groupParam->load();
        slot.energy = (float) myEnergy;
        slot.talking = isTalking;
        slot.updatedMs = bf::MicLink::now();
    }

    // the rider only learns from real speech on this mic, never from noise or bleed
    rider.setLearningAllowed (! guardOn || aboveNoise ? (! haveOthers || share > 0.35) : false);

    // --- debleed
    float debleedDb = 0.0f;
    if (micModeOn && debleed == debleedLinked && haveOthers)
        debleedDb = juce::jlimit (-debleedDepthDb, 0.0f, 10.0f * (float) std::log10 (std::max (share, 1.0e-6)));
    else if (micModeOn && debleed != debleedOff)
    {
        const float threshold = target - 6.0f;   // bleed usually arrives well below the mic's own voice
        if (levelledDb < threshold)
            debleedDb = juce::jmax (-debleedDepthDb, (levelledDb - threshold) * 3.0f);
    }

    // --- noise guard: between phrases, never let noise come out louder than it went in
    float guardDb = 0.0f;
    if (guardOn)
    {
        const float margin = levelDb - floorDb;
        const float depth = juce::jlimit (0.0f, 30.0f, riderGainDb) + 6.0f;
        if (margin < 12.0f)
            guardDb = juce::jmax (-depth, -(12.0f - margin) * 3.0f);
    }

    float wanted = juce::jmin (debleedDb, guardDb);

    // short hold so word endings aren't clipped off
    if (wanted > -1.0f) holdSeconds = 0.12;
    else if (holdSeconds > 0.0) { holdSeconds -= controlDt; wanted = 0.0f; }

    duckTargetDb = wanted;
    duckingBleed = debleedDb < -3.0f && debleedDb <= guardDb;
}

void ButterfaderAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int chans = juce::jmin (numChannels, buffer.getNumChannels());
    if (chans <= 0 || n == 0) return;

    if (resetRequested.exchange (false))
        resetMeters();

    const bool  micModeOn = isMicMode();
    const float target  = getTargetLufs();
    const float ceiling = bf::dbToGain (getCeilingDb() - 0.1f);  // small margin for inter-sample error
    const bool  autoOn  = autoParam->load() > 0.5f;
    const bool  guardOn = guardParam->load() > 0.5f;
    const int   debleed = (int) debleedParam->load();
    const int   speed   = (int) speedParam->load();
    const int   character = (int) characterParam->load();
    const float bypassTarget = bypassParam->get() ? 1.0f : 0.0f;
    const float manualTarget = inputGainParam->load();

    const auto others = (micModeOn && debleed == debleedLinked && linkSlot >= 0)
                            ? bf::MicLink::others (linkSlot, (int) groupParam->load()) : bf::MicLink::Others {};

    if (autoOn && ! wasAuto) rider.setGainDb (manualGainDb);
    if (! autoOn && wasAuto) manualGainDb = rider.getGainDb();
    wasAuto = autoOn;

    std::array<float*, 8> frame {};
    std::array<const float*, 8> readFrame {};
    std::array<float, 8> dry {};
    auto* const* channelData = buffer.getArrayOfWritePointers();
    const int latency = limiter.getLatencySamples();
    double lastOutputEnergy = 0.0;

    for (int i = 0; i < n; ++i)
    {
        // ---- source analysis: clipping + input loudness -------------------------------------
        for (int ch = 0; ch < chans; ++ch)
        {
            const float x = channelData[ch][i];
            const float a = std::abs (x);
            blockInPeak = juce::jmax (blockInPeak, a);

            const bool flat = a >= 0.5f && std::abs (x - lastSample[(size_t) ch]) < 1.0e-7f;
            flatRun[(size_t) ch] = flat ? flatRun[(size_t) ch] + 1 : 0;
            lastSample[(size_t) ch] = x;

            if ((flatRun[(size_t) ch] == 3 || a > 1.0001f) && clipCooldown == 0)
            {
                clipCount.fetch_add (1);
                clipCooldown = (int) (sampleRate * 0.05);
                clipHoldSamples = (int) (sampleRate * 1.5);
            }
            readFrame[(size_t) ch] = channelData[ch] + i;
            frame[(size_t) ch] = channelData[ch] + i;
            dry[(size_t) ch] = x;
        }
        if (clipCooldown > 0) --clipCooldown;
        if (clipHoldSamples > 0) --clipHoldSamples;

        const double inEnergy = inputMeter.pushFrame (readFrame.data(), 0);
        tickEnergy += inEnergy;

        // ---- gain ---------------------------------------------------------------------------
        float gainDb;
        if (autoOn)
            gainDb = rider.push (inEnergy, lastOutputEnergy, target, speed);
        else
        {
            manualGainDb += (manualTarget - manualGainDb) * gainSmoothCoef;
            gainDb = manualGainDb;
        }

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updateControl (gainDb, guardOn, debleed, micModeOn, target, others);
        }

        const float gain = bf::dbToGain (gainDb);
        float preLimitPeak = 0.0f;
        for (int ch = 0; ch < chans; ++ch)
        {
            channelData[ch][i] *= gain;
            preLimitPeak = juce::jmax (preLimitPeak, std::abs (channelData[ch][i]));
        }

        // ---- limiter, then debleed / noise ducking on the delayed audio (free look-ahead) ----
        const float g = limiter.processFrame (frame.data(), ceiling, character);

        duckSmoothedDb += (duckTargetDb - duckSmoothedDb) * (duckTargetDb > duckSmoothedDb ? duckOpenCoef : duckCloseCoef);
        if (duckSmoothedDb < -0.01f)
        {
            const float duckGain = bf::dbToGain (duckSmoothedDb);
            for (int ch = 0; ch < chans; ++ch)
                channelData[ch][i] *= duckGain;
        }

        lastOutputEnergy = outputMeter.pushFrame (readFrame.data(), 0);

        float outPeak = 0.0f, tp = 0.0f;
        for (int ch = 0; ch < chans; ++ch)
        {
            outPeak = juce::jmax (outPeak, std::abs (channelData[ch][i]));
            tp = juce::jmax (tp, outputPeak[(size_t) ch].push (channelData[ch][i]));
        }
        tpMax = juce::jmax (tpMax, tp);

        // ---- level-matched bypass -----------------------------------------------------------
        if (rider.isActive() || ! autoOn)
        {
            inLongEnergy  += (inEnergy - inLongEnergy) * matchCoef;
            outLongEnergy += (lastOutputEnergy - outLongEnergy) * matchCoef;
        }
        bypassMix += (bypassTarget - bypassMix) * bypassCoef;

        if (bypassMix > 1.0e-4f)
        {
            if (inLongEnergy > 1.0e-10 && outLongEnergy > 1.0e-10)
                matchDb = (float) juce::jlimit (-30.0, 30.0, bf::energyToLufs (outLongEnergy) - bf::energyToLufs (inLongEnergy));
            const float matchGain = bf::dbToGain (matchDb);

            for (int ch = 0; ch < chans; ++ch)
            {
                auto& line = dryDelay[(size_t) ch];
                const float delayed = juce::jlimit (-1.0f, 1.0f, line[(size_t) dryPos] * matchGain);
                line[(size_t) dryPos] = dry[(size_t) ch];
                channelData[ch][i] += (delayed - channelData[ch][i]) * bypassMix;
            }
        }
        else
        {
            for (int ch = 0; ch < chans; ++ch)
                dryDelay[(size_t) ch][(size_t) dryPos] = dry[(size_t) ch];
        }
        dryPos = (dryPos + 1) % latency;

        // ---- metering -----------------------------------------------------------------------
        accInPeak  = juce::jmax (accInPeak, preLimitPeak);
        accOutPeak = juce::jmax (accOutPeak, outPeak);
        accGrMin   = juce::jmin (accGrMin, g);
        accDuckMin = juce::jmin (accDuckMin, duckSmoothedDb);

        if (++historyCounter >= historyInterval)
        {
            historyCounter = 0;
            const int w = (historyWrite.load() + 1) % historySize;
            auto& h = history[(size_t) w];
            h.inputDb = bf::gainToDb (accInPeak);
            h.outputDb = bf::gainToDb (accOutPeak);
            h.grDb = bf::gainToDb (accGrMin);
            h.duckDb = accDuckMin;
            h.shortTerm = (float) outputMeter.getShortTerm();
            historyWrite = w;

            grNowDb = h.grDb;
            grPeakDb = juce::jmin (grPeakDb.load(), h.grDb);
            accInPeak = accOutPeak = 0.0f; accGrMin = 1.0f; accDuckMin = 0.0f;
        }
    }

    for (int ch = chans; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, n);

    momentaryLufs  = (float) outputMeter.getMomentary();
    shortTermLufs  = (float) outputMeter.getShortTerm();
    integratedLufs = (float) outputMeter.getIntegrated();
    autoGainDb     = autoOn ? rider.getGainDb() : manualGainDb;
    inputPeakDb    = bf::gainToDb (blockInPeak);
    blockInPeak    = 0.0f;
    outputTruePeakMaxDb = bf::gainToDb (tpMax);
    clippingNow    = clipHoldSamples > 0;
    riderActive    = autoOn && rider.isActive();
    riderLearning  = autoOn && rider.isLearning();
    duckDb         = duckSmoothedDb;
    noiseFloorDb   = noiseFloor.get();
    talking        = isTalking;
    linkedMics     = others.count;
    linkedTalking  = others.talking;
}

//==============================================================================
void ButterfaderAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void ButterfaderAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* ButterfaderAudioProcessor::createEditor()
{
   #if BUTTERFADER_NO_EDITOR
    return nullptr;
   #else
    return new ButterfaderAudioProcessorEditor (*this);
   #endif
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ButterfaderAudioProcessor();
}
