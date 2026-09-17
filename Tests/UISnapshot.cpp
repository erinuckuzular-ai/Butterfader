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

    // Mic mode: two linked lapels bleeding into each other; snapshot mic B while A is talking.
    {
        auto makeMic = [] { auto p = std::unique_ptr<ButterfaderAudioProcessor> (dynamic_cast<ButterfaderAudioProcessor*> (createPluginFilter())); return p; };
        auto a = makeMic(), b = makeMic();
        const double sr = 48000.0;
        for (auto* p : { a.get(), b.get() })
        {
            auto set = [&] (const char* id, float v) { auto* prm = p->apvts.getParameter (id); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); };
            set ("mode", 0.0f);
            p->setRateAndBufferSizeDetails (sr, 512);
            p->prepareToPlay (sr, 512);
        }
        juce::Random rng (5);
        juce::AudioBuffer<float> ba (2, 512), bb (2, 512);
        juce::MidiBuffer midi;
        double la = 0, la2 = 0, lb = 0, lb2 = 0, t = 0;
        const int blocks = (int) (sr * 21.5 / 512);
        for (int blk = 0; blk < blocks; ++blk)
        {
            for (int i = 0; i < 512; ++i, t += 1.0 / sr)
            {
                const bool aTalks = std::fmod (t, 8.0) < 3.6, bTalks = std::fmod (t, 8.0) > 4.0 && std::fmod (t, 8.0) < 7.6;
                la += (rng.nextDouble() * 2 - 1 - la) * 0.3; la2 += (la - la2) * 0.3;
                lb += (rng.nextDouble() * 2 - 1 - lb) * 0.3; lb2 += (lb - lb2) * 0.3;
                const double va = aTalks ? std::max (0.0, std::sin (t * 26.0)) * la2 * 0.9 : 0.0;
                const double vb = bTalks ? std::max (0.0, std::sin (t * 23.0)) * lb2 * 0.9 : 0.0;
                const double hiss = (rng.nextDouble() * 2 - 1) * 0.0007;
                const float sa = (float) (va + 0.18 * vb + hiss), sb = (float) (0.4 * (vb + 0.18 * va) + hiss);
                ba.setSample (0, i, sa); ba.setSample (1, i, sa);
                bb.setSample (0, i, sb); bb.setSample (1, i, sb);
            }
            a->processBlock (ba, midi);
            b->processBlock (bb, midi);
        }
        std::unique_ptr<juce::AudioProcessorEditor> editor (b->createEditor());
        editor->setSize (940, 620);
        for (int i = 0; i < 20; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        auto file = outDir.getChildFile ("ui_mic_linked.png");
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (image, out);
        std::printf ("wrote %s\n", file.getFullPathName().toRawUTF8());
    }
    return 0;
}
