#pragma once

#include <juce_core/juce_core.h>

namespace bf
{

struct Platform
{
    const char* name;
    const char* note;     // shown under the dropdown
    float targetLufs;
    float maxTruePeak;    // the platform's own ceiling; the plug-in never goes above this
};

// Published delivery / normalisation targets (integrated loudness).
inline const Platform platforms[] =
{
    { "Spotify",                  "Turns louder tracks down",          -14.0f,  -1.0f },
    { "Apple Music",              "Sound Check level",                 -16.0f,  -1.0f },
    { "YouTube",                  "Turns louder uploads down",         -14.0f,  -1.0f },
    { "Amazon Music",             "Wants extra peak headroom",         -14.0f,  -2.0f },
    { "Tidal",                    "Turns louder tracks down",          -14.0f,  -1.0f },
    { "Deezer",                   "Turns louder tracks down",          -15.0f,  -1.0f },
    { "SoundCloud",               "No normalising, -14 is a safe bet", -14.0f,  -1.0f },
    { "Instagram / TikTok",       "Social video",                      -14.0f,  -1.0f },
    { "Podcast (Apple, Spotify)", "Spoken word standard",              -16.0f,  -1.0f },
    { "Audiobook (ACX)",          "Close to ACX spec",                 -20.0f,  -3.0f },
    { "Broadcast EU (EBU R128)",  "TV and radio in Europe",            -23.0f,  -1.0f },
    { "Broadcast US (ATSC A/85)", "TV in the US",                      -24.0f,  -2.0f },
    { "Custom",                   "Pick your own target",              -14.0f,   0.0f },
};

inline constexpr int numPlatforms = (int) (sizeof (platforms) / sizeof (platforms[0]));
inline constexpr int customPlatform = numPlatforms - 1;

inline juce::StringArray platformNames()
{
    juce::StringArray names;
    for (auto& p : platforms) names.add (p.name);
    return names;
}

} // namespace bf
