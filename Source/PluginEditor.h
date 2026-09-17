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
    juce::String statusMessage (juce::Colour& colour) const;

    ButterfaderAudioProcessor& processor;
    bf::ui::LookAndFeel lookAndFeel;
    Content content { *this };
    juce::TooltipWindow tooltips { this, 600 };

    bf::ui::ButterLogo logo;
    bf::ui::SegmentedControl modeControl;
    juce::ComboBox platformBox, groupBox;
    std::unique_ptr<juce::ComboBoxParameterAttachment> platformAttachment, groupAttachment;
    bf::ui::CompareButton compare;

    bf::ui::AutoSwitch autoSwitch, guardSwitch;
    bf::ui::RiderFader rider;

    bf::ui::HistoryView historyView;
    bf::ui::ClipBadge clipBadge;
    juce::TextButton resetButton { "Reset" };

    bf::ui::LoudnessBar loudnessBar;
    bf::ui::ReductionBar reductionBar;

    bf::ui::SegmentedControl character, debleedControl, leveling;
    juce::Slider targetKnob, micTargetKnob, ceilingKnob;
    std::unique_ptr<juce::SliderParameterAttachment> targetAttachment, micTargetAttachment, ceilingAttachment;
    void updateModeVisibility();
    juce::String linkNote() const;

    // bounds of painted areas (design coordinates)
    juce::Rectangle<float> headerArea, riderPanel, centrePanel, metersPanel, controlsPanel, readoutArea, statusArea;

    // latest meter values
    float shortTerm = -120.0f, momentary = -120.0f, integrated = -120.0f, truePeak = -120.0f, target = -14.0f, ceiling = -1.0f;
    float grHeld = 0.0f;
    float duck = 0.0f;
    bool bypassed = false, autoOn = true, learning = true, riding = false, clippingNow = false, ceilingCapped = false;
    bool micMode = false, talkingNow = false, duckingBleed = false;
    int platform = 0, debleed = 0, linkedMics = 0, linkedTalking = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ButterfaderAudioProcessorEditor)
};
