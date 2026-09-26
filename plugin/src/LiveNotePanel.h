#pragma once
// Left panel: "LIVE NOTE" (target / nearest note name, cents deviation), the vertical
// +50 / SHARP / IN TUNE / FLAT / -50 meter and the detector confidence bar.

#include <juce_gui_basics/juce_gui_basics.h>

#include "PianoRoll.h"

namespace pitchlane {

class LiveNotePanel : public juce::Component
{
public:
    void setReadout(const Readout& r, OctaveConvention conv, float tolerance);
    void paint(juce::Graphics& g) override;

private:
    Readout r_;
    OctaveConvention conv_ = OctaveConvention::Scientific;
    float tolerance_ = 25.f;
    float shownConfidence_ = 0.f;
    float shownCents_ = 0.f;
};

} // namespace pitchlane
