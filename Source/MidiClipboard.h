#pragma once
#include <juce_core/juce_core.h>
#include <functional>

namespace gh
{
using MidiClipboardWriter = std::function<juce::Result(const juce::MemoryBlock&)>;
#if JUCE_WINDOWS || JUCE_IOS
juce::Result copyMidiToClipboard(const juce::MemoryBlock&);
#else
inline juce::Result copyMidiToClipboard(const juce::MemoryBlock&)
{
    return juce::Result::fail("Binary MIDI clipboard is not supported on this platform. Use MIDI file export.");
}
#endif
}
