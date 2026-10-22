/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2024 - 2025 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "common/Enums.h"
#include "common/ScreenLayout.h"
#include "common/Settings.h"
#include "common/ShortcutRules.h"
#include "gui/FileTail.h"
#include "gui/config/ServerConfig.h"

#include <QMutex>
#include <QObject>
#include <QProcess>
#include <QTimer>

class CoreProcessStopTests;

namespace deskflow::gui {

namespace ipc {
class CoreIpcClient;
class DaemonIpcClient;
} // namespace ipc

class CoreProcess : public QObject
{
  // Keep the class declaration identical in the library and test translation
  // units; MSVC encodes a method's access level in its linker symbol.
  friend class ::CoreProcessStopTests;

  using ConnectionState = deskflow::core::ConnectionState;
  using ProcessMode = Settings::ProcessMode;
  using ProcessState = deskflow::core::ProcessState;
  Q_OBJECT

public:
  enum class Error
  {
    AddressMissing,
    StartFailed
  };

  explicit CoreProcess(const ServerConfig &serverConfig);

  void start(std::optional<ProcessMode> processMode = std::nullopt);
  void stop(std::optional<ProcessMode> processMode = std::nullopt);
  void restart();
  void cleanup();
  void applyLogLevel();
  void clearSettings();
  void retryDaemon();
  const deskflow::layout::Snapshot &screenLayout() const { return m_screenLayout; }
  bool requestLayout(deskflow::layout::Direction direction);
  bool requestLayoutMove(const deskflow::layout::Move &move);
  void refreshScreenLayout();
  void completeLayoutRequest(quint32 requestId, bool success, const QString &error = {});

  // getters
  Settings::CoreMode mode() const
  {
    return m_mode;
  }
  QString secureSocketVersion() const
  {
    return m_secureSocketVersion;
  }
  bool isStarted() const
  {
    return m_processState == ProcessState::Started;
  }
  ProcessState processState() const
  {
    return m_processState;
  }
  ConnectionState connectionState() const
  {
    return m_connectionState;
  }
  shortcuts::DeviceOS sourceOperatingSystem() const { return m_sourceOperatingSystem; }
  bool serverModifierMapping() const { return m_serverModifierMapping; }

  // setters
  void setAddress(const QString &address)
  {
    m_address = correctedAddress(address);
  }
  void setMode(Settings::CoreMode mode)
  {
    m_mode = mode;
  }

Q_SIGNALS:
  void error(deskflow::gui::CoreProcess::Error error);
  void logLine(const QString &line);
  void connectionStateChanged(deskflow::core::ConnectionState state);
  void processStateChanged(deskflow::core::ProcessState state);
  void secureSocket(bool enabled);
  void daemonIpcClientConnectionFailed();
  void connectedClientsChanged(const QStringList &clients);
  void securityLevelChanged(QString securityLevel);
  void unrecognisedClient(const QString &clientName);
  void connectionRefused(deskflow::core::ConnectionRefusal reason);
  void retryIn(int seconds);
  void peerFingerprint(const QString &fingerprint);
  void missingKeyboardLayouts(const QString &layouts);
  void sourceOperatingSystemChanged(deskflow::shortcuts::DeviceOS os);
  void serverModifierMappingChanged(bool active);
  void screenLayoutChanged(const deskflow::layout::Snapshot &snapshot);
  void layoutRequestFinished(bool success, const QString &error);
  void layoutRequestReceived(quint32 requestId, deskflow::layout::Direction expected, deskflow::layout::Direction desired);
  void layoutMoveReceived(quint32 requestId, const deskflow::layout::Move &move);
  void layoutApplyFinished(quint32 requestId, bool success, const QString &error);
  void layoutSynchronizationUncertain(const QString &error);

private Q_SLOTS:
  void onProcessFinished(int exitCode, QProcess::ExitStatus);
  void onProcessReadyReadStandardOutput();
  void onProcessReadyReadStandardError();
  void onCoreIpcMessageReceived(const QString &command, const QString &args);
  void daemonIpcClientConnected();

private:
  void startForegroundProcess(const QStringList &args);
  void startProcessFromDaemon();
  void stopForegroundProcess();
  void stopProcessFromDaemon();
  QPair<bool, QString> persistServerConfig() const;
  void setConnectionState(ConnectionState state);
  void setProcessState(ProcessState state);
  bool checkSecureSocket(const QString &line);
  void handleLogLines(const QString &text);
  void handleProcessOutput(const QByteArray &bytes, QString &pending);
  void setSourceOperatingSystem(shortcuts::DeviceOS os);
  void setServerModifierMapping(bool active);
  QString correctedAddress(const QString &address) const;
  void setupDaemonLogTail(const QString &logPath);
  void checkExistingProcess();
  void onCoreIpcConnectionFailed();
  void reconcileScreenLayout(const deskflow::layout::Snapshot &snapshot);
  static QString makeQuotedArgs(const QString &app, const QStringList &args);
  static QString processModeToString(const Settings::ProcessMode mode);
  static QString processStateToString(const CoreProcess::ProcessState state);
  static QString wrapIpv6(const QString &address);

  const ServerConfig &m_serverConfig;
  QString m_address;
  ProcessState m_processState = ProcessState::Stopped;
  ConnectionState m_connectionState = ConnectionState::Disconnected;
  Settings::CoreMode m_mode = Settings::CoreMode::None;
  QMutex m_processMutex;
  QString m_secureSocketVersion;
  std::optional<ProcessMode> m_lastProcessMode = std::nullopt;
  QTimer m_retryTimer;
  deskflow::gui::ipc::CoreIpcClient *m_coreIpcClient = nullptr;
  deskflow::gui::ipc::DaemonIpcClient *m_daemonIpcClient = nullptr;
  FileTail *m_daemonFileTail = nullptr;
  QProcess *m_process = nullptr;
  QString m_appPath;
  shortcuts::DeviceOS m_sourceOperatingSystem = shortcuts::DeviceOS::Unknown;
  bool m_serverModifierMapping = false;
  bool m_sourceOperatingSystemFromIpc = false;
  bool m_serverModifierMappingFromIpc = false;
  deskflow::layout::Snapshot m_screenLayout;
  quint32 m_nextLayoutRequestId = 0;
  quint32 m_pendingLayoutRequestId = 0;
  QTimer m_layoutRequestTimer;
  struct ServerLayoutTransaction
  {
    quint32 id;
    deskflow::layout::Direction expected;
    deskflow::layout::Direction desired;
    QString serverName;
    QString clientName;
    QString expectedRevision;
    QString desiredRevision;
  };
  std::optional<ServerLayoutTransaction> m_serverLayoutTransaction;
  bool m_layoutReconcileNeeded = false;
  int m_layoutReconnectAttempts = 0;
  quint32 m_layoutQueryId = 0;
  QTimer m_layoutReconnectTimer;
  QString m_stdoutPending;
  QString m_stderrPending;
};

} // namespace deskflow::gui
