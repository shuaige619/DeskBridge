/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#include "common/Settings.h"
#include "gui/personal/PreferencesApplyController.h"
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

using namespace deskflow;
using gui::personal::PreferencesApplyController;

namespace {
struct Scenario
{
  QVariantMap stored{{Settings::Core::Port, 24800}, {Settings::Gui::Autohide, false}};
  core::ProcessState process = core::ProcessState::Stopped;
  core::ConnectionState connection = core::ConnectionState::Disconnected;
  int starts = 0;
  int stops = 0;
  int writes = 0;
  int restores = 0;
  bool deferStop = false;
  bool failWrite = false;
  bool partialWrite = false;
  bool failRestore = false;
  std::function<void()> onStart;
  std::unique_ptr<PreferencesApplyController> controller;

  explicit Scenario(int timeout = 1000)
  {
    PreferencesApplyController::Operations operations{
        [this](const QStringList &keys) {
          QVariantMap previous;
          for (const auto &key : keys)
            previous.insert(key, stored.value(key)); // Preserve absent keys as invalid values.
          return previous;
        },
        [this](const QVariantMap &values, QString *error) {
          ++writes;
          if (failWrite) {
            if (partialWrite)
              store(values);
            *error = QStringLiteral("模拟原子写入失败");
            return false;
          }
          store(values);
          return true;
        },
        [this](const QVariantMap &values, QString *error) {
          ++restores;
          if (failRestore) {
            *error = QStringLiteral("模拟恢复失败");
            return false;
          }
          store(values);
          return true;
        },
        [this] { return process; },
        [this] { return connection; },
        [this] {
          ++starts;
          process = core::ProcessState::Started;
          controller->notifyProcessState(process);
          if (onStart)
            onStart();
        },
        [this] {
          ++stops;
          if (deferStop)
            process = core::ProcessState::Stopping;
          else
            stopped();
        }
    };
    controller = std::make_unique<PreferencesApplyController>(std::move(operations), nullptr, timeout);
  }

  void store(const QVariantMap &values)
  {
    for (auto it = values.begin(); it != values.end(); ++it) {
      if (it.value().isValid())
        stored.insert(it.key(), it.value());
      else
        stored.remove(it.key());
    }
  }

  void connected()
  {
    process = core::ProcessState::Started;
    connection = core::ConnectionState::Connected;
  }

  void stopped()
  {
    process = core::ProcessState::Stopped;
    connection = core::ConnectionState::Disconnected;
    controller->notifyProcessState(process);
  }

  void reconnected()
  {
    connection = core::ConnectionState::Connected;
    controller->notifyConnectionState(connection);
  }

  QVariantMap changed() const { return {{Settings::Core::Port, 24801}}; }
};
} // namespace

class PreferencesApplyControllerTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase()
  {
    QVERIFY(m_files.isValid());
    Settings::setSettingsFile(m_files.filePath(QStringLiteral("settings.conf")));
    Settings::setStateFile(m_files.filePath(QStringLiteral("state.conf")));
    Settings::setValue(Settings::Core::Port, 24800);
    Settings::setValue(Settings::Core::Interface, QStringLiteral(""));
    Settings::save(false);
  }

  void uiAndLogChangesDoNotRestart_data()
  {
    QTest::addColumn<QString>("key");
    QTest::addColumn<QVariant>("value");
    QTest::newRow("window") << Settings::Gui::Autohide << QVariant(true);
    QTest::newRow("level") << Settings::Log::Level << QVariant(QStringLiteral("DEBUG"));
    QTest::newRow("gui-debug") << Settings::Log::GuiDebug << QVariant(true);
  }

  void uiAndLogChangesDoNotRestart()
  {
    QFETCH(QString, key);
    QFETCH(QVariant, value);
    Scenario scene;
    scene.connected();
    QSignalSpy result(scene.controller.get(), &PreferencesApplyController::finished);
    scene.controller->apply({{key, value}});
    QCOMPARE(result.count(), 1);
    QVERIFY(result.first().first().toBool());
    QCOMPARE(scene.stored.value(key), value);
    QCOMPARE(scene.writes, 1);
    QCOMPARE(scene.starts, 0);
    QCOMPARE(scene.stops, 0);
  }

  void offlineSaveDoesNotStartSharing()
  {
    Scenario scene;
    QSignalSpy result(scene.controller.get(), &PreferencesApplyController::finished);
    scene.controller->apply(scene.changed());
    QCOMPARE(result.count(), 1);
    QVERIFY(result.first().first().toBool());
    QCOMPARE(scene.stored.value(Settings::Core::Port).toInt(), 24801);
    QCOMPARE(scene.starts, 0);
    QCOMPARE(scene.stops, 0);
  }

  void waitsForStoppedBeforeCommitting()
  {
    Scenario scene;
    scene.connected();
    scene.deferStop = true;
    QSignalSpy result(scene.controller.get(), &PreferencesApplyController::finished);
    scene.controller->apply(scene.changed());
    QTRY_COMPARE(scene.stops, 1);
    QCOMPARE(scene.writes, 0);
    QCOMPARE(scene.starts, 0);
    QCOMPARE(scene.stored.value(Settings::Core::Port).toInt(), 24800);
    scene.stopped();
    QTRY_COMPARE(scene.starts, 1);
    QCOMPARE(scene.writes, 1);
    QCOMPARE(result.count(), 0);
    QVERIFY(scene.controller->isApplying());
    scene.reconnected();
    QCOMPARE(result.count(), 1);
    QVERIFY(result.first().first().toBool());
  }

  void atomicWriteFailureRestartsOriginalService()
  {
    Scenario scene;
    scene.connected();
    const auto previous = scene.stored;
    scene.failWrite = true;
    scene.onStart = [&] { scene.reconnected(); };
    QSignalSpy result(scene.controller.get(), &PreferencesApplyController::finished);
    scene.controller->apply(scene.changed());
    QTRY_COMPARE(result.count(), 1);
    QVERIFY(!result.first().first().toBool());
    QCOMPARE(scene.stored, previous);
    QCOMPARE(scene.writes, 1);
    QCOMPARE(scene.restores, 1);
    QCOMPARE(scene.starts, 1);
    QCOMPARE(scene.connection, core::ConnectionState::Connected);
  }

  void startFailureRollsBackOnlyChangedKeys()
  {
    Scenario scene;
    scene.connected();
    const auto previousPort = scene.stored.value(Settings::Core::Port);
    scene.onStart = [&] {
      if (scene.starts == 1) {
        // Simulate an unrelated preference saved by a different owner.
        scene.stored.insert(Settings::Gui::Autohide, true);
        scene.process = core::ProcessState::Stopped;
        scene.controller->notifyStartFailure();
      } else {
        scene.reconnected();
      }
    };
    QSignalSpy result(scene.controller.get(), &PreferencesApplyController::finished);
    scene.controller->apply(scene.changed());
    QTRY_COMPARE(result.count(), 1);
    QVERIFY(!result.first().first().toBool());
    QCOMPARE(scene.stored.value(Settings::Core::Port), previousPort);
    QVERIFY(scene.stored.value(Settings::Gui::Autohide).toBool());
    QCOMPARE(scene.restores, 1);
    QCOMPARE(scene.starts, 2);
  }

  void failedWriteRollsBackPartialChangesBeforeRestarting()
  {
    Scenario scene;
    scene.connected();
    const auto previous = scene.stored;
    scene.failWrite = true;
    scene.partialWrite = true;
    scene.onStart = [&] {
      QCOMPARE(scene.stored, previous);
      scene.reconnected();
    };
    QSignalSpy result(scene.controller.get(), &PreferencesApplyController::finished);
    scene.controller->apply(scene.changed());
    QTRY_COMPARE(result.count(), 1);
    QVERIFY(!result.first().first().toBool());
    QCOMPARE(scene.stored, previous);
    QCOMPARE(scene.restores, 1);
    QCOMPARE(scene.starts, 1);
  }

  void failedWriteAndFailedRollbackKeepServiceStopped()
  {
    Scenario scene;
    scene.connected();
    scene.failWrite = true;
    scene.partialWrite = true;
    scene.failRestore = true;
    QSignalSpy result(scene.controller.get(), &PreferencesApplyController::finished);
    scene.controller->apply(scene.changed());
    QTRY_COMPARE(result.count(), 1);
    QVERIFY(!result.first().first().toBool());
    QVERIFY(result.first().last().toString().contains(QStringLiteral("恢复设置失败")));
    QCOMPARE(scene.restores, 1);
    QCOMPARE(scene.starts, 0);
    QCOMPARE(scene.process, core::ProcessState::Stopped);
  }

  void offlineFailedWriteRollsBackPartialChanges()
  {
    Scenario scene;
    const auto previous = scene.stored;
    scene.failWrite = true;
    scene.partialWrite = true;
    QSignalSpy result(scene.controller.get(), &PreferencesApplyController::finished);
    scene.controller->apply(scene.changed());
    QCOMPARE(result.count(), 1);
    QVERIFY(!result.first().first().toBool());
    QCOMPARE(scene.stored, previous);
    QCOMPARE(scene.restores, 1);
    QCOMPARE(scene.starts, 0);
    QCOMPARE(scene.stops, 0);
  }

  void rollbackRemovesPreviouslyAbsentKeys()
  {
    Scenario scene;
    scene.connected();
    QVERIFY(!scene.stored.contains(Settings::Core::Interface));
    scene.onStart = [&] {
      if (scene.starts == 1) {
        scene.process = core::ProcessState::Stopped;
        scene.controller->notifyStartFailure();
      } else {
        scene.reconnected();
      }
    };
    QSignalSpy result(scene.controller.get(), &PreferencesApplyController::finished);
    scene.controller->apply({{Settings::Core::Interface, QStringLiteral("192.0.2.1")}});
    QTRY_COMPARE(result.count(), 1);
    QVERIFY(!result.first().first().toBool());
    QVERIFY(!scene.stored.contains(Settings::Core::Interface));
  }

  void reconnectTimeoutRestoresPreviousSettings()
  {
    Scenario scene(25);
    scene.connected();
    const auto previous = scene.stored;
    scene.onStart = [&] {
      if (scene.starts == 2)
        scene.reconnected();
    };
    QSignalSpy result(scene.controller.get(), &PreferencesApplyController::finished);
    scene.controller->apply(scene.changed());
    QTRY_COMPARE(result.count(), 1);
    QVERIFY(!result.first().first().toBool());
    QCOMPARE(scene.stored, previous);
    QCOMPARE(scene.restores, 1);
    QCOMPARE(scene.starts, 2);
    QCOMPARE(scene.stops, 2);
  }

  void restoreFailureDoesNotStartCandidateAgain()
  {
    Scenario scene;
    scene.connected();
    scene.failRestore = true;
    scene.onStart = [&] {
      scene.process = core::ProcessState::Stopped;
      scene.controller->notifyStartFailure();
    };
    QSignalSpy result(scene.controller.get(), &PreferencesApplyController::finished);
    scene.controller->apply(scene.changed());
    QTRY_COMPARE(result.count(), 1);
    QVERIFY(!result.first().first().toBool());
    QVERIFY(result.first().last().toString().contains(QStringLiteral("恢复设置失败")));
    QCOMPARE(scene.starts, 1);
    QCOMPARE(scene.restores, 1);
  }

  void duplicateApplyDoesNotReplaceTransaction()
  {
    Scenario scene;
    scene.connected();
    QSignalSpy result(scene.controller.get(), &PreferencesApplyController::finished);
    scene.controller->apply(scene.changed());
    scene.controller->apply({{Settings::Core::Port, 24802}});
    QTRY_COMPARE(scene.starts, 1);
    scene.reconnected();
    QCOMPARE(result.count(), 1);
    QCOMPARE(scene.writes, 1);
    QCOMPARE(scene.stored.value(Settings::Core::Port).toInt(), 24801);
  }

  void unchangedCoreValueDoesNotRestart()
  {
    Scenario scene;
    scene.connected();
    QSignalSpy result(scene.controller.get(), &PreferencesApplyController::finished);
    scene.controller->apply({{Settings::Core::Port, 24800}, {Settings::Gui::Autohide, true}});
    QCOMPARE(result.count(), 1);
    QVERIFY(result.first().first().toBool());
    QCOMPARE(scene.writes, 1);
    QCOMPARE(scene.stops, 0);
    QCOMPARE(scene.starts, 0);
  }

  void retryPendingApplyResumesExistingSharing()
  {
    Scenario scene;
    scene.process = core::ProcessState::RetryPending;
    QSignalSpy result(scene.controller.get(), &PreferencesApplyController::finished);
    scene.controller->apply(scene.changed());
    QTRY_COMPARE(result.count(), 1);
    QVERIFY(result.first().first().toBool());
    QCOMPARE(scene.starts, 1);
    QCOMPARE(scene.stops, 1);
  }

  void explicitMaintenanceRunsEvenWhenStoredParametersMatch()
  {
    Scenario scene;
    scene.connected();
    scene.onStart = [&] { scene.reconnected(); };
    QSignalSpy result(scene.controller.get(), &PreferencesApplyController::finished);
    scene.controller->apply({{Settings::Core::Port, 24800}}, true);
    QTRY_COMPARE(result.count(), 1);
    QVERIFY(result.first().first().toBool());
    QCOMPARE(scene.writes, 1);
    QCOMPARE(scene.stops, 1);
    QCOMPARE(scene.starts, 1);
  }

  void certificateMaintenanceDoesNotWaitForPeerToTrustNewIdentity()
  {
    Scenario scene;
    scene.connected();
    QSignalSpy result(scene.controller.get(), &PreferencesApplyController::finished);
    scene.controller->apply({{Settings::Security::Certificate, QStringLiteral("/test/identity.pem")},
                             {Settings::Security::KeySize, 2048}}, true, false);
    QTRY_COMPARE(result.count(), 1);
    QVERIFY(result.first().first().toBool());
    QCOMPARE(scene.writes, 1);
    QCOMPARE(scene.restores, 0);
    QCOMPARE(scene.stops, 1);
    QCOMPARE(scene.starts, 1);
    QCOMPARE(scene.connection, core::ConnectionState::Disconnected);
    QVERIFY(!scene.controller->isApplying());
  }

  void stopTimeoutDoesNotCommitCandidateAndResumesAfterLateStop()
  {
    Scenario scene(25);
    scene.connected();
    scene.deferStop = true;
    const auto previous = scene.stored;
    QSignalSpy result(scene.controller.get(), &PreferencesApplyController::finished);
    scene.controller->apply(scene.changed());
    QTRY_COMPARE(result.count(), 1);
    QVERIFY(!result.first().first().toBool());
    QCOMPARE(scene.stored, previous);
    QCOMPARE(scene.writes, 0);
    QCOMPARE(scene.starts, 0);
    scene.stopped();
    scene.controller->notifyProcessState(core::ProcessState::Stopped);
    QTRY_COMPARE(scene.starts, 1);
    QCOMPARE(scene.writes, 0);
  }

private:
  QTemporaryDir m_files;
};

QTEST_MAIN(PreferencesApplyControllerTests)
#include "PreferencesApplyControllerTests.moc"
