#pragma once

#include <array>
#include <vector>
#include "Filters.h"

namespace bf
{
struct EqBand
{
    bool on = false;
    FilterType type = FilterType::bell;
    float freq = 1000.0f, gainDb = 0.0f, q = 1.0f;
};

// Six-band stereo parametric EQ with smoothed, click-free parameter moves.
class ParametricEq
{
public:
    static constexpr int numBands = 6;
    static constexpr int maxChannels = 2;

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        for (auto& s : states)
            s.first = true;
        reset();
    }

    void reset()
    {
        for (auto& s : states)
            for (auto& ch : s.svf)
                for (auto& f : ch)
                    f.reset();
    }

    void setBand (int index, const EqBand& band) { targets[(size_t) index] = band; }

    // Magnitude in dB of one band at frequency f, used by the UI.
    static double bandMagnitudeDb (const EqBand& band, double f, double sampleRate)
    {
        if (! band.on)
            return 0.0;
        double mag = 1.0;
        for (int stage = 0; stage < stageCount (band.type); ++stage)
            mag *= std::abs (makeSvf (band.type, band.freq, stageQ (band, stage), band.gainDb, sampleRate).response (f, sampleRate));
        return 20.0 * std::log10 (std::max (mag, 1.0e-9));
    }

    void process (float* const* channels, int numChannels, int numSamples)
    {
        numChannels = std::min (numChannels, maxChannels);
        for (int start = 0; start < numSamples; start += subBlock)
        {
            const int n = std::min (subBlock, numSamples - start);
            for (int b = 0; b < numBands; ++b)
            {
                auto& st = states[(size_t) b];
                const auto& target = targets[(size_t) b];
                advance (st, target, n);
                if (st.mix < 1.0e-4 && ! target.on)
                    continue;

                const int stages = stageCount (st.type);
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    auto* data = channels[ch] + start;
                    double mix = st.mixStart;
                    const double mixStep = (st.mix - st.mixStart) / n;
                    for (int i = 0; i < n; ++i)
                    {
                        const double dry = data[i];
                        double wet = dry;
                        for (int s = 0; s < stages; ++s)
                            wet = st.svf[(size_t) ch][(size_t) s].process (wet, st.coeffs[(size_t) s]);
                        mix += mixStep;
                        data[i] = (float) (dry + mix * (wet - dry));
                    }
                }
            }
        }
    }

private:
    static constexpr int subBlock = 16;

    struct State
    {
        bool first = true;
        FilterType type = FilterType::bell;
        double logFreq = std::log (1000.0), gainDb = 0.0, logQ = 0.0;
        double mix = 0.0, mixStart = 0.0;
        std::array<SvfCoeffs, 2> coeffs;
        std::array<std::array<Svf, 2>, maxChannels> svf;
    };

    static int stageCount (FilterType t) { return (t == FilterType::lowCut24 || t == FilterType::highCut24) ? 2 : 1; }

    static double stageQ (const EqBand& band, int stage)
    {
        if (stageCount (band.type) == 2)
            return stage == 0 ? 0.5411961 * band.q / 0.7071 : 1.3065630;
        return band.q;
    }

    void advance (State& st, const EqBand& target, int n)
    {
        const double a = 1.0 - std::exp (-(double) n / (0.03 * sampleRate));
        const double tf = std::log ((double) target.freq), tq = std::log ((double) target.q);
        st.mixStart = st.mix;

        if (st.first || st.type != target.type)
        {
            if (! st.first)
                for (auto& ch : st.svf)
                    for (auto& f : ch)
                        f.reset();
            st.first = false;
            st.type = target.type;
            st.logFreq = tf; st.gainDb = target.gainDb; st.logQ = tq;
        }
        else
        {
            st.logFreq += (tf - st.logFreq) * a;
            st.gainDb += (target.gainDb - st.gainDb) * a;
            st.logQ += (tq - st.logQ) * a;
        }
        st.mix += ((target.on ? 1.0 : 0.0) - st.mix) * a * 1.5;
        st.mix = std::clamp (st.mix, 0.0, 1.0);

        EqBand now { true, st.type, (float) std::exp (st.logFreq), (float) st.gainDb, (float) std::exp (st.logQ) };
        for (int s = 0; s < stageCount (st.type); ++s)
            st.coeffs[(size_t) s] = makeSvf (st.type, now.freq, stageQ (now, s), now.gainDb, sampleRate);
    }

    double sampleRate = 48000.0;
    std::array<EqBand, numBands> targets;
    std::array<State, numBands> states;
};
} // namespace bf
