#include "MidiProcessor.h"
#include <iostream>

/**
    Offline checks for GHOST HEALER DRUMS MIDI.

    The routing hub is a separate plugin target, so it cannot be linked into
    GhostTests without duplicating the JUCE plugin client. These checks create
    the processor and its editor directly, which also proves every editor
    attachment binds to a real parameter.
*/
namespace
{
void require(bool ok, const char* message)
{
    if (! ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
void set(MidiRoutingProcessor& p, const juce::String& id, float value)
{
    auto* parameter = p.state.getParameter(id);
    require(parameter != nullptr, "Parameter exists");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
int countNoteOns(const juce::MidiBuffer& buffer)
{
    int count = 0;
    for (const auto event : buffer) if (event.getMessage().isNoteOn()) ++count;
    return count;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    MidiRoutingProcessor processor;
    processor.setRateAndBufferSizeDetails(48000, 256);
    processor.prepareToPlay(48000, 256);

    juce::AudioBuffer<float> audio(2, 256);
    juce::MidiBuffer midi;
    const auto settle = [&]
    {
        // Routing changes release every held note, so drain that first.
        audio.clear();
        midi.clear();
        processor.processBlock(audio, midi);
    };

    // Defaults: output on channel 10 plus an untouched thru copy.
    audio.clear();
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 40, 0.9f), 7);
    processor.processBlock(audio, midi);
    require(midi.getNumEvents() == 2, "Default settings emit output and thru");
    require((*midi.cbegin()).samplePosition == 7, "Routed events keep their sample position");
    require(processor.getMonitor().inCount == 1 && processor.getMonitor().outCount == 2,
        "Monitor counts incoming and outgoing notes");

    // Channel filter drops everything else.
    set(processor, "inChannel", 1);
    audio.clear();
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(2, 40, 0.9f), 0);
    processor.processBlock(audio, midi);
    require(midi.isEmpty(), "Input channel filter drops other channels");

    // Transpose and channel remap.
    set(processor, "inChannel", 0);
    set(processor, "outChannel", 2);
    set(processor, "transpose", 12);
    set(processor, "midiThru", 0);
    settle();
    audio.clear();
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 40, 0.9f), 3);
    processor.processBlock(audio, midi);
    require(midi.getNumEvents() == 1, "Thru can be disabled independently");
    const auto moved = (*midi.cbegin()).getMessage();
    require(moved.getChannel() == 2 && moved.getNoteNumber() == 52, "Output channel and transpose apply");

    // Note range filter.
    set(processor, "lowNote", 60);
    settle();
    audio.clear();
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 40, 0.9f), 0);
    processor.processBlock(audio, midi);
    require(midi.isEmpty(), "Note range filter drops low notes");
    set(processor, "lowNote", 0);

    // CV output replaces audio with pitch and gate CV.
    set(processor, "transpose", 0);
    set(processor, "mode", 1);
    audio.clear();
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 72, 1.0f), 0);
    processor.processBlock(audio, midi);
    require(std::abs(audio.getSample(0, 128) - 1.0f) < 1.0e-4f, "CV mode writes 1 V/oct pitch");
    require(audio.getSample(1, 128) > 0.5f, "CV mode writes a gate");
    require(processor.isCvMode(), "Monitor reports CV mode");

    // CV input becomes MIDI.
    set(processor, "mode", 0);
    set(processor, "cvToMidi", 1);
    set(processor, "midiThru", 0);
    set(processor, "outChannel", 10);
    audio.clear();
    for (int s = 0; s < 256; ++s)
    {
        audio.setSample(0, s, gh::midi::noteToVolts(48));
        audio.setSample(1, s, s < 100 ? 0.8f : 0.0f);
    }
    midi.clear();
    processor.processBlock(audio, midi);
    bool sawNoteOn = false, sawNoteOff = false;
    for (const auto event : midi)
    {
        const auto message = event.getMessage();
        if (message.isNoteOn() && message.getNoteNumber() == 48) sawNoteOn = true;
        if (message.isNoteOff() && message.getNoteNumber() == 48) sawNoteOff = true;
    }
    require(sawNoteOn && sawNoteOff, "CV input converts gate and pitch into MIDI notes");

    // Editor: every attachment resolves, and the panel renders.
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    require(editor != nullptr, "MIDI plugin editor exists");
    editor->setSize(460, 700);
    juce::Image image(juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
    { juce::Graphics graphics(image); editor->paintEntireComponent(graphics, false); }
    const auto preview = juce::File::getCurrentWorkingDirectory().getChildFile("midi-plugin-preview.png");
    preview.deleteFile();
    if (auto stream = preview.createOutputStream())
    {
        juce::PNGImageFormat().writeImageToStream(image, *stream);
        require(stream->getStatus().wasOk(), "MIDI plugin preview written");
    }
    require(preview.existsAsFile(), "MIDI plugin preview exists");

    // State round-trip keeps the routing settings.
    juce::MemoryBlock state;
    processor.getStateInformation(state);
    set(processor, "transpose", -5);
    processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(std::abs(processor.state.getRawParameterValue("transpose")->load()) < 0.001f,
        "MIDI plugin state restores the stored routing");

    std::cout << "PASS: MIDI plugin routing, CV conversion, state and editor\n";
    return 0;
}
