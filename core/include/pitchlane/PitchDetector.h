#pragma once
// Real-time monophonic pitch detector (YIN) intended to run inside processBlock.
//  * All memory is allocated in prepare(); process() never allocates, locks or does I/O.
//  * Input is high-passed, low-passed and decimated to ~22-24 kHz internally, which keeps the
//    per-hop cost small (~0.2 M multiply-adds every ~5 ms) while staying accurate to ~1 cent.
//  * Frames are gated by RMS level and by YIN aperiodicity ("clarity") so silence, breaths
//    and noise produce unvoiced frames.

#include <cstdint>
#include <vector>

#include "pitchlane/Filters.h"

namespace pitchlane {

struct PitchDetectorSettings
{
    double minHz = 65.0;            // ~C2
    double maxHz = 1200.0;          // ~D6
    double windowMs = 21.0;         // YIN integration window
    double hopMs = 5.0;             // analysis interval
    float yinThreshold = 0.15f;     // YIN absolute threshold
    float maxAperiodicity = 0.30f;  // frames above this are unvoiced (low clarity)
    float gateDb = -50.0f;          // RMS gate in dBFS
};

struct PitchFrame
{
    int sampleIndex = 0;     // index within the processed block at which the frame completed
    float hz = 0.f;          // 0 when unvoiced
    float midi = 0.f;        // fractional MIDI note, 0 when unvoiced
    float confidence = 0.f;  // 1 - aperiodicity, clamped to [0, 1]
    float rmsDb = -120.f;    // window RMS in dBFS
    bool voiced = false;
};

class PitchDetector
{
public:
    /** Allocates. Call from prepareToPlay (never from the audio thread). */
    void prepare(double sampleRate, const PitchDetectorSettings& settings = {});

    /** Clears history; keeps allocations. Real-time safe. */
    void reset() noexcept;

    /** Real-time safe. Updates thresholds without reallocating. */
    void setGateDb(float db) noexcept { settings_.gateDb = db; }
    void setMaxAperiodicity(float a) noexcept { settings_.maxAperiodicity = a; }

    /** Analyse n mono samples. Writes up to maxFrames frames to out, returns the count.
        Real-time safe. */
    int process(const float* mono, int n, PitchFrame* out, int maxFrames) noexcept;

    /** Delay (in input samples) between the centre of the analysed window and the most
        recent input sample at the moment a frame is produced. Subtract it from the frame's
        sample position to get the time the frame describes. */
    int latencySamples() const noexcept { return latencySamples_; }

    double hopSeconds() const noexcept { return hopSeconds_; }
    double internalRate() const noexcept { return internalRate_; }
    const PitchDetectorSettings& settings() const noexcept { return settings_; }

    /** Maximum frames that can be produced for a block of n samples (for buffer sizing). */
    int maxFramesForBlock(int n) const noexcept { return n / (hop_ * decimation_) + 2; }

private:
    PitchFrame analyse(int sampleIndex) noexcept;

    PitchDetectorSettings settings_;
    Decimator decimator_;
    double inputRate_ = 44100.0, internalRate_ = 22050.0, hopSeconds_ = 0.005;
    int decimation_ = 1;
    int W_ = 0, maxLag_ = 0, minLag_ = 0, bufLen_ = 0, hop_ = 0;
    int writePos_ = 0, filled_ = 0, hopCounter_ = 0;
    int latencySamples_ = 0;
    std::vector<float> ring_;      // 2 * bufLen_, written twice so any window is contiguous
    std::vector<float> diff_, cmndf_;
};

} // namespace pitchlane
