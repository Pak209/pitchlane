#include "TestFramework.h"
#include "pitchlane/TempoMap.h"

using namespace pitchlane;

namespace {
TempoObservation obs(double t, double ppq, double bpm, int num, int den, double barStart)
{
    TempoObservation o;
    o.time = t; o.ppq = ppq; o.hasPpq = true; o.bpm = bpm; o.num = num; o.den = den;
    o.barStartPpq = barStart; o.hasBarStart = true;
    return o;
}
} // namespace

TEST_CASE("TempoMap: constant tempo, several tempos and signatures")
{
    struct Case { double bpm; int num, den; };
    const Case cases[] = { { 60, 4, 4 }, { 90, 3, 4 }, { 104, 4, 4 }, { 120, 6, 8 }, { 174, 7, 8 }, { 75, 5, 4 }, { 140, 2, 2 } };
    for (const auto& c : cases)
    {
        TempoMap m;
        m.setConstant(c.bpm, c.num, c.den);
        const double quarter = 60.0 / c.bpm;
        const double beat = quarter * 4.0 / c.den;          // one denominator unit
        const double bar = beat * c.num;
        INFO(c.bpm << " bpm " << c.num << "/" << c.den);
        CHECK_NEAR(m.secondsPerBeatAt(3.0), beat, 1e-9);
        CHECK_NEAR(m.timeOfBar(1), 0.0, 1e-9);
        CHECK_NEAR(m.timeOfBar(2), bar, 1e-9);
        CHECK_NEAR(m.timeOfBar(17), 16 * bar, 1e-9);
        // Middle of beat 3 of bar 5 (or the last beat if the bar is shorter).
        const int beatNo = std::min(3, c.num);
        const auto bb = m.barBeatAt(4 * bar + (beatNo - 1) * beat + 0.5 * beat);
        CHECK_EQ(bb.bar, 5);
        CHECK_EQ(bb.beat, beatNo);
        CHECK_NEAR(bb.fraction, 0.5, 1e-6);
        CHECK_NEAR(m.ppqAt(10.0), 10.0 / quarter, 1e-9);
        CHECK_NEAR(m.timeAtPpq(m.ppqAt(7.3)), 7.3, 1e-9);
        const auto lines = m.barLines(bar * 2.5, bar * 6.5);   // bars 4, 5, 6, 7 start inside
        CHECK_EQ(lines.size(), size_t(4));
        if (!lines.empty()) CHECK_EQ(lines.front().bar, 4);
        const auto beats = m.beatLines(0.0, bar - 1e-6);
        CHECK_EQ(static_cast<int>(beats.size()), c.num);
        if (!beats.empty()) CHECK(beats.front().downbeat);
    }
}

TEST_CASE("TempoMap: host observations with a tempo change and a signature change")
{
    TempoMap m;
    m.setConstant(120, 4, 4);
    // Playback starts at 2 s at 100 bpm, 4/4 (ppq 0 at 0 s).
    CHECK(m.observe(obs(2.0, 2.0 * 100 / 60, 100, 4, 4, 0.0)));
    CHECK(!m.observe(obs(3.0, 3.0 * 100 / 60, 100, 4, 4, 4.0)));  // consistent -> no change
    CHECK_NEAR(m.timeOfBar(3), 2 * 2.4, 1e-9);                        // 4/4 @100: 2.4 s per bar
    // Tempo change to 150 bpm at bar 5 (t = 9.6 s, ppq 16).
    CHECK(m.observe(obs(9.6, 16.0, 150, 4, 4, 16.0)));
    CHECK_EQ(m.segments().size(), size_t(2));
    CHECK_NEAR(m.timeOfBar(5), 9.6, 1e-9);
    CHECK_NEAR(m.timeOfBar(6), 9.6 + 1.6, 1e-9);                      // 4/4 @150: 1.6 s per bar
    CHECK_EQ(m.barBeatAt(9.6 + 1.6 + 0.4 + 0.01).bar, 6);
    CHECK_EQ(m.barBeatAt(9.6 + 1.6 + 0.4 + 0.01).beat, 2);
    CHECK_NEAR(m.timeOfBar(4), 7.2, 1e-9);                            // earlier bars unchanged
    // Signature change to 3/4 at bar 9 (ppq 32, t = 9.6 + 4*1.6 = 16.0).
    CHECK(m.observe(obs(16.0, 32.0, 150, 3, 4, 32.0)));
    CHECK_NEAR(m.timeOfBar(9), 16.0, 1e-9);
    CHECK_NEAR(m.timeOfBar(10), 16.0 + 1.2, 1e-9);                    // 3/4 @150: 1.2 s per bar
    const auto bb = m.barBeatAt(16.0 + 1.2 + 0.8 + 0.1);             // bar 10, beat 3
    CHECK_EQ(bb.bar, 10);
    CHECK_EQ(bb.beat, 3);
    const auto lines = m.barLines(14.0, 19.0);
    CHECK_EQ(lines.size(), size_t(4));                                // bars 8 (14.4), 9 (16.0), 10 (17.2), 11 (18.4)
    if (lines.size() == 4) { CHECK_EQ(lines[0].bar, 8); CHECK_EQ(lines[3].bar, 11); CHECK_NEAR(lines[3].time, 18.4, 1e-9); }
    // Seeking back before the change and observing consistent data changes nothing.
    CHECK(!m.observe(obs(4.8, 8.0, 100, 4, 4, 8.0)));
    CHECK_EQ(m.segments().size(), size_t(3));
    // User edits Logic's tempo at bar 5 to 120: the stale future is replaced.
    CHECK(m.observe(obs(9.6, 16.0, 120, 4, 4, 16.0)));
    CHECK_EQ(m.segments().size(), size_t(2));
    CHECK_NEAR(m.timeOfBar(6), 9.6 + 2.0, 1e-9);
}

TEST_CASE("TempoMap: first observation mid-song rebuilds with pre-roll-free bar numbers")
{
    TempoMap m;
    m.setConstant(120, 4, 4);
    // Host at bar 17 of a 6/8 song at 90 bpm (bar = 3 quarters = 2 s).
    const double t = 32.5, ppq = t * 90 / 60;
    const double barStart = std::floor(ppq / 3.0) * 3.0;
    CHECK(m.observe(obs(t, ppq, 90, 6, 8, barStart)));
    CHECK_EQ(m.barBeatAt(t).bar, 17);
    CHECK_NEAR(m.timeOfBar(1), 0.0, 1e-9);
    CHECK_NEAR(m.secondsPerBeatAt(t), 60.0 / 90 / 2, 1e-9);  // eighth notes
    CHECK_EQ(m.barBeatAt(t).beat, 2);                        // 32.5 s = bar 17 + 0.5 s = 1.5 eighths
}

TEST_CASE("TempoMap: a confirmed constant map keeps earlier bars when the tempo changes")
{
    TempoMap m;
    m.setConstant(90, 3, 4);
    CHECK(!m.observe(obs(0.0, 0.0, 90, 3, 4, 0.0)));    // host agrees with the constant map
    CHECK(m.observe(obs(4.0, 6.0, 120, 3, 4, 6.0)));     // tempo change at bar 3
    CHECK_NEAR(m.timeOfBar(2), 2.0, 1e-9);
    CHECK_NEAR(m.timeOfBar(4), 5.5, 1e-9);
}
