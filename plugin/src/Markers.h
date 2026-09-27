#pragma once
// User-editable section markers ("A Verse 1", "B Pre-Chorus", ...). An Audio Unit cannot read
// Logic's arrangement markers, so these are created, renamed, moved and deleted by the user
// and saved in the plugin state. Times are song seconds (the host timeline), independent of
// the reference offset. Message thread + get/setStateInformation only (never the audio
// thread), guarded by a lock like ReferenceModel.

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

#include <vector>

namespace pitchlane {

struct SectionMarker
{
    double time = 0.0;     // song seconds
    juce::String name;
};

class MarkerModel : public juce::ChangeBroadcaster
{
public:
    std::vector<SectionMarker> getMarkers() const;
    int size() const;

    /** Adds a marker (kept sorted by time) and returns its index. */
    int add(double time, const juce::String& name);
    bool rename(int index, const juce::String& name);
    bool remove(int index);
    /** Moves a marker; returns its new index after re-sorting (or -1). */
    int move(int index, double time);
    void setMarkers(std::vector<SectionMarker> markers);
    void clear();

    /** Index of the marker whose section contains `time` (last marker at or before it), or -1. */
    int indexAt(double time) const;

    /** Section letter for a marker index: A..Z, then AA, AB, ... */
    static juce::String letterFor(int index);
    /** A default name for a new marker, e.g. "Section C". */
    juce::String defaultNameFor(int index) const { return "Section " + letterFor(index); }

    juce::ValueTree toTree() const;
    /** Replaces the markers with those in `tree` (a "Markers" tree); ignores invalid entries. */
    void fromTree(const juce::ValueTree& tree);
    static const juce::Identifier treeType;

private:
    static void sortMarkers(std::vector<SectionMarker>& m);
    mutable juce::CriticalSection lock_;
    std::vector<SectionMarker> markers_;
};

} // namespace pitchlane
