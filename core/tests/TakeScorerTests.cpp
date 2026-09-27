#include "TestFramework.h"
#include "pitchlane/TakeScorer.h"

#include <cmath>

using namespace pitchlane;

namespace {
// Four quarter notes at 120 bpm (0.5 s each) starting at 1.0 s: C4 D4 E4 F4.
NoteList melody()
{
    NoteList n;
    const int p[] = { 60, 62, 64, 65 };
    for (int i = 0; i < 4; ++i) n.push_back({ 1.0 + 0.5 * i, 0.45, p[i], 1.f, 100 });
    return n;
}

// Sings `pitch(t)` (MIDI, <= 0 = silence) with 5 ms frames from t0 to t1.
template <typename F>
void sing(TakeScorer& s, double t0, double t1, F pitch)
{
    for (double t = t0; t < t1; t += 0.005) s.addFrame(t, static_cast<float>(pitch(t)));
}

// Pitch of the melody note sounding at t, with an onset shift and a cents offset.
double sungMelody(double t, double shift, double cents, int skipNote = -1)
{
    const int p[] = { 60, 62, 64, 65 };
    for (int i = 0; i < 4; ++i)
    {
        const double s = 1.0 + 0.5 * i + shift;
        if (t >= s && t < s + 0.45 && i != skipNote) return p[i] + cents / 100.0;
    }
    return 0.0;
}
} // namespace

TEST_CASE("TakeScorer: on time, early, late (80 ms tolerance)")
{
    struct Case { double shiftMs; NoteScore::Timing want; };
    const Case cases[] = { { 0, NoteScore::Timing::OnTime }, { 60, NoteScore::Timing::OnTime }, { -60, NoteScore::Timing::OnTime },
                           { 130, NoteScore::Timing::Late }, { -130, NoteScore::Timing::Early } };
    for (const auto& c : cases)
    {
        TakeScorer s;
        s.setReference(melody(), {});
        sing(s, 0.0, 4.0, [&](double t) { return sungMelody(t, c.shiftMs * 0.001, 0.0); });
        INFO("shift " << c.shiftMs << " ms");
        CHECK_EQ(s.scores().size(), size_t(4));
        for (const auto& sc : s.scores())
        {
            CHECK(sc.timing == c.want);
            CHECK_NEAR(sc.onsetErrorMs, c.shiftMs, 6.0);
            CHECK(sc.pitch == NoteScore::Pitch::InTune);
        }
        const auto sum = s.summary();
        CHECK_EQ(sum.scored, 4);
        CHECK_NEAR(sum.meanAbsOnsetMs, std::abs(c.shiftMs), 6.0);
    }
}

TEST_CASE("TakeScorer: sharp, flat, drift, missed, muted notes, reset, offset + transpose")
{
    {   // sharp by 40 cents
        TakeScorer s;
        s.setReference(melody(), {});
        sing(s, 0.0, 4.0, [](double t) { return sungMelody(t, 0.0, 40.0); });
        for (const auto& sc : s.scores()) { CHECK(sc.pitch == NoteScore::Pitch::Sharp); CHECK_NEAR(sc.meanCents, 40.0, 1.0); }
        CHECK_EQ(s.summary().sharp, 4);
    }
    {   // flat by 35 cents
        TakeScorer s;
        s.setReference(melody(), {});
        sing(s, 0.0, 4.0, [](double t) { return sungMelody(t, 0.0, -35.0); });
        CHECK_EQ(s.summary().flat, 4);
    }
    {   // a long note drifting flat by 60 cents (in tune on average: -30 -> "flat" edge case avoided with 25 tol? mean -30)
        NoteList n { { 1.0, 2.0, 67, 1.f, 100 } };
        TakeScorer s;
        s.setReference(n, {});
        sing(s, 0.0, 3.5, [](double t) { return (t >= 1.0 && t < 3.0) ? 67.0 - 0.6 * (t - 1.0) / 2.0 : 0.0; });
        CHECK_EQ(s.scores().size(), size_t(1));
        CHECK(s.scores()[0].drifting);
        CHECK_NEAR(s.scores()[0].driftCents, -60.0, 3.0);
        CHECK_EQ(s.summary().drifting, 1);
    }
    {   // third note not sung -> missed; wrong pitch also counts as missed
        TakeScorer s;
        s.setReference(melody(), {});
        sing(s, 0.0, 4.0, [](double t) { return sungMelody(t, 0.0, 0.0, 2); });
        CHECK_EQ(s.summary().missed, 1);
        CHECK(s.scoreFor(2) != nullptr && s.scoreFor(2)->timing == NoteScore::Timing::Missed);
        TakeScorer w;
        w.setReference(melody(), {});
        sing(w, 0.0, 4.0, [](double t) { return sungMelody(t, 0.0, 0.0) > 0 ? 72.0 : 0.0; });
        CHECK_EQ(w.summary().missed, 4);
    }
    {   // muted notes are not scored
        auto m = melody();
        m[1].setFlag(RefNote::Muted, true);
        TakeScorer s;
        s.setReference(m, {});
        sing(s, 0.0, 4.0, [](double t) { return sungMelody(t, 0.0, 0.0, 1); });
        CHECK_EQ(s.summary().scored, 3);
        CHECK_EQ(s.summary().missed, 0);
        CHECK(s.scoreFor(1) == nullptr);
    }
    {   // reset (jump) starts a new take; notes before the new start are not "missed"
        TakeScorer s;
        s.setReference(melody(), {});
        sing(s, 0.0, 4.0, [](double t) { return sungMelody(t, 0.0, 0.0); });
        CHECK_EQ(s.summary().scored, 4);
        s.reset();
        sing(s, 2.0, 4.0, [](double t) { return sungMelody(t, 0.0, 0.0); });
        CHECK_EQ(s.summary().scored, 2);
        CHECK_EQ(s.summary().missed, 0);
        // time going backwards without reset also starts a new take
        sing(s, 1.9, 4.0, [](double t) { return sungMelody(t, 0.0, 0.0); });
        CHECK_EQ(s.summary().scored, 2);
    }
    {   // reference offset + transpose are applied
        ReferenceMapping map;
        map.offsetSec = 2.0;
        map.transpose = -12;
        TakeScorer s;
        s.setReference(melody(), map);
        sing(s, 0.0, 6.0, [](double t) { const double p = sungMelody(t - 2.0, 0.1, 0.0); return p > 0 ? p - 12.0 : 0.0; });
        CHECK_EQ(s.summary().late, 4);
    }
    {   // repeated same-pitch notes: the second onset is not taken from the first note
        NoteList n { { 1.0, 0.4, 60, 1.f, 100 }, { 1.5, 0.4, 60, 1.f, 100 } };
        TakeScorer s;
        s.setReference(n, {});
        sing(s, 0.0, 2.5, [](double t) { return ((t >= 1.0 && t < 1.4) || (t >= 1.62 && t < 1.9)) ? 60.0 : 0.0; });
        CHECK_EQ(s.scores().size(), size_t(2));
        CHECK(s.scores()[1].timing == NoteScore::Timing::Late);
        CHECK_NEAR(s.scores()[1].onsetErrorMs, 120.0, 6.0);
    }
}
