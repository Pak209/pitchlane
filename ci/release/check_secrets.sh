#!/usr/bin/env bash
# Fails early, naming every missing signing/notarization secret. Never prints values.
# Usage: check_secrets.sh <mode>   (mode: dry | sign | release)
# shellcheck source=ci/release/common.sh
source "$(dirname "$0")/common.sh"
mode="${1:?mode}"
names=(MACOS_DEVID_APP_P12_BASE64 MACOS_DEVID_APP_P12_PASSWORD
       MACOS_DEVID_INSTALLER_P12_BASE64 MACOS_DEVID_INSTALLER_P12_PASSWORD
       APPLE_TEAM_ID ASC_API_KEY_P8_BASE64 ASC_API_KEY_ID ASC_API_ISSUER_ID KEYCHAIN_PASSWORD)
missing=()
for n in "${names[@]}"; do
    if [ -z "${!n:-}" ]; then missing+=("$n"); fi
done
present=$(( ${#names[@]} - ${#missing[@]} ))
echo "Signing secrets present: ${present}/${#names[@]}"
if [ "${#missing[@]}" -gt 0 ]; then
    if [ "$mode" = "dry" ]; then
        echo "Dry run: signing is skipped, so missing secrets are fine: ${missing[*]}"
        exit 0
    fi
    err "Missing GitHub secrets for a signed build: ${missing[*]}. Add them under Settings > Secrets and variables > Actions (see docs/RELEASING.md), or run with dry_run=true."
    exit 1
fi
if [ "$mode" != "dry" ] && ! [[ "$APPLE_TEAM_ID" =~ ^[A-Z0-9]{10}$ ]]; then
    err "APPLE_TEAM_ID must be the 10-character Team ID (e.g. from developer.apple.com > Membership)."
    exit 1
fi
