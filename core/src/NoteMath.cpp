#include "pitchlane/NoteMath.h"

namespace pitchlane {

namespace {
constexpr const char* kNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

inline int wrap12(int n) noexcept { return ((n % 12) + 12) % 12; }

// Floor division so that negative MIDI numbers get the right octave.
inline int floorDiv(int a, int b) noexcept { return (a >= 0) ? a / b : -((-a + b - 1) / b); }
} // namespace

const char* pitchClassName(int midiNote) noexcept { return kNames[wrap12(midiNote)]; }

std::string noteName(int midiNote, OctaveConvention conv)
{
    const int octave = floorDiv(midiNote, 12) - (conv == OctaveConvention::Scientific ? 1 : 2);
    return std::string(pitchClassName(midiNote)) + std::to_string(octave);
}

const char* scaleName(ScaleType s) noexcept
{
    switch (s)
    {
        case ScaleType::Chromatic:       return "Chromatic";
        case ScaleType::Major:           return "Major";
        case ScaleType::NaturalMinor:    return "Natural Minor";
        case ScaleType::HarmonicMinor:   return "Harmonic Minor";
        case ScaleType::MelodicMinor:    return "Melodic Minor";
        case ScaleType::MajorPentatonic: return "Major Pentatonic";
        case ScaleType::MinorPentatonic: return "Minor Pentatonic";
        case ScaleType::Blues:           return "Blues";
        case ScaleType::Dorian:          return "Dorian";
        case ScaleType::Mixolydian:      return "Mixolydian";
        default:                         return "?";
    }
}

unsigned scaleMask(ScaleType s) noexcept
{
    auto m = [](std::initializer_list<int> degrees) {
        unsigned mask = 0;
        for (int d : degrees) mask |= 1u << d;
        return mask;
    };
    switch (s)
    {
        case ScaleType::Chromatic:       return 0xFFFu;
        case ScaleType::Major:           return m({ 0, 2, 4, 5, 7, 9, 11 });
        case ScaleType::NaturalMinor:    return m({ 0, 2, 3, 5, 7, 8, 10 });
        case ScaleType::HarmonicMinor:   return m({ 0, 2, 3, 5, 7, 8, 11 });
        case ScaleType::MelodicMinor:    return m({ 0, 2, 3, 5, 7, 9, 11 });
        case ScaleType::MajorPentatonic: return m({ 0, 2, 4, 7, 9 });
        case ScaleType::MinorPentatonic: return m({ 0, 3, 5, 7, 10 });
        case ScaleType::Blues:           return m({ 0, 3, 5, 6, 7, 10 });
        case ScaleType::Dorian:          return m({ 0, 2, 3, 5, 7, 9, 10 });
        case ScaleType::Mixolydian:      return m({ 0, 2, 4, 5, 7, 9, 10 });
        default:                         return 0xFFFu;
    }
}

bool isInScale(int midiNote, int keyRoot, ScaleType s) noexcept
{
    return (scaleMask(s) >> wrap12(midiNote - keyRoot)) & 1u;
}

} // namespace pitchlane
