#pragma once

#include <atomic>
#include <array>
#include <cmath>
#include <cstdint>
#include <juce_core/juce_core.h>

namespace bf
{

// Lets every Butterfader on a mic track (in the same host process) see how loud the others are,
// without any audio routing. Premiere and Audition load all instances into one process.
class MicLink
{
public:
    static constexpr int maxMics = 32;
    static constexpr std::uint32_t staleMs = 400;   // an instance that stopped processing drops out

    struct Slot
    {
        std::atomic<bool> used { false };
        std::atomic<bool> linked { false };
        std::atomic<int> group { 0 };
        std::atomic<float> energy { 0.0f };          // levelled, fast-envelope speech energy
        std::atomic<bool> talking { false };
        std::atomic<std::uint32_t> updatedMs { 0 };
    };

    static std::array<Slot, maxMics>& slots()
    {
        static std::array<Slot, maxMics> s;
        return s;
    }

    static int acquire()
    {
        for (int i = 0; i < maxMics; ++i)
        {
            bool expected = false;
            if (slots()[(size_t) i].used.compare_exchange_strong (expected, true))
            {
                slots()[(size_t) i].linked = false;
                return i;
            }
        }
        return -1;
    }

    static void release (int index)
    {
        if (index < 0) return;
        slots()[(size_t) index].linked = false;
        slots()[(size_t) index].used = false;
    }

    static std::uint32_t now() { return juce::Time::getMillisecondCounter(); }

    struct Others { double energy = 0.0; int count = 0; int talking = 0; };

    static Others others (int self, int group)
    {
        Others o;
        const auto t = now();
        for (int i = 0; i < maxMics; ++i)
        {
            if (i == self) continue;
            auto& s = slots()[(size_t) i];
            if (! s.used.load() || ! s.linked.load() || s.group.load() != group) continue;
            if (t - s.updatedMs.load() > staleMs) continue;
            o.energy += s.energy.load();
            ++o.count;
            if (s.talking.load()) ++o.talking;
        }
        return o;
    }
};

//==============================================================================
// Tracks the background noise floor: follows dips quickly, creeps up slowly,
// and is never allowed to rise into the speech level.
class NoiseFloor
{
public:
    void reset() { floorDb = -90.0f; initialised = false; }

    inline float update (float levelDb, float speechDb, bool haveSpeech, double dt)
    {
        if (! initialised) { floorDb = levelDb; initialised = true; }
        if (levelDb < floorDb) floorDb += (levelDb - floorDb) * (float) (1.0 - std::exp (-dt / 0.15));
        else                   floorDb += (float) (1.5 * dt);   // dB per second
        if (haveSpeech) floorDb = std::min (floorDb, speechDb - 20.0f);
        floorDb = std::max (floorDb, -100.0f);
        return floorDb;
    }

    float get() const { return floorDb; }

private:
    float floorDb = -90.0f;
    bool initialised = false;
};

} // namespace bf
