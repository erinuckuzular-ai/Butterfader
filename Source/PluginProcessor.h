#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/ParametricEq.h"
#include "DSP/SplitDynamics.h"
#include "DSP/RideEngine.h"
#include "DSP/Dynamics.h"

// Single-producer / single-consumer sample tap for the spectrum analyser.
class AnalyserTap
{
public:
    AnalyserTap() : fifo (1 << 15) { buffer.resize (1 << 15); }

    void push (const float* data, int n)
    {
        const auto scope = fifo.write (n);
        if (scope.blockSize1 > 0) std::copy (data, data + scope.blockSize1, buffer.data() + scope.startIndex1);
        if (scope.blockSize2 > 0) std::copy (data + scope.blockSize1, data + scope.blockSize1 + scope.blockSize2, buffer.data() + scope.startIndex2);
    }

    int pull (float* dest, int maxN)
    {
        const auto scope = fifo.read (std::min (maxN, fifo.getNumReady()));
        if (scope.blockSize1 > 0) std::copy (buffer.data() + scope.startIndex1, buffer.data() + scope.startIndex1 + scope.blockSize1, dest);
        if (scope.blockSize2 > 0) std::copy (buffer.data() + scope.startIndex2, buffer.data() + scope.startIndex2 + scope.blockSize2, dest + scope.blockSize1);
        return scope.blockSize1 + scope.blockSize2;
    }

private:
    juce::AbstractFifo fifo;
    std::vector<float> buffer;
};

//==============================================================================
class ButterfaderAudioProcessor : public juce::AudioProcessor
{
public:
    ButterfaderAudioProcessor();
    ~ButterfaderAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Butterfader"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static juce::String bandId (int band, const char* what) { return "eq" + juce::String (band + 1) + what; }
    static const juce::StringArray& filterTypeNames();

    bf::EqBand getBandSettings (int band) const;
    double getCurrentSampleRate() const { return currentSampleRate; }

    juce::AudioProcessorValueTreeState apvts;
    bf::RideEngine ride;
    bf::LoudnessMeter loudness;
    AnalyserTap preTap, postTap;

    std::atomic<float> meterDeessDb { 0.0f }, meterPlosiveDb { 0.0f }, meterSilkDb { 0.0f },
                       meterLimitDb { 0.0f }, meterOutLufs { -120.0f };

private:
    void updateDsp (int numChannels);

    double currentSampleRate = 48000.0;
    int currentProgram = 0;

    bf::DePlosive plosive;
    bf::ParametricEq eq;
    bf::DeEsser deesser;
    bf::SilkCompressor silk;
    bf::GlassCeiling ceiling;
    bf::DelayLine rideDelay;
    bf::KWeighting outK[2];
    double outPow = 0.0;

    juce::SmoothedValue<float> inputGain, outputGain;
    std::vector<float> identMono, levelMono, scMono, gains, tapScratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ButterfaderAudioProcessor)
};
