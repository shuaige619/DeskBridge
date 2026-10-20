#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Copy an existing Linux Deskflow server into a private DeskBridge profile.

Dry-run is the default. No service, application or login item is changed.
"""
import argparse
import configparser
import ctypes
import errno
import json
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile


def _rename_exclusive(source: Path, destination: Path) -> None:
    """Linux atomic publication, including refusal of an existing empty dir."""
    if sys.platform != "linux":
        raise OSError(errno.ENOTSUP, "Applying a server profile requires Linux renameat2.")
    try:
        rename = ctypes.CDLL(None, use_errno=True).renameat2
    except (AttributeError, OSError) as error:
        raise OSError(errno.ENOTSUP, "Linux exclusive rename is unavailable; nothing installed.") from error
    rename.argtypes = (ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint)
    rename.restype = ctypes.c_int
    ctypes.set_errno(0)
    if rename(-100, os.fsencode(source), -100, os.fsencode(destination), 1) != 0:  # AT_FDCWD, RENAME_NOREPLACE
        error = ctypes.get_errno() or errno.EIO
        raise OSError(error, os.strerror(error), str(destination))


def _file(path: Path) -> Path:
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"Expected a regular file, without a symlink: {path}")
    return path


def _path_value(value: str, base: Path) -> Path:
    value = value.strip()
    if value.startswith('"'):
        if not value.endswith('"'):
            raise ValueError("Malformed quoted path in existing settings.")
        value = value[1:-1].replace('\\"', '"').replace('\\\\', '\\')
    if not value or any(char in value for char in "\r\n\0") or value.startswith("@"):
        raise ValueError("Unsupported path in existing settings.")
    path = Path(value).expanduser()
    return path if path.is_absolute() else base / path


def _quoted_path(path: Path) -> str:
    value = str(path)
    if any(char in value for char in "\r\n\0"):
        raise ValueError("Profile paths cannot contain line breaks or NUL.")
    return '"' + value.replace('\\', '\\\\').replace('"', '\\"') + '"'


def _patch_ini(text: str, updates: dict) -> str:
    """Change selected QSettings keys while leaving every other byte intact."""
    pending = {section: dict(values) for section, values in updates.items()}
    newline = "\r\n" if "\r\n" in text else "\n"
    output = []
    section = None

    def append_missing():
        values = pending.get(section, {})
        if values and output and not output[-1].endswith(("\n", "\r")):
            output[-1] += newline
        for key, value in values.items():
            output.append(f"{key}={value}{newline}")
        values.clear()

    for line in text.splitlines(keepends=True):
        header = re.match(r"^\[([^\]]+)\]\s*$", line.rstrip("\r\n"))
        if header:
            append_missing()
            section = header[1]
        entry = re.match(r"^\s*([^=]+?)\s*=", line) if not line.lstrip().startswith(("#", ";")) else None
        values = pending.get(section, {})
        if entry and entry[1] in values:
            ending = "\r\n" if line.endswith("\r\n") else "\n" if line.endswith("\n") else ""
            output.append(f"{entry[1]}={values.pop(entry[1])}{ending}")
        else:
            output.append(line)
    append_missing()
    for name, values in pending.items():
        if not values:
            continue
        if output and not output[-1].endswith(("\n", "\r")):
            output[-1] += newline
        output.append(f"[{name}]{newline}")
        for key, value in values.items():
            output.append(f"{key}={value}{newline}")
    return "".join(output)


def _tls_files(directory: Path) -> list:
    if directory.is_symlink() or not directory.is_dir():
        raise ValueError(f"Existing TLS directory is missing or is a symlink: {directory}")
    files = []
    for path in sorted(directory.rglob("*")):
        if path.is_symlink() or not (path.is_dir() or path.is_file()):
            raise ValueError(f"Unsupported entry in TLS directory: {path}")
        if path.is_file():
            files.append(path)
    return files


def prepare(source: Path, destination: Path, apply: bool = False) -> dict:
    source = source.expanduser().resolve()
    destination = destination.expanduser().absolute()
    if destination.exists() or destination.is_symlink():
        raise ValueError(f"Destination already exists; nothing overwritten: {destination}")
    profile = _file(source / "Deskflow.conf")
    original = profile.read_bytes()
    text = original.decode("utf-8-sig")
    config = configparser.RawConfigParser(delimiters=("=",), interpolation=None)
    config.optionxform = str
    config.read_string(text)
    if config.get("core", "coreMode", fallback="") != "2":
        raise ValueError("The existing profile must be a Deskflow server (coreMode=2).")
    screen_name = config.get("core", "computerName", fallback="").strip()
    if not screen_name:
        raise ValueError("The existing server has no screen name.")
    for key in ("tlsEnabled", "checkPeerFingerprints"):
        if config.get("security", key, fallback="true").lower() != "true":
            raise ValueError("The existing server must have TLS and peer validation enabled.")
    certificate = _file(_path_value(config.get("security", "certificate", fallback="tls/deskflow.pem"), source))
    _file(source / "tls/trusted-clients")
    tls_files = _tls_files(source / "tls")
    external = config.get("server", "externalConfig", fallback="false").lower() == "true"
    server_file = _path_value(config.get("server", "externalConfigFile", fallback="deskflow-server.conf"), source) if external else source / "deskflow-server.conf"
    _file(server_file)
    updates = {
        "gui": {"startCoreWithGui": "false"},
        "security": {"certificate": _quoted_path(destination / "tls/deskflow.pem")},
        "log": {"file": _quoted_path(destination / "deskbridge.log")},
    }
    if external:
        updates["server"] = {"externalConfigFile": _quoted_path(destination / "deskflow-server.conf")}
    migrated = _patch_ini(text, updates).encode("utf-8")
    if original.startswith(b"\xef\xbb\xbf"):
        migrated = b"\xef\xbb\xbf" + migrated
    plan = {
        "source": str(source), "destination": str(destination), "role": "server",
        "screen_name": screen_name, "external_config": external,
        "tls": True, "peer_validation": True, "auto_start_core": False,
        "copy_existing_local_identity": True, "copy_tls_files": len(tls_files),
        "copy_server_config": str(server_file), "preserve_grid_and_other_settings": True,
        "modified_settings": [f"{section}/{key}" for section, values in updates.items() for key in values],
        "applied": apply,
    }
    if not apply:
        return plan
    destination.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=".deskbridge-server-prepare-", dir=destination.parent))
    try:
        os.chmod(stage, 0o700)
        (stage / "tls").mkdir(mode=0o700)
        for src in tls_files:
            target = stage / "tls" / src.relative_to(source / "tls")
            target.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
            shutil.copyfile(src, target)
            os.chmod(target, 0o600)
        shutil.copyfile(certificate, stage / "tls/deskflow.pem")
        os.chmod(stage / "tls/deskflow.pem", 0o600)
        shutil.copyfile(server_file, stage / "deskflow-server.conf")
        os.chmod(stage / "deskflow-server.conf", 0o600)
        (stage / "DeskBridge.conf").write_bytes(migrated)
        os.chmod(stage / "DeskBridge.conf", 0o600)
        if profile.read_bytes() != original:
            raise ValueError("Source settings changed during migration; retry after saving the server configuration.")
        if destination.exists() or destination.is_symlink():
            raise ValueError(f"Destination appeared; nothing overwritten: {destination}")
        try:
            _rename_exclusive(stage, destination)
        except OSError as error:
            if error.errno in (errno.EEXIST, errno.ENOTEMPTY):
                raise ValueError(f"Destination appeared; nothing overwritten: {destination}") from error
            raise
    finally:
        if stage.exists():
            shutil.rmtree(stage)
    return plan


def main() -> None:
    config_root = Path(os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config")))
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=config_root / "Deskflow")
    parser.add_argument("--destination", type=Path, default=config_root / "DeskBridge")
    parser.add_argument("--apply", action="store_true", help="Copy the server identity into a new private profile; Linux only.")
    args = parser.parse_args()
    try:
        print(json.dumps(prepare(args.source, args.destination, args.apply), ensure_ascii=False, indent=2))
    except (OSError, ValueError, configparser.Error) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
