#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/PluginProcessor.h"

// Renders the editor to PNGs without a DAW: UISnapshot <outDir>
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());

    std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());
    processor->setPlayConfigDetails (2, 2, 48000.0, 512);
    processor->prepareToPlay (48000.0, 512);

    // Feed some fake "speech" so the displays have something to show.
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    juce::Random rng (7);

    auto snap = [&] (const juce::String& name, int preset)
    {
        processor->setCurrentProgram (preset);
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor->createEditor());
        editor->setSize (1240, 800);
        for (int frame = 0; frame < 200; ++frame)
        {
            for (int i = 0; i < 512; ++i)
            {
                const double t = (frame * 512 + i) / 48000.0;
                const float env = (float) (0.5 + 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 3.0 * t)) * (std::fmod (t, 2.0) < 1.4 ? 0.2f : 0.001f);
                const float s = env * (float) (std::sin (2.0 * juce::MathConstants<double>::pi * 180.0 * t) + 0.3f * (rng.nextFloat() - 0.5f));
                buffer.setSample (0, i, s);
                buffer.setSample (1, i, s);
            }
            processor->processBlock (buffer, midi);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (3);
        }
        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        juce::FileOutputStream out (outDir.getChildFile (name));
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (image, out);
    };

    snap ("ui_default.png", 0);
    snap ("ui_radio.png", 7);
    return 0;
}
