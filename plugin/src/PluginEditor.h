#pragma once
// Pitch Lane editor: header (logo, reference stem, Analyze Vocal, key, BPM, Logic Sync,
// status, settings), LIVE NOTE panel, the roll, the gesture hint bar and the bottom
// controls (reference offset, transpose, vocal range, tolerance, guide, display, smoothing).

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "LiveNotePanel.h"
#include "PianoRoll.h"
#include "PluginProcessor.h"
#include "Theme.h"
#include "Widgets.h"

namespace pitchlane {

class PitchLaneEditor : public juce::AudioProcessorEditor,
                        public juce::FileDragAndDropTarget,
                        private juce::Timer
{
public:
    explicit PitchLaneEditor(PitchLaneProcessor&);
    ~PitchLaneEditor() override;

    static constexpr int kDefaultW = 1240, kDefaultH = 720, kMinW = 1040, kMinH = 620;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragExit(const juce::StringArray&) override { dragHover_ = false; repaint(); }
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    PianoRoll& getRoll() noexcept { return roll_; }   // used by the snapshot tool

    // ---- hint-bar status line ---------------------------------------------------------------
    enum class ToastKind { Info, Success, Error };
    /** Shows a message in the hint bar. seconds <= 0: stays until the next message or a click
        (errors). An optional action button (e.g. "Expand range to G3-A5") sits next to it. */
    void showToast(const juce::String& message, ToastKind kind = ToastKind::Info, double seconds = 6.0,
                   const juce::String& actionText = {}, std::function<void()> action = {});
    juce::String getStatusMessage() const { return toastVisible() ? toast_ : juce::String(); }
    ToastKind getStatusKind() const noexcept { return toastKind_; }
    juce::String getStatusActionText() const { return toastActionBtn_.isVisible() ? toastActionBtn_.getButtonText() : juce::String(); }

    // ---- vocal loading / analysis (also driven by the integration test) ----------------------
    /** Same path as the file chooser and drag and drop: remembers the file for ANALYZE VOCAL.
        Returns false (and shows why) if the file can't be used. */
    bool loadVocalFile(const juce::File& f);
    /** ANALYZE VOCAL: analyses the loaded file (or re-analyses the current source). */
    void startAnalysis();
    /** "Go to notes": fits the roll to the first active note (~8 bars) and makes sure the
        notes' pitch range is visible (sets the vocal range automatically while it is still
        the default, otherwise offers to expand it). */
    PianoRoll::FitResult goToNotes(bool afterAnalysis = false);
    juce::TextButton& getGoToNotesButton() noexcept { return fitBtn_; }
    StatusDot& getStatusDot() noexcept { return statusDot_; }
    /** Runs one UI refresh (the 60 Hz timer body); used by tests. */
    void refreshForTest() { timerCallback(); }

    static constexpr const char* kHintText =
        "Drag to adjust reference notes  \xc2\xb7  Option-drag to create  \xc2\xb7  Shift-drag to stretch  \xc2\xb7  Double-click to delete";

private:
    void timerCallback() override;
    void chooseVocalFile();
    void setPendingVocal(const juce::File& f);
    void handleAnalysisOutcome(const PitchLaneProcessor::AnalysisOutcome& o);
    void setRange(int lo, int hi);
    bool toastVisible() const;
    void mouseDown(const juce::MouseEvent& e) override;
    void importMidi();
    void importMidiFile(const juce::File& f);
    void exportMidi();
    void updateReferenceField();
    void showSettings();
    void setupBottomBar();
    static bool isMidiFile(const juce::File& f);
    juce::RangedAudioParameter* param(const char* id) { return proc_.getApvts().getParameter(id); }

    PitchLaneProcessor& proc_;
    theme::LookAndFeel laf_;
    juce::TooltipWindow tooltips_ { this, 450 };

    PianoRoll roll_;
    LiveNotePanel live_;

    // header
    ReferenceField refField_;
    juce::TextButton analyzeBtn_ { "ANALYZE VOCAL" };
    ValueField keyField_, bpmField_;
    SyncButton syncBtn_;
    std::unique_ptr<juce::ButtonParameterAttachment> syncAttachment_;
    StatusDot statusDot_;
    IconButton gearBtn_;

    // hint bar
    IconButton scrollLeft_, scrollRight_;
    juce::TextButton selDown_, selUp_, selEarlier_, selLater_, selDelete_ { "Delete" };
    juce::TextButton fitBtn_ { "Go to notes" }, toastActionBtn_;
    std::function<void()> toastAction_;

    // bottom bar
    Caption offsetCap_, transposeCap_, rangeCap_, toleranceCap_, guideCap_, displayCap_, smoothingCap_;
    ValueField offsetField_, transposeField_, rangeField_, toleranceField_;
    SegmentedControl guideSeg_, displaySeg_;
    juce::Slider smoothingKnob_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> smoothingAttachment_;
    juce::Rectangle<int> smoothingValueArea_, headerArea_, hintArea_, bottomArea_, keyLabelArea_, bpmLabelArea_;

    juce::File pendingVocal_;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::String toast_;
    ToastKind toastKind_ = ToastKind::Info;
    double toastUntilMs_ = 0.0;   // < 0: until dismissed
    uint32_t lastAnalysisSerial_ = 0;
    bool openFitChecked_ = false;
    int tick_ = 0;
    bool dragHover_ = false;
    double progress_ = 0.0;
    bool analysing_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchLaneEditor)
};

} // namespace pitchlane
