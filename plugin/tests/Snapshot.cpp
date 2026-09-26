// Dev tool (not part of the test suite): renders the editor to a PNG after feeding a
// synthetic "sung" melody through the real processor, so the UI can be checked on a headless
// box (run under xvfb-run). Everything shown is TEST DATA: a made-up melody, a synthesised
// voice, and user-style section markers added by this tool.
//   PitchLaneSnapshot out.png [width height]

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <random>

#include "Params.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "SettingsPanel.h"
#include "UiModel.h"

using namespace pitchlane;

namespace {

constexpr double kBpm = 104.0;

struct PlayHead : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setTimeInSeconds(time);
        p.setPpqPosition(time * kBpm / 60.0);
        p.setBpm(kBpm);
        p.setTimeSignature(TimeSignature { 4, 4 });
        p.setIsPlaying(true);
        return p;
    }
    double time = 0.0;
};

void setParam(PitchLaneProcessor& p, const char* id, float v)
{
    auto* prm = p.getApvts().getParameter(id);
    prm->setValueNotifyingHost(prm->convertTo0to1(v));
}

/** Synthesised voice following a pitch contour (MIDI, 0 = silence): harmonics, breath noise,
    soft attack/release, a little random pitch wobble. */
std::vector<float> sing(double sr, double t0, double t1, const std::function<double(double)>& contour)
{
    std::vector<float> out(static_cast<size_t>((t1 - t0) * sr), 0.f);
    std::mt19937 rng(7);
    std::normal_distribution<double> noise(0.0, 1.0);
    double phase = 0.0, env = 0.0, wobble = 0.0;
    for (size_t i = 0; i < out.size(); ++i)
    {
        const double t = t0 + static_cast<double>(i) / sr;
        const double m = contour(t);
        const double targetEnv = m > 0.0 ? 1.0 : 0.0;
        env += (targetEnv - env) * (targetEnv > env ? 0.004 : 0.002);
        wobble += (noise(rng) * 350.0 - wobble) * 0.0004;       // random pitch jitter, a few cents
        if (m > 0.0)
        {
            const double hz = 440.0 * std::pow(2.0, (m + wobble * 0.01 - 69.0) / 12.0);
            phase += juce::MathConstants<double>::twoPi * hz / sr;
            if (phase > 1e6) phase = std::fmod(phase, juce::MathConstants<double>::twoPi);
        }
        double s = 0.0;
        for (int k = 1; k <= 6; ++k) s += std::sin(phase * k) / k;
        out[i] = static_cast<float>(env * (0.28 * s + 0.13 * noise(rng)));
    }
    return out;
}

} // namespace

bool writePng(const juce::Image& img, const juce::File& f)
{
    f.deleteFile();
    juce::FileOutputStream os(f);
    return os.openedOk() && juce::PNGImageFormat().writeImageToStream(img, os);
}

/** PL_ANALYZE=<audio file>: the "Dan in Logic" flow. Host stopped at bar 1 (120 BPM), default
    settings, Load Vocal + ANALYZE VOCAL through the editor; writes <out>-progress.png while the
    analysis runs and <out> after it finished (auto-fit). Dev tool only. */
int analyzeSnapshot(const juce::File& audio, const juce::File& out, int width, int height)
{
    struct Stopped : juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p;
            p.setTimeInSeconds(0.0);
            p.setPpqPosition(0.0);
            p.setBpm(120.0);
            p.setTimeSignature(TimeSignature { 4, 4 });
            p.setIsPlaying(false);
            return p;
        }
    } ph;
    PitchLaneProcessor proc;
    proc.setPlayHead(&ph);
    proc.prepareToPlay(44100.0, 512);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buf(2, 512);
    buf.clear();
    proc.processBlock(buf, midi);
    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditorAndMakeActive());
    editor->setSize(width, height);
    editor->setVisible(true);
    auto* ed = dynamic_cast<PitchLaneEditor*>(editor.get());
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    if (!ed->loadVocalFile(audio)) return 1;
    ed->startAnalysis();
    bool wroteProgress = false;
    while (proc.getAnalysis().isRunning() || proc.getLastAnalysis().serial == 0)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        buf.clear();
        proc.processBlock(buf, midi);
        if (!wroteProgress && proc.getAnalysis().getProgress() > 0.3f)
        {
            ed->refreshForTest();
            writePng(editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f),
                     out.getSiblingFile(out.getFileNameWithoutExtension() + "-progress.png"));
            wroteProgress = true;
        }
    }
    for (int i = 0; i < 10; ++i)
    {
        buf.clear();
        proc.processBlock(buf, midi);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    }
    const bool ok = writePng(editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f), out);
    if (proc.getReference().size() > 0)
    {
        // Also: scrolled back to bar 1 (what Dan saw), with the "notes outside this view" card.
        ed->getRoll().setViewStart(0.0);
        ed->getRoll().repaint();
        writePng(editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f),
                 out.getSiblingFile(out.getFileNameWithoutExtension() + "-away.png"));
    }
    std::printf("%s %s: %s\n", ok ? "wrote" : "FAILED", out.getFullPathName().toRawUTF8(), ed->getStatusMessage().toRawUTF8());
    editor.reset();
    proc.editorBeingDeleted(nullptr);
    return ok ? 0 : 1;
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File out(argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[1])
                                  : juce::File::getCurrentWorkingDirectory().getChildFile("snapshot.png"));
    const int width = argc > 3 ? std::atoi(argv[2]) : PitchLaneEditor::kDefaultW;
    const int height = argc > 3 ? std::atoi(argv[3]) : PitchLaneEditor::kDefaultH;

    if (const char* analyze = std::getenv("PL_ANALYZE"))
        return analyzeSnapshot(juce::File(juce::String::fromUTF8(analyze)), out, width, height);

    PitchLaneProcessor proc;
    const double secondsPerBar = 4 * 60.0 / kBpm;
    auto bar = [secondsPerBar](double b) { return (b - 1.0) * secondsPerBar; }; // fractional bar -> s

    // ---- settings (A minor, 104 BPM, C3-C5, +42 ms offset, ±15 cents) -------------------------
    setParam(proc, params::key, 9.f);
    setParam(proc, params::scale, static_cast<float>(ScaleType::NaturalMinor));
    setParam(proc, params::lowNote, 48.f);
    setParam(proc, params::highNote, 72.f);
    setParam(proc, params::refOffset, 42.f);
    setParam(proc, params::tolerance, 15.f);
    setParam(proc, params::viewSeconds, 13.f);
    setParam(proc, params::smoothing, 60.f);

    // ---- TEST DATA: reference melody (bars 15-20) --------------------------------------------
    struct N { double b0, b1; int pitch; };
    const std::vector<N> melody = {
        { 15.00, 15.48, 62 }, { 15.52, 15.98, 64 }, { 16.00, 16.37, 65 }, { 16.40, 16.97, 67 },
        { 17.00, 17.74, 69 }, { 17.77, 18.30, 67 }, { 18.33, 18.75, 64 }, { 18.77, 18.99, 65 },
        { 19.02, 19.50, 65 }, { 19.58, 19.73, 67 }, { 19.75, 19.98, 69 }, { 20.00, 20.30, 71 },
        { 20.32, 20.55, 69 },
    };
    NoteList ref;
    for (auto& n : melody) ref.push_back({ bar(n.b0), bar(n.b1) - bar(n.b0), n.pitch });
    {
        // TEST DATA: a muted harmony note (a third below the long A4), as Analyze Vocal flags them.
        RefNote h { bar(17.0), bar(17.74) - bar(17.0), 65, 0.6f, 70 };
        h.setFlag(RefNote::HarmonySuspect, true);
        h.setFlag(RefNote::Muted, true);
        auto withHarmony = ref;
        withHarmony.push_back(h);
        proc.getReference().setNotes(withHarmony, false);
    }
    const auto demoDir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("pitchlane-demo");
    demoDir.createDirectory();
    const auto stem = demoDir.getChildFile("Lead Vocal Stem.wav");
    stem.replaceWithText("test data placeholder");
    proc.getReference().setSourcePath(stem.getFullPathName());

    // TEST DATA: user-style section markers (in the plugin these are only ever created by the user).
    proc.getMarkers().add(bar(15.2), "Verse 1");
    proc.getMarkers().add(bar(18.5), "Pre-Chorus");
    proc.getMarkers().add(bar(19.85), "Chorus");

    // ---- TEST DATA: the "singer" -------------------------------------------------------------
    // Follows the melody (+42 ms reference offset) with scoops, some drift, and sings the E4
    // at bar 18.33 about 18 cents flat from its second half on (outside the ±15 cent tolerance).
    const double offset = 0.042;
    const double playhead = bar(18.62);
    auto contour = [&](double t) -> double {
        for (size_t i = 0; i < melody.size(); ++i)
        {
            const double s = bar(melody[i].b0) + offset, e = bar(melody[i].b1) + offset;
            const double nextS = i + 1 < melody.size() ? bar(melody[i + 1].b0) + offset : e;
            if (t < s || t >= (nextS - e < 0.1 ? nextS : e)) continue;
            double m = melody[i].pitch;
            const double prev = i > 0 ? melody[i - 1].pitch : m - 1.0;
            const double into = t - s;
            if (into < 0.07) m = prev + (m - prev) * (0.5 - 0.5 * std::cos(juce::MathConstants<double>::pi * into / 0.07));
            if (e - s > 1.5 && into > 0.5) m += 0.07 * std::sin(juce::MathConstants<double>::twoPi * 5.4 * into);  // vibrato
            if (i == 1) m -= 0.06;                                    // slightly under on the E4
            if (i == 6 && into > 0.12) m -= 0.18;                     // flat part
            return m;
        }
        return 0.0;
    };
    const double sr = 48000.0;
    const double startT = bar(14.85);
    const auto audio = sing(sr, startT, playhead, contour);

    if (std::getenv("PL_DEBUG") != nullptr)
    {
        PitchLaneProcessor dbg;
        PlayHead dph;
        dph.time = startT;
        dbg.setPlayHead(&dph);
        dbg.prepareToPlay(sr, 512);
        juce::MidiBuffer mb;
        for (size_t pos = 0; pos + 512 <= audio.size(); pos += 512)
        {
            juce::AudioBuffer<float> buf(2, 512);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i) buf.setSample(ch, i, audio[pos + static_cast<size_t>(i)]);
            dbg.processBlock(buf, mb);
            dph.time += 512 / sr;
        }
        LiveFrame f;
        int k = 0;
        while (dbg.popFrame(f))
            if ((++k % 25) == 0)
            {
                const int idx = findActiveNote(ref, f.songTime - offset);
                std::printf("t=%.3f midi=%.3f raw=%.3f conf=%.2f flags=%u note=%d cents=%.1f contour=%.3f\n", f.songTime, f.midi, f.rawMidi,
                            f.confidence, f.flags, idx >= 0 ? ref[(size_t) idx].pitch : -1,
                            idx >= 0 ? (f.midi - ref[(size_t) idx].pitch) * 100.0 : 0.0, contour(f.songTime));
            }
        dbg.setPlayHead(nullptr);
    }

    PlayHead ph;
    ph.time = startT;
    proc.setPlayHead(&ph);
    proc.prepareToPlay(sr, 512);

    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditorAndMakeActive());
    editor->setSize(width, height);
    editor->setVisible(true);
    auto* ed = dynamic_cast<PitchLaneEditor*>(editor.get());

    juce::MidiBuffer midi;
    const int block = 512;
    for (size_t pos = 0; pos + block <= audio.size(); pos += block)
    {
        juce::AudioBuffer<float> buf(2, block);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i) buf.setSample(ch, i, audio[pos + static_cast<size_t>(i)]);
        proc.processBlock(buf, midi);
        ph.time += block / sr;
        if ((pos / block) % 8 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil(3);
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    if (ed != nullptr) ed->getRoll().setViewStart(bar(15.0) - 0.25);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(120);

    if (const char* settingsOut = std::getenv("PL_SETTINGS_PNG"))
    {
        // Also render the settings popover content (for layout checks).
        SettingsPanel panel(proc, {});
        panel.setLookAndFeel(&editor->getLookAndFeel());
        juce::Image simg(juce::Image::ARGB, panel.getWidth() + 28, panel.getHeight() + 28, true);
        {
            juce::Graphics g(simg);
            g.fillAll(juce::Colour(0xff171d29));
            g.setOrigin(14, 14);
            panel.paintEntireComponent(g, true);
        }
        juce::File sf(settingsOut);
        sf.deleteFile();
        juce::FileOutputStream sos(sf);
        juce::PNGImageFormat().writeImageToStream(simg, sos);
        panel.setLookAndFeel(nullptr);
    }

    const auto img = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
    juce::PNGImageFormat png;
    out.deleteFile();
    juce::FileOutputStream os(out);
    const bool ok = os.openedOk() && png.writeImageToStream(img, os);
    std::printf("%s %s (%dx%d)\n", ok ? "wrote" : "FAILED", out.getFullPathName().toRawUTF8(), width, height);
    editor.reset();
    proc.editorBeingDeleted(nullptr);
    return ok ? 0 : 1;
}
