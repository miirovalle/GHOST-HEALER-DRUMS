# Building GHOST HEALER DRUMS

Targets: **AUv3 + Standalone** on iOS/iPadOS, **VST3 + Standalone** on Windows/macOS.

JUCE 9.0.5 ships in `ThirdParty/JUCE`, so no network access is required.

---

## 1. Prerequisites

| Platform | Tool |
|---|---|
| All | CMake **3.22+** |
| Windows | Visual Studio 2022 (Desktop C++ workload) |
| iOS / iPadOS | macOS 13+, Xcode **15+**, iOS SDK 17+ |
| iPad device | iPadOS 17.0+ (tested target: iPad Pro 10.5", A10X, 4 GB) |

---

## 2. Windows — VST3 + Standalone

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Outputs:

```
build/GhostDrums_artefacts/Release/VST3/GHOST HEALER DRUMS.vst3
build/GhostDrums_artefacts/Release/Standalone/GHOST HEALER DRUMS.exe
build/GhostDrumsMidi_artefacts/Release/VST3/GHOST HEALER DRUMS MIDI.vst3
build/GhostDrumsMidi_artefacts/Release/Standalone/GHOST HEALER DRUMS MIDI.exe
```

The second target, **GHOST HEALER DRUMS MIDI**, is the MIDI/CV routing hub:
an audio pass-through effect with MIDI in/out/thru, channel and note-range
filtering, transposition, velocity shaping and CV conversion. It is a separate
plugin and does not embed the sample library.

`COPY_PLUGIN_AFTER_BUILD` is `FALSE`, so the VST3 is not auto-installed.
To install, copy the bundle to `C:\Program Files\Common Files\VST3\`.

Prebuilt artifacts from the optimisation pass are checked in under `dist/`:

```
dist/GHOST HEALER DRUMS.vst3     (252 MB - includes the embedded sample library)
dist/GHOST HEALER DRUMS.exe      (Standalone)
```

The built VST3 reads its sample library from
`GHOST HEALER DRUMS.vst3\Contents\Resources\GHLibrary\`. The post-build step
copies `Content/` there automatically.

---

## 3. iOS / iPadOS — AUv3 + Standalone container

The iOS build **must** use the Xcode generator; CMake aborts otherwise.

```bash
cmake -S . -B build-ios -G Xcode \
      -DCMAKE_SYSTEM_NAME=iOS \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 \
      -DCMAKE_OSX_ARCHITECTURES=arm64 \
      -DGH_BUILD_TESTS=OFF \
      -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=<YOUR_TEAM_ID>

cmake --build build-ios --config Release \
      --target GhostDrums_AUv3 GhostDrums_Standalone \
               GhostDrumsMidi_AUv3 GhostDrumsMidi_Standalone
```

Two products are produced:

| Target | Product | Purpose |
|---|---|---|
| `GhostDrums_AUv3` | `GHOST HEALER DRUMS.appex` | The audio unit itself |
| `GhostDrums_Standalone` | `GHOST HEALER DRUMS.app` | Container app that hosts the AUv3 |
| `GhostDrumsMidi_AUv3` | `GHOST HEALER DRUMS MIDI.appex` | MIDI/CV routing hub audio unit |
| `GhostDrumsMidi_Standalone` | `GHOST HEALER DRUMS MIDI.app` | Routing hub container app |

Signing is automatic (`CODE_SIGN_STYLE = Automatic`). Designated requirement:
set `CMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM` to your Apple team ID.

### Deploying to the iPad

1. Connect the iPad, trust it, and select it as the run destination in Xcode.
2. Build the **Standalone** target and run it once — this registers the AUv3 with
   the system.
3. Open AUM, Cubasis, GarageBand, or Auria and add **GHOST HEALER DRUMS** from the
   Audio Unit / Inter-App Audio list.

### iPad-specific build settings (applied automatically)

Set by `GH_OPTIMISE_FOR_IPAD` (default `ON`) in `CMakeLists.txt`:

| Setting | Value | Why |
|---|---|---|
| `IPHONEOS_DEPLOYMENT_TARGET` | `17.0` | Required iPadOS floor |
| `ARCHS` | `arm64` | A10X Fusion is arm64; no 32-bit slice |
| `ONLY_ACTIVE_ARCH` | `NO` | One binary for all arm64 iPads |
| `ENABLE_BITCODE` | `NO` | Deprecated, adds size |
| `DEAD_CODE_STRIPPING` | `YES` | Smaller extension binary |
| `TARGETED_DEVICE_FAMILY` | `1,2` | iPhone + iPad |
| LTO | on | `juce_recommended_lto_flags` |

The sample library is embedded as **extension resources** (`Resources/GHLibrary/`),
mapped read-only from the app bundle.

---

## 4. Tests

```powershell
cmake --build build --config Release --target GhostTests

# Full correctness suite
build/GhostTests_artefacts/Release/GhostTests.exe Content

# Memory + CPU profile
build/GhostTests_artefacts/Release/GhostTests.exe Content --profile

# DSP bit-exactness guard
build/GhostTests_artefacts/Release/GhostTests.exe x --check-dsp-reference Tests/dsp_reference.bin
```

`Tests/dsp_reference.bin` is the render captured from the pre-optimisation 1.0.8
binary. Any change that alters the audio output will fail this check.

`GhostTests` exits non-zero on the first failed assertion, so it can be wired
straight into CI.

---

## 5. Sample storage

Samples are decoded **once at load time into interleaved 16-bit PCM** and the source WAV
bytes are released. 16-bit sources are stored losslessly; 24-bit and 32-bit sources are
quantised to 16-bit (measured delta: −89 dBFS). This keeps a full 48-pad kit at
**10.76 MiB** instead of 18.56 MiB.

When a kit is saved or exported, `gh::encodeWav()` writes a canonical 16-bit PCM WAV, so
saved kits remain standard, loadable WAV files.

---

## 6. Repacking the sample library

`Content/` is generated by `GHContentPack` from a folder tree of numbered WAVs:

```powershell
build/GHContentPack_artefacts/Release/GHContentPack.exe <kits-root> <midi-root> <out-dir>
```

Not required for a normal build — the packed library is committed.

---

## 7. Troubleshooting

| Symptom | Fix |
|---|---|
| `Use Xcode for the AUv3/container app` | The iOS configure ran with a non-Xcode generator; add `-G Xcode` |
| AUv3 does not appear on the iPad | Run the Standalone container once, then reopen the host app |
| "High memory kit" in the status bar | A kit exceeded the recommended budget; see TEST-REPORT.md |
| No sound, status shows `0.0 MiB kit` | `Resources/GHLibrary/catalog.json` is missing from the bundle |

---

## 8. GitHub Actions — iOS AUv3 + IPA

[`.github/workflows/ios-auv3.yml`](.github/workflows/ios-auv3.yml) builds the AUv3
extension and packages a flashable `.ipa` on a `macos-15` runner.

Triggers:

- **Actions → iOS AUv3 + IPA → Run workflow** (inputs: deployment target, signed build)
- pushing a `v*` tag — the `.ipa` and `.appex.zip` are also attached to that GitHub release

Artifacts per run:

```
GHOST-HEALER-DRUMS-<version>-ios-unsigned.ipa              # Payload/GHOST HEALER DRUMS.app
GHOST-HEALER-DRUMS-<version>-AUv3-unsigned.appex.zip
GHOST-HEALER-DRUMS-MIDI-<version>-ios-unsigned.ipa         # Payload/GHOST HEALER DRUMS MIDI.app
GHOST-HEALER-DRUMS-MIDI-<version>-AUv3-unsigned.appex.zip
```

### The runner needs the sample library

`Content/` is gitignored, so the workflow resolves it in this order:

1. `Content/catalog.json` present in the checkout — use it as-is.
2. Repository variable `GH_CONTENT_URL` set — download that tarball and unpack it into `Content/`
   (`tar` is expected to contain a single top-level folder, e.g. `Content/`).
3. Otherwise the job fails with an explicit error rather than shipping a silent, sample-less plugin.

To commit the library instead, remove `Content/` from `.gitignore` (~245 MB, ~3 600 files).

### Unsigned builds (default)

No secrets required. `CODE_SIGNING_ALLOWED=NO` produces an unsigned `.ipa` that can be
re-signed and sideloaded with AltStore, Sideloadly or TrollStore. After installing, run the
container app once so iOS registers the AUv3, then open it from AUM, Cubasis, GarageBand, etc.

### Signed builds

Set `signed: true` when running the workflow; it fails fast if any of these repository secrets
are missing:

| Secret | Value |
|---|---|
| `APPLE_TEAM_ID` | 10-character Apple Developer team ID |
| `APPLE_CERT_P12_BASE64` | base64 of an `Apple Distribution` certificate exported as `.p12` |
| `APPLE_CERT_PASSWORD` | password of that `.p12` |
| `APPLE_API_KEY_P8_BASE64` | base64 of an App Store Connect API key (`.p8`, *Developer* role) |
| `APPLE_API_KEY_ID` | the key's ID |
| `APPLE_API_ISSUER_ID` | the key's issuer UUID |

Signing is automatic (`-allowProvisioningUpdates`), so Xcode provisions both bundle
identifiers (`audio.mk.ghostdrums` and the `.AUv3` extension) itself.

```bash
base64 -i GhostDrums.p12 -o p12.txt      # macOS
base64 -i AuthKey_XXXX.p8 -o p8.txt
```
