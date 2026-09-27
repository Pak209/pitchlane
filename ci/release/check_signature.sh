#!/usr/bin/env bash
# Signature gates for PitchLane.component.
#
#   check_signature.sh pre  <component>          before Developer ID signing
#       Fails if the bundle or its Info.plist carries an ad-hoc signature that was applied on
#       purpose (codesign -s -, e.g. JUCE's copy step): such a seal would be silently replaced or,
#       worse, shipped without the hardened runtime. The linker's automatic ad-hoc signature on the
#       arm64 slice ("linker-signed", Info.plist not bound) is expected and is fine.
#   check_signature.sh post <component> <team>   after Developer ID signing
#       Requires Developer ID Application authority for <team>, hardened runtime, a secure
#       timestamp, a bound Info.plist and sealed resources, on every architecture, and a clean
#       codesign --verify --deep --strict.
# shellcheck source=ci/release/common.sh
source "$(dirname "$0")/common.sh"
mode="${1:?pre|post}"
comp="${2:?component}"
bin="$comp/Contents/MacOS/PitchLane"
[ -d "$comp" ] && [ -f "$bin" ] && [ -f "$comp/Contents/Info.plist" ] || { err "$comp is not a complete PitchLane.component"; exit 1; }
archs=$(lipo -archs "$bin")
echo "Architectures: $archs"
fail=0

describe() { codesign -dvvv "$@" 2>&1 || true; }
field() { sed -n "s/^$1=//p" <<<"$2" | head -1; }
# The code-directory flags live on the "CodeDirectory v=... flags=0x20002(adhoc,linker-signed) ..." line.
cdflags() { grep -oE 'flags=0x[0-9a-f]+\([^)]*\)' <<<"$1" | head -1 | sed 's/^flags=//' || true; }

case "$mode" in
pre)
    top=$(describe "$comp")
    if grep -q "code object is not signed" <<<"$top"; then
        echo "bundle: unsigned (ok)"
    else
        flags=$(cdflags "$top")
        plist=$(grep -E '^Info.plist' <<<"$top" | head -1)
        echo "bundle: $flags; $plist; $(grep -E '^Sealed Resources' <<<"$top" | head -1)"
        if grep -q "Signature=adhoc" <<<"$top"; then
            if ! grep -q "linker-signed" <<<"$flags"; then
                err "PitchLane.component carries a full ad-hoc signature (codesign -s -; flags $flags). It must not reach Developer ID signing: build with -DPITCHLANE_DISTRIBUTION=ON -DPITCHLANE_COPY_AFTER_BUILD=OFF and do not ad-hoc sign the build output (sign a copy for local testing)."
                fail=1
            fi
            if grep -q "Info.plist entries" <<<"$top"; then
                err "PitchLane.component's Info.plist is bound to an ad-hoc signature. Rebuild without the ad-hoc signing step."
                fail=1
            fi
        fi
        if [ -d "$comp/Contents/_CodeSignature" ] && grep -q "Signature=adhoc" <<<"$top"; then
            err "PitchLane.component has an ad-hoc _CodeSignature/CodeResources seal."
            fail=1
        fi
    fi
    for a in $archs; do
        d=$(describe --arch "$a" "$bin")
        if grep -q "code object is not signed" <<<"$d"; then echo "$a: unsigned (ok)"
        else
            f=$(cdflags "$d")
            echo "$a: $f"
            if grep -q "Signature=adhoc" <<<"$d" && ! grep -q "linker-signed" <<<"$f"; then
                err "$a slice carries a full ad-hoc signature ($f); expected unsigned or linker-signed."
                fail=1
            fi
        fi
    done
    ;;
post)
    team="${3:?team id}"
    for target in "bundle" $archs; do
        if [ "$target" = "bundle" ]; then d=$(describe "$comp"); else d=$(describe --arch "$target" "$bin"); fi
        f=$(cdflags "$d")
        auth=$(grep -m1 '^Authority=' <<<"$d" | sed 's/^Authority=//')
        echo "$target: $f; Authority=$auth; $(grep -m1 -E '^Timestamp=' <<<"$d" || echo 'Timestamp=none')"
        if grep -q "Signature=adhoc" <<<"$d"; then err "$target: still ad-hoc signed"; fail=1; fi
        grep -q "^Authority=Developer ID Application:" <<<"$d" || { err "$target: not signed with a Developer ID Application certificate"; fail=1; }
        [ "$(field TeamIdentifier "$d")" = "$team" ] || { err "$target: TeamIdentifier does not match APPLE_TEAM_ID"; fail=1; }
        grep -q "runtime" <<<"$f" || { err "$target: hardened runtime flag missing (sign with --options runtime)"; fail=1; }
        grep -q "^Timestamp=" <<<"$d" || { err "$target: no secure timestamp (sign with --timestamp)"; fail=1; }
        if [ "$target" = "bundle" ]; then
            grep -q "Info.plist entries" <<<"$d" || { err "Info.plist is not bound to the signature"; fail=1; }
            grep -q "Sealed Resources version" <<<"$d" || { err "bundle resources are not sealed"; fail=1; }
        fi
    done
    echo "codesign --verify --deep --strict --verbose=2:"
    codesign --verify --deep --strict --verbose=2 "$comp" 2>&1 | sed 's/^/  /' || true
    codesign --verify --deep --strict "$comp" || { err "codesign --verify --deep --strict failed"; fail=1; }
    ;;
*) err "usage: check_signature.sh pre|post <component> [team]"; exit 2 ;;
esac

if [ "$fail" -ne 0 ]; then exit 1; fi
echo "Signature check ($mode): OK"
