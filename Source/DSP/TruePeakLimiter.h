#pragma once

#include <array>
#include <cmath>
#include <vector>
#include <algorithm>

namespace bf
{

//==============================================================================
// 8x polyphase interpolator reporting the highest inter-sample peak (true peak).
// Odd-length filter centred on a sample, so phase 0 is the exact sample value.
// Each push covers positions [n - 12, n - 11.125] of the input.
class TruePeakDetector
{
public:
    static constexpr int tapsPerPhase = 24;
    static constexpr int phases = 8;
    static constexpr int delaySamples = tapsPerPhase / 2;

    TruePeakDetector()
    {
        const int n = tapsPerPhase * phases + 1;
        const double centre = tapsPerPhase * phases * 0.5, pi = 3.14159265358979323846;
        auto besselI0 = [] (double x)
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 40; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; }
            return sum;
        };
        const double beta = 8.0;
        for (int k = 0; k < n; ++k)
        {
            const double t = (k - centre) / phases;
            const double sinc = std::abs (t) < 1.0e-9 ? 1.0 : std::sin (pi * t) / (pi * t);
            const double r = (k - centre) / (centre + 1.0);
            const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / besselI0 (beta);
            coeffs[(size_t) k] = (float) (sinc * w);
        }
        for (int p = 1; p < phases; ++p)   // unity DC gain per phase (phase 0 is already a pure delay)
        {
            double sum = 0.0;
            for (int k = p; k < n; k += phases) sum += coeffs[(size_t) k];
            for (int k = p; k < n; k += phases) coeffs[(size_t) k] = (float) (coeffs[(size_t) k] / sum);
        }
    }

    void reset() { history.fill (0.0f); pos = 0; }

    inline float push (float x)
    {
        pos = (pos + historyLength - 1) % historyLength;
        history[(size_t) pos] = x;

        float peak = std::abs (history[(size_t) ((pos + delaySamples) % historyLength)]);
        for (int p = 1; p < phases; ++p)
        {
            // y at position n - delaySamples + p / phases uses taps k = (phases - p) + phases * j on x[n - j]
            float y = 0.0f;
            const int first = phases - p;
            for (int j = 0; j < tapsPerPhase; ++j)
                y += coeffs[(size_t) (first + phases * j)] * history[(size_t) ((pos + j) % historyLength)];
            peak = std::max (peak, std::abs (y));
        }
        return peak;
    }

private:
    static constexpr int historyLength = tapsPerPhase + 1;
    std::array<float, tapsPerPhase * phases + 1> coeffs {};
    std::array<float, historyLength> history {};
    int pos = 0;
};

//==============================================================================
// Look-ahead, stereo-linked, true-peak brickwall limiter.
// Gain path: required gain -> sliding minimum -> program-dependent release -> moving average.
// The minimum + average over the same window guarantees the gain has fully reached its
// target by the time the peak comes out of the delay line, with a smooth (no-click) attack.
class TruePeakLimiter
{
public:
    enum Character { clean = 0, punchy, smooth };

    void prepare (double sampleRate, int numChannels, float lookaheadMs = 2.0f)
    {
        sr = sampleRate;
        channels = std::max (1, numChannels);
        window = std::max (8, (int) std::lround (sampleRate * lookaheadMs * 0.001));
        latency = window - 1 + TruePeakDetector::delaySamples + 1; // sample (m - 13) sits inside the 3-push coverage [m - 14, m - 11.125]

        detectors.assign ((size_t) channels, {});
        delay.assign ((size_t) channels, std::vector<float> ((size_t) latency, 0.0f));
        sustainCoef = 1.0f - std::exp (-1.0f / (float) sampleRate);
        minValues.assign ((size_t) window + 1, 1.0f);
        minIndices.assign ((size_t) window + 1, 0);
        boxBuffer.assign ((size_t) window, 1.0f);
        reset();
    }

    void reset()
    {
        for (auto& d : detectors) d.reset();
        for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0f);
        std::fill (boxBuffer.begin(), boxBuffer.end(), 1.0f);
        boxSum = window; boxPos = 0; delayPos = 0;
        dequeHead = dequeTail = 0; sampleCounter = 0;
        releaseState = 1.0f; sustain = 0.0f; lastPeak = lastPeak2 = 0.0f; currentGain = 1.0f;
    }

    int getLatencySamples() const { return latency; }
    float getCurrentGain() const  { return currentGain; }

    // Processes one frame in place. Returns the applied gain (<= 1).
    inline float processFrame (float* const* data, float ceiling, int character)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
            peak = std::max (peak, detectors[(size_t) ch].push (data[ch][0]));

        const float detected = std::max ({ peak, lastPeak, lastPeak2 });
        lastPeak2 = lastPeak;
        lastPeak = peak;
        const float required = detected > ceiling ? ceiling / detected : 1.0f;

        // sliding minimum over `window` samples (monotonic deque)
        const size_t cap = minValues.size();
        while (dequeHead != dequeTail && minValues[(dequeTail + cap - 1) % cap] >= required)
            dequeTail = (dequeTail + cap - 1) % cap;
        minValues[dequeTail] = required;
        minIndices[dequeTail] = sampleCounter;
        dequeTail = (dequeTail + 1) % cap;
        while (minIndices[dequeHead] + window <= sampleCounter)
            dequeHead = (dequeHead + 1) % cap;
        const float held = minValues[dequeHead];
        ++sampleCounter;

        // program-dependent release: sustained limiting releases more slowly (less pumping)
        const float grDb = -20.0f * std::log10 (std::max (held, 1.0e-4f));
        sustain += (std::min (grDb / 6.0f, 1.0f) - sustain) * sustainCoef;
        if (held < releaseState)
            releaseState = held;
        else
        {
            const float baseMs = character == punchy ? 40.0f : character == smooth ? 600.0f : 180.0f;
            const float ms = baseMs * (1.0f + 3.0f * sustain);
            const float coef = 1.0f - std::exp (-1.0f / (float) (sr * ms * 0.001));
            releaseState += (held - releaseState) * coef;
        }

        // moving average over the same window
        boxSum += (double) releaseState - boxBuffer[(size_t) boxPos];
        boxBuffer[(size_t) boxPos] = releaseState;
        boxPos = (boxPos + 1) % window;
        if (boxPos == 0) // re-sum occasionally to stop floating point drift
        {
            boxSum = 0.0;
            for (auto v : boxBuffer) boxSum += v;
        }
        currentGain = std::min (1.0f, (float) (boxSum / window));

        // delay the audio and apply the gain, with a last-resort sample clamp
        const int len = latency;
        for (int ch = 0; ch < channels; ++ch)
        {
            auto& line = delay[(size_t) ch];
            const float in = data[ch][0];
            const float out = line[(size_t) delayPos] * currentGain;
            line[(size_t) delayPos] = in;
            data[ch][0] = std::clamp (out, -ceiling, ceiling);
        }
        delayPos = (delayPos + 1) % len;
        return currentGain;
    }

private:
    double sr = 48000.0;
    int channels = 2, window = 96, latency = 101;
    std::vector<TruePeakDetector> detectors;
    std::vector<std::vector<float>> delay;
    std::vector<float> minValues;
    std::vector<long long> minIndices;
    size_t dequeHead = 0, dequeTail = 0;
    long long sampleCounter = 0;
    std::vector<float> boxBuffer;
    double boxSum = 96.0;
    int boxPos = 0, delayPos = 0;
    float releaseState = 1.0f, sustain = 0.0f, lastPeak = 0.0f, lastPeak2 = 0.0f, currentGain = 1.0f;
    float sustainCoef = 1.0f - std::exp (-1.0f / 48000.0f);
};

} // namespace bf
