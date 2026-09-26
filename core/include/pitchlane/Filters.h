#pragma once
// Tiny allocation-free filters used before pitch analysis.

#include <cmath>

namespace pitchlane {

/** Transposed direct form II biquad. */
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void reset() noexcept { z1 = z2 = 0; }

    /** RBJ cookbook low-pass. */
    void setLowPass(double sampleRate, double cutoffHz, double q) noexcept
    {
        const double w0 = 2.0 * 3.14159265358979323846 * cutoffHz / sampleRate;
        const double cw = std::cos(w0), sw = std::sin(w0);
        const double alpha = sw / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = (1.0 - cw) * 0.5 / a0;
        b1 = (1.0 - cw) / a0;
        b2 = b0;
        a1 = -2.0 * cw / a0;
        a2 = (1.0 - alpha) / a0;
    }

    /** RBJ cookbook high-pass. */
    void setHighPass(double sampleRate, double cutoffHz, double q) noexcept
    {
        const double w0 = 2.0 * 3.14159265358979323846 * cutoffHz / sampleRate;
        const double cw = std::cos(w0), sw = std::sin(w0);
        const double alpha = sw / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = (1.0 + cw) * 0.5 / a0;
        b1 = -(1.0 + cw) / a0;
        b2 = b0;
        a1 = -2.0 * cw / a0;
        a2 = (1.0 - alpha) / a0;
    }

    inline double process(double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

/** High-pass (rumble/DC) + 4th-order Butterworth low-pass + integer decimation. */
class Decimator
{
public:
    void prepare(double inputRate, int factor, double lowPassHz, double highPassHz) noexcept
    {
        factor_ = factor < 1 ? 1 : factor;
        hp_.setHighPass(inputRate, highPassHz, 0.7071);
        lp1_.setLowPass(inputRate, lowPassHz, 0.54119610);
        lp2_.setLowPass(inputRate, lowPassHz, 1.30656296);
        reset();
    }

    void reset() noexcept
    {
        hp_.reset(); lp1_.reset(); lp2_.reset();
        phase_ = 0;
    }

    int factor() const noexcept { return factor_; }

    /** Feed one input sample; returns true and writes out when a decimated sample is ready. */
    inline bool push(float x, float& out) noexcept
    {
        double y = hp_.process(x);
        y = lp2_.process(lp1_.process(y));
        if (++phase_ >= factor_)
        {
            phase_ = 0;
            out = static_cast<float>(y);
            return true;
        }
        return false;
    }

private:
    Biquad hp_, lp1_, lp2_;
    int factor_ = 1;
    int phase_ = 0;
};

} // namespace pitchlane
