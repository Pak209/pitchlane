# What still has to be validated on a real Mac with Logic Pro

Everything below was **not** verifiable on the Linux build machine. The macOS CI workflow
(`.github/workflows/macos-au.yml`, see `ci/README.md`) compiles the universal AU and runs `auval` on every push. Even a green CI run only proves that the AU compiles as a
universal binary and passes `auval`. It says nothing about Logic's UI, transport or project saving.

## 0. First macOS build
- [x] CI enabled (`.github/workflows/macos-au.yml`): universal build + `auval` on `macos-14`.
- [ ] Build locally with the commands in the README (Xcode version on your Mac may differ from CI).

## 1. Install and scan
- [ ] Install `PitchLane.component` into `~/Library/Audio/Plug-Ins/Components/` (built locally or unzipped from the CI artifact).
- [ ] If downloaded from CI: `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/PitchLane.component`
      and `codesign --force --deep -s - ~/Library/Audio/Plug-Ins/Components/PitchLane.component`.
- [ ] `auval -v aufx Plne Pk20` passes on Twin's machine (Apple Silicon **and/or** Intel: the build is universal).
- [ ] Logic's Plug-in Manager lists **Pak209 > Pitch Lane** as validated. It appears under Audio FX > Audio Units > Pak209.

## 2. Audio path
- [ ] Insert on a **mono** vocal track and on a **stereo** track. Both instantiate, and the sound is unchanged.
- [ ] Null test: duplicate a track, insert Pitch Lane on one, invert phase on the other, then bounce. The result should be silence.
- [ ] Logic's plug-in latency display shows 0 samples.
- [ ] CPU meter stays low at 32/64/128-sample buffers and 44.1/48/96 kHz.

## 3. Live pitch
- [ ] Record-enable the track with input monitoring on. Sing/play an A4: readout ≈ 440 Hz, "A4" (or "A3" with
      *Names = Middle C = C3 (Logic)*), close to 0 cents.
- [ ] Silence and breaths show no pitch line. If they do, raise *Clarity* or *Gate*.
- [ ] Vibrato and slides look natural, neither jittery nor over-smoothed.

## 4. Transport
- [ ] Play from different positions: the playhead and notes line up with Logic's playhead.
- [ ] Seek while playing: the trace restarts at the new position.
- [ ] Cycle/loop mode: at each wrap the loop region's trace is redrawn. The loop region is shaded.
- [ ] Record: the trace draws while recording.
- [ ] Stop: the trace freezes, the readout still works (compares with the note under the playhead).
- [ ] Tempo changes / the tempo track: note positions come from Logic's `timeInSeconds`, so they should stay aligned.
      Only the beat grid assumes a constant tempo.
- [ ] Check timing: if the sung line is consistently early or late versus the target bars while *monitoring live*,
      set *Calibration* (typically +/− a few ms up to the I/O buffer latency) and note the value.

## 5. Reference workflow
- [ ] *Load Vocal…* / drag-drop of WAV, AIFF, MP3 and **M4A** (M4A decoding uses Core Audio, which is only
      available on macOS, so it is untested so far).
- [ ] *Analyze Vocal* on a real separated lead-vocal stem: the progress bar moves, *Cancel* works, and Logic stays
      responsive during analysis. Judge note quality (false notes from reverb/harmony, missed notes).
- [ ] Editing inside Logic: click/shift-click/rubber-band select, drag to move, drag the right edge to resize,
      double-click to add, and the on-screen *Delete / ±1 st / ±10 ms / Undo / Redo* buttons.
      **Keyboard shortcuts** (Delete, arrows, ⌘Z, ⌘A) may be captured by Logic. If so, use the buttons. It would be
      good to know whether Logic passes these keys to the plugin window.
- [ ] *Import MIDI…* of a region exported from Logic (File > Export > Selection as MIDI File). Check alignment with the
      song, and use *Ref offset* if the export didn't start at bar 1. Try the track-choice menu on a multi-track file.
- [ ] *Export MIDI…* then drag the file into Logic. It should land at the right bars.
- [ ] Native file dialogs open from inside the plugin window, including in Logic's sandbox.

## 6. Persistence
- [ ] Save the project, quit Logic, reopen: the notes, key/scale, range, tolerance, offsets, transpose and names
      convention are restored.
- [ ] Move or rename the source vocal file, reopen: the orange "Source file missing" label appears and the notes still work.
- [ ] Duplicate the track / copy the plugin to another track: its state is copied.

## 7. UI
- [ ] Resize the window (default 1240×720, minimum 1040×620). Check Retina rendering, the Inter font, and that the 60 Hz
      repaint doesn't load the CPU noticeably with a long song.
- [ ] Header: status dot green while Logic plays, BPM shows Logic's tempo, turning **LOGIC SYNC** off switches to the
      free clock (grey dot, BPM editable), turning it back on re-locks to Logic.
- [ ] Bar numbers match Logic's bars (constant tempo, including a 3/4 or 6/8 project).
- [ ] Hint-bar gestures inside Logic: drag, Option-drag (create), Shift-drag (stretch), double-click (delete),
      ⌘ + scroll (zoom), Scroll ← → buttons. Note whether Logic intercepts Option/Shift/⌘ with the plugin window focused.
- [ ] Settings popover (gear) opens inside the plugin window; all commands work; tooltips (ⓘ) appear.
- [ ] Section markers: add (double-click lane), rename, drag, right-click delete; saved with the project.
- [ ] Smoothing knob visibly changes how calm the live line is (0 % = raw).
- [ ] Close and reopen the plugin window during playback. The trace continues (up to about 40 s is kept while the window is closed).

## 8. Tempo map, key shading, formats, feedback, harmonies (v0.3)
Tempo and time signature:
- [ ] In a project with a **tempo change** (e.g. 90 → 120 at bar 9), play through the change: the bar numbers and grid
      line up with Logic's bars before and after it. Seek back before the change: still correct.
- [ ] Project with a **time-signature change** (4/4 → 7/8 at bar 5): the bars after the change are 7/8 long and
      numbered like Logic's.
- [ ] Projects in 3/4 and 6/8 at 60, 104 and 174 bpm: bar 1 starts at Logic's bar 1 and the numbers match.
- [ ] **Logic Sync** off: the BPM menu's *Time signature* submenu changes the grid (for example 6/8), saved with the project.
- [ ] Key = B♭ major in *Notes* mode: in-key rows are faintly lighter and the tonic tinted; *Scales* mode is unchanged.

Formats (Load Vocal / drag-drop → Analyze):
- [ ] Analyze an **M4A (AAC)** voice memo, an Apple Lossless M4A, an MP3, a 96 kHz WAV and a stereo AIFF: each finishes
      with a sensible note count.
- [ ] Drop a renamed text file (`fake.wav`): the status shows "…looks damaged…". Drop a `.txt`: nothing happens (not
      accepted). Cancel a 15-minute file during "Reading audio…" and during "Analyzing…": it stops within about a second.

Take feedback:
- [ ] Sing along with a reference, deliberately late on one note: that note gets an orange tick and "+xxx ms"; the left
      panel says "Late +… ms". On-time notes get green ticks. Skip a note: dashed outline, counted as missed.
- [ ] Change **Timing tolerance** in Settings (for example to 40 ms): the next take is judged against it.
- [ ] Seek / cycle wrap / stop and play: the ticks reset and a new take summary starts.
- [ ] With the **Timing calibration** set for your monitoring latency, on-time singing reads within about ±30 ms.

Harmonies:
- [ ] Import a MIDI part with two voices: the dialog offers *Lead only: top voice*, *Lead only: loudest / longest*,
      *Keep all voices, harmonies muted*, *Keep all voices*. Each result looks right.
- [ ] Analyze a stem with backing harmonies: suspected harmony notes are grey with an amber outline and are not judged.
- [ ] Select notes and press **M** (and use the right-click menu): they grey out; again: unmute. *Mute/Unmute
      harmonies* toggles all flagged notes; *Remove muted notes* deletes them; ⌘Z restores.
- [ ] Export MIDI: muted notes are not in the file. Save, reopen the project: muted notes are still muted.
