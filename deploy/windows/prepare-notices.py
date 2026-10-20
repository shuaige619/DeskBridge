#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
# SPDX-License-Identifier: MIT
"""Collect exact third-party license texts for the Windows desktop payload."""
import argparse
import hashlib
from pathlib import Path, PurePosixPath
import re
import shutil
import tarfile
import time
import urllib.request
import xml.etree.ElementTree as ET
import zipfile

QT_VERSION = "6.11.2"
QT_SOURCES = {
    "qtbase": "5b2e00eccaf5a4d8c14134ffa0ea8dfd0a35ae1ffc7f8d87fa4305a1ed23cf22",
    "qtsvg": "d594337feca84c26fb67fe87b85e6a5c12fda404b611d905f9d138210c311876",
    "qttranslations": "021684c1a7937a9fabc3b056a6698ad5978794caf9ac190fd6cc11399e67c014",
}
CRT_LICENSE_URL = "https://visualstudio.microsoft.com/wp-content/uploads/2021/09/Visual-C-Runtime-2015-2022-License-1.docx"
CRT_LICENSE_SHA256 = "f1e3d56ceb2ad68aae0711b910375009e651ac5530fa0760f0dea6e81e54fae1"


def download(directory, name, url, digest):
    path = directory / name
    if not path.exists():
        partial = path.with_suffix(path.suffix + ".partial")
        for attempt in range(3):
            try:
                with urllib.request.urlopen(url, timeout=60) as response, partial.open("wb") as output:
                    shutil.copyfileobj(response, output)
                partial.replace(path)
                break
            except OSError:
                if attempt == 2:
                    raise
                time.sleep(2)
    if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
        raise SystemExit("SHA256 mismatch: " + name)
    return path


def collect_notices(archive, destination):
    count = 0
    with tarfile.open(archive) as source:
        for member in source:
            parts = PurePosixPath(member.name).parts
            if not member.isfile() or len(parts) < 2 or any(part in ("..", "") for part in parts):
                continue
            relative = Path(*parts[1:])
            if ("LICENSES" in relative.parts or
                    re.match(r"(license|copying|copyright|notice)(\b|[._-])", relative.name, re.I) or
                    relative.name == "qt_attribution.json"):
                target = destination / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(source.extractfile(member).read())
                count += 1
    if count == 0:
        raise SystemExit("No source licenses found in " + str(archive))


def openssl_version(prefix):
    status = (prefix.parent / "vcpkg/status").read_text(encoding="utf-8")
    for record in re.split(r"\r?\n\r?\n", status):
        fields = dict(re.findall(r"^([^:]+): (.*)$", record, re.M))
        if fields.get("Package") == "openssl" and fields.get("Architecture") == prefix.name:
            version = fields.get("Version", "")
            if re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
                return version
    raise SystemExit("Cannot identify the installed OpenSSL package version")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", required=True, type=Path)
    parser.add_argument("--openssl-prefix", required=True, type=Path)
    args = parser.parse_args()
    root = args.directory.resolve()
    downloads = root.parent / "notice-downloads"
    downloads.mkdir(parents=True, exist_ok=True)
    root.mkdir(parents=True, exist_ok=True)
    source_urls = []
    for module, digest in QT_SOURCES.items():
        name = module + "-everywhere-src-" + QT_VERSION + ".tar.xz"
        url = "https://download.qt.io/archive/qt/6.11/" + QT_VERSION + "/submodules/" + name
        archive = download(downloads, name, url, digest)
        collect_notices(archive, root / ("Qt-" + QT_VERSION) / module)
        source_urls.append(url)
    version = openssl_version(args.openssl_prefix)
    openssl_notices = root / ("OpenSSL-" + version)
    openssl_notices.mkdir(exist_ok=True)
    shutil.copy2(args.openssl_prefix / "share/openssl/copyright", openssl_notices / "LICENSE.txt")
    crt = root / "Microsoft-Visual-C-Runtime-2015-2022"
    crt.mkdir(exist_ok=True)
    license_docx = download(downloads, "Visual-C-Runtime-2015-2022-License.docx", CRT_LICENSE_URL, CRT_LICENSE_SHA256)
    shutil.copy2(license_docx, crt / "LICENSE.docx")
    with zipfile.ZipFile(license_docx) as document:
        tree = ET.fromstring(document.read("word/document.xml"))
        ns = {"w": "http://schemas.openxmlformats.org/wordprocessingml/2006/main"}
        paragraphs = ["".join(p.itertext()) for p in tree.findall(".//w:p", ns)]
        (crt / "LICENSE.txt").write_text("\n\n".join(paragraphs) + "\n", encoding="utf-8")
    (root / "THIRD-PARTY-NOTICES.md").write_text(
        "# Redistributed components\n\n"
        "DeskBridge derives from Deskflow. Project license texts are in the adjacent LICENSES directory.\n"
        "Source: https://github.com/shuaige619/DeskBridge\n"
        "Upstream: https://github.com/deskflow/deskflow\n\n"
        "Qt " + QT_VERSION + " (Core, GUI, Widgets, Network, SVG, deployment plugins and translation data) "
        "is dynamically linked from the unmodified official MSVC2022 x64 SDK. Exact module license texts, "
        "third-party notices and qt_attribution.json records are under Qt-" + QT_VERSION + ". "
        "The license options and copyright notices in those files apply. Corresponding source archives:\n" +
        "\n".join(source_urls) + "\n"
        "The Qt DLLs can be replaced with compatible rebuilt versions. Qt tools are build dependencies.\n\n"
        "OpenSSL " + version + " is dynamically linked from the pinned vcpkg manifest. "
        "Its installed package license is under OpenSSL-" + version + ".\n"
        "Source: https://github.com/openssl/openssl/releases/tag/openssl-" + version + "\n\n"
        "Microsoft Visual C++ 2015-2022 runtime DLLs are redistributed from the installed Visual Studio 2022 "
        "toolchain. DLL file properties identify their exact build versions. The original vendor license "
        "document and its extracted text are under Microsoft-Visual-C-Runtime-2015-2022.\n"
        "License source: https://visualstudio.microsoft.com/license-terms/vs2022-cruntime/\n" + CRT_LICENSE_URL + "\n",
        encoding="utf-8")
    print("Third-party licenses prepared: " + str(root))


if __name__ == "__main__":
    main()
