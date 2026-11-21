#include "Processor.h"
#include "Editor.h"

namespace
{
constexpr std::array<const char*, 7> ids {"tune", "pan", "level", "velocityPitch", "lowCut", "highCut", "compressor"};
constexpr std::array<const char*, 7> names {"Tune", "Pan", "Level", "Velocity to pitch", "Low cut", "High cut", "Compressor"};
juce::File userRoot()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("MK Audio/Ghost Drums");
}
}
juce::String DrumProcessor::padId(int slot, int control)
{
    return "pad" + juce::String(slot + 1).paddedLeft('0', 2) + "_" + ids[static_cast<size_t>(control)];
}
juce::AudioProcessorValueTreeState::ParameterLayout DrumProcessor::layout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout result;
    const std::array<float, 7> lo {-24, -1, -60, -24, 0, 100, 0}, hi {24, 1, 6, 24, 2000, 20000, 1};
    const std::array<float, 7> initial {0, 0, 0, 0, 0, 20000, 0};
    for (int p = 0; p < gh::pads; ++p)
        for (int c = 0; c < 7; ++c)
        {
            const auto i = static_cast<size_t>(c);
            juce::NormalisableRange<float> range(lo[i], hi[i], c == 4 || c == 5 ? 1.0f : 0.01f);
            if (c == 5) range.setSkewForCentre(3000);
            if (c == 4) range.setSkewForCentre(180);
            result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{padId(p, c), 1},
                gh::noteLabel(p) + " " + names[i], range, initial[i]));
        }
    result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"master", 1}, "Master level",
        juce::NormalisableRange<float>{-36, 0, 0.1f}, -12.0f));
    result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"saturation", 1}, "Master saturation",
        juce::NormalisableRange<float>{0, 1, 0.01f}, 0.0f));
    result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"tempo", 1}, "Preview tempo",
        juce::NormalisableRange<float>{20, 300, 0.1f}, 120.0f));
    result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"midiOut", 1}, "Live MIDI output", false));
    result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"koalaRecord", 1}, "Record to Koala", false));
    return result;
}
DrumProcessor::DrumProcessor(juce::File library, std::unique_ptr<gh::MidiOutput> recordOutput,
                             gh::MidiClipboardWriter writer)
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "GhostDrums", layout()), worker(*this)
{
    for (int p = 0; p < gh::pads; ++p)
        for (int c = 0; c < 7; ++c)
            parameters[static_cast<size_t>(p)][static_cast<size_t>(c)] = state.getRawParameterValue(padId(p, c));
    masterParameter = state.getRawParameterValue("master");
    satParameter = state.getRawParameterValue("saturation");
    tempoParameter = state.getRawParameterValue("tempo");
    midiOutParameter = state.getRawParameterValue("midiOut");
    koalaRecordParameter = state.getRawParameterValue("koalaRecord");
    recordMidiOutput = std::move(recordOutput);
    clipboardWriter = std::move(writer);
#if JUCE_IOS
    if (! recordMidiOutput) recordMidiOutput = gh::makeKoalaMidiOutput();
#endif
    for (int s = 0; s < 16; ++s)
    {
        if (s % 4 == 0) grid.scenes[0].velocity[0][static_cast<size_t>(s)] = 110;
        if (s == 4 || s == 12) grid.scenes[0].velocity[2][static_cast<size_t>(s)] = 114;
        grid.scenes[0].velocity[7][static_cast<size_t>(s)] = static_cast<uint8_t>((s % 2) == 0 ? 90 : 64);
    }
    view.catalogue.root = library == juce::File{} ? gh::findBundledLibrary() : library;
    request.pattern = true;
    worker.startThread();
}
DrumProcessor::~DrumProcessor()
{
    releaseResources();
    cancelPendingUpdate();
    worker.stopThread(-1);
    delete pendingKit.exchange(nullptr); delete pendingPattern.exchange(nullptr);
    delete activeKit; delete activePattern;
    drainRetired();
}
void DrumProcessor::prepareToPlay(double rate, int)
{
    for (auto& output : midiOutputBuffers) { output.clear(); output.ensureSize((maxMidiEvents + gh::pads) * 9); }
    hostMidiFlush.clear(); hostMidiFlush.ensureSize(gh::pads * 9);
    primeMidiBuffer = true;
    midiLifecycleStop = activeMidiNotes != 0;
    engine.prepare(rate); engine.setKit(activeKit);
    clock.stop(); clock.setPattern(activePattern); gates.fill(false);
    lastPreview = false; lastPlaying = false; swapping = false;
    queuedHitCount = 0;
    stopRequested.store(false, std::memory_order_relaxed);
}
void DrumProcessor::releaseResources()
{
    if (midiRoute == 2 && recordMidiOutput && activeMidiNotes != 0)
    {
        blockMidiOutput = &midiOutputBuffers[1]; blockMidiOutput->clear();
        stopMidiNotes(0);
        recordMidiOutput->send(*blockMidiOutput, getSampleRate(), std::nullopt);
        blockMidiOutput = nullptr;
    }
    midiLifecycleStop = activeMidiNotes != 0;
    engine.reset(); clock.stop(); gates.fill(false);
    preview.store(false); lastPreview = false;
    listenPreview.store(false, std::memory_order_relaxed);
    queuedHitCount = 0;
    stopRequested.store(false, std::memory_order_relaxed);
}
bool DrumProcessor::isBusesLayoutSupported(const BusesLayout& buses) const
{
    const auto out = buses.getMainOutputChannelSet();
    return buses.getMainInputChannelSet().isDisabled()
        && (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono());
}
std::array<gh::PadValues, gh::pads> DrumProcessor::readPads() const
{
    std::array<gh::PadValues, gh::pads> result;
    for (size_t p = 0; p < result.size(); ++p)
    {
        const auto& a = parameters[p];
        auto& v = result[p];
        v.tune = a[0]->load(std::memory_order_relaxed); v.pan = a[1]->load(std::memory_order_relaxed);
        v.level = a[2]->load(std::memory_order_relaxed); v.velocityPitch = a[3]->load(std::memory_order_relaxed);
        v.lowCut = a[4]->load(std::memory_order_relaxed); v.highCut = a[5]->load(std::memory_order_relaxed);
        v.compressor = a[6]->load(std::memory_order_relaxed);
    }
    return result;
}
void DrumProcessor::retire(gh::Retired* object)
{
    if (! object) return;
    auto* head = retired.load(std::memory_order_relaxed);
    do { object->next = head; } while (! retired.compare_exchange_weak(head, object, std::memory_order_release, std::memory_order_relaxed));
}
void DrumProcessor::drainRetired()
{
    auto* object = retired.exchange(nullptr, std::memory_order_acquire);
    while (object) { auto* next = object->next; delete object; object = next; }
}
void DrumProcessor::handleMidi(const uint8_t* data, int size, int samplePosition)
{
    if (size < 3) return;
    const int type = data[0] & 0xf0, channel = data[0] & 15;
    const int note = data[1] & 127;
    if (type == 0xb0 && (note == 120 || note == 123))
    {
        gates.fill(false); clock.stop(); preview.store(false);
        stopMidiNotes(samplePosition);
        engine.fadeVoices(false, 5); queuedHitCount = 0; return;
    }
    if (type != 0x80 && type != 0x90) return;
    const bool on = type == 0x90 && data[2] > 0;
    if (note == gh::gateNote)
    {
        gates[static_cast<size_t>(channel)] = on;
        if (on) { stopMidiNotes(samplePosition); clock.start(data[2] / 127.0f, midiRoute != 2); }
        else if (std::none_of(gates.begin(), gates.end(), [](bool b) { return b; }) && ! preview.load())
        {
            clock.stop(); engine.fadeVoices(true, 5);
            stopMidiNotes(samplePosition);
            int keep = 0;
            for (int i = 0; i < queuedHitCount; ++i) if (! queuedHits[static_cast<size_t>(i)].loop) queuedHits[static_cast<size_t>(keep++)] = queuedHits[static_cast<size_t>(i)];
            queuedHitCount = keep;
        }
    }
    else if (on) trigger(note - gh::firstNote, data[2] / 127.0f);
}
void DrumProcessor::trigger(int slot, float velocity, bool loop)
{
    if (slot < 0 || slot >= gh::pads) return;
    if (! swapping) { engine.trigger(slot, velocity, loop); return; }
    if (queuedHitCount < static_cast<int>(queuedHits.size())) queuedHits[static_cast<size_t>(queuedHitCount++)] = {slot, velocity, loop};
    else droppedHits.fetch_add(1, std::memory_order_relaxed);
}
bool DrumProcessor::emitMidi(const juce::MidiMessage& message, int samplePosition)
{
    if (midiAborted) return false;
    if (midiEventCount >= maxMidiEvents)
    {
        stopMidiNotes(samplePosition);
        midiAborted = true;
        midiOverruns.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    blockMidiOutput->addEvent(message, samplePosition);
    ++midiEventCount;
    return true;
}
void DrumProcessor::stopMidiNotes(int samplePosition)
{
    if (activeMidiNotes == 0) return;
    for (size_t slot = 0; slot < midiNoteBeats.size(); ++slot)
        if (midiNoteBeats[slot] > 0)
        {
            blockMidiOutput->addEvent(juce::MidiMessage::noteOff(10, gh::firstNote + static_cast<int>(slot)), samplePosition);
            ++midiEventCount;
            midiNoteBeats[slot] = 0;
        }
    activeMidiNotes = 0;
}
void DrumProcessor::tickMidiNotes(double beatsPerSample, int samplePosition)
{
    if (activeMidiNotes == 0) return;
    for (size_t slot = 0; slot < midiNoteBeats.size(); ++slot)
        if (midiNoteBeats[slot] > 0)
        {
            const auto remaining = midiNoteBeats[slot] - beatsPerSample;
            if (remaining <= 1.0e-12)
            {
                if (! emitMidi(juce::MidiMessage::noteOff(10, gh::firstNote + static_cast<int>(slot)), samplePosition)) return;
                midiNoteBeats[slot] = 0; --activeMidiNotes;
            }
            else midiNoteBeats[slot] = remaining;
        }
}
void DrumProcessor::triggerMidiNote(int slot, float velocity, double duration, int samplePosition)
{
    if (midiAborted || slot < 0 || slot >= gh::pads) return;
    auto& remaining = midiNoteBeats[static_cast<size_t>(slot)];
    const int note = gh::firstNote + slot;
    if (remaining > 0)
    {
        if (! emitMidi(juce::MidiMessage::noteOff(10, note), samplePosition)) return;
        remaining = 0; --activeMidiNotes;
    }
    const auto value = static_cast<juce::uint8>(juce::jlimit(1, 127, juce::roundToInt(velocity * 127)));
    if (emitMidi(juce::MidiMessage::noteOn(10, note, value), samplePosition))
    { remaining = std::max(duration, 1.0e-9); ++activeMidiNotes; }
}
void DrumProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    blockMidiOutput = &midiOutputBuffers[primeMidiBuffer ? 0 : 1];
    blockMidiOutput->clear(); hostMidiFlush.clear();
    midiEventCount = 0; midiAborted = false;
    double bpm = tempoParameter->load(std::memory_order_relaxed);
    bool playing = false, haveHostPosition = false;
    std::optional<uint64_t> hostTimeNs;
    if (auto* hostClock = getPlayHead())
        if (const auto position = hostClock->getPosition())
        {
            if (const auto tempo = position->getBpm(); tempo && std::isfinite(*tempo) && *tempo > 0) bpm = *tempo;
            playing = position->getIsPlaying(); haveHostPosition = true;
            if (const auto time = position->getHostTimeNs()) hostTimeNs = *time;
        }
    const bool recordRequested = koalaRecordParameter->load(std::memory_order_relaxed) > 0.5f;
    const bool listening = listenPreview.load(std::memory_order_relaxed);
    const bool recordReady = recordMidiOutput && recordMidiOutput->isReady();
    if (recordRequested && ! lastRecordRequested && ! recordReady)
        recordUnavailable.fetch_add(1, std::memory_order_relaxed);
    lastRecordRequested = recordRequested;
    const int nextRoute = listening ? 0 : recordRequested && recordReady ? 2 : midiOutParameter->load(std::memory_order_relaxed) > 0.5f ? 1 : 0;
    const bool routeChanged = nextRoute != midiRoute;
    if (routeChanged)
    {
        stopMidiNotes(0);
        if (midiRoute == 2) recordMidiOutput->send(*blockMidiOutput, getSampleRate(), hostTimeNs);
        else if (midiRoute == 1) hostMidiFlush.addEvents(*blockMidiOutput, 0, -1, 0);
        blockMidiOutput->clear();
        if (midiRoute == 2)
        {
            clock.stop(); gates.fill(false); engine.fadeVoices(true, 5);
            if (! listening) preview.store(false, std::memory_order_relaxed);
            lastPreview = false;
        }
        midiRoute = nextRoute;
        if (midiRoute == 2)
        {
            engine.fadeVoices(true, 5);
            int keep = 0;
            for (int i = 0; i < queuedHitCount; ++i)
                if (! queuedHits[static_cast<size_t>(i)].loop) queuedHits[static_cast<size_t>(keep++)] = queuedHits[static_cast<size_t>(i)];
            queuedHitCount = keep;
        }
    }
    if (midiLifecycleStop) { stopMidiNotes(0); midiLifecycleStop = false; }
    const auto values = readPads();
    const auto master = masterParameter->load(std::memory_order_relaxed);
    const auto saturation = satParameter->load(std::memory_order_relaxed);
    engine.setValues(values, master, saturation);
    if (auto* pattern = pendingPattern.exchange(nullptr, std::memory_order_acquire))
    {
        stopMidiNotes(0);
        retire(activePattern); activePattern = pattern; clock.setPattern(pattern);
    }
    if (pendingKit.load(std::memory_order_acquire) && ! swapping)
    { engine.fadeVoices(false, 2); swapping = true; }
    const bool sequenceStopped = stopRequested.exchange(false, std::memory_order_acquire);
    if (sequenceStopped)
    {
        stopMidiNotes(0);
        gates.fill(false); clock.stop(); engine.fadeVoices(true, 5);
        lastPreview = false;
        int keep = 0;
        for (int i = 0; i < queuedHitCount; ++i)
            if (! queuedHits[static_cast<size_t>(i)].loop) queuedHits[static_cast<size_t>(keep++)] = queuedHits[static_cast<size_t>(i)];
        queuedHitCount = keep;
    }
    const bool play = preview.load(std::memory_order_relaxed);
    if (play != lastPreview)
    {
        if (play) { stopMidiNotes(0); clock.start(1, midiRoute != 2); }
        else if (std::none_of(gates.begin(), gates.end(), [](bool b) { return b; }))
        { stopMidiNotes(0); clock.stop(); engine.fadeVoices(true, 5); }
        lastPreview = play;
    }
    if (haveHostPosition)
    {
        if (midiRoute == 2)
        {
            if (! sequenceStopped && playing && (! lastPlaying || routeChanged)) { stopMidiNotes(0); clock.start(1, false); }
            if (! playing && lastPlaying)
            {
                stopMidiNotes(0); clock.stop(); gates.fill(false);
                preview.store(false, std::memory_order_relaxed); lastPreview = false;
            }
        }
        else if (lastPlaying && ! playing)
        {
            gates.fill(false);
            if (! play || midiRoute == 1)
            {
                stopMidiNotes(0); clock.stop(); engine.fadeVoices(true, 5);
                if (midiRoute == 1) { preview.store(false, std::memory_order_relaxed); lastPreview = false; }
            }
        }
        lastPlaying = playing;
    }
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    auditionFifo.prepareToRead(auditionFifo.getNumReady(), start1, size1, start2, size2);
    for (int i = 0; i < size1; ++i) trigger(auditionNotes[static_cast<size_t>(start1 + i)], 1);
    for (int i = 0; i < size2; ++i) trigger(auditionNotes[static_cast<size_t>(start2 + i)], 1);
    auditionFifo.finishedRead(size1 + size2);
    if (! engine.hasVoices() && ! clock.isActive() && midi.isEmpty() && ! swapping
        && activeMidiNotes == 0 && blockMidiOutput->isEmpty() && hostMidiFlush.isEmpty())
    { buffer.clear(); blockMidiOutput = nullptr; return; }
    const auto beatsPerSample = bpm / (60 * getSampleRate());
    auto event = midi.cbegin();
    for (int s = 0; s < buffer.getNumSamples(); ++s)
    {
        if (swapping && ! engine.hasVoices())
        {
            if (auto* kit = pendingKit.exchange(nullptr, std::memory_order_acquire))
            {
                retire(activeKit); activeKit = kit; engine.setKit(kit);
                engine.setValues(values, master, saturation);
            }
            swapping = false;
            for (int i = 0; i < queuedHitCount; ++i)
            { const auto& hit = queuedHits[static_cast<size_t>(i)]; engine.trigger(hit.slot, hit.velocity, hit.loop); }
            queuedHitCount = 0;
        }
        while (event != midi.cend() && (*event).samplePosition <= s)
        { const auto e = *event; handleMidi(e.data, e.numBytes, s); ++event; }
        tickMidiNotes(beatsPerSample, s);
        const bool wasActive = clock.isActive();
        clock.process(beatsPerSample, [this, s](int slot, float velocity, double duration)
        {
            if (midiRoute != 2) trigger(slot, velocity, true);
            if (midiRoute != 0) triggerMidiNote(slot, velocity, duration, s);
        });
        if (midiRoute == 2 && wasActive && ! clock.isActive())
        { preview.store(false, std::memory_order_relaxed); lastPreview = false; }
        float left = 0, right = 0; engine.process(left, right);
        buffer.setSample(0, s, buffer.getNumChannels() > 1 ? left : (left + right) * 0.5f);
        if (buffer.getNumChannels() > 1) buffer.setSample(1, s, right);
        for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.setSample(c, s, 0);
    }
    if (midiRoute == 2)
        recordMidiOutput->send(*blockMidiOutput, getSampleRate(), hostTimeNs);
    if (midiRoute != 1) blockMidiOutput->clear();
    blockMidiOutput->addEvents(hostMidiFlush, 0, -1, 0);
    if (! blockMidiOutput->isEmpty())
    {
        midi.swapWith(*blockMidiOutput);
        primeMidiBuffer = false;
    }
    else midi.clear();
    blockMidiOutput = nullptr;
}
DrumProcessor::View DrumProcessor::getView() const
{
    std::lock_guard<std::mutex> lock(mutex);
    auto current = view;
    current.patternReady = ! view.patternLoading && exportPattern && exportRevision == patternRevision;
    return current;
}
gh::Grid DrumProcessor::getGrid() const { std::lock_guard<std::mutex> lock(mutex); return grid; }
void DrumProcessor::setGrid(const gh::Grid& next, bool startPlayback)
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        grid = next; view.gridSource = true; request.pattern = true;
        request.loop = juce::File{}; request.loopUrl = juce::URL{};
        request.revision = ++patternRevision; view.patternLoading = true;
        if (startPlayback) startPreviewWhenReady = true;
    }
    triggerAsyncUpdate();
    worker.notify();
}
void DrumProcessor::setPreview(bool play, bool listenOnly)
{
    std::lock_guard<std::mutex> lock(mutex);
    const bool wasListening = listenPreview.exchange(play && listenOnly, std::memory_order_relaxed);
    if (! play && wasListening) stopRequested.store(true, std::memory_order_release);
    if (play && listenOnly && ! wasListening && preview.load(std::memory_order_relaxed))
        stopRequested.store(true, std::memory_order_release);
    startPreviewWhenReady = play && view.patternLoading;
    if (! startPreviewWhenReady) preview.store(play, std::memory_order_relaxed);
}
void DrumProcessor::stopSequence()
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        startPreviewWhenReady = false; preview.store(false, std::memory_order_relaxed);
        listenPreview.store(false, std::memory_order_relaxed);
    }
    stopRequested.store(true, std::memory_order_release);
    triggerAsyncUpdate();
}
void DrumProcessor::chooseKit(const juce::File& file)
{
    { std::lock_guard<std::mutex> lock(mutex); request.kit = file; }
    worker.notify();
}
void DrumProcessor::chooseLoop(const juce::File& file, bool gm, bool autoCopy)
{
    {
        std::lock_guard<std::mutex> lock(mutex); request.loop = file; request.gm = gm;
        beginLoopSelectionLocked(autoCopy);
    }
    triggerAsyncUpdate();
    worker.notify();
}
void DrumProcessor::beginLoopSelectionLocked(bool autoCopy)
{
    if (listenPreview.exchange(false, std::memory_order_relaxed))
    {
        preview.store(false, std::memory_order_relaxed); startPreviewWhenReady = false;
        stopRequested.store(true, std::memory_order_release);
    }
    request.revision = ++patternRevision; view.patternLoading = true;
    if (autoCopy) pendingClipboardRevision = request.revision;
}
void DrumProcessor::randomKit()
{
    const auto v = getView();
    if (v.catalogue.kits.empty()) { report("No kits available.", true); return; }
    std::vector<juce::File> choices;
    for (const auto& kit : v.catalogue.kits) if (kit.file != v.kitFile) choices.push_back(kit.file);
    if (choices.empty()) choices.push_back(v.catalogue.kits.front().file);
    chooseKit(choices[static_cast<size_t>(juce::Random::getSystemRandom().nextInt(static_cast<int>(choices.size())))]);
}
void DrumProcessor::stepLoop(int direction)
{
    const auto v = getView(); const auto& loops = v.catalogue.loops;
    if (loops.empty()) { report("No MIDI loops available.", true); return; }
    int index = -1;
    for (size_t i = 0; i < loops.size(); ++i) if (loops[i].file == v.loopFile) index = static_cast<int>(i);
    index = (index + direction + static_cast<int>(loops.size())) % static_cast<int>(loops.size());
    chooseLoop(loops[static_cast<size_t>(index)].file);
}
void DrumProcessor::randomLoop()
{
    const auto v = getView();
    if (v.catalogue.loops.empty()) { report("No MIDI loops available.", true); return; }
    chooseLoop(v.catalogue.loops[static_cast<size_t>(juce::Random::getSystemRandom().nextInt(static_cast<int>(v.catalogue.loops.size())))].file);
}
void DrumProcessor::setHumanized(bool enabled)
{
    {
        std::lock_guard<std::mutex> lock(mutex); view.humanized = enabled;
        humanizeSeed = static_cast<uint32_t>(juce::Random::getSystemRandom().nextInt()); request.pattern = true;
        request.revision = ++patternRevision; view.patternLoading = true;
    }
    triggerAsyncUpdate();
    worker.notify();
}
void DrumProcessor::generateGrid(bool mutate)
{
    auto next = getGrid(); auto& scene = next.scenes[static_cast<size_t>(next.scene)];
    auto& random = juce::Random::getSystemRandom();
    for (int lane = 0; lane < 12; ++lane)
        for (int step = 0; step < next.stepCount; ++step)
        {
            auto& velocity = scene.velocity[static_cast<size_t>(lane)][static_cast<size_t>(step)];
            if (mutate) { if (random.nextFloat() < 0.08f) velocity = velocity == 0 ? 95 : 0; continue; }
            float probability = 0.06f;
            if (lane == 0) probability = step % 4 == 0 ? 0.95f : 0.06f;
            if (lane == 2 || lane == 3) probability = step % 16 == 4 || step % 16 == 12 ? (lane == 2 ? 0.9f : 0.4f) : 0.03f;
            if (lane == 7 || lane == 9) probability = lane == 7 ? 0.7f : 0.25f;
            if (lane == 10) probability = step % 4 == 2 ? 0.35f : 0.03f;
            velocity = random.nextFloat() < probability ? static_cast<uint8_t>(65 + random.nextInt(55)) : 0;
        }
    setGrid(next);
}
void DrumProcessor::audition(int slot)
{
    int a = 0, n = 0, b = 0, m = 0;
    auditionFifo.prepareToWrite(1, a, n, b, m);
    if (n + m == 0) { report("Pad audition queue is full.", true); return; }
    auditionNotes[static_cast<size_t>(n != 0 ? a : b)] = slot;
    auditionFifo.finishedWrite(1);
}
void DrumProcessor::report(const juce::String& text, bool error)
{
    if (error) juce::Logger::writeToLog("Ghost Drums: " + text);
    { std::lock_guard<std::mutex> lock(mutex); view.status = text; view.error = error; }
    triggerAsyncUpdate();
}
void DrumProcessor::handleAsyncUpdate()
{
    std::array<gh::PadValues, gh::pads> next;
    bool apply = false;
    std::optional<uint64_t> copyRevision;
    { std::lock_guard<std::mutex> lock(mutex); apply = applyValues; applyValues = false; next = loadedValues; }
    if (apply)
        for (int p = 0; p < gh::pads; ++p)
        {
            const auto& v = next[static_cast<size_t>(p)];
            const std::array<float, 7> values {v.tune, v.pan, v.level, v.velocityPitch, v.lowCut, v.highCut, v.compressor};
            for (int c = 0; c < 7; ++c)
                if (auto* parameter = state.getParameter(padId(p, c)))
                    parameter->setValueNotifyingHost(parameter->convertTo0to1(values[static_cast<size_t>(c)]));
        }
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (pendingClipboardRevision)
        {
            if (*pendingClipboardRevision != patternRevision) pendingClipboardRevision.reset();
            else if (! view.patternLoading)
            {
                if (exportPattern && exportRevision == *pendingClipboardRevision) copyRevision = pendingClipboardRevision;
                pendingClipboardRevision.reset();
            }
        }
    }
    if (copyRevision) copyMidiToClipboard(copyRevision);
    sendChangeMessage();
}
void DrumProcessor::load(const juce::File& file, bool preserveValues, bool initial)
{
    std::unique_ptr<gh::Kit> kit;
    if (auto r = gh::loadKit(file, kit); r.failed()) { report(r.getErrorMessage(), true); return; }
    {
        std::lock_guard<std::mutex> lock(mutex);
        view.kitName = kit->name; view.kitFile = file; view.memory = kit->bytes;
        view.padNames = kit->padNames;
        view.importInfo = kit->importInfo;
        if (! preserveValues && ! (initial && restoring.load()))
        { loadedValues = kit->values; applyValues = true; }
    }
    delete pendingKit.exchange(kit.release(), std::memory_order_acq_rel);
    report("Kit ready: " + file.getFileNameWithoutExtension());
}
void DrumProcessor::rebuildPattern(uint64_t revision)
{
    gh::Grid current; View currentView; uint32_t seed = 0;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (revision != patternRevision) return;
        current = grid; currentView = view; seed = humanizeSeed;
    }
    std::unique_ptr<gh::Pattern> pattern;
    int ignored = 0;
    if (currentView.gridSource) pattern = gh::gridPattern(current);
    else if (auto r = gh::loadMidi(currentView.loopFile, pattern, ignored, currentView.loopGm); r.failed())
    {
        { std::lock_guard<std::mutex> lock(mutex);
          if (revision != patternRevision) return;
          view.patternLoading = false; startPreviewWhenReady = false; }
        report(r.getErrorMessage(), true); return;
    }
    if (currentView.humanized) gh::humanize(*pattern, seed, tempoParameter->load());
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (revision != patternRevision) return;
        view.loopName = pattern->name; exportPattern = gh::clonePattern(*pattern);
        exportRevision = revision;
        view.patternLoading = false;
        delete pendingPattern.exchange(pattern.release(), std::memory_order_acq_rel);
        if (startPreviewWhenReady) { preview.store(true, std::memory_order_relaxed); startPreviewWhenReady = false; }
    }
    report(ignored > 0 ? "Loop loaded; ignored " + juce::String(ignored) + " unmapped notes (outside MIDI 36-83)." : "Pattern ready.");
}
void DrumProcessor::work()
{
    gh::Catalogue catalogue;
    const auto root = getView().catalogue.root;
    if (auto r = gh::scanCatalogue(root, catalogue); r.failed()) report(r.getErrorMessage() + " Import a kit to start.", true);
    else
    {
        { std::lock_guard<std::mutex> lock(mutex); view.catalogue = catalogue; }
        load(catalogue.kits.front().file, false, true);
    }
    const auto userKits = userRoot().getChildFile("User kits").findChildFiles(juce::File::findFiles, true, "*.ghkit");
    { std::lock_guard<std::mutex> lock(mutex);
      for (const auto& file : userKits) view.catalogue.kits.push_back({file.getFileNameWithoutExtension(), "User", file}); }
    triggerAsyncUpdate();
    while (! worker.threadShouldExit())
    {
        drainRetired();
        Request next;
        { std::lock_guard<std::mutex> lock(mutex); next = request; request = {}; }
        if (const int count = droppedHits.exchange(0); count > 0)
            report("Kit-switch queue full; dropped " + juce::String(count) + " hits.", true);
        if (const int count = midiOverruns.exchange(0); count > 0)
            report("MIDI output exceeded 4096 events in " + juce::String(count)
                + " blocks; notes released and remaining output skipped.", true);
        if (recordUnavailable.exchange(0) > 0)
            report("Record to Koala is unavailable: no iPad virtual MIDI source is ready.", true);
        if (recordMidiOutput)
            if (const auto error = recordMidiOutput->takeError(); error.isNotEmpty()) report(error, true);
        if (next.importUrl.toString(false).isNotEmpty()) next.import = copyInput(next.importUrl);
        if (next.loopUrl.toString(false).isNotEmpty())
        {
            next.loop = copyInput(next.loopUrl);
            if (next.loop == juce::File{})
            {
                { std::lock_guard<std::mutex> lock(mutex);
                  if (next.revision == patternRevision) { view.patternLoading = false; startPreviewWhenReady = false; } }
                triggerAsyncUpdate();
            }
        }
        if (next.replaceUrl.toString(false).isNotEmpty()) next.replace = copyInput(next.replaceUrl);
        if (next.kit != juce::File{}) load(next.kit, next.preserveValues);
        if (next.loop != juce::File{})
        {
            std::unique_ptr<gh::Pattern> parsed; int ignored = 0;
            if (auto r = gh::loadMidi(next.loop, parsed, ignored, next.gm); r.failed())
            {
                { std::lock_guard<std::mutex> lock(mutex);
                  if (next.revision == patternRevision) { view.patternLoading = false; startPreviewWhenReady = false; } }
                report(r.getErrorMessage(), true);
            }
            else
            {
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (next.revision == patternRevision)
                    { view.loopFile = next.loop; view.gridSource = false; view.loopGm = next.gm; }
                }
                next.pattern = true;
            }
        }
        juce::String batteryImportError;
        if (next.import != juce::File{})
        {
            import(next.import);
            if (next.import.hasFileExtension("nbkt"))
            {
                const auto outcome = getView();
                if (outcome.error) batteryImportError = outcome.status;
            }
        }
        if (next.saveName.isNotEmpty()) save(next.saveName);
        if (next.replace != juce::File{}) save("Edited " + getView().kitName, next.replaceSlot, next.replace);
        if (next.pattern) rebuildPattern(next.revision);
        if (next.pattern && batteryImportError.isNotEmpty())
        {
            // A same-batch startup pattern must not hide a failed Battery import.
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (! view.error) { view.status = batteryImportError; view.error = true; }
            }
            triggerAsyncUpdate();
        }
        worker.wait(20);
    }
    drainRetired();
}
void DrumProcessor::importKit(const juce::File& file)
{
    { std::lock_guard<std::mutex> lock(mutex); request.import = file; }
    worker.notify();
}
void DrumProcessor::importKit(const juce::URL& url)
{
    { std::lock_guard<std::mutex> lock(mutex); request.importUrl = url; }
    worker.notify();
}
void DrumProcessor::chooseLoop(const juce::URL& url, bool gm, bool autoCopy)
{
    {
        std::lock_guard<std::mutex> lock(mutex); request.loopUrl = url; request.gm = gm;
        beginLoopSelectionLocked(autoCopy);
    }
    triggerAsyncUpdate();
    worker.notify();
}
void DrumProcessor::replaceSample(int slot, const juce::URL& url)
{
    { std::lock_guard<std::mutex> lock(mutex); request.replaceUrl = url; request.replaceSlot = slot; }
    worker.notify();
}
juce::File DrumProcessor::copyInput(juce::URL& url)
{
    if (! url.isLocalFile()) { report("Only local files can be imported.", true); return {}; }
    auto in = url.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress));
    if (! in || in->getTotalLength() <= 0 || in->getTotalLength() > 512LL * 1024 * 1024)
    { report("Cannot access import file, or file exceeds 512 MiB.", true); return {}; }
    const auto file = userRoot().getChildFile("Incoming/" + juce::Uuid().toString()).getChildFile(url.getLocalFile().getFileName());
    if (auto r = file.getParentDirectory().createDirectory(); r.failed()) { report(r.getErrorMessage(), true); return {}; }
    auto out = file.createOutputStream();
    if (! out || out->writeFromInputStream(*in, -1) != in->getTotalLength() || out->getStatus().failed())
    { report("Could not copy the imported file into the plugin library.", true); return {}; }
    return file;
}
void DrumProcessor::saveKit(const juce::String& name)
{
    { std::lock_guard<std::mutex> lock(mutex); request.saveName = name; }
    worker.notify();
}
void DrumProcessor::replaceSample(int slot, const juce::File& file)
{
    { std::lock_guard<std::mutex> lock(mutex); request.replace = file; request.replaceSlot = slot; }
    worker.notify();
}
void DrumProcessor::import(const juce::File& input)
{
    if (input.hasFileExtension("nbkt"))
    {
        gh::BatteryKit battery;
        if (auto r = gh::readBatteryKit(input, battery); r.failed()) { report(r.getErrorMessage(), true); return; }
        const auto banks = (battery.samples.size() + gh::pads - 1) / gh::pads;
        juce::File first;
        size_t imported = 0, bytes = 0;
        for (size_t bank = 0; bank < banks; ++bank)
        {
            std::unique_ptr<gh::Kit> kit;
            if (auto r = gh::loadBatteryBank(battery, bank, kit); r.failed())
            { report("Battery import stopped after " + juce::String(imported) + " banks: " + r.getErrorMessage(), true); return; }
            bytes += kit->bytes;
            if (bytes > 512ULL * 1024 * 1024)
            { report("Battery import exceeds 512 MiB; completed " + juce::String(imported) + " banks.", true); return; }
            juce::File saved;
            if (auto r = storeKit(*kit, kit->name, kit->values, saved); r.failed())
            { report("Battery import stopped after " + juce::String(imported) + " banks: " + r.getErrorMessage(), true); return; }
            if (imported++ == 0) first = saved;
        }
        load(first);
        if (getView().kitFile != first) return;
        report("Battery: imported " + juce::String(imported) + " bank(s); "
            + juce::String(battery.omittedZones) + " extra zones and " + juce::String(battery.hiddenCells)
            + " hidden cells omitted.");
        return;
    }
    auto folder = input;
    if (input.hasFileExtension("zip"))
    {
        folder = userRoot().getChildFile("Imports/" + juce::Uuid().toString());
        if (auto r = folder.createDirectory(); r.failed()) { report(r.getErrorMessage(), true); return; }
        juce::ZipFile zip(input);
        juce::int64 total = 0;
        if (zip.getNumEntries() < 1 || zip.getNumEntries() > 10000) { report("Invalid or excessively large kit ZIP.", true); return; }
        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            const auto* entry = zip.getEntry(i);
            const auto path = entry->filename.replaceCharacter('\\', '/');
            total += entry->uncompressedSize;
            if (entry->isSymbolicLink || entry->uncompressedSize < 0 || total > 512LL * 1024 * 1024
                || juce::File::isAbsolutePath(path) || ! folder.getChildFile(path).isAChildOf(folder))
            { report("Unsafe path in kit ZIP.", true); return; }
        }
        if (auto r = zip.uncompressTo(folder); r.failed()) { report(r.getErrorMessage(), true); return; }
    }
    const auto importDefinition = [this](const juce::File& definition)
    {
        std::unique_ptr<gh::Kit> kit;
        if (auto r = gh::loadKit(definition, kit); r.failed()) { report(r.getErrorMessage(), true); return; }
        juce::File saved;
        if (auto r = storeKit(*kit, kit->name, kit->values, saved); r.failed()) { report(r.getErrorMessage(), true); return; }
        load(saved);
    };
    if (input.hasFileExtension("ghkit") && input.existsAsFile()) { importDefinition(input); return; }
    const auto definitions = folder.findChildFiles(juce::File::findFiles, true, "*.ghkit");
    if (! definitions.isEmpty())
    {
        for (const auto& file : definitions) importDefinition(file);
        return;
    }
    if (! folder.isDirectory()) { report("Import a kit folder, .ghkit, .nbkt or .zip file.", true); return; }
    auto samples = folder.findChildFiles(juce::File::findFiles, false, "*.wav");
    if (samples.isEmpty())
    {
        const auto children = folder.findChildFiles(juce::File::findDirectories, false);
        if (children.size() == 1)
        { folder = children.getFirst(); samples = folder.findChildFiles(juce::File::findFiles, false, "*.wav"); }
    }
    samples.sort();
    if (samples.size() != gh::pads)
    { report("Import a folder with 48 numbered WAVs, or a portable .ghkit/.zip kit.", true); return; }
    gh::Kit kit; kit.name = folder.getFileName();
    for (int i = 0; i < gh::pads; ++i)
    {
        const auto& file = samples.getReference(i);
        if (file.getFileName().substring(0, 2).getIntValue() != i + 1)
        { report("Number imported samples 01-48 in MIDI slot order.", true); return; }
        if (auto r = kit.samples[static_cast<size_t>(i)].load(file); r.failed()) { report(r.getErrorMessage(), true); return; }
    }
    juce::File saved;
    if (auto r = storeKit(kit, kit.name, kit.values, saved); r.failed()) { report(r.getErrorMessage(), true); return; }
    load(saved);
}
void DrumProcessor::save(const juce::String& name, int replaceSlot, const juce::File& replacement)
{
    const auto info = getView();
    if (info.kitFile == juce::File{}) { report("Load a kit before saving.", true); return; }
    std::unique_ptr<gh::Kit> kit;
    if (auto r = gh::loadKit(info.kitFile, kit); r.failed()) { report(r.getErrorMessage(), true); return; }
    if (replaceSlot >= 0 && replaceSlot < gh::pads)
        if (auto r = kit->samples[static_cast<size_t>(replaceSlot)].load(replacement); r.failed())
        { report(r.getErrorMessage(), true); return; }
    juce::File saved;
    if (auto r = storeKit(*kit, name, readPads(), saved); r.failed()) { report(r.getErrorMessage(), true); return; }
    load(saved);
}
juce::Result DrumProcessor::storeKit(const gh::Kit& kit, const juce::String& name,
                                   const std::array<gh::PadValues, gh::pads>& values, juce::File& saved)
{
    const auto folder = userRoot().getChildFile("User kits/" + juce::Uuid().toString());
    if (folder.exists()) return juce::Result::fail("The new kit folder already exists.");
    if (auto r = folder.createDirectory(); r.failed()) return r;
    const auto failed = [&folder](const juce::String& error)
    {
        return juce::Result::fail(error + (folder.deleteRecursively() ? juce::String()
            : " Could not remove incomplete kit folder: " + folder.getFullPathName()));
    };
    auto* object = new juce::DynamicObject;
    object->setProperty("version", 1); object->setProperty("name", name);
    if (kit.importInfo.isNotEmpty()) object->setProperty("importInfo", kit.importInfo);
    juce::Array<juce::var> pads;
    for (int i = 0; i < gh::pads; ++i)
    {
        const auto file = juce::String(i + 1).paddedLeft('0', 2) + ".wav";
        const auto wav = gh::encodeWav(kit.samples[static_cast<size_t>(i)]);
        if (! folder.getChildFile(file).replaceWithData(wav.getData(), wav.getSize()))
        { delete object; return failed("Could not save kit sample " + file); }
        auto* pad = new juce::DynamicObject; pad->setProperty("sample", file);
        if (kit.padNames[static_cast<size_t>(i)].isNotEmpty())
            pad->setProperty("name", kit.padNames[static_cast<size_t>(i)]);
        const auto& v = values[static_cast<size_t>(i)];
        const std::array<float, 7> a {v.tune, v.pan, v.level, v.velocityPitch, v.lowCut, v.highCut, v.compressor};
        for (int c = 0; c < 7; ++c) pad->setProperty(ids[static_cast<size_t>(c)], a[static_cast<size_t>(c)]);
        pads.add(juce::var(pad));
    }
    object->setProperty("pads", pads);
    const auto file = folder.getChildFile(juce::File::createLegalFileName(name) + ".ghkit");
    if (! file.replaceWithText(juce::JSON::toString(juce::var(object)))) return failed("Could not save kit definition.");
    { std::lock_guard<std::mutex> lock(mutex); view.catalogue.kits.push_back({name, "User", file}); }
    saved = file;
    return juce::Result::ok();
}
juce::File DrumProcessor::exportMidi()
{
    std::unique_ptr<gh::Pattern> pattern;
    bool loading = false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        loading = view.patternLoading;
        if (! loading && exportPattern && exportRevision == patternRevision) pattern = gh::clonePattern(*exportPattern);
    }
    if (loading) { report("The selected loop is still loading. Try MIDI export again when ready.", true); return {}; }
    if (! pattern) { report("The selected pattern is not ready to export.", true); return {}; }
    return writeMidiExport(*pattern, tempoParameter->load());
}
juce::File DrumProcessor::exportLoopMidi(const juce::File& source)
{
    std::unique_ptr<gh::Pattern> pattern;
    int ignored = 0;
    if (auto result = gh::loadMidi(source, pattern, ignored); result.failed())
    { report(result.getErrorMessage(), true); return {}; }
    bool humanized = false; uint32_t seed = 0;
    {
        std::lock_guard<std::mutex> lock(mutex);
        humanized = view.humanized; seed = humanizeSeed;
    }
    const auto bpm = tempoParameter->load();
    if (humanized) gh::humanize(*pattern, seed, bpm);
    const auto file = writeMidiExport(*pattern, bpm);
    if (file.existsAsFile() && ignored > 0)
        report("MIDI drag snapshot omitted " + juce::String(ignored) + " unmapped notes (outside MIDI 36-83).");
    return file;
}
juce::File DrumProcessor::writeMidiExport(const gh::Pattern& pattern, double bpm)
{
    const auto folder = userRoot().getChildFile("MIDI exports");
    if (auto r = folder.createDirectory(); r.failed()) { report(r.getErrorMessage(), true); return {}; }
    const auto file = folder.getChildFile(juce::File::createLegalFileName(pattern.name) + "-" + juce::Uuid().toString() + ".mid");
    if (auto r = gh::writeMidi(pattern, file, bpm); r.failed()) { report(r.getErrorMessage(), true); return {}; }
    report("MIDI file ready. Drop into your host's playlist, piano roll or sequence slot.");
    return file;
}
bool DrumProcessor::copyMidiToClipboard(std::optional<uint64_t> expectedRevision)
{
    std::unique_ptr<gh::Pattern> pattern;
    bool loading = false, superseded = false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        loading = view.patternLoading;
        superseded = expectedRevision && *expectedRevision != patternRevision;
        if (! loading && ! superseded && exportPattern && exportRevision == patternRevision)
            pattern = gh::clonePattern(*exportPattern);
    }
    if (superseded) { report("MIDI auto-copy cancelled: the selection changed."); return false; }
    if (! pattern)
    {
        report(loading ? "The selected MIDI loop is still loading; nothing was copied."
                       : "The selected MIDI loop is not ready; nothing was copied.", true);
        return false;
    }
    juce::MemoryOutputStream output;
    if (auto result = gh::writeMidi(*pattern, output, tempoParameter->load()); result.failed())
    { report(result.getErrorMessage(), true); return false; }
    if (! clipboardWriter) { report("MIDI clipboard output is unavailable.", true); return false; }
    if (auto result = clipboardWriter(output.getMemoryBlock()); result.failed())
    { report(result.getErrorMessage(), true); return false; }
#if JUCE_WINDOWS
    report("MIDI copied: " + pattern->name + ". FL Studio: Piano roll > File > Paste from MIDI clipboard (Shift+Ctrl+V).");
#else
    report("MIDI copied: " + pattern->name + ". Paste requires MIDI clipboard support in the receiving app.");
#endif
    return true;
}
std::optional<juce::String> DrumProcessor::getNameForMidiNoteNumber(int note, int)
{
    if (note == gh::gateNote) return "Loop gate (MIDI 1)";
    if (note < gh::firstNote || note >= gh::firstNote + gh::pads) return std::nullopt;
    const int pad = note - gh::firstNote;
    return gh::noteLabel(pad) + " " + gh::roles[static_cast<size_t>(pad % 12)];
}
void DrumProcessor::getStateInformation(juce::MemoryBlock& memory)
{
    auto tree = state.copyState(); const auto v = getView();
    uint32_t seed = 0;
    { std::lock_guard<std::mutex> lock(mutex); seed = humanizeSeed; }
    // Store portable relative paths for library/user files so shared projects
    // and presets never embed this machine's account-name path.
    const auto storeFile = [&tree](const juce::String& key, const juce::File& file,
                                   const juce::File& libraryRoot, const juce::File& userRootDir)
    {
        tree.removeProperty(key, nullptr);
        tree.removeProperty(key + "Library", nullptr);
        tree.removeProperty(key + "User", nullptr);
        if (file == juce::File{}) return;
        if (file.isAChildOf(libraryRoot)) { tree.setProperty(key + "Library", file.getRelativePathFrom(libraryRoot), nullptr); return; }
        if (file.isAChildOf(userRootDir)) { tree.setProperty(key + "User", file.getRelativePathFrom(userRootDir), nullptr); return; }
        tree.setProperty(key, file.getFullPathName(), nullptr);
    };
    storeFile("kit", v.kitFile, v.catalogue.root, userRoot());
    storeFile("loop", v.loopFile, v.catalogue.root, userRoot());
    tree.setProperty("grid", juce::JSON::toString(gh::gridToVar(getGrid())), nullptr);
    tree.setProperty("gridSource", v.gridSource, nullptr); tree.setProperty("humanized", v.humanized, nullptr);
    tree.setProperty("humanizeSeed", static_cast<juce::int64>(seed), nullptr);
    tree.setProperty("loopGm", v.loopGm, nullptr);
    if (auto xml = tree.createXml()) copyXmlToBinary(*xml, memory);
}
void DrumProcessor::setStateInformation(const void* data, int size)
{
    auto xml = getXmlFromBinary(data, size);
    if (! xml || ! xml->hasTagName("GhostDrums")) { report("Rejected invalid plugin state.", true); return; }
    auto tree = juce::ValueTree::fromXml(*xml);
    gh::Grid restored; juce::var gridData;
    if (auto r = juce::JSON::parse(tree["grid"].toString(), gridData); r.failed())
    { report("Rejected invalid sequencer state.", true); return; }
    if (auto r = gh::gridFromVar(gridData, restored); r.failed()) { report(r.getErrorMessage(), true); return; }
    restoring.store(true);
    state.replaceState(tree);
    {
        std::lock_guard<std::mutex> lock(mutex);
        grid = restored; view.gridSource = static_cast<bool>(tree["gridSource"]);
        view.humanized = static_cast<bool>(tree["humanized"]);
        view.loopGm = static_cast<bool>(tree["loopGm"]);
        humanizeSeed = static_cast<uint32_t>(static_cast<juce::int64>(tree["humanizeSeed"]));
        applyValues = false; request.preserveValues = true;
        const auto resolve = [this, &tree](const char* kind)
        {
            const juce::String key(kind);
            if (tree.hasProperty(key + "Library")) return view.catalogue.root.getChildFile(tree[key + "Library"].toString());
            if (tree.hasProperty(key + "User")) return userRoot().getChildFile(tree[key + "User"].toString());
            return tree[key].toString().isEmpty() ? juce::File{} : juce::File(tree[key].toString());
        };
        request.kit = resolve("kit"); view.loopFile = resolve("loop"); request.pattern = true;
        request.revision = ++patternRevision; view.patternLoading = true;
    }
    worker.notify();
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DrumProcessor(); }
