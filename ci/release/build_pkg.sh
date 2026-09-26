#!/usr/bin/env bash
# Builds PitchLane-<version>.pkg around an (already signed) PitchLane.component.
# Usage: build_pkg.sh <component> <pkg-version> <out.pkg> [installer-identity]
#   Without an installer identity the product archive is left unsigned (dry run).
#   SIGN_KEYCHAIN must be set when an identity is given.
# shellcheck source=ci/release/common.sh
source "$(dirname "$0")/common.sh"
here="$(cd "$(dirname "$0")" && pwd)"
comp="${1:?component}"; version="${2:?version}"; out="${3:?out.pkg}"; identity="${4:-}"
work="${RUNNER_TEMP:-/tmp}/pl-pkg"
rm -rf "$work"; mkdir -p "$work/root" "$work/pkgs" "$(dirname "$out")"

ditto "$comp" "$work/root/PitchLane.component"   # ditto keeps the signature and xattrs intact

# Component property list written explicitly (not via pkgbuild --analyze): never let Installer
# "relocate" the update into some other copy of the bundle it finds elsewhere on the disk.
cat > "$work/components.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<array>
    <dict>
        <key>BundleHasStrictIdentifier</key><true/>
        <key>BundleIsRelocatable</key><false/>
        <key>BundleIsVersionChecked</key><false/>
        <key>BundleOverwriteAction</key><string>upgrade</string>
        <key>RootRelativeBundlePath</key><string>PitchLane.component</string>
    </dict>
</array>
</plist>
PLIST
plutil -lint "$work/components.plist"

pkgbuild --root "$work/root" \
    --component-plist "$work/components.plist" \
    --identifier com.dkimoto.pitchlane.pkg \
    --version "$version" \
    --install-location /Library/Audio/Plug-Ins/Components \
    --scripts "$here/scripts" \
    "$work/pkgs/PitchLane-component.pkg"

cat > "$work/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>Pitch Lane</title>
    <welcome file="welcome.txt" mime-type="text/plain"/>
    <options customize="never" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>
    <allowed-os-versions><os-version min="11.0"/></allowed-os-versions>
    <choices-outline>
        <line choice="default"><line choice="com.dkimoto.pitchlane.pkg"/></line>
    </choices-outline>
    <choice id="default"/>
    <choice id="com.dkimoto.pitchlane.pkg" visible="false">
        <pkg-ref id="com.dkimoto.pitchlane.pkg"/>
    </choice>
    <pkg-ref id="com.dkimoto.pitchlane.pkg" version="$version" onConclusion="none">PitchLane-component.pkg</pkg-ref>
</installer-gui-script>
XML

unsigned="$work/PitchLane-unsigned.pkg"
productbuild --distribution "$work/distribution.xml" \
    --resources "$here/resources" \
    --package-path "$work/pkgs" \
    "$unsigned"

if [ -n "$identity" ]; then
    productsign --sign "$identity" --keychain "${SIGN_KEYCHAIN:?}" --timestamp "$unsigned" "$out"
else
    cp "$unsigned" "$out"
fi
echo "Built $out"
pkgutil --payload-files "$work/pkgs/PitchLane-component.pkg" | sed 's/^/  payload: /' | head -20 || true
