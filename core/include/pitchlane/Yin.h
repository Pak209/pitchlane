#pragma once
// Low-level YIN building blocks (de Cheveigne & Kawahara 2002), shared by the
// real-time detector and the offline analyzer. No allocation; callers own buffers.

#include <cstddef>

namespace pitchlane::yin {

/** Difference function d[tau] = sum_{j<W} (x[j] - x[j+tau])^2 for tau in [0, maxLag].
    x must hold at least W + maxLag samples; d must hold maxLag + 1 values. */
void difference(const float* x, int W, int maxLag, float* d) noexcept;

/** Cumulative-mean-normalised difference d'[tau]; d'[0] = 1. out may alias d. */
void cumulativeMeanNormalised(const float* d, int maxLag, float* out) noexcept;

/** Parabolic interpolation of the minimum around integer index i (1 <= i < n-1). */
double parabolicMin(const float* v, int n, int i) noexcept;

struct PeriodEstimate
{
    double period = 0.0;      // in samples (sub-sample accurate); 0 when not found
    float aperiodicity = 1.f; // d'(tau*) in [0, ~1+]; 0 = perfectly periodic
    bool belowThreshold = false;
};

/** YIN absolute-threshold step: first dip of cmndf below threshold in [minLag, maxLag),
    followed down to its local minimum; falls back to the global minimum. The period is
    refined by parabolic interpolation on the raw difference function d. */
PeriodEstimate pickPeriod(const float* cmndf, const float* d, int minLag, int maxLag, float threshold) noexcept;

/** Mean square of n samples. */
float meanSquare(const float* x, int n) noexcept;

} // namespace pitchlane::yin
