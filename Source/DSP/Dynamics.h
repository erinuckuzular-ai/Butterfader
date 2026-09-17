#pragma once

#include <array>
#include <atomic>
#include <vector>
#include "Filters.h"

namespace bf
{
// Fixed delay for up to two channels.
class DelayLine
{
public:
    void prepare (int delaySamples)
    {
        delay = delaySamples;
        for (auto& b : buffers) b.assign ((size_t) delay + 1, 0.0f);
        pos = 0;
    }

    void reset() { for (auto& b : buffers) std::fill (b.begin(), b.end(), 0.0f); }

    // Pushes x into channel ch and returns the sample from `delay` samples ago.
    // Call for every channel before calling advance().
    float process (int ch, float x)
    {
        auto& b = buffers[(size_t) ch];
        b[(size_t) pos] = x;
        const int read = pos - delay < 0 ? pos - delay + (int) b.size() : pos - delay;
        return b[(size_t) read];
    }

    void advance() { if (++pos >= delay + 1) pos = 0; }
    int getDelay() const { return delay; }

private:
    std::array<std::vector<float>, 2> buffers;
    int delay = 0, pos = 0;
};

//==============================================================================
// "Silk": a soft-knee, opto-flavoured leveling compressor. Wide knee, low ratio, and a release
// that slows down the longer it has been working, so it glides instead of pumping.
class SilkCompressor
{
public:
    void prepare (double sr) { sampleRate = sr; reset(); }
    void reset() { env = 0.0; grDb = 0.0; slowGr = 0.0; }

    void set (float amount01, float thresholdLufs)
    {
        amount = amount01;
        threshold = thresholdLufs + 2.0 - 4.0 * amount01;
        ratio = 1.0 + 3.0 * amount01;
    }

    void process (float* const* channels, int numChannels, int numSamples)
    {
        numChannels = std::min (numChannels, 2);
        peak = 0.0f;
        if (amount <= 0.0f && grDb > -0.01)
            return;

        const double aEnv = coeffForMs (6.0, sampleRate), aAtt = coeffForMs (12.0, sampleRate);
        const double aSlow = coeffForMs (1500.0, sampleRate);
        const double knee = 12.0;

        for (int i = 0; i < numSamples; ++i)
        {
            double p = 0.0;
            for (int ch = 0; ch < numChannels; ++ch)
                p += (double) channels[ch][i] * channels[ch][i];
            env += (p - env) * aEnv;

            const double over = powToDb (env) - 0.691 - threshold;
            const double slope = 1.0 / ratio - 1.0;
            double target = 0.0;
            if (over > knee * 0.5)        target = slope * over;
            else if (over > -knee * 0.5)  target = slope * (over + knee * 0.5) * (over + knee * 0.5) / (2.0 * knee);

            slowGr += (grDb - slowGr) * aSlow;
            const double releaseMs = 90.0 + 110.0 * std::min (6.0, -slowGr);
            const double aRel = coeffForMs (releaseMs, sampleRate);
            grDb += (target - grDb) * (target < grDb ? aAtt : aRel);

            const float g = (float) std::pow (10.0, grDb / 20.0);
            for (int ch = 0; ch < numChannels; ++ch)
                channels[ch][i] *= g;
            peak = std::max (peak, (float) -grDb);
        }
    }

    float getReductionDb() const { return peak; }

private:
    double sampleRate = 48000.0, env = 0.0, grDb = 0.0, slowGr = 0.0;
    double threshold = -16.0, ratio = 2.0;
    float amount = 0.0f, peak = 0.0f;
};

//==============================================================================
// Lookahead brickwall limiter with a smooth (moving-average) gain curve. Because the gain
// starts moving a full lookahead window before a peak, it never has to snap.
class GlassCeiling
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        lookahead = std::max (1, (int) std::round (sr * 0.005));
        window.assign ((size_t) lookahead + 1, 1.0f);
        smooth.assign ((size_t) lookahead + 1, 1.0f);
        delay.prepare (lookahead);
        reset();
    }

    void reset()
    {
        std::fill (window.begin(), window.end(), 1.0f);
        std::fill (smooth.begin(), smooth.end(), 1.0f);
        delay.reset();
        sum = (double) smooth.size();
        env = 1.0; pos = 0; sinceRecalc = 0;
    }

    int getLatency() const { return lookahead; }
    void set (float ceilingDb) { ceiling = std::pow (10.0f, ceilingDb / 20.0f); }

    void process (float* const* channels, int numChannels, int numSamples)
    {
        numChannels = std::min (numChannels, 2);
        const double aRel = coeffForMs (120.0, sampleRate);
        const size_t len = window.size();
        float minGain = 1.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            float pk = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                pk = std::max (pk, std::abs (channels[ch][i]));
            const float need = pk > ceiling ? ceiling / pk : 1.0f;

            window[(size_t) pos] = need;
            float held = 1.0f;
            for (auto w : window)
                held = std::min (held, w);

            env = held < env ? held : env + (held - env) * aRel;

            sum += env - smooth[(size_t) pos];
            smooth[(size_t) pos] = (float) env;
            if (++sinceRecalc > 8192)
            {
                sinceRecalc = 0;
                sum = 0.0;
                for (auto s : smooth) sum += s;
            }
            if (++pos >= (int) len) pos = 0;

            const float gain = (float) std::min (1.0, sum / (double) len);
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float y = delay.process (ch, channels[ch][i]) * gain;
                channels[ch][i] = std::clamp (y, -ceiling, ceiling);
            }
            delay.advance();
            minGain = std::min (minGain, gain);
        }
        peakReductionDb = -20.0f * std::log10 (std::max (minGain, 1.0e-6f));
    }

    float getReductionDb() const { return peakReductionDb; }

private:
    double sampleRate = 48000.0, env = 1.0, sum = 1.0;
    int lookahead = 240, pos = 0, sinceRecalc = 0;
    float ceiling = 0.89f, peakReductionDb = 0.0f;
    std::vector<float> window, smooth;
    DelayLine delay;
};

//==============================================================================
// ITU-R BS.1770-4 loudness: momentary (400 ms), short-term (3 s) and gated integrated.
class LoudnessMeter
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        for (auto& k : kw) k.prepare (sr);
        hopSamples = (int) std::round (sr * 0.1);
        reset();
    }

    void reset()
    {
        for (auto& k : kw) k.reset();
        hopPower.fill (0.0);
        acc = 0.0; accCount = 0; hopIndex = 0; hopsFilled = 0;
        counts.fill (0); powers.fill (0.0);
        momentary.store (-120.0f); shortTerm.store (-120.0f); integrated.store (-120.0f);
        truePeakish.store (-120.0f);
    }

    void requestReset() { resetRequested.store (true); }

    void process (const float* const* channels, int numChannels, int numSamples)
    {
        if (resetRequested.exchange (false))
            reset();

        numChannels = std::min (numChannels, 2);
        const double chanFactor = numChannels == 1 ? 2.0 : 1.0; // mono plays as dual-mono
        float pk = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            double p = 0.0;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const double y = kw[(size_t) ch].process (channels[ch][i]);
                p += y * y;
                pk = std::max (pk, std::abs (channels[ch][i]));
            }
            acc += p * chanFactor;

            if (++accCount >= hopSamples)
            {
                hopPower[(size_t) hopIndex] = acc / accCount;
                hopIndex = (hopIndex + 1) % (int) hopPower.size();
                hopsFilled = std::min (hopsFilled + 1, (int) hopPower.size());
                acc = 0.0; accCount = 0;
                onHop();
            }
        }
        truePeakish.store (20.0f * std::log10 (std::max (pk, 1.0e-6f)));
    }

    std::atomic<float> momentary { -120.0f }, shortTerm { -120.0f }, integrated { -120.0f }, truePeakish { -120.0f };

private:
    double meanOfLast (int hops) const
    {
        double s = 0.0;
        int n = std::min (hops, hopsFilled);
        for (int k = 1; k <= n; ++k)
            s += hopPower[(size_t) ((hopIndex - k + (int) hopPower.size()) % (int) hopPower.size())];
        return n > 0 ? s / n : 0.0;
    }

    void onHop()
    {
        if (hopsFilled < 4)
            return;
        const double blockPower = meanOfLast (4);
        const double blockLufs = -0.691 + powToDb (blockPower);
        momentary.store ((float) blockLufs);
        shortTerm.store ((float) (-0.691 + powToDb (meanOfLast (30))));

        if (blockLufs > -70.0)
        {
            const int bin = std::clamp ((int) ((blockLufs + 70.0) * 10.0), 0, numBins - 1);
            ++counts[(size_t) bin];
            powers[(size_t) bin] += blockPower;
        }

        double total = 0.0; long n = 0;
        for (int b = 0; b < numBins; ++b) { total += powers[(size_t) b]; n += counts[(size_t) b]; }
        if (n == 0)
            return;
        const double relGate = -0.691 + powToDb (total / (double) n) - 10.0;
        const int firstBin = std::clamp ((int) std::ceil ((relGate + 70.0) * 10.0), 0, numBins - 1);
        total = 0.0; n = 0;
        for (int b = firstBin; b < numBins; ++b) { total += powers[(size_t) b]; n += counts[(size_t) b]; }
        if (n > 0)
            integrated.store ((float) (-0.691 + powToDb (total / (double) n)));
    }

    static constexpr int numBins = 900; // -70 .. +20 LUFS in 0.1 dB steps
    double sampleRate = 48000.0;
    std::array<KWeighting, 2> kw;
    std::array<double, 30> hopPower {};
    double acc = 0.0;
    int accCount = 0, hopSamples = 4800, hopIndex = 0, hopsFilled = 0;
    std::array<long, numBins> counts {};
    std::array<double, numBins> powers {};
    std::atomic<bool> resetRequested { false };
};
} // namespace bf
