// Plugin-level tests (headless, Linux/macOS): pass-through bit-exactness, state round
// trip, bus layouts, live frames through the real processor, background analysis.

#include <juce_audio_processors/juce_audio_processors.h>

#include <random>

#include "../../core/tests/TestFramework.h"
#include "../../core/tests/TestSignals.h"
#include "AnalysisManager.h"
#include "Params.h"
#include "PluginProcessor.h"
#include "StateCodec.h"
#include "Markers.h"
#include "PluginEditor.h"
#include "UiModel.h"
#include "pitchlane/NoteMath.h"

using namespace pitchlane;

namespace {

struct FakePlayHead : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        if (!valid) return {};
        PositionInfo p;
        p.setTimeInSeconds(time);
        p.setPpqPosition(time * bpm / 60.0);
        p.setBpm(bpm);
        p.setIsPlaying(playing);
        p.setIsRecording(recording);
        p.setIsLooping(looping);
        if (looping) p.setLoopPoints(LoopPoints { 0.0, 8.0 });
        return p;
    }
    bool valid = true, playing = true, recording = false, looping = false;
    double time = 0.0, bpm = 120.0;
};

void fillRandom(juce::AudioBuffer<float>& b, uint32_t seed)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> d(-1.f, 1.f);
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i) b.setSample(ch, i, d(rng));
    // A few special values that a careless gain stage or denormal flush would change.
    b.setSample(0, 0, 1.0e-39f);   // denormal
    b.setSample(0, 1, -0.0f);
    b.setSample(0, 2, 1.0f);
    b.setSample(0, 3, -1.0f);
    if (b.getNumSamples() > 4) b.setSample(0, 4, 3.4e38f);
}

bool bitIdentical(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples()) return false;
    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        if (std::memcmp(a.getReadPointer(ch), b.getReadPointer(ch), sizeof(float) * static_cast<size_t>(a.getNumSamples())) != 0)
            return false;
    return true;
}

void runPassThrough(int channels)
{
    PitchLaneProcessor proc;
    const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(set);
    layout.outputBuses.add(set);
    CHECK(proc.setBusesLayout(layout));
    CHECK_EQ(proc.getTotalNumInputChannels(), channels);
    CHECK_EQ(proc.getTotalNumOutputChannels(), channels);

    FakePlayHead ph;
    proc.setPlayHead(&ph);
    proc.prepareToPlay(48000.0, 512);
    CHECK_EQ(proc.getLatencySamples(), 0);

    juce::MidiBuffer midi;
    uint32_t seed = 1;
    bool allSame = true;
    // Include block sizes larger than announced, odd sizes, 1-sample blocks, and a musical
    // signal (so the detector is actually busy) as well as random noise.
    for (int blockSize : { 512, 1, 17, 256, 1024, 4096, 480 })
    {
        for (int rep = 0; rep < 20; ++rep)
        {
            juce::AudioBuffer<float> buf(channels, blockSize);
            if (rep % 2 == 0)
            {
                fillRandom(buf, seed++);
            }
            else
            {
                const auto tone = testsig::steady(48000.0, blockSize / 48000.0 + 0.001, 69, 6, 0.5);
                for (int ch = 0; ch < channels; ++ch)
                    for (int i = 0; i < blockSize; ++i) buf.setSample(ch, i, tone[static_cast<size_t>(i)] * (ch + 1) * 0.5f);
            }
            juce::AudioBuffer<float> original;
            original.makeCopyOf(buf);
            proc.processBlock(buf, midi);
            ph.time += blockSize / 48000.0;
            if (!bitIdentical(buf, original)) allSame = false;
        }
    }
    CHECK(allSame);
    CHECK(midi.isEmpty());
    proc.setPlayHead(nullptr);
}

} // namespace

TEST_CASE("Plugin: processBlock is bit-identical pass-through (mono)") { runPassThrough(1); }
TEST_CASE("Plugin: processBlock is bit-identical pass-through (stereo)") { runPassThrough(2); }

TEST_CASE("Plugin: bus layouts mono->mono and stereo->stereo only")
{
    PitchLaneProcessor proc;
    auto layout = [](juce::AudioChannelSet in, juce::AudioChannelSet out) {
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add(in);
        l.outputBuses.add(out);
        return l;
    };
    CHECK(proc.checkBusesLayoutSupported(layout(juce::AudioChannelSet::mono(), juce::AudioChannelSet::mono())));
    CHECK(proc.checkBusesLayoutSupported(layout(juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo())));
    CHECK(!proc.checkBusesLayoutSupported(layout(juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo())));
    CHECK(!proc.checkBusesLayoutSupported(layout(juce::AudioChannelSet::create5point1(), juce::AudioChannelSet::create5point1())));
    CHECK(!proc.acceptsMidi());
    CHECK(!proc.producesMidi());
    CHECK_EQ(proc.getTailLengthSeconds(), 0.0);
}

TEST_CASE("Plugin: state round trip (settings + embedded notes + source path)")
{
    PitchLaneProcessor a;
    auto setParam = [](PitchLaneProcessor& p, const char* id, float value) {
        auto* param = p.getApvts().getParameter(id);
        param->setValueNotifyingHost(param->convertTo0to1(value));
    };
    setParam(a, params::tolerance, 17.f);
    setParam(a, params::calibration, -42.f);
    setParam(a, params::refOffset, 1234.f);
    setParam(a, params::transpose, -3.f);
    setParam(a, params::key, 9.f);
    setParam(a, params::scale, 2.f);
    setParam(a, params::lowNote, 45.f);
    setParam(a, params::highNote, 84.f);
    setParam(a, params::tempo, 93.5f);
    setParam(a, params::gateDb, -61.f);
    setParam(a, params::clarity, 0.8f);
    setParam(a, params::noteNames, 1.f);

    NoteList notes;
    for (int i = 0; i < 300; ++i)
        notes.push_back({ i * 0.3371 + 0.123456789, 0.25 + (i % 7) * 0.01, 50 + (i * 7) % 30, 0.5f + (i % 5) * 0.1f, 64 + i % 60 });
    a.getReference().setNotes(notes, false);
    a.getReference().setSourcePath("/Users/twin/Music/Stems/Lead Vocal (missing).wav");

    juce::MemoryBlock blob;
    a.getStateInformation(blob);
    CHECK(blob.getSize() > 100);

    PitchLaneProcessor b;
    b.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));

    auto val = [](PitchLaneProcessor& p, const char* id) { return p.getApvts().getRawParameterValue(id)->load(); };
    for (auto* id : { params::tolerance, params::calibration, params::refOffset, params::transpose, params::key, params::scale,
                      params::lowNote, params::highNote, params::tempo, params::gateDb, params::clarity, params::noteNames })
        CHECK_NEAR(val(b, id), val(a, id), 1e-4);
    CHECK_NEAR(val(b, params::refOffset), 1234.0, 1e-3);
    CHECK_NEAR(val(b, params::transpose), -3.0, 1e-6);

    const auto back = b.getReference().getNotes();
    auto sorted = notes;
    sortNotes(sorted);
    CHECK_EQ(back.size(), sorted.size());
    bool same = back.size() == sorted.size();
    for (size_t i = 0; same && i < back.size(); ++i)
        same = std::abs(back[i].start - sorted[i].start) < 1e-9 && std::abs(back[i].length - sorted[i].length) < 1e-9
               && back[i].pitch == sorted[i].pitch && back[i].velocity == sorted[i].velocity
               && std::abs(back[i].confidence - sorted[i].confidence) < 1e-6f;
    CHECK(same);
    CHECK_EQ(b.getReference().getSourcePath(), juce::String("/Users/twin/Music/Stems/Lead Vocal (missing).wav"));

    // The source file doesn't exist, but the notes are fully usable (embedded).
    CHECK(!juce::File(b.getReference().getSourcePath()).existsAsFile());
    CHECK_EQ(b.getReference().size(), 300);

    // Garbage state is ignored safely.
    const char junk[] = "not a state";
    b.setStateInformation(junk, sizeof(junk));
    CHECK_EQ(b.getReference().size(), 300);
}

TEST_CASE("Plugin: live frames from the real processor (A4 = 440 Hz, song-time stamps, loop jump marker)")
{
    PitchLaneProcessor proc;
    FakePlayHead ph;
    ph.time = 10.0;
    proc.setPlayHead(&ph);
    proc.prepareToPlay(44100.0, 256);
    const auto tone = testsig::steady(44100.0, 1.0, 69, 6, 0.4);
    juce::MidiBuffer midi;
    for (size_t pos = 0; pos + 256 <= tone.size(); pos += 256)
    {
        juce::AudioBuffer<float> buf(2, 256);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 256; ++i) buf.setSample(ch, i, tone[pos + static_cast<size_t>(i)]);
        proc.processBlock(buf, midi);
        ph.time += 256 / 44100.0;
    }
    LiveFrame f;
    int jumps = 0, onTimeline = 0, near440 = 0;
    double firstT = -1, lastT = -1;
    while (proc.popFrame(f))
    {
        if (f.flags & LiveFrame::Jump) { ++jumps; continue; }
        if ((f.flags & LiveFrame::Playing) && (f.flags & LiveFrame::Voiced))
        {
            ++onTimeline;
            if (firstT < 0) firstT = f.songTime;
            lastT = f.songTime;
            if (std::abs(f.hz - 440.f) < 1.f && std::abs(centsFromNote(f.midi, 69)) < 3.0) ++near440;
        }
    }
    INFO("frames on timeline: " << onTimeline << ", first " << firstT << " s, last " << lastT << " s");
    CHECK_EQ(jumps, 1); // playback start
    CHECK(onTimeline > 150);
    CHECK(near440 >= onTimeline * 95 / 100);
    CHECK(firstT >= 10.0 && firstT < 10.1);
    CHECK(lastT > 10.9 && lastT <= 11.0);

    // Seek backwards -> Jump marker at the new position.
    ph.time = 2.0;
    juce::AudioBuffer<float> buf(2, 256);
    buf.clear();
    proc.processBlock(buf, midi);
    bool sawJump = false;
    while (proc.popFrame(f))
        if ((f.flags & LiveFrame::Jump) && std::abs(f.songTime - 2.0) < 1e-9) sawJump = true;
    CHECK(sawJump);

    // No playhead at all (e.g. some hosts / standalone) -> free-running clock, no crash.
    proc.setPlayHead(nullptr);
    proc.processBlock(buf, midi);
    CHECK(proc.getTransport().source == static_cast<int>(TimeSource::FreeRunning));
    CHECK(proc.getTransport().playing);
}

TEST_CASE("Plugin: background Analyze Vocal on a WAV file")
{
    const double sr = 44100.0;
    const std::vector<testsig::MelodyNote> truth = { { 0.2, 0.4, 60 }, { 0.8, 0.4, 64 }, { 1.4, 0.6, 67 } };
    const auto audio = testsig::melody(sr, 2.2, truth);
    auto file = juce::File::createTempFile(".wav");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os = file.createOutputStream();
        CHECK(os != nullptr);
        auto writer = wav.createWriterFor(os, juce::AudioFormatWriterOptions {}.withSampleRate(sr).withNumChannels(2).withBitsPerSample(24));
        CHECK(writer != nullptr);
        juce::AudioBuffer<float> b(2, static_cast<int>(audio.size()));
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i) b.setSample(ch, i, audio[static_cast<size_t>(i)]);
        writer->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
    }

    AnalysisManager am;
    CHECK(am.canOpen(file));
    CHECK(am.start(file));
    for (int i = 0; i < 1000 && am.isRunning(); ++i) juce::Thread::sleep(10);
    CHECK(!am.isRunning());
    CHECK(am.getStatus() == AnalysisManager::Status::Finished);
    INFO("status: " << am.getStatusText());
    CHECK(am.getStatusText().startsWith("3 notes"));

    // Missing file fails gracefully.
    AnalysisManager am2;
    CHECK(am2.start(juce::File("/nonexistent/vocal.wav")));
    for (int i = 0; i < 500 && am2.isRunning(); ++i) juce::Thread::sleep(10);
    CHECK(am2.getStatus() == AnalysisManager::Status::Failed);

    // Cancel works.
    const auto longAudio = testsig::steady(sr, 60.0, 62, 4);
    auto longFile = juce::File::createTempFile(".wav");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os = longFile.createOutputStream();
        auto writer = wav.createWriterFor(os, juce::AudioFormatWriterOptions {}.withSampleRate(sr).withNumChannels(1).withBitsPerSample(16));
        juce::AudioBuffer<float> b(1, static_cast<int>(longAudio.size()));
        b.copyFrom(0, 0, longAudio.data(), b.getNumSamples());
        writer->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
    }
    AnalysisManager am3;
    CHECK(am3.start(longFile));
    juce::Thread::sleep(30);
    am3.cancel();
    for (int i = 0; i < 1000 && am3.isRunning(); ++i) juce::Thread::sleep(10);
    CHECK(am3.getStatus() == AnalysisManager::Status::Cancelled);

    file.deleteFile();
    longFile.deleteFile();
}

TEST_CASE("Plugin: reference model edits + undo")
{
    ReferenceModel m;
    m.setNotes({ { 0.0, 0.5, 60 }, { 1.0, 0.5, 62 }, { 2.0, 0.5, 64 } }, false);
    m.selectOnly(1);
    m.deleteSelected();
    CHECK_EQ(m.size(), 2);
    m.selectAll();
    m.nudgeSelected(2, 0.1);
    auto n = m.getNotes();
    CHECK_EQ(n[0].pitch, 62);
    CHECK_NEAR(n[1].start, 2.1, 1e-12);
    CHECK(m.undo());
    CHECK(m.undo());
    CHECK_EQ(m.size(), 3);
    CHECK(m.redo());
    CHECK_EQ(m.size(), 2);
    m.selectInRange(1.5, 3.0, 0, 127, false);
    CHECK_EQ(m.numSelected(), 1);
}

// ---- UI model (restyle) -------------------------------------------------------------------------

TEST_CASE("UI: roll gesture mapping matches the hint bar")
{
    using ui::RollAction;
    auto act = [](bool onNote, bool edge, bool alt, bool shift, bool cmd, bool middle = false, bool ruler = false) {
        ui::PressInfo p;
        p.onNote = onNote; p.nearRightEdge = edge; p.alt = alt; p.shift = shift; p.cmd = cmd;
        p.middleButton = middle; p.onRuler = ruler;
        return ui::actionForPress(p);
    };
    // "Drag to adjust reference notes"
    CHECK(act(true, false, false, false, false) == RollAction::Move);
    // "Option-drag to create" (on empty space or over a note)
    CHECK(act(false, false, true, false, false) == RollAction::Create);
    CHECK(act(true, false, true, false, false) == RollAction::Create);
    // "Shift-drag to stretch" (and the right edge of a note)
    CHECK(act(true, false, false, true, false) == RollAction::Stretch);
    CHECK(act(true, true, false, false, false) == RollAction::Stretch);
    // "Double-click to delete"
    CHECK(ui::actionForDoubleClick(true) == ui::DoubleClickAction::DeleteNote);
    CHECK(ui::actionForDoubleClick(false) == ui::DoubleClickAction::None);
    // selection / navigation
    CHECK(act(true, false, false, false, true) == RollAction::ToggleSelect);
    CHECK(act(false, false, false, false, false) == RollAction::Rubber);
    CHECK(act(false, false, false, true, false) == RollAction::RubberAdd);
    CHECK(act(false, false, false, false, true) == RollAction::RubberAdd);
    CHECK(act(true, false, true, true, true, true) == RollAction::Pan);   // middle button always pans
    CHECK(act(false, false, false, false, false, false, true) == RollAction::Pan); // ruler drag pans

    // Option-drag spans in either direction, never negative, minimum length.
    auto s1 = ui::createdSpan(2.0, 3.5);
    CHECK_NEAR(s1.start, 2.0, 1e-12);
    CHECK_NEAR(s1.length, 1.5, 1e-12);
    auto s2 = ui::createdSpan(2.0, 1.0);
    CHECK_NEAR(s2.start, 1.0, 1e-12);
    CHECK_NEAR(s2.length, 1.0, 1e-12);
    auto s3 = ui::createdSpan(0.01, -0.5);
    CHECK_NEAR(s3.start, 0.0, 1e-12);
    CHECK(s3.length >= 0.05 - 1e-12);
    // Scroll buttons and ⌘ + scroll zoom.
    CHECK_NEAR(ui::scrollStep(8.0, 1), 2.0, 1e-12);
    CHECK_NEAR(ui::scrollStep(8.0, -1), -2.0, 1e-12);
    CHECK(ui::zoomedSpan(8.0, 1.f) < 8.0);
    CHECK(ui::zoomedSpan(8.0, -1.f) > 8.0);
    CHECK_NEAR(ui::zoomedSpan(2.0, 1.f), 2.0, 1e-12);   // clamped
    CHECK_NEAR(ui::zoomedSpan(30.0, -1.f), 30.0, 1e-12);
}

TEST_CASE("UI: bar ruler maths, pitch window, labels")
{
    ui::BarGrid g { 104.0, 4, 4 };
    CHECK_NEAR(g.secondsPerBeat(), 60.0 / 104.0, 1e-12);
    CHECK_NEAR(g.secondsPerBar(), 4 * 60.0 / 104.0, 1e-12);
    CHECK_EQ(g.barAt(0.0), 1);
    CHECK_EQ(g.barAt(g.barStart(15)), 15);
    CHECK_EQ(g.barAt(g.barStart(15) - 1e-6), 14);
    ui::BarGrid g68 { 120.0, 6, 8 };
    CHECK_NEAR(g68.secondsPerBar(), 6 * 0.25, 1e-12);   // six eighth notes at 120 bpm

    // Whole range fits -> shown entirely.
    auto w = ui::fitPitchWindow(48, 72, 500.f, 15.f, 60.0);
    CHECK_EQ(w.lo, 48);
    CHECK_EQ(w.hi, 72);
    // Too tall -> window around the centre, clamped to the range.
    w = ui::fitPitchWindow(36, 84, 300.f, 15.f, 60.0);
    CHECK_EQ(w.hi - w.lo + 1, 20);
    CHECK(w.lo <= 60 && w.hi >= 60);
    w = ui::fitPitchWindow(36, 84, 300.f, 15.f, 30.0);
    CHECK_EQ(w.lo, 36);
    w = ui::fitPitchWindow(36, 84, 300.f, 15.f, 200.0);
    CHECK_EQ(w.hi, 84);
    // Phrase zoom: a narrow phrase gets at least 12-13 rows.
    CHECK_EQ(ui::rowsForPhrase(62, 71), 15);
    CHECK_EQ(ui::rowsForPhrase(64, 65), 13);
    w = ui::fitPitchWindow(48, 72, 500.f, 15.f, 66.5, ui::rowsForPhrase(62, 71));
    CHECK_EQ(w.hi - w.lo + 1, 15);
    CHECK(w.lo <= 62 && w.hi >= 71);

    CHECK_EQ(ui::keyScaleText(9, ScaleType::NaturalMinor), juce::String("A minor"));
    CHECK_EQ(ui::keyScaleText(0, ScaleType::Major), juce::String("C major"));
    CHECK_EQ(ui::rangeText(48, 72, OctaveConvention::Scientific), juce::String(juce::CharPointer_UTF8("C3 \xe2\x80\x93 C5")));
    CHECK_EQ(ui::rangeText(48, 72, OctaveConvention::Yamaha), juce::String(juce::CharPointer_UTF8("C2 \xe2\x80\x93 C4")));
    CHECK_EQ(ui::signedMsText(42.0), juce::String("+42 ms"));
    CHECK_EQ(ui::signedMsText(0.0), juce::String("0 ms"));
    CHECK_EQ(ui::signedMsText(-8.0), juce::String(juce::CharPointer_UTF8("\xe2\x88\x92" "8 ms")));
    CHECK_EQ(ui::semitoneText(0), juce::String("0 st"));
    CHECK_EQ(ui::centsText(-18.2), juce::String(juce::CharPointer_UTF8("\xe2\x88\x92" "18 cents")));
    CHECK_NEAR(ui::meterFraction(0.0), 0.5f, 1e-6);
    CHECK_NEAR(ui::meterFraction(50.0), 1.f, 1e-6);
    CHECK_NEAR(ui::meterFraction(-80.0), 0.f, 1e-6);
    // Scales guide: nearest in-scale note (A minor has no G#, so 67.6 -> G, 68.4 -> A).
    CHECK_EQ(ui::nearestScaleNote(67.6, 9, ScaleType::NaturalMinor), 67);
    CHECK_EQ(ui::nearestScaleNote(68.4, 9, ScaleType::NaturalMinor), 69);
    CHECK_EQ(ui::nearestScaleNote(60.8, 0, ScaleType::Major), 60);
    CHECK_EQ(ui::nearestScaleNote(61.2, 0, ScaleType::Major), 62);
}

TEST_CASE("UI: section markers (user-editable, sorted, letters, persistence)")
{
    MarkerModel m;
    CHECK_EQ(m.size(), 0);  // never any built-in / demo sections
    const int c = m.add(40.0, "Chorus");
    const int a = m.add(10.0, "Verse 1");
    const int b = m.add(25.0, "  Pre-Chorus  ");
    CHECK_EQ(c, 0);
    CHECK_EQ(a, 0);
    CHECK_EQ(b, 1);
    auto ms = m.getMarkers();
    CHECK_EQ(ms.size(), static_cast<size_t>(3));
    CHECK_EQ(ms[0].name, juce::String("Verse 1"));
    CHECK_EQ(ms[1].name, juce::String("Pre-Chorus"));   // trimmed
    CHECK_EQ(ms[2].name, juce::String("Chorus"));
    CHECK_EQ(MarkerModel::letterFor(0), juce::String("A"));
    CHECK_EQ(MarkerModel::letterFor(2), juce::String("C"));
    CHECK_EQ(MarkerModel::letterFor(25), juce::String("Z"));
    CHECK_EQ(MarkerModel::letterFor(26), juce::String("AA"));
    CHECK_EQ(MarkerModel::letterFor(27), juce::String("AB"));
    CHECK_EQ(m.indexAt(5.0), -1);
    CHECK_EQ(m.indexAt(10.0), 0);
    CHECK_EQ(m.indexAt(30.0), 1);
    CHECK_EQ(m.indexAt(99.0), 2);

    CHECK(m.rename(2, "Chorus 1"));
    CHECK(m.rename(1, "   "));                 // empty -> default "Section"
    CHECK_EQ(m.getMarkers()[1].name, juce::String("Section"));
    CHECK(!m.rename(7, "x"));
    CHECK_EQ(m.move(0, 50.0), 2);               // moved past the others, re-sorted
    CHECK_EQ(m.getMarkers()[2].name, juce::String("Verse 1"));
    CHECK_EQ(m.move(2, -5.0), 0);               // clamped to 0
    CHECK_NEAR(m.getMarkers()[0].time, 0.0, 1e-12);
    CHECK(m.remove(1));
    CHECK(!m.remove(5));
    CHECK_EQ(m.size(), 2);

    // Tree round trip.
    MarkerModel m2;
    m2.fromTree(m.toTree());
    CHECK_EQ(m2.size(), 2);
    CHECK_EQ(m2.getMarkers()[1].name, m.getMarkers()[1].name);
    CHECK_NEAR(m2.getMarkers()[1].time, m.getMarkers()[1].time, 1e-12);

    // Saved with the plugin state, together with the new UI parameters.
    PitchLaneProcessor p1;
    p1.getMarkers().add(12.5, "Verse 1");
    p1.getMarkers().add(31.25, "Bridge");
    auto setParam = [](PitchLaneProcessor& p, const char* id, float value) {
        auto* prm = p.getApvts().getParameter(id);
        prm->setValueNotifyingHost(prm->convertTo0to1(value));
    };
    setParam(p1, params::smoothing, 35.f);
    setParam(p1, params::guide, 1.f);
    setParam(p1, params::display, 2.f);
    setParam(p1, params::hostSync, 0.f);
    juce::MemoryBlock blob;
    p1.getStateInformation(blob);
    PitchLaneProcessor p2;
    p2.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    CHECK_EQ(p2.getMarkers().size(), 2);
    CHECK_EQ(p2.getMarkers().getMarkers()[1].name, juce::String("Bridge"));
    CHECK_NEAR(p2.getMarkers().getMarkers()[0].time, 12.5, 1e-12);
    auto val = [](PitchLaneProcessor& p, const char* id) { return p.getApvts().getRawParameterValue(id)->load(); };
    CHECK_NEAR(val(p2, params::smoothing), 35.f, 1e-4);
    CHECK_NEAR(val(p2, params::guide), 1.f, 1e-6);
    CHECK_NEAR(val(p2, params::display), 2.f, 1e-6);
    CHECK_NEAR(val(p2, params::hostSync), 0.f, 1e-6);

    // A v1 state (no markers) loads and clears markers.
    PitchLaneProcessor p3;
    p3.getMarkers().add(1.0, "stale");
    auto tree = state::makeStateTree(p1.getApvts(), p1.getReference(), nullptr);
    CHECK(!tree.getChildWithName(MarkerModel::treeType).isValid());
    CHECK(state::applyStateTree(tree, p3.getApvts(), p3.getReference(), &p3.getMarkers()));
    CHECK_EQ(p3.getMarkers().size(), 0);
}

TEST_CASE("Plugin: Logic Sync off + smoothing changes keep bit-identical pass-through and 0 latency")
{
    PitchLaneProcessor proc;
    FakePlayHead ph;
    ph.time = 5.0;
    proc.setPlayHead(&ph);
    proc.prepareToPlay(48000.0, 256);
    CHECK_EQ(proc.getLatencySamples(), 0);
    auto* sync = proc.getApvts().getParameter(params::hostSync);
    auto* smooth = proc.getApvts().getParameter(params::smoothing);
    juce::MidiBuffer midi;
    bool same = true;
    for (int i = 0; i < 200; ++i)
    {
        if (i == 50) sync->setValueNotifyingHost(0.f);
        if (i % 20 == 0) smooth->setValueNotifyingHost(static_cast<float>((i / 20) % 5) / 4.f);
        if (i == 150) sync->setValueNotifyingHost(1.f);
        juce::AudioBuffer<float> buf(2, 256);
        fillRandom(buf, static_cast<uint32_t>(100 + i));
        if (i % 2 == 1)
        {
            const auto tone = testsig::steady(48000.0, 256 / 48000.0 + 0.001, 64, 6, 0.5);
            for (int ch = 0; ch < 2; ++ch)
                for (int k = 0; k < 256; ++k) buf.setSample(ch, k, tone[static_cast<size_t>(k)]);
        }
        juce::AudioBuffer<float> orig;
        orig.makeCopyOf(buf);
        proc.processBlock(buf, midi);
        ph.time += 256 / 48000.0;
        if (!bitIdentical(buf, orig)) same = false;
        if (i == 100)
        {
            // Sync off: the host timeline is ignored, the free clock runs.
            CHECK(proc.getTransport().source == static_cast<int>(TimeSource::FreeRunning));
        }
        if (i == 199)
        {
            CHECK(proc.getTransport().source != static_cast<int>(TimeSource::FreeRunning));
            CHECK_EQ(proc.getTransport().timeSigNum, 4);
        }
    }
    CHECK(same);
    CHECK_EQ(proc.getLatencySamples(), 0);
    proc.setPlayHead(nullptr);
}

TEST_CASE("UI: editor builds, lays out and paints at default and minimum size")
{
    PitchLaneProcessor proc;
    proc.getReference().setNotes({ { 0.5, 0.5, 60 }, { 1.0, 0.5, 64 } }, false);
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditorAndMakeActive());
    CHECK(ed != nullptr);
    for (auto size : { juce::Point<int>(PitchLaneEditor::kDefaultW, PitchLaneEditor::kDefaultH),
                       juce::Point<int>(PitchLaneEditor::kMinW, PitchLaneEditor::kMinH) })
    {
        ed->setSize(size.x, size.y);
        bool inside = true, nonEmpty = true;
        for (auto* c : ed->getChildren())
        {
            if (!c->isVisible() || dynamic_cast<juce::ResizableCornerComponent*>(c) != nullptr) continue;
            if (!ed->getLocalBounds().contains(c->getBounds())) inside = false;
            if (c->getWidth() < 12 || c->getHeight() < 12) nonEmpty = false;
        }
        CHECK(inside);
        CHECK(nonEmpty);
        const auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
        CHECK_EQ(img.getWidth(), size.x);
    }
    ed.reset();
    proc.editorBeingDeleted(nullptr);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    return tf::runAll("PitchLaneTests");
}
