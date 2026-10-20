/*
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "common/Enums.h"
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>
#include <functional>

namespace deskflow::gui::personal {

// Applies one page's settings as a transaction. Process operations are injected
// so tests can verify recovery without launching a core or changing real settings.
class PreferencesApplyController : public QObject
{
  Q_OBJECT
public:
  struct Operations
  {
    std::function<QVariantMap(const QStringList &)> readConfig;
    std::function<bool(const QVariantMap &, QString *)> writeConfig;
    std::function<bool(const QVariantMap &, QString *)> restoreConfig;
    std::function<core::ProcessState()> processState;
    std::function<core::ConnectionState()> connectionState;
    std::function<void()> start;
    std::function<void()> stop;
  };

  explicit PreferencesApplyController(Operations operations, QObject *parent = nullptr, int timeoutMs = 15000);
  bool isApplying() const;

public Q_SLOTS:
  void apply(const QVariantMap &changes, bool forceReconnect = false, bool waitForConnection = true);
  void notifyProcessState(deskflow::core::ProcessState state);
  void notifyConnectionState(deskflow::core::ConnectionState state);
  void notifyStartFailure();

Q_SIGNALS:
  void applyingChanged(bool applying);
  void statusChanged(const QString &message);
  void finished(bool success, const QString &message);

private:
  enum class Phase { Idle, Saving, Stopping, Starting, StoppingToRestore, Restoring };
  void requestStop();
  void stopped();
  void writeCandidateAndStart();
  void recover(const QString &reason);
  void restoreAndStart();
  void timeout();
  void finish(bool success, const QString &message);

  Operations m_operations;
  Phase m_phase = Phase::Idle;
  QTimer m_timer;
  int m_timeoutMs;
  QVariantMap m_previous;
  QVariantMap m_candidate;
  bool m_wasRunning = false;
  bool m_requireConnection = false;
  bool m_writeAttempted = false;
  bool m_restartWhenStopped = false;
  QString m_failure;
};

} // namespace deskflow::gui::personal
