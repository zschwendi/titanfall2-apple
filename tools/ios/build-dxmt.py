#!/usr/bin/env python3
"""Configure/build the actual DXMT libraries for iPhoneOS. No game assets copied."""
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'third_party/dxmt'
OUTPUT = ROOT / 'build/dxmt-ios'
LLVM_BUILD = ROOT / 'build/llvm15-ios'
LLVM_SOURCE = ROOT / 'third_party/llvm15'

def run(args):
    subprocess.run([str(arg) for arg in args], cwd=ROOT, check=True)

sdk = subprocess.check_output(['xcrun', '--sdk', 'iphoneos', '--show-sdk-path'], text=True).strip()
flags = ['-target', 'arm64-apple-ios18.0', '-isysroot', sdk]
cross = ROOT / 'build/ios-dxmt-cross.ini'
cross.parent.mkdir(exist_ok=True)
cross.write_text('''[binaries]
c = ['xcrun', '--sdk', 'iphoneos', 'clang']
cpp = ['xcrun', '--sdk', 'iphoneos', 'clang++']
ar = ['xcrun', 'ar']
strip = ['xcrun', 'strip']
[host_machine]
system = 'darwin'
cpu_family = 'aarch64'
cpu = 'arm64'
endian = 'little'
[properties]
needs_exe_wrapper = true
[built-in options]
''' + '\n'.join(f'{key} = {flags!r}' for key in ('c_args', 'cpp_args', 'c_link_args', 'cpp_link_args')) + '\n')

meson = ROOT / '.venv/bin/meson'
setup = [meson, 'setup', OUTPUT, SOURCE, '--cross-file', cross,
         '--buildtype', 'release', '-Dnative_apple_platform=ios',
         f'-Dnative_llvm_path={LLVM_BUILD}',
         f'-Dnative_llvm_source_include={LLVM_SOURCE / "include"}']
if (OUTPUT / 'meson-private/coredata.dat').exists():
    setup.append('--reconfigure')
run(setup)
if '--configure-only' not in sys.argv:
    run([meson, 'compile', '-C', OUTPUT, '-j', os.environ.get('TF_BUILD_JOBS', '2')])
