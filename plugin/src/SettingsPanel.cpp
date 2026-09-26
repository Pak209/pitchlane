#include "SettingsPanel.h"

#include "Params.h"
#include "PluginProcessor.h"

namespace pitchlane {

using namespace juce;
namespace col = theme::col;

SettingsPanel::SettingsPanel(PitchLaneProcessor& p, Actions actions)
    : proc_(p), actions_(std::move(actions)),
      names_({ "Middle C = C4", "Middle C = C3 (Logic)" }, p.getApvts().getParameter(params::noteNames))
{
    namesLabel_.setText("Note names", dontSendNotification);
    namesLabel_.setFont(theme::font(13.f));
    addAndMakeVisible(namesLabel_);
    addAndMakeVisible(names_);
    addRow(calib_, params::calibration, "Timing calibration",
           "Shifts your live pitch line in time to compensate for input / monitoring latency.");
    addRow(gate_, params::gateDb, "Input gate", "Input level below which no pitch is shown.");
    addRow(clarity_, params::clarity, "Clarity threshold",
           "Higher = only very clean, periodic sound shows a pitch (suppresses breaths and noise).");
    addRow(span_, params::viewSeconds, "Visible time", String(CharPointer_UTF8("How many seconds the roll shows (also \xe2\x8c\x98 + scroll).")));
    calib_.slider.setTextValueSuffix(" ms");
    gate_.slider.setTextValueSuffix(" dB");
    span_.slider.setTextValueSuffix(" s");
    addAndMakeVisible(follow_);
    follow_.setWantsKeyboardFocus(false);
    follow_.onClick = [this] { if (actions_.setFollow) actions_.setFollow(follow_.getToggleState()); };

    auto& model = proc_.getReference();
    button("Import MIDI...", actions_.importMidi, "Load the reference melody from a MIDI file.");
    button("Export MIDI...", actions_.exportMidi, "Save the reference notes (offset / transpose applied) as a MIDI file.");
    button("Clear notes", [&model] { model.clear(); }, "Remove all reference notes (undoable).");
    undo_ = &button("Undo", [&model] { model.undo(); });
    redo_ = &button("Redo", [&model] { model.redo(); });
    button("Select all", [&model] { model.selectAll(); });
    selectionButtons_.push_back(&button("Delete", [&model] { model.deleteSelected(); }, "Delete the selected notes."));
    selectionButtons_.push_back(&button(String(CharPointer_UTF8("\xe2\x88\x92")) + "1 st", [&model] { model.nudgeSelected(-1, 0.0); }));
    selectionButtons_.push_back(&button("+1 st", [&model] { model.nudgeSelected(1, 0.0); }));
    selectionButtons_.push_back(&button(String(CharPointer_UTF8("\xe2\x97\x80")) + " 10 ms", [&model] { model.nudgeSelected(0, -0.01); }));
    selectionButtons_.push_back(&button(String(CharPointer_UTF8("10 ms \xe2\x96\xb6")), [&model] { model.nudgeSelected(0, 0.01); }));
    button("Add at playhead", actions_.addMarker, "Add a section marker at the playhead (then type its name).");
    button("Clear sections", [this] { proc_.getMarkers().clear(); }, "Remove all section markers.");
    button("Clear trace", actions_.clearTrace, "Erase the drawn pitch line.");
    restart_ = &button("Restart clock", actions_.restartClock, "Restart the free-running clock (no host timeline / Logic Sync off).");

    setSize(400, 540);
    timerCallback();
    startTimerHz(10);
}

SettingsPanel::~SettingsPanel()
{
    stopTimer();
}

void SettingsPanel::addRow(Row& r, const char* paramId, const String& text, const String& tip)
{
    r.label.setText(text, dontSendNotification);
    r.label.setFont(theme::font(13.f));
    r.label.setTooltip(tip);
    r.slider.setSliderStyle(Slider::LinearHorizontal);
    r.slider.setTextBoxStyle(Slider::TextBoxRight, false, 70, 22);
    r.slider.setTooltip(tip);
    r.slider.setWantsKeyboardFocus(false);
    addAndMakeVisible(r.label);
    addAndMakeVisible(r.slider);
    r.attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(proc_.getApvts(), paramId, r.slider);
}

TextButton& SettingsPanel::button(const String& text, std::function<void()> fn, const String& tip)
{
    auto* b = buttons_.add(new TextButton(text));
    b->setWantsKeyboardFocus(false);
    b->setTooltip(tip);
    b->onClick = [fn] { if (fn) fn(); };
    addAndMakeVisible(b);
    return *b;
}

void SettingsPanel::timerCallback()
{
    auto& model = proc_.getReference();
    undo_->setEnabled(model.canUndo());
    redo_->setEnabled(model.canRedo());
    const bool anySel = model.numSelected() > 0;
    for (auto* b : selectionButtons_) b->setEnabled(anySel);
    restart_->setEnabled(proc_.getTransport().source == static_cast<int>(TimeSource::FreeRunning));
    if (actions_.getFollow) follow_.setToggleState(actions_.getFollow(), dontSendNotification);
    names_.refresh();
}

void SettingsPanel::paint(Graphics& g)
{
    g.setColour(col::text);
    g.setFont(theme::font(15.f, theme::Weight::SemiBold));
    g.drawText("Settings", getLocalBounds().removeFromTop(26), Justification::centredLeft, false);
    for (auto& s : sections_)
    {
        g.setColour(col::textDim);
        g.setFont(Font(theme::font(11.f, theme::Weight::SemiBold)).withExtraKerningFactor(0.08f));
        g.drawText(s.first.toUpperCase(), Rectangle<int>(0, s.second, getWidth(), 16), Justification::centredLeft, false);
        g.setColour(col::divider);
        g.drawHorizontalLine(s.second + 17, 0.f, static_cast<float>(getWidth()));
    }
}

void SettingsPanel::resized()
{
    sections_.clear();
    auto b = getLocalBounds();
    b.removeFromTop(32);
    auto row = [&b](int h) { auto r = b.removeFromTop(h); b.removeFromTop(4); return r; };
    auto section = [&](const String& title) { b.removeFromTop(6); sections_.push_back({ title, b.getY() }); b.removeFromTop(22); };

    section("Display");
    {
        auto r = row(28);
        namesLabel_.setBounds(r.removeFromLeft(130));
        names_.setBounds(r);
    }
    for (auto* rr : { &calib_, &gate_, &clarity_, &span_ })
    {
        auto r = row(26);
        rr->label.setBounds(r.removeFromLeft(130));
        rr->slider.setBounds(r);
    }
    follow_.setBounds(row(26));

    auto buttonRows = [&](int first, int count, int perRow) {
        for (int i = 0; i < count; i += perRow)
        {
            auto r = row(28);
            const int n = jmin(perRow, count - i);
            const int w = (r.getWidth() - (perRow - 1) * 6) / perRow;
            for (int k = 0; k < n; ++k)
            {
                buttons_[first + i + k]->setBounds(r.removeFromLeft(w));
                r.removeFromLeft(6);
            }
        }
    };
    section("Reference notes");
    buttonRows(0, 3, 3);   // import, export, clear
    buttonRows(3, 3, 3);   // undo, redo, select all
    buttonRows(6, 5, 5);   // selection edits
    section("Section markers");
    buttonRows(11, 2, 2);
    section("Live pitch");
    buttonRows(13, 2, 2);
    if (getHeight() != b.getY() + 4) setSize(getWidth(), b.getY() + 4);
}

} // namespace pitchlane
