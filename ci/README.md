# CI

The macOS workflow now lives at [`.github/workflows/macos-au.yml`](../.github/workflows/macos-au.yml) and runs on every
push, pull request and manual dispatch (`workflow_dispatch`). It:

1. builds the AU (Release, **universal arm64 + x86_64**) plus the Standalone and the unit tests on `macos-14`;
2. runs the unit tests (`ctest`);
3. checks the binary is universal (`lipo`), ad-hoc signs the `.component`, copies it to
   `~/Library/Audio/Plug-Ins/Components/`, restarts `AudioComponentRegistrar`;
4. runs `auval -v aufx Plne Pk20` and **fails the job if validation fails** (plus an informational `auval -strict`);
5. uploads `PitchLane-AU-macOS-universal.zip` (the `.component`) and `auval.log` as an artifact.

## Using it

```bash
gh run list --workflow macos-au.yml --branch feat/m1-m4-core
gh run watch <run-id>
gh run view <run-id> --log-failed
gh run download <run-id> -n PitchLane-AU-macOS-universal -D ./artifacts
```

A run takes roughly 10–15 minutes (most of it is fetching and compiling JUCE twice, once per architecture).
Install the downloaded component by following the steps in the main README (quarantine removal + ad-hoc codesign).

Note: pushing changes to files under `.github/workflows/` needs a token with the `workflow` scope
(`gh auth refresh -h github.com -s workflow`).
