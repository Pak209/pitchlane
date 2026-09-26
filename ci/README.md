# macOS CI workflow (not yet active)

`ci/macos-au.yml` is a GitHub Actions workflow. On every push and PR it:

1. builds the AU (Release, **universal arm64 + x86_64**) plus the Standalone and the unit tests on `macos-14`;
2. runs the unit tests (`ctest`);
3. checks the binary is universal (`lipo`), ad-hoc signs the `.component`, copies it to
   `~/Library/Audio/Plug-Ins/Components/`, restarts `AudioComponentRegistrar`;
4. runs `auval -v aufx Plne Pk20` and **fails the job if validation fails** (plus an informational `auval -strict`);
5. uploads `PitchLane-AU-macOS-universal.zip` (the `.component`) and `auval.log` as an artifact.

## Why it lives here instead of `.github/workflows/`

GitHub refuses pushes that add or modify files under `.github/workflows/` from an OAuth token without the
`workflow` scope. The `gh` token used to create this repo has only `repo, gist, read:org`, so the push was rejected.

## Activate it (one time, on a machine with `gh`)

```bash
gh auth refresh -h github.com -s workflow     # adds the 'workflow' scope (opens a browser to confirm)
git checkout main && git pull                 # after the PR is merged (or do this on the feature branch)
mkdir -p .github/workflows
git mv ci/macos-au.yml .github/workflows/macos-au.yml
git commit -m "CI: enable macOS AU workflow"
git push
```

Alternatively, create the file through the GitHub web UI (Add file > Create new file >
`.github/workflows/macos-au.yml`, paste the contents). The web UI doesn't need the token scope.

Then open the **Actions** tab. The *macOS AU (build + auval)* run takes about 10–15 minutes. Download the artifact and follow the
install steps in the main README (quarantine removal + ad-hoc codesign).
