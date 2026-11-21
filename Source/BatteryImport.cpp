#include "Library.h"
#include <optional>

namespace gh
{
namespace
{
constexpr size_t metadataLimit = 64 * 1024 * 1024;
struct InvalidBattery { juce::String message; };
void check(bool condition, const juce::String& message)
{
    if (! condition) throw InvalidBattery {message};
}
uint32_t tag(const char* value) { return juce::ByteOrder::littleEndianInt(value); }

class Bytes
{
public:
    Bytes(const void* bytes, size_t size) : data(static_cast<const uint8_t*>(bytes)), length(size) {}
    size_t remaining() const { return length - position; }
    const uint8_t* take(size_t count)
    {
        check(count <= remaining(), "Truncated Battery metadata.");
        const auto* result = data + position; position += count; return result;
    }
    Bytes block(uint64_t count)
    {
        check(count <= remaining(), "Invalid Battery record length.");
        const auto size = static_cast<size_t>(count);
        return {take(size), size};
    }
    uint8_t u8() { return *take(1); }
    uint16_t u16() { return juce::ByteOrder::littleEndianShort(take(2)); }
    uint32_t u32() { return juce::ByteOrder::littleEndianInt(take(4)); }
    uint64_t u64() { return juce::ByteOrder::littleEndianInt64(take(8)); }
    void finish() const { check(remaining() == 0, "Unexpected trailing Battery metadata."); }
    juce::String text()
    {
        const auto count = u32();
        check(count <= 524288, "Battery text exceeds the metadata limit.");
        auto input = block(static_cast<uint64_t>(count) * 2);
        juce::String result;
        while (input.remaining() != 0)
        {
            uint32_t character = input.u16();
            check(character != 0, "Embedded NUL in Battery text.");
            if (character >= 0xd800 && character <= 0xdbff)
            {
                const auto low = input.u16();
                check(low >= 0xdc00 && low <= 0xdfff, "Invalid Battery UTF-16 text.");
                character = 0x10000 + ((character - 0xd800) << 10) + low - 0xdc00;
            }
            else check(character < 0xdc00 || character > 0xdfff, "Invalid Battery UTF-16 text.");
            result += static_cast<juce::juce_wchar>(character);
        }
        return result;
    }
private:
    const uint8_t* data;
    size_t length, position = 0;
};

void classPayload(Bytes input, uint32_t wanted, int depth, std::optional<Bytes>& result)
{
    check(depth <= 64, "Battery class nesting limit exceeded.");
    const auto kind = input.u32(), id = input.u32();
    check(input.u32() == 1, "Unsupported Battery class version.");
    if (kind == tag("DSIN") && id == 1)
    {
        input.take(12); input.finish(); return;
    }
    const auto length = input.u64();
    check(length > 0, "Invalid Battery base class.");
    classPayload(input.block(length), wanted, depth + 1, result);
    if (kind == tag("DSIN") && id == wanted)
    {
        check(! result.has_value(), "Duplicate Battery document payload.");
        result = input.block(input.remaining());
    }
}

void nativeObject(Bytes input, uint32_t wanted, int depth, size_t& objects,
                  bool& battery, std::optional<Bytes>& result)
{
    check(depth <= 64 && ++objects <= 100000, "Battery object nesting/count limit exceeded.");
    const auto size = input.remaining();
    check(size >= 48 && input.u64() == size, "Invalid Battery container length.");
    check(input.u32() == 1 && input.u32() == tag("hsin") && input.u32() == 1 && input.u32() == 0,
        "Unsupported Battery container header.");
    input.take(16);
    classPayload(input.block(input.u64()), wanted, 0, result);
    while (input.remaining() != 0)
    {
        input.u32();
        const auto kind = input.u32(), id = input.u32();
        if (kind == tag("4KIN") && id == 3) battery = true;
        auto header = input;
        nativeObject(input.block(header.u64()), wanted, depth + 1, objects, battery, result);
    }
}

Bytes nativePayload(Bytes input, uint32_t wanted, bool requireBattery)
{
    std::optional<Bytes> result;
    size_t objects = 0; bool battery = false;
    nativeObject(input, wanted, 0, objects, battery, result);
    check(result.has_value() && (! requireBattery || battery), "Not a supported Battery 4 kit document.");
    return *result;
}

std::vector<uint8_t> fastLz(Bytes input)
{
    const auto first = input.u8();
    const auto level = (first >> 5) + 1;
    check(level == 1 || level == 2, "Unsupported Battery FastLZ compression level.");
    uint8_t control = first & 31;
    std::vector<uint8_t> output;
    output.reserve(256 * 1024);
    for (;;)
    {
        if (control < 32)
        {
            const size_t count = control + 1;
            check(count <= metadataLimit - output.size(), "Decompressed Battery metadata exceeds 64 MiB.");
            const auto* bytes = input.take(count);
            output.insert(output.end(), bytes, bytes + count);
        }
        else
        {
            size_t count = (control >> 5) + 2;
            if (count == 9)
            {
                uint8_t extra;
                do
                {
                    extra = input.u8();
                    check(extra <= metadataLimit - count, "Battery match exceeds the metadata limit.");
                    count += extra;
                } while (level == 2 && extra == 255);
            }
            const size_t high = (control & 31) << 8;
            const auto low = input.u8();
            size_t distance = high + low + 1;
            if (level == 2 && high == 7936 && low == 255)
            {
                const auto upper = input.u8(), lower = input.u8();
                distance = (static_cast<size_t>(upper) << 8) + lower + 8192;
            }
            check(distance <= output.size(), "Invalid Battery compression back-reference.");
            check(count <= metadataLimit - output.size(), "Decompressed Battery metadata exceeds 64 MiB.");
            for (size_t i = 0; i < count; ++i) output.push_back(output[output.size() - distance]);
        }
        if (input.remaining() == 0) return output;
        control = input.u8();
    }
}

Bytes chunk(Bytes input, uint16_t wanted)
{
    std::optional<Bytes> result;
    while (input.remaining() != 0)
    {
        const auto kind = input.u16();
        const auto payload = input.block(input.u32());
        if (kind == wanted)
        {
            check(! result.has_value(), "Duplicate Battery chunk " + juce::String(wanted) + ".");
            result = payload;
        }
    }
    check(result.has_value(), "Missing Battery chunk " + juce::String(wanted) + ".");
    return *result;
}

struct Record { uint16_t kind; Bytes common, properties, body; };
Record record(Bytes& input)
{
    check(input.u8() == 1, "Unsupported Battery record version.");
    const auto kind = input.u16();
    const auto common = input.block(input.u32());
    const auto properties = input.block(input.u32());
    const auto body = input.block(input.u32());
    return {kind, common, properties, body};
}

juce::String location(Bytes& input, bool auxiliary = false)
{
    const auto count = input.u32();
    check(count <= 256, "Battery path component limit exceeded.");
    juce::StringArray parts;
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto kind = input.u8();
        if (kind == 1)
        {
            check(i == 0 && input.text().isEmpty(), "Unsupported Battery volume root.");
            parts.add("");
        }
        else if (kind == 3) parts.add("..");
        else if (kind == 2 || kind == 4)
        {
            auto value = input.text();
            check(value.isNotEmpty() && value != "." && value != ".."
                && ! value.containsAnyOf("/\\:") && (kind != 4 || i + 1 == count),
                "Invalid Battery path component.");
            parts.add(std::move(value));
        }
        else if (auxiliary && kind == 6) check(i == 0, "Invalid Battery auxiliary resource.");
        else if (auxiliary && kind == 8) input.text();
        else throw InvalidBattery {"Unsupported Battery path token."};
    }
    return parts.joinIntoString("/");
}

std::vector<juce::String> samplePaths(Bytes input)
{
    const auto version = input.u16();
    if (version == 2)
    {
        check(input.u32() == 1, "Unsupported Battery path-table header.");
        location(input);
    }
    else check(version == 3, "Unsupported Battery path-table version.");
    const auto count = input.u32();
    check(count <= 100000, "Battery sample path table limit exceeded.");
    std::vector<juce::String> paths; paths.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        if (version == 3) check(input.u64() == 0, "Unsupported Battery resource header.");
        paths.push_back(location(input));
        if (version == 3) input.take(20);
    }
    if (version == 2)
    {
        input.take(static_cast<size_t>(count) * 12);
        const auto auxiliary = input.u32();
        check(auxiliary <= 100000, "Battery auxiliary path-table limit exceeded.");
        for (uint32_t i = 0; i < auxiliary; ++i) location(input, true);
        for (uint32_t i = 0; i < auxiliary; ++i)
            check(input.u16() <= 1, "Unsupported Battery auxiliary path flags.");
    }
    input.finish();
    return paths;
}

juce::File resolveSample(const juce::File& kitFile, const juce::String& path)
{
    check(path.isNotEmpty() && ! juce::File::isAbsolutePath(path),
        "Battery import requires relative external WAV sample references.");
    const auto folder = kitFile.getParentDirectory();
    const auto root = folder.getFileName().equalsIgnoreCase("Kits")
        && folder.getParentDirectory().getChildFile("Samples").isDirectory()
        ? folder.getParentDirectory() : folder;
    const auto file = folder.getChildFile(path);
    check(file.isAChildOf(root), "Battery sample path escapes its library.");
    for (auto ancestor = file; ; ancestor = ancestor.getParentDirectory())
    {
        check(! ancestor.isSymbolicLink(), "Symbolic links are not supported in Battery imports.");
        if (ancestor == root) break;
    }
    check(file.hasFileExtension("wav") && file.existsAsFile(), "Missing external Battery WAV: " + path);
    check(file.getSize() > 0 && file.getSize() <= 128LL * 1024 * 1024, "Battery sample exceeds 128 MiB: " + path);
    return file;
}

size_t gridHeaderSize(uint16_t kind)
{
    switch (kind)
    {
        case 163: return 93; case 164: return 89; case 168: case 171: return 99;
        case 174: case 175: case 178: return 113;
        default: throw InvalidBattery {"Unsupported Battery grid-settings version."};
    }
}

Sample silence()
{
    Sample sample; sample.pcm.assign(2, 0);
    sample.frames = 2; sample.channels = 1; sample.rate = 48000;
    return sample;
}
}

juce::Result readBatteryKit(const juce::File& file, BatteryKit& result)
{
    juce::MemoryBlock bytes;
    if (! file.hasFileExtension("nbkt") || file.isSymbolicLink() || file.getSize() <= 0
        || file.getSize() > static_cast<juce::int64>(metadataLimit))
        return juce::Result::fail("Cannot read Battery kit, or metadata exceeds 64 MiB: " + file.getFileName());
    const auto input = file.createInputStream();
    if (! input || input->getTotalLength() <= 0 || input->getTotalLength() > static_cast<juce::int64>(metadataLimit))
        return juce::Result::fail("Cannot open bounded Battery metadata: " + file.getFileName());
    const auto expected = input->getTotalLength();
    if (input->readIntoMemoryBlock(bytes, metadataLimit + 1) != static_cast<size_t>(expected) || input->getStatus().failed()
        || bytes.getSize() > metadataLimit)
        return juce::Result::fail("Could not read complete bounded Battery metadata: " + file.getFileName());
    try
    {
        auto compressed = nativePayload({bytes.getData(), bytes.getSize()}, 115, true);
        check(compressed.u8() == 0, "Unsupported Battery compression header.");
        auto decoded = fastLz(compressed.block(compressed.u32()));
        compressed.take(8); compressed.finish();
        auto document = nativePayload({decoded.data(), decoded.size()}, 109, false);
        check(document.u32() == 1, "Unsupported Battery document version.");
        const auto records = document.block(document.u64());
        document.take(16); document.finish();
        const auto paths = samplePaths(chunk(records, 75));
        auto kitBytes = chunk(records, 40);
        auto kit = record(kitBytes); kitBytes.finish();
        check(kit.common.remaining() == gridHeaderSize(kit.kind), "Unsupported Battery grid record size.");
        kit.common.take(48);
        const auto rows = kit.common.u32(), columns = kit.common.u32();
        check(rows >= 1 && rows <= 8 && columns >= 1 && columns <= 16, "Invalid Battery grid dimensions.");
        BatteryKit parsed; parsed.name = kit.properties.text().trim();
        check(parsed.name.isNotEmpty() && parsed.name.length() <= 256, "Invalid Battery kit name.");
        auto cells = chunk(kit.body, 51);
        check(cells.u32() == 128, "Unsupported Battery cell capacity.");
        struct Cell { juce::String name; std::optional<size_t> sample; size_t zones = 0; };
        std::array<Cell, 128> entries;
        for (auto& cell : entries)
        {
            auto value = record(cells);
            check(value.kind == 149, "Unsupported Battery cell version.");
            cell.name = value.properties.text();
            check(cell.name.length() <= 256, "Battery cell name is too long.");
        }
        cells.finish();
        auto zones = chunk(kit.body, 52);
        const auto count = zones.u32();
        check(count <= 100000, "Battery sample-zone limit exceeded.");
        for (uint32_t i = 0; i < count; ++i)
        {
            const auto cell = zones.u32(); auto value = record(zones);
            check(cell < entries.size(), "Battery zone references an absent cell.");
            check((value.kind == 152 && value.properties.remaining() == 76)
                || (value.kind == 154 && value.properties.remaining() == 82), "Unsupported Battery sample-zone version.");
            value.properties.take(value.kind == 152 ? 42 : 48);
            const auto index = value.properties.u32();
            check(index < paths.size(), "Battery zone references an absent sample.");
            if (! entries[cell].sample) entries[cell].sample = index;
            ++entries[cell].zones;
        }
        zones.finish();
        juce::int64 total = 0;
        for (size_t i = 0; i < entries.size(); ++i)
        {
            const auto& cell = entries[i];
            if (i / 16 >= rows || i % 16 >= columns)
            {
                if (cell.zones > 0) ++parsed.hiddenCells;
                continue;
            }
            juce::File sample;
            if (cell.sample)
            {
                sample = resolveSample(file, paths[*cell.sample]);
                total += sample.getSize(); parsed.omittedZones += cell.zones - 1;
                check(total <= 512LL * 1024 * 1024, "Battery import exceeds the 512 MiB sample limit.");
            }
            parsed.samples.push_back(sample); parsed.padNames.push_back(cell.name);
        }
        result = std::move(parsed);
        return juce::Result::ok();
    }
    catch (const InvalidBattery& error)
    {
        return juce::Result::fail(file.getFileName() + ": " + error.message);
    }
}

juce::Result loadBatteryBank(const BatteryKit& source, size_t bank, std::unique_ptr<Kit>& result)
{
    const auto banks = (source.samples.size() + pads - 1) / pads;
    if (bank >= banks || source.samples.size() != source.padNames.size())
        return juce::Result::fail("Invalid Battery bank selection.");
    auto kit = std::make_unique<Kit>();
    kit->name = source.name + (banks > 1 ? " - Bank " + juce::String(bank + 1) : juce::String());
    kit->importInfo = "Battery samples only: first zone per cell; processing/original MIDI map omitted. Empty cells are silent.";
    for (size_t i = 0; i < pads; ++i)
    {
        const auto cell = bank * pads + i;
        if (cell < source.samples.size() && source.samples[cell] != juce::File{})
        {
            if (auto r = kit->samples[i].load(source.samples[cell]); r.failed()) return r;
        }
        else kit->samples[i] = silence();
        if (cell < source.padNames.size()) kit->padNames[i] = source.padNames[cell];
        kit->bytes += kit->samples[i].storedBytes();
        if (kit->bytes > 512ULL * 1024 * 1024) return juce::Result::fail("Battery bank exceeds 512 MiB.");
    }
    result = std::move(kit);
    return juce::Result::ok();
}
}
