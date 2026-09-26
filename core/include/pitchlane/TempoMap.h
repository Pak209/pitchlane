#pragma once
// Song time <-> musical time (beats, bars) for the bar ruler, the grid and note snapping.
//
// A host only reports its *current* tempo, time signature and position, so the map is learned
// from observations made during playback: each observation that disagrees with the current
// prediction (a tempo change, a time-signature change, a relocated bar line) starts a new
// segment there, and segments after it are dropped (they are re-learned when playback gets
// there). The first observation after setConstant() rebuilds the map from that observation.
// Outside the observed range the nearest segment is extended. Without a host timeline,
// setConstant() describes a single segment (manual BPM and time signature).
//
// Not real-time safe (uses std::vector); used on the message thread only.

#include <vector>

namespace pitchlane {

struct TempoObservation
{
    double time = 0.0;         // song seconds
    double ppq = 0.0;          // quarter notes since song start (host)
    bool hasPpq = false;
    double bpm = 120.0;
    int num = 4, den = 4;
    double barStartPpq = 0.0;  // ppq of the bar line at or before ppq (host), if known
    bool hasBarStart = false;
};

struct BarBeat
{
    int bar = 1;               // 1-based
    int beat = 1;              // 1-based, in units of the time-signature denominator
    double fraction = 0.0;     // position within the beat, 0..1
};

class TempoMap
{
public:
    struct Segment
    {
        double time = 0.0;     // song seconds where the segment starts
        double ppq = 0.0;      // quarter notes at `time`
        double bpm = 120.0;    // quarter notes per minute
        int num = 4, den = 4;
        double barPpq = 0.0;   // a bar line (in ppq) of this segment
        int barNumber = 1;     // the 1-based number of that bar line
    };

    TempoMap() { setConstant(120.0, 4, 4); }

    /** One segment from song time 0: bar 1 starts at 0 s. */
    void setConstant(double bpm, int num, int den);

    /** Learn from a host observation. Returns true if the map changed. */
    bool observe(const TempoObservation& o);

    const std::vector<Segment>& segments() const noexcept { return segs_; }

    double ppqAt(double time) const noexcept;
    double timeAtPpq(double ppq) const noexcept;
    double bpmAt(double time) const noexcept { return segmentForTime(time).bpm; }

    BarBeat barBeatAt(double time) const noexcept;
    /** Song time of the start of a 1-based bar number. */
    double timeOfBar(int bar) const noexcept;
    /** Bar lines (start times) within [t0, t1], with their numbers. */
    struct BarLine { double time; int bar; int num, den; };
    std::vector<BarLine> barLines(double t0, double t1) const;
    /** Beat lines (in denominator units) within [t0, t1]; `downbeat` marks bar lines. */
    struct BeatLine { double time; bool downbeat; };
    std::vector<BeatLine> beatLines(double t0, double t1) const;

    /** Seconds per beat (denominator unit) at a time. */
    double secondsPerBeatAt(double time) const noexcept;

    static double barLengthPpq(int num, int den) noexcept { return num * 4.0 / (den > 0 ? den : 4); }

private:
    const Segment& segmentForTime(double t) const noexcept;
    const Segment& segmentForPpq(double ppq) const noexcept;
    const Segment& segmentForBar(int bar) const noexcept;
    static double barFloat(const Segment& s, double ppq) noexcept;

    std::vector<Segment> segs_;
    bool learned_ = false;   // false after setConstant(): the next observation rebuilds the map
};

} // namespace pitchlane
