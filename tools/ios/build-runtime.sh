#!/bin/sh
# Builds the actual portable runtime, not the game or an installable launcher.
# A game build additionally needs owned game DLLs/assets and a vision/iOS DXMT port.
set -eu
cd "$(dirname "$0")/../.."
sdk="${TF_IOS_SDK:-iphoneos}"
case "$sdk" in
  iphoneos) target=arm64-apple-ios18.0 ;;
  iphonesimulator) target=arm64-apple-ios18.0-simulator ;;
  *) echo 'TF_IOS_SDK must be iphoneos or iphonesimulator' >&2; exit 2 ;;
esac
exec make -j "${TF_BUILD_JOBS:-4}" rt rt-link-check PY=python3 RT_PLATFORM=ios \
    RT_CC="xcrun --sdk $sdk clang -target $target" \
    RT_OUT="build/ios-$sdk/runtime" RT_GEN="build/ios-$sdk/gen" GUEST_MEM=tsob
