#!/bin/sh
# Fetch only owned engine DLLs for AOT work. No campaign/media assets or saved credentials.
set -eu
cd "$(dirname "$0")/../.."
repo=$(pwd -P)
downloader=${DEPOTDOWNLOADER:-}
if [ -z "$downloader" ]; then
    downloader=$(command -v DepotDownloader || true)
fi
if [ -z "$downloader" ] && [ -x ../tools/depotdownloader/3.4.0/DepotDownloader ]; then
    downloader="$(cd ../tools/depotdownloader/3.4.0 && pwd -P)/DepotDownloader"
fi
if [ ! -x "$downloader" ]; then
    echo 'Set DEPOTDOWNLOADER to the official SteamRE DepotDownloader executable.' >&2
    exit 1
fi
python3 - <<'PY'
import shutil
if shutil.disk_usage('.').free < 3 * 1024**3:
    raise SystemExit('Keep at least 3 GiB free before fetching engine files.')
PY
mkdir -p "$repo/game"
echo 'Authorize the QR code with the Steam mobile app for the account that owns Titanfall 2.'
echo 'This downloads only engine files. It does not download the full game or save your login.'
echo 'The native Steam desktop session does not authorize DepotDownloader automatically.'
exec "$downloader" -app 1237970 -os windows -osarch 64 -language english \
    -dir "$repo/game" -filelist "$repo/tools/ios/engine-files.txt" \
    -max-downloads 4 -loginid 123797028 -qr
