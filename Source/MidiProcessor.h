#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "MidiRouting.h"

/**
    GHOST HEALER DRUMS MIDI - the routing hub for the drum instrument.

    MIDI IN is filtered by channel, note range and velocity, then forwarded as
    remapped MIDI OUT and/or an untouched MIDI THRU copy. CV mode replaces the
    stereo audio output with 1 V/oct pitch and gate CV, and CV IN can convert a
    DC-coupled audio input back into MIDI notes.
*/
class MidiRoutingProcessor final : public juce::AudioProcessor
{
public:
    MidiRoutingProcessor();
    ~MidiRoutingProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "MIDI"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    struct Monitor
    {
        int inCount = 0, outCount = 0;
        int lastInNote = -1, lastInVelocity = 0, lastInChannel = 0;
        int lastOutNote = -1, lastOutVelocity = 0, lastOutChannel = 0;
    };
    Monitor getMonitor() const;
    bool isCvMode() const { return cvMode.load(std::memory_order_relaxed); }

    juce::AudioProcessorValueTreeState state;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void readSettings();
    void noteActivity(const juce::MidiMessage&, bool outgoing);

    gh::midi::Router router;
    gh::midi::CvOutput cvOutput;
    gh::midi::CvInput cvInput;
    bool needsRelease = false;

    std::atomic<float>* midiInParameter = nullptr;
    std::atomic<float>* inChannelParameter = nullptr;
    std::atomic<float>* midiOutParameter = nullptr;
    std::atomic<float>* outChannelParameter = nullptr;
    std::atomic<float>* midiThruParameter = nullptr;
    std::atomic<float>* transposeParameter = nullptr;
    std::atomic<float>* lowNoteParameter = nullptr;
    std::atomic<float>* highNoteParameter = nullptr;
    std::atomic<float>* velocityParameter = nullptr;
    std::atomic<float>* velocityCurveParameter = nullptr;
    std::atomic<float>* modeParameter = nullptr;
    std::atomic<float>* cvGateParameter = nullptr;
    std::atomic<float>* cvToMidiParameter = nullptr;
    std::atomic<float>* cvThresholdParameter = nullptr;

    int outChannel = gh::midi::defaultChannel, transpose = 0;
    bool cvOutEnabled = false, cvInEnabled = false;
    std::atomic<bool> cvMode {false};
    std::atomic<int> midiInCount {0}, midiOutCount {0}, lastInNote {-1}, lastInVelocity {0},
                     lastInChannel {0}, lastOutNote {-1}, lastOutVelocity {0}, lastOutChannel {0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiRoutingProcessor)
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();
