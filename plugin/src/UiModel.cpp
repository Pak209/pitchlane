#include "UiModel.h"

#include <cmath>

namespace pitchlane::ui {

using namespace juce;

RollAction actionForPress(const PressInfo& p) noexcept
{
    if (p.middleButton || p.onRuler) return RollAction::Pan;
    if (p.alt) return RollAction::Create;
    if (p.onNote)
    {
        if (p.shift || p.nearRightEdge) return RollAction::Stretch;
        if (p.cmd) return RollAction::ToggleSelect;
        return RollAction::Move;
    }
    return (p.shift || p.cmd) ? RollAction::RubberAdd : RollAction::Rubber;
}

DoubleClickAction actionForDoubleClick(bool onNote) noexcept
{
    return onNote ? DoubleClickAction::DeleteNote : DoubleClickAction::None;
}

CreatedSpan createdSpan(double downTime, double dragTime, double minLength) noexcept
{
    const double a = std::max(0.0, std::min(downTime, dragTime));
    const double b = std::max(downTime, dragTime);
    return { a, std::max(minLength, b - a) };
}

int BarGrid::barAt(double t) const noexcept
{
    return static_cast<int>(std::floor(t / secondsPerBar() + 1e-9)) + 1;
}

PitchWindow fitPitchWindow(int rangeLo, int rangeHi, float heightPx, float minRowPx, double centre, int wantedRows) noexcept
{
    if (rangeHi < rangeLo) std::swap(rangeLo, rangeHi);
    const int total = rangeHi - rangeLo + 1;
    int fit = std::max(6, static_cast<int>(std::floor(heightPx / std::max(1.f, minRowPx))));
    if (wantedRows > 0) fit = std::min(fit, std::max(12, wantedRows));
    if (total <= fit) return { rangeLo, rangeHi };
    int lo = static_cast<int>(std::lround(centre - (fit - 1) * 0.5));
    lo = jlimit(rangeLo, rangeHi - fit + 1, lo);
    return { lo, lo + fit - 1 };
}

double zoomedSpan(double span, float wheelDelta, double minSpan, double maxSpan) noexcept
{
    if (std::abs(wheelDelta) < 1e-9f) return jlimit(minSpan, maxSpan, span);
    return jlimit(minSpan, maxSpan, span * (wheelDelta > 0 ? 0.85 : 1.0 / 0.85));
}

String keyScaleText(int key, ScaleType scale)
{
    if (scale == ScaleType::Chromatic) return "Chromatic";
    String s;
    switch (scale)
    {
        case ScaleType::Major:           s = "major"; break;
        case ScaleType::NaturalMinor:    s = "minor"; break;
        case ScaleType::HarmonicMinor:   s = "harmonic minor"; break;
        case ScaleType::MelodicMinor:    s = "melodic minor"; break;
        case ScaleType::MajorPentatonic: s = "major pent."; break;
        case ScaleType::MinorPentatonic: s = "minor pent."; break;
        case ScaleType::Blues:           s = "blues"; break;
        case ScaleType::Dorian:          s = "dorian"; break;
        case ScaleType::Mixolydian:      s = "mixolydian"; break;
        case ScaleType::Chromatic:
        case ScaleType::NumScales:
        default:                         s = scaleName(scale); break;
    }
    return String(pitchClassName(((key % 12) + 12) % 12)) + " " + s;
}

String rangeText(int lo, int hi, OctaveConvention conv)
{
    return String(noteName(lo, conv)) + String(CharPointer_UTF8(" \xe2\x80\x93 ")) + String(noteName(hi, conv));
}

static String minus() { return String(CharPointer_UTF8("\xe2\x88\x92")); }

String signedMsText(double ms)
{
    const int v = roundToInt(ms);
    return (v > 0 ? "+" : v < 0 ? minus() : String()) + String(std::abs(v)) + " ms";
}

String semitoneText(int st)
{
    return (st > 0 ? "+" : st < 0 ? minus() : String()) + String(std::abs(st)) + " st";
}

String centsText(double cents)
{
    const int v = roundToInt(cents);
    return (v > 0 ? "+" : v < 0 ? minus() : String()) + String(std::abs(v)) + " cents";
}

const std::vector<RangePreset>& rangePresets()
{
    static const std::vector<RangePreset> p = {
        { "Bass", 40, 64 },         // E2 - E4
        { "Baritone", 45, 69 },     // A2 - A4
        { "Tenor", 48, 72 },        // C3 - C5
        { "Alto", 53, 77 },         // F3 - F5
        { "Mezzo-soprano", 57, 81 },// A3 - A5
        { "Soprano", 60, 84 },      // C4 - C6
        { "Wide (C2 - C6)", 36, 84 },
    };
    return p;
}

int nearestScaleNote(double midi, int key, ScaleType scale) noexcept
{
    const int n = nearestNote(midi);
    int best = n;
    double bestD = 1e9;
    for (int d = -6; d <= 6; ++d)
    {
        const int c = n + d;
        if (!isInScale(c, key, scale)) continue;
        const double dist = std::abs(midi - c);
        if (dist < bestD) { bestD = dist; best = c; }
    }
    return best;
}

} // namespace pitchlane::ui
