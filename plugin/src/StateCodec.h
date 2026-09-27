#pragma once
// Plugin state <-> binary blob (saved inside the Logic project).
// The reference notes themselves are embedded, so the project never depends on the
// source audio file still existing.

#include <juce_audio_processors/juce_audio_processors.h>

#include "Markers.h"
#include "ReferenceModel.h"

namespace pitchlane::state {

inline constexpr int kVersion = 2; // 2: + section markers

juce::ValueTree notesToTree(const NoteList& notes);
NoteList notesFromTree(const juce::ValueTree& tree);

juce::ValueTree makeStateTree(juce::AudioProcessorValueTreeState& apvts, const ReferenceModel& model,
                              const MarkerModel* markers = nullptr);
bool applyStateTree(const juce::ValueTree& tree, juce::AudioProcessorValueTreeState& apvts, ReferenceModel& model,
                    MarkerModel* markers = nullptr);

void writeBinary(juce::AudioProcessorValueTreeState& apvts, const ReferenceModel& model, juce::MemoryBlock& dest,
                 const MarkerModel* markers = nullptr);
bool readBinary(const void* data, int size, juce::AudioProcessorValueTreeState& apvts, ReferenceModel& model,
                MarkerModel* markers = nullptr);

} // namespace pitchlane::state
