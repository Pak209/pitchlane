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
        if (t < it->end()) return static_cast<int>(it - notes.begin());
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

} // namespace pitchlane
