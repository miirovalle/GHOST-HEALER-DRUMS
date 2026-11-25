# GHOST HEALER DRUMS

Original lightweight 48-pad drum sampler and 12-lane sequencer. Windows x64
VST3/standalone and iOS AUv3/container-app source. Not a modified or embedded
Battery or Little Drum Machine binary. The Little Drum Machine manual was used
as a functional reference, not as copied code or artwork.

## Play

- 54 Ghost Healer kits; load only the active kit. One-shots ignore note-off.
- FL labels C3â€“B6 mean **numeric MIDI 36â€“83**. Each octave repeats Kick, Kick 2,
  Snare, Clap, Snare 2, Percussion, Extra percussion, Closed hat, Tom, Shaker,
  Open hat, Crash/Ride. Other hosts may display different octave names.
- Select a pad to adjust Tune, Pan, Level, velocity-to-pitch, low cut, high cut
  and compressor. Velocity-to-pitch is bipolar around velocity 0.5; Tune also
  affects ringing voices. Low cut at 0 Hz and high cut at 20 kHz bypass their
  two-pole filters. Compressor at 0 and master saturation at 0 bypass effects.
- Master starts at -12 dB for headroom. A final peak clamp protects the output;
  reduce the master level if many simultaneous hits clip. No loudness matching
  or automatic normalization changes your samples.
- Kit/loop dropdowns, previous/next loop, random kit/loop, and Humanize.
  Both selectors occupy half their previous width. The MIDI selector now
  has a separate name/drag button and a 44-pixel dropdown arrow: the name
  does not share the ComboBox's mouse handlers. With a mouse, drag the name
  directly; on touchscreens hold it for 0.5 seconds until the border turns
  yellow, then move to start dragging. A short tap or the arrow opens the
  loop menu. Entries inside category submenus use the same drag control:
  drag any entry to export and select that loop, even if another loop was
  previously selected. The popup closes before the Windows native drag
  starts, using the persistent plugin window rather than the disappearing
  menu window. A short tap selects the entry normally.
  iPad uses a native UIDragInteraction
  on the name area; its long-press timing is controlled by iOS.
  Humanize affects generated/imported loops only (Â±3 ms, Â±7 velocity units),
  not live pad MIDI. Undo by switching Humanize off.
- Hold **numeric MIDI note 1** (FL C#0) to play the selected loop. Release stops
  generated hits and fades loop voices for 5 ms; live one-shots remain ringing.
  `Play loop` plays without a held note. **Listen / Stop listen**, beside the
  MIDI loop controls, auditions through the current kit without sending MIDI
  to the host or Koala and without changing the clipboard or saved routing
  switches. Selecting another MIDI loop stops the old Listen preview.
  Host tempo overrides Preview Tempo.
- Tune, Pan, Level, Velocity-to-pitch, Compressor, Master level and Master
  saturation use large yellow rotary knobs with editable value boxes and
  centred labels below. Wide views arrange Tune, Comp, Pan, Level,
  Velocity-to-pitch, Master saturation and Master level in one aligned row,
  with Tempo above and a compact vertical filter panel to the right.
  Narrow views wrap the knobs into evenly spaced rows and move the filter
  below. In standalone, Tempo has an editable BPM box and touch-sized
  minus/plus buttons. In a plugin, those controls are disabled and show
  **HOST**: set the tempo in your DAW instead.
  Drag knobs vertically or horizontally to adjust.
  Low/high cut share one bar: the left handle controls low cut (0â€“2000 Hz,
  0 = off), and the right handle controls high cut (100â€“20000 Hz).
  On the horizontal portrait bar these are the upper/lower handles instead.
  Each retains its existing independent range and host automation parameter;
  selecting another pad rebinds both. Up/down selects a handle and left/right
  adjusts it. Existing crossed filter values remain supported.
- Drum families have consistent colours across all four kit octaves, pad
  outlines/fills and sequencer lane labels/variant buttons: kicks orange,
  snares rose, claps yellow, percussion purple, closed hats cyan, toms blue,
  shaker lime, open hats green and crash/ride pink. Kick 2, Snare 2 and Extra
  percussion share their respective family colour. Step beat-group colours
  remain separate from family colours.
- `Open sequencer` opens the 12-lane grid in its own resizable Windows
  window. On iPad it opens a separate modal panel inside the AUv3 host view.
  Closing/reopening preserves edits; closing the plugin editor closes its window.
  Sixteen independent scenes,
  four per-lane kit variants (the four octaves), rate, swing, per-step velocity,
  Random, Mutate, clear/copy/paste and left/right shift. Each step is a vertical
  velocity bar, not a numeric button. A single tap selects and toggles a locked
  step on/off, remembering its previous velocity during the current scene view
  (new steps start at 96). Double-tap unlocks velocity editing without clearing
  the step; subsequent vertical drags adjust 0â€“127 without jumping on press.
  A white grip marks the unlocked bar. Selecting another step, changing scenes
  or hiding the step locks it again. The bottom is off.
  The 1â€“16/32 column headers
  show positions, and the 1â€“4 lane buttons still select sample variants.
  Blue/green colours alternate in groups of four steps, matching beat groups.
  Choose **16 steps** or **32 steps** beside the scene/rate selectors; this
  length applies to all scenes. At 1/16 rate, 32 steps span two 4/4 bars.
  New extra steps start off. Switching to 16 hides/silences steps 17â€“32
  without deleting them; switching back restores them. Resize the window
  or scroll horizontally (swipe the column headers on touchscreens) to
  reach the entire 32-step grid without shrinking the velocity bars.
  Shift, Random/Mutate and Clear operate on the active length; Copy/Paste
  and saved host state preserve all 32 stored steps. Older 16-step states
  load normally, with their new extra steps off.
  **Play** in the sequencer selects/plays the edited grid after it is ready.
  **Stop** ends preview and held-note loop playback, with a short fade; it
  does not cut unrelated live pad one-shots. **Random kit** is available in
  both the main sampler and the sequencer window.
  Editing a grid or choosing `Use grid` selects grid playback.
  Imported loops remain separate from the editable grid.
- Buttons/pads are at least 44 pixels high. Narrow/portrait host views scroll
  vertically; the step grid scrolls horizontally. AUv3 hosts control the outer
  editor size. This release does not implement an arranger, polyrhythm,
  per-step sample-variant mode or proprietary LDM hosting.

## MIDI library and export

2,704 valid loops are bundled in categorized menus. GM/FPC drum roles were
remapped to your 48-pad layout **only in the bundled copies**. For example:
GM kick 36 â†’ 36, closed hat 42 â†’ 43, open hat 46 â†’ 46, crash 49 â†’ 47,
ride 51 â†’ 59, low floor tom 41 â†’ 80. Percussion/toms/cymbals use the four
available variants; the original MIDI library remains unchanged.

14 unreadable or SMPTE-timed files were excluded; their names/reasons are
recorded in `Content/catalog.json`. Unsupported notes are reported when
loading a loop instead of pretending an empty loop loaded.

The loop menu offers **Import GM/FPC MIDI** and **Import mapped MIDI**.
Desktop MIDI drops default to GM/FPC mapping. Ghost exports carry a standard
text marker so they are not remapped twice. Imports use SMF PPQ timing and the
explicit end-of-track duration; files without a positive end time round up
to a bar. Embedded tempo changes do not override the host/preview tempo.

**Copy to MIDI clipboard**, directly beside **Random loop**, copies the
selected loop as binary Standard MIDI File data. Explicit MIDI menu selections
and completed menu imports automatically copy after loading; a visible notice
explains this. Reopening/resizing the editor does not copy, and choosing another
source cancels a pending copy. Failed loads cannot copy/export an older loop;
clipboard errors are shown in the status line.

In **FL Studio**, select a new empty Pattern and open GHOST HEALER DRUMS'
**Piano roll > File > Paste from MIDI clipboard** (**Shift+Ctrl+V**).
**Plain Ctrl+V is not sufficient**; it uses FL's internal note clipboard.
Accept the MIDI import dialog if shown, then draw that Pattern in the Playlist.
Copying does not itself create a Playlist clip.
See the [official Piano roll menu](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/pianoroll_menu.htm).

Windows publishes raw SMF bytes using the registered **Standard MIDI File**
clipboard format; encoding, native publication and regression tests passed.
Actual FL Studio paste remains unverified. iPad source publishes
**public.midi-audio**; receiving apps
must explicitly support MIDI paste. Koala clipboard acceptance and iOS
compilation remain unverified; this is not a promise of direct Piano roll paste.

Windows: **Drag MIDI** snapshots the selected loop on mouse-down so file
generation does not delay the drag gesture. The Windows shell performs the
native file drag on the source UI thread; cancellation and rejected targets
are handled without claiming a successful import. For version 1.0.6, the user
confirmed successful FL Studio note import by dragging both the main-panel
loop name and a loop entry inside the open menu. **Save MIDI file** saves/reveals
the `.mid` without starting a drag. The host may show an import dialog or
require a selected instrument/pattern; the plugin cannot force every host to
create a playlist clip. In **FL Studio**, the supported drop targets are the
instrument's **Piano roll** or **Channel Rack**, not a raw MIDI clip on the
Playlist. Select a new empty Pattern to avoid replacing existing notes, drop
onto GHOST HEALER DRUMS' channel/Piano roll, accept the MIDI import dialog if
shown, then place that Pattern in the Playlist with the Draw tool.
`Save MIDI file` plus Piano roll > File > Import MIDI file is an alternative
for hosts that reject a plugin-originated native drag.
See the [official FL MIDI import instructions](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/automation_midiimport.htm).
MIDI output/routing is separate from file import and does not automatically
insert a Playlist Pattern. **Live MIDI out** enables generated note output,
default off. The channel (1-16, default 10), the pad-to-note table and the
velocity response are set in the **MIDI / CV** panel. It outputs mapped
notes, the played velocity and sample-timed note-offs; it does not echo pad
input or loop-trigger note 1 unless **Pads > MIDI** or a MIDI THRU mode is
selected. Normal sampler audio remains enabled. Configure the host's MIDI
output routing and recording separately. Stop, gate release, output-disable,
pattern replacement and same-note retriggers release outstanding notes.
Host transport stop also stops MIDI-enabled preview; with output off,
the existing independent audio-preview behaviour remains unchanged.
There is a fixed 4096-event/block budget plus room for releasing all 48
notes; overflow releases notes and reports an error off the audio thread.
Internal output buffers and note timers are allocated before processing.
On host deactivation, host-routed note-offs can only be delivered if another
audio callback occurs; the host must also handle MIDI cleanup on unloading.
Drag controls ignore touch scrolling and reset after
drop/cancellation. Copy/export are disabled while loading or after a failed
selection, preventing an older loop from being exported.
Export is a standard single-track **SMF type 0** file with numeric
notes, velocity, tempo, time signature and end-of-track; it is not an API that
can forcibly edit every host's piano roll.

iPad source includes native MIDI drag and tap-to-share/save. Export files
remain in the plugin's private `MIDI exports` folder so a host can read them
after the gesture. Delete old exports when no longer needed.

### Koala on iPad

1. Use **standalone Koala**, not Koala loaded as an AUv3 inside another host.
2. Koala's **MIXER** purchase enables hosting AUv3 instruments on a pad:
   Samples browser â†’ More â†’ Audio Units â†’ GHOST HEALER DRUMS.
3. **SAMURAI** enables Koala's piano roll. Select the instrument pad and put
   it in **chromatic keyboard mode**, without pitch transposition.
4. In Koala's MIDI settings, turn **Use MIDI mapping** off, set **Keyboard
   Mode MIDI Channel** to **10**, and enable **MIDI velocity**. Keep the
   instrument pad selected. Look for **GHOST HEALER DRUMS Record** in Koala's
   MIDI devices/input list and enable that input if Koala offers a selector.
5. Select a new empty sequence and choose the loop's length (one bar for a
   16-step 1/16 grid, two bars for 32). Set Koala's count-in to **NONE** for
   this initial workflow. Open the plugin and enable
   **Record to Koala**, then press **Record** in Koala from stopped transport.
   The plugin sends one pass of the selected loop when the host transport
   starts. If your Koala version does not supply transport information to the
   AUv3, press the plugin's **Play loop** after arming Koala's recording.
6. Stop recording after the pass, turn **Record to Koala** off, then play the
   recorded sequence in Koala. The individual pitches/velocities are intended
   to appear as editable Piano roll notes, not as a resampled audio loop.

The iOS source creates a **CoreMIDI virtual source**, independently of
whether Koala records MIDI returned by its hosted AUv3. Record mode mutes
only the plugin's internally generated loop audio: notes returned from
Koala still play the sampler normally and are not re-emitted. It takes
precedence over **Live MIDI out**, avoiding two MIDI routes at once. Its
notes are released on Stop, disable, host stop and resource release.
Both MIDI modes default off and are stored in host state.

Match **numeric MIDI notes**, not FL octave labels. Koala's displayed C3
corresponds to MIDI 60. Playing only trigger note 1 records a trigger,
not the individual drum notes.

**Not yet verified on iPad:** this is an implementation and recording
workflow to test, not a claim of successful Koala recording or a signed IPA.
CoreMIDI/AUv3 compilation, virtual-source visibility, hosted-pad MIDI return,
host transport/count-in timing, registration and native drag/share all
require macOS/Xcode and an iPad. Windows tests verify the recording model,
not Apple's APIs or Koala's actual routing. Koala's manual documents incoming
MIDI recording, but does not promise capture of MIDI output from a hosted
instrument. If the virtual source is unavailable, an error is shown; no
automatic Piano roll insertion is claimed.
Official references: [Koala MIDI settings](https://manual.koalasampler.com/mobile/8-settings/#83-midi-settings),
[Koala sample/AUv3 hosting](https://manual.koalasampler.com/mobile/4-sample/),
[Koala sequencing](https://manual.koalasampler.com/mobile/5-sequence/),
[Little Drum Machine manual](https://www.snorkelaudio.com/little-drum-machine/manual/).

## MIDI IN, OUT, THRU and CV

The editor's **MIDI / CV** panel controls every external connection:

- **MIDI in** (on by default) lets Drambo MIDI racks, sequencers, controllers,
  external drum machines and other plugins play the pads. Numeric note 1 still
  gates the selected loop. **In channel** filters 1-16 or **All**;
  **Velocity** selects linear, soft or hard response. Only notes present in the
  pad note table trigger pads.
- **Selected pad note** edits the note of the pad chosen in the pad grid, so
  input and output share one configurable 48-entry table (default `36`-`83`).
  Host drum maps follow the same table.
- **Live MIDI out** sends loop notes and, with **Pads > MIDI**, pad hits on the
  **Out channel**. Velocity is preserved and note-offs stay sample-accurate
  across block boundaries.
- **Thru mode** chooses the three routing modes: **Trigger only** (default,
  1.0.8 behaviour, input is not echoed), **Thru only** (input is forwarded
  untouched and does not play the kit) and **Trigger + thru** (both at once).
  Forwarded notes go to the host on their original channel and are released on
  stop, panic (CC 120/123), route change, preset load and plugin deactivation
  so no external synth is left with stuck notes. Live input is never echoed
  back into the plugin's own MIDI output, which is what prevents feedback loops
  inside the instrument.
- **Signal mode / CV** replaces the stereo audio output with control voltage
  for a DC-coupled interface: **left = 1 V/oct pitch** (MIDI note 60 = 0 V) and
  **right = gate 0-1 V** scaled by velocity. Gate length for live hits is set
  by **CV gate**; loop hits use the pattern's own note length. CV mode is not
  an analog connection by itself: route the plugin to a DC-coupled audio
  output (or a MIDI-CV interface that accepts audio-rate CV), and expect no
  audio while CV mode is active.

## GHOST HEALER DRUMS MIDI

The second plugin built from this source is a routing hub that can sit before,
after or beside the instrument:

| Control | Meaning |
|---|---|
| **MIDI IN** / **MIDI channel** | Accept all MIDI or one channel only |
| **MIDI OUT** / **MIDI channel** | Send filtered, transposed, channel-remapped notes |
| **MIDI THRU** | Forward accepted input untouched to external instruments |
| **Transpose**, **Note range** | Shift and limit the note stream |
| **Velocity scale** / **curve** | Trim and shape outgoing velocity |
| **Signal mode** | `MIDI` passes audio through; `CV` writes pitch/gate CV to the output |
| **CV IN** / threshold | Convert a DC-coupled pitch + gate input into MIDI notes |
| **Monitor** | Incoming/outgoing note counts, last note, velocity and channel |

Both MIDI OUT and MIDI THRU can run at once, so one controller can play the
drums and an external synth simultaneously. Identical thru and output events
are only sent once. Notes held by either path are released when the routing
changes or the plugin is deactivated. A MIDI plugin cannot detect loops in the
host's own routing graph, so keep the host's MIDI feedback monitoring enabled
when chaining it back into its own input.

Both plugins need a host that exposes MIDI input/output for the format in use;
AUv3 MIDI output support and CV routing depend on the host, and CV requires a
DC-coupled audio interface or a MIDI-CV converter.

## Import and save kits

Windows imports a folder with exactly 48 WAVs numbered `01`â€“`48`, a `.ghkit`,
a Battery 4 `.nbkt`, or a ZIP containing a portable Ghost kit. iPad imports a ZIP through the system
document picker. The source folder is never modified.

### Battery 4 sample-based import (1.0.8)

Click **Import kit** and select an individual `.nbkt` in your Battery library's
`Kits` folder, or drop that file onto the editor. Keep the original `Samples`
folder in place: an `.nbkt` alone does not contain its referenced WAVs.
The whole Battery factory-library folder is not a bulk-import selection.

- Preserve the saved visible cell order, not Ghost's FPC instrument-role order.
  Every bank is mapped sequentially to **MIDI 36â€“83**; original Battery MIDI
  assignments are not imported. Ghost's family colours/sequence lanes remain
  positional, so bundled FPC loops may not match a Battery kit's arrangement.
- Split grids larger than 48 cells into `Bank 1`, `Bank 2`, etc., keeping every
  visible cell. Select other banks from the drum-kit menu. Bank 1 is selected
  after import. Empty cells and final-bank padding contain tiny silent WAVs.
  Cells retained outside Battery's saved visible grid are omitted and reported.
- Use **the first serialized sample zone per cell**, not a velocity-dependent
  selection. Extra zones are omitted and counted. Original cell names appear
  in the selected-pad inspector and are retained in saved Ghost kits.
- This imports original WAV audio, **not Battery's playback engine**. Battery
  tune/pan/level, envelopes, effects, sample crops, loop modes, choke groups,
  velocity layers and round-robin behaviour are not recreated. Ghost's existing
  pad controls and one-shot playback apply. The sample-only notice stays visible
  with the imported kit and is also available in the status tooltip.
- Resolve exact relative paths within the kit folder, or within the parent
  library for the standard sibling `Kits`/`Samples` layout. No fuzzy filename
  substitutions, absolute-path relocation, protected/embedded sample extraction,
  or non-WAV decoding. Missing samples, unsafe paths and unsupported versions
  produce errors rather than silently replacing audio.

Imports create self-contained **personal user kits**, without modifying the
NI library. NI factory samples are not bundled in Ghost release archives.
Metadata/decompressed documents are bounded to 64 MiB; existing 128 MiB/sample
and 512 MiB/import audio limits apply. Banks load/save one at a time off the
audio thread. A failed multi-bank import reports how many banks completed;
those completed banks remain available in the menu.

The native C++ reader was locally checked against **143 Battery 4 factory kits
and 190 first-zone banks**, loading their selected WAV audio without exporting
library samples. Synthetic tests cover splitting, silent cells, first-zone
selection, Unicode names, portable saving, malformed headers/compression,
escaping paths and startup error visibility. Binary framing was cross-checked
against [NITools' structured Battery reader](https://github.com/joanroig/nitools/blob/main/src/formats/battery.py)
and [NI container framing](https://github.com/joanroig/nitools/blob/main/src/formats/ni_container.py).
No Python tool or new runtime dependency is shipped.
This is a Windows-tested import feature; iPad uses the saved Ghost kit ZIP,
and AUv3 compilation/device testing remains unverified.

### Portable Ghost kits

`Save kit` asks for a name and makes a **self-contained** user kit with 48 WAVs
and a version-1 JSON `.ghkit` containing relative sample references and pad
settings, plus optional pad names and an import notice. User kits are indexed at launch. `Replace WAV` copies a new sample
and creates an edited kit, preserving the old kit.

Windows storage: `%APPDATA%\MK Audio\Ghost Drums\User kits`. On iOS the same
relative library lives inside the extension's sandbox. ZIP a saved kit's
entire folder to transfer it to iPad; `.ghkit` alone does not contain audio.
Factory kits are immutable, deduplicated manifests plus shared samples.
Limits: PCM 8/16/24/32 or float32 WAV, mono/stereo, 8â€“384 kHz; 128 MiB per
sample, 512 MiB per active kit/import and 16,384 drum hits per loop. Other
formats, invalid paths, symbolic links and malformed data produce errors.

Only active-kit raw WAV bytes are kept in RAM, not the whole library or
expanded floating-point copies. Fixed 64-voice playback; decoding/interpolation
only for active voices. Version 1.0.7 uses sorted active voice/pad indices,
resets filter histories only when a pad stops, and reuses overlapping decoded
frames in a small fixed per-voice cache. Summation order, interpolation,
sample precision, voice stealing and all effects remain unchanged.
Loop-name drag controls are allocated only for attached menu entries and
released when those entries close. The 384 sequencer velocity controls are
allocated on first opening the sequencer, then retained until the editor closes
to preserve its editing state when reopening.
File I/O, kit loading and deletion run off the audio
callback. Kit changes fade old hits for 2 ms and queue incoming hits during
the transition; a finite queue overflow is reported. Peak memory temporarily
includes old/new kits, and an extra copy during import/save.

Bundled sample storage: **255,609,678 bytes**, 857 unique WAVs, versus
1,082,066,164 bytes for the original 2,592 kit files. This reduces disk storage,
not each active kit's sample count. Source WAV audio is not altered.

## Windows build

Requires CMake 3.22+, Visual Studio 2022 C++ build tools and JUCE 9.0.5.
The source archive includes the required JUCE modules/build helpers for
offline builds. A full JUCE checkout may instead be supplied:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 "-DJUCE_SOURCE_DIR=C:\path\to\JUCE"
cmake --build build --config Release --target GhostDrums_VST3 GhostDrums_Standalone GhostTests --parallel 3
ctest --test-dir build -C Release --output-on-failure
```

Copy the **whole** `GHOST HEALER DRUMS.vst3` directory to
`C:\Program Files\Common Files\VST3`. Do not copy just its inner DLL.
The standalone app finds the same library in its sibling `VST3` folder;
keep the delivered `Standalone` and `VST3` folders together.

To regenerate content without touching the original kits or MIDI:

```powershell
cmake --build build --config Release --target GHContentPack
.\build\GHContentPack_artefacts\Release\GHContentPack.exe "C:\path\to\sample kits" "C:\path\to\MIDI loops" "C:\path\to\new Content"
```

The packer rejects an existing destination. Set `GH_LIBRARY_DIR` to the new
folder when configuring; it defaults to this project's `Content`.

## iOS AUv3 and IPA build

**This is source, not a signed IPA.** Windows cannot run Xcode or verify an
iOS build. Jailbreaking does not supply missing Apple build tools or third-party
source/redistribution permission. No supplied proprietary IPA is modified.

On a Mac with Xcode, configure this project from its source package:

```sh
cmake -S . -B build-ios -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DGH_BUILD_TESTS=OFF
open build-ios/GhostDrums.xcodeproj
```

Select your development team, build/archive `GhostDrums_Standalone` for an
iPad, and export/sign the containing app with the embedded `GhostDrums_AUv3`
extension. JUCE embeds the extension; the content is bundled once inside it.
Both components support iPhone/iPad. The standalone discovers the embedded
extension's resources; check actual bundle paths and document-picker
permissions on-device. Native imports retain JUCE security-scoped URLs and
copy file data on the worker before using private paths.

## License and validation

Code: AGPLv3 (`LICENSE`), matching the earlier project licensing choice.
JUCE is used under its applicable AGPLv3/commercial terms. Samples and MIDI
retain their original rights; verify redistribution rights before publishing
the content. This package is not affiliated with Native Instruments or Snorkel.

Tests cover supported WAV encodings, malformed replacements, DSP/filter
reset, pan/tune/velocity pitch, compression/saturation, finite one-shots,
loop-boundary/restart timing, humanize, MIDI round-trips, every bundled
kit/loop, async loading, state restore, self-contained kit save/reindex and
responsive editor construction. Version 1.0.1 also tests single-track MIDI
export, latest-pattern selection, velocity-bar editing, independent sequencer
window close/reopen/resize, Random kit, Play/Stop and pending transport
cancellation while preserving live one-shots.
iOS builds/device testing and native Koala MIDI import remain outstanding.
Version 1.0.2 additionally tests four-step colour groups, 16/32-step editing,
late-step playback and MIDI export, hidden-step preservation, 32-step scene
operations, legacy 16-step state compatibility and 32-step host state restore.
It also verifies drum-family colours, rotary parameter binding and independent
dual-handle filter editing/rebinding without changing the original DSP ranges.
Version 1.0.3 tests tap/double-tap editing, large yellow knob rendering and
non-overlapping responsive layout, generated MIDI timing/duration/retrigger/
disable cleanup, one-pass recording synchronization, virtual-destination
handover, returned notes without feedback, audio preservation and bounded
output overflow. The CoreMIDI transport itself is not tested by Windows.

Controlled Windows Release measurements on the build machine, fixed GH Kit 01 - Umbra,
48 kHz, 256-frame blocks. Two fresh processes per version, with three warmed-up
CPU trials per process; averages of their medians are shown. Both versions use
the same corrected profiler and load the default kit only once during startup:

| Workload | 1.0.6 | 1.0.7 |
| --- | ---: | ---: |
| Idle, % of one CPU core | 0.00450% | 0.00422% |
| Eight simultaneous hits every 128 ms | 1.503% | 1.282% |
| 64 simultaneous hits every 128 ms | 2.751% | 2.566% |
| Process private-memory increase on initial editor creation | 6.54 MiB | 1.82 MiB |

This is approximately 15% less CPU in the eight-hit case and 4.7 MiB less
initial editor overhead. Audio/MIDI handling is included; the 64-hit case
exercises the existing voice limit/stealing. Active-kit sample storage remains
18.56 MiB for this kit: no sample-quality reduction or expanded sample cache.
The memory row is editor-creation overhead, not total process memory or peak
memory with the sequencer/menu open. Allocators and host caches can retain RAM.
A deterministic 100,000-frame stereo render covering overlapping/stealing voices,
pitch/pan/gain automation, filters, compressor, saturation, fades and kit reset
matched the original 1.0.6 render exactly (maximum absolute error 0).

Repeat the controlled benchmark from the project directory in PowerShell:

```powershell
& '.\build\GhostTests_artefacts\Release\GhostTests.exe' (Resolve-Path '.\Content').Path --profile
```

These are local benchmarks, not an iPad result or a measured comparison with
Battery; CPU and memory depend on the kit, host and machine. Pluginval 1.0.4
strictness 5 passed for Windows 1.0.7 and 1.0.8, including editor, processing, state,
automation and buses.
