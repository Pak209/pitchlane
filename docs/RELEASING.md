# Releasing Pitch Lane

Releases are built by [`.github/workflows/release.yml`](../.github/workflows/release.yml) on a `macos-14` runner.
It produces a **signed and notarized installer** `PitchLane-<version>.pkg` plus a `.sha256` file and publishes
both on the repository's GitHub Releases page with install steps for non-technical users.

What the workflow does, in order:

1. builds the AU in Release, universal (arm64 + x86_64), with `-DPITCHLANE_DISTRIBUTION=ON`
   (no JUCE copy step, so the build output carries no ad-hoc bundle signature);
2. runs the unit tests and `auval -v aufx Plne Pk20` on an ad-hoc signed copy (the job fails if auval fails);
3. **signature gate**: fails if the component or its Info.plist carries an ad-hoc signature
   (`ci/release/check_signature.sh pre`); the linker's automatic ad-hoc signature on the arm64 slice is fine;
4. imports both Developer ID certificates into a temporary keychain (deleted again in an `always()` step);
5. `codesign --force --options runtime --timestamp` with **Developer ID Application**, then checks authority,
   team, hardened runtime, timestamp, bound Info.plist and sealed resources on every architecture;
6. `pkgbuild` (install location `/Library/Audio/Plug-Ins/Components`, identifier `com.dkimoto.pitchlane.pkg`,
   bundle marked non-relocatable, postinstall refreshes the AU registrar) and `productbuild` with a welcome text,
   then `productsign` with **Developer ID Installer**;
7. `xcrun notarytool submit --wait` with the App Store Connect API key (prints the notary log on failure),
   then `xcrun stapler staple`;
8. verifies with `spctl -a -vv -t install`, `pkgutil --check-signature`, `stapler validate`,
   `codesign --verify --deep --strict` (also on the component extracted from the pkg);
9. test-installs the pkg on the runner with `sudo installer` and re-runs `auval` on the installed copy;
10. creates (or updates) the GitHub Release for the tag with the pkg and its `.sha256`.

## One-time setup: secrets

Add these under **GitHub > Pak209/pitchlane > Settings > Secrets and variables > Actions > New repository
secret**. The names must match exactly. The workflow never prints them and fails early, naming any that
are missing, when it needs to sign.

| Secret | What it is |
|---|---|
| `MACOS_DEVID_APP_P12_BASE64` | "Developer ID Application" certificate **with its private key**, exported as .p12, base64 encoded |
| `MACOS_DEVID_APP_P12_PASSWORD` | password of that .p12 |
| `MACOS_DEVID_INSTALLER_P12_BASE64` | "Developer ID Installer" certificate with private key, .p12, base64 |
| `MACOS_DEVID_INSTALLER_P12_PASSWORD` | password of that .p12 |
| `APPLE_TEAM_ID` | 10-character Team ID (developer.apple.com > Account > Membership details) |
| `ASC_API_KEY_P8_BASE64` | App Store Connect API key file `AuthKey_XXXXXXXXXX.p8`, base64 encoded |
| `ASC_API_KEY_ID` | that key's Key ID |
| `ASC_API_ISSUER_ID` | the Issuer ID shown above the key list |
| `KEYCHAIN_PASSWORD` | any random string; protects the temporary keychain on the runner |

How to get them:

1. **Certificates.** In Xcode > Settings > Accounts > Manage Certificates (or on developer.apple.com >
   Certificates) create a *Developer ID Application* and a *Developer ID Installer* certificate. Only the
   Account Holder can create Developer ID certificates. In **Keychain Access > My Certificates**, expand each
   one, make sure the private key is underneath, right-click > *Export* as `.p12` with a password.
   If you build a .p12 with OpenSSL 3 instead, add `-legacy`, since macOS `security import` can't read
   the OpenSSL 3 default encryption.
2. **API key for notarization.** App Store Connect > Users and Access > Integrations > App Store Connect API >
   *Team Keys* > generate a key with the **Developer** role (or higher). Download the `.p8` (only possible once)
   and note the Key ID and Issuer ID.
3. **Encode and store** (from Terminal; `gh` sets a secret without the value touching your shell history):
   ```bash
   base64 -i DeveloperIDApplication.p12 | gh secret set MACOS_DEVID_APP_P12_BASE64 -R Pak209/pitchlane
   base64 -i DeveloperIDInstaller.p12   | gh secret set MACOS_DEVID_INSTALLER_P12_BASE64 -R Pak209/pitchlane
   base64 -i AuthKey_XXXXXXXXXX.p8      | gh secret set ASC_API_KEY_P8_BASE64 -R Pak209/pitchlane
   gh secret set MACOS_DEVID_APP_P12_PASSWORD -R Pak209/pitchlane        # prompts for the value
   gh secret set MACOS_DEVID_INSTALLER_P12_PASSWORD -R Pak209/pitchlane
   gh secret set APPLE_TEAM_ID -R Pak209/pitchlane
   gh secret set ASC_API_KEY_ID -R Pak209/pitchlane
   gh secret set ASC_API_ISSUER_ID -R Pak209/pitchlane
   openssl rand -base64 24 | gh secret set KEYCHAIN_PASSWORD -R Pak209/pitchlane
   ```
   Then delete the exported .p12/.p8 copies you no longer need.

The Developer ID Application certificate expires (currently 2027-02-01). Renew it before then and update the
two `MACOS_DEVID_APP_*` secrets. Already-notarized releases keep working after expiry because they are timestamped.

## Cutting a release

1. Make sure `main` (or the release branch) is green, and bump the version in the top-level `CMakeLists.txt`
   (`project(PitchLane VERSION x.y.z ...)`). The workflow refuses a tag whose `x.y.z` differs, so the AU
   version Logic shows always matches the release.
2. Tag and push:
   ```bash
   git tag v0.1.0 && git push origin v0.1.0
   ```
   Tags such as `v0.2.0-beta1` are published as pre-releases (the `x.y.z` part must still match).
3. Watch it: `gh run watch $(gh run list --workflow release.yml --limit 1 --json databaseId --jq '.[0].databaseId')`.
   It takes about 15–25 minutes, a few of them waiting for Apple's notary service.
4. Check the release page, download the pkg on a Mac, double-click it, and confirm Logic finds
   *Pak209 > Pitch Lane*.

Re-running the workflow for the same tag (Actions > Release > Re-run) replaces the assets and notes of the
existing release.

## Test runs without publishing

From the Actions tab (*Release (signed + notarized pkg)* > *Run workflow*) or with gh:

```bash
# Dry run: no secrets used; unsigned pkg (component ad-hoc signed) uploaded as a workflow artifact.
gh workflow run release.yml --ref <branch> -f dry_run=true
# Signed + notarized + verified pkg uploaded as a workflow artifact; no GitHub Release is created.
gh workflow run release.yml --ref <branch> -f sign_only=true
```

Manual runs need the workflow file on the repository's default branch; until it is merged, GitHub only lists
the workflow for tag pushes. Running with `dry_run=false` on a branch is refused, because a real release needs a
`v*` tag.

## Troubleshooting the pipeline

- **"Missing GitHub secrets: ..."**: add the named secrets (see above).
- **Import failed**: wrong .p12 password, or an OpenSSL 3 .p12 (re-export from Keychain Access or with `-legacy`).
- **"No valid 'Developer ID Application: ... (TEAM)' identity"**: the .p12 lacks the private key, the
  certificate expired, or `APPLE_TEAM_ID` doesn't match the certificate's team.
- **Notarization "Invalid"**: the log is printed in the step and uploaded as `notary-log.json`. Typical causes
  are a missing hardened runtime or timestamp, or an unsigned nested binary.
- **Signature gate fails with an ad-hoc seal**: something ad-hoc signed the build output (e.g.
  `PITCHLANE_COPY_AFTER_BUILD=ON`). The release build must not use the copy step.
