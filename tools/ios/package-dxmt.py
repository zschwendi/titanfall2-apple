#!/usr/bin/env python3
"""Wrap the device-built DXMT libraries as embeddable iOS frameworks."""
from pathlib import Path
import os
import plistlib
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build/dxmt-ios'
OUTPUT = ROOT / 'build/ios-dxmt-frameworks'
LIBRARIES = {'winemetal': 'nativemetal', 'dxgi': 'dxgi', 'd3d11': 'd3d11'}

for name, directory in LIBRARIES.items():
    source = BUILD / 'src' / directory / (name + '.dylib')
    metadata = subprocess.check_output(['xcrun', 'vtool', '-show-build', str(source)], text=True)
    if 'platform IOS\n' not in metadata:
        raise SystemExit(f'Refusing non-iOS renderer: {source}\n{metadata}')

for name, directory in LIBRARIES.items():
    framework = OUTPUT / (name + '.framework')
    framework.mkdir(parents=True, exist_ok=True)
    binary = framework / name
    shutil.copy2(BUILD / 'src' / directory / (name + '.dylib'), binary)
    args = ['xcrun', 'install_name_tool', '-id', f'@rpath/{name}.framework/{name}']
    dependencies = subprocess.check_output(['xcrun', 'otool', '-L', str(binary)], text=True)
    for dependency in LIBRARIES:
        old = f'@rpath/{dependency}.dylib'
        if old in dependencies:
            args += ['-change', old, f'@rpath/{dependency}.framework/{dependency}']
    subprocess.run(args + [str(binary)], check=True)
    info = {
        'CFBundleIdentifier': os.environ.get('TF_BUNDLE_PREFIX', 'com.example') + '.titanfall.dxmt.' + name,
        'CFBundleName': name, 'CFBundleExecutable': name, 'CFBundlePackageType': 'FMWK',
        'CFBundleShortVersionString': '0.1', 'CFBundleVersion': '1',
        'CFBundleSupportedPlatforms': ['iPhoneOS'], 'MinimumOSVersion': '18.0',
    }
    (framework / 'Info.plist').write_bytes(plistlib.dumps(info))
    print(framework)
