#include "PianoRoll.h"

#include "Params.h"
#include "PluginProcessor.h"
#include "Theme.h"

namespace pitchlane {

using namespace juce;
namespace col = theme::col;

namespace {
bool isBlackKey(int n)
{
    const int pc = ((n % 12) + 12) % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}
constexpr float kMinRowPx = 15.f;
constexpr double kOnsetGraceSec = 0.10;
} // namespace

Colour PianoRoll::statusColour(TuningStatus s)
{
    switch (s)
    {
        case TuningStatus::InTune: return col::cyan;
        case TuningStatus::Flat:
        case TuningStatus::Sharp:  return col::off;
        case TuningStatus::NoPitch:
        default:                   return col::textDim;
    }
}

PianoRoll::PianoRoll(PitchLaneProcessor& p) : proc_(p)
{
    setWantsKeyboardFocus(true);
    setOpaque(true);
    history_.reserve(1 << 16);
}

PianoRoll::~PianoRoll() = default;

PianoRoll::View PianoRoll::makeView() const
{
    auto& ap = proc_.getApvts();
    auto get = [&](const char* id) { return ap.getRawParameterValue(id)->load(); };
    View v;
    v.rangeLo = roundToInt(get(params::lowNote));
    v.rangeHi = roundToInt(get(params::highNote));
    if (v.rangeHi < v.rangeLo + 11) v.rangeHi = v.rangeLo + 11; // at least an octave
    v.span = get(params::viewSeconds);
    v.start = viewStart_;
    v.offset = get(params::refOffset) * 0.001;
    v.calib = get(params::calibration) * 0.001;
    v.transpose = roundToInt(get(params::transpose));
    v.tolerance = get(params::tolerance);
    v.key = roundToInt(get(params::key));
    v.scale = static_cast<ScaleType>(roundToInt(get(params::scale)));
    v.conv = roundToInt(get(params::noteNames)) == 1 ? OctaveConvention::Yamaha : OctaveConvention::Scientific;
    v.scalesGuide = roundToInt(get(params::guide)) == static_cast<int>(params::GuideMode::Scales);
    const auto disp = static_cast<params::DisplayMode>(roundToInt(get(params::display)));
    v.showNotes = disp != params::DisplayMode::Vocal;
    v.showTrace = disp != params::DisplayMode::Reference;
    v.tempo = &tempo_.map();

    auto b = getLocalBounds().toFloat();
    v.ruler = b.removeFromTop(kRulerH);
    v.lane = b.removeFromTop(kLaneH);
    v.keys = b.removeFromLeft(kKeysW);
    v.area = b;
    v.ruler.removeFromLeft(kKeysW);
    v.lane.removeFromLeft(kKeysW);
    const auto win = ui::fitPitchWindow(v.rangeLo, v.rangeHi, v.area.getHeight(), kMinRowPx, pitchCentre_,
                                        roundToInt(rowsWanted_));
    v.lo = win.lo;
    v.hi = win.hi;
    return v;
}

double PianoRoll::nowTime() const
{
    if (!snap_.playing) return snap_.songTime;
    const double dt = (Time::getMillisecondCounterHiRes() - snap_.wallMs) * 0.001;
    return snap_.songTime + jlimit(0.0, 0.25, dt); // extrapolate between audio blocks
}

void PianoRoll::update()
{
    snap_ = proc_.getTransport();
    if (tempo_.update(snap_)) repaint();

    LiveFrame f;
    int drained = 0;
    while (drained < 20000 && proc_.popFrame(f))
    {
        ++drained;
        if (f.flags & LiveFrame::Jump)
        {
            // New pass (seek, loop wrap, play start): drop the old trace from here on so the
            // re-sung section is drawn fresh.
            auto it = std::lower_bound(history_.begin(), history_.end(), f.songTime - 1e-3,
                                       [](const Hist& h, double t) { return h.t < t; });
            history_.erase(it, history_.end());
            if (!history_.empty()) history_.push_back({ history_.back().t + 1e-6, 0.f }); // break the line
            continue;
        }
        latest_ = f;
        if (f.flags & LiveFrame::Voiced) latestWallMs_ = Time::getMillisecondCounterHiRes();
        if (f.flags & LiveFrame::Playing)
        {
            if (!history_.empty() && f.songTime < history_.back().t)
            {
                auto it = std::lower_bound(history_.begin(), history_.end(), f.songTime,
                                           [](const Hist& h, double t) { return h.t < t; });
                history_.erase(it, history_.end());
            }
            history_.push_back({ f.songTime, (f.flags & LiveFrame::Voiced) ? f.midi : 0.f });
        }
    }
    if (history_.size() > 400000) history_.erase(history_.begin(), history_.begin() + 100000);

    const bool playing = snap_.playing;
    if (playing && !wasPlaying_) { follow_ = true; manualPitch_ = false; } // re-engage follow on play
    wasPlaying_ = playing;

    auto v = makeView();
    const double now = nowTime();
    if (follow_ && (playing || now < viewStart_ || now > viewStart_ + v.span))
        viewStart_ = now - 0.35 * v.span;

    // Vertical window: follow the reference notes on screen (and the live pitch) when the
    // whole vocal range doesn't fit at a legible row height.
    if (!manualPitch_)
    {
        const auto notes = proc_.getReference().getNotes();
        int pLo = 1000, pHi = -1000;
        for (size_t i = firstNoteEndingAfter(notes, viewStart_ - v.offset); i < notes.size(); ++i)
        {
            if (notes[i].start + v.offset > viewStart_ + v.span) break;
            pLo = jmin(pLo, notes[i].pitch + v.transpose);
            pHi = jmax(pHi, notes[i].pitch + v.transpose);
        }
        double desired = pitchCentre_;
        const int rows = ui::rowsForPhrase(pLo, pHi);
        if (rows > 0) rowsWanted_ = rowsWanted_ <= 0.0 ? rows : rowsWanted_ + 0.08 * (rows - rowsWanted_);
        if (pHi >= pLo) desired = 0.5 * (pLo + pHi);
        else if (Time::getMillisecondCounterHiRes() - latestWallMs_ < 200.0 && latest_.midi > 0.f) desired = latest_.midi;
        else if (pitchCentre_ < v.rangeLo || pitchCentre_ > v.rangeHi) desired = 0.5 * (v.rangeLo + v.rangeHi);
        pitchCentre_ += 0.15 * (desired - pitchCentre_);
    }
    repaint();
}

void PianoRoll::scroll(int direction)
{
    const auto v = makeView();
    viewStart_ += ui::scrollStep(v.span, direction);
    follow_ = false;
    repaint();
}

PianoRoll::Target PianoRoll::targetAt(const View& v, const NoteList& notes, double displayTime, float midi) const
{
    Target t;
    const int idx = findActiveNote(notes, displayTime - v.offset);
    if (v.scalesGuide)
    {
        t.note = ui::nearestScaleNote(midi, v.key, v.scale);
        t.noteIndex = idx;
        return t;
    }
    if (idx < 0) return t;
    t.noteIndex = idx;
    t.note = notes[static_cast<size_t>(idx)].pitch + v.transpose;
    return t;
}

Readout PianoRoll::computeReadout() const
{
    Readout r;
    r.confidence = latest_.confidence;
    const bool fresh = Time::getMillisecondCounterHiRes() - latestWallMs_ < 200.0;
    if (!fresh || latest_.midi <= 0.f)
    {
        if (!fresh) r.confidence = 0.f;
        return r;
    }
    r.hasPitch = true;
    r.midi = latest_.midi;
    r.hz = static_cast<float>(midiToHz(latest_.midi));
    const auto v = makeView();
    const auto notes = proc_.getReference().getNotes();
    // While playing: the note at the (calibrated) frame time. While stopped: the note under
    // the playhead, so a single target can be practised with the transport parked.
    const double t = snap_.playing ? latest_.songTime + v.calib : snap_.songTime;
    const auto tgt = targetAt(v, notes, t, latest_.midi);
    if (tgt.note >= 0)
    {
        r.hasTarget = true;
        r.target = tgt.note;
        r.cents = static_cast<float>(centsFromNote(latest_.midi, r.target));
        r.status = classifyCents(r.cents, v.tolerance);
    }
    else
    {
        const int nearest = nearestNote(latest_.midi);
        r.target = nearest;
        r.cents = static_cast<float>(centsFromNote(latest_.midi, nearest));
        r.status = TuningStatus::NoPitch;
    }
    return r;
}

Rectangle<float> PianoRoll::noteRect(const View& v, const RefNote& n) const
{
    const float x0 = v.xForTime(n.start + v.offset), x1 = v.xForTime(n.end() + v.offset);
    const float yc = v.yForMidi(n.pitch + v.transpose);
    const float h = jlimit(6.f, 16.f, v.rowH() * 0.62f);
    return { x0, yc - h * 0.5f, jmax(3.f, x1 - x0), h };
}

int PianoRoll::hitTestNote(const View& v, const NoteList& notes, Point<float> p, bool& nearRight) const
{
    nearRight = false;
    if (!v.showNotes) return -1;
    for (int i = static_cast<int>(notes.size()) - 1; i >= 0; --i)
    {
        auto r = noteRect(v, notes[static_cast<size_t>(i)]);
        r = r.withSizeKeepingCentre(r.getWidth(), jmax(r.getHeight(), v.rowH() * 0.9f));
        if (r.expanded(0.f, 1.f).contains(p))
        {
            nearRight = r.getWidth() > 14.f && p.x > r.getRight() - 6.f;
            return i;
        }
    }
    return -1;
}

int PianoRoll::hitTestMarker(const View& v, Point<float> p) const
{
    if (!v.lane.contains(p)) return -1;
    const auto markers = proc_.getMarkers().getMarkers();
    for (int i = static_cast<int>(markers.size()) - 1; i >= 0; --i)
    {
        const float x = v.xForTime(markers[static_cast<size_t>(i)].time);
        const float w = 22.f + GlyphArrangement::getStringWidth(Font(theme::font(12.f)), markers[static_cast<size_t>(i)].name);
        if (p.x >= x - 3.f && p.x <= x + w) return i;
    }
    return -1;
}

void PianoRoll::resized()
{
    if (renameEditor_ != nullptr) beginRename(renameIndex_);
}

// ---- painting --------------------------------------------------------------------------------

void PianoRoll::paint(Graphics& g)
{
    const auto v = makeView();
    const auto notes = proc_.getReference().getNotes();
    const auto sel = proc_.getReference().getSelection();
    const float rowH = v.rowH();
    const double now = nowTime();

    g.fillAll(col::roll);

    // ---- rows ----------------------------------------------------------------------------
    for (int n = v.lo; n <= v.hi; ++n)
    {
        const float y = v.yForMidi(n) - rowH * 0.5f;
        Colour c = isBlackKey(n) ? col::rollDark : col::roll;
        const bool inScale = isInScale(n, v.key, v.scale);
        const bool root = ((n - v.key) % 12 + 12) % 12 == 0;
        if (v.scalesGuide)
            c = root ? Colour(0xff222544) : inScale ? Colour(0xff1b2230) : Colour(0xff12161e);
        else if (v.scale != ScaleType::Chromatic)
            // Notes mode: a faint key hint (in-scale rows a touch lighter, the tonic tinted).
            c = root ? c.interpolatedWith(Colour(0xff222544), 0.45f)
                     : inScale ? c.brighter(0.06f) : c.darker(0.12f);
        g.setColour(c);
        g.fillRect(v.area.getX(), y, v.area.getWidth(), rowH);
        g.setColour(((n % 12) + 12) % 12 == 0 ? col::gridBar : col::gridRow);
        g.drawHorizontalLine(roundToInt(y + rowH), v.area.getX(), v.area.getRight());
    }

    // ---- loop region -----------------------------------------------------------------------
    if (snap_.hasLoop && snap_.looping)
    {
        const float x0 = jmax(v.area.getX(), v.xForTime(snap_.loopStart));
        const float x1 = jmin(v.area.getRight(), v.xForTime(snap_.loopEnd));
        if (x1 > x0)
        {
            g.setColour(col::lavender.withAlpha(0.05f));
            g.fillRect(x0, v.area.getY(), x1 - x0, v.area.getHeight());
        }
    }

    // ---- beat / bar grid -------------------------------------------------------------------
    {
        const double beat = v.tempo->secondsPerBeatAt(v.start);
        const bool beatsVisible = v.area.getWidth() / (v.span / beat) > 5.0;
        for (const auto& l : v.tempo->beatLines(v.start, v.start + v.span))
        {
            if (!l.downbeat && !beatsVisible) continue;
            g.setColour(l.downbeat ? col::gridBar : col::gridBeat);
            g.drawVerticalLine(roundToInt(v.xForTime(l.time)), v.area.getY(), v.area.getBottom());
        }
    }

    // ---- live pitch trace (built first: it decides which note portions are out of tune) ----
    Path cyanPath, offPath, dimPath;
    struct BadSpan { int note; double t0, t1; };
    std::vector<BadSpan> bad;
    if (v.showTrace && !history_.empty())
    {
        auto it = std::lower_bound(history_.begin(), history_.end(), v.start - v.calib - 0.1,
                                   [](const Hist& h, double t) { return h.t < t; });
        const Hist* prev = nullptr;
        int openPath = -1;
        for (; it != history_.end(); ++it)
        {
            const double t = it->t + v.calib;
            if (t > v.start + v.span + 0.1) break;
            if (it->midi <= 0.f) { prev = nullptr; openPath = -1; continue; }
            const auto tgt = targetAt(v, notes, t, it->midi);
            int kind = 2; // 0 in tune, 1 off, 2 no target / not judged
            // Short grace period at each note onset: scoops into a note are not marked as errors.
            const bool onset = tgt.noteIndex >= 0 && !v.scalesGuide
                               && t - v.offset - notes[static_cast<size_t>(tgt.noteIndex)].start < kOnsetGraceSec;
            if (tgt.note >= 0 && !onset)
            {
                const auto st = classifyCents(centsFromNote(it->midi, tgt.note), v.tolerance);
                kind = st == TuningStatus::InTune ? 0 : 1;
                if (kind == 1 && tgt.noteIndex >= 0 && prev != nullptr)
                {
                    const double pt = prev->t + v.calib;
                    if (!bad.empty() && bad.back().note == tgt.noteIndex && pt - bad.back().t1 < 0.03)
                        bad.back().t1 = t;
                    else
                        bad.push_back({ tgt.noteIndex, pt, t });
                }
            }
            const float x = v.xForTime(t), y = v.yForMidi(it->midi);
            if (prev != nullptr && it->t - prev->t < 0.05)
            {
                auto& path = kind == 0 ? cyanPath : kind == 1 ? offPath : dimPath;
                if (kind != openPath) path.startNewSubPath(v.xForTime(prev->t + v.calib), v.yForMidi(prev->midi));
                path.lineTo(x, y);
                openPath = kind;
            }
            else
            {
                openPath = -1;
            }
            prev = &*it;
        }
    }

    g.saveState();
    g.reduceClipRegion(v.area.toNearestInt());

    // ---- reference notes -------------------------------------------------------------------
    if (v.showNotes)
    {
        const int activeIdx = findActiveNote(notes, now + v.calib - v.offset);
        for (size_t i = firstNoteEndingAfter(notes, v.start - v.offset); i < notes.size(); ++i)
        {
            const auto& n = notes[i];
            if (n.start + v.offset > v.start + v.span) break;
            const int pitch = n.pitch + v.transpose;
            const bool isSel = i < sel.size() && sel[i];
            auto r = noteRect(v, n);
            if (pitch < v.lo || pitch > v.hi)
            {
                // Outside the visible rows: small tab at the edge.
                const float y = pitch < v.lo ? v.area.getBottom() - 4.f : v.area.getY();
                g.setColour(col::noteFill.withAlpha(0.7f));
                g.fillRoundedRectangle(r.getX(), y, r.getWidth(), 4.f, 2.f);
                continue;
            }
            const bool active = static_cast<int>(i) == activeIdx;
            const float alpha = 0.55f + 0.45f * jlimit(0.f, 1.f, n.confidence);
            theme::glowRoundedRect(g, r, 3.f, col::lavender.withAlpha(alpha), active ? 9.f : 6.f);
            ColourGradient grad(col::lavenderHi.withAlpha(alpha), 0.f, r.getY(), col::noteFill.withAlpha(alpha), 0.f,
                                r.getBottom(), false);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(r, 3.f);
            g.setColour(Colours::white.withAlpha(active ? 0.55f : 0.3f));
            g.drawHorizontalLine(roundToInt(r.getY() + 1.f), r.getX() + 2.f, r.getRight() - 2.f);
            g.setColour(col::noteEdge.withAlpha(alpha));
            g.drawRoundedRectangle(r, 3.f, 1.f);
            if (isSel)
            {
                g.setColour(Colours::white.withAlpha(0.95f));
                g.drawRoundedRectangle(r.expanded(1.5f), 4.f, 1.5f);
            }
        }
        // Portions of notes sung outside the tolerance.
        for (const auto& s : bad)
        {
            if (s.note < 0 || s.note >= static_cast<int>(notes.size())) continue;
            const auto r = noteRect(v, notes[static_cast<size_t>(s.note)]);
            const float x0 = jmax(r.getX(), v.xForTime(s.t0)), x1 = jmin(r.getRight(), v.xForTime(s.t1));
            if (x1 - x0 < 1.f) continue;
            const auto br = Rectangle<float>(x0, r.getY(), x1 - x0, r.getHeight());
            theme::glowRoundedRect(g, br, 2.f, col::off, 5.f);
            g.setColour(col::off);
            g.fillRoundedRectangle(br, 2.f);
        }
    }

    // ---- trace -------------------------------------------------------------------------------
    if (v.showTrace)
    {
        const PathStrokeType glow(7.f, PathStrokeType::curved, PathStrokeType::rounded);
        const PathStrokeType line(2.f, PathStrokeType::curved, PathStrokeType::rounded);
        g.setColour(col::cyan.withAlpha(0.2f));
        g.strokePath(cyanPath, glow);
        g.strokePath(dimPath, glow);
        g.setColour(col::off.withAlpha(0.18f));
        g.strokePath(offPath, glow);
        g.setColour(col::cyan.withAlpha(0.85f));
        g.strokePath(dimPath, line);
        g.setColour(col::cyan);
        g.strokePath(cyanPath, line);
        g.setColour(col::off);
        g.strokePath(offPath, PathStrokeType(2.5f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    g.restoreState();

    // ---- playhead + live marker ------------------------------------------------------------
    const float px = v.xForTime(now);
    const bool playheadVisible = px >= v.area.getX() && px <= v.area.getRight();
    if (playheadVisible)
    {
        g.setColour(Colours::white.withAlpha(0.08f));
        g.fillRect(px - 3.f, v.lane.getY(), 6.f, v.area.getBottom() - v.lane.getY());
        g.setColour(Colours::white.withAlpha(0.9f));
        g.fillRect(px - 0.75f, v.lane.getY() + 8.f, 1.5f, v.area.getBottom() - v.lane.getY() - 8.f);
    }
    const auto readout = computeReadout();
    if (readout.hasPitch && v.showTrace && readout.midi >= v.lo - 0.5f && readout.midi <= v.hi + 0.5f)
    {
        const float y = v.yForMidi(readout.midi);
        const float x = jlimit(v.area.getX() + 6.f, v.area.getRight() - 6.f, px);
        const auto c = readout.hasTarget ? statusColour(readout.status) : col::cyan;
        g.setColour(c.withAlpha(0.25f));
        g.fillEllipse(x - 9.f, y - 9.f, 18.f, 18.f);
        g.setColour(c);
        g.fillEllipse(x - 4.5f, y - 4.5f, 9.f, 9.f);
    }

    paintKeyboard(g, v, readout);
    paintRuler(g, v);
    paintLane(g, v);

    if (playheadVisible)
    {
        Path tri;
        const float ty = v.lane.getY() + 2.f;
        tri.addTriangle(px - 6.f, ty, px + 6.f, ty, px, ty + 9.f);
        g.setColour(Colours::white);
        g.fillPath(tri);
    }

    // ---- overlays --------------------------------------------------------------------------
    if (!rubber_.isEmpty())
    {
        g.setColour(col::lavender.withAlpha(0.12f));
        g.fillRect(rubber_);
        g.setColour(col::lavender.withAlpha(0.7f));
        g.drawRect(rubber_, 1.f);
    }
    if (notes.empty() && v.showNotes)
    {
        g.setColour(col::textDim);
        g.setFont(theme::font(14.f));
        g.drawFittedText("No reference notes yet.\nDrop an isolated lead-vocal stem (then Analyze Vocal) or a MIDI file here,\n"
                         "or use the folder button / settings > Import MIDI.",
                         v.area.reduced(20.f).toNearestInt(), Justification::centred, 4);
    }
    if (!snap_.playing)
    {
        g.setColour(col::textFaint);
        g.setFont(theme::font(11.5f));
        g.drawText("Stopped: live pitch is compared with the note under the playhead",
                   v.area.reduced(10.f, 6.f).toNearestInt(), Justification::topRight, false);
    }
    g.setColour(col::divider);
    g.drawHorizontalLine(getHeight() - 1, 0.f, static_cast<float>(getWidth()));
}

void PianoRoll::paintRuler(Graphics& g, const View& v)
{
    auto full = v.ruler.withLeft(0.f);
    g.setColour(col::ruler);
    g.fillRect(full);
    g.setColour(col::divider);
    g.drawHorizontalLine(roundToInt(full.getBottom()) - 1, 0.f, full.getRight());

    // loop region band
    if (snap_.hasLoop && snap_.looping)
    {
        const float x0 = jmax(v.ruler.getX(), v.xForTime(snap_.loopStart));
        const float x1 = jmin(v.ruler.getRight(), v.xForTime(snap_.loopEnd));
        if (x1 > x0)
        {
            g.setColour(col::lavender.withAlpha(0.22f));
            g.fillRect(x0, v.ruler.getBottom() - 5.f, x1 - x0, 4.f);
        }
    }

    g.saveState();
    g.reduceClipRegion(v.ruler.toNearestInt());
    const auto bb = v.tempo->barBeatAt(v.start);
    const double barSec = v.tempo->timeOfBar(bb.bar + 1) - v.tempo->timeOfBar(bb.bar);
    const float barPx = static_cast<float>(barSec / v.span * v.area.getWidth());
    int labelEvery = 1;
    while (barPx * labelEvery < 34.f && labelEvery < 64) labelEvery *= 2;
    for (const auto& l : v.tempo->barLines(v.start - barSec, v.start + v.span))
    {
        if (l.bar < 1) continue;
        const int bar = l.bar;
        const float x = v.xForTime(l.time);
        const bool labelled = (bar - 1) % labelEvery == 0;
        g.setColour(labelled ? col::textDim.withAlpha(0.7f) : col::gridBar);
        g.drawVerticalLine(roundToInt(x), v.ruler.getY() + (labelled ? 6.f : 14.f), v.ruler.getBottom());
        if (labelled)
        {
            g.setColour(col::text.withAlpha(0.85f));
            g.setFont(theme::font(12.5f, theme::Weight::Medium));
            g.drawText(String(bar), Rectangle<float>(x + 6.f, v.ruler.getY(), 40.f, v.ruler.getHeight()),
                       Justification::centredLeft, false);
        }
    }
    // beat ticks
    if (barPx > 40.f)
        for (const auto& l : v.tempo->beatLines(v.start, v.start + v.span))
            if (!l.downbeat)
            {
                g.setColour(col::gridBar);
                g.drawVerticalLine(roundToInt(v.xForTime(l.time)), v.ruler.getBottom() - 5.f, v.ruler.getBottom());
            }
    g.restoreState();
}

void PianoRoll::paintLane(Graphics& g, const View& v)
{
    auto full = v.lane.withLeft(0.f);
    g.setColour(col::lane);
    g.fillRect(full);
    g.setColour(col::divider);
    g.drawHorizontalLine(roundToInt(full.getBottom()) - 1, 0.f, full.getRight());

    const auto markers = proc_.getMarkers().getMarkers();
    g.saveState();
    g.reduceClipRegion(v.lane.toNearestInt());
    if (markers.empty())
    {
        g.setColour(col::textFaint);
        g.setFont(theme::font(11.5f));
        g.drawText("Double-click here to add a section marker", v.lane.reduced(10.f, 0.f), Justification::centredLeft, false);
    }
    const auto labelFont = theme::font(12.f);
    for (size_t i = 0; i < markers.size(); ++i)
    {
        const auto& m = markers[i];
        const float x = v.xForTime(m.time);
        const float nextX = i + 1 < markers.size() ? v.xForTime(markers[i + 1].time) : v.lane.getRight() + 40.f;
        if (nextX < v.lane.getX() - 4.f || x > v.lane.getRight()) continue;
        const float cy = v.lane.getCentreY();
        Rectangle<float> badge(x, cy - 8.f, 16.f, 16.f);
        g.setColour(col::badge);
        g.fillRoundedRectangle(badge, 3.f);
        g.setColour(col::lavenderHi);
        g.setFont(theme::font(11.f, theme::Weight::SemiBold));
        g.drawText(MarkerModel::letterFor(static_cast<int>(i)), badge, Justification::centred, false);
        if (static_cast<int>(i) == renameIndex_ && renameEditor_ != nullptr) continue;
        const float tw = GlyphArrangement::getStringWidth(Font(labelFont), m.name);
        g.setColour(col::text.withAlpha(0.8f));
        g.setFont(labelFont);
        g.drawText(m.name, Rectangle<float>(badge.getRight() + 6.f, v.lane.getY(), tw + 4.f, v.lane.getHeight()),
                   Justification::centredLeft, false);
        // arrow line to the next section
        const float lx0 = badge.getRight() + 14.f + tw, lx1 = jmin(nextX, v.lane.getRight() + 20.f) - 12.f;
        if (lx1 - lx0 > 20.f)
        {
            g.setColour(col::textDim.withAlpha(0.6f));
            g.drawHorizontalLine(roundToInt(cy), lx0, lx1);
            Path head;
            head.startNewSubPath(lx1 - 5.f, cy - 3.5f);
            head.lineTo(lx1, cy);
            head.lineTo(lx1 - 5.f, cy + 3.5f);
            g.strokePath(head, PathStrokeType(1.f));
        }
    }
    g.restoreState();
}

void PianoRoll::paintKeyboard(Graphics& g, const View& v, const Readout& readout)
{
    auto keys = v.keys;
    g.setColour(col::whiteKey);
    g.fillRect(keys);
    const float rowH = v.rowH();
    const int sung = readout.hasPitch ? nearestNote(readout.midi) : -1000;
    const float blackW = keys.getWidth() * 0.55f;
    g.saveState();
    g.reduceClipRegion(keys.toNearestInt());
    for (int n = v.lo - 1; n <= v.hi + 1; ++n)
    {
        const float y = v.yForMidi(n) - rowH * 0.5f;
        const int pc = ((n % 12) + 12) % 12;
        const Rectangle<float> row(keys.getX(), y, keys.getWidth(), rowH);
        if (isBlackKey(n))
        {
            g.setColour(Colour(0xffb4b7c0));
            g.drawHorizontalLine(roundToInt(row.getCentreY()), keys.getX() + blackW, keys.getRight());
            g.setColour(n == sung ? statusColour(readout.status).darker(0.4f) : col::blackKey);
            g.fillRoundedRectangle(row.withWidth(blackW).reduced(0.f, 0.5f), 2.f);
        }
        else
        {
            if (pc == 4 || pc == 11) // E|F and B|C boundaries
            {
                g.setColour(Colour(0xffb4b7c0));
                g.drawHorizontalLine(roundToInt(row.getY()), keys.getX(), keys.getRight());
            }
            if (n == sung)
            {
                g.setColour((readout.hasTarget ? statusColour(readout.status) : col::cyan).withAlpha(0.45f));
                g.fillRect(row.withTrimmedLeft(blackW));
            }
            if (rowH >= 11.f || pc == 0)
            {
                g.setColour(Colour(0xff1b2030).withAlpha(pc == 0 ? 0.95f : 0.75f));
                g.setFont(theme::font(jmin(11.5f, jmax(8.f, rowH * 0.8f)), pc == 0 ? theme::Weight::SemiBold : theme::Weight::Medium));
                g.drawText(String(noteName(n, v.conv)), row.withTrimmedLeft(blackW - 4.f).withTrimmedRight(5.f),
                           Justification::centredRight, false);
            }
        }
        if (readout.hasTarget && n == readout.target)
        {
            g.setColour(col::lavender);
            g.fillRect(row.withLeft(keys.getRight() - 3.f));
        }
    }
    g.restoreState();
    g.setColour(col::ruler);
    g.drawVerticalLine(roundToInt(keys.getRight()) - 1, keys.getY(), keys.getBottom());
}

// ---- mouse editing ------------------------------------------------------------------------

void PianoRoll::mouseDown(const MouseEvent& e)
{
    grabKeyboardFocus();
    const auto v = makeView();
    auto& model = proc_.getReference();
    downPos_ = e.position;
    viewStartAtDown_ = viewStart_;
    gestureStarted_ = false;
    rubber_ = {};
    drag_ = ui::RollAction::None;
    markerDrag_ = -1;
    createdIndex_ = -1;

    // ---- marker lane ----
    if (v.lane.contains(e.position))
    {
        const int mi = hitTestMarker(v, e.position);
        if (e.mods.isPopupMenu())
        {
            PopupMenu m;
            const double t = v.timeForX(e.position.x);
            if (mi >= 0)
            {
                m.addItem(1, "Rename section...");
                m.addItem(2, "Delete section");
            }
            m.addItem(3, "Add section marker here");
            SafePointer<PianoRoll> safe(this);
            m.showMenuAsync(PopupMenu::Options().withTargetScreenArea(Rectangle<int>(e.getScreenPosition(), e.getScreenPosition()).expanded(1)),
                            [safe, mi, t](int r) {
                                if (safe == nullptr) return;
                                auto& mm = safe->proc_.getMarkers();
                                if (r == 1) safe->beginRename(mi);
                                else if (r == 2) mm.remove(mi);
                                else if (r == 3) safe->beginRename(mm.add(t, mm.defaultNameFor(mm.size())));
                                safe->repaint();
                            });
            return;
        }
        if (mi >= 0)
        {
            markerDrag_ = mi;
            markerDownTime_ = proc_.getMarkers().getMarkers()[static_cast<size_t>(mi)].time;
        }
        return;
    }

    ui::PressInfo press;
    press.onRuler = v.ruler.withLeft(0.f).contains(e.position);
    press.middleButton = e.mods.isMiddleButtonDown();
    if (!press.onRuler && !press.middleButton && !v.area.contains(e.position)) return; // keyboard
    press.alt = e.mods.isAltDown();
    press.shift = e.mods.isShiftDown();
    press.cmd = e.mods.isCommandDown();
    const auto notes = model.getNotes();
    bool nearRight = false;
    const int idx = press.onRuler ? -1 : hitTestNote(v, notes, e.position, nearRight);
    press.onNote = idx >= 0;
    press.nearRightEdge = nearRight;
    drag_ = ui::actionForPress(press);

    if (!v.showNotes && drag_ != ui::RollAction::Pan)
        drag_ = ui::RollAction::Pan; // reference hidden: dragging just scrolls

    switch (drag_)
    {
        case ui::RollAction::Move:
        case ui::RollAction::Stretch:
        case ui::RollAction::ToggleSelect:
        {
            const auto sel = model.getSelection();
            if (drag_ == ui::RollAction::ToggleSelect)
            {
                model.toggleSelected(idx);
                drag_ = ui::RollAction::Move;
            }
            else if (!sel[static_cast<size_t>(idx)])
            {
                model.selectOnly(idx);
            }
            anchor_ = idx;
            dragOrig_ = model.getNotes();
            dragSel_ = model.getSelection();
            break;
        }
        case ui::RollAction::Rubber:
            model.clearSelection();
            break;
        case ui::RollAction::RubberAdd:
        case ui::RollAction::Create:
        case ui::RollAction::Pan:
        case ui::RollAction::None:
        default:
            break;
    }
}

void PianoRoll::mouseDrag(const MouseEvent& e)
{
    const auto v = makeView();
    auto& model = proc_.getReference();
    const float dx = e.position.x - downPos_.x, dy = e.position.y - downPos_.y;
    const double pps = v.area.getWidth() / v.span;

    if (markerDrag_ >= 0)
    {
        if (!gestureStarted_ && std::abs(dx) < 3.f) return;
        gestureStarted_ = true;
        markerDrag_ = proc_.getMarkers().move(markerDrag_, markerDownTime_ + dx / pps);
        repaint();
        return;
    }

    switch (drag_)
    {
        case ui::RollAction::Pan:
            follow_ = false;
            viewStart_ = viewStartAtDown_ - dx / pps;
            repaint();
            break;

        case ui::RollAction::Move:
        case ui::RollAction::Stretch:
        {
            if (!gestureStarted_)
            {
                if (std::abs(dx) < 3.f && std::abs(dy) < 3.f) return;
                model.beginGesture();
                gestureStarted_ = true;
                follow_ = false;
            }
            const double dt = dx / pps;
            for (size_t i = 0; i < dragOrig_.size() && i < dragSel_.size(); ++i)
            {
                if (!dragSel_[i]) continue;
                auto n = dragOrig_[i];
                if (drag_ == ui::RollAction::Move)
                {
                    n.start = jmax(0.0, n.start + dt);
                    n.pitch = jlimit(0, 127, n.pitch + roundToInt(-dy / v.rowH()));
                }
                else
                {
                    n.length = jmax(0.02, n.length + dt);
                }
                model.setNote(static_cast<int>(i), n);
            }
            repaint();
            break;
        }

        case ui::RollAction::Create:
        {
            if (!gestureStarted_)
            {
                if (std::abs(dx) < 3.f) return;
                gestureStarted_ = true;
                follow_ = false;
            }
            const auto span = ui::createdSpan(v.timeForX(downPos_.x) - v.offset, v.timeForX(e.position.x) - v.offset);
            RefNote n;
            n.start = span.start;
            n.length = span.length;
            n.pitch = jlimit(0, 127, roundToInt(v.midiForY(downPos_.y)) - v.transpose);
            n.confidence = 1.f;
            if (createdIndex_ < 0) createdIndex_ = model.addNote(n); // undoable, selects it
            else model.setNote(createdIndex_, n);
            repaint();
            break;
        }

        case ui::RollAction::Rubber:
        case ui::RollAction::RubberAdd:
        {
            rubber_ = Rectangle<float>(downPos_, e.position).getIntersection(v.area);
            const double t0 = v.timeForX(rubber_.getX()) - v.offset, t1 = v.timeForX(rubber_.getRight()) - v.offset;
            const int pHi = static_cast<int>(std::floor(v.midiForY(rubber_.getY()) + 0.5)) - v.transpose;
            const int pLo = static_cast<int>(std::ceil(v.midiForY(rubber_.getBottom()) - 0.5)) - v.transpose;
            model.selectInRange(t0, t1, pLo, pHi, drag_ == ui::RollAction::RubberAdd);
            repaint();
            break;
        }

        case ui::RollAction::ToggleSelect:
        case ui::RollAction::None:
        default:
            break;
    }
}

void PianoRoll::mouseUp(const MouseEvent& e)
{
    auto& model = proc_.getReference();
    const auto v = makeView();
    if (markerDrag_ < 0 && !gestureStarted_ && !e.mouseWasDraggedSinceMouseDown())
    {
        if (drag_ == ui::RollAction::Create)
        {
            // Option-click: a one-beat note at the click.
            RefNote n;
            n.start = jmax(0.0, v.timeForX(e.position.x) - v.offset);
            n.length = v.tempo->secondsPerBeatAt(n.start + v.offset);
            n.pitch = jlimit(0, 127, roundToInt(v.midiForY(e.position.y)) - v.transpose);
            n.confidence = 1.f;
            model.addNote(n);
        }
        else if (drag_ == ui::RollAction::Stretch && e.mods.isShiftDown() && anchor_ >= 0)
        {
            model.toggleSelected(anchor_); // Shift-click without dragging toggles selection
        }
    }
    if (gestureStarted_ && drag_ != ui::RollAction::Pan && markerDrag_ < 0) model.sortKeepingSelection();
    gestureStarted_ = false;
    drag_ = ui::RollAction::None;
    markerDrag_ = -1;
    createdIndex_ = -1;
    rubber_ = {};
    repaint();
}

void PianoRoll::mouseMove(const MouseEvent& e)
{
    const auto v = makeView();
    if (v.lane.contains(e.position))
    {
        setMouseCursor(hitTestMarker(v, e.position) >= 0 ? MouseCursor::DraggingHandCursor : MouseCursor::NormalCursor);
        return;
    }
    if (v.ruler.withLeft(0.f).contains(e.position)) { setMouseCursor(MouseCursor::LeftRightResizeCursor); return; }
    bool nearRight = false;
    const int idx = hitTestNote(v, proc_.getReference().getNotes(), e.position, nearRight);
    if (idx >= 0)
        setMouseCursor((nearRight || e.mods.isShiftDown()) ? MouseCursor::LeftRightResizeCursor : MouseCursor::DraggingHandCursor);
    else
        setMouseCursor(e.mods.isAltDown() && v.area.contains(e.position) ? MouseCursor::CrosshairCursor : MouseCursor::NormalCursor);
}

void PianoRoll::mouseDoubleClick(const MouseEvent& e)
{
    const auto v = makeView();
    if (v.lane.contains(e.position))
    {
        auto& mm = proc_.getMarkers();
        const int mi = hitTestMarker(v, e.position);
        if (mi >= 0) beginRename(mi);
        else beginRename(mm.add(v.timeForX(e.position.x), mm.defaultNameFor(mm.size())));
        return;
    }
    if (!v.area.contains(e.position)) return;
    bool nearRight = false;
    const int idx = hitTestNote(v, proc_.getReference().getNotes(), e.position, nearRight);
    if (ui::actionForDoubleClick(idx >= 0) == ui::DoubleClickAction::DeleteNote)
    {
        auto& model = proc_.getReference();
        model.selectOnly(idx);
        model.deleteSelected();
        repaint();
    }
}

void PianoRoll::mouseWheelMove(const MouseEvent& e, const MouseWheelDetails& w)
{
    auto v = makeView();
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        // ⌘ + scroll: zoom time around the mouse.
        auto* p = proc_.getApvts().getParameter(params::viewSeconds);
        const double tMouse = v.timeForX(e.position.x);
        const float delta = std::abs(w.deltaY) >= std::abs(w.deltaX) ? w.deltaY : w.deltaX;
        const auto newSpan = static_cast<float>(ui::zoomedSpan(v.span, delta));
        p->setValueNotifyingHost(p->convertTo0to1(newSpan));
        viewStart_ = tMouse - (e.position.x - v.area.getX()) / v.area.getWidth() * newSpan;
        follow_ = false;
    }
    else if (e.mods.isShiftDown() && std::abs(w.deltaY) > std::abs(w.deltaX) && v.hi - v.lo < v.rangeHi - v.rangeLo)
    {
        // Shift + vertical scroll: move the visible pitch window (when the range doesn't fit).
        manualPitch_ = true;
        pitchCentre_ = jlimit(static_cast<double>(v.rangeLo), static_cast<double>(v.rangeHi),
                              pitchCentre_ + (w.deltaY > 0 ? 1.0 : -1.0) * jmax(1.0, (v.hi - v.lo) * 0.15));
    }
    else
    {
        const float d = std::abs(w.deltaX) > std::abs(w.deltaY) ? -w.deltaX : w.deltaY;
        viewStart_ -= d * v.span * 0.25;
        follow_ = false;
    }
    repaint();
}

// ---- markers --------------------------------------------------------------------------------

void PianoRoll::addMarkerAtPlayhead()
{
    auto& mm = proc_.getMarkers();
    beginRename(mm.add(nowTime(), mm.defaultNameFor(mm.size())));
}

void PianoRoll::beginRename(int index)
{
    const auto markers = proc_.getMarkers().getMarkers();
    if (index < 0 || index >= static_cast<int>(markers.size())) { renameEditor_.reset(); renameIndex_ = -1; return; }
    const auto v = makeView();
    renameIndex_ = index;
    if (renameEditor_ == nullptr)
    {
        renameEditor_ = std::make_unique<TextEditor>();
        renameEditor_->setFont(theme::font(12.f));
        renameEditor_->setIndents(4, 3);
        addAndMakeVisible(*renameEditor_);
        SafePointer<PianoRoll> safe(this);
        auto commit = [safe](bool apply) {
            if (safe == nullptr || safe->renameEditor_ == nullptr) return;
            if (apply) safe->proc_.getMarkers().rename(safe->renameIndex_, safe->renameEditor_->getText());
            safe->renameIndex_ = -1;
            MessageManager::callAsync([safe] { if (safe != nullptr) { safe->renameEditor_.reset(); safe->repaint(); } });
        };
        renameEditor_->onReturnKey = [commit] { commit(true); };
        renameEditor_->onEscapeKey = [commit] { commit(false); };
        renameEditor_->onFocusLost = [commit] { commit(true); };
    }
    renameEditor_->setText(markers[static_cast<size_t>(index)].name, false);
    const float x = v.xForTime(markers[static_cast<size_t>(index)].time) + 20.f;
    renameEditor_->setBounds(Rectangle<float>(jlimit(v.lane.getX(), jmax(v.lane.getX(), v.lane.getRight() - 160.f), x),
                                              v.lane.getY() + 3.f, 150.f, v.lane.getHeight() - 6.f).toNearestInt());
    renameEditor_->selectAll();
    renameEditor_->grabKeyboardFocus();
    repaint();
}

bool PianoRoll::handleKey(const KeyPress& key)
{
    auto& model = proc_.getReference();
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();
    if (code == KeyPress::deleteKey || code == KeyPress::backspaceKey) { model.deleteSelected(); return true; }
    if (code == KeyPress::upKey)    { model.nudgeSelected(mods.isShiftDown() ? 12 : 1, 0.0); return true; }
    if (code == KeyPress::downKey)  { model.nudgeSelected(mods.isShiftDown() ? -12 : -1, 0.0); return true; }
    if (code == KeyPress::leftKey)  { model.nudgeSelected(0, mods.isShiftDown() ? -0.1 : -0.01); return true; }
    if (code == KeyPress::rightKey) { model.nudgeSelected(0, mods.isShiftDown() ? 0.1 : 0.01); return true; }
    if (code == KeyPress::escapeKey) { model.clearSelection(); return true; }
    if (mods.isCommandDown() && (code == 'A' || code == 'a')) { model.selectAll(); return true; }
    if (mods.isCommandDown() && (code == 'Z' || code == 'z'))
    {
        if (mods.isShiftDown()) model.redo(); else model.undo();
        return true;
    }
    return false;
}

} // namespace pitchlane
