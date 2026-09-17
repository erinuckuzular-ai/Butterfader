#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/PluginProcessor.h"

// Simulated two-person podcast. Host A is "our" mic; host B bleeds into it at -16 dB (and duller).
// Checks that the rider rides A, ignores B's bleed, and the output never passes the ceiling.
namespace
{
constexpr double sr = 48000.0;
constexpr int blockSize = 480;

struct Voice
{
    double f0, formants[3], phase = 0.0;
    std::array<bf::Svf, 3> filters;
    std::array<bf::SvfCoeffs, 3> coeffs;
    juce::Random rng;

    Voice (double pitch, double fa, double fb, double fc, int seed) : f0 (pitch), formants { fa, fb, fc }, rng (seed)
    {
        for (int i = 0; i < 3; ++i)
            coeffs[(size_t) i] = bf::makeSvf (bf::FilterType::bandPass, formants[i], 5.0, 0.0, sr);
    }

    float next (double t)
    {
        // Pitch wobble, glottal-ish pulse, a bit of breath noise.
        phase += f0 * (1.0 + 0.04 * std::sin (2.0 * bf::pi * 0.7 * t)) / sr;
        if (phase >= 1.0) phase -= 1.0;
        const double src = (phase < 0.08 ? 1.0 : 0.0) + 0.05 * (rng.nextDouble() - 0.5);
        double y = 0.0;
        for (int i = 0; i < 3; ++i)
            y += filters[(size_t) i].process (src, coeffs[(size_t) i]) * (i == 0 ? 1.0 : 0.6);
        // Syllables at ~4 Hz with small gaps.
        const double syl = std::max (0.0, std::sin (2.0 * bf::pi * 4.1 * t + std::sin (t * 1.3)));
        return (float) (y * syl);
    }
};

struct Segment { double start, end; char who; float levelDb; };

const std::vector<Segment> timeline {
    { 0.0, 7.0, 'A', -18.0f },   // A, right at target
    { 7.0, 9.0, '-', 0.0f },     // silence
    { 9.0, 16.0, 'B', -20.0f },  // B talks (only bleed reaches A's mic)
    { 16.0, 22.0, 'A', -16.0f }, // A, loud
    { 22.0, 28.0, 'A', -30.0f }, // A, quiet again
};

struct Result
{
    float gainAtBleedStart = 0, gainMaxDuringBleed = -100, gainMinDuringBleed = 100;
    float gainEndQuietA = 0, gainEndQuietA2 = 0, gainEndLoudA = 0, maxPeak = 0;
    bool finite = true;
};

Result run (bool withSidechain, bool guardOn, bool voiceLock, bool learnFirst, const char* label)
{
    ButterfaderAudioProcessor proc;
    auto layout = proc.getBusesLayout();
    layout.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
    proc.setBusesLayout (layout);
    proc.setRateAndBufferSizeDetails (sr, blockSize);
    proc.prepareToPlay (sr, blockSize);
    std::printf ("  buses in=%d sidechain enabled=%d total in ch=%d\n", proc.getBusCount (true), (int) proc.getBus (true, 1)->isEnabled(), proc.getTotalNumInputChannels());
    proc.apvts.getParameter ("guardOn")->setValueNotifyingHost (guardOn ? 1.0f : 0.0f);
    proc.apvts.getParameter ("voiceLock")->setValueNotifyingHost (voiceLock ? 1.0f : 0.0f);

    Voice a (118.0, 520.0, 1450.0, 2600.0, 1), b (205.0, 820.0, 1900.0, 3300.0, 2);
    Voice aIntoB (118.0, 520.0, 1450.0, 2600.0, 3), bIntoA (205.0, 820.0, 1900.0, 3300.0, 4);
    auto bleedTone = bf::makeSvf (bf::FilterType::lowPass, 3500.0, 0.7, 0.0, sr);
    bf::Svf bleedFilterA;

    juce::AudioBuffer<float> buffer (4, blockSize);
    juce::MidiBuffer midi;
    Result r;

    // Calibrate each synthetic voice with the real loudness meter so segment levels are true LUFS.
    auto measure = [] (Voice v)
    {
        bf::LoudnessMeter m;
        m.prepare (sr);
        std::vector<float> buf ((size_t) blockSize);
        for (int n = 0; n < (int) (6.0 * sr); n += blockSize)
        {
            for (int i = 0; i < blockSize; ++i) buf[(size_t) i] = v.next ((n + i) / sr);
            const float* chans[] { buf.data(), buf.data() };
            m.process (chans, 2, blockSize);
        }
        return m.integrated.load();
    };
    const float unityA = measure (Voice (118.0, 520.0, 1450.0, 2600.0, 9));
    const float unityB = measure (Voice (205.0, 820.0, 1900.0, 3300.0, 9));
    auto scale = [] (float lufs, float unity) { return std::pow (10.0f, (lufs - unity) / 20.0f); };

    if (learnFirst)
    {
        proc.ride.startLearning();
        for (int n = 0; n < (int) (10.0 * sr); n += blockSize)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const float s = a.next ((n + i) / sr) * scale (-24.0f, unityA);
                buffer.setSample (0, i, s); buffer.setSample (1, i, s);
                buffer.setSample (2, i, 0.0f); buffer.setSample (3, i, 0.0f);
            }
            proc.processBlock (buffer, midi);
        }
        std::printf ("  unity A %.1f B %.1f\n", unityA, unityB);
        std::printf ("  learned voice: %s\n", proc.ride.hasProfile() ? "yes" : "NO");
    }

    const double total = timeline.back().end;
    bool bleedStarted = false;

    for (int n = 0; n < (int) (total * sr); n += blockSize)
    {
        const double tBlock = n / sr;
        const Segment* seg = &timeline.front();
        for (auto& s : timeline) if (tBlock >= s.start && tBlock < s.end) seg = &s;

        for (int i = 0; i < blockSize; ++i)
        {
            const double t = (n + i) / sr;
            float ownA = 0, ownB = 0;
            if (seg->who == 'A') ownA = a.next (t) * scale (seg->levelDb, unityA);
            if (seg->who == 'B') ownB = b.next (t) * scale (seg->levelDb, unityB);
            const float bleedB = (float) bleedFilterA.process ((seg->who == 'B' ? bIntoA.next (t) * scale (seg->levelDb, unityB) : 0.0f) * 0.16f, bleedTone);
            const float bleedA = seg->who == 'A' ? aIntoB.next (t) * scale (seg->levelDb, unityA) * 0.16f : 0.0f;
            const float noise = 0.0003f * (float) (std::sin (t * 977.0) * 0.5);

            const float micA = ownA + bleedB + noise;
            const float micB = withSidechain ? ownB + bleedA + noise : 0.0f;
            buffer.setSample (0, i, micA); buffer.setSample (1, i, micA);
            buffer.setSample (2, i, micB); buffer.setSample (3, i, micB);
        }

        proc.processBlock (buffer, midi);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < blockSize; ++i)
            {
                const float v = buffer.getSample (ch, i);
                if (! std::isfinite (v)) r.finite = false;
                r.maxPeak = std::max (r.maxPeak, std::abs (v));
            }

        const float g = proc.ride.meterGainDb.load();
        if (std::getenv ("BF_DEBUG") && n % (int) (sr * 0.5) < blockSize)
            std::printf ("    t=%5.1f seg=%c inLufs=%6.1f state=%d gain=%+5.1f match=%.2f\n", tBlock, seg->who,
                         proc.ride.meterInputLufs.load(), proc.ride.meterState.load(), g, proc.ride.meterMatch.load());
        if (seg->who == 'B')
        {
            if (! bleedStarted) { r.gainAtBleedStart = g; bleedStarted = true; }
            r.gainMaxDuringBleed = std::max (r.gainMaxDuringBleed, g);
            r.gainMinDuringBleed = std::min (r.gainMinDuringBleed, g);
        }
        if (tBlock < 7.0) r.gainEndQuietA = g;
        if (tBlock >= 22.0) r.gainEndQuietA2 = g;
        if (tBlock >= 16.0 && tBlock < 22.0) r.gainEndLoudA = g;
    }

    std::printf ("%-34s gain@bleed %+5.1f  bleed range [%+5.1f, %+5.1f]  quietA %+5.1f  quietA2 %+5.1f  loudA %+5.1f  peak %.2f dBFS  integrated %.1f LUFS  latency %d\n",
                 label, r.gainAtBleedStart, r.gainMinDuringBleed, r.gainMaxDuringBleed, r.gainEndQuietA, r.gainEndQuietA2, r.gainEndLoudA,
                 20.0f * std::log10 (std::max (r.maxPeak, 1.0e-6f)), proc.loudness.integrated.load(), proc.getLatencySamples());
    return r;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    int failures = 0;
    auto check = [&] (bool ok, const char* what) { std::printf ("  [%s] %s\n", ok ? "ok" : "FAIL", what); failures += ok ? 0 : 1; };

    auto naive = run (false, false, false, false, "no protection (like other riders)");
    std::printf ("  (reference) bleed pushed gain up by %.1f dB\n", naive.gainMaxDuringBleed - naive.gainAtBleedStart);
    check (naive.gainMaxDuringBleed - naive.gainAtBleedStart > 3.0f, "test is meaningful: unprotected rider does boost bleed");

    auto guarded = run (true, true, false, false, "sidechain guard");
    check (guarded.finite, "output finite");
    check (guarded.maxPeak <= std::pow (10.0f, -1.0f / 20.0f) + 1.0e-4f, "peak under -1 dBFS ceiling");
    check (guarded.gainMaxDuringBleed - guarded.gainAtBleedStart < 1.0f, "no boost while other host talks");
    check (guarded.gainEndQuietA2 > 6.0f, "quiet talker boosted");
    check (guarded.gainEndLoudA < 0.0f, "loud talker pulled down");

    auto locked = run (false, false, true, true, "voice lock (no sidechain)");
    check (locked.finite, "output finite");
    check (locked.gainMaxDuringBleed - locked.gainAtBleedStart < 1.5f, "voice lock: no boost on bleed");
    check (locked.gainEndQuietA2 > 6.0f, "voice lock: quiet talker still boosted");

    std::printf (failures == 0 ? "ALL PASSED\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
