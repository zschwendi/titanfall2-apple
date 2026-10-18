#!/usr/bin/env python3
"""dxmt_syntax_check.py -- does DXMT's native front end parse with the Xcode on this Mac? (finding 35)

This is NOT a build.  meson, ninja, LLVM 15 and the Metal Toolchain are missing, so the real build cannot
start.  This script runs `clang -fsyntax-only` on every C++/ObjC source of the native configuration that
does not need LLVM, with the include paths, defines and flags that third_party/dxmt/meson.build,
src/util/meson.build, src/nativemetal/meson.build and the per-directory meson.build files give them.
Two generated headers are stood in for by stubs written to a temporary directory (version.h, and the
xxd dump of the compiled Metal library that src/dxmt/dxmt_command.cpp includes).  Nothing is linked.

The airconv sources (src/airconv/*.cpp) are skipped: they include LLVM 15 headers.  Homebrew's llvm here is 23.

Usage:  .venv/bin/python tools/scout/dxmt_syntax_check.py [--dxmt third_party/dxmt] [-j N]
"""

import argparse
import concurrent.futures
import glob
import os
import subprocess
import sys
import tempfile


def sources(src):
    cpp = []
    for d in ("d3d11", "d3d10", "dxgi", "dxmt"):
        cpp += sorted(glob.glob(os.path.join(src, d, "*.cpp")))
    util = ["util_env.cpp", "util_string.cpp", "util_bloom.cpp", "util_md5.cpp", "thread.cpp",
            "com/com_guid.cpp", "com/com_private_data.cpp", "config/config.cpp", "log/log.cpp",
            "sha1/sha1_util.cpp", "wsi_monitor_headless.cpp", "wsi_window_headless.cpp", "wsi_platform_darwin.cpp"]
    cpp += [os.path.join(src, "util", u) for u in util]
    cpp += sorted(glob.glob(os.path.join(os.path.dirname(src), "libs", "DXBCParser", "*.cpp")))
    objc = [os.path.join(src, "winemetal", "winemetal_thunks.c"),
            os.path.join(src, "winemetal", "unix", "winemetal_unix.c"),
            os.path.join(src, "winemetal", "unix", "cache.c")]
    return cpp, objc


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dxmt", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "third_party", "dxmt"))
    ap.add_argument("-j", type=int, default=4)
    a = ap.parse_args()
    root = os.path.abspath(a.dxmt)
    src = os.path.join(root, "src")
    if not os.path.isfile(os.path.join(root, "include/native/directx/d3d11.h")):
        sys.exit("need the include/native/directx submodule checked out in %s" % root)
    with tempfile.TemporaryDirectory() as gen:
        with open(os.path.join(gen, "version.h"), "w") as f:
            f.write('#pragma once\n#define DXMT_VERSION "scout-stub"\n')
        with open(os.path.join(gen, "dxmt_command.h"), "w") as f:       # xxd -i of dxmt_command.metallib
            f.write("unsigned char dxmt_command[] = {0};\nunsigned int dxmt_command_len = 1;\n")
        inc = [root + "/include", root + "/libs", root + "/include/native/windows", root + "/include/native/directx", gen]
        inc += [os.path.join(src, d) for d in ("util", "dxmt", "dxgi", "d3d11", "d3d10", "winemetal", "nativemetal", "airconv")]
        args_common = ["-fsyntax-only", "-DDXMT_PAGE_SIZE=4096", "-DNOMINMAX", "-D_WIN32_WINNT=0xa00", "-fblocks",
                       "-Wno-unused-parameter", "-Wno-missing-braces", "-Wimplicit-fallthrough"] + ["-I" + i for i in inc]
        cpp, objc = sources(src)

        def run(path):
            if path in objc:
                # src/nativemetal/meson.build: include dirs are the project include dirs, '.' (src/nativemetal) first,
                # and src/airconv (through airconv_dep_darwin); not src/winemetal, whose wineunixlib.h is Wine's.
                oinc = ["-I" + i for i in (root + "/include", root + "/libs", src + "/nativemetal", src + "/airconv")]
                cmd = ["clang", "-DDXMT_NATIVE=1", "-x", "objective-c", "-fsyntax-only", "-fblocks", "-Wno-unused-parameter"] + oinc + [path]
            else:
                cmd = ["clang++", "-std=c++20", "-w"] + args_common + [path]
            r = subprocess.run(cmd, capture_output=True, text=True)
            return path, r.returncode, r.stderr

        bad = []
        with concurrent.futures.ThreadPoolExecutor(a.j) as ex:
            for path, rc, err in ex.map(run, cpp + objc):
                rel = os.path.relpath(path, root)
                if rc:
                    bad.append(rel)
                    print("FAIL %s\n%s" % (rel, "\n".join(err.splitlines()[:12])))
        print("syntax-checked %d sources (arm64-apple-macos, Apple clang): %d failed" % (len(cpp) + len(objc), len(bad)))
        return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
