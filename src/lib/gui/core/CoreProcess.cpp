/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2024 - 2025 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CoreProcess.h"

#include "common/ExitCodes.h"
#include "gui/ipc/CoreIpcClient.h"
#include "gui/ipc/DaemonIpcClient.h"

#if defined(Q_OS_MACOS)
#include "OSXHelpers.h"
#endif

#ifdef Q_OS_LINUX
#include <signal.h>
#include <sys/prctl.h>
#endif

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMetaEnum>
#include <QMutexLocker>
#include <QRegularExpression>

namespace deskflow::gui {

const int kRetryDelay = 1000;
const auto kLineSplitRegex = QRegularExpression("\r|\n|\r\n");

QString CoreProcess::processModeToString(const Settings::ProcessMode mode)
{
  return QVariant::fromValue(mode).toString().toLower();
}

QString CoreProcess::processStateToString(const CoreProcess::ProcessState state)
{
  return QVariant::fromValue(state).toString().toLower();
}

/**
 * @brief Wraps options that contain spaces in quotes
 *
 * Useful to handle things like paths with spaces (e.g. "C:\Program Files").
 *
 * Can also be used to create a representation of a command that can be pasted
 * into a terminal.
 */
QString CoreProcess::makeQuotedArgs(const QString &app, const QStringList &args)
{
  QStringList command = {app};
  command.append(args);

  static const auto quote = QStringLiteral("\"");
  static const auto space = QStringLiteral(" ");
  QStringList quoted;
  for (const auto &item : std::as_const(command)) {
    auto temp = item.simplified();
    if (const auto wrapped = (temp.startsWith(quote) && temp.endsWith(quote)); temp.contains(space) && !wrapped) {
      temp = QStringLiteral("%1%2%1").arg(quote, temp);
    }
    quoted.append(temp);
  }

  return quoted.join(space);
}

/**
 * @brief If IPv6, ensures the IP is surround in square brackets.
 */
QString CoreProcess::wrapIpv6(const QString &address)
{
  static const auto colon = QStringLiteral(":");
  static const auto openBracket = QStringLiteral("[");
  static const auto closeBracket = QStringLiteral("]");

  if (!address.contains(colon) || address.isEmpty()) {
    return address;
  }

  QString wrapped = address;

  if (!address.startsWith(openBracket)) {
    wrapped.prepend(openBracket);
  }

  if (!address.endsWith(closeBracket)) {
    wrapped.append(closeBracket);
  }

  return wrapped;
}

//
// CoreProcess
//

CoreProcess::CoreProcess(const ServerConfig &serverConfig)
    : m_serverConfig(serverConfig),
      m_daemonIpcClient{new ipc::DaemonIpcClient(this)}
{
  m_layoutReconnectTimer.setSingleShot(true);
  m_layoutReconnectTimer.setInterval(1000);
  connect(&m_layoutReconnectTimer, &QTimer::timeout, this, [this] {
    if (!m_layoutReconcileNeeded || !m_serverLayoutTransaction || m_processState != ProcessState::Started)
      return;
    if (m_layoutReconnectAttempts++ >= 3) {
      Q_EMIT layoutSynchronizationUncertain(tr("仍无法确认服务端实际布局。请恢复连接，或停止共享以恢复原布局。"));
      return;
    }
    if (m_coreIpcClient) {
      if (m_coreIpcClient->isConnected())
        refreshScreenLayout();
      else
        m_coreIpcClient->connectToServer();
    }
    if (m_layoutReconcileNeeded)
      m_layoutReconnectTimer.start();
  });
  m_layoutRequestTimer.setSingleShot(true);
  m_layoutRequestTimer.setInterval(12000);
  connect(&m_layoutRequestTimer, &QTimer::timeout, this, [this] {
    if (m_pendingLayoutRequestId) {
      if (m_serverLayoutTransaction) {
        onCoreIpcConnectionFailed();
        return;
      }
      m_pendingLayoutRequestId = 0;
      Q_EMIT layoutRequestFinished(false, tr("等待来源端确认超时，请检查连接后重试。"));
      refreshScreenLayout();
    }
  });
  m_appPath = QStringLiteral("%1/%2").arg(QCoreApplication::applicationDirPath(), kCoreBinName);
  if (!QFile::exists(m_appPath)) {
    qCritical("core server binary does not exist");
  }

  connect(m_daemonIpcClient, &ipc::DaemonIpcClient::connected, this, &CoreProcess::daemonIpcClientConnected);
  connect(
      m_daemonIpcClient, &ipc::DaemonIpcClient::connectionFailed, this, &CoreProcess::daemonIpcClientConnectionFailed
  );
  connect(m_daemonIpcClient, &ipc::DaemonIpcClient::logPathReceived, this, &CoreProcess::setupDaemonLogTail);

  connect(&m_retryTimer, &QTimer::timeout, this, [this] {
    if (m_processState == ProcessState::RetryPending) {
      start();
    } else {
      qDebug("retry cancelled, process state is not retry pending");
    }
  });
}

void CoreProcess::onProcessReadyReadStandardOutput()
{
  if (m_process) {
    handleProcessOutput(m_process->readAllStandardOutput(), m_stdoutPending);
  }
}

void CoreProcess::onProcessReadyReadStandardError()
{
  if (m_process) {
    handleProcessOutput(m_process->readAllStandardError(), m_stderrPending);
  }
}

void CoreProcess::daemonIpcClientConnected()
{
  applyLogLevel();
  m_daemonIpcClient->requestLogPath();
}

void CoreProcess::checkExistingProcess()
{
  qInfo("checking existing core");

  auto *client = new ipc::CoreIpcClient(this);
  connect(client, &ipc::CoreIpcClient::connected, this, [client] {
    qInfo("existing core has matching version, leaving it running");
    client->deleteLater();
  });
  connect(client, &ipc::CoreIpcClient::versionMismatch, this, [client] {
    qInfo("existing core has mismatched version, asking it to stop");
    client->sendStop();
  });
  connect(client, &ipc::CoreIpcClient::serverShutdown, this, [this, client] {
    qInfo("existing core stopped successfully");
    client->deleteLater();
    setProcessState(ProcessState::RetryPending);
    m_retryTimer.setSingleShot(true);
    m_retryTimer.start(kRetryDelay);
  });
  connect(client, &ipc::CoreIpcClient::connectionFailed, this, [client] {
    qCritical("could not contact existing core");
    client->deleteLater();
  });
  client->connectToServer();
}

void CoreProcess::onProcessFinished(int exitCode, QProcess::ExitStatus)
{
  using enum ProcessState;
  setConnectionState(ConnectionState::Disconnected);

  if (m_retryTimer.isActive()) {
    m_retryTimer.stop();
  }

  if (exitCode != s_exitSuccess) {
    setProcessState(Stopped);
    if (exitCode == s_exitDuplicate) {
      checkExistingProcess();
      return;
    }
    qWarning("desktop process exited with code: %d", exitCode);
    return;
  }

  qDebug("desktop process exited normally");

  if (const auto wasStarted = m_processState == Started; wasStarted) {
    qDebug("desktop process was running, retrying in %d ms", kRetryDelay);
    setProcessState(RetryPending);
    m_retryTimer.setSingleShot(true);
    m_retryTimer.start(kRetryDelay);
  } else {
    setProcessState(Stopped);
  }
}

void CoreProcess::applyLogLevel()
{
  const auto processMode = Settings::value(Settings::Core::ProcessMode).value<Settings::ProcessMode>();
  if (processMode == ProcessMode::Service) {
    qDebug() << "setting daemon log level:" << Settings::logLevelText();
    m_daemonIpcClient->sendLogLevel(Settings::logLevelText());
  }
  if (m_coreIpcClient && m_coreIpcClient->isConnected()) {
    m_coreIpcClient->sendLogLevel(Settings::logLevelText());
  }
}

void CoreProcess::startForegroundProcess(const QStringList &args)
{
  using enum ProcessState;

  if (m_processState != Starting) {
    qCritical("not starting core desktop process, unexpected process state");
    return;
  }

  // only make quoted args for printing the command for convenience; so that the
  // core command can be easily copy/pasted to the terminal for testing.
  const auto quoted = makeQuotedArgs(m_appPath, args);
  qInfo("running command: %s", qPrintable(quoted));

#ifdef Q_OS_LINUX
  m_process->setChildProcessModifier([] {
    // the core process becomes orphaned when the gui process exits abruptly (e.g. with kill -9),
    // so ensure the os also kills the core when that happens to the gui.
    prctl(PR_SET_PDEATHSIG, SIGTERM);
  });
#endif

  m_process->start(m_appPath, args);

  if (m_process->waitForStarted()) {
    setProcessState(Started);
  } else {
    setProcessState(Stopped);
    Q_EMIT error(Error::StartFailed);
  }
}

void CoreProcess::startProcessFromDaemon()
{
  if (m_processState != ProcessState::Starting) {
    qCritical("not starting core process from daemon, unexpected process state");
    return;
  }

  const auto configFile = Settings::settingsFile();
  qInfo("sending start to daemon (config file: %s)", qPrintable(configFile));

  auto sendStart = [this, configFile] {
    m_daemonIpcClient->sendConfigFile(configFile);
    m_daemonIpcClient->sendStartProcess();
    setProcessState(ProcessState::Started);
  };

  if (m_daemonIpcClient->isConnected()) {
    sendStart();
  } else {
    connect(
        m_daemonIpcClient, &ipc::DaemonIpcClient::connected, this, sendStart,
        static_cast<Qt::ConnectionType>(Qt::SingleShotConnection | Qt::QueuedConnection)
    );
    m_daemonIpcClient->connectToServer();
  }
}

void CoreProcess::stopForegroundProcess()
{
  if (m_processState != ProcessState::Stopping) {
    qCritical("not stopping core desktop process, unexpected process state");
    return;
  }

  if (!m_process) {
    qDebug("no desktop process remains to stop");
    setProcessState(ProcessState::Stopped);
    return;
  }

  qInfo("stopping core desktop process");
  if (m_coreIpcClient && m_coreIpcClient->isConnected()) {
    qDebug("sending stop command to core process");
    m_coreIpcClient->sendStop();

    // Fallback: if the core doesn't confirm shutdown via IPC within the
    // timeout, force-terminate it so we never hang in the Stopping state.
    QTimer::singleShot(kRetryDelay, this, [this] {
      if (m_processState != ProcessState::Stopping) {
        return;
      }

      qWarning("core process did not confirm graceful stop in time, forcing terminate");

      if (m_coreIpcClient) {
        m_coreIpcClient->disconnectFromServer();
        m_coreIpcClient->deleteLater();
        m_coreIpcClient = nullptr;
      }

      if (m_process && m_process->state() == QProcess::ProcessState::Running) {
        m_process->terminate();
      }
    });
    return;
  }

  if (m_process->state() == QProcess::ProcessState::Running) {
    qDebug("process is running, terminating");
    m_process->terminate();
  } else {
    qDebug("process is not running, skipping terminate");
    if (m_process->state() == QProcess::ProcessState::NotRunning)
      setProcessState(ProcessState::Stopped);
  }
}

void CoreProcess::stopProcessFromDaemon()
{
  if (m_processState != ProcessState::Stopping) {
    qCritical("not stopping core process from daemon, unexpected process state");
    return;
  }

  auto sendStop = [this] {
    m_daemonIpcClient->sendStopProcess();
    setProcessState(ProcessState::Stopped);
  };

  if (m_daemonIpcClient->isConnected()) {
    sendStop();
  } else {
    connect(
        m_daemonIpcClient, &ipc::DaemonIpcClient::connected, this, sendStop,
        static_cast<Qt::ConnectionType>(Qt::SingleShotConnection | Qt::QueuedConnection)
    );
    m_daemonIpcClient->connectToServer();
  }
}

void CoreProcess::handleLogLines(const QString &text)
{
  const auto lines = text.split(kLineSplitRegex);
  for (const auto &line : lines) {
    if (line.isEmpty()) {
      continue;
    }

    const auto sourceMarker = QStringLiteral("DeskBridge source OS: ");
    if (const auto position = line.indexOf(sourceMarker); position >= 0) {
      const auto name = line.mid(position + sourceMarker.size()).trimmed();
      auto os = shortcuts::DeviceOS::Unknown;
      for (const auto candidate : {shortcuts::DeviceOS::MacOS, shortcuts::DeviceOS::Windows, shortcuts::DeviceOS::Linux}) {
        if (name == shortcuts::deviceOSName(candidate))
          os = candidate;
      }
      setSourceOperatingSystem(os);
    }
    const auto mappingMarker = QStringLiteral("DeskBridge modifier remap: ");
    if (const auto position = line.indexOf(mappingMarker); position >= 0) {
      const auto value = line.mid(position + mappingMarker.size()).trimmed();
      if (value == QStringLiteral("active") || value == QStringLiteral("none"))
        setServerModifierMapping(value == QStringLiteral("active"));
    }

#if defined(Q_OS_MACOS)
    // HACK: macOS 10.13.4+ spamming error lines in logs making them
    // impossible to read and debug; giving users a red herring.
    if (line.contains("calling TIS/TSM in non-main thread environment")) {
      continue;
    }

    // the core process is not allowed to show the permission prompt
    // (called "notification permission") and the notification log line is emitted from
    // deep inside cocoa code in the core binary to stdout, so it can't be sent over
    // ipc from the core to the gui and instead the gui has to parse the core output.
    static const QString needle = "OSX Notification: ";
    if (line.contains(needle) && line.contains('|')) {
      const int delimiterPosition = line.indexOf('|');
      const int start = line.indexOf(needle);
      const QString title = line.mid(start + needle.length(), delimiterPosition - start - needle.length());
      const QString body = line.mid(delimiterPosition + 1, line.length() - delimiterPosition);
      if (!showOSXNotification(title, body)) {
        qDebug("osx notification was not shown");
      }
    }
#endif

    Q_EMIT logLine(line);
  }
}

void CoreProcess::handleProcessOutput(const QByteArray &bytes, QString &pending)
{
  pending += QString::fromUtf8(bytes);
  const auto complete = std::max(pending.lastIndexOf('\r'), pending.lastIndexOf('\n'));
  if (complete >= 0) {
    const auto lines = pending.left(complete + 1);
    pending.remove(0, complete + 1);
    // A signal handler may stop the connection and clear the pending buffers.
    // Extract complete lines first rather than iterating over a mutable buffer.
    handleLogLines(lines);
  }
  // Do not retain an unbounded unterminated output line from a failed core.
  if (pending.size() > 65536) {
    Q_EMIT logLine(pending);
    pending.clear();
  }
}

void CoreProcess::setSourceOperatingSystem(shortcuts::DeviceOS os)
{
  if (m_sourceOperatingSystem == os)
    return;
  m_sourceOperatingSystem = os;
  Q_EMIT sourceOperatingSystemChanged(os);
}

void CoreProcess::setServerModifierMapping(bool active)
{
  if (m_serverModifierMapping == active)
    return;
  m_serverModifierMapping = active;
  Q_EMIT serverModifierMappingChanged(active);
}

void CoreProcess::start(std::optional<ProcessMode> processModeOption)
{
  if (m_processState == ProcessState::Started) {
    qCritical("not starting core process, already started");
    return;
  }

  if (m_mode == Settings::CoreMode::None) {
    qCritical("not starting core process, no core mode set");
    return;
  }

  QMutexLocker locker(&m_processMutex);

  const auto currentMode = Settings::value(Settings::Core::ProcessMode).value<ProcessMode>();
  const auto processMode = processModeOption.value_or(currentMode);
  const auto coreMode = QVariant::fromValue(m_mode).toString().toLower();

  qInfo().noquote() << QString("starting %1 process (%2 mode)").arg(coreMode, processModeToString(processMode));

  setProcessState(ProcessState::Starting);

#ifdef Q_OS_MACOS
  requestOSXNotificationPermission();
#endif

  setConnectionState(ConnectionState::Connecting);

  if (processMode == ProcessMode::Desktop) {
    m_process = new QProcess(this);
    connect(m_process, &QProcess::finished, this, &CoreProcess::onProcessFinished, Qt::UniqueConnection);
    connect(
        m_process, &QProcess::readyReadStandardOutput, this, &CoreProcess::onProcessReadyReadStandardOutput,
        Qt::UniqueConnection
    );
    connect(
        m_process, &QProcess::readyReadStandardError, this, &CoreProcess::onProcessReadyReadStandardError,
        Qt::UniqueConnection
    );
  }

  QStringList args = {coreMode};

  if (m_mode == Settings::CoreMode::Server) {
    const auto [hasNeededPermissions, configFilename] = persistServerConfig();
    if (configFilename.isEmpty()) {
      qCritical("not starting core process, no server config file");
      setProcessState(ProcessState::Stopped);
      setConnectionState(ConnectionState::Disconnected);
      Q_EMIT error(Error::StartFailed);
      return;
    }
    if (!hasNeededPermissions) {
      setProcessState(ProcessState::Stopped);
      setConnectionState(ConnectionState::Disconnected);
      Q_EMIT error(Error::StartFailed);
      return;
    }
    qInfo("core config file: %s", qPrintable(configFilename));
  }

  qDebug().noquote() << "log level:" << Settings::logLevelText();

  if (Settings::value(Settings::Log::ToFile).toBool()) {
    const auto logFile = Settings::value(Settings::Log::File).toString();
    QDir(QFileInfo(logFile).absolutePath()).mkpath(".");
    qInfo().noquote() << "log file:" << logFile;
  }

  // Wired before the start calls so it catches Started from both sync (desktop) and async (service) paths.
  connect(
      this, &CoreProcess::processStateChanged, this,
      [this](ProcessState state) {
        if (state != ProcessState::Started) {
          return;
        }

        // Delay briefly to give the core process time to start its IPC server.
        QTimer::singleShot(kRetryDelay, this, [this] {
          if (m_processState != ProcessState::Started) {
            return;
          }

          if (m_coreIpcClient) {
            m_coreIpcClient->disconnectFromServer();
            m_coreIpcClient->deleteLater();
            m_coreIpcClient = nullptr;
          }

          m_coreIpcClient = new ipc::CoreIpcClient(this);

          // queued so a modal dialog opened by a handler can't block readyRead and strand later messages
          connect(
              m_coreIpcClient, &ipc::CoreIpcClient::commandReceived, this, &CoreProcess::onCoreIpcMessageReceived,
              Qt::QueuedConnection
          );
          connect(m_coreIpcClient, &ipc::CoreIpcClient::connected, this, [this] {
            qDebug("connected to core ipc server");
            applyLogLevel();
            refreshScreenLayout();
          });
          connect(m_coreIpcClient, &ipc::CoreIpcClient::connectionFailed, this, &CoreProcess::onCoreIpcConnectionFailed);
          connect(m_coreIpcClient, &ipc::CoreIpcClient::serverShutdown, this, [this, client = m_coreIpcClient] {
            qDebug("core ipc server shut down cleanly");
            if (m_serverLayoutTransaction) {
              onCoreIpcConnectionFailed();
              return;
            }
            client->deleteLater();
            if (m_coreIpcClient == client)
              m_coreIpcClient = nullptr;
          });

          m_coreIpcClient->connectToServer();
        });
      },
      static_cast<Qt::ConnectionType>(Qt::SingleShotConnection | Qt::QueuedConnection)
  );

  if (processMode == ProcessMode::Desktop) {
    startForegroundProcess(args);
  } else if (processMode == ProcessMode::Service) {
    startProcessFromDaemon();
  }

  m_lastProcessMode = processMode;
}

void CoreProcess::stop(std::optional<ProcessMode> processModeOption)
{
  QMutexLocker locker(&m_processMutex);

  // A manual stop cancels a scheduled restart even when there is no process
  // left to terminate (for example, between automatic retry attempts).
  m_retryTimer.stop();

  const auto currentMode = Settings::value(Settings::Core::ProcessMode).value<ProcessMode>();
  const auto processMode = processModeOption.value_or(currentMode);

  qInfo("stopping core process (%s mode)", qPrintable(processModeToString(processMode)));

  if (m_coreIpcClient && processMode != ProcessMode::Desktop) {
    m_coreIpcClient->disconnectFromServer();
    m_coreIpcClient->deleteLater();
    m_coreIpcClient = nullptr;
  }

  if (processMode == ProcessMode::Desktop &&
      (!m_process || m_process->state() == QProcess::ProcessState::NotRunning)) {
    setProcessState(ProcessState::Stopped);
  } else if (m_processState == ProcessState::Starting) {
    qDebug("core process is starting, cancelling");
    setProcessState(ProcessState::Stopped);
  } else if (m_processState != ProcessState::Stopped) {
    setProcessState(ProcessState::Stopping);

    if (processMode == ProcessMode::Service) {
      stopProcessFromDaemon();
    } else if (processMode == ProcessMode::Desktop) {
      stopForegroundProcess();
    }

  } else {
    qWarning("core process already stopped");
  }

  setConnectionState(ConnectionState::Disconnected);
}

void CoreProcess::restart()
{
  qDebug("restarting core process");

  const auto processMode = Settings::value(Settings::Core::ProcessMode).value<ProcessMode>();

  const bool waitForProcess = m_process && m_process->state() != QProcess::ProcessState::NotRunning;

  if (waitForProcess) {
    qInfo("waiting for current desktop core to exit before restarting");

    connect(
        m_process, &QProcess::finished, this,
        [this](int, QProcess::ExitStatus) {
          qInfo("desktop core exited, restarting");
          start();
        },
        Qt::SingleShotConnection
    );
  }

  if (m_lastProcessMode != std::nullopt && m_lastProcessMode != processMode) {
    const auto debugMessage =
        QStringLiteral("process mode changed to %1, stopping %2 process")
            .arg(processModeToString(processMode), processModeToString(m_lastProcessMode.value()));
    qDebug().noquote() << debugMessage;
    stop(m_lastProcessMode);
  } else {
    // in service mode: though there is technically no need to stop the service
    // before restarting it, it does make for cleaner process state tracking,
    // especially if something goes wrong with starting the service.
    stop();
  }

  if (!waitForProcess) {
    start();
  }
}

void CoreProcess::cleanup()
{
  qInfo("cleaning up core process");

  const auto isDesktop = Settings::value(Settings::Core::ProcessMode).value<ProcessMode>() == ProcessMode::Desktop;
  const auto isRunning = m_processState == ProcessState::Started;
  if (isDesktop && isRunning) {
    stop();
  }
}

QPair<bool, QString> CoreProcess::persistServerConfig() const
{
  if (Settings::value(Settings::Server::ExternalConfig).toBool()) {
    return {Settings::isServerConfigFileReadable(), Settings::value(Settings::Server::ExternalConfigFile).toString()};
  }

  const auto configFilePath = Settings::defaultValue(Settings::Server::ExternalConfigFile).toString();
  QFile configFile(configFilePath);
  if (!configFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    qWarning() << "failed to open core config file for write:" << configFilePath;
    return {false, configFile.fileName()};
  }

  m_serverConfig.save(configFile);
  configFile.close();
  return {Settings::isServerConfigFileReadable(), configFile.fileName()};
}

void CoreProcess::setConnectionState(ConnectionState state)
{
  if (state == ConnectionState::Disconnected || state == ConnectionState::Connecting) {
    setSourceOperatingSystem(shortcuts::DeviceOS::Unknown);
    setServerModifierMapping(false);
    m_stdoutPending.clear();
    m_stderrPending.clear();
  }
  if ((state == ConnectionState::Disconnected || state == ConnectionState::Connecting) &&
      m_mode == Settings::CoreMode::Client) {
    if (m_screenLayout != deskflow::layout::Snapshot{}) {
      m_screenLayout = {};
      Q_EMIT screenLayoutChanged(m_screenLayout);
    }
    if (m_pendingLayoutRequestId) {
      m_pendingLayoutRequestId = 0;
      m_layoutRequestTimer.stop();
      Q_EMIT layoutRequestFinished(false, tr("连接已断开，请重新连接后调整布局。"));
    }
  }
  if (m_connectionState == state) {
    return;
  }

  m_connectionState = state;
  Q_EMIT connectionStateChanged(state);
}

void CoreProcess::setProcessState(ProcessState state)
{
  if (state == ProcessState::Stopped) {
    if (m_serverLayoutTransaction) {
      const auto transaction = *m_serverLayoutTransaction;
      m_serverLayoutTransaction.reset();
      m_layoutReconcileNeeded = false;
      m_layoutReconnectTimer.stop();
      Q_EMIT layoutApplyFinished(transaction.id, false, tr("服务端已停止，已恢复原布局。"));
    }
    if (m_screenLayout != deskflow::layout::Snapshot{}) {
      m_screenLayout = {};
      Q_EMIT screenLayoutChanged(m_screenLayout);
    }
    if (m_pendingLayoutRequestId) {
      m_pendingLayoutRequestId = 0;
      m_layoutRequestTimer.stop();
      Q_EMIT layoutRequestFinished(false, tr("连接服务已停止，布局更改未确认。"));
    }
  }
  if (m_processState == state) {
    return;
  }

  qDebug(
      "core process state changed: %s -> %s", //
      qPrintable(processStateToString(m_processState)), qPrintable(processStateToString(state))
  );
  m_processState = state;
  Q_EMIT processStateChanged(state);
}

void CoreProcess::onCoreIpcMessageReceived(const QString &command, const QString &args)
{
  using namespace deskflow::layout;
  if (command == "layoutQueryResult") {
    const auto object = decodeIpc(args);
    const auto id = object ? requestIdFromJson(*object) : std::nullopt;
    if (!object || !id || *id != m_layoutQueryId || !object->value("snapshot").isObject())
      return;
    const auto snapshot = snapshotFromJson(object->value("snapshot").toObject());
    if (snapshot) {
      if (*snapshot != m_screenLayout) {
        m_screenLayout = *snapshot;
        Q_EMIT screenLayoutChanged(m_screenLayout);
      }
      if (m_layoutReconcileNeeded)
        reconcileScreenLayout(*snapshot);
    }
    return;
  }
  if (command == "screenLayout") {
    const auto object = decodeIpc(args);
    if (!object)
      return;
    const auto snapshot = snapshotFromJson(*object);
    if (snapshot && *snapshot != m_screenLayout) {
      m_screenLayout = *snapshot;
      Q_EMIT screenLayoutChanged(m_screenLayout);
    }
    return;
  }
  if (command == "layoutMoveReceived") {
    const auto object = decodeIpc(args);
    const auto id = object ? requestIdFromJson(*object) : std::nullopt;
    const auto move = object && object->value("move").isObject() ? moveFromJson(object->value("move").toObject()) : std::nullopt;
    if (!id || !move || m_mode != Settings::CoreMode::Server) return;
    const auto desired = movedSnapshot(m_screenLayout, *move);
    m_serverLayoutTransaction = ServerLayoutTransaction{*id, m_screenLayout.direction,
        desired ? desired->direction : deskflow::layout::Direction::Unknown,
        m_screenLayout.serverName, m_screenLayout.clientName, move->expectedRevision,
        desired ? desired->revision : QString()};
    Q_EMIT layoutMoveReceived(*id, *move);
    return;
  }
  if (command == "layoutRequestReceived" || command == "layoutResult" || command == "layoutApplyFinished") {
    const auto object = decodeIpc(args);
    if (!object)
      return;
    const auto id = requestIdFromJson(*object);
    if (!id)
      return;
    if (command == "layoutRequestReceived") {
      const auto expected = object->value("expected").toInt(-1);
      const auto desired = object->value("desired").toInt(-1);
      if (m_mode == Settings::CoreMode::Server && isCardinal(static_cast<Direction>(expected)) &&
          isCardinal(static_cast<Direction>(desired)) && object->value("expected").toDouble() == expected &&
          object->value("desired").toDouble() == desired) {
        m_serverLayoutTransaction = ServerLayoutTransaction{*id, static_cast<Direction>(expected),
            static_cast<Direction>(desired), m_screenLayout.serverName, m_screenLayout.clientName};
        Q_EMIT layoutRequestReceived(*id, static_cast<Direction>(expected), static_cast<Direction>(desired));
      }
      return;
    }
    if (!object->value("success").isBool() || !object->value("error").isString() ||
        object->value("error").toString().size() > kMaxErrorLength)
      return;
    const bool success = object->value("success").toBool();
    const auto error = object->value("error").toString();
    if (command == "layoutApplyFinished") {
      if (m_mode == Settings::CoreMode::Server) {
        if (m_serverLayoutTransaction && m_serverLayoutTransaction->id == *id) {
          m_serverLayoutTransaction.reset();
          m_layoutReconcileNeeded = false;
          m_layoutReconnectTimer.stop();
        }
        Q_EMIT layoutApplyFinished(*id, success, error);
      }
    } else if (*id == m_pendingLayoutRequestId) {
      m_pendingLayoutRequestId = 0;
      m_layoutRequestTimer.stop();
      Q_EMIT layoutRequestFinished(success, error);
    }
    return;
  }
  if (command == "connectionState") {
    const auto metaEnum = QMetaEnum::fromType<ConnectionState>();
    bool ok = false;
    const auto state = static_cast<ConnectionState>(metaEnum.keyToValue(args.toUtf8().constData(), &ok));
    if (!ok) {
      qWarning("core ipc got unknown connection state: %s", args.toUtf8().constData());
      return;
    }
    setConnectionState(state);
  } else if (command == "connectedClients") {
    const auto clients = args.isEmpty() ? QStringList() : args.split(",");
    Q_EMIT connectedClientsChanged(clients);
  } else if (command == "secureSocket") {
    Q_EMIT secureSocket(true);
    if (args != m_secureSocketVersion) {
      m_secureSocketVersion = args;
      Q_EMIT securityLevelChanged(args);
    }
  } else if (command == "unrecognisedClient") {
    Q_EMIT unrecognisedClient(args);
  } else if (command == "connectionRefused") {
    const auto metaEnum = QMetaEnum::fromType<deskflow::core::ConnectionRefusal>();
    bool ok = false;
    const auto reason =
        static_cast<deskflow::core::ConnectionRefusal>(metaEnum.keyToValue(args.toUtf8().constData(), &ok));
    if (ok) {
      Q_EMIT connectionRefused(reason);
    } else {
      qWarning("core ipc got unknown connection refusal: %s", args.toUtf8().constData());
    }
  } else if (command == "retryIn") {
    Q_EMIT retryIn(args.toInt());
  } else if (command == "peerFingerprint") {
    Q_EMIT peerFingerprint(args);
  } else if (command == "missingKeyboardLayouts") {
    Q_EMIT missingKeyboardLayouts(args);
  }
}

bool CoreProcess::checkSecureSocket(const QString &line)
{
  static const QString tlsCheckString = "network encryption protocol: ";
  const auto index = line.indexOf(tlsCheckString, 0, Qt::CaseInsensitive);
  if (index == -1) {
    return false;
  }

  Q_EMIT secureSocket(true);
  if (const auto ssv = line.mid(index + tlsCheckString.size()); ssv != m_secureSocketVersion) {
    m_secureSocketVersion = ssv;
    Q_EMIT securityLevelChanged(ssv);
  }

  return true;
}

QString CoreProcess::correctedAddress(const QString &address) const
{
  return wrapIpv6(address.simplified());
}

void CoreProcess::setupDaemonLogTail(const QString &logPath)
{
  qDebug() << "daemon log path:" << logPath;

  if (QFileInfo logFile(logPath); !logFile.isFile()) {
    auto file = QFile(logPath);
    if (!file.open(QFile::ReadWrite)) {
      qCritical() << "daemon log path file can not be written:" << logPath;
      return;
    }
    file.write(""); // Create an empty file
  }

  if (m_daemonFileTail) {
    m_daemonFileTail->setWatchedFile(logPath);
  } else {
    m_daemonFileTail = new FileTail(logPath, this);
    connect(m_daemonFileTail, &FileTail::newLine, this, &CoreProcess::handleLogLines);
  }
}

void CoreProcess::clearSettings()
{
  const auto processMode = Settings::value(Settings::Core::ProcessMode).value<ProcessMode>();
  if (processMode == ProcessMode::Desktop) {
    qDebug("no core settings to clear in desktop mode");
    return;
  }

  if (processMode != ProcessMode::Service) {
    qCritical("not clearing core settings, unexpected process mode");
    return;
  }

  qInfo("clearing core settings through daemon");
  m_daemonIpcClient->sendClearSettings();
}

void CoreProcess::retryDaemon()
{
  m_daemonIpcClient->connectToServer();
}

} // namespace deskflow::gui

namespace deskflow::gui {
void CoreProcess::refreshScreenLayout()
{
  if (m_coreIpcClient && m_coreIpcClient->isConnected()) {
    if (++m_layoutQueryId == 0) ++m_layoutQueryId;
    m_coreIpcClient->sendLayoutCommand(QStringLiteral("layoutQuery"),
        deskflow::layout::encodeIpc({{"id", static_cast<double>(m_layoutQueryId)}}));
  }
}

bool CoreProcess::requestLayout(deskflow::layout::Direction direction)
{
  if (!deskflow::layout::isCardinal(direction) || m_screenLayout.screens.size() != 2) return false;
  return requestLayoutMove({m_screenLayout.clientName,
      direction == deskflow::layout::Direction::Left ? -1 : direction == deskflow::layout::Direction::Right ? 1 : 0,
      direction == deskflow::layout::Direction::Above ? -1 : direction == deskflow::layout::Direction::Below ? 1 : 0,
      m_screenLayout.revision});
}

bool CoreProcess::requestLayoutMove(const deskflow::layout::Move &move)
{
  if (m_screenLayout.protocolVersion != 2 || !m_screenLayout.editable || m_pendingLayoutRequestId ||
      !m_coreIpcClient || !m_coreIpcClient->isConnected() || !deskflow::layout::movedSnapshot(m_screenLayout, move))
    return false;
  if (++m_nextLayoutRequestId == 0) ++m_nextLayoutRequestId;
  m_pendingLayoutRequestId = m_nextLayoutRequestId;
  m_coreIpcClient->sendLayoutCommand(QStringLiteral("layoutMove"), deskflow::layout::encodeIpc(
      {{"id", static_cast<double>(m_pendingLayoutRequestId)}, {"move", deskflow::layout::moveToJson(move)}}));
  m_layoutRequestTimer.start();
  return true;
}

void CoreProcess::onCoreIpcConnectionFailed()
{
  qWarning("failed to establish core ipc connection");
  if (!m_serverLayoutTransaction)
    return;
  m_layoutRequestTimer.stop();
  if (!m_layoutReconcileNeeded) {
    m_layoutReconcileNeeded = true;
    m_layoutReconnectAttempts = 0;
    Q_EMIT layoutSynchronizationUncertain(tr("与服务端的通信中断，正在重新确认实际布局。"));
  }
  if (!m_layoutReconnectTimer.isActive() && m_layoutReconnectAttempts <= 3)
    m_layoutReconnectTimer.start();
}

void CoreProcess::reconcileScreenLayout(const deskflow::layout::Snapshot &snapshot)
{
  if (!m_serverLayoutTransaction || !m_layoutReconcileNeeded)
    return;
  const auto transaction = *m_serverLayoutTransaction;
  const bool fullTopology = !transaction.expectedRevision.isEmpty();
  const bool knownState = fullTopology
      ? snapshot.serverName == transaction.serverName &&
        (snapshot.revision == transaction.expectedRevision || snapshot.revision == transaction.desiredRevision)
      : snapshot.serverName == transaction.serverName && snapshot.clientName == transaction.clientName &&
        (snapshot.direction == transaction.expected || snapshot.direction == transaction.desired);
  if (!knownState) {
    Q_EMIT layoutSynchronizationUncertain(tr("服务端布局已另外更改，无法自动确认本次调整。请停止共享后重新检查布局。"));
    return;
  }
  const bool applied = fullTopology ? snapshot.revision == transaction.desiredRevision : snapshot.direction == transaction.desired;
  m_serverLayoutTransaction.reset();
  m_layoutReconcileNeeded = false;
  m_layoutReconnectTimer.stop();
  const auto error = applied ? QString() : tr("服务端未应用本次调整，已恢复原布局。");
  Q_EMIT layoutApplyFinished(transaction.id, applied, error);
  if (m_pendingLayoutRequestId) {
    m_pendingLayoutRequestId = 0;
    m_layoutRequestTimer.stop();
    Q_EMIT layoutRequestFinished(applied, error);
  }
}

void CoreProcess::completeLayoutRequest(quint32 requestId, bool success, const QString &error)
{
  if (requestId && m_mode == Settings::CoreMode::Server && m_coreIpcClient && m_coreIpcClient->isConnected())
    m_coreIpcClient->sendLayoutCommand(QStringLiteral("layoutComplete"), deskflow::layout::encodeIpc(
        {{"id", static_cast<double>(requestId)}, {"success", success},
         {"error", error.left(deskflow::layout::kMaxErrorLength)}}));
}
} // namespace deskflow::gui
