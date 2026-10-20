# SPDX-License-Identifier: MIT
import configparser
import errno
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

import prepare_personal_server as migration


class PersonalServerProfileTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name) / "Deskflow"
        self.dest = Path(self.temp.name) / "DeskBridge"
        (self.source / "tls").mkdir(parents=True)
        (self.source / "tls/deskflow.pem").write_bytes(b"SYNTHETIC SERVER IDENTITY\n")
        (self.source / "tls/trusted-clients").write_bytes(b"SYNTHETIC CLIENT TRUST\n")
        (self.source / "tls/trusted-servers").write_bytes(b"SYNTHETIC SERVER TRUST\n")
        (self.source / "deskflow-server.conf").write_bytes(b"section: links\nsource:\n right = target\nend\n")
        self.original = (
            "; retain this comment and exact variant/grid serialization\n"
            "[core]\ncoreMode=2\ncomputerName=source\nport=24800\n"
            "[gui]\nstartCoreWithGui=true\nautoHide=true\n"
            "[security]\ntlsEnabled=true\ncheckPeerFingerprints=true\n"
            "[server]\ngridWidth=5\ngridHeight=3\nexternalConfig=false\n"
            "[internalConfig]\nscreens\\8\\name=source\nscreens\\9\\name=target\n"
            "screens\\size=15\nhotkeys\\1\\keySequence=@ByteArray(00\\x0f)\n"
            "[unfamiliar]\ncustomValue=  \"comma, backslash\\\\, equals=, semicolon; and percent%\"\n"
        ).encode()
        (self.source / "Deskflow.conf").write_bytes(self.original)

    def publish(self, source, destination):
        # Tests use a real exclusive rename on both development platforms.
        if sys.platform == "darwin":
            from prepare_personal_config import _rename_exclusive
            return _rename_exclusive(source, destination)
        return self.linux_publish(source, destination)

    linux_publish = staticmethod(migration._rename_exclusive)

    def apply(self):
        with patch.object(migration, "_rename_exclusive", side_effect=self.publish):
            return migration.prepare(self.source, self.dest, True)

    def test_dry_run_creates_nothing_and_keeps_source(self):
        destination = Path(self.temp.name) / "does-not-exist/DeskBridge"
        result = migration.prepare(self.source, destination)
        self.assertFalse(result["applied"])
        self.assertFalse(destination.parent.exists())
        self.assertEqual((self.source / "Deskflow.conf").read_bytes(), self.original)
        self.assertEqual(result["screen_name"], "source")

    def test_apply_preserves_grid_unknown_settings_tls_and_generated_config(self):
        result = self.apply()
        self.assertTrue(result["applied"])
        config = configparser.RawConfigParser()
        config.read(self.dest / "DeskBridge.conf")
        self.assertEqual(config.get("core", "coreMode"), "2")
        self.assertEqual(config.get("core", "computername"), "source")
        self.assertEqual(config.get("gui", "startcorewithgui"), "false")
        self.assertEqual(config.get("gui", "autohide"), "true")
        output = (self.dest / "DeskBridge.conf").read_bytes()
        retained = self.original.split(b"[internalConfig]", 1)[1]
        self.assertIn(b"[internalConfig]" + retained, output)
        self.assertIn(b"; retain this comment", output)
        self.assertIn(str(self.dest / "tls/deskflow.pem").encode(), output)
        for name in ("deskflow.pem", "trusted-clients", "trusted-servers"):
            self.assertEqual((self.dest / "tls" / name).read_bytes(), (self.source / "tls" / name).read_bytes())
            self.assertEqual((self.dest / "tls" / name).stat().st_mode & 0o777, 0o600)
        self.assertEqual((self.dest / "deskflow-server.conf").read_bytes(), (self.source / "deskflow-server.conf").read_bytes())
        self.assertEqual((self.dest / "DeskBridge.conf").stat().st_mode & 0o777, 0o600)
        self.assertEqual(self.dest.stat().st_mode & 0o777, 0o700)
        self.assertEqual((self.source / "Deskflow.conf").read_bytes(), self.original)

    def test_external_certificate_and_configuration_are_copied_privately(self):
        certificate = Path(self.temp.name) / 'external cert "identity".pem'
        certificate.write_bytes(b"SYNTHETIC EXTERNAL IDENTITY")
        external_config = Path(self.temp.name) / "custom server.conf"
        external_config.write_bytes(b"SYNTHETIC CUSTOM SERVER CONFIG")
        text = self.original.decode().replace("externalConfig=false", f"externalConfig=true\nexternalConfigFile={external_config}")
        text = text.replace("[security]", f"[security]\ncertificate={migration._quoted_path(certificate)}")
        (self.source / "Deskflow.conf").write_text(text)
        self.apply()
        self.assertEqual((self.dest / "tls/deskflow.pem").read_bytes(), certificate.read_bytes())
        self.assertEqual((self.dest / "deskflow-server.conf").read_bytes(), external_config.read_bytes())
        config = configparser.RawConfigParser()
        config.read(self.dest / "DeskBridge.conf")
        self.assertEqual(migration._path_value(config.get("server", "externalconfigfile"), self.source), self.dest / "deskflow-server.conf")

    def test_crlf_bom_and_final_line_are_preserved(self):
        original = b"\xef\xbb\xbf" + self.original.replace(b"\n", b"\r\n").rstrip(b"\r\n")
        (self.source / "Deskflow.conf").write_bytes(original)
        self.apply()
        output = (self.dest / "DeskBridge.conf").read_bytes()
        self.assertTrue(output.startswith(b"\xef\xbb\xbf"))
        self.assertIn(original.split(b"[internalConfig]", 1)[1], output)

    def test_client_profile_is_refused(self):
        (self.source / "Deskflow.conf").write_bytes(self.original.replace(b"coreMode=2", b"coreMode=1"))
        with self.assertRaisesRegex(ValueError, "must be a Deskflow server"):
            self.apply()
        self.assertFalse(self.dest.exists())

    def test_disabled_tls_or_peer_validation_is_refused(self):
        for key in (b"tlsEnabled", b"checkPeerFingerprints"):
            with self.subTest(key=key):
                (self.source / "Deskflow.conf").write_bytes(self.original.replace(key + b"=true", key + b"=false"))
                with self.assertRaises(ValueError):
                    self.apply()
                self.assertFalse(self.dest.exists())

    def test_missing_or_symlinked_identity_trust_and_config_are_refused(self):
        for relative in ("tls/deskflow.pem", "tls/trusted-clients", "deskflow-server.conf"):
            file = self.source / relative
            contents = file.read_bytes()
            file.unlink()
            with self.subTest(relative=relative, kind="missing"):
                with self.assertRaises(ValueError):
                    self.apply()
            external = Path(self.temp.name) / "external"
            external.write_bytes(contents)
            file.symlink_to(external)
            with self.subTest(relative=relative, kind="symlink"):
                with self.assertRaises(ValueError):
                    self.apply()
            file.unlink()
            file.write_bytes(contents)

    def test_existing_destination_is_never_replaced(self):
        self.dest.mkdir()
        sentinel = self.dest / "keep"
        sentinel.write_text("existing profile")
        with self.assertRaisesRegex(ValueError, "already exists"):
            self.apply()
        self.assertEqual(sentinel.read_text(), "existing profile")

    def test_destination_races_do_not_replace_directory_or_symlink(self):
        for symlink in (False, True):
            with self.subTest(symlink=symlink):
                target = Path(self.temp.name) / "existing"
                target.mkdir(exist_ok=True)

                def racing_publish(stage, destination):
                    if symlink:
                        destination.symlink_to(target, target_is_directory=True)
                    else:
                        destination.mkdir()
                    self.publish(stage, destination)

                with patch.object(migration, "_rename_exclusive", side_effect=racing_publish):
                    with self.assertRaisesRegex(ValueError, "Destination appeared"):
                        migration.prepare(self.source, self.dest, True)
                self.assertTrue(self.dest.is_symlink() if symlink else self.dest.is_dir())
                self.assertEqual(list(self.dest.iterdir()), [])
                self.dest.unlink() if symlink else self.dest.rmdir()
                self.assertEqual(list(self.dest.parent.glob(".deskbridge-server-prepare-*")), [])

    def test_unavailable_atomic_publication_fails_closed(self):
        with patch.object(migration, "_rename_exclusive", side_effect=OSError(errno.ENOTSUP, "unavailable")):
            with self.assertRaises(OSError):
                migration.prepare(self.source, self.dest, True)
        self.assertFalse(self.dest.exists())
        self.assertEqual(list(self.dest.parent.glob(".deskbridge-server-prepare-*")), [])

    def test_linux_rename_uses_no_replace_flag_and_reports_errno(self):
        rename = Mock(return_value=-1)
        library = Mock(renameat2=rename)
        with patch.object(migration.sys, "platform", "linux"), patch.object(migration.ctypes, "CDLL", return_value=library), patch.object(migration.ctypes, "get_errno", return_value=errno.EEXIST):
            with self.assertRaises(OSError) as raised:
                migration._rename_exclusive(Path("stage"), self.dest)
        self.assertEqual(raised.exception.errno, errno.EEXIST)
        rename.assert_called_once_with(-100, b"stage", -100, os.fsencode(self.dest), 1)


if __name__ == "__main__":
    unittest.main()
