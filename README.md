# Pitch Lane

A real-time singing-coach audio plugin (JUCE / C++) for Logic Pro on macOS.

Pitch Lane sits on a vocal track as an Audio Unit effect, passes the audio through
**completely unchanged**, and shows the detected vocal pitch as a line on a scrolling
piano roll against reference melody target notes, with flat / in-tune / sharp feedback
in cents.

Status: under construction. Full build and usage docs arrive with the first feature PR.

## License

Pitch Lane is licensed under the **GNU Affero General Public License v3.0** (see `LICENSE`).

Why AGPLv3: Pitch Lane is built on [JUCE 8](https://juce.com), which is dual-licensed
under AGPLv3 or a commercial JUCE licence. This project uses JUCE under its AGPLv3
option, so the plugin as a whole must be distributed under AGPLv3-compatible terms with
its full source code available. That is exactly what this public repo does. Personal use
(building it and using it in your own Logic projects) is unrestricted. If you ever want to
distribute Pitch Lane as closed source, you would need a commercial JUCE licence first.
