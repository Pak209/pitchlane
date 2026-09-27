#include "pitchlane/Yin.h"

#include <cmath>

namespace pitchlane::yin {

void difference(const float* x, int W, int maxLag, float* d) noexcept
{
    d[0] = 0.f;
    for (int tau = 1; tau <= maxLag; ++tau)
    {
        const float* y = x + tau;
        float acc = 0.f;
        // Simple loop; compilers vectorise this well (-O2/-O3).
        for (int j = 0; j < W; ++j)
        {
            const float diff = x[j] - y[j];
            acc += diff * diff;
        }
        d[tau] = acc;
    }
}

void cumulativeMeanNormalised(const float* d, int maxLag, float* out) noexcept
{
    double running = 0.0;
    out[0] = 1.f;
    for (int tau = 1; tau <= maxLag; ++tau)
    {
        running += d[tau];
        out[tau] = running > 0.0 ? static_cast<float>(d[tau] * tau / running) : 1.f;
    }
}

double parabolicMin(const float* v, int n, int i) noexcept
{
    if (i <= 0 || i >= n - 1) return static_cast<double>(i);
    const double a = v[i - 1], b = v[i], c = v[i + 1];
    const double denom = a - 2.0 * b + c;
    if (std::abs(denom) < 1e-12) return static_cast<double>(i);
    double shift = 0.5 * (a - c) / denom;
    if (shift > 1.0) shift = 1.0;
    if (shift < -1.0) shift = -1.0;
    return i + shift;
}

PeriodEstimate pickPeriod(const float* cmndf, const float* d, int minLag, int maxLag, float threshold) noexcept
{
    PeriodEstimate est;
    if (minLag < 2) minLag = 2;
    if (maxLag <= minLag + 1) return est;

    int tau = -1;
    for (int t = minLag; t < maxLag; ++t)
    {
        if (cmndf[t] < threshold)
        {
            while (t + 1 < maxLag && cmndf[t + 1] < cmndf[t]) ++t;
            tau = t;
            est.belowThreshold = true;
            break;
        }
    }
    if (tau < 0)
    {
        float best = 1e9f;
        for (int t = minLag; t < maxLag; ++t)
            if (cmndf[t] < best) { best = cmndf[t]; tau = t; }
    }
    if (tau < 0) return est;

    est.aperiodicity = cmndf[tau];
    // Refine on the raw difference function (less biased than the normalised one).
    est.period = parabolicMin(d, maxLag + 1, tau);
    return est;
}

float meanSquare(const float* x, int n) noexcept
{
    double acc = 0.0;
    for (int i = 0; i < n; ++i) acc += static_cast<double>(x[i]) * x[i];
    return n > 0 ? static_cast<float>(acc / n) : 0.f;
}

} // namespace pitchlane::yin
