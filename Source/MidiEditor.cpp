#include "MidiEditor.h"

namespace
{
void caption(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setFont(gh::theme::font(13.0f));
    label.setColour(juce::Label::textColourId, gh::theme::textLight);
    label.setJustificationType(juce::Justification::centredRight);
}

void heading(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setFont(gh::theme::font(15.0f, true));
    label.setColour(juce::Label::textColourId, gh::theme::textLight);
    label.setJustificationType(juce::Justification::centredLeft);
}

void slider(juce::Slider& s, double min, double max, double step, const juce::String& suffix)
{
    s.setSliderStyle(juce::Slider::LinearHorizontal);
    s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 24);
    s.setRange(min, max, step);
    s.setTextValueSuffix(suffix);
}

juce::String noteName(int note)
{
    return note < 0 ? juce::String("-") : juce::MidiMessage::getMidiNoteName(note, true, true, 3);
}
}

MidiRoutingEditor::MidiRoutingEditor(MidiRoutingProcessor& p)
    : AudioProcessorEditor(p), processor(p)
{
    look.setColour(juce::ResizableWindow::backgroundColourId, gh::theme::back);
    setLookAndFeel(&look);
    look.setPopupsStayInsideEditor(processor.wrapperType == juce::AudioProcessor::wrapperType_AudioUnitv3);

    title.setText("GHOST HEALER DRUMS MIDI", juce::dontSendNotification);
    title.setFont(gh::theme::font(20.0f, true));
    title.setColour(juce::Label::textColourId, gh::theme::textLight);
    subtitle.setText("MIDI in / out / thru routing hub for GHOST HEALER DRUMS", juce::dontSendNotification);
    subtitle.setFont(gh::theme::font(12.0f));
    subtitle.setColour(juce::Label::textColourId, gh::theme::textDim);

    for (auto* component : std::initializer_list<juce::Component*>{
        &title, &subtitle, &monitor, &inputHeading, &outputHeading, &signalHeading,
        &midiIn, &midiOut, &midiThru, &cvIn, &inChannel, &outChannel, &mode, &curve,
        &transpose, &lowNote, &highNote, &velocity, &cvGate, &cvThreshold,
        &inChannelLabel, &outChannelLabel, &transposeLabel, &lowNoteLabel, &highNoteLabel,
        &velocityLabel, &curveLabel, &modeLabel, &cvGateLabel, &cvThresholdLabel})
        addAndMakeVisible(component);

    heading(inputHeading, "INPUT");
    heading(outputHeading, "OUTPUT");
    heading(signalHeading, "SIGNAL MODE / CV");
    caption(inChannelLabel, "MIDI channel"); caption(outChannelLabel, "MIDI channel");
    caption(transposeLabel, "Transpose"); caption(lowNoteLabel, "Note range low");
    caption(highNoteLabel, "Note range high"); caption(velocityLabel, "Velocity scale");
    caption(curveLabel, "Velocity curve"); caption(modeLabel, "Signal mode");
    caption(cvGateLabel, "CV gate time"); caption(cvThresholdLabel, "CV gate threshold");

    midiIn.setTooltip("Receive MIDI from Drambo, a sequencer, a controller or another plugin.");
    midiOut.setTooltip("Send filtered, transposed and channel-remapped notes on the MIDI output.");
    midiThru.setTooltip("Forward accepted input untouched so external instruments keep playing.");
    cvIn.setTooltip("Convert a DC-coupled CV input (left = pitch 1 V/oct, right = gate) into MIDI notes.");

    inChannel.addItem("All", 1);
    for (int c = 1; c <= 16; ++c) inChannel.addItem("Channel " + juce::String(c), c + 1);
    for (int c = 1; c <= 16; ++c) outChannel.addItem("Channel " + juce::String(c), c + 1);
    inChannel.setTooltip("Accept one channel only, or every incoming channel.");
    outChannel.setTooltip("Channel for MIDI OUT, THRU remapping and CV-input notes.");
    mode.addItemList({"MIDI", "CV"}, 1);
    mode.setTooltip("CV replaces the audio output with 1 V/oct pitch CV (left) and gate CV (right).");
    curve.addItemList({"Linear", "Soft", "Hard"}, 1);
    curve.setTooltip("Velocity response applied to outgoing notes.");

    slider(transpose, -24, 24, 1, " st");
    slider(lowNote, 0, 127, 1, "");
    slider(highNote, 0, 127, 1, "");
    slider(velocity, 0, 2, 0.01, " x");
    slider(cvGate, 5, 2000, 1, " ms");
    slider(cvThreshold, 0.01, 1, 0.001, " V");
    lowNote.textFromValueFunction = [](double value) { return noteName(juce::roundToInt(value)); };
    highNote.textFromValueFunction = [](double value) { return noteName(juce::roundToInt(value)); };
    lowNote.updateText(); highNote.updateText();

    inChannelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.state, "inChannel", inChannel);
    outChannelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.state, "outChannel", outChannel);
    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.state, "mode", mode);
    curveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.state, "velocityCurve", curve);
    midiInAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.state, "midiIn", midiIn);
    midiOutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.state, "midiOut", midiOut);
    midiThruAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.state, "midiThru", midiThru);
    cvInAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.state, "cvToMidi", cvIn);
    transposeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.state, "transpose", transpose);
    lowNoteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.state, "lowNote", lowNote);
    highNoteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.state, "highNote", highNote);
    velocityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.state, "velocity", velocity);
    cvGateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.state, "cvGate", cvGate);
    cvThresholdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.state, "cvThreshold", cvThreshold);

    monitor.setName("MIDI activity");
    monitor.setFont(gh::theme::font(12.0f));
    monitor.setColour(juce::Label::textColourId, gh::theme::textDim);
    monitor.setTooltip("Live incoming and outgoing MIDI activity.");
    monitor.setText("IN 0   last -\nOUT 0   last -", juce::dontSendNotification);

    setResizable(true, false);
    setResizeLimits(400, 690, 760, 1000);
    setSize(460, 700);
    startTimerHz(5);
}

MidiRoutingEditor::~MidiRoutingEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void MidiRoutingEditor::paint(juce::Graphics& g)
{
    g.fillAll(gh::theme::back);
    auto area = getLocalBounds().reduced(10).toFloat();
    gh::theme::outlinedBox(g, area);
}

void MidiRoutingEditor::resized()
{
    auto area = getLocalBounds().reduced(16);
    title.setBounds(area.removeFromTop(28));
    subtitle.setBounds(area.removeFromTop(18));
    area.removeFromTop(4);

    const auto pair = [&area](juce::Label& label, juce::Component& control, int height = 36)
    {
        auto row = area.removeFromTop(height);
        auto captionArea = row.removeFromLeft(juce::jmax(96, row.getWidth() * 2 / 5));
        label.setBounds(captionArea.reduced(2, 0));
        control.setBounds(row.reduced(2));
    };
    const auto toggles = [&area](std::initializer_list<juce::Component*> items, int height = 40)
    {
        auto row = area.removeFromTop(height);
        const auto width = row.getWidth() / static_cast<int>(items.size());
        for (auto* item : items) item->setBounds(row.removeFromLeft(width).reduced(2));
    };

    inputHeading.setBounds(area.removeFromTop(20));
    toggles({&midiIn});
    pair(inChannelLabel, inChannel);
    pair(lowNoteLabel, lowNote);
    pair(highNoteLabel, highNote);
    area.removeFromTop(6);

    outputHeading.setBounds(area.removeFromTop(20));
    toggles({&midiOut, &midiThru});
    pair(outChannelLabel, outChannel);
    pair(transposeLabel, transpose);
    pair(velocityLabel, velocity);
    pair(curveLabel, curve);
    area.removeFromTop(6);

    signalHeading.setBounds(area.removeFromTop(20));
    pair(modeLabel, mode);
    pair(cvGateLabel, cvGate);
    toggles({&cvIn});
    pair(cvThresholdLabel, cvThreshold);
    area.removeFromTop(6);

    monitor.setBounds(area.removeFromTop(48).reduced(2));
}

void MidiRoutingEditor::timerCallback()
{
    const auto m = processor.getMonitor();
    monitor.setText("IN " + juce::String(m.inCount) + "   last " + noteName(m.lastInNote)
        + (m.lastInNote >= 0 ? " v" + juce::String(m.lastInVelocity) + " ch" + juce::String(m.lastInChannel) : juce::String())
        + "\nOUT " + juce::String(m.outCount) + "   last " + noteName(m.lastOutNote)
        + (m.lastOutNote >= 0 ? " v" + juce::String(m.lastOutVelocity) + " ch" + juce::String(m.lastOutChannel) : juce::String())
        + (processor.isCvMode() ? "   |   CV MODE" : juce::String()), juce::dontSendNotification);
}
