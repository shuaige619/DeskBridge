/*
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "common/Enums.h"
#include "common/ShortcutRules.h"
#include <QObject>
#include <QTimer>
#include <functional>

class MainWindow;

namespace deskflow::gui::personal {

// Separates the apply/restore state machine from process launch and disk I/O.
// Tests use in-memory operations; they never launch a sharing core or post keys.
class ShortcutApplyController : public QObject
{
  Q_OBJECT
public:
  struct Operations
  {
    std::function<shortcuts::Configuration()> readConfig;
    std::function<bool(const shortcuts::Configuration &, QString *)> writeConfig;
    std::function<core::ProcessState()> processState;
    std::function<core::ConnectionState()> connectionState;
    std::function<void()> start;
    std::function<void()> stop;
  };

  explicit ShortcutApplyController(MainWindow *window);
  explicit ShortcutApplyController(Operations operations, QObject *parent = nullptr, int timeoutMs = 20000);
  bool isApplying() const;

public Q_SLOTS:
  void apply(const deskflow::shortcuts::Configuration &configuration);
  void notifyProcessState(deskflow::core::ProcessState state);
  void notifyConnectionState(deskflow::core::ConnectionState state);
  void notifyStartFailure();

Q_SIGNALS:
  void applyingChanged(bool applying);
  void statusChanged(const QString &message);
  void finished(bool success, const QString &message);

private:
  enum class Phase { Idle, Stopping, Starting, StoppingToRestore, Restoring };
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
  shortcuts::Configuration m_previous;
  shortcuts::Configuration m_candidate;
  bool m_wasRunning = false;
  bool m_requireConnection = false;
  bool m_candidateWritten = false;
  bool m_restartWhenStopped = false;
  QString m_failure;
};

} // namespace deskflow::gui::personal
