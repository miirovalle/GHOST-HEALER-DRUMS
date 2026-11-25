#pragma once
#include "Engine.h"
#include "MidiOutput.h"
#include "MidiClipboard.h"
#include "MidiRouting.h"
#include <mutex>

class DrumProcessor final : public juce::AudioProcessor,
                            public juce::ChangeBroadcaster,
                            private juce::AsyncUpdater
{
public:
    explicit DrumProcessor(juce::File library = {}, std::unique_ptr<gh::MidiOutput> recordOutput = {},
                           gh::MidiClipboardWriter clipboardWriter = gh::copyMidiToClipboard);
    ~DrumProcessor() override;
    static juce::String midiNoteId(int slot);
    void prepareToPlay(double, int) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 30; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Ghost"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    std::optional<juce::String> getNameForMidiNoteNumber(int, int) override;

    struct View
    {
        gh::Catalogue catalogue;
        std::array<juce::String, gh::pads> padNames;
        juce::String kitName, loopName, status, importInfo;
        juce::File kitFile, loopFile;
        size_t memory = 0;
        bool error = false, humanized = false, gridSource = true, loopGm = false, patternLoading = true, patternReady = false;
    };
    /** Last MIDI seen by the audio thread, for the MIDI activity display. */
    struct Monitor
    {
        int inCount = 0, outCount = 0, triggerCount = 0;
        int lastInNote = -1, lastInChannel = 0, lastInVelocity = 0;
        int lastOutNote = -1, lastOutChannel = 0;
    };

    View getView() const;
    Monitor getMidiMonitor() const;
    bool isCvMode() const { return cvMode.load(std::memory_order_relaxed); }
    gh::Grid getGrid() const;
    void setGrid(const gh::Grid&, bool startPlayback = false);
    void chooseKit(const juce::File&);
    void chooseLoop(const juce::File&, bool gm = false, bool autoCopy = false);
    void randomKit();
    void stepLoop(int direction);
    void randomLoop();
    void setHumanized(bool);
    void generateGrid(bool mutate);
    void audition(int slot);
    void setPreview(bool play, bool listenOnly = false);
    void stopSequence();
    bool isPreviewing() const { return preview.load(std::memory_order_relaxed); }
    bool isListening() const { return listenPreview.load(std::memory_order_relaxed); }
    bool isHosted() const { return wrapperType != wrapperType_Undefined && wrapperType != wrapperType_Standalone; }
    void importKit(const juce::File&);
    void importKit(const juce::URL&);
    void chooseLoop(const juce::URL&, bool gm = false, bool autoCopy = false);
    void saveKit(const juce::String& name);
    void replaceSample(int slot, const juce::File&);
    void replaceSample(int slot, const juce::URL&);
    juce::File exportMidi();
    juce::File exportLoopMidi(const juce::File&);
    bool copyMidiToClipboard(std::optional<uint64_t> expectedRevision = std::nullopt);
    void report(const juce::String&, bool error = false);
    static juce::String padId(int slot, int control);
    juce::AudioProcessorValueTreeState state;

private:
    struct Worker final : juce::Thread
    {
        explicit Worker(DrumProcessor& p) : Thread("Ghost library loader"), owner(p) {}
        void run() override { owner.work(); }
        DrumProcessor& owner;
    } worker;
    struct Request
    {
        juce::File kit, loop, import, replace;
        juce::URL importUrl, loopUrl, replaceUrl;
        juce::String saveName;
        int replaceSlot = -1;
        bool pattern = false, preserveValues = false, gm = false;
        uint64_t revision = 0;
    };
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void handleAsyncUpdate() override;
    void work();
    void rebuildPattern(uint64_t revision);
    void beginLoopSelectionLocked(bool autoCopy);
    juce::File writeMidiExport(const gh::Pattern&, double bpm);
    void load(const juce::File&, bool preserveValues = false, bool initial = false);
    void import(const juce::File&);
    void save(const juce::String&, int slot = -1, const juce::File& replacement = {});
    juce::Result storeKit(const gh::Kit&, const juce::String&, const std::array<gh::PadValues, gh::pads>&, juce::File& saved);
    juce::File copyInput(juce::URL&);
    void retire(gh::Retired*);
    void drainRetired();
    void handleMidi(const uint8_t*, int, int samplePosition);
    void readMidiSettings();
    void trigger(int, float, bool loop = false, double durationSeconds = -1.0);
    bool emitMidi(const juce::MidiMessage&, int samplePosition);
    void stopMidiNotes(int samplePosition);
    void flushThruNotes(int samplePosition);
    void tickMidiNotes(double beatsPerSample, int samplePosition);
    void triggerMidiNote(int slot, float velocity, double duration, int samplePosition);
    std::array<gh::PadValues, gh::pads> readPads() const;
    std::array<std::array<std::atomic<float>*, 7>, gh::pads> parameters {};
    std::atomic<float>* masterParameter = nullptr;
    std::atomic<float>* satParameter = nullptr;
    std::atomic<float>* tempoParameter = nullptr;
    std::atomic<float>* midiOutParameter = nullptr;
    std::atomic<float>* koalaRecordParameter = nullptr;
    std::atomic<float>* midiInParameter = nullptr;
    std::atomic<float>* midiInChannelParameter = nullptr;
    std::atomic<float>* midiOutChannelParameter = nullptr;
    std::atomic<float>* midiThruParameter = nullptr;
    std::atomic<float>* midiVelocityCurveParameter = nullptr;
    std::atomic<float>* midiCvModeParameter = nullptr;
    std::atomic<float>* cvGateParameter = nullptr;
    std::atomic<float>* midiOutPadsParameter = nullptr;
    std::array<std::atomic<float>*, gh::pads> midiNoteParameters {};
    std::unique_ptr<gh::MidiOutput> recordMidiOutput;
    static constexpr int maxMidiEvents = 4096;
    std::array<juce::MidiBuffer, 2> midiOutputBuffers;
    juce::MidiBuffer hostMidiFlush;
    juce::MidiBuffer* blockMidiOutput = nullptr;
    struct SentNote { int note = -1, channel = 0; double beats = 0; };
    std::array<SentNote, gh::pads> sentNotes {};
    gh::midi::NoteMap noteMap;
    gh::midi::Router thruRouter;
    gh::midi::CvOutput cv;
    int activeMidiNotes = 0, midiEventCount = 0, midiRoute = 0;
    int outChannel = gh::midi::defaultChannel, inChannel = 0, thruMode = 0, velocityCurve = 0;
    bool midiInEnabled = true, triggerFromMidi = true, padsMidiOut = false;
    std::atomic<bool> cvMode {false};
    std::atomic<int> midiInCount {0}, midiOutCount {0}, midiTriggerCount {0}, lastInNote {-1}, lastInChannel {0},
                     lastInVelocity {0}, lastOutNote {-1}, lastOutChannel {0};
    bool primeMidiBuffer = true, midiAborted = false, midiLifecycleStop = false, lastRecordRequested = false;
    std::atomic<int> midiOverruns {0}, recordUnavailable {0};
    mutable std::mutex mutex;
    Request request;
    View view;
    gh::Grid grid;
    std::unique_ptr<gh::Pattern> exportPattern;
    gh::MidiClipboardWriter clipboardWriter;
    std::optional<uint64_t> pendingClipboardRevision;
    uint64_t exportRevision = 0;
    std::array<gh::PadValues, gh::pads> loadedValues {};
    bool applyValues = false;
    bool startPreviewWhenReady = false;
    uint32_t humanizeSeed = 12345;
    uint64_t patternRevision = 0;
    std::atomic<gh::Kit*> pendingKit {nullptr};
    std::atomic<gh::Pattern*> pendingPattern {nullptr};
    std::atomic<gh::Retired*> retired {nullptr};
    gh::Kit* activeKit = nullptr;
    gh::Pattern* activePattern = nullptr;
    gh::Engine engine;
    gh::Clock clock;
    std::atomic<bool> preview {false};
    std::atomic<bool> listenPreview {false};
    std::atomic<bool> stopRequested {false};
    std::atomic<bool> restoring {false};
    struct QueuedHit { int slot; float velocity; bool loop; double seconds; };
    std::array<QueuedHit, 128> queuedHits {};
    int queuedHitCount = 0;
    std::atomic<int> droppedHits {0};
    bool lastPreview = false, swapping = false, lastPlaying = false;
    std::array<bool, 16> gates {};
    juce::AbstractFifo auditionFifo {128};
    std::array<int, 128> auditionNotes {};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumProcessor)
};
