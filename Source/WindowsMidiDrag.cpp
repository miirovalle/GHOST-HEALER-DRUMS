#include "Editor.h"
#include <Windows.h>
#include <ShlObj.h>

juce::Result dragMidiFileOnWindows(const juce::File& file, juce::Component& source)
{
    const auto status = OleInitialize(nullptr);
    if (FAILED(status))
        return juce::Result::fail("Windows could not initialise MIDI drag (OLE " + juce::String(static_cast<int>(status)) + ").");
    struct OleScope { ~OleScope() { OleUninitialize(); } } oleScope;
    struct Pidl { PIDLIST_ABSOLUTE value; ~Pidl() { ILFree(value); } };
    const Pidl parent {ILCreateFromPathW(file.getParentDirectory().getFullPathName().toWideCharPointer())};
    const Pidl item {ILCreateFromPathW(file.getFullPathName().toWideCharPointer())};
    if (! parent.value || ! item.value)
        return juce::Result::fail("Windows could not prepare the MIDI file for dragging. Use Save MIDI file.");
    PCUITEMID_CHILD children[] {ILFindLastID(item.value)};
    IDataObject* data = nullptr;
    const auto created = SHCreateDataObject(parent.value, 1, children, nullptr, IID_PPV_ARGS(&data));
    if (FAILED(created))
        return juce::Result::fail("Windows could not create MIDI drag data (OLE " + juce::String(static_cast<int>(created)) + ").");
    DWORD effect = DROPEFFECT_NONE;
    const auto* peer = source.getPeer();
    if (peer == nullptr)
    {
        data->Release();
        return juce::Result::fail("MIDI drag needs a visible plugin window.");
    }
    const auto window = reinterpret_cast<HWND>(peer->getNativeHandle());
    if (GetCapture() == window) ReleaseCapture();
    // Keep the shell drag loop on the source UI thread, not JUCE's Windows worker.
    const auto dropped = SHDoDragDrop(window, data, nullptr, DROPEFFECT_COPY, &effect);
    data->Release();
    if (dropped == DRAGDROP_S_CANCEL) return juce::Result::ok();
    if (FAILED(dropped))
        return juce::Result::fail("Native MIDI drag failed (OLE " + juce::String(static_cast<int>(dropped)) + ").");
    if (effect == DROPEFFECT_NONE)
        return juce::Result::fail("The target did not accept MIDI. In FL Studio use the instrument's Piano roll or Channel Rack, or Save MIDI file.");
    return juce::Result::ok();
}
