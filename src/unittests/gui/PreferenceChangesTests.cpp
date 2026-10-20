/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#include "common/Settings.h"
#include "gui/personal/PreferenceChanges.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRect>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>

using namespace deskflow::gui::personal;

class PreferenceChangesTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase()
  {
    QVERIFY(m_files.isValid());
    qputenv("XDG_CONFIG_HOME", m_files.path().toUtf8());
    qputenv("XDG_STATE_HOME", m_files.path().toUtf8());
  }

  void init()
  {
    const auto profile = QString::number(++m_profile);
    Settings::setSettingsFile(m_files.filePath(profile + ".conf"));
    Settings::setStateFile(m_files.filePath(profile + ".state"));
    Settings::save(false);
  }

  void batchPublishesCompleteConfigurationAndPreservesUnrelatedKeys()
  {
    const auto shortcutRules = QStringLiteral("{\"test\":\"keep shortcut rules\"}");
    Settings::setValue(Settings::Client::ShortcutConfig, shortcutRules);
    Settings::setValue(Settings::Core::ComputerName, QStringLiteral("unchanged-computer"));
    QSignalSpy spy(Settings::instance(), &Settings::settingsChanged);
    bool completeAtEverySignal = true;
    const auto connection = connect(Settings::instance(), &Settings::settingsChanged, this, [&](const QString &) {
      completeAtEverySignal &= Settings::value(Settings::Core::Port).toInt() == 25432 &&
                               Settings::value(Settings::Gui::Autohide).toBool();
    });

    QString error;
    QVERIFY2(commitPreferenceChanges({{Settings::Core::Port, 25432}, {Settings::Gui::Autohide, true}}, &error),
             qPrintable(error));
    disconnect(connection);
    QVERIFY(completeAtEverySignal);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(Settings::value(Settings::Client::ShortcutConfig).toString(), shortcutRules);
    QCOMPARE(Settings::value(Settings::Core::ComputerName).toString(), QStringLiteral("unchanged-computer"));
    QSettings persisted(Settings::settingsFile(), QSettings::IniFormat);
    QCOMPARE(persisted.value(Settings::Core::Port).toInt(), 25432);
    QVERIFY(persisted.value(Settings::Gui::Autohide).toBool());
    QCOMPARE(persisted.value(Settings::Client::ShortcutConfig).toString(), shortcutRules);
  }

  void invalidDraftChangesNothing()
  {
    const auto previous = settingsBytes();
    QSignalSpy spy(Settings::instance(), &Settings::settingsChanged);
    QString error;
    QVERIFY(!commitPreferenceChanges({{Settings::Gui::Autohide, true}, {Settings::Core::Port, 65536}}, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(settingsBytes(), previous);
    QVERIFY(!Settings::value(Settings::Gui::Autohide).toBool());
    QCOMPARE(Settings::value(Settings::Core::Port).toInt(), 24800);
    QCOMPARE(spy.count(), 0);
  }

  void unchangedAndImplicitDefaultsEmitNoSignals()
  {
    const auto previous = settingsBytes();
    QSignalSpy spy(Settings::instance(), &Settings::settingsChanged);
    QVERIFY(commitPreferenceChanges({{Settings::Gui::Autohide, false}, {Settings::Core::Port, 24800}}));
    QCOMPARE(spy.count(), 0);
    QCOMPARE(settingsBytes(), previous);
    QSettings persisted(Settings::settingsFile(), QSettings::IniFormat);
    QVERIFY(!persisted.contains(Settings::Gui::Autohide));
    QVERIFY(!persisted.contains(Settings::Core::Port));
  }

  void restoringAbsenceRemovesAnExplicitDefaultWithoutASignal()
  {
    Settings::setValue(Settings::Gui::Autohide, false);
    QSettings persisted(Settings::settingsFile(), QSettings::IniFormat);
    QVERIFY(persisted.contains(Settings::Gui::Autohide));
    QSignalSpy spy(Settings::instance(), &Settings::settingsChanged);
    QVERIFY(Settings::trySetValues({{Settings::Gui::Autohide, QVariant()}}));
    persisted.sync();
    QVERIFY(!persisted.contains(Settings::Gui::Autohide));
    QVERIFY(!Settings::value(Settings::Gui::Autohide).toBool());
    QCOMPARE(spy.count(), 0);
  }

  void rejectsCertificateMutationAndUnrecognizedLevels()
  {
    const auto certificate = Settings::value(Settings::Security::Certificate);
    const auto keySize = Settings::value(Settings::Security::KeySize);
    QVERIFY(!commitPreferenceChanges({{Settings::Security::Certificate, QStringLiteral("/tmp/replaced.pem")}}));
    QVERIFY(!commitPreferenceChanges({{Settings::Security::KeySize, 4096}}));
    QVERIFY(!commitPreferenceChanges({{Settings::Log::Level, QStringLiteral("TRACE")}}));
    QVERIFY(!commitPreferenceChanges({{Settings::Core::Language, QStringLiteral("en_US")}}));
    QCOMPARE(Settings::value(Settings::Security::Certificate), certificate);
    QCOMPARE(Settings::value(Settings::Security::KeySize), keySize);
    QCOMPARE(Settings::value(Settings::Log::Level).toString().toUpper(), QStringLiteral("INFO"));
  }

  void validatesTypesRangesAndAddresses()
  {
    QVERIFY(!validatePreferenceChanges({{Settings::Security::TlsEnabled, QStringLiteral("false")}}));
    QVERIFY(!validatePreferenceChanges({{Settings::Core::Port, 1234.5}}));
    QVERIFY(!validatePreferenceChanges({{Settings::Client::XScrollScale, 0.0}}));
    QVERIFY(!validatePreferenceChanges({{Settings::Client::YScrollScale, std::numeric_limits<double>::infinity()}}));
    QVERIFY(!validatePreferenceChanges({{Settings::Client::YScrollScale, std::numeric_limits<double>::quiet_NaN()}}));
    QVERIFY(!validatePreferenceChanges({{Settings::Core::Interface, QStringLiteral("computer.local")}}));
    QVERIFY(validatePreferenceChanges({{Settings::Core::Interface, QStringLiteral("192.168.1.2")}}));
    QVERIFY(validatePreferenceChanges({{Settings::Core::Interface, QStringLiteral("::1")}}));
    QVERIFY(validatePreferenceChanges({{Settings::Core::Interface, QString()}}));
    QVERIFY(validatePreferenceChanges({{Settings::Client::XScrollScale, 0.1}, {Settings::Client::YScrollScale, 10.0}}));
#ifndef Q_OS_WIN
    QVERIFY(!validatePreferenceChanges({{Settings::Daemon::Elevate, true}}));
#endif
  }

  void commandValidationUsesTheMergedDraft()
  {
    QVERIFY(!validatePreferenceChanges({{Settings::Core::EnableEnterCommand, true}}));
    QVERIFY(!validatePreferenceChanges({{Settings::Core::EnableExitCommand, true},
                                       {Settings::Core::ComputerExitCommand, QStringLiteral("  ")}}));
    QVERIFY(commitPreferenceChanges({{Settings::Core::EnableEnterCommand, true},
                                     {Settings::Core::ComputerEnterCommand, QStringLiteral("echo entered")}}));
    QVERIFY(!validatePreferenceChanges({{Settings::Core::ComputerEnterCommand, QString()}}));
    QVERIFY(validatePreferenceChanges({{Settings::Core::EnableEnterCommand, false},
                                      {Settings::Core::ComputerEnterCommand, QString()}}));
  }

  void preservesLegacyValuesWhenEditingOtherPreferences()
  {
    Settings::setValue(Settings::Core::Interface, QStringLiteral("legacy-host-name"));
    Settings::setValue(Settings::Log::ToFile, true);
    Settings::setValue(Settings::Log::File, QStringLiteral("legacy-relative.log"));
    Settings::setValue(Settings::Core::EnableEnterCommand, true);
    Settings::setValue(Settings::Core::ComputerEnterCommand, QString());
    QVERIFY(commitPreferenceChanges({{Settings::Gui::Autohide, true},
                                     {Settings::Core::Interface, QStringLiteral("legacy-host-name")},
                                     {Settings::Log::File, QStringLiteral("legacy-relative.log")}}));
    QCOMPARE(Settings::value(Settings::Log::File).toString(), QStringLiteral("legacy-relative.log"));
  }

  void logPathValidationDoesNotCreateFiles()
  {
    const auto path = m_files.filePath(QStringLiteral("new-log-directory/deskbridge.log"));
    QVERIFY(validatePreferenceChanges({{Settings::Log::ToFile, true}, {Settings::Log::File, path}}));
    QVERIFY(!QFileInfo::exists(path));
    QVERIFY(!QFileInfo::exists(QFileInfo(path).absolutePath()));
    QVERIFY(!validatePreferenceChanges({{Settings::Log::ToFile, true}, {Settings::Log::File, QString()}}));
    QVERIFY(!validatePreferenceChanges({{Settings::Log::ToFile, true},
                                       {Settings::Log::File, QStringLiteral("relative.log")}}));
    QVERIFY(!validatePreferenceChanges({{Settings::Log::ToFile, true}, {Settings::Log::File, m_files.path()}}));
    QVERIFY(validatePreferenceChanges({{Settings::Log::ToFile, false}, {Settings::Log::File, QString()}}));
  }

  void reconnectClassificationConsidersOnlyEffectiveCoreChanges()
  {
    QVERIFY(!preferencesRequireReconnect({{Settings::Gui::AutoStartCore, true},
                                         {Settings::Log::Level, QStringLiteral("DEBUG")},
                                         {Settings::Log::GuiDebug, true}}));
    QVERIFY(!preferencesRequireReconnect({{Settings::Core::Port, Settings::value(Settings::Core::Port)}}));
    QVERIFY(preferencesRequireReconnect({{Settings::Core::Port, 25432}}));
    QVERIFY(preferencesRequireReconnect({{Settings::Security::TlsEnabled, false}}));
    QVERIFY(!preferencesRequireReconnect({{Settings::Core::ComputerEnterCommand, QStringLiteral("echo entered")}}));
    QVERIFY(preferencesRequireReconnect({{Settings::Core::EnableEnterCommand, true},
                                        {Settings::Core::ComputerEnterCommand, QStringLiteral("echo entered")}}));
    QVERIFY(!preferencesRequireReconnect({{Settings::Log::File, m_files.filePath("other.log")}}));
    QVERIFY(preferencesRequireReconnect({{Settings::Log::ToFile, true}}));
    Settings::setValue(Settings::Log::ToFile, true);
    QVERIFY(preferencesRequireReconnect({{Settings::Log::File, m_files.filePath("other.log")}}));
  }

  void mixedBackendsAreRejectedBeforeWriting()
  {
    if (Settings::isPortableMode())
      QSKIP("Portable mode intentionally stores state in the config file.");
    const auto previous = settingsBytes();
    QSignalSpy spy(Settings::instance(), &Settings::settingsChanged);
    QVERIFY(!Settings::trySetValues({{Settings::Gui::Autohide, true}, {Settings::Gui::WindowGeometry, QRect(1, 2, 3, 4)}}));
    QCOMPARE(settingsBytes(), previous);
    QCOMPARE(spy.count(), 0);
    QVERIFY(Settings::trySetValues({{Settings::Gui::WindowGeometry, QRect(1, 2, 3, 4)}}));
    QCOMPARE(Settings::value(Settings::Gui::WindowGeometry).toRect(), QRect(1, 2, 3, 4));
  }

  void readonlyFailureKeepsAbsentKeysAndAllowsRetry()
  {
    const auto filename = Settings::settingsFile();
    const auto originalPermissions = QFile::permissions(filename);
    const auto previous = settingsBytes();
    QVERIFY(QFile::setPermissions(filename, QFileDevice::ReadOwner | QFileDevice::ReadUser));
    const bool writable = QFileInfo(filename).isWritable();
    QString error;
    QSignalSpy spy(Settings::instance(), &Settings::settingsChanged);
    const bool saved = commitPreferenceChanges({{Settings::Gui::Autohide, true}, {Settings::Core::Port, 25432}}, &error);
    const auto bytesAfterFailure = settingsBytes();
    QVERIFY(QFile::setPermissions(filename, originalPermissions));
    if (writable)
      QSKIP("The current account bypasses file write permissions.");
    QVERIFY(!saved);
    QVERIFY(!error.isEmpty());
    QCOMPARE(bytesAfterFailure, previous);
    QVERIFY(!Settings::value(Settings::Gui::Autohide).toBool());
    QSettings persisted(filename, QSettings::IniFormat);
    QVERIFY(!persisted.contains(Settings::Gui::Autohide));
    QVERIFY(!persisted.contains(Settings::Core::Port));
    QCOMPARE(spy.count(), 0);
    QVERIFY2(commitPreferenceChanges({{Settings::Gui::Autohide, true}, {Settings::Core::Port, 25432}}, &error),
             qPrintable(error));
    QCOMPARE(spy.count(), 2);
  }

  void atomicWriteFailureRestoresExistingValuesAndAbsence()
  {
    const auto directory = m_files.filePath(QStringLiteral("atomic-write-failure"));
    QVERIFY(QDir().mkpath(directory));
    Settings::setSettingsFile(directory + QStringLiteral("/settings.conf"));
    Settings::setValue(Settings::Gui::AutoStartCore, true);
    Settings::save(false);
    const auto previous = settingsBytes();
    const auto permissions = QFile::permissions(directory);
    QVERIFY(QFile::setPermissions(directory, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    const bool writable = QFileInfo(directory).isWritable();
    QSignalSpy spy(Settings::instance(), &Settings::settingsChanged);
    QString error;
    const bool saved = Settings::trySetValues({{Settings::Gui::AutoStartCore, false},
                                              {Settings::Gui::Autohide, true},
                                              {Settings::Core::Port, 25432}}, &error);
    const auto after = settingsBytes();
    QVERIFY(QFile::setPermissions(directory, permissions));
    if (writable)
      QSKIP("The current account bypasses directory write permissions.");
    QVERIFY(!saved);
    QVERIFY(!error.isEmpty());
    QCOMPARE(after, previous);
    QVERIFY(Settings::value(Settings::Gui::AutoStartCore).toBool());
    QVERIFY(!Settings::value(Settings::Gui::Autohide).toBool());
    QCOMPARE(Settings::value(Settings::Core::Port).toInt(), 24800);
    QSettings persisted(Settings::settingsFile(), QSettings::IniFormat);
    QVERIFY(!persisted.contains(Settings::Gui::Autohide));
    QVERIFY(!persisted.contains(Settings::Core::Port));
    QCOMPARE(spy.count(), 0);
  }

private:
  QByteArray settingsBytes() const
  {
    QFile file(Settings::settingsFile());
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
  }

  QTemporaryDir m_files;
  int m_profile = 0;
};

QTEST_MAIN(PreferenceChangesTests)
#include "PreferenceChangesTests.moc"
