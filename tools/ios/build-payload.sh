#!/bin/sh
# build-payload.sh: build TF2Game.framework, the game payload, from your own copy of the game.
#
# The payload holds the runtime, the game driver (gate3/run.c), the Direct3D back end (runtime/dxback), the
# module table and the recompiled game modules, behind the one entry point in tools/ios/payload/tf2_payload.h.
# It contains code translated from your game files: it is for your own devices, never to be shared.
#
# Needs ./tfrecomp setup (the game link, .venv, third_party/dxmt) and DXMT patched as in README step 4; DXMT
# and LLVM need not be built. Steps: the runtime (minutes), the game modules (hours the first time, then
# cached), then the payload's own files and the link. TF2_GAME_BUILD=DIR reuses a finished game-module build
# of this checkout (DIR/lifted/objects.filelist and DIR/gen) instead of running the game-module step.
set -eu
cd "$(dirname "$0")/../.."
out=build/ios-payload
fingerprint=$(sh tools/ios/dxmt-fingerprint.sh)
prefix=${TF_BUNDLE_PREFIX:-com.example}
source=$(git describe --always --dirty 2>/dev/null || echo unknown)
cc="xcrun --sdk iphoneos clang -target arm64-apple-ios18.0"
cxx="xcrun --sdk iphoneos clang++ -target arm64-apple-ios18.0"
[ -f third_party/dxmt/include/native/dxmt_native_display.h ] || {
    echo "third_party/dxmt is missing or not patched: run ./tfrecomp setup, then the patch lines of README step 4" >&2; exit 2; }

sh tools/ios/build-runtime.sh
game=${TF2_GAME_BUILD:-build/ios-game}
if [ -z "${TF2_GAME_BUILD:-}" ]; then
    [ -e game ] || { echo "no game link: run ./tfrecomp setup --game <your Titanfall 2 folder>" >&2; exit 2; }
    make -j1 PY=.venv/bin/python RT_PLATFORM=ios RT_VM_LAYOUT=ios \
        'RT_CC=xcrun --sdk iphoneos clang -target arm64-apple-ios18.0' \
        RT_OUT=build/ios-iphoneos/runtime RT_GEN=build/ios-iphoneos/gen \
        GUEST_MEM=tsob GATE3_OUT=build/ios-game LIFT_CACHE=build/ios-game-cache \
        GATE3_JOBS="${GATE3_JOBS:-2}" build/ios-game/lifted/objects.filelist build/ios-game/gen/gate3_table.c
fi
[ -f "$game/lifted/objects.filelist" ] && [ -f "$game/gen/gate3_table.c" ] || {
    echo "no game-module build in $game" >&2; exit 2; }
# A relative path in the filelist is relative to the checkout that made it (two levels above build/ios-game).
root=$(cd "$game/../.." && pwd -P)
mkdir -p "$out"
awk -v root="$root" '{ print (substr($0, 1, 1) == "/" ? $0 : root "/" $0) }' "$game/lifted/objects.filelist" > "$out/objects.filelist"

# The same flags as the TFNative target in Xcode's Release configuration.
defs='-DRT_EMBEDDED_APP=1 -DRT_VM_LAYOUT_IOS=1 -DGATE3_INSTALL_ROOT=\"\" -DGATE3_RUN_OUT=\"\"'   # eval keeps the quotes
incs="-Ithird_party/dxmt/include/native/windows -Ithird_party/dxmt/include/native/directx -Ithird_party/dxmt/include/native"
incs="$incs -Iruntime/include -Iharness -Igate2 -Igate3 -Ibuild/ios-iphoneos/gen -I$game/gen -Itools/ios/payload"
common='-Os -fno-common -g -fvisibility=hidden'
rm -rf "$out/obj" "$out/TF2Game.framework"
mkdir -p "$out/obj" "$out/TF2Game.framework"
eval "$cc -std=gnu17 $common $defs $incs -c gate3/run.c -o $out/obj/run.o"
eval "$cc -std=gnu17 $common $defs $incs -c $game/gen/gate3_table.c -o $out/obj/gate3_table.o"
for f in runtime/dxback/*.cpp; do
    eval "$cxx -std=c++20 -stdlib=libc++ -fvisibility-inlines-hidden $common $defs $incs -c $f -o $out/obj/dxback_$(basename "$f" .cpp).o"
done
eval "$cc -std=gnu17 -fobjc-arc -fobjc-weak $common $defs $incs -DNS_BLOCK_ASSERTIONS=1 -DOBJC_OLD_DISPATCH_PROTOTYPES=0 \
    -DTF2_DXMT_FINGERPRINT='\"$fingerprint\"' -DTF2_PAYLOAD_SOURCE='\"$source\"' -c tools/ios/payload/payload.m -o $out/obj/payload.o"

# One exported symbol; the runtime's archive is force-loaded because it registers its parts in constructors.
$cxx -dynamiclib -install_name @rpath/TF2Game.framework/TF2Game -Wl,-exported_symbol,_tf2_payload \
    -o "$out/TF2Game.framework/TF2Game" "$out"/obj/*.o \
    -Wl,-force_load,build/ios-iphoneos/runtime/librt.a -Wl,-filelist,"$out/objects.filelist" \
    -framework UIKit -framework Foundation -framework Metal -framework QuartzCore -framework CoreGraphics \
    -framework GameController -framework AudioToolbox -framework AVFAudio -lobjc
abi=$(sed -n 's/^#define TF2_PAYLOAD_ABI \([0-9]*\)$/\1/p' tools/ios/payload/tf2_payload.h)
/usr/bin/python3 -I -c '
import plistlib, sys
path, prefix, abi, fingerprint, source = sys.argv[1:]
plistlib.dump({"CFBundleIdentifier": prefix + ".titanfall.game", "CFBundleName": "TF2Game", "CFBundleExecutable": "TF2Game",
               "CFBundlePackageType": "FMWK", "CFBundleShortVersionString": "0.1", "CFBundleVersion": "1",
               "CFBundleSupportedPlatforms": ["iPhoneOS"], "MinimumOSVersion": "18.0",
               "TF2PayloadABI": int(abi), "TF2DXMTFingerprint": fingerprint, "TF2PayloadSource": source,
               "TF2ContainsGameCode": True}, open(path, "wb"))
' "$out/TF2Game.framework/Info.plist" "$prefix" "$abi" "$fingerprint" "$source"
exports=$(nm -gU "$out/TF2Game.framework/TF2Game" | awk '{print $3}' | paste -sd' ' -)
[ "$exports" = _tf2_payload ] || { echo "unexpected exports: $exports" >&2; exit 1; }
echo "payload: $out/TF2Game.framework ($(du -sh "$out/TF2Game.framework" | cut -f1); interface $abi, DXMT $fingerprint)"
echo "it contains code translated from your game: keep it for your own devices"
