#!/usr/bin/env python3
"""Prepare menu/training assets as hard links; never edit or duplicate Steam data.

Transfer Game to the app's Documents/Game first. Transfer game-files-ready.json
only after the entire file transfer succeeds. Later campaign maps are not included.
"""
from pathlib import Path
from collections import Counter
import json
import os
import re

ROOT = Path(__file__).resolve().parents[2]
GAME = (ROOT / 'game').resolve()
OUTPUT = ROOT / 'build/ios-game-assets'
STAGE = OUTPUT / 'Game'
MAP_NAME = re.compile(r'^(?:english)?(?:client_|server_)?(mp_|sp_)', re.I)
ENGLISH_AUDIO = 'r2/sound/general_english.mstr'

# A normal install folder already holds the English files. Steam console downloads
# keep them in their own depot beside the main one (or name it with TF_ENGLISH_DEPOT).
if (GAME / ENGLISH_AUDIO).is_file():
    SOURCES = (GAME,)
else:
    SOURCES = (GAME, Path(os.environ.get('TF_ENGLISH_DEPOT') or GAME.parent / 'depot_1237972').resolve())
if not (GAME / 'bin/x64_retail/engine.dll').is_file() or not (SOURCES[-1] / ENGLISH_AUDIO).is_file():
    raise SystemExit('The owned game files, including the English audio, must be complete.')

selected = {}
omitted = Counter()
for source_root in SOURCES:
    for source in sorted(source_root.rglob('*')):
        if not source.is_file():
            continue
        relative = source.relative_to(source_root)
        name = source.name.lower()
        reason = None
        if relative.parts[0] in ('__Installer', 'Support', 'Core', '__overlay'):
            reason = 'desktop installer and support files'
        elif MAP_NAME.match(name) and name.startswith(('mp_', 'client_mp_', 'server_mp_', 'englishclient_mp_', 'englishserver_mp_')):
            if not any(shared in name for shared in ('mp_common', 'mp_lobby')):
                reason = 'other multiplayer maps'
        elif MAP_NAME.match(name) and 'sp_training' not in name:
            reason = 'later campaign maps'
        if reason:
            omitted[reason] += source.stat().st_size
        else:
            selected[relative.as_posix()] = source

rows = []
for relative, source in sorted(selected.items()):
    destination = STAGE / relative
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists():
        if not os.path.samefile(source, destination):
            raise SystemExit(f'Refusing to replace a different staging file: {destination}')
    else:
        os.link(source, destination)
    rows.append({'path': relative, 'size': source.stat().st_size})

manifest = {'scope': 'menu and pilot training; later campaign maps and multiplayer omitted',
            'bytes': sum(row['size'] for row in rows), 'files': rows}
(OUTPUT / 'game-files-ready.json').write_text(json.dumps(manifest, indent=2) + '\n')
(OUTPUT / 'selection-report.json').write_text(json.dumps({'selected_bytes': manifest['bytes'],
    'selected_files': len(rows), 'omitted_bytes': dict(omitted), 'main_depot': str(GAME),
    'english_depot': str(SOURCES[-1]), 'staging': str(STAGE), 'method': 'same-volume hard links; source untouched'}, indent=2) + '\n')
print(json.dumps({'files': len(rows), 'bytes': manifest['bytes'], 'staging': str(STAGE)}))
