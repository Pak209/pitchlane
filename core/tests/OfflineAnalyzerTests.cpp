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

TEST_CASE("OfflineAnalyzer: other sample rates (22.05k, 48k, 96k)")
{
    checkMelody(22050.0);
    checkMelody(96000.0);
}

TEST_CASE("OfflineAnalyzer: lead + harmony mix -> harmony notes flagged and muted, lead kept")
{
    const double sr = 44100.0;
    // Lead: A4 B4 C5 D5 C5 B4 A4 (0.5 s each, back to back) with a 0.25 s breath after the
    // 3rd note; harmony a minor sixth below (not a simple frequency ratio, so the mix has no
    // common "missing fundamental"), quieter, sustained through the breath.
    std::vector<testsig::MelodyNote> lead, harmony;
    const int leadPitches[] = { 69, 71, 72, 74, 72, 71, 69 };
    double t = 0.2;
    for (int i = 0; i < 7; ++i)
    {
        lead.push_back({ t, 0.5, leadPitches[i], 20.0, 5.5 });
        harmony.push_back({ t, (i == 2 ? 0.75 : 0.5), leadPitches[i] - 8 });
        t += (i == 2) ? 0.75 : 0.5;   // breath: lead silent for 0.25 s, harmony keeps going
    }
    auto a = testsig::melody(sr, t + 0.3, lead, 6, 0.40);
    const auto b = testsig::melody(sr, t + 0.3, harmony, 6, 0.22);
    for (size_t i = 0; i < a.size(); ++i) a[i] += b[i];

    const auto res = analyzeMonophonic(a.data(), a.size(), sr);
    int leadKept = 0, harmonyFlagged = 0, harmonyNotes = 0, leadFlagged = 0;
    for (const auto& n : res.notes)
    {
        INFO("  " << noteName(n.pitch) << " start " << n.start << " len " << n.length << " flags " << int(n.flags));
        const bool isHarmony = n.start > 1.60 && n.start < 1.95 && n.pitch == 72 - 8;
        if (isHarmony) { ++harmonyNotes; if (n.harmonySuspect() && n.muted()) ++harmonyFlagged; }
        else if (n.harmonySuspect()) ++leadFlagged;
        else ++leadKept;
    }
    CHECK_EQ(harmonyNotes, 1);          // the transcriber follows the harmony through the breath...
    CHECK_EQ(harmonyFlagged, 1);        // ...and the detector flags + mutes that note
    CHECK_EQ(leadFlagged, 0);
    CHECK(leadKept >= 7);               // every lead note survives (C5 may be split around the breath)
}

TEST_CASE("OfflineAnalyzer: spike removal and octave-error folding")
{
    std::vector<AnalysisFrame> fr(40);
    for (size_t i = 0; i < fr.size(); ++i) { fr[i].time = i * 0.005; fr[i].midi = 64.f; }
    fr[10].midi = 52.f;                        // 1-frame octave dip inside a run
    fr[20].midi = fr[21].midi = 76.f;          // 2-frame octave jump
    fr[0].midi = 40.f;                         // run edge outlier
    removePitchSpikes(fr);
    CHECK_NEAR(fr[10].midi, 64.0, 0.01);
    CHECK_NEAR(fr[20].midi, 64.0, 0.01);
    CHECK_NEAR(fr[21].midi, 64.0, 0.01);
    CHECK_NEAR(fr[0].midi, 0.0, 0.01);         // unvoiced
    CHECK_NEAR(fr[5].midi, 64.0, 0.01);        // untouched

    NoteList notes;
    auto add = [&](double s, double l, int p) { RefNote n; n.start = s; n.length = l; n.pitch = p; notes.push_back(n); };
    add(0.0, 0.5, 64); add(0.55, 0.3, 53); add(0.9, 0.5, 65);   // F3 between E4 and F4 -> F4
    add(2.0, 0.5, 60); add(3.1, 0.4, 72); add(3.6, 0.5, 60);    // octave leap after a 0.6 s rest: kept
    add(5.0, 1.2, 48); add(6.3, 0.5, 60);                       // long note: never folded
    const int fixed = fixOctaveErrors(notes);
    CHECK_EQ(notes[1].pitch, 65);
    CHECK_EQ(notes[0].pitch, 64);
    CHECK_EQ(notes[4].pitch, 72);
    CHECK_EQ(notes[6].pitch, 48);
    CHECK_EQ(fixed, 1);
}

TEST_CASE("OfflineAnalyzer: long file (6 min) keeps reporting progress and cancels quickly")
{
    const double sr = 44100.0;
    std::vector<testsig::MelodyNote> notes;
    for (double t = 0.1; t < 359.0; t += 0.6) notes.push_back({ t, 0.5, 60 + static_cast<int>(t * 7) % 12 });
    const auto audio = testsig::melody(sr, 360.0, notes, 4, 0.3);

    using clock = std::chrono::steady_clock;
    auto last = clock::now();
    double maxGapMs = 0.0;
    const auto start = clock::now();
    const auto res = analyzeMonophonic(audio.data(), audio.size(), sr, {}, [&](float p) {
        const auto now = clock::now();
        maxGapMs = std::max(maxGapMs, std::chrono::duration<double, std::milli>(now - last).count());
        last = now;
        return p < 0.5f;   // cancel half way
    });
    const double totalMs = std::chrono::duration<double, std::milli>(clock::now() - start).count();
    INFO("6-min file: cancelled after " << totalMs << " ms, longest gap between progress calls " << maxGapMs << " ms");
    CHECK(res.cancelled);
    CHECK(res.notes.empty());
    CHECK(maxGapMs < 400.0);
}
