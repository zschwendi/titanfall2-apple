#!/usr/bin/env python3
"""Selftest for ./tfrecomp. Builds a fake checkout in a temporary directory and runs the script
against it as a subprocess. Nothing under the real checkout is read, built or changed.

Usage: python3 tools/tfrecomp/selftest.py
"""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.realpath(__file__))
REAL_ROOT = os.path.dirname(os.path.dirname(HERE))
SCRIPT = os.path.join(REAL_ROOT, "tfrecomp")
DLLS = ["tier0.dll", "vstdlib.dll", "launcher.dll"]
PASSED = [0]


def check(cond, msg):
    if not cond:
        print(f"FAIL: {msg}")
        sys.exit(1)
    PASSED[0] += 1


def make_fake_root(tmp, name="root"):
    root = os.path.join(tmp, name)
    os.makedirs(os.path.join(root, "gate3"))
    with open(os.path.join(root, "gate3", "modules.def"), "w") as f:
        f.write("LIBRARY fake\n")
        for d in DLLS:
            f.write(f"lifted   {d}              dll=game/bin/x64_retail/{d}\n")
    shutil.copy(SCRIPT, os.path.join(root, "tfrecomp"))
    os.chmod(os.path.join(root, "tfrecomp"), 0o755)
    return root


def make_game(tmp, skip=None):
    game = os.path.join(tmp, "titanfall")
    os.makedirs(os.path.join(game, "bin", "x64_retail"))
    for d in DLLS:
        if d != skip:
            with open(os.path.join(game, "bin", "x64_retail", d), "w") as f:
                f.write("x")
    return game


def make_bundle(root):
    prog = os.path.join(root, "build", "app", "TFRecomp.app", "Contents", "MacOS", "gate3")
    os.makedirs(os.path.dirname(prog))
    with open(prog, "w") as f:
        f.write("#!/bin/sh\nexit 3\n")
    os.chmod(prog, 0o755)


def make_fake_venv(root):
    py = os.path.join(root, ".venv", "bin", "python")
    os.makedirs(os.path.dirname(py))
    with open(py, "w") as f:
        f.write("#!/bin/sh\nexit 0\n")
    os.chmod(py, 0o755)


def run(root, args, home, sign=None, cwd=None):
    env = {k: v for k, v in os.environ.items() if k != "TFRECOMP_SIGN"}
    env["HOME"] = home
    if sign is not None:
        env["TFRECOMP_SIGN"] = sign
    proc = subprocess.run([sys.executable, os.path.join(root, "tfrecomp")] + args,
                          cwd=cwd or root, env=env, capture_output=True, text=True)
    return proc.returncode, proc.stdout + proc.stderr


def main():
    with tempfile.TemporaryDirectory() as tmp:
        home = os.path.join(tmp, "home")
        os.makedirs(home)
        game = make_game(tmp)

        # setup --dry-run in a fresh root: prints venv, pip and three git commands, creates nothing
        root = make_fake_root(tmp, "fresh")
        rc, out = run(root, ["setup", "--game", game, "--dry-run"], home)
        check(rc == 0, f"setup --dry-run exit {rc}: {out}")
        check("run: python3 -m venv .venv" in out, "dry-run prints venv command")
        check("run: .venv/bin/python -m pip install -r requirements.txt" in out, "dry-run prints pip")
        check("run: git clone https://github.com/3Shain/dxmt third_party/dxmt" in out, "dry-run git clone")
        check("run: git -C third_party/dxmt checkout -q e94c312f" in out, "dry-run git checkout")
        check("submodule update --init --depth 1 include/native/directx" in out, "dry-run submodule")
        check(not os.path.exists(os.path.join(root, ".venv")), "dry-run created .venv")
        check(not os.path.lexists(os.path.join(root, "game")), "dry-run created game link")
        check(not os.path.exists(os.path.join(root, "third_party")), "dry-run created third_party")

        # real setup with a fake venv and an existing DXMT checkout: only the game link is made
        root = make_fake_root(tmp, "real")
        make_fake_venv(root)
        os.makedirs(os.path.join(root, "third_party", "dxmt", ".git"))
        rc, out = run(root, ["setup", "--game", game], home)
        check(rc == 0, f"setup exit {rc}: {out}")
        link = os.path.join(root, "game")
        check(os.path.islink(link), "setup made game a symlink")
        check(os.path.realpath(link) == os.path.realpath(game), "game link points at the game folder")
        check("setup: done; next: ./tfrecomp build" in out, "setup last line")
        rc, out = run(root, ["setup", "--game", game], home)
        check(rc == 0 and "already links" in out, f"second setup says already there: {out}")

        # game is a real directory: exit 2, nothing changed
        root = make_fake_root(tmp, "realdir")
        make_fake_venv(root)
        os.makedirs(os.path.join(root, "third_party", "dxmt", ".git"))
        os.makedirs(os.path.join(root, "game"))
        with open(os.path.join(root, "game", "keep.txt"), "w") as f:
            f.write("keep")
        rc, out = run(root, ["setup", "--game", game], home)
        check(rc == 2, f"real game dir: exit {rc}")
        check(os.path.isdir(os.path.join(root, "game")) and not os.path.islink(os.path.join(root, "game")),
              "real game dir left as a directory")
        check(os.listdir(os.path.join(root, "game")) == ["keep.txt"], "real game dir contents unchanged")

        # a different symlink at game: exit 2
        root = make_fake_root(tmp, "otherlink")
        other = os.path.join(tmp, "other")
        os.makedirs(other)
        os.symlink(other, os.path.join(root, "game"))
        rc, out = run(root, ["setup", "--game", game], home)
        check(rc == 2 and os.readlink(os.path.join(root, "game")) == other, "other link: exit 2, unchanged")

        # a game folder missing one DLL: exit 2 naming it
        root = make_fake_root(tmp, "nodll")
        bad_game = make_game(os.path.join(tmp, "partial"), skip="vstdlib.dll")
        rc, out = run(root, ["setup", "--game", bad_game], home)
        check(rc == 2 and "vstdlib.dll" in out, f"missing DLL named: exit {rc}, {out}")
        check(not os.path.lexists(os.path.join(root, "game")), "missing DLL: no link made")

        # build --dry-run --jobs 3, adhoc and dev
        root = make_fake_root(tmp, "build")
        rc, out = run(root, ["build", "--dry-run", "--jobs", "3"], home, sign="adhoc")
        check(rc == 0, f"build adhoc dry exit {rc}")
        check("run: make gate3-build GUEST_MEM=tsob GATE3_JOBS=3" in out, "adhoc gate3 build line")
        check("run: make -f app/app.mk app-game APP_SIGN=adhoc" in out, "adhoc app line")
        check("run: sh tools/dxmt/load_probe.sh" in out, "build step 1")
        check("signing: adhoc" in out.splitlines()[0], "build first line is signing")
        rc, out = run(root, ["build", "--dry-run", "--jobs", "3"], home, sign="dev")
        check(rc == 0, f"build dev dry exit {rc}")
        check("run: make gate3-build GATE3_JOBS=3" in out, "dev gate3 build line")
        check("run: make -f app/app.mk app-game\n" in out + "\n", "dev app line has no APP_SIGN")
        check("GUEST_MEM" not in out and "APP_SIGN" not in out, "dev build has no adhoc variables")
        check("signing: dev (hardware x86 memory ordering)" in out.splitlines()[0], "dev first line")
        check(run(root, ["build", "--dry-run", "--jobs", "0"], home)[0] == 2, "build --jobs 0 exit 2")

        # play --dry-run with a fake bundle
        root = make_fake_root(tmp, "play")
        make_bundle(root)
        state = os.path.join(home, "Library", "Application Support", "tfrecomp", "player")
        rc, out = run(root, ["play", "--dry-run"], home, sign="adhoc")
        check(rc == 0, f"play dry exit {rc}: {out}")
        line = [l for l in out.splitlines() if l.startswith("run: ")][0]
        for want in ["RT_TSO=off", "RT_D3D_BACKEND=dxmt",
                     f"RT_DXMT_DIR={os.path.realpath(root)}/build/app/TFRecomp.app/Contents/Frameworks",
                     "RT_HOST_WINDOW=appkit-fullscreen", "RT_HOST_SCREEN=builtin", "GATE3_WORKERS=3",
                     "RT_D3D_PRESENT=immediate", "GATE3_CONSOLE=0:fps_max 0", "GATE3_SOUND=on",
                     "RT_HOST_AUDIO=coreaudio", "RT_HOST_AUDIO_VOLUME=0.4", "GATE3_TIMEOUT=14400",
                     f"GATE3_START=player:{state}", " run"]:
            check(want in line, f"play line has {want!r}: {line}")
        check("GATE3_SIZE" not in line and "GATE3_UPSCALE" not in line, "no size/upscale by default")
        rc, out = run(root, ["play", "--dry-run", "--size", "2016x1260", "--upscale", "1.5"], home, sign="dev")
        line = [l for l in out.splitlines() if l.startswith("run: ")][0]
        check(rc == 0 and "RT_TSO=require" in line, "dev play uses RT_TSO=require")
        check("GATE3_SIZE=2016x1260" in line and "GATE3_UPSCALE=1.5" in line, "size and upscale added")
        rc, out = run(root, ["play", "--dry-run", "--window", "hidden", "--volume", "0.2"], home, sign="adhoc")
        line = [l for l in out.splitlines() if l.startswith("run: ")][0]
        check("RT_HOST_WINDOW=appkit-hidden" in line and "RT_HOST_AUDIO=none" in line
              and "RT_HOST_AUDIO_VOLUME=0.2" in line, "hidden window settings")
        for bad in (["--size", "99x99"], ["--upscale", "3"], ["--volume", "2"], ["--window", "big"]):
            rc, out = run(root, ["play", "--dry-run"] + bad, home, sign="adhoc")
            check(rc == 2, f"play {bad} exit {rc}")
        check(run(root, ["play", "--dry-run", "--upscale", "nan"], home)[0] == 2, "upscale nan exit 2")

        # play without the bundle
        root = make_fake_root(tmp, "nobundle")
        check(run(root, ["play"], home)[0] == 2, "play without bundle exit 2")

        # real play: runs the fake program, propagates its status, creates the state's parent only
        root = make_fake_root(tmp, "realplay")
        make_bundle(root)
        playhome = os.path.join(tmp, "playhome")
        os.makedirs(playhome)
        rc, _ = run(root, ["play"], playhome, sign="adhoc")
        check(rc == 3, f"play propagates program status: {rc}")
        check(os.path.isdir(os.path.join(playhome, "Library", "Application Support", "tfrecomp")),
              "state parent created")
        check(not os.path.exists(os.path.join(playhome, "Library", "Application Support", "tfrecomp", "player")),
              "state dir itself not created")

        # smoke --dry-run: state missing -> new; state present -> save:
        root = make_fake_root(tmp, "smoke")
        make_bundle(root)
        smokehome = os.path.join(tmp, "smokehome")
        os.makedirs(smokehome)
        rc, out = run(root, ["smoke", "--dry-run", "sp_tday"], smokehome, sign="adhoc")
        check(rc == 0 and "SMOKE_TSO=off" in out and "SMOKE_START=new" in out
              and "sh gate3/smoke_maps.sh sp_tday" in out, f"smoke adhoc dry: {out}")
        os.makedirs(os.path.join(smokehome, "Library", "Application Support", "tfrecomp", "player"))
        rc, out = run(root, ["smoke", "--dry-run", "sp_tday"], smokehome, sign="dev")
        check(rc == 0 and "SMOKE_TSO=require" in out and "SMOKE_START=save:" in out,
              f"smoke dev dry with state: {out}")
        check(run(make_fake_root(tmp, "smokenobundle"), ["smoke", "sp_tday"], home)[0] == 2,
              "smoke without bundle exit 2")

        # bogus signing override
        root = make_fake_root(tmp, "sign")
        rc, _ = run(root, ["doctor"], home, sign="bogus")
        check(rc == 2, "TFRECOMP_SIGN=bogus exit 2")

        # doctor in the fake root: exit 1, MISSING python, every line well-formed
        rc, out = run(root, ["doctor"], home, cwd=tmp)
        check(rc == 1, f"doctor fake root exit {rc}")
        check("MISSING python:" in out, "doctor names python")
        check(out.splitlines()[-1].startswith("doctor: ") and out.splitlines()[-1].endswith("missing"),
              "doctor last line")
        for l in out.splitlines():
            check(l.startswith(("ok      ", "MISSING ", "signing: ", "doctor: ")), f"doctor line shape: {l!r}")

    # The free mode (a free Apple ID's Personal Team): from app/signing.local.mk, or TFRECOMP_SIGN.
    with tempfile.TemporaryDirectory() as tmp:
        root = make_fake_root(tmp, "freeroot")
        home = os.path.join(tmp, "home"); os.makedirs(home)
        os.makedirs(os.path.join(root, "app"), exist_ok=True)
        with open(os.path.join(root, "app", "signing.local.mk"), "w") as f:
            f.write("APP_SIGN    := free\nAPP_ID      := com.example.tfrecomp\nAPP_TEAM    := ABCDE12345\n")
        rc, out = run(root, ["build", "--dry-run", "--jobs", "2"], home)
        check(rc == 0 and out.startswith("signing: free"), "free: the mode comes from signing.local.mk")
        check("GUEST_MEM" not in out and "APP_SIGN=free" in out, "free: the normal build, signed with APP_SIGN=free")
        make_bundle(root)
        rc, out = run(root, ["play", "--dry-run"], home)
        check(rc == 0 and "RT_TSO=require" in out, "free: play asks for the hardware mode")
        rc, out = run(root, ["signing", "--dry-run", "--team", "ABCDE12345", "--bundle-id", "com.example.tfrecomp"], home)
        check(rc == 0 and "run: sh tools/signing/make_profile.sh --team ABCDE12345 --bundle-id com.example.tfrecomp" in out
              and "--paid" not in out, "signing --dry-run: the script, free by default")
        rc, out = run(root, ["signing", "--dry-run", "--team", "ABCDE12345", "--bundle-id", "x.y", "--paid"], home)
        check(rc == 0 and out.rstrip().endswith("--paid"), "signing --paid passes --paid")
        rc, out = run(root, ["build", "--dry-run"], home, sign="adhoc")
        check(rc == 0 and "GUEST_MEM=" in out, "TFRECOMP_SIGN=adhoc overrides the file")
        check(run(root, ["doctor"], home, sign="bogus")[0] == 2, "TFRECOMP_SIGN=bogus exit 2 (free is not bogus)")

    print(f"tfrecomp selftest: {PASSED[0]} checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
