#pragma once
#include "Processor.h"
#include "Theme.h"
#include <juce_gui_extra/juce_gui_extra.h>

#if JUCE_IOS
std::unique_ptr<juce::Component> makeMidiShare(std::function<juce::File()>, std::function<void(juce::String)>);
std::unique_ptr<juce::UIViewComponent> makeMidiNameDrag(std::function<juce::File()>, std::function<void(juce::String)>, std::function<void()>);
void setMidiNameDragText(juce::UIViewComponent&, const juce::String&);
#endif
#if JUCE_WINDOWS
juce::Result dragMidiFileOnWindows(const juce::File&, juce::Component&);
#endif
class DrumEditor final : public juce::AudioProcessorEditor,
                         private juce::ChangeListener, private juce::Timer,
                         public juce::FileDragAndDropTarget
{
public:
    using MidiDragLauncher = std::function<void(const juce::File&, juce::Component&, std::function<void(juce::Result)>)>;
    explicit DrumEditor(DrumProcessor&, MidiDragLauncher = {});
    ~DrumEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
    void filesDropped(const juce::StringArray&, int, int) override;
private:
    struct PadButton final : juce::Button
    {
        explicit PadButton(int number) : Button(gh::noteLabel(number)), slot(number) {}
        void paintButton(juce::Graphics&, bool, bool) override;
        int slot;
    };
    struct ExportButton final : juce::TextButton
    {
        ExportButton(DrumProcessor& p, MidiDragLauncher& launcher) : TextButton("Drag MIDI"), processor(p), launch(launcher)
        { setViewportIgnoreDragFlag(true); }
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;
        DrumProcessor& processor;
        MidiDragLauncher& launch;
        juce::File dragFile;
        bool dragging = false;
    };
    struct LoopName final : juce::TextButton, private juce::Timer
    {
        LoopName(DrumProcessor&, MidiDragLauncher&, std::function<juce::File()> = {});
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;
        void paintButton(juce::Graphics&, bool, bool) override;
        void timerCallback() override;
        void resetGesture();
        DrumProcessor& processor;
        MidiDragLauncher& launch;
        std::function<juce::File()> exporter;
        juce::File dragFile;
        bool pressed = false, armed = false, cancelled = false, dragging = false;
    };
    struct LoopItem final : juce::PopupMenu::CustomComponent
    {
        LoopItem(DrumProcessor&, MidiDragLauncher&, DrumEditor&, const gh::Entry&);
        void getIdealSize(int&, int&) override;
        void resized() override;
        void parentHierarchyChanged() override;
        DrumProcessor& processor;
        gh::Entry loopEntry;
        MidiDragLauncher launchFromEditor;
        std::unique_ptr<LoopName> name;
#if JUCE_IOS
        std::unique_ptr<juce::UIViewComponent> nativeName;
#endif
    };
    struct VelocitySlider final : juce::Slider
    {
        VelocitySlider();
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;
        void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        bool keyPressed(const juce::KeyPress&) override;
        std::function<void(bool)> selectStep;
        bool greenGroup = false;
        bool selected = false, velocityUnlocked = false, sliderGesture = false, toggledLastTap = false, pressed = false;
        double valueBeforeTap = 0, lastVelocity = 96;
    };
    struct CutoffBar final : juce::Component, juce::SettableTooltipClient
    {
        CutoffBar();
        ~CutoffBar() override;
        void bind(juce::RangedAudioParameter&, juce::RangedAudioParameter&);
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;
        bool keyPressed(const juce::KeyPress&) override;
        juce::Point<float> handlePosition(int) const;
        void finishGesture();
        juce::NormalisableRange<float> range {0, 20000, 1};
        std::array<float, 2> values {0, 20000};
        std::array<juce::RangedAudioParameter*, 2> parameters {};
        std::array<std::unique_ptr<juce::ParameterAttachment>, 2> attachments;
        int handle = 0;
        bool dragging = false;
    };
    struct SequencerWindow final : juce::DocumentWindow
    {
        SequencerWindow(juce::Component&, std::function<void()> layout, std::function<void()> close);
        void closeButtonPressed() override;
        void resized() override;
        std::function<void()> layoutCallback, closeCallback;
    };
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refresh();
    void selectPad(int);
    void updateInspector();
    void refreshGrid();
    void browse(int mode);
    void nameKit();
    void openSequencer();
    void layoutSequencer();
    void selectStep(VelocitySlider&, bool unlock);
    DrumProcessor& processor;
    MidiDragLauncher dragLauncher;
    gh::theme::Look look;
    juce::Viewport viewport, gridViewport;
    juce::Component body, gridBody;
    juce::Viewport sequenceViewport;
    juce::Component sequenceBody;
    juce::Label title, status, inspector, gridHint, clipboardHint, dragHint;
    juce::ComboBox kits, loops, scenes, rate, length;
    LoopName loopName;
    juce::TextButton add {"Import kit"}, save {"Save kit"}, randomKit {"Random kit"};
    juce::TextButton replace {"Replace WAV"};
    juce::TextButton previous {"<"}, next {">"}, randomLoop {"Random loop"}, humanize {"Humanize"};
    juce::TextButton copyMidi {"Copy to MIDI clipboard"}, listen {"Listen"};
    ExportButton exportButton;
    juce::TextButton saveMidi {"Save MIDI file"};
    juce::ToggleButton midiOut {"Live MIDI out"}, koalaRecord {"Record to Koala"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> midiOutAttachment, koalaRecordAttachment;
    std::unique_ptr<juce::Component> nativeExport;
#if JUCE_IOS
    std::unique_ptr<juce::UIViewComponent> nativeLoopName;
#endif
    juce::TextButton expand {"Open sequencer"}, play {"Play loop"}, useGrid {"Use grid"};
    juce::TextButton sequencePlay {"Play"}, sequenceStop {"Stop"}, sequenceRandomKit {"Random kit"};
    juce::TextButton randomGrid {"Random"}, mutate {"Mutate"}, clear {"Clear"}, copy {"Copy"}, paste {"Paste"}, shiftLeft {"Shift <"}, shiftRight {"Shift >"};
    std::array<std::unique_ptr<PadButton>, gh::pads> pads;
    std::array<juce::Slider, 7> sliders;
    std::array<juce::Label, 7> labels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 7> attachments;
    CutoffBar cutoffBar;
    juce::Label cutoffLabel;
    std::array<juce::Slider, 3> globalSliders;
    std::array<juce::Label, 3> globalLabels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 3> globalAttachments;
    juce::Slider swing;
    std::array<juce::TextButton, 12> variants;
    std::array<juce::Label, 12> laneNames;
    std::array<juce::Label, gh::maxSteps> stepNumbers;
    using StepGrid = std::array<std::array<VelocitySlider, gh::maxSteps>, 12>;
    std::unique_ptr<StepGrid> steps;
    std::optional<gh::Scene> clipboard;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::File cachedKitFile, cachedLoopFile;
    int cachedKitIndex = -1, cachedLoopIndex = -1;
    int selected = 0;
    std::array<juce::String, gh::pads> padNames;
    size_t kitCount = 0, loopCount = 0;
    bool refreshing = false;
    std::unique_ptr<SequencerWindow> sequenceWindow;
    VelocitySlider* selectedStep = nullptr;
    int displayedScene = -1;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumEditor)
};
