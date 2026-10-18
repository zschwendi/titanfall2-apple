#!/bin/sh
# make_profile.sh --team TEAMID --bundle-id ID [--paid]: get a development provisioning profile that grants the
# x86 memory-ordering mode, test it, and write app/signing.local.mk for app/app.mk.
#
# A FREE Apple ID is enough (the default): its Personal Team may use the entitlement
# com.apple.developer.cross-architecture-support-unmanaged ("the same except that it's available to free
# developer accounts", wine-devel, 7 August 2026).  --paid uses com.apple.developer.cross-architecture-support,
# which needs the paid Apple Developer Program.  UNTESTED WITH A FREE ACCOUNT so far: report what happens.
#
# Before the first run: Xcode > Settings > Accounts: add your Apple ID.  Then open
# app/profile-stub/TFRecompProfile.xcodeproj in Xcode once, pick your team under Signing & Capabilities and
# build it (Cmd-B): on the command line, automatic signing has been seen to refuse a new capability until the
# project was opened in Xcode's window once.
#
#   TEAMID     your team's 10-character id (Xcode > Settings > Accounts > your team; for a free Apple ID it
#              is the "Personal Team")
#   ID         a bundle id of your own, unique among all of Apple's (e.g. com.yourname.tfrecomp)
#
# A free account's profile lasts 7 days: run this again when `./tfrecomp doctor` says it has expired.
# Nothing here is sent anywhere except what Xcode's own automatic signing sends to Apple.
set -eu
cd "$(dirname "$0")/../.." || exit 2
team=; bundle=; kind=free
while [ $# -gt 0 ]; do
    case "$1" in
        --team) team=${2:-}; shift 2;;
        --bundle-id) bundle=${2:-}; shift 2;;
        --paid) kind=paid; shift;;
        *) echo "usage: $0 --team TEAMID --bundle-id ID [--paid]" >&2; exit 2;;
    esac
done
case "$team" in [A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9]) ;;
    *) echo "make_profile: --team is the 10-character team id (letters and digits)" >&2; exit 2;; esac
case "$bundle" in *[!A-Za-z0-9.-]*|''|.*|*.) echo "make_profile: --bundle-id is like com.yourname.tfrecomp" >&2; exit 2;; esac

key=com.apple.developer.cross-architecture-support
[ "$kind" = free ] && key=$key-unmanaged
dd=build/signing/dd
app=$dd/Build/Products/Debug/TFRecompProfile.app
mkdir -p build/signing

echo "make_profile: building the stub with team $team, bundle id $bundle, entitlement $key"
xcodebuild -project app/profile-stub/TFRecompProfile.xcodeproj -scheme TFRecompProfile -configuration Debug \
    -destination 'platform=macOS,arch=arm64' -derivedDataPath "$dd" \
    -allowProvisioningUpdates -allowProvisioningDeviceRegistration \
    DEVELOPMENT_TEAM="$team" PRODUCT_BUNDLE_IDENTIFIER="$bundle" \
    CODE_SIGN_ENTITLEMENTS="TFRecompProfile-$kind.entitlements" build > build/signing/xcodebuild.log 2>&1 \
    || { echo "make_profile: xcodebuild failed; the end of build/signing/xcodebuild.log:" >&2; tail -15 build/signing/xcodebuild.log >&2; exit 1; }

prof=$app/Contents/embedded.provisionprofile
[ -f "$prof" ] || { echo "make_profile: the stub has no embedded profile ($prof)" >&2; exit 1; }
security cms -D -i "$prof" > build/signing/profile.plist 2>/dev/null \
    || { echo "make_profile: cannot read the profile" >&2; exit 1; }
/usr/libexec/PlistBuddy -c "Print :Entitlements:$key" build/signing/profile.plist >/dev/null 2>&1 \
    || { echo "make_profile: the profile does not grant $key (your account may not offer it)" >&2; exit 1; }
expires=$(/usr/libexec/PlistBuddy -c "Print :ExpirationDate" build/signing/profile.plist 2>/dev/null || echo unknown)

echo "make_profile: the profile grants $key (expires $expires); testing the mode"
out=$("$app/Contents/MacOS/TFRecompProfile" 1 2 2>&1) || true
echo "$out" | sed 's/^/  /'
echo "$out" | grep -q "writer kr=0 " && echo "$out" | grep -q "reader kr=0 " && echo "$out" | grep -q "violations=0$" \
    || { echo "make_profile: the mode was NOT granted (see above); app/signing.local.mk not written" >&2; exit 1; }

cp "$prof" build/signing/TFRecomp.provisionprofile
sign=dev
[ "$kind" = free ] && sign=free
{
    echo "# Written by tools/signing/make_profile.sh ($(date +%Y-%m-%d)); git-ignored.  Profile expires $expires."
    echo "APP_SIGN    := $sign"
    echo "APP_ID      := $bundle"
    echo "APP_TEAM    := $team"
    echo "APP_PROFILE := build/signing/TFRecomp.provisionprofile"
} > app/signing.local.mk
echo "make_profile: ok: the mode works with this profile; wrote app/signing.local.mk (APP_SIGN=$sign); next: ./tfrecomp build"
