#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/PluginProcessor.h"

// Feeds audio through the processor, then renders the editor to PNGs: UISnapshot <outDir>
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());

    auto render = [&] (const juce::String& name, float sourceLufs, float driveDb, bool bypass, bool manual, int platform, float scale)
    {
        auto processor = std::unique_ptr<ButterfaderAudioProcessor> (dynamic_cast<ButterfaderAudioProcessor*> (createPluginFilter()));
        auto set = [&] (const char* id, float v) { auto* p = processor->apvts.getParameter (id); p->setValueNotifyingHost (p->convertTo0to1 (v)); };
        set ("platform", (float) platform);
        set ("auto", manual ? 0.0f : 1.0f);
        set ("inputGain", 4.5f);

        const double sr = 48000.0;
        processor->setRateAndBufferSizeDetails (sr, 512);
        processor->prepareToPlay (sr, 512);
        juce::Random rng (3);
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        double lp = 0.0, lp2 = 0.0, t = 0.0;
        const float gain = juce::Decibels::decibelsToGain (sourceLufs + 12.0f + driveDb);
        for (int b = 0; b < (int) (sr * 14 / 512); ++b)
        {
            for (int i = 0; i < 512; ++i, t += 1.0 / sr)
            {
                const double env = std::max (0.0, std::sin (2.0 * juce::MathConstants<double>::pi * 3.7 * t)) * (0.55 + 0.45 * std::sin (t * 1.3));
                lp += (rng.nextDouble() * 2.0 - 1.0 - lp) * 0.3; lp2 += (lp - lp2) * 0.3;
                const bool pause = std::fmod (t, 5.0) > 4.2;
                float v = (float) ((pause ? 0.01 : env) * lp2 * 2.5 * gain);
                if (driveDb > 0.0f) v = juce::jlimit (-0.7f, 0.7f, v);  // pre-clipped source
                buf.setSample (0, i, v); buf.setSample (1, i, v);
            }
            processor->processBlock (buf, midi);
        }
        set ("bypass", bypass ? 1.0f : 0.0f);

        std::unique_ptr<juce::AudioProcessorEditor> editor (processor->createEditor());
        editor->setSize ((int) (940 * scale), (int) (620 * scale));
        for (int i = 0; i < 20; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5);

        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        auto file = outDir.getChildFile (name);
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (image, out);
        std::printf ("wrote %s\n", file.getFullPathName().toRawUTF8());
    };

    render ("ui_quiet_spotify.png", -34.0f, 0.0f, false, false, 0, 1.0f);
    render ("ui_clipped_podcast.png", -20.0f, 14.0f, false, false, 8, 1.0f);
    render ("ui_manual_bypass.png", -30.0f, 0.0f, true, true, 12, 0.8f);
    return 0;
}
