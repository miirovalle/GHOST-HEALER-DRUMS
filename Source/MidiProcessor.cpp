#include "MidiProcessor.h"
#include "MidiEditor.h"

namespace
{
constexpr auto midiChannelCount = 16;
}

juce::AudioProcessorValueTreeState::ParameterLayout MidiRoutingProcessor::layout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout result;
    result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"midiIn", 1},
        "External MIDI input", true));
    result.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{"inChannel", 1},
        "Input channel", 0, midiChannelCount, 0));
    result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"midiOut", 1},
        "MIDI output", true));
    result.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{"outChannel", 1},
        "Output channel", 1, midiChannelCount, gh::midi::defaultChannel));
    result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"midiThru", 1},
        "MIDI thru", true));
    result.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{"transpose", 1},
        "Transpose", -24, 24, 0));
    result.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{"lowNote", 1},
        "Lowest note", 0, 127, 0));
    result.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{"highNote", 1},
        "Highest note", 0, 127, 127));
    result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"velocity", 1},
        "Velocity scale", juce::NormalisableRange<float>{0.0f, 2.0f, 0.01f}, 1.0f));
    result.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"velocityCurve", 1},
        "Velocity response", juce::StringArray{"Linear", "Soft", "Hard"}, 0));
    result.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"mode", 1},
        "Signal mode", juce::StringArray{"MIDI", "CV"}, 0));
    result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"cvGate", 1},
        "CV gate time", juce::NormalisableRange<float>{5.0f, 2000.0f, 1.0f, 0.5f}, 120.0f));
    result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"cvToMidi", 1},
        "CV input to MIDI", false));
    result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"cvThreshold", 1},
        "CV gate threshold", juce::NormalisableRange<float>{0.01f, 1.0f, 0.001f}, 0.05f));
    return result;
}

MidiRoutingProcessor::MidiRoutingProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "GhostDrumsMidi", layout())
{
    midiInParameter = state.getRawParameterValue("midiIn");
    inChannelParameter = state.getRawParameterValue("inChannel");
    midiOutParameter = state.getRawParameterValue("midiOut");
    outChannelParameter = state.getRawParameterValue("outChannel");
    midiThruParameter = state.getRawParameterValue("midiThru");
    transposeParameter = state.getRawParameterValue("transpose");
    lowNoteParameter = state.getRawParameterValue("lowNote");
    highNoteParameter = state.getRawParameterValue("highNote");
    velocityParameter = state.getRawParameterValue("velocity");
    velocityCurveParameter = state.getRawParameterValue("velocityCurve");
    modeParameter = state.getRawParameterValue("mode");
    cvGateParameter = state.getRawParameterValue("cvGate");
    cvToMidiParameter = state.getRawParameterValue("cvToMidi");
    cvThresholdParameter = state.getRawParameterValue("cvThreshold");
}

void MidiRoutingProcessor::prepareToPlay(double rate, int)
{
    cvOutput.setSampleRate(rate);
    cvOutput.reset();
    cvInput.reset();
    needsRelease = true;
}

void MidiRoutingProcessor::releaseResources()
{
    needsRelease = true;
    cvOutput.noteOff();
    cvInput.forceOff();
}

bool MidiRoutingProcessor::isBusesLayoutSupported(const BusesLayout& buses) const
{
    const auto in = buses.getMainInputChannelSet(), out = buses.getMainOutputChannelSet();
    const bool stereo = in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::stereo();
    const bool mono = in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::mono();
    return stereo || mono;
}

void MidiRoutingProcessor::readSettings()
{
    gh::midi::Router::Settings next;
    next.input = midiInParameter->load(std::memory_order_relaxed) > 0.5f;
    next.output = midiOutParameter->load(std::memory_order_relaxed) > 0.5f;
    next.thru = midiThruParameter->load(std::memory_order_relaxed) > 0.5f;
    next.inputChannel = juce::jlimit(0, midiChannelCount,
        juce::roundToInt(inChannelParameter->load(std::memory_order_relaxed)));
    outChannel = juce::jlimit(1, midiChannelCount,
        juce::roundToInt(outChannelParameter->load(std::memory_order_relaxed)));
    transpose = juce::jlimit(-24, 24,
        juce::roundToInt(transposeParameter->load(std::memory_order_relaxed)));
    const auto low = juce::roundToInt(lowNoteParameter->load(std::memory_order_relaxed));
    const auto high = juce::roundToInt(highNoteParameter->load(std::memory_order_relaxed));
    next.outputChannel = outChannel;
    next.transpose = transpose;
    next.lowNote = juce::jmin(low, high);
    next.highNote = juce::jmax(low, high);
    next.velocityScale = velocityParameter->load(std::memory_order_relaxed);
    next.velocityCurve = juce::jlimit(0, 2,
        juce::roundToInt(velocityCurveParameter->load(std::memory_order_relaxed)));

    const auto previous = router.getSettings();
    router.setSettings(next);
    if ((previous.output && ! next.output) || (previous.thru && ! next.thru)
        || previous.outputChannel != next.outputChannel || previous.transpose != next.transpose
        || previous.lowNote != next.lowNote || previous.highNote != next.highNote)
        needsRelease = true;

    cvMode.store(modeParameter->load(std::memory_order_relaxed) > 0.5f, std::memory_order_relaxed);
    cvOutEnabled = cvMode.load(std::memory_order_relaxed);
    cvInEnabled = cvToMidiParameter->load(std::memory_order_relaxed) > 0.5f;
    cvOutput.setDefaultGateTime(cvGateParameter->load(std::memory_order_relaxed) / 1000.0f);
    cvInput.onThreshold = juce::jlimit(0.01f, 1.0f, cvThresholdParameter->load(std::memory_order_relaxed));
    cvInput.offThreshold = cvInput.onThreshold * 0.4f;
    if (! cvOutEnabled) cvOutput.noteOff();
}

void MidiRoutingProcessor::noteActivity(const juce::MidiMessage& message, bool)
{
    lastInNote.store(message.getNoteNumber(), std::memory_order_relaxed);
    lastInVelocity.store(message.getVelocity(), std::memory_order_relaxed);
    lastInChannel.store(message.getChannel(), std::memory_order_relaxed);
    midiInCount.fetch_add(1, std::memory_order_relaxed);
}

void MidiRoutingProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const auto numSamples = buffer.getNumSamples();
    readSettings();

    juce::MidiBuffer output;
    output.ensureSize(static_cast<size_t>(midi.getNumEvents()) * 3 + 64);

    if (needsRelease)
    {
        if (router.hasHeldNotes()) router.releaseAll(0, output);
        needsRelease = false;
    }

    // CV input has to be read before the buffer is overwritten with CV output.
    if (cvInEnabled && buffer.getNumChannels() > 0 && numSamples > 0)
    {
        const auto* pitch = buffer.getReadPointer(0);
        const auto* gate = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : pitch;
        for (int s = 0; s < numSamples; ++s)
        {
            const auto edge = cvInput.process(pitch[s], gate[s]);
            if (edge == 0) continue;
            if (edge > 0)
            {
                const auto message = juce::MidiMessage::noteOn(outChannel, cvInput.currentNote(), cvInput.currentVelocity());
                output.addEvent(message, s);
                noteActivity(message, false);
            }
            else output.addEvent(juce::MidiMessage::noteOff(outChannel, cvInput.currentNote()), s);
        }
    }

    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        router.route(message, metadata.samplePosition, output);
        if (! router.accepts(message) || ! message.isNoteOnOrOff()) continue;
        if (message.isNoteOn())
        {
            noteActivity(message, false);
            if (cvOutEnabled) cvOutput.noteOn(message.getNoteNumber() + transpose, message.getFloatVelocity());
        }
        else if (cvOutEnabled) cvOutput.noteOff();
    }

    if (cvOutEnabled)
    {
        auto* left = buffer.getWritePointer(0);
        auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : left;
        for (int s = 0; s < numSamples; ++s)
        {
            float pitch = 0, gate = 0;
            cvOutput.render(pitch, gate);
            left[s] = pitch;
            right[s] = gate;
        }
        for (int channel = 2; channel < buffer.getNumChannels(); ++channel)
            buffer.clear(channel, 0, numSamples);
    }

    int generated = 0;
    for (const auto metadata : output)
    {
        const auto message = metadata.getMessage();
        if (! message.isNoteOn()) continue;
        ++generated;
        lastOutNote.store(message.getNoteNumber(), std::memory_order_relaxed);
        lastOutVelocity.store(message.getVelocity(), std::memory_order_relaxed);
        lastOutChannel.store(message.getChannel(), std::memory_order_relaxed);
    }
    if (generated > 0) midiOutCount.fetch_add(generated, std::memory_order_relaxed);

    midi.swapWith(output);
}

MidiRoutingProcessor::Monitor MidiRoutingProcessor::getMonitor() const
{
    Monitor monitor;
    monitor.inCount = midiInCount.load(std::memory_order_relaxed);
    monitor.outCount = midiOutCount.load(std::memory_order_relaxed);
    monitor.lastInNote = lastInNote.load(std::memory_order_relaxed);
    monitor.lastInVelocity = lastInVelocity.load(std::memory_order_relaxed);
    monitor.lastInChannel = lastInChannel.load(std::memory_order_relaxed);
    monitor.lastOutNote = lastOutNote.load(std::memory_order_relaxed);
    monitor.lastOutVelocity = lastOutVelocity.load(std::memory_order_relaxed);
    monitor.lastOutChannel = lastOutChannel.load(std::memory_order_relaxed);
    return monitor;
}

juce::AudioProcessorEditor* MidiRoutingProcessor::createEditor()
{
    return new MidiRoutingEditor(*this);
}

void MidiRoutingProcessor::getStateInformation(juce::MemoryBlock& memory)
{
    if (auto xml = state.copyState().createXml()) copyXmlToBinary(*xml, memory);
}

void MidiRoutingProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
    {
        if (! xml->hasTagName("GhostDrumsMidi")) return;
        state.replaceState(juce::ValueTree::fromXml(*xml));
        needsRelease = true;
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MidiRoutingProcessor();
}
