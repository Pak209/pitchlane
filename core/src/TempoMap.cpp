#include "pitchlane/TempoMap.h"

#include <algorithm>
#include <cmath>

namespace pitchlane {

namespace {
double sane(double bpm) { return bpm > 1.0 && bpm < 1000.0 ? bpm : 120.0; }
int saneNum(int n) { return n >= 1 && n <= 64 ? n : 4; }
int saneDen(int d) { return (d == 1 || d == 2 || d == 4 || d == 8 || d == 16 || d == 32) ? d : 4; }
} // namespace

void TempoMap::setConstant(double bpm, int num, int den)
{
    segs_.clear();
    Segment s;
    s.bpm = sane(bpm);
    s.num = saneNum(num);
    s.den = saneDen(den);
    segs_.push_back(s);
    learned_ = false;
}

double TempoMap::barFloat(const Segment& s, double ppq) noexcept
{
    return s.barNumber + (ppq - s.barPpq) / barLengthPpq(s.num, s.den);
}

const TempoMap::Segment& TempoMap::segmentForTime(double t) const noexcept
{
    auto it = std::upper_bound(segs_.begin(), segs_.end(), t, [](double v, const Segment& s) { return v < s.time; });
    return it == segs_.begin() ? segs_.front() : *(it - 1);
}

const TempoMap::Segment& TempoMap::segmentForPpq(double ppq) const noexcept
{
    auto it = std::upper_bound(segs_.begin(), segs_.end(), ppq, [](double v, const Segment& s) { return v < s.ppq; });
    return it == segs_.begin() ? segs_.front() : *(it - 1);
}

const TempoMap::Segment& TempoMap::segmentForBar(int bar) const noexcept
{
    // Segment whose bar range contains `bar`: the last segment starting at or before it.
    const Segment* best = &segs_.front();
    for (const auto& s : segs_)
        if (std::floor(barFloat(s, s.ppq) - 1e-9) < bar || &s == &segs_.front()) best = &s;
    return *best;
}

double TempoMap::ppqAt(double time) const noexcept
{
    const auto& s = segmentForTime(time);
    return s.ppq + (time - s.time) * s.bpm / 60.0;
}

double TempoMap::timeAtPpq(double ppq) const noexcept
{
    const auto& s = segmentForPpq(ppq);
    return s.time + (ppq - s.ppq) * 60.0 / s.bpm;
}

double TempoMap::secondsPerBeatAt(double time) const noexcept
{
    const auto& s = segmentForTime(time);
    return 60.0 / s.bpm * 4.0 / s.den;
}

BarBeat TempoMap::barBeatAt(double time) const noexcept
{
    const auto& s = segmentForTime(time);
    const double ppq = s.ppq + (time - s.time) * s.bpm / 60.0;
    const double bf = barFloat(s, ppq);
    BarBeat bb;
    bb.bar = static_cast<int>(std::floor(bf + 1e-9));
    const double beatsIn = (bf - bb.bar) * s.num;
    bb.beat = static_cast<int>(std::floor(beatsIn + 1e-9)) + 1;
    bb.fraction = std::max(0.0, beatsIn - (bb.beat - 1));
    return bb;
}

double TempoMap::timeOfBar(int bar) const noexcept
{
    const auto& s = segmentForBar(bar);
    const double ppq = s.barPpq + (bar - s.barNumber) * barLengthPpq(s.num, s.den);
    return s.time + (ppq - s.ppq) * 60.0 / s.bpm;
}

std::vector<TempoMap::BarLine> TempoMap::barLines(double t0, double t1) const
{
    std::vector<BarLine> out;
    if (t1 < t0) return out;
    int bar = barBeatAt(t0).bar;
    for (int guard = 0; guard < 100000; ++guard, ++bar)
    {
        const double t = timeOfBar(bar);
        if (t > t1 + 1e-9) break;
        if (t >= t0 - 1e-9)
        {
            const auto& s = segmentForTime(t + 1e-9);
            out.push_back({ t, bar, s.num, s.den });
        }
    }
    return out;
}

std::vector<TempoMap::BeatLine> TempoMap::beatLines(double t0, double t1) const
{
    std::vector<BeatLine> out;
    if (t1 < t0) return out;
    int bar = barBeatAt(t0).bar;
    for (int guard = 0; guard < 100000; ++guard, ++bar)
    {
        const double tb = timeOfBar(bar);
        if (tb > t1 + 1e-9) break;
        const double tn = timeOfBar(bar + 1);
        const auto& s = segmentForTime(tb + 1e-9);
        for (int b = 0; b < s.num; ++b)
        {
            const double t = tb + (tn - tb) * b / s.num;
            if (t >= t0 - 1e-9 && t <= t1 + 1e-9) out.push_back({ t, b == 0 });
        }
    }
    return out;
}

bool TempoMap::observe(const TempoObservation& o)
{
    Segment n;
    n.time = o.time;
    n.bpm = sane(o.bpm);
    n.num = saneNum(o.num);
    n.den = saneDen(o.den);
    n.ppq = o.hasPpq ? o.ppq : o.time * n.bpm / 60.0;
    const double barLen = barLengthPpq(n.num, n.den);
    // Anchor a bar line: the host's last bar start when known, else bars counted from ppq 0.
    n.barPpq = o.hasBarStart ? o.barStartPpq : std::floor(n.ppq / barLen + 1e-9) * barLen;

    // Consistent with what we already predict? Then nothing to learn.
    const auto& cur = segmentForTime(o.time);
    const double predictedPpq = cur.ppq + (o.time - cur.time) * cur.bpm / 60.0;
    const double curBarLen = barLengthPpq(cur.num, cur.den);
    const double barPhaseErr = std::remainder(n.barPpq - cur.barPpq, curBarLen);
    const bool same = std::abs(cur.bpm - n.bpm) < 0.01 && cur.num == n.num && cur.den == n.den
                      && std::abs(predictedPpq - n.ppq) < 0.05 && std::abs(barPhaseErr) < 0.05;
    if (same)
    {
        learned_ = true;   // the host confirmed the current map; later changes append to it
        return false;
    }

    // First observation after setConstant(): rebuild the whole map from it (bars counted from
    // ppq 0 with this signature, back-extrapolated to the song start).
    auto it = std::upper_bound(segs_.begin(), segs_.end(), o.time, [](double v, const Segment& s) { return v < s.time; });
    if (!learned_ || it == segs_.begin())
    {
        learned_ = true;
        n.barNumber = 1 + static_cast<int>(std::lround(n.barPpq / barLen));
        Segment first = n;
        first.time = n.time - n.ppq * 60.0 / n.bpm;   // song time of ppq 0
        first.ppq = 0.0;
        segs_.clear();
        segs_.push_back(first);
        return true;
    }

    // Otherwise a change happened here: continue the bar count from the segment before (a new
    // time signature starts a new bar at the anchor), drop the stale future, append.
    const auto& p = *(it - 1);
    const double bfp = barFloat(p, n.barPpq);
    n.barNumber = (p.num != n.num || p.den != n.den) ? static_cast<int>(std::ceil(bfp - 1e-6))
                                                     : static_cast<int>(std::lround(bfp));
    segs_.erase(it, segs_.end());
    if (!segs_.empty() && std::abs(segs_.back().time - n.time) < 1e-9) segs_.pop_back();
    if (segs_.empty()) { n.barNumber = 1 + static_cast<int>(std::lround(n.barPpq / barLen)); }
    segs_.push_back(n);
    return true;
}

} // namespace pitchlane
