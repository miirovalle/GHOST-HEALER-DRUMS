#include "Editor.h"
#include <chrono>
#include <iostream>
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

namespace
{
void require(bool ok, const juce::String& message)
{
    if (! ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
void set(DrumProcessor& p, const juce::String& id, float value)
{
    auto* param = p.state.getParameter(id); require(param != nullptr, "Parameter exists");
    param->setValueNotifyingHost(param->convertTo0to1(value));
}
gh::Kit testKit()
{
    gh::Kit kit;
    for (auto& sample : kit.samples)
    {
        sample.rate = 48000; sample.channels = 1;
        sample.frames = 48000;
        sample.pcm.resize(sample.frames);
        for (size_t i = 0; i < sample.frames; ++i)
        {
            // Quantise exactly as the loader does for float WAV input.
            const auto scaled = std::lround(0.2 * std::sin(static_cast<double>(i) * 0.05) * 32768.0);
            sample.pcm[i] = static_cast<int16_t>(juce::jlimit(-32768L, 32767L, scaled));
        }
    }
    return kit;
}
double rms(gh::Engine& engine, int length = 4096)
{
    double sum = 0;
    for (int i = 0; i < length; ++i)
    { float l = 0, r = 0; engine.process(l, r); require(std::isfinite(l) && std::isfinite(r), "Finite output"); sum += l * l; }
    return std::sqrt(sum / length);
}
void pump(int ms = 5) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }
void dspReference(const juce::File& file, bool write)
{
    auto kit = testKit();
    gh::Engine engine; engine.prepare(48000); engine.setKit(&kit);
    std::array<gh::PadValues, gh::pads> values {};
    for (size_t i = 0; i < values.size(); ++i)
    {
        auto& p = values[i];
        p.tune = static_cast<float>(static_cast<int>(i % 12) - 6);
        p.pan = static_cast<float>(static_cast<int>(i % 7) - 3) / 3;
        p.level = -6; p.velocityPitch = 4;
        p.lowCut = i % 3 == 0 ? 180.0f : 0;
        p.highCut = i % 4 == 0 ? 1800.0f : 20000;
        p.compressor = i % 5 == 0 ? 0.65f : 0;
    }
    engine.setValues(values, -12, 0.2f);
    for (int i = 0; i < 80; ++i) engine.trigger(i % gh::pads, 0.3f + static_cast<float>(i % 7) * 0.1f, i % 2 == 0);
    std::vector<float> audio(100000 * 2);
    for (int i = 0; i < 100000; ++i)
    {
        if (i < 50000 && i % 1024 == 0) engine.trigger((i / 1024 * 11) % gh::pads, 0.8f, true);
        if (i == 7000) engine.fadeVoices(true, 5);
        if (i == 23000)
        {
            for (auto& p : values) { p.tune += 2; p.pan = -p.pan; p.level -= 3; p.velocityPitch = -2; }
            engine.setValues(values, -6, 0.6f);
        }
        if (i == 42000) engine.fadeVoices(false, 2);
        if (i == 60000) { engine.setKit(&kit); engine.setValues(values, -18, 0); engine.trigger(47, 1); }
        engine.process(audio[static_cast<size_t>(i) * 2], audio[static_cast<size_t>(i) * 2 + 1]);
    }
    const auto size = audio.size() * sizeof(float);
    if (write)
    {
        require(! file.exists() && file.replaceWithData(audio.data(), size), "New DSP reference captured");
        std::cout << "Captured DSP reference: " << size << " bytes\n";
        return;
    }
    juce::MemoryBlock reference;
    require(file.loadFileAsData(reference) && reference.getSize() == size, "Existing DSP reference loads");
    const auto* expected = static_cast<const float*>(reference.getData());
    double maxError = 0;
    for (size_t i = 0; i < audio.size(); ++i)
    {
        require(std::isfinite(audio[i]), "Reference render remains finite");
        maxError = std::max(maxError, std::abs(static_cast<double>(audio[i]) - expected[i]));
    }
    std::cout << "DSP reference maximum absolute error: " << maxError << '\n';
    require(maxError <= 1.0e-7, "Optimized DSP preserves the original render");
}
#if JUCE_WINDOWS
size_t privateBytes()
{
    PROCESS_MEMORY_COUNTERS_EX counters {};
    require(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
        sizeof(counters)) != 0, "Process private-memory measurement succeeds");
    return counters.PrivateUsage;
}
#endif
void profile(const juce::File& library)
{
    gh::Catalogue catalogue;
    require(gh::scanCatalogue(library, catalogue).wasOk() && ! catalogue.kits.empty(), "Profile catalogue loads");
    DrumProcessor processor(library);
    for (int i = 0; i < 1000 && (processor.getView().memory == 0 || processor.getView().patternLoading); ++i) pump();
    require(processor.getView().memory > 0 && ! processor.getView().patternLoading
        && processor.getView().kitFile == catalogue.kits.front().file,
        "Fixed benchmark kit is ready");
    processor.setRateAndBufferSizeDetails(48000, 256); processor.prepareToPlay(48000, 256);
    juce::AudioBuffer<float> buffer(2, 256); juce::MidiBuffer midi;
    processor.processBlock(buffer, midi);
    std::cout << "Fixed kit: " << processor.getView().kitName << ", raw samples: "
        << processor.getView().memory / (1024.0 * 1024) << " MiB\n";
#if JUCE_WINDOWS
    const auto before = privateBytes();
    std::cout << "Private bytes after first kit: "
        << static_cast<double>(before) / (1024 * 1024) << " MiB\n";
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        pump(50);
        std::cout << "Editor private-memory increase: "
            << (static_cast<double>(privateBytes()) - static_cast<double>(before)) / (1024 * 1024) << " MiB\n";
    }
    // Browse the whole library: peak memory must not grow with the number of kits
    // visited, which is what makes the plugin safe on a 4 GB iPad.
    const auto afterEditor = privateBytes();
    size_t peak = afterEditor;
    for (const auto& entry : catalogue.kits)
    {
        processor.chooseKit(entry.file);
        for (int n = 0; n < 400; ++n)
        {
            pump(2);
            const auto view = processor.getView();
            if (! view.patternLoading && view.memory > 0 && view.kitFile == entry.file) break;
        }
        peak = std::max(peak, privateBytes());
    }
    for (int n = 0; n < 400; ++n) { pump(2); if (! processor.getView().patternLoading) break; }
    const auto settled = privateBytes();
    std::cout << "Peak private bytes over " << catalogue.kits.size() << " kits: "
        << static_cast<double>(peak) / (1024 * 1024) << " MiB (settled "
        << static_cast<double>(settled) / (1024 * 1024) << " MiB)\n";
    require(static_cast<double>(peak) / (1024 * 1024) < 256.0,
        "Browsing all kits keeps peak memory inside the iPad AUv3 budget");
    // Restore the fixed benchmark kit so timing stays comparable between builds.
    processor.chooseKit(catalogue.kits.front().file);
    for (int n = 0; n < 400; ++n)
    {
        pump(2);
        if (! processor.getView().patternLoading && processor.getView().kitFile == catalogue.kits.front().file) break;
    }
#endif
    constexpr int blocks = 10000;
    for (int notes : {0, 8, 64})
    {
        std::array<double, 3> trials {};
        for (auto& trial : trials)
        {
            processor.releaseResources(); processor.prepareToPlay(48000, 256); midi.clear();
            const auto render = [&](int b)
            {
                if (notes > 0 && b % 24 == 0)
                    for (int n = 0; n < notes; ++n)
                        midi.addEvent(juce::MidiMessage::noteOn(1, 36 + n % gh::pads, 0.8f), 0);
                processor.processBlock(buffer, midi);
            };
            for (int b = 0; b < 240; ++b) render(b);
            const auto start = std::chrono::steady_clock::now();
            for (int b = 0; b < blocks; ++b) render(b);
            trial = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
                / (blocks * 256.0 / 48000) * 100;
        }
        std::sort(trials.begin(), trials.end());
        std::cout << notes << "-hit playback median: " << trials[1] << "% of one core\n";
    }
}
void savePreview(const juce::Image& image, const juce::String& name)
{
    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile(name);
    auto stream = file.createOutputStream();
    require(stream != nullptr, "Preview output opens");
    stream->setPosition(0); stream->truncate();
    require(juce::PNGImageFormat().writeImageToStream(image, *stream)
        && stream->getStatus().wasOk(), "Preview output replaces previous image");
}
template <typename T>
T* findControl(juce::Component& parent, const juce::String& name)
{
    if (parent.getName() == name)
        if (auto* control = dynamic_cast<T*>(&parent)) return control;
    for (auto* child : parent.getChildren())
        if (auto* control = findControl<T>(*child, name)) return control;
    return nullptr;
}
void testPadControls(juce::AudioProcessorEditor& editor, DrumProcessor& processor,
                     juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    for (const auto* name : {"Tune (st)", "Pan", "Level (dB)", "Velocity > pitch (st)",
                            "Compressor", "Master (dB)", "Master saturation"})
    {
        auto* control = findControl<juce::Slider>(editor, name);
        require(control && control->getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag
            && control->getHeight() >= 148 && control->getWidth() >= 108, "Requested knob is larger and touch-sized");
    }
    auto* tuneKnob = findControl<juce::Slider>(editor, "Tune (st)");
    const auto initialTune = tuneKnob->getValue();
    tuneKnob->setValue(-5.25, juce::sendNotificationSync);
    require(std::abs(processor.state.getRawParameterValue(DrumProcessor::padId(0, 0))->load() + 5.25f) < 0.001f,
        "Rotary knob updates the existing pad parameter");
    tuneKnob->setValue(initialTune, juce::sendNotificationSync);
    auto* cutBar = findControl<juce::Component>(editor, "Low / High cut");
    require(cutBar && cutBar->getHeight() >= 80, "Low/high cuts share one touch-sized bar");
    const auto mouseEvent = [cutBar](juce::Point<float> position, juce::Point<float> start, bool moved)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position,
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
            cutBar, cutBar, now, start, now, 1, moved);
    };
    const juce::Point<float> lowStart {14, 20}, highStart {static_cast<float>(cutBar->getWidth() - 14), 36};
    cutBar->mouseDown(mouseEvent(lowStart, lowStart, false));
    cutBar->mouseDrag(mouseEvent({highStart.x, 20}, lowStart, true));
    cutBar->mouseUp(mouseEvent({highStart.x, 20}, lowStart, true));
    require(processor.state.getRawParameterValue(DrumProcessor::padId(0, 4))->load() == 2000
        && processor.state.getRawParameterValue(DrumProcessor::padId(0, 5))->load() == 20000,
        "Upper handle edits low cut within its original range without changing high cut");
    cutBar->mouseDown(mouseEvent(highStart, highStart, false));
    cutBar->mouseDrag(mouseEvent({14, 36}, highStart, true));
    cutBar->mouseUp(mouseEvent({14, 36}, highStart, true));
    require(processor.state.getRawParameterValue(DrumProcessor::padId(0, 4))->load() == 2000
        && processor.state.getRawParameterValue(DrumProcessor::padId(0, 5))->load() == 100,
        "Lower handle edits high cut independently, preserving crossed existing filter values");
    const auto pad1Low = processor.state.getRawParameterValue(DrumProcessor::padId(1, 4))->load();
    auto* otherPad = findControl<juce::Button>(editor, "C#3");
    auto* firstPad = findControl<juce::Button>(editor, "C3");
    require(otherPad && firstPad, "Pad selection controls exist");
    otherPad->triggerClick(); pump(20);
    cutBar->mouseDown(mouseEvent(lowStart, lowStart, false));
    cutBar->mouseDrag(mouseEvent({highStart.x, 20}, lowStart, true));
    cutBar->mouseUp(mouseEvent({highStart.x, 20}, lowStart, true));
    require(processor.state.getRawParameterValue(DrumProcessor::padId(1, 4))->load() == 2000
        && processor.state.getRawParameterValue(DrumProcessor::padId(0, 5))->load() == 100,
        "Shared cutoff bar rebinds to selected pad, leaving the previous pad unchanged");
    set(processor, DrumProcessor::padId(1, 4), pad1Low);
    firstPad->triggerClick(); pump(20);
    set(processor, DrumProcessor::padId(0, 4), 0);
    set(processor, DrumProcessor::padId(0, 5), 20000);
    processor.processBlock(buffer, midi);
}
int crossings(gh::Engine& engine)
{
    int count = 0; float before = 0, left = 0, right = 0;
    for (int i = 0; i < 8192; ++i)
    { engine.process(left, right); if (before <= 0 && left > 0) ++count; before = left; }
    return count;
}
struct CapturedMidi final : gh::MidiOutput
{
    bool isReady() const noexcept override { return true; }
    void send(const juce::MidiBuffer& source, double, std::optional<uint64_t>) noexcept override
    { midi.addEvents(source, 0, -1, 0); }
    juce::String takeError() override { return {}; }
    juce::MidiBuffer midi;
};
struct TestPlayHead final : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info; info.setBpm(120); info.setIsPlaying(playing); return info;
    }
    bool playing = false;
};
void testMidiClipboard(const gh::Catalogue& catalogue)
{
    juce::MemoryBlock copied;
    int copies = 0;
    bool fail = false;
    DrumProcessor p(catalogue.root, {}, [&](const juce::MemoryBlock& bytes)
    {
        if (fail) return juce::Result::fail("Test clipboard is busy.");
        copied = bytes; ++copies; return juce::Result::ok();
    });
    const auto ready = [&]
    {
        for (int i = 0; i < 1000 && (p.getView().memory == 0 || p.getView().patternLoading); ++i) pump();
        pump(30);
        require(p.getView().memory > 0 && p.getView().patternReady, "Clipboard selection ready");
    };
    ready();
    require(copies == 0, "Opening processor does not overwrite clipboard");
    std::vector<juce::File> dragged;
    juce::Component* lastDragSource = nullptr;
    juce::Component* detachOnDrag = nullptr;
    auto dragResult = juce::Result::ok();
    DrumEditor editorInstance(p, [&](const juce::File& file, juce::Component& source, std::function<void(juce::Result)> finish)
    {
        if (detachOnDrag) detachOnDrag->getParentComponent()->removeChildComponent(detachOnDrag);
        require(file.existsAsFile(), "Drag snapshot survives removal of its menu control");
        dragged.push_back(file); lastDragSource = &source; finish(dragResult);
    });
    auto* editor = &editorInstance;
    auto* menu = findControl<juce::ComboBox>(*editor, "MIDI loop");
    auto* copy = findControl<juce::TextButton>(*editor, "Copy to MIDI clipboard");
    auto* listen = findControl<juce::TextButton>(*editor, "Listen loop");
    auto* random = findControl<juce::TextButton>(*editor, "Random loop");
    auto* dragName = findControl<juce::TextButton>(*editor, "MIDI loop drag handle");
    juce::Component* nameEvents = dragName;
    require(menu && copy && listen && random && dragName, "Requested loop controls exist");
    require(dragName->getParentComponent() == menu->getParentComponent()
        && ! menu->isParentOf(dragName), "Name drag handle is a separate sibling, not part of the popup menu");
    auto* kitMenu = findControl<juce::ComboBox>(*editor, "Drum kit");
    auto* instructions = findControl<juce::Label>(*editor, "MIDI clipboard instructions");
    require(kitMenu && instructions, "Kit menu and full-width instructions exist");
    for (int width : {360, 800, 1060})
    {
        editor->setSize(width, 960);
        require(kitMenu->getWidth() == instructions->getWidth() / 2
            && dragName->getWidth() + menu->getWidth() == instructions->getWidth() / 2
            && menu->getWidth() == 44 && dragName->getRight() == menu->getX(),
            "Both menus are exactly 50 percent of their previous width");
    }
    menu->setSelectedId(1, juce::sendNotificationSync); ready();
    require(copies == 1, "Explicit menu selection automatically copies exactly once");
    std::unique_ptr<gh::Pattern> expected;
    int ignored = 0;
    require(gh::loadMidi(catalogue.loops[0].file, expected, ignored).wasOk(), "Expected selected MIDI loads");
    juce::MemoryOutputStream bytes;
    require(gh::writeMidi(*expected, bytes, 120).wasOk() && copied == bytes.getMemoryBlock(),
        "Clipboard contains exact mapped notes, velocities, timing and complete SMF data");
    juce::MemoryInputStream input(copied, false); juce::MidiFile parsed;
    require(parsed.readFrom(input) && parsed.getNumTracks() == 1 && parsed.getTimeFormat() == 960,
        "Clipboard parses as single-track PPQ-960 MIDI, without text or a length prefix");
    pump(60); editor->setSize(800, 960); pump(20);
    require(copies == 1, "Passive refresh and resizing do not recopy MIDI");
    require(copy->getY() == random->getY() && copy->getX() > random->getX(),
        "Copy sits directly beside Random loop in wrapped layout");
    copy->onClick();
    require(copies == 2, "Copy button writes the current loop");
    p.chooseLoop(catalogue.loops[0].file, false, true);
    p.chooseLoop(catalogue.loops[1].file, false, true); ready();
    require(copies == 3, "Superseded selection does not copy an older loop");
    expected.reset();
    require(gh::loadMidi(catalogue.loops[1].file, expected, ignored).wasOk(), "Latest expected loop loads");
    juce::MemoryOutputStream latest;
    require(gh::writeMidi(*expected, latest, 120).wasOk() && copied == latest.getMemoryBlock(),
        "Latest requested loop wins auto-copy");
    p.chooseLoop(catalogue.loops[0].file, false, true);
    p.setGrid(gh::Grid{}); ready();
    require(copies == 3, "Changing source cancels pending menu auto-copy");
    const auto previous = copied;
    const auto bad = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("gh-bad-clipboard-" + juce::Uuid().toString() + ".mid");
    require(bad.replaceWithText("not MIDI"), "Invalid MIDI fixture writes");
    p.chooseLoop(bad, false, true);
    for (int i = 0; i < 1000 && p.getView().patternLoading; ++i) pump();
    pump(30);
    require(! p.getView().patternReady && copies == 3 && copied == previous,
        "Failed selection leaves clipboard unchanged and rejects stale readiness");
    require(! p.copyMidiToClipboard() && p.exportMidi() == juce::File{},
        "Neither manual clipboard nor file export substitutes the previous loop after failure");
    require(bad.deleteFile(), "Invalid MIDI fixture cleaned");
    p.chooseLoop(juce::URL(bad), false, true);
    for (int i = 0; i < 1000 && p.getView().patternLoading; ++i) pump();
    pump(30);
    require(! p.getView().patternReady && p.getView().error && copies == 3,
        "Inaccessible URL import reports failure without copying an old loop");
    p.chooseLoop(catalogue.loops[0].file); ready();
    fail = true;
    require(! p.copyMidiToClipboard() && p.getView().error && copies == 3,
        "Clipboard failure is explicitly reported, not claimed as success");
    fail = false;
    gh::Grid grid; grid.scenes[0].velocity[0][0] = 100; p.setGrid(grid); ready();
    const auto event = [dragName](juce::Point<float> position, int duration, bool moved)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position,
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
            dragName, dragName, now, {20, 20}, now - juce::RelativeTime::milliseconds(duration), 1, moved);
    };
    nameEvents->mouseDown(event({20, 20}, 0, false));
    nameEvents->mouseUp(event({20, 20}, 40, false));
    require(menu->isPopupActive() && dragged.empty(), "Short tap opens the menu without exporting or starting a drag");
    menu->hidePopup();
    nameEvents->mouseDown(event({20, 20}, 0, false));
    nameEvents->mouseDrag(event({40, 20}, 40, true));
    nameEvents->mouseUp(event({40, 20}, 60, true));
    require(dragged.size() == 1 && ! menu->isPopupActive(), "Ordinary mouse dragging starts immediately without opening the menu");
    nameEvents->mouseDown(event({20, 20}, 0, false)); pump(550);
    require(dragName->getTooltip().contains("Drag ready") && dragged.size() == 1, "Long press arms dragging without starting it prematurely");
    nameEvents->mouseDrag(event({40, 20}, 600, true));
    nameEvents->mouseUp(event({40, 20}, 650, true));
    require(dragged.size() == 2 && ! menu->isPopupActive() && ! dragName->getTooltip().contains("Drag ready"),
        "Armed movement launches one native file drag and resets without opening the menu");
    std::unique_ptr<gh::Pattern> dragPattern;
    require(gh::loadMidi(dragged[0], dragPattern, ignored).wasOk() && dragPattern->hits.size() == 1
        && dragPattern->hits[0].note == 36, "Long-press drag delivers actual selected mapped MIDI");
    for (const auto& file : dragged) require(file.deleteFile(), "Test-owned drag snapshot cleaned");
    p.setRateAndBufferSizeDetails(48000, 256); p.prepareToPlay(48000, 256);
    juce::AudioBuffer<float> audio(2, 256); juce::MidiBuffer midi;
    p.processBlock(audio, midi); set(p, "midiOut", 1);
    listen->onClick(); p.processBlock(audio, midi);
    require(p.isListening() && p.isPreviewing() && audio.getRMSLevel(0, 0, 256) > 0 && midi.isEmpty(),
        "Listen button auditions audibly without host MIDI output");
    require(copies == 3 && p.state.getRawParameterValue("midiOut")->load() == 1,
        "Listen preserves MIDI routing and clipboard contents");
    listen->onClick(); midi.clear(); p.processBlock(audio, midi); p.processBlock(audio, midi);
    require(! p.isListening() && ! p.isPreviewing() && audio.getRMSLevel(0, 0, 256) == 0,
        "Listen button stops audition with a bounded fade");
    p.setPreview(true, true);
    p.chooseLoop(catalogue.loops[1].file); ready();
    require(! p.isListening() && ! p.isPreviewing(), "New loop selection stops the old listen preview");
    p.chooseLoop(juce::URL(catalogue.loops[0].file), false, true); ready();
    require(copies == 4, "Completed URL-based MIDI import also automatically copies exactly once");
    const auto incoming = p.getView().loopFile;
    require(incoming != catalogue.loops[0].file && incoming.deleteFile()
        && incoming.getParentDirectory().deleteFile(), "Test-owned incoming MIDI copy cleaned");
    juce::PopupMenu::CustomComponent* row = nullptr;
    juce::PopupMenu::MenuItemIterator items(*menu->getRootMenu(), true);
    while (items.next())
        if (items.getItem().itemID == 2) { row = items.getItem().customComponent.get(); break; }
    require(row != nullptr, "Loop menu uses draggable custom entries");
    require(row->getNumChildComponents() == 0, "Closed menu entries do not allocate drag controls");
    juce::Component rowParent;
    rowParent.addAndMakeVisible(*row);
    row->setSize(360, 44);
    require(findControl<juce::TextButton>(*row, "MIDI loop drag handle") != nullptr,
        "Attaching a displayed menu entry creates its drag control");
    rowParent.removeChildComponent(row);
    require(row->getNumChildComponents() == 0, "Closing menu entries releases their drag controls");
    rowParent.addAndMakeVisible(*row);
    auto* rowName = findControl<juce::TextButton>(*row, "MIDI loop drag handle");
    require(rowName != nullptr, "Reopened menu entry recreates its name drag handle");
    juce::Component* rowEvents = rowName;
    row->setSize(360, 44);
    const auto rowEvent = [&rowName](juce::Point<float> position, bool moved)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position,
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
            rowName, rowName, now, {20, 20}, now, 1, moved);
    };
    menu->showPopup(); pump(20);
    detachOnDrag = row;
    rowEvents->mouseDown(rowEvent({20, 20}, false));
    rowEvents->mouseDrag(rowEvent({40, 20}, true)); ready();
    require(dragged.size() == 3 && lastDragSource == editor && ! menu->isPopupActive(),
        "Menu-entry drag dismisses the popup and uses the persistent plugin window as native source");
    juce::MemoryBlock rowMidi;
    require(dragged.back().loadFileAsData(rowMidi) && rowMidi == latest.getMemoryBlock(),
        "Dragging a different menu entry exports that entry, not the previously selected loop");
    require(p.getView().loopFile == catalogue.loops[1].file && copies == 5,
        "Menu-entry drag selects and auto-copies its loop once");
    require(dragged.back().deleteFile(), "Test-owned menu drag snapshot cleaned");
    require(row->getNumChildComponents() == 0, "Dragging does not retain closed-menu drag controls");
    rowParent.addAndMakeVisible(*row);
    auto* reopened = findControl<juce::TextButton>(*row, "MIDI loop drag handle");
    require(reopened != nullptr, "Menu-entry drag can be repeated after reopening");
    rowName = reopened;
    dragResult = juce::Result::fail("Simulated native drag failure");
    juce::Component* reopenedEvents = reopened;
    reopenedEvents->mouseDown(rowEvent({20, 20}, false));
    reopenedEvents->mouseDrag(rowEvent({40, 20}, true));
    require(p.getView().error && p.getView().status.contains("Simulated native drag failure"),
        "Native drag errors remain visible after the temporary drag control is destroyed");
    require(dragged.back().deleteFile(), "Test-owned failed drag snapshot cleaned");
}
void testHostedTempo(const juce::File& library)
{
    for (auto wrapper : {juce::AudioProcessor::wrapperType_Standalone,
                         juce::AudioProcessor::wrapperType_VST3, juce::AudioProcessor::wrapperType_AudioUnitv3})
    {
        juce::AudioProcessor::setTypeOfNextNewPlugin(wrapper);
        DrumProcessor p(library);
        juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Undefined);
        std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
        auto* tempo = findControl<juce::Slider>(*editor, "Tempo (host overrides)");
        const bool standalone = wrapper == juce::AudioProcessor::wrapperType_Standalone;
        require(tempo && tempo->isEnabled() == standalone, "Tempo is editable only in standalone");
        require(standalone || tempo->getTextFromValue(120) == "HOST", "Hosted tempo displays HOST instead of stale preview BPM");
        require(p.state.getRawParameterValue("tempo")->load() == 120, "Hosted UI retains existing tempo parameter state");
    }
}
void testMidiOutput(const juce::File& library)
{
    auto sink = std::make_unique<CapturedMidi>();
    auto* captured = sink.get();
    DrumProcessor p(library, std::move(sink));
    p.setRateAndBufferSizeDetails(48000, 256); p.prepareToPlay(48000, 256);
    gh::Grid g; g.scenes[0].velocity[0][0] = 100; g.scenes[0].variant[0] = 2;
    p.setGrid(g);
    for (int i = 0; i < 1000 && (p.getView().memory == 0 || p.getView().patternLoading); ++i) pump();
    require(p.getView().memory > 0 && ! p.getView().patternLoading, "MIDI test kit and grid ready");
    pump(30);
    juce::AudioBuffer<float> audio(2, 256); juce::MidiBuffer midi;
    p.processBlock(audio, midi);
    midi.addEvent(juce::MidiMessage::noteOn(1, gh::gateNote, 1.0f), 64);
    p.processBlock(audio, midi);
    require(midi.isEmpty() && audio.getRMSLevel(0, 64, 192) > 0, "MIDI output defaults off and preserves loop audio");
    p.stopSequence(); p.processBlock(audio, midi);
    set(p, "midiOut", 1);
    midi.addEvent(juce::MidiMessage::noteOn(1, gh::gateNote, 1.0f), 64);
    p.processBlock(audio, midi);
    require(midi.getNumEvents() == 1, "Only generated note is output; gate input is not echoed");
    auto event = *midi.cbegin();
    require(event.samplePosition == 64 && event.getMessage().isNoteOn()
        && event.getMessage().getChannel() == 10 && event.getMessage().getNoteNumber() == 60
        && event.getMessage().getVelocity() == 100, "Output carries mapped pitch, channel, velocity and exact start offset");
    require(audio.getRMSLevel(0, 64, 192) > 0, "Live MIDI output does not mute normal sampler audio");
    int offPosition = -1;
    for (int block = 1; block <= 10; ++block)
    {
        midi.clear(); p.processBlock(audio, midi);
        for (const auto e : midi) if (e.getMessage().isNoteOff()) offPosition = block * 256 + e.samplePosition;
    }
    require(offPosition == 2464, "Duration note-off is sample-accurate across block boundaries");
    p.stopSequence(); midi.clear(); p.processBlock(audio, midi);
    midi.addEvent(juce::MidiMessage::noteOn(1, gh::gateNote, 1.0f), 20);
    midi.addEvent(juce::MidiMessage::noteOff(1, gh::gateNote), 150);
    p.processBlock(audio, midi);
    require(midi.getNumEvents() == 2 && (*++midi.cbegin()).samplePosition == 150
        && (*++midi.cbegin()).getMessage().isNoteOff(), "Gate release flushes output at the input's exact offset");
    midi.clear(); midi.addEvent(juce::MidiMessage::noteOn(1, gh::gateNote, 1.0f), 0);
    p.processBlock(audio, midi); set(p, "midiOut", 0); midi.clear(); p.processBlock(audio, midi);
    require(midi.getNumEvents() == 1 && (*midi.cbegin()).getMessage().isNoteOff(), "Disabling output releases outstanding notes");
    p.stopSequence(); midi.clear(); p.processBlock(audio, midi);
    set(p, "midiOut", 1);
    midi.addEvent(juce::MidiMessage::noteOn(1, gh::gateNote, 1.0f), 10);
    midi.addEvent(juce::MidiMessage::noteOn(1, gh::gateNote, 1.0f), 100);
    p.processBlock(audio, midi);
    require(midi.getNumEvents() == 3 && (*++midi.cbegin()).getMessage().isNoteOff(),
        "Same-note restart emits note-off before retrigger");
    p.releaseResources(); p.prepareToPlay(48000, 256); midi.clear(); p.processBlock(audio, midi);
    require(midi.getNumEvents() == 1 && (*midi.cbegin()).getMessage().isNoteOff(), "Lifecycle restart releases old host-output notes");
    TestPlayHead host; p.setPlayHead(&host);
    set(p, "koalaRecord", 1); captured->midi.clear(); midi.clear(); p.processBlock(audio, midi);
    require(captured->midi.isEmpty(), "Koala recording mode waits for host transport");
    host.playing = true; midi.clear(); p.processBlock(audio, midi);
    require(captured->midi.getNumEvents() == 1 && (*captured->midi.cbegin()).samplePosition == 0
        && midi.isEmpty(), "Koala host start generates notes at zero via virtual source only");
    require(audio.getRMSLevel(0, 0, 256) == 0, "Koala recording mode avoids doubling internal loop audio");
    captured->midi.clear(); midi.addEvent(juce::MidiMessage::noteOn(10, 60, 1.0f), 40); p.processBlock(audio, midi);
    require(captured->midi.isEmpty() && audio.getRMSLevel(0, 40, 216) > 0,
        "Notes returned by Koala sound normally without MIDI feedback");
    host.playing = false; captured->midi.clear(); midi.clear(); p.processBlock(audio, midi);
    require(captured->midi.getNumEvents() == 1 && (*captured->midi.cbegin()).getMessage().isNoteOff(),
        "Koala transport stop releases virtual-source notes even without a held gate");
    host.playing = true; captured->midi.clear(); midi.clear(); p.processBlock(audio, midi);
    captured->midi.clear();
    for (int i = 0; i < 400; ++i) { midi.clear(); p.processBlock(audio, midi); }
    int extraOns = 0;
    for (const auto e : captured->midi) if (e.getMessage().isNoteOn()) ++extraOns;
    require(extraOns == 0 && ! p.isPreviewing(), "Koala records one pass without repeating into recorded playback");
    host.playing = false; midi.clear(); p.processBlock(audio, midi);
    host.playing = true; captured->midi.clear(); midi.clear(); p.processBlock(audio, midi);
    set(p, "koalaRecord", 0); captured->midi.clear(); midi.clear(); p.processBlock(audio, midi);
    require(captured->midi.getNumEvents() == 1 && (*captured->midi.cbegin()).getMessage().isNoteOff(),
        "Switching away from recording mode releases the old MIDI destination");
    p.setPreview(true); midi.clear(); p.processBlock(audio, midi);
    host.playing = false; midi.clear(); p.processBlock(audio, midi);
    require(midi.getNumEvents() == 1 && (*midi.cbegin()).getMessage().isNoteOff() && ! p.isPreviewing(),
        "Host stop releases MIDI-enabled preview notes");
    p.releaseResources(); p.prepareToPlay(48000, 256);
    g.scenes[0].velocity[2][4] = 110; p.setGrid(g);
    for (int i = 0; i < 1000 && p.getView().patternLoading; ++i) pump();
    set(p, "koalaRecord", 1); midi.clear(); p.processBlock(audio, midi);
    host.playing = true; midi.clear(); p.processBlock(audio, midi);
    set(p, "koalaRecord", 0); midi.clear(); p.processBlock(audio, midi);
    for (int i = 0; i < 100; ++i)
    {
        midi.clear(); p.processBlock(audio, midi);
        require(midi.isEmpty() && audio.getRMSLevel(0, 0, 256) == 0, "Leaving record mode cannot resume internal loop hits");
    }
    set(p, "koalaRecord", 1); midi.clear(); p.processBlock(audio, midi);
    captured->midi.clear();
    p.setPreview(true, true); midi.clear(); p.processBlock(audio, midi);
    require(audio.getRMSLevel(0, 0, 256) > 0 && midi.isEmpty(), "Listen remains audible with Koala recording enabled");
    for (const auto e : captured->midi) require(! e.getMessage().isNoteOn(), "Listen releases old notes without sending new recording notes");
    captured->midi.clear();
    for (int i = 0; i < 50; ++i) { midi.clear(); p.processBlock(audio, midi); }
    require(captured->midi.isEmpty() && p.state.getRawParameterValue("koalaRecord")->load() == 1,
        "Listening suppresses virtual MIDI without changing saved Koala routing");
    p.stopSequence(); captured->midi.clear();
    for (int i = 0; i < 100; ++i) { midi.clear(); p.processBlock(audio, midi); }
    require(captured->midi.isEmpty() && audio.getRMSLevel(0, 0, 256) == 0,
        "Stopping Listen cannot restart a Koala recording pass while host transport stays playing");
    set(p, "koalaRecord", 0);
    p.setPlayHead(nullptr); p.stopSequence(); midi.clear(); p.processBlock(audio, midi);
    gh::Pattern dense; dense.length = 4;
    for (int i = 0; i < 5000; ++i) dense.hits.push_back({0, 1, 36, 1});
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("gh-dense-" + juce::Uuid().toString() + ".mid");
    require(gh::writeMidi(dense, file, 120).wasOk(), "Dense MIDI fixture writes");
    p.chooseLoop(file);
    for (int i = 0; i < 1000 && p.getView().patternLoading; ++i) pump();
    require(! p.getView().patternLoading, "Dense MIDI fixture ready");
    midi.clear(); midi.addEvent(juce::MidiMessage::noteOn(1, gh::gateNote, 1.0f), 0); p.processBlock(audio, midi);
    int balance = 0;
    for (const auto e : midi) balance += e.getMessage().isNoteOn() ? 1 : e.getMessage().isNoteOff() ? -1 : 0;
    require(midi.getNumEvents() <= 4096 + gh::pads && balance == 0, "Overrun stays bounded and leaves no stuck notes");
    require(file.deleteFile(), "Dense MIDI fixture cleaned");
}
void wavTest(int bits, bool floating, int channels, bool extensible)
{
    juce::MemoryOutputStream out;
    const int fmtLength = extensible ? 40 : 16, dataLength = 4 * channels * bits / 8;
    out.write("RIFF", 4); out.writeInt(20 + fmtLength + dataLength); out.write("WAVEfmt ", 8);
    out.writeInt(fmtLength); out.writeShort(static_cast<short>(extensible ? 65534 : floating ? 3 : 1));
    out.writeShort(static_cast<short>(channels)); out.writeInt(48000); out.writeInt(48000 * channels * bits / 8);
    out.writeShort(static_cast<short>(channels * bits / 8)); out.writeShort(static_cast<short>(bits));
    if (extensible)
    {
        out.writeShort(22); out.writeShort(static_cast<short>(bits)); out.writeInt(0);
        out.writeInt(floating ? 3 : 1);
        constexpr std::array<uint8_t, 12> tail {0, 0, 16, 0, 128, 0, 0, 170, 0, 56, 155, 113}; out.write(tail.data(), tail.size());
    }
    out.write("data", 4); out.writeInt(dataLength);
    for (int i = 0; i < 4; ++i)
        for (int c = 0; c < channels; ++c)
        {
            const float value = std::array<float, 4>{0.25f, -0.5f, 0.75f, 0}[static_cast<size_t>(i)] * (c == 0 ? 1 : -1);
            if (floating) out.writeFloat(value);
            else if (bits == 8) out.writeByte(static_cast<char>(128 + static_cast<int>(value * 128)));
            else if (bits == 16) out.writeShort(static_cast<short>(value * 32768));
            else if (bits == 24)
            {
                const auto n = static_cast<uint32_t>(static_cast<int32_t>(value * 8388608));
                out.writeByte(static_cast<char>(n)); out.writeByte(static_cast<char>(n >> 8)); out.writeByte(static_cast<char>(n >> 16));
            }
            else out.writeInt(static_cast<int>(value * 2147483648.0));
        }
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("gh-wave-" + juce::Uuid().toString() + ".wav");
    require(file.replaceWithData(out.getData(), out.getDataSize()), "Write test WAV");
    gh::Sample sample; require(sample.load(file).wasOk(), "WAV encoding loads");
    float l = 0, r = 0; sample.frame(1, l, r);
    require(std::abs(l + 0.5f) < 0.0001 && std::abs(r - (channels == 1 ? -0.5f : 0.5f)) < 0.0001, "PCM decode and channel order");
    gh::Kit kit; kit.samples[0] = sample;
    for (float tune : {-12.0f, 0.0f, 19.0f})
    {
        gh::Engine engine; engine.prepare(48000); engine.setKit(&kit);
        kit.values[0].tune = tune; engine.setValues(kit.values, 0, 0); engine.trigger(0, 0.8f);
        const double increment = std::exp2(tune / 12.0f);
        double position = 0;
        constexpr std::array<float, 4> frames {0.25f, -0.5f, 0.75f, 0};
        for (int i = 0; i < 16; ++i)
        {
            engine.process(l, r);
            const auto index = static_cast<size_t>(position);
            float expected = 0;
            if (index + 1 < frames.size())
            {
                const float fraction = static_cast<float>(position - static_cast<double>(index));
                expected = (frames[index] + fraction * (frames[index + 1] - frames[index]))
                    * (0.8f * static_cast<float>(std::min(1.0, (4 - position) / 48)));
            }
            require(std::abs(l - expected) <= 1.0e-7f
                && std::abs(r - (channels == 1 ? expected : -expected)) <= 1.0e-7f,
                "PCM/float playback preserves first, repeated, adjacent and skipped frames with stereo channel order");
            position += increment;
        }
        require(! engine.hasVoices(), "Cached playback still completes its one-shot");
    }
    juce::MemoryBlock bad(out.getData(), out.getDataSize());
    static_cast<uint8_t*>(bad.getData())[12] = 'X';
    require(file.replaceWithData(bad.getData(), bad.getSize()), "Write malformed WAV");
    require(sample.load(file).failed(), "Malformed replacement cannot inherit old WAV format");
    require(file.deleteFile(), "Clean test WAV");
}
void batteryText(juce::MemoryOutputStream& out, const juce::String& text)
{
    juce::MemoryOutputStream encoded;
    auto characters = text.getCharPointer();
    while (! characters.isEmpty())
    {
        auto value = static_cast<uint32_t>(characters.getAndAdvance());
        if (value > 0xffff)
        {
            value -= 0x10000;
            encoded.writeShort(static_cast<short>(0xd800 + (value >> 10)));
            encoded.writeShort(static_cast<short>(0xdc00 + (value & 1023)));
        }
        else encoded.writeShort(static_cast<short>(value));
    }
    out.writeInt(static_cast<int>(encoded.getDataSize() / 2));
    out.write(encoded.getData(), encoded.getDataSize());
}
void batteryChunk(juce::MemoryOutputStream& out, int kind, const juce::MemoryBlock& bytes)
{
    out.writeShort(static_cast<short>(kind)); out.writeInt(static_cast<int>(bytes.getSize()));
    out.write(bytes.getData(), bytes.getSize());
}
juce::MemoryBlock batteryRecord(int kind, const juce::MemoryBlock& common,
                               const juce::MemoryBlock& properties, const juce::MemoryBlock& body)
{
    juce::MemoryOutputStream out;
    out.writeByte(1); batteryChunk(out, kind, common);
    out.writeInt(static_cast<int>(properties.getSize())); out.write(properties.getData(), properties.getSize());
    out.writeInt(static_cast<int>(body.getSize())); out.write(body.getData(), body.getSize());
    return out.getMemoryBlock();
}
juce::MemoryBlock batteryContainer(int id, const juce::MemoryBlock& payload, const juce::MemoryBlock& child = {})
{
    juce::MemoryOutputStream base, header, out;
    base.write("DSIN", 4); base.writeInt(1); base.writeInt(1); base.writeRepeatedByte(0, 12);
    header.write("DSIN", 4); header.writeInt(id); header.writeInt(1); header.writeInt64(24);
    header.write(base.getData(), base.getDataSize()); header.write(payload.getData(), payload.getSize());
    out.writeInt64(static_cast<juce::int64>(48 + header.getDataSize() + (child.getSize() > 0 ? 12 + child.getSize() : 0)));
    out.writeInt(1); out.write("hsin", 4); out.writeInt(1); out.writeInt(0); out.writeRepeatedByte(0, 16);
    out.writeInt64(static_cast<juce::int64>(header.getDataSize())); out.write(header.getData(), header.getDataSize());
    if (child.getSize() > 0)
    { out.writeInt(0); out.write("4KIN", 4); out.writeInt(3); out.write(child.getData(), child.getSize()); }
    return out.getMemoryBlock();
}
juce::MemoryBlock batteryFixture(const juce::String& name, bool escape = false, bool badCompression = false)
{
    juce::MemoryOutputStream paths, cells, zones, body, common, properties, top, document, compressed;
    paths.writeShort(2); paths.writeInt(1); paths.writeInt(0); paths.writeInt(2);
    for (const auto* file : {"first.wav", "second.wav"})
    {
        paths.writeInt(2);
        paths.writeByte(escape ? 3 : 2);
        if (! escape) batteryText(paths, "Samples");
        paths.writeByte(4); batteryText(paths, file);
    }
    paths.writeRepeatedByte(0, 24); paths.writeInt(0);
    cells.writeInt(128);
    for (int i = 0; i < 128; ++i)
    {
        juce::MemoryOutputStream label;
        batteryText(label, i == 0 ? juce::String::fromUTF8("Fixture kick \xc3\xa4")
            : i == 64 ? "Second bank kick" : "");
        const auto record = batteryRecord(149, {}, label.getMemoryBlock(), {});
        cells.write(record.getData(), record.getSize());
    }
    zones.writeInt(4);
    for (int i = 0; i < 4; ++i)
    {
        zones.writeInt(i < 2 ? 0 : i == 2 ? 64 : 127);
        juce::MemoryOutputStream settings;
        settings.writeRepeatedByte(0, 42); settings.writeInt(i == 1 ? 1 : 0); settings.writeRepeatedByte(0, 30);
        const auto record = batteryRecord(152, {}, settings.getMemoryBlock(), {});
        zones.write(record.getData(), record.getSize());
    }
    batteryChunk(body, 51, cells.getMemoryBlock()); batteryChunk(body, 52, zones.getMemoryBlock());
    common.writeRepeatedByte(0, 48); common.writeInt(5); common.writeInt(12); common.writeRepeatedByte(0, 37);
    batteryText(properties, name);
    batteryChunk(top, 40, batteryRecord(163, common.getMemoryBlock(), properties.getMemoryBlock(), body.getMemoryBlock()));
    batteryChunk(top, 75, paths.getMemoryBlock());
    document.writeInt(1); document.writeInt64(static_cast<juce::int64>(top.getDataSize()));
    document.write(top.getData(), top.getDataSize()); document.writeRepeatedByte(0, 16);
    const auto raw = batteryContainer(109, document.getMemoryBlock());
    juce::MemoryOutputStream packed;
    if (badCompression)
    {
        constexpr uint8_t invalid[] {0, 'A', 32, 1};
        packed.write(invalid, sizeof(invalid));
    }
    else
        for (size_t offset = 0; offset < raw.getSize(); offset += 32)
        {
            const auto count = std::min(size_t{32}, raw.getSize() - offset);
            packed.writeByte(static_cast<char>(count - 1));
            packed.write(static_cast<const uint8_t*>(raw.getData()) + offset, count);
        }
    compressed.writeByte(0); compressed.writeInt(static_cast<int>(packed.getDataSize()));
    compressed.write(packed.getData(), packed.getDataSize()); compressed.writeRepeatedByte(0, 8);
    return batteryContainer(118, {}, batteryContainer(115, compressed.getMemoryBlock()));
}
void testMidiRouter()
{
    using namespace gh::midi;
    Router router;
    Router::Settings settings;
    settings.output = true;
    settings.thru = false;
    router.setSettings(settings);

    juce::MidiBuffer out;
    router.route(juce::MidiMessage::noteOn(10, 36, 0.8f), 5, out);
    require(out.getNumEvents() == 1, "Router emits one note");
    require((*out.cbegin()).samplePosition == 5, "Router keeps the exact sample position");
    const auto routed = (*out.cbegin()).getMessage();
    require(routed.getChannel() == 10 && routed.getNoteNumber() == 36 && routed.getVelocity() == 102,
        "Router keeps channel, note and velocity");
    require(router.hasHeldNotes(), "Router tracks the sounding note");
    out.clear();
    router.route(juce::MidiMessage::noteOff(10, 36), 0, out);
    require(! router.hasHeldNotes(), "Note-off clears the tracked note");

    out.clear();
    settings.inputChannel = 1;
    router.setSettings(settings);
    router.route(juce::MidiMessage::noteOn(2, 36, 1.0f), 0, out);
    require(out.isEmpty(), "Channel filter drops other channels");
    router.route(juce::MidiMessage::noteOn(1, 120, 1.0f), 0, out);
    settings.highNote = 100;
    router.setSettings(settings);
    out.clear();
    router.route(juce::MidiMessage::noteOn(1, 120, 1.0f), 0, out);
    require(out.isEmpty(), "Note range filter drops notes outside the range");

    settings.highNote = 127;
    settings.inputChannel = 0;
    settings.outputChannel = 2;
    settings.transpose = 12;
    router.setSettings(settings);
    out.clear();
    router.route(juce::MidiMessage::noteOn(1, 36, 1.0f), 7, out);
    const auto moved = (*out.cbegin()).getMessage();
    require(moved.getChannel() == 2 && moved.getNoteNumber() == 48, "Router remaps the channel and transposes");
    router.reset();

    out.clear();
    settings.transpose = 0;
    settings.outputChannel = 10;
    settings.thru = true;
    router.setSettings(settings);
    router.route(juce::MidiMessage::noteOn(10, 40, 1.0f), 0, out);
    require(out.getNumEvents() == 1, "Identical thru and output notes are not duplicated");
    router.reset();
    out.clear();
    settings.outputChannel = 11;
    router.setSettings(settings);
    router.route(juce::MidiMessage::noteOn(10, 40, 1.0f), 0, out);
    require(out.getNumEvents() == 2, "Thru plus a different output channel emits both routes");

    out.clear();
    router.releaseAll(12, out);
    require(out.getNumEvents() == 2 && (*out.cbegin()).samplePosition == 12, "releaseAll releases every routed copy");
    require(! router.hasHeldNotes(), "releaseAll clears the tracked notes");

    require(std::abs(applyVelocityCurve(0.25f, soft) - 0.5f) < 1.0e-6f, "Soft curve lifts low velocities");
    require(std::abs(applyVelocityCurve(0.5f, hard) - 0.25f) < 1.0e-6f, "Hard curve lowers velocity");

    NoteMap map;
    require(map.noteForSlot(0) == 36 && map.slotForNote(40) == 4, "Default note map matches the 1.0.8 layout");
    map.setNote(4, 100);
    require(map.slotForNote(100) == 4 && map.slotForNote(40) == -1, "Custom note map replaces the default mapping");
}

void testCvConversion()
{
    using namespace gh::midi;
    CvOutput cv;
    cv.setSampleRate(48000);
    cv.noteOn(72, 0.8f, 0.05);
    float pitch = 0, gate = 0;
    for (int i = 0; i < 100; ++i) cv.render(pitch, gate);
    require(std::abs(pitch - 1.0f) < 1.0e-6f, "MIDI note 72 is 1 V");
    require(gate > 0.1f, "Gate CV follows the note");
    for (int i = 0; i < 8000; ++i) cv.render(pitch, gate);
    require(gate < 0.001f, "Gate CV closes after its duration");

    CvOutput source;
    source.setSampleRate(48000);
    source.noteOn(48, 1.0f, 0.2);
    CvInput sink;
    int opened = 0, closed = 0, detected = -1;
    for (int i = 0; i < 40000; ++i)
    {
        float p = 0, g = 0;
        source.render(p, g);
        const auto edge = sink.process(p, g);
        if (edge > 0) { ++opened; detected = sink.currentNote(); }
        if (edge < 0) ++closed;
    }
    require(opened == 1 && closed == 1 && detected == 48, "CV round-trip recovers the note and a single gate");
}

void testMidiIntegration(const juce::File& library)
{
    DrumProcessor p(library);
    p.setRateAndBufferSizeDetails(48000, 256);
    p.prepareToPlay(48000, 256);
    for (int i = 0; i < 1000 && (p.getView().memory == 0 || p.getView().patternLoading); ++i) pump();
    require(p.getView().memory > 0, "MIDI integration kit ready");
    p.stopSequence();
    juce::AudioBuffer<float> audio(2, 256);
    juce::MidiBuffer midi;
    p.processBlock(audio, midi);

    set(p, "midiInChannel", 1);
    const auto filteredStart = p.getMidiMonitor().inCount;
    midi.addEvent(juce::MidiMessage::noteOn(2, 36, 1.0f), 0);
    p.processBlock(audio, midi);
    require(p.getMidiMonitor().inCount == filteredStart, "Input channel filter drops other channels");
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 0);
    p.processBlock(audio, midi);
    require(p.getMidiMonitor().triggerCount == filteredStart + 1, "Matching input channel triggers the pad");
    set(p, "midiInChannel", 0);

    const auto beforeMap = p.getMidiMonitor().triggerCount;
    midi.addEvent(juce::MidiMessage::noteOn(1, 100, 1.0f), 0);
    p.processBlock(audio, midi);
    require(p.getMidiMonitor().triggerCount == beforeMap, "Unmapped notes do not trigger a pad");
    set(p, "midiNote05", 100);
    midi.addEvent(juce::MidiMessage::noteOn(1, 100, 1.0f), 0);
    p.processBlock(audio, midi);
    require(p.getMidiMonitor().triggerCount == beforeMap + 1, "Custom note map routes a new note to its pad");
    set(p, "midiNote05", 40);

    set(p, "midiIn", 0);
    const auto beforeDisabled = p.getMidiMonitor().inCount;
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 0);
    p.processBlock(audio, midi);
    require(p.getMidiMonitor().inCount == beforeDisabled, "MIDI input switch disables external triggering");
    set(p, "midiIn", 1);

    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(3, 55, 0.9f), 10);
    p.processBlock(audio, midi);
    require(midi.isEmpty(), "Trigger-only mode does not echo input MIDI");

    set(p, "midiThru", 1);
    const auto beforeThru = p.getMidiMonitor().triggerCount;
    midi.addEvent(juce::MidiMessage::noteOn(3, 55, 0.9f), 10);
    p.processBlock(audio, midi);
    require(midi.getNumEvents() == 1 && (*midi.cbegin()).getMessage().getNoteNumber() == 55
        && (*midi.cbegin()).samplePosition == 10, "Thru-only mode echoes the input at its exact offset");
    require(p.getMidiMonitor().triggerCount == beforeThru, "Thru-only mode does not trigger the pads");
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOff(3, 55), 3);
    p.processBlock(audio, midi);
    require(midi.getNumEvents() == 1 && (*midi.cbegin()).getMessage().isNoteOff(), "Thru forwards note-off");
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(3, 55, 0.9f), 0);
    p.processBlock(audio, midi);
    require(midi.getNumEvents() == 1, "Thru forwards a held note");
    p.stopSequence();
    midi.clear();
    p.processBlock(audio, midi);
    require(midi.getNumEvents() == 1 && (*midi.cbegin()).getMessage().isNoteOff(),
        "Stop releases passed-through notes to prevent stuck notes");

    set(p, "midiThru", 2);
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(3, 55, 0.9f), 5);
    p.processBlock(audio, midi);
    require(midi.getNumEvents() == 1 && p.getMidiMonitor().triggerCount == beforeThru + 1,
        "Trigger + thru echoes and triggers simultaneously");

    set(p, "midiThru", 0);
    set(p, "midiOut", 1);
    set(p, "midiOutChannel", 2);
    set(p, "midiNote01", 60);
    gh::Grid g;
    g.scenes[0].velocity[0][0] = 100;
    p.setGrid(g);
    for (int i = 0; i < 500 && p.getView().patternLoading; ++i) pump();
    p.stopSequence();
    midi.clear();
    p.processBlock(audio, midi);
    midi.addEvent(juce::MidiMessage::noteOn(1, gh::gateNote, 1.0f), 0);
    p.processBlock(audio, midi);
    bool foundMapped = false;
    for (const auto e : midi)
        if (e.getMessage().isNoteOn())
            foundMapped = e.getMessage().getChannel() == 2 && e.getMessage().getNoteNumber() == 60;
    require(foundMapped, "Loop notes use the selected output channel and note map");

    p.stopSequence();
    midi.clear();
    p.processBlock(audio, midi);
    set(p, "midiOut", 0);
    set(p, "midiCvMode", 1);
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 72, 1.0f), 0);
    p.processBlock(audio, midi);
    require(std::abs(audio.getSample(0, 128) - 1.0f) < 1.0e-4f, "CV mode emits 1 V/oct pitch CV on the left channel");
    require(audio.getSample(1, 128) > 0.5f, "CV mode emits a gate on the right channel");
    set(p, "midiCvMode", 0);
}

void testAuv3Popups(const juce::File& library)
{
    juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_AudioUnitv3);
    DrumProcessor p(library);
    juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Undefined);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    // AUv3 hosts always give the editor a real peer; mirror that here so the
    // popup-parent code path is the one under test.
    editor->addToDesktop(juce::ComponentPeer::windowHasTitleBar);
    editor->setVisible(true);
    editor->setSize(900, 800);
    pump(20);
    auto* kits = findControl<juce::ComboBox>(*editor, "Drum kit");
    require(kits != nullptr, "AUv3 editor exposes the kit selector");
    require(juce::Component::getCurrentlyModalComponent() == nullptr, "AUv3 editor starts unblocked");
    kits->showPopup();
    pump(40);
    auto* modal = juce::Component::getCurrentlyModalComponent();
    require(modal != nullptr, "AUv3 kit menu opens");
    require(editor->isParentOf(modal) || modal->getTopLevelComponent() == editor.get(),
        "AUv3 menu is hosted inside the plugin view instead of a separate window");
    juce::PopupMenu::dismissAllActiveMenus();
    pump(40);
    require(juce::Component::getCurrentlyModalComponent() == nullptr,
        "Dismissing the menu unblocks every control again");
}

void testBatteryImport(const juce::File& library)
{
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("gh-battery-" + juce::Uuid().toString());
    require(folder.getChildFile("Samples").createDirectory().wasOk(), "Synthetic Battery sample folder created");
    juce::MemoryOutputStream wave;
    wave.write("RIFF", 4); wave.writeInt(40); wave.write("WAVEfmt ", 8); wave.writeInt(16);
    wave.writeShort(1); wave.writeShort(1); wave.writeInt(48000); wave.writeInt(96000);
    wave.writeShort(2); wave.writeShort(16); wave.write("data", 4); wave.writeInt(4);
    wave.writeShort(8192); wave.writeShort(8192);
    for (const auto* name : {"first.wav", "second.wav"})
        require(folder.getChildFile("Samples").getChildFile(name).replaceWithData(wave.getData(), wave.getDataSize()),
            "Synthetic Battery WAV created");
    const auto name = "Battery validation " + juce::Uuid().toString();
    const auto file = folder.getChildFile("Synthetic.nbkt");
    const auto fixture = batteryFixture(name);
    require(file.replaceWithData(fixture.getData(), fixture.getSize()), "Synthetic Battery container created");
    gh::BatteryKit metadata;
    auto result = gh::readBatteryKit(file, metadata);
    require(result.wasOk(), result.getErrorMessage());
    require(metadata.samples.size() == 60 && metadata.omittedZones == 1 && metadata.hiddenCells == 1
        && metadata.samples[0] == folder.getChildFile("Samples/first.wav")
        && metadata.samples[48] == metadata.samples[0] && metadata.samples[1] == juce::File{},
        "Battery visible-grid stride, first-zone choice and hidden-cell handling are preserved");
    std::unique_ptr<gh::Kit> bank;
    require(gh::loadBatteryBank(metadata, 1, bank).wasOk() && bank->name == name + " - Bank 2"
        && bank->padNames[0] == "Second bank kick", "Large Battery kit splits without dropping visible cells or names");
    gh::Engine engine; engine.prepare(48000); engine.setKit(bank.get()); engine.setValues(bank->values, 0, 0);
    engine.trigger(12, 1);
    require(rms(engine, 4) == 0, "Unused cells and bank padding render as silence");
    engine.trigger(0, 1);
    require(rms(engine, 4) > 0, "Selected Battery WAV renders audibly");
    std::vector<juce::File> savedFolders;
    {
        DrumProcessor processor(library);
        for (int i = 0; i < 1000 && processor.getView().memory == 0; ++i) pump();
        processor.importKit(file);
        for (int i = 0; i < 1000 && processor.getView().kitName != name + " - Bank 1"; ++i) pump();
        pump(30);
        const auto view = processor.getView();
        require(view.kitName == name + " - Bank 1" && ! view.error && view.importInfo.contains("first zone"),
            "Direct NBKT import selects the first bank and persistently discloses omitted Battery features: " + view.status);
        for (const auto& entry : view.catalogue.kits)
            if (entry.name.startsWith(name))
            {
                std::unique_ptr<gh::Kit> saved;
                require(gh::loadKit(entry.file, saved).wasOk() && saved->padNames[0].isNotEmpty()
                    && saved->importInfo.contains("first zone"),
                    "Battery banks save as portable Ghost kits with original cell names");
                savedFolders.push_back(entry.file.getParentDirectory());
            }
        require(savedFolders.size() == 2 && view.padNames[0] == juce::String::fromUTF8("Fixture kick \xc3\xa4"),
            "Every imported bank is indexed and selected-pad Unicode names reach the UI");
        juce::MemoryBlock original;
        require(file.loadFileAsData(original) && original == fixture, "Original NBKT remains untouched");
    }
    for (const auto& saved : savedFolders) require(saved.deleteRecursively(), "Only test-owned imported bank cleaned");
    auto malformed = fixture;
    static_cast<uint8_t*>(malformed.getData())[12] = 'X';
    require(file.replaceWithData(malformed.getData(), malformed.getSize())
        && gh::readBatteryKit(file, metadata).failed(), "Invalid NI container header is rejected");
    {
        DrumProcessor processor(library);
        processor.importKit(file);
        for (int i = 0; i < 1000 && (! processor.getView().error || processor.getView().patternLoading); ++i) pump();
        pump(30);
        require(processor.getView().error && processor.getView().status.contains("Battery"),
            "A failed Battery import remains visible even when the startup pattern finishes in the same batch");
    }
    const auto invalid = batteryFixture(name, false, true);
    require(file.replaceWithData(invalid.getData(), invalid.getSize()), "Invalid compressed fixture created");
    result = gh::readBatteryKit(file, metadata);
    require(result.failed() && result.getErrorMessage().contains("back-reference"), "Invalid FastLZ references are rejected");
    const auto escaped = batteryFixture(name, true);
    require(file.replaceWithData(escaped.getData(), escaped.getSize()), "Escaping path fixture created");
    result = gh::readBatteryKit(file, metadata);
    require(result.failed() && result.getErrorMessage().contains("escapes"), "Battery paths cannot escape their sample library");
    require(folder.deleteRecursively(), "Only synthetic Battery fixture folder cleaned");
}
void checkBatteryLibrary(const juce::File& input)
{
    const auto files = input.isDirectory()
        ? input.findChildFiles(juce::File::findFiles, true, "*.nbkt") : juce::Array<juce::File>{input};
    require(! files.isEmpty(), "Battery validation found kit files");
    size_t banks = 0, zones = 0, hidden = 0;
    for (const auto& file : files)
    {
        gh::BatteryKit metadata;
        auto result = gh::readBatteryKit(file, metadata);
        require(result.wasOk(), result.getErrorMessage());
        zones += metadata.omittedZones; hidden += metadata.hiddenCells;
        const auto count = (metadata.samples.size() + gh::pads - 1) / gh::pads;
        for (size_t bank = 0; bank < count; ++bank)
        {
            std::unique_ptr<gh::Kit> kit;
            result = gh::loadBatteryBank(metadata, bank, kit);
            require(result.wasOk(), file.getFileName() + ": " + result.getErrorMessage());
            ++banks;
        }
    }
    std::cout << "Verified Battery metadata and first-zone audio: " << files.size() << " kits, "
        << banks << " banks, " << zones << " extra zones, " << hidden
        << " hidden cells. No library audio exported.\n";
}
}
int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc == 3 && juce::String(argv[2]) == "--check-battery")
    {
        checkBatteryLibrary(juce::File(juce::String::fromUTF8(argv[1])));
        return 0;
    }
    if (argc == 3 && juce::String(argv[2]) == "--profile")
    {
        profile(juce::File(juce::String::fromUTF8(argv[1])));
        return 0;
    }
    if (argc == 4 && (juce::String(argv[2]) == "--write-dsp-reference" || juce::String(argv[2]) == "--check-dsp-reference"))
    {
        dspReference(juce::File(juce::String::fromUTF8(argv[3])), juce::String(argv[2]) == "--write-dsp-reference");
        return 0;
    }
    if (argc > 2 && juce::String(argv[2]) == "--copy-test-midi")
    {
        gh::Grid grid;
        grid.scenes[0].velocity[0][0] = 100; grid.scenes[0].velocity[2][4] = 110;
        juce::MemoryOutputStream output;
        require(gh::writeMidi(*gh::gridPattern(grid), output, 120).wasOk(), "Clipboard fixture encodes");
        const auto result = gh::copyMidiToClipboard(output.getMemoryBlock());
        require(result.wasOk(), result.getErrorMessage());
        std::cout << "Published one-bar MIDI clipboard fixture: kick 36 at beat 0, snare 38 at beat 1\n";
        return 0;
    }
    for (int bits : {8, 16, 24, 32}) wavTest(bits, false, 1, false);
    wavTest(32, true, 2, false); wavTest(24, false, 2, true); wavTest(32, true, 1, true);
    auto kit = testKit();
    gh::Engine engine; engine.prepare(48000); engine.setKit(&kit);
    std::array<gh::PadValues, gh::pads> values {};
    engine.setValues(values, -12, 0); engine.trigger(0, 1);
    const double clean = rms(engine); require(clean > 0.02, "Sample playback");
    engine.fadeVoices(true, 5); require(rms(engine) > 0.02, "Loop release does not cut live one-shots");
    engine.fadeVoices(false, 5); require(rms(engine, 4096) < clean * 0.3, "Finite fade");
    engine.reset(); values[0].pan = 1; engine.setValues(values, -12, 0); engine.trigger(0, 1);
    float l = 0, r = 0; for (int i = 0; i < 4096; ++i) engine.process(l, r);
    require(std::abs(l) < 1.0e-6f && std::abs(r) > 0.001f, "Hard-right pan");
    engine.reset(); values[0].pan = 0; values[0].highCut = 100; engine.setValues(values, -12, 0); engine.trigger(0, 1);
    require(rms(engine) < clean * 0.1, "High-cut attenuation");
    engine.setKit(&kit); engine.setValues(values, -12, 0); engine.trigger(0, 1);
    require(rms(engine) < clean * 0.1, "Kit switch preserves filter coefficients");
    values[0] = {}; values[0].lowCut = 2000; engine.reset(); engine.setValues(values, -12, 0); engine.trigger(0, 1);
    require(rms(engine) < clean * 0.1, "Low-cut attenuation");
    values[0] = {}; engine.reset(); engine.setValues(values, -12, 0); engine.trigger(0, 1, true); engine.trigger(1, 1);
    engine.fadeVoices(true, 5); for (int i = 0; i < 300; ++i) engine.process(l, r);
    require(engine.hasVoices(), "Live hit survives loop gate release");
    for (int i = 0; i < 50000; ++i) engine.process(l, r); require(! engine.hasVoices(), "One-shot ends without note-off");
    values[0] = {}; engine.reset(); engine.setValues(values, -12, 0); engine.trigger(0, 1);
    const int base = crossings(engine);
    values[0].tune = 12; engine.setValues(values, -12, 0);
    require(std::abs(crossings(engine) - 2 * base) <= 2, "Tune updates ringing one-shot");
    values[0] = {}; values[0].velocityPitch = 24; engine.reset(); engine.setValues(values, -12, 0); engine.trigger(0, 0.25f);
    require(std::abs(crossings(engine) - base / 2) <= 2, "Velocity-to-pitch");
    values[0] = {}; values[0].compressor = 1; engine.reset(); engine.setValues(values, -12, 0); engine.trigger(0, 1);
    require(rms(engine) < clean * 0.75, "Compressor reduces dynamic peak level");
    engine.reset(); values[0] = {}; engine.setValues(values, 0, 1);
    for (int i = 0; i < 100; ++i) engine.trigger(0, 1);
    for (int i = 0; i < 8192; ++i) { engine.process(l, r); require(std::isfinite(l) && std::abs(l) < 0.501, "Voice stealing and master saturation remain bounded"); }

    gh::Grid grid; grid.scenes[0].velocity[0][0] = 127; grid.scenes[0].velocity[7][1] = 96;
    grid.scenes[0].variant[7] = 3; grid.swing = 0.2f;
    auto pattern = gh::gridPattern(grid);
    require(pattern->hits[1].note == 79 && std::abs(pattern->hits[1].beat - 0.3) < 0.001, "Four variants and swing");
    gh::Grid restored; require(gh::gridFromVar(gh::gridToVar(grid), restored).wasOk(), "Scene state round-trip");
    require(restored.scenes[0].variant[7] == 3, "State preserves variants");
    auto legacy = gh::gridToVar(grid);
    juce::Array<juce::var> legacyScenes;
    for (const auto& scene : grid.scenes)
    {
        juce::Array<juce::var> cells;
        for (const auto& lane : scene.velocity) for (int s = 0; s < 16; ++s) cells.add(int(lane[static_cast<size_t>(s)]));
        for (auto variant : scene.variant) cells.add(int(variant));
        legacyScenes.add(cells);
    }
    legacy.getDynamicObject()->removeProperty("stepCount");
    legacy.getDynamicObject()->setProperty("scenes", legacyScenes);
    require(gh::gridFromVar(legacy, restored).wasOk() && restored.stepCount == 16
        && restored.scenes[0].velocity[7][1] == 96 && restored.scenes[0].variant[7] == 3
        && restored.scenes[0].velocity[7][17] == 0, "Legacy 16-step scenes restore without remapping lanes");
    auto longGrid = grid; longGrid.stepCount = 32; longGrid.scenes[0].velocity[2][31] = 83;
    auto longPattern = gh::gridPattern(longGrid);
    require(longPattern->length == 8 && longPattern->hits.size() == 3
        && longPattern->hits.back().note == 38 && std::abs(longPattern->hits.back().beat - 7.8) < 0.001,
        "32-step pattern includes step 32 with correct swing and doubled length");
    require(gh::gridFromVar(juce::JSON::parse(juce::JSON::toString(gh::gridToVar(longGrid))), restored).wasOk()
        && restored.stepCount == 32 && restored.scenes[0].velocity[2][31] == 83, "32-step scenes survive JSON state round-trip");
    longGrid.stepCount = 16;
    require(gh::gridPattern(longGrid)->hits.size() == 2
        && gh::gridFromVar(gh::gridToVar(longGrid), restored).wasOk()
        && restored.scenes[0].velocity[2][31] == 83, "16-step mode silences, but saves, hidden extra steps");
    auto invalidLength = gh::gridToVar(longGrid);
    invalidLength.getDynamicObject()->setProperty("stepCount", 24);
    require(gh::gridFromVar(invalidLength, restored).failed(), "Unsupported sequencer length rejected");
    invalidLength.getDynamicObject()->setProperty("stepCount", 32.5);
    require(gh::gridFromVar(invalidLength, restored).failed(), "Fractional sequencer length rejected");
    gh::Clock longClock; longClock.setPattern(longPattern.get()); longClock.start();
    int lateHits = 0, starts = 0;
    const auto countLongHit = [&lateHits, &starts](int slot, float, double)
    { if (slot == 2) ++lateHits; if (slot == 0) ++starts; };
    for (int i = 0; i < 32; ++i) longClock.process(0.25, countLongHit);
    require(lateHits == 1 && starts == 1, "32-step playback reaches second half before wrapping");
    longClock.process(0.25, countLongHit);
    require(lateHits == 1 && starts == 2, "32-step playback wraps at eight beats");
    auto human = gh::clonePattern(*pattern); gh::humanize(*human, 42, 120);
    auto same = gh::clonePattern(*pattern); gh::humanize(*same, 42, 120);
    require(human->hits[1].beat == same->hits[1].beat && human->hits[1].velocity == same->hits[1].velocity, "Deterministic humanize");
    require(human->hits[1].beat != pattern->hits[1].beat && std::abs(human->hits[1].beat - pattern->hits[1].beat) <= 0.0061, "Humanize timing bounded to 3ms");
    const auto midiFile = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("gh-test-" + juce::Uuid().toString() + ".mid");
    require(gh::writeMidi(*pattern, midiFile, 120).wasOk(), "MIDI export");
    juce::MemoryBlock exported;
    require(midiFile.loadFileAsData(exported) && exported.getSize() >= 14, "MIDI header exists");
    const auto* header = static_cast<const uint8_t*>(exported.getData());
    require(header[8] == 0 && header[9] == 0 && header[10] == 0 && header[11] == 1,
        "Single-track export is SMF type 0 for host interoperability");
    std::unique_ptr<gh::Pattern> imported; int ignored = 0;
    require(gh::loadMidi(midiFile, imported, ignored).wasOk(), "MIDI round-trip");
    require(imported->hits.size() == 2 && imported->hits[1].note == 79 && imported->length == 4, "MIDI notes and loop length");
    require(midiFile.deleteFile(), "Test MIDI cleaned up");
    require(gh::writeMidi(*longPattern, midiFile, 120).wasOk()
        && gh::loadMidi(midiFile, imported, ignored).wasOk()
        && imported->length == 8 && imported->hits.size() == 3
        && std::abs(imported->hits.back().beat - 7.8) < 0.001, "MIDI export retains all 32 steps and exact loop length");
    require(midiFile.deleteFile(), "32-step test MIDI cleaned up");
    grid.rate = 0; pattern = gh::gridPattern(grid);
    require(gh::writeMidi(*pattern, midiFile, 137).wasOk() && gh::loadMidi(midiFile, imported, ignored, true).wasOk(), "Short loop exports and imports");
    require(imported->length == 2 && imported->hits[1].note == 79, "EOT preserves half-bar length; mapped export is not remapped twice");
    require(midiFile.deleteFile(), "Short MIDI cleaned up");
    gh::Pattern ticks; ticks.length = 0.25; ticks.hits.push_back({0, 0.05, 36, 1});
    gh::Clock clock; clock.setPattern(&ticks); clock.start(); int hitCount = 0;
    for (int i = 0; i < 12002; ++i) clock.process(120.0 / (60 * 48000), [&hitCount](int, float, double) { ++hitCount; });
    require(hitCount == 3, "First hit repeats across loop boundaries");
    clock.stop(); for (int i = 0; i < 100; ++i) clock.process(0.001, [&hitCount](int, float, double) { ++hitCount; });
    require(hitCount == 3, "Gate release stops sequencer generation");
    clock.start(); clock.process(0.001, [&hitCount](int, float, double) { ++hitCount; });
    require(hitCount == 4, "Restart replays first hit");
    require(gh::mapGmNote(42) == 43 && gh::mapGmNote(49) == 47 && gh::mapGmNote(51) == 59 && gh::mapGmNote(41) == 80, "GM/FPC drum roles match user layout");

    gh::Catalogue catalogue;
    require(argc == 2 && gh::scanCatalogue(juce::File(juce::String::fromUTF8(argv[1])), catalogue).wasOk(), "Packed catalogue loads");
    require(catalogue.kits.size() == 54, "All 54 kits available");
    testBatteryImport(catalogue.root);
    for (const auto& entry : catalogue.kits)
    {
        std::unique_ptr<gh::Kit> loaded;
        const auto result = gh::loadKit(entry.file, loaded); require(result.wasOk(), result.getErrorMessage());
        for (const auto& sample : loaded->samples) require(sample.frames > 1 && ! sample.pcm.empty(), "Every pad has valid audio");
    }
    int supportedLoops = 0;
    for (const auto& entry : catalogue.loops)
    {
        std::unique_ptr<gh::Pattern> loop; const auto result = gh::loadMidi(entry.file, loop, ignored);
        require(result.wasOk(), result.getErrorMessage()); ++supportedLoops;
    }
    std::cout << "Validated 54 x 48 samples and " << supportedLoops << " MIDI loops\n";
    testMidiOutput(catalogue.root);
    testMidiClipboard(catalogue);
    testHostedTempo(catalogue.root);
    testAuv3Popups(catalogue.root);
    testMidiRouter();
    testCvConversion();
    testMidiIntegration(catalogue.root);
    DrumProcessor processor(catalogue.root);
    processor.setRateAndBufferSizeDetails(48000, 256);
    processor.prepareToPlay(48000, 256);
    for (int i = 0; i < 500 && processor.getView().memory == 0; ++i) pump();
    require(processor.getView().memory > 0, "Async kit load completes");
    pump(50);
    const auto originalGrid = processor.getGrid();
    gh::Grid chosen;
    chosen.scenes[0].velocity[0][0] = 100;
    for (uint8_t variant : {uint8_t{0}, uint8_t{1}, uint8_t{2}})
    { chosen.scenes[0].variant[0] = variant; processor.setGrid(chosen); }
    for (int i = 0; i < 500 && processor.getView().patternLoading; ++i) pump();
    const auto latestFile = processor.exportMidi();
    std::unique_ptr<gh::Pattern> latest;
    require(latestFile.existsAsFile() && gh::loadMidi(latestFile, latest, ignored).wasOk(),
        "Latest selection exports after loading");
    require(latest->hits.size() == 1 && latest->hits[0].note == 60, "Export never substitutes an older selected pattern");
    require(latestFile.deleteFile(), "Clean test-owned export");
    processor.setGrid(originalGrid);
    for (int i = 0; i < 500 && processor.getView().patternLoading; ++i) pump();
    juce::AudioBuffer<float> buffer(2, 256); juce::MidiBuffer midi;
    processor.processBlock(buffer, midi);
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 64);
    processor.processBlock(buffer, midi);
    for (int i = 0; i < 64; ++i) require(buffer.getSample(0, i) == 0, "Sample-accurate hit offset");
    require(buffer.getRMSLevel(0, 64, 192) > 0.00001, "Processor plays factory sample");
    midi.addEvent(juce::MidiMessage::noteOff(1, 36), 0); processor.processBlock(buffer, midi);
    require(buffer.getRMSLevel(0, 0, 256) > 0.00001, "Note-off does not cut factory one-shot");
    processor.releaseResources(); processor.prepareToPlay(48000, 256);
    midi.addEvent(juce::MidiMessage::noteOn(1, gh::gateNote, 1.0f), 64);
    midi.addEvent(juce::MidiMessage::noteOff(1, gh::gateNote), 160);
    processor.processBlock(buffer, midi);
    for (int i = 0; i < 64; ++i) require(buffer.getSample(0, i) == 0, "Gate starts at exact MIDI offset");
    require(buffer.getRMSLevel(0, 80, 80) > 0.00001, "Numeric MIDI 1 plays loop");
    processor.processBlock(buffer, midi); processor.processBlock(buffer, midi);
    require(buffer.getRMSLevel(0, 0, 256) == 0, "Gate release silences loop after finite fade");
    midi.addEvent(juce::MidiMessage::noteOn(1, gh::gateNote, 1.0f), 64);
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 100);
    midi.addEvent(juce::MidiMessage::noteOff(1, gh::gateNote), 160);
    processor.processBlock(buffer, midi); processor.processBlock(buffer, midi); processor.processBlock(buffer, midi);
    require(buffer.getRMSLevel(0, 0, 256) > 0.00001, "Gate release does not cut overlapping live pad");
    set(processor, DrumProcessor::padId(0, 0), -7.5f);
    auto hostGrid = processor.getGrid(); hostGrid.stepCount = 32;
    hostGrid.scenes[15].velocity[11][31] = 89; hostGrid.scenes[15].variant[11] = 3;
    processor.setGrid(hostGrid);
    juce::MemoryBlock saved; processor.getStateInformation(saved);
    {
        const auto stateXml = juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
        require(stateXml != nullptr, "State serialises as XML");
        const auto stateTree = juce::ValueTree::fromXml(*stateXml);
        require(stateTree.hasProperty("kitLibrary") && ! stateTree.hasProperty("kit"),
            "State stores a library-relative kit path instead of an absolute path");
        require(! stateTree.hasProperty("loop"), "State never embeds an absolute loop path");
    }
    processor.setGrid(originalGrid);
    DrumProcessor restoredProcessor(catalogue.root); restoredProcessor.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    for (int i = 0; i < 500 && restoredProcessor.getView().memory == 0; ++i) pump();
    pump(50);
    require(restoredProcessor.state.getRawParameterValue(DrumProcessor::padId(0, 0))->load() == -7.5f, "Pad tuning state restores");
    const auto restoredHostGrid = restoredProcessor.getGrid();
    require(restoredHostGrid.stepCount == 32 && restoredHostGrid.scenes[15].velocity[11][31] == 89
        && restoredHostGrid.scenes[15].variant[11] == 3, "Host state restores length and last step of last scene");
    const auto saveName = "GH validation " + juce::Uuid().toString();
    processor.saveKit(saveName);
    for (int i = 0; i < 1000 && processor.getView().kitName != saveName; ++i) pump();
    require(processor.getView().kitName == saveName, "Kit save completes");
    const auto savedKit = processor.getView().kitFile;
    std::unique_ptr<gh::Kit> savedSamples;
    require(gh::loadKit(savedKit, savedSamples).wasOk() && savedSamples->values[0].tune == -7.5f, "Saved kit retains audio and pad settings");
    require(savedKit.getParentDirectory().findChildFiles(juce::File::findFiles, false, "*.wav").size() == 48, "Saved kit self-contained");
    {
        DrumProcessor reopened(catalogue.root);
        for (int i = 0; i < 500 && reopened.getView().catalogue.kits.size() <= 54; ++i) pump();
        bool found = false; for (const auto& entry : reopened.getView().catalogue.kits) if (entry.file == savedKit) found = true;
        require(found, "User kit indexed on next launch");
    }
    processor.chooseKit(catalogue.kits.front().file);
    for (int i = 0; i < 1000 && processor.getView().kitFile == savedKit; ++i) pump();
    pump(20);
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        for (const auto size : {juce::Point<int>(1060, 880), juce::Point<int>(768, 1024), juce::Point<int>(1024, 700), juce::Point<int>(360, 480)})
        {
            editor->setSize(size.x, size.y);
            juce::Image image(juce::Image::ARGB, size.x, size.y, true);
            { juce::Graphics g(image); editor->paintEntireComponent(g, false); }
            require(image.getPixelAt(0, 0).getAlpha() == 255, "Responsive editor paints");
            std::vector<juce::Rectangle<int>> knobBounds;
            for (const auto* name : {"Tune (st)", "Compressor", "Pan", "Level (dB)", "Velocity > pitch (st)", "Master saturation", "Master (dB)"})
            {
                auto* control = findControl<juce::Slider>(*editor, name);
                require(control && control->getWidth() >= 108 && control->getHeight() >= 148, "Large knobs fit every supported layout");
                const auto bounds = control->getBounds();
                for (const auto previousBounds : knobBounds) require(! previousBounds.intersects(bounds), "Knobs never overlap");
                knobBounds.push_back(bounds);
            }
            if (size.x == 1060)
            {
                for (size_t i = 1; i < knobBounds.size(); ++i)
                    require(knobBounds[i].getY() == knobBounds[0].getY() && knobBounds[i].getX() > knobBounds[i - 1].getX(),
                        "Reference knob order forms one aligned row");
                auto* filter = findControl<juce::Component>(*editor, "Low / High cut");
                require(filter && filter->getHeight() > filter->getWidth() && filter->getX() >= knobBounds.back().getRight(),
                    "Reference filter is vertical and separate from the knob row");
                const auto filterEvent = [filter](juce::Point<float> position, juce::Point<float> start, bool moved)
                {
                    const auto now = juce::Time::getCurrentTime();
                    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position,
                        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
                        filter, filter, now, start, now, 1, moved);
                };
                const juce::Point<float> low {filter->getWidth() * 0.5f - 10, static_cast<float>(filter->getHeight() - 42)};
                const juce::Point<float> high {filter->getWidth() * 0.5f + 10, 32};
                filter->mouseDown(filterEvent(low, low, false));
                filter->mouseDrag(filterEvent({low.x, 32}, low, true)); filter->mouseUp(filterEvent({low.x, 32}, low, true));
                require(processor.state.getRawParameterValue(DrumProcessor::padId(0, 4))->load() == 2000
                    && processor.state.getRawParameterValue(DrumProcessor::padId(0, 5))->load() == 20000,
                    "Vertical left handle adjusts only low cut");
                filter->mouseDown(filterEvent(high, high, false));
                filter->mouseDrag(filterEvent({high.x, low.y}, high, true)); filter->mouseUp(filterEvent({high.x, low.y}, high, true));
                require(processor.state.getRawParameterValue(DrumProcessor::padId(0, 4))->load() == 2000
                    && processor.state.getRawParameterValue(DrumProcessor::padId(0, 5))->load() == 100,
                    "Vertical right handle preserves independent crossed cutoff ranges");
                set(processor, DrumProcessor::padId(0, 4), 0); set(processor, DrumProcessor::padId(0, 5), 20000); pump(10);
                for (const auto* name : {"Tune (st)", "Compressor", "Pan", "Level (dB)", "Velocity > pitch (st)", "Master saturation", "Master (dB)"})
                {
                    auto* control = findControl<juce::Slider>(*editor, name);
                    const auto knobImage = control->createComponentSnapshot(control->getLocalBounds());
                    int ring = 0, ringTop = knobImage.getHeight(), ringBottom = 0;
                    for (int yy = 0; yy < knobImage.getHeight() - 28; ++yy)
                        for (int xx = 0; xx < knobImage.getWidth(); ++xx)
                        {
                            const auto c = knobImage.getPixelAt(xx, yy);
                            // The dial arc track is drawn in the theme's mid grey.
                            const bool grey = std::abs(c.getRed() - c.getGreen()) <= 2
                                && std::abs(c.getGreen() - c.getBlue()) <= 2
                                && c.getRed() >= 84 && c.getRed() <= 104;
                            if (grey) { ++ring; ringTop = std::min(ringTop, yy); ringBottom = std::max(ringBottom, yy); }
                        }
                    require(ring > 80 && ringBottom - ringTop >= 80,
                        "Every knob paints a large ring, including zero-valued controls (ring=" + juce::String(ring)
                        + " span=" + juce::String(ringBottom - ringTop) + ")");
                }
                { juce::Graphics graphics(image); editor->paintEntireComponent(graphics, false); }
                savePreview(image, "editor-preview.png");
            }
        }
        testPadControls(*editor, processor, buffer, midi);
        auto* open = findControl<juce::TextButton>(*editor, "Open sequencer");
        require(open != nullptr, "Sequencer open control exists");
        require(findControl<juce::Slider>(*editor, "Kick step 1 velocity") == nullptr,
            "Unused sequencer does not allocate velocity controls");
        const int originalHeight = editor->getHeight();
        open->triggerClick(); pump(30);
        juce::DocumentWindow* window = nullptr;
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (desktop.getComponent(i)->getName().startsWith("GHOST HEALER - Sequencer"))
                window = dynamic_cast<juce::DocumentWindow*>(desktop.getComponent(i));
        require(window && window->isVisible() && editor->getHeight() == originalHeight,
            "Sequencer opens in a separate window, not an expanding editor");
        const auto familyColour = [window](const juce::String& name)
        {
            auto* label = findControl<juce::Label>(*window, name);
            require(label != nullptr, "Colour-coded drum lane label exists");
            return label->findColour(juce::Label::textColourId);
        };
        juce::Array<juce::Colour> familyColours;
        for (const auto* role : {"Kick", "Snare", "Clap", "Percussion", "Closed hat", "Tom", "Shaker", "Open hat", "Crash / Ride"})
        {
            const auto colour = familyColour(role);
            require(! familyColours.contains(colour), "Every drum family has a distinct colour");
            familyColours.add(colour);
        }
        require(familyColour("Kick") == familyColour("Kick 2") && familyColour("Snare") == familyColour("Snare 2")
            && familyColour("Percussion") == familyColour("Extra perc"), "Sample variants retain their family colour");
        auto* velocity = findControl<juce::Slider>(*window, "Kick step 1 velocity");
        require(velocity != nullptr && velocity->getWidth() >= 44 && velocity->getHeight() >= 44, "Touch-sized velocity slider");
        velocity->setValue(32, juce::sendNotificationSync);
        auto edited = processor.getGrid();
        require(edited.scenes[static_cast<size_t>(edited.scene)].velocity[0][0] == 32, "Bar edits step velocity");
        juce::Image bar(juce::Image::ARGB, 44, 44, true);
        { juce::Graphics graphics(bar); velocity->paintEntireComponent(graphics, false); }
        require(bar.getPixelAt(22, 5) != bar.getPixelAt(22, 38), "Velocity is shown as filled bar height");
        velocity->setValue(0, juce::sendNotificationSync);
        edited = processor.getGrid();
        require(edited.scenes[static_cast<size_t>(edited.scene)].velocity[0][0] == 0, "Bar zero disables step");
        velocity->setValue(127, juce::sendNotificationSync);
        const auto uiGrid = processor.getGrid();
        auto* length = findControl<juce::ComboBox>(*window, "Sequence length");
        auto* lastStep = findControl<juce::Slider>(*window, "Kick step 32 velocity");
        require(length && lastStep && ! lastStep->isVisible(), "Default 16-step view hides extra steps");
        const auto stepColour = [window](int step)
        {
            auto* control = findControl<juce::Slider>(*window, "Kick step " + juce::String(step) + " velocity");
            require(control != nullptr, "Coloured step exists");
            control->setValue(64, juce::dontSendNotification);
            juce::Image image(juce::Image::ARGB, 44, 44, true);
            { juce::Graphics graphics(image); control->paintEntireComponent(graphics, false); }
            return image.getPixelAt(22, 38);
        };
        const auto blue = stepColour(4), green = stepColour(5);
        require(blue.getRed() > green.getRed()
            && blue.getRed() == blue.getGreen() && blue.getGreen() == blue.getBlue()
            && green.getRed() == green.getGreen() && green.getGreen() == green.getBlue()
            && stepColour(1) == blue && stepColour(8) == green && stepColour(9) == blue
            && stepColour(16) == green && stepColour(17) == blue && stepColour(32) == green,
            "Velocity bars alternate in four-step groups across both halves");
        length->setSelectedId(32, juce::sendNotificationSync); pump(30);
        require(processor.getGrid().stepCount == 32 && lastStep->isVisible()
            && lastStep->getWidth() >= 44 && lastStep->getParentComponent()->getWidth() > lastStep->getRight(),
            "Length selector exposes a scrollable, touch-sized 32-step grid");
        auto* gridBody = lastStep->getParentComponent();
        juce::Image longPreview(juce::Image::ARGB, gridBody->getWidth(), gridBody->getHeight(), true);
        { juce::Graphics graphics(longPreview); gridBody->paintEntireComponent(graphics, false); }
        savePreview(longPreview, "sequencer-32-preview.png");
        auto editLong = uiGrid; editLong.stepCount = 32; editLong.scenes[0].velocity = {};
        editLong.scenes[0].velocity[0][31] = 97; processor.setGrid(editLong); pump(30);
        auto* shiftRight = findControl<juce::TextButton>(*window, "Shift >");
        auto* shiftLeft = findControl<juce::TextButton>(*window, "Shift <");
        auto* clear = findControl<juce::TextButton>(*window, "Clear");
        auto* copy = findControl<juce::TextButton>(*window, "Copy");
        auto* paste = findControl<juce::TextButton>(*window, "Paste");
        require(shiftRight && shiftLeft && clear && copy && paste, "Scene edit controls exist");
        shiftRight->triggerClick(); pump(20);
        require(processor.getGrid().scenes[0].velocity[0][0] == 97
            && processor.getGrid().scenes[0].velocity[0][31] == 0, "Shift wraps step 32 to step 1");
        shiftLeft->triggerClick(); pump(20);
        require(processor.getGrid().scenes[0].velocity[0][31] == 97, "Reverse shift wraps over all 32 steps");
        copy->triggerClick(); pump(20);
        clear->triggerClick(); pump(20);
        require(processor.getGrid().scenes[0].velocity[0][31] == 0, "Clear covers active step 32");
        paste->triggerClick(); pump(20);
        require(processor.getGrid().scenes[0].velocity[0][31] == 97, "Copy/paste includes extra steps");
        length->setSelectedId(16, juce::sendNotificationSync); pump(20);
        clear->triggerClick(); pump(20);
        require(! lastStep->isVisible() && processor.getGrid().scenes[0].velocity[0][31] == 97,
            "Short-mode edits preserve hidden second half");
        length->setSelectedId(32, juce::sendNotificationSync); pump(20);
        require(lastStep->isVisible() && lastStep->getValue() == 97, "Switching back restores extra-step velocity");
        processor.setGrid(uiGrid); pump(30);
        const auto gestureGrid = processor.getGrid();
        auto* anotherStep = findControl<juce::Slider>(*window, "Kick step 2 velocity");
        require(anotherStep != nullptr, "Second step exists for editing lock test");
        const auto stepEvent = [](juce::Slider& step, juce::Point<float> position,
                                  juce::Point<float> start, int clicks, bool down, bool moved)
        {
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position,
                juce::ModifierKeys(down ? juce::ModifierKeys::leftButtonModifier : 0), 1, 0, 0, 0, 0,
                &step, &step, now, start, now, clicks, moved);
        };
        const auto tapStep = [&stepEvent](juce::Slider& step, int clicks)
        {
            const juce::Point<float> point {22, 30};
            step.mouseDown(stepEvent(step, point, point, clicks, true, false));
            step.mouseUp(stepEvent(step, point, point, clicks, false, false));
        };
        velocity->setValue(64, juce::sendNotificationSync);
        tapStep(*velocity, 1);
        require(velocity->getValue() == 0, "Single tap disables step instead of jumping velocity to pointer");
        tapStep(*velocity, 1);
        require(velocity->getValue() == 64, "Single tap re-enables step at its remembered velocity");
        tapStep(*velocity, 1); tapStep(*velocity, 2);
        require(velocity->getValue() == 64, "Double tap unlocks editing without clearing or repositioning velocity");
        const juce::Point<float> dragStart {22, 30}, dragEnd {22, 20};
        velocity->mouseDown(stepEvent(*velocity, dragStart, dragStart, 1, true, false));
        velocity->mouseDrag(stepEvent(*velocity, dragEnd, dragStart, 1, true, true));
        velocity->mouseUp(stepEvent(*velocity, dragEnd, dragStart, 1, false, true));
        require(velocity->getValue() > 64, "Unlocked step supports subsequent velocity drag");
        const auto unlockedValue = velocity->getValue();
        tapStep(*anotherStep, 1);
        velocity->mouseDown(stepEvent(*velocity, dragStart, dragStart, 1, true, false));
        velocity->mouseDrag(stepEvent(*velocity, dragEnd, dragStart, 1, true, true));
        velocity->mouseUp(stepEvent(*velocity, dragEnd, dragStart, 1, false, true));
        require(velocity->getValue() == unlockedValue, "Selecting another step locks the previous velocity slider");
        processor.setGrid(gestureGrid); pump(30);
        auto* startButton = findControl<juce::TextButton>(*window, "Play");
        auto* stopButton = findControl<juce::TextButton>(*window, "Stop");
        auto* randomButton = findControl<juce::TextButton>(*window, "Random kit");
        require(startButton && stopButton && randomButton, "Sequencer has Play, Stop and Random kit");
        processor.releaseResources(); processor.prepareToPlay(48000, 256);
        startButton->triggerClick(); pump(30);
        for (int i = 0; i < 500 && ! processor.isPreviewing(); ++i) pump();
        processor.processBlock(buffer, midi);
        require(processor.isPreviewing() && buffer.getRMSLevel(0, 0, 256) > 0.00001, "Sequencer Play renders selected grid");
        midi.addEvent(juce::MidiMessage::noteOn(1, gh::gateNote, 1.0f), 0);
        processor.processBlock(buffer, midi);
        stopButton->triggerClick(); pump(20);
        processor.processBlock(buffer, midi); processor.processBlock(buffer, midi); processor.processBlock(buffer, midi);
        require(! processor.isPreviewing() && buffer.getRMSLevel(0, 0, 256) == 0, "Stop ends preview and held-note loop");
        processor.setGrid(processor.getGrid(), true); processor.stopSequence();
        for (int i = 0; i < 500 && processor.getView().patternLoading; ++i) pump();
        processor.processBlock(buffer, midi);
        require(! processor.isPreviewing() && buffer.getRMSLevel(0, 0, 256) == 0, "Stop cancels a pending Play");
        processor.stopSequence(); processor.setGrid(processor.getGrid(), true);
        for (int i = 0; i < 500 && ! processor.isPreviewing(); ++i) pump();
        processor.processBlock(buffer, midi);
        require(buffer.getRMSLevel(0, 0, 256) > 0.00001, "Immediate Play after Stop is not swallowed by pending Stop");
        midi.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 0);
        processor.processBlock(buffer, midi);
        processor.stopSequence();
        processor.processBlock(buffer, midi); processor.processBlock(buffer, midi); processor.processBlock(buffer, midi);
        require(buffer.getRMSLevel(0, 0, 256) > 0.00001, "Explicit Stop preserves live one-shots");
        const auto beforeRandom = processor.getView().kitFile;
        randomButton->triggerClick(); pump(20);
        for (int i = 0; i < 500 && processor.getView().kitFile == beforeRandom; ++i) pump();
        require(processor.getView().kitFile != beforeRandom, "Sequencer Random kit selects another kit");
        window->setSize(600, 500);
        require(velocity->getHeight() >= 44, "Resizing preserves touch-sized grid");
        window->closeButtonPressed();
        require(! window->isVisible(), "Sequencer window closes independently");
        open->triggerClick(); pump(20);
        require(window->isVisible(), "Sequencer reopens without losing edits");
        require(velocity->getValue() == 127, "Reopening retains edited velocity");
        window->setSize(1000, 980);
        juce::Image preview(juce::Image::ARGB, window->getWidth(), window->getHeight(), true);
        { juce::Graphics graphics(preview); window->paintEntireComponent(graphics, false); }
        savePreview(preview, "sequencer-preview.png");
    }
    require(savedKit.getParentDirectory().deleteRecursively(), "Clean only test-owned saved kit");
    processor.releaseResources(); processor.prepareToPlay(48000, 256);
    constexpr int blocks = 5000;
    auto start = std::chrono::steady_clock::now();
    for (int b = 0; b < blocks; ++b) processor.processBlock(buffer, midi);
    auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << "Idle: " << elapsed / (blocks * 256.0 / 48000) * 100 << "% of one core\n";
    start = std::chrono::steady_clock::now();
    for (int b = 0; b < blocks; ++b)
    {
        if (b % 24 == 0)
            for (int note = 36; note < 44; ++note) midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0);
        processor.processBlock(buffer, midi);
    }
    elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << "Dense 8-hit playback: " << elapsed / (blocks * 256.0 / 48000) * 100 << "% of one core\n";
    std::cout << "PASS: samples, MIDI, DSP, state, responsive UI\n";
}
