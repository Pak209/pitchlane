#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace pitchlane::params {

// Parameter IDs (stable: they are stored in Logic projects).
inline constexpr const char* tolerance   = "tolerance";    // cents
inline constexpr const char* calibration = "calibration";  // ms, shifts the live trace in time
inline constexpr const char* refOffset   = "refOffset";    // ms, shifts reference notes in time
inline constexpr const char* transpose   = "transpose";    // semitones applied to reference notes
inline constexpr const char* key         = "key";          // 0 = C ... 11 = B
inline constexpr const char* scale       = "scale";        // pitchlane::ScaleType
inline constexpr const char* lowNote     = "lowNote";      // MIDI note, bottom of the roll
inline constexpr const char* highNote    = "highNote";     // MIDI note, top of the roll
inline constexpr const char* tempo       = "tempo";        // manual BPM (fallback when host has none)
inline constexpr const char* gateDb      = "gateDb";       // detector RMS gate
inline constexpr const char* clarity     = "clarity";      // minimum periodicity 0..1
inline constexpr const char* noteNames   = "noteNames";    // 0 = C4 middle C (scientific), 1 = C3 (Logic)
inline constexpr const char* viewSeconds = "viewSeconds";  // visible time span

juce::AudioProcessorValueTreeState::ParameterLayout createLayout(std::function<int()> octaveConvention);

} // namespace pitchlane::params
