#!/bin/sh
# Compile DXMT's Metal bridge objects only. This does not link the renderer,
# convert game shaders, or establish graphics execution on a device.
# Requires the pinned DXMT clone, existing native patches, then patches/dxmt-ios/.
set -eu
cd "$(dirname "$0")/../.."
sdk="${TF_DXMT_SDK:-iphoneos}"
case "$sdk" in
    iphoneos) target=arm64-apple-ios18.0 ;;
    macosx) target=arm64-apple-macos26.5 ;;
    *) echo 'TF_DXMT_SDK must be iphoneos or macosx' >&2; exit 2 ;;
esac
dxmt=third_party/dxmt
test -f "$dxmt/src/winemetal/unix/winemetal_unix.c" || {
    echo 'Pinned DXMT source is missing; see PORTING.md.' >&2; exit 1;
}
out="build/ios-dxmt-bridge/$sdk"
mkdir -p "$out"
for source in src/winemetal/winemetal_thunks.c src/winemetal/unix/winemetal_unix.c \
              src/winemetal/unix/cache.c src/nativemetal/host_hooks.c; do
    name=$(basename "$source" .c)
    xcrun --sdk "$sdk" clang -target "$target" -x objective-c -std=gnu17 \
        -fblocks -fno-objc-arc -DDXMT_NATIVE=1 \
        -Werror=objc-method-access -Werror=unguarded-availability-new \
        -I"$dxmt/include" -I"$dxmt/libs" -I"$dxmt/src/nativemetal" -I"$dxmt/src/airconv" \
        -c "$dxmt/$source" -o "$out/$name.o"
done
echo "Compiled four $sdk bridge objects in $out; renderer is not linked or tested."
