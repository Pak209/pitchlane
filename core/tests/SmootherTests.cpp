#include <algorithm>
#include <vector>

#include "TestFramework.h"
#include "TestSignals.h"
#include "pitchlane/PitchDetector.h"
#include "pitchlane/PitchSmoother.h"

using namespace pitchlane;

namespace {

struct Track
{
    std::vector<double> t;
    std::vector<float> raw, smooth;
};

Track detectAndSmooth(const std::vector<float>& sig, double sr)
{
    PitchDetector det;
    det.prepare(sr);
    PitchSmoother sm;
    sm.configure(det.hopSeconds());
    std::vector<PitchFrame> buf(static_cast<size_t>(det.maxFramesForBlock(512)));
    Track tr;
    for (size_t pos = 0; pos < sig.size(); pos += 512)
    {
        const int n = static_cast<int>(std::min<size_t>(512, sig.size() - pos));
        const int got = det.process(sig.data() + pos, n, buf.data(), static_cast<int>(buf.size()));
        for (int i = 0; i < got; ++i)
        {
            const auto& f = buf[static_cast<size_t>(i)];
            tr.t.push_back((static_cast<double>(pos) + f.sampleIndex - det.latencySamples()) / sr);
            tr.raw.push_back(f.voiced ? f.midi : 0.f);
            tr.smooth.push_back(sm.process(f.voiced, f.midi));
        }
    }
    return tr;
}

} // namespace

TEST_CASE("Smoother: removes single-frame outliers")
{
    PitchSmoother sm;
    sm.configure(0.005);
    float last = 0.f;
    for (int i = 0; i < 20; ++i) last = sm.process(true, i == 10 ? 81.f : 69.f); // octave blip
    CHECK_NEAR(last, 69.0, 0.01);
    // The blip frame itself must not appear in the output.
    sm.reset();
    float maxOut = 0.f;
    for (int i = 0; i < 20; ++i) maxOut = std::max(maxOut, sm.process(true, i == 10 ? 81.f : 69.f));
    CHECK(maxOut < 69.5f);
}

TEST_CASE("Smoother: unvoiced resets (no bridging across silence)")
{
    PitchSmoother sm;
    sm.configure(0.005);
    for (int i = 0; i < 10; ++i) sm.process(true, 60.f);
    CHECK_EQ(sm.process(false, 0.f), 0.f);
    CHECK_NEAR(sm.process(true, 67.f), 67.0, 1e-6); // snaps, no glide from 60
}

TEST_CASE("Smoother: vibrato depth is preserved (not flattened)")
{
    const double sr = 48000.0;
    const double depthCents = 50.0, rate = 5.5; // +/-50 cents (100 cents peak-to-peak)
    const auto sig = testsig::tone(sr, 2.0, [&](double t) { return 69.0 + depthCents / 100.0 * std::sin(testsig::kTwoPi * rate * t); }, 6);
    const auto tr = detectAndSmooth(sig, sr);
    float rmin = 1e9f, rmax = -1e9f, smin = 1e9f, smax = -1e9f;
    for (size_t i = 0; i < tr.t.size(); ++i)
    {
        if (tr.t[i] < 0.3 || tr.t[i] > 1.9) continue;
        if (tr.raw[i] > 0) { rmin = std::min(rmin, tr.raw[i]); rmax = std::max(rmax, tr.raw[i]); }
        if (tr.smooth[i] > 0) { smin = std::min(smin, tr.smooth[i]); smax = std::max(smax, tr.smooth[i]); }
    }
    const double rawPP = (rmax - rmin) * 100.0, smPP = (smax - smin) * 100.0;
    INFO("vibrato p-p: true 100 cents, raw " << rawPP << ", smoothed " << smPP);
    CHECK(rawPP > 92.0 && rawPP < 108.0);
    CHECK(smPP > 85.0 && smPP < 108.0);
}

TEST_CASE("Smoother: slide is tracked with small lag")
{
    const double sr = 48000.0;
    auto midiAt = [](double t) { return t < 0.3 ? 60.0 : (t < 0.8 ? 60.0 + 14.0 * (t - 0.3) : 67.0); };
    const auto tr = detectAndSmooth(testsig::tone(sr, 1.2, midiAt, 6), sr);
    // Estimate lag: shift s (ms) minimising error between smoothed and truth during the slide.
    double bestLag = 0, bestErr = 1e9;
    for (int lagMs = 0; lagMs <= 60; ++lagMs)
    {
        double err = 0;
        int n = 0;
        for (size_t i = 0; i < tr.t.size(); ++i)
            if (tr.smooth[i] > 0 && tr.t[i] > 0.4 && tr.t[i] < 0.75)
            {
                err += std::abs(tr.smooth[i] - midiAt(tr.t[i] - lagMs * 0.001));
                ++n;
            }
        err /= std::max(1, n);
        if (err < bestErr) { bestErr = err; bestLag = lagMs; }
    }
    // Final value reached.
    float endVal = 0.f;
    for (size_t i = 0; i < tr.t.size(); ++i) if (tr.t[i] > 1.0 && tr.smooth[i] > 0) endVal = tr.smooth[i];
    INFO("slide: estimated display lag " << bestLag << " ms, residual " << bestErr * 100 << " cents");
    CHECK(bestLag <= 25.0);
    CHECK(bestErr * 100.0 < 5.0);
    CHECK_NEAR(endVal, 67.0, 0.05);
    // Monotonic within a few cents during the slide.
    float prev = 0.f;
    bool mono = true;
    for (size_t i = 0; i < tr.t.size(); ++i)
        if (tr.smooth[i] > 0 && tr.t[i] > 0.35 && tr.t[i] < 0.8)
        {
            if (prev > 0 && tr.smooth[i] < prev - 0.03f) mono = false;
            prev = tr.smooth[i];
        }
    CHECK(mono);
}

TEST_CASE("Smoother: smoothing amount mapping (60 % = original tuning, monotonic, 0 = raw)")
{
    const auto def = smoothingForAmount(0.6);
    CHECK_EQ(def.medianLength, 5);
    CHECK_NEAR(def.timeConstantMs, 10.0, 0.5);
    const auto raw = smoothingForAmount(0.0);
    CHECK_EQ(raw.medianLength, 1);
    CHECK_NEAR(raw.timeConstantMs, 0.0, 1e-9);
    const auto maxS = smoothingForAmount(1.0);
    CHECK_EQ(maxS.medianLength, 7);
    CHECK_NEAR(maxS.timeConstantMs, 25.0, 1e-9);
    double prevTau = -1.0;
    int prevMed = 0;
    bool monotonic = true;
    for (int i = 0; i <= 100; ++i)
    {
        const auto s = smoothingForAmount(i / 100.0);
        if (s.timeConstantMs < prevTau || s.medianLength < prevMed) monotonic = false;
        prevTau = s.timeConstantMs;
        prevMed = s.medianLength;
    }
    CHECK(monotonic);
    CHECK_EQ(smoothingForAmount(-3.0).medianLength, 1);   // clamped
    CHECK_EQ(smoothingForAmount(7.0).medianLength, 7);

    // amount 0: the smoother passes the detector output straight through.
    PitchSmoother sm;
    sm.configure(0.005, raw.medianLength, raw.timeConstantMs);
    CHECK_NEAR(sm.process(true, 60.0f), 60.0f, 1e-6);
    CHECK_NEAR(sm.process(true, 60.3f), 60.3f, 1e-6);
}
