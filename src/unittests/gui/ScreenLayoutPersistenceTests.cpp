/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#include "gui/personal/ScreenLayoutPersistence.h"
#include "common/Settings.h"
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

using deskflow::layout::Direction;
using deskflow::layout::Move;
using deskflow::gui::personal::ScreenLayoutPersistence;

class ScreenLayoutPersistenceTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase()
  {
    QVERIFY(m_files.isValid());
    Settings::setStateFile(m_files.filePath("state.conf"));
  }

  void init()
  {
    const auto directory = m_files.filePath(QString::number(++m_profile));
    QVERIFY(QDir().mkpath(directory));
    const auto path = directory + "/settings.conf";
    Settings::setSettingsFile(path);
    Settings::setValue(Settings::Core::ComputerName, "source");
    Settings::setValue(Settings::Server::GridWidth, 5);
    Settings::setValue(Settings::Server::GridHeight, 3);
    Settings::proxy().setValue("unrelated/value", "preserve");
    ServerConfig initial;
    initial.addClient("target");
    QVERIFY(initial.setScreenLayout(Direction::Right));
    initial.commit();
    Settings::proxy().sync();
    Settings::save(false);
  }

  void savesBothConfigurationsAndReloadsAfterConfirmation()
  {
    ServerConfig config;
    ScreenLayoutPersistence transaction(config);
    QString error;
    QVERIFY2(transaction.prepare(Direction::Right, Direction::Above, &error), qPrintable(error));
    QVERIFY(transaction.isPending());
    QCOMPARE(config.screenLayout().direction, Direction::Above);
    const auto text = readGenerated();
    QVERIFY(text.contains("up = target"));
    QVERIFY(text.contains("down = source"));
    QVERIFY(transaction.finish(true));
    QVERIFY(!transaction.isPending());
    ServerConfig reloaded;
    QCOMPARE(reloaded.screenLayout().direction, Direction::Above);
    QCOMPARE(Settings::proxy().value("unrelated/value").toString(), QStringLiteral("preserve"));
  }

  void failedCoreApplyRestoresThePreviousFileAndSavedGrid()
  {
    const QByteArray previous = "# previous generated configuration\nsection: links\nend\n";
    QFile file(Settings::serverConfigFile());
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(previous), previous.size());
    file.close();
    ServerConfig config;
    ScreenLayoutPersistence transaction(config);
    QString error;
    QVERIFY(transaction.prepare(Direction::Right, Direction::Below, &error));
    QVERIFY(transaction.finish(false, &error));
    QCOMPARE(readGenerated(), previous);
    QCOMPARE(config.screenLayout().direction, Direction::Right);
    ServerConfig reloaded;
    QCOMPARE(reloaded.screenLayout().direction, Direction::Right);
    QVERIFY(!transaction.isPending());
  }

  void failedApplyRemovesOnlyANewlyGeneratedFile()
  {
    QVERIFY(!QFile::exists(Settings::serverConfigFile()));
    ServerConfig config;
    ScreenLayoutPersistence transaction(config);
    QString error;
    QVERIFY(transaction.prepare(Direction::Right, Direction::Left, &error));
    QVERIFY(QFile::exists(Settings::serverConfigFile()));
    QVERIFY(transaction.finish(false, &error));
    QVERIFY(!QFile::exists(Settings::serverConfigFile()));
    QCOMPARE(config.screenLayout().direction, Direction::Right);
  }

  void rejectsStaleOrConcurrentRequests()
  {
    ServerConfig config;
    ScreenLayoutPersistence transaction(config);
    QString error;
    QVERIFY(!transaction.prepare(Direction::Left, Direction::Above, &error));
    QVERIFY(!transaction.isPending());
    QCOMPARE(config.screenLayout().direction, Direction::Right);
    QVERIFY(!QFile::exists(Settings::serverConfigFile()));
    QVERIFY(transaction.prepare(Direction::Right, Direction::Above, &error));
    const auto pendingFile = readGenerated();
    QVERIFY(!transaction.prepare(Direction::Above, Direction::Below, &error));
    QCOMPARE(readGenerated(), pendingFile);
    QVERIFY(transaction.finish(false, &error));
  }

  void failedAtomicWriteDoesNotChangeTheSavedLayout()
  {
    QVERIFY(QDir().mkdir(Settings::serverConfigFile()));
    ServerConfig config;
    ScreenLayoutPersistence transaction(config);
    QString error;
    QVERIFY(!transaction.prepare(Direction::Right, Direction::Above, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!transaction.isPending());
    QCOMPARE(config.screenLayout().direction, Direction::Right);
    ServerConfig reloaded;
    QCOMPARE(reloaded.screenLayout().direction, Direction::Right);
    QVERIFY(QFileInfo(Settings::serverConfigFile()).isDir());
  }

  void savesAndReloadsThreeDeviceTopology()
  {
    seedThreeDevices();
    ServerConfig config;
    const auto before = config.screenLayout();
    QVERIFY2(before.editable, qPrintable(before.error));
    QCOMPARE(before.screens.size(), 3);
    ScreenLayoutPersistence transaction(config);
    QString error;
    QVERIFY2(transaction.prepareMove({"third", 0, 1, before.revision}, &error), qPrintable(error));
    const auto after = config.screenLayout();
    QVERIFY(after.revision != before.revision);
    QCOMPARE(after.screens.size(), 3);
    QVERIFY(readGenerated().contains("down = third"));
    QVERIFY(transaction.finish(true));
    ServerConfig reloaded;
    QCOMPARE(reloaded.screenLayout().revision, after.revision);
    QCOMPARE(Settings::proxy().value("unrelated/value").toString(), QStringLiteral("preserve"));
    QCOMPARE(namedComputer(reloaded, "third"), namedComputerFromBefore());
  }

  void restoresAllThreeDevicesAndExactConfigurationAfterRejectedMove()
  {
    seedThreeDevices();
    ServerConfig config;
    const auto before = config;
    const auto previous = QByteArray("# keep comments and user options byte for byte\n");
    QFile file(Settings::serverConfigFile());
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(previous), previous.size());
    file.close();
    ScreenLayoutPersistence transaction(config);
    QString error;
    QVERIFY2(transaction.prepareMove({"third", 0, -1, before.screenLayout().revision}, &error), qPrintable(error));
    QVERIFY(transaction.finish(false, &error));
    QCOMPARE(config, before);
    QCOMPARE(readGenerated(), previous);
    ServerConfig reloaded;
    QCOMPARE(reloaded, before);
  }

  void rejectsStaleFullTopologyAndOccupiedCellsWithoutWriting()
  {
    seedThreeDevices();
    ServerConfig config;
    const auto original = config.screenLayout();
    QString error;
    QVERIFY2(config.moveScreen({"third", 0, 1, original.revision}, &error), qPrintable(error));
    config.commit();
    Settings::save(false);
    ScreenLayoutPersistence transaction(config);
    QVERIFY(!transaction.prepareMove({"target", 0, -1, original.revision}, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!QFile::exists(Settings::serverConfigFile()));
    const auto current = config.screenLayout();
    QVERIFY(!transaction.prepareMove({"third", 1, 0, current.revision}, &error));
    QCOMPARE(config.screenLayout().revision, current.revision);
    QVERIFY(!transaction.isPending());
    QVERIFY(!QFile::exists(Settings::serverConfigFile()));
  }

  void movingSourceReanchorsSavedTopology()
  {
    seedThreeDevices();
    ServerConfig config;
    const auto before = config.screenLayout();
    ScreenLayoutPersistence transaction(config);
    QString error;
    // Source moves below target; third remains connected through target.
    QVERIFY2(transaction.prepareMove({"source", 1, 1, before.revision}, &error), qPrintable(error));
    const auto expected = deskflow::layout::movedSnapshot(before, {"source", 1, 1, before.revision}, &error);
    QVERIFY(expected.has_value());
    QCOMPARE(config.screenLayout().revision, expected->revision);
    QVERIFY(transaction.finish(true));
    ServerConfig reloaded;
    QCOMPARE(reloaded.screenLayout().revision, expected->revision);
  }

private:
  class ConfiguredComputer : public Computer
  {
  public:
    explicit ConfiguredComputer(const QString &name) : Computer(name)
    {
      addAlias(name + "-alias");
      setSwitchCornerSize(17);
      setSwitchCorner(ComputerConfig::SwitchCorner::TopLeft, true);
    }
  };

  Computer namedComputerFromBefore() const { return ConfiguredComputer("third"); }
  Computer namedComputer(const ServerConfig &config, const QString &name) const
  {
    for (const auto &computer : config.computers())
      if (computer.name() == name) return computer;
    return Computer();
  }

  void seedThreeDevices()
  {
    auto &settings = Settings::proxy();
    settings.beginGroup("internalConfig");
    settings.remove("");
    settings.beginWriteArray("screens");
    for (int i = 0; i < 15; ++i) {
      settings.setArrayIndex(i);
      Computer computer;
      if (i == 6) { computer = Computer("source"); computer.markAsServer(); }
      if (i == 7) computer = Computer("target");
      if (i == 8) computer = ConfiguredComputer("third");
      computer.saveSettings(settings);
    }
    settings.endArray();
    settings.endGroup();
    settings.sync();
  }

  QByteArray readGenerated() const
  {
    QFile file(Settings::serverConfigFile());
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
  }
  QTemporaryDir m_files;
  int m_profile = 0;
};

QTEST_MAIN(ScreenLayoutPersistenceTests)
#include "ScreenLayoutPersistenceTests.moc"
