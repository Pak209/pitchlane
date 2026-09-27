## Pitch Lane {{VERSION}}

A singing coach for Logic Pro: it shows the pitch you sing against the melody you should be singing.

### Install (about a minute)

1. **Quit Logic Pro** (⌘Q).
2. **Download** `PitchLane-{{VERSION}}.pkg` below (under *Assets*) and **double-click** it. Click **Continue**, then **Install**.
3. **Enter your Mac password** when asked (the one you log in with).
4. **Open Logic Pro** again.
5. On an **audio track**, click an empty **Audio FX** slot and choose **Audio Units > Pak209 > Pitch Lane**.

Had an older copy? If a `PitchLane.component` is in your own `~/Library/Audio/Plug-Ins/Components` folder (Finder > Go > Go to Folder…), delete it so Logic doesn't see two.

### What it does

- **Live pitch line:** your voice drawn in real time, cyan when in tune, orange when you're off.
- **Analyze Vocal:** turns a lead-vocal stem (M4A, WAV, AIFF, MP3…) into reference notes you can edit.
- **Logic Sync:** follows Logic's play/stop, tempo and bars; the dot is green while playing, amber when stopped.
- **Take feedback:** each note is marked on time, early, late or missed, plus how sharp or flat it was.

### It doesn't show up in Logic?

1. In Logic, open **Logic Pro > Settings > Plug-in Manager**.
2. Click **Pak209** on the left. If Pitch Lane says "failed validation", select it and click **Reset & Rescan Selection**.
3. If Pak209 isn't listed, click **Full Audio Unit Reset**, then quit and reopen Logic.

### Something went wrong?

Pitch Lane keeps a small log at `~/Library/Logs/PitchLane/pitchlane.log`. Send that file along with a screenshot.

---

Signed by Dan Kimoto and notarized by Apple, so macOS opens it without warnings. Requires macOS 11 or later (Apple Silicon and Intel).
Check the download (optional): `shasum -a 256 -c PitchLane-{{VERSION}}.pkg.sha256`
