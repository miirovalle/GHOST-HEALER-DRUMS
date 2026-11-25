#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <cmath>

/**
    Shared MIDI/CV plumbing for GHOST HEALER DRUMS and GHOST HEALER DRUMS MIDI.

    Everything here is plain message logic so it can be unit tested without a
    host: the note table, channel filtering, velocity response, MIDI THRU with
    stuck-note protection, and 1 V/oct CV generation/detection.
*/
namespace gh::midi
{
// Mirrors gh::pads from Library.h; asserted in the translation units that use both.
inline constexpr int padCount = 48;
inline constexpr int firstNote = 36;
inline constexpr int gateNote = 1;
inline constexpr int defaultChannel = 10;
inline constexpr int referenceNote = 60;

inline int clampNote(int note) noexcept { return juce::jlimit(0, 127, note); }

/** Per-pad note table. The default is the 1.0.8 layout: numeric MIDI 36-83. */
struct NoteMap
{
    std::array<int, padCount> notes {};

    NoteMap()
    {
        for (int i = 0; i < padCount; ++i)
            notes[static_cast<size_t>(i)] = firstNote + i;
    }

    int noteForSlot(int slot) const noexcept
    {
        return slot >= 0 && slot < padCount ? notes[static_cast<size_t>(slot)] : -1;
    }

    void setNote(int slot, int note) noexcept
    {
        if (slot >= 0 && slot < padCount)
            notes[static_cast<size_t>(slot)] = clampNote(note);
    }

    /** First pad whose mapped note matches. -1 when the note is unmapped. */
    int slotForNote(int note) const noexcept
    {
        for (int i = 0; i < padCount; ++i)
            if (notes[static_cast<size_t>(i)] == note)
                return i;
        return -1;
    }
};

enum VelocityCurve { linear = 0, soft = 1, hard = 2 };

/** Incoming velocity response. Outgoing notes always keep the played velocity. */
inline float applyVelocityCurve(float velocity, int curve) noexcept
{
    const auto v = juce::jlimit(0.0f, 1.0f, velocity);
    if (curve == soft) return std::sqrt(v);
    if (curve == hard) return v * v;
    return v;
}

inline juce::uint8 velocityToByte(float velocity) noexcept
{
    return static_cast<juce::uint8>(juce::jlimit(1, 127, juce::roundToInt(velocity * 127.0f)));
}

/** Tracks sounding notes so a stop, route change or preset load can release them. */
struct NoteTracker
{
    void add(int channel, int note) noexcept
    {
        if (channel < 1 || channel > 16 || note < 0 || note > 127) return;
        const auto index = (channel - 1) * 128 + note;
        bits[static_cast<size_t>(index >> 6)] |= uint64_t{1} << (index & 63);
    }

    void remove(int channel, int note) noexcept
    {
        if (channel < 1 || channel > 16 || note < 0 || note > 127) return;
        const auto index = (channel - 1) * 128 + note;
        bits[static_cast<size_t>(index >> 6)] &= ~(uint64_t{1} << (index & 63));
    }

    bool any() const noexcept
    {
        for (const auto word : bits) if (word != 0) return true;
        return false;
    }

    void clear() noexcept { bits.fill(0); }

    /** Calls f(channel, note) for every held note, then forgets them. */
    template <typename Fn>
    void drain(Fn&& fn) noexcept
    {
        for (size_t word = 0; word < bits.size(); ++word)
        {
            auto value = bits[word];
            while (value != 0)
            {
                unsigned long bitIndex = 0;
                for (bitIndex = 0; (value & (uint64_t{1} << bitIndex)) == 0; ++bitIndex) {}
                const auto index = static_cast<int>(word * 64 + bitIndex);
                fn(index / 128 + 1, index % 128);
                value &= value - 1;
            }
            bits[word] = 0;
        }
    }

private:
    std::array<uint64_t, 32> bits {};
};

/**
    Routes incoming MIDI to two destinations:

    - "thru": the untouched input event (external instruments keep playing).
    - "output": a copy transposed and moved to the output channel (this feeds
      the drum engine / the next plugin).

    Both can be active at once. Identical events are only emitted once so a
    same-channel, no-transpose setup never doubles note-on messages.
*/
class Router
{
public:
    struct Settings
    {
        bool input = true, output = true, thru = false;
        int inputChannel = 0, outputChannel = defaultChannel;
        int transpose = 0, lowNote = 0, highNote = 127;
        float velocityScale = 1.0f;
        int velocityCurve = linear;
    };

    void setSettings(const Settings& next) noexcept { settings = next; }
    const Settings& getSettings() const noexcept { return settings; }

    /** True when this event passes the input channel and note range filters. */
    bool accepts(const juce::MidiMessage& message) const noexcept
    {
        if (! settings.input) return false;
        const int channel = message.getChannel();
        if (channel < 1 || channel > 16) return false;
        if (settings.inputChannel != 0 && channel != settings.inputChannel) return false;
        if (message.isNoteOnOrOff())
        {
            const int note = message.getNoteNumber();
            if (note < settings.lowNote || note > settings.highNote) return false;
        }
        return true;
    }

    void route(const juce::MidiMessage& message, int samplePosition, juce::MidiBuffer& output)
    {
        if (! settings.input) return;
        const int channel = message.getChannel();

        if (channel < 1 || channel > 16)
        {
            // System realtime / SysEx: never remapped, only forwarded by THRU.
            if (settings.thru) output.addEvent(message, samplePosition);
            return;
        }

        if (settings.inputChannel != 0 && channel != settings.inputChannel) return;

        if (! message.isNoteOnOrOff())
        {
            if (settings.thru) output.addEvent(message, samplePosition);
            return;
        }

        const int note = message.getNoteNumber();
        if (note < settings.lowNote || note > settings.highNote) return;

        const bool on = message.isNoteOn();
        bool thruWritten = false;

        if (settings.thru)
        {
            output.addEvent(message, samplePosition);
            thruWritten = true;
            if (on) thruHeld.add(channel, note); else thruHeld.remove(channel, note);
        }

        if (! settings.output) return;

        const auto outChannel = juce::jlimit(1, 16, settings.outputChannel);
        const auto outNote = clampNote(note + settings.transpose);
        const auto velocity = on
            ? velocityToByte(applyVelocityCurve(message.getFloatVelocity(), settings.velocityCurve) * settings.velocityScale)
            : static_cast<juce::uint8>(juce::jlimit(1, 127, static_cast<int>(message.getVelocity())));

        if (thruWritten && outChannel == channel && outNote == note && velocity == message.getVelocity())
            return;

        output.addEvent(on ? juce::MidiMessage::noteOn(outChannel, outNote, velocity)
                           : juce::MidiMessage::noteOff(outChannel, outNote, velocity), samplePosition);
        if (on) outHeld.add(outChannel, outNote); else outHeld.remove(outChannel, outNote);
    }

    /** Releases every note this router left sounding, at the given position. */
    void releaseAll(int samplePosition, juce::MidiBuffer& output)
    {
        const auto release = [&output, samplePosition](int channel, int note)
        {
            output.addEvent(juce::MidiMessage::noteOff(channel, note), samplePosition);
        };
        thruHeld.drain(release);
        outHeld.drain(release);
    }

    bool hasHeldNotes() const noexcept { return thruHeld.any() || outHeld.any(); }

    void reset()
    {
        thruHeld.clear();
        outHeld.clear();
    }

private:
    Settings settings;
    NoteTracker thruHeld, outHeld;
};

/** 1 V/oct CV. MIDI note 60 is 0 V, matching common MIDI-to-CV interfaces. */
inline float noteToVolts(int note) noexcept { return (static_cast<float>(note) - referenceNote) / 12.0f; }
inline int voltsToNote(float volts) noexcept { return clampNote(referenceNote + juce::roundToInt(volts * 12.0f)); }

/** MIDI note to gate/pitch CV on the plugin's stereo output. */
struct CvOutput
{
    void setSampleRate(double rate) noexcept
    {
        sampleRate = rate > 0 ? rate : 48000.0;
        reset();
    }

    void setDefaultGateTime(float seconds) noexcept { gateSeconds = juce::jlimit(0.005f, 2.0f, seconds); }

    /** durationSeconds < 0 uses the default gate time. */
    void noteOn(int note, float velocity, double durationSeconds = -1.0) noexcept
    {
        pitch = noteToVolts(note);
        level = juce::jlimit(0.0f, 1.0f, velocity);
        const auto duration = durationSeconds > 0 ? durationSeconds : static_cast<double>(gateSeconds);
        remaining = std::max(1.0, duration * sampleRate);
    }

    void noteOff() noexcept { remaining = 0; }

    void render(float& pitchOut, float& gateOut) noexcept
    {
        gate += smoothing * ((remaining > 0 ? level : 0.0f) - gate);
        if (remaining > 0) remaining -= 1.0;
        pitchOut = pitch;
        gateOut = gate;
    }

    bool gateOpen() const noexcept { return remaining > 0; }

    void reset() noexcept
    {
        pitch = noteToVolts(referenceNote);
        gate = 0;
        level = 0;
        remaining = 0;
    }

private:
    double sampleRate = 48000;
    float gateSeconds = 0.12f, pitch = 0, gate = 0, level = 0;
    double remaining = 0;
    static constexpr float smoothing = 0.01f;
};

/** Gate/pitch CV input to MIDI note events. */
struct CvInput
{
    void reset() noexcept { held = false; note = referenceNote; velocity = 1; }

    /** Returns 1 on gate open, -1 on gate close, 0 while unchanged. */
    int process(float pitchVolts, float gateVolts) noexcept
    {
        if (! held && gateVolts > onThreshold)
        {
            held = true;
            note = voltsToNote(pitchVolts);
            velocity = juce::jlimit(0.05f, 1.0f, gateVolts);
            return 1;
        }
        if (held && gateVolts < offThreshold)
        {
            held = false;
            return -1;
        }
        return 0;
    }

    void forceOff() noexcept { held = false; }
    bool isHeld() const noexcept { return held; }
    int currentNote() const noexcept { return note; }
    float currentVelocity() const noexcept { return velocity; }

    float onThreshold = 0.05f, offThreshold = 0.02f;

private:
    bool held = false;
    int note = referenceNote;
    float velocity = 1;
};
} // namespace gh::midi
