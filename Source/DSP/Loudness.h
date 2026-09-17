#pragma once

#include <array>
#include <cmath>
#include <vector>
#include <algorithm>

namespace bf
{

inline double energyToLufs (double e)  { return e > 1.0e-12 ? -0.691 + 10.0 * std::log10 (e) : -120.0; }
inline double lufsToEnergy (double l)  { return std::pow (10.0, (l + 0.691) / 10.0); }
inline float  dbToGain (float db)      { return std::pow (10.0f, db / 20.0f); }
inline float  gainToDb (float g)       { return g > 1.0e-6f ? 20.0f * std::log10 (g) : -120.0f; }

//==============================================================================
// ITU-R BS.1770 K-weighting (pre-filter shelf + RLB high-pass), valid at any sample rate.
class KFilter
{
public:
    void prepare (double sampleRate)
    {
        const double pi = 3.14159265358979323846;

        {   // stage 1: high shelf
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K  = std::tan (pi * f0 / sampleRate);
            const double Vh = std::pow (10.0, G / 20.0);
            const double Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            b1 = { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0 };
            a1 = { 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
        {   // stage 2: high-pass
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K  = std::tan (pi * f0 / sampleRate);
            const double a0 = 1.0 + K / Q + K * K;
            a2 = { 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
        reset();
    }

    void reset() { s1 = { 0.0, 0.0 }; s2 = { 0.0, 0.0 }; }

    inline double process (double x)
    {
        // transposed direct form II
        const double y1 = b1[0] * x + s1[0];
        s1[0] = b1[1] * x - a1[0] * y1 + s1[1];
        s1[1] = b1[2] * x - a1[1] * y1;

        const double y2 = y1 + s2[0];
        s2[0] = -2.0 * y1 - a2[0] * y2 + s2[1];
        s2[1] = y1 - a2[1] * y2;
        return y2;
    }

private:
    std::array<double, 3> b1 {};
    std::array<double, 2> a1 {}, a2 {}, s1 {}, s2 {};
};

//==============================================================================
// Momentary (400 ms), short-term (3 s) and gated integrated loudness per BS.1770-4 / EBU R128.
// Mono input counts as dual-mono (as it plays back through two speakers).
class LoudnessMeter
{
public:
    void prepare (double sampleRate, int numChannels)
    {
        channels = std::max (1, std::min (numChannels, 8));
        filters.assign ((size_t) channels, {});
        for (auto& f : filters) f.prepare (sampleRate);
        blockLength = std::max (1, (int) std::lround (sampleRate * 0.1));
        reset();
    }

    void reset()
    {
        for (auto& f : filters) f.reset();
        blocks.fill (0.0);
        blockCount = 0; writePos = 0; accumulator = 0.0; samplesInBlock = 0;
        histCount.fill (0); histEnergy.fill (0.0);
        momentary = shortTerm = integrated = -120.0;
    }

    // Returns the K-weighted, channel-summed energy of the sample (useful for other detectors).
    inline double pushFrame (const float* const* data, int index)
    {
        double e = 0.0;
        for (int ch = 0; ch < channels; ++ch)
        {
            const double k = filters[(size_t) ch].process (data[ch][index]);
            e += k * k;
        }
        if (channels == 1) e *= 2.0;

        accumulator += e;
        if (++samplesInBlock >= blockLength)
            finishBlock();
        return e;
    }

    double getMomentary() const  { return momentary; }
    double getShortTerm() const  { return shortTerm; }
    double getIntegrated() const { return integrated; }

private:
    static constexpr int historyBlocks = 30;       // 3 s of 100 ms blocks
    static constexpr int numBins = 1000;           // 0.1 LU bins from -70 to +30 LUFS

    void finishBlock()
    {
        blocks[(size_t) writePos] = accumulator / blockLength;
        writePos = (writePos + 1) % historyBlocks;
        blockCount = std::min (blockCount + 1, 1 << 30);
        accumulator = 0.0; samplesInBlock = 0;

        auto meanOfLast = [this] (int n)
        {
            n = std::min (n, std::min (blockCount, historyBlocks));
            double sum = 0.0;
            for (int i = 1; i <= n; ++i)
                sum += blocks[(size_t) ((writePos - i + historyBlocks) % historyBlocks)];
            return n > 0 ? sum / n : 0.0;
        };

        const double mEnergy = meanOfLast (4);
        momentary = energyToLufs (mEnergy);
        shortTerm = energyToLufs (meanOfLast (30));

        if (blockCount >= 4 && momentary > -70.0)
        {
            const int bin = std::clamp ((int) ((momentary + 70.0) * 10.0), 0, numBins - 1);
            ++histCount[(size_t) bin];
            histEnergy[(size_t) bin] += mEnergy;
            updateIntegrated();
        }
    }

    void updateIntegrated()
    {
        double sum = 0.0; long long count = 0;
        for (int b = 0; b < numBins; ++b) { sum += histEnergy[(size_t) b]; count += histCount[(size_t) b]; }
        if (count == 0) return;

        const double relativeGate = energyToLufs (sum / (double) count) - 10.0;
        const int firstBin = std::clamp ((int) std::ceil ((relativeGate + 70.0) * 10.0), 0, numBins);
        sum = 0.0; count = 0;
        for (int b = firstBin; b < numBins; ++b) { sum += histEnergy[(size_t) b]; count += histCount[(size_t) b]; }
        integrated = count > 0 ? energyToLufs (sum / (double) count) : -120.0;
    }

    int channels = 2, blockLength = 4800;
    std::vector<KFilter> filters;
    std::array<double, historyBlocks> blocks {};
    int blockCount = 0, writePos = 0, samplesInBlock = 0;
    double accumulator = 0.0;
    std::array<long long, numBins> histCount {};
    std::array<double, numBins> histEnergy {};
    double momentary = -120.0, shortTerm = -120.0, integrated = -120.0;
};

} // namespace bf
