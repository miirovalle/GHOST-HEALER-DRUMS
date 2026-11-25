#pragma once
#include "MidiProcessor.h"
#include "Theme.h"

class MidiRoutingEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MidiRoutingEditor(MidiRoutingProcessor&);
    ~MidiRoutingEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    MidiRoutingProcessor& processor;
    gh::theme::Look look;

    juce::Label title, subtitle, monitor;
    juce::Label inputHeading, outputHeading, signalHeading;

    juce::ToggleButton midiIn {"MIDI IN"}, midiOut {"MIDI OUT"}, midiThru {"MIDI THRU"}, cvIn {"CV IN"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> midiInAttachment, midiOutAttachment,
        midiThruAttachment, cvInAttachment;

    juce::ComboBox inChannel, outChannel, mode, curve;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> inChannelAttachment,
        outChannelAttachment, modeAttachment, curveAttachment;

    juce::Slider transpose, lowNote, highNote, velocity, cvGate, cvThreshold;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> transposeAttachment, lowNoteAttachment,
        highNoteAttachment, velocityAttachment, cvGateAttachment, cvThresholdAttachment;

    juce::Label inChannelLabel, outChannelLabel, transposeLabel, lowNoteLabel, highNoteLabel,
        velocityLabel, curveLabel, modeLabel, cvGateLabel, cvThresholdLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiRoutingEditor)
};
