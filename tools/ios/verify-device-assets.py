#!/usr/bin/env python3
"""Check a completed devicectl file inventory against the staged asset manifest.

Use device info files with subdirectory Documents/Game and JSON output. This
checks path/size completeness, not file hashes; require a successful copy first.
"""
import argparse
import json
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('inventory', type=Path)
parser.add_argument('--manifest', type=Path, default=Path(__file__).resolve().parents[2] / 'build/ios-game-assets/game-files-ready.json')
args = parser.parse_args()
inventory = json.loads(args.inventory.read_text())
if inventory.get('info', {}).get('outcome') != 'success':
    raise SystemExit('Device inventory did not succeed.')
files = inventory['result']['files']
actual = {row['relativePath']: row['metadata']['size'] for row in files
          if not row['resources']['isDirectory']}
expected = json.loads(args.manifest.read_text())['files']
mismatches = [{'path': row['path'], 'expected': row['size'], 'actual': actual.get(row['path'])}
              for row in expected if actual.get(row['path']) != row['size']]
print(json.dumps({'complete': not mismatches, 'expected_files': len(expected),
    'matching_files': len(expected) - len(mismatches),
    'expected_bytes': sum(row['size'] for row in expected),
    'reported_bytes': sum(actual.values()), 'mismatches': mismatches[:15],
    'mismatch_count': len(mismatches), 'proof': 'path and size inventory; not hashes'}, indent=2))
raise SystemExit(bool(mismatches))
