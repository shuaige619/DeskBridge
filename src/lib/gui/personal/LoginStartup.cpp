/*
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "LoginStartup.h"

#include <QFutureWatcher>
#include <QPromise>
#include <QThreadPool>
#include <memory>
#include <utility>

namespace deskflow::gui::personal {

#ifdef Q_OS_MAC
LoginStartup::Operations nativeLoginStartupOperations();
#endif

namespace {
LoginStartup::Status readStatus(const LoginStartup::Operations &operations)
{
  if (operations.read) {
    try {
      return operations.read();
    } catch (...) {
      LoginStartup::Status status;
      status.error = LoginStartup::tr("无法读取登录启动状态。");
      return status;
    }
  }
  LoginStartup::Status status;
  status.message = LoginStartup::tr("当前平台不支持此登录启动方式。");
  return status;
}

LoginStartup::Operations platformOperations()
{
#ifdef Q_OS_MAC
  return nativeLoginStartupOperations();
#else
  return {};
#endif
}
} // namespace

bool LoginStartup::Status::operator==(const Status &other) const
{
  return supported == other.supported && enabled == other.enabled && approvalRequired == other.approvalRequired &&
         message == other.message && error == other.error;
}

LoginStartup::LoginStartup(QObject *parent) : LoginStartup(platformOperations(), parent)
{
}

LoginStartup::LoginStartup(Operations operations, QObject *parent)
    : QObject(parent), m_operations(std::move(operations))
{
  setObjectName(QStringLiteral("loginStartup"));
  refresh();
}

LoginStartup::Status LoginStartup::status() const
{
  return m_status;
}

bool LoginStartup::isApplying() const
{
  return m_applying;
}

void LoginStartup::refresh()
{
  if (!m_applying)
    updateStatus(readStatus(m_operations));
}

void LoginStartup::setEnabled(bool enabled)
{
  if (m_applying)
    return;
  if (!m_status.supported || !m_status.error.isEmpty() || !m_operations.change) {
    Q_EMIT finished(false, m_status.error.isEmpty() ? tr("当前环境不支持修改登录启动。") : m_status.error);
    return;
  }

  m_applying = true;
  Q_EMIT applyingChanged(true);
  auto *watcher = new QFutureWatcher<ChangeResult>(this);
  connect(watcher, &QFutureWatcher<ChangeResult>::finished, this, [this, watcher] {
    const auto result = watcher->result();
    watcher->deleteLater();
    updateStatus(result.status);
    m_applying = false;
    Q_EMIT applyingChanged(false);
    const auto message = result.success ? result.status.message :
        (!result.error.isEmpty() ? result.error :
         (!result.status.error.isEmpty() ? result.status.error : tr("无法修改登录启动。")));
    Q_EMIT finished(result.success, message);
  });

  auto promise = std::make_shared<QPromise<ChangeResult>>();
  promise->start();
  watcher->setFuture(promise->future());
  // The worker owns copies of its operations and promise. It never references
  // the UI object, including if the window is destroyed while macOS responds.
  QThreadPool::globalInstance()->start([operations = m_operations, enabled, promise] {
    ChangeResult result;
    try {
      result = operations.change(enabled);
    } catch (...) {
      result.status = readStatus(operations);
      result.error = LoginStartup::tr("修改登录启动时发生错误。");
    }
    promise->addResult(std::move(result));
    promise->finish();
  });
}

void LoginStartup::openSystemSettings()
{
  if (m_status.supported && !m_applying && m_operations.openSettings)
    m_operations.openSettings();
}

void LoginStartup::updateStatus(Status status)
{
  if (m_status == status)
    return;
  m_status = std::move(status);
  Q_EMIT statusChanged();
}

} // namespace deskflow::gui::personal
