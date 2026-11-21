#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <vector>
#include "Mapping.h"

namespace gh
{
constexpr int pads = 48, firstNote = 36, gateNote = 1;
inline constexpr std::array<const char*, 12> roles {
    "Kick", "Kick 2", "Snare", "Clap", "Snare 2", "Percussion",
    "Extra perc", "Closed hat", "Tom", "Shaker", "Open hat", "Crash / Ride"
};
inline juce::String noteLabel(int slot)
{
    static constexpr std::array<const char*, 12> names {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return juce::String(names[static_cast<size_t>(slot % 12)]) + juce::String(slot / 12 + 3);
}
struct Retired
{
    virtual ~Retired() = default;
    Retired* next = nullptr;
};
struct PadValues
{
    float tune = 0, pan = 0, level = 0, velocityPitch = 0;
    float lowCut = 0, highCut = 20000, compressor = 0;
};
struct Sample
{
    // Decoded audio is kept as interleaved 16-bit PCM. WAV sources are 16, 24 or
    // 32-bit; 16-bit sources are stored losslessly, wider sources are quantised to
    // 16-bit, which halves the resident cost of the library on memory-tight devices.
    std::vector<int16_t> pcm;
    juce::File file;
    size_t frames = 0;
    int channels = 0;
    double rate = 0;
    juce::Result load(const juce::File&);
    void frame(size_t index, float& left, float& right) const noexcept;
    size_t storedBytes() const noexcept { return pcm.size() * sizeof(int16_t); }
};

/** Encodes a loaded sample as a canonical 16-bit PCM WAV file. */
juce::MemoryBlock encodeWav(const Sample&);
struct Kit final : Retired
{
    juce::String name;
    juce::File definition;
    std::array<Sample, pads> samples;
    std::array<PadValues, pads> values;
    std::array<juce::String, pads> padNames;
    juce::String importInfo;
    size_t bytes = 0;
};
struct BatteryKit
{
    juce::String name;
    std::vector<juce::File> samples;
    std::vector<juce::String> padNames;
    size_t omittedZones = 0, hiddenCells = 0;
};
struct Hit
{
    double beat = 0, duration = 0.1;
    int note = firstNote;
    float velocity = 1;
};
struct Pattern final : Retired
{
    juce::String name = "Grid";
    double length = 4;
    int numerator = 4, denominator = 4;
    std::vector<Hit> hits;
};
constexpr int maxSteps = 32;
struct Scene
{
    std::array<std::array<uint8_t, maxSteps>, 12> velocity {};
    std::array<uint8_t, 12> variant {};
};
struct Grid
{
    std::array<Scene, 16> scenes;
    int scene = 0, rate = 1, stepCount = 16;
    float swing = 0;
};
struct Entry { juce::String name, category; juce::File file; };
struct Catalogue { std::vector<Entry> kits, loops; juce::File root; };
juce::Result scanCatalogue(const juce::File&, Catalogue&);
juce::Result loadKit(const juce::File&, std::unique_ptr<Kit>&);
juce::Result readBatteryKit(const juce::File&, BatteryKit&);
juce::Result loadBatteryBank(const BatteryKit&, size_t bank, std::unique_ptr<Kit>&);
juce::Result loadMidi(const juce::File&, std::unique_ptr<Pattern>&, int& ignoredNotes, bool gm = false);
std::unique_ptr<Pattern> gridPattern(const Grid&);
std::unique_ptr<Pattern> clonePattern(const Pattern&);
void humanize(Pattern&, uint32_t seed, double bpm);
juce::Result writeMidi(const Pattern&, const juce::File&, double bpm);
juce::Result writeMidi(const Pattern&, juce::OutputStream&, double bpm);
juce::var gridToVar(const Grid&);
juce::Result gridFromVar(const juce::var&, Grid&);
juce::File findBundledLibrary();
}
