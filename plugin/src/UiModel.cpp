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

NoteList notesForImport(NoteList notes, ImportVoices choice)
{
    sortNotes(notes);
    switch (choice)
    {
        case ImportVoices::LeadHighest:       markLeadLine(notes, LeadMode::Highest); removeMuted(notes); break;
        case ImportVoices::LeadLoudest:       markLeadLine(notes, LeadMode::LoudestSustained); removeMuted(notes); break;
        case ImportVoices::AllHarmoniesMuted: markLeadLine(notes, LeadMode::Highest); break;
        case ImportVoices::All:               break;
    }
    return notes;
}

bool TempoFollower::update(const TransportSnapshot& s)
{
    const double bpm = s.bpm > 1.0 ? s.bpm : 120.0;
    const int num = s.timeSigNum > 0 ? s.timeSigNum : 4;
    const int den = s.timeSigDen > 0 ? s.timeSigDen : 4;
    if (s.hasPpq)
    {
        bool changed = false;
        if (!following_) { following_ = true; map_.setConstant(bpm, num, den); changed = true; }
        TempoObservation o;
        o.time = s.songTime;
        o.ppq = s.ppq;
        o.hasPpq = true;
        o.bpm = bpm;
        o.num = num;
        o.den = den;
        o.barStartPpq = s.barStartPpq;
        o.hasBarStart = s.hasBarStart != 0;
        return map_.observe(o) || changed;
    }
    const auto& seg = map_.segments();
    if (!following_ && seg.size() == 1 && std::abs(seg[0].bpm - bpm) < 1e-6 && seg[0].num == num && seg[0].den == den)
        return false;
    following_ = false;
    map_.setConstant(bpm, num, den);
    return true;
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


TimeFit planTimeFit(const TempoMap& tempo, double firstNoteTime, int bars, double minSpan, double maxSpan) noexcept
{
    TimeFit f;
    const auto bb = tempo.barBeatAt(firstNoteTime);
    f.bar = std::max(1, bb.bar);
    double start = tempo.timeOfBar(f.bar);
    if (start > firstNoteTime) start = firstNoteTime;
    if (firstNoteTime - start < 0.2) start -= 0.5 * tempo.secondsPerBeatAt(firstNoteTime);
    const double span = tempo.timeOfBar(f.bar + std::max(1, bars)) - tempo.timeOfBar(f.bar);
    f.span = jlimit(minSpan, maxSpan, span > 0.0 ? span : 8.0);
    // The first note must be well inside the window even when the span was clamped.
    if (firstNoteTime > start + 0.5 * f.span) start = firstNoteTime - 0.1 * f.span;
    f.start = start;
    return f;
}

RangeFit planVocalRange(int curLo, int curHi, int notesLo, int notesHi, bool isDefault) noexcept
{
    RangeFit r;
    r.lo = curLo;
    r.hi = curHi;
    if (notesHi < notesLo || (notesLo >= curLo && notesHi <= curHi)) return r;
    r.needed = true;
    r.automatic = isDefault;
    if (isDefault)
    {
        r.lo = notesLo - 2;
        r.hi = notesHi + 2;
        if (r.hi - r.lo < 12)
        {
            const int grow = 12 - (r.hi - r.lo);
            r.lo -= grow / 2;
            r.hi += grow - grow / 2;
        }
    }
    else
    {
        r.lo = std::min(curLo, notesLo - 2);
        r.hi = std::max(curHi, notesHi + 2);
    }
    r.lo = jlimit(24, 96, r.lo);    // parameter limits
    r.hi = jlimit(r.lo + 11, 108, r.hi);
    return r;
}

HostLink hostLinkState(bool syncOn, bool fresh, bool hostTimeline, bool playing) noexcept
{
    if (!syncOn) return HostLink::SyncOff;
    if (!fresh) return HostLink::NoHostInfo;
    if (!hostTimeline) return HostLink::NoTimeline;
    return playing ? HostLink::Playing : HostLink::Stopped;
}

String hostLinkTooltip(HostLink s)
{
    const String legend = String::fromUTF8("\n\nGreen = Logic playing (synced)  \xc2\xb7  Amber = Logic stopped (synced)  \xc2\xb7  "
                          "Grey = no transport info from Logic");
    switch (s)
    {
        case HostLink::SyncOff:
            return String("Logic Sync off: free-running clock at the manual BPM.") + legend;
        case HostLink::NoHostInfo:
            return String("Grey: Logic Sync is on, but Logic is not sending audio to Pitch Lane right now, so "
                                     "there is no transport or tempo info (the BPM shown is the manual one). Logic only "
                                     "runs a plugin while playing, or while its track is selected / record-armed. "
                                     "Press play to sync.") + legend;
        case HostLink::NoTimeline:
            return String("Grey: the host sends audio but no timeline, so the free-running clock is used."
                                    ) + legend;
        case HostLink::Stopped:
            return String("Amber: synced to Logic, transport stopped. Live pitch is compared with the note "
                                     "under Logic's playhead.") + legend;
        case HostLink::Playing:
            return String("Green: synced to Logic, playing.") + legend;
    }
    return {};
}

} // namespace pitchlane::ui
