#pragma once
// Per-note feedback for a take: onset timing (early / on time / late), pitch accuracy (sharp /
// in tune / flat), drift across the note, and missed notes; plus a summary for the take.
// Fed with the live pitch frames in time order (message thread; not real-time safe).
// Muted reference notes are ignored. reset() starts a new take (seek, loop wrap, play start).

#include <vector>

#include "pitchlane/ReferenceNotes.h"

namespace pitchlane {

struct ScoreSettings
{
    double timingTolMs = 80.0;     // |onset error| <= this is "on time"
    double pitchTolCents = 25.0;   // |mean deviation| <= this is "in tune"
    double driftCents = 30.0;      // pitch change across a note beyond this is "drifting"
    double matchCents = 100.0;     // frames within this of the target count as singing the note
    double minRunMs = 40.0;        // an onset needs this much continuous matching pitch
    double earlySearchMs = 300.0;  // how early before the note an onset is looked for
};

struct NoteScore
{
    enum class Timing { OnTime, Early, Late, Missed };
    enum class Pitch { InTune, Sharp, Flat, Unknown };
    int noteIndex = -1;
    double noteSongStart = 0.0;    // song time of the reference note
    Timing timing = Timing::Missed;
    double onsetErrorMs = 0.0;     // + = late, - = early
    double onsetSongTime = 0.0;
    Pitch pitch = Pitch::Unknown;
    double meanCents = 0.0;        // + = sharp
    double driftCents = 0.0;       // fitted change from note start to end (+ = rising)
    bool drifting = false;
};

struct TakeSummary
{
    int scored = 0, onTime = 0, early = 0, late = 0, missed = 0;
    int inTune = 0, sharp = 0, flat = 0, drifting = 0;
    double meanAbsOnsetMs = 0.0;   // over sung notes
    double meanAbsCents = 0.0;
};

class TakeScorer
{
public:
    void setSettings(const ScoreSettings& s) { settings_ = s; }
    const ScoreSettings& settings() const noexcept { return settings_; }

    /** New reference (or mapping): resets the take. Notes must be sorted. */
    void setReference(const NoteList& notes, const ReferenceMapping& mapping);
    /** New take: drop all scores (keeps the reference). */
    void reset();

    /** One live frame (song time, detected MIDI pitch, <= 0 when unvoiced). Times must not
        go backwards between resets. Notes whose window has passed are finalised. */
    void addFrame(double songTime, float midi);
    /** Finalise every note that ends before songTime (e.g. when playback stops). */
    void finishUpTo(double songTime);

    const std::vector<NoteScore>& scores() const noexcept { return scores_; }
    /** The most recently finalised score, or nullptr. */
    const NoteScore* latest() const noexcept { return scores_.empty() ? nullptr : &scores_.back(); }
    /** Score of reference note `index` in this take, or nullptr. */
    const NoteScore* scoreFor(int index) const noexcept;
    TakeSummary summary() const;

private:
    struct Frame { double t; float midi; };
    void finalise(size_t noteSlot);

    ScoreSettings settings_;
    NoteList notes_;
    ReferenceMapping map_;
    std::vector<int> order_;          // unmuted note indices, by song start
    size_t nextToFinalise_ = 0;       // into order_
    std::vector<Frame> frames_;       // recent frames (trimmed as notes finalise)
    std::vector<NoteScore> scores_;
    double lastTime_ = -1e300;
};

} // namespace pitchlane
