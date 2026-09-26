# Pitch Lane

A real-time **singing coach** Audio Unit for **Logic Pro on macOS** (JUCE 8 / C++17).

Insert Pitch Lane on a vocal track. It passes the audio through **bit-identical and unchanged, with zero latency**.
It never tunes or processes the voice. It shows:

- your live sung pitch as a line on a scrolling chromatic piano roll;
- the reference melody as target bars at their pitches and song times;
- while you hold a target note: **FLAT / IN TUNE / SHARP** plus the offset in cents (in tune = within ±10 cents by default, adjustable);
- key + scale row highlighting and a selectable vocal range (low/high note).

The reference melody can come from an **isolated lead-vocal stem** (drop it in, click **Analyze Vocal**, then edit the
notes) or from a **MIDI file** (Import MIDI). Everything, including the notes themselves, is saved in the Logic project.

> **Status (v0.1.0).** Core DSP, analyzer, MIDI, transport and state logic are unit-tested on Linux. The Linux
> Standalone builds. A macOS CI workflow (universal AU build + `auval`) is ready in [`ci/macos-au.yml`](ci/README.md)
> but **not active yet** (it needs a one-time `gh auth refresh -s workflow`). So the AU has **not yet been compiled on
> macOS or tried inside Logic Pro**: see [`docs/MAC_VALIDATION.md`](docs/MAC_VALIDATION.md).

---

## Build (macOS, for Logic)

Requirements: macOS 11+, Xcode 14+ command-line tools, CMake ≥ 3.22 (`brew install cmake ninja`).
JUCE 8.0.15 is downloaded automatically by CMake.

```bash
git clone https://github.com/Pak209/pitchlane.git
cd pitchlane
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --target PitchLane_AU PitchLane_Standalone
```

Output: `build/plugin/PitchLane_artefacts/Release/AU/PitchLane.component` (and a Standalone app next to it).
Options: `-DPITCHLANE_VST3=ON` adds VST3. `-DPITCHLANE_COPY_AFTER_BUILD=ON` installs automatically after each build.

### Install

The exact install path is:

```
~/Library/Audio/Plug-Ins/Components/PitchLane.component
```

```bash
mkdir -p ~/Library/Audio/Plug-Ins/Components
cp -R build/plugin/PitchLane_artefacts/Release/AU/PitchLane.component ~/Library/Audio/Plug-Ins/Components/
codesign --force --deep -s - ~/Library/Audio/Plug-Ins/Components/PitchLane.component   # ad-hoc sign
killall -9 AudioComponentRegistrar 2>/dev/null || true                                   # refresh the AU cache
auval -v aufx Plne Pk20                                                                    # should end in "AU VALIDATION SUCCEEDED"
```

**Using the CI build instead** (once [`ci/macos-au.yml`](ci/README.md) is enabled: Actions > *macOS AU* > artifact `PitchLane-AU-macOS-universal`): unzip it, copy
`PitchLane.component` to the path above, then remove the download quarantine and ad-hoc sign it (the build is
unsigned):

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/PitchLane.component
codesign --force --deep -s - ~/Library/Audio/Plug-Ins/Components/PitchLane.component
killall -9 AudioComponentRegistrar 2>/dev/null || true
```

The build is universal (Apple Silicon + Intel).

### If Logic doesn't show it / validation failed

1. **Logic Pro > Settings > Plug-in Manager.** Find **Pitch Lane** under the manufacturer **Pak209**.
2. Select it and click **Reset & Rescan Selection**. Make sure the **Use** checkbox is enabled.
3. Still failing: quit Logic, then in Terminal:
   ```bash
   killall -9 AudioComponentRegistrar
   auval -a | grep -i Plne          # is it registered at all?  expect: aufx Plne Pk20 - Pak209: Pitch Lane
   auval -v aufx Plne Pk20          # full validation log
   ```
4. Downloaded build: run the `xattr -dr com.apple.quarantine …` and `codesign --force --deep -s - …` commands
   above, then rescan.

In Logic the plug-in is under **Audio FX > Audio Units > Pak209 > Pitch Lane**.

---

## Using it

| Control | What it does |
|---|---|
| **Key / Scale** | Highlights in-scale rows (the root row is brighter). Visual only: the reference notes are the actual target. |
| **Low / High** | Visible vocal range of the roll. |
| **Tolerance ±** | Cents counted as "in tune" (default ±10). |
| **Tempo** | Manual BPM. Only used when the host provides none (for example the Standalone app). |
| **Names** | `Middle C = C4` (A4 = 440 Hz, scientific) or `Middle C = C3` (Logic's default display). |
| **Calibration (ms)** | Shifts your live line in time if it looks consistently early or late versus the targets (monitoring latency). |
| **Ref offset (ms)** | Moves the reference notes relative to the song (align an imported melody). |
| **Transpose (st)** | Transposes all reference notes (for example to sing a song in a different key). |
| **Gate / Clarity** | Raise these if breaths, noise or bleed show a pitch. Lower them if quiet singing is missed. |
| **View / Follow** | Seconds visible. Follow keeps the playhead in view (re-engages when playback starts). |

**Reference melody.**
- **Load Vocal…** (or drag an audio file onto the window: wav/aiff/flac/ogg/mp3, and m4a/aac/caf on macOS), then **Analyze Vocal**.
  Analysis runs in the background with a progress bar and **Cancel**.
- **Import MIDI…** (or drag a `.mid`). If the file has several tracks, you pick the lead track. **Export MIDI…** saves the
  notes with offset/transpose applied.
- **Editing.** Click a bar to select it (Shift/⌘ adds). Drag in empty space for a rubber-band selection. Drag a bar to move it in time and pitch,
  drag its right edge to change its length, double-click empty space to add a note. Buttons: **Delete**, **+1 st / −1 st**,
  **<< 10ms / 10ms >>**, **Undo / Redo**, **Clear Notes**. Keys (if Logic passes them through): Delete/Backspace,
  ↑/↓ (Shift = octave), ←/→ (Shift = 100 ms), ⌘A, ⌘Z / ⇧⌘Z, Esc. Mouse wheel pans (when not following),
  ⌘/Ctrl + wheel zooms, Alt-drag pans.
- Notes are stored **inside the Logic project**. If the original vocal file moves, you'll see
  "Source file missing" but the notes keep working.

Colours: **green** = in tune, **blue** = flat, **orange** = sharp, **white** = no target note at that moment.

---

## Test procedure for Twin (about 10 minutes)

1. Open a Logic project with a vocal track. On the track's **Audio FX** slot choose **Audio Units > Pak209 > Pitch Lane**.
   The sound must be exactly the same as without it.
2. Record-enable the track with input monitoring on (or play back a recorded vocal). **Sing an A** (the A above middle C,
   A4) and hold it: the readout should say **A4, ≈ 440 Hz, close to 0 cents**. (Set
   *Names* to "Middle C = C3" if you prefer Logic's naming. It will then read A3.)
3. **Import MIDI…** and choose a MIDI file of the melody (in Logic: select the melody region > File > Export >
   Selection as MIDI File). Bars appear on the roll.
4. **Play** from the start, then **seek** to the middle, then turn on **Cycle** over 2 bars. The target bars should
   stay locked to Logic's playhead, and each loop pass redraws your line. If the bars are offset from the song, adjust
   **Ref offset**.
5. Sing along: while holding a target note the readout says FLAT / IN TUNE / SHARP with cents, and your line turns
   green when in tune.
6. **Load Vocal…** an isolated lead-vocal stem, click **Analyze Vocal** (watch the progress bar; try **Cancel** once
   and then run it again). Notes appear as bars.
7. Find a **bad note** (for example a short blip from reverb or a harmony), click it, press **Delete** (button). Drag another
   note slightly, then **Undo**.
8. **Save** the project, close it, reopen it: the notes and settings are still there. Optionally rename the stem file
   and reopen to see the "Source file missing" message (the notes still work).

Report back: macOS version, Logic version, Apple Silicon or Intel, and anything in `docs/MAC_VALIDATION.md` that
didn't behave.

---

## Development

```bash
# Core library + tests only (no JUCE, any OS):
cmake -S . -B build-core -G Ninja -DPITCHLANE_BUILD_PLUGIN=OFF && cmake --build build-core && ctest --test-dir build-core
# Everything (Linux needs: libasound2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxrandr-dev
#   libxinerama-dev libxcursor-dev libxcomposite-dev libxext-dev libgl1-mesa-dev):
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build && ctest --test-dir build
```

Docs: [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) · [`docs/MAC_VALIDATION.md`](docs/MAC_VALIDATION.md) ·
[`docs/LICENSES.md`](docs/LICENSES.md) · [`docs/BASIC_PITCH.md`](docs/BASIC_PITCH.md)

## License

Pitch Lane is licensed under the **GNU Affero General Public License v3.0** (see `LICENSE`).

Why AGPLv3: Pitch Lane is built on [JUCE 8](https://juce.com), which is dual-licensed under AGPLv3 or a commercial
JUCE licence. This project uses JUCE under its AGPLv3 option, so the plugin as a whole must be distributed under
AGPLv3-compatible terms with its full source available, which is what this public repo does. Personal use (building
it and using it in your own Logic projects) is unrestricted. Distributing Pitch Lane as closed source would require a
commercial JUCE licence first. Details: [`docs/LICENSES.md`](docs/LICENSES.md).
