#!/bin/sh
# Build DXMT's native (no Wine) libraries on this Mac.  See docs/findings/35-dxmt-build.md.
#
#   sh tools/dxmt/build.sh                          the release build (asserts compiled out), third_party/dxmt/build-native
#   DXMT_BUILD_KIND=assert sh tools/dxmt/build.sh   the assert build (debugoptimized, b_ndebug=false),
#                                                   third_party/dxmt/build-native-assert
#   sh tools/dxmt/build.sh --print-dir              print the build directory of the chosen kind and do nothing else
#
# What it does, in order:
#   1. checks the tools and the clone (nothing is installed or downloaded);
#   2. if patches/dxmt/*.patch exist, applies the ones not yet applied to the local branch `tf2`
#      of the clone (third_party/dxmt) -- with no patches the clone is not touched at all;
#   3. configures the build directory of the chosen kind with meson if it is not configured yet;
#   4. builds incrementally with ninja (niced, DXMT_JOBS jobs: the machine is shared).
# Exit status is non-zero if anything fails.
#
# The two kinds are the same sources and the same targets (the three libraries, d3d10core and the standalone
# converter src/airconv/darwin/airconv).  They differ in what an assert does: nothing in the release build, an
# abort in the assert build.  docs/handoff/pic-fork.md says what that means for a shader the converter does not
# know.  The release build stays the default because other programs load it.
#
# Environment:  DXMT_BUILD_KIND  release (default) or assert
#               DXMT_LLVM15      LLVM 15 prefix (default /opt/homebrew/opt/llvm@15, keg-only: not linked)
#               DXMT_BUILD_DIR   build directory (default by kind, above; probe.sh and window.sh expect the default)
#               DXMT_JOBS        parallel compile jobs (default 6)
#
# The clone is shared with other workers and is git-ignored.  Never push it anywhere.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
dxmt="$repo/third_party/dxmt"
kind="${DXMT_BUILD_KIND:-release}"
case "$kind" in
  release) defbld="$dxmt/build-native";        setup="--buildtype release" ;;
  assert)  defbld="$dxmt/build-native-assert"; setup="--buildtype debugoptimized -Db_ndebug=false" ;;
  *) echo "build.sh: DXMT_BUILD_KIND must be release or assert, not '$kind'" >&2; exit 1 ;;
esac
bld="${DXMT_BUILD_DIR:-$defbld}"
jobs="${DXMT_JOBS:-6}"
if [ "${1:-}" = --print-dir ]; then echo "$bld"; exit 0; fi
llvm="${DXMT_LLVM15:-/opt/homebrew/opt/llvm@15}"
base=e94c312f5c054263acf261cfa109edf13e757587   # commit the findings were made against

die() { echo "build.sh: $*" >&2; exit 1; }

# --- 1. tools and sources --------------------------------------------------------------------
[ -d "$dxmt/.git" ] || die "no DXMT clone at $dxmt (expected: git clone of https://github.com/3Shain/dxmt at $base)"
for t in meson ninja xcrun xxd git; do
  command -v "$t" >/dev/null 2>&1 || die "missing tool: $t (this script installs nothing)"
done
[ -f "$llvm/include/llvm/IR/Module.h" ] && [ -f "$llvm/lib/libLLVMSupport.a" ] \
  || die "LLVM 15 with headers and static libraries not found at $llvm (set DXMT_LLVM15)"
"$llvm/bin/llvm-config" --version 2>/dev/null | grep -q '^15\.' \
  || die "$llvm is not LLVM 15 (DXMT needs exactly LLVM 15)"
xcrun -sdk macosx --find metal >/dev/null 2>&1 \
  && xcrun -sdk macosx --find metallib >/dev/null 2>&1 \
  || die "Metal Toolchain missing (xcrun metal / metallib); owner runs: xcodebuild -downloadComponent MetalToolchain"
[ -f "$dxmt/include/native/directx/d3d11.h" ] \
  || die "submodule include/native/directx missing; owner runs: git -C $dxmt submodule update --init --depth 1 include/native/directx"
git -C "$dxmt" merge-base --is-ancestor "$base" HEAD 2>/dev/null \
  || die "clone HEAD is not based on $base"

# --- 2. patches ------------------------------------------------------------------------------
patches=$(ls "$repo"/patches/dxmt/*.patch 2>/dev/null || true)
if [ -n "$patches" ]; then
  if [ "$(git -C "$dxmt" rev-parse --abbrev-ref HEAD)" != tf2 ]; then
    git -C "$dxmt" diff --quiet && git -C "$dxmt" diff --cached --quiet \
      || die "clone has uncommitted changes; refusing to switch it to branch tf2"
    if git -C "$dxmt" rev-parse --verify -q tf2 >/dev/null; then
      git -C "$dxmt" checkout -q tf2
    else
      git -C "$dxmt" checkout -q -b tf2 "$base"
    fi
  fi
  applied_subjects=$(git -C "$dxmt" log --format=%s "$base..HEAD")
  for p in $patches; do
    # Applied if it reverses cleanly, or if a commit on tf2 since the base has its subject (a later
    # patch may change lines next to an earlier one, and then the earlier one no longer reverses).
    subj=$(sed -n 's/^Subject: \(\[PATCH[^]]*\] \)\{0,1\}//p' "$p" | head -1)
    if git -C "$dxmt" apply --check --reverse "$p" >/dev/null 2>&1 \
       || { [ -n "$subj" ] && printf '%s\n' "$applied_subjects" | grep -qF -- "$subj"; }; then
      echo "build.sh: already applied: $(basename "$p")"
    else
      echo "build.sh: applying $(basename "$p")"
      git -C "$dxmt" -c user.name="dxmt-build" -c user.email="dxmt-build@localhost" am --quiet "$p" \
        || { git -C "$dxmt" am --abort >/dev/null 2>&1 || true; die "patch $(basename "$p") does not apply"; }
    fi
  done
fi

# --- 3. configure ----------------------------------------------------------------------------
if [ ! -f "$bld/build.ninja" ]; then
  echo "build.sh: configuring $bld ($kind)"
  # shellcheck disable=SC2086
  meson setup "$bld" "$dxmt" -Dnative_llvm_path="$llvm" $setup
fi

# --- 4. build --------------------------------------------------------------------------------
nice meson compile -C "$bld" -j "$jobs"

for f in nativemetal/winemetal.dylib dxgi/dxgi.dylib d3d11/d3d11.dylib d3d10/d3d10core.dylib airconv/darwin/airconv; do
  [ -f "$bld/src/$f" ] || die "expected output missing: $bld/src/$f"
done
echo "build.sh: ok ($kind) -> $bld/src"
