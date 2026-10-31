#!/bin/sh
# Print the fingerprint of the DXMT this checkout builds: the pinned commit and every patch, in order.
# The shell app and the game payload must be built with the same one (tools/ios/payload/tf2_payload.h).
set -eu
cd "$(dirname "$0")/../.."
commit=$(sed -n 's/^DXMT_COMMIT = "\([0-9a-f]*\)"$/\1/p' tfrecomp)
[ -n "$commit" ] || { echo "no DXMT_COMMIT in tfrecomp" >&2; exit 2; }
{ echo "$commit"; cat patches/dxmt/*.patch patches/dxmt-ios/*.patch; } | shasum -a 256 | cut -c1-16
