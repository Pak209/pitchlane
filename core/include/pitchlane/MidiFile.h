#pragma once
// Minimal, dependency-free Standard MIDI File reader/writer (format 0 and 1).
// Handles running status, meta/sysex events, tempo maps, note-on velocity 0 as note-off,
// and SMPTE time division. Used for "Import MIDI" / "Export MIDI" of reference melodies.
// Pure computation: callers do the file I/O (off the audio thread).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "pitchlane/ReferenceNotes.h"

namespace pitchlane {

struct MidiNoteEvent
{
    double start = 0.0;   // seconds
    double length = 0.0;  // seconds
    int pitch = 60;
    int velocity = 100;
    int channel = 0;      // 0-15
    int track = 0;
};

struct MidiTrackInfo
{
    std::string name;
    int noteCount = 0;
};

struct MidiParseResult
{
    bool ok = false;
    std::string error;
    int format = 0;
    int ticksPerQuarter = 480;   // 0 when SMPTE timing is used
    double initialBpm = 120.0;
    std::vector<MidiTrackInfo> tracks;
    std::vector<MidiNoteEvent> notes;  // all tracks, sorted by start time
};

MidiParseResult parseMidiFile(const uint8_t* data, size_t size);

/** Choose the notes for a reference melody: the given track, or (trackIndex < 0) the
    track with the most notes, skipping General-MIDI drum channel 10. */
NoteList referenceNotesFromMidi(const MidiParseResult& midi, int trackIndex = -1);

/** Write notes as a format-1 SMF (tempo track + one note track) at a constant tempo.
    Muted notes are left out unless includeMuted. */
std::vector<uint8_t> writeMidiFile(const NoteList& notes, double bpm, int ticksPerQuarter = 480,
                                   const std::string& trackName = "Pitch Lane Reference", bool includeMuted = false);

} // namespace pitchlane
