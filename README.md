# ToTape Studio

A native UI for **Airwindows ToTape9**, following the cream, charcoal, and brass
style of [ButterComp Studio](https://github.com/johandoornenbal/codex-tryout).
Built with JUCE for **VST3 + standalone on Linux, Windows, and macOS**, and
**Audio Unit on macOS**. This is an independent adaptation, not an official
Airwindows release.

![Native plugin editor](docs/ui-preview.png)

Nine automatable controls preserve the original ToTape9 parameter mappings and
50% defaults. Double-click any knob to reset it, type a value below it, or use
Reset to Default to reset all nine. Host session recall stores all parameters.

| Control | Display | Meaning |
| --- | --- | --- |
| Input | 0–100% | Original squared input drive; 50% = unity |
| Tilt | 0–100% | Dubly encode/decode balance |
| Shape | 0–100% | Dubly encode/decode filter shape |
| Flutter | 0–100% | Flutter depth; 0% disables modulation |
| Flutter speed | 0–100% | Original flutter speed mapping |
| Bias | 0–100% | Under/overbias; 50% = neutral |
| Head bump | 0–100% | Low-frequency resonance amount |
| Head frequency | 25–200 Hz | Resonance frequency; default 68.75 Hz |
| Output | 0–100% | Original output gain before ClipOnly3; 50% = unity |

Stereo input/output; 32- and 64-bit floating-point processing. The output meters
show stereo sample peaks in dBFS after processing, with visual decay and a −60 dBFS
floor. They are not VU, loudness, or true-peak meters. Reel motion is decorative
and follows signal activity, not simulated physical tape speed. The upstream
ClipOnly3 output stage is retained. One sample of small-signal latency is reported
to the host; flutter additionally introduces its original variable delay.
Parameters are sampled once per audio block, as in the original plugin.

## Build

Requires CMake 3.22+, a C++17 compiler, and Git. The first configure fetches
JUCE 8.0.12 at a pinned commit. An existing copy can be supplied with
`-DJUCE_PATH=/absolute/path/to/JUCE`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 3
ctest --test-dir build -C Release --output-on-failure
```

Outputs are under `build/ToTapeStudio_artefacts/Release/`:

- `VST3/ToTape Studio.vst3`
- `Standalone/ToTape Studio` (`.app` on macOS, `.exe` on Windows)
- `AU/ToTape Studio.component` (macOS only)

**macOS:** Install Xcode Command Line Tools and CMake. For Apple Silicon, add
`-DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0` when configuring.
Use `"arm64;x86_64"` for a universal build with an appropriate SDK. Copy the VST3
to `~/Library/Audio/Plug-Ins/VST3/` or AU to `~/Library/Audio/Plug-Ins/Components/`.
Logic Pro uses AU. Local builds may need ad-hoc signing, for example:

```sh
codesign --force --deep --sign - "build/ToTapeStudio_artefacts/Release/AU/ToTape Studio.component"
```

Distribution requires appropriate signing/notarization and license compliance.

**Windows:** Use Visual Studio 2022 with Desktop development with C++, plus
CMake and Git. Run the build commands from a Developer PowerShell. Copy the VST3
bundle to `C:\Program Files\Common Files\VST3\` or a host-supported user location.

**Linux (Debian/Ubuntu):** Install `build-essential cmake git pkg-config
libasound2-dev libfreetype-dev libfontconfig-dev libx11-dev libxext-dev
libxinerama-dev libxrandr-dev libxcursor-dev libgl1-mesa-dev`. WebKit and curl
are not required. For this managed Debian cloud instance, use
`bash scripts/setup-cloud.sh`, then `source scripts/cloud-env.sh` in new shells.
The script installs dependencies in `/workspace`, preserving system and repository
source files. Copy the VST3 bundle to `~/.vst3/` to use it in a Linux host.

For the standalone app, select your audio device in its Audio/MIDI settings and
feed stereo input. Monitor at a suitable level to avoid feedback through speakers.

## Checks

```sh
# Engine checks without JUCE, GUI libraries, or an audio device:
cmake -S . -B build-dsp -DTAPE_DSP_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-dsp --parallel 3
ctest --test-dir build-dsp --output-on-failure
```

The DSP suite covers parameter extremes, rates from 44.1 to 192 kHz, variable
block lengths, deterministic reset, and impulse latency. The reference suite
compiles the untouched upstream processing source behind a small test-only VST2
interface and compares float/double processing including silence and parameter
changes. Both implementations use the same deterministic dither seeds.
The JUCE wrapper check verifies all nine slider bindings, defaults/reset, state
recall, float/double audio, host latency, and rendering the actual editor to PNG.
It writes `ui-preview.png` in the build directory.

CI is configured for Linux, Windows, and macOS, with build artifacts uploaded for
each platform. Creating the workflow does not run it; native macOS/Windows builds
and DAW playback/automation validation remain necessary before release.

## Licenses

Airwindows ToTape9 © Chris Johnson / Airwindows, MIT. Wrapper/UI: MIT.
JUCE is separately licensed under AGPLv3 or commercial terms; the wrapper's MIT
license does not relicense JUCE. See [THIRD_PARTY.md](THIRD_PARTY.md).

## Verified in this cloud instance

Linux VST3 and standalone builds passed. All three CTest suites passed: 21 DSP
scenarios, 48 upstream comparisons with maximum absolute error 0, and the JUCE
wrapper/editor checks. The standalone window opened on a virtual X display.
No physical audio device is available here; audio checks render test buffers.
The screenshot above comes from the actual editor. macOS AU/VST3 and Windows
builds, signing, and DAW playback have not been executed in this Linux instance.
