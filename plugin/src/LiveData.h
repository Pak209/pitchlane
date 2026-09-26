#pragma once
// Data passed lock-free from the audio thread to the UI.

#include <cstdint>

namespace pitchlane {

struct LiveFrame
{
    enum Flags : uint32_t
    {
        Voiced    = 1u << 0,
        Playing   = 1u << 1,   // transport running: frame belongs on the timeline
        Recording = 1u << 2,
        Jump      = 1u << 3,   // marker: discontinuity; UI drops trace at/after songTime
    };

    double songTime = 0.0;   // seconds (host timeline or free clock), latency-compensated
    float midi = 0.f;        // smoothed pitch for display (0 = none)
    float rawMidi = 0.f;     // unsmoothed detector output (0 = unvoiced)
    float hz = 0.f;
    float confidence = 0.f;
    float rmsDb = -120.f;
    uint32_t flags = 0;
};

struct TransportSnapshot
{
    double songTime = 0.0;   // at the start of the last processed block
    double wallMs = 0.0;     // juce::Time::getMillisecondCounterHiRes() at that block
    double bpm = 120.0;
    double loopStart = 0.0, loopEnd = 0.0;
    int32_t source = 2;      // pitchlane::TimeSource
    int32_t timeSigNum = 4, timeSigDen = 4;   // host time signature (4/4 when unknown)
    uint8_t playing = 0, recording = 0, looping = 0, hasLoop = 0;
    uint32_t blockCounter = 0;
};

} // namespace pitchlane
