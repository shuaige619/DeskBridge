/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#include "common/Settings.h"
#include "gui/config/ServerConfig.h"

#include <QFile>
#include <QScopeGuard>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <utility>

using deskflow::layout::Direction;
using deskflow::layout::Move;
using deskflow::layout::Screen;
using deskflow::layout::Snapshot;

class ScreenLayoutTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase()
  {
    QVERIFY(m_files.isValid());
    qputenv("XDG_CONFIG_HOME", m_files.filePath("config").toUtf8());
    qputenv("XDG_STATE_HOME", m_files.filePath("state").toUtf8());
    Settings::setStateFile(m_files.filePath("state.conf"));
  }

  void loadsRealLayout_data()
  {
    QTest::addColumn<int>("clientIndex");
    QTest::addColumn<Direction>("direction");
    QTest::newRow("left") << 6 << Direction::Left;
    QTest::newRow("right") << 8 << Direction::Right;
    QTest::newRow("above") << 2 << Direction::Above;
    QTest::newRow("below") << 12 << Direction::Below;
  }

  void loadsRealLayout()
  {
    QFETCH(int, clientIndex);
    QFETCH(Direction, direction);
    loadProfile(5, 3, 7, {clientIndex});
    ServerConfig config;
    const auto layout = config.screenLayout();
    QCOMPARE(layout.serverName, QStringLiteral("source"));
    QCOMPARE(layout.clientName, QStringLiteral("target"));
    QCOMPARE(layout.direction, direction);
    QCOMPARE(layout.protocolVersion, 2);
    QCOMPARE(layout.screens.size(), 2);
    QCOMPARE(layout.revision, deskflow::layout::revisionFor(layout));
    QVERIFY(layout.connectedNames.isEmpty()); // Saved configuration has no live connection state.
    QVERIFY(layout.editable);
    QVERIFY(layout.error.isEmpty());
  }

  void movesThroughAllDirectionsWithoutLosingSettings()
  {
    loadProfile(5, 3, 7, {8}, true);
    ServerConfig config;
    const auto originalComputers = occupied(config);
    const auto originalHotkeys = std::as_const(config).hotkeys();
    QVERIFY(!originalHotkeys.isEmpty());
    QCOMPARE(originalComputers.front().aliases(), QStringList{QStringLiteral("source-alias")});
    QCOMPARE(originalComputers.front().switchCornerSize(), 21);
    const auto persisted = fileBytes();
    for (const auto direction : {Direction::Above, Direction::Left, Direction::Below, Direction::Right}) {
      QString error = QStringLiteral("previous error");
      QVERIFY2(config.setScreenLayout(direction, &error), qPrintable(error));
      QVERIFY(error.isEmpty());
      QCOMPARE(config.screenLayout().direction, direction);
      QCOMPARE(occupied(config), originalComputers);
      QVERIFY(std::as_const(config).hotkeys() == originalHotkeys);
      QCOMPARE(fileBytes(), persisted); // Caller controls the save transaction.
      QString text;
      QTextStream stream(&text);
      stream << config;
      const QString name = direction == Direction::Left ? "left" : direction == Direction::Right ? "right" :
          direction == Direction::Above ? "up" : "down";
      QVERIFY(text.contains(name + " = target"));
    }
  }

  void explicitCommitReloadsMovedPair()
  {
    loadProfile(5, 3, 2, {7}, true);
    ServerConfig config;
    const auto computers = occupied(config);
    const auto hotkeys = std::as_const(config).hotkeys();
    QVERIFY(config.setScreenLayout(Direction::Above));
    config.commit();
    Settings::proxy().sync();
    Settings::save(false);
    ServerConfig restored;
    QCOMPARE(restored.screenLayout().direction, Direction::Above);
    QCOMPARE(occupied(restored), computers);
    QVERIFY(std::as_const(restored).hotkeys() == hotkeys);
  }

  void fitsEveryGridBoundary_data()
  {
    QTest::addColumn<int>("serverIndex");
    QTest::addColumn<int>("clientIndex");
    QTest::addColumn<Direction>("direction");
    QTest::newRow("left-edge") << 5 << 6 << Direction::Left;
    QTest::newRow("right-edge") << 9 << 8 << Direction::Right;
    QTest::newRow("top-edge") << 2 << 7 << Direction::Above;
    QTest::newRow("bottom-edge") << 12 << 7 << Direction::Below;
  }

  void fitsEveryGridBoundary()
  {
    QFETCH(int, serverIndex);
    QFETCH(int, clientIndex);
    QFETCH(Direction, direction);
    loadProfile(5, 3, serverIndex, {clientIndex}, true);
    ServerConfig config;
    const auto original = occupied(config);
    QVERIFY(config.setScreenLayout(direction));
    QCOMPARE(config.screenLayout().direction, direction);
    QCOMPARE(occupied(config), original);
    QCOMPARE(std::as_const(config).computers().size(), 15);
  }

  void rejectsDisconnectedOrAmbiguousLayouts_data()
  {
    QTest::addColumn<int>("serverIndex");
    QTest::addColumn<QList<int>>("clients");
    QTest::newRow("diagonal") << 7 << QList<int>{1};
    QTest::newRow("gap") << 7 << QList<int>{9};
    QTest::newRow("row-wrap") << 4 << QList<int>{5};
    QTest::newRow("disconnected-third") << 7 << QList<int>{6, 14};
    QTest::newRow("no-target") << 7 << QList<int>{};
    QTest::newRow("no-source") << -1 << QList<int>{7, 8};
  }

  void rejectsDisconnectedOrAmbiguousLayouts()
  {
    QFETCH(int, serverIndex);
    QFETCH(QList<int>, clients);
    loadProfile(5, 3, serverIndex, clients);
    ServerConfig config;
    const auto original = config;
    const auto layout = config.screenLayout();
    QVERIFY(!layout.editable);
    QCOMPARE(layout.direction, Direction::Unknown);
    QVERIFY(!layout.error.isEmpty());
    QString error;
    QVERIFY(!config.setScreenLayout(Direction::Left, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(config == original);
  }

  void rejectsUnknownDirectionAndNarrowGrid()
  {
    loadProfile(1, 2, 0, {1});
    ServerConfig config;
    const auto original = config;
    QVERIFY(config.screenLayout().editable);
    QVERIFY(!config.setScreenLayout(Direction::Unknown));
    QVERIFY(!config.setScreenLayout(static_cast<Direction>(99)));
    QVERIFY(!config.setScreenLayout(Direction::Left));
    QVERIFY(config == original);
    QVERIFY(config.setScreenLayout(Direction::Above));
    QCOMPARE(config.screenLayout().direction, Direction::Above);
  }

  void refusesExternalConfiguration()
  {
    loadProfile(5, 3, 7, {8});
    Settings::setValue(Settings::Server::ExternalConfig, true);
    ServerConfig config;
    const auto original = config;
    const auto layout = config.screenLayout();
    QVERIFY(!layout.editable);
    QCOMPARE(layout.direction, Direction::Unknown);
    QVERIFY(!config.setScreenLayout(Direction::Above));
    QVERIFY(config == original);
  }

  void refusesReadOnlyConfiguration()
  {
    loadProfile(5, 3, 7, {8});
    ServerConfig config;
    const auto original = config;
    const auto path = Settings::settingsFile();
    const auto permissions = QFile::permissions(path);
    const auto restore = qScopeGuard([path, permissions] { QFile::setPermissions(path, permissions); });
    QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::ReadUser | QFileDevice::ReadGroup | QFileDevice::ReadOther));
    if (Settings::isWritable()) QSKIP("This environment does not enforce read-only file permissions.");
    const auto layout = config.screenLayout();
    QCOMPARE(layout.direction, Direction::Right);
    QVERIFY(!layout.editable);
    QVERIFY(!config.setScreenLayout(Direction::Below));
    QVERIFY(config == original);
  }

  void loadsAndMovesFullTopologies_data()
  {
    QTest::addColumn<int>("serverIndex");
    QTest::addColumn<QList<int>>("clients");
    QTest::addColumn<QString>("moving");
    QTest::addColumn<int>("column");
    QTest::addColumn<int>("row");
    QTest::newRow("three-devices") << 7 << QList<int>{6, 8} << QString("target-1") << 0 << 1;
    QTest::newRow("four-device-T") << 7 << QList<int>{6, 8, 2} << QString("target-2") << 0 << 1;
    QTest::newRow("four-device-cycle") << 7 << QList<int>{8, 12, 13} << QString("target-2") << 0 << -1;
    QTest::newRow("four-device-chain") << 6 << QList<int>{7, 8, 9} << QString("target-2") << 1 << 1;
  }

  void loadsAndMovesFullTopologies()
  {
    QFETCH(int, serverIndex);
    QFETCH(QList<int>, clients);
    QFETCH(QString, moving);
    QFETCH(int, column);
    QFETCH(int, row);
    loadProfile(5, 3, serverIndex, clients, true);
    ServerConfig config;
    const auto before = config.screenLayout();
    QVERIFY2(before.editable, qPrintable(before.error));
    QCOMPARE(before.screens.size(), clients.size() + 1);
    QCOMPARE(before.direction, Direction::Unknown);
    QVERIFY(before.clientName.isEmpty());
    const auto computers = occupied(config);
    const auto hotkeys = std::as_const(config).hotkeys();
    const auto bytes = fileBytes();
    QString error;
    QVERIFY2(config.moveScreen({moving, column, row, before.revision}, &error), qPrintable(error));
    const auto after = config.screenLayout();
    QVERIFY(deskflow::layout::validateTopology(after));
    QVERIFY(after.revision != before.revision);
    QCOMPARE(occupied(config), computers);
    QVERIFY(std::as_const(config).hotkeys() == hotkeys);
    QCOMPARE(fileBytes(), bytes);
    config.commit();
    Settings::proxy().sync();
    Settings::save(false);
    ServerConfig restored;
    QCOMPARE(restored.screenLayout().revision, after.revision);
    QCOMPARE(occupied(restored), computers);
  }

  void invalidMovesLeaveEveryDeviceAndSettingUntouched()
  {
    loadProfile(5, 3, 6, {7, 8}, true); // source -> target -> target-1.
    ServerConfig config;
    const auto original = config;
    const auto before = config.screenLayout();
    const auto bytes = fileBytes();
    for (const auto &move : QList<Move>{
        {"target-1", 0, 0, before.revision}, // Occupied by the source.
        {"target", 0, 1, before.revision}, // Removing the bridge disconnects target-1.
        {"target-1", 0, 1, QString(64, u'0')},
        {"missing", 0, 1, before.revision},
        {"target-1", 65, 0, before.revision}}) {
      QString error;
      QVERIFY(!config.moveScreen(move, &error));
      QVERIFY(!error.isEmpty());
      QVERIFY(config == original);
      QCOMPARE(fileBytes(), bytes);
    }
    QVERIFY(!config.setScreenLayout(Direction::Left)); // v1 API cannot collapse a multi-device map.
    QVERIFY(config == original);
  }

  void movingSourceReanchorsAndKeepsOtherAbsolutePositions()
  {
    loadProfile(5, 3, 7, {8, 9}, true);
    ServerConfig config;
    const auto before = config.screenLayout();
    const auto computers = occupied(config);
    const Move move{"source", 1, 1, before.revision};
    const auto expected = deskflow::layout::movedSnapshot(before, move);
    QVERIFY(expected);
    QVERIFY(config.moveScreen(move));
    QCOMPARE(config.screenLayout().revision, expected->revision);
    QCOMPARE(occupied(config), computers);
    const auto &grid = std::as_const(config).computers();
    QCOMPARE(grid[8].name(), QStringLiteral("target"));
    QCOMPARE(grid[9].name(), QStringLiteral("target-1"));
    QCOMPARE(grid[13].name(), QStringLiteral("source"));
  }

  void wholeGridShiftsOnlyEnoughToFitAndRejectsOversizedShape()
  {
    loadProfile(5, 3, 0, {1, 2}, true);
    ServerConfig config;
    const auto before = config.screenLayout();
    const auto computers = occupied(config);
    QVERIFY(config.moveScreen({"target-1", 0, -1, before.revision}));
    const auto &grid = std::as_const(config).computers();
    QCOMPARE(grid[0].name(), QStringLiteral("target-1"));
    QCOMPARE(grid[5].name(), QStringLiteral("source"));
    QCOMPARE(grid[6].name(), QStringLiteral("target"));
    QCOMPARE(occupied(config), computers);

    loadProfile(2, 2, 0, {1, 3}, true);
    ServerConfig narrow;
    const auto original = narrow;
    const auto current = narrow.screenLayout();
    // The connected three-wide result cannot fit the existing two-column grid.
    QVERIFY(!narrow.moveScreen({"target-1", 2, 0, current.revision}));
    QVERIFY(narrow == original);
  }

  void revisionTracksOnlyCanonicalTopology()
  {
    auto state = topology({{"source", 0, 0}, {"one", 1, 0}, {"two", 2, 0}});
    auto changed = state;
    std::reverse(changed.screens.begin(), changed.screens.end());
    changed.editable = false;
    changed.error = "offline";
    changed.localName = "two";
    changed.connectedNames = {"source", "one"};
    changed.protocolVersion = 1;
    QCOMPARE(deskflow::layout::revisionFor(changed), state.revision);
    changed.serverName = "one";
    QVERIFY(deskflow::layout::revisionFor(changed) != state.revision);
  }

  void topologyValidationRejectsAmbiguityAndAllowsCycles()
  {
    const auto cycle = topology({{"source", 0, 0}, {"one", 1, 0}, {"two", 1, 1}, {"three", 0, 1}});
    QVERIFY(deskflow::layout::validateTopology(cycle));
    for (const auto &screens : QList<QList<Screen>>{
        {{"source", 0, 0}, {"one", 2, 0}},
        {{"source", 0, 0}, {"one", 0, 0}},
        {{"source", 0, 0}, {"source", 1, 0}},
        {{"source", 1, 0}, {"one", 2, 0}},
        {{"source", 0, 0}, {"one", 65, 0}}}) {
      QString error;
      QVERIFY(!deskflow::layout::validateTopology(topology(screens), &error));
      QVERIFY(!error.isEmpty());
    }
  }

  void topologyCodecChecksRevisionShapeAndBoundedUnicodePayloads()
  {
    using namespace deskflow::layout;
    auto state = topology({{"source", 0, 0}, {"one", 1, 0}, {"two", 2, 0}});
    state.connectedNames = {"source", "one"};
    state.localName = "one";
    const auto encoded = encodeIpc(toJson(state));
    const auto object = decodeIpc(encoded);
    QVERIFY(object);
    QCOMPARE(snapshotFromJson(*object), std::optional<Snapshot>(state));
    const Move move{"two", 0, 1, state.revision};
    QCOMPARE(moveFromJson(moveToJson(move)), std::optional<Move>(move));
    auto invalidMove = moveToJson(move);
    invalidMove.insert("column", 1.5);
    QVERIFY(!moveFromJson(invalidMove));
    invalidMove = moveToJson(move);
    invalidMove.insert("expectedRevision", "invalid");
    QVERIFY(!moveFromJson(invalidMove));

    auto stale = toJson(state);
    stale.insert("revision", QString(64, u'0'));
    QVERIFY(!snapshotFromJson(stale));
    auto disconnected = topology({{"source", 0, 0}, {"one", 2, 0}});
    QVERIFY(!snapshotFromJson(toJson(disconnected)));
    disconnected.editable = false;
    QVERIFY(snapshotFromJson(toJson(disconnected)));
    Snapshot unknown{"source", {}, Direction::Unknown, false, "unsupported custom config"};
    unknown.protocolVersion = 2;
    unknown.localName = "one";
    unknown.connectedNames = {"source", "one"};
    QCOMPARE(snapshotFromJson(toJson(unknown)), std::optional<Snapshot>(unknown));

    QList<Screen> screens{{"source", 0, 0}};
    for (int i = 1; i < kMaxScreens; ++i)
      screens.append({QString(82, QChar(0x6c49)) + QString::number(i), i, 0});
    auto maximum = topology(screens);
    for (const auto &screen : maximum.screens) maximum.connectedNames.append(screen.name);
    QVERIFY(validateTopology(maximum));
    QVERIFY(QJsonDocument(toJson(maximum)).toJson(QJsonDocument::Compact).size() < 96 * 1024);
    QVERIFY(encodeIpc(toJson(maximum)).size() <= kMaxIpcPayloadLength);
    QCOMPARE(snapshotFromJson(toJson(maximum)), std::optional<Snapshot>(maximum));
    maximum.screens.append({"too-many", 64, 0});
    maximum.revision = revisionFor(maximum);
    QVERIFY(!validateTopology(maximum));
    QVERIFY(!snapshotFromJson(toJson(maximum)));
    auto longName = topology({{"source", 0, 0}, {QString(86, QChar(0x6c49)), 1, 0}});
    QVERIFY(!validateTopology(longName));
    QVERIFY(!snapshotFromJson(toJson(longName)));
  }

private:
  Snapshot topology(const QList<Screen> &screens) const
  {
    Snapshot result{"source", {}, Direction::Unknown, true, {}};
    result.protocolVersion = 2;
    result.screens = screens;
    result.localName = "source";
    result.revision = deskflow::layout::revisionFor(result);
    return result;
  }

  void loadProfile(int columns, int rows, int serverIndex, const QList<int> &clients, bool withAttributes = false)
  {
    const auto path = m_files.filePath(QStringLiteral("settings-%1.conf").arg(++m_profileNumber));
    // Initialize the empty modern profile before adding grid entries. Name-only
    // entries in a pre-existing profile are interpreted as legacy arrays.
    Settings::setSettingsFile(path);
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue(Settings::Core::ComputerName, "source");
    settings.setValue(Settings::Server::GridWidth, columns);
    settings.setValue(Settings::Server::GridHeight, rows);
    auto &proxy = Settings::proxy();
    QStringList configuredNames;
    proxy.beginWriteArray("internalConfig/screens");
    for (int i = 0; i < columns * rows; ++i) {
      proxy.setArrayIndex(i);
      QString name;
      if (i == serverIndex) name = "source";
      else if (const auto index = clients.indexOf(i); index != -1)
        name = index == 0 ? "target" : QStringLiteral("target-%1").arg(index);
      Computer(name).saveSettings(proxy);
      if (!name.isEmpty()) configuredNames.append(name);
    }
    proxy.endArray();
    if (withAttributes) {
      for (const auto &name : configuredNames) {
        settings.setValue(Settings::Computer::Aliases.arg(name), QStringList{name + "-alias"});
        settings.setValue(Settings::Computer::SwitchCornerSize.arg(name), 21);
        settings.setValue(Settings::Computer::SwitchCornerTopLeft.arg(name), true);
        settings.setValue(Settings::Computer::HalfDuplexCapsLock.arg(name), true);
        settings.setValue(Settings::Computer::ModifierCtrl.arg(name), "super");
      }
      Hotkey hotkey;
      KeySequence sequence;
      sequence.appendKey(Qt::ControlModifier, Qt::Key_F1);
      hotkey.setKeySequence(sequence);
      settings.beginWriteArray("internalConfig/hotkeys", 1);
      settings.setArrayIndex(0);
      hotkey.saveSettings(settings);
      settings.endArray();
    }
    settings.sync();
    proxy.sync();
    Settings::save(false);
  }

  QList<Computer> occupied(const ServerConfig &config) const
  {
    QList<Computer> result;
    for (const auto &computer : config.computers())
      if (!computer.isNull()) result.append(computer);
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) { return a.name() < b.name(); });
    return result;
  }

  QByteArray fileBytes() const
  {
    QFile file(Settings::settingsFile());
    return file.open(QFile::ReadOnly) ? file.readAll() : QByteArray();
  }

  QTemporaryDir m_files;
  int m_profileNumber = 0;
};

QTEST_MAIN(ScreenLayoutTests)
#include "ScreenLayoutTests.moc"
