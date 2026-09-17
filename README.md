# Butterfader

A loudness plug-in for Premiere Pro, Audition and any VST3/AU host. Put one on each mic to level voices and duck bleed, and one on the master to hit a platform's loudness target with a true-peak limiter on the end.

![Butterfader](docs/screenshot.png)

## Two modes

**Mic** goes on each voice track. It levels the voice to an even speech level (about -20 LUFS) with a -2 dBTP safety ceiling, and removes bleed from other mics.

**Master** goes on the Mix track. It takes the whole programme to a platform target.

![Mic mode](docs/screenshot-mic.png)

## What it does

| | |
|---|---|
| **Debleed (Mic)** | *Linked*: every Butterfader in Mic mode in the same link group shares who's talking, with no routing, and turns down the mics that aren't talking (like an auto-mixer). *Gate*: ducks quiet bleed using this mic's own level, and is used automatically when a linked mic has no partners. *Off*. |
| **Link group (Mic)** | A to D. Mics only duck others in the same group, so separate shows or scenes don't interfere. |
| **Noise guard** | Tracks the background hiss and hum. The rider never learns levels from noise or bleed, and between phrases the noise is pushed back down to at least the level it came in at. |
| **Deliver to (Master)** | Spotify, Apple Music, YouTube, Amazon, Tidal, Deezer, SoundCloud, Instagram/TikTok, Podcast, Audiobook (ACX), EBU R128, ATSC A/85 or Custom. Sets the loudness target and caps the ceiling at the platform's limit. |
| **Rider (Auto)** | Learns how loud the source is while it plays and moves the gain toward the target, up to +36 dB for quiet recordings. It holds still through pauses, so room tone isn't pumped up. Switch to Manual to set the gain yourself. |
| **True-peak limiter** | Look-ahead, stereo-linked, 8x true-peak detection. The ceiling defaults to -1 dBTP. Character: Clean, Punchy (fast release) or Smooth (slow release). Release slows automatically under sustained limiting. |
| **Display** | The last few seconds of output, with limiter reduction in red from the top, ducking in blue and the short-term loudness line. The big number is integrated LUFS, coloured green on target, amber a bit hot, red too loud, blue quiet. |
| **Meters** | LUFS column (short-term fill, momentary tick, target notch; measured to ITU-R BS.1770 / EBU R128) and GR segments showing how hard the limiter works: green under 3 dB, amber 3 to 6, red over 6. |
| **Integrated / true peak** | Gated integrated LUFS and max true peak since the last Reset. |
| **Source clipping** | Watches the audio before any processing and counts flat-topped or over-0 dBFS clipping already in the recording. Click the badge to clear it. |
| **A/B Compare** | Plays the original, delayed and gain-matched to the processed loudness, so you hear the processing rather than the volume change. It's also the host bypass. |

Leveling sets how quickly the rider reacts: Gentle, Normal or Tight.

Latency is about 2.3 ms, reported to the host. The window scales from 60% to 200% (drag the corner).

## Tips for Premiere Pro

- Put a **Mic** instance on each voice track, and a **Master** instance on the **Mix** track, then export.
- Linked debleed relies on the host running all the instances together, which Premiere and Audition do on playback and export. If linking ever doesn't kick in, the header says "No other mics linked yet" and Gate is used instead.
- The rider learns while audio plays, so the first second or so after a cold start is where it locks on.
- Mono tracks are measured as dual-mono, which is how they play back on two speakers.

## Install

Every push to `main` builds fresh installers on the [latest release](../../releases/latest) page.

### Mac

1. Download **Butterfader.dmg**.
2. Open it and double-click **Install Butterfader.pkg**. The installer isn't notarised, so if macOS blocks it, right-click the .pkg and choose **Open**.
3. Rescan plug-ins:
   - **Premiere Pro:** Settings > Audio > Audio Plug-in Manager > Scan for Plug-ins
   - **Audition:** Effects > Audio Plug-in Manager > Scan for Plug-ins

Installs the VST3 and the Audio Unit. Works on Apple Silicon and Intel Macs, macOS 11 or later.

### Windows

1. Download **Butterfader-Setup.exe**.
2. Run it. The installer isn't code-signed yet, so Windows SmartScreen may say it protected your PC. Click **More info**, then **Run anyway**. Approve the admin prompt, because the plug-in goes into a system folder.
3. Rescan plug-ins:
   - **Premiere Pro:** Edit > Preferences > Audio > Audio Plug-in Manager > Scan for Plug-ins, then make sure **Butterfader** is ticked
   - **Audition:** Effects > Audio Plug-in Manager > Scan for Plug-ins
4. Find it under Audio Effects in Premiere Pro, or in the Effects Rack in Audition.

**Without the installer:** download **Butterfader-Windows-VST3.zip**, unzip it, and copy the `Butterfader.vst3` folder into `C:\Program Files\Common Files\VST3`. Then rescan as above.

**Uninstall:** Settings > Apps > Installed apps > **Butterfader VST3** > Uninstall, or delete `C:\Program Files\Common Files\VST3\Butterfader.vst3`.

Needs 64-bit Windows 10 or 11. Windows gets the VST3 only, because Audio Units are Mac-only.

## Build

Requires CMake, plus Xcode on Mac or Visual Studio 2022 on Windows. JUCE 8 is fetched automatically.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Butterfader_VST3 Butterfader_AU   # also copies into ~/Library/Audio/Plug-Ins
```

Checks:

```bash
cmake --build build --target DSPTest && ./build/DSPTest_artefacts/Release/DSPTest        # loudness, true peak, auto level, bypass match, clip detection, noise guard, debleed
cmake --build build --target UISnapshot && ./build/UISnapshot_artefacts/Release/UISnapshot /tmp   # renders the UI to PNGs
./scripts/make-dmg.sh                                                                    # universal installer DMG
```

### Signing and notarizing the macOS release

With these variables `make-dmg.sh` signs with a Developer ID and notarizes, so macOS opens
the release without a warning; without them it produces a working but ad-hoc signed build
that users must right-click > Open. Store the notary credentials once (an App Store Connect
API key avoids app-specific passwords):

```bash
xcrun notarytool store-credentials butterfader-notary \
  --key ~/Downloads/AuthKey_KEYID.p8 --key-id KEYID --issuer ISSUER-UUID
```

Then build:

```bash
APP_SIGN_ID="Developer ID Application: Your Name (TEAMID)" \
INSTALLER_SIGN_ID="Developer ID Installer: Your Name (TEAMID)" \
NOTARY_PROFILE=butterfader-notary \
./scripts/make-dmg.sh
```

The plug-ins, the .pkg and the DMG are signed with the hardened runtime and a secure
timestamp; Apple's notary service takes a few minutes per file, and the script waits and
staples the tickets so everything validates offline. Check a build with
`spctl -a -vvv -t install dist/Butterfader-<version>.dmg` (expect `source=Notarized Developer ID`).

The release workflow does the same when the repository has `MACOS_CERT_P12`,
`MACOS_CERT_PASSWORD`, `NOTARY_KEY`, `NOTARY_KEY_ID` and `NOTARY_ISSUER` secrets.

Source layout: `Source/DSP/Loudness.h` (BS.1770 meter), `Source/DSP/TruePeakLimiter.h`, `Source/DSP/Rider.h`, `Source/DSP/MicLink.h` (debleed link + noise floor), `Source/Platforms.h` (targets), `Source/UI/`.

## License

Butterfader is free software under the [GNU Affero General Public License v3.0](LICENSE). It's built with [JUCE](https://juce.com), used under JUCE's AGPLv3 option.
