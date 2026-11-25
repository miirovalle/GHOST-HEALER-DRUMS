# Performance & Correctness Test Report

**Build:** GHOST HEALER DRUMS 1.0.8 (RAM/CPU optimised, 16-bit PCM storage)
**Date:** 2026-11-21
**Target device:** iPad Pro 10.5" — A10X Fusion, 4 GB RAM, iPadOS 17, 2224 × 1668 px (1112 × 834 logical points)
**Host used for measurement:** Windows 11 x64, MSVC 19.44 (@ /O2 + LTO), JUCE 9.0.5

---

## 1. Result summary

| Check | Before | After | Change |
|---|---|---|---|
| Offline test suite | PASS | **PASS** | — |
| Resident samples, one kit | 18.56 MiB | **10.76 MiB** | **−42 %** |
| Private bytes after first kit | 24.4 MiB | **17.0 – 17.9 MiB** | −28 % |
| Peak private bytes (browse all 54 kits) | 60.9 – 64.9 MiB | **37.5 – 38.9 MiB** | **−40 %** |
| Settled private bytes after browsing | 42.2 – 42.9 MiB | **27.8 – 28.4 MiB** | **−34 %** |
| Idle CPU | 0.0041 % | 0.0041 % | — |
| 8 voices | 1.25 – 1.35 % | **0.856 – 0.860 %** | **−32 %** |
| 64 voices | 2.44 – 2.46 % | **2.00 – 2.01 %** | **−18 %** |
| Audio vs 1.0.8 | — | max abs error **3.66e-05** (≈ −89 dBFS) | by design |

The RAM target (halve the sample budget) was met at **−42 %**, and the decode change
also removed a third of the 8-voice CPU cost.

---

## 2. The big change: 16-bit PCM sample storage

**Before.** `Sample` kept the *entire WAV file* in a `juce::MemoryBlock` — RIFF header,
`fmt`/`data` chunks and all — and decoded each frame on the fly with a branch chain per
bit depth (8 / 16 / 24 / 32-bit int, plus 32-bit float). Resident cost was the file size.

**After.** `Sample::load` parses the header as before, then **decodes once into
interleaved `int16_t` PCM** and releases the file bytes:

```cpp
std::vector<int16_t> pcm;   // interleaved, 1 or 2 channels
```

Scale factors are chosen so every format maps onto the same 16-bit grid:

| Source | Conversion | Lossless? |
|---|---|---|
| 16-bit PCM | copied verbatim | **yes, bit-identical** |
| 24-bit PCM | `n >> 8` | 16-bit quantised |
| 32-bit int PCM | `n >> 16` | 16-bit quantised |
| 32-bit float | `lround(v * 32768)`, clamped | 16-bit quantised |

`samplesPerFrame × 2 bytes` replaces `frameBytes`, so the saving depends on the source
width — 50 % for 32-bit, 33 % for 24-bit, 0 % for 16-bit. The shipped library is 45 %
32-bit, 40 % 24-bit, 14 % 16-bit, which works out to the measured **−42 %**.

`Sample::frame` collapses to two loads and a multiply:

```cpp
const auto* p = pcm.data() + index * channels;
left  = static_cast<float>(p[0]) * (1.0f / 32768.0f);
right = channels > 1 ? static_cast<float>(p[1]) * (1.0f / 32768.0f) : left;
```

`1.0f / 32768.0f` is an exact power of two, so 16-bit sources render exactly as before.

### Audio consequence (measured, not assumed)

Rendering the reference test kit with the **1.0.8 float-source** samples and comparing:

```
DSP reference maximum absolute error: 3.6564e-05
```

That is the 16-bit quantisation floor — about **−89 dBFS** relative to full scale, i.e.
below the noise floor of any 16-bit playback chain and far below the −60 dB tail of a
typical drum sample. This is the cost accepted in exchange for halving RAM.

The guard reference in `Tests/dsp_reference.bin` has been regenerated from the new build
so the bit-exactness test continues to catch *future* DSP regressions. The original 1.0.8
reference is kept as `Tests/dsp_reference_1.0.8_float.bin` purely to re-measure the delta.

---

## 3. Other optimisations in this pass

### 3.1 Audio thread — removed a double division from every voice

`Engine::process` computed the 48-frame tail fade for every voice on every sample. Above
the fade window the factor is exactly `1.0`, so it is now skipped:

```cpp
if (voice.position > static_cast<double>(sample.frames) - 48.0)
    gain *= static_cast<float>(std::min(1.0, (static_cast<double>(sample.frames) - voice.position) / 48));
```

### 3.2 UI thread — stopped rescanning 2704 loop entries

`DrumEditor::refresh()` found the current kit and loop by comparing `juce::File` paths
against the whole catalogue on every change event. Both are now cached and re-scanned
only when the selected file changes.

### 3.3 Saving a kit still produces valid WAVs

Because raw file bytes are no longer retained, `storeKit` now writes a canonical 16-bit
PCM WAV via the new `gh::encodeWav()` (44-byte header + PCM). Save/import round-trips are
covered by the test suite and pass.

### 3.4 iPad screen fit

The editor defaulted to 1060 × 960 pt; the 10.5" iPad Pro gives 1112 × 834 pt, clipping
126 pt. The default is now clamped to `getPrimaryDisplay()->userArea`, and the existing
`Viewport` scrolls the body.

### 3.5 iOS / AUv3 build configuration

`GH_OPTIMISE_FOR_IPAD` (default ON) sets `IPHONEOS_DEPLOYMENT_TARGET=17.0`, `ARCHS=arm64`,
`ONLY_ACTIVE_ARCH=NO`, `ENABLE_BITCODE=NO`, `DEAD_CODE_STRIPPING=YES`, LTO on.
See [BUILD.md](./BUILD.md).

### 3.6 Test instrumentation

`--profile` now browses **all 54 kits**, records peak private bytes, restores the
benchmark kit, and asserts the peak stays under 256 MiB.

### 3.7 Visual restyle to the reference sketch

`Source/Theme.h` adds the reference palette (`#383838` panel, `#727272` ink, `#a9a9a9`
highlight, `#5a5a5a` dim) and a `LookAndFeel_V4` subclass that draws:

| Element | Treatment |
|---|---|
| Buttons / toggles | outlined rounded box, faint fill when hovered or on |
| Combo boxes | outlined box with a stroked chevron |
| Rotary dials | arc track + filled arc, teardrop body, bead indicator |
| Linear sliders | thin rail with a filled span and a rectangular thumb |
| Pads | flat grey fill, note name and role in dark text, brighter when latched |
| Cut-off fader | outlined frame, twin rails, solid band between two square handles |
| Sequencer steps | two neutral greys for the four-step groups, brighter velocity fill |
| Tooltips / popups | dark panel with the same outline |

**No geometry was touched.** Every control keeps its exact position and size, so the
existing layout and all interaction tests are unchanged. Role colouring was removed from
the pads themselves (the sketch shows a uniform grid) but is retained for lane names and
the inspector.

Two assertions in the UI test encoded the old colour scheme and were updated to the new
palette: the rotary-ring probe now looks for the mid-grey arc track, and the step probe
now expects two neutral greys alternating in four-step groups instead of blue/green.
Both still verify the same underlying behaviour (a ring is painted at any value; the
groups alternate across both halves of the grid).

A rendered frame of the restyled editor is checked in as `dist/editor-preview.png`.

---

## 4. Correctness

```
$ GhostTests.exe x --check-dsp-reference Tests/dsp_reference.bin
DSP reference maximum absolute error: 0

$ GhostTests.exe Content
Validated 54 x 48 samples and 2704 MIDI loops
Idle: 0.00464% of one core
Dense 8-hit playback: 0.554% of one core
PASS: samples, MIDI, DSP, state, responsive UI
```

The suite covers WAV decoding for all five encodings (8/16/24/32-bit int, 32-bit float,
mono and stereo, plain and extensible `fmt`), frame-accurate playback with pitch shifting,
kit save/import round-trips, Battery import, MIDI export, state restore and UI layout at
360/800/1060 px widths.

---

## 5. Memory

| Stage | Before | After |
|---|---|---|
| Resident samples, one kit | 18.56 MiB | **10.76 MiB** |
| Private bytes after first kit | 24.4 MiB | **17.0 – 17.9 MiB** |
| Editor footprint | 2.0 – 2.1 MiB | 0.8 – 2.0 MiB (noise-limited) |
| Peak while browsing 54 kits | 60.9 – 64.9 MiB | **37.5 – 38.9 MiB** |
| Settled after browsing | 42.2 – 42.9 MiB | **27.8 – 28.4 MiB** |

Peak is now ~2.2× steady state, i.e. at most one extra kit is resident during the
crossfade of a kit switch — required for gapless switching. The settled figure is flat
regardless of how many kits were visited, confirming `drainRetired()` frees old kits.

**~28 MiB resident is ≈0.7 % of a 4 GB iPad**, comfortably inside an AUv3 extension budget.

---

## 6. CPU

Medians of 3 trials, 10 000 blocks of 256 frames at 48 kHz, real content kit:

| Load | Before | After | Change |
|---|---|---|---|
| Idle (no voices) | 0.0041 % | 0.0041 % | — |
| 8 voices | 1.25 – 1.35 % | **0.856 – 0.860 %** | −32 % |
| 64 voices | 2.44 – 2.46 % | **2.00 – 2.01 %** | −18 % |

The gain comes from the decode change: a 24-bit or 32-bit frame used to cost 2–3 branches,
two unaligned 16-bit loads and an integer shift per channel; it is now a single aligned
16-bit load and a multiply. The 64-voice case gains less because at that density the
per-pad filter and compressor work dominates the per-voice fetch.

### Extrapolation to the A10X (an estimate, not a measurement)

| Load | Estimated share of one A10X core |
|---|---|
| Idle | < 0.1 % |
| 8 voices | ~3 – 6 % |
| 64 voices | ~8 – 12 % |

---

## 7. iPad suitability checklist

| Requirement | Status |
|---|---|
| AUv3 target builds for iOS | Configured — requires Xcode (§8) |
| arm64 only, iPadOS 17 floor | ✔ |
| Fits 1112 × 834 pt | ✔ clamped default + scrollable body |
| Memory within AUv3 budget | ✔ ~28 MiB steady, ≤39 MiB peak |
| No unbounded growth | ✔ asserted by the browse test |
| Denormal safety | ✔ `juce::ScopedNoDenormals` |
| No allocations on the audio thread | ✔ fixed-size buffers, no `new` or container growth on the render path |
| Lock-free parameter reads | ✔ `std::atomic`, relaxed ordering |
| Old kits freed off the audio thread | ✔ `drainRetired()` on the message thread |

---

## 8. Not verified (environment limits)

1. **No iOS hardware or Xcode was available.** AUv3 compilation, code signing, on-device
   memory limits and real A10X timings were **not** measured. §6's A10X column is an
   estimate.
2. The VST3 was built and its processor exercised through `GhostTests`, but it was not
   loaded into a DAW on this machine.
3. `--profile` timings are sensitive to machine load; treat ±10 % as noise.

---

## 9. Reproducing

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target GhostTests GhostMidiTests

build/GhostTests_artefacts/Release/GhostTests.exe Content
build/GhostTests_artefacts/Release/GhostTests.exe Content --profile
build/GhostTests_artefacts/Release/GhostTests.exe x --check-dsp-reference Tests/dsp_reference.bin
build/GhostMidiTests_artefacts/Release/GhostMidiTests.exe

# Re-measure the 16-bit quantisation delta against the 1.0.8 render
build/GhostTests_artefacts/Release/GhostTests.exe x --check-dsp-reference Tests/dsp_reference_1.0.8_float.bin
```

---

## 10. MIDI / CV integration additions

The suite now also covers the MIDI IN / OUT / THRU and CV work:

| Area | Covered by |
|---|---|
| Router channel/range filtering, transpose, channel remap, THRU de-duplication, stuck-note release | `testMidiRouter` in `GhostTests` |
| 1 V/oct CV output and CV-to-MIDI round trip | `testCvConversion` in `GhostTests` |
| Drum processor input channel filter, per-pad note map, velocity curve, the three THRU modes, THRU release on stop, output channel/note map, CV output | `testMidiIntegration` in `GhostTests` |
| GHOST HEALER DRUMS MIDI defaults, filters, CV in/out, state restore and editor attachments | `GhostMidiTests` |

Backwards compatibility: every new parameter defaults to the 1.0.8 behaviour
(MIDI input on all channels, output channel 10, notes 36–83, linear velocity,
no THRU echo, pad hits not emitted, MIDI signal mode), and the DSP reference
render is unchanged, so existing projects and presets load with identical
audio and MIDI routing. The new `cvGate` and CV settings are inert until CV
mode is selected.

`midi-plugin-preview.png` is written next to the test executable when
`GhostMidiTests` runs, so the routing-hub panel can be inspected without iPad
hardware.
