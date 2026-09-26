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

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    return tf::runAll("PitchLaneTests");
}
