#!/usr/bin/env python3
"""Assemble your private, playable TF2 app: the public shell, your game payload, signed with your own team.

Usage: assemble_private_ipa.py --shell SHELL --payload FRAMEWORK --signing STUB_APP --out DIR

  SHELL      the public shell: TF2-shell-*.ipa from a release, or a TFNative.app built by tools/ios/build-shell.sh
  FRAMEWORK  build/ios-payload/TF2Game.framework from tools/ios/build-payload.sh
  STUB_APP   TF2Profile.app built by Xcode from tools/ios/profile-stub with your team and bundle prefix; its
             provisioning profile, entitlements and signing certificate are used for the whole app
  DIR        a folder outside this checkout

Writes DIR/TFNative.app (install it with `xcrun devicectl device install app`) and, through
tools/ios/package-ipa.sh, an .ipa with its .sha256 and .json manifest. The result contains code translated from
your game: keep it for your own devices and never share it.
"""
import argparse
import datetime
import hashlib
import plistlib
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EVA = "com.apple.developer.kernel.extended-virtual-addressing"


def die(message):
    sys.exit(f"assemble: {message}")


def run(*args, capture=False):
    result = subprocess.run([str(a) for a in args], capture_output=True, text=not capture or None)
    if result.returncode != 0:
        err = result.stderr if isinstance(result.stderr, str) else result.stderr.decode(errors="replace")
        die(f"{Path(str(args[0])).name} failed: {err.strip()}")
    return result.stdout


def read_plist(path):
    with open(path, "rb") as f:
        return plistlib.load(f)


def write_plist_like(path, data):
    """Write data in the format (binary or XML) that path already has."""
    raw = Path(path).read_bytes()
    fmt = plistlib.FMT_BINARY if raw.startswith(b"bplist") else plistlib.FMT_XML
    with open(path, "wb") as f:
        plistlib.dump(data, f, fmt=fmt)


def find_app(path, work):
    path = Path(path)
    if path.suffix == ".app" and path.is_dir():
        return path
    if path.suffix == ".ipa" and path.is_file():
        run("ditto", "-x", "-k", path, work / "shell")
        apps = list((work / "shell" / "Payload").glob("*.app"))
        if len(apps) != 1:
            die(f"{path} does not hold exactly one app")
        return apps[0]
    die(f"not an .app folder or .ipa file: {path}")


def signing_identity(stub, work):
    """The SHA-1 of the certificate that signed the stub, if that identity is in this Mac's keychain."""
    run("codesign", "-d", f"--extract-certificates={work}/cert", stub)
    leaf = (work / "cert0").read_bytes()
    sha1 = hashlib.sha1(leaf).hexdigest().upper()
    identities = run("security", "find-identity", "-v", "-p", "codesigning")
    if sha1 not in identities:
        die("the certificate that signed the stub is not in this Mac's keychain")
    return sha1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--shell", required=True)
    parser.add_argument("--payload", required=True)
    parser.add_argument("--signing", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    out = Path(args.out).expanduser().resolve()
    if out == ROOT or ROOT in out.parents:
        die(f"{out} is inside this checkout; choose a folder outside it")
    payload = Path(args.payload).resolve()
    stub = Path(args.signing).resolve()
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        shell = find_app(args.shell, work)

        # The shell and the payload must speak the same interface and use the same DXMT.
        shell_info = read_plist(shell / "Info.plist")
        payload_info = read_plist(payload / "Info.plist")
        if not shell_info.get("TF2Shell"):
            die(f"{shell} is not a TF2 shell (no TF2Shell key)")
        for key in ("TF2PayloadABI", "TF2DXMTFingerprint"):
            if shell_info.get(key) != payload_info.get(key):
                die(f"{key}: shell {shell_info.get(key)!r}, payload {payload_info.get(key)!r}; "
                    "build the payload from the same release as the shell")
        exports = run("nm", "-gU", payload / payload_info["CFBundleExecutable"]).split()
        if exports[-1:] != ["_tf2_payload"] or len(exports) != 3:
            die(f"the payload must export only _tf2_payload, not {exports[2::3]}")
        run("sh", ROOT / "tools/ios/audit-shell.sh", shell)

        # Everything about signing comes from the stub Xcode built for your team.
        profile_path = stub / "embedded.mobileprovision"
        if not profile_path.is_file():
            die(f"{stub} has no provisioning profile; build tools/ios/profile-stub with your team")
        profile = plistlib.loads(run("security", "cms", "-D", "-i", profile_path, capture=True))
        now = datetime.datetime.now(datetime.timezone.utc).replace(tzinfo=None)   # plistlib dates are naive UTC
        if profile["ExpirationDate"] <= now:
            die(f"the provisioning profile expired on {profile['ExpirationDate']}; build the stub again")
        entitlements_xml = run("codesign", "-d", "--entitlements", "-", "--xml", stub, capture=True)
        entitlements = plistlib.loads(entitlements_xml)
        if not entitlements.get(EVA):
            die(f"the stub was signed without {EVA}; your team must offer Extended Virtual Addressing")
        app_id = entitlements["application-identifier"]
        team, bundle = app_id.split(".", 1)
        identity = signing_identity(stub, work)
        (work / "entitlements.plist").write_bytes(entitlements_xml)

        # Assemble: the shell, the payload in Frameworks, your bundle identifier and profile.
        out.mkdir(parents=True, exist_ok=True)
        app = out / shell.name
        if app.exists():
            shutil.rmtree(app)
        shutil.copytree(shell, app, symlinks=True)
        shutil.copytree(payload, app / "Frameworks" / payload.name, symlinks=True)
        info = read_plist(app / "Info.plist")
        info["CFBundleIdentifier"] = bundle
        write_plist_like(app / "Info.plist", info)
        shutil.copy2(profile_path, app / "embedded.mobileprovision")

        # Sign the frameworks first, then the app with the stub's entitlements.
        for framework in sorted((app / "Frameworks").glob("*.framework")):
            run("codesign", "--force", "--sign", identity, "--timestamp=none", "--generate-entitlement-der", framework)
        run("codesign", "--force", "--sign", identity, "--timestamp=none", "--generate-entitlement-der",
            "--entitlements", work / "entitlements.plist", app)
        run("codesign", "--verify", "--deep", "--strict", app)
        signed = plistlib.loads(run("codesign", "-d", "--entitlements", "-", "--xml", app, capture=True))
        if signed.get(EVA) is not True or signed.get("application-identifier") != app_id:
            die("the signed app does not carry the expected entitlements")
        for framework in sorted((app / "Frameworks").glob("*.framework")):
            details = subprocess.run(["codesign", "-dv", str(framework)], capture_output=True, text=True).stderr
            if f"TeamIdentifier={team}" not in details:
                die(f"{framework.name} is not signed by team {team}")

    print(f"assembled {app}: bundle {bundle}, team {team}, profile expires {profile['ExpirationDate']:%Y-%m-%d}", flush=True)
    subprocess.run(["sh", str(ROOT / "tools/ios/package-ipa.sh"), str(app), str(out)], check=True)
    print(f"install: xcrun devicectl device install app --device DEVICE '{app}'")
    print("this app contains code translated from your game: keep it for your own devices; never share it")


if __name__ == "__main__":
    main()
