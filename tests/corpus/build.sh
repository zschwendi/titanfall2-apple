#!/bin/sh
# Build the test corpus as Windows x64 DLLs with the Microsoft ABI.
# Needs Apple clang (compiles COFF objects) and lld-link (Homebrew: brew install lld).
#
# Every .c and .cpp file in this directory is compiled; adding a file needs no change here.
# Output, one set per optimisation level:
#   build/corpus_<opt>.dll       everything
#   build/one_<name>_<opt>.dll   one source file alone (plus mem_str.c, which defines memcpy,
#                                memset, memmove and _fltused for the freestanding build)
set -eu
cd "$(dirname "$0")"
LLD=${LLD:-$(command -v lld-link || echo /opt/homebrew/opt/lld/bin/lld-link)}
mkdir -p build
CFLAGS="--target=x86_64-pc-windows-msvc -ffreestanding -fno-stack-protector -fno-exceptions -mno-avx -msse2 -fno-math-errno -funwind-tables -Wall -Wno-unused-function"
# /Brepro: no timestamp, so the same sources always give the same bytes (test caches are keyed by DLL hash)
LINK="/dll /noentry /nodefaultlib /machine:x64 /dynamicbase /Brepro"
for OPT in O1 O2 Os; do
    OBJS=""
    for SRC in *.c *.cpp; do
        [ -e "$SRC" ] || continue
        NAME=${SRC%.*}
        case "$SRC" in
            *.cpp) clang++ $CFLAGS -fno-rtti -$OPT -c "$SRC" -o "build/${NAME}_$OPT.obj" ;;
            *)     clang   $CFLAGS -$OPT -c "$SRC" -o "build/${NAME}_$OPT.obj" ;;
        esac
        OBJS="$OBJS build/${NAME}_$OPT.obj"
    done
    if [ "${COMPILE_ONLY:-0}" = 1 ]; then continue; fi
    "$LLD" $LINK "/out:build/corpus_$OPT.dll" $OBJS
    for SRC in *.c *.cpp; do
        [ -e "$SRC" ] || continue
        NAME=${SRC%.*}
        if [ "$NAME" = mem_str ]; then
            "$LLD" $LINK "/out:build/one_${NAME}_$OPT.dll" "build/${NAME}_$OPT.obj"
        else
            "$LLD" $LINK "/out:build/one_${NAME}_$OPT.dll" "build/${NAME}_$OPT.obj" "build/mem_str_$OPT.obj"
        fi
    done
done
ls -la build/*.dll 2>/dev/null || echo "objects built; link skipped"
