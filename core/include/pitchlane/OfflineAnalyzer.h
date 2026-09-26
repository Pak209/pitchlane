#pragma once
// Offline monophonic transcription of an isolated lead-vocal stem into target notes.
//
// Pipeline (pYIN-inspired, no external dependencies):
//   1. high-pass / low-pass, decimate to ~11-12 kHz;
//   2. per 5 ms frame: YIN cumulative-mean-normalised difference; every local minimum is a
//      pitch candidate; candidate probabilities come from a Beta(2,18) prior over YIN
//      thresholds (as in pYIN, Mauch & Dixon 2014); frames below the RMS gate are unvoiced;
//   3. Viterbi decoding on an HMM of 20-cent pitch bins + an unvoiced state (smooth pitch
//      transitions, sticky voicing) -> a clean f0 track;
//   4. note segmentation: split voiced runs where the pitch departs from the running note
//      median by more than splitCents for at least splitHoldMs (vibrato and scoops stay in
//      one note), note pitch = median of its frames;
//   5. clean-up: merge same-pitch notes across short gaps, drop notes shorter than
//      minNoteMs or with low confidence.
// Runs on a background thread; reports progress and supports cancellation.

#include <functional>
#include <string>
#include <vector>

#include "pitchlane/ReferenceNotes.h"

namespace pitchlane {

struct AnalyzerSettings
{
    double minHz = 65.0;
    double maxHz = 1100.0;
    double hopMs = 5.0;
    double windowMs = 30.0;
    double gateDbAbsolute = -55.0;   // frames quieter than this are unvoiced
    double gateDbRelative = -40.0;   // ...or quieter than (loud reference level + this)
    double minNoteMs = 80.0;
    double mergeGapMs = 60.0;
    double minConfidence = 0.35;     // mean voicing probability of a note
    double splitCents = 60.0;
    double splitHoldMs = 60.0;
    double edgeRefineMs = 80.0;      // max onset/offset adjustment from the energy envelope
    double edgeRefineDb = -15.0;     // envelope threshold relative to the note's median level
    double voicingSwitchProb = 0.02; // HMM voiced<->unvoiced transition probability per frame
    int binsPerSemitone = 5;
    int maxJumpBins = 30;            // triangular pitch-transition width (bins per frame)
};

struct AnalysisFrame
{
    double time = 0.0;      // seconds, centre of the analysis window
    float midi = 0.f;       // decoded fractional MIDI pitch, 0 if unvoiced
    float voicedProb = 0.f; // per-frame voicing probability
    float rmsDb = -120.f;   // analysis-window RMS
    float shortRmsDb = -120.f; // 10 ms RMS around the frame centre (for onset/offset refinement)
};

struct AnalysisResult
{
    NoteList notes;
    std::vector<AnalysisFrame> frames;
    bool cancelled = false;
    std::string error;
};

/** Progress callback: receives 0..1, return false to cancel. */
using ProgressFn = std::function<bool(float)>;

AnalysisResult analyzeMonophonic(const float* mono, size_t numSamples, double sampleRate,
                                 const AnalyzerSettings& settings = {},
                                 const ProgressFn& progress = {});

/** Segmentation + clean-up only (steps 4-5), exposed for testing and re-running with new
    parameters without re-analysing audio. */
NoteList segmentNotes(const std::vector<AnalysisFrame>& frames, double hopSeconds,
                      const AnalyzerSettings& settings);

} // namespace pitchlane
