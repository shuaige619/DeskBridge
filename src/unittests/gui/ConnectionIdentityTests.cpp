/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#include "common/Hotkey.h"
#include "common/KeyboardModifier.h"
#include "common/Settings.h"
#include "gui/config/ServerConfig.h"
#include "gui/personal/ConnectionIdentity.h"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <utility>

using deskflow::gui::personal::expandedConnectionIdentityChanges;

class ConnectionIdentityTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase()
  {
    QVERIFY(m_files.isValid());
    qputenv("XDG_CONFIG_HOME", m_files.filePath("isolated-config").toUtf8());
    qputenv("XDG_STATE_HOME", m_files.filePath("isolated-state").toUtf8());
    Settings::setStateFile(m_files.filePath(QStringLiteral("state.conf")));
  }

  void init()
  {
    const QString directory = m_files.filePath(QString::number(++m_profile));
    QVERIFY(QDir().mkpath(directory));
    Settings::setSettingsFile(directory + QStringLiteral("/settings.conf"));
    Settings::setValue(Settings::Core::ComputerName, QStringLiteral("source"));
    Settings::setValue(Settings::Server::GridWidth, 5);
    Settings::setValue(Settings::Server::GridHeight, 3);
    ServerConfig config;
    config.addClient(QStringLiteral("target"));
    QVERIFY(config.setScreenLayout(deskflow::layout::Direction::Right));
    config.commit();
    Settings::setValue(Settings::Computer::ModifierCtrl.arg(QStringLiteral("source")), QStringLiteral("meta"));
    Settings::setValue(Settings::Computer::ModifierMeta.arg(QStringLiteral("source")), QStringLiteral("ctrl"));
    Settings::setValue(
        Settings::Computer::Aliases.arg(QStringLiteral("source")), QStringList{QStringLiteral("source-alias")}
    );
    Settings::setValue(Settings::Computer::ModifierCtrl.arg(QStringLiteral("target")), QStringLiteral("alt"));
    Settings::setValue(
        Settings::Computer::Aliases.arg(QStringLiteral("target")), QStringList{QStringLiteral("target-alias")}
    );
    Settings::proxy().setValue(QStringLiteral("computer_source/future/nestedOption"), QStringLiteral("keep"));
    Settings::proxy().setValue(QStringLiteral("computer_sourceExtra/custom"), QStringLiteral("unrelated"));

    Hotkey hotkey;
    KeySequence trigger;
    QVERIFY(trigger.appendKey(Qt::Key_F9, Qt::NoModifier));
    hotkey.setKeySequence(trigger);
    Action switchTo;
    switchTo.setType(static_cast<int>(Action::Type::switchToComputer));
    switchTo.setSwitchComputerName(QStringLiteral("source"));
    hotkey.addAction(switchTo);
    Action key;
    key.setType(static_cast<int>(Action::Type::keyDown));
    KeySequence output;
    QVERIFY(output.appendKey(Qt::Key_F8, Qt::NoModifier));
    key.setKeySequence(output);
    key.setHaveComputers(true);
    key.addComputer(QStringLiteral("source"));
    key.addComputer(QStringLiteral("target"));
    hotkey.addAction(key);
    auto &settings = Settings::proxy();
    settings.beginGroup(QStringLiteral("internalConfig"));
    settings.beginWriteArray(QStringLiteral("hotkeys"));
    settings.setArrayIndex(0);
    hotkey.saveSettings(settings.get());
    settings.endArray();
    settings.endGroup();
    Settings::save(false);
  }

  void atomicRenameSurvivesReconstructionWithModifiersAndActionTargets()
  {
    const QByteArray before = bytes(Settings::settingsFile());
    QSignalSpy changes(Settings::instance(), &Settings::settingsChanged);
    const QVariantMap requested{
        {Settings::Core::ComputerName, QStringLiteral("renamed")},
        {Settings::Client::RemoteHost, QStringLiteral("192.0.2.8")}
    };
    const auto expanded = expandedConnectionIdentityChanges(requested);
    QCOMPARE(changes.count(), 0);
    QCOMPARE(bytes(Settings::settingsFile()), before);
    QCOMPARE(expanded.value(Settings::Client::RemoteHost).toString(), QStringLiteral("192.0.2.8"));
    QCOMPARE(
        expanded.value(Settings::Computer::Name.arg(QStringLiteral("renamed"))).toString(), QStringLiteral("renamed")
    );
    QCOMPARE(expanded.value(QStringLiteral("computer_renamed/future/nestedOption")).toString(), QStringLiteral("keep"));
    QVERIFY(!expanded.contains(QStringLiteral("computer_renamedExtra/custom")));

    bool completeFromFirstSignal = true;
    const auto observer = connect(Settings::instance(), &Settings::settingsChanged, this, [&] {
      completeFromFirstSignal &= Settings::value(Settings::Core::ComputerName).toString() == QStringLiteral("renamed");
      completeFromFirstSignal &=
          Settings::value(Settings::Computer::ModifierCtrl.arg(QStringLiteral("renamed"))).toString() ==
          QStringLiteral("meta");
    });
    QString error;
    const bool saved = Settings::trySetValues(expanded, &error);
    disconnect(observer);
    QVERIFY2(saved, qPrintable(error));
    QVERIFY(completeFromFirstSignal);
    ServerConfig reloaded;
    const auto &computers = std::as_const(reloaded).computers();
    const auto source = std::ranges::find_if(computers, [](const Computer &computer) {
      return computer.name() == QStringLiteral("renamed");
    });
    QVERIFY(source != computers.cend());
    QVERIFY(source->isServer());
    QCOMPARE(source->modifier(static_cast<int>(KeyboardModifier::Ctrl)), static_cast<int>(KeyboardModifier::Meta));
    QCOMPARE(source->aliases(), QStringList{QStringLiteral("source-alias")});
    QVERIFY(!reloaded.computerExists(QStringLiteral("source")));
    const auto &actions = std::as_const(reloaded).hotkeys().first().actions();
    QCOMPARE(actions.at(0).switchComputerName(), QStringLiteral("renamed"));
    QCOMPARE(actions.at(1).typeComputerNames(), (QStringList{QStringLiteral("renamed"), QStringLiteral("target")}));
    QVERIFY(!Settings::proxy().contains(Settings::Computer::ModifierCtrl.arg(QStringLiteral("source"))));
  }

  void rollbackRestoresMissingDestinationKeysAndTheOriginalIdentity()
  {
    const auto expanded =
        expandedConnectionIdentityChanges({{Settings::Core::ComputerName, QStringLiteral("renamed")}});
    QVariantMap previous;
    for (auto it = expanded.cbegin(); it != expanded.cend(); ++it)
      previous.insert(it.key(), Settings::proxy().value(it.key()));
    QVERIFY(!previous.value(Settings::Computer::Name.arg(QStringLiteral("renamed"))).isValid());
    QVERIFY(Settings::trySetValues(expanded));
    QVERIFY(Settings::proxy().contains(Settings::Computer::Name.arg(QStringLiteral("renamed"))));
    QVERIFY(Settings::trySetValues(previous));
    QCOMPARE(Settings::value(Settings::Core::ComputerName).toString(), QStringLiteral("source"));
    for (auto it = previous.cbegin(); it != previous.cend(); ++it)
      if (!it.value().isValid())
        QVERIFY2(!Settings::proxy().contains(it.key()), qPrintable(it.key()));
    ServerConfig reloaded;
    QVERIFY(reloaded.computerExists(QStringLiteral("source")));
    QVERIFY(!reloaded.computerExists(QStringLiteral("renamed")));
    QCOMPARE(std::as_const(reloaded).hotkeys().first().actions().at(0).switchComputerName(), QStringLiteral("source"));
    QCOMPARE(
        std::as_const(reloaded).hotkeys().first().actions().at(1).typeComputerNames(),
        (QStringList{QStringLiteral("source"), QStringLiteral("target")})
    );
  }

  void renameKeepsExactlyTheConfiguredDevices_data()
  {
    QTest::addColumn<QString>("newName");
    QTest::newRow("new-name") << QStringLiteral("renamed");
    QTest::newRow("case-only") << QStringLiteral("SOURCE");
  }

  void renameKeepsExactlyTheConfiguredDevices()
  {
    QFETCH(QString, newName);
    const auto expanded = expandedConnectionIdentityChanges({{Settings::Core::ComputerName, newName}});
    QVERIFY(Settings::trySetValues(expanded));
    auto actual = Settings::knownComputers();
    actual.removeAll(QString());
    actual.sort();
    QStringList expected{newName, QStringLiteral("target")};
    expected.sort();
    QCOMPARE(actual, expected);
    QVERIFY(!Settings::proxy().contains(Settings::Computer::Name.arg(QStringLiteral("source"))));
    QCOMPARE(Settings::value(Settings::Computer::ModifierCtrl.arg(newName)).toString(), QStringLiteral("meta"));
    QCOMPARE(Settings::value(Settings::Computer::Aliases.arg(newName)).toStringList(),
             QStringList{QStringLiteral("source-alias")});
    QCOMPARE(Settings::proxy().value(QStringLiteral("computer_%1/future/nestedOption").arg(newName)).toString(),
             QStringLiteral("keep"));
    QCOMPARE(Settings::value(Settings::Computer::ModifierCtrl.arg(QStringLiteral("target"))).toString(),
             QStringLiteral("alt"));
    QCOMPARE(Settings::value(Settings::Computer::Aliases.arg(QStringLiteral("target"))).toStringList(),
             QStringList{QStringLiteral("target-alias")});
    ServerConfig reloaded;
    const auto layout = reloaded.screenLayout();
    QVERIFY2(layout.editable, qPrintable(layout.error));
    QCOMPARE(layout.serverName, newName);
    const auto &actions = std::as_const(reloaded).hotkeys().first().actions();
    QCOMPARE(actions.at(0).switchComputerName(), newName);
    QCOMPARE(actions.at(1).typeComputerNames(), (QStringList{newName, QStringLiteral("target")}));

    // QSettings shares caches between objects using the same filename. Reopen
    // an actual disk copy at a new path to verify the serialized migration.
    const QString copiedPath = Settings::settingsFile() + QStringLiteral(".reopened");
    QVERIFY(QFile::copy(Settings::settingsFile(), copiedPath));
    QSettings disk(copiedPath, QSettings::IniFormat);
    QStringList persistedNames;
    for (const auto &group : disk.childGroups()) {
      if (!group.startsWith(QStringLiteral("computer_")))
        continue;
      const auto name = disk.value(group + QStringLiteral("/name")).toString();
      if (!name.isEmpty())
        persistedNames.append(name);
    }
    persistedNames.sort();
    QCOMPARE(persistedNames, expected);
    QVERIFY(!disk.contains(Settings::Computer::Name.arg(QStringLiteral("source"))));
    QCOMPARE(disk.value(Settings::Computer::ModifierCtrl.arg(newName)).toString(), QStringLiteral("meta"));
    QCOMPARE(disk.value(Settings::Computer::Aliases.arg(newName)).toStringList(),
             QStringList{QStringLiteral("source-alias")});
    QCOMPARE(disk.value(QStringLiteral("computer_%1/future/nestedOption").arg(newName)).toString(),
             QStringLiteral("keep"));
    QCOMPARE(disk.value(Settings::Computer::ModifierCtrl.arg(QStringLiteral("target"))).toString(),
             QStringLiteral("alt"));
    QCOMPARE(disk.value(Settings::Computer::Aliases.arg(QStringLiteral("target"))).toStringList(),
             QStringList{QStringLiteral("target-alias")});
    QCOMPARE(disk.status(), QSettings::NoError);
  }

  void normalizationMatchesStoredNameAndInvalidRequestsDoNotExpand()
  {
    for (const QVariant &name :
         {QVariant(), QVariant(QString()), QVariant(QStringLiteral("bad/name")), QVariant(QStringLiteral("...")),
          QVariant(QStringLiteral("source"))}) {
      const QVariantMap requested{{Settings::Core::ComputerName, name}};
      QCOMPARE(expandedConnectionIdentityChanges(requested), requested);
    }
    const QVariantMap other{{Settings::Client::RemoteHost, QStringLiteral("192.0.2.4")}};
    QCOMPARE(expandedConnectionIdentityChanges(other), other);
    const auto normalized =
        expandedConnectionIdentityChanges({{Settings::Core::ComputerName, QStringLiteral(".renamed-")}});
    QCOMPARE(
        normalized.value(Settings::Computer::Name.arg(QStringLiteral("renamed"))).toString(), QStringLiteral("renamed")
    );
    QVERIFY(Settings::trySetValues(normalized));
    QCOMPARE(Settings::value(Settings::Core::ComputerName).toString(), QStringLiteral("renamed"));
    ServerConfig reloaded;
    QVERIFY(reloaded.computerExists(QStringLiteral("renamed")));
  }

  void externalFileAndUnrelatedNamesRemainUntouched()
  {
    const QString fileName = m_files.filePath(QStringLiteral("external.conf"));
    QFile file(fileName);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray original = "# source remains external text\nsection: links\nsource:\nright = target\nend\n";
    QCOMPARE(file.write(original), original.size());
    file.close();
    Settings::setValue(Settings::Server::ExternalConfig, true);
    Settings::setValue(Settings::Server::ExternalConfigFile, fileName);
    const auto expanded =
        expandedConnectionIdentityChanges({{Settings::Core::ComputerName, QStringLiteral("renamed")}});
    QVERIFY(Settings::trySetValues(expanded));
    QCOMPARE(bytes(fileName), original);
    QCOMPARE(Settings::value(Settings::Server::ExternalConfigFile).toString(), fileName);
    QCOMPARE(
        Settings::proxy().value(QStringLiteral("computer_sourceExtra/custom")).toString(), QStringLiteral("unrelated")
    );
    QCOMPARE(
        Settings::value(Settings::Computer::Name.arg(QStringLiteral("target"))).toString(), QStringLiteral("target")
    );
  }

private:
  static QByteArray bytes(const QString &path)
  {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
      return {};
    return file.readAll();
  }

  QTemporaryDir m_files;
  int m_profile = 0;
};

QTEST_MAIN(ConnectionIdentityTests)
#include "ConnectionIdentityTests.moc"
