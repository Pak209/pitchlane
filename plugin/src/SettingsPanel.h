#pragma once
// Settings popover (gear button): secondary settings and every editing command that isn't
// in the main layout, so nothing from the previous UI is lost and everything stays reachable
// with the mouse even when Logic swallows key presses.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Widgets.h"

namespace pitchlane {

class PitchLaneProcessor;

class SettingsPanel : public juce::Component, private juce::Timer
{
public:
    struct Actions
    {
        std::function<void()> importMidi, exportMidi, clearTrace, restartClock, addMarker;
        std::function<bool()> getFollow;
        std::function<void(bool)> setFollow;
    };

    SettingsPanel(PitchLaneProcessor& p, Actions actions);
    ~SettingsPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    struct Row
    {
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    void timerCallback() override;
    void addRow(Row& r, const char* paramId, const juce::String& text, const juce::String& tip);
    juce::TextButton& button(const juce::String& text, std::function<void()> fn, const juce::String& tip = {});

    PitchLaneProcessor& proc_;
    Actions actions_;
    SegmentedControl names_;
    juce::Label namesLabel_;
    Row calib_, gate_, clarity_, span_;
    juce::ToggleButton follow_ { "Follow playhead" };
    juce::OwnedArray<juce::TextButton> buttons_;
    juce::TextButton *undo_ = nullptr, *redo_ = nullptr, *restart_ = nullptr;
    std::vector<juce::TextButton*> selectionButtons_;
    std::vector<std::pair<juce::String, int>> sections_; // title, y
};

} // namespace pitchlane
