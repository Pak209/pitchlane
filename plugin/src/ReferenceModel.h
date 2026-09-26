#pragma once
// The reference melody being edited: notes + selection + undo, and the optional source
// audio path. Accessed from the message thread (UI) and from get/setStateInformation (any
// non-audio thread), so it is protected by a lock. The audio thread never touches it.

#include <juce_events/juce_events.h>

#include "pitchlane/ReferenceNotes.h"

namespace pitchlane {

class ReferenceModel : public juce::ChangeBroadcaster
{
public:
    // ---- snapshot access -------------------------------------------------------------
    NoteList getNotes() const;
    std::vector<bool> getSelection() const;
    int size() const;
    uint32_t getRevision() const noexcept { return revision_.load(); }

    // ---- whole-list edits (undoable) ------------------------------------------------
    void setNotes(NoteList notes, bool undoable = true);
    void clear();

    // ---- selection --------------------------------------------------------------------
    void selectOnly(int index);
    void toggleSelected(int index);
    void setSelected(int index, bool selected);
    void selectAll();
    void clearSelection();
    void selectInRange(double refStart, double refEnd, int pitchLo, int pitchHi, bool add);
    int numSelected() const;

    // ---- edits --------------------------------------------------------------------------
    /** Call once before a sequence of live edits (e.g. at mouse-down of a drag) to make the
        whole gesture a single undo step. */
    void beginGesture();
    void deleteSelected();
    void nudgeSelected(int semitones, double seconds);
    /** Apply absolute positions to the given notes (during drags; no undo push). */
    void setNote(int index, const RefNote& n);
    int addNote(const RefNote& n);   // returns new index, selects it (undoable)
    /** Re-sort after a drag, keeping the selection attached to its notes. */
    void sortKeepingSelection();

    bool undo();
    bool redo();
    bool canUndo() const;
    bool canRedo() const;

    // ---- source audio -----------------------------------------------------------------
    void setSourcePath(const juce::String& path);
    juce::String getSourcePath() const;

private:
    struct Snapshot { NoteList notes; std::vector<bool> sel; };
    void pushUndoLocked();
    void changed();

    mutable juce::CriticalSection lock_;
    NoteList notes_;
    std::vector<bool> selected_;
    std::vector<Snapshot> undo_, redo_;
    juce::String sourcePath_;
    std::atomic<uint32_t> revision_ { 0 };
    static constexpr size_t kMaxUndo = 100;
};

} // namespace pitchlane
