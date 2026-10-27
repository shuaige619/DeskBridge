#!/usr/bin/env python3
"""Assemble the isolated Ubuntu 22.04 amd64 DeskBridge binary package."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile

VERSION = "1.27.0.3"
DEB_VERSION = VERSION + "-1~ubuntu22.04"
NAME = "deskbridge_" + DEB_VERSION + "_amd64"
QT_SOURCE_SHA256 = {
    "qtbase": "56001b905601bb9023d399f3ba780d7fa940f3e4861e496a7c490331f49e0b80",
    "qtsvg": "35eb516460f00f264eb504baa253432384351cf23fb9980a5857190e8deef438",
    "qtwayland": "20fe385887d21190165a3180c17dcfc8b9a0e1da4ec76865b6334bdc709994b0",
    "qttranslations": "c3c61d79c3d8fe316a20b3617c64673ce5b5519b2e45535f49bee313152fa531",
}
SDK = Path("/work/Qt/6.8.3/gcc_64")
DEPS = Path("/work/deps")
SOURCE = Path("/work/source")
STAGE = Path("/work/stage")
WORK = Path("/work/package-deskbridge")
ROOT = WORK / "root"
OUTPUT = Path("/job/output")
CACHE = Path("/work/package-downloads")
APP = ROOT / "opt/deskbridge"
BIN = APP / "libexec/deskbridge"
LIB = APP / "lib"
DOC = ROOT / "usr/share/doc/deskbridge"
ENV = dict(os.environ)
for _key in ("LD_LIBRARY_PATH", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH"):
    ENV.pop(_key, None)
BUILD_ENV = dict(ENV, LD_LIBRARY_PATH=f"{SDK}/lib:{DEPS}/lib")
origins = {}


def run(*args, env=ENV, cwd=None):
    result = subprocess.run([str(a) for a in args], env=env, cwd=cwd,
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise RuntimeError(f"Command failed: {args}\n{result.stdout}{result.stderr}")
    return result.stdout.strip()


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest() if hasattr(hashlib, "file_digest") else hashlib.sha256(stream.read()).hexdigest()


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def copy(source, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)


def elf(path):
    if path.is_file() and not path.is_symlink():
        with path.open("rb") as stream:
            return stream.read(4) == b"\x7fELF"
    return False


def elfs():
    return sorted(p for p in APP.rglob("*") if elf(p))


def dependencies(path, env):
    output = run("ldd", path, env=env)
    if re.search(r"=>\s+not found", output):
        raise RuntimeError(f"Unresolved dependency: {path}\n{output}")
    result = []
    for line in output.splitlines():
        match = re.match(r"\s*(\S+)\s+=>\s+(/\S+)", line)
        if match:
            result.append((match[1], Path(match[2])))
    return result


def add_library(source, needed):
    real = source.resolve(strict=True)
    target = LIB / real.name
    if target.exists():
        if sha(target) != sha(real):
            raise RuntimeError(f"Conflicting private library: {target}")
    else:
        copy(real, target)
        origins[str(target.relative_to(ROOT))] = {"source": str(real), "source_sha256": sha(real)}
    for alias in (needed, run("patchelf", "--print-soname", real)):
        if not alias or alias == real.name:
            continue
        if "/" in alias:
            raise RuntimeError(f"Nonportable SONAME: {alias}")
        link = LIB / alias
        if link.is_symlink():
            if link.readlink() != Path(real.name):
                raise RuntimeError(f"Conflicting library alias: {link}")
        elif not link.exists():
            link.symlink_to(real.name)
        elif sha(link) != sha(real):
            raise RuntimeError(f"Conflicting library alias: {link}")
    return target


def download(name, url, expected_sha256=None):
    CACHE.mkdir(parents=True, exist_ok=True)
    target = CACHE / name
    if not target.exists():
        temporary = target.with_suffix(target.suffix + ".partial")
        run("curl", "--fail", "--location", "--retry", "3", "--connect-timeout", "20",
            "--output", temporary, url)
        temporary.replace(target)
    if expected_sha256 and sha(target) != expected_sha256:
        raise RuntimeError(f"SHA256 mismatch for {target}: expected {expected_sha256}, got {sha(target)}")
    return target


def source_notices(archive, destination):
    with tarfile.open(archive) as bundle:
        members, selected, attributions = {}, set(), []
        for member in bundle:
            archive_path = Path(member.name)
            if archive_path.is_absolute() or ".." in archive_path.parts:
                raise RuntimeError(f"Unsafe source archive path: {member.name}")
            parts = archive_path.parts[1:]
            if not member.isfile() or not parts:
                continue
            relative = Path(*parts)
            if relative in members:
                raise RuntimeError(f"Duplicate source archive file: {member.name}")
            members[relative] = member
            if ("LICENSES" in parts or re.match(r"(license|copying|copyright|notice)(\b|[._-])", relative.name, re.I)
                    or relative.name == "qt_attribution.json"):
                selected.add(relative)
            if relative.name == "qt_attribution.json":
                attributions.append(relative)
        for attribution in attributions:
            # Qt's QJsonDocument accepts literal newlines in strings; official
            # attribution files use them in Copyright and Description fields.
            data = json.loads(bundle.extractfile(members[attribution]).read(), strict=False)
            entries = data if isinstance(data, list) else [data]
            if not entries or not all(isinstance(entry, dict) for entry in entries):
                raise RuntimeError(f"Invalid Qt attribution records: {archive}:{attribution}")
            for entry in entries:
                references = []
                for key in ("LicenseFile", "CopyrightFile"):
                    if key in entry:
                        references.append(entry[key])
                if "LicenseFiles" in entry:
                    if not isinstance(entry["LicenseFiles"], list) or not entry["LicenseFiles"]:
                        raise RuntimeError(f"Invalid LicenseFiles: {archive}:{attribution}")
                    references.extend(entry["LicenseFiles"])
                for reference in references:
                    if not isinstance(reference, str) or not reference.strip():
                        raise RuntimeError(f"Empty/invalid attribution file reference: {archive}:{attribution}")
                    # Qt resolves these paths relative to the attribution file,
                    # independently of its optional third-party source Path key.
                    if Path(reference).is_absolute() or "\\" in reference:
                        raise RuntimeError(f"Unsafe attribution file reference: {attribution}: {reference}")
                    relative = Path(os.path.normpath(attribution.parent / reference))
                    if relative.is_absolute() or ".." in relative.parts:
                        raise RuntimeError(f"Attribution file escapes module: {attribution}: {reference}")
                    if relative not in members or members[relative].size == 0:
                        raise RuntimeError(f"Missing/empty attribution file: {archive}:{relative}")
                    selected.add(relative)
        if not selected:
            raise RuntimeError(f"No license notices found in {archive}")
        for relative in sorted(selected):
            target = destination / relative
            if not target.resolve().is_relative_to(destination.resolve()):
                raise RuntimeError(f"License destination escapes its root: {target}")
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(bundle.extractfile(members[relative]).read())


def prepare_notices():
    copy(SOURCE / "LICENSE", DOC / "LICENSE")
    shutil.copytree(SOURCE / "LICENSES", DOC / "LICENSES")
    notices = DOC / "ThirdPartyLicenses"
    records = []
    for module in ("qtbase", "qtsvg", "qtwayland", "qttranslations"):
        name = f"{module}-everywhere-src-6.8.3.tar.xz"
        url = f"https://download.qt.io/archive/qt/6.8/6.8.3/submodules/{name}"
        archive = download(name, url, QT_SOURCE_SHA256[module])
        source_notices(archive, notices / "Qt-6.8.3" / module)
        records.append({"component": module, "version": "6.8.3", "source_url": url,
                        "source_sha256": sha(archive)})
    pinned = {"libei": ("1.6.0", "8a46bf3d4b6af7f25a61be53386cf618218114ef",
                         "https://gitlab.freedesktop.org/libinput/libei/-/archive/{commit}/libei-{commit}.tar.gz"),
              "libportal": ("0.9.1", "8f5dc8d192f6e31dafe69e35219e3b707bde71ce",
                            "https://github.com/flatpak/libportal/archive/{commit}.tar.gz")}
    for component, (version, expected, template) in pinned.items():
        source = Path("/work/sources") / component
        commit = run("git", "-C", source, "rev-parse", "HEAD")
        if commit != expected:
            raise RuntimeError(f"Unexpected {component} source: {commit}")
        copy(source / "COPYING", notices / f"{component}-{version}" / "COPYING")
        records.append({"component": component, "version": version, "source_commit": commit,
                        "source_url": template.format(commit=commit)})
    copy(Path("/usr/share/common-licenses/GPL-3"), notices / "libportal-0.9.1/COPYING.GPL3")
    # ICU is a separate SDK archive. Ask its C ABI for the actual patch version.
    icu = next((p for p in LIB.glob("libicuuc.so.*") if not p.is_symlink()), None)
    if icu:
        soname = run("patchelf", "--print-soname", icu)
        major = soname.rsplit(".", 1)[1]
        code = ("import ctypes,json; a=(ctypes.c_uint8*4)(); "
                f"getattr(ctypes.CDLL({str(icu)!r}), 'u_getVersion_{major}')(a); "
                "print(json.dumps(list(a)))")
        parts = json.loads(run("python3", "-c", code, env=BUILD_ENV))
        while len(parts) > 2 and parts[-1] == 0:
            parts.pop()
        version = ".".join(map(str, parts))
        tag = "release-" + "-".join(map(str, parts))
        url = f"https://github.com/unicode-org/icu/releases/download/{tag}/icu4c-{'_'.join(map(str, parts))}-src.tgz"
        license_url = f"https://raw.githubusercontent.com/unicode-org/icu/{tag}/icu4c/LICENSE"
        license_file = download(f"ICU-{version}-LICENSE", license_url)
        copy(license_file, notices / f"ICU-{version}" / "LICENSE")
        records.append({"component": "ICU", "version": version, "source_url": url,
                        "license_url": license_url, "license_sha256": sha(license_file)})
    commit = os.environ.get("DESKBRIDGE_SOURCE_COMMIT")
    if not commit and (SOURCE / ".git").exists():
        commit = run("git", "-C", SOURCE, "rev-parse", "HEAD")
    ref = commit or "v" + VERSION
    records.insert(0, {"component": "DeskBridge", "version": VERSION, "source_commit": commit,
                       "source_ref": ref, "source_url": f"https://github.com/shuaige619/DeskBridge/archive/{ref}.tar.gz"})
    text = (f"DeskBridge {VERSION} derives from Deskflow (https://github.com/deskflow/deskflow).\n"
            "Project: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception; exact texts in LICENSES.\n"
            "Qt 6.8.3 Core, GUI, Widgets, Network, DBus, SVG, Wayland and deployment plugins\n"
            "come from the unmodified official gcc_64 SDK. Qt module licenses, third-party\n"
            "notices and attribution records are under ThirdPartyLicenses/Qt-6.8.3.\n"
            "libei is MIT; libportal is LGPL-3.0. Their exact license texts are included.\n"
            "ICU, when bundled, has its exact license in ThirdPartyLicenses/ICU-*.\n"
            "All private libraries are dynamically linked and may be rebuilt/replaced\n"
            "in /opt/deskbridge/lib. No dependency source changes were made. ELF RUNPATH\n"
            "and debug information were adjusted for deployment. Build/packaging scripts\n"
            "and corresponding application source are available with this release.\n"
            "System OpenSSL, GLib, graphics and desktop libraries are supplied by Ubuntu,\n"
            "are not included in this package, and retain their own Ubuntu package notices.\n\n"
            "Exact corresponding source locations:\n" +
            "\n".join(f"{r['component']} {r['version']}: {r['source_url']}" for r in records) + "\n")
    write(DOC / "NOTICE.md", text)
    write(DOC / "copyright", text + "\n" + (SOURCE / "LICENSE").read_text() + "\n" +
          (SOURCE / "LICENSES/LicenseRef-OpenSSL-Exception.txt").read_text())
    return records


def main():
    if run("dpkg", "--print-architecture") != "amd64":
        raise RuntimeError("This recipe requires Ubuntu 22.04 amd64")
    if 'VERSION_ID="22.04"' not in Path("/etc/os-release").read_text():
        raise RuntimeError("This recipe requires Ubuntu 22.04")
    if (SOURCE / "VERSION").read_text().strip() != VERSION:
        raise RuntimeError("Unexpected application version")
    if WORK.is_symlink():
        raise RuntimeError("Refusing a symlink at the package workspace")
    if WORK.exists():
        shutil.rmtree(WORK)
    shutil.copytree(STAGE, ROOT, symlinks=True)
    if set(ROOT.iterdir()) != {ROOT / "opt"} or set((ROOT / "opt").iterdir()) != {APP}:
        raise RuntimeError("Original stage contains unexpected install roots")
    for name in ("DeskBridge", "deskflow-core"):
        if not elf(BIN / name):
            raise RuntimeError(f"Missing executable: {name}")
    for path in ROOT.rglob("*"):
        if path.is_file() and (path.suffix.lower() in (".pem", ".key", ".conf", ".ini", ".log") or
                               path.name.startswith(("trusted-servers", "trusted-clients"))):
            raise RuntimeError(f"Unexpected mutable/private file in installation: {path}")
    LIB.mkdir(parents=True, exist_ok=True)
    groups = ("platforms", "xcbglintegrations", "wayland-decoration-client",
              "wayland-graphics-integration-client", "wayland-shell-integration",
              "platformthemes", "imageformats", "iconengines", "tls", "networkinformation")
    for group in groups:
        for plugin in sorted((SDK / "plugins" / group).glob("*.so")):
            if group == "platforms" and plugin.name not in (
                    "libqxcb.so", "libqoffscreen.so", "libqminimal.so") and not plugin.name.startswith("libqwayland"):
                continue
            target = APP / "plugins" / group / plugin.name
            copy(plugin.resolve(), target)
            origins[str(target.relative_to(ROOT))] = {"source": str(plugin), "source_sha256": sha(plugin)}
    for plugin in ("platforms/libqxcb.so", "platforms/libqoffscreen.so", "platforms/libqminimal.so",
                   "imageformats/libqsvg.so", "iconengines/libqsvgicon.so", "tls/libqopensslbackend.so"):
        if not (APP / "plugins" / plugin).is_file():
            raise RuntimeError(f"Missing required Qt plugin: {plugin}")
    if not list((APP / "plugins/platforms").glob("libqwayland*.so")):
        raise RuntimeError("Missing Qt Wayland client platform plugin")
    pending, seen = elfs(), set()
    while pending:
        path = pending.pop()
        if path in seen:
            continue
        seen.add(path)
        for needed, resolved in dependencies(path, BUILD_ENV):
            real = resolved.resolve(strict=True)
            if real.is_relative_to(SDK) or real.is_relative_to(DEPS):
                pending.append(add_library(real, needed))
            elif real.is_relative_to(LIB):
                if str(real.relative_to(ROOT)) not in origins:
                    raise RuntimeError(f"Untracked private dependency: {resolved}")
                pending.append(real)
            elif not (real.is_relative_to(Path("/usr/lib")) or real.is_relative_to(Path("/lib"))):
                raise RuntimeError(f"Unexpected dependency origin: {resolved}")
    for path in elfs():
        run("strip", "--strip-unneeded", path)
        relative = os.path.relpath(LIB, path.parent)
        runpath = "$ORIGIN" + ("/" + relative if relative != "." else "")
        run("patchelf", "--set-rpath", runpath, path)
        if any("/" in name for name in run("patchelf", "--print-needed", path).splitlines()):
            raise RuntimeError(f"Absolute DT_NEEDED entry: {path}")
    write(BIN / "qt.conf", "[Paths]\nPrefix=../..\nLibraries=lib\nPlugins=plugins\nTranslations=libexec/deskbridge/translations\n")
    translations = list((SDK / "translations").glob("*.qm"))
    if not translations:
        raise RuntimeError("Missing Qt translations")
    for translation in translations:
        copy(translation, BIN / "translations" / translation.name)
    for translation in (SDK / "translations").glob("qtbase_*.qm"):
        copy(translation, BIN / "translations" / translation.name.replace("qtbase_", "qt_", 1))
    for directory in ("applications", "icons", "metainfo"):
        origin = APP / "share" / directory
        shutil.copytree(origin, ROOT / "usr/share" / directory, dirs_exist_ok=True)
        shutil.rmtree(origin)
    desktop = ROOT / "usr/share/applications/local.shuai.deskbridge.desktop"
    content, changed = re.subn(r"^Exec=.*$", "Exec=deskbridge", desktop.read_text(), flags=re.M)
    if changed != 1:
        raise RuntimeError("Unexpected desktop launcher")
    write(desktop, content)
    run("desktop-file-validate", desktop)
    launcher = ROOT / "usr/bin/deskbridge"
    write(launcher, '#!/bin/sh\nunset LD_LIBRARY_PATH QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH QML2_IMPORT_PATH QML_IMPORT_PATH\nexec /opt/deskbridge/libexec/deskbridge/DeskBridge "$@"\n')
    launcher.chmod(0o755)
    components = prepare_notices()
    system, records = {}, []
    private_names = {path.name for path in LIB.iterdir()}
    for path in elfs():
        resolved = dependencies(path, ENV)
        for needed, dependency in resolved:
            real = dependency.resolve(strict=True)
            if not real.is_relative_to(ROOT):
                if needed in private_names or needed.startswith(("libQt6", "libei.", "libportal.")):
                    raise RuntimeError(f"Private dependency escaped package: {needed}: {real}")
                system[needed] = str(real)
        records.append({"path": "/" + str(path.relative_to(ROOT)), "sha256": sha(path),
                        "runpath": run("patchelf", "--print-rpath", path),
                        "needed": run("patchelf", "--print-needed", path).splitlines(),
                        "resolved": {name: str(p) for name, p in resolved},
                        **origins.get(str(path.relative_to(ROOT)), {})})
    # A minimal source-control file lets dpkg-shlibdeps classify the package.
    write(WORK / "debian/control", "Source: deskbridge\nSection: utils\nPriority: optional\nMaintainer: DeskBridge contributors <noreply@github.com>\n\nPackage: deskbridge\nArchitecture: amd64\nDescription: Keyboard and mouse sharing utility\n")
    command = ["dpkg-shlibdeps", "--ignore-missing-info", "-O", "-l" + str(LIB)]
    command += ["-e" + str(path) for path in elfs()]
    process = subprocess.run(command, env=ENV, cwd=WORK, text=True, capture_output=True)
    if process.returncode or re.search(r"cannot find library|couldn't find library|=>\s*not found", process.stderr, re.I):
        raise RuntimeError(f"dpkg-shlibdeps failed:\n{process.stdout}{process.stderr}")
    depends = next((line.split("=", 1)[1] for line in process.stdout.splitlines()
                    if line.startswith("shlibs:Depends=")), "")
    if not depends or re.search(r"(?:^|,\s*)(?:qt6-|libqt6|libei\d|libportal\d)", depends):
        raise RuntimeError(f"Unexpected package dependencies: {depends}")
    if re.search(r"(?:^|[ ,])\S+-dev(?:[ ,(]|$)", depends):
        raise RuntimeError(f"Development package in runtime dependencies: {depends}")
    size = sum(p.stat().st_size for p in ROOT.rglob("*") if p.is_file() and not p.is_symlink())
    write(ROOT / "DEBIAN/control", f"Package: deskbridge\nVersion: {DEB_VERSION}\nSection: utils\nPriority: optional\nArchitecture: amd64\nMaintainer: DeskBridge contributors <noreply@github.com>\nInstalled-Size: {(size + 1023) // 1024}\nDepends: {depends}\nRecommends: fonts-noto-cjk\nHomepage: https://github.com/shuaige619/DeskBridge\nDescription: Share keyboard and mouse across computers\n DeskBridge personal edition, with an isolated Qt runtime for Ubuntu 22.04.\n")
    md5sums = []
    for path in sorted(ROOT.rglob("*")):
        if path.is_symlink():
            if not path.resolve().is_relative_to(ROOT):
                raise RuntimeError(f"Package symlink escapes its root: {path}")
        elif path.is_file() and not path.is_relative_to(ROOT / "DEBIAN"):
            md5sums.append(f"{hashlib.md5(path.read_bytes()).hexdigest()}  {path.relative_to(ROOT)}")
    write(ROOT / "DEBIAN/md5sums", "\n".join(md5sums) + "\n")
    # Do not package build-host timestamps or sensitive mutable configuration.
    epoch = int(os.environ.get("SOURCE_DATE_EPOCH", "1792454400"))
    for path in ROOT.rglob("*"):
        if not path.is_symlink():
            os.utime(path, (epoch, epoch))
    OUTPUT.mkdir(parents=True, exist_ok=True)
    package = OUTPUT / (NAME + ".deb")
    run("dpkg-deb", "--root-owner-group", "--build", ROOT, package)
    provenance = {"package": package.name, "package_sha256": sha(package), "version": DEB_VERSION,
                  "architecture": "amd64", "target": "Ubuntu 22.04", "components": components,
                  "system_dependencies": system, "depends": depends, "source_date_epoch": epoch,
                  "qt_sdk": str(SDK), "private_prefix": "/opt/deskbridge", "elf_count": len(records)}
    write(OUTPUT / (NAME + ".elf.json"), json.dumps(records, indent=2, sort_keys=True) + "\n")
    write(OUTPUT / (NAME + ".provenance.json"), json.dumps(provenance, indent=2, sort_keys=True) + "\n")
    write(OUTPUT / (NAME + ".dependencies.txt"), depends + "\n\n" +
          "\n".join(f"{key}\t{value}" for key, value in sorted(system.items())) + "\n")
    write(OUTPUT / (NAME + ".shlibdeps.log"), process.stdout + process.stderr)
    print(json.dumps(provenance, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
