#pragma once
// Scrolling chromatic piano roll: vertical keyboard, in-scale row highlighting, reference
// target bars (editable), the live pitch trace coloured by flat / in-tune / sharp, the
// playhead and the loop region. Painted from a 60 Hz timer in the editor; all data comes
// from the lock-free frame queue and the (message-thread) reference model.

#include <juce_gui_basics/juce_gui_basics.h>

#include "LiveData.h"
#include "pitchlane/NoteMath.h"
#include "pitchlane/ReferenceNotes.h"

namespace pitchlane {

class PitchLaneProcessor;

struct Readout
{
    bool hasPitch = false;
    float midi = 0.f;
    float hz = 0.f;
    bool hasTarget = false;
    int target = 0;
    float cents = 0.f;  // vs target if any, else vs nearest semitone
    TuningStatus status = TuningStatus::NoPitch;
};

class PianoRoll : public juce::Component
{
public:
    explicit PianoRoll(PitchLaneProcessor& p);

    /** Drain the audio->UI queue and update the view. Call from the editor timer. */
    void update();

    Readout computeReadout() const;
    void clearTrace() { history_.clear(); }

    void setFollow(bool f) { follow_ = f; }
    bool getFollow() const { return follow_; }

    /** Keyboard editing (forwarded by the editor). Returns true if handled. */
    bool handleKey(const juce::KeyPress& key);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;

    static juce::Colour statusColour(TuningStatus s);

private:
    struct Hist { double t; float midi; };

    // Settings snapshot (read once per paint / event).
    struct View
    {
        int lo = 48, hi = 79;
        double start = 0.0, span = 8.0;
        double offset = 0.0;    // reference offset (s)
        double calib = 0.0;     // trace calibration (s)
        int transpose = 0;
        float tolerance = 10.f;
        int key = 0;
        ScaleType scale = ScaleType::Major;
        OctaveConvention conv = OctaveConvention::Scientific;
        float keyboardW = 58.f;
        juce::Rectangle<float> area;  // note area (right of the keyboard)
        float rowH() const { return area.getHeight() / static_cast<float>(hi - lo + 1); }
        float yForMidi(double m) const { return area.getBottom() - static_cast<float>((m - lo + 0.5) * rowH()); }
        double midiForY(float y) const { return (area.getBottom() - y) / rowH() + lo - 0.5; }
        float xForTime(double t) const { return area.getX() + static_cast<float>((t - start) / span * area.getWidth()); }
        double timeForX(float x) const { return start + (x - area.getX()) / area.getWidth() * span; }
    };
    View makeView() const;
    double nowTime() const;
    int hitTestNote(const View& v, const NoteList& notes, juce::Point<float> p, bool& nearRightEdge) const;
    TuningStatus statusAt(const View& v, const NoteList& notes, double displayTime, float midi) const;

    PitchLaneProcessor& proc_;
    std::vector<Hist> history_;
    LiveFrame latest_;
    double latestWallMs_ = 0.0;
    TransportSnapshot snap_;
    bool wasPlaying_ = false;
    bool follow_ = true;
    double viewStart_ = 0.0;

    enum class Drag { None, Move, Resize, Rubber, Pan };
    Drag drag_ = Drag::None;
    bool gestureStarted_ = false;
    bool rubberAdd_ = false;
    int anchor_ = -1;
    NoteList dragOrig_;
    std::vector<bool> dragSel_;
    juce::Point<float> downPos_;
    double viewStartAtDown_ = 0.0;
    juce::Rectangle<float> rubber_;
};

} // namespace pitchlane
