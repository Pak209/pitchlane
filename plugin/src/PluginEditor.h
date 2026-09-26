#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PianoRoll.h"
#include "PluginProcessor.h"

namespace pitchlane {

/** Big note / cents / flat-in tune-sharp readout with a cents meter. */
class ReadoutDisplay : public juce::Component
{
public:
    void setReadout(const Readout& r, OctaveConvention conv, float tolerance);
    void paint(juce::Graphics& g) override;

private:
    Readout r_;
    OctaveConvention conv_ = OctaveConvention::Scientific;
    float tolerance_ = 10.f;
};

class PitchLaneEditor : public juce::AudioProcessorEditor,
                        public juce::FileDragAndDropTarget,
                        private juce::Timer
{
public:
    explicit PitchLaneEditor(PitchLaneProcessor&);
    ~PitchLaneEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    struct LabeledSlider
    {
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    struct LabeledCombo
    {
        juce::Label label;
        juce::ComboBox combo;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };

    void timerCallback() override;
    void setupSlider(LabeledSlider& s, const char* paramId, const juce::String& label, juce::Slider::SliderStyle style);
    void setupCombo(LabeledCombo& c, const char* paramId, const juce::String& label);
    void chooseVocalFile();
    void setPendingVocal(const juce::File& f);
    void startAnalysis();
    void importMidi();
    void importMidiFile(const juce::File& f);
    void exportMidi();
    void updateSourceLabel();
    static bool isMidiFile(const juce::File& f);

    PitchLaneProcessor& proc_;
    PianoRoll roll_;
    ReadoutDisplay readout_;
    juce::Label transportLabel_;

    LabeledCombo key_, scale_, names_;
    LabeledSlider tempo_, low_, high_, tolerance_, calibration_, offset_, transpose_, gate_, clarity_, span_;

    juce::TextButton loadVocalBtn_ { "Load Vocal..." }, analyzeBtn_ { "Analyze Vocal" }, cancelBtn_ { "Cancel" };
    juce::TextButton importMidiBtn_ { "Import MIDI..." }, exportMidiBtn_ { "Export MIDI..." };
    juce::TextButton deleteBtn_ { "Delete" }, upBtn_ { "+1 st" }, downBtn_ { "-1 st" };
    juce::TextButton earlierBtn_ { "<< 10ms" }, laterBtn_ { "10ms >>" };
    juce::TextButton undoBtn_ { "Undo" }, redoBtn_ { "Redo" }, clearNotesBtn_ { "Clear Notes" };
    juce::TextButton clearTraceBtn_ { "Clear Trace" }, restartBtn_ { "Restart Clock" };
    juce::ToggleButton followBtn_ { "Follow" };
    double progress_ = 0.0;
    juce::ProgressBar progressBar_ { progress_ };
    juce::Label statusLabel_, sourceLabel_;

    juce::File pendingVocal_;
    std::unique_ptr<juce::FileChooser> chooser_;
    int tick_ = 0;
    bool dragHover_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchLaneEditor)
};

} // namespace pitchlane
