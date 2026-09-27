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
    double minConfidence = 0.05;     // mean voicing probability of a note (stems with backing: often 0.05-0.3)
    double steadyRescueCents = 35.0; // keep a note down to minConfidence/2 if >= 150 ms, >= 80 % voiced and its
                                     // frames stay within this median deviation of the note pitch (0 = off)
    double splitCents = 60.0;
    double splitHoldMs = 60.0;
    double edgeRefineMs = 80.0;      // max onset/offset adjustment from the energy envelope
    double edgeRefineDb = -15.0;     // envelope threshold relative to the note's median level
    double voicingSwitchProb = 0.02; // HMM voiced<->unvoiced transition probability per frame
    double unvoicedWeight = 0.02;    // scales the unvoiced-state observation (1 - voicing prob);
                                     // < 1 lets real (reverberant, breathy) vocals stay voiced
    int binsPerSemitone = 5;
    int maxJumpBins = 30;            // triangular pitch-transition width (bins per frame)

    // ---- real-vocal clean-up ----
    bool salienceCorrection = true;  // fix 2x/3x/4x-period (sub-octave) errors with a harmonic-sum check
    double salienceSwitch = 1.15;    // ...when a multiple explains the spectrum this much better
    bool removeSpikes = true;        // replace 1-3 frame pitch outliers (>= 6 st off both sides)
    bool fixOctaveErrors = false;    // fold short notes an octave away from both neighbours (off: the
                                     // salience check handles octave errors; folding can hide harmonies)
    bool detectHarmonies = true;     // spectral check for a second voice + melodic excursions
    bool muteHarmonySuspects = true; // suspects start muted (greyed, excluded from scoring)
    double polyRatio = 0.8;
    // Range outliers: a note shorter than 0.5 s this many semitones below / above the
    // duration-weighted median of the notes within +-4 s is flagged as a harmony suspect.
    int outlierBelow = 9;
    int outlierAbove = 12;
    double octaveSlack = 0.1;        // move a candidate to the dip at 1/2 or 1/3 of its lag when
                                     // that dip is within this much (CMND units); 0 = off          // secondary/primary harmonic salience that counts as a 2nd voice
};

struct AnalysisFrame
{
    double time = 0.0;      // seconds, centre of the analysis window
    float midi = 0.f;       // decoded fractional MIDI pitch, 0 if unvoiced
    float voicedProb = 0.f; // per-frame voicing probability
    float rmsDb = -120.f;   // analysis-window RMS
    float shortRmsDb = -120.f; // 10 ms RMS around the frame centre (for onset/offset refinement)
    float altMidi = 0.f;    // strongest other voice after cancelling this pitch's harmonics (0 = none)
    float altRatio = 0.f;   // its harmonic salience relative to the decoded pitch (checked every 4th voiced frame)
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

/** Replace short (1-3 frame) pitch outliers that sit >= 6 semitones away from both
    neighbouring frames (which agree with each other) by interpolation. */
void removePitchSpikes(std::vector<AnalysisFrame>& frames);

/** Fold short notes (< 0.8 s) that sit an octave away from both close neighbours (both required;
    agree with the folded pitch within 4 semitones) back into the neighbours' octave:
    typical YIN sub-octave / octave errors on real voices. Sorted input. Returns #fixed. */
int fixOctaveErrors(NoteList& notes);

/** Harmony-suspect detector for analysed stems. Flags RefNote::HarmonySuspect when
      - a short note (< 0.35 s) jumps >= 5 st away from both close neighbours which agree
        within 2 st (the transcriber briefly followed another voice), or
      - for most of the note a second voice is present (AnalysisFrame::altRatio >= polyRatio)
        and that voice is at least as strong and above the note (the note is likely a lower
        harmony under the lead), or clearly stronger (>= 1.25x) anywhere, or
      - a short (< 0.25 s) note with very low voicing confidence (< 0.15).
    Muted as well when settings.muteHarmonySuspects. Returns the number flagged. */
int markHarmonySuspects(NoteList& notes, const std::vector<AnalysisFrame>& frames, double hopSeconds,
                        const AnalyzerSettings& settings);

} // namespace pitchlane
