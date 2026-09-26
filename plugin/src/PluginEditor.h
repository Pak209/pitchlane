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
    void showToast(const juce::String& message);

    static constexpr const char* kHintText =
        "Drag to adjust reference notes  \xc2\xb7  Option-drag to create  \xc2\xb7  Shift-drag to stretch  \xc2\xb7  Double-click to delete";

private:
    void timerCallback() override;
    void chooseVocalFile();
    void setPendingVocal(const juce::File& f);
    void startAnalysis();
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

    // bottom bar
    Caption offsetCap_, transposeCap_, rangeCap_, toleranceCap_, guideCap_, displayCap_, smoothingCap_;
    ValueField offsetField_, transposeField_, rangeField_, toleranceField_;
    SegmentedControl guideSeg_, displaySeg_;
    juce::Slider smoothingKnob_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> smoothingAttachment_;
    juce::Rectangle<int> smoothingValueArea_, headerArea_, hintArea_, bottomArea_, keyLabelArea_, bpmLabelArea_;

    juce::File pendingVocal_;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::String toast_, lastAnalysisStatus_;
    double toastUntilMs_ = 0.0;
    int tick_ = 0;
    bool dragHover_ = false;
    double progress_ = 0.0;
    bool analysing_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchLaneEditor)
};

} // namespace pitchlane
