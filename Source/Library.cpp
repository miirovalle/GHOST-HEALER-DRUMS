#include "Library.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace gh
{
namespace
{
uint16_t u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t u32(const uint8_t* p) { return juce::ByteOrder::littleEndianInt(p); }
juce::Result jsonFile(const juce::File& file, juce::var& data)
{
    if (! file.existsAsFile()) return juce::Result::fail("File missing: " + file.getFullPathName());
    const auto result = juce::JSON::parse(file.loadFileAsString(), data);
    if (result.failed()) return juce::Result::fail(file.getFileName() + ": " + result.getErrorMessage());
    return juce::Result::ok();
}
float number(const juce::var& object, const char* name, float fallback, float lo, float hi)
{
    const auto value = object.getProperty(name, fallback);
    const float f = static_cast<float>(value);
    return std::isfinite(f) ? juce::jlimit(lo, hi, f) : fallback;
}
}
juce::Result Sample::load(const juce::File& source)
{
    frames = 0; channels = 0; rate = 0; file = juce::File{};
    pcm.clear(); pcm.shrink_to_fit();

    int bits = 0, format = 0, frameBytes = 0;
    size_t dataOffset = 0;
    juce::MemoryBlock bytes;

    if (source.getSize() > 128LL * 1024 * 1024)
        return juce::Result::fail("WAV exceeds the 128 MiB per-sample limit.");
    if (! source.loadFileAsData(bytes)) return juce::Result::fail("Cannot read sample: " + source.getFullPathName());
    const auto* b = static_cast<const uint8_t*>(bytes.getData());
    const auto size = bytes.getSize();
    if (size < 44 || std::memcmp(b, "RIFF", 4) != 0 || std::memcmp(b + 8, "WAVE", 4) != 0)
        return juce::Result::fail("Only RIFF WAV samples are supported: " + source.getFileName());
    for (size_t p = 12; p + 8 <= size;)
    {
        const auto length = static_cast<size_t>(u32(b + p + 4));
        if (length > size - p - 8) return juce::Result::fail("Truncated WAV: " + source.getFileName());
        if (std::memcmp(b + p, "fmt ", 4) == 0 && length >= 16)
        {
            format = u16(b + p + 8); channels = u16(b + p + 10);
            rate = u32(b + p + 12); frameBytes = u16(b + p + 20); bits = u16(b + p + 22);
            if (format == 65534 && length >= 40)
            {
                constexpr std::array<uint8_t, 12> guidTail {0, 0, 16, 0, 128, 0, 0, 170, 0, 56, 155, 113};
                if (u16(b + p + 24) < 22 || u16(b + p + 26) == 0 || u16(b + p + 26) > bits
                    || std::memcmp(b + p + 36, guidTail.data(), guidTail.size()) != 0)
                    return juce::Result::fail("Unsupported extensible WAV format.");
                format = static_cast<int>(u32(b + p + 32));
            }
        }
        else if (std::memcmp(b + p, "data", 4) == 0)
        {
            dataOffset = p + 8;
            frames = length;
        }
        p += 8 + length + (length & 1);
    }
    if (dataOffset == 0 || channels < 1 || channels > 2 || rate < 8000 || rate > 384000
        || frameBytes != channels * (bits / 8) || (format != 1 && format != 3)
        || (bits != 8 && bits != 16 && bits != 24 && bits != 32) || (format == 3 && bits != 32)
        || frameBytes == 0 || frames % static_cast<size_t>(frameBytes) != 0)
        return juce::Result::fail("Unsupported WAV encoding: " + source.getFileName());
    frames /= static_cast<size_t>(frameBytes);
    if (frames < 2) return juce::Result::fail("Sample has fewer than two frames: " + source.getFileName());

    // Decode once into compact 16-bit PCM and drop the source file bytes.
    pcm.resize(frames * static_cast<size_t>(channels));
    const auto samplesPerFrame = static_cast<size_t>(channels);
    const auto bytesPerSample = static_cast<size_t>(bits / 8);
    const auto* data = b + dataOffset;

    const auto toInt16 = [bits, format](const uint8_t* q) -> int16_t
    {
        if (format == 3)
        {
            float value; const auto raw = u32(q); std::memcpy(&value, &raw, 4);
            if (! std::isfinite(value)) return 0;
            const auto scaled = std::lround(static_cast<double>(value) * 32768.0);
            return static_cast<int16_t>(juce::jlimit(-32768L, 32767L, scaled));
        }
        if (bits == 32) return static_cast<int16_t>(static_cast<int32_t>(u32(q)) >> 16);
        if (bits == 24)
        {
            int32_t n = q[0] | (q[1] << 8) | (q[2] << 16);
            if ((n & 0x800000) != 0) n -= 0x1000000;
            return static_cast<int16_t>(n >> 8);
        }
        if (bits == 16) return static_cast<int16_t>(u16(q));
        return static_cast<int16_t>((static_cast<int>(*q) - 128) << 8);
    };

    for (size_t i = 0; i < frames; ++i)
    {
        const auto* frameData = data + i * static_cast<size_t>(frameBytes);
        for (size_t c = 0; c < samplesPerFrame; ++c)
            pcm[i * samplesPerFrame + c] = toInt16(frameData + c * bytesPerSample);
    }

    file = source;
    return juce::Result::ok();
}
void Sample::frame(size_t index, float& left, float& right) const noexcept
{
    const auto* p = pcm.data() + index * static_cast<size_t>(channels);
    left = static_cast<float>(p[0]) * (1.0f / 32768.0f);
    right = channels > 1 ? static_cast<float>(p[1]) * (1.0f / 32768.0f) : left;
}
juce::MemoryBlock encodeWav(const Sample& sample)
{
    const auto dataBytes = static_cast<int>(sample.storedBytes());
    juce::MemoryBlock out;
    out.setSize(static_cast<size_t>(44 + dataBytes), false);
    auto* p = static_cast<uint8_t*>(out.getData());
    const auto write16 = [&p](uint16_t v) { p[0] = static_cast<uint8_t>(v); p[1] = static_cast<uint8_t>(v >> 8); p += 2; };
    const auto write32 = [&p](uint32_t v)
    {
        p[0] = static_cast<uint8_t>(v); p[1] = static_cast<uint8_t>(v >> 8);
        p[2] = static_cast<uint8_t>(v >> 16); p[3] = static_cast<uint8_t>(v >> 24); p += 4;
    };
    std::memcpy(p, "RIFF", 4); p += 4;
    write32(static_cast<uint32_t>(36 + dataBytes));
    std::memcpy(p, "WAVEfmt ", 8); p += 8;
    write32(16); write16(1); write16(static_cast<uint16_t>(sample.channels));
    write32(static_cast<uint32_t>(juce::jmax(8000.0, sample.rate))); write32(static_cast<uint32_t>(dataBytes + 36));
    write16(static_cast<uint16_t>(sample.channels * 2)); write16(16);
    std::memcpy(p, "data", 4); p += 4; write32(static_cast<uint32_t>(dataBytes));
    if (dataBytes > 0) std::memcpy(p, sample.pcm.data(), static_cast<size_t>(dataBytes));
    return out;
}
juce::Result scanCatalogue(const juce::File& root, Catalogue& result)
{
    juce::var json;
    if (const auto r = jsonFile(root.getChildFile("catalog.json"), json); r.failed()) return r;
    if (static_cast<int>(json["version"]) != 1 || ! json["kits"].isArray() || ! json["loops"].isArray())
        return juce::Result::fail("Invalid Ghost library catalogue.");
    Catalogue catalogue;
    catalogue.root = root;
    const auto append = [&root](const juce::var& array, std::vector<Entry>& entries)
    {
        for (const auto& entry : *array.getArray())
        {
            const auto relative = entry["file"].toString();
            const auto file = root.getChildFile(relative);
            if (relative.isEmpty() || juce::File::isAbsolutePath(relative) || ! file.isAChildOf(root))
                return juce::Result::fail("Library path escapes its folder.");
            entries.push_back({entry["name"].toString(), entry["category"].toString(), file});
        }
        return juce::Result::ok();
    };
    if (auto r = append(json["kits"], catalogue.kits); r.failed()) return r;
    if (auto r = append(json["loops"], catalogue.loops); r.failed()) return r;
    if (catalogue.kits.empty()) return juce::Result::fail("Library contains no kits.");
    result = std::move(catalogue);
    return juce::Result::ok();
}
juce::Result loadKit(const juce::File& definition, std::unique_ptr<Kit>& result)
{
    juce::var json;
    if (auto r = jsonFile(definition, json); r.failed()) return r;
    const auto* slots = json["pads"].getArray();
    if (static_cast<int>(json["version"]) != 1 || slots == nullptr || slots->size() != pads)
        return juce::Result::fail("A Ghost kit must have exactly 48 pads and version 1.");
    auto kit = std::make_unique<Kit>();
    kit->name = json["name"].toString();
    kit->definition = definition;
    if (json.hasProperty("importInfo"))
    {
        if (! json["importInfo"].isString() || json["importInfo"].toString().length() > 1024)
            return juce::Result::fail("Invalid kit import notice.");
        kit->importInfo = json["importInfo"].toString();
    }
    for (int i = 0; i < pads; ++i)
    {
        const auto& slot = slots->getReference(i);
        const auto path = slot["sample"].toString();
        if (path.isEmpty() || juce::File::isAbsolutePath(path))
            return juce::Result::fail("Kit sample references must be relative paths.");
        auto& sample = kit->samples[static_cast<size_t>(i)];
        if (slot.hasProperty("name"))
        {
            const auto name = slot["name"];
            if (! name.isString() || name.toString().length() > 256)
                return juce::Result::fail("Invalid pad name.");
            kit->padNames[static_cast<size_t>(i)] = name.toString();
        }
        const auto parent = definition.getParentDirectory();
        const auto root = parent.getFileName() == "Kits" && parent.getParentDirectory().getChildFile("catalog.json").existsAsFile()
            ? parent.getParentDirectory() : parent;
        const auto file = parent.getChildFile(path);
        if (! file.isAChildOf(root) || file.isSymbolicLink())
            return juce::Result::fail("Kit sample path escapes its library, or is a symbolic link.");
        if (auto r = sample.load(file); r.failed()) return r;
        constexpr std::array<const char*, 7> keys {"tune", "pan", "level", "velocityPitch", "lowCut", "highCut", "compressor"};
        constexpr std::array<float, 7> lo {-24, -1, -60, -24, 0, 100, 0}, hi {24, 1, 6, 24, 2000, 20000, 1};
        for (size_t c = 0; c < keys.size(); ++c)
            if (slot.hasProperty(keys[c]))
            {
                const auto value = slot[keys[c]];
                const double n = static_cast<double>(value);
                if ((! value.isInt() && ! value.isInt64() && ! value.isDouble()) || ! std::isfinite(n) || n < lo[c] || n > hi[c])
                    return juce::Result::fail("Invalid pad setting: " + juce::String(keys[c]));
            }
        kit->bytes += sample.storedBytes();
        if (kit->bytes > 512ULL * 1024 * 1024) return juce::Result::fail("Active kit exceeds the 512 MiB sample limit.");
        auto& v = kit->values[static_cast<size_t>(i)];
        v.tune = number(slot, "tune", 0, -24, 24); v.pan = number(slot, "pan", 0, -1, 1);
        v.level = number(slot, "level", 0, -60, 6); v.velocityPitch = number(slot, "velocityPitch", 0, -24, 24);
        v.lowCut = number(slot, "lowCut", 0, 0, 2000); v.highCut = number(slot, "highCut", 20000, 100, 20000);
        v.compressor = number(slot, "compressor", 0, 0, 1);
    }
    result = std::move(kit);
    return juce::Result::ok();
}
juce::Result loadMidi(const juce::File& file, std::unique_ptr<Pattern>& result, int& ignored, bool gm)
{
    auto stream = file.createInputStream();
    juce::MidiFile midi;
    if (! stream || ! midi.readFrom(*stream)) return juce::Result::fail("Cannot read MIDI: " + file.getFileName());
    const int ppq = midi.getTimeFormat();
    if (ppq <= 0) return juce::Result::fail("SMPTE-timed MIDI is not supported.");
    auto pattern = std::make_unique<Pattern>();
    pattern->name = file.getFileNameWithoutExtension();
    double last = 0, trackEnd = 0;
    for (int track = 0; track < midi.getNumTracks(); ++track)
        for (int i = 0; i < midi.getTrack(track)->getNumEvents(); ++i)
        {
            const auto& message = midi.getTrack(track)->getEventPointer(i)->message;
            if (message.isTextMetaEvent() && message.getTextFromTextMetaEvent() == "GH_MAP=36-83") gm = false;
        }
    ignored = 0;
    for (int track = 0; track < midi.getNumTracks(); ++track)
    {
        juce::MidiMessageSequence sequence(*midi.getTrack(track));
        sequence.updateMatchedPairs();
        for (int i = 0; i < sequence.getNumEvents(); ++i)
        {
            const auto* event = sequence.getEventPointer(i);
            const auto& message = event->message;
            last = std::max(last, message.getTimeStamp() / ppq);
            if (message.isEndOfTrackMetaEvent()) trackEnd = std::max(trackEnd, message.getTimeStamp() / ppq);
            if (message.isTimeSignatureMetaEvent())
                message.getTimeSignatureInfo(pattern->numerator, pattern->denominator);
            if (! message.isNoteOn()) continue;
            const int note = gm ? mapGmNote(message.getNoteNumber()) : message.getNoteNumber();
            if (note < firstNote || note >= firstNote + pads)
            { ++ignored; continue; }
            const double beat = std::max(0.0, message.getTimeStamp() / ppq);
            const double end = event->noteOffObject != nullptr
                ? event->noteOffObject->message.getTimeStamp() / ppq : beat + 0.1;
            pattern->hits.push_back({beat, std::max(0.01, end - beat), note, message.getFloatVelocity()});
            if (pattern->hits.size() > 16384) return juce::Result::fail("MIDI loop exceeds 16384 drum hits.");
        }
    }
    if (pattern->hits.empty()) return juce::Result::fail("No notes in the drum range MIDI 36-83: " + file.getFileName());
    const double bar = pattern->numerator * 4.0 / pattern->denominator;
    pattern->length = trackEnd > 0 ? trackEnd : std::max(bar, std::ceil((last - 1.0e-7) / bar) * bar);
    for (const auto& hit : pattern->hits) pattern->length = std::max(pattern->length, hit.beat + 0.001);
    std::stable_sort(pattern->hits.begin(), pattern->hits.end(), [](const Hit& a, const Hit& b) { return a.beat < b.beat; });
    result = std::move(pattern);
    return juce::Result::ok();
}
std::unique_ptr<Pattern> clonePattern(const Pattern& source)
{
    auto result = std::make_unique<Pattern>();
    result->name = source.name; result->length = source.length;
    result->numerator = source.numerator; result->denominator = source.denominator;
    result->hits = source.hits;
    return result;
}
std::unique_ptr<Pattern> gridPattern(const Grid& grid)
{
    auto pattern = std::make_unique<Pattern>();
    pattern->name = "Grid scene " + juce::String(grid.scene + 1);
    const double step = std::array<double, 4>{0.125, 0.25, 0.5, 1.0}[static_cast<size_t>(grid.rate)];
    pattern->length = grid.stepCount * step;
    const auto& scene = grid.scenes[static_cast<size_t>(grid.scene)];
    for (int lane = 0; lane < 12; ++lane)
        for (int s = 0; s < grid.stepCount; ++s)
        {
            const auto velocity = scene.velocity[static_cast<size_t>(lane)][static_cast<size_t>(s)];
            if (velocity != 0)
                pattern->hits.push_back({(s + ((s & 1) != 0 ? grid.swing : 0.0)) * step, step * 0.4,
                    firstNote + lane + 12 * scene.variant[static_cast<size_t>(lane)], velocity / 127.0f});
        }
    std::stable_sort(pattern->hits.begin(), pattern->hits.end(), [](const Hit& a, const Hit& b) { return a.beat < b.beat; });
    return pattern;
}
void humanize(Pattern& pattern, uint32_t seed, double bpm)
{
    juce::Random random(static_cast<juce::int64>(seed));
    for (auto& hit : pattern.hits)
    {
        hit.beat = juce::jlimit(0.0, std::max(0.0, pattern.length - 0.001),
            hit.beat + (random.nextDouble() * 2 - 1) * 0.003 * bpm / 60.0);
        hit.velocity = juce::jlimit(1.0f / 127, 1.0f, hit.velocity + (random.nextFloat() * 2 - 1) * 7.0f / 127);
        hit.duration = std::min(hit.duration, std::max(0.001, pattern.length - hit.beat));
    }
    std::stable_sort(pattern.hits.begin(), pattern.hits.end(), [](const Hit& a, const Hit& b) { return a.beat < b.beat; });
}
juce::Result writeMidi(const Pattern& pattern, juce::OutputStream& stream, double bpm)
{
    constexpr int ppq = 960;
    juce::MidiMessageSequence track;
    track.addEvent(juce::MidiMessage::textMetaEvent(1, "GH_MAP=36-83"));
    track.addEvent(juce::MidiMessage::tempoMetaEvent(static_cast<int>(60000000.0 / bpm)));
    track.addEvent(juce::MidiMessage::timeSignatureMetaEvent(pattern.numerator, pattern.denominator));
    for (const auto& hit : pattern.hits)
    {
        auto on = juce::MidiMessage::noteOn(10, hit.note, hit.velocity);
        on.setTimeStamp(hit.beat * ppq); track.addEvent(on);
        auto off = juce::MidiMessage::noteOff(10, hit.note);
        off.setTimeStamp(std::min(pattern.length, hit.beat + hit.duration) * ppq); track.addEvent(off);
    }
    auto end = juce::MidiMessage::endOfTrack(); end.setTimeStamp(pattern.length * ppq); track.addEvent(end);
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote(ppq); midi.addTrack(track);
    return midi.writeTo(stream, 0) ? juce::Result::ok() : juce::Result::fail("Could not encode MIDI data.");
}
juce::Result writeMidi(const Pattern& pattern, const juce::File& file, double bpm)
{
    auto stream = file.createOutputStream();
    if (! stream) return juce::Result::fail("Could not open MIDI output: " + file.getFullPathName());
    stream->setPosition(0); stream->truncate();
    if (writeMidi(pattern, *stream, bpm).failed() || stream->getStatus().failed())
        return juce::Result::fail("Could not export MIDI: " + file.getFullPathName());
    return juce::Result::ok();
}
juce::var gridToVar(const Grid& grid)
{
    auto* object = new juce::DynamicObject;
    object->setProperty("scene", grid.scene); object->setProperty("rate", grid.rate); object->setProperty("swing", grid.swing);
    object->setProperty("stepCount", grid.stepCount);
    juce::Array<juce::var> scenes;
    for (const auto& scene : grid.scenes)
    {
        juce::Array<juce::var> values;
        for (const auto& lane : scene.velocity) for (const auto value : lane) values.add(static_cast<int>(value));
        for (const auto variant : scene.variant) values.add(static_cast<int>(variant));
        scenes.add(values);
    }
    object->setProperty("scenes", scenes);
    return juce::var(object);
}
juce::Result gridFromVar(const juce::var& data, Grid& grid)
{
    const auto* scenes = data["scenes"].getArray();
    const int scene = static_cast<int>(data["scene"]), rate = static_cast<int>(data["rate"]);
    const float swing = static_cast<float>(data["swing"]);
    const auto length = data["stepCount"];
    const int stepCount = data.hasProperty("stepCount") ? static_cast<int>(length) : 16;
    if (! scenes || scenes->size() != 16 || scene < 0 || scene >= 16 || rate < 0 || rate > 3
        || (data.hasProperty("stepCount") && ! (length.isInt() || length.isInt64()))
        || (stepCount != 16 && stepCount != maxSteps)
        || ! std::isfinite(swing) || swing < 0 || swing > 0.45f)
        return juce::Result::fail("Invalid sequencer state.");
    Grid parsed;
    parsed.scene = scene; parsed.rate = rate; parsed.swing = swing; parsed.stepCount = stepCount;
    for (int s = 0; s < 16; ++s)
    {
        const auto* values = scenes->getReference(s).getArray();
        if (! values || (values->size() != 204 && values->size() != 12 * maxSteps + 12))
            return juce::Result::fail("Invalid scene data.");
        const int storedSteps = (values->size() - 12) / 12;
        const int velocities = 12 * storedSteps;
        for (int i = 0; i < values->size(); ++i)
        {
            const int n = static_cast<int>(values->getReference(i));
            if (n < 0 || n > (i < velocities ? 127 : 3)) return juce::Result::fail("Invalid step velocity or variant.");
            if (i < velocities) parsed.scenes[static_cast<size_t>(s)].velocity[static_cast<size_t>(i / storedSteps)][static_cast<size_t>(i % storedSteps)] = static_cast<uint8_t>(n);
            else parsed.scenes[static_cast<size_t>(s)].variant[static_cast<size_t>(i - velocities)] = static_cast<uint8_t>(n);
        }
    }
    grid = parsed;
    return juce::Result::ok();
}
juce::File findBundledLibrary()
{
    auto file = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
    for (int level = 0; level < 8; ++level, file = file.getParentDirectory())
    {
        for (const auto& relative : {"GHLibrary", "Resources/GHLibrary", "Contents/Resources/GHLibrary",
                                    "VST3/GHOST HEALER DRUMS.vst3/Contents/Resources/GHLibrary"})
        {
            const auto candidate = file.getChildFile(relative);
            if (candidate.getChildFile("catalog.json").existsAsFile()) return candidate;
        }
        for (const auto& extension : file.getChildFile("PlugIns").findChildFiles(juce::File::findDirectories, false, "*.appex"))
            for (const auto& relative : {"GHLibrary", "Resources/GHLibrary"})
                if (extension.getChildFile(relative).getChildFile("catalog.json").existsAsFile())
                    return extension.getChildFile(relative);
    }
    return {};
}
}
