#include <atomic>
#include <chrono>
#include <vector>

#include "TestFramework.h"
#include "TestSignals.h"
#include "pitchlane/NoteMath.h"
#include "pitchlane/OfflineAnalyzer.h"

using namespace pitchlane;

namespace {

void checkMelody(double sr)
{
    const std::vector<testsig::MelodyNote> truth = {
        { 0.20, 0.40, 57 },            // A3
        { 0.70, 0.30, 64 },            // E4
        { 1.10, 0.80, 69, 40.0, 5.5 }, // A4 with +/-40 cent vibrato
        { 2.00, 0.25, 72 },            // C5
        { 2.35, 0.35, 67 },            // G4 ...
        { 2.70, 0.35, 69 },            // ... legato into A4 (no gap)
        { 3.20, 0.15, 62 },            // short D4 (150 ms)
        { 3.50, 0.50, 60 },            // C4
    };
    auto audio = testsig::melody(sr, 4.3, truth, 6, 0.4);
    // A little noise floor, like a real stem.
    const auto noise = testsig::whiteNoise(sr, 4.3, 0.001);
    for (size_t i = 0; i < audio.size(); ++i) audio[i] += noise[i];

    int progressCalls = 0;
    float lastProgress = 0.f;
    const auto t0 = std::chrono::steady_clock::now();
    const auto res = analyzeMonophonic(audio.data(), audio.size(), sr, {}, [&](float p) {
        ++progressCalls;
        lastProgress = p;
        return true;
    });
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

    INFO("analyzer @" << sr << " Hz: " << res.notes.size() << " notes in " << ms << " ms");
    for (auto& n : res.notes)
        INFO("  " << noteName(n.pitch) << " start " << n.start << " len " << n.length << " conf " << n.confidence);

    CHECK(!res.cancelled);
    CHECK(progressCalls > 2);
    CHECK_NEAR(lastProgress, 1.0, 1e-6);
    CHECK_EQ(res.notes.size(), truth.size());
    if (res.notes.size() != truth.size()) return;
    for (size_t i = 0; i < truth.size(); ++i)
    {
        CHECK_EQ(res.notes[i].pitch, truth[i].midi);
        CHECK_NEAR(res.notes[i].start, truth[i].start, 0.030);
        CHECK_NEAR(res.notes[i].end(), truth[i].start + truth[i].length, 0.060);
    }
}

} // namespace

TEST_CASE("OfflineAnalyzer: synthetic melody -> correct notes, pitches and onsets")
{
    checkMelody(44100.0);
    checkMelody(48000.0);
}

TEST_CASE("OfflineAnalyzer: silence and noise produce no notes")
{
    const double sr = 44100.0;
    std::vector<float> silence(static_cast<size_t>(sr * 2), 0.f);
    CHECK(analyzeMonophonic(silence.data(), silence.size(), sr).notes.empty());
    const auto noise = testsig::whiteNoise(sr, 2.0, 0.1);
    const auto res = analyzeMonophonic(noise.data(), noise.size(), sr);
    INFO("noise notes: " << res.notes.size());
    CHECK(res.notes.size() <= 1);
}

TEST_CASE("OfflineAnalyzer: cancellation stops early")
{
    const double sr = 44100.0;
    const auto audio = testsig::steady(sr, 20.0, 60, 4);
    int calls = 0;
    const auto res = analyzeMonophonic(audio.data(), audio.size(), sr, {}, [&](float) { return ++calls < 3; });
    CHECK(res.cancelled);
    CHECK(res.notes.empty());
}

TEST_CASE("OfflineAnalyzer: short blips and reverb-tail artefacts are filtered")
{
    const double sr = 44100.0;
    const std::vector<testsig::MelodyNote> notes = {
        { 0.2, 0.5, 64 },
        { 0.75, 0.04, 76 }, // 40 ms blip (e.g. separation artefact) -> removed
        { 1.0, 0.5, 67 },
    };
    const auto audio = testsig::melody(sr, 1.8, notes);
    const auto res = analyzeMonophonic(audio.data(), audio.size(), sr);
    CHECK_EQ(res.notes.size(), size_t(2));
    if (res.notes.size() == 2)
    {
        CHECK_EQ(res.notes[0].pitch, 64);
        CHECK_EQ(res.notes[1].pitch, 67);
    }
}
