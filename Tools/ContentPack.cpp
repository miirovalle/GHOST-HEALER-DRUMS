#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <iostream>
#include <set>
#include "../Source/Mapping.h"

namespace
{
void require(bool condition, const juce::String& text)
{
    if (! condition) { std::cerr << text << '\n'; std::exit(1); }
}
void json(const juce::File& file, const juce::var& data)
{
    require(file.getParentDirectory().createDirectory().wasOk(), "Cannot create " + file.getParentDirectory().getFullPathName());
    require(file.replaceWithText(juce::JSON::toString(data)), "Cannot write " + file.getFullPathName());
}
}
int main(int argc, char** argv)
{
    if (argc != 4) { std::cerr << "GHContentPack <48-WAV-kit-root> <MIDI-root> <new-content-folder>\n"; return 1; }
    const juce::File kitsRoot(juce::String::fromUTF8(argv[1])), midiRoot(juce::String::fromUTF8(argv[2])), out(juce::String::fromUTF8(argv[3]));
    require(kitsRoot.isDirectory() && midiRoot.isDirectory(), "Input folders missing.");
    require(! out.exists(), "Use a new output folder; existing content is never overwritten.");
    require(out.getChildFile("Samples").createDirectory().wasOk(), "Cannot create content folder.");
    auto directories = kitsRoot.findChildFiles(juce::File::findDirectories, false); directories.sort();
    juce::Array<juce::var> kits, loops, rejected;
    std::set<juce::String> hashes;
    juce::int64 sourceBytes = 0, packedBytes = 0;
    for (const auto& directory : directories)
    {
        auto samples = directory.findChildFiles(juce::File::findFiles, false, "*.wav"); samples.sort();
        require(samples.size() == 48, directory.getFileName() + " must contain 48 WAV files.");
        auto* kit = new juce::DynamicObject;
        kit->setProperty("version", 1); kit->setProperty("name", directory.getFileName());
        juce::Array<juce::var> pads;
        for (int i = 0; i < 48; ++i)
        {
            const auto& sample = samples.getReference(i);
            require(sample.getFileName().substring(0, 2).getIntValue() == i + 1, "Samples must be numbered 01-48.");
            juce::MemoryBlock data; require(sample.loadFileAsData(data), "Cannot read " + sample.getFullPathName());
            const auto hash = juce::SHA256(data).toHexString(); sourceBytes += static_cast<juce::int64>(data.getSize());
            const auto path = hash + ".wav";
            if (hashes.insert(hash).second)
            {
                require(out.getChildFile("Samples/" + path).replaceWithData(data.getData(), data.getSize()), "Cannot write shared sample.");
                packedBytes += static_cast<juce::int64>(data.getSize());
            }
            auto* pad = new juce::DynamicObject; pad->setProperty("sample", "../Samples/" + path); pads.add(juce::var(pad));
        }
        kit->setProperty("pads", pads);
        const auto relative = "Kits/" + directory.getFileName() + ".ghkit"; json(out.getChildFile(relative), juce::var(kit));
        auto* entry = new juce::DynamicObject; entry->setProperty("name", directory.getFileName());
        entry->setProperty("category", "Ghost Healer"); entry->setProperty("file", relative); kits.add(juce::var(entry));
    }
    auto midis = midiRoot.findChildFiles(juce::File::findFiles, true, "*.mid");
    midis.addArray(midiRoot.findChildFiles(juce::File::findFiles, true, "*.midi")); midis.sort();
    std::array<juce::int64, 128> histogram {};
    for (const auto& file : midis)
    {
        auto input = file.createInputStream(); juce::MidiFile midi;
        int supported = 0, unsupported = 0;
        juce::MidiFile mapped;
        const bool readable = input && midi.readFrom(*input) && midi.getTimeFormat() > 0;
        if (readable) mapped.setTicksPerQuarterNote(midi.getTimeFormat());
        if (readable)
            for (int t = 0; t < midi.getNumTracks(); ++t)
            {
                juce::MidiMessageSequence track(*midi.getTrack(t));
                for (int e = 0; e < track.getNumEvents(); ++e)
                {
                    auto& message = track.getEventPointer(e)->message;
                    if (! message.isNoteOnOrOff()) continue;
                    const int note = message.getNoteNumber(), target = gh::mapGmNote(note);
                    if (message.isNoteOn())
                    {
                        ++histogram[static_cast<size_t>(note)];
                        if (target >= 36 && target <= 83) ++supported; else ++unsupported;
                    }
                    if (target >= 0) message.setNoteNumber(target);
                }
                if (t == 0) track.addEvent(juce::MidiMessage::textMetaEvent(1, "GH_MAP=36-83"));
                mapped.addTrack(track);
            }
        if (! readable || supported == 0 || supported > 16384)
        {
            rejected.add(file.getRelativePathFrom(midiRoot) + ": " + (! readable ? "invalid/SMPTE MIDI" : supported == 0 ? "no notes 36-83" : "too many hits"));
            continue;
        }
        const auto relative = file.getRelativePathFrom(midiRoot).replaceCharacter('\\', '/');
        const auto destination = out.getChildFile("Loops/" + relative);
        require(destination.getParentDirectory().createDirectory().wasOk(), "Cannot create MIDI folder.");
        auto output = destination.createOutputStream();
        require(output && mapped.writeTo(*output) && output->getStatus().wasOk(), "Cannot write mapped MIDI.");
        auto* entry = new juce::DynamicObject;
        entry->setProperty("name", file.getFileNameWithoutExtension()); entry->setProperty("file", "Loops/" + relative);
        entry->setProperty("category", relative.upToLastOccurrenceOf("/", false, false));
        entry->setProperty("unmappedNotes", unsupported); loops.add(juce::var(entry));
    }
    auto* catalogue = new juce::DynamicObject;
    catalogue->setProperty("version", 1); catalogue->setProperty("kits", kits); catalogue->setProperty("loops", loops);
    catalogue->setProperty("rejectedLoops", rejected); catalogue->setProperty("uniqueSamples", static_cast<int>(hashes.size()));
    catalogue->setProperty("sampleBytes", packedBytes);
    catalogue->setProperty("midiMapping", "GM/FPC roles remapped to MIDI 36-83; originals unchanged");
    juce::Array<juce::var> noteCounts; for (const auto n : histogram) noteCounts.add(n);
    catalogue->setProperty("midiNoteHistogram", noteCounts); json(out.getChildFile("catalog.json"), juce::var(catalogue));
    std::cout << kits.size() << " kits, " << loops.size() << " loops, " << rejected.size() << " rejected loops\n"
              << hashes.size() << " unique WAVs; " << packedBytes << " bytes vs " << sourceBytes << " original bytes\n";
}
