#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
# SPDX-License-Identifier: MIT
"""Prepare pinned official SDKs and notices in an isolated build directory."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile

QT_VERSION = "6.11.2"
QT_BASE = "https://download.qt.io/online/qtsdkrepository/mac_x64/desktop/qt6_6112/qt6_6112/qt.qt6.6112.clang_64/"
QT_SUFFIX = "-MacOS-MacOS_15-Clang-MacOS-MacOS_15-X86_64-ARM64.7z"
QT_ARCHIVES = {
    "qtbase": "9592f84f7e26d532c5c56824d1da7c9214a766cb0a17beb5af71022bcfbcd271",
    "qtdeclarative": "ceb8e3f3830531a52de5007ef3f50ffc5525021feb132ee322f59583eb2903cf",
    "qtsvg": "c52f6cec6ce4b52ca017e0046345a85d33b2b73e0f6a93474bb7a60f8e829317",
    "qttools": "415b5008059d0066ac0d4806de98f6e8dacf7b5c80ca0305875160d2107d3719",
    "qttranslations": "72d8806329194654cfb6157a14065b6367dd2e728e960b5f747fceede1b3f6b8",
}
SOURCES = {
    "qtbase-everywhere-src-6.11.2.tar.xz": (
        "https://download.qt.io/archive/qt/6.11/6.11.2/submodules/qtbase-everywhere-src-6.11.2.tar.xz",
        "5b2e00eccaf5a4d8c14134ffa0ea8dfd0a35ae1ffc7f8d87fa4305a1ed23cf22"),
    "qtsvg-everywhere-src-6.11.2.tar.xz": (
        "https://download.qt.io/archive/qt/6.11/6.11.2/submodules/qtsvg-everywhere-src-6.11.2.tar.xz",
        "d594337feca84c26fb67fe87b85e6a5c12fda404b611d905f9d138210c311876"),
    "qttranslations-everywhere-src-6.11.2.tar.xz": (
        "https://download.qt.io/archive/qt/6.11/6.11.2/submodules/qttranslations-everywhere-src-6.11.2.tar.xz",
        "021684c1a7937a9fabc3b056a6698ad5978794caf9ac190fd6cc11399e67c014"),
    "openssl-3.6.5.tar.gz": (
        "https://github.com/openssl/openssl/releases/download/openssl-3.6.5/openssl-3.6.5.tar.gz",
        "a2157c2830efdec3788939b00c9b0638306d3f0bbb76dc4832ee503bb397df98"),
}


def run(*args, **kwargs):
    subprocess.run(args, check=True, **kwargs)


def download(folder, name, url, digest):
    path = folder / name
    if not path.exists():
        partial = path.with_suffix(path.suffix + ".partial")
        run("curl", "--fail", "--location", "--retry", "3", "--connect-timeout", "20", "-o", str(partial), url)
        partial.rename(path)
    if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
        raise SystemExit("SHA256 mismatch: " + name)
    return path


def collect_notices(archive, destination):
    with tarfile.open(archive) as source:
        for member in source:
            rel = Path(member.name).relative_to(member.name.split("/")[0])
            if member.isfile() and ("LICENSES" in rel.parts or re.match(r"(license|copying|copyright|notice)(\b|[._-])", rel.name, re.I) or rel.name == "qt_attribution.json"):
                target = destination / rel
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(source.extractfile(member).read())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", nargs="?", type=Path, default=Path("build/release-deps"))
    args = parser.parse_args()
    if os.uname().sysname != "Darwin" or os.uname().machine != "arm64":
        raise SystemExit("This recipe requires an Apple Silicon macOS build host")
    for command in ("curl", "tar", "make", "perl", "clang", "cmake", "ninja"):
        if not shutil.which(command):
            raise SystemExit("Missing build dependency: " + command)
    root = args.directory.resolve()
    downloads = root / "downloads"
    downloads.mkdir(parents=True, exist_ok=True)
    sdk = root / "qt-6.11.2-macos"
    sdk.mkdir(exist_ok=True)
    for module, digest in QT_ARCHIVES.items():
        name = "6.11.2-0-202608131016" + module + QT_SUFFIX
        archive = download(downloads, name, QT_BASE + name, digest)
        stamp = sdk / (".deskbridge-" + module)
        if not stamp.exists():
            run("/usr/bin/tar", "-xf", str(archive), "-C", str(sdk))
            stamp.touch()
    for name, (url, digest) in SOURCES.items():
        download(downloads, name, url, digest)
    openssl_source = root / "openssl-3.6.5"
    prefix = root / "openssl-3.6.5-install-root/usr/local"
    if not (prefix / ".deskbridge-release-14.4").exists():
        if not openssl_source.exists():
            run("/usr/bin/tar", "-xf", str(downloads / "openssl-3.6.5.tar.gz"), "-C", str(root))
        if (openssl_source / "Makefile").exists():
            run("make", "clean", cwd=openssl_source)
        # Relative input paths and a generic configure prefix avoid private
        # source/install paths in OpenSSL's compiled build-information strings.
        run("./Configure", "darwin64-arm64-cc", "no-shared", "no-tests", "--prefix=/usr/local", "--openssldir=/etc/ssl", "-mmacosx-version-min=14.4", cwd=openssl_source)
        env = dict(os.environ, SOURCE_DATE_EPOCH="1792454400")
        run("make", "-j" + os.environ.get("DESKBRIDGE_BUILD_JOBS", "8"), cwd=openssl_source, env=env)
        run("make", "install_sw", "DESTDIR=" + str(root / "openssl-3.6.5-install-root"), cwd=openssl_source)
        (prefix / ".deskbridge-release-14.4").touch()
    notices = root / "third-party-licenses-openssl-3.6.5"
    notices.mkdir(exist_ok=True)
    for module in ("qtbase", "qtsvg", "qttranslations"):
        collect_notices(downloads / (module + "-everywhere-src-6.11.2.tar.xz"), notices / "Qt-6.11.2" / module)
    openssl_notices = notices / "OpenSSL-3.6.5"
    openssl_notices.mkdir(exist_ok=True)
    for filename in ("LICENSE.txt", "AUTHORS.md"):
        shutil.copy2(openssl_source / filename, openssl_notices / filename)
    (notices / "THIRD-PARTY-NOTICES.md").write_text("""# Redistributed components

DeskBridge derives from Deskflow. Project license texts are in the adjacent
LICENSES directory. Source: https://github.com/shuaige619/DeskBridge
Upstream: https://github.com/deskflow/deskflow

Qt 6.11.2 (Qt Core, GUI, Widgets, Network, DBus, SVG and deployment plugins)
is dynamically linked from the unmodified official macOS SDK. Exact module
license texts, third-party notices and qt_attribution.json records are included
under Qt-6.11.2. The license options and copyright notices in those files apply.
Corresponding source archives:
https://download.qt.io/archive/qt/6.11/6.11.2/submodules/qtbase-everywhere-src-6.11.2.tar.xz
https://download.qt.io/archive/qt/6.11/6.11.2/submodules/qtsvg-everywhere-src-6.11.2.tar.xz
https://download.qt.io/archive/qt/6.11/6.11.2/submodules/qttranslations-everywhere-src-6.11.2.tar.xz
Qt translation files are included. Qt tools and QML are build dependencies,
not bundled app libraries.
Qt can be rebuilt and the dynamic frameworks replaced, followed by local signing.

OpenSSL 3.6.5 is statically linked into the core under Apache License 2.0.
Its exact license and author list are included under OpenSSL-3.6.5.
Source: https://github.com/openssl/openssl/releases/tag/openssl-3.6.5
No OpenSSL source changes were made. It was built for arm64, macOS 14.4,
using no-shared, no-tests, generic /usr/local prefix and /etc/ssl openssldir.
""", encoding="utf-8")
    print("QT_PREFIX=" + str(sdk))
    print("OPENSSL_PREFIX=" + str(prefix))
    print("THIRD_PARTY_LICENSE_DIR=" + str(notices))


if __name__ == "__main__":
    main()
