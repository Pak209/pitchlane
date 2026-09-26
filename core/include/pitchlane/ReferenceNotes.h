#pragma once
// Reference (target) melody model. Times are in seconds of "reference time"; the song
// position of a note is start + referenceOffset, its target pitch is pitch + transpose.

#include <cstdint>
#include <vector>

namespace pitchlane {

struct RefNote
{
    double start = 0.0;       // seconds (reference time)
    double length = 0.0;      // seconds
    int pitch = 60;           // MIDI note number
    float confidence = 1.f;   // 0..1 (analyzer confidence; 1 for MIDI / hand-made notes)
    int velocity = 100;

    double end() const noexcept { return start + length; }
};

using NoteList = std::vector<RefNote>;

struct ReferenceMapping
{
    double offsetSec = 0.0;  // added to note times to get song time
    int transpose = 0;       // semitones added to note pitch

    double toSong(double refTime) const noexcept { return refTime + offsetSec; }
    double toRef(double songTime) const noexcept { return songTime - offsetSec; }
    int targetPitch(const RefNote& n) const noexcept { return n.pitch + transpose; }
};

/** Sort by start time (then pitch). */
void sortNotes(NoteList& notes);

/** Index of the note sounding at reference time t, or -1. If notes overlap, the one that
    started most recently wins. Notes must be sorted. O(log n) + overlap scan. */
int findActiveNote(const NoteList& notes, double t) noexcept;

/** Index of the first note whose end is after t (for iterating a visible range). Sorted input. */
size_t firstNoteEndingAfter(const NoteList& notes, double t) noexcept;

/** Remove notes shorter than minLength seconds. */
void removeShortNotes(NoteList& notes, double minLength);

/** Merge consecutive same-pitch notes separated by at most maxGap seconds. Sorted input. */
void mergeSamePitchGaps(NoteList& notes, double maxGap);

/** Clamp pitches to the MIDI range and lengths to a minimum. */
void sanitiseNotes(NoteList& notes, double minLength = 0.01);

} // namespace pitchlane
