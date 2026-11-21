#include "MidiOutput.h"
#import <CoreMIDI/CoreMIDI.h>
#include <mach/mach_time.h>
#include <array>
#include <atomic>
#include <utility>

namespace gh
{
namespace
{
class KoalaMidiOutput final : public MidiOutput
{
public:
    KoalaMidiOutput()
    {
        mach_timebase_info_data_t timebase {};
        if (mach_timebase_info(&timebase) != KERN_SUCCESS || timebase.denom == 0 || timebase.numer == 0)
        {
            initialError = "Could not obtain the iPad MIDI timing clock.";
            return;
        }
        nanosPerTick = static_cast<double>(timebase.numer) / timebase.denom;
        auto status = MIDIClientCreate(CFSTR("GHOST HEALER DRUMS"), nullptr, nullptr, &client);
        if (status == noErr)
            status = MIDISourceCreate(client, CFSTR("GHOST HEALER DRUMS Record"), &source);
        if (status != noErr)
            initialError = "Could not create iPad MIDI recording source (CoreMIDI "
                + juce::String(static_cast<int>(status)) + ").";
    }
    ~KoalaMidiOutput() override
    {
        if (source != 0) MIDIEndpointDispose(source);
        if (client != 0) MIDIClientDispose(client);
    }
    bool isReady() const noexcept override { return source != 0; }
    void send(const juce::MidiBuffer& midi, double rate, std::optional<uint64_t> hostTimeNs) noexcept override
    {
        if (! isReady() || midi.isEmpty()) return;
        const auto start = hostTimeNs && *hostTimeNs > 0 ? static_cast<uint64_t>(*hostTimeNs / nanosPerTick) : mach_absolute_time();
        // CoreMIDI packet lists are variable-sized and require aligned storage.
        auto* list = reinterpret_cast<MIDIPacketList*>(packetStorage.data());
        auto* packet = MIDIPacketListInit(list);
        const auto flush = [this, list]
        {
            if (const auto status = MIDIReceived(source, list); status != noErr)
                lastError.store(status, std::memory_order_relaxed);
        };
        for (const auto event : midi)
        {
            const auto timestamp = std::max(lastScheduledTick,
                start + static_cast<uint64_t>(event.samplePosition * (1.0e9 / rate) / nanosPerTick));
            lastScheduledTick = timestamp;
            auto* next = MIDIPacketListAdd(list, packetStorage.size(), packet, timestamp,
                static_cast<ByteCount>(event.numBytes), event.data);
            if (next == nullptr)
            {
                flush();
                packet = MIDIPacketListInit(list);
                next = MIDIPacketListAdd(list, packetStorage.size(), packet, timestamp,
                    static_cast<ByteCount>(event.numBytes), event.data);
            }
            if (next == nullptr)
            {
                lastError.store(kMIDIMessageSendErr, std::memory_order_relaxed);
                return;
            }
            packet = next;
        }
        flush();
    }
    juce::String takeError() override
    {
        if (initialError.isNotEmpty()) return std::exchange(initialError, {});
        if (const auto status = lastError.exchange(noErr); status != noErr)
            return "iPad MIDI recording output failed (CoreMIDI " + juce::String(static_cast<int>(status)) + ").";
        return {};
    }
private:
    MIDIClientRef client = 0;
    MIDIEndpointRef source = 0;
    double nanosPerTick = 1;
    uint64_t lastScheduledTick = 0;
    alignas(MIDIPacketList) std::array<uint8_t, 1024> packetStorage {};
    std::atomic<OSStatus> lastError {noErr};
    juce::String initialError;
};
}
std::unique_ptr<MidiOutput> makeKoalaMidiOutput() { return std::make_unique<KoalaMidiOutput>(); }
}
