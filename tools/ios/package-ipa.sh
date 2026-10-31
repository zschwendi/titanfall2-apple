#!/bin/sh
# package-ipa.sh APP OUT_DIR: wrap an app you built and signed yourself into an .ipa for your own
# registered devices, with a SHA-256 and a manifest of what is inside. It does not build or re-sign.
#
# A TFNative app contains code translated from your copy of the game. Keep the .ipa for your own
# devices and never publish or share it; the manifest records whether the app contains that code.
# OUT_DIR must be outside this checkout so the .ipa cannot be committed by accident.
set -eu
cd "$(dirname "$0")/../.."
repo=$(pwd -P)
app=${1:?usage: package-ipa.sh APP OUT_DIR}
out=${2:?usage: package-ipa.sh APP OUT_DIR}
[ -d "$app" ] && [ -f "$app/Info.plist" ] || { echo "not an app bundle: $app" >&2; exit 2; }
mkdir -p "$out"
app=$(cd "$app" && pwd -P)
out=$(cd "$out" && pwd -P)
case "$out/" in
    "$repo"/*) echo "refusing: $out is inside this checkout; choose a folder outside it" >&2; exit 2 ;;
esac

plist() { /usr/libexec/PlistBuddy -c "Print :$1" "$2" 2>/dev/null || true; }
info="$app/Info.plist"
exe=$(plist CFBundleExecutable "$info")
bundle=$(plist CFBundleIdentifier "$info")
version=$(plist CFBundleShortVersionString "$info")
build=$(plist CFBundleVersion "$info")
[ -n "$exe" ] && [ -f "$app/$exe" ] || { echo "no executable in $app" >&2; exit 2; }
[ -f "$app/embedded.mobileprovision" ] || { echo "refusing: $app has no provisioning profile; build it signed for your devices" >&2; exit 2; }
codesign --verify --deep --strict "$app" || { echo "refusing: the signature of $app does not verify" >&2; exit 2; }

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM
security cms -D -i "$app/embedded.mobileprovision" > "$tmp/profile.plist"
expires=$(plist ExpirationDate "$tmp/profile.plist")
devices=$(/usr/libexec/PlistBuddy -c 'Print :ProvisionedDevices' "$tmp/profile.plist" 2>/dev/null | grep -c '^ ' || true)
all_devices=$(plist ProvisionsAllDevices "$tmp/profile.plist")
codesign -d --entitlements :- "$app" 2>/dev/null > "$tmp/entitlements.plist" || true
entitlements=$(plutil -convert json -o - "$tmp/entitlements.plist" 2>/dev/null |
    /usr/bin/python3 -I -c 'import json,sys; print(",".join(sorted(json.load(sys.stdin))))' 2>/dev/null || true)
# Translated game modules are named after the game's DLLs (gate3/modules.def): engine_, client_, ...
if nm -U "$app/$exe" 2>/dev/null | grep -q -E ' _(engine|client|server|materialsystem_dx11)_'; then game_code=true; else game_code=false; fi

name=$(basename "$app" .app)
stem="$name-${version:-unversioned}-${build:-0}-$(date -u +%Y%m%d)"
mkdir "$tmp/Payload"
ditto "$app" "$tmp/Payload/$name.app"
rm -f "$out/$stem.ipa"
(cd "$tmp" && zip -qryX "$out/$stem.ipa" Payload)
mkdir "$tmp/check"
(cd "$tmp/check" && unzip -q "$out/$stem.ipa")
codesign --verify --deep --strict "$tmp/check/Payload/$name.app" || { echo "the packaged app does not verify; removed $out/$stem.ipa" >&2; rm -f "$out/$stem.ipa"; exit 1; }
sum=$(shasum -a 256 "$out/$stem.ipa" | awk '{print $1}')
printf '%s  %s\n' "$sum" "$stem.ipa" > "$out/$stem.ipa.sha256"
/usr/bin/python3 -I -c '
import json, sys
(path, ipa, sha, bundle, version, build, expires, devices, all_devices, entitlements, game_code) = sys.argv[1:]
json.dump({"ipa": ipa, "sha256": sha, "bundle_id": bundle, "version": version, "build": build,
           "profile_expires": expires, "registered_devices": int(devices or 0),
           "provisions_all_devices": all_devices == "true",
           "entitlements": [e for e in entitlements.split(",") if e],
           "contains_game_code": game_code == "true",
           "share": "never" if game_code == "true" else "only if every file in it may be shared"},
          open(path, "w"), indent=2)
' "$out/$stem.json" "$stem.ipa" "$sum" "$bundle" "$version" "$build" "$expires" "$devices" "$all_devices" \
  "$entitlements" "$game_code"
echo "wrote $out/$stem.ipa ($(du -h "$out/$stem.ipa" | cut -f1)), sha256 $sum"
echo "profile expires $expires; registered devices: ${devices:-0}; bundle $bundle"
if [ "$game_code" = true ]; then
    echo "this app contains code translated from your copy of the game: keep it for your own devices; never publish or share it"
fi
