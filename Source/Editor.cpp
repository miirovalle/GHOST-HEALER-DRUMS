#include "Editor.h"

namespace
{
const juce::Colour gold {gh::theme::textLight};
void slider(juce::Slider& s)
{
    s.setSliderStyle(juce::Slider::LinearHorizontal);
    s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 62, 28);
    s.setMouseDragSensitivity(160);
}
void knob(juce::Slider& s)
{
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 94, 28);
    s.setColour(juce::Slider::textBoxTextColourId, gh::theme::textLight);
    s.setColour(juce::Slider::textBoxOutlineColourId, gh::theme::inkDim);
    s.setColour(juce::Slider::textBoxBackgroundColourId, gh::theme::back);
    s.setMouseDragSensitivity(160);
}
juce::Colour roleColour(int role)
{
    constexpr std::array<uint32_t, 12> colours {
        0xffff9548, 0xffff9548, 0xfff36882, 0xffefd55b, 0xfff36882, 0xffb48cec,
        0xffb48cec, 0xff52cfec, 0xff769bff, 0xffacd65b, 0xff55d59b, 0xffee85cf
    };
    return juce::Colour(colours[static_cast<size_t>(role)]);
}
}
void DrumEditor::PadButton::paintButton(juce::Graphics& g, bool hover, bool down)
{
    const auto r = getLocalBounds().toFloat().reduced(1.5f);
    const bool on = getToggleState();

    auto fill = on ? gh::theme::inkBright : gh::theme::ink;
    if (hover) fill = fill.brighter(0.05f);
    if (down) fill = fill.darker(0.09f);

    gh::theme::filledBox(g, r, gh::theme::padCorner, fill);

    g.setColour(gh::theme::textDark.withAlpha(on ? 0.90f : 0.62f));
    g.setFont(gh::theme::font(15.0f, true));
    g.drawText(gh::noteLabel(slot), 3, 4, getWidth() - 6, 18, juce::Justification::centred);
    g.setFont(gh::theme::font(12.0f));
    g.drawFittedText(gh::roles[static_cast<size_t>(slot % 12)], 3, 23, getWidth() - 6, 22, juce::Justification::centred, 1);
}
DrumEditor::CutoffBar::CutoffBar()
{
    range.setSkewForCentre(1000);
    setName("Low / High cut");
    setWantsKeyboardFocus(true);
    setViewportIgnoreDragFlag(true);
    setTooltip("Low cut: left handle (upper on horizontal bar). High cut: right handle (lower on horizontal bar). Low 0 = off. Up/down selects a handle; left/right adjusts it.");
}
DrumEditor::CutoffBar::~CutoffBar() { finishGesture(); }
void DrumEditor::CutoffBar::finishGesture()
{
    if (dragging) { attachments[static_cast<size_t>(handle)]->endGesture(); dragging = false; }
}
void DrumEditor::CutoffBar::bind(juce::RangedAudioParameter& low, juce::RangedAudioParameter& high)
{
    finishGesture();
    for (auto& attachment : attachments) attachment.reset();
    parameters = {&low, &high};
    for (size_t i = 0; i < attachments.size(); ++i)
    {
        attachments[i] = std::make_unique<juce::ParameterAttachment>(*parameters[i],
            [this, i](float value) { values[i] = value; repaint(); });
        attachments[i]->sendInitialUpdate();
    }
}
juce::Point<float> DrumEditor::CutoffBar::handlePosition(int index) const
{
    if (getHeight() > getWidth())
        return {getWidth() * 0.5f + (index == 0 ? -10.0f : 10.0f),
            32 + std::max(1, getHeight() - 74) * (1 - range.convertTo0to1(values[static_cast<size_t>(index)]))};
    return {14 + std::max(1, getWidth() - 28) * range.convertTo0to1(values[static_cast<size_t>(index)]),
        index == 0 ? 20.0f : 36.0f};
}
void DrumEditor::CutoffBar::paint(juce::Graphics& g)
{
    gh::theme::outlinedBox(g, getLocalBounds().toFloat().reduced(0.5f), 3.0f);

    const auto low = handlePosition(0), high = handlePosition(1);
    if (getHeight() > getWidth())
    {
        const auto x = getWidth() * 0.5f;
        const auto top = 32.0f, bottom = static_cast<float>(getHeight()) - 42.0f;

        g.setColour(gh::theme::inkDim);
        g.fillRect(x - 3.8f, top, 1.2f, bottom - top);
        g.fillRect(x + 2.6f, top, 1.2f, bottom - top);

        g.setColour(gh::theme::ink);
        g.fillRect(x - 3.8f, std::min(low.y, high.y), 7.6f, std::abs(low.y - high.y));

        for (const auto point : {low, high})
        {
            const auto thumb = juce::Rectangle<float>(point.x - 9.0f, point.y - 5.0f, 18.0f, 10.0f);
            g.setColour(gh::theme::inkBright);
            g.fillRect(thumb);
            g.setColour(gh::theme::inkDim);
            g.drawRect(thumb, 1.0f);
        }

        g.setColour(gh::theme::textDim);
        g.setFont(gh::theme::font(12.0f));
        g.drawText("High " + juce::String(values[1], 0), 4, 6, getWidth() - 8, 20, juce::Justification::centred);
        g.drawText(values[0] == 0 ? "Low off" : "Low " + juce::String(values[0], 0),
            4, getHeight() - 26, getWidth() - 8, 20, juce::Justification::centred);
        return;
    }

    g.setColour(gh::theme::inkDim);
    g.fillRect(14.0f, 27.0f, static_cast<float>(getWidth()) - 28.0f, 1.2f);
    g.fillRect(14.0f, 29.0f, static_cast<float>(getWidth()) - 28.0f, 1.2f);

    g.setColour(gh::theme::ink);
    g.fillRect(std::min(low.x, high.x), 27.0f, std::abs(low.x - high.x), 3.2f);

    for (const auto point : {low, high})
    {
        const auto thumb = juce::Rectangle<float>(point.x - 9.0f, point.y - 5.0f, 18.0f, 10.0f);
        g.setColour(gh::theme::inkBright);
        g.fillRect(thumb);
        g.setColour(gh::theme::inkDim);
        g.drawRect(thumb, 1.0f);
    }

    g.setColour(gh::theme::textDim);
    g.setFont(gh::theme::font(12.0f));
    g.drawText(values[0] == 0 ? "Low off" : "Low " + juce::String(values[0], 0),
        6, 52, getWidth() / 2 - 6, 22, juce::Justification::centredLeft);
    g.drawText("High " + juce::String(values[1], 0), getWidth() / 2, 52, getWidth() / 2 - 6, 22, juce::Justification::centredRight);
}
void DrumEditor::CutoffBar::mouseDown(const juce::MouseEvent& event)
{
    if (! attachments[0] || ! attachments[1]) return;
    finishGesture();
    handle = event.position.getDistanceFrom(handlePosition(0)) <= event.position.getDistanceFrom(handlePosition(1)) ? 0 : 1;
    attachments[static_cast<size_t>(handle)]->beginGesture(); dragging = true;
    grabKeyboardFocus();
    mouseDrag(event);
}
void DrumEditor::CutoffBar::mouseDrag(const juce::MouseEvent& event)
{
    if (! dragging) return;
    const auto index = static_cast<size_t>(handle);
    const auto position = juce::jlimit(0.0f, 1.0f, getHeight() > getWidth()
        ? 1 - (event.position.y - 32) / std::max(1, getHeight() - 74)
        : (event.position.x - 14) / std::max(1, getWidth() - 28));
    const auto value = parameters[index]->getNormalisableRange().snapToLegalValue(range.convertFrom0to1(position));
    attachments[index]->setValueAsPartOfGesture(value);
}
void DrumEditor::CutoffBar::mouseUp(const juce::MouseEvent&) { finishGesture(); }
bool DrumEditor::CutoffBar::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
    { handle = key == juce::KeyPress::upKey ? 0 : 1; return true; }
    if (key != juce::KeyPress::leftKey && key != juce::KeyPress::rightKey) return false;
    const auto index = static_cast<size_t>(handle);
    if (! attachments[index]) return false;
    const float delta = key == juce::KeyPress::leftKey ? -0.01f : 0.01f;
    const auto value = range.convertFrom0to1(juce::jlimit(0.0f, 1.0f, range.convertTo0to1(values[index]) + delta));
    attachments[index]->setValueAsCompleteGesture(parameters[index]->getNormalisableRange().snapToLegalValue(value));
    return true;
}
void DrumEditor::ExportButton::mouseDown(const juce::MouseEvent& event)
{
    if (! dragging)
    {
        juce::TextButton::mouseDown(event);
        dragFile = processor.exportMidi();
    }
}
void DrumEditor::ExportButton::mouseDrag(const juce::MouseEvent& event)
{
    if (dragging || event.getDistanceFromDragStart() < 8) return;
    if (! dragFile.existsAsFile()) return;
    dragging = true;
    const auto safe = juce::Component::SafePointer<ExportButton>(this);
    launch(dragFile, *this, [safe](juce::Result result)
    {
        if (! safe) return;
        safe->dragging = false; safe->setState(juce::Button::buttonNormal);
        if (result.failed()) safe->processor.report(result.getErrorMessage(), true);
    });
}
void DrumEditor::ExportButton::mouseUp(const juce::MouseEvent& event)
{
    if (! dragging) juce::TextButton::mouseUp(event);
    else setState(juce::Button::buttonNormal);
}
namespace
{
void launchMidiFileDrag(const juce::File& file, juce::Component& source, std::function<void(juce::Result)> finish)
{
#if JUCE_WINDOWS
    finish(dragMidiFileOnWindows(file, source));
#elif JUCE_IOS
    (void)file; (void)source;
    finish(juce::Result::fail("Use the loop name's native iPad drag or Export MIDI."));
#else
    if (! juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()}, false, &source,
        [finish] { finish(juce::Result::ok()); }))
        finish(juce::Result::fail("Could not start native MIDI drag. Use Save MIDI file."));
#endif
}
}
DrumEditor::LoopName::LoopName(DrumProcessor& p, MidiDragLauncher& launcher, std::function<juce::File()> exportFile)
    : TextButton("Use grid"), processor(p), launch(launcher)
{
    exporter = exportFile ? std::move(exportFile) : std::function<juce::File()>([&p] { return p.exportMidi(); });
    resetGesture();
}
void DrumEditor::LoopName::resetGesture()
{
    stopTimer(); pressed = armed = cancelled = dragging = false;
    setState(juce::Button::buttonNormal);
    setViewportIgnoreDragFlag(false);
    setTooltip("Drag the loop name with a mouse. On touchscreens hold for 0.5 seconds, then drag. Use the separate arrow to choose a loop.");
    repaint();
}
void DrumEditor::LoopName::mouseDown(const juce::MouseEvent& event)
{
    if (dragging || ! isEnabled()) return;
    resetGesture();
    if (! event.mods.isLeftButtonDown()) return;
    pressed = true;
    setState(juce::Button::buttonDown);
    setViewportIgnoreDragFlag(event.source.isMouse());
    startTimer(500);
}
void DrumEditor::LoopName::timerCallback()
{
    stopTimer();
    if (! pressed || cancelled || dragging) return;
    dragFile = exporter();
    if (! dragFile.existsAsFile()) { cancelled = true; return; }
    armed = true; setViewportIgnoreDragFlag(true);
    setTooltip("Drag ready: move the loop name into the Piano roll or Channel Rack, then release. Escape cancels.");
    repaint();
}
void DrumEditor::LoopName::mouseDrag(const juce::MouseEvent& event)
{
    if (! pressed || dragging || cancelled || event.getDistanceFromDragStart() < 8) return;
    if (! armed)
    {
        if (! event.source.isMouse()) { cancelled = true; stopTimer(); return; }
        timerCallback();
        if (! armed) return;
    }
    dragging = true;
    const auto safe = juce::Component::SafePointer<LoopName>(this);
    launch(dragFile, *this, [safe](juce::Result result)
    {
        if (! safe) return;
        safe->resetGesture();
        if (result.failed()) safe->processor.report(result.getErrorMessage(), true);
    });
}
void DrumEditor::LoopName::mouseUp(const juce::MouseEvent& event)
{
    if (dragging) return;
    const bool tap = pressed && ! armed && ! cancelled && event.getDistanceFromDragStart() < 8;
    resetGesture();
    if (tap && onClick) onClick();
}
void DrumEditor::LoopName::paintButton(juce::Graphics& g, bool over, bool down)
{
    juce::TextButton::paintButton(g, over, down);
    if (armed)
    {
        g.setColour(gold);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1), 3, 2);
    }
}
DrumEditor::LoopItem::LoopItem(DrumProcessor& p, MidiDragLauncher& launcher, DrumEditor& editor, const gh::Entry& entry)
    : CustomComponent(false), processor(p), loopEntry(entry),
      launchFromEditor([&launcher, safe = juce::Component::SafePointer<DrumEditor>(&editor)]
          (const juce::File& file, juce::Component& source, std::function<void(juce::Result)> finish)
          {
              const auto snapshot = file;
              const auto sourceSafe = juce::Component::SafePointer<juce::Component>(&source);
              auto completed = [safe, sourceSafe, finish = std::move(finish)](juce::Result result)
              {
                  if (result.failed() && ! sourceSafe && safe) safe->processor.report(result.getErrorMessage(), true);
                  finish(result);
              };
              juce::PopupMenu::dismissAllActiveMenus();
              if (safe) launcher(snapshot, *safe, std::move(completed));
              else completed(juce::Result::fail("The plugin window closed before MIDI drag could start."));
          })
{}
void DrumEditor::LoopItem::getIdealSize(int& width, int& height) { width = 360; height = 44; }
void DrumEditor::LoopItem::resized()
{
    if (name) name->setBounds(getLocalBounds().reduced(1));
#if JUCE_IOS
    if (nativeName && name) nativeName->setBounds(name->getBounds());
#endif
}
void DrumEditor::LoopItem::parentHierarchyChanged()
{
    if (getParentComponent() != nullptr && ! name)
    {
        name = std::make_unique<LoopName>(processor, launchFromEditor, [this]
        {
            const auto exported = processor.exportLoopMidi(loopEntry.file);
            if (exported.existsAsFile()) processor.chooseLoop(loopEntry.file, false, true);
            return exported;
        });
        name->setButtonText(loopEntry.name);
        name->setName("MIDI loop drag handle");
        name->onClick = [this] { triggerMenuItem(); };
        addAndMakeVisible(*name);
        resized();
    }
    else if (getParentComponent() == nullptr)
    {
#if JUCE_IOS
        nativeName.reset();
#endif
        name.reset();
    }
#if JUCE_IOS
    if (getParentComponent() != nullptr && ! nativeName)
    {
        nativeName = makeMidiNameDrag(name->exporter,
            [this](juce::String error) { processor.report(error, true); },
            [this] { triggerMenuItem(); });
        setMidiNameDragText(*nativeName, name->getButtonText());
        addAndMakeVisible(*nativeName);
        resized();
    }
#endif
}
DrumEditor::VelocitySlider::VelocitySlider()
{
    setSliderStyle(juce::Slider::LinearBarVertical);
    setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    setRange(0, 127, 1);
    setDoubleClickReturnValue(false, 0);
    setSliderSnapsToMousePosition(false);
    setMouseDragSensitivity(44);
    setViewportIgnoreDragFlag(true);
}
void DrumEditor::VelocitySlider::mouseDown(const juce::MouseEvent& event)
{
    if (! isEnabled() || ! event.mods.isLeftButtonDown()) return;
    pressed = true;
    const bool doubleTap = event.getNumberOfClicks() >= 2;
    if (doubleTap && toggledLastTap)
    {
        setValue(valueBeforeTap, juce::sendNotificationSync);
        toggledLastTap = false;
    }
    if (selectStep) selectStep(doubleTap);
    sliderGesture = velocityUnlocked;
    if (sliderGesture) juce::Slider::mouseDown(event);
    else valueBeforeTap = getValue();
}
void DrumEditor::VelocitySlider::mouseDrag(const juce::MouseEvent& event)
{
    if (sliderGesture && velocityUnlocked) juce::Slider::mouseDrag(event);
}
void DrumEditor::VelocitySlider::mouseUp(const juce::MouseEvent& event)
{
    if (! pressed) return;
    if (sliderGesture)
    {
        juce::Slider::mouseUp(event);
        if (getValue() > 0) lastVelocity = getValue();
    }
    else if (event.getDistanceFromDragStart() < 8)
    {
        if (getValue() > 0) lastVelocity = getValue();
        setValue(getValue() > 0 ? 0 : lastVelocity, juce::sendNotificationSync);
        toggledLastTap = true;
    }
    sliderGesture = false; pressed = false;
}
void DrumEditor::VelocitySlider::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (velocityUnlocked) juce::Slider::mouseWheelMove(event, wheel);
}
bool DrumEditor::VelocitySlider::keyPressed(const juce::KeyPress& key)
{
    return velocityUnlocked && juce::Slider::keyPressed(key);
}
void DrumEditor::VelocitySlider::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced(2);
    // Four-step groups stay visually distinct, using two neutral greys.
    const auto base = juce::Colour(greenGroup ? 0xff464646 : 0xff525252);
    const auto fill = juce::Colour(greenGroup ? 0xff6f6f6f : 0xff8b8b8b);

    g.setColour(base); g.fillRoundedRectangle(bounds, 4);
    if (getValue() > 0)
    {
        const float height = bounds.getHeight() * static_cast<float>(getValue() / 127);
        g.setColour(fill);
        g.fillRoundedRectangle(bounds.withTop(bounds.getBottom() - height), 3);
    }
    g.setColour(selected ? juce::Colours::white : gh::theme::inkDim);
    g.drawRoundedRectangle(bounds, 4, selected ? 2.0f : 1.0f);
    if (velocityUnlocked)
    {
        const float top = bounds.getBottom() - bounds.getHeight() * static_cast<float>(getValue() / 127);
        g.setColour(gh::theme::inkBright);
        g.drawLine(bounds.getX() + 4, juce::jlimit(bounds.getY() + 2, bounds.getBottom() - 2, top),
            bounds.getRight() - 4, juce::jlimit(bounds.getY() + 2, bounds.getBottom() - 2, top), 2);
    }
}
DrumEditor::SequencerWindow::SequencerWindow(juce::Component& content,
    std::function<void()> layout, std::function<void()> close)
    : DocumentWindow("GHOST HEALER - Sequencer", gh::theme::back, closeButton, false),
      layoutCallback(std::move(layout)), closeCallback(std::move(close))
{
    setTitleBarHeight(48);
#if JUCE_IOS
    setUsingNativeTitleBar(false);
    setDraggable(false);
    setResizable(false, false);
#else
    setUsingNativeTitleBar(true);
    setResizable(true, false);
#endif
    setResizeLimits(360, 360, 1800, 1400);
    setContentNonOwned(&content, false);
}
void DrumEditor::SequencerWindow::closeButtonPressed()
{
    setVisible(false);
    if (closeCallback) closeCallback();
}
void DrumEditor::SequencerWindow::resized()
{
    juce::DocumentWindow::resized();
    if (layoutCallback) layoutCallback();
}
DrumEditor::DrumEditor(DrumProcessor& p, MidiDragLauncher launcher)
    : AudioProcessorEditor(p), processor(p), dragLauncher(launcher ? std::move(launcher) : launchMidiFileDrag),
      loopName(p, dragLauncher), exportButton(p, dragLauncher)
{
    look.setColour(juce::ResizableWindow::backgroundColourId, gh::theme::back);
    look.setColour(juce::TextButton::buttonColourId, gh::theme::ink);
    look.setColour(juce::TextButton::buttonOnColourId, gh::theme::inkBright);
    setLookAndFeel(&look);
    look.setPopupsStayInsideEditor(processor.wrapperType == juce::AudioProcessor::wrapperType_AudioUnitv3);
    viewport.setViewedComponent(&body, false); addAndMakeVisible(viewport);
    gridViewport.setViewedComponent(&gridBody, false); sequenceBody.addAndMakeVisible(gridViewport);
    sequenceViewport.setViewedComponent(&sequenceBody, false);
    gridViewport.setScrollBarsShown(false, true);
    title.setText("GHOST HEALER DRUMS", juce::dontSendNotification);
    title.setFont(juce::Font(juce::FontOptions(22.0f)));
    for (auto* component : std::initializer_list<juce::Component*>{
        &title, &status, &inspector, &kits, &loops, &loopName, &dragHint, &add, &save, &randomKit, &replace, &previous, &next,
        &randomLoop, &copyMidi, &listen, &clipboardHint, &humanize, &exportButton, &saveMidi, &midiOut, &expand, &play, &useGrid})
        body.addAndMakeVisible(component);
    for (auto* component : std::initializer_list<juce::Component*>{
        &randomGrid, &mutate, &clear, &copy, &paste, &shiftLeft, &shiftRight, &scenes,
        &rate, &length, &gridHint, &swing, &sequencePlay, &sequenceStop, &sequenceRandomKit})
        sequenceBody.addAndMakeVisible(component);
    kits.setName("Drum kit");
    dragHint.setText("Drag name with mouse. Touch: hold 0.5 s, then drag. Arrow: choose loop.", juce::dontSendNotification);
    dragHint.setFont(juce::Font(juce::FontOptions(13.0f)));
    kits.onChange = [this]
    {
        if (refreshing) return;
        const auto v = processor.getView(); const int i = kits.getSelectedId() - 1;
        if (i >= 0 && i < static_cast<int>(v.catalogue.kits.size())) processor.chooseKit(v.catalogue.kits[static_cast<size_t>(i)].file);
    };
    loops.setName("MIDI loop");
    loops.setColour(juce::ComboBox::textColourId, juce::Colours::transparentBlack);
    loops.setTooltip("Choose a MIDI loop. Drag from the separate name area, not this arrow.");
    loopName.onClick = [this] { loops.showPopup(); };
    loopName.setName("MIDI loop drag handle");
    loops.onChange = [this]
    {
        if (refreshing) return;
        const int id = loops.getSelectedId();
        if (id == 100001) { processor.setGrid(processor.getGrid()); return; }
        if (id == 100000) { browse(3); return; }
        if (id == 100002) { browse(4); return; }
        const auto v = processor.getView(); const int i = id - 1;
        if (i >= 0 && i < static_cast<int>(v.catalogue.loops.size()))
            processor.chooseLoop(v.catalogue.loops[static_cast<size_t>(i)].file, false, true);
    };
    add.onClick = [this] { browse(0); }; save.onClick = [this] { nameKit(); };
    replace.onClick = [this] { browse(2); };
    randomKit.onClick = [this] { processor.randomKit(); };
    previous.onClick = [this] { processor.stepLoop(-1); }; next.onClick = [this] { processor.stepLoop(1); };
    randomLoop.onClick = [this] { processor.randomLoop(); };
    copyMidi.onClick = [this] { processor.copyMidiToClipboard(); };
    copyMidi.setTooltip("Copy the selected loop as binary MIDI, not text or a file path. Loop menu selections also copy automatically after loading.");
    listen.setName("Listen loop");
    listen.onClick = [this]
    {
        if (processor.isPreviewing() && processor.isListening()) processor.stopSequence();
        else processor.setPreview(true, true);
    };
    listen.setTooltip("Listen to the selected loop using the current drum kit. Click again to stop. Preview does not send MIDI or change the clipboard.");
    clipboardHint.setName("MIDI clipboard instructions");
    clipboardHint.setFont(juce::Font(juce::FontOptions(13.0f)));
#if JUCE_WINDOWS
    clipboardHint.setText("Loop menu selections auto-copy MIDI. FL Studio: Piano roll > File > Paste from MIDI clipboard (Shift+Ctrl+V), not Ctrl+V.", juce::dontSendNotification);
#else
    clipboardHint.setText("Loop menu selections auto-copy MIDI. The receiving app must support pasting MIDI clipboard data.", juce::dontSendNotification);
#endif
    humanize.setClickingTogglesState(true);
    humanize.onClick = [this] { processor.setHumanized(humanize.getToggleState()); };
    exportButton.setTooltip("FL Studio: drop onto this instrument's piano roll or Channel Rack, then place the resulting Pattern in the Playlist. Other hosts choose their own MIDI import target.");
    exportButton.onClick = [this] { processor.report("FL Studio: drag to the piano roll or Channel Rack, then place the Pattern in the Playlist. Save MIDI file opens Explorer for file import."); };
    saveMidi.onClick = [this] { const auto file = processor.exportMidi(); if (file.existsAsFile()) file.revealToUser(); };
    midiOut.setTooltip("Send generated loop notes on the selected MIDI channel. Route this output in the host to record it. Does not insert notes directly into a host's piano roll.");
    midiOutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.state, "midiOut", midiOut);
    midiTitle.setText("MIDI / CV", juce::dontSendNotification);
    midiTitle.setFont(juce::Font(juce::FontOptions(18.0f)));
    midiTitle.setColour(juce::Label::textColourId, gold);
    for (auto* component : std::initializer_list<juce::Component*>{
        &midiTitle, &midiIn, &midiOutPads, &midiMonitor, &inChannelLabel, &inChannel,
        &outChannelLabel, &outChannel, &thruLabel, &thruMode, &velocityLabel, &velocityCurve,
        &modeLabel, &cvMode, &padNoteLabel, &padNote, &cvGateLabel, &cvGate})
        body.addAndMakeVisible(component);
    const auto caption = [](juce::Label& label, const juce::String& text)
    {
        label.setText(text, juce::dontSendNotification);
        label.setFont(juce::Font(juce::FontOptions(13.0f)));
        label.setColour(juce::Label::textColourId, gh::theme::textLight);
        label.setJustificationType(juce::Justification::centredRight);
    };
    caption(inChannelLabel, "In channel"); caption(outChannelLabel, "Out channel");
    caption(thruLabel, "Thru mode"); caption(velocityLabel, "Velocity");
    caption(modeLabel, "Signal mode"); caption(padNoteLabel, "Selected pad note");
    caption(cvGateLabel, "CV gate time");
    cvGate.setSliderStyle(juce::Slider::LinearHorizontal);
    cvGate.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 24);
    cvGate.setRange(5.0, 2000.0, 1.0);
    cvGate.setTextValueSuffix(" ms");
    cvGate.setTooltip("Gate length for live hits in CV mode. Loop hits use the pattern's own note length.");
    cvGateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.state, "cvGate", cvGate);
    midiIn.setTooltip("Let external MIDI (Drambo, a sequencer, a controller) trigger pads and the loop gate.");
    midiInAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.state, "midiIn", midiIn);
    midiOutPads.setTooltip("Also send every pad hit on the MIDI output. Live MIDI input is never echoed back to avoid feedback.");
    midiOutPadsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.state, "midiOutPads", midiOutPads);
    inChannel.addItem("All", 1);
    for (int c = 1; c <= 16; ++c) inChannel.addItem("Channel " + juce::String(c), c + 1);
    inChannel.setTooltip("Accept notes from one channel only, or from every channel.");
    inChannelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.state, "midiInChannel", inChannel);
    for (int c = 1; c <= 16; ++c) outChannel.addItem("Channel " + juce::String(c), c + 1);
    outChannel.setTooltip("Channel used for loop playback, pad hits and MIDI THRU remapping.");
    outChannelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.state, "midiOutChannel", outChannel);
    thruMode.addItemList({"Trigger only", "Thru only", "Trigger + thru"}, 1);
    thruMode.setTooltip("Trigger only: input plays this kit. Thru only: input is forwarded untouched. Trigger + thru: both.");
    thruModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.state, "midiThru", thruMode);
    velocityCurve.addItemList({"Linear", "Soft", "Hard"}, 1);
    velocityCurve.setTooltip("Velocity response for incoming MIDI. Outgoing notes always keep the played velocity.");
    velocityCurveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.state, "midiVelocity", velocityCurve);
    cvMode.addItemList({"MIDI", "CV"}, 1);
    cvMode.setTooltip("CV replaces the stereo audio output with 1 V/oct pitch CV (left) and gate CV 0-1 V (right) for a DC-coupled interface.");
    cvModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.state, "midiCvMode", cvMode);
    for (int note = 0; note < 128; ++note)
        padNote.addItem(juce::MidiMessage::getMidiNoteName(note, true, true, 3) + " (" + juce::String(note) + ")", note + 1);
    padNote.setTooltip("MIDI note that plays the selected pad, used for input and output alike.");
    padNote.onChange = [this]
    {
        if (bindingPadNote) return;
        if (auto* parameter = processor.state.getParameter(DrumProcessor::midiNoteId(selected)))
        {
            const auto note = padNote.getSelectedId() - 1;
            parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(note)));
            updateInspector();
        }
    };
    midiMonitor.setName("MIDI activity");
    midiMonitor.setFont(juce::Font(juce::FontOptions(13.0f)));
    midiMonitor.setColour(juce::Label::textColourId, gh::theme::textDim);
    midiMonitor.setTooltip("Incoming and outgoing MIDI activity, updated while the plugin runs.");
    midiMonitor.setText("IN 0  hits 0   |   OUT 0", juce::dontSendNotification);
#if JUCE_IOS
    body.addAndMakeVisible(koalaRecord);
    koalaRecord.setTooltip("Send one loop to the GHOST HEALER DRUMS Record MIDI input. Arm Record in Koala, with this instrument pad in chromatic keyboard mode on channel 10. If Koala does not start the loop, tap Play loop. Internal loop audio is muted to prevent doubled hits.");
    koalaRecordAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.state, "koalaRecord", koalaRecord);
    const auto safe = juce::Component::SafePointer<DrumEditor>(this);
    nativeExport = makeMidiShare([safe] { return safe ? safe->processor.exportMidi() : juce::File{}; },
        [safe](juce::String error) { if (safe) safe->processor.report(error, true); });
    body.addAndMakeVisible(*nativeExport);
    nativeLoopName = makeMidiNameDrag([safe] { return safe ? safe->processor.exportMidi() : juce::File{}; },
        [safe](juce::String error) { if (safe) safe->processor.report(error, true); },
        [safe] { if (safe) safe->loops.showPopup(); });
    body.addAndMakeVisible(*nativeLoopName);
    exportButton.setVisible(false);
    saveMidi.setVisible(false);
#endif
    expand.onClick = [this] { openSequencer(); };
    play.setClickingTogglesState(true); play.onClick = [this] { processor.setPreview(play.getToggleState()); };
    sequencePlay.onClick = [this] { processor.setGrid(processor.getGrid(), true); };
    sequenceStop.onClick = [this] { processor.stopSequence(); };
    sequenceRandomKit.onClick = [this] { processor.randomKit(); };
    useGrid.onClick = [this] { processor.setGrid(processor.getGrid()); };
    randomGrid.onClick = [this] { processor.generateGrid(false); }; mutate.onClick = [this] { processor.generateGrid(true); };
    clear.onClick = [this]
    {
        auto g = processor.getGrid();
        for (auto& lane : g.scenes[static_cast<size_t>(g.scene)].velocity)
            std::fill_n(lane.begin(), g.stepCount, uint8_t{0});
        processor.setGrid(g);
    };
    copy.onClick = [this] { const auto g = processor.getGrid(); clipboard = g.scenes[static_cast<size_t>(g.scene)]; };
    paste.onClick = [this]
    {
        if (! clipboard) { processor.report("Copy a scene before pasting.", true); return; }
        auto g = processor.getGrid(); g.scenes[static_cast<size_t>(g.scene)] = *clipboard; processor.setGrid(g);
    };
    const auto shift = [this](int offset)
    {
        auto g = processor.getGrid();
        for (auto& lane : g.scenes[static_cast<size_t>(g.scene)].velocity)
        {
            const auto old = lane;
            for (int s = 0; s < g.stepCount; ++s) lane[static_cast<size_t>((s + offset + g.stepCount) % g.stepCount)] = old[static_cast<size_t>(s)];
        }
        processor.setGrid(g);
    };
    shiftLeft.onClick = [shift] { shift(-1); }; shiftRight.onClick = [shift] { shift(1); };
    for (int i = 0; i < 16; ++i) scenes.addItem("Scene " + juce::String(i + 1), i + 1);
    scenes.onChange = [this] { if (! refreshing) { auto g = processor.getGrid(); g.scene = scenes.getSelectedId() - 1; processor.setGrid(g); } };
    rate.addItemList({"1/32", "1/16", "1/8", "1/4"}, 1);
    rate.onChange = [this] { if (! refreshing) { auto g = processor.getGrid(); g.rate = rate.getSelectedId() - 1; processor.setGrid(g); } };
    length.setName("Sequence length");
    length.addItem("16 steps", 16); length.addItem("32 steps", gh::maxSteps);
    length.onChange = [this]
    {
        if (! refreshing) { auto g = processor.getGrid(); g.stepCount = length.getSelectedId(); processor.setGrid(g); }
    };
    length.setTooltip("Pattern length for all scenes. Switching to 16 hides, but preserves, steps 17-32.");
    slider(swing); swing.setRange(0, 45, 1); swing.setTextValueSuffix("% swing");
    swing.onValueChange = [this] { if (! refreshing) { auto g = processor.getGrid(); g.swing = static_cast<float>(swing.getValue() / 100); processor.setGrid(g); } };
    gridHint.setText("Tap toggles a step. Double-tap unlocks velocity drag until another step is selected.", juce::dontSendNotification);
    for (int s = 0; s < gh::maxSteps; ++s)
    {
        auto& label = stepNumbers[static_cast<size_t>(s)];
        label.setText(juce::String(s + 1), juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setColour(juce::Label::textColourId, (s / 4) % 2 != 0 ? gh::theme::inkBright : gh::theme::textDim);
        gridBody.addAndMakeVisible(label);
    }
    for (int i = 0; i < gh::pads; ++i)
    {
        auto& pad = pads[static_cast<size_t>(i)]; pad = std::make_unique<PadButton>(i);
        body.addAndMakeVisible(*pad);
        pad->onClick = [this, i] { selectPad(i); processor.audition(i); };
        pad->setTooltip("MIDI " + juce::String(i + gh::firstNote) + ". One-shot; note-off does not cut the sample.");
    }
    for (size_t i = 0; i < sliders.size(); ++i)
    {
        if (i == 4 || i == 5) continue;
        knob(sliders[i]); body.addAndMakeVisible(sliders[i]); body.addAndMakeVisible(labels[i]);
        labels[i].setText(std::array<const char*, 7>{"Tune (st)", "Pan", "Level (dB)", "Velocity > pitch (st)", "Low cut (Hz)", "High cut (Hz)", "Compressor"}[i], juce::dontSendNotification);
        sliders[i].setName(labels[i].getText());
        labels[i].setText(std::array<const char*, 7>{"Tune", "Pan", "Level (dB)", "Velocity > pitch", "", "", "Comp"}[i], juce::dontSendNotification);
        labels[i].setJustificationType(juce::Justification::centred);
        labels[i].setFont(juce::Font(juce::FontOptions(16.0f)));
        labels[i].setColour(juce::Label::textColourId, gold);
    }
    body.addAndMakeVisible(cutoffBar); body.addAndMakeVisible(cutoffLabel);
    cutoffLabel.setText("Filter (Hz)", juce::dontSendNotification);
    cutoffLabel.setJustificationType(juce::Justification::centred);
    cutoffLabel.setColour(juce::Label::textColourId, gold);
    for (size_t i = 0; i < globalSliders.size(); ++i)
    {
        if (i < 2) knob(globalSliders[i]);
        else
        {
            globalSliders[i].setSliderStyle(juce::Slider::IncDecButtons);
            globalSliders[i].setTextBoxStyle(juce::Slider::TextBoxLeft, false, 96, 44);
            globalSliders[i].setIncDecButtonsMode(juce::Slider::incDecButtonsDraggable_Horizontal);
            globalSliders[i].setTooltip("Preview tempo. The host tempo takes precedence when supplied.");
            if (processor.isHosted())
            {
                globalSliders[i].setEnabled(false);
                globalSliders[i].setTooltip("Tempo is controlled by the DAW. Adjustable only in standalone mode.");
            }
        }
        body.addAndMakeVisible(globalSliders[i]); body.addAndMakeVisible(globalLabels[i]);
        globalLabels[i].setText(std::array<const char*, 3>{"Master (dB)", "Master saturation", "Tempo (host overrides)"}[i], juce::dontSendNotification);
        globalSliders[i].setName(globalLabels[i].getText());
        globalLabels[i].setText(std::array<const char*, 3>{"Master (dB)", "Master saturation", "Tempo"}[i], juce::dontSendNotification);
        globalLabels[i].setJustificationType(juce::Justification::centred);
        globalLabels[i].setFont(juce::Font(juce::FontOptions(i == 2 ? 20.0f : 16.0f)));
        globalLabels[i].setColour(juce::Label::textColourId, gold);
        globalAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.state, std::array<const char*, 3>{"master", "saturation", "tempo"}[i], globalSliders[i]);
        if (i == 2 && processor.isHosted())
        {
            globalSliders[i].textFromValueFunction = [](double) { return juce::String("HOST"); };
            globalSliders[i].updateText();
        }
    }
    for (int lane = 0; lane < 12; ++lane)
    {
        const auto l = static_cast<size_t>(lane);
        gridBody.addAndMakeVisible(laneNames[l]); laneNames[l].setText(gh::roles[l], juce::dontSendNotification);
        laneNames[l].setName(gh::roles[l]); laneNames[l].setColour(juce::Label::textColourId, roleColour(lane));
        gridBody.addAndMakeVisible(variants[l]);
        variants[l].setColour(juce::TextButton::buttonColourId, roleColour(lane).withMultipliedBrightness(0.3f));
        variants[l].onClick = [this, l] { auto g = processor.getGrid(); auto& v = g.scenes[static_cast<size_t>(g.scene)].variant[l]; v = static_cast<uint8_t>((v + 1) % 4); processor.setGrid(g); };
    }
    selectPad(0); processor.addChangeListener(this); refresh();
    setResizable(true, false); setResizeLimits(360, 320, 1800, 1400);
    // Clamp the window to the host display. A 10.5" iPad Pro exposes 1112 x 834
    // logical points, so the historical 1060 x 960 default would be clipped.
    int defaultWidth = 1060, defaultHeight = 960;
    if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        // Some AUv3 hosts report an empty or swapped display while the editor is
        // being created, so only clamp against a usable area.
        const auto area = display->userBounds.getSmallestIntegerContainer();
        if (area.getWidth() >= 360 && area.getHeight() >= 320)
        {
            defaultWidth = juce::jmin(defaultWidth, juce::jmax(360, area.getWidth() - 8));
            defaultHeight = juce::jmin(defaultHeight, juce::jmax(320, area.getHeight() - 8));
        }
    }
    setSize(defaultWidth, defaultHeight);
    startTimerHz(5);
}
DrumEditor::~DrumEditor()
{
    stopTimer(); processor.removeChangeListener(this);
    sequenceWindow.reset();
    setLookAndFeel(nullptr);
}
void DrumEditor::selectPad(int pad)
{
    selected = pad;
    bindPadNote(pad);
    updateInspector();
    inspector.setColour(juce::Label::textColourId, roleColour(pad % 12));
    for (int i = 0; i < gh::pads; ++i) pads[static_cast<size_t>(i)]->setToggleState(i == selected, juce::dontSendNotification);
    for (int i = 0; i < 7; ++i)
    {
        attachments[static_cast<size_t>(i)].reset();
        if (i == 4 || i == 5) continue;
        attachments[static_cast<size_t>(i)] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.state, DrumProcessor::padId(pad, i), sliders[static_cast<size_t>(i)]);
    }
    auto* low = processor.state.getParameter(DrumProcessor::padId(pad, 4));
    auto* high = processor.state.getParameter(DrumProcessor::padId(pad, 5));
    cutoffBar.setEnabled(low && high);
    if (low && high) cutoffBar.bind(*low, *high);
    else processor.report("Could not bind filter controls.", true);
}
void DrumEditor::bindPadNote(int pad)
{
    bindingPadNote = true;
    if (const auto* parameter = processor.state.getRawParameterValue(DrumProcessor::midiNoteId(pad)))
        padNote.setSelectedId(juce::jlimit(0, 127, juce::roundToInt(parameter->load())) + 1, juce::dontSendNotification);
    bindingPadNote = false;
}
void DrumEditor::updateInspector()
{
    const auto& name = padNames[static_cast<size_t>(selected)];
    int note = selected + gh::firstNote;
    if (const auto* parameter = processor.state.getRawParameterValue(DrumProcessor::midiNoteId(selected)))
        note = juce::roundToInt(parameter->load());
    inspector.setText(gh::noteLabel(selected) + "  "
        + (name.isNotEmpty() ? name : juce::String(gh::roles[static_cast<size_t>(selected % 12)]))
        + "  |  MIDI " + juce::String(note), juce::dontSendNotification);
}
void DrumEditor::refresh()
{
    refreshing = true; const auto v = processor.getView();
    koalaRecord.setEnabled(! v.patternLoading);
    if (sequenceWindow) sequenceWindow->setName("GHOST HEALER - Sequencer - " + v.kitName);
    if (kitCount != v.catalogue.kits.size())
    {
        kits.clear(juce::dontSendNotification); kitCount = v.catalogue.kits.size();
        for (size_t i = 0; i < kitCount; ++i) kits.addItem(v.catalogue.kits[i].name, static_cast<int>(i + 1));
        cachedKitIndex = -1;
    }
    if (loopCount != v.catalogue.loops.size() || loops.getNumItems() == 0)
    {
        loops.clear(juce::dontSendNotification); loopCount = v.catalogue.loops.size();
        loops.addItem("Import GM/FPC MIDI...", 100000); loops.addItem("Import mapped MIDI...", 100002);
        loops.addItem("Use grid", 100001); loops.addSeparator();        std::map<juce::String, juce::PopupMenu> menus;
        for (size_t i = 0; i < loopCount; ++i)
        {
            const auto& entry = v.catalogue.loops[i];
            menus[entry.category].addCustomItem(static_cast<int>(i + 1),
                std::make_unique<LoopItem>(processor, dragLauncher, *this, entry), nullptr, entry.name);
        }
        for (const auto& menu : menus) loops.getRootMenu()->addSubMenu(menu.first, menu.second);
    }
    // Only rescan the (large) catalogue when the selected file actually changed.
    if (v.kitFile != cachedKitFile)
    {
        cachedKitFile = v.kitFile; cachedKitIndex = -1;
        for (size_t i = 0; i < kitCount; ++i)
            if (v.catalogue.kits[i].file == v.kitFile) { cachedKitIndex = static_cast<int>(i); break; }
    }
    if (cachedKitIndex >= 0) kits.setSelectedId(cachedKitIndex + 1, juce::dontSendNotification);
    if (v.gridSource) loops.setSelectedId(100001, juce::dontSendNotification);
    else
    {
        loops.setSelectedId(0, juce::dontSendNotification);
        if (v.loopFile != cachedLoopFile)
        {
            cachedLoopFile = v.loopFile; cachedLoopIndex = -1;
            for (size_t i = 0; i < loopCount; ++i)
                if (v.catalogue.loops[i].file == v.loopFile) { cachedLoopIndex = static_cast<int>(i); break; }
        }
        if (cachedLoopIndex >= 0) loops.setSelectedId(cachedLoopIndex + 1, juce::dontSendNotification);
        if (loops.getSelectedId() == 0) loops.setText(v.loopName, juce::dontSendNotification);
    }
    humanize.setToggleState(v.humanized, juce::dontSendNotification);
    loopName.setButtonText(loops.getText());
    loopName.setName("MIDI loop drag handle");
#if JUCE_IOS
    if (nativeLoopName) setMidiNameDragText(*nativeLoopName, loops.getText());
#endif
    copyMidi.setEnabled(v.patternReady);
    listen.setEnabled(v.patternReady || processor.isListening());
    exportButton.setEnabled(v.patternReady);
    saveMidi.setEnabled(v.patternReady);
    if (nativeExport) nativeExport->setEnabled(v.patternReady);
    status.setText(juce::String(static_cast<double>(v.memory) / 1048576, 1) + " MiB kit | " + v.status
        + (v.importInfo.isNotEmpty() ? "\n" + v.importInfo : juce::String()), juce::dontSendNotification);
    status.setTooltip(v.importInfo);
    status.setColour(juce::Label::textColourId, v.error ? juce::Colour(0xffe8a08c) : gh::theme::textLight);
    padNames = v.padNames; bindPadNote(selected); updateInspector();
    refreshGrid(); refreshing = false;
}
void DrumEditor::refreshGrid()
{
    if (! steps) return;
    const auto g = processor.getGrid(); scenes.setSelectedId(g.scene + 1, juce::dontSendNotification);
    if (displayedScene != g.scene)
    {
        if (selectedStep) { selectedStep->selected = false; selectedStep->velocityUnlocked = false; selectedStep->repaint(); }
        selectedStep = nullptr; displayedScene = g.scene;
        for (auto& lane : *steps) for (auto& step : lane)
        { step.lastVelocity = 96; step.toggledLastTap = false; }
    }
    rate.setSelectedId(g.rate + 1, juce::dontSendNotification); swing.setValue(g.swing * 100, juce::dontSendNotification);
    length.setSelectedId(g.stepCount, juce::dontSendNotification);
    for (int s = 0; s < gh::maxSteps; ++s) stepNumbers[static_cast<size_t>(s)].setVisible(s < g.stepCount);
    const auto& scene = g.scenes[static_cast<size_t>(g.scene)];
    for (size_t l = 0; l < 12; ++l)
    {
        variants[l].setButtonText(juce::String(scene.variant[l] + 1));
        for (size_t s = 0; s < gh::maxSteps; ++s)
        {
            (*steps)[l][s].setVisible(s < static_cast<size_t>(g.stepCount));
            (*steps)[l][s].setValue(scene.velocity[l][s], juce::dontSendNotification);
        }
    }
    if (gridBody.getWidth() != 190 + g.stepCount * 44 + 6) layoutSequencer();
    if (selectedStep && ! selectedStep->isVisible())
    {
        selectedStep->selected = false; selectedStep->velocityUnlocked = false;
        selectedStep->toggledLastTap = false; selectedStep = nullptr;
    }
}
void DrumEditor::selectStep(VelocitySlider& step, bool unlock)
{
    if (selectedStep != &step)
    {
        if (selectedStep)
        {
            selectedStep->selected = false; selectedStep->velocityUnlocked = false;
            selectedStep->toggledLastTap = false; selectedStep->repaint();
        }
        selectedStep = &step;
    }
    step.selected = true;
    if (unlock) step.velocityUnlocked = true;
    step.repaint();
}
void DrumEditor::changeListenerCallback(juce::ChangeBroadcaster*) { refresh(); }
void DrumEditor::timerCallback()
{
    play.setToggleState(processor.isPreviewing(), juce::dontSendNotification);
    const bool listening = processor.isPreviewing() && processor.isListening();
    listen.setToggleState(listening, juce::dontSendNotification);
    listen.setButtonText(listening ? "Stop listen" : "Listen");
    listen.setName("Listen loop");
    sequencePlay.setToggleState(processor.isPreviewing(), juce::dontSendNotification);
    const auto monitor = processor.getMidiMonitor();
    const auto noteName = [](int note)
    {
        return note < 0 ? juce::String("-") : juce::MidiMessage::getMidiNoteName(note, true, true, 3);
    };
    midiMonitor.setText("IN " + juce::String(monitor.inCount)
        + "  hits " + juce::String(monitor.triggerCount)
        + "  last " + noteName(monitor.lastInNote)
        + (monitor.lastInNote >= 0 ? " v" + juce::String(monitor.lastInVelocity)
            + " ch" + juce::String(monitor.lastInChannel) : juce::String())
        + "   |   OUT " + juce::String(monitor.outCount)
        + "  last " + noteName(monitor.lastOutNote)
        + (monitor.lastOutNote >= 0 ? " ch" + juce::String(monitor.lastOutChannel) : juce::String())
        + (processor.isCvMode() ? "   |   CV OUT" : juce::String()), juce::dontSendNotification);
}
void DrumEditor::paint(juce::Graphics& g) { g.fillAll(gh::theme::back); }
void DrumEditor::resized()
{
    viewport.setBounds(getLocalBounds());
    const int w = std::max(340, getWidth() - viewport.getScrollBarThickness() - 4);
    int y = 8;
    const auto row = [w, &y](std::initializer_list<juce::Component*> components, int height = 48)
    {
        const int width = (w - 16) / static_cast<int>(components.size());
        int x = 8;
        for (auto* c : components) { c->setBounds(x + 2, y + 2, width - 4, height - 4); x += width; }
        y += height;
    };
    row({&title}, 34); row({&kits}); row({&add, &save, &randomKit}); row({&loops});
    kits.setSize(kits.getWidth() / 2, kits.getHeight());
    const auto loopBounds = loops.getBounds().withWidth(loops.getWidth() / 2);
    loopName.setBounds(loopBounds.withTrimmedRight(44));
    loops.setBounds(loopBounds.withTrimmedLeft(loopBounds.getWidth() - 44));
    dragHint.setBounds(loops.getRight() + 8, loops.getY(), w - 10 - loops.getRight() - 8, loops.getHeight());
#if JUCE_IOS
    if (nativeLoopName) nativeLoopName->setBounds(loopName.getBounds().reduced(2));
#endif
    if (w >= 950) row({&previous, &next, &randomLoop, &copyMidi, &listen, &humanize});
    else if (w >= 600) { row({&previous, &next, &humanize}); row({&randomLoop, &copyMidi, &listen}); }
    else { row({&randomLoop, &copyMidi}); row({&previous, &next, &listen, &humanize}); }
    row({&clipboardHint}, w < 600 ? 68 : 42);
    if (nativeExport)
    {
        row({nativeExport.get()});
        row({&midiOut, &koalaRecord});
    }
    else if (w >= 560) row({&exportButton, &saveMidi, &midiOut});
    else { row({&exportButton, &saveMidi}); row({&midiOut}); }
    row({&midiTitle}, 30);
    const auto pairRow = [w, &y](std::initializer_list<std::pair<juce::Label*, juce::Component*>> items, int height = 44)
    {
        if (items.size() == 0) return;
        const auto count = static_cast<int>(items.size());
        const int width = (w - 16) / count;
        int x = 8;
        for (auto& item : items)
        {
            auto cell = juce::Rectangle<int>(x + 2, y + 2, width - 4, height - 4);
            auto captionArea = cell.removeFromLeft(juce::jmax(64, cell.getWidth() / 2));
            item.first->setBounds(captionArea.reduced(4, 0));
            item.second->setBounds(cell);
            x += width;
        }
        y += height;
    };
    row({&midiIn, &midiOutPads});
    if (w >= 950)
        pairRow({{&inChannelLabel, &inChannel}, {&outChannelLabel, &outChannel}, {&thruLabel, &thruMode}});
    else if (w >= 560)
    {
        pairRow({{&inChannelLabel, &inChannel}, {&outChannelLabel, &outChannel}});
        pairRow({{&thruLabel, &thruMode}});
    }
    else
    {
        pairRow({{&inChannelLabel, &inChannel}});
        pairRow({{&outChannelLabel, &outChannel}});
        pairRow({{&thruLabel, &thruMode}});
    }
    if (w >= 950)
        pairRow({{&modeLabel, &cvMode}, {&velocityLabel, &velocityCurve}, {&padNoteLabel, &padNote}});
    else if (w >= 560)
    {
        pairRow({{&modeLabel, &cvMode}, {&velocityLabel, &velocityCurve}});
        pairRow({{&padNoteLabel, &padNote}, {&cvGateLabel, &cvGate}});
    }
    else
    {
        pairRow({{&modeLabel, &cvMode}});
        pairRow({{&velocityLabel, &velocityCurve}});
        pairRow({{&padNoteLabel, &padNote}});
        pairRow({{&cvGateLabel, &cvGate}});
    }
    if (w >= 950) pairRow({{&cvGateLabel, &cvGate}});
    row({&midiMonitor}, 36);
    const int columns = w >= 850 ? 12 : w >= 560 ? 6 : 4;
    const int cell = (w - 16) / columns;
    for (int i = 0; i < gh::pads; ++i) pads[static_cast<size_t>(i)]->setBounds(8 + (i % columns) * cell, y + (i / columns) * 48, cell, 48);
    y += (gh::pads / columns) * 48 + 4;
    row({&inspector, &replace});
    const int tempoX = (w - 268) / 2;
    globalLabels[2].setBounds(tempoX, y + 8, 60, 44);
    globalSliders[2].setBounds(tempoX + 68, y + 8, 200, 44);
    y += 64;
    const bool wide = w >= 950;
    const int controls = wide ? 7 : w >= 700 ? 4 : w >= 500 ? 3 : 2;
    const int controlArea = w - 16 - (wide ? 124 : 0), gap = 12;
    const int controlWidth = (controlArea - (controls - 1) * gap) / controls;
    const std::array<juce::Slider*, 7> knobs {&sliders[0], &sliders[6], &sliders[1], &sliders[2], &sliders[3], &globalSliders[1], &globalSliders[0]};
    const std::array<juce::Label*, 7> captions {&labels[0], &labels[6], &labels[1], &labels[2], &labels[3], &globalLabels[1], &globalLabels[0]};
    constexpr int tileHeight = 198;
    for (int i = 0; i < 7; ++i)
    {
        const int count = std::min(controls, 7 - (i / controls) * controls);
        const int offset = (controlArea - (count * controlWidth + (count - 1) * gap)) / 2;
        const int x = 8 + offset + (i % controls) * (controlWidth + gap), top = y + (i / controls) * tileHeight;
        knobs[static_cast<size_t>(i)]->setBounds(x, top + 2, controlWidth, 148);
        captions[static_cast<size_t>(i)]->setBounds(x, top + 150, controlWidth, 34);
    }
    if (wide)
    {
        cutoffLabel.setBounds(w - 116, y, 108, 24);
        cutoffBar.setBounds(w - 116, y + 28, 108, 166);
    }
    y += ((7 + controls - 1) / controls) * tileHeight + 8;
    if (! wide)
    {
        cutoffLabel.setBounds(8, y, w - 16, 24);
        cutoffBar.setBounds(8, y + 28, w - 16, 80);
        y += 116;
    }
    row({&expand, &play, &useGrid});
    row({&status}, 48);
    body.setSize(w, std::max(y + 8, getHeight()));
#if JUCE_IOS
    if (sequenceWindow) sequenceWindow->setBounds(getLocalBounds().reduced(4));
#endif
}
void DrumEditor::openSequencer()
{
    if (! steps)
    {
        steps = std::make_unique<StepGrid>();
        for (size_t l = 0; l < steps->size(); ++l)
            for (int s = 0; s < gh::maxSteps; ++s)
            {
                auto& step = (*steps)[l][static_cast<size_t>(s)]; gridBody.addAndMakeVisible(step);
                step.greenGroup = (s / 4) % 2 != 0;
                step.setName(juce::String(gh::roles[l]) + " step " + juce::String(s + 1) + " velocity");
                step.setTooltip("Tap on/off. Double-tap to unlock velocity editing; drag vertically. Selecting another step locks it again.");
                step.selectStep = [this, &step](bool unlock) { selectStep(step, unlock); };
                step.onValueChange = [this, l, s]
                {
                    if (refreshing) return;
                    auto g = processor.getGrid(); auto& v = g.scenes[static_cast<size_t>(g.scene)].velocity[l][static_cast<size_t>(s)];
                    v = static_cast<uint8_t>((*steps)[l][static_cast<size_t>(s)].getValue()); processor.setGrid(g);
                };
            }
        const juce::ScopedValueSetter<bool> guard(refreshing, true);
        refreshGrid();
    }
    if (! sequenceWindow)
    {
        const auto safe = juce::Component::SafePointer<DrumEditor>(this);
        sequenceWindow = std::make_unique<SequencerWindow>(sequenceViewport,
            [safe] { if (safe) safe->layoutSequencer(); },
            [safe] { if (safe) safe->expand.setToggleState(false, juce::dontSendNotification); });
        sequenceWindow->setLookAndFeel(&look);
#if JUCE_IOS
        addChildComponent(*sequenceWindow);
        sequenceWindow->setBounds(getLocalBounds().reduced(4));
#else
        sequenceWindow->addToDesktop();
        sequenceWindow->centreAroundComponent(this, 1000, 980);
#endif
    }
    sequenceWindow->setVisible(true);
    sequenceWindow->toFront(true);
    expand.setToggleState(true, juce::dontSendNotification);
    layoutSequencer();
}
void DrumEditor::layoutSequencer()
{
    if (! steps) return;
    const int w = std::max(340, sequenceViewport.getWidth() - sequenceViewport.getScrollBarThickness() - 4);
    int y = 8;
    const auto row = [w, &y](std::initializer_list<juce::Component*> components, int height = 48)
    {
        const int width = (w - 16) / static_cast<int>(components.size());
        int x = 8;
        for (auto* c : components) { c->setBounds(x + 2, y + 2, width - 4, height - 4); x += width; }
        y += height;
    };
    row({&sequencePlay, &sequenceStop, &sequenceRandomKit}); row({&scenes, &rate, &length}); row({&swing}); row({&randomGrid, &mutate, &clear});
    row({&copy, &paste, &shiftLeft, &shiftRight}); row({&gridHint}, 48);
    gridViewport.setBounds(8, y, w - 16, 13 * 48 + 20);
    gridBody.setSize(190 + processor.getGrid().stepCount * 44 + 6, 13 * 48);
    for (int s = 0; s < gh::maxSteps; ++s) stepNumbers[static_cast<size_t>(s)].setBounds(190 + s * 44, 0, 44, 48);
    for (int lane = 0; lane < 12; ++lane)
    {
        const auto l = static_cast<size_t>(lane);
        laneNames[l].setBounds(0, (lane + 1) * 48, 140, 48); variants[l].setBounds(142, (lane + 1) * 48 + 2, 44, 44);
        for (int s = 0; s < gh::maxSteps; ++s) (*steps)[l][static_cast<size_t>(s)].setBounds(190 + s * 44, (lane + 1) * 48 + 2, 44, 44);
    }
    sequenceBody.setSize(w, y + gridViewport.getHeight() + 8);
}
void DrumEditor::browse(int mode)
{
    auto filter = mode == 2 ? "*.wav" : mode >= 3 ? "*.mid;*.midi" : "*.zip;*.ghkit;*.nbkt";
    int browserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
#if ! JUCE_IOS
    if (mode == 0) browserFlags |= juce::FileBrowserComponent::canSelectDirectories;
#else
    if (mode == 0) filter = "*.zip";
#endif
    chooser = std::make_unique<juce::FileChooser>("Import", juce::File{}, filter, true);
    const auto safe = juce::Component::SafePointer<DrumEditor>(this);
    chooser->launchAsync(browserFlags, [safe, mode](const juce::FileChooser& dialog)
    {
        if (! safe) return;
#if JUCE_IOS
        const auto url = dialog.getURLResult(); if (url.toString(false).isEmpty()) return;
        if (mode == 2) safe->processor.replaceSample(safe->selected, url);
        else if (mode >= 3) safe->processor.chooseLoop(url, mode == 3, true);
        else safe->processor.importKit(url);
#else
        const auto file = dialog.getResult(); if (file == juce::File{}) return;
        if (mode == 2) safe->processor.replaceSample(safe->selected, file);
        else if (mode >= 3) safe->processor.chooseLoop(file, mode == 3, true);
        else safe->processor.importKit(file);
#endif
    });
}
void DrumEditor::nameKit()
{
    auto* alert = new juce::AlertWindow("Save portable kit", "A self-contained copy is saved in the plugin's user library.", juce::MessageBoxIconType::NoIcon);
    alert->addTextEditor("name", processor.getView().kitName + " copy", "Kit name:");
    alert->addButton("Save", 1); alert->addButton("Cancel", 0);
    const auto safe = juce::Component::SafePointer<DrumEditor>(this);
    alert->enterModalState(true, juce::ModalCallbackFunction::create([safe, alert](int result)
    {
        if (safe && result == 1)
        {
            const auto name = alert->getTextEditorContents("name").trim();
            if (name.isEmpty()) safe->processor.report("Kit name cannot be empty.", true);
            else safe->processor.saveKit(name);
        }
    }), true);
}
void DrumEditor::filesDropped(const juce::StringArray& files, int, int)
{
    if (files.size() != 1) { processor.report("Drop one kit folder, ZIP, .nbkt, MIDI file or WAV at a time.", true); return; }
    const juce::File file(files[0]);
    if (file.hasFileExtension("mid;midi")) processor.chooseLoop(file, true);
    else if (file.hasFileExtension("wav")) processor.replaceSample(selected, file);
    else processor.importKit(file);
}
juce::AudioProcessorEditor* DrumProcessor::createEditor() { return new DrumEditor(*this); }
