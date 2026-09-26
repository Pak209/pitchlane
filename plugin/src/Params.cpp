#include "Params.h"

#include "pitchlane/NoteMath.h"

namespace pitchlane::params {

using namespace juce;

AudioProcessorValueTreeState::ParameterLayout createLayout(std::function<int()> octaveConvention)
{
    AudioProcessorValueTreeState::ParameterLayout layout;
    const int version = 1;

    auto noteText = [octaveConvention](int v, int) {
        const auto conv = (octaveConvention && octaveConvention() == 1) ? OctaveConvention::Yamaha : OctaveConvention::Scientific;
        return String(noteName(v, conv));
    };
    auto noteFromText = [octaveConvention](const String& text) {
        // Accept a number or a note name like "C4" / "F#3".
        if (text.containsOnly("0123456789")) return text.getIntValue();
        const auto conv = (octaveConvention && octaveConvention() == 1) ? OctaveConvention::Yamaha : OctaveConvention::Scientific;
        for (int n = 0; n < 128; ++n)
            if (text.trim().equalsIgnoreCase(String(noteName(n, conv)))) return n;
        return 60;
    };

    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { tolerance, version }, "In-tune tolerance",
                                                     NormalisableRange<float>(1.f, 50.f, 1.f), 10.f,
                                                     AudioParameterFloatAttributes().withLabel("cents")));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { calibration, version }, "Timing calibration",
                                                     NormalisableRange<float>(-500.f, 500.f, 1.f), 0.f,
                                                     AudioParameterFloatAttributes().withLabel("ms")));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { refOffset, version }, "Reference offset",
                                                     NormalisableRange<float>(-60000.f, 60000.f, 1.f), 0.f,
                                                     AudioParameterFloatAttributes().withLabel("ms")));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { transpose, version }, "Reference transpose", -24, 24, 0,
                                                   AudioParameterIntAttributes().withLabel("st")));

    StringArray keys;
    for (int k = 0; k < 12; ++k) keys.add(pitchClassName(k));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { key, version }, "Key", keys, 0));

    StringArray scales;
    for (int s = 0; s < static_cast<int>(ScaleType::NumScales); ++s) scales.add(scaleName(static_cast<ScaleType>(s)));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { scale, version }, "Scale", scales,
                                                      static_cast<int>(ScaleType::Major)));

    layout.add(std::make_unique<AudioParameterInt>(ParameterID { lowNote, version }, "Range low", 24, 96, 48,
                                                   AudioParameterIntAttributes().withStringFromValueFunction(noteText)
                                                       .withValueFromStringFunction(noteFromText)));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { highNote, version }, "Range high", 24, 108, 72,
                                                   AudioParameterIntAttributes().withStringFromValueFunction(noteText)
                                                       .withValueFromStringFunction(noteFromText)));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { tempo, version }, "Manual tempo",
                                                     NormalisableRange<float>(30.f, 300.f, 0.1f), 120.f,
                                                     AudioParameterFloatAttributes().withLabel("bpm")));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { gateDb, version }, "Input gate",
                                                     NormalisableRange<float>(-80.f, -20.f, 1.f), -50.f,
                                                     AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { clarity, version }, "Clarity threshold",
                                                     NormalisableRange<float>(0.3f, 0.95f, 0.01f), 0.70f));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { noteNames, version }, "Note names",
                                                      StringArray { "Middle C = C4", "Middle C = C3 (Logic)" }, 0));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { viewSeconds, version }, "View span",
                                                     NormalisableRange<float>(2.f, 30.f, 0.5f), 8.f,
                                                     AudioParameterFloatAttributes().withLabel("s")));

    const int v2 = 2;
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { smoothing, v2 }, "Smoothing",
                                                     NormalisableRange<float>(0.f, 100.f, 1.f), 60.f,
                                                     AudioParameterFloatAttributes().withLabel("%")));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { guide, v2 }, "Guide",
                                                      StringArray { "Notes", "Scales" }, 0));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { display, v2 }, "Display",
                                                      StringArray { "Both", "Vocal", "Reference" }, 0));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { hostSync, v2 }, "Logic Sync", true));

    const int v3 = 3;
    StringArray sigs;
    for (const auto& s : timeSigChoices) sigs.add(String(s.num) + "/" + String(s.den));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { timeSig, v3 }, "Manual time signature", sigs,
                                                      defaultTimeSigIndex));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { timingTol, v3 }, "Timing tolerance",
                                                     NormalisableRange<float>(20.f, 300.f, 5.f), 80.f,
                                                     AudioParameterFloatAttributes().withLabel("ms")));
    return layout;
}

} // namespace pitchlane::params
