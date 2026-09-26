# Pitch Lane: architecture

```
pitchlane/
├── core/                    portable C++17 library, no JUCE (builds and tests on Linux/macOS/Windows)
│   ├── include/pitchlane/
│   │   ├── NoteMath.h          Hz <-> MIDI, cents, note names (C4 / Logic-style C3), keys & scales, flat/in-tune/sharp
│   │   ├── Yin.h               YIN difference / CMNDF / threshold / parabolic refinement (shared by both detectors)
│   │   ├── Filters.h           biquads + anti-alias decimator
│   │   ├── PitchDetector.h     real-time YIN, preallocated, runs inside processBlock
│   │   ├── PitchSmoother.h     median-5 + one-pole (cents) + jump snap, for the display line
│   │   ├── SpscRing.h          lock-free SPSC ring + seqlock (audio -> UI)
│   │   ├── Transport.h         host playhead -> song seconds, seek/loop/stop/record detection, free-run fallback
│   │   ├── ReferenceNotes.h    target note model + lookup + clean-up helpers
│   │   ├── OfflineAnalyzer.h   "Analyze Vocal": pYIN-style candidates + HMM/Viterbi + note segmentation
│   │   └── MidiFile.h          dependency-free SMF reader/writer (tempo maps, running status, SMPTE)
│   └── tests/                  pitchlane_core_tests (own tiny test harness, no deps)
├── plugin/                  JUCE 8 plugin (AU + Standalone; VST3 behind PITCHLANE_VST3)
│   ├── src/
│   │   ├── PluginProcessor     pass-through AU effect; detector + transport on the audio thread
│   │   ├── PluginEditor        controls, readout, file workflows (Load Vocal / Analyze / MIDI import-export)
│   │   ├── PianoRoll           scrolling roll, keyboard, target bars, trace, mouse/keyboard editing
│   │   ├── ReferenceModel      notes + selection + undo (message thread, lock-protected)
│   │   ├── AnalysisManager     background thread: decode audio file -> OfflineAnalyzer -> notes
│   │   ├── StateCodec          ValueTree/XML state (parameters + embedded notes + source path)
│   │   └── Params              parameter layout (APVTS)
│   └── tests/PluginTests.cpp   headless tests of the real processor (pass-through, state, frames, analysis)
├── ci/macos-au.yml          macOS CI (universal AU build, auval, artifact). Move to .github/workflows/ to enable, see ci/README.md
└── docs/
```

## Threads and data flow

```
 Audio thread (processBlock)                         Message thread (editor, 60 Hz timer)
 ───────────────────────────                         ────────────────────────────────────
 buffer ──(never written: bit-identical)──> host
    │
    ├─ mono mix (preallocated) ─> PitchDetector ─> PitchSmoother ─┐
    │                                                             ├─> SpscRing<LiveFrame, 8192> ─> PianoRoll history
 AudioPlayHead ─> TransportMapper ─── songTime, jumps ────────────┘                               + readout
                          └────────> SeqLock<TransportSnapshot> ───────────────────────────────> playhead / BPM / loop

 Background thread (AnalysisManager)                 ReferenceModel (notes, undo), lock-protected,
 decode file -> mono -> analyzeMonophonic() ──async──> never touched by the audio thread
```

- **Audio thread rules.** `processBlock` allocates nothing, takes no locks and does no I/O. All buffers are sized in
  `prepareToPlay`. Oversized host blocks are processed in chunks. The only outputs are a lock-free ring push and a
  seqlock store. Frames are dropped if the UI isn't draining (editor closed).
- **Pass-through.** JUCE wrappers process in place, and the processor never writes to the buffer, so output == input
  bit for bit. `PluginTests` checks this with `memcmp` for mono and stereo, with random data, denormals, ±0,
  full-scale values and odd block sizes. Latency is reported as 0.
- **Pitch detection.** Input is high-passed at 50 Hz, low-passed (4th-order Butterworth, ≤ 5 kHz) and decimated to about
  22–24 kHz (factor 2 at 44.1/48 k, 4 at 88.2/96 k). YIN uses a 21 ms integration window, lags down to 65 Hz
  (≈ 38 ms of audio in total), a hop of about 5 ms, and parabolic refinement on the raw difference function. The cost is
  about 0.2 M multiply-adds per hop. Frames are unvoiced when RMS < gate (default −50 dBFS) or CMNDF aperiodicity >
  1 − clarity (default clarity 0.70). Each frame is timestamped with the song time of the **centre** of its analysis
  window (latency compensated), so the line sits where the note was sung.
- **Display smoothing.** Median of 5 frames (≈ 25 ms), then a one-pole filter with τ = 10 ms in the cents domain. Jumps
  over 80 cents snap immediately. Unvoiced frames reset the filter. Measured in tests: vibrato ±50 c at 5.5 Hz keeps
  92 of 100 cents p-p, and a 14 st/s slide lags ≈ 24 ms with < 1 c residual.
- **Transport.** Uses `timeInSeconds` if present, else `ppq × 60 / bpm`, else a free-running clock (Standalone, or hosts
  without a timeline). Manual tempo is used when the host gives no BPM. A *jump* is flagged at play/record start, on
  seeks (|Δ| > max(4 ms, ½ block)) and on loop wraps. The UI then drops the old trace from that point so a looped
  section is redrawn cleanly. Frames whose analysis window straddles a jump are kept for the readout but not drawn on
  the timeline.
- **Timing controls.** *Calibration (ms)* shifts the live trace. It is applied at draw time, so changing it moves the
  trace you already sang. *Ref offset (ms)* and *Transpose (st)* map the stored reference notes to song time and
  target pitch without changing them. MIDI export applies both.
- **Tuning evaluation (UI thread).** Target = the reference note active at the frame's calibrated song time (or,
  while stopped, the note under the parked playhead). cents = 100 × (midi − target). In tune if |cents| ≤ tolerance
  (default ±10 c). The trace is coloured green (in tune), blue (flat), orange (sharp) or white (no target).

## Offline analyzer ("Analyze Vocal")

1. Decode (JUCE `AudioFormatManager`: WAV/AIFF/FLAC/Ogg/MP3, plus M4A/AAC/CAF through Core Audio on macOS), mix to mono.
   Limit: 20 min.
2. Filter and decimate to about 11–12 kHz. Frames: 30 ms window, 5 ms hop, centred timestamps.
3. **pYIN-style candidates.** Every CMNDF local minimum is a candidate. Its probability comes from a Beta(2,18) prior
   over 100 YIN thresholds. Frames below max(−55 dBFS, loud-level − 40 dB) are unvoiced.
4. **Viterbi** over 20-cent bins (65–1100 Hz) plus one unvoiced state. Triangular ±30-bin transitions plus a small
   uniform floor allow leaps. Voicing switch probability is 0.02 per frame.
5. **Segmentation.** A voiced run is split where the pitch leaves the running median of the current note by more than
   60 cents for at least 60 ms in one direction. Vibrato and scoops don't split a note. Note pitch = median of its frames.
6. **Edges.** Onsets/offsets that border silence are refined from a 10 ms energy envelope (≤ 80 ms, −15 dB re
   note level), because voicing itself starts about half a window late.
7. **Clean-up.** Merge same-pitch notes across gaps ≤ 60 ms. Drop notes < 80 ms or with mean voicing probability < 0.35.

Measured on the synthetic 8-note test melody (vibrato, legato, 150 ms note): all notes and pitches correct, onsets
within 5 ms (20 ms for the legato transition), about 40 ms of CPU for 4.3 s of audio.

## State (saved in the Logic project)

`getStateInformation` writes XML (via `copyXmlToBinary`):
`<PitchLaneState version="1"><Params …APVTS…/><Reference sourcePath="…"><Note s l p c v/>…</Reference></PitchLaneState>`.
The notes are embedded, so the source audio file is optional. If the stored path no longer exists, the UI shows
"Source file missing: … (notes are saved in the project and still work)".

## Adding VST3 later

`cmake -DPITCHLANE_VST3=ON …` adds `VST3` to `FORMATS`. Nothing in the code is AU-specific.
