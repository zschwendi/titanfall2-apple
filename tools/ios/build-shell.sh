#!/bin/sh
# build-shell.sh: build the public TF2 shell, the launcher and the DXMT renderer with no game code, unsigned,
# then audit it and package it as build/ios-shell-release/TF2-shell-<version>-<dxmt fingerprint>.ipa with a
# SHA-256. Needs the DXMT build of README steps 3 and 4 (build/dxmt-ios), Xcode and XcodeGen; it reads no
# game files. The shell cannot play on its own: see tools/ios/assemble_private_ipa.py.
set -eu
cd "$(dirname "$0")/../.."
fingerprint=$(sh tools/ios/dxmt-fingerprint.sh)
abi=$(sed -n 's/^#define TF2_PAYLOAD_ABI \([0-9]*\)$/\1/p' tools/ios/payload/tf2_payload.h)
prefix=${TF_BUNDLE_PREFIX:-com.example}
[ -f build/dxmt-ios/src/d3d11/d3d11.dylib ] || { echo "build DXMT first (README steps 3 and 4)" >&2; exit 2; }
TF_BUNDLE_PREFIX=$prefix .venv/bin/python tools/ios/package-dxmt.py >/dev/null
xcodegen generate --quiet --spec tools/ios/shell/project.yml
xcodebuild -quiet -project tools/ios/shell/TF2Shell.xcodeproj -scheme TF2Shell -configuration Release \
    -destination 'generic/platform=iOS' -derivedDataPath build/ios-shell \
    CODE_SIGNING_ALLOWED=NO TF_BUNDLE_PREFIX="$prefix" TF2_DXMT_FINGERPRINT="$fingerprint" build
app=build/ios-shell/Build/Products/Release-iphoneos/TFNative.app
plist=$app/Info.plist
for key in TF2Shell:bool:true TF2PayloadABI:integer:$abi TF2DXMTFingerprint:string:$fingerprint; do
    name=${key%%:*}; rest=${key#*:}; type=${rest%%:*}; value=${rest#*:}
    /usr/libexec/PlistBuddy -c "Delete :$name" "$plist" 2>/dev/null || true
    /usr/libexec/PlistBuddy -c "Add :$name $type $value" "$plist"
done
# The shell redistributes DXMT (LGPL 2.1+) with LLVM inside it: their licences and where the source is.
[ -f third_party/llvm15/LICENSE.TXT ] || { echo "third_party/llvm15/LICENSE.TXT is missing (README step 3)" >&2; exit 2; }
licenses=$app/Licenses
rm -rf "$licenses"; mkdir "$licenses"
cp LICENSE "$licenses/titanfall2-apple-LICENSE.txt"; cp NOTICE "$licenses/titanfall2-apple-NOTICE.txt"
cp third_party/dxmt/LICENSE "$licenses/DXMT-LICENSE.txt"; cp third_party/dxmt/COPYING.LIB "$licenses/DXMT-COPYING.LIB.txt"
cp third_party/dxmt/LICENSE.OLD "$licenses/DXMT-LICENSE.OLD.txt"; cp third_party/llvm15/LICENSE.TXT "$licenses/LLVM-LICENSE.txt"
revision=$(git rev-parse HEAD)$(git diff --quiet HEAD -- . 2>/dev/null || echo "-modified")
dxmt_commit=$(sed -n 's/^DXMT_COMMIT = "\([0-9a-f]*\)"$/\1/p' tfrecomp)
cat > "$licenses/SOURCE.txt" <<EOF
TF2 shell: the launcher and the DXMT renderer. It contains no game code and no game files.
titanfall2-apple source: https://github.com/zschwendi/titanfall2-apple, commit $revision
DXMT source: https://github.com/3Shain/dxmt, commit $dxmt_commit, with patches/dxmt and patches/dxmt-ios of the
commit above (fingerprint $fingerprint)
LLVM 15.0.7 source: https://github.com/llvm/llvm-project/releases/tag/llvmorg-15.0.7
EOF
sh tools/ios/audit-shell.sh "$app"
version=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$plist")
out=build/ios-shell-release
stem=TF2-shell-$version-$fingerprint
rm -rf "$out/Payload" "$out/$stem.ipa"
mkdir -p "$out/Payload"
ditto "$app" "$out/Payload/TFNative.app"
(cd "$out" && zip -qryX "$stem.ipa" Payload && rm -rf Payload && shasum -a 256 "$stem.ipa" > "$stem.ipa.sha256")
echo "shell: $out/$stem.ipa (unsigned, no game code; payload interface $abi, DXMT $fingerprint)"
cat "$out/$stem.ipa.sha256"
