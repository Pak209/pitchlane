# Spotify Basic Pitch as an optional analyzer backend

**Status:** investigated, not implemented. The shipped analyzer is the native YIN/pYIN-style transcriber
(`core/src/OfflineAnalyzer.cpp`). Licence details are in `docs/LICENSES.md`.

## Should we add it?

**Recommendation:** not for the lead-vocal use case right now. Maybe later, as an opt-in "Polyphonic / robust"
backend.

| | Native pYIN-style (shipped) | Basic Pitch |
|---|---|---|
| Polyphony | Monophonic (fits a lead-vocal stem) | Polyphonic (would *add* harmony/bleed notes we then have to delete) |
| Dependencies | none | RTNeural + ONNX Runtime (custom static build), model files (~few hundred KB) |
| Binary / build complexity | trivial, universal build is free | ONNX Runtime needs a universal (arm64+x86_64) static lib. NeuralNote ships a custom one. Adds CI time |
| Pitch accuracy on sung vocals | sub-cent f0 per frame; note pitch = median (vibrato-safe) | Good onsets. Semitone-quantised notes plus pitch bends, and vibrato often splits into several notes |
| Robustness to stem-separation artefacts / reverb | relies on voicing + gates + clean-up, and manual delete | Better at ignoring noise, but will transcribe reverb/harmony as extra notes |
| Speed | ~0.5 s per minute of audio (measured: 4.3 s of audio in ~40 ms) | a few seconds per minute on CPU |

A lead-vocal stem is exactly the case a monophonic f0 tracker handles well. Basic Pitch's strength (polyphony,
many instruments) mostly produces false notes here. It would help mainly with badly separated or very noisy stems.

## If we add it: plan

1. **Interface.** Add `IAnalyzerBackend { AnalysisResult analyze(const float*, size_t, double sr, const ProgressFn&); }`
   in `core/`. `OfflineAnalyzer` becomes `PyinBackend`. `AnalysisManager` gets a backend choice (UI: combo next to
   *Analyze Vocal*). Results use the same `NoteList`, so editing, persistence and MIDI export need no changes.
2. **Model + runtime.** Take the approach from **NeuralNote v1.1.0** (Apache-2.0), not v2 (v2 switched to non-commercial
   MuScriptor weights):
   - CQT + harmonic-stacking front end: `features_model.ort` run with **ONNX Runtime** (MIT). Use a minimal static
     build (reduced operator set) compiled **universal** for macOS 11+.
   - CNN: the 4 sequential sub-models run with **RTNeural** (BSD-3-Clause). Header-only-ish, fine for universal builds.
   - Note creation: port Basic Pitch's `output_to_notes_polyphonic` (onset/frame thresholds, min note length,
     melodia trick) as NeuralNote `Notes.cpp` does.
   - Resample input to 22,050 Hz mono (Basic Pitch's rate) with `juce::LagrangeInterpolator` or a windowed-sinc
     resampler on the background thread.
3. **Monophonic reduction for vocals.** After Basic Pitch, keep the most salient note per time slice (highest
   amplitude × duration), then run our existing `mergeSamePitchGaps` / `removeShortNotes` clean-up.
4. **Build switch.** `option(PITCHLANE_BASIC_PITCH "Build the Basic Pitch backend" OFF)`. Fetch RTNeural and the ONNX
   Runtime static lib only when it is ON, so the default build stays dependency-free.
5. **Licensing chores.** Add `THIRD_PARTY_NOTICES.md` with the Apache-2.0 (Basic Pitch, NeuralNote code we adapt),
   BSD-3 (RTNeural) and MIT (ONNX Runtime) notices. Keep the Basic Pitch model files' Apache-2.0 headers. All of these are
   AGPLv3-compatible.
6. **Tests.** Reuse the synthetic-melody test with a looser onset tolerance. Add a two-voice test to check the
   monophonic reduction picks the louder line.

Estimated effort: 2–4 days, mostly the ONNX Runtime universal static build and CI.
