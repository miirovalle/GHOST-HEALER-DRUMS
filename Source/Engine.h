#pragma once
#include "Library.h"
#include <cmath>
#include <limits>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace gh
{
class Engine
{
public:
    void prepare(double rate)
    {
        sampleRate = rate;
        smoothing = 1.0f - std::exp(-1.0f / (0.005f * static_cast<float>(rate)));
        attack = std::exp(-1.0f / (0.005f * static_cast<float>(rate)));
        release = std::exp(-1.0f / (0.09f * static_cast<float>(rate)));
        dirty = true;
        reset();
    }
    void reset()
    {
        voices.fill({});
        activeVoiceCount = activePadCount = 0; previousPads = 0;
        for (auto& f : filters) { f.stateL.fill(0); f.stateR.fill(0); f.envelope = 0; }
    }
    void setKit(Kit* next) { reset(); kit = next; }
    void setValues(const std::array<PadValues, pads>& next, float masterDb, float saturation)
    {
        for (size_t i = 0; i < values.size(); ++i)
        {
            const auto& n = next[i]; const auto& old = values[i];
            auto& f = filters[i];
            if (dirty || n.lowCut != old.lowCut)
                f.hp = n.lowCut <= 0 ? 0 : std::exp(-2.0f * pi * n.lowCut / static_cast<float>(sampleRate));
            if (dirty || n.highCut != old.highCut)
                f.lp = n.highCut >= 19999 ? 1 : 1 - std::exp(-2.0f * pi * std::min(n.highCut, static_cast<float>(sampleRate) * 0.45f) / static_cast<float>(sampleRate));
            if (dirty || n.level != old.level) f.levelTarget = std::pow(10.0f, n.level / 20.0f);
            if (dirty || n.compressor != old.compressor)
            {
                f.threshold = std::pow(10.0f, -30.0f * n.compressor / 20.0f);
                f.makeup = std::pow(10.0f, 6.0f * n.compressor / 20.0f);
            }
            if (n.tune != old.tune || n.velocityPitch != old.velocityPitch)
                for (auto& v : voices)
                    if (v.sample && v.slot == static_cast<int>(i))
                        v.increment *= std::exp2((n.tune - old.tune
                            + (n.velocityPitch - old.velocityPitch) * (v.velocity - 0.5f) * 2) / 12.0f);
        }
        values = next;
        if (dirty || masterDb != lastMaster) masterTarget = std::pow(10.0f, masterDb / 20.0f);
        lastMaster = masterDb; saturationTarget = saturation; dirty = false;
    }
    void trigger(int slot, float velocity, bool fromLoop = false)
    {
        if (! kit || slot < 0 || slot >= pads) return;
        Voice* chosen = nullptr;
        bool audible = false, padPlaying = false;
        for (auto& v : voices)
        {
            if (! v.sample && ! chosen) chosen = &v;
            if (v.sample) { audible = true; if (v.slot == slot) padPlaying = true; }
        }
        const bool addingVoice = chosen != nullptr;
        if (! chosen) chosen = &*std::min_element(voices.begin(), voices.end(),
            [](const Voice& a, const Voice& b) { return a.serial < b.serial; });
        const auto& sample = kit->samples[static_cast<size_t>(slot)];
        const auto& p = values[static_cast<size_t>(slot)];
        if (! audible) { master = masterTarget; sat = saturationTarget; }
        if (! padPlaying) { filters[static_cast<size_t>(slot)].level = filters[static_cast<size_t>(slot)].levelTarget; filters[static_cast<size_t>(slot)].pan = p.pan; }
        *chosen = {};
        chosen->sample = &sample; chosen->slot = slot; chosen->velocity = velocity;
        chosen->increment = sample.rate / sampleRate * std::exp2((p.tune + p.velocityPitch * (velocity - 0.5f) * 2) / 12.0f);
        chosen->serial = ++serial; chosen->loop = fromLoop;
        if (addingVoice)
        {
            // Reused voice slots must retain the original floating-point summation order.
            const auto index = static_cast<uint8_t>(chosen - voices.data());
            const auto end = voiceIndices.begin() + static_cast<ptrdiff_t>(activeVoiceCount);
            const auto position = std::lower_bound(voiceIndices.begin(), end, index);
            std::move_backward(position, end, end + 1);
            *position = index; ++activeVoiceCount;
        }
    }
    void fadeVoices(bool loopOnly, float ms)
    {
        for (auto& voice : voices)
            if (voice.sample && (! loopOnly || voice.loop))
            {
                const int frames = std::max(1, static_cast<int>(sampleRate * ms / 1000));
                if (voice.fade < 0 || voice.fade > frames) voice.fade = voice.fadeTotal = frames;
            }
    }
    bool hasVoices() const { return activeVoiceCount != 0; }
    void process(float& left, float& right)
    {
        std::array<float, pads> l {}, r {};
        uint64_t activePads = 0;
        size_t retained = 0;
        for (size_t v = 0; v < activeVoiceCount; ++v)
        {
            const auto indexInVoices = voiceIndices[v];
            auto& voice = voices[indexInVoices];
            const auto& sample = *voice.sample;
            const auto index = static_cast<size_t>(voice.position);
            if (index + 1 >= sample.frames) { voice.sample = nullptr; continue; }
            // Cache overlapping decoded frames, not expanded copies of whole samples.
            if (index != voice.frameIndex)
            {
                if (index > 0 && index - 1 == voice.frameIndex)
                { voice.a = voice.c; voice.b = voice.d; }
                else sample.frame(index, voice.a, voice.b);
                sample.frame(index + 1, voice.c, voice.d);
                voice.frameIndex = index;
            }
            const float fraction = static_cast<float>(voice.position - static_cast<double>(index));
            float gain = voice.velocity;
            // The 48-frame tail fade is the only place a division is needed. Skipping it
            // above the fade keeps the render bit-identical while removing a double
            // division from every voice of every sample on the hot path.
            if (voice.position > static_cast<double>(sample.frames) - 48.0)
                gain *= static_cast<float>(std::min(1.0, (static_cast<double>(sample.frames) - voice.position) / 48));
            if (voice.fade >= 0)
            {
                gain *= static_cast<float>(voice.fade) / static_cast<float>(voice.fadeTotal);
                if (--voice.fade <= 0) { voice.sample = nullptr; continue; }
            }
            const auto slot = static_cast<size_t>(voice.slot);
            l[slot] += (voice.a + fraction * (voice.c - voice.a)) * gain;
            r[slot] += (voice.b + fraction * (voice.d - voice.b)) * gain;
            activePads |= uint64_t{1} << slot;
            voice.position += voice.increment;
            voiceIndices[retained++] = indexInVoices;
        }
        activeVoiceCount = retained;
        left = right = 0;
        if (previousPads != activePads)
        {
            auto endedPads = previousPads & ~activePads;
            while (endedPads != 0)
            {
                auto& f = filters[firstSetBit(endedPads)];
                f.stateL.fill(0); f.stateR.fill(0); f.envelope = 0;
                endedPads &= endedPads - 1;
            }
            previousPads = activePads; activePadCount = 0;
            while (activePads != 0)
            {
                padIndices[activePadCount++] = static_cast<uint8_t>(firstSetBit(activePads));
                activePads &= activePads - 1;
            }
        }
        for (size_t pad = 0; pad < activePadCount; ++pad)
        {
            const auto i = padIndices[pad];
            auto& f = filters[i];
            const auto& p = values[i];
            f.level += smoothing * (f.levelTarget - f.level);
            f.pan += smoothing * (p.pan - f.pan);
            float a = filter(l[i], f.stateL, f), b = filter(r[i], f.stateR, f);
            if (p.compressor > 0.001f)
            {
                const float peak = std::max(std::abs(a), std::abs(b));
                const float coefficient = peak > f.envelope ? attack : release;
                f.envelope = peak + coefficient * (f.envelope - peak);
                float gain = 1;
                if (f.envelope > f.threshold)
                {
                    const float ratio = f.threshold / f.envelope;
                    gain = std::sqrt(ratio) * std::sqrt(std::sqrt(ratio));
                }
                a *= gain * f.makeup; b *= gain * f.makeup;
            }
            left += a * f.level * std::min(1.0f, 1 - f.pan);
            right += b * f.level * std::min(1.0f, 1 + f.pan);
        }
        master += smoothing * (masterTarget - master);
        sat += smoothing * (saturationTarget - sat);
        left *= master; right *= master;
        if (sat > 0.0001f)
        {
            left = left * (1 + sat) / (1 + 4 * sat * std::abs(left));
            right = right * (1 + sat) / (1 + 4 * sat * std::abs(right));
        }
        left = juce::jlimit(-0.999f, 0.999f, left); right = juce::jlimit(-0.999f, 0.999f, right);
    }
private:
    // Rebuild sorted pad indices only when the set of sounding pads changes.
    static size_t firstSetBit(uint64_t bits)
    {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
        unsigned long index = 0;
        _BitScanForward64(&index, bits);
        return static_cast<size_t>(index);
#elif defined(__clang__) || defined(__GNUC__)
        return static_cast<size_t>(__builtin_ctzll(bits));
#else
        size_t index = 0;
        while ((bits & 1) == 0) { bits >>= 1; ++index; }
        return index;
#endif
    }
    struct Voice
    {
        const Sample* sample = nullptr;
        double position = 0, increment = 1;
        float velocity = 1;
        int slot = 0, fade = -1, fadeTotal = 1;
        uint64_t serial = 0;
        bool loop = false;
        size_t frameIndex = std::numeric_limits<size_t>::max();
        float a = 0, b = 0, c = 0, d = 0;
    };
    struct Filter
    {
        std::array<float, 4> stateL {}, stateR {};
        float hp = 0, lp = 1, level = 1, levelTarget = 1, pan = 0;
        float threshold = 1, makeup = 1, envelope = 0;
    };
    static float filter(float input, std::array<float, 4>& state, const Filter& f)
    {
        if (f.hp > 0)
            for (int i = 0; i < 2; ++i)
            {
                const float low = (1 - f.hp) * input + f.hp * state[static_cast<size_t>(i)];
                state[static_cast<size_t>(i)] = low; input -= low;
            }
        if (f.lp < 1)
            for (int i = 2; i < 4; ++i)
            {
                state[static_cast<size_t>(i)] += f.lp * (input - state[static_cast<size_t>(i)]);
                input = state[static_cast<size_t>(i)];
            }
        return input;
    }
    static constexpr float pi = 3.14159265358979323846f;
    Kit* kit = nullptr;
    std::array<Voice, 64> voices {};
    std::array<PadValues, pads> values {};
    std::array<Filter, pads> filters {};
    uint64_t serial = 0;
    std::array<uint8_t, 64> voiceIndices {};
    std::array<uint8_t, pads> padIndices {};
    size_t activeVoiceCount = 0, activePadCount = 0;
    uint64_t previousPads = 0;
    double sampleRate = 48000;
    float smoothing = 0.004f, attack = 0, release = 0;
    float master = 0.25f, masterTarget = 0.25f, lastMaster = -12;
    float sat = 0, saturationTarget = 0;
    bool dirty = true;
};
class Clock
{
public:
    void start(float velocity = 1, bool repeat = true) { position = 0; cursor = 0; active = true; gain = velocity; looping = repeat; }
    void stop() { active = false; }
    void setPattern(const Pattern* next) { pattern = next; position = 0; cursor = 0; }
    template <typename Trigger>
    void process(double beatsPerSample, Trigger&& trigger)
    {
        if (! active || ! pattern) return;
        const double end = position + beatsPerSample;
        while (cursor < pattern->hits.size() && pattern->hits[cursor].beat < end)
        {
            const auto& hit = pattern->hits[cursor++];
            trigger(hit.note - firstNote, hit.velocity * gain, hit.duration);
        }
        position = end;
        if (position >= pattern->length)
        {
            position -= pattern->length;
            cursor = 0;
            if (! looping) active = false;
        }
    }
    double getPosition() const { return position; }
    bool isActive() const { return active; }
private:
    const Pattern* pattern = nullptr;
    double position = 0;
    size_t cursor = 0;
    bool active = false, looping = true;
    float gain = 1;
};
}
