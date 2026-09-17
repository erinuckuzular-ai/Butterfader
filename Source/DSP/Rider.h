#pragma once

#include "Loudness.h"

namespace bf
{

// Waves-Rider-style automatic gain: learns the source loudness while it is actually playing,
// holds still through pauses (so room tone is never pumped up), and eases the gain toward
// whatever it takes to land on the target. A slow output trim corrects for loudness the
// limiter shaves off.
class Rider
{
public:
    enum Speed { gentle = 0, normal, tight };
    static constexpr int controlInterval = 32;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        dt = controlInterval / sampleRate;
        momentaryCoef = coefFor (0.4);
    }

    // Keeps the learned gain across transport stops so playback resumes at the right level.
    void resetLearning()
    {
        momentary = estimate = outputEstimate = outputMomentary = belowGateSeconds = 0.0;
        haveEstimate = false;
        activeSeconds = 0.0;
        trimDb = 0.0f;
    }

    // Call once per sample with K-weighted energies. Returns the gain in dB to apply now.
    inline float push (double inputEnergy, double outputEnergy, float targetLufs, int speed)
    {
        momentary += (inputEnergy - momentary) * momentaryCoef;
        outputAccum += outputEnergy;

        if (++counter >= controlInterval)
        {
            counter = 0;
            update (targetLufs, speed, outputAccum / controlInterval);
            outputAccum = 0.0;
        }

        smoothedDb += (gainDb - smoothedDb) * (1.0f / controlInterval);
        return smoothedDb;
    }

    float getGainDb() const    { return smoothedDb; }
    bool  isActive() const     { return active; }
    bool  isLearning() const   { return ! haveEstimate || activeSeconds < 2.0; }
    void  setGainDb (float db) { gainDb = smoothedDb = db; }

private:
    double coefFor (double seconds) const { return 1.0 - std::exp (-1.0 / (sr * seconds)); }

    void update (float target, int speed, double outputEnergy)
    {
        outputMomentary += (outputEnergy - outputMomentary) * (1.0 - std::exp (-dt / 0.4));

        const double inL = energyToLufs (momentary);
        const double estL = haveEstimate ? energyToLufs (estimate) : -120.0;

        // present: something is playing (not silence, not a pause well below the programme)
        active = inL > -60.0 && (! haveEstimate || inL > estL - 20.0);
        if (! active) { belowGateSeconds = 0.0; return; }  // hold through pauses

        // counted: mirrors the -10 LU relative gate of integrated loudness, but still follows
        // a source that has genuinely become much quieter for a few seconds
        const bool counted = ! haveEstimate || inL > estL - 10.0 || belowGateSeconds > 3.0;
        belowGateSeconds = inL > estL - 10.0 ? 0.0 : belowGateSeconds + dt;

        const double detectSeconds = speed == gentle ? 4.0 : speed == tight ? 0.8 : 2.0;
        const double smoothSeconds = speed == gentle ? 1.2 : speed == tight ? 0.2 : 0.5;
        const float  maxRateDbPerSec = speed == gentle ? 2.0f : speed == tight ? 12.0f : 5.0f;
        const bool locking = activeSeconds < 2.0;   // lock on fast so the first words already sit right
        activeSeconds += dt;

        if (counted)
        {
            if (! haveEstimate) { estimate = momentary; haveEstimate = true; }
            else estimate += (momentary - estimate) * (1.0 - std::exp (-dt / (locking ? 0.4 : detectSeconds)));

            outputEstimate += (outputMomentary - outputEstimate) * (1.0 - std::exp (-dt / 3.0));
            if (activeSeconds > 3.0)
            {
                const double err = std::clamp ((double) target - energyToLufs (outputEstimate), -3.0, 3.0);
                trimDb = std::clamp (trimDb + (float) (err * dt / 4.0), -6.0f, 6.0f);
            }
        }

        const float desired = std::clamp ((float) (target - energyToLufs (estimate)) + trimDb, -24.0f, 36.0f);
        const float step = (desired - gainDb) * (float) (1.0 - std::exp (-dt / (locking ? 0.08 : smoothSeconds)));
        const float maxStep = (locking ? 60.0f : maxRateDbPerSec) * (float) dt;
        gainDb += std::clamp (step, -maxStep, maxStep);
    }

    double sr = 48000.0, dt = 32.0 / 48000.0, momentaryCoef = 0.0;
    double outputMomentary = 0.0, belowGateSeconds = 0.0;
    double momentary = 0.0, estimate = 0.0, outputEstimate = 0.0, outputAccum = 0.0, activeSeconds = 0.0;
    bool haveEstimate = false, active = false;
    int counter = 0;
    float gainDb = 0.0f, smoothedDb = 0.0f, trimDb = 0.0f;
};

} // namespace bf
