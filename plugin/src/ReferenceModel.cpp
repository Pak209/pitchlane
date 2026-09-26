#include "ReferenceModel.h"

#include <algorithm>
#include <numeric>

namespace pitchlane {

NoteList ReferenceModel::getNotes() const
{
    const juce::ScopedLock sl(lock_);
    return notes_;
}

std::vector<bool> ReferenceModel::getSelection() const
{
    const juce::ScopedLock sl(lock_);
    return selected_;
}

int ReferenceModel::size() const
{
    const juce::ScopedLock sl(lock_);
    return static_cast<int>(notes_.size());
}

void ReferenceModel::changed()
{
    ++revision_;
    sendChangeMessage();
}

void ReferenceModel::pushUndoLocked()
{
    undo_.push_back({ notes_, selected_ });
    if (undo_.size() > kMaxUndo) undo_.erase(undo_.begin());
    redo_.clear();
}

void ReferenceModel::setNotes(NoteList notes, bool undoable)
{
    {
        const juce::ScopedLock sl(lock_);
        if (undoable) pushUndoLocked();
        sortNotes(notes);
        notes_ = std::move(notes);
        selected_.assign(notes_.size(), false);
    }
    changed();
}

void ReferenceModel::clear() { setNotes({}, true); }

void ReferenceModel::selectOnly(int index)
{
    {
        const juce::ScopedLock sl(lock_);
        std::fill(selected_.begin(), selected_.end(), false);
        if (index >= 0 && index < static_cast<int>(selected_.size())) selected_[static_cast<size_t>(index)] = true;
    }
    changed();
}

void ReferenceModel::toggleSelected(int index)
{
    {
        const juce::ScopedLock sl(lock_);
        if (index >= 0 && index < static_cast<int>(selected_.size()))
            selected_[static_cast<size_t>(index)] = !selected_[static_cast<size_t>(index)];
    }
    changed();
}

void ReferenceModel::setSelected(int index, bool s)
{
    {
        const juce::ScopedLock sl(lock_);
        if (index >= 0 && index < static_cast<int>(selected_.size())) selected_[static_cast<size_t>(index)] = s;
    }
    changed();
}

void ReferenceModel::selectAll()
{
    {
        const juce::ScopedLock sl(lock_);
        std::fill(selected_.begin(), selected_.end(), true);
    }
    changed();
}

void ReferenceModel::clearSelection()
{
    {
        const juce::ScopedLock sl(lock_);
        std::fill(selected_.begin(), selected_.end(), false);
    }
    changed();
}

void ReferenceModel::selectInRange(double a, double b, int lo, int hi, bool add)
{
    {
        const juce::ScopedLock sl(lock_);
        for (size_t i = 0; i < notes_.size(); ++i)
        {
            const auto& n = notes_[i];
            const bool hit = n.end() > a && n.start < b && n.pitch >= lo && n.pitch <= hi;
            selected_[i] = add ? (selected_[i] || hit) : hit;
        }
    }
    changed();
}

int ReferenceModel::numSelected() const
{
    const juce::ScopedLock sl(lock_);
    return static_cast<int>(std::count(selected_.begin(), selected_.end(), true));
}

void ReferenceModel::beginGesture()
{
    const juce::ScopedLock sl(lock_);
    pushUndoLocked();
}

void ReferenceModel::deleteSelected()
{
    {
        const juce::ScopedLock sl(lock_);
        if (std::find(selected_.begin(), selected_.end(), true) == selected_.end()) return;
        pushUndoLocked();
        NoteList kept;
        for (size_t i = 0; i < notes_.size(); ++i)
            if (!selected_[i]) kept.push_back(notes_[i]);
        notes_.swap(kept);
        selected_.assign(notes_.size(), false);
    }
    changed();
}

void ReferenceModel::setSelectedMuted(bool muted)
{
    {
        const juce::ScopedLock sl(lock_);
        bool any = false;
        for (size_t i = 0; i < notes_.size(); ++i) any = any || (selected_[i] && notes_[i].muted() != muted);
        if (!any) return;
        pushUndoLocked();
        for (size_t i = 0; i < notes_.size(); ++i)
            if (selected_[i]) notes_[i].setFlag(RefNote::Muted, muted);
    }
    changed();
}

void ReferenceModel::toggleSelectedMuted()
{
    bool allMuted = true, anySel = false;
    {
        const juce::ScopedLock sl(lock_);
        for (size_t i = 0; i < notes_.size(); ++i)
            if (selected_[i]) { anySel = true; allMuted = allMuted && notes_[i].muted(); }
    }
    if (anySel) setSelectedMuted(!allMuted);
}

void ReferenceModel::setHarmoniesMuted(bool muted)
{
    {
        const juce::ScopedLock sl(lock_);
        bool any = false;
        for (const auto& n : notes_) any = any || (n.harmonySuspect() && n.muted() != muted);
        if (!any) return;
        pushUndoLocked();
        for (auto& n : notes_)
            if (n.harmonySuspect()) n.setFlag(RefNote::Muted, muted);
    }
    changed();
}

int ReferenceModel::numHarmonySuspects() const
{
    const juce::ScopedLock sl(lock_);
    return static_cast<int>(std::count_if(notes_.begin(), notes_.end(), [](const RefNote& n) { return n.harmonySuspect(); }));
}

int ReferenceModel::numMuted() const
{
    const juce::ScopedLock sl(lock_);
    return static_cast<int>(std::count_if(notes_.begin(), notes_.end(), [](const RefNote& n) { return n.muted(); }));
}

bool ReferenceModel::harmoniesMuted() const
{
    const juce::ScopedLock sl(lock_);
    bool any = false;
    for (const auto& n : notes_)
        if (n.harmonySuspect()) { any = true; if (!n.muted()) return false; }
    return any;
}

void ReferenceModel::removeMuted()
{
    {
        const juce::ScopedLock sl(lock_);
        if (std::none_of(notes_.begin(), notes_.end(), [](const RefNote& n) { return n.muted(); })) return;
        pushUndoLocked();
        NoteList kept;
        std::vector<bool> sel;
        for (size_t i = 0; i < notes_.size(); ++i)
            if (!notes_[i].muted()) { kept.push_back(notes_[i]); sel.push_back(selected_[i]); }
        notes_.swap(kept);
        selected_.swap(sel);
    }
    changed();
}

void ReferenceModel::nudgeSelected(int semis, double secs)
{
    {
        const juce::ScopedLock sl(lock_);
        if (std::find(selected_.begin(), selected_.end(), true) == selected_.end()) return;
        pushUndoLocked();
        for (size_t i = 0; i < notes_.size(); ++i)
            if (selected_[i])
            {
                notes_[i].pitch = juce::jlimit(0, 127, notes_[i].pitch + semis);
                notes_[i].start = std::max(0.0, notes_[i].start + secs);
            }
    }
    sortKeepingSelection();
}

void ReferenceModel::setNote(int index, const RefNote& n)
{
    {
        const juce::ScopedLock sl(lock_);
        if (index < 0 || index >= static_cast<int>(notes_.size())) return;
        notes_[static_cast<size_t>(index)] = n;
    }
    changed();
}

int ReferenceModel::addNote(const RefNote& n)
{
    int idx = -1;
    {
        const juce::ScopedLock sl(lock_);
        pushUndoLocked();
        notes_.push_back(n);
        selected_.assign(notes_.size(), false);
        selected_.back() = true;
    }
    sortKeepingSelection();
    const juce::ScopedLock sl(lock_);
    for (size_t i = 0; i < selected_.size(); ++i)
        if (selected_[i]) idx = static_cast<int>(i);
    return idx;
}

void ReferenceModel::sortKeepingSelection()
{
    {
        const juce::ScopedLock sl(lock_);
        std::vector<size_t> order(notes_.size());
        std::iota(order.begin(), order.end(), size_t(0));
        std::stable_sort(order.begin(), order.end(), [this](size_t a, size_t b) {
            if (notes_[a].start < notes_[b].start) return true;
            if (notes_[b].start < notes_[a].start) return false;
            return notes_[a].pitch < notes_[b].pitch;
        });
        NoteList n2;
        std::vector<bool> s2;
        for (auto i : order) { n2.push_back(notes_[i]); s2.push_back(selected_[i]); }
        notes_.swap(n2);
        selected_.swap(s2);
    }
    changed();
}

bool ReferenceModel::undo()
{
    {
        const juce::ScopedLock sl(lock_);
        if (undo_.empty()) return false;
        redo_.push_back({ notes_, selected_ });
        notes_ = std::move(undo_.back().notes);
        selected_ = std::move(undo_.back().sel);
        undo_.pop_back();
    }
    changed();
    return true;
}

bool ReferenceModel::redo()
{
    {
        const juce::ScopedLock sl(lock_);
        if (redo_.empty()) return false;
        undo_.push_back({ notes_, selected_ });
        notes_ = std::move(redo_.back().notes);
        selected_ = std::move(redo_.back().sel);
        redo_.pop_back();
    }
    changed();
    return true;
}

bool ReferenceModel::canUndo() const
{
    const juce::ScopedLock sl(lock_);
    return !undo_.empty();
}

bool ReferenceModel::canRedo() const
{
    const juce::ScopedLock sl(lock_);
    return !redo_.empty();
}

void ReferenceModel::setSourcePath(const juce::String& path)
{
    {
        const juce::ScopedLock sl(lock_);
        sourcePath_ = path;
    }
    changed();
}

juce::String ReferenceModel::getSourcePath() const
{
    const juce::ScopedLock sl(lock_);
    return sourcePath_;
}

} // namespace pitchlane
