# ToTape Studio

A native UI for **Airwindows ToTape7, ToTape8, and ToTape9**, following the cream, charcoal, and brass
style of [ButterComp Studio](https://github.com/johandoornenbal/codex-tryout).
Built with JUCE for **VST3 + standalone on Linux, Windows, and macOS**, and
**Audio Unit on macOS**. This is an independent adaptation, not an official
Airwindows release.

![Native plugin editor](docs/ui-preview.png)

Choose **ToTape7**, **ToTape8**, or **ToTape9** from the selector at the top.
The controls update to the original parameters of that version. Each version has
its own automatable parameter bank, retained when switching and saved with the
selected version in the host session. Double-click a knob to reset it, type a
value below it, or use Reset to Default to reset only the selected version.
All original parameters default to 50%; head frequency is displayed as 68.8 Hz
(the underlying default is 68.75 Hz).

ToTape9 remains the default. Existing ToTape9 sessions and automation retain their
original parameter IDs/order; older session states without a version selector
load as ToTape9. The plugin identity is unchanged.

Version selection takes effect at the next audio block. The entering engine is
reset to its original initial state, with its retained parameter values applied.
No crossfade, smoothing, normalization, or extra effect is inserted into the DSP.
Switch at a quiet point or with playback stopped to avoid an audible discontinuity.

**ToTape8 and ToTape9 controls:**

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
| Output | 0–100% | Original output gain before the clipping stage; 50% = unity |

**ToTape7 controls:** Encode amount, Encode frequency, Tape drive, Flutter,
Flutter speed, Bias, Head bump, Head frequency, Decode amount, and Decode
frequency. All except Head frequency use normalized 0–100% displays, preserving
the original mappings. Encode/decode frequency are normalized filter controls,
not Hz readouts. Tape drive at 50% is unity. ToTape7 has no separate Output knob.

![ToTape7 editor](docs/ui-preview-7.png)

[ToTape8 editor preview](docs/ui-preview-8.png)

Stereo input/output; 32- and 64-bit floating-point processing. The output meters
show stereo sample peaks in dBFS after processing, with visual decay and a −60 dBFS
floor. They are not VU, loudness, or true-peak meters. Reel motion is decorative
and follows signal activity, not simulated physical tape speed. The original output stages are retained:
ClipOnly2 for ToTape7/8, ClipOnly3 for ToTape9. One sample of small-signal latency is reported
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

The three DSP suites cover parameter extremes, rates from 44.1 to 192 kHz, variable
block lengths, deterministic reset, and impulse latency. The three reference
suites compile the untouched upstream processing source behind a small test-only
VST2 interface and compare float/double processing including silence and parameter
changes. Both implementations use the same deterministic dither seeds.
The JUCE wrapper check verifies all three banks and their selector/slider bindings,
independent reset, state recall, legacy ToTape9 migration, float/double routing,
host latency, and rendering each native editor. It writes `ui-preview.png`,
`ui-preview-7.png`, and `ui-preview-8.png` in the build directory.

CI is configured for Linux, Windows, and macOS, with build artifacts uploaded for
each platform. Creating the workflow does not run it; native macOS/Windows builds
and DAW playback/automation validation remain necessary before release.

## Licenses

Airwindows ToTape7/8/9 © Chris Johnson / Airwindows, MIT. Wrapper/UI: MIT.
JUCE is separately licensed under AGPLv3 or commercial terms; the wrapper's MIT
license does not relicense JUCE. See [THIRD_PARTY.md](THIRD_PARTY.md).

## Verified in this cloud instance

Linux VST3 and standalone builds passed. All seven CTest suites passed: 63 DSP
scenarios, 144 upstream comparisons with maximum absolute error 0, and the JUCE
wrapper/editor checks for all three versions. The standalone window opened on a virtual X display.
No physical audio device is available here; audio checks render test buffers.
The screenshot above comes from the actual editor. macOS AU/VST3 and Windows
builds, signing, and DAW playback have not been executed in this Linux instance.
