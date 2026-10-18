#!/bin/sh
# Build Unity's HLSLcc (third_party/hlslcc, a clone; nothing of ours goes in it) into
# build/hlslcc/libhlslcc.a and the driver build/shaderxc/hlslcc2msl.
set -eu
cd "$(dirname "$0")/../.."
H=third_party/hlslcc
[ -d "$H/src" ] || { echo "build.sh: no clone at $H" >&2; exit 2; }
mkdir -p build/hlslcc/obj build/shaderxc
I="-I$H -I$H/include -I$H/src -I$H/src/cbstring -I$H/src/internal_includes"
for f in "$H"/src/*.cpp; do
  o=build/hlslcc/obj/$(basename "$f" .cpp).o
  [ "$o" -nt "$f" ] || xcrun clang++ -std=c++14 -O2 -w $I -c "$f" -o "$o"
done
for f in "$H"/src/cbstring/*.c; do
  o=build/hlslcc/obj/cb_$(basename "$f" .c).o
  [ "$o" -nt "$f" ] || xcrun clang -O2 -w -I"$H/src/cbstring" -c "$f" -o "$o"
done
rm -f build/hlslcc/libhlslcc.a && ar rcs build/hlslcc/libhlslcc.a build/hlslcc/obj/*.o
xcrun clang++ -std=c++14 -O2 $I tools/shaderxc/hlslcc2msl.cpp build/hlslcc/libhlslcc.a -o build/shaderxc/hlslcc2msl
echo "build.sh: build/shaderxc/hlslcc2msl"
