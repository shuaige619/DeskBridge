/*
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "CoreProcessStopTests.h"

// Friend access seeds in-memory lifecycle states without starting a subprocess,
// connecting IPC or injecting any system input. Rewriting private as public
// would change the decorated method names on MSVC and break library linking.
#include "common/Enums.h"
#include "common/Settings.h"
#include "common/ShortcutRules.h"
#include "gui/FileTail.h"
#include "gui/config/ServerConfig.h"
#include <QMutex>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include "gui/core/CoreProcess.h"

#include <QCoreApplication>
#include <QFile>
#include <QSignalSpy>
#include <QTest>

using deskflow::core::ConnectionState;
using deskflow::core::ProcessState;
using deskflow::gui::CoreProcess;

void CoreProcessStopTests::initTestCase()
{
  QVERIFY(m_files.isValid());
  qputenv("XDG_CONFIG_HOME", (m_files.path() + "/config").toUtf8());
  qputenv("XDG_STATE_HOME", (m_files.path() + "/state").toUtf8());
  Settings::setSettingsFile(m_files.filePath(QStringLiteral("settings.conf")));
  Settings::setStateFile(m_files.filePath(QStringLiteral("state.conf")));
  Settings::save(false);
}

void CoreProcessStopTests::stopWithoutLiveProcess_data()
{
  QTest::addColumn<ProcessState>("initial");
  QTest::addColumn<bool>("hasProcess");
  for (const auto [name, state] : {
           std::pair{"retry-pending", ProcessState::RetryPending}, std::pair{"started", ProcessState::Started},
           std::pair{"starting", ProcessState::Starting}, std::pair{"stopping", ProcessState::Stopping},
           std::pair{"already-stopped", ProcessState::Stopped}
       }) {
    QTest::newRow((QByteArray(name) + "-null").constData()) << state << false;
    QTest::newRow((QByteArray(name) + "-not-running").constData()) << state << true;
  }
}

void CoreProcessStopTests::stopWithoutLiveProcess()
{
  QFETCH(ProcessState, initial);
  QFETCH(bool, hasProcess);
  ServerConfig config;
  const auto expectedBinary = QStringLiteral("%1/%2").arg(QCoreApplication::applicationDirPath(), kCoreBinName);
  if (!QFile::exists(expectedBinary))
    QTest::ignoreMessage(QtCriticalMsg, "core server binary does not exist");
  CoreProcess process(config);
  process.m_processState = initial;
  process.m_connectionState = ConnectionState::Connecting;
  if (hasProcess)
    process.m_process = new QProcess(&process); // Deliberately never started.

  // Disconnect the launch callback so even a regression in timer cancellation
  // cannot start a real core. Observe the actual QTimer's state and emissions.
  QObject::disconnect(&process.m_retryTimer, nullptr, &process, nullptr);
  QSignalSpy retries(&process.m_retryTimer, &QTimer::timeout);
  QSignalSpy states(&process, &CoreProcess::processStateChanged);
  QSignalSpy connections(&process, &CoreProcess::connectionStateChanged);
  process.m_retryTimer.setSingleShot(true);
  process.m_retryTimer.start(5);
  QVERIFY(process.m_retryTimer.isActive());

  process.stop(Settings::ProcessMode::Desktop);
  QCOMPARE(process.processState(), ProcessState::Stopped);
  QCOMPARE(process.connectionState(), ConnectionState::Disconnected);
  QVERIFY(!process.m_retryTimer.isActive());
  QCOMPARE(states.count(), initial == ProcessState::Stopped ? 0 : 1);
  if (!states.isEmpty())
    QCOMPARE(states.last().first().value<ProcessState>(), ProcessState::Stopped);
  QCOMPARE(connections.count(), 1);
  if (process.m_process)
    QCOMPARE(process.m_process->state(), QProcess::NotRunning);

  QTest::qWait(20);
  QCOMPARE(retries.count(), 0);
  QCOMPARE(process.processState(), ProcessState::Stopped);
}

void CoreProcessStopTests::sourceMetadataLogs()
{
  ServerConfig config;
  const auto binary = QStringLiteral("%1/%2").arg(QCoreApplication::applicationDirPath(), kCoreBinName);
  if (!QFile::exists(binary))
    QTest::ignoreMessage(QtCriticalMsg, "core server binary does not exist");
  CoreProcess process(config);
  using deskflow::shortcuts::DeviceOS;
  QCOMPARE(process.sourceOperatingSystem(), DeviceOS::Unknown);
  QVERIFY(!process.serverModifierMapping());
  QSignalSpy sources(&process, &CoreProcess::sourceOperatingSystemChanged);
  QSignalSpy modifiers(&process, &CoreProcess::serverModifierMappingChanged);
  for (const auto os : {DeviceOS::MacOS, DeviceOS::Windows, DeviceOS::Linux, DeviceOS::Unknown}) {
    const auto line = QStringLiteral("[2026-10-19] INFO: DeskBridge source OS: %1")
                          .arg(deskflow::shortcuts::deviceOSName(os));
    process.handleLogLines(line);
    QCOMPARE(process.sourceOperatingSystem(), os);
    const auto count = sources.count();
    process.handleLogLines(line); // Identical announcements do not cause UI churn.
    QCOMPARE(sources.count(), count);
  }
  QCOMPARE(sources.count(), 4);
  process.handleLogLines(QStringLiteral("INFO: DeskBridge modifier remap: active\n"));
  QVERIFY(process.serverModifierMapping());
  process.handleLogLines(QStringLiteral("INFO: DeskBridge modifier remap: invalid\n"));
  QVERIFY(process.serverModifierMapping());
  process.handleLogLines(QStringLiteral("INFO: DeskBridge modifier remap: none\n"));
  QVERIFY(!process.serverModifierMapping());
  QCOMPARE(modifiers.count(), 2);
  process.handleLogLines(QStringLiteral("INFO: DeskBridge source OS: windows\n"));
  process.handleLogLines(QStringLiteral("INFO: DeskBridge source OS: windows-future\n"));
  QCOMPARE(process.sourceOperatingSystem(), DeviceOS::Unknown);
}

void CoreProcessStopTests::sourceMetadataChunkBoundaries()
{
  ServerConfig config;
  const auto binary = QStringLiteral("%1/%2").arg(QCoreApplication::applicationDirPath(), kCoreBinName);
  if (!QFile::exists(binary))
    QTest::ignoreMessage(QtCriticalMsg, "core server binary does not exist");
  CoreProcess process(config);
  using deskflow::shortcuts::DeviceOS;
  const QByteArray line("INFO: DeskBridge source OS: windows\r\n");
  for (int boundary = 1; boundary < line.size() - 1; ++boundary) {
    process.setConnectionState(ConnectionState::Connecting);
    process.handleProcessOutput(line.left(boundary), process.m_stdoutPending);
    QCOMPARE(process.sourceOperatingSystem(), DeviceOS::Unknown);
    process.handleProcessOutput(line.mid(boundary), process.m_stdoutPending);
    QCOMPARE(process.sourceOperatingSystem(), DeviceOS::Windows);
    QVERIFY(process.m_stdoutPending.isEmpty());
  }
  process.setConnectionState(ConnectionState::Disconnected);
  process.handleProcessOutput("INFO: DeskBridge source ", process.m_stdoutPending);
  process.handleProcessOutput("OS: macos\n", process.m_stderrPending);
  QCOMPARE(process.sourceOperatingSystem(), DeviceOS::Unknown); // Never join channels.
  process.handleProcessOutput("OS: linux\nINFO: DeskBridge modifier remap: active\n", process.m_stdoutPending);
  QCOMPARE(process.sourceOperatingSystem(), DeviceOS::Linux);
  QVERIFY(process.serverModifierMapping());
}

void CoreProcessStopTests::sourceMetadataResets_data()
{
  QTest::addColumn<ConnectionState>("state");
  QTest::newRow("connecting") << ConnectionState::Connecting;
  QTest::newRow("disconnected") << ConnectionState::Disconnected;
}

void CoreProcessStopTests::sourceMetadataResets()
{
  QFETCH(ConnectionState, state);
  ServerConfig config;
  const auto binary = QStringLiteral("%1/%2").arg(QCoreApplication::applicationDirPath(), kCoreBinName);
  if (!QFile::exists(binary))
    QTest::ignoreMessage(QtCriticalMsg, "core server binary does not exist");
  CoreProcess process(config);
  using deskflow::shortcuts::DeviceOS;
  process.m_connectionState = state; // Reset must work even if the enum did not change.
  process.handleLogLines(QStringLiteral("INFO: DeskBridge source OS: macos\nDeskBridge modifier remap: active\n"));
  process.m_stdoutPending = QStringLiteral("DeskBridge source OS: win");
  process.m_stderrPending = QStringLiteral("DeskBridge modifier remap: ac");
  process.setConnectionState(state);
  QCOMPARE(process.sourceOperatingSystem(), DeviceOS::Unknown);
  QVERIFY(!process.serverModifierMapping());
  QVERIFY(process.m_stdoutPending.isEmpty());
  QVERIFY(process.m_stderrPending.isEmpty());
}

QTEST_GUILESS_MAIN(CoreProcessStopTests)

void CoreProcessStopTests::layoutServerStopRollsBackPendingApply()
{
  using D = deskflow::layout::Direction;
  ServerConfig config;
  const auto binary = QStringLiteral("%1/%2").arg(QCoreApplication::applicationDirPath(), kCoreBinName);
  if (!QFile::exists(binary)) QTest::ignoreMessage(QtCriticalMsg, "core server binary does not exist");
  CoreProcess process(config);
  process.m_mode = Settings::CoreMode::Server;
  process.m_processState = ProcessState::Started;
  process.m_screenLayout = {"source", "client", D::Right, true, {}};
  QSignalSpy request(&process, &CoreProcess::layoutRequestReceived);
  QSignalSpy applied(&process, &CoreProcess::layoutApplyFinished);
  process.onCoreIpcMessageReceived("layoutRequestReceived", deskflow::layout::encodeIpc(
      {{"id", 42}, {"expected", 2}, {"desired", 3}}));
  QCOMPARE(request.size(), 1);
  QVERIFY(process.m_serverLayoutTransaction.has_value());
  process.setProcessState(ProcessState::Stopped);
  QCOMPARE(applied.size(), 1);
  QCOMPARE(applied.at(0).at(0).toUInt(), quint32(42));
  QCOMPARE(applied.at(0).at(1).toBool(), false);
  QVERIFY(!process.m_serverLayoutTransaction);
}

void CoreProcessStopTests::layoutReconciliationRequiresFreshConfirmedQuery_data()
{
  QTest::addColumn<bool>("applied");
  QTest::newRow("runtime-already-applied") << true;
  QTest::newRow("runtime-never-applied") << false;
}

void CoreProcessStopTests::layoutReconciliationRequiresFreshConfirmedQuery()
{
  QFETCH(bool, applied);
  using D = deskflow::layout::Direction;
  ServerConfig config;
  const auto binary = QStringLiteral("%1/%2").arg(QCoreApplication::applicationDirPath(), kCoreBinName);
  if (!QFile::exists(binary)) QTest::ignoreMessage(QtCriticalMsg, "core server binary does not exist");
  CoreProcess process(config);
  process.m_mode = Settings::CoreMode::Server;
  process.m_processState = ProcessState::Started;
  process.m_screenLayout = {"source", "client", D::Right, true, {}};
  QSignalSpy outcome(&process, &CoreProcess::layoutApplyFinished);
  QSignalSpy uncertain(&process, &CoreProcess::layoutSynchronizationUncertain);
  process.onCoreIpcMessageReceived("layoutRequestReceived", deskflow::layout::encodeIpc(
      {{"id", 17}, {"expected", 2}, {"desired", 3}}));
  process.onCoreIpcConnectionFailed();
  QCOMPARE(uncertain.size(), 1);
  QVERIFY(outcome.isEmpty());
  const deskflow::layout::Snapshot actual{"source", "client", applied ? D::Above : D::Right, true, {}};
  // A cached/replayed broadcast must never decide whether disk gets rolled back.
  process.onCoreIpcMessageReceived("screenLayout", deskflow::layout::encodeIpc(deskflow::layout::toJson(actual)));
  QVERIFY(outcome.isEmpty());
  process.m_layoutQueryId = 9;
  process.onCoreIpcMessageReceived("layoutQueryResult", deskflow::layout::encodeIpc(
      {{"id", 8}, {"snapshot", deskflow::layout::toJson(actual)}}));
  QVERIFY(outcome.isEmpty());
  process.onCoreIpcMessageReceived("layoutQueryResult", deskflow::layout::encodeIpc(
      {{"id", 9}, {"snapshot", deskflow::layout::toJson(actual)}}));
  QCOMPARE(outcome.size(), 1);
  QCOMPARE(outcome.at(0).at(0).toUInt(), quint32(17));
  QCOMPARE(outcome.at(0).at(1).toBool(), applied);
  QVERIFY(!process.m_serverLayoutTransaction);
  QVERIFY(!process.m_layoutReconcileNeeded);
}

void CoreProcessStopTests::topologyReconciliationUsesWholeGraphRevision()
{
  using namespace deskflow::layout;
  ServerConfig config;
  const auto binary = QStringLiteral("%1/%2").arg(QCoreApplication::applicationDirPath(), kCoreBinName);
  if (!QFile::exists(binary)) QTest::ignoreMessage(QtCriticalMsg, "core server binary does not exist");
  CoreProcess process(config);
  process.m_mode = Settings::CoreMode::Server;
  process.m_processState = ProcessState::Started;
  Snapshot original{"source", {}, deskflow::layout::Direction::Unknown, true, {}};
  original.screens = {{"source", 0, 0}, {"client", 1, 0}, {"third", 1, 1}};
  original.protocolVersion = 2;
  original.localName = "source";
  original.connectedNames = {"source", "client"};
  original.revision = revisionFor(original);
  process.m_screenLayout = original;
  const Move move{"third", 0, 1, original.revision};
  const auto desired = movedSnapshot(original, move);
  QVERIFY(desired);
  QSignalSpy moves(&process, &CoreProcess::layoutMoveReceived);
  QSignalSpy outcomes(&process, &CoreProcess::layoutApplyFinished);
  QSignalSpy uncertain(&process, &CoreProcess::layoutSynchronizationUncertain);
  process.onCoreIpcMessageReceived("layoutMoveReceived", encodeIpc({{"id", 31}, {"move", moveToJson(move)}}));
  QCOMPARE(moves.size(), 1);
  QCOMPARE(qvariant_cast<Move>(moves.at(0).at(1)), move);
  process.onCoreIpcConnectionFailed();
  process.m_layoutQueryId = 17;
  // An unrelated whole-graph change is unknown even though pair direction
  // remains Unknown in all three snapshots.
  Snapshot unrelated = original;
  unrelated.screens = {{"source", 0, 0}, {"client", -1, 0}, {"third", -1, 1}};
  unrelated.revision = revisionFor(unrelated);
  process.onCoreIpcMessageReceived("layoutQueryResult", encodeIpc({{"id", 17}, {"snapshot", toJson(unrelated)}}));
  QVERIFY(outcomes.isEmpty());
  QVERIFY(process.m_serverLayoutTransaction);
  QVERIFY(uncertain.size() >= 2);
  auto actual = *desired;
  actual.connectedNames = {"source"}; // Status changes do not invalidate a move.
  process.onCoreIpcMessageReceived("layoutQueryResult", encodeIpc({{"id", 17}, {"snapshot", toJson(actual)}}));
  QCOMPARE(outcomes.size(), 1);
  QCOMPARE(outcomes.at(0).at(0).toUInt(), quint32(31));
  QCOMPARE(outcomes.at(0).at(1).toBool(), true);
  QVERIFY(!process.m_serverLayoutTransaction);
}
