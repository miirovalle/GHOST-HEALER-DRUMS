#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <optional>

namespace gh
{
class MidiOutput
{
public:
    virtual ~MidiOutput() = default;
    virtual bool isReady() const noexcept = 0;
    virtual void send(const juce::MidiBuffer&, double sampleRate,
                      std::optional<uint64_t> hostTimeNs) noexcept = 0;
    virtual juce::String takeError() = 0;
};
#if JUCE_IOS
std::unique_ptr<MidiOutput> makeKoalaMidiOutput();
#endif
}
