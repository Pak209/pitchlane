#include <algorithm>
#include <vector>

#include "TestFramework.h"
#include "TestSignals.h"
#include "pitchlane/NoteMath.h"
#include "pitchlane/PitchDetector.h"

using namespace pitchlane;

namespace {

struct TimedFrame
{
    double time;   // seconds, compensated for detector latency
    PitchFrame f;
};

std::vector<TimedFrame> runDetector(const std::vector<float>& sig, double sr, int blockSize = 256,
                                    PitchDetectorSettings settings = {})
{
    PitchDetector det;
    det.prepare(sr, settings);
    std::vector<PitchFrame> buf(static_cast<size_t>(det.maxFramesForBlock(blockSize)));
    std::vector<TimedFrame> out;
    for (size_t pos = 0; pos < sig.size(); pos += static_cast<size_t>(blockSize))
    {
        const int n = static_cast<int>(std::min<size_t>(static_cast<size_t>(blockSize), sig.size() - pos));
        const int got = det.process(sig.data() + pos, n, buf.data(), static_cast<int>(buf.size()));
        for (int i = 0; i < got; ++i)
            out.push_back({ (static_cast<double>(pos) + buf[static_cast<size_t>(i)].sampleIndex - det.latencySamples()) / sr,
                            buf[static_cast<size_t>(i)] });
    }
    return out;
}

void checkSteadyNote(double sr, int midi, int harmonics)
{
    const auto sig = testsig::steady(sr, 0.6, midi, harmonics);
    const auto frames = runDetector(sig, sr);
    int voiced = 0, within = 0, considered = 0;
    double worst = 0.0;
    for (const auto& tf : frames)
    {
        if (tf.time < 0.05 || tf.time > 0.55) continue;
        ++considered;
        if (!tf.f.voiced) continue;
        ++voiced;
        const double c = centsFromNote(tf.f.midi, midi);
        worst = std::max(worst, std::abs(c));
        if (std::abs(c) <= 5.0) ++within;
    }
    INFO(noteName(midi) << " @" << sr << " Hz, harmonics=" << harmonics << ": voiced " << voiced << "/" << considered
                        << ", worst error " << worst << " cents");
    CHECK(considered > 50);
    CHECK_EQ(voiced, considered);
    CHECK_EQ(within, voiced);
}

} // namespace

TEST_CASE("PitchDetector: sines at known notes within +/-5 cents")
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int midi : { 43 /*G2*/, 57 /*A3*/, 64 /*E4*/, 69 /*A4*/, 72 /*C5*/, 81 /*A5*/ })
            checkSteadyNote(sr, midi, 1);
}

TEST_CASE("PitchDetector: harmonic-rich tones within +/-5 cents")
{
    for (double sr : { 44100.0, 48000.0 })
        for (int midi : { 45 /*A2*/, 57, 64, 69, 72, 76 /*E5*/ })
            checkSteadyNote(sr, midi, 8);
}

TEST_CASE("PitchDetector: A4 reads 440 Hz")
{
    const auto frames = runDetector(testsig::steady(48000.0, 0.5, 69, 4), 48000.0);
    std::vector<float> hz;
    for (auto& tf : frames) if (tf.f.voiced && tf.time > 0.05) hz.push_back(tf.f.hz);
    CHECK(!hz.empty());
    std::sort(hz.begin(), hz.end());
    CHECK_NEAR(hz[hz.size() / 2], 440.0, 0.5);
}

TEST_CASE("PitchDetector: silence and noise are gated")
{
    const double sr = 48000.0;
    std::vector<float> silence(static_cast<size_t>(sr), 0.f);
    int voiced = 0;
    for (auto& tf : runDetector(silence, sr)) voiced += tf.f.voiced;
    CHECK_EQ(voiced, 0);

    // Loud white noise (-20 dBFS-ish): above the RMS gate but aperiodic -> unvoiced.
    const auto noise = testsig::whiteNoise(sr, 1.0, 0.17);
    const auto nf = runDetector(noise, sr);
    voiced = 0;
    for (auto& tf : nf) voiced += tf.f.voiced;
    INFO("noise voiced frames: " << voiced << "/" << nf.size());
    CHECK(voiced <= static_cast<int>(nf.size() / 50));

    // A very quiet tone (-70 dBFS) is below the -50 dB gate.
    const auto quiet = testsig::steady(sr, 0.5, 69, 1, 0.0004);
    voiced = 0;
    for (auto& tf : runDetector(quiet, sr)) voiced += tf.f.voiced;
    CHECK_EQ(voiced, 0);

    // Breath-like band noise at moderate level also stays unvoiced.
    auto breath = testsig::whiteNoise(sr, 0.5, 0.05, 99);
    float prev = 0.f;
    for (auto& v : breath) { prev = 0.7f * prev + 0.3f * v; v = prev; }
    voiced = 0;
    const auto bf = runDetector(breath, sr);
    for (auto& tf : bf) voiced += tf.f.voiced;
    INFO("breath voiced frames: " << voiced << "/" << bf.size());
    CHECK(voiced <= static_cast<int>(bf.size() / 50));
}

TEST_CASE("PitchDetector: tone in light noise still detected")
{
    const double sr = 44100.0;
    auto sig = testsig::steady(sr, 0.6, 64, 6, 0.4);
    const auto noise = testsig::whiteNoise(sr, 0.6, 0.02);
    for (size_t i = 0; i < sig.size(); ++i) sig[i] += noise[i];
    int voiced = 0, good = 0, n = 0;
    for (auto& tf : runDetector(sig, sr))
    {
        if (tf.time < 0.05 || tf.time > 0.55) continue;
        ++n;
        if (tf.f.voiced) { ++voiced; if (std::abs(centsFromNote(tf.f.midi, 64)) < 5.0) ++good; }
    }
    CHECK(voiced >= n * 9 / 10);
    CHECK(good >= voiced * 95 / 100);
}

TEST_CASE("PitchDetector: block size does not change results")
{
    const double sr = 48000.0;
    const auto sig = testsig::tone(sr, 0.5, [](double t) { return 60.0 + 4.0 * t; }, 5);
    const auto a = runDetector(sig, sr, 1);
    const auto b = runDetector(sig, sr, 64);
    const auto c = runDetector(sig, sr, 4096);
    CHECK_EQ(a.size(), b.size());
    CHECK_EQ(a.size(), c.size());
    bool same = a.size() == b.size() && a.size() == c.size();
    for (size_t i = 0; same && i < a.size(); ++i)
        same = a[i].f.midi == b[i].f.midi && a[i].f.midi == c[i].f.midi && a[i].time == b[i].time && a[i].time == c[i].time;
    CHECK(same);
}

TEST_CASE("PitchDetector: latency-compensated timestamps line up on a slide")
{
    const double sr = 48000.0;
    // One octave slide over 0.5 s (24 semitones/s = 2.4 cents per ms).
    auto midiAt = [](double t) { return t < 0.2 ? 57.0 : (t < 0.7 ? 57.0 + 24.0 * (t - 0.2) : 69.0); };
    const auto frames = runDetector(testsig::tone(sr, 1.0, midiAt, 5), sr);
    double worst = 0.0;
    int n = 0;
    for (auto& tf : frames)
    {
        if (!tf.f.voiced || tf.time < 0.3 || tf.time > 0.6) continue;
        ++n;
        worst = std::max(worst, std::abs(tf.f.midi - midiAt(tf.time)) * 100.0);
    }
    INFO("slide: " << n << " frames, worst raw error " << worst << " cents");
    CHECK(n > 40);
    CHECK(worst < 25.0); // i.e. timing within ~10 ms
}
