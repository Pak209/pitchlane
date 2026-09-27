#pragma once
// Display smoothing for the live pitch line.
//  1. short running median (default 5 frames, ~25 ms at 5 ms hops) removes single-frame
//     outliers (octave blips) without rounding off vibrato;
//  2. a light one-pole low-pass in the cents domain (default tau = 10 ms, i.e. ~16 Hz
//     corner, so 5-7 Hz vibrato keeps >90% of its depth);
//  3. large jumps (> jumpCents, e.g. a new note) snap immediately instead of gliding, so
//     note changes and fast slides are not smeared;
//  4. unvoiced frames reset the state so a line never bridges across silence.
// Allocation-free after construction; real-time safe.

#include <array>

namespace pitchlane {

/** Median length / time constant for a user-facing "smoothing amount" in [0, 1].
    0.6 (the default, "60 %") gives the original tuning: median 5, tau 10 ms.
    0 = raw detector output (no median, no low-pass); 1 = median 7, tau 25 ms. */
struct SmoothingSettings
{
    int medianLength = 5;
    double timeConstantMs = 10.0;
};
SmoothingSettings smoothingForAmount(double amount01) noexcept;

class PitchSmoother
{
public:
    static constexpr int kMaxMedian = 9;

    void configure(double hopSeconds, int medianLength = 5, double timeConstantMs = 10.0,
                   double jumpCents = 80.0) noexcept;

    void reset() noexcept;

    /** Feed one frame. Returns the smoothed MIDI pitch, or 0 if unvoiced / not yet stable. */
    float process(bool voiced, float midi) noexcept;

private:
    std::array<float, kMaxMedian> hist_ {};
    int medianLen_ = 5, count_ = 0, head_ = 0;
    double alpha_ = 0.4, jump_ = 0.8;
    double y_ = 0.0;
    bool hasY_ = false;
};

} // namespace pitchlane
