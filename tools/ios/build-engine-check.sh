#!/bin/sh
# Prepare the two-module native iOS diagnostic from the owner's game files.
# This builds no renderer and does not install or launch on a device.
set -eu
cd "$(dirname "$0")/../.."
py="${TF_PYTHON:-.venv/bin/python}"
"$py" - <<'PY'
import hashlib
from pathlib import Path
import tomllib

table = tomllib.loads(Path('lifter/guest_patches.toml').read_text())
rows = table['patch']
verified = []
for module in ('tier0', 'vstdlib'):
    source = Path('game/bin/x64_retail') / (module + '.dll')
    expected = {row['sha256'] for row in rows if row['module'] == module}
    data = source.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if len(expected) != 1 or digest not in expected:
        raise SystemExit(f'{source}: unsupported or incomplete DLL; refusing build')
    verified.append((source.name, data))
    print(f'Verified {module}: {digest}')
destination = Path('build/ios-engine-check/Resources/Game/bin/x64_retail')
destination.mkdir(parents=True, exist_ok=True)
for name, data in verified:
    target = destination / name
    if not target.exists() or target.read_bytes() != data:
        temporary = target.with_suffix('.dll.tmp')
        temporary.write_bytes(data)
        temporary.replace(target)
PY
make -j "${TF_BUILD_JOBS:-2}" PY="$py" RT_PLATFORM=ios RT_VM_LAYOUT=ios \
    RT_CC='xcrun --sdk iphoneos clang -target arm64-apple-ios18.0' \
    RT_OUT=build/ios-iphoneos/runtime RT_GEN=build/ios-iphoneos/gen \
    GUEST_MEM=tsob GATE2_OUT=build/ios-engine-check \
    rt rt-link-check \
    build/ios-iphoneos/runtime/lifted/tier0.o \
    build/ios-iphoneos/runtime/lifted/vstdlib.o \
    build/ios-engine-check/gen/gate2_table.c
xcodegen generate --spec tools/ios/engine-smoke/project.yml
