#pragma once
// Plugin state <-> binary blob (saved inside the Logic project).
// The reference notes themselves are embedded, so the project never depends on the
// source audio file still existing.

#include <juce_audio_processors/juce_audio_processors.h>

#include "ReferenceModel.h"

namespace pitchlane::state {

inline constexpr int kVersion = 1;

juce::ValueTree notesToTree(const NoteList& notes);
NoteList notesFromTree(const juce::ValueTree& tree);

juce::ValueTree makeStateTree(juce::AudioProcessorValueTreeState& apvts, const ReferenceModel& model);
bool applyStateTree(const juce::ValueTree& tree, juce::AudioProcessorValueTreeState& apvts, ReferenceModel& model);

void writeBinary(juce::AudioProcessorValueTreeState& apvts, const ReferenceModel& model, juce::MemoryBlock& dest);
bool readBinary(const void* data, int size, juce::AudioProcessorValueTreeState& apvts, ReferenceModel& model);

} // namespace pitchlane::state
