#include "PluginEditor.h"

#include "Params.h"
#include "SettingsPanel.h"
#include "UiModel.h"
#include "pitchlane/MidiFile.h"

namespace pitchlane {

using namespace juce;
namespace col = theme::col;

namespace {
const String kMinus = String(CharPointer_UTF8("\xe2\x88\x92"));

OctaveConvention convOf(AudioProcessorValueTreeState& ap)
{
    return roundToInt(ap.getRawParameterValue(params::noteNames)->load()) == 1 ? OctaveConvention::Yamaha
                                                                               : OctaveConvention::Scientific;
}
} // namespace

PitchLaneEditor::PitchLaneEditor(PitchLaneProcessor& p)
    : AudioProcessorEditor(&p), proc_(p), roll_(p),
      keyField_(p.getApvts().getParameter(params::key), ValueField::Style::ChevronDown),
      bpmField_(p.getApvts().getParameter(params::tempo), ValueField::Style::ChevronDown),
      gearBtn_("Settings", theme::icons::gear, true, 17.f),
      scrollLeft_("Scroll left", theme::icons::arrowLeft, false, 14.f),
      scrollRight_("Scroll right", theme::icons::arrowRight, false, 14.f),
      offsetCap_("Reference offset", "Shifts the reference notes earlier (" + kMinus + ") or later (+) against the song, in ms. "
                                     "Arrows: 10 ms (Shift: 1 ms). Drag or scroll to change, double-click to type."),
      transposeCap_("Transpose", "Transposes the reference notes in semitones (e.g. to fit your range)."),
      rangeCap_("Vocal range", "Lowest and highest note shown in the roll. Pick a voice type or set each end."),
      toleranceCap_("Tolerance", "How far (in cents) you may be from the target and still count as in tune."),
      guideCap_("Guide", "Notes: compare your pitch with the reference melody. "
                         "Scales: compare with the nearest note of the selected key / scale."),
      displayCap_("Display", "Show your live pitch line, the reference notes, or both."),
      smoothingCap_("Smoothing", "How much the live pitch line is smoothed (median + low-pass). "
                                 "0 % = raw detector output; more = calmer line, slightly more lag."),
      offsetField_(p.getApvts().getParameter(params::refOffset), ValueField::Style::Stepper),
      transposeField_(p.getApvts().getParameter(params::transpose), ValueField::Style::ChevronRight),
      rangeField_(nullptr, ValueField::Style::ChevronRight),
      toleranceField_(p.getApvts().getParameter(params::tolerance), ValueField::Style::ChevronRight),
      guideSeg_({ "Notes", "Scales" }, p.getApvts().getParameter(params::guide)),
      displaySeg_({ "Both", "Vocal", "Reference" }, p.getApvts().getParameter(params::display))
{
    setLookAndFeel(&laf_);
    setWantsKeyboardFocus(true);
    addAndMakeVisible(roll_);
    addAndMakeVisible(live_);

    // ---- header -----------------------------------------------------------------------------
    addAndMakeVisible(refField_);
    refField_.onClick = [this] { chooseVocalFile(); };
    addAndMakeVisible(analyzeBtn_);
    analyzeBtn_.getProperties().set("accent", true);
    analyzeBtn_.setWantsKeyboardFocus(false);
    analyzeBtn_.setTooltip("Convert the reference vocal stem into editable target notes (runs in the background).");
    analyzeBtn_.onClick = [this] {
        if (proc_.getAnalysis().isRunning()) proc_.getAnalysis().cancel();
        else startAnalysis();
    };

    auto& ap = proc_.getApvts();
    addAndMakeVisible(keyField_);
    keyField_.setTooltip("Key and scale (used by the Scales guide and the row shading).");
    keyField_.textFor = [&ap](float) {
        return ui::keyScaleText(roundToInt(ap.getRawParameterValue(params::key)->load()),
                                static_cast<ScaleType>(roundToInt(ap.getRawParameterValue(params::scale)->load())));
    };
    keyField_.buildMenu = [&ap](PopupMenu& m) {
        const int key = roundToInt(ap.getRawParameterValue(params::key)->load());
        const int scale = roundToInt(ap.getRawParameterValue(params::scale)->load());
        m.addSectionHeader("Key");
        for (int k = 0; k < 12; ++k) m.addItem(1 + k, pitchClassName(k), true, k == key);
        m.addSectionHeader("Scale");
        for (int s = 0; s < static_cast<int>(ScaleType::NumScales); ++s)
            m.addItem(100 + s, scaleName(static_cast<ScaleType>(s)), true, s == scale);
    };
    keyField_.menuResult = [this](int r) {
        auto set = [](RangedAudioParameter* prm, float v) {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost(prm->convertTo0to1(v));
            prm->endChangeGesture();
        };
        if (r >= 1 && r <= 12) set(param(params::key), static_cast<float>(r - 1));
        else if (r >= 100) set(param(params::scale), static_cast<float>(r - 100));
        keyField_.refresh();
    };
    keyField_.step = 1.f;
    keyField_.dragPixelsPerStep = 12.f;

    addAndMakeVisible(bpmField_);
    bpmField_.step = 1.f;
    bpmField_.fineStep = 0.1f;
    bpmField_.textFor = [this](float manual) {
        const auto t = proc_.getTransport();
        const double bpm = t.source != static_cast<int>(TimeSource::FreeRunning) ? t.bpm : manual;
        return std::abs(bpm - std::round(bpm)) < 0.05 ? String(roundToInt(bpm)) : String(bpm, 1);
    };
    bpmField_.buildMenu = [this](PopupMenu& m) {
        if (bpmField_.readOnly)
        {
            m.addSectionHeader("Following Logic's tempo");
            m.addItem(1, "Turn Logic Sync off to set a manual tempo", false, false);
            return;
        }
        m.addSectionHeader("Manual tempo (drag, scroll or double-click to type)");
        for (int b : { 60, 70, 80, 90, 100, 104, 110, 120, 128, 140, 160 }) m.addItem(1000 + b, String(b) + " BPM");
        PopupMenu sig;
        const int cur = roundToInt(param(params::timeSig)->convertFrom0to1(param(params::timeSig)->getValue()));
        for (int i = 0; i < params::numTimeSigChoices; ++i)
        {
            const auto ts = params::timeSigChoices[i];
            sig.addItem(100 + i, String(ts.num) + "/" + String(ts.den), true, i == cur);
        }
        m.addSubMenu("Time signature", sig);
    };
    bpmField_.menuResult = [this](int r) {
        if (r > 1000) bpmField_.setValue(static_cast<float>(r - 1000));
        else if (r >= 100 && r < 100 + params::numTimeSigChoices)
        {
            auto* ts = param(params::timeSig);
            ts->beginChangeGesture();
            ts->setValueNotifyingHost(ts->convertTo0to1(static_cast<float>(r - 100)));
            ts->endChangeGesture();
        }
    };

    addAndMakeVisible(syncBtn_);
    syncBtn_.setTooltip("Logic Sync: follow Logic's transport and tempo. Off = free-running clock at the manual BPM.");
    syncAttachment_ = std::make_unique<ButtonParameterAttachment>(*param(params::hostSync), syncBtn_);
    addAndMakeVisible(statusDot_);
    addAndMakeVisible(gearBtn_);
    gearBtn_.onClick = [this] { showSettings(); };

    // ---- hint bar ----------------------------------------------------------------------------
    addAndMakeVisible(scrollLeft_);
    addAndMakeVisible(scrollRight_);
    scrollLeft_.onClick = [this] { roll_.scroll(-1); };
    scrollRight_.onClick = [this] { roll_.scroll(1); };
    auto& model = proc_.getReference();
    selDown_.setButtonText(kMinus + "1 st");
    selUp_.setButtonText("+1 st");
    selEarlier_.setButtonText(String(CharPointer_UTF8("\xe2\x97\x80 10 ms")));
    selLater_.setButtonText(String(CharPointer_UTF8("10 ms \xe2\x96\xb6")));
    selDown_.onClick = [&model] { model.nudgeSelected(-1, 0.0); };
    selUp_.onClick = [&model] { model.nudgeSelected(1, 0.0); };
    selEarlier_.onClick = [&model] { model.nudgeSelected(0, -0.01); };
    selLater_.onClick = [&model] { model.nudgeSelected(0, 0.01); };
    selDelete_.onClick = [&model] { model.deleteSelected(); };
    for (auto* b : { &selDown_, &selUp_, &selEarlier_, &selLater_, &selDelete_ })
    {
        addChildComponent(*b);
        b->setWantsKeyboardFocus(false);
    }

    setupBottomBar();

    setResizable(true, true);
    setResizeLimits(kMinW, kMinH, 2800, 1800);
    setSize(kDefaultW, kDefaultH);
    updateReferenceField();
    startTimerHz(60);
}

PitchLaneEditor::~PitchLaneEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void PitchLaneEditor::setupBottomBar()
{
    for (auto* c : { &offsetCap_, &transposeCap_, &rangeCap_, &toleranceCap_, &guideCap_, &displayCap_, &smoothingCap_ })
        addAndMakeVisible(*c);

    addAndMakeVisible(offsetField_);
    offsetField_.step = 10.f;
    offsetField_.fineStep = 1.f;
    offsetField_.dragPixelsPerStep = 4.f;
    offsetField_.textFor = [](float v) { return ui::signedMsText(v); };
    offsetField_.parse = [](const String& s) { return s.retainCharacters("-0123456789.").getFloatValue(); };

    addAndMakeVisible(transposeField_);
    transposeField_.dragPixelsPerStep = 10.f;
    transposeField_.textFor = [](float v) { return ui::semitoneText(roundToInt(v)); };
    transposeField_.parse = [](const String& s) { return s.retainCharacters("-0123456789").getFloatValue(); };
    transposeField_.buildMenu = [this](PopupMenu& m) {
        const int cur = roundToInt(transposeField_.getValue());
        for (int st = 12; st >= -12; --st) m.addItem(1000 + st, ui::semitoneText(st), true, st == cur);
    };
    transposeField_.menuResult = [this](int r) { transposeField_.setValue(static_cast<float>(r - 1000)); };

    addAndMakeVisible(rangeField_);
    auto& ap = proc_.getApvts();
    rangeField_.textFor = [&ap](float) {
        return ui::rangeText(roundToInt(ap.getRawParameterValue(params::lowNote)->load()),
                             roundToInt(ap.getRawParameterValue(params::highNote)->load()), convOf(ap));
    };
    rangeField_.buildMenu = [&ap](PopupMenu& m) {
        const auto conv = convOf(ap);
        const int lo = roundToInt(ap.getRawParameterValue(params::lowNote)->load());
        const int hi = roundToInt(ap.getRawParameterValue(params::highNote)->load());
        const auto& presets = ui::rangePresets();
        m.addSectionHeader("Voice type");
        for (size_t i = 0; i < presets.size(); ++i)
            m.addItem(1 + static_cast<int>(i), String(presets[i].name) + "   " + ui::rangeText(presets[i].lo, presets[i].hi, conv),
                      true, presets[i].lo == lo && presets[i].hi == hi);
        PopupMenu low, high;
        for (int n = 24; n <= 96; ++n) low.addItem(200 + n, String(noteName(n, conv)), true, n == lo);
        for (int n = 36; n <= 108; ++n) high.addItem(400 + n, String(noteName(n, conv)), true, n == hi);
        m.addSeparator();
        m.addSubMenu("Lowest note", low);
        m.addSubMenu("Highest note", high);
    };
    rangeField_.menuResult = [this](int r) {
        auto set = [](RangedAudioParameter* prm, int v) {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost(prm->convertTo0to1(static_cast<float>(v)));
            prm->endChangeGesture();
        };
        const auto& presets = ui::rangePresets();
        if (r >= 1 && r <= static_cast<int>(presets.size()))
        {
            set(param(params::lowNote), presets[static_cast<size_t>(r - 1)].lo);
            set(param(params::highNote), presets[static_cast<size_t>(r - 1)].hi);
        }
        else if (r >= 200 && r < 400) set(param(params::lowNote), r - 200);
        else if (r >= 400) set(param(params::highNote), r - 400);
        rangeField_.refresh();
    };

    addAndMakeVisible(toleranceField_);
    toleranceField_.dragPixelsPerStep = 5.f;
    toleranceField_.textFor = [](float v) { return String(CharPointer_UTF8("\xc2\xb1")) + String(roundToInt(v)) + String(CharPointer_UTF8(" \xc2\xa2")); };
    toleranceField_.parse = [](const String& s) { return s.retainCharacters("0123456789.").getFloatValue(); };
    toleranceField_.buildMenu = [this](PopupMenu& m) {
        const int cur = roundToInt(toleranceField_.getValue());
        for (int c : { 5, 10, 15, 20, 25, 30, 40, 50 })
            m.addItem(1000 + c, String(CharPointer_UTF8("\xc2\xb1")) + String(c) + " cents", true, c == cur);
    };
    toleranceField_.menuResult = [this](int r) { toleranceField_.setValue(static_cast<float>(r - 1000)); };

    addAndMakeVisible(guideSeg_);
    guideSeg_.setTooltips({ "Target = the reference melody note", "Target = nearest note of the key / scale" });
    addAndMakeVisible(displaySeg_);
    displaySeg_.setTooltips({ "Live pitch line and reference notes", "Only your live pitch line", "Only the reference notes" });

    addAndMakeVisible(smoothingKnob_);
    smoothingKnob_.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
    smoothingKnob_.setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
    smoothingKnob_.setRotaryParameters(MathConstants<float>::pi * 1.2f, MathConstants<float>::pi * 2.8f, true);
    smoothingKnob_.setWantsKeyboardFocus(false);
    smoothingKnob_.setTooltip("Smoothing of the live pitch line (drag up/down).");
    smoothingAttachment_ = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(proc_.getApvts(), params::smoothing,
                                                                                            smoothingKnob_);
    smoothingKnob_.onValueChange = [this] { repaint(smoothingValueArea_); };
}

// ---- layout / paint ------------------------------------------------------------------------------

void PitchLaneEditor::resized()
{
    auto b = getLocalBounds();
    headerArea_ = b.removeFromTop(76);
    bottomArea_ = b.removeFromBottom(104);
    auto leftCol = b.removeFromLeft(jlimit(140, 180, getWidth() / 8));
    hintArea_ = b.removeFromBottom(34);
    live_.setBounds(leftCol);
    roll_.setBounds(b);

    // ---- header ----
    {
        auto h = headerArea_.reduced(20, 0);
        const int fieldY = 24, fieldH = 36;
        auto place = [&](Component& c, int w, int y, int hh) { c.setBounds(h.removeFromRight(w).withY(y).withHeight(hh)); };
        place(gearBtn_, 28, fieldY + 4, 28);
        h.removeFromRight(12);
        place(statusDot_, 24, fieldY + 6, 24);
        h.removeFromRight(16);
        place(syncBtn_, getWidth() < 1150 ? 112 : 124, fieldY, fieldH);
        h.removeFromRight(18);
        place(bpmField_, 78, fieldY + 2, fieldH - 4);
        bpmLabelArea_ = bpmField_.getBounds().withY(6).withHeight(18);
        h.removeFromRight(16);
        place(keyField_, getWidth() < 1150 ? 116 : 128, fieldY + 2, fieldH - 4);
        keyLabelArea_ = keyField_.getBounds().withY(6).withHeight(18);
        h.removeFromRight(24);
        const int wordmarkW = getWidth() < 1150 ? 196 : 234;
        h.removeFromLeft(wordmarkW);
        place(analyzeBtn_, 128, fieldY, fieldH);
        h.removeFromRight(16);
        refField_.setBounds(h.withY(fieldY).withHeight(fieldH));
    }

    // ---- hint bar ----
    {
        auto hb = hintArea_.reduced(14, 0);
        auto right = hb.removeFromRight(230);
        right.removeFromRight(118); // "Zoom: ⌘ + scroll" text
        scrollRight_.setBounds(right.removeFromRight(26).withSizeKeepingCentre(26, 24));
        scrollLeft_.setBounds(right.removeFromRight(26).withSizeKeepingCentre(26, 24));
        auto sel = hb.withTrimmedLeft(110);
        for (auto* btn : { &selDown_, &selUp_, &selEarlier_, &selLater_, &selDelete_ })
        {
            btn->setBounds(sel.removeFromLeft(btn == &selDelete_ ? 64 : 70).withSizeKeepingCentre(btn == &selDelete_ ? 64 : 70, 24));
            sel.removeFromLeft(6);
        }
    }

    // ---- bottom bar ----
    {
        auto bb = bottomArea_.reduced(24, 0);
        struct G { Caption* cap; Component* ctl; int w; };
        G groups[] = { { &offsetCap_, &offsetField_, 150 }, { &transposeCap_, &transposeField_, 124 },
                       { &rangeCap_, &rangeField_, 134 },   { &toleranceCap_, &toleranceField_, 124 },
                       { &guideCap_, &guideSeg_, 142 },     { &displayCap_, &displaySeg_, 216 },
                       { &smoothingCap_, &smoothingKnob_, 104 } };
        int sum = 0;
        for (auto& g : groups) sum += g.w;
        const float scale = jmin(1.f, static_cast<float>(bb.getWidth()) / static_cast<float>(sum + 6 * 12));
        const int gap = jmax(12, (bb.getWidth() - roundToInt(sum * scale)) / 6);
        int x = bb.getX();
        for (auto& g : groups)
        {
            const int w = roundToInt(g.w * scale);
            g.cap->setBounds(x, bb.getY() + 18, w, 18);
            if (g.ctl == &smoothingKnob_)
            {
                smoothingKnob_.setBounds(x + 4, bb.getY() + 40, 44, 44);
                smoothingValueArea_ = { x + 54, bb.getY() + 44, w - 54, 36 };
            }
            else
            {
                g.ctl->setBounds(x, bb.getY() + 44, w, 38);
            }
            x += w + gap;
        }
        // right-align the smoothing group with the edge
        const int shift = bb.getRight() - (smoothingValueArea_.getRight());
        if (shift > 0)
        {
            smoothingCap_.setTopLeftPosition(smoothingCap_.getX() + shift, smoothingCap_.getY());
            smoothingKnob_.setTopLeftPosition(smoothingKnob_.getX() + shift, smoothingKnob_.getY());
            smoothingValueArea_.translate(shift, 0);
        }
    }
}

void PitchLaneEditor::paint(Graphics& g)
{
    g.fillAll(col::window);

    // header
    g.setColour(col::header);
    g.fillRect(headerArea_);
    g.setColour(col::divider);
    g.drawHorizontalLine(headerArea_.getBottom() - 1, 0.f, static_cast<float>(getWidth()));
    {
        const bool compact = getWidth() < 1150;
        const float cy = static_cast<float>(refField_.getBounds().getCentreY());
        auto logo = Rectangle<float>(20.f, cy - 11.f, compact ? 30.f : 36.f, 22.f);
        ColourGradient grad(Colour(0xff7b6cf0), logo.getX(), 0.f, Colour(0xffb9b0ff), logo.getRight(), 0.f, false);
        g.setGradientFill(grad);
        g.strokePath(theme::icons::wave(logo), PathStrokeType(2.6f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour(Colour(0xffdfe2ff));
        g.setFont(Font(theme::font(compact ? 20.f : 24.f, theme::Weight::Medium)).withExtraKerningFactor(0.16f));
        g.drawText("PITCH LANE", Rectangle<float>(logo.getRight() + 12.f, cy - 16.f, 220.f, 32.f), Justification::centredLeft, false);
    }
    g.setColour(col::textDim);
    g.setFont(theme::font(12.f));
    g.drawText("Key", keyLabelArea_.translated(2, 0), Justification::centredLeft, false);
    g.drawText("BPM", bpmLabelArea_.translated(2, 0), Justification::centredLeft, false);

    // analysis progress under the Analyze button
    if (analysing_)
    {
        auto pb = analyzeBtn_.getBounds().toFloat().reduced(8.f, 0.f);
        pb = pb.withY(pb.getBottom() + 4.f).withHeight(3.f);
        g.setColour(col::field);
        g.fillRoundedRectangle(pb, 1.5f);
        g.setColour(col::lavender);
        g.fillRoundedRectangle(pb.withWidth(pb.getWidth() * static_cast<float>(jlimit(0.0, 1.0, progress_))), 1.5f);
    }

    // hint bar
    g.setColour(col::hintBar);
    g.fillRect(hintArea_);
    g.setColour(col::divider);
    g.drawHorizontalLine(hintArea_.getBottom() - 1, static_cast<float>(hintArea_.getX()), static_cast<float>(getWidth()));
    {
        auto hb = hintArea_.reduced(16, 0);
        auto right = hb.removeFromRight(230);
        g.setFont(theme::font(12.f));
        g.setColour(col::textDim);
        g.drawText(String(CharPointer_UTF8("Zoom: \xe2\x8c\x98 + scroll")), right.removeFromRight(112), Justification::centredRight, false);
        right.removeFromRight(58);
        g.drawText("Scroll:", right, Justification::centredRight, false);
        hb.removeFromRight(10);
        const int nSel = proc_.getReference().numSelected();
        if (Time::getMillisecondCounterHiRes() < toastUntilMs_ && toast_.isNotEmpty())
        {
            g.setColour(col::lavenderHi);
            g.drawText(toast_, hb, Justification::centredLeft, true);
        }
        else if (nSel > 0)
        {
            g.setColour(col::text);
            g.drawText(String(nSel) + (nSel == 1 ? " note selected" : " notes selected"), hb.withWidth(104), Justification::centredLeft, true);
        }
        else
        {
            // hint text with dim separators
            StringArray parts;
            parts.addTokens(String(CharPointer_UTF8(kHintText)), String(CharPointer_UTF8("\xc2\xb7")), "");
            float x = static_cast<float>(hb.getX());
            const auto f = Font(theme::font(12.f));
            for (int i = 0; i < parts.size(); ++i)
            {
                const auto s = parts[i].trim();
                const float w = GlyphArrangement::getStringWidth(f, s);
                if (x + w > hb.getRight()) break;
                g.setColour(col::textDim);
                g.drawText(s, Rectangle<float>(x, static_cast<float>(hb.getY()), w + 2.f, static_cast<float>(hb.getHeight())),
                           Justification::centredLeft, false);
                x += w + 12.f;
                if (i + 1 < parts.size())
                {
                    g.setColour(col::border);
                    g.drawVerticalLine(roundToInt(x), hb.getCentreY() - 6.f, hb.getCentreY() + 6.f);
                    x += 12.f;
                }
            }
        }
    }

    // bottom bar
    g.setColour(col::panel);
    g.fillRect(bottomArea_);
    g.setColour(col::divider);
    g.drawHorizontalLine(bottomArea_.getY(), 0.f, static_cast<float>(getWidth()));
    g.setColour(col::text.withAlpha(0.9f));
    g.setFont(theme::font(13.f, theme::Weight::Medium));
    g.drawText(String(roundToInt(smoothingKnob_.getValue())) + "%", smoothingValueArea_, Justification::centredLeft, false);

    if (dragHover_)
    {
        g.setColour(col::lavender);
        g.drawRect(getLocalBounds(), 3);
    }
}

bool PitchLaneEditor::keyPressed(const KeyPress& key)
{
    return roll_.handleKey(key);
}

void PitchLaneEditor::showToast(const String& message)
{
    toast_ = message;
    toastUntilMs_ = Time::getMillisecondCounterHiRes() + 6000.0;
    repaint(hintArea_);
}

// ---- timer ---------------------------------------------------------------------------------------

void PitchLaneEditor::timerCallback()
{
    roll_.update();
    auto& ap = proc_.getApvts();
    live_.setReadout(roll_.computeReadout(), convOf(ap), ap.getRawParameterValue(params::tolerance)->load());

    auto& an = proc_.getAnalysis();
    const bool running = an.isRunning();
    progress_ = running ? an.getProgress() : 0.0;
    if (running != analysing_ || running)
    {
        analysing_ = running;
        analyzeBtn_.setButtonText(running ? "CANCEL  " + String(roundToInt(progress_ * 100.0)) + "%" : "ANALYZE VOCAL");
        analyzeBtn_.setTooltip(running ? "Analysis running: click to cancel." : "Convert the reference vocal stem into editable target notes (runs in the background).");
        repaint(analyzeBtn_.getBounds().expanded(0, 10));
    }

    if (++tick_ % 6 == 0)
    {
        const auto t = proc_.getTransport();
        const bool sync = ap.getRawParameterValue(params::hostSync)->load() >= 0.5f;
        const bool host = t.source != static_cast<int>(TimeSource::FreeRunning);
        if (!sync) statusDot_.setState(StatusDot::State::Off, "Logic Sync off: free-running clock at the manual BPM");
        else if (!host) statusDot_.setState(StatusDot::State::Waiting, "Logic Sync on, but the host provides no timeline");
        else if (t.playing) statusDot_.setState(StatusDot::State::Playing, "Synced to the host transport (playing)");
        else statusDot_.setState(StatusDot::State::Synced, "Synced to the host transport (stopped)");
        bpmField_.readOnly = sync && host;
        bpmField_.setTooltip(bpmField_.readOnly ? "Following Logic's tempo (Logic Sync on)."
                                                : "Manual tempo, used when Logic Sync is off or the host has no tempo.");
        for (auto* f : { &keyField_, &bpmField_, &offsetField_, &transposeField_, &rangeField_, &toleranceField_ }) f->refresh();

        const String status = an.getStatusText();
        if (status != lastAnalysisStatus_)
        {
            lastAnalysisStatus_ = status;
            if (status.isNotEmpty()) showToast(status);
        }
        const bool anySel = proc_.getReference().numSelected() > 0;
        const bool toastOn = Time::getMillisecondCounterHiRes() < toastUntilMs_;
        for (auto* b : { &selDown_, &selUp_, &selEarlier_, &selLater_, &selDelete_ }) b->setVisible(anySel && !toastOn);
        repaint(hintArea_);
    }
    if (tick_ % 60 == 0) updateReferenceField();
}

void PitchLaneEditor::updateReferenceField()
{
    const String path = proc_.getReference().getSourcePath();
    if (pendingVocal_ != File())
    {
        refField_.setFile(pendingVocal_.getFileName(), col::text);
        refField_.setTooltip(pendingVocal_.getFullPathName() + "\nNot analysed yet: click ANALYZE VOCAL.");
        return;
    }
    if (path.isEmpty())
    {
        refField_.setFile("Load a vocal stem or MIDI...", col::textFaint);
        refField_.setTooltip("Click to choose an isolated lead-vocal file, or drop a vocal / MIDI file onto the window.");
        return;
    }
    const File f(path);
    if (f.existsAsFile())
    {
        refField_.setFile(f.getFileName(), col::text);
        refField_.setTooltip(path);
    }
    else
    {
        refField_.setFile(f.getFileName() + " (missing)", col::off);
        refField_.setTooltip(path + "\nThe file is missing; the notes are saved in the project and still work.");
    }
}

// ---- settings popover -------------------------------------------------------------------------

void PitchLaneEditor::showSettings()
{
    SettingsPanel::Actions a;
    SafePointer<PitchLaneEditor> safe(this);
    a.importMidi = [safe] { if (safe) safe->importMidi(); };
    a.exportMidi = [safe] { if (safe) safe->exportMidi(); };
    a.clearTrace = [safe] { if (safe) safe->roll_.clearTrace(); };
    a.restartClock = [safe] { if (safe) { safe->proc_.restartFreeClock(); safe->roll_.clearTrace(); } };
    a.addMarker = [safe] { if (safe) safe->roll_.addMarkerAtPlayhead(); };
    a.getFollow = [safe] { return safe != nullptr && safe->roll_.getFollow(); };
    a.setFollow = [safe](bool f) { if (safe) safe->roll_.setFollow(f); };
    auto panel = std::make_unique<SettingsPanel>(proc_, std::move(a));
    CallOutBox::launchAsynchronously(std::move(panel), gearBtn_.getBounds(), this);
}

// ---- files ----------------------------------------------------------------------------------------

bool PitchLaneEditor::isMidiFile(const File& f)
{
    return f.hasFileExtension("mid;midi;smf");
}

bool PitchLaneEditor::isInterestedInFileDrag(const StringArray& files)
{
    for (auto& s : files)
    {
        const File f(s);
        if (isMidiFile(f) || proc_.getAnalysis().canOpen(f)) { dragHover_ = true; repaint(); return true; }
    }
    return false;
}

void PitchLaneEditor::filesDropped(const StringArray& files, int, int)
{
    dragHover_ = false;
    repaint();
    for (auto& s : files)
    {
        const File f(s);
        if (isMidiFile(f)) { importMidiFile(f); return; }
        if (proc_.getAnalysis().canOpen(f)) { setPendingVocal(f); return; }
    }
}

void PitchLaneEditor::setPendingVocal(const File& f)
{
    pendingVocal_ = f;
    updateReferenceField();
    showToast("Ready: click ANALYZE VOCAL to turn " + f.getFileName() + " into reference notes");
}

void PitchLaneEditor::chooseVocalFile()
{
    chooser_ = std::make_unique<FileChooser>("Choose an isolated lead-vocal file", File(), proc_.getAnalysis().getWildcard());
    chooser_->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this](const FileChooser& fc) {
        const auto f = fc.getResult();
        if (f.existsAsFile()) setPendingVocal(f);
    });
}

void PitchLaneEditor::startAnalysis()
{
    File f = pendingVocal_;
    if (f == File())
    {
        const File src(proc_.getReference().getSourcePath());
        if (src.existsAsFile() && proc_.getAnalysis().canOpen(src)) f = src;
    }
    if (f == File() || !f.existsAsFile())
    {
        chooseVocalFile();
        return;
    }
    if (proc_.getAnalysis().start(f)) pendingVocal_ = File();
    else showToast("An analysis is already running");
    updateReferenceField();
}

void PitchLaneEditor::importMidi()
{
    chooser_ = std::make_unique<FileChooser>("Import reference melody (MIDI)", File(), "*.mid;*.midi;*.smf");
    chooser_->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this](const FileChooser& fc) {
        const auto f = fc.getResult();
        if (f.existsAsFile()) importMidiFile(f);
    });
}

void PitchLaneEditor::importMidiFile(const File& f)
{
    MemoryBlock data;
    if (!f.loadFileAsData(data) || data.getSize() > 16 * 1024 * 1024)
    {
        AlertWindow::showMessageBoxAsync(MessageBoxIconType::WarningIcon, "Import MIDI", "Could not read " + f.getFileName());
        return;
    }
    auto parsed = std::make_shared<MidiParseResult>(parseMidiFile(static_cast<const uint8_t*>(data.getData()), data.getSize()));
    if (!parsed->ok)
    {
        AlertWindow::showMessageBoxAsync(MessageBoxIconType::WarningIcon, "Import MIDI", f.getFileName() + ": " + String(parsed->error));
        return;
    }

    auto commit = [this, f](NoteList notes) {
        const int muted = static_cast<int>(std::count_if(notes.begin(), notes.end(), [](const RefNote& n) { return n.muted(); }));
        proc_.getReference().setNotes(std::move(notes), true);
        proc_.getReference().setSourcePath(f.getFullPathName());
        pendingVocal_ = File();
        updateReferenceField();
        showToast("Imported " + String(proc_.getReference().size()) + " notes from " + f.getFileName()
                  + (muted > 0 ? " (" + String(muted) + " harmony notes muted)" : String()));
    };
    auto apply = [this, parsed, f, commit](int track) {
        auto notes = referenceNotesFromMidi(*parsed, track);
        if (notes.empty())
        {
            AlertWindow::showMessageBoxAsync(MessageBoxIconType::InfoIcon, "Import MIDI", "No notes found in " + f.getFileName());
            return;
        }
        const int voices = maxPolyphony(notes);
        if (voices <= 1)
        {
            commit(std::move(notes));
            return;
        }
        // Harmonies / chords in the part: ask how to handle them.
        PopupMenu m;
        m.addSectionHeader("This part has harmonies (up to " + String(voices) + " voices at once)");
        m.addItem(static_cast<int>(ui::ImportVoices::LeadHighest), "Lead only: top voice");
        m.addItem(static_cast<int>(ui::ImportVoices::LeadLoudest), "Lead only: loudest / longest notes");
        m.addItem(static_cast<int>(ui::ImportVoices::AllHarmoniesMuted), "Keep all voices, harmonies muted (grey)");
        m.addItem(static_cast<int>(ui::ImportVoices::All), "Keep all voices as they are");
        auto shared = std::make_shared<NoteList>(std::move(notes));
        m.showMenuAsync(PopupMenu::Options().withTargetComponent(&refField_), [shared, commit](int r) {
            if (r <= 0) return;
            commit(ui::notesForImport(*shared, static_cast<ui::ImportVoices>(r)));
        });
    };

    // Offer a track choice when several tracks contain (non-drum) notes.
    std::vector<int> candidates;
    for (int t = 0; t < static_cast<int>(parsed->tracks.size()); ++t)
    {
        int count = 0;
        for (auto& n : parsed->notes) if (n.track == t && n.channel != 9) ++count;
        if (count > 0) candidates.push_back(t);
    }
    if (candidates.size() <= 1)
    {
        apply(-1);
        return;
    }
    PopupMenu menu;
    menu.addSectionHeader("Which track is the lead melody?");
    for (int t : candidates)
    {
        int count = 0;
        for (auto& n : parsed->notes) if (n.track == t && n.channel != 9) ++count;
        const auto& name = parsed->tracks[static_cast<size_t>(t)].name;
        menu.addItem(t + 1, (name.empty() ? "Track " + String(t + 1) : String(name)) + "  (" + String(count) + " notes)");
    }
    menu.showMenuAsync(PopupMenu::Options().withTargetComponent(&refField_), [apply](int result) {
        if (result > 0) apply(result - 1);
    });
}

void PitchLaneEditor::exportMidi()
{
    auto& ap = proc_.getApvts();
    const double offset = ap.getRawParameterValue(params::refOffset)->load() * 0.001;
    const int transpose = roundToInt(ap.getRawParameterValue(params::transpose)->load());
    auto notes = proc_.getReference().getNotes();
    if (notes.empty())
    {
        showToast("Nothing to export");
        return;
    }
    // Export in song time with the current offset and transpose applied.
    NoteList mapped;
    for (auto n : notes)
    {
        n.start += offset;
        n.pitch = jlimit(0, 127, n.pitch + transpose);
        if (n.end() <= 0.0) continue;
        if (n.start < 0.0) { n.length += n.start; n.start = 0.0; }
        mapped.push_back(n);
    }
    const double bpm = proc_.getTransport().bpm;
    chooser_ = std::make_unique<FileChooser>("Export reference melody",
                                             File::getSpecialLocation(File::userMusicDirectory).getChildFile("PitchLane Reference.mid"),
                                             "*.mid");
    chooser_->launchAsync(FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles
                              | FileBrowserComponent::warnAboutOverwriting,
                          [this, mapped, bpm](const FileChooser& fc) {
                              auto f = fc.getResult();
                              if (f == File()) return;
                              if (!f.hasFileExtension("mid;midi")) f = f.withFileExtension("mid");
                              const auto bytes = writeMidiFile(mapped, bpm);
                              const bool ok = f.replaceWithData(bytes.data(), bytes.size());
                              showToast(ok ? "Exported " + String(static_cast<int>(mapped.size())) + " notes to " + f.getFileName()
                                           : "Could not write " + f.getFullPathName());
                          });
}

} // namespace pitchlane
