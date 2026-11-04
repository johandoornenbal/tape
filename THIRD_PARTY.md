# Third-party provenance

## Airwindows ToTape7, ToTape8, and ToTape9

Source: https://github.com/airwindows/airwindows
Commit: `7fd5f5cae262a41e6dd7d9a83d60c46e84d3092a` (retrieved 2026-11-04).
Original `plugins/WinVST/ToTape9/ToTape9.h`, `ToTape9.cpp`, `ToTape9Proc.cpp`,
and repository MIT license are retained unchanged in `third_party/airwindows/`.
ToTape7 and ToTape8 header, wrapper, and processing sources from the same commit
are retained unchanged in its `ToTape7/` and `ToTape8/` subdirectories.

`Source/TapeEngine.*`, `TapeEngine7.*`, and `TapeEngine8.*` adapt the VST2 wrapper to a standalone C++ engine:
- The original float/double sample-processing bodies are retained.
- Initialization is exposed as `reset(sampleRate)`.
- Per-instance noise seeds are fixed nonzero unsigned values for repeatable tests,
  rather than using the original global `rand()` seeding. This changes random
  flutter/dither sequences relative to an arbitrary upstream instance.
- Parameter setters clamp to the normalized 0–1 range.
- VST2 metadata/chunks are replaced by JUCE parameter automation and XML state.

All three sample-processing bodies are retained verbatim apart from class/type
names in method signatures. ToTape9 engine files remain unchanged from v0.1.0.
Version selection routes to exactly one engine, with no added processing or
crossfade. Each entering engine is reset on version changes. Separate reference
tests compare each adapted engine to its untouched upstream processing source
using matched initial seeds.

No VST2 SDK is needed. `tests/shim/audioeffectx.h` is a minimal independently
written test interface for compiling the original source in reference tests.

## UI reference

The initial JUCE wrapper/build structure and rotary/analogue-meter drawing styles are adapted
from ButterComp Studio, https://github.com/johandoornenbal/codex-tryout,
commit `3d2daf0d2f350d73640bd03d3f9210cd1b09bbf6` (MIT).
Its license notice is retained in `third_party/ButterComp-LICENSE`.

## JUCE and VST3

JUCE 8.0.12: `29396c22c93392d6738e021b83196283d6e4d850`.
https://github.com/juce-framework/JUCE/blob/8.0.12/LICENSE.md

JUCE is dual-licensed under AGPLv3 and commercial terms. Distribution must comply
with the applicable JUCE license, including corresponding source obligations when
using AGPLv3, or use an appropriate commercial license. The MIT license for this
wrapper does not relicense JUCE. Retain the VST3 SDK license notices bundled with
JUCE. This is not an official Airwindows or JUCE product.
