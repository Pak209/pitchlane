#include "PianoRoll.h"

#include "Params.h"
#include "PluginProcessor.h"

namespace pitchlane {

using namespace juce;

namespace colours {
const Colour background { 0xff14161b };
const Colour rowInScale { 0xff1f232b };
const Colour rowOutScale { 0xff16181d };
const Colour rowRoot { 0xff262b35 };
const Colour gridBeat { 0x14ffffff };
const Colour gridBar { 0x30ffffff };
const Colour note { 0xff3b82f6 };
const Colour noteActive { 0xff60a5fa };
const Colour selected { 0xfffacc15 };
const Colour inTune { 0xff34d399 };
const Colour flat { 0xff38bdf8 };
const Colour sharp { 0xfffb923c };
const Colour noTarget { 0xffe5e7eb };
const Colour playhead { 0xccffffff };
const Colour loop { 0x1822c55e };
} // namespace colours

Colour PianoRoll::statusColour(TuningStatus s)
{
    switch (s)
    {
        case TuningStatus::InTune: return colours::inTune;
        case TuningStatus::Flat:   return colours::flat;
        case TuningStatus::Sharp:  return colours::sharp;
        case TuningStatus::NoPitch:
        default:                   return colours::noTarget;
    }
}

PianoRoll::PianoRoll(PitchLaneProcessor& p) : proc_(p)
{
    setWantsKeyboardFocus(true);
    setOpaque(true);
    history_.reserve(1 << 16);
}

PianoRoll::View PianoRoll::makeView() const
{
    auto& ap = proc_.getApvts();
    auto get = [&](const char* id) { return ap.getRawParameterValue(id)->load(); };
    View v;
    v.lo = roundToInt(get(params::lowNote));
    v.hi = roundToInt(get(params::highNote));
    if (v.hi < v.lo + 11) v.hi = v.lo + 11; // at least an octave
    v.span = get(params::viewSeconds);
    v.start = viewStart_;
    v.offset = get(params::refOffset) * 0.001;
    v.calib = get(params::calibration) * 0.001;
    v.transpose = roundToInt(get(params::transpose));
    v.tolerance = get(params::tolerance);
    v.key = roundToInt(get(params::key));
    v.scale = static_cast<ScaleType>(roundToInt(get(params::scale)));
    v.conv = roundToInt(get(params::noteNames)) == 1 ? OctaveConvention::Yamaha : OctaveConvention::Scientific;
    v.area = getLocalBounds().toFloat().withTrimmedLeft(v.keyboardW);
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
                // Out-of-order (should not happen without a Jump marker): truncate defensively.
                auto it = std::lower_bound(history_.begin(), history_.end(), f.songTime,
                                           [](const Hist& h, double t) { return h.t < t; });
                history_.erase(it, history_.end());
            }
            history_.push_back({ f.songTime, (f.flags & LiveFrame::Voiced) ? f.midi : 0.f });
        }
    }
    if (history_.size() > 400000) history_.erase(history_.begin(), history_.begin() + 100000);

    const bool playing = snap_.playing;
    if (playing && !wasPlaying_) follow_ = true; // re-engage follow when playback starts
    wasPlaying_ = playing;

    const auto v = makeView();
    const double now = nowTime();
    if (follow_ && (playing || now < viewStart_ || now > viewStart_ + v.span))
        viewStart_ = now - 0.35 * v.span;

    repaint();
}

TuningStatus PianoRoll::statusAt(const View& v, const NoteList& notes, double displayTime, float midi) const
{
    const int idx = findActiveNote(notes, displayTime - v.offset);
    if (idx < 0) return TuningStatus::NoPitch;
    const double cents = centsFromNote(midi, notes[static_cast<size_t>(idx)].pitch + v.transpose);
    return classifyCents(cents, v.tolerance);
}

Readout PianoRoll::computeReadout() const
{
    Readout r;
    const bool fresh = Time::getMillisecondCounterHiRes() - latestWallMs_ < 200.0;
    if (!fresh || latest_.midi <= 0.f) return r;
    r.hasPitch = true;
    r.midi = latest_.midi;
    r.hz = static_cast<float>(midiToHz(latest_.midi));
    const auto v = makeView();
    const auto notes = proc_.getReference().getNotes();
    // While playing: the note at the (calibrated) frame time. While stopped: the note under
    // the playhead, so a single target can be practised with the transport parked.
    const double t = snap_.playing ? latest_.songTime + v.calib : snap_.songTime;
    const int idx = findActiveNote(notes, t - v.offset);
    if (idx >= 0)
    {
        r.hasTarget = true;
        r.target = notes[static_cast<size_t>(idx)].pitch + v.transpose;
        r.cents = static_cast<float>(centsFromNote(latest_.midi, r.target));
        // Far away (e.g. an octave off): show it, but it is still just "flat"/"sharp".
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

int PianoRoll::hitTestNote(const View& v, const NoteList& notes, Point<float> p, bool& nearRight) const
{
    nearRight = false;
    for (int i = static_cast<int>(notes.size()) - 1; i >= 0; --i)
    {
        const auto& n = notes[static_cast<size_t>(i)];
        const int pitch = n.pitch + v.transpose;
        const float x0 = v.xForTime(n.start + v.offset), x1 = v.xForTime(n.end() + v.offset);
        const float yc = v.yForMidi(pitch), h = jmax(6.f, v.rowH());
        Rectangle<float> r(x0, yc - h * 0.5f, jmax(3.f, x1 - x0), h);
        if (r.expanded(0.f, 1.f).contains(p))
        {
            nearRight = r.getWidth() > 12.f && p.x > r.getRight() - 6.f;
            return i;
        }
    }
    return -1;
}

void PianoRoll::paint(Graphics& g)
{
    const auto v = makeView();
    const auto notes = proc_.getReference().getNotes();
    const auto sel = proc_.getReference().getSelection();
    const float rowH = v.rowH();
    const double now = nowTime();

    g.fillAll(colours::background);

    // ---- rows ----------------------------------------------------------------------------
    for (int n = v.lo; n <= v.hi; ++n)
    {
        const float y = v.yForMidi(n) - rowH * 0.5f;
        const bool inScale = isInScale(n, v.key, v.scale);
        const bool root = ((n - v.key) % 12 + 12) % 12 == 0;
        g.setColour(root ? colours::rowRoot : (inScale ? colours::rowInScale : colours::rowOutScale));
        g.fillRect(v.area.getX(), y, v.area.getWidth(), rowH);
        if (n % 12 == 0)
        {
            g.setColour(Colour(0x28ffffff));
            g.drawHorizontalLine(roundToInt(y + rowH), v.area.getX(), v.area.getRight());
        }
    }

    // ---- loop region -----------------------------------------------------------------------
    if (snap_.hasLoop && snap_.looping)
    {
        const float x0 = jmax(v.area.getX(), v.xForTime(snap_.loopStart));
        const float x1 = jmin(v.area.getRight(), v.xForTime(snap_.loopEnd));
        if (x1 > x0)
        {
            g.setColour(colours::loop);
            g.fillRect(x0, v.area.getY(), x1 - x0, v.area.getHeight());
        }
    }

    // ---- beat grid ---------------------------------------------------------------------------
    {
        const double bpm = snap_.bpm > 1.0 ? snap_.bpm : 120.0;
        const double beat = 60.0 / bpm;
        if (v.area.getWidth() / (v.span / beat) > 4.0)
        {
            const auto first = static_cast<long long>(std::ceil(v.start / beat));
            for (long long b = first; b * beat <= v.start + v.span; ++b)
            {
                const float x = v.xForTime(static_cast<double>(b) * beat);
                g.setColour(b % 4 == 0 ? colours::gridBar : colours::gridBeat);
                g.drawVerticalLine(roundToInt(x), v.area.getY(), v.area.getBottom());
            }
        }
    }

    // ---- reference notes -------------------------------------------------------------------
    const int activeIdx = findActiveNote(notes, now + v.calib - v.offset);
    for (size_t i = firstNoteEndingAfter(notes, v.start - v.offset); i < notes.size(); ++i)
    {
        const auto& n = notes[i];
        if (n.start + v.offset > v.start + v.span) break;
        const int pitch = n.pitch + v.transpose;
        const float x0 = v.xForTime(n.start + v.offset), x1 = v.xForTime(n.end() + v.offset);
        const bool isSel = i < sel.size() && sel[i];
        if (pitch < v.lo || pitch > v.hi)
        {
            // Out of the visible range: small arrow tab at the edge.
            const float y = pitch < v.lo ? v.area.getBottom() - 4.f : v.area.getY();
            g.setColour(colours::note.withAlpha(0.8f));
            g.fillRect(x0, y, jmax(3.f, x1 - x0), 4.f);
            continue;
        }
        const float yc = v.yForMidi(pitch);
        const float h = jmax(4.f, rowH - 2.f);
        Rectangle<float> r(x0, yc - h * 0.5f, jmax(3.f, x1 - x0), h);
        const bool active = static_cast<int>(i) == activeIdx;
        const float alpha = 0.35f + 0.65f * jlimit(0.f, 1.f, n.confidence);
        // Tolerance band (the "in tune" zone) around the exact target pitch.
        const float band = static_cast<float>(v.tolerance / 100.0) * rowH;
        g.setColour((active ? colours::noteActive : colours::note).withAlpha(alpha * (active ? 1.f : 0.8f)));
        g.fillRoundedRectangle(r, 3.f);
        g.setColour(Colours::white.withAlpha(active ? 0.35f : 0.18f));
        g.fillRect(r.getX(), yc - band, r.getWidth(), 2.f * band);
        g.setColour(Colours::white.withAlpha(0.6f));
        g.drawHorizontalLine(roundToInt(yc), r.getX(), r.getRight());
        if (isSel)
        {
            g.setColour(colours::selected);
            g.drawRoundedRectangle(r.expanded(1.f), 3.f, 2.f);
        }
        if (r.getWidth() > 26.f && h >= 11.f)
        {
            g.setColour(Colours::white.withAlpha(0.85f));
            g.setFont(FontOptions(jmin(12.f, h - 1.f)));
            g.drawText(String(noteName(pitch, v.conv)), r.reduced(4.f, 0.f), Justification::centredLeft, false);
        }
    }

    // ---- live pitch trace ------------------------------------------------------------------
    if (!history_.empty())
    {
        Path paths[4]; // NoPitch(no target), Flat, InTune, Sharp
        auto it = std::lower_bound(history_.begin(), history_.end(), v.start - v.calib - 0.1,
                                   [](const Hist& h, double t) { return h.t < t; });
        const Hist* prev = nullptr;
        int openPath = -1; // path whose current sub-path ends at `prev`
        for (; it != history_.end(); ++it)
        {
            const double t = it->t + v.calib;
            if (t > v.start + v.span + 0.1) break;
            if (it->midi <= 0.f) { prev = nullptr; openPath = -1; continue; }
            const int st = static_cast<int>(statusAt(v, notes, t, it->midi));
            const float x = v.xForTime(t), y = v.yForMidi(it->midi);
            if (prev != nullptr && it->t - prev->t < 0.05)
            {
                auto& path = paths[st];
                if (st != openPath)
                    path.startNewSubPath(v.xForTime(prev->t + v.calib), v.yForMidi(prev->midi));
                path.lineTo(x, y);
                openPath = st;
            }
            else
            {
                openPath = -1;
            }
            prev = &*it;
        }
        g.saveState();
        g.reduceClipRegion(v.area.toNearestInt());
        const PathStrokeType stroke(2.5f, PathStrokeType::curved, PathStrokeType::rounded);
        const TuningStatus order[] = { TuningStatus::NoPitch, TuningStatus::Flat, TuningStatus::Sharp, TuningStatus::InTune };
        for (auto s : order)
        {
            g.setColour(statusColour(s));
            g.strokePath(paths[static_cast<int>(s)], stroke);
        }
        g.restoreState();
    }

    // ---- playhead + live marker ------------------------------------------------------------
    const float px = v.xForTime(now);
    if (px >= v.area.getX() && px <= v.area.getRight())
    {
        g.setColour(colours::playhead);
        g.drawVerticalLine(roundToInt(px), v.area.getY(), v.area.getBottom());
    }
    const auto readout = computeReadout();
    if (readout.hasPitch)
    {
        const float y = v.yForMidi(readout.midi);
        const float x = jlimit(v.area.getX() + 6.f, v.area.getRight() - 6.f, px);
        g.setColour(statusColour(readout.status));
        g.fillEllipse(x - 6.f, y - 6.f, 12.f, 12.f);
        g.setColour(Colours::black.withAlpha(0.6f));
        g.drawEllipse(x - 6.f, y - 6.f, 12.f, 12.f, 1.5f);
    }

    // ---- keyboard --------------------------------------------------------------------------
    const int sung = readout.hasPitch ? nearestNote(readout.midi) : -1000;
    for (int n = v.lo; n <= v.hi; ++n)
    {
        const float y = v.yForMidi(n) - rowH * 0.5f;
        const int pc = ((n % 12) + 12) % 12;
        const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        Rectangle<float> key(0.f, y, v.keyboardW, rowH);
        g.setColour(black ? Colour(0xff2a2d34) : Colour(0xffd9dce2));
        g.fillRect(key.reduced(0.f, 0.5f));
        if (!isInScale(n, v.key, v.scale))
        {
            g.setColour(Colours::black.withAlpha(0.25f));
            g.fillRect(key.reduced(0.f, 0.5f));
        }
        if (readout.hasTarget && n == readout.target)
        {
            g.setColour(colours::note);
            g.fillRect(key.withWidth(6.f));
        }
        if (n == sung)
        {
            g.setColour(statusColour(readout.status).withAlpha(0.85f));
            g.fillRect(key.withTrimmedLeft(v.keyboardW - 10.f));
        }
        const bool label = pc == 0 || (rowH >= 13.f && !black);
        if (label && rowH >= 7.f)
        {
            g.setColour(black ? Colours::white : Colours::black.withAlpha(pc == 0 ? 0.9f : 0.55f));
            g.setFont(FontOptions(jmin(12.f, rowH), pc == 0 ? Font::bold : Font::plain));
            g.drawText(String(noteName(n, v.conv)), key.reduced(4.f, 0.f).withTrimmedRight(10.f),
                       Justification::centredRight, false);
        }
    }
    g.setColour(Colours::black);
    g.drawVerticalLine(roundToInt(v.keyboardW) - 1, 0.f, static_cast<float>(getHeight()));

    // ---- overlays --------------------------------------------------------------------------
    if (!rubber_.isEmpty())
    {
        g.setColour(colours::selected.withAlpha(0.15f));
        g.fillRect(rubber_);
        g.setColour(colours::selected.withAlpha(0.7f));
        g.drawRect(rubber_, 1.f);
    }
    if (notes.empty())
    {
        g.setColour(Colours::white.withAlpha(0.45f));
        g.setFont(FontOptions(15.f));
        g.drawFittedText("No reference notes yet.\nDrop an isolated lead-vocal file (then click Analyze Vocal) or a MIDI file here,\n"
                         "or use Load Vocal... / Import MIDI...",
                         v.area.reduced(20.f).toNearestInt(), Justification::centred, 4);
    }
    if (!snap_.playing)
    {
        g.setColour(Colours::white.withAlpha(0.5f));
        g.setFont(FontOptions(12.f));
        g.drawText("Transport stopped - live pitch is compared with the note under the playhead",
                   v.area.reduced(8.f).toNearestInt(), Justification::topRight, false);
    }
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
    drag_ = Drag::None;
    if (e.position.x < v.keyboardW) return;

    if (e.mods.isMiddleButtonDown() || e.mods.isAltDown())
    {
        drag_ = Drag::Pan;
        return;
    }

    const auto notes = model.getNotes();
    bool nearRight = false;
    const int idx = hitTestNote(v, notes, e.position, nearRight);
    if (idx >= 0)
    {
        const auto sel = model.getSelection();
        if (e.mods.isShiftDown() || e.mods.isCommandDown())
            model.toggleSelected(idx);
        else if (!sel[static_cast<size_t>(idx)])
            model.selectOnly(idx);
        anchor_ = idx;
        dragOrig_ = model.getNotes();
        dragSel_ = model.getSelection();
        drag_ = nearRight ? Drag::Resize : Drag::Move;
    }
    else
    {
        rubberAdd_ = e.mods.isShiftDown() || e.mods.isCommandDown();
        if (!rubberAdd_) model.clearSelection();
        drag_ = Drag::Rubber;
    }
}

void PianoRoll::mouseDrag(const MouseEvent& e)
{
    const auto v = makeView();
    auto& model = proc_.getReference();
    const float dx = e.position.x - downPos_.x, dy = e.position.y - downPos_.y;
    const double pps = v.area.getWidth() / v.span;

    switch (drag_)
    {
        case Drag::Pan:
            follow_ = false;
            viewStart_ = viewStartAtDown_ - dx / pps;
            repaint();
            break;

        case Drag::Move:
        case Drag::Resize:
        {
            if (!gestureStarted_)
            {
                if (std::abs(dx) < 3.f && std::abs(dy) < 3.f) return;
                model.beginGesture();
                gestureStarted_ = true;
                follow_ = false;
            }
            const double dt = dx / pps;
            if (drag_ == Drag::Move)
            {
                const int dp = roundToInt(-dy / v.rowH());
                for (size_t i = 0; i < dragOrig_.size() && i < dragSel_.size(); ++i)
                {
                    if (!dragSel_[i]) continue;
                    auto n = dragOrig_[i];
                    n.start = jmax(0.0, n.start + dt);
                    n.pitch = jlimit(0, 127, n.pitch + dp);
                    model.setNote(static_cast<int>(i), n);
                }
            }
            else if (anchor_ >= 0 && anchor_ < static_cast<int>(dragOrig_.size()))
            {
                auto n = dragOrig_[static_cast<size_t>(anchor_)];
                n.length = jmax(0.02, n.length + dt);
                model.setNote(anchor_, n);
            }
            repaint();
            break;
        }

        case Drag::Rubber:
        {
            rubber_ = Rectangle<float>(downPos_, e.position).getIntersection(v.area);
            const double t0 = v.timeForX(rubber_.getX()) - v.offset, t1 = v.timeForX(rubber_.getRight()) - v.offset;
            const int pHi = static_cast<int>(std::floor(v.midiForY(rubber_.getY()) + 0.5)) - v.transpose;
            const int pLo = static_cast<int>(std::ceil(v.midiForY(rubber_.getBottom()) - 0.5)) - v.transpose;
            model.selectInRange(t0, t1, pLo, pHi, rubberAdd_);
            repaint();
            break;
        }

        case Drag::None:
            break;
    }
}

void PianoRoll::mouseUp(const MouseEvent&)
{
    if (gestureStarted_) proc_.getReference().sortKeepingSelection();
    gestureStarted_ = false;
    drag_ = Drag::None;
    rubber_ = {};
    repaint();
}

void PianoRoll::mouseMove(const MouseEvent& e)
{
    const auto v = makeView();
    bool nearRight = false;
    const int idx = hitTestNote(v, proc_.getReference().getNotes(), e.position, nearRight);
    setMouseCursor(idx >= 0 ? (nearRight ? MouseCursor::LeftRightResizeCursor : MouseCursor::DraggingHandCursor)
                            : MouseCursor::NormalCursor);
}

void PianoRoll::mouseDoubleClick(const MouseEvent& e)
{
    const auto v = makeView();
    if (e.position.x < v.keyboardW) return;
    bool nearRight = false;
    if (hitTestNote(v, proc_.getReference().getNotes(), e.position, nearRight) >= 0) return;
    RefNote n;
    n.start = jmax(0.0, v.timeForX(e.position.x) - v.offset);
    n.length = 60.0 / (snap_.bpm > 1.0 ? snap_.bpm : 120.0);
    n.pitch = jlimit(0, 127, roundToInt(v.midiForY(e.position.y)) - v.transpose);
    n.confidence = 1.f;
    proc_.getReference().addNote(n);
}

void PianoRoll::mouseWheelMove(const MouseEvent& e, const MouseWheelDetails& w)
{
    auto v = makeView();
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        // Zoom time around the mouse.
        auto* p = proc_.getApvts().getParameter(params::viewSeconds);
        const double tMouse = v.timeForX(e.position.x);
        const float newSpan = jlimit(2.f, 30.f, static_cast<float>(v.span * (w.deltaY > 0 ? 0.85 : 1.0 / 0.85)));
        p->setValueNotifyingHost(p->convertTo0to1(newSpan));
        viewStart_ = tMouse - (e.position.x - v.area.getX()) / v.area.getWidth() * newSpan;
        follow_ = false;
    }
    else
    {
        const float d = std::abs(w.deltaX) > std::abs(w.deltaY) ? w.deltaX : w.deltaY;
        viewStart_ -= d * v.span * 0.25;
        follow_ = false;
    }
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
