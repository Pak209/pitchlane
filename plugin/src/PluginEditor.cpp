#include "PluginEditor.h"

#include "Params.h"
#include "pitchlane/MidiFile.h"

namespace pitchlane {

using namespace juce;

// ============================================================================ ReadoutDisplay

void ReadoutDisplay::setReadout(const Readout& r, OctaveConvention conv, float tolerance)
{
    r_ = r;
    conv_ = conv;
    tolerance_ = tolerance;
    repaint();
}

void ReadoutDisplay::paint(Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour(Colour(0xff1b1e25));
    g.fillRoundedRectangle(b, 6.f);
    b.reduce(10.f, 6.f);

    const auto col = PianoRoll::statusColour(r_.status);
    auto left = b.removeFromLeft(110.f);
    g.setColour(r_.hasPitch ? Colours::white : Colours::white.withAlpha(0.3f));
    g.setFont(FontOptions(34.f, Font::bold));
    g.drawText(r_.hasPitch ? String(noteName(nearestNote(r_.midi), conv_)) : String("--"), left.removeFromTop(40.f),
               Justification::centredLeft, false);
    g.setFont(FontOptions(13.f));
    g.setColour(Colours::white.withAlpha(0.7f));
    g.drawText(r_.hasPitch ? String(r_.hz, 1) + " Hz" : String("no pitch"), left, Justification::centredLeft, false);

    // Status text.
    auto statusArea = b.removeFromTop(b.getHeight() * 0.55f);
    String status;
    if (!r_.hasPitch)
        status = "Sing...";
    else if (!r_.hasTarget)
        status = "No target  (" + String(r_.cents >= 0 ? "+" : "") + String(roundToInt(r_.cents)) + " c vs "
                 + String(noteName(r_.target, conv_)) + ")";
    else
    {
        const String cents = String(r_.cents >= 0 ? "+" : "") + String(roundToInt(r_.cents)) + " cents";
        const String tgt = "target " + String(noteName(r_.target, conv_));
        switch (r_.status)
        {
            case TuningStatus::InTune: status = "IN TUNE  " + cents + "   " + tgt; break;
            case TuningStatus::Flat:   status = "FLAT  " + cents + "   " + tgt; break;
            case TuningStatus::Sharp:  status = "SHARP  " + cents + "   " + tgt; break;
            case TuningStatus::NoPitch:
            default: break;
        }
    }
    g.setColour(r_.hasPitch ? col : Colours::white.withAlpha(0.4f));
    g.setFont(FontOptions(20.f, Font::bold));
    g.drawText(status, statusArea, Justification::centredLeft, true);

    // Cents meter: -50 .. +50 with the tolerance zone.
    auto meter = b.reduced(0.f, 4.f).withWidth(jmin(b.getWidth(), 320.f));
    g.setColour(Colour(0xff2a2e38));
    g.fillRoundedRectangle(meter, 3.f);
    const float cx = meter.getCentreX(), half = meter.getWidth() * 0.5f;
    const float tolW = jlimit(0.f, half, tolerance_ / 50.f * half);
    g.setColour(PianoRoll::statusColour(TuningStatus::InTune).withAlpha(0.25f));
    g.fillRect(cx - tolW, meter.getY(), 2.f * tolW, meter.getHeight());
    g.setColour(Colours::white.withAlpha(0.5f));
    g.drawVerticalLine(roundToInt(cx), meter.getY(), meter.getBottom());
    if (r_.hasPitch)
    {
        const float x = cx + jlimit(-1.f, 1.f, r_.cents / 50.f) * half;
        g.setColour(r_.hasTarget ? col : Colours::white.withAlpha(0.6f));
        g.fillRoundedRectangle(x - 3.f, meter.getY() - 2.f, 6.f, meter.getHeight() + 4.f, 2.f);
    }
}

// ============================================================================ Editor

PitchLaneEditor::PitchLaneEditor(PitchLaneProcessor& p) : AudioProcessorEditor(&p), proc_(p), roll_(p)
{
    setWantsKeyboardFocus(true);
    addAndMakeVisible(roll_);
    addAndMakeVisible(readout_);
    addAndMakeVisible(transportLabel_);
    transportLabel_.setJustificationType(Justification::centredRight);
    transportLabel_.setFont(FontOptions(13.f));

    setupCombo(key_, params::key, "Key");
    setupCombo(scale_, params::scale, "Scale");
    setupCombo(names_, params::noteNames, "Names");
    setupSlider(tempo_, params::tempo, "Tempo", Slider::LinearBar);
    setupSlider(low_, params::lowNote, "Low", Slider::IncDecButtons);
    setupSlider(high_, params::highNote, "High", Slider::IncDecButtons);
    setupSlider(tolerance_, params::tolerance, "Tolerance +/-", Slider::LinearBar);
    setupSlider(calibration_, params::calibration, "Calibration", Slider::LinearBar);
    setupSlider(offset_, params::refOffset, "Ref offset", Slider::LinearBar);
    setupSlider(transpose_, params::transpose, "Transpose", Slider::IncDecButtons);
    setupSlider(gate_, params::gateDb, "Gate", Slider::LinearBar);
    setupSlider(clarity_, params::clarity, "Clarity", Slider::LinearBar);
    setupSlider(span_, params::viewSeconds, "View", Slider::LinearBar);

    tempo_.slider.setTooltip("Used only when the host provides no tempo (e.g. Standalone).");
    calibration_.slider.setTooltip("Shifts your live pitch line in time to compensate for input/monitoring latency.");
    offset_.slider.setTooltip("Shifts the reference notes relative to the song (drag, or double-click to type).");
    clarity_.slider.setTooltip("Higher = only very clean, periodic sound shows a pitch (suppresses breaths/noise).");
    gate_.slider.setTooltip("Input level below which no pitch is shown.");

    for (auto* b : { &loadVocalBtn_, &analyzeBtn_, &cancelBtn_, &importMidiBtn_, &exportMidiBtn_, &deleteBtn_, &upBtn_,
                     &downBtn_, &earlierBtn_, &laterBtn_, &undoBtn_, &redoBtn_, &clearNotesBtn_, &clearTraceBtn_, &restartBtn_ })
    {
        addAndMakeVisible(*b);
        b->setWantsKeyboardFocus(false);
    }
    addAndMakeVisible(followBtn_);
    followBtn_.setToggleState(true, dontSendNotification);
    addAndMakeVisible(progressBar_);
    progressBar_.setPercentageDisplay(true);
    addAndMakeVisible(statusLabel_);
    addAndMakeVisible(sourceLabel_);
    statusLabel_.setFont(FontOptions(12.f));
    sourceLabel_.setFont(FontOptions(12.f));

    analyzeBtn_.setColour(TextButton::buttonColourId, Colour(0xff2563eb));
    loadVocalBtn_.setTooltip("Pick an isolated lead-vocal audio file (wav, aiff, mp3, m4a, flac...). You can also drag it onto the window.");
    analyzeBtn_.setTooltip("Convert the vocal file into editable target notes (runs in the background).");
    importMidiBtn_.setTooltip("Load the reference melody from a MIDI file.");
    exportMidiBtn_.setTooltip("Save the reference notes (with offset/transpose applied) as a MIDI file.");

    auto& model = proc_.getReference();
    loadVocalBtn_.onClick = [this] { chooseVocalFile(); };
    analyzeBtn_.onClick = [this] { startAnalysis(); };
    cancelBtn_.onClick = [this] { proc_.getAnalysis().cancel(); };
    importMidiBtn_.onClick = [this] { importMidi(); };
    exportMidiBtn_.onClick = [this] { exportMidi(); };
    deleteBtn_.onClick = [&model] { model.deleteSelected(); };
    upBtn_.onClick = [&model] { model.nudgeSelected(1, 0.0); };
    downBtn_.onClick = [&model] { model.nudgeSelected(-1, 0.0); };
    earlierBtn_.onClick = [&model] { model.nudgeSelected(0, -0.01); };
    laterBtn_.onClick = [&model] { model.nudgeSelected(0, 0.01); };
    undoBtn_.onClick = [&model] { model.undo(); };
    redoBtn_.onClick = [&model] { model.redo(); };
    clearNotesBtn_.onClick = [&model] { model.clear(); };
    clearTraceBtn_.onClick = [this] { roll_.clearTrace(); };
    restartBtn_.onClick = [this] { proc_.restartFreeClock(); roll_.clearTrace(); };
    followBtn_.onClick = [this] { roll_.setFollow(followBtn_.getToggleState()); };

    setResizable(true, true);
    setResizeLimits(900, 560, 2400, 1600);
    setSize(1180, 720);
    updateSourceLabel();
    startTimerHz(60);
}

PitchLaneEditor::~PitchLaneEditor()
{
    stopTimer();
}

void PitchLaneEditor::setupSlider(LabeledSlider& s, const char* paramId, const String& label, Slider::SliderStyle style)
{
    s.label.setText(label, dontSendNotification);
    s.label.setFont(FontOptions(12.f));
    s.label.setJustificationType(Justification::centredRight);
    s.slider.setSliderStyle(style);
    s.slider.setTextBoxStyle(style == Slider::LinearBar ? Slider::TextBoxLeft : Slider::TextBoxLeft, false, 52, 22);
    s.slider.setWantsKeyboardFocus(false);
    addAndMakeVisible(s.label);
    addAndMakeVisible(s.slider);
    s.attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(proc_.getApvts(), paramId, s.slider);
}

void PitchLaneEditor::setupCombo(LabeledCombo& c, const char* paramId, const String& label)
{
    c.label.setText(label, dontSendNotification);
    c.label.setFont(FontOptions(12.f));
    c.label.setJustificationType(Justification::centredRight);
    if (auto* choice = dynamic_cast<AudioParameterChoice*>(proc_.getApvts().getParameter(paramId)))
        c.combo.addItemList(choice->choices, 1);
    c.combo.setWantsKeyboardFocus(false);
    addAndMakeVisible(c.label);
    addAndMakeVisible(c.combo);
    c.attachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment>(proc_.getApvts(), paramId, c.combo);
}

void PitchLaneEditor::paint(Graphics& g)
{
    g.fillAll(Colour(0xff101216));
    if (dragHover_)
    {
        g.setColour(Colour(0xff2563eb));
        g.drawRect(getLocalBounds(), 3);
    }
}

void PitchLaneEditor::resized()
{
    auto b = getLocalBounds().reduced(8);

    // Header: readout + transport info.
    auto header = b.removeFromTop(72);
    readout_.setBounds(header.removeFromLeft(jmin(640, header.getWidth() * 2 / 3)));
    transportLabel_.setBounds(header.reduced(6, 0));
    b.removeFromTop(6);

    auto place = [](Label& l, Component& c, Rectangle<int>& row, int labelW, int w) {
        l.setBounds(row.removeFromLeft(labelW));
        row.removeFromLeft(4);
        c.setBounds(row.removeFromLeft(w).reduced(0, 2));
        row.removeFromLeft(8);
    };

    auto row1 = b.removeFromTop(28);
    place(key_.label, key_.combo, row1, 28, 60);
    place(scale_.label, scale_.combo, row1, 38, 140);
    place(tempo_.label, tempo_.slider, row1, 44, 110);
    place(low_.label, low_.slider, row1, 30, 100);
    place(high_.label, high_.slider, row1, 32, 100);
    place(tolerance_.label, tolerance_.slider, row1, 84, 110);
    place(names_.label, names_.combo, row1, 44, jmax(120, row1.getWidth()));

    auto row2 = b.removeFromTop(28);
    place(calibration_.label, calibration_.slider, row2, 70, 110);
    place(offset_.label, offset_.slider, row2, 64, 120);
    place(transpose_.label, transpose_.slider, row2, 64, 100);
    place(gate_.label, gate_.slider, row2, 34, 90);
    place(clarity_.label, clarity_.slider, row2, 46, 90);
    place(span_.label, span_.slider, row2, 34, 90);
    followBtn_.setBounds(row2.removeFromLeft(80));
    b.removeFromTop(4);

    auto row3 = b.removeFromTop(28);
    auto btn = [&row3](Component& c, int w) { c.setBounds(row3.removeFromLeft(w).reduced(1, 1)); row3.removeFromLeft(4); };
    btn(loadVocalBtn_, 100);
    btn(analyzeBtn_, 110);
    btn(cancelBtn_, 64);
    btn(progressBar_, 150);
    btn(importMidiBtn_, 100);
    btn(exportMidiBtn_, 100);
    row3.removeFromLeft(10);
    btn(restartBtn_, 104);
    statusLabel_.setBounds(row3);

    auto row4 = b.removeFromTop(28);
    auto btn4 = [&row4](Component& c, int w) { c.setBounds(row4.removeFromLeft(w).reduced(1, 1)); row4.removeFromLeft(4); };
    btn4(deleteBtn_, 64);
    btn4(upBtn_, 52);
    btn4(downBtn_, 52);
    btn4(earlierBtn_, 70);
    btn4(laterBtn_, 70);
    btn4(undoBtn_, 52);
    btn4(redoBtn_, 52);
    btn4(clearNotesBtn_, 90);
    btn4(clearTraceBtn_, 90);
    sourceLabel_.setBounds(row4);
    b.removeFromTop(6);

    roll_.setBounds(b);
}

bool PitchLaneEditor::keyPressed(const KeyPress& key)
{
    return roll_.handleKey(key);
}

void PitchLaneEditor::timerCallback()
{
    roll_.update();
    const auto conv = roundToInt(proc_.getApvts().getRawParameterValue(params::noteNames)->load()) == 1
                          ? OctaveConvention::Yamaha : OctaveConvention::Scientific;
    readout_.setReadout(roll_.computeReadout(), conv, proc_.getApvts().getRawParameterValue(params::tolerance)->load());
    if (followBtn_.getToggleState() != roll_.getFollow()) followBtn_.setToggleState(roll_.getFollow(), dontSendNotification);

    auto& an = proc_.getAnalysis();
    const bool running = an.isRunning();
    progress_ = running ? an.getProgress() : (an.getStatus() == AnalysisManager::Status::Finished ? 1.0 : 0.0);
    analyzeBtn_.setEnabled(!running);
    loadVocalBtn_.setEnabled(!running);
    cancelBtn_.setEnabled(running);
    auto& model = proc_.getReference();

    if (++tick_ % 6 == 0)
    {
        const auto t = proc_.getTransport();
        const int mins = static_cast<int>(t.songTime / 60.0);
        const double secs = t.songTime - mins * 60.0;
        String src = t.source == static_cast<int>(TimeSource::FreeRunning) ? "free clock (no host timeline)"
                   : t.source == static_cast<int>(TimeSource::HostPpq) ? "host beats" : "host";
        String state = t.recording ? "REC" : (t.playing ? "PLAY" : "STOP");
        transportLabel_.setText(state + "  " + String(mins) + ":" + String(secs, 2).paddedLeft('0', 5) + "   "
                                    + String(t.bpm, 1) + " bpm" + (t.looping ? "   LOOP" : "") + "\n" + src
                                    + "   |   " + String(model.size()) + " notes",
                                dontSendNotification);
        restartBtn_.setVisible(t.source == static_cast<int>(TimeSource::FreeRunning));

        String status = an.getStatusText();
        if (!running && status.isEmpty() && pendingVocal_ != File())
            status = "Ready: click Analyze Vocal for " + pendingVocal_.getFileName();
        statusLabel_.setText(status, dontSendNotification);
        undoBtn_.setEnabled(model.canUndo());
        redoBtn_.setEnabled(model.canRedo());
        const bool anySel = model.numSelected() > 0;
        for (auto* b2 : { &deleteBtn_, &upBtn_, &downBtn_, &earlierBtn_, &laterBtn_ }) b2->setEnabled(anySel);
    }
    if (tick_ % 60 == 0) updateSourceLabel();
}

void PitchLaneEditor::updateSourceLabel()
{
    const String path = proc_.getReference().getSourcePath();
    if (path.isEmpty())
    {
        sourceLabel_.setText(pendingVocal_ != File() ? "Selected: " + pendingVocal_.getFileName() : "No source file",
                             dontSendNotification);
        sourceLabel_.setColour(Label::textColourId, Colours::white.withAlpha(0.6f));
        return;
    }
    const File f(path);
    if (f.existsAsFile())
    {
        sourceLabel_.setText("Source: " + f.getFileName(), dontSendNotification);
        sourceLabel_.setColour(Label::textColourId, Colours::white.withAlpha(0.7f));
    }
    else
    {
        sourceLabel_.setText("Source file missing: " + f.getFileName() + " (notes are saved in the project and still work)",
                             dontSendNotification);
        sourceLabel_.setColour(Label::textColourId, Colour(0xfffb923c));
    }
    sourceLabel_.setTooltip(path);
}

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
    updateSourceLabel();
    statusLabel_.setText("Ready: click Analyze Vocal for " + f.getFileName(), dontSendNotification);
}

void PitchLaneEditor::chooseVocalFile()
{
    chooser_ = std::make_unique<FileChooser>("Choose an isolated lead-vocal file", File(),
                                             proc_.getAnalysis().getWildcard());
    chooser_->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                          [this](const FileChooser& fc) {
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
    if (!proc_.getAnalysis().start(f))
        statusLabel_.setText("An analysis is already running", dontSendNotification);
}

void PitchLaneEditor::importMidi()
{
    chooser_ = std::make_unique<FileChooser>("Import reference melody (MIDI)", File(), "*.mid;*.midi;*.smf");
    chooser_->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                          [this](const FileChooser& fc) {
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
        AlertWindow::showMessageBoxAsync(MessageBoxIconType::WarningIcon, "Import MIDI",
                                         f.getFileName() + ": " + String(parsed->error));
        return;
    }

    auto apply = [this, parsed, f](int track) {
        auto notes = referenceNotesFromMidi(*parsed, track);
        if (notes.empty())
        {
            AlertWindow::showMessageBoxAsync(MessageBoxIconType::InfoIcon, "Import MIDI", "No notes found in " + f.getFileName());
            return;
        }
        proc_.getReference().setNotes(std::move(notes), true);
        proc_.getReference().setSourcePath(f.getFullPathName());
        pendingVocal_ = File();
        updateSourceLabel();
        statusLabel_.setText("Imported " + String(proc_.getReference().size()) + " notes from " + f.getFileName(),
                             dontSendNotification);
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
    menu.showMenuAsync(PopupMenu::Options().withTargetComponent(&importMidiBtn_), [apply](int result) {
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
        statusLabel_.setText("Nothing to export", dontSendNotification);
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
    chooser_ = std::make_unique<FileChooser>("Export reference melody", File::getSpecialLocation(File::userMusicDirectory)
                                                                              .getChildFile("PitchLane Reference.mid"),
                                             "*.mid");
    chooser_->launchAsync(FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles
                              | FileBrowserComponent::warnAboutOverwriting,
                          [this, mapped, bpm](const FileChooser& fc) {
                              auto f = fc.getResult();
                              if (f == File()) return;
                              if (!f.hasFileExtension("mid;midi")) f = f.withFileExtension("mid");
                              const auto bytes = writeMidiFile(mapped, bpm);
                              const bool ok = f.replaceWithData(bytes.data(), bytes.size());
                              statusLabel_.setText(ok ? "Exported " + String(static_cast<int>(mapped.size())) + " notes to " + f.getFileName()
                                                      : "Could not write " + f.getFullPathName(),
                                                   dontSendNotification);
                          });
}

} // namespace pitchlane
