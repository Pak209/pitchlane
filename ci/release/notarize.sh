#!/usr/bin/env bash
# Notarize and staple a signed pkg with an App Store Connect API key.
# Usage: notarize.sh <pkg>   (needs ASC_KEY_PATH, ASC_API_KEY_ID, ASC_API_ISSUER_ID)
# shellcheck source=ci/release/common.sh
source "$(dirname "$0")/common.sh"
pkg="${1:?pkg}"
auth=(--key "${ASC_KEY_PATH:?}" --key-id "${ASC_API_KEY_ID:?}" --issuer "${ASC_API_ISSUER_ID:?}")

set +e
out=$(xcrun notarytool submit "$pkg" "${auth[@]}" --wait --timeout 60m --output-format json 2> notary-stderr.txt)
rc=$?
set -e
json=$(grep -E '^\{' <<<"$out" | tail -1 || true)
jget() { python3 -c 'import json,sys
try: print(json.loads(sys.argv[1]).get(sys.argv[2], "") or "")
except Exception: print("")' "$json" "$1"; }
id=$(jget id); status=$(jget status); message=$(jget message)
echo "Notarization submission id: ${id:-?}"
echo "Notarization status: ${status:-?} ${message:+($message)}"
{
    echo "### Notarization"
    echo "- Submission id: \`${id:-?}\`"
    echo "- Status: **${status:-?}**"
} >> "${GITHUB_STEP_SUMMARY:-/dev/null}"
echo "NOTARY_ID=${id:-}" >> "${GITHUB_ENV:-/dev/null}"
echo "NOTARY_STATUS=${status:-}" >> "${GITHUB_ENV:-/dev/null}"

if [ -n "$id" ]; then
    xcrun notarytool log "$id" "${auth[@]}" notary-log.json > /dev/null 2>&1 || true
fi
if [ "$rc" -ne 0 ] || [ "$status" != "Accepted" ]; then
    err "Notarization failed (exit $rc, status '${status:-none}')."
    sed 's/^/  notarytool: /' notary-stderr.txt >&2 || true
    if [ -s notary-log.json ]; then echo "Notary log:"; cat notary-log.json; fi
    exit 1
fi
if [ -s notary-log.json ]; then
    echo "Notary log issues (should be empty):"
    python3 -c 'import json,sys; print(json.dumps(json.load(open(sys.argv[1])).get("issues"), indent=1))' notary-log.json || true
fi

xcrun stapler staple -v "$pkg" > staple.log 2>&1 || { err "stapler staple failed"; cat staple.log; exit 1; }
tail -2 staple.log
xcrun stapler validate "$pkg"
