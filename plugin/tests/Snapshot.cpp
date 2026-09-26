// Dev tool: renders the editor to a PNG after feeding a synthetic "sung" melody through
// the real processor. Used to eyeball the UI on a headless box (run under xvfb-run).
//   PitchLaneSnapshot out.png

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../../core/tests/TestSignals.h"
#include "PluginProcessor.h"

using namespace pitchlane;

struct PlayHead : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setTimeInSeconds(time);
        p.setPpqPosition(time * 2.0);
        p.setBpm(120.0);
        p.setIsPlaying(true);
        p.setIsLooping(true);
        p.setLoopPoints(LoopPoints { 0.0, 16.0 });
        return p;
    }
    double time = 0.0;
};

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File out(argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[1])
                                  : juce::File::getCurrentWorkingDirectory().getChildFile("snapshot.png"));

    PitchLaneProcessor proc;
    // Reference melody (C major-ish phrase).
    NoteList ref = { { 0.5, 0.5, 60 }, { 1.0, 0.5, 62 }, { 1.5, 0.5, 64 }, { 2.0, 1.0, 67 },
                     { 3.0, 0.5, 65 }, { 3.5, 0.5, 64 }, { 4.0, 1.5, 62 }, { 5.6, 0.4, 60 },
                     { 6.0, 1.0, 64 }, { 7.0, 1.0, 67 } };
    ref[5].confidence = 0.4f;
    proc.getReference().setNotes(ref, false);
    proc.getReference().setSourcePath("/Users/twin/Stems/lead_vocal.wav");
    proc.getReference().selectOnly(3);

    // "Sung" version: in tune, sharp, flat, vibrato, a scoop and a gap.
    std::vector<testsig::MelodyNote> sung = {
        { 0.52, 0.46, 60 }, { 1.0, 0.5, 62 }, { 1.5, 0.48, 64 }, { 2.0, 1.0, 67, 35.0, 5.5 },
        { 3.0, 0.5, 65 }, { 3.5, 0.5, 64 }, { 4.05, 1.4, 62, 25.0, 5.0 }, { 5.6, 0.4, 60 },
    };
    const double sr = 48000.0;
    auto audio = testsig::melody(sr, 6.2, sung, 6, 0.4);
    // Detune: note 3 (E4) +22 cents sharp and note 6 (D4) -25 cents flat, via resynthesis.
    {
        auto sharp = testsig::melody(sr, 6.2, { { 1.5, 0.48, 64 } }, 6, 0.4);
        auto sharpT = testsig::tone(sr, 0.48, [](double) { return 64.22; }, 6, 0.4);
        auto flatT = testsig::tone(sr, 1.4, [](double t) { return 61.75 + 0.25 * std::sin(6.2831853 * 5.0 * t); }, 6, 0.4);
        for (size_t i = 0; i < sharpT.size(); ++i) audio[static_cast<size_t>(1.5 * sr) + i] = sharpT[i];
        for (size_t i = 0; i < flatT.size(); ++i) audio[static_cast<size_t>(4.05 * sr) + i] = flatT[i];
    }

    PlayHead ph;
    proc.setPlayHead(&ph);
    proc.prepareToPlay(sr, 512);

    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditorAndMakeActive());
    editor->setSize(1180, 720);
    editor->setVisible(true);

    juce::MidiBuffer midi;
    const int block = 512;
    const size_t stopAt = static_cast<size_t>((argc > 2 ? std::atof(argv[2]) : 4.9) * sr);
    for (size_t pos = 0; pos + block <= audio.size() && pos < stopAt; pos += block)
    {
        juce::AudioBuffer<float> buf(2, block);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i) buf.setSample(ch, i, audio[pos + static_cast<size_t>(i)]);
        proc.processBlock(buf, midi);
        ph.time += block / sr;
        if ((pos / block) % 8 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil(60);

    const auto img = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
    juce::PNGImageFormat png;
    out.deleteFile();
    juce::FileOutputStream os(out);
    const bool ok = os.openedOk() && png.writeImageToStream(img, os);
    std::printf("%s %s\n", ok ? "wrote" : "FAILED", out.getFullPathName().toRawUTF8());
    editor.reset();
    proc.editorBeingDeleted(nullptr);
    return ok ? 0 : 1;
}
