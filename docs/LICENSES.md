# Licences

Research date: 2026-09-26. This is an engineering summary, not legal advice.

## Pitch Lane itself: AGPLv3

Pitch Lane is released under the **GNU AGPL v3.0** (`LICENSE` in the repo root).

## JUCE 8 (the framework Pitch Lane is built on)

- JUCE 8 modules are **dual-licensed: AGPLv3 or the commercial JUCE 8 licence**
  (see `LICENSE.md` in the JUCE repo and <https://juce.com/legal/juce-8-licence/>).
  Pitch Lane uses JUCE under the **AGPLv3** option, pinned to tag `8.0.15` via CMake FetchContent.
- What that means here:
  - **Public repo:** fine. The full source is public under AGPLv3, which is what the licence requires.
  - **Personal use** (Dan and his twin building it and using it in their own Logic projects): no restrictions.
    The AGPL's obligations apply when you *distribute* the plugin, or offer it to others over a network.
  - **Sharing builds** (for example the CI artifact `.component` zip): allowed, as long as recipients can get the
    corresponding source (this repo) under AGPLv3.
  - **Closed-source or commercial distribution** would need a commercial JUCE licence (JUCE's free "Starter"
    tier or a paid tier, depending on revenue), and then Pitch Lane could be relicensed. Every file in this repo
    is ours, so relicensing later is possible.
- JUCE's bundled dependencies that Pitch Lane actually compiles in are permissive: AudioUnitSDK (Apache-2.0),
  FLAC/Ogg Vorbis (BSD), zlib/pnglib (zlib), HarfBuzz (Old MIT), SheenBidi (Apache-2.0), VST3 SDK (MIT, only if
  `PITCHLANE_VST3=ON`). The JUCE MP3 *decoder* (`JUCE_USE_MP3AUDIOFORMAT=1`) is part of JUCE. MP3 patents have expired.
  We do not use ASIO or AAX.

## Pitch Lane's own analyzer (shipped)

- Native C++ YIN / pYIN-style monophonic transcription (`core/src/OfflineAnalyzer.cpp`). It is written from the published
  algorithms (de Cheveigné & Kawahara 2002; Mauch & Dixon 2014) and uses **no third-party code or model weights**.
  Nothing extra to license.

## Spotify Basic Pitch (investigated, not shipped)

| Component | Licence | Notes |
|---|---|---|
| `spotify/basic-pitch` (Python library **and** the bundled model files `saved_models/icassp_2022/nmp.{onnx,tflite,mlpackage}` + SavedModel) | **Apache-2.0** | The model weights live in the Apache-2.0 repo. No separate weights licence was found. |
| `spotify/basic-pitch-ts` (TypeScript/TF.js port + model JSON) | Apache-2.0 | Source of the model graph NeuralNote v1 converted. |
| Basic Pitch Python runtime deps (TensorFlow / tflite-runtime / coremltools / onnxruntime, librosa, mir_eval, pretty_midi, resampy, scipy, numpy) | Apache-2.0 / MIT / ISC / BSD | Only relevant if we ran Python. We would not: we would run the model in C++. |
| **NeuralNote v1.x** (`DamRsn/NeuralNote`, tags `v1.0.0` / `v1.1.0`): C++ Basic Pitch inference | Apache-2.0 | CNN runs in **RTNeural (BSD-3-Clause)**. The CQT + harmonic-stacking "features" model runs in **ONNX Runtime (MIT)** as an `.ort` file, via a custom minimal static build (`tiborvass/libonnxruntime-neuralnote`). Also uses minimp3 (CC0). |
| **NeuralNote v2 (2026)** | Code Apache-2.0, **model weights CC BY-NC 4.0** | v2 **dropped Basic Pitch** for MuScriptor (Kyutai/Mirelo transformer, 103M–1.4B params, run via `muscriptor.cpp`/ggml, MIT). The weights are **non-commercial only** and downloaded at runtime. **Do not use v2 as a template for a Basic Pitch backend.** Use the v1.1.0 tag. |

**Compatibility with AGPLv3:** Apache-2.0, BSD-3-Clause, MIT, ISC and CC0 are all one-way compatible with
(A)GPLv3. We can include them in an AGPLv3 plugin as long as we keep their notices (Apache-2.0 also needs a
NOTICE file, if any, carried along). CC BY-NC 4.0 (MuScriptor weights) is **not** compatible with the AGPL's "no
further restrictions" rule for distributed code/data, so it must stay out of this repo.

See `docs/BASIC_PITCH.md` for the integration write-up.
