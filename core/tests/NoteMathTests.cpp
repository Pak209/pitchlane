#include "TestFramework.h"
#include "pitchlane/NoteMath.h"

using namespace pitchlane;

TEST_CASE("NoteMath: Hz <-> MIDI")
{
    CHECK_NEAR(hzToMidi(440.0), 69.0, 1e-9);
    CHECK_NEAR(hzToMidi(261.6255653), 60.0, 1e-6);
    CHECK_NEAR(hzToMidi(220.0), 57.0, 1e-9);
    CHECK_NEAR(midiToHz(69.0), 440.0, 1e-9);
    CHECK_NEAR(midiToHz(81.0), 880.0, 1e-9);
    CHECK_NEAR(hzToMidi(midiToHz(63.37)), 63.37, 1e-9);
    CHECK_NEAR(hzToMidi(432.0, 432.0), 69.0, 1e-9);
}

TEST_CASE("NoteMath: cents")
{
    CHECK_NEAR(centsBetween(880.0, 440.0), 1200.0, 1e-9);
    CHECK_NEAR(centsBetween(440.0 * std::pow(2.0, 10.0 / 1200.0), 440.0), 10.0, 1e-9);
    CHECK_NEAR(centsFromNote(69.12, 69), 12.0, 1e-9);
    CHECK_NEAR(centsFromNote(68.9, 69), -10.0, 1e-9);
    CHECK_EQ(nearestNote(68.51), 69);
    CHECK_EQ(nearestNote(68.49), 68);
}

TEST_CASE("NoteMath: note names")
{
    CHECK_EQ(noteName(60), std::string("C4"));
    CHECK_EQ(noteName(69), std::string("A4"));
    CHECK_EQ(noteName(61), std::string("C#4"));
    CHECK_EQ(noteName(59), std::string("B3"));
    CHECK_EQ(noteName(0), std::string("C-1"));
    CHECK_EQ(noteName(60, OctaveConvention::Yamaha), std::string("C3"));
    CHECK_EQ(noteName(69, OctaveConvention::Yamaha), std::string("A3"));
}

TEST_CASE("NoteMath: tuning classification")
{
    CHECK(classifyCents(0.0, 10.0) == TuningStatus::InTune);
    CHECK(classifyCents(10.0, 10.0) == TuningStatus::InTune);
    CHECK(classifyCents(-10.0, 10.0) == TuningStatus::InTune);
    CHECK(classifyCents(-10.5, 10.0) == TuningStatus::Flat);
    CHECK(classifyCents(12.0, 10.0) == TuningStatus::Sharp);
    CHECK(classifyCents(4.0, 3.0) == TuningStatus::Sharp);
}

TEST_CASE("NoteMath: scales")
{
    // C major
    CHECK(isInScale(60, 0, ScaleType::Major));
    CHECK(!isInScale(61, 0, ScaleType::Major));
    CHECK(isInScale(71, 0, ScaleType::Major));
    // A minor (natural) contains G, not G#
    CHECK(isInScale(67, 9, ScaleType::NaturalMinor));
    CHECK(!isInScale(68, 9, ScaleType::NaturalMinor));
    // A harmonic minor contains G#
    CHECK(isInScale(68, 9, ScaleType::HarmonicMinor));
    // Chromatic contains everything
    for (int n = 0; n < 128; ++n) CHECK(isInScale(n, 5, ScaleType::Chromatic));
    // Negative-safe
    CHECK(isInScale(-12, 0, ScaleType::Major));
}
