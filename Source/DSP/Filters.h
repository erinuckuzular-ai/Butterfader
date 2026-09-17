#pragma once

#include <algorithm>
#include <cmath>
#include <complex>

namespace bf
{
constexpr double pi = 3.14159265358979323846;

inline double coeffForMs (double ms, double sampleRate)
{
    return ms <= 0.0 ? 1.0 : 1.0 - std::exp (-1.0 / (ms * 0.001 * sampleRate));
}

inline float dbToGain (float db)      { return std::pow (10.0f, db / 20.0f); }
inline double powToDb (double power)  { return power > 1.0e-12 ? 10.0 * std::log10 (power) : -120.0; }

// BS.1770 K-weighting pre-filter (high shelf + RLB high-pass), bilinear with prewarping.
class KWeighting
{
public:
    void prepare (double sampleRate)
    {
        // Stage 1: high shelf, +4 dB around 1.68 kHz.
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (pi * f0 / sampleRate);
            const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            s1 = { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                   2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
        // Stage 2: high-pass around 38 Hz.
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (pi * f0 / sampleRate);
            const double a0 = 1.0 + K / Q + K * K;
            s2 = { 1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
        reset();
    }

    void reset() { s1.z1 = s1.z2 = s2.z1 = s2.z2 = 0.0; }

    double process (double x) { return s2.process (s1.process (x)); }

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double process (double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };
    Biquad s1, s2;
};

//==============================================================================
enum class FilterType
{
    bell = 0, lowShelf, highShelf, lowCut12, lowCut24, highCut12, highCut24, notch,
    lowPass, highPass, bandPass
};

// Coefficients for Andrew Simper's trapezoidal SVF. Output = m0*v0 + m1*v1 + m2*v2.
struct SvfCoeffs
{
    double g = 0.0, k = 1.0, a1 = 1.0, a2 = 0.0, a3 = 0.0;
    double m0 = 1.0, m1 = 0.0, m2 = 0.0;

    // Exact response of this filter at frequency f (bilinear transform of the analog prototype).
    std::complex<double> response (double f, double sampleRate) const
    {
        const double w = std::tan (pi * std::min (f, sampleRate * 0.4999) / sampleRate) / g;
        const std::complex<double> s (0.0, w);
        const auto d = s * s + k * s + 1.0;
        return m0 + m1 * s / d + m2 / d;
    }
};

inline SvfCoeffs makeSvf (FilterType type, double freq, double q, double gainDb, double sampleRate)
{
    SvfCoeffs c;
    const double fc = std::clamp (freq, 5.0, sampleRate * 0.49);
    const double A = std::pow (10.0, gainDb / 40.0);
    const double t = std::tan (pi * fc / sampleRate);
    q = std::max (q, 0.025);
    c.g = t;
    c.k = 1.0 / q;

    switch (type)
    {
        case FilterType::bell:      c.k = 1.0 / (q * A); c.m1 = c.k * (A * A - 1.0); break;
        case FilterType::lowShelf:  c.g = t / std::sqrt (A); c.m1 = c.k * (A - 1.0); c.m2 = A * A - 1.0; break;
        case FilterType::highShelf: c.g = t * std::sqrt (A); c.m0 = A * A; c.m1 = c.k * (1.0 - A) * A; c.m2 = 1.0 - A * A; break;
        case FilterType::notch:     c.m1 = -c.k; break;
        case FilterType::lowPass:
        case FilterType::highCut12:
        case FilterType::highCut24: c.m0 = 0.0; c.m2 = 1.0; break;
        case FilterType::highPass:
        case FilterType::lowCut12:
        case FilterType::lowCut24:  c.m1 = -c.k; c.m2 = -1.0; break;
        case FilterType::bandPass:  c.m0 = 0.0; c.m1 = c.k; break;
    }

    c.a1 = 1.0 / (1.0 + c.g * (c.g + c.k));
    c.a2 = c.g * c.a1;
    c.a3 = c.g * c.a2;
    return c;
}

struct Svf
{
    double ic1 = 0.0, ic2 = 0.0;
    double v1 = 0.0, v2 = 0.0;

    void reset() { ic1 = ic2 = v1 = v2 = 0.0; }

    double process (double v0, const SvfCoeffs& c)
    {
        const double v3 = v0 - ic2;
        v1 = c.a1 * ic1 + c.a2 * v3;
        v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        return c.m0 * v0 + c.m1 * v1 + c.m2 * v2;
    }

    double high (double v0, const SvfCoeffs& c) const { return v0 - c.k * v1 - v2; }
    double low() const { return v2; }
};
} // namespace bf
