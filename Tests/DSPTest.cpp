#include <juce_audio_processors/juce_audio_processors.h>
#include "../Source/PluginProcessor.h"

// Offline checks of loudness accuracy, true-peak ceiling, auto-level, bypass matching and clip detection.
static int failures = 0;
static void check (bool ok, const char* what, double value)
{
    std::printf ("%s %-58s %.2f\n", ok ? "  ok " : "FAIL ", what, value);
    if (! ok) ++failures;
}

// Independent 16x sinc-interpolated true peak (long filter, not shared with the plug-in).
static double independentTruePeakDb (const juce::AudioBuffer<float>& b, int start)
{
    const int os = 16, half = 64;
    double peak = 0.0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
    {
        auto* x = b.getReadPointer (ch);
        for (int i = start + half; i < b.getNumSamples() - half; ++i)
        {
            if (std::max ({ std::abs (x[i - 1]), std::abs (x[i]), std::abs (x[i + 1]) }) < 0.4f) continue; // < -8 dBFS can't reach the ceiling
            for (int p = 0; p < os; ++p)
            {
                const double t = (double) p / os;
                double y = 0.0;
                for (int k = -half + 1; k <= half; ++k)
                {
                    const double d = k - t;
                    const double sinc = std::abs (d) < 1e-9 ? 1.0 : std::sin (juce::MathConstants<double>::pi * d) / (juce::MathConstants<double>::pi * d);
                    const double w = 0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * d / half);
                    y += x[i + k] * sinc * w;
                }
                peak = std::max (peak, std::abs (y));
            }
        }
    }
    return 20.0 * std::log10 (std::max (peak, 1e-9));
}

static double integratedOf (const juce::AudioBuffer<float>& b, double sr, int start)
{
    bf::LoudnessMeter m;
    m.prepare (sr, b.getNumChannels());
    std::array<const float*, 8> f {};
    for (int i = start; i < b.getNumSamples(); ++i)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch) f[(size_t) ch] = b.getReadPointer (ch) + i;
        m.pushFrame (f.data(), 0);
    }
    return m.getIntegrated();
}

// Speech-like programme: filtered noise in syllable bursts with pauses, plus sharp transients.
static juce::AudioBuffer<float> makeProgramme (double sr, double seconds, float levelDb, int channels, int seed)
{
    juce::Random rng (seed);
    const int n = (int) (sr * seconds);
    juce::AudioBuffer<float> b (channels, n);
    double lp = 0.0, lp2 = 0.0, phase = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const double syllable = std::max (0.0, std::sin (2.0 * juce::MathConstants<double>::pi * 4.3 * t));
        const bool pause = std::fmod (t, 7.0) > 5.5;
        const double env = pause ? 0.003 : syllable * (0.6 + 0.4 * std::sin (t * 0.9));
        const double white = rng.nextDouble() * 2.0 - 1.0;
        lp  += (white - lp) * 0.3;
        lp2 += (lp - lp2) * 0.3;
        phase += 2.0 * juce::MathConstants<double>::pi * 180.0 / sr;
        const bool sibilant = std::fmod (t, 2.3) < 0.15;                // bright "s" sounds
        double s = env * (0.7 * lp2 * 4.0 + 0.5 * std::sin (phase)) + (sibilant ? 0.08 * white : 0.0);
        const int sinceClick = i % (int) (sr * 1.3);               // plosive-like transients
        if (sinceClick < (int) (sr * 0.02)) s += 0.9 * std::exp (-sinceClick / (sr * 0.004)) * std::sin (sinceClick * 0.9);
        for (int ch = 0; ch < channels; ++ch) b.setSample (ch, i, (float) s);
    }
    b.applyGain ((float) juce::Decibels::decibelsToGain (levelDb - integratedOf (b, sr, 0)));  // calibrate to LUFS
    return b;
}


static double integratedRange (const juce::AudioBuffer<float>& b, double sr, double fromSec, double toSec)
{
    juce::AudioBuffer<float> part (b.getNumChannels(), (int) ((toSec - fromSec) * sr));
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        part.copyFrom (ch, 0, b, ch, (int) (fromSec * sr), part.getNumSamples());
    return integratedOf (part, sr, 0);
}

static double rmsDb (const juce::AudioBuffer<float>& b, double sr, double fromSec, double toSec)
{
    double sum = 0.0; int count = 0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = (int) (fromSec * sr); i < (int) (toSec * sr); ++i, ++count)
            sum += b.getSample (ch, i) * b.getSample (ch, i);
    return 10.0 * std::log10 (std::max (1.0e-20, sum / std::max (1, count)));
}

// A voice that only talks when `talks(t)` is true.
static juce::AudioBuffer<float> makeVoice (double sr, double seconds, int seed, std::function<bool (double)> talks)
{
    juce::Random rng (seed);
    juce::AudioBuffer<float> b (1, (int) (sr * seconds));
    double lp = 0.0, lp2 = 0.0, phase = 0.0;
    const double pitch = 120.0 + seed * 23.0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double t = i / sr;
        const double syllable = std::max (0.0, std::sin (2.0 * juce::MathConstants<double>::pi * (4.0 + seed * 0.3) * t));
        lp += (rng.nextDouble() * 2.0 - 1.0 - lp) * 0.3; lp2 += (lp - lp2) * 0.3;
        phase += 2.0 * juce::MathConstants<double>::pi * pitch / sr;
        b.setSample (0, i, talks (t) ? (float) (0.25 * syllable * (lp2 * 4.0 + 0.6 * std::sin (phase))) : 0.0f);
    }
    return b;
}

static void addHiss (juce::AudioBuffer<float>& b, float rmsDbFs, int seed)
{
    juce::Random rng (seed);
    const float amp = juce::Decibels::decibelsToGain (rmsDbFs) * std::sqrt (3.0f);
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.addSample (ch, i, (rng.nextFloat() * 2.0f - 1.0f) * amp);
}

static juce::AudioBuffer<float> run (ButterfaderAudioProcessor& p, juce::AudioBuffer<float> in, double sr, int block)
{
    p.setRateAndBufferSizeDetails (sr, block);
    p.prepareToPlay (sr, block);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < in.getNumSamples(); pos += block)
    {
        const int len = std::min (block, in.getNumSamples() - pos);
        juce::AudioBuffer<float> view (in.getArrayOfWritePointers(), in.getNumChannels(), pos, len);
        p.processBlock (view, midi);
    }
    return in;
}

static void setParam (ButterfaderAudioProcessor& p, const char* id, float value)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    // 1. BS.1770 reference: 997 Hz sine at -23 dBFS in both channels = -23 LUFS
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        juce::AudioBuffer<float> sine (2, (int) (sr * 20));
        for (int i = 0; i < sine.getNumSamples(); ++i)
            for (int ch = 0; ch < 2; ++ch)
                sine.setSample (ch, i, (float) (juce::Decibels::decibelsToGain (-23.0) * std::sin (2.0 * juce::MathConstants<double>::pi * 997.0 * i / sr)));
        const double l = integratedOf (sine, sr, 0);
        check (std::abs (l + 23.0) < 0.1, juce::String ("sine -23 dBFS reads -23 LUFS @ " + juce::String (sr)).toRawUTF8(), l);
    }

    // 2. Auto level + true-peak ceiling, quiet / loud / mono sources, all platforms of interest
    struct Case { const char* name; float level; int channels; int platform; double sr; int speed; };
    for (auto c : { Case { "quiet speech (-35 LUFS) -> Spotify (-14)", -35.0f, 2, 0, 48000.0, 1 },
                    Case { "loud speech (-10 LUFS) -> Apple Podcasts (-16)", -10.0f, 2, 8, 48000.0, 1 },
                    Case { "mono speech (-30 LUFS) -> EBU R128 (-23)", -30.0f, 1, 10, 44100.0, 0 },
                    Case { "very quiet (-45 LUFS) -> YouTube (-14), tight", -45.0f, 2, 2, 96000.0, 2 } })
    {
        ButterfaderAudioProcessor p;
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (c.channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
        layout.outputBuses.add (layout.inputBuses[0]);
        p.setBusesLayout (layout);
        setParam (p, "platform", (float) c.platform);
        setParam (p, "speed", (float) c.speed);

        auto source = makeProgramme (c.sr, 60.0, c.level, c.channels, 7);
        const bool sourceOver = source.getMagnitude (0, source.getNumSamples()) > 1.0f;
        auto out = run (p, source, c.sr, 512);
        const int skip = (int) (c.sr * 10.0);
        const double target = bf::platforms[c.platform].targetLufs;
        const double ceilingDb = std::min (-1.0, (double) bf::platforms[c.platform].maxTruePeak);
        const double integrated = integratedOf (out, c.sr, skip);
        const double tp = independentTruePeakDb (out, skip);
        std::printf ("  [%s] integrated %.2f LUFS (target %.1f), true peak %.2f dBTP, auto gain %.1f dB, GR %.1f dB, plugin TP %.2f\n",
                     c.name, integrated, target, tp, p.autoGainDb.load(), p.grPeakDb.load(), p.outputTruePeakMaxDb.load());
        check (std::abs (integrated - target) < 1.0, "  within 1 LU of target", integrated - target);
        check (tp <= ceilingDb + 0.1, "  true peak under ceiling (+0.1 dB tolerance)", tp - ceilingDb);
        if (sourceOver) check (p.clipCount.load() > 0, "  source over 0 dBFS is flagged", p.clipCount.load());
        else            check (p.clipCount.load() == 0, "  no false clip detections", p.clipCount.load());
    }

    // 3. Level-matched bypass lands near the processed loudness
    {
        ButterfaderAudioProcessor p;
        auto in = makeProgramme (48000.0, 40.0, -35.0f, 2, 3);
        auto processed = run (p, in, 48000.0, 256);
        setParam (p, "bypass", 1.0f);
        juce::AudioBuffer<float> in2 (in);
        // keep running without re-preparing so the match value carries over
        juce::MidiBuffer midi;
        for (int pos = 0; pos < in2.getNumSamples(); pos += 256)
        {
            juce::AudioBuffer<float> view (in2.getArrayOfWritePointers(), 2, pos, std::min (256, in2.getNumSamples() - pos));
            p.processBlock (view, midi);
        }
        const double a = integratedOf (processed, 48000.0, 480000), b = integratedOf (in2, 48000.0, 480000);
        check (std::abs (a - b) < 1.5, "bypass is level matched (LU difference)", b - a);
    }

    // 4. Clip detection: a hard-clipped sine is flagged, a clean one is not
    {
        auto makeSine = [] (bool clipped)
        {
            juce::AudioBuffer<float> b (2, 48000 * 3);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                float v = (float) (1.6 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * i / 48000.0));
                v = clipped ? juce::jlimit (-0.8f, 0.8f, v) : v * 0.5f;
                b.setSample (0, i, v); b.setSample (1, i, v);
            }
            return b;
        };
        ButterfaderAudioProcessor a, b;
        run (a, makeSine (true), 48000.0, 512);
        run (b, makeSine (false), 48000.0, 512);
        check (a.clipCount.load() > 0, "clipped source detected (events)", a.clipCount.load());
        check (b.clipCount.load() == 0, "clean source not flagged (events)", b.clipCount.load());
    }


    // 5. Noise guard: hiss at the start and between phrases is not boosted by the rider
    {
        const double sr = 48000.0;
        auto voice = makeVoice (sr, 40.0, 1, [] (double t) { return t > 5.0 && std::fmod (t, 8.0) < 5.5; });
        voice.applyGain ((float) juce::Decibels::decibelsToGain (-36.0 - integratedOf (voice, sr, 0)));
        addHiss (voice, -68.0f, 9);
        ButterfaderAudioProcessor p;
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (juce::AudioChannelSet::mono()); layout.outputBuses.add (juce::AudioChannelSet::mono());
        p.setBusesLayout (layout);
        auto out = run (p, voice, sr, 512);
        const double hissInStart = rmsDb (voice, sr, 1.0, 4.5), hissOutStart = rmsDb (out, sr, 1.0, 4.5);
        const double hissIn = rmsDb (voice, sr, 30.0, 31.5), hissOut = rmsDb (out, sr, 30.0, 31.5);   // pause 29.5..32
        const double speech = integratedRange (out, sr, 24.0, 29.0);
        std::printf ("  [noise guard] speech %.1f LUFS, gain %.1f dB, hiss at start %.1f -> %.1f dBFS, hiss in pause %.1f -> %.1f dBFS\n",
                     speech, p.autoGainDb.load(), hissInStart, hissOutStart, hissIn, hissOut);
        check (std::abs (speech + 14.0) < 1.5, "noise guard: speech still reaches target (LU off)", speech + 14.0);
        check (hissOutStart <= hissInStart + 1.0, "noise guard: hiss before speech not boosted (dB)", hissOutStart - hissInStart);
        check (hissOut <= hissIn + 1.0, "noise guard: hiss between phrases not boosted (dB)", hissOut - hissIn);
    }

    // 6. Debleed: two lapels, each picks up the other at -15 dB; mic B's preamp is 8 dB lower; both hiss
    for (int mode : { ButterfaderAudioProcessor::debleedLinked, ButterfaderAudioProcessor::debleedGate })
    {
        const double sr = 48000.0, seconds = 48.0;
        auto aTalks = [] (double t) { return std::fmod (t, 12.0) < 5.5; };
        auto bTalks = [] (double t) { const double m = std::fmod (t, 12.0); return m > 6.0 && m < 11.5; };
        auto voiceA = makeVoice (sr, seconds, 2, aTalks), voiceB = makeVoice (sr, seconds, 3, bTalks);
        juce::AudioBuffer<float> micA (1, voiceA.getNumSamples()), micB (1, voiceA.getNumSamples());
        const float bleed = juce::Decibels::decibelsToGain (-15.0f), preampB = juce::Decibels::decibelsToGain (-8.0f);
        for (int i = 0; i < micA.getNumSamples(); ++i)
        {
            micA.setSample (0, i, voiceA.getSample (0, i) + bleed * voiceB.getSample (0, i));
            micB.setSample (0, i, preampB * (voiceB.getSample (0, i) + bleed * voiceA.getSample (0, i)));
        }
        addHiss (micA, -66.0f, 4); addHiss (micB, -72.0f, 5);

        ButterfaderAudioProcessor pa, pb;
        for (auto* p : { &pa, &pb })
        {
            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add (juce::AudioChannelSet::mono()); layout.outputBuses.add (juce::AudioChannelSet::mono());
            p->setBusesLayout (layout);
            setParam (*p, "mode", (float) ButterfaderAudioProcessor::micMode);
            setParam (*p, "debleed", (float) mode);
            p->setRateAndBufferSizeDetails (sr, 480);
            p->prepareToPlay (sr, 480);
        }
        juce::MidiBuffer midi;
        for (int pos = 0; pos < micA.getNumSamples(); pos += 480)   // interleaved like a host rendering two tracks
        {
            const int len = std::min (480, micA.getNumSamples() - pos);
            juce::AudioBuffer<float> va (micA.getArrayOfWritePointers(), 1, pos, len), vb (micB.getArrayOfWritePointers(), 1, pos, len);
            pa.processBlock (va, midi);
            pb.processBlock (vb, midi);
            juce::Thread::sleep (0);
        }

        // judge the second half, after learning: B-only talk vs A's bleed on B's mic
        const double bOwn = integratedRange (micB, sr, 30.5, 35.0), bBleed = integratedRange (micB, sr, 24.5, 29.0);
        const double aOwn = integratedRange (micA, sr, 24.5, 29.0), aBleed = integratedRange (micA, sr, 30.5, 35.0);
        const char* name = mode == ButterfaderAudioProcessor::debleedLinked ? "linked" : "gate";
        std::printf ("  [debleed %s] mic A own %.1f / bleed %.1f LUFS, mic B own %.1f / bleed %.1f LUFS (input separation 15 LU), linked peers seen %d\n",
                     name, aOwn, aBleed, bOwn, bBleed, pa.linkedMics.load());
        const double minSeparation = mode == ButterfaderAudioProcessor::debleedLinked ? 25.0 : 20.0;
        check (bOwn - bBleed >= minSeparation, "  mic B separation (LU)", bOwn - bBleed);
        check (aOwn - aBleed >= minSeparation, "  mic A separation (LU)", aOwn - aBleed);
        check (std::abs (bOwn + 20.0) < 2.0 && std::abs (aOwn + 20.0) < 2.0, "  own voices levelled to -20 LUFS (worst LU off)",
               std::max (std::abs (bOwn + 20.0), std::abs (aOwn + 20.0)));
    }

    std::printf (failures == 0 ? "ALL OK\n" : "FAILURES: %d\n", failures);
    return failures;
}
