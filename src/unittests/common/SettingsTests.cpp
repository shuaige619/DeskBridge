/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "SettingsTests.h"

#include <QFile>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <filesystem>

namespace {
QString expectedSettingsBase(const QString &isolatedPath)
{
#ifdef Q_OS_WIN
  if (!Settings::isPortableMode()) return Settings::SystemDir;
#endif
  return isolatedPath;
}

bool canIsolateCertificateMigration()
{
#ifdef Q_OS_WIN
  if (!Settings::isPortableMode())
    return false;
#endif
  return QString::fromUtf8(kAppName).toLower() != QString::fromUtf8(kAppId).toLower();
}
} // namespace

void SettingsTests::initTestCase()
{
  QVERIFY(m_initialProfile.isValid());
  qputenv("XDG_CONFIG_HOME", m_initialProfile.path().toUtf8());
  qputenv("XDG_STATE_HOME", m_initialProfile.path().toUtf8());
  QFile oldSettings(m_settingsFile);
  if (oldSettings.exists())
    oldSettings.remove();
}

void SettingsTests::setSettingsFile()
{
  Settings::setSettingsFile(m_settingsFile);
}

void SettingsTests::setStateFile()
{
  Settings::setStateFile(m_stateFile);
}

void SettingsTests::settingsFile()
{
  QVERIFY(Settings::settingsFile().endsWith(m_settingsFile));
}

void SettingsTests::settingsPath()
{
  QVERIFY(Settings::settingsPath().endsWith(expectedSettingsBase(m_settingsPathTemp)));
}

void SettingsTests::tlsDir()
{
  QVERIFY(Settings::tlsDir().endsWith(expectedSettingsBase(m_settingsPathTemp) + "/tls"));
}

void SettingsTests::certificatePathMigration_data()
{
  QTest::addColumn<bool>("createLegacy");
  QTest::addColumn<bool>("createBranded");
  QTest::addColumn<int>("explicitSelection");
  QTest::addColumn<int>("expectedSelection");
  QTest::newRow("fresh-install-uses-branded-name") << false << false << 0 << 0;
  QTest::newRow("implicit-legacy-identity-is-preserved") << true << false << 0 << 1;
  QTest::newRow("both-files-preserve-old-implicit-identity") << true << true << 0 << 1;
  QTest::newRow("existing-branded-identity-is-preserved") << false << true << 0 << 0;
  QTest::newRow("explicit-custom-path-is-never-replaced") << true << true << 2 << 2;
  QTest::newRow("explicit-legacy-path-is-never-renamed") << true << true << 1 << 1;
  QTest::newRow("explicit-branded-path-overrides-legacy-file") << true << true << 3 << 0;
}

void SettingsTests::certificatePathMigration()
{
  if (QString::fromUtf8(kAppName).toLower() == QString::fromUtf8(kAppId).toLower())
    QSKIP("Certificate path migration applies only to a renamed personal application.");
#ifdef Q_OS_WIN
  // Non-portable Windows installations intentionally share the system TLS
  // directory. Do not create or remove real identities from an isolated test.
  if (!Settings::isPortableMode())
    QSKIP("Certificate fixtures require an isolated TLS directory.");
#endif
  QFETCH(bool, createLegacy);
  QFETCH(bool, createBranded);
  QFETCH(int, explicitSelection);
  QFETCH(int, expectedSelection);
  QTemporaryDir profile;
  QVERIFY(profile.isValid());
  const auto originalSettingsFile = Settings::settingsFile();
  const auto restoreSettings = qScopeGuard([&] { Settings::setSettingsFile(originalSettingsFile); });
  const auto configurationFile = profile.filePath(QStringLiteral("settings.conf"));
  const auto tlsDirectory = profile.filePath(QStringLiteral("tls"));
  const auto brandedPath = QStringLiteral("%1/%2.pem").arg(tlsDirectory, QString::fromUtf8(kAppName).toLower());
  const auto legacyPath = QStringLiteral("%1/%2.pem").arg(tlsDirectory, kAppId);
  const auto customPath = profile.filePath(QStringLiteral("custom identity.pem"));
  const auto expectedPath = expectedSelection == 1 ? legacyPath : (expectedSelection == 2 ? customPath : brandedPath);
  QVERIFY(QDir().mkpath(tlsDirectory));
  const auto writeIdentity = [](const QString &filename, const QByteArray &contents) {
    QFile file(filename);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
  };
  const auto readIdentity = [](const QString &filename) {
    QFile file(filename);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
  };
  const QByteArray legacyIdentity("existing legacy identity; do not replace");
  const QByteArray brandedIdentity("existing branded identity; do not replace");
  if (createLegacy)
    QVERIFY(writeIdentity(legacyPath, legacyIdentity));
  if (createBranded)
    QVERIFY(writeIdentity(brandedPath, brandedIdentity));
  if (explicitSelection != 0) {
    QSettings existing(configurationFile, QSettings::IniFormat);
    existing.setValue(Settings::Security::Certificate,
                      explicitSelection == 1 ? legacyPath : (explicitSelection == 2 ? customPath : brandedPath));
    existing.sync();
    QCOMPARE(existing.status(), QSettings::NoError);
  }

  Settings::setSettingsFile(configurationFile);
  QCOMPARE(Settings::defaultValue(Settings::Security::Certificate).toString(), brandedPath);
  QCOMPARE(Settings::value(Settings::Security::Certificate).toString(), expectedPath);
  QCOMPARE(Settings::proxy().value(Settings::Security::Certificate).toString(), expectedPath);
  QSettings persisted(configurationFile, QSettings::IniFormat);
  QCOMPARE(persisted.value(Settings::Security::Certificate).toString(), expectedPath);
  if (createLegacy)
    QCOMPARE(readIdentity(legacyPath), legacyIdentity);
  else
    QVERIFY(!QFile::exists(legacyPath));
  if (createBranded)
    QCOMPARE(readIdentity(brandedPath), brandedIdentity);
  else
    QVERIFY(!QFile::exists(brandedPath));
  QVERIFY(!QFile::exists(customPath));

  // A later file addition/deletion and another startup must not silently select
  // a different identity. Explicit custom and legacy paths remain untouched too.
  if (!createLegacy)
    QVERIFY(writeIdentity(legacyPath, legacyIdentity));
  if (!createBranded)
    QVERIFY(writeIdentity(brandedPath, brandedIdentity));
  if (expectedPath != customPath)
    QVERIFY(QFile::remove(expectedPath));
  QCOMPARE(Settings::value(Settings::Security::Certificate).toString(), expectedPath);
  Settings::setSettingsFile(profile.filePath(QStringLiteral("another.conf")));
  Settings::setSettingsFile(configurationFile);
  QCOMPARE(Settings::value(Settings::Security::Certificate).toString(), expectedPath);
  QCOMPARE(Settings::proxy().value(Settings::Security::Certificate).toString(), expectedPath);
  persisted.sync();
  QCOMPARE(persisted.value(Settings::Security::Certificate).toString(), expectedPath);
}

void SettingsTests::certificateMigrationPreservesDanglingSymlink()
{
  if (!canIsolateCertificateMigration())
    QSKIP("Certificate fixtures require an isolated renamed application's TLS directory.");
  QTemporaryDir profile;
  QVERIFY(profile.isValid());
  const auto originalSettingsFile = Settings::settingsFile();
  const auto restoreSettings = qScopeGuard([&] { Settings::setSettingsFile(originalSettingsFile); });
  const auto legacyPath = profile.filePath(QStringLiteral("tls/%1.pem").arg(kAppId));
  QVERIFY(QDir().mkpath(profile.filePath(QStringLiteral("tls"))));
  std::error_code error;
  std::filesystem::create_symlink(QFile(profile.filePath(QStringLiteral("missing-identity.pem"))).filesystemFileName(),
                                  QFile(legacyPath).filesystemFileName(), error);
  if (error)
    QSKIP("The current platform/account cannot create a symbolic-link fixture.");
  QVERIFY(QFileInfo(legacyPath).isSymLink());
  QVERIFY(!QFile::exists(legacyPath));

  const auto configurationFile = profile.filePath(QStringLiteral("settings.conf"));
  Settings::setSettingsFile(configurationFile);
  QCOMPARE(Settings::value(Settings::Security::Certificate).toString(), legacyPath);
  QCOMPARE(Settings::proxy().value(Settings::Security::Certificate).toString(), legacyPath);
  QSettings persisted(configurationFile, QSettings::IniFormat);
  QCOMPARE(persisted.value(Settings::Security::Certificate).toString(), legacyPath);
  QVERIFY(QFileInfo(legacyPath).isSymLink());
  QVERIFY(!QFile::exists(Settings::defaultValue(Settings::Security::Certificate).toString()));
}

void SettingsTests::certificateMigrationRejectsUnreadableConfiguration()
{
  if (!canIsolateCertificateMigration())
    QSKIP("Certificate fixtures require an isolated renamed application's TLS directory.");
  QTemporaryDir profile;
  QVERIFY(profile.isValid());
  const auto originalSettingsFile = Settings::settingsFile();
  const auto restoreSettings = qScopeGuard([&] { Settings::setSettingsFile(originalSettingsFile); });
  const auto configurationFile = profile.filePath(QStringLiteral("broken.conf"));
  const QByteArray malformed("[broken-section\ninvalid configuration without an equals sign\n");
  QFile file(configurationFile);
  QVERIFY(file.open(QIODevice::WriteOnly));
  QCOMPARE(file.write(malformed), malformed.size());
  file.close();

  Settings::setSettingsFile(configurationFile);
  const auto legacyPath = profile.filePath(QStringLiteral("tls/%1.pem").arg(kAppId));
  QCOMPARE(Settings::defaultValue(Settings::Security::Certificate).toString(), legacyPath);
  QCOMPARE(Settings::value(Settings::Security::Certificate).toString(), legacyPath);
  QVERIFY(!Settings::proxy().contains(Settings::Security::Certificate));
  QVERIFY(!QFile::exists(profile.filePath(QStringLiteral("tls/%1.pem").arg(QString::fromUtf8(kAppName).toLower()))));
  QVERIFY(file.open(QIODevice::ReadOnly));
  QCOMPARE(file.readAll(), malformed);
}

void SettingsTests::tlsTrustedServersDb()
{
  QVERIFY(Settings::tlsTrustedServersDb().endsWith(expectedSettingsBase(m_settingsPathTemp) + "/tls/trusted-servers"));
}

void SettingsTests::tlsTrustedClientsDb()
{
  QVERIFY(Settings::tlsTrustedClientsDb().endsWith(expectedSettingsBase(m_settingsPathTemp) + "/tls/trusted-clients"));
}

void SettingsTests::checkValidSettings()
{
  QSignalSpy spy(Settings::instance(), &Settings::settingsChanged);
  QVERIFY(spy.isValid());

  const auto keysToCheck = QRegularExpression(QLatin1String("[^%1]").arg(Settings::Core::ComputerName));
  const auto validKeys = Settings::validKeys().filter(keysToCheck);
  for (const auto &setting : validKeys) {
    const auto value = Settings::value(setting).toString();
    QCOMPARE(Settings::defaultValue(setting).toString(), value);

    Settings::setValue(setting, "NEW_VALUE");
    QCOMPARE(spy.count(), 1);
    QCOMPARE(qvariant_cast<QString>(spy.first().at(0)), setting);
    QCOMPARE(Settings::value(setting).toString(), "NEW_VALUE");

    Settings::setValue(setting);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(Settings::value(setting).toString(), value);

    // Reset the spy for the next loop
    spy.clear();
    QCOMPARE(spy.count(), 0);
  }
}

void SettingsTests::checkCleanComputerName()
{
  const auto input = QStringLiteral("--!_ _-C@o#mpu$t%e^&*(r)= +Name\n[1]2|3?4--5>6<,7`~/8*90\\.lan--..    ..");
  const auto expected = QStringLiteral("Computer_Name_1234--567890.lan");

  Settings::setValue(Settings::Core::ComputerName, input);

  QCOMPARE(Settings::value(Settings::Core::ComputerName).toString(), expected);
}

void SettingsTests::checkClipboardShortcutPreferences()
{
  const QStringList defaultApps{QStringLiteral("com.openai.codex"), QStringLiteral("com.electron.lark")};
  QVERIFY(Settings::value(Settings::Client::ClipboardShortcuts).toBool());
  QCOMPARE(Settings::value(Settings::Client::ClipboardShortcutApps).toStringList(), defaultApps);

  // Disabling every app is an explicit choice, distinct from an unset preference.
  Settings::setValue(Settings::Client::ClipboardShortcuts, false);
  Settings::setValue(Settings::Client::ClipboardShortcutApps, QStringList{});
  Settings::setSettingsFile(m_settingsFile);
  QVERIFY(!Settings::value(Settings::Client::ClipboardShortcuts).toBool());
  QVERIFY(Settings::value(Settings::Client::ClipboardShortcutApps).toStringList().isEmpty());

  // A single app rule must also survive settings cleanup and a fresh proxy load.
  const QStringList selectedApps{QStringLiteral("com.electron.lark")};
  Settings::setValue(Settings::Client::ClipboardShortcutApps, selectedApps);
  Settings::setSettingsFile(m_settingsFile);
  QCOMPARE(Settings::value(Settings::Client::ClipboardShortcutApps).toStringList(), selectedApps);
  QCOMPARE(Settings::proxy().value(Settings::Client::ClipboardShortcutApps).toStringList(), selectedApps);

  Settings::setValue(Settings::Client::ClipboardShortcuts);
  Settings::setValue(Settings::Client::ClipboardShortcutApps);
  QVERIFY(Settings::value(Settings::Client::ClipboardShortcuts).toBool());
  QCOMPARE(Settings::value(Settings::Client::ClipboardShortcutApps).toStringList(), defaultApps);
}

void SettingsTests::checkCleanComputerName_LongName()
{
  QString input;
  input.fill('f', 300);
  input.prepend('.');

  QString expected;
  expected.fill('f', 255);

  Settings::setValue(Settings::Core::ComputerName, input);

  QCOMPARE(Settings::value(Settings::Core::ComputerName).toString(), expected);
}

void SettingsTests::checkClearState()
{
  const auto computerName = Settings::value(Settings::Core::ComputerName);
  const auto restore = qScopeGuard([&] { Settings::setValue(Settings::Core::ComputerName, computerName); });
  Settings::setValue(Settings::Core::ComputerName, "keep-my-connection-settings");
  Settings::setValue(Settings::Gui::WindowGeometry, QRect(1, 2, 3, 4));
  Settings::setValue(Settings::Client::XdpRestoreToken, "client-session-state");
  Settings::setValue(Settings::Server::XdpRestoreToken, "server-session-state");
  QVERIFY(Settings::value(Settings::Gui::WindowGeometry).toRect().isValid());

  Settings::clearState();

  QVERIFY(!Settings::value(Settings::Gui::WindowGeometry).toRect().isValid());
  QVERIFY(Settings::value(Settings::Client::XdpRestoreToken).toString().isEmpty());
  QVERIFY(Settings::value(Settings::Server::XdpRestoreToken).toString().isEmpty());
  QCOMPARE(Settings::value(Settings::Core::ComputerName).toString(), QStringLiteral("keep-my-connection-settings"));
}

QTEST_MAIN(SettingsTests)
