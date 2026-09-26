#pragma once
// Synthetic test signals.

#include <cmath>
#include <cstdint>
#include <functional>
#include <random>
#include <vector>

namespace testsig {

constexpr double kTwoPi = 6.283185307179586;

/** Harmonic tone with an arbitrary instantaneous-pitch function (fractional MIDI vs time).
    numHarmonics = 1 gives a pure sine. Harmonic k has amplitude 1/k (bright, voice-like). */
inline std::vector<float> tone(double sr, double seconds, const std::function<double(double)>& midiAt,
                               int numHarmonics = 1, double amp = 0.5)
{
    std::vector<float> out(static_cast<size_t>(seconds * sr));
    double phase = 0.0;
    double norm = 0.0;
    for (int k = 1; k <= numHarmonics; ++k) norm += 1.0 / k;
    for (size_t i = 0; i < out.size(); ++i)
    {
        const double t = i / sr;
        const double hz = 440.0 * std::pow(2.0, (midiAt(t) - 69.0) / 12.0);
        phase += kTwoPi * hz / sr;
        if (phase > kTwoPi * 1000.0) phase = std::fmod(phase, kTwoPi);
        double v = 0.0;
        for (int k = 1; k <= numHarmonics; ++k)
            if (hz * k < sr * 0.45) v += std::sin(k * phase) / k;
        out[i] = static_cast<float>(amp * v / norm);
    }
    return out;
}

inline std::vector<float> steady(double sr, double seconds, double midi, int harmonics = 1, double amp = 0.5)
{
    return tone(sr, seconds, [midi](double) { return midi; }, harmonics, amp);
}

inline std::vector<float> whiteNoise(double sr, double seconds, double amp, uint32_t seed = 1234)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> d(-1.f, 1.f);
    std::vector<float> out(static_cast<size_t>(seconds * sr));
    for (auto& v : out) v = static_cast<float>(amp) * d(rng);
    return out;
}

struct MelodyNote
{
    double start, length;
    int midi;
    double vibratoCents = 0.0, vibratoHz = 5.5;
};

/** Render a melody of harmonic tones with 15 ms attack/release (optional vibrato). */
inline std::vector<float> melody(double sr, double totalSeconds, const std::vector<MelodyNote>& notes,
                                 int harmonics = 6, double amp = 0.4)
{
    std::vector<float> out(static_cast<size_t>(totalSeconds * sr), 0.f);
    for (const auto& n : notes)
    {
        double phase = 0.0;
        const size_t a = static_cast<size_t>(n.start * sr);
        const size_t len = static_cast<size_t>(n.length * sr);
        const double ramp = 0.015 * sr;
        double norm = 0.0;
        for (int k = 1; k <= harmonics; ++k) norm += 1.0 / k;
        for (size_t i = 0; i < len && a + i < out.size(); ++i)
        {
            const double t = i / sr;
            const double midi = n.midi + (n.vibratoCents / 100.0) * std::sin(kTwoPi * n.vibratoHz * t);
            const double hz = 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
            phase += kTwoPi * hz / sr;
            double env = 1.0;
            if (i < ramp) env = i / ramp;
            if (len - i < ramp) env = std::min(env, (len - i) / ramp);
            double v = 0.0;
            for (int k = 1; k <= harmonics; ++k)
                if (hz * k < sr * 0.45) v += std::sin(k * phase) / k;
            out[a + i] += static_cast<float>(amp * env * v / norm);
        }
    }
    return out;
}

} // namespace testsig
