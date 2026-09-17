#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/Loudness.h"
#include "DSP/Rider.h"
#include "DSP/TruePeakLimiter.h"
#include "Platforms.h"

class ButterfaderAudioProcessor : public juce::AudioProcessor
{
public:
    ButterfaderAudioProcessor();
    ~ButterfaderAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Butterfader"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorValueTreeState apvts;

    float getTargetLufs() const;
    float getCeilingDb() const;
    bool  isCeilingCappedByPlatform() const;

    //==============================================================================
    // Meter data, written by the audio thread and read by the editor.
    struct HistoryPoint { float inputDb = -120.0f, outputDb = -120.0f, grDb = 0.0f, shortTerm = -120.0f; };
    static constexpr int historySize = 1024;

    std::array<HistoryPoint, historySize> history;
    std::atomic<int> historyWrite { 0 };

    std::atomic<float> momentaryLufs { -120.0f }, shortTermLufs { -120.0f }, integratedLufs { -120.0f };
    std::atomic<float> grNowDb { 0.0f }, grPeakDb { 0.0f };   // grPeak: deepest since the editor last read it
    std::atomic<float> autoGainDb { 0.0f }, inputPeakDb { -120.0f }, outputTruePeakMaxDb { -120.0f };
    std::atomic<int>   clipCount { 0 };
    std::atomic<bool>  clippingNow { false }, riderActive { false }, riderLearning { true };
    std::atomic<bool>  resetRequested { false };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void resetMeters();

    juce::AudioParameterBool* bypassParam = nullptr;
    std::atomic<float>* platformParam = nullptr, *targetParam = nullptr, *ceilingParam = nullptr,
                      *autoParam = nullptr, *inputGainParam = nullptr, *speedParam = nullptr,
                      *characterParam = nullptr;

    double sampleRate = 48000.0;
    int numChannels = 2;

    bf::LoudnessMeter inputMeter, outputMeter;
    bf::Rider rider;
    bf::TruePeakLimiter limiter;
    std::vector<bf::TruePeakDetector> outputPeak;

    // level-matched bypass
    std::vector<std::vector<float>> dryDelay;
    int dryPos = 0;
    double inLongEnergy = 0.0, outLongEnergy = 0.0, matchCoef = 0.0;
    float matchDb = 0.0f, bypassMix = 0.0f, bypassCoef = 0.0f;
    float manualGainDb = 0.0f, gainSmoothCoef = 0.0f;
    bool wasAuto = true;

    // clip detector
    std::vector<float> lastSample;
    std::vector<int> flatRun;
    int clipHoldSamples = 0, clipCooldown = 0;

    // history accumulation
    int historyCounter = 0, historyInterval = 960;
    float accInPeak = 0.0f, accOutPeak = 0.0f, accGrMin = 1.0f, accTp = 0.0f;
    float tpMax = 0.0f;
    float blockInPeak = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ButterfaderAudioProcessor)
};
