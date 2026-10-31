#!/bin/sh
# pull-native-run.sh OUT_DIR: copy the native game's logs and captured frames from the iPad's
# app container into OUT_DIR, converting frames to PNG. Never copies Documents/Game back.
cd "$(dirname "$0")/../.." || exit 2
out=${1:?usage: pull-native-run.sh OUT_DIR}
dev=${TF_DEVICE:?set TF_DEVICE to the device UDID or name (xcrun devicectl list devices)}
bundle=${TF_BUNDLE_PREFIX:-com.example}.titanfall.native
mkdir -p "$out/frames"
for f in game-progress.json game-runtime.log game-console.log game-result.json; do
    xcrun devicectl device copy from --device "$dev" --domain-type appDataContainer \
        --domain-identifier "$bundle" --source "Documents/$f" \
        --destination "$out/$f" >/dev/null 2>&1 || echo "no $f"
done
xcrun devicectl device copy from --device "$dev" --domain-type appDataContainer \
    --domain-identifier "$bundle" --source Documents/frames \
    --destination "$out/frames" >/dev/null 2>&1 || echo "no frames"
for s in "$out"/frames/*.rtshot; do
    [ -f "$s" ] || continue
    python3 -I tools/trace/d3d/frame_compare.py topng "$s" "${s%.rtshot}.png"
    printf '%s: ' "$(basename "$s")"; python3 -I tools/trace/d3d/frame_compare.py stats "$s"
done
cat "$out/game-progress.json" 2>/dev/null; echo
grep 'report: at\|menu: \|GATE3_CONSOLE\|mission' "$out/game-runtime.log" 2>/dev/null | cut -c1-400 | tail -12
