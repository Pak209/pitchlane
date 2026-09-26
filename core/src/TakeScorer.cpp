#include "pitchlane/TakeScorer.h"

#include <algorithm>
#include <cmath>

namespace pitchlane {

void TakeScorer::setReference(const NoteList& notes, const ReferenceMapping& mapping)
{
    notes_ = notes;
    map_ = mapping;
    order_.clear();
    for (int i = 0; i < static_cast<int>(notes_.size()); ++i)
        if (!notes_[static_cast<size_t>(i)].muted() && notes_[static_cast<size_t>(i)].length > 0.0) order_.push_back(i);
    std::stable_sort(order_.begin(), order_.end(), [this](int a, int b) { return notes_[size_t(a)].start < notes_[size_t(b)].start; });
    reset();
}

void TakeScorer::reset()
{
    frames_.clear();
    scores_.clear();
    nextToFinalise_ = 0;
    lastTime_ = -1e300;
}

void TakeScorer::addFrame(double songTime, float midi)
{
    if (songTime < lastTime_)   // time went backwards without a reset: treat as a new take
        reset();
    if (frames_.empty() && scores_.empty() && nextToFinalise_ == 0)
    {
        // Start of a take: skip notes that had already begun (their onset can't be judged).
        const double tol = settings_.timingTolMs * 0.001;
        while (nextToFinalise_ < order_.size() && map_.toSong(notes_[size_t(order_[nextToFinalise_])].start) < songTime - tol)
            ++nextToFinalise_;
    }
    lastTime_ = songTime;
    frames_.push_back({ songTime, midi });
    finishUpTo(songTime);
}

void TakeScorer::finishUpTo(double songTime)
{
    const double settle = settings_.timingTolMs * 0.001;
    while (nextToFinalise_ < order_.size())
    {
        const auto& n = notes_[size_t(order_[nextToFinalise_])];
        if (map_.toSong(n.end()) + settle > songTime) break;
        finalise(nextToFinalise_);
        ++nextToFinalise_;
    }
    // Keep only frames that a pending note can still use.
    if (nextToFinalise_ < order_.size())
    {
        const auto& n = notes_[size_t(order_[nextToFinalise_])];
        const double keepFrom = map_.toSong(n.start) - settings_.earlySearchMs * 0.001 - 0.05;
        auto it = std::lower_bound(frames_.begin(), frames_.end(), keepFrom, [](const Frame& f, double v) { return f.t < v; });
        if (it - frames_.begin() > 4096) frames_.erase(frames_.begin(), it);
    }
    else if (frames_.size() > 4096)
        frames_.erase(frames_.begin(), frames_.end() - 1);
}

void TakeScorer::finalise(size_t slot)
{
    const int idx = order_[slot];
    const auto& n = notes_[size_t(idx)];
    const double target = map_.targetPitch(n);
    const double S = map_.toSong(n.start), E = map_.toSong(n.end());

    // Onset search window: a little before the note, but not inside a previous note with the
    // same target pitch (a repeated note would otherwise "start" during its predecessor).
    double w0 = S - settings_.earlySearchMs * 0.001;
    double prevTarget = -1000.0;
    if (slot > 0)
    {
        const auto& p = notes_[size_t(order_[slot - 1])];
        prevTarget = map_.targetPitch(p);
        if (map_.targetPitch(p) == map_.targetPitch(n)) w0 = std::max(w0, map_.toSong(p.end()));
        else w0 = std::max(w0, map_.toSong(p.start) + 0.5 * p.length);
    }
    const double matchSemis = settings_.matchCents / 100.0;
    auto matches = [&](const Frame& f) {
        if (f.midi <= 0.f || std::abs(f.midi - target) > matchSemis) return false;
        // A frame must be nearer this note than the previous one (with a semitone step the
        // tail of a late previous note would otherwise count as this note's onset).
        return prevTarget == target || std::abs(f.midi - target) < std::abs(f.midi - prevTarget);
    };

    NoteScore sc;
    sc.noteIndex = idx;
    sc.noteSongStart = S;

    auto it = std::lower_bound(frames_.begin(), frames_.end(), w0, [](const Frame& f, double v) { return f.t < v; });
    double onset = -1.0;
    for (auto a = it; a != frames_.end() && a->t < E; ++a)
    {
        if (!matches(*a)) continue;
        auto b = a;
        while (b != frames_.end() && matches(*b) && b->t < E) ++b;
        const double runEnd = (b != frames_.end()) ? b->t : (b - 1)->t;
        if (runEnd - a->t >= settings_.minRunMs * 0.001) { onset = a->t; break; }
        a = b - 1;
    }
    if (onset < 0.0)
    {
        sc.timing = NoteScore::Timing::Missed;
        scores_.push_back(sc);
        return;
    }
    sc.onsetSongTime = onset;
    sc.onsetErrorMs = (onset - S) * 1000.0;
    sc.timing = std::abs(sc.onsetErrorMs) <= settings_.timingTolMs ? NoteScore::Timing::OnTime
              : sc.onsetErrorMs < 0.0 ? NoteScore::Timing::Early : NoteScore::Timing::Late;

    // Pitch over the sung part of the note (from the later of onset / note start).
    double sumT = 0, sumC = 0, sumTT = 0, sumTC = 0;
    int k = 0;
    const double p0 = std::max(onset, S);
    for (auto f = std::lower_bound(frames_.begin(), frames_.end(), p0, [](const Frame& fr, double v) { return fr.t < v; });
         f != frames_.end() && f->t < E; ++f)
    {
        if (!matches(*f)) continue;
        const double c = (f->midi - target) * 100.0, t = f->t - p0;
        sumT += t; sumC += c; sumTT += t * t; sumTC += t * c; ++k;
    }
    if (k > 0)
    {
        sc.meanCents = sumC / k;
        sc.pitch = std::abs(sc.meanCents) <= settings_.pitchTolCents ? NoteScore::Pitch::InTune
                 : sc.meanCents > 0.0 ? NoteScore::Pitch::Sharp : NoteScore::Pitch::Flat;
        const double denom = k * sumTT - sumT * sumT;
        const double span = E - p0;
        if (k >= 8 && span >= 0.3 && denom > 1e-12)
        {
            const double slope = (k * sumTC - sumT * sumC) / denom;   // cents per second
            sc.driftCents = slope * span;
            sc.drifting = std::abs(sc.driftCents) > settings_.driftCents;
        }
    }
    scores_.push_back(sc);
}

const NoteScore* TakeScorer::scoreFor(int index) const noexcept
{
    for (auto it = scores_.rbegin(); it != scores_.rend(); ++it)
        if (it->noteIndex == index) return &*it;
    return nullptr;
}

TakeSummary TakeScorer::summary() const
{
    TakeSummary s;
    double onsetSum = 0, centsSum = 0;
    int sung = 0, pitched = 0;
    for (const auto& sc : scores_)
    {
        ++s.scored;
        switch (sc.timing)
        {
            case NoteScore::Timing::OnTime: ++s.onTime; break;
            case NoteScore::Timing::Early:  ++s.early; break;
            case NoteScore::Timing::Late:   ++s.late; break;
            case NoteScore::Timing::Missed: ++s.missed; continue;
        }
        ++sung;
        onsetSum += std::abs(sc.onsetErrorMs);
        if (sc.pitch == NoteScore::Pitch::InTune) ++s.inTune;
        if (sc.pitch == NoteScore::Pitch::Sharp) ++s.sharp;
        if (sc.pitch == NoteScore::Pitch::Flat) ++s.flat;
        if (sc.pitch != NoteScore::Pitch::Unknown) { ++pitched; centsSum += std::abs(sc.meanCents); }
        if (sc.drifting) ++s.drifting;
    }
    s.meanAbsOnsetMs = sung > 0 ? onsetSum / sung : 0.0;
    s.meanAbsCents = pitched > 0 ? centsSum / pitched : 0.0;
    return s;
}

} // namespace pitchlane
