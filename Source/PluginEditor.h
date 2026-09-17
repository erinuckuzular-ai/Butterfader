#pragma once

#include "PluginProcessor.h"
#include "UI/Components.h"

class ButterfaderAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int designWidth = 940, designHeight = 620;

    explicit ButterfaderAudioProcessorEditor (ButterfaderAudioProcessor&);
    ~ButterfaderAudioProcessorEditor() override;

    void paint (juce::Graphics&) override {}
    void resized() override;

private:
    // Everything is laid out at a fixed design size and scaled as the window is resized.
    class Content : public juce::Component
    {
    public:
        explicit Content (ButterfaderAudioProcessorEditor& e) : editor (e) {}
        void paint (juce::Graphics& g) override { editor.paintContent (g); }
        void resized() override               { editor.layoutContent(); }
    private:
        ButterfaderAudioProcessorEditor& editor;
    };

    void timerCallback() override;
    void paintContent (juce::Graphics&);
    void layoutContent();
    void layoutHeader();
    void updateModeVisibility();
    juce::String statusMessage (juce::Colour& colour) const;
    juce::String headerSpec() const;

    ButterfaderAudioProcessor& processor;
    bf::ui::LookAndFeel lookAndFeel;
    Content content { *this };
    juce::TooltipWindow tooltips { this, 600 };

    bf::ui::ModeTabs modeTabs;
    juce::ComboBox platformBox, groupBox;
    std::unique_ptr<juce::ComboBoxParameterAttachment> platformAttachment, groupAttachment;
    bf::ui::CompareKey compare;

    bf::ui::StageView stage;

    bf::ui::RiderFader rider;
    bf::ui::LedToggle autoToggle, guardToggle;
    bf::ui::KeySelector character, debleedSelector, leveling;
    juce::Slider targetKnob, micTargetKnob, ceilingKnob;
    std::unique_ptr<juce::SliderParameterAttachment> targetAttachment, micTargetAttachment, ceilingAttachment;

    juce::Rectangle<float> topBar, rail, specArea;
    std::array<float, 2> dividers {};

    // latest meter values
    float shortTerm = -120.0f, integrated = -120.0f, target = -14.0f, ceiling = -1.0f, grHeld = 0.0f, duck = 0.0f;
    bool bypassed = false, autoOn = true, learning = true, clippingNow = false, ceilingCapped = false;
    bool micMode = false, talkingNow = false, duckingBleed = false;
    int platform = -1, debleed = -1, linkedMics = 0, linkedTalking = 0;
    juce::String lastSpec;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ButterfaderAudioProcessorEditor)
};
