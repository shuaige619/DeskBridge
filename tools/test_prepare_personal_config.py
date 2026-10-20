# SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
# SPDX-License-Identifier: MIT
import configparser
import errno
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import prepare_personal_config as migration
from prepare_personal_config import prepare


class PersonalProfileTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name) / "Deskflow"
        self.dest = Path(self.temp.name) / "DeskBridge"
        (self.source / "tls").mkdir(parents=True)
        (self.source / "tls/deskflow.pem").write_text("SYNTHETIC TEST IDENTITY")
        (self.source / "tls/trusted-servers").write_text("SYNTHETIC TEST TRUST")
        self.original = "[core]\ncoreMode=1\ncomputerName=test-mac\n[client]\nremoteHost=192.0.2.10\n[gui]\nstartCoreWithGui=true\n[security]\ntlsEnabled=true\ncheckPeerFingerprints=true\n"
        (self.source / "Deskflow.conf").write_text(self.original)

    def test_dry_run_has_no_filesystem_effect(self):
        result = prepare(self.source, self.dest)
        self.assertFalse(result["applied"])
        self.assertFalse(self.dest.exists())

    def test_isolated_profile_preserves_pairing_and_disables_auto_connect(self):
        prepare(self.source, self.dest, True)
        config = configparser.RawConfigParser()
        config.read(self.dest / "DeskBridge.conf")
        self.assertEqual(config.get("client", "remoteHost"), "192.0.2.10")
        self.assertEqual(config.get("core", "computerName"), "test-mac")
        self.assertEqual(config.get("gui", "startCoreWithGui"), "false")
        self.assertEqual(config.get("security", "certificate"), str(self.dest / "tls/deskflow.pem"))
        self.assertEqual((self.dest / "tls/deskflow.pem").read_text(), "SYNTHETIC TEST IDENTITY")
        self.assertEqual((self.source / "Deskflow.conf").read_text(), self.original)
        self.assertEqual((self.dest / "tls/deskflow.pem").stat().st_mode & 0o777, 0o600)
        self.assertEqual(self.dest.stat().st_mode & 0o777, 0o700)

    def test_existing_profile_is_never_overwritten(self):
        self.dest.mkdir()
        (self.dest / "keep").write_text("keep")
        with self.assertRaises(ValueError):
            prepare(self.source, self.dest, True)
        self.assertEqual((self.dest / "keep").read_text(), "keep")

    def test_empty_destination_created_at_rename_is_not_replaced(self):
        rename = migration._rename_exclusive
        created_inode = []

        def competing_directory(stage, destination):
            destination.mkdir()
            created_inode.append(destination.stat().st_ino)
            rename(stage, destination)

        with patch.object(migration, "_rename_exclusive", side_effect=competing_directory):
            with self.assertRaisesRegex(ValueError, "Destination appeared"):
                prepare(self.source, self.dest, True)
        self.assertTrue(self.dest.is_dir())
        self.assertEqual(self.dest.stat().st_ino, created_inode[0])
        self.assertEqual(list(self.dest.iterdir()), [])
        self.assertEqual(list(self.dest.parent.glob(".deskbridge-prepare-*")), [])
        self.assertEqual((self.source / "Deskflow.conf").read_text(), self.original)

    def test_symlink_created_at_rename_and_its_target_are_not_changed(self):
        rename = migration._rename_exclusive
        target = self.dest.parent / "existing-profile"
        target.mkdir()
        sentinel = target / "keep"
        sentinel.write_text("keep existing profile")

        def competing_symlink(stage, destination):
            destination.symlink_to(target, target_is_directory=True)
            rename(stage, destination)

        with patch.object(migration, "_rename_exclusive", side_effect=competing_symlink):
            with self.assertRaisesRegex(ValueError, "Destination appeared"):
                prepare(self.source, self.dest, True)
        self.assertTrue(self.dest.is_symlink())
        self.assertEqual(self.dest.readlink(), target)
        self.assertEqual(sentinel.read_text(), "keep existing profile")
        self.assertEqual(list(target.iterdir()), [sentinel])
        self.assertEqual(list(self.dest.parent.glob(".deskbridge-prepare-*")), [])

    def test_missing_exclusive_rename_fails_closed_and_cleans_stage(self):
        with patch.object(migration.ctypes, "CDLL", return_value=object()):
            with self.assertRaises(OSError) as raised:
                prepare(self.source, self.dest, True)
        self.assertEqual(raised.exception.errno, errno.ENOTSUP)
        self.assertFalse(self.dest.exists())
        self.assertEqual(list(self.dest.parent.glob(".deskbridge-prepare-*")), [])

    def test_other_platform_fails_closed_and_cleans_stage(self):
        with patch.object(migration.sys, "platform", "linux"):
            with self.assertRaises(OSError) as raised:
                prepare(self.source, self.dest, True)
        self.assertEqual(raised.exception.errno, errno.ENOTSUP)
        self.assertFalse(self.dest.exists())
        self.assertEqual(list(self.dest.parent.glob(".deskbridge-prepare-*")), [])

    def test_missing_identity_does_not_create_profile(self):
        (self.source / "tls/deskflow.pem").unlink()
        with self.assertRaises(ValueError):
            prepare(self.source, self.dest, True)
        self.assertFalse(self.dest.exists())

    def test_disabled_peer_validation_is_not_migrated(self):
        (self.source / "Deskflow.conf").write_text(self.original.replace("checkPeerFingerprints=true", "checkPeerFingerprints=false"))
        with self.assertRaises(ValueError):
            prepare(self.source, self.dest, True)
        self.assertFalse(self.dest.exists())


if __name__ == "__main__":
    unittest.main()
