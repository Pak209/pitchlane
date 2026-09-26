// pitchlane_analyze: run the offline vocal analyzer on a WAV file (developer tool).
//   pitchlane_analyze <in.wav> <out-prefix> [key=value ...]
// Writes <prefix>.mid, <prefix>.notes.csv, <prefix>.frames.csv and prints a JSON summary.
// key=value overrides AnalyzerSettings fields (e.g. minNoteMs=100 splitCents=70).

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>

#include "WavIO.h"
#include "pitchlane/MidiFile.h"
#include "pitchlane/OfflineAnalyzer.h"

using namespace pitchlane;

static bool applySetting(AnalyzerSettings& s, const std::string& kv)
{
    const auto eq = kv.find('=');
    if (eq == std::string::npos) return false;
    const std::string k = kv.substr(0, eq);
    const double v = std::atof(kv.c_str() + eq + 1);
    std::map<std::string, double*> d {
        { "minHz", &s.minHz }, { "maxHz", &s.maxHz }, { "hopMs", &s.hopMs }, { "windowMs", &s.windowMs },
        { "gateDbAbsolute", &s.gateDbAbsolute }, { "gateDbRelative", &s.gateDbRelative },
        { "minNoteMs", &s.minNoteMs }, { "mergeGapMs", &s.mergeGapMs }, { "minConfidence", &s.minConfidence },
        { "splitCents", &s.splitCents }, { "splitHoldMs", &s.splitHoldMs }, { "edgeRefineMs", &s.edgeRefineMs },
        { "edgeRefineDb", &s.edgeRefineDb }, { "voicingSwitchProb", &s.voicingSwitchProb },
        { "unvoicedWeight", &s.unvoicedWeight }, { "octaveSlack", &s.octaveSlack },
        { "polyRatio", &s.polyRatio }, { "salienceSwitch", &s.salienceSwitch } };
    if (auto it = d.find(k); it != d.end()) { *it->second = v; return true; }
    if (k == "binsPerSemitone") { s.binsPerSemitone = static_cast<int>(v); return true; }
    if (k == "maxJumpBins") { s.maxJumpBins = static_cast<int>(v); return true; }
    return false;
}

int main(int argc, char** argv)
{
    if (argc < 3) { std::fprintf(stderr, "usage: %s in.wav out-prefix [key=value ...]\n", argv[0]); return 2; }
    AnalyzerSettings settings;
    for (int i = 3; i < argc; ++i)
        if (!applySetting(settings, argv[i])) { std::fprintf(stderr, "unknown setting %s\n", argv[i]); return 2; }

    const auto audio = tools::readWavMono(argv[1]);
    if (!audio.error.empty()) { std::fprintf(stderr, "error: %s\n", audio.error.c_str()); return 1; }

    const auto t0 = std::chrono::steady_clock::now();
    int lastPct = -1;
    const auto res = analyzeMonophonic(audio.samples.data(), audio.samples.size(), audio.sampleRate, settings,
                                       [&](float p) {
                                           const int pct = static_cast<int>(p * 10.f);
                                           if (pct != lastPct) { lastPct = pct; std::fprintf(stderr, "\r%3d%%", pct * 10); }
                                           return true;
                                       });
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::fprintf(stderr, "\n");
    if (!res.error.empty()) { std::fprintf(stderr, "error: %s\n", res.error.c_str()); return 1; }

    const std::string prefix = argv[2];
    {
        std::ofstream n(prefix + ".notes.csv");
        n << "start,length,pitch,confidence,flags\n";
        for (const auto& x : res.notes) n << x.start << ',' << x.length << ',' << x.pitch << ',' << x.confidence << ',' << static_cast<int>(x.flags) << '\n';
    }
    {
        std::ofstream fr(prefix + ".frames.csv");
        fr << "time,midi,voicedProb,rmsDb\n";
        for (const auto& x : res.frames) fr << x.time << ',' << x.midi << ',' << x.voicedProb << ',' << x.rmsDb << '\n';
    }
    {
        const auto bytes = writeMidiFile(res.notes, 120.0, 480, "Pitch Lane analysis");
        std::ofstream m(prefix + ".mid", std::ios::binary);
        m.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    int flagged = 0;
    for (const auto& x : res.notes) if (x.flags & RefNote::HarmonySuspect) ++flagged;
    std::printf("{\"file\":\"%s\",\"seconds\":%.3f,\"sampleRate\":%.0f,\"channels\":%d,\"notes\":%zu,"
                "\"harmonySuspects\":%d,\"analysisSeconds\":%.3f}\n",
                argv[1], audio.samples.size() / audio.sampleRate, audio.sampleRate, audio.channels,
                res.notes.size(), flagged, secs);
    return 0;
}
