#include "pitchlane/PitchDetector.h"

#include <algorithm>
#include <cmath>

#include "pitchlane/NoteMath.h"
#include "pitchlane/Yin.h"

namespace pitchlane {

void PitchDetector::prepare(double sampleRate, const PitchDetectorSettings& s)
{
    settings_ = s;
    inputRate_ = sampleRate > 0 ? sampleRate : 44100.0;
    decimation_ = std::max(1, static_cast<int>(std::floor(inputRate_ / 22050.0 + 1e-6)));
    internalRate_ = inputRate_ / decimation_;

    const double lowPass = std::min(5000.0, 0.4 * internalRate_);
    decimator_.prepare(inputRate_, decimation_, lowPass, 50.0);

    W_ = std::max(64, static_cast<int>(std::lround(settings_.windowMs * 0.001 * internalRate_)));
    maxLag_ = static_cast<int>(std::ceil(internalRate_ / settings_.minHz)) + 2;
    minLag_ = std::max(2, static_cast<int>(std::floor(internalRate_ / settings_.maxHz)));
    bufLen_ = W_ + maxLag_ + 1;
    hop_ = std::max(16, static_cast<int>(std::lround(settings_.hopMs * 0.001 * internalRate_)));
    hopSeconds_ = hop_ / internalRate_;
    latencySamples_ = (bufLen_ / 2) * decimation_;

    ring_.assign(static_cast<size_t>(2 * bufLen_), 0.f);
    diff_.assign(static_cast<size_t>(maxLag_ + 2), 0.f);
    cmndf_.assign(static_cast<size_t>(maxLag_ + 2), 0.f);
    reset();
}

void PitchDetector::reset() noexcept
{
    decimator_.reset();
    std::fill(ring_.begin(), ring_.end(), 0.f);
    writePos_ = filled_ = hopCounter_ = 0;
}

int PitchDetector::process(const float* mono, int n, PitchFrame* out, int maxFrames) noexcept
{
    if (ring_.empty()) return 0;
    int produced = 0;
    for (int i = 0; i < n; ++i)
    {
        float y;
        if (!decimator_.push(mono[i], y)) continue;

        ring_[static_cast<size_t>(writePos_)] = y;
        ring_[static_cast<size_t>(writePos_ + bufLen_)] = y;
        if (++writePos_ >= bufLen_) writePos_ = 0;
        if (filled_ < bufLen_) ++filled_;

        if (++hopCounter_ >= hop_)
        {
            hopCounter_ = 0;
            if (filled_ >= bufLen_ && produced < maxFrames)
                out[produced++] = analyse(i);
        }
    }
    return produced;
}

PitchFrame PitchDetector::analyse(int sampleIndex) noexcept
{
    PitchFrame f;
    f.sampleIndex = sampleIndex;

    // Oldest sample is at writePos_; window is contiguous thanks to the double write.
    const float* x = ring_.data() + writePos_;

    const float ms = yin::meanSquare(x + maxLag_ / 2, W_);
    f.rmsDb = ms > 1e-12f ? 10.f * std::log10(ms) : -120.f;
    if (f.rmsDb < settings_.gateDb) return f;

    yin::difference(x, W_, maxLag_, diff_.data());
    yin::cumulativeMeanNormalised(diff_.data(), maxLag_, cmndf_.data());
    const auto est = yin::pickPeriod(cmndf_.data(), diff_.data(), minLag_, maxLag_, settings_.yinThreshold);

    f.confidence = std::clamp(1.f - est.aperiodicity, 0.f, 1.f);
    if (est.period <= 0.0 || est.aperiodicity > settings_.maxAperiodicity) return f;

    const double hz = internalRate_ / est.period;
    if (hz < settings_.minHz || hz > settings_.maxHz) return f;

    f.hz = static_cast<float>(hz);
    f.midi = static_cast<float>(hzToMidi(hz));
    f.voiced = true;
    return f;
}

} // namespace pitchlane
