#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
# SPDX-License-Identifier: MIT
"""Audit a new macOS bundle without opening it or reading user profiles."""
import argparse
import json
import plistlib
import re
import subprocess
from pathlib import Path


def run(*args):
    return subprocess.check_output(args, text=True)


def version(value):
    return tuple(int(part) for part in value.split(".")) + (0,) * (3 - len(value.split(".")))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path)
    parser.add_argument("--maximum-os", default="14.4")
    parser.add_argument("--private-prefix", action="append", default=[])
    args = parser.parse_args()
    app = args.app.resolve()
    metadata = plistlib.loads((app / "Contents/Info.plist").read_bytes())
    problems, binaries = [], []
    max_minimum = "0.0"
    sensitive = re.compile(r"(^|/)(?:trusted-(?:clients|servers)[^/]*|(?:DeskBridge|Deskflow)\.conf|[^/]+\.(?:pem|key|p12|pfx|log))(?:$|/)", re.I)
    for path in sorted(app.rglob("*")):
        rel = path.relative_to(app).as_posix()
        if path.is_symlink():
            if not path.resolve().is_relative_to(app):
                problems.append("External symlink: " + rel)
            continue
        if not path.is_file():
            continue
        if sensitive.search(rel):
            problems.append("Private runtime filename: " + rel)
        data = path.read_bytes()
        for prefix in args.private_prefix:
            if prefix.encode() in data:
                problems.append("Private build path: " + rel)
        if data[:4] not in (b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca"):
            continue
        arches = run("/usr/bin/lipo", "-archs", str(path)).split()
        if "arm64" not in arches:
            problems.append("Missing arm64 architecture: " + rel)
        if rel.startswith("Contents/MacOS/") and arches != ["arm64"]:
            problems.append("Unexpected executable architecture: " + rel)
        commands = run("/usr/bin/otool", "-l", str(path))
        minimums = []
        for block in commands.split("Load command "):
            if re.search(r"^\s+cmd LC_BUILD_VERSION$", block, re.M):
                minimums += re.findall(r"^\s+minos\s+([0-9.]+)\s*$", block, re.M)
            elif re.search(r"^\s+cmd LC_VERSION_MIN_MACOSX$", block, re.M):
                minimums += re.findall(r"^\s+version\s+([0-9.]+)\s*$", block, re.M)
        minimum = max(minimums, key=version) if minimums else "0.0"
        max_minimum = max(max_minimum, minimum, key=version)
        if version(minimum) > version(args.maximum_os):
            problems.append("Dependency requires macOS " + minimum + ": " + rel)
        # LC_ID_DYLIB is an install ID, not a loaded dependency.
        for load in re.finditer(r"cmd LC_(?:LOAD_DYLIB|LOAD_WEAK_DYLIB|REEXPORT_DYLIB|LOAD_UPWARD_DYLIB|LAZY_LOAD_DYLIB)\n.*?\n\s+name (.*?) \(offset", commands, re.S):
            name = load.group(1)
            if not name.startswith(("@", "/System/Library/", "/usr/lib/", "/Library/Apple/System/Library/")):
                problems.append("External dependency " + name + ": " + rel)
        binaries.append({"path": rel, "architectures": arches, "minimumMacOS": minimum})
    declared = metadata.get("LSMinimumSystemVersion", "0.0")
    if version(declared) < version(max_minimum):
        problems.append("Info.plist minimum is below a bundled dependency")
    notice = app / "Contents/Resources/ThirdPartyLicenses/THIRD-PARTY-NOTICES.md"
    if not notice.is_file():
        problems.append("Missing third-party notice index")
    subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", str(app)], check=True)
    print(json.dumps({"version": metadata.get("CFBundleShortVersionString"), "bundleId": metadata.get("CFBundleIdentifier"), "declaredMinimumMacOS": declared, "actualMinimumMacOS": max_minimum, "binaries": binaries, "problems": problems}, indent=2))
    if problems:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
