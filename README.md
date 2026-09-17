# Butterfader

A loudness plug-in for Premiere Pro, Audition and any VST3/AU host. Pick where the audio is going, and Butterfader rides the level to that platform's target with a true-peak limiter on the end.

![Butterfader](docs/screenshot.png)

## What it does

| | |
|---|---|
| **Deliver to** | Spotify, Apple Music, YouTube, Amazon, Tidal, Deezer, SoundCloud, Instagram/TikTok, Podcast, Audiobook (ACX), EBU R128, ATSC A/85 or Custom. Sets the loudness target and caps the ceiling at the platform's limit. |
| **Rider (Auto)** | Learns how loud the source is while it plays and moves the gain toward the target, up to +36 dB for quiet recordings. It holds still through pauses, so room tone isn't pumped up. Switch to Manual to set the gain yourself. |
| **True-peak limiter** | Look-ahead, stereo-linked, 8x true-peak detection. The ceiling defaults to -1 dBTP. Character: Clean, Punchy (fast release) or Smooth (slow release). Release slows automatically under sustained limiting. |
| **Loudness meter** | Short-term fill with a momentary tick, measured to ITU-R BS.1770 / EBU R128. Green is on target, amber a bit hot, red too loud, blue quiet. |
| **Integrated / true peak** | Gated integrated LUFS and max true peak since the last Reset. |
| **Reduction meter** | How hard the limiter is working: Easy (under 3 dB), Working (3 to 6 dB), Too hard (over 6 dB). The butter melts when it's working hard. |
| **Source clipping** | Watches the audio before any processing and counts flat-topped or over-0 dBFS clipping already in the recording. Click the badge to clear it. |
| **A/B Compare** | Plays the original, delayed and gain-matched to the processed loudness, so you hear the processing rather than the volume change. It's also the host bypass. |

Leveling sets how quickly the rider reacts: Gentle, Normal or Tight.

Latency is about 2.3 ms, reported to the host. The window scales from 60% to 200% (drag the corner).

## Tips for Premiere Pro

- Put Butterfader on the **Mix** track (the master) so it hears the whole programme, then export.
- The rider learns while audio plays, so the first second or so after a cold start is where it locks on.
- Mono tracks are measured as dual-mono, which is how they play back on two speakers.

## Install

Download **Butterfader.dmg** from the [latest release](../../releases/latest). Every push to `main` builds one automatically. Open it and double-click **Install Butterfader.pkg**, then:

- **Premiere Pro:** Settings > Audio > Audio Plug-in Manager > Scan for Plug-ins
- **Audition:** Effects > Audio Plug-in Manager > Scan for Plug-ins

The installer isn't notarised. If macOS blocks it, right-click > Open.

## Build

Requires Xcode and CMake. JUCE 8 is fetched automatically.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Butterfader_VST3 Butterfader_AU   # also copies into ~/Library/Audio/Plug-Ins
```

Checks:

```bash
cmake --build build --target DSPTest && ./build/DSPTest_artefacts/Release/DSPTest        # loudness accuracy, true peak, auto level, bypass match, clip detection
cmake --build build --target UISnapshot && ./build/UISnapshot_artefacts/Release/UISnapshot /tmp   # renders the UI to PNGs
./scripts/make-dmg.sh                                                                    # universal installer DMG
```

Source layout: `Source/DSP/Loudness.h` (BS.1770 meter), `Source/DSP/TruePeakLimiter.h`, `Source/DSP/Rider.h`, `Source/Platforms.h` (targets), `Source/UI/`.
