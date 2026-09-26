## Pitch Lane {{VERSION}}

A real-time singing coach for Logic Pro: it shows the pitch you sing against the reference melody.

### Install (about a minute)

1. **Download** `PitchLane-{{VERSION}}.pkg` below (under *Assets*).
2. **Double-click** the downloaded file to open the installer, then click **Continue** and **Install**.
3. When asked, **enter your Mac password** (the one you use to log in). It installs for all users, into `/Library/Audio/Plug-Ins/Components`.
4. **Restart Logic Pro** (quit it completely with ⌘Q, then open it again).
5. On a vocal track, click an empty **Audio FX** slot and choose **Pak209 > Pitch Lane** (in Logic's default menu it sits under **Audio Units > Pak209 > Pitch Lane**).

The installer is signed by Dan Kimoto and notarized by Apple, so macOS opens it without warnings.

### It doesn't show up in Logic?

1. In Logic, open **Logic Pro > Settings > Plug-in Manager** (older versions: *Preferences*).
2. Click **Pak209** in the left column. If Pitch Lane shows "failed validation", select it and click **Reset & Rescan Selection**.
3. If Pak209 isn't listed at all, click **Full Audio Unit Reset**, then quit and reopen Logic.

Apple's guides: [Use the Plug-in Manager](https://support.apple.com/guide/logicpro/use-the-plug-in-manager-lgcp9e26ef17/mac) · [If you can't find a recently installed plug-in](https://support.apple.com/en-us/122179)

If you installed an older copy by hand into your own `~/Library/Audio/Plug-Ins/Components` folder, delete that copy so Logic doesn't see two.

### Check the download (optional)

```
shasum -a 256 -c PitchLane-{{VERSION}}.pkg.sha256
```

Requires macOS 11 or later. Runs natively on Apple Silicon and Intel Macs.
