#pragma once

#include <array>
#include "Filters.h"

namespace bf
{
// Split-band de-esser: only the band above `freq` is turned down, and only when it's loud
// relative to the whole signal, so it works the same on quiet and loud speech.
class DeEsser
{
public:
    void prepare (double sr) { sampleRate = sr; reset(); }
    void reset()
    {
        detector.reset();
        for (auto& f : split) f.reset();
        fullEnv = hiEnv = 0.0; reductionDb = 0.0;
    }

    void set (float freq, float sensitivity01, float maxReductionDb)
    {
        coeffs = makeSvf (FilterType::highPass, freq, 0.5, 0.0, sampleRate);
        detCoeffs = makeSvf (FilterType::highPass, freq * 0.85, 0.7071, 0.0, sampleRate);
        thresholdDb = 2.0 - 18.0 * sensitivity01;
        maxDb = maxReductionDb;
    }

    void process (float* const* channels, int numChannels, int numSamples)
    {
        numChannels = std::min (numChannels, 2);
        const double att = coeffForMs (0.5, sampleRate), rel = coeffForMs (45.0, sampleRate);
        const double grAtt = coeffForMs (0.8, sampleRate), grRel = coeffForMs (70.0, sampleRate);
        peak = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            double mono = 0.0;
            for (int ch = 0; ch < numChannels; ++ch)
                mono += channels[ch][i];
            mono /= numChannels;

            const double hi = detector.process (mono, detCoeffs);
            const double pf = mono * mono, ph = hi * hi;
            fullEnv += (pf - fullEnv) * (pf > fullEnv ? att : rel);
            hiEnv += (ph - hiEnv) * (ph > hiEnv ? att : rel);

            double target = 0.0;
            const double hiDb = powToDb (hiEnv);
            if (hiDb > -60.0)
                target = std::clamp ((hiDb - powToDb (fullEnv) - thresholdDb) * 2.0, 0.0, (double) maxDb);

            reductionDb += (target - reductionDb) * (target > reductionDb ? grAtt : grRel);
            const double cut = 1.0 - std::pow (10.0, -reductionDb / 20.0);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const double x = channels[ch][i];
                split[(size_t) ch].process (x, coeffs);
                channels[ch][i] = (float) (x - cut * split[(size_t) ch].high (x, coeffs));
            }
            peak = std::max (peak, (float) reductionDb);
        }
    }

    float getReductionDb() const { return peak; }

private:
    double sampleRate = 48000.0;
    SvfCoeffs coeffs, detCoeffs;
    Svf detector;
    std::array<Svf, 2> split;
    double fullEnv = 0.0, hiEnv = 0.0, reductionDb = 0.0, thresholdDb = -8.0;
    float maxDb = 8.0f, peak = 0.0f;
};

// Plosive tamer: catches sudden low-frequency bursts ("P", "B", mic bumps) and briefly
// thins out the lows. Steady low end (a deep voice) is left alone.
class DePlosive
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        coeffs = makeSvf (FilterType::lowPass, 140.0, 0.5, 0.0, sr);
        detCoeffs = makeSvf (FilterType::lowPass, 120.0, 0.7071, 0.0, sr);
        reset();
    }

    void reset()
    {
        detector.reset();
        for (auto& f : split) f.reset();
        lowFast = lowSlow = fullEnv = 0.0; reductionDb = 0.0;
    }

    void set (float amount01) { amount = amount01; }

    void process (float* const* channels, int numChannels, int numSamples)
    {
        numChannels = std::min (numChannels, 2);
        peak = 0.0f;
        if (amount <= 0.0f && reductionDb < 0.01)
            return;

        const double att = coeffForMs (2.0, sampleRate), rel = coeffForMs (60.0, sampleRate);
        const double slow = coeffForMs (400.0, sampleRate);
        const double grAtt = coeffForMs (1.5, sampleRate), grRel = coeffForMs (120.0, sampleRate);

        for (int i = 0; i < numSamples; ++i)
        {
            double mono = 0.0;
            for (int ch = 0; ch < numChannels; ++ch)
                mono += channels[ch][i];
            mono /= numChannels;

            detector.process (mono, detCoeffs);
            const double lo = detector.low();
            const double pl = lo * lo, pf = mono * mono;
            lowFast += (pl - lowFast) * (pl > lowFast ? att : rel);
            lowSlow += (pl - lowSlow) * slow;
            fullEnv += (pf - fullEnv) * (pf > fullEnv ? att : rel);

            double target = 0.0;
            const double lowDb = powToDb (lowFast);
            if (lowDb > -45.0)
            {
                const double burst = lowDb - powToDb (lowSlow);
                const double dominance = lowDb - powToDb (fullEnv);
                if (dominance > -4.0)
                    target = std::clamp ((burst - 4.0) * 1.5, 0.0, 18.0) * amount;
            }

            reductionDb += (target - reductionDb) * (target > reductionDb ? grAtt : grRel);
            const double cut = 1.0 - std::pow (10.0, -reductionDb / 20.0);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const double x = channels[ch][i];
                split[(size_t) ch].process (x, coeffs);
                channels[ch][i] = (float) (x - cut * split[(size_t) ch].low());
            }
            peak = std::max (peak, (float) reductionDb);
        }
    }

    float getReductionDb() const { return peak; }

private:
    double sampleRate = 48000.0;
    SvfCoeffs coeffs, detCoeffs;
    Svf detector;
    std::array<Svf, 2> split;
    double lowFast = 0.0, lowSlow = 0.0, fullEnv = 0.0, reductionDb = 0.0;
    float amount = 0.0f, peak = 0.0f;
};
} // namespace bf
