/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#include "ShortcutApplyControllerTests.h"
#include "common/Settings.h"
#include "common/ShortcutRules.h"
#include "gui/personal/ShortcutApplyController.h"
#include <QSignalSpy>
#include <QtTest>
#include <memory>

using namespace deskflow;
using gui::personal::ShortcutApplyController;

namespace {
struct Scenario
{
  shortcuts::Configuration stored = shortcuts::defaults();
  core::ProcessState process = core::ProcessState::Stopped;
  core::ConnectionState connection = core::ConnectionState::Disconnected;
  int starts = 0;
  int stops = 0;
  int writes = 0;
  std::function<void()> onStart;
  std::function<bool(const shortcuts::Configuration &, QString *)> onWrite;
  std::unique_ptr<ShortcutApplyController> controller;

  explicit Scenario(int timeout = 1000)
  {
    ShortcutApplyController::Operations operations{
        [this] { return stored; },
        [this](const shortcuts::Configuration &config, QString *error) {
          ++writes;
          if (onWrite && !onWrite(config, error))
            return false;
          stored = config;
          return true;
        },
        [this] { return process; }, [this] { return connection; },
        [this] {
          ++starts;
          process = core::ProcessState::Started;
          controller->notifyProcessState(process);
          if (onStart)
            onStart();
        },
        [this] {
          ++stops;
          process = core::ProcessState::Stopped;
          connection = core::ConnectionState::Disconnected;
          controller->notifyProcessState(process);
        }
    };
    controller = std::make_unique<ShortcutApplyController>(std::move(operations), nullptr, timeout);
  }
  void connected()
  {
    process = core::ProcessState::Started;
    connection = core::ConnectionState::Connected;
  }
  void reconnected()
  {
    connection = core::ConnectionState::Connected;
    controller->notifyConnectionState(connection);
  }
  shortcuts::Configuration changed() const
  {
    auto config = stored;
    config.rules.first().output.key = 'x';
    return config;
  }
};
} // namespace

void ShortcutApplyControllerTests::initTestCase()
{
  QVERIFY(m_files.isValid());
  Settings::setSettingsFile(m_files.filePath("settings.conf"));
  Settings::setStateFile(m_files.filePath("state.conf"));
  Settings::save(false);
}

void ShortcutApplyControllerTests::disconnectedApplyDoesNotStartSharing()
{
  Scenario scene;
  const auto requested = scene.changed();
  QSignalSpy result(scene.controller.get(), &ShortcutApplyController::finished);
  scene.controller->apply(requested);
  QTRY_COMPARE(result.count(), 1);
  QVERIFY(result.first().first().toBool());
  QVERIFY(scene.stored == requested);
  QCOMPARE(scene.starts, 0);
  QCOMPARE(scene.stops, 0);
}

void ShortcutApplyControllerTests::connectedApplyWaitsForReconnect()
{
  Scenario scene;
  scene.connected();
  QSignalSpy result(scene.controller.get(), &ShortcutApplyController::finished);
  scene.controller->apply(scene.changed());
  QTRY_COMPARE(scene.starts, 1);
  QCOMPARE(scene.stops, 1);
  QCOMPARE(result.count(), 0);
  QVERIFY(scene.controller->isApplying());
  scene.reconnected();
  QCOMPARE(result.count(), 1);
  QVERIFY(result.first().first().toBool());
  QVERIFY(!scene.controller->isApplying());
}

void ShortcutApplyControllerTests::startFailureRestoresPreviousRules()
{
  Scenario scene;
  scene.connected();
  const auto previous = scene.stored;
  scene.onStart = [&] {
    if (scene.starts == 1) {
      scene.process = core::ProcessState::Stopped;
      scene.controller->notifyStartFailure();
    } else {
      scene.reconnected();
    }
  };
  QSignalSpy result(scene.controller.get(), &ShortcutApplyController::finished);
  scene.controller->apply(scene.changed());
  QTRY_COMPARE(result.count(), 1);
  QVERIFY(!result.first().first().toBool());
  QVERIFY(scene.stored == previous);
  QCOMPARE(scene.starts, 2);
  QCOMPARE(scene.connection, core::ConnectionState::Connected);
}

void ShortcutApplyControllerTests::retryPendingApplySavesAndResumesSharing()
{
  Scenario scene;
  scene.process = core::ProcessState::RetryPending;
  const auto requested = scene.changed();
  QSignalSpy result(scene.controller.get(), &ShortcutApplyController::finished);
  scene.controller->apply(requested);
  QTRY_COMPARE(result.count(), 1);
  QVERIFY(result.first().first().toBool());
  QVERIFY(scene.stored == requested);
  QCOMPARE(scene.stops, 1);
  QCOMPARE(scene.starts, 1);
  QVERIFY(!scene.controller->isApplying());
}

void ShortcutApplyControllerTests::connectionTimeoutRestoresPreviousRules()
{
  Scenario scene(25);
  scene.connected();
  const auto previous = scene.stored;
  scene.onStart = [&] {
    if (scene.starts == 2)
      scene.reconnected();
  };
  QSignalSpy result(scene.controller.get(), &ShortcutApplyController::finished);
  scene.controller->apply(scene.changed());
  QTRY_COMPARE(result.count(), 1);
  QVERIFY(!result.first().first().toBool());
  QVERIFY(scene.stored == previous);
  QCOMPARE(scene.starts, 2);
  QCOMPARE(scene.stops, 2);
}

void ShortcutApplyControllerTests::failedWriteKeepsPreviousRulesAndRestarts()
{
  Scenario scene;
  scene.connected();
  const auto previous = scene.stored;
  scene.onWrite = [](const auto &, QString *error) { *error = "test write failure"; return false; };
  scene.onStart = [&] { scene.reconnected(); };
  QSignalSpy result(scene.controller.get(), &ShortcutApplyController::finished);
  scene.controller->apply(scene.changed());
  QTRY_COMPARE(result.count(), 1);
  QVERIFY(!result.first().first().toBool());
  QVERIFY(scene.stored == previous);
  QCOMPARE(scene.writes, 1);
  QCOMPARE(scene.starts, 1);
}

void ShortcutApplyControllerTests::duplicateApplyDoesNotStartSecondTransaction()
{
  Scenario scene;
  scene.connected();
  const auto requested = scene.changed();
  QSignalSpy result(scene.controller.get(), &ShortcutApplyController::finished);
  scene.controller->apply(requested);
  scene.controller->apply(scene.stored);
  QTRY_COMPARE(scene.starts, 1);
  scene.reconnected();
  QCOMPARE(result.count(), 1);
  QCOMPARE(scene.writes, 1);
  QVERIFY(scene.stored == requested);
}

void ShortcutApplyControllerTests::invalidRulesDoNotStopSharing()
{
  Scenario scene;
  scene.connected();
  auto requested = scene.changed();
  requested.rules.first().input = {' ', KeyModifierControl};
  QSignalSpy result(scene.controller.get(), &ShortcutApplyController::finished);
  scene.controller->apply(requested);
  QCOMPARE(result.count(), 1);
  QVERIFY(!result.first().first().toBool());
  QCOMPARE(scene.stops, 0);
  QCOMPARE(scene.writes, 0);
}

QTEST_MAIN(ShortcutApplyControllerTests)
