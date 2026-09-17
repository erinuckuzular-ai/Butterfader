#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/ButterLookAndFeel.h"
#include "UI/EqDisplay.h"
#include "UI/RideDisplay.h"

class ButterfaderAudioProcessorEditor : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
public:
    explicit ButterfaderAudioProcessorEditor (ButterfaderAudioProcessor&);
    ~ButterfaderAudioProcessorEditor() override;

    void resized() override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    static constexpr int baseWidth = 1240, baseHeight = 800;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    struct Toggle
    {
        juce::ToggleButton button;
        std::unique_ptr<ButtonAttachment> attachment;
    };

    class Canvas : public juce::Component
    {
    public:
        std::function<void (juce::Graphics&)> onPaint;
        void paint (juce::Graphics& g) override { if (onPaint) onPaint (g); }
    };

    void timerCallback() override;
    void paintCanvas (juce::Graphics&);

    Knob& addKnob (const juce::String& paramId, const juce::String& label, juce::Colour accent, const juce::String& quip);
    Toggle& addToggle (const juce::String& paramId, const juce::String& text, juce::Colour accent, const juce::String& quip);
    void addQuip (juce::Component& c, const juce::String& text) { quips[&c] = text; }
    void placeKnob (Knob&, int x, int y);
    void bindBand (int band);
    void say (const juce::String& text, int frames = 90) { pokeText = text; pokeFrames = frames; }
    juce::String idleCommentary() const;
    ButterBuddy::Mood currentMood() const;

    ButterfaderAudioProcessor& processor;
    ButterLookAndFeel lookAndFeel;
    Canvas canvas;

    EqDisplay eqDisplay;
    RideDisplay rideDisplay;

    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Toggle>> toggles;
    std::map<juce::String, Knob*> knobById;
    std::map<juce::String, Toggle*> toggleById;
    std::map<juce::Component*, juce::String> quips;

    juce::ComboBox presetBox;
    juce::TextButton prevPreset { "<" }, nextPreset { ">" };
    juce::TextButton sniffButton { "SNIFF MY VOICE" }, resetLoudness { "reset" };

    // Selected EQ band strip
    juce::Label bandTitle;
    juce::ToggleButton bandOn { "on" };
    juce::ComboBox bandType;
    Knob bandFreq, bandGain, bandQ;
    std::unique_ptr<ButtonAttachment> bandOnAttachment;
    std::unique_ptr<ComboAttachment> bandTypeAttachment;

    juce::String hoverQuip, pokeText;
    int pokeFrames = 0, pokeCount = 0, frame = 0;
    int rideState = 0;
    float gainDb = 0.0f, silkHold = 0.0f, limitHold = 0.0f, deessHold = 0.0f, plosiveHold = 0.0f;
    float inLufs = -120.0f, outShort = -120.0f, outIntegrated = -120.0f, outMomentary = -120.0f, match = 0.0f;
    bool sidechainLive = false;
    ButterBuddy::Mood mood = ButterBuddy::Mood::sleepy;
    ButterBuddy::Mood lastMood = ButterBuddy::Mood::sleepy;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ButterfaderAudioProcessorEditor)
};
