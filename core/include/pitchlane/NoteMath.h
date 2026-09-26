#pragma once
// Pitch / note / cents helpers. Pure functions, no allocation, safe anywhere.

#include <array>
#include <cmath>
#include <string>

namespace pitchlane {

constexpr double kA4Hz = 440.0;
constexpr int kA4Midi = 69;

/** Frequency (Hz) to fractional MIDI note number (69.0 = A4 = 440 Hz). */
inline double hzToMidi(double hz, double a4 = kA4Hz) noexcept
{
    return hz > 0.0 ? kA4Midi + 12.0 * std::log2(hz / a4) : 0.0;
}

/** Fractional MIDI note number to frequency (Hz). */
inline double midiToHz(double midi, double a4 = kA4Hz) noexcept
{
    return a4 * std::pow(2.0, (midi - kA4Midi) / 12.0);
}

/** Signed cents from reference frequency to frequency (positive = sharp). */
inline double centsBetween(double hz, double refHz) noexcept
{
    return (hz > 0.0 && refHz > 0.0) ? 1200.0 * std::log2(hz / refHz) : 0.0;
}

/** Signed cents of a fractional MIDI pitch relative to an integer target note. */
inline double centsFromNote(double midi, int targetNote) noexcept
{
    return (midi - static_cast<double>(targetNote)) * 100.0;
}

/** Nearest integer MIDI note. */
inline int nearestNote(double midi) noexcept
{
    return static_cast<int>(std::lround(midi));
}

enum class OctaveConvention
{
    Scientific,  // MIDI 60 = C4 (A4 = 440 Hz). Default.
    Yamaha       // MIDI 60 = C3 (Logic Pro's default display).
};

/** Pitch-class name, sharps only ("C", "C#", ... "B"). */
const char* pitchClassName(int midiNote) noexcept;

/** Note name with octave, e.g. 60 -> "C4" (Scientific) or "C3" (Yamaha). */
std::string noteName(int midiNote, OctaveConvention conv = OctaveConvention::Scientific);

enum class TuningStatus { NoPitch, Flat, InTune, Sharp };

/** Classify a cents offset against a symmetric tolerance (e.g. +/-10 cents). */
inline TuningStatus classifyCents(double cents, double toleranceCents) noexcept
{
    if (std::abs(cents) <= toleranceCents) return TuningStatus::InTune;
    return cents < 0.0 ? TuningStatus::Flat : TuningStatus::Sharp;
}

// ---------------------------------------------------------------------------
// Keys and scales (used only for row highlighting; reference notes are the target)
// ---------------------------------------------------------------------------
enum class ScaleType
{
    Chromatic = 0,
    Major,
    NaturalMinor,
    HarmonicMinor,
    MelodicMinor,
    MajorPentatonic,
    MinorPentatonic,
    Blues,
    Dorian,
    Mixolydian,
    NumScales
};

const char* scaleName(ScaleType s) noexcept;

/** 12-bit mask of pitch classes (bit 0 = root) for the scale. */
unsigned scaleMask(ScaleType s) noexcept;

/** Is midiNote in the scale rooted at keyRoot (0 = C ... 11 = B)? */
bool isInScale(int midiNote, int keyRoot, ScaleType s) noexcept;

} // namespace pitchlane
