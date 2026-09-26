#pragma once

#include "LiveData.h"
#include "pitchlane/TempoMap.h"
// Pure UI logic (no painting) so it can be unit-tested headlessly: mouse-gesture mapping for
// the piano roll, bar/beat maths for the ruler, visible pitch window, label formatting.

#include <juce_core/juce_core.h>

#include <algorithm>

#include "pitchlane/NoteMath.h"

namespace pitchlane::ui {

// ---- piano-roll gestures -------------------------------------------------------------
// Labelled in the hint bar as:
//   "Drag to adjust reference notes · Option-drag to create · Shift-drag to stretch ·
//    Double-click to delete"
enum class RollAction
{
    None,
    Move,          // drag a note (and the rest of the selection): time + pitch
    Stretch,       // change note length (Shift-drag on a note, or drag its right edge)
    Create,        // Option(Alt)-drag: draw a new note
    ToggleSelect,  // Cmd-click a note: add/remove it from the selection, then drag moves
    Rubber,        // drag on empty space: rubber-band selection
    RubberAdd,     // Shift/Cmd-drag on empty space: add to the selection
    Pan            // middle-button drag (or drag on the ruler): scroll time
};

struct PressInfo
{
    bool onNote = false;
    bool nearRightEdge = false;
    bool alt = false, shift = false, cmd = false;
    bool middleButton = false;
    bool onRuler = false;
};

RollAction actionForPress(const PressInfo& p) noexcept;

enum class DoubleClickAction { None, DeleteNote };
DoubleClickAction actionForDoubleClick(bool onNote) noexcept;

/** Creation drag: the note spans the two times (in either direction), at least minLength. */
struct CreatedSpan { double start, length; };
CreatedSpan createdSpan(double downTime, double dragTime, double minLength = 0.05) noexcept;

// ---- ruler / grid ----------------------------------------------------------------------
/** Keeps the editor's tempo map in step with the transport: follows the host's tempo map
    (learned from ppq / bar-start observations) while Logic supplies a musical position, and
    otherwise shows a constant grid at the current (manual) tempo and time signature. */
class TempoFollower
{
public:
    /** Returns true when the map changed (repaint the grid). */
    bool update(const TransportSnapshot& s);
    const TempoMap& map() const noexcept { return map_; }
    bool followingHost() const noexcept { return following_; }

private:
    TempoMap map_;
    bool following_ = false;
};

// ---- vertical window -------------------------------------------------------------------
/** Visible pitch rows [lo, hi] inside the vocal range. The window shows `wantedRows` rows
    (at least 12, e.g. the current phrase plus a margin; <= 0 means the whole range), but never
    more than fit at minRowPx per semitone, centred on `centre` and clamped to the range. */
struct PitchWindow { int lo, hi; };
PitchWindow fitPitchWindow(int rangeLo, int rangeHi, float heightPx, float minRowPx, double centre,
                           int wantedRows = 0) noexcept;
/** Rows wanted to show a phrase spanning [pLo, pHi] comfortably (2-3 rows of margin). */
inline int rowsForPhrase(int pLo, int pHi) noexcept { return pHi >= pLo ? std::max(13, pHi - pLo + 6) : 0; }

// ---- scrolling ---------------------------------------------------------------------------
/** Scroll ← → buttons move the view by a quarter of the visible span. */
inline double scrollStep(double spanSeconds, int direction) noexcept { return 0.25 * spanSeconds * (direction < 0 ? -1 : 1); }
/** ⌘ + scroll zoom: new visible span for a wheel delta (clamped to the parameter range). */
double zoomedSpan(double span, float wheelDelta, double minSpan = 2.0, double maxSpan = 30.0) noexcept;

// ---- labels ------------------------------------------------------------------------------
/** "A minor", "C major", "D dorian", "E harmonic minor", "chromatic". */
juce::String keyScaleText(int key, ScaleType scale);
/** "C3 – C5" */
juce::String rangeText(int lo, int hi, OctaveConvention conv);
/** "+42 ms", "0 ms", "−8 ms" (typographic minus). */
juce::String signedMsText(double ms);
/** "0 st", "+2 st", "−3 st". */
juce::String semitoneText(int st);
/** "−18 cents" / "+4 cents". */
juce::String centsText(double cents);

struct RangePreset { const char* name; int lo, hi; };
/** Common vocal ranges (MIDI notes) offered by the Vocal range menu. */
const std::vector<RangePreset>& rangePresets();

/** Meter position of a cents value, 0 = bottom (−50), 1 = top (+50), clamped. */
inline float meterFraction(double cents) noexcept
{
    return static_cast<float>(juce::jlimit(0.0, 1.0, 0.5 + cents / 100.0));
}

/** Target used for colouring/readout in "Scales" guide mode: nearest in-scale note. */
int nearestScaleNote(double midi, int key, ScaleType scale) noexcept;

} // namespace pitchlane::ui
