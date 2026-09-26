#!/usr/bin/env bash
# Temporary signing keychain.
#   keychain.sh setup    imports both Developer ID .p12s and the App Store Connect API key
#   keychain.sh cleanup  deletes the keychain and every decoded secret file (run in always())
# Writes SIGN_KEYCHAIN, APP_IDENTITY, INSTALLER_IDENTITY, ASC_KEY_PATH to $GITHUB_ENV.
# shellcheck source=ci/release/common.sh
source "$(dirname "$0")/common.sh"
KC="${RUNNER_TEMP:?}/pitchlane-signing.keychain-db"
SECRETS_DIR="${RUNNER_TEMP}/pl-secrets"

decode() { # decode <base64 var name> <out file>
    printf '%s' "${!1}" | tr -d ' \r\n' | base64 --decode > "$2"
    [ -s "$2" ] || { err "$1 did not decode to a non-empty file (is it base64?)"; exit 1; }
}

case "${1:?setup|cleanup}" in
setup)
    umask 077
    mkdir -p "$SECRETS_DIR"
    decode MACOS_DEVID_APP_P12_BASE64 "$SECRETS_DIR/app.p12"
    decode MACOS_DEVID_INSTALLER_P12_BASE64 "$SECRETS_DIR/installer.p12"
    decode ASC_API_KEY_P8_BASE64 "$SECRETS_DIR/AuthKey_${ASC_API_KEY_ID}.p8"

    security create-keychain -p "$KEYCHAIN_PASSWORD" "$KC"
    security set-keychain-settings -lut 21600 "$KC"
    security unlock-keychain -p "$KEYCHAIN_PASSWORD" "$KC"
    security import "$SECRETS_DIR/app.p12" -k "$KC" -f pkcs12 -P "$MACOS_DEVID_APP_P12_PASSWORD" \
        -T /usr/bin/codesign -T /usr/bin/security \
        || { err "Importing MACOS_DEVID_APP_P12_BASE64 failed (wrong password, or a .p12 made with OpenSSL 3 defaults; re-export with -legacy, see docs/RELEASING.md)"; exit 1; }
    security import "$SECRETS_DIR/installer.p12" -k "$KC" -f pkcs12 -P "$MACOS_DEVID_INSTALLER_P12_PASSWORD" \
        -T /usr/bin/productsign -T /usr/bin/productbuild -T /usr/bin/pkgbuild -T /usr/bin/security \
        || { err "Importing MACOS_DEVID_INSTALLER_P12_BASE64 failed (wrong password or OpenSSL 3 .p12, see docs/RELEASING.md)"; exit 1; }
    rm -f "$SECRETS_DIR/app.p12" "$SECRETS_DIR/installer.p12"
    security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "$KEYCHAIN_PASSWORD" "$KC" > /dev/null
    # Put the temp keychain first in the user search list, keeping the existing ones.
    existing=()
    while IFS= read -r line; do
        line="${line#"${line%%[![:space:]]*}"}"; line="${line%\"}"; line="${line#\"}"
        [ -n "$line" ] && existing+=("$line")
    done < <(security list-keychains -d user)
    security list-keychains -d user -s "$KC" "${existing[@]}"

    # Pick the identities for this team (names are not secret; the team id is masked in logs).
    app_line=$(security find-identity -v -p codesigning "$KC" | grep "\"Developer ID Application: .*(${APPLE_TEAM_ID})\"" | head -1 || true)
    inst_line=$(security find-identity -v "$KC" | grep "\"Developer ID Installer: .*(${APPLE_TEAM_ID})\"" | head -1 || true)
    [ -n "$app_line" ] || { err "No valid 'Developer ID Application: ... (APPLE_TEAM_ID)' identity in MACOS_DEVID_APP_P12_BASE64 (check the team id, that the .p12 includes the private key, and that the cert has not expired)"; security find-identity -v "$KC" | sed 's/^/  /'; exit 1; }
    [ -n "$inst_line" ] || { err "No valid 'Developer ID Installer: ... (APPLE_TEAM_ID)' identity in MACOS_DEVID_INSTALLER_P12_BASE64"; security find-identity -v "$KC" | sed 's/^/  /'; exit 1; }
    app_hash=$(awk '{print $2}' <<<"$app_line")
    inst_name=$(sed -E 's/^[^"]*"([^"]+)".*$/\1/' <<<"$inst_line")
    echo "Application identity: $(sed -E 's/^[^"]*"([^"]+)".*$/\1/' <<<"$app_line") [$app_hash]"
    echo "Installer identity:   $inst_name"
    {
        echo "SIGN_KEYCHAIN=$KC"
        echo "APP_IDENTITY=$app_hash"
        echo "INSTALLER_IDENTITY=$inst_name"
        echo "ASC_KEY_PATH=$SECRETS_DIR/AuthKey_${ASC_API_KEY_ID}.p8"
    } >> "${GITHUB_ENV:?}"
    ;;
cleanup)
    if [ -f "$KC" ]; then security delete-keychain "$KC" || true; fi
    rm -rf "$SECRETS_DIR"
    echo "Temporary keychain and decoded secrets removed."
    ;;
*) err "usage: keychain.sh setup|cleanup"; exit 2 ;;
esac
