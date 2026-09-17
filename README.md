# Butterfader

A silky vocal chain for podcasts, in one plug-in: vocal rider, bleed rejection, 6-band EQ, de-esser, plosive tamer, smooth compressor, lookahead limiter and a loudness meter. It aims to level voices without crushing them, and it's goofy to use.

## Download

Get **Butterfader.dmg** from the [latest release](../../releases/latest). Every push to `main` builds a fresh DMG automatically.

Open the DMG and double-click **Install Butterfader.pkg**. Everything is signed with a Developer ID and notarized, so nothing is blocked by Gatekeeper. The installer puts:

- VST3 in `/Library/Audio/Plug-Ins/VST3`
- AU in `/Library/Audio/Plug-Ins/Components`
- Standalone app in `/Applications`

Rescan plug-ins in your DAW and look for **Butterfader** by **Arrow**. Works on Apple Silicon and Intel, macOS 11+.

## Signal chain

```
input gain → Pop Stop (plosives) → Tone Kitchen (6-band EQ) → Sss Snake (de-esser)
          → Butter Rider (with Bleed Bouncer) → Silk (compressor) → output gain → Glass Ceiling (limiter) → loudness meter
```

| Section | What it does |
|---|---|
| **Pop Stop** | Catches sudden low-frequency bursts (P-pops, mic bumps) and briefly thins the lows. A steady deep voice is left alone. |
| **Tone Kitchen** | 6 bands: bell, shelves, 12/24 dB cuts, notch. It has a live pre/post spectrum. Drag dots to move a band, scroll to change the width (Q), double-click to add or toggle a band, right-click to change its shape, shift-drag to change frequency only. |
| **Sss Snake** | Split-band de-esser. It measures sibilance *relative to the voice*, so it behaves the same whether the talker is quiet or loud. |
| **Butter Rider** | Steers short-term loudness (LUFS) toward the Sweet Spot, within the Pump Up / Chill Out limits. It looks ahead 25 ms, so the fader moves before the word. It ignores anything below the Hush Floor (room tone, breaths) and quickly pulls down sudden yells. |
| **Bleed Bouncer** | Stops the rider from boosting the *other* person's voice in this mic. See below. |
| **Silk** | Wide-knee, low-ratio compressor with an opto-style release that slows down the longer it works. It rounds off loud words instead of squashing them. |
| **Glass Ceiling** | 5 ms lookahead limiter with a smooth gain curve (sample peak). |
| **Loudness** | BS.1770 momentary, short-term and gated integrated readouts. Integrated turns green between -17.5 and -14.5 LUFS. |

## Bleed rejection (the whole point)

Ordinary riders boost whatever gets louder, including your co-host's voice leaking into your mic. Butterfader decides many times a second whether the sound is **our talker**, **someone else**, or **silence**. It only rides our talker. When it isn't sure, it freezes the fader instead of guessing.

1. **Sidechain guard (most reliable).** Route the other hosts' mics (a bus works) into the plug-in's **Other Mics** sidechain. If this mic isn't at least *Hog Margin* dB louder than the others, the sound belongs to someone else.
2. **Voice lock (no routing needed).** Press **SNIFF MY VOICE** and play about 8 seconds of *only* this talker. Butterfader learns a fingerprint: pitch plus the shape of the vocal formants, with level and tilt removed. Afterwards, voices that don't match are ignored. *Nosiness* sets how strict the match is. The fingerprint is saved with the session.
3. **Tuck Bleed** (optional). Instead of just ignoring bleed, it gently turns it down.

The offline test (`Tests/DSPTest.cpp`) simulates a two-host show where host B bleeds into host A's mic at -16 dB. A rider without protection boosted the bleed by **+8.5 dB**. With the sidechain guard or voice lock, the boost was **0 dB**. The quiet host was still lifted and the loud host still pulled down.

Voice lock separates talkers with clearly different voices well. Two similar voices (for example, two hosts with close pitch) are harder to tell apart, so use the sidechain guard for those.

## Building

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release          # first time only
cmake --build build --config Release               # builds + installs VST3/AU to ~/Library/Audio/Plug-Ins
cmake --build build --target DSPTest && ./build/DSPTest_artefacts/Release/DSPTest        # bleed / leveling / ceiling test
cmake --build build --target UISnapshot && ./build/UISnapshot_artefacts/Release/UISnapshot shots   # render UI to PNG
auval -v aufx BtFd Arrw                            # validate AU
./scripts/make-dmg.sh                              # universal DMG in dist/
```

Latency is 30 ms (25 ms rider lookahead + 5 ms limiter lookahead), reported to the host.

### Signing and notarizing the release

Releases are signed with a Developer ID and notarized, so macOS opens them without a warning.
Without these variables `make-dmg.sh` still produces a working but ad-hoc signed build that
users must right-click > Open. Store the notary credentials once (an App Store Connect API
key avoids app-specific passwords):

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

The plug-ins, the standalone app, the .pkg and the DMG are all signed with the hardened
runtime and a secure timestamp; Apple's notary service takes a few minutes per file, and the
script waits and staples the tickets so everything validates offline. Check a build with
`spctl -a -vvv -t install dist/Butterfader-<version>.dmg` (expect `source=Notarized Developer ID`).

The standalone app carries `packaging/standalone.entitlements` (microphone access) — the
plug-ins don't need it, because the host owns the input.
