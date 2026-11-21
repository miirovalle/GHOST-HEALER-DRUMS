#include "MidiClipboard.h"
#include <cstring>
#include <Windows.h>

juce::Result gh::copyMidiToClipboard(const juce::MemoryBlock& data)
{
    if (data.getSize() < 14 || std::memcmp(data.getData(), "MThd", 4) != 0)
        return juce::Result::fail("Cannot copy invalid MIDI data.");
    const auto format = RegisterClipboardFormatW(L"Standard MIDI File");
    if (format == 0) return juce::Result::fail("Could not register the MIDI clipboard format.");
    struct Memory
    {
        HGLOBAL handle;
        ~Memory() { if (handle != nullptr) GlobalFree(handle); }
    } memory {GlobalAlloc(GMEM_MOVEABLE, data.getSize())};
    if (memory.handle == nullptr) return juce::Result::fail("Could not allocate MIDI clipboard data.");
    auto* bytes = GlobalLock(memory.handle);
    if (bytes == nullptr) return juce::Result::fail("Could not access MIDI clipboard data.");
    std::memcpy(bytes, data.getData(), data.getSize());
    GlobalUnlock(memory.handle);
    struct Window
    {
        HWND handle;
        ~Window() { if (handle != nullptr) DestroyWindow(handle); }
    } window {CreateWindowExW(0, L"STATIC", L"Ghost MIDI clipboard", 0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr)};
    if (window.handle == nullptr) return juce::Result::fail("Could not create a MIDI clipboard owner.");
    if (! OpenClipboard(window.handle))
        return juce::Result::fail("Clipboard is busy. Try Copy to MIDI clipboard again.");
    struct Clipboard { ~Clipboard() { CloseClipboard(); } } clipboard;
    if (! EmptyClipboard() || SetClipboardData(format, memory.handle) == nullptr)
        return juce::Result::fail("Windows could not write MIDI to the clipboard.");
    memory.handle = nullptr;
    return juce::Result::ok();
}
