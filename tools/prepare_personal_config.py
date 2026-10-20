#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
# SPDX-License-Identifier: MIT
"""Prepare an isolated personal client profile; dry-run unless --apply is given."""
import argparse
import configparser
import ctypes
import errno
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile


def _rename_exclusive(source: Path, destination: Path) -> None:
    """Publish a Mac profile atomically, refusing every existing destination."""
    if sys.platform != "darwin":
        raise OSError(errno.ENOTSUP, "Atomic profile installation requires macOS renamex_np.")
    try:
        rename = ctypes.CDLL(None, use_errno=True).renamex_np
    except (AttributeError, OSError) as error:
        # Never fall back to rename(), which can replace an existing empty dir.
        raise OSError(errno.ENOTSUP, "macOS exclusive rename is unavailable; nothing installed.") from error
    rename.argtypes = (ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint)
    rename.restype = ctypes.c_int
    rename_excl = 0x00000004  # RENAME_EXCL in the macOS SDK's sys/stdio.h.
    ctypes.set_errno(0)
    if rename(os.fsencode(source), os.fsencode(destination), rename_excl) != 0:
        error = ctypes.get_errno() or errno.EIO
        raise OSError(error, os.strerror(error), str(destination))


def prepare(source: Path, destination: Path, apply: bool = False) -> dict:
    source, destination = source.expanduser().resolve(), destination.expanduser().absolute()
    if destination.exists() or destination.is_symlink():
        raise ValueError(f"Destination already exists; nothing overwritten: {destination}")
    config = configparser.RawConfigParser(strict=False, delimiters=("=",))
    config.optionxform = str
    with (source / "Deskflow.conf").open(encoding="utf-8") as handle:
        config.read_file(handle)
    if config.get("core", "coreMode", fallback="") != "1":
        raise ValueError("This migration supports the existing Mac client profile only.")
    if not config.get("client", "remoteHost", fallback="").strip():
        raise ValueError("The existing profile has no server address.")
    for key in ("tlsEnabled", "checkPeerFingerprints"):
        if config.get("security", key, fallback="true").lower() != "true":
            raise ValueError("The existing profile must have TLS and peer validation enabled.")
    certificate = Path(config.get("security", "certificate", fallback=str(source / "tls/deskflow.pem")).strip('"'))
    if not certificate.is_absolute():
        certificate = source / certificate
    trusted = source / "tls/trusted-servers"
    if not certificate.is_file() or not trusted.is_file():
        raise ValueError("Existing client identity or trusted-server file is missing.")
    for section in ("client", "gui", "security", "log"):
        if not config.has_section(section):
            config.add_section(section)
    config.set("gui", "startCoreWithGui", "false")
    config.set("gui", "autoHide", "false")
    config.set("gui", "enableUpdateCheck", "false")
    config.set("client", "clipboardShortcuts", "true")
    config.set("security", "tlsEnabled", "true")
    config.set("security", "checkPeerFingerprints", "true")
    config.set("security", "certificate", str(destination / "tls/deskflow.pem"))
    config.set("log", "file", str(destination / "deskbridge.log"))
    plan = {"source": str(source), "destination": str(destination),
            "server": config.get("client", "remoteHost"),
            "screen_name": config.get("core", "computerName", fallback=""),
            "tls": True, "peer_validation": True, "auto_connect": False,
            "copy_existing_local_identity": True, "applied": apply}
    if not apply:
        return plan
    destination.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=".deskbridge-prepare-", dir=destination.parent))
    try:
        os.chmod(stage, 0o700)
        (stage / "tls").mkdir(mode=0o700)
        for src, name in ((certificate, "deskflow.pem"), (trusted, "trusted-servers")):
            target = stage / "tls" / name
            shutil.copyfile(src, target)
            os.chmod(target, 0o600)
        profile = stage / "DeskBridge.conf"
        with profile.open("w", encoding="utf-8") as handle:
            config.write(handle, space_around_delimiters=False)
        os.chmod(profile, 0o600)
        # The syscall also refuses a destination created after this check.
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


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path.home() / "Library/Deskflow")
    parser.add_argument("--destination", type=Path, default=Path.home() / "Library/DeskBridge")
    parser.add_argument("--apply", action="store_true", help="Copy this Mac's existing pairing identity into a new private profile.")
    args = parser.parse_args()
    try:
        print(json.dumps(prepare(args.source, args.destination, args.apply), ensure_ascii=False, indent=2))
    except (OSError, ValueError, configparser.Error) as error:
        parser.exit(1, f"{error}\n")
