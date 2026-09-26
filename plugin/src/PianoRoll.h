#pragma once
// The roll: bar-number ruler, user section-marker lane, vertical piano keyboard, grid,
// reference notes (lavender bars with a soft glow, editable), the live pitch trace (cyan,
// orange-red where the singer is outside the tolerance on a note), playhead and loop region.
// Painted from the editor's 60 Hz timer; data comes from the lock-free frame queue and the
// message-thread reference / marker models. Never touches the audio thread's state.

#include <juce_gui_basics/juce_gui_basics.h>

#include "LiveData.h"
#include "UiModel.h"
#include "pitchlane/NoteMath.h"
#include "pitchlane/ReferenceNotes.h"
#include "pitchlane/TempoMap.h"
#include "pitchlane/TakeScorer.h"

namespace pitchlane {

class PitchLaneProcessor;

struct Readout
{
    bool hasPitch = false;
    float midi = 0.f;
    float hz = 0.f;
    bool hasTarget = false;
    int target = 0;
    float cents = 0.f;       // vs target if any, else vs nearest semitone
    float confidence = 0.f;  // detector clarity (1 - YIN aperiodicity) of the latest frame
    TuningStatus status = TuningStatus::NoPitch;
    // Take feedback (last finished note + running summary); empty when nothing scored yet.
    juce::String lastNoteText;   // e.g. "Late +80 ms · 12¢ sharp"
    int lastNoteTiming = -1;     // NoteScore::Timing as int, -1 = none
    juce::String takeText;       // e.g. "Take: 9/12 on time · 2 late · 1 missed"
};

class PianoRoll : public juce::Component
{
public:
    static constexpr float kRulerH = 28.f, kLaneH = 26.f, kKeysW = 56.f;

    explicit PianoRoll(PitchLaneProcessor& p);
    ~PianoRoll() override;

    /** Drain the audio->UI queue and update the view. Call from the editor timer. */
    void update();

    Readout computeReadout() const;
    void clearTrace() { history_.clear(); }

    void setFollow(bool f) { follow_ = f; }
    bool getFollow() const { return follow_; }
    /** Scroll ← → (a quarter of the visible span per step); disables follow. */
    void scroll(int direction);
    void setViewStart(double seconds) { viewStart_ = seconds; follow_ = false; repaint(); }
    double getViewStart() const { return viewStart_; }
    /** Current song position (seconds), extrapolated between audio blocks. */
    double nowTime() const;
    TransportSnapshot getSnapshot() const { return snap_; }

    // ---- "Go to notes" ------------------------------------------------------------------
    struct FitResult
    {
        bool ok = false;           // false: no notes
        double start = 0.0, span = 0.0;
        int bar = 1;               // bar shown at the left edge
        double firstNoteTime = 0.0;   // song seconds of the first active note (start + offset)
        int pitchLo = 0, pitchHi = -1; // active notes (after transpose)
        int notesInView = 0;
    };
    /** Scrolls / zooms to the first active (unmuted) note: ~8 bars from its bar line, the
        vertical window centred on the phrase. Turns follow off (until the next play). The
        vocal range is the editor's business (see PitchLaneEditor::goToNotes). */
    FitResult fitToNotes();
    /** Active (unmuted; all if every note is muted) notes intersecting the visible window. */
    int numNotesInView() const;
    double getViewSpan() const;
    /** Visible pitch rows [lo, hi]. */
    std::pair<int, int> visiblePitchRange() const;
    /** Pitch range of the active notes after transpose; false when there are no notes. */
    static bool activePitchRange(const NoteList& notes, int transpose, int& lo, int& hi);
    /** Error card shown in the middle of the roll (e.g. a failed analysis); click to dismiss. */
    void setNotice(const juce::String& text) { notice_ = text; repaint(); }
    juce::String getNotice() const { return notice_; }
    /** Called when the "notes are outside this view" overlay is clicked. */
    std::function<void()> onGoToNotes;

    /** Adds a section marker at the playhead and opens the rename box. */
    void addMarkerAtPlayhead();

    /** Keyboard editing (forwarded by the editor). Returns true if handled. */
    bool handleKey(const juce::KeyPress& key);

    void paint(juce::Graphics& g) override;
    void resized() override;
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
        int rangeLo = 48, rangeHi = 72;    // vocal range (parameters)
        int lo = 48, hi = 72;              // visible rows
        double start = 0.0, span = 8.0;
        double offset = 0.0;    // reference offset (s)
        double calib = 0.0;     // trace calibration (s)
        int transpose = 0;
        float tolerance = 25.f;
        int key = 0;
        ScaleType scale = ScaleType::Major;
        OctaveConvention conv = OctaveConvention::Scientific;
        bool scalesGuide = false;
        bool showNotes = true, showTrace = true;
        const TempoMap* tempo = nullptr;   // bars/beats (host tempo map or manual tempo)
        juce::Rectangle<float> area;   // note area (right of the keyboard, below ruler + lane)
        juce::Rectangle<float> ruler, lane, keys;
        float rowH() const { return area.getHeight() / static_cast<float>(hi - lo + 1); }
        float yForMidi(double m) const { return area.getBottom() - static_cast<float>((m - lo + 0.5) * rowH()); }
        double midiForY(float y) const { return (area.getBottom() - y) / rowH() + lo - 0.5; }
        float xForTime(double t) const { return area.getX() + static_cast<float>((t - start) / span * area.getWidth()); }
        double timeForX(float x) const { return start + (x - area.getX()) / area.getWidth() * span; }
    };
    struct Target { int note = -1; int noteIndex = -1; };

    View makeView() const;
    Target targetAt(const View& v, const NoteList& notes, double displayTime, float midi) const;
    int hitTestNote(const View& v, const NoteList& notes, juce::Point<float> p, bool& nearRightEdge) const;
    int hitTestMarker(const View& v, juce::Point<float> p) const;
    juce::Rectangle<float> noteRect(const View& v, const RefNote& n) const;
    void beginRename(int markerIndex);
    void paintRuler(juce::Graphics& g, const View& v);
    void paintLane(juce::Graphics& g, const View& v);
    void paintKeyboard(juce::Graphics& g, const View& v, const Readout& readout);
    void paintOverlays(juce::Graphics& g, const View& v, const NoteList& notes);
    int countInView(const View& v, const NoteList& notes) const;
    juce::Rectangle<float> offscreenHint_, noticeRect_;
    juce::String notice_;   // clickable "Go to notes" overlay (empty = none)

    PitchLaneProcessor& proc_;
    std::vector<Hist> history_;
    LiveFrame latest_;
    double latestWallMs_ = 0.0;
    TransportSnapshot snap_;
    ui::TempoFollower tempo_;
    TakeScorer scorer_;
    uint32_t scorerRevision_ = 0xffffffffu;
    double scorerOffset_ = 1e300;
    int scorerTranspose_ = 0;
    float scorerTimingTol_ = -1.f, scorerPitchTol_ = -1.f;
    void syncScorer();
    void showNoteMenu(const juce::MouseEvent& e, int noteIndex);

public:
    const TakeScorer& getScorer() const noexcept { return scorer_; }
    /** Test hook: feed a frame to the take scorer as update() would. */
    void scoreFrameForTest(double songTime, float midi) { syncScorer(); scorer_.addFrame(songTime, midi); }

private:
    bool wasPlaying_ = false;
    bool follow_ = true;
    double viewStart_ = 0.0;
    double pitchCentre_ = 60.0;
    double rowsWanted_ = 0.0;      // auto vertical zoom (0 = whole range)
    bool manualPitch_ = false;

    ui::RollAction drag_ = ui::RollAction::None;
    bool gestureStarted_ = false;
    int anchor_ = -1;
    int createdIndex_ = -1;
    NoteList dragOrig_;
    std::vector<bool> dragSel_;
    juce::Point<float> downPos_;
    double viewStartAtDown_ = 0.0;
    juce::Rectangle<float> rubber_;
    int markerDrag_ = -1;
    double markerDownTime_ = 0.0;
    std::unique_ptr<juce::TextEditor> renameEditor_;
    int renameIndex_ = -1;
};

} // namespace pitchlane
