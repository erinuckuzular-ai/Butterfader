#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
struct Preset
{
    const char* name;
    std::vector<std::pair<juce::String, float>> values;
};

const std::vector<Preset>& presets()
{
    static const std::vector<Preset> list {
        { "Butter (Default)", {} },
        { "Two Mics, One Table", { { "guard", 6.0f }, { "tuck", 6.0f }, { "speed", 45.0f } } },
        { "Rowdy Panel", { { "speed", 70.0f }, { "maxBoost", 12.0f }, { "maxCut", 12.0f }, { "guard", 3.0f },
                           { "tuck", 8.0f }, { "silk", 50.0f }, { "plosive", 50.0f } } },
        { "Soft Talker", { { "maxBoost", 15.0f }, { "floor", -58.0f }, { "silk", 30.0f }, { "deessSens", 40.0f },
                           { "eq4On", 1.0f }, { "eq4Freq", 3200.0f }, { "eq4Gain", 2.0f }, { "eq4Q", 0.8f } } },
        { "The Laugher", { { "maxCut", 15.0f }, { "speed", 65.0f }, { "silk", 60.0f }, { "ceiling", -1.5f } } },
        { "Just a Little Polish", { { "maxBoost", 4.0f }, { "maxCut", 4.0f }, { "speed", 25.0f }, { "silk", 20.0f } } },
        { "Guest On A Phone", { { "eq1Freq", 150.0f }, { "eq3On", 1.0f }, { "eq3Freq", 450.0f }, { "eq3Gain", -3.0f },
                                { "eq4On", 1.0f }, { "eq4Freq", 2500.0f }, { "eq4Gain", 2.5f }, { "eq6On", 1.0f },
                                { "eq6Type", 5.0f }, { "eq6Freq", 7500.0f }, { "floor", -45.0f }, { "silk", 45.0f } } },
        { "Late Night Radio", { { "eq2On", 1.0f }, { "eq2Freq", 120.0f }, { "eq2Gain", 3.0f }, { "eq4On", 1.0f },
                                { "eq4Freq", 3000.0f }, { "eq4Gain", 1.5f }, { "eq5On", 1.0f }, { "eq5Gain", 2.0f },
                                { "silk", 65.0f }, { "deessSens", 65.0f }, { "plosive", 60.0f } } },
    };
    return list;
}

using APF = juce::AudioParameterFloat;
using APB = juce::AudioParameterBool;
using APC = juce::AudioParameterChoice;

juce::NormalisableRange<float> freqRange (float lo, float hi)
{
    return { lo, hi,
             [] (float s, float e, float t) { return s * std::pow (e / s, t); },
             [] (float s, float e, float v) { return std::log (v / s) / std::log (e / s); },
             [] (float, float, float v) { return v; } };
}

juce::AudioProcessorValueTreeState::ParameterLayout buildLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    auto id = [] (const juce::String& s) { return juce::ParameterID { s, 1 }; };
    auto dB = juce::AudioParameterFloatAttributes().withLabel ("dB")
                  .withStringFromValueFunction ([] (float v, int) { return std::abs (v) < 0.05f ? juce::String ("0.0 dB") : (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; });
    auto pct = juce::AudioParameterFloatAttributes().withLabel ("%")
                   .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; });
    auto hz = juce::AudioParameterFloatAttributes().withLabel ("Hz")
                  .withStringFromValueFunction ([] (float v, int) { return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; })
                  .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue() * (t.containsIgnoreCase ("k") ? 1000.0f : 1.0f); });
    auto qAttr = juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return "Q " + juce::String (v, 2); });

    p.push_back (std::make_unique<APF> (id ("inputGain"), "Input Gain", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, dB));
    p.push_back (std::make_unique<APF> (id ("plosive"), "Plosive Reduction", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 35.0f, pct));

    p.push_back (std::make_unique<APB> (id ("eqOn"), "EQ Enabled", true));
    struct BandDefault { bool on; int type; float freq, gain, q; };
    const BandDefault defaults[] { { true, 3, 80.0f, 0.0f, 0.707f }, { false, 1, 180.0f, 0.0f, 0.707f },
                                   { false, 0, 400.0f, 0.0f, 1.0f },  { false, 0, 3000.0f, 0.0f, 1.0f },
                                   { false, 2, 10000.0f, 0.0f, 0.707f }, { false, 0, 6500.0f, 0.0f, 2.0f } };
    for (int b = 0; b < bf::ParametricEq::numBands; ++b)
    {
        const auto& d = defaults[b];
        const juce::String n = "EQ Band " + juce::String (b + 1) + " ";
        p.push_back (std::make_unique<APB> (id (ButterfaderAudioProcessor::bandId (b, "On")), n + "Enabled", d.on));
        p.push_back (std::make_unique<APC> (id (ButterfaderAudioProcessor::bandId (b, "Type")), n + "Type", ButterfaderAudioProcessor::filterTypeNames(), d.type));
        p.push_back (std::make_unique<APF> (id (ButterfaderAudioProcessor::bandId (b, "Freq")), n + "Frequency", freqRange (20.0f, 20000.0f), d.freq, hz));
        p.push_back (std::make_unique<APF> (id (ButterfaderAudioProcessor::bandId (b, "Gain")), n + "Gain", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f), d.gain, dB));
        p.push_back (std::make_unique<APF> (id (ButterfaderAudioProcessor::bandId (b, "Q")), n + "Q", freqRange (0.1f, 18.0f), d.q, qAttr));
    }

    p.push_back (std::make_unique<APB> (id ("deessOn"), "De-esser Enabled", true));
    p.push_back (std::make_unique<APF> (id ("deessFreq"), "De-esser Frequency", freqRange (2500.0f, 12000.0f), 6000.0f, hz));
    p.push_back (std::make_unique<APF> (id ("deessSens"), "De-esser Sensitivity", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f, pct));
    p.push_back (std::make_unique<APF> (id ("deessMax"), "De-esser Max Reduction", juce::NormalisableRange<float> (0.0f, 18.0f, 0.1f), 8.0f, dB));

    p.push_back (std::make_unique<APB> (id ("rideOn"), "Rider Enabled", true));
    p.push_back (std::make_unique<APF> (id ("target"), "Target Loudness", juce::NormalisableRange<float> (-30.0f, -10.0f, 0.1f), -18.0f,
                                        juce::AudioParameterFloatAttributes().withLabel ("LUFS")
                                            .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " LUFS"; })));
    p.push_back (std::make_unique<APF> (id ("maxBoost"), "Max Boost", juce::NormalisableRange<float> (0.0f, 18.0f, 0.1f), 9.0f, dB));
    p.push_back (std::make_unique<APF> (id ("maxCut"), "Max Cut", juce::NormalisableRange<float> (0.0f, 18.0f, 0.1f), 9.0f, dB));
    p.push_back (std::make_unique<APF> (id ("speed"), "Ride Speed", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f, pct));
    p.push_back (std::make_unique<APF> (id ("floor"), "Speech Floor", juce::NormalisableRange<float> (-70.0f, -20.0f, 0.5f), -50.0f, dB));

    p.push_back (std::make_unique<APB> (id ("guardOn"), "Sidechain Guard", true));
    p.push_back (std::make_unique<APF> (id ("guard"), "Guard Margin", juce::NormalisableRange<float> (-6.0f, 18.0f, 0.5f), 4.0f, dB));
    p.push_back (std::make_unique<APB> (id ("voiceLock"), "Voice Lock", false));
    p.push_back (std::make_unique<APF> (id ("strict"), "Voice Lock Strictness", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f, pct));
    p.push_back (std::make_unique<APF> (id ("tuck"), "Bleed Reduction", juce::NormalisableRange<float> (0.0f, 24.0f, 0.5f), 0.0f, dB));

    p.push_back (std::make_unique<APF> (id ("silk"), "Smoothing Compression", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 35.0f, pct));
    p.push_back (std::make_unique<APF> (id ("ceiling"), "Output Ceiling", juce::NormalisableRange<float> (-6.0f, 0.0f, 0.1f), -1.0f, dB));
    p.push_back (std::make_unique<APF> (id ("output"), "Output Gain", juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, dB));

    return { p.begin(), p.end() };
}

float param (const juce::AudioProcessorValueTreeState& s, const juce::String& id)
{
    return s.getRawParameterValue (id)->load();
}
} // namespace

//==============================================================================
ButterfaderAudioProcessor::ButterfaderAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Other Mics", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "Butterfader", buildLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout ButterfaderAudioProcessor::createLayout() { return buildLayout(); }

const juce::StringArray& ButterfaderAudioProcessor::filterTypeNames()
{
    static const juce::StringArray names { "Bell", "Low Shelf", "High Shelf", "Low Cut 12", "Low Cut 24",
                                           "High Cut 12", "High Cut 24", "Notch" };
    return names;
}

bool ButterfaderAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (in != out || (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo()))
        return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void ButterfaderAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    plosive.prepare (sampleRate);
    eq.prepare (sampleRate);
    deesser.prepare (sampleRate);
    ride.prepare (sampleRate);
    silk.prepare (sampleRate);
    ceiling.prepare (sampleRate);
    loudness.prepare (sampleRate);
    for (auto& k : outK) k.prepare (sampleRate);

    const int rideLookahead = (int) std::round (sampleRate * 0.025);
    rideDelay.prepare (rideLookahead);
    setLatencySamples (rideLookahead + ceiling.getLatency());

    inputGain.reset (sampleRate, 0.05);
    outputGain.reset (sampleRate, 0.05);
    inputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (param (apvts, "inputGain")));
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (param (apvts, "output")));

    const auto n = (size_t) juce::jmax (samplesPerBlock, 16);
    identMono.assign (n, 0.0f); levelMono.assign (n, 0.0f); scMono.assign (n, 0.0f);
    gains.assign (n, 1.0f); tapScratch.assign (n, 0.0f);
}

bf::EqBand ButterfaderAudioProcessor::getBandSettings (int b) const
{
    bf::EqBand band;
    band.on = param (apvts, bandId (b, "On")) > 0.5f;
    band.type = (bf::FilterType) (int) param (apvts, bandId (b, "Type"));
    band.freq = param (apvts, bandId (b, "Freq"));
    band.gainDb = param (apvts, bandId (b, "Gain"));
    band.q = param (apvts, bandId (b, "Q"));
    return band;
}

void ButterfaderAudioProcessor::updateDsp (int numChannels)
{
    plosive.set (param (apvts, "plosive") / 100.0f);

    const bool eqOn = param (apvts, "eqOn") > 0.5f;
    for (int b = 0; b < bf::ParametricEq::numBands; ++b)
    {
        auto band = getBandSettings (b);
        band.on = band.on && eqOn;
        eq.setBand (b, band);
    }

    deesser.set (param (apvts, "deessFreq"), param (apvts, "deessSens") / 100.0f,
                 param (apvts, "deessOn") > 0.5f ? param (apvts, "deessMax") : 0.0f);

    bf::RideEngine::Settings s;
    s.enabled = param (apvts, "rideOn") > 0.5f;
    s.targetLufs = param (apvts, "target");
    s.maxBoostDb = param (apvts, "maxBoost");
    s.maxCutDb = param (apvts, "maxCut");
    s.speed01 = param (apvts, "speed") / 100.0f;
    s.floorDb = param (apvts, "floor");
    s.guardOn = param (apvts, "guardOn") > 0.5f;
    s.guardDb = param (apvts, "guard");
    s.voiceLock = param (apvts, "voiceLock") > 0.5f;
    s.strict01 = param (apvts, "strict") / 100.0f;
    s.tuckDb = param (apvts, "tuck");
    s.channels = numChannels == 1 ? 2 : numChannels; // mono is treated as dual-mono, like the meter
    ride.setSettings (s);

    silk.set (param (apvts, "silk") / 100.0f, s.targetLufs);
    ceiling.set (param (apvts, "ceiling"));
    inputGain.setTargetValue (juce::Decibels::decibelsToGain (param (apvts, "inputGain")));
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (param (apvts, "output")));
}

void ButterfaderAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto main = getBusBuffer (buffer, true, 0);
    const int numChannels = juce::jmin (2, main.getNumChannels());
    const int numSamples = buffer.getNumSamples();
    if (numChannels == 0 || numSamples == 0)
        return;

    if ((size_t) numSamples > gains.size())
    {
        // Host sent a bigger block than promised; process in chunks.
        const int chunk = (int) gains.size();
        for (int start = 0; start < numSamples; start += chunk)
        {
            const int n = juce::jmin (chunk, numSamples - start);
            juce::AudioBuffer<float> part (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start, n);
            juce::MidiBuffer none;
            processBlock (part, none);
        }
        return;
    }

    for (int ch = main.getNumChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    updateDsp (numChannels);
    auto* const* data = main.getArrayOfWritePointers();

    // Raw mic (for bleed decisions) and sidechain.
    for (int i = 0; i < numSamples; ++i)
    {
        float m = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch) m += data[ch][i];
        identMono[(size_t) i] = m / (float) numChannels;
    }

    const float* scPtr = nullptr;
    if (getBusCount (true) > 1 && getBus (true, 1)->isEnabled())
    {
        auto sc = getBusBuffer (buffer, true, 1);
        if (sc.getNumChannels() > 0)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                float m = 0.0f;
                for (int ch = 0; ch < sc.getNumChannels(); ++ch) m += sc.getSample (ch, i);
                scMono[(size_t) i] = m / (float) sc.getNumChannels();
            }
            scPtr = scMono.data();
        }
    }

    // Input gain
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = inputGain.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch) data[ch][i] *= g;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        float m = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch) m += data[ch][i];
        tapScratch[(size_t) i] = m / (float) numChannels;
    }
    preTap.push (tapScratch.data(), numSamples);

    // Cleanup: plosives, tone, sibilance.
    plosive.process (data, numChannels, numSamples);
    eq.process (data, numChannels, numSamples);
    deesser.process (data, numChannels, numSamples);

    // Rider: listens now, acts on audio delayed by the lookahead.
    for (int i = 0; i < numSamples; ++i)
    {
        float m = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch) m += data[ch][i];
        levelMono[(size_t) i] = m / (float) numChannels;
    }
    ride.process (levelMono.data(), identMono.data(), scPtr, gains.data(), numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = gains[(size_t) i];
        for (int ch = 0; ch < numChannels; ++ch)
            data[ch][i] = rideDelay.process (ch, data[ch][i]) * g;
        rideDelay.advance();
    }

    // Finish: silk, output gain, glass ceiling.
    silk.process (data, numChannels, numSamples);
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = outputGain.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch) data[ch][i] *= g;
    }
    ceiling.process (data, numChannels, numSamples);
    loudness.process (main.getArrayOfReadPointers(), numChannels, numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        float m = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch) m += data[ch][i];
        tapScratch[(size_t) i] = m / (float) numChannels;
    }
    postTap.push (tapScratch.data(), numSamples);

    if (numChannels == 1)
        for (int ch = 1; ch < main.getNumChannels(); ++ch)
            main.copyFrom (ch, 0, main, 0, 0, numSamples);

    meterPlosiveDb.store (juce::jmax (meterPlosiveDb.load(), plosive.getReductionDb()));
    meterDeessDb.store (juce::jmax (meterDeessDb.load(), deesser.getReductionDb()));
    meterSilkDb.store (juce::jmax (meterSilkDb.load(), silk.getReductionDb()));
    meterLimitDb.store (juce::jmax (meterLimitDb.load(), ceiling.getReductionDb()));
}

//==============================================================================
int ButterfaderAudioProcessor::getNumPrograms() { return (int) presets().size(); }

const juce::String ButterfaderAudioProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, getNumPrograms()) ? presets()[(size_t) index].name : "";
}

void ButterfaderAudioProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms()))
        return;
    currentProgram = index;

    for (auto* p : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (ranged->getParameterID() != "voiceLock" && ranged->getParameterID() != "guardOn")
                ranged->setValueNotifyingHost (ranged->getDefaultValue());

    for (const auto& [paramId, value] : presets()[(size_t) index].values)
        if (auto* p = apvts.getParameter (paramId))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void ButterfaderAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    juce::StringArray profile;
    for (auto v : ride.getProfile())
        profile.add (juce::String (v, 3));
    state.setProperty ("voiceProfile", profile.joinIntoString (" "), nullptr);
    state.setProperty ("voiceProfileValid", ride.hasProfile(), nullptr);
    state.setProperty ("program", currentProgram, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ButterfaderAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    juce::StringArray tokens;
    tokens.addTokens (state.getProperty ("voiceProfile").toString(), " ", "");
    std::array<float, bf::RideEngine::numProfileValues> profile {};
    const bool valid = (bool) state.getProperty ("voiceProfileValid", false) && tokens.size() == bf::RideEngine::numProfileValues;
    for (int b = 0; b < juce::jmin (tokens.size(), bf::RideEngine::numProfileValues); ++b)
        profile[(size_t) b] = tokens[b].getFloatValue();
    ride.setProfile (profile, valid);
    currentProgram = state.getProperty ("program", 0);
    apvts.replaceState (state);
}

juce::AudioProcessorEditor* ButterfaderAudioProcessor::createEditor() { return new ButterfaderAudioProcessorEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ButterfaderAudioProcessor(); }
