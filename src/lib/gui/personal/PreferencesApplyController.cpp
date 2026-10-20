/*
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "PreferencesApplyController.h"
#include "PreferenceChanges.h"

#include <utility>

namespace deskflow::gui::personal {

PreferencesApplyController::PreferencesApplyController(Operations operations, QObject *parent, int timeoutMs)
    : QObject(parent), m_operations(std::move(operations)), m_timeoutMs(qMax(10, timeoutMs))
{
  setObjectName(QStringLiteral("preferencesApplyController"));
  m_timer.setSingleShot(true);
  connect(&m_timer, &QTimer::timeout, this, &PreferencesApplyController::timeout);
}

bool PreferencesApplyController::isApplying() const
{
  return m_phase != Phase::Idle;
}

void PreferencesApplyController::apply(const QVariantMap &changes, bool forceReconnect, bool waitForConnection)
{
  if (isApplying() || m_restartWhenStopped) {
    Q_EMIT statusChanged(tr("正在应用设置，请稍候…"));
    return;
  }

  m_previous = m_operations.readConfig(changes.keys());
  m_candidate = changes;
  for (auto it = m_candidate.begin(); it != m_candidate.end();) {
    if (!forceReconnect && m_previous.value(it.key()) == it.value()) {
      m_previous.remove(it.key());
      it = m_candidate.erase(it);
    } else {
      ++it;
    }
  }
  if (m_candidate.isEmpty()) {
    Q_EMIT finished(true, tr("设置未更改。"));
    return;
  }

  m_writeAttempted = false;
  m_failure.clear();
  const bool reconnect = forceReconnect || preferencesRequireReconnect(m_candidate);
  m_wasRunning = reconnect && m_operations.processState() != core::ProcessState::Stopped;
  const auto connection = m_operations.connectionState();
  m_requireConnection = waitForConnection && (connection == core::ConnectionState::Connected ||
                                            connection == core::ConnectionState::Listening);
  m_phase = m_wasRunning ? Phase::Stopping : Phase::Saving;
  Q_EMIT applyingChanged(true);

  if (!m_wasRunning) {
    Q_EMIT statusChanged(tr("正在保存设置…"));
    QString error;
    m_writeAttempted = true;
    if (!m_operations.writeConfig(m_candidate, &error)) {
      QString restoreError;
      const bool restored = m_operations.restoreConfig(m_previous, &restoreError);
      finish(false, tr("无法保存设置：%1").arg(error) +
                        (restored ? tr(" 已恢复上次设置。") : tr(" 恢复设置失败：%1").arg(restoreError)));
      return;
    }
    finish(true, reconnect ? tr("设置已保存，下次连接时生效。") : tr("设置已保存。"));
    return;
  }

  Q_EMIT statusChanged(tr("正在应用设置，将短暂重新连接…"));
  m_timer.start(m_timeoutMs);
  requestStop();
}

void PreferencesApplyController::requestStop()
{
  // CoreProcess emits lifecycle signals while holding its process mutex.
  // Leave that call stack before invoking another start or stop operation.
  QTimer::singleShot(0, this, [this] {
    if (m_phase != Phase::Stopping && m_phase != Phase::StoppingToRestore)
      return;
    const auto state = m_operations.processState();
    if (state == core::ProcessState::Stopped)
      stopped();
    else if (state != core::ProcessState::Stopping)
      m_operations.stop();
  });
}

void PreferencesApplyController::stopped()
{
  if (m_operations.processState() != core::ProcessState::Stopped)
    return;
  if (m_phase == Phase::Stopping)
    writeCandidateAndStart();
  else if (m_phase == Phase::StoppingToRestore)
    restoreAndStart();
}

void PreferencesApplyController::writeCandidateAndStart()
{
  m_phase = Phase::Starting;
  QString error;
  // Maintenance operations can replace a file before settings persistence fails.
  // Even a failed write must therefore run the injected rollback operation.
  m_writeAttempted = true;
  if (!m_operations.writeConfig(m_candidate, &error)) {
    recover(tr("无法保存设置：%1").arg(error));
    return;
  }
  Q_EMIT statusChanged(tr("设置已保存，正在恢复共享连接…"));
  m_timer.start(m_timeoutMs);
  m_operations.start();
}

void PreferencesApplyController::notifyProcessState(core::ProcessState state)
{
  if (state == core::ProcessState::Stopped) {
    if (m_restartWhenStopped && m_phase == Phase::Idle) {
      QTimer::singleShot(0, this, [this] {
        if (m_restartWhenStopped && m_phase == Phase::Idle &&
            m_operations.processState() == core::ProcessState::Stopped) {
          m_restartWhenStopped = false;
          m_operations.start();
        }
      });
      return;
    }
    if (m_phase == Phase::Stopping || m_phase == Phase::StoppingToRestore)
      QTimer::singleShot(0, this, [this] { stopped(); });
  }
  if (state != core::ProcessState::Started)
    return;
  if (m_phase == Phase::Idle)
    m_restartWhenStopped = false;

  if (m_phase == Phase::Starting && !m_requireConnection)
    finish(true, tr("设置已保存，连接成功后生效。"));
  else if (m_phase == Phase::Restoring && !m_requireConnection)
    finish(false, m_failure + tr(" 已恢复上次设置。"));
  else {
    const auto connection = m_operations.connectionState();
    if (connection == core::ConnectionState::Connected || connection == core::ConnectionState::Listening)
      notifyConnectionState(connection);
  }
}

void PreferencesApplyController::notifyConnectionState(core::ConnectionState state)
{
  if (state != core::ConnectionState::Connected && state != core::ConnectionState::Listening)
    return;
  // A queued notification from the previous core must not confirm a new start.
  if (m_operations.processState() != core::ProcessState::Started || m_operations.connectionState() != state)
    return;
  if (m_phase == Phase::Starting)
    finish(true, tr("设置已应用，连接已恢复。"));
  else if (m_phase == Phase::Restoring)
    finish(false, m_failure + tr(" 已恢复上次设置并重新连接。"));
}

void PreferencesApplyController::notifyStartFailure()
{
  if (m_phase == Phase::Starting)
    recover(tr("共享核心未能启动。"));
  else if (m_phase == Phase::Restoring)
    finish(false, m_failure + tr(" 上次设置已恢复，但尚未连接；请在设备页重试。"));
}

void PreferencesApplyController::recover(const QString &reason)
{
  if (!isApplying())
    return;
  m_failure = reason;
  m_phase = Phase::StoppingToRestore;
  Q_EMIT statusChanged(tr("正在恢复上次设置…"));
  m_timer.start(m_timeoutMs);
  requestStop();
}

void PreferencesApplyController::restoreAndStart()
{
  QString error;
  if (m_writeAttempted && !m_operations.restoreConfig(m_previous, &error)) {
    finish(false, m_failure + tr(" 恢复设置失败：%1").arg(error));
    return;
  }
  m_writeAttempted = false;
  m_phase = Phase::Restoring;
  Q_EMIT statusChanged(tr("上次设置已恢复，正在重新连接…"));
  m_timer.start(m_timeoutMs);
  m_operations.start();
}

void PreferencesApplyController::timeout()
{
  if (m_phase == Phase::Starting || m_phase == Phase::Stopping) {
    recover(tr("应用设置超时。"));
    return;
  }
  if (m_phase == Phase::Restoring) {
    finish(false, m_failure + tr(" 上次设置已恢复，但连接仍不可用；请在设备页重试。"));
    return;
  }
  if (m_phase == Phase::StoppingToRestore) {
    QString error;
    const bool restored = !m_writeAttempted || m_operations.restoreConfig(m_previous, &error);
    m_restartWhenStopped = m_wasRunning && restored;
    finish(false, restored
                      ? m_failure + tr(" 上次设置已恢复，等待共享核心停止后重连。")
                      : m_failure + tr(" 无法恢复设置：%1").arg(error));
  }
}

void PreferencesApplyController::finish(bool success, const QString &message)
{
  m_timer.stop();
  m_phase = Phase::Idle;
  m_writeAttempted = false;
  Q_EMIT applyingChanged(false);
  Q_EMIT finished(success, message);
}

} // namespace deskflow::gui::personal
