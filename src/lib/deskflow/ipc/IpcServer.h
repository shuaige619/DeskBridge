/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QObject>
#include <QHash>
#include <QSet>

class QLocalServer;
class QLocalSocket;

namespace deskflow::core::ipc {

class IpcServer : public QObject
{
  Q_OBJECT

public:
  enum class AccessPolicy { SystemWide, SameUser };
  explicit IpcServer(QObject *parent, const QString &serverName, const QString &typeName,
                     AccessPolicy accessPolicy = AccessPolicy::SystemWide);
  ~IpcServer() override;

  void listen();
  void broadcastCommand(const QString &command, const QString &args = "");

Q_SIGNALS:
  void logLevelChanged(const QString &logLevel);
  void configFileChanged(const QString &configFile);
  void startProcessRequested();
  void stopProcessRequested();
  void clearSettingsRequested();

protected:
  // The native peer check complements socket ACLs, which Qt ignores on macOS.
  virtual bool peerIdentityAllowed(QLocalSocket *clientSocket) const;
  /**!
   * Write a message to the client socket and append a newline character.
   *
   * \param clientSocket The client socket to write to.
   * \param message The message to write (without trailing newline).
   */
  void writeToClientSocket(QLocalSocket *&clientSocket, const QString &message) const;

private:
  void processMessage(QLocalSocket *clientSocket, const QString &message);
  virtual void processCommand(QLocalSocket *clientSocket, const QString &command, const QStringList &parts) = 0;
  void handleNewConnection();
  void handleReadyRead();
  void handleDisconnected();
  void handleErrorOccurred();

  QLocalServer *m_server;
  AccessPolicy m_accessPolicy;
  QSet<QLocalSocket *> m_clients;
  QHash<QLocalSocket *, QByteArray> m_readBuffers;
  QString m_serverName;
  QStringList m_pendingMessages;
  QByteArray m_typeName;
};

} // namespace deskflow::core::ipc
