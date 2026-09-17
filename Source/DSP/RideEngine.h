#pragma once

#include <array>
#include <atomic>
#include "Filters.h"

namespace bf
{
// The vocal rider. Decides, many times a second, whether the mic is hearing *its own* talker,
// someone else's bleed, or nothing, and only rides the gain for its own talker.
//
// Bleed detection has two independent layers:
//  - Sidechain guard: compare this mic against the other mics. If this mic isn't louder by
//    `guardDb`, the sound belongs to someone else.
//  - Voice lock: a learned long-term spectral fingerprint of this talker. Sound that doesn't
//    match (after a short listening window) is treated as bleed.
// When in doubt the rider freezes rather than boosting, so a wrong guess costs nothing audible.
class RideEngine
{
public:
    enum State { silent = 0, unsure = 1, owner = 2, bleed = 3 };
    static constexpr int numVoiceBands = 12;
    static constexpr int numProfileValues = numVoiceBands + 2; // band shape + pitch mean/spread (semitones)

    struct Settings
    {
        float targetLufs = -18.0f, maxBoostDb = 9.0f, maxCutDb = 9.0f, speed01 = 0.5f;
        float floorDb = -50.0f, guardDb = 4.0f, strict01 = 0.5f, tuckDb = 0.0f;
        bool enabled = true, guardOn = true, voiceLock = false;
        int channels = 1;
    };

    void prepare (double sr)
    {
        sampleRate = sr;
        kw.prepare (sr);
        const double fLo = 150.0, fHi = 6000.0;
        for (int b = 0; b < numVoiceBands; ++b)
        {
            const double f = fLo * std::pow (fHi / fLo, b / double (numVoiceBands - 1));
            bandCoeffs[(size_t) b] = makeSvf (FilterType::bandPass, f, 2.0, 0.0, sr);
        }
        hop = std::max (1, (int) std::round (sr * 0.001));
        decimation = std::max (1, (int) std::round (sr / 8000.0));
        pitchRate = sr / decimation;
        pitchLp = makeSvf (FilterType::lowPass, 900.0, 0.7071, 0.0, sr);
        bandSmooth = coeffForMs (250.0, sr);
        reset();
    }

    void reset()
    {
        kw.reset();
        for (auto& f : bands) f.reset();
        bandEnv.fill (1.0e-9);
        fastPow = idPow = scPow = 1.0e-12;
        levelPow = 0.0; levelInit = false;
        noiseDb = -80.0; gainDb = 0.0; desiredDb = 0.0; tuckNow = 0.0;
        match = 0.0; speechTime = silenceTime = ownerCandidate = 0.0; scSeen = 0.0;
        hopCounter = 0; state = silent;
        pitchFilter.reset(); pitchRing.fill (0.0f); pitchWrite = decimCounter = pitchCounter = 0;
        currentSemis = -1.0; pitchScore = 0.5;
    }

    void setSettings (const Settings& s) { settings = s; }

    // level:   post-EQ mono signal (what the listener hears) used to measure loudness
    // ident:   raw mono mic signal used for bleed decisions
    // sidechain: raw mono sidechain (other mics) or nullptr
    void process (const float* level, const float* ident, const float* sidechain, float* gainOut, int numSamples)
    {
        const double aFast = coeffForMs (20.0, sampleRate);
        const double aLevel = coeffForMs (900.0 - 600.0 * settings.speed01, sampleRate);
        const double gainMs = 3000.0 * std::pow (120.0 / 3000.0, (double) settings.speed01);
        const double aUp = coeffForMs (gainMs, sampleRate), aDown = coeffForMs (gainMs * 0.5, sampleRate);
        const double aTuckDown = coeffForMs (120.0, sampleRate), aTuckUp = coeffForMs (10.0, sampleRate);
        const double chanFactor = std::max (1, settings.channels);

        for (int i = 0; i < numSamples; ++i)
        {
            const double kx = kw.process (level[i]);
            const double p = kx * kx * chanFactor;
            fastPow += (p - fastPow) * aFast;

            const double id = ident[i];
            idPow += (id * id - idPow) * aFast;

            const double lp = pitchFilter.process (id, pitchLp);
            if (++decimCounter >= decimation)
            {
                decimCounter = 0;
                pitchRing[(size_t) pitchWrite] = (float) lp;
                pitchWrite = (pitchWrite + 1) % pitchRingSize;
                if (++pitchCounter >= 80)
                {
                    pitchCounter = 0;
                    currentSemis = detectPitchSemis();
                }
            }
            if (sidechain != nullptr)
                scPow += ((double) sidechain[i] * sidechain[i] - scPow) * aFast;

            for (int b = 0; b < numVoiceBands; ++b)
            {
                const double v = bands[(size_t) b].process (id, bandCoeffs[(size_t) b]);
                bandEnv[(size_t) b] += (v * v - bandEnv[(size_t) b]) * bandSmooth;
            }

            if (++hopCounter >= hop)
            {
                hopCounter = 0;
                decide (sidechain != nullptr);
            }

            if (state == owner)
            {
                if (! levelInit) { levelPow = fastPow; levelInit = true; }
                levelPow += (p - levelPow) * aLevel;

                const double levelLufs = powToDb (levelPow) - 0.691;
                const double fastLufs = powToDb (fastPow) - 0.691;
                double want = settings.targetLufs - levelLufs;
                want = std::min (want, settings.targetLufs + 7.0 - fastLufs); // catch sudden yells
                desiredDb = std::clamp (want, (double) -settings.maxCutDb, (double) settings.maxBoostDb);
            }

            const double wantGain = settings.enabled ? desiredDb : 0.0;
            gainDb += (wantGain - gainDb) * (wantGain > gainDb ? aUp : aDown);

            const double tuckTarget = (settings.enabled && state == bleed) ? -settings.tuckDb : (state == silent ? tuckNow : 0.0);
            tuckNow += (tuckTarget - tuckNow) * (tuckTarget < tuckNow ? aTuckDown : aTuckUp);

            gainOut[i] = (float) std::pow (10.0, (gainDb + tuckNow) / 20.0);
        }

        meterInputLufs.store ((float) (powToDb (fastPow) - 0.691));
        meterGainDb.store ((float) gainDb);
        meterTuckDb.store ((float) tuckNow);
        meterState.store (state);
        meterMatch.store ((float) match);
        meterSidechain.store (scSeen > 0.0);
    }

    //==============================================================================
    // Voice fingerprint ("sniff") API. Safe to call from the message thread.
    void startLearning()  { learnRequest.store (1); }
    void cancelLearning() { learnRequest.store (2); }
    bool isLearning() const { return learning.load(); }
    float getLearnProgress() const { return learnProgress.load(); }
    bool hasProfile() const { return profileValid.load(); }

    std::array<float, numProfileValues> getProfile() const
    {
        std::array<float, numProfileValues> p {};
        for (int b = 0; b < numProfileValues; ++b)
            p[(size_t) b] = profile[(size_t) b].load();
        return p;
    }

    void setProfile (const std::array<float, numProfileValues>& p, bool valid)
    {
        for (int b = 0; b < numProfileValues; ++b)
            profile[(size_t) b].store (p[(size_t) b]);
        profileValid.store (valid);
    }

    std::atomic<float> meterInputLufs { -120.0f }, meterGainDb { 0.0f }, meterTuckDb { 0.0f }, meterMatch { 0.0f };
    std::atomic<int> meterState { silent };
    std::atomic<bool> meterSidechain { false };

private:
    void decide (bool haveSidechain)
    {
        const double hopSec = hop / sampleRate;
        const double fastDb = powToDb (fastPow);

        // Background noise tracker: falls quickly, rises slowly.
        if (fastDb < noiseDb) noiseDb += (fastDb - noiseDb) * 0.05;
        else                  noiseDb = std::min (fastDb, noiseDb + 4.0 * hopSec);

        const bool speaking = fastDb > settings.floorDb && fastDb > noiseDb + 9.0;
        speechTime = speaking ? speechTime + hopSec : (silenceTime > 0.25 ? 0.0 : speechTime);
        silenceTime = speaking ? 0.0 : silenceTime + hopSec;

        // Sidechain guard
        const double scDb = powToDb (scPow);
        scSeen = (haveSidechain && scDb > -65.0) ? 2.0 : std::max (0.0, scSeen - hopSec);
        const bool guardBleed = settings.guardOn && haveSidechain && scDb > -65.0
                                && powToDb (idPow) - scDb < settings.guardDb;

        // Voice fingerprint
        std::array<double, numVoiceBands> shape {};
        if (speaking)
        {
            double mean = 0.0;
            for (int b = 0; b < numVoiceBands; ++b) { shape[(size_t) b] = powToDb (bandEnv[(size_t) b] + 1.0e-12); mean += shape[(size_t) b]; }
            mean /= numVoiceBands;
            // Remove overall level and spectral tilt so only the formant "shape" is compared
            // (tilt changes with mic distance and EQ; formants are the talker).
            double slopeNum = 0.0, slopeDen = 0.0;
            const double mid = (numVoiceBands - 1) * 0.5;
            for (int b = 0; b < numVoiceBands; ++b) { slopeNum += (b - mid) * (shape[(size_t) b] - mean); slopeDen += (b - mid) * (b - mid); }
            const double slope = slopeNum / slopeDen;
            for (int b = 0; b < numVoiceBands; ++b) shape[(size_t) b] -= mean + slope * (b - mid);
        }

        handleLearning (speaking && ! guardBleed && fastDb > noiseDb + 12.0, shape, hopSec);

        bool voiceBleed = false, voiceUnsure = false;
        if (settings.voiceLock && profileValid.load() && speaking && ! learning.load())
        {
            double dot = 0.0, na = 0.0, nb = 0.0;
            for (int b = 0; b < numVoiceBands; ++b)
            {
                const double pv = profile[(size_t) b].load();
                dot += shape[(size_t) b] * pv; na += shape[(size_t) b] * shape[(size_t) b]; nb += pv * pv;
            }
            const double corr = dot / std::sqrt (na * nb + 1.0e-12);

            // Pitch: how many "spreads" away from the learned pitch is this voice?
            if (currentSemis > 0.0)
            {
                const double z = std::abs (currentSemis - profile[numVoiceBands].load()) / std::max (1.5f, profile[numVoiceBands + 1].load());
                const double frame = std::exp (-0.5 * std::pow (std::max (0.0, z - 1.0), 2.0));
                pitchScore += (frame - pitchScore) * 0.02;
            }
            const double combined = 0.5 * std::max (0.0, corr) + 0.5 * pitchScore;
            if (speechTime < 2.0 * hopSec) pitchScore = 0.5;
            match = speechTime < 2.0 * hopSec ? combined : match + (combined - match) * 0.01;

            const double needed = 0.3 + 0.65 * settings.strict01;
            voiceUnsure = speechTime < 0.15;
            voiceBleed = ! voiceUnsure && match < needed;
        }

        State next;
        if (! speaking)          next = silent;
        else if (guardBleed)     next = bleed;
        else if (voiceUnsure)    next = unsure;
        else if (voiceBleed)     next = bleed;
        else                     next = owner;

        // Debounce the switch from someone else's voice back to ours (crosstalk flutter).
        if (next == owner && state == bleed)
        {
            ownerCandidate += hopSec;
            if (ownerCandidate < 0.04)
                next = bleed;
        }
        else
        {
            ownerCandidate = 0.0;
        }
        state = next;
    }

    void handleLearning (bool clean, const std::array<double, numVoiceBands>& shape, double hopSec)
    {
        const int req = learnRequest.exchange (0);
        if (req == 1)
        {
            learnSum.fill (0.0); learnTime = 0.0;
            pitchSum = pitchSqSum = 0.0; pitchFrames = 0;
            learnProgress.store (0.0f);
            learning.store (true);
        }
        else if (req == 2)
        {
            learning.store (false);
        }

        if (! learning.load() || ! clean)
            return;

        for (int b = 0; b < numVoiceBands; ++b)
            learnSum[(size_t) b] += shape[(size_t) b];
        if (currentSemis > 0.0)
        {
            pitchSum += currentSemis; pitchSqSum += currentSemis * currentSemis; ++pitchFrames;
        }
        learnTime += hopSec;
        learnProgress.store ((float) std::min (1.0, learnTime / learnSeconds));

        if (learnTime >= learnSeconds)
        {
            const double frames = learnTime / hopSec;
            for (int b = 0; b < numVoiceBands; ++b)
                profile[(size_t) b].store ((float) (learnSum[(size_t) b] / frames));
            const double pm = pitchFrames > 0 ? pitchSum / pitchFrames : 30.0;
            const double pv = pitchFrames > 0 ? pitchSqSum / pitchFrames - pm * pm : 100.0;
            profile[numVoiceBands].store ((float) pm);
            profile[numVoiceBands + 1].store ((float) std::sqrt (std::max (0.0, pv)));
            profileValid.store (true);
            learning.store (false);
        }
    }

    // YIN pitch estimate on the ~8 kHz decimated signal. Returns semitones above 55 Hz, or -1 if unvoiced.
    double detectPitchSemis() const
    {
        const int window = (int) (pitchRate * 0.025);
        const int minLag = (int) (pitchRate / 400.0), maxLag = (int) (pitchRate / 70.0);
        if (window + maxLag + 2 > pitchRingSize)
            return -1.0;

        const int start = (pitchWrite - window - maxLag - 2 + 2 * pitchRingSize) % pitchRingSize;
        auto at = [&] (int k) { return (double) pitchRing[(size_t) ((start + k) % pitchRingSize)]; };

        double energy = 0.0;
        for (int j = 0; j < window; ++j) energy += at (j) * at (j);
        if (energy < 1.0e-9)
            return -1.0;

        std::array<double, 160> d {};
        double running = 0.0;
        int best = -1;
        for (int lag = 1; lag <= maxLag + 1 && lag < (int) d.size(); ++lag)
        {
            double sum = 0.0;
            for (int j = 0; j < window; ++j) { const double diff = at (j) - at (j + lag); sum += diff * diff; }
            running += sum;
            d[(size_t) lag] = sum * lag / std::max (running, 1.0e-12);
        }
        for (int lag = minLag; lag <= maxLag && lag + 1 < (int) d.size(); ++lag)
        {
            if (d[(size_t) lag] < 0.2)
            {
                while (lag + 1 <= maxLag && d[(size_t) lag + 1] < d[(size_t) lag]) ++lag;
                best = lag;
                break;
            }
        }
        if (best < 0)
            return -1.0;

        const double a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1];
        const double denom = a - 2.0 * b + c;
        const double refined = best + (std::abs (denom) > 1.0e-12 ? 0.5 * (a - c) / denom : 0.0);
        return 12.0 * std::log2 ((pitchRate / refined) / 55.0);
    }

    static constexpr double learnSeconds = 8.0;
    static constexpr int pitchRingSize = 512;

    Settings settings;
    double sampleRate = 48000.0;
    KWeighting kw;
    std::array<SvfCoeffs, numVoiceBands> bandCoeffs;
    std::array<Svf, numVoiceBands> bands;
    std::array<double, numVoiceBands> bandEnv {};
    double bandSmooth = 0.0;

    double fastPow = 0, idPow = 0, scPow = 0, levelPow = 0;
    bool levelInit = false;
    double noiseDb = -80, gainDb = 0, desiredDb = 0, tuckNow = 0;
    double match = 0, speechTime = 0, silenceTime = 0, ownerCandidate = 0, scSeen = 0;
    int hop = 48, hopCounter = 0;
    State state = silent;

    std::array<std::atomic<float>, numProfileValues> profile {};
    std::atomic<bool> profileValid { false }, learning { false };
    std::atomic<int> learnRequest { 0 };
    std::atomic<float> learnProgress { 0.0f };
    std::array<double, numVoiceBands> learnSum {};
    double learnTime = 0.0, pitchSum = 0.0, pitchSqSum = 0.0;
    long pitchFrames = 0;

    SvfCoeffs pitchLp;
    Svf pitchFilter;
    std::array<float, pitchRingSize> pitchRing {};
    int pitchWrite = 0, decimation = 6, decimCounter = 0, pitchCounter = 0;
    double pitchRate = 8000.0, currentSemis = -1.0, pitchScore = 0.5;
};
} // namespace bf
