#include "pitchlane/ReferenceNotes.h"

#include <algorithm>

namespace pitchlane {

void sortNotes(NoteList& notes)
{
    std::stable_sort(notes.begin(), notes.end(), [](const RefNote& a, const RefNote& b) {
        return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
    });
}

int findActiveNote(const NoteList& notes, double t) noexcept
{
    // Last note starting at or before t.
    auto it = std::upper_bound(notes.begin(), notes.end(), t,
                               [](double v, const RefNote& n) { return v < n.start; });
    // Scan backwards a little for overlaps (notes are short relative to the list).
    int scanned = 0;
    while (it != notes.begin() && scanned < 64)
    {
        --it;
        ++scanned;
        if (t < it->end() && !it->muted()) return static_cast<int>(it - notes.begin());
    }
    return -1;
}

size_t firstNoteEndingAfter(const NoteList& notes, double t) noexcept
{
    // Notes are sorted by start, not end; step back from the first note starting after
    // t - (a generous max note length) to stay O(log n) in practice.
    constexpr double kMaxLookback = 30.0;
    auto it = std::lower_bound(notes.begin(), notes.end(), t - kMaxLookback,
                               [](const RefNote& n, double v) { return n.start < v; });
    for (; it != notes.end(); ++it)
        if (it->end() > t) break;
    return static_cast<size_t>(it - notes.begin());
}

void removeShortNotes(NoteList& notes, double minLength)
{
    notes.erase(std::remove_if(notes.begin(), notes.end(),
                               [minLength](const RefNote& n) { return n.length < minLength; }),
                notes.end());
}

void mergeSamePitchGaps(NoteList& notes, double maxGap)
{
    if (notes.size() < 2) return;
    NoteList out;
    out.reserve(notes.size());
    out.push_back(notes.front());
    for (size_t i = 1; i < notes.size(); ++i)
    {
        RefNote& prev = out.back();
        const RefNote& n = notes[i];
        if (n.pitch == prev.pitch && n.start - prev.end() <= maxGap)
        {
            const double totalA = prev.length, totalB = n.length;
            const double newEnd = std::max(prev.end(), n.end());
            prev.confidence = static_cast<float>((prev.confidence * totalA + n.confidence * totalB)
                                                 / std::max(1e-9, totalA + totalB));
            prev.length = newEnd - prev.start;
        }
        else
        {
            out.push_back(n);
        }
    }
    notes.swap(out);
}

void sanitiseNotes(NoteList& notes, double minLength)
{
    for (auto& n : notes)
    {
        n.pitch = std::clamp(n.pitch, 0, 127);
        n.velocity = std::clamp(n.velocity, 1, 127);
        n.length = std::max(n.length, minLength);
        n.confidence = std::clamp(n.confidence, 0.f, 1.f);
    }
}

int maxPolyphony(const NoteList& notes, double tolerance) noexcept
{
    // Sweep over start/end events; an overlap only counts once it lasts `tolerance`.
    std::vector<std::pair<double, int>> ev;
    ev.reserve(notes.size() * 2);
    for (const auto& n : notes)
    {
        if (n.length <= tolerance) continue;
        ev.emplace_back(n.start + tolerance, +1);
        ev.emplace_back(n.end(), -1);
    }
    std::sort(ev.begin(), ev.end(), [](const auto& a, const auto& b) {
        return a.first < b.first || (a.first == b.first && a.second < b.second);   // ends first
    });
    int cur = 0, best = 0;
    for (const auto& e : ev) { cur += e.second; best = std::max(best, cur); }
    return best;
}

int markLeadLine(NoteList& notes, LeadMode mode)
{
    auto beats = [mode](const RefNote& a, const RefNote& b) {   // does a outrank b?
        if (mode == LeadMode::LoudestSustained)
        {
            const double sa = a.velocity * a.length, sb = b.velocity * b.length;
            if (std::abs(sa - sb) > 1e-9 * std::max(sa, sb)) return sa > sb;
        }
        if (a.pitch != b.pitch) return a.pitch > b.pitch;
        return a.start < b.start;   // same pitch doubled: the earlier one leads
    };
    std::vector<char> harmony(notes.size(), 0);
    for (size_t i = 0; i < notes.size(); ++i)
    {
        const auto& n = notes[i];
        if (n.length <= 0.0) continue;
        // Union of the parts of n covered by higher-priority notes.
        std::vector<std::pair<double, double>> cover;
        for (size_t j = 0; j < notes.size(); ++j)
        {
            if (j == i) continue;
            const auto& m = notes[j];
            if (m.start >= n.end()) break;   // sorted by start
            const double a = std::max(n.start, m.start), b = std::min(n.end(), m.end());
            if (b > a && beats(m, n)) cover.emplace_back(a, b);
        }
        std::sort(cover.begin(), cover.end());
        double covered = 0.0, runA = 0.0, runB = -1.0;
        for (const auto& c : cover)
        {
            if (c.first > runB) { if (runB > runA) covered += runB - runA; runA = c.first; runB = c.second; }
            else runB = std::max(runB, c.second);
        }
        if (runB > runA) covered += runB - runA;
        harmony[i] = covered > 0.5 * n.length ? 1 : 0;
    }
    int flagged = 0;
    for (size_t i = 0; i < notes.size(); ++i)
        if (harmony[i])
        {
            notes[i].setFlag(RefNote::HarmonySuspect, true);
            notes[i].setFlag(RefNote::Muted, true);
            ++flagged;
        }
    return flagged;
}

void removeMuted(NoteList& notes)
{
    notes.erase(std::remove_if(notes.begin(), notes.end(), [](const RefNote& n) { return n.muted(); }), notes.end());
}

} // namespace pitchlane
