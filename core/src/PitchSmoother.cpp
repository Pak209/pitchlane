#include "pitchlane/PitchSmoother.h"

#include <algorithm>
#include <cmath>

namespace pitchlane {

SmoothingSettings smoothingForAmount(double a) noexcept
{
    a = std::clamp(a, 0.0, 1.0);
    SmoothingSettings s;
    s.medianLength = a < 0.05 ? 1 : a < 0.35 ? 3 : a < 0.85 ? 5 : 7;
    s.timeConstantMs = 25.0 * std::pow(a, 1.8); // 0.6 -> ~10 ms
    return s;
}

void PitchSmoother::configure(double hopSeconds, int medianLength, double timeConstantMs, double jumpCents) noexcept
{
    medianLen_ = std::clamp(medianLength | 1, 1, kMaxMedian);
    alpha_ = timeConstantMs <= 0.0 ? 1.0 : 1.0 - std::exp(-hopSeconds / (timeConstantMs * 0.001));
    jump_ = jumpCents / 100.0;
    reset();
}

void PitchSmoother::reset() noexcept
{
    count_ = head_ = 0;
    hasY_ = false;
    y_ = 0.0;
}

float PitchSmoother::process(bool voiced, float midi) noexcept
{
    if (!voiced || midi <= 0.f)
    {
        reset();
        return 0.f;
    }

    hist_[static_cast<size_t>(head_)] = midi;
    head_ = (head_ + 1) % medianLen_;
    if (count_ < medianLen_) ++count_;

    std::array<float, kMaxMedian> tmp {};
    // Most recent `count_` values live in the circular buffer.
    for (int i = 0; i < count_; ++i)
        tmp[static_cast<size_t>(i)] = hist_[static_cast<size_t>((head_ - 1 - i + medianLen_) % medianLen_)];
    std::sort(tmp.begin(), tmp.begin() + count_);
    const double med = (count_ % 2 == 1) ? tmp[static_cast<size_t>(count_ / 2)]
                                         : 0.5 * (tmp[static_cast<size_t>(count_ / 2 - 1)] + tmp[static_cast<size_t>(count_ / 2)]);

    if (!hasY_ || std::abs(med - y_) > jump_)
    {
        y_ = med;
        hasY_ = true;
    }
    else
    {
        y_ += alpha_ * (med - y_);
    }
    return static_cast<float>(y_);
}

} // namespace pitchlane
