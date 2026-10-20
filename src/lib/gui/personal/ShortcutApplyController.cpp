/*
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "ShortcutApplyController.h"
#include "common/Settings.h"
#include "gui/MainWindow.h"
#include "gui/core/CoreProcess.h"
#include "gui/personal/ShortcutPage.h"

#include <utility>

namespace deskflow::gui::personal {
namespace {
ShortcutApplyController::Operations operationsFor(MainWindow *window)
{
  auto *process = window->personalCoreProcess();
  return {
      [] { return shortcuts::load(); },
      [](const shortcuts::Configuration &config, QString *error) { return shortcuts::save(config, error); },
      [process] { return process->processState(); },
      [process] { return process->connectionState(); },
      [process] { process->start(); },
      [process] { process->stop(); }
  };
}
} // namespace

ShortcutApplyController::ShortcutApplyController(MainWindow *window)
    : ShortcutApplyController(operationsFor(window), window)
{
  auto *page = window->personalShortcutPage();
  auto *process = window->personalCoreProcess();
  connect(window, &MainWindow::personalShortcutApplyRequested, this, &ShortcutApplyController::apply);
  connect(this, &ShortcutApplyController::applyingChanged, page, &ShortcutPage::setApplying);
  connect(this, &ShortcutApplyController::statusChanged, page, &ShortcutPage::setStatusMessage);
  connect(this, &ShortcutApplyController::finished, page, &ShortcutPage::applied);
  connect(this, &ShortcutApplyController::finished, window, [window] { window->refreshPersonalUi(); });
  // The process emits some state changes while holding its mutex. Always leave
  // that call stack before starting or stopping it again.
  connect(process, &CoreProcess::processStateChanged, this, &ShortcutApplyController::notifyProcessState,
          Qt::QueuedConnection);
  connect(process, &CoreProcess::connectionStateChanged, this, &ShortcutApplyController::notifyConnectionState,
          Qt::QueuedConnection);
  connect(process, &CoreProcess::error, this, [this] { notifyStartFailure(); }, Qt::QueuedConnection);
}

ShortcutApplyController::ShortcutApplyController(Operations operations, QObject *parent, int timeoutMs)
    : QObject(parent), m_operations(std::move(operations)), m_timeoutMs(qMax(10, timeoutMs))
{
  setObjectName(QStringLiteral("shortcutApplyController"));
  m_timer.setSingleShot(true);
  connect(&m_timer, &QTimer::timeout, this, &ShortcutApplyController::timeout);
}

bool ShortcutApplyController::isApplying() const
{
  return m_phase != Phase::Idle;
}

void ShortcutApplyController::apply(const shortcuts::Configuration &configuration)
{
  if (isApplying()) {
    Q_EMIT statusChanged(tr("正在应用更改，请稍候…"));
    return;
  }
  if (const auto error = shortcuts::validate(configuration); !error.isEmpty()) {
    Q_EMIT finished(false, error);
    return;
  }
  if (!Settings::isWritable()) {
    Q_EMIT finished(false, tr("设置文件为只读，无法应用更改。"));
    return;
  }

  m_previous = m_operations.readConfig();
  m_candidate = configuration;
  m_candidateWritten = false;
  m_failure.clear();
  m_restartWhenStopped = false;
  m_wasRunning = m_operations.processState() != core::ProcessState::Stopped;
  const auto connection = m_operations.connectionState();
  m_requireConnection = connection == core::ConnectionState::Connected ||
                        connection == core::ConnectionState::Listening;
  m_phase = Phase::Stopping;
  Q_EMIT applyingChanged(true);
  Q_EMIT statusChanged(m_wasRunning ? tr("正在应用更改，将短暂重新连接…") : tr("正在保存映射规则…"));
  m_timer.start(m_timeoutMs);
  requestStop();
}

void ShortcutApplyController::requestStop()
{
  QTimer::singleShot(0, this, [this] {
    if (m_phase != Phase::Stopping && m_phase != Phase::StoppingToRestore)
      return;
    const auto process = m_operations.processState();
    if (process == core::ProcessState::Stopped)
      stopped();
    else if (process != core::ProcessState::Stopping)
      m_operations.stop();
  });
}

void ShortcutApplyController::stopped()
{
  if (m_phase == Phase::Stopping)
    writeCandidateAndStart();
  else if (m_phase == Phase::StoppingToRestore)
    restoreAndStart();
}

void ShortcutApplyController::writeCandidateAndStart()
{
  m_phase = Phase::Starting;
  QString error;
  if (!m_operations.writeConfig(m_candidate, &error)) {
    recover(tr("无法保存更改：%1").arg(error));
    return;
  }
  m_candidateWritten = true;
  if (!m_wasRunning) {
    finish(true, tr("映射规则已保存，下次连接时生效。"));
    return;
  }
  Q_EMIT statusChanged(tr("正在恢复共享连接…"));
  m_timer.start(m_timeoutMs);
  m_operations.start();
}

void ShortcutApplyController::notifyProcessState(core::ProcessState state)
{
  if (state == core::ProcessState::Stopped) {
    if (m_restartWhenStopped && m_phase == Phase::Idle) {
      m_restartWhenStopped = false;
      QTimer::singleShot(0, this, [this] { m_operations.start(); });
      return;
    }
    if (m_phase == Phase::Stopping || m_phase == Phase::StoppingToRestore)
      QTimer::singleShot(0, this, [this] { stopped(); });
  }
  if (state != core::ProcessState::Started)
    return;
  const auto connection = m_operations.connectionState();
  if (m_phase == Phase::Starting && !m_requireConnection)
    finish(true, tr("映射规则已保存，连接成功后生效。"));
  else if (m_phase == Phase::Restoring && !m_requireConnection)
    finish(false, m_failure + tr(" 已恢复上次设置。"));
  else if (connection == core::ConnectionState::Connected || connection == core::ConnectionState::Listening)
    notifyConnectionState(connection);
}

void ShortcutApplyController::notifyConnectionState(core::ConnectionState state)
{
  if (state != core::ConnectionState::Connected && state != core::ConnectionState::Listening)
    return;
  if (m_phase == Phase::Starting)
    finish(true, tr("更改已应用，连接已恢复。"));
  else if (m_phase == Phase::Restoring)
    finish(false, m_failure + tr(" 已恢复上次设置并重新连接。"));
}

void ShortcutApplyController::notifyStartFailure()
{
  if (m_phase == Phase::Starting)
    recover(tr("共享核心未能启动。"));
  else if (m_phase == Phase::Restoring)
    finish(false, m_failure + tr(" 上次设置已恢复，但尚未连接；请在设备页重试。"));
}

void ShortcutApplyController::recover(const QString &reason)
{
  if (!isApplying())
    return;
  m_failure = reason;
  m_phase = Phase::StoppingToRestore;
  Q_EMIT statusChanged(tr("正在恢复上次设置…"));
  m_timer.start(m_timeoutMs);
  requestStop();
}

void ShortcutApplyController::restoreAndStart()
{
  QString error;
  if (m_candidateWritten && !m_operations.writeConfig(m_previous, &error)) {
    finish(false, m_failure + tr(" 恢复设置失败：%1").arg(error));
    return;
  }
  m_candidateWritten = false;
  if (!m_wasRunning) {
    finish(false, m_failure + tr(" 未应用更改。"));
    return;
  }
  m_phase = Phase::Restoring;
  Q_EMIT statusChanged(tr("上次设置已恢复，正在重新连接…"));
  m_timer.start(m_timeoutMs);
  m_operations.start();
}

void ShortcutApplyController::timeout()
{
  if (m_phase == Phase::Starting || m_phase == Phase::Stopping) {
    recover(tr("应用更改超时。"));
    return;
  }
  if (m_phase == Phase::Restoring) {
    finish(false, m_failure + tr(" 上次设置已恢复，但连接仍不可用；请在设备页重试。"));
    return;
  }
  if (m_phase == Phase::StoppingToRestore) {
    QString error;
    const bool restored = !m_candidateWritten || m_operations.writeConfig(m_previous, &error);
    m_restartWhenStopped = m_wasRunning && restored;
    finish(false, restored
                      ? m_failure + tr(" 上次设置已恢复，等待共享核心停止后重连。")
                      : m_failure + tr(" 无法恢复设置：%1").arg(error));
  }
}

void ShortcutApplyController::finish(bool success, const QString &message)
{
  m_timer.stop();
  m_phase = Phase::Idle;
  m_candidateWritten = false;
  Q_EMIT applyingChanged(false);
  Q_EMIT finished(success, message);
}

} // namespace deskflow::gui::personal
