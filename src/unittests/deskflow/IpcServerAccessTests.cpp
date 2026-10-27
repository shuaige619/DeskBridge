/*
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "base/Log.h"
#include "deskflow/ipc/CoreIpc.h"
#include "deskflow/ipc/CoreIpcServer.h"
#include "deskflow/ipc/IpcServer.h"

#include <QFile>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>

using deskflow::core::ipc::IpcServer;

namespace {
QString uniqueName()
{
  // A short fresh name stays within the macOS Unix-domain path length limit.
  return QStringLiteral("deskbridge-ipc-test-%1").arg(QUuid::createUuid().toString(QUuid::Id128).left(16));
}

class ProbeServer : public IpcServer
{
public:
  explicit ProbeServer(const QString &name) : IpcServer(nullptr, name, QStringLiteral("test")) {}
  ProbeServer(const QString &name, AccessPolicy access) : IpcServer(nullptr, name, QStringLiteral("test"), access) {}
  mutable int identityChecks = 0;
  bool denyPeer = false;
  int commands = 0;
  bool nativeIdentityPassed = false;

protected:
  bool peerIdentityAllowed(QLocalSocket *socket) const override
  {
    ++identityChecks;
    return !denyPeer && IpcServer::peerIdentityAllowed(socket);
  }

private:
  void processCommand(QLocalSocket *socket, const QString &command, const QStringList &) override
  {
    ++commands;
    nativeIdentityPassed = identityChecks > 0;
    writeToClientSocket(socket, command + QStringLiteral("=accepted"));
  }
};
} // namespace

class IpcServerAccessTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void lateClientCanQueryCurrentDeviceContextAndDisconnectClearsIt()
  {
    using deskflow::core::ConnectionState;
    using deskflow::core::ipc::CoreIpcServer;
    using deskflow::shortcuts::DeviceOS;
    CoreIpcServer server(nullptr);
    // Do not call CoreIpcServer::listen(): its fixed runtime socket may belong
    // to an installed app. Exercise the actual command handler over an isolated
    // transport while leaving any real connection untouched.
    QLocalServer transport;
    const auto name = uniqueName();
    QVERIFY(transport.listen(name));
    QLocalSocket client;
    client.connectToServer(name);
    QVERIFY(client.waitForConnected(2000));
    QTRY_VERIFY(transport.hasPendingConnections());
    auto *peer = transport.nextPendingConnection();
    QVERIFY(peer);

    ipcSendConnectionState(ConnectionState::Connecting);
    ipcPublishSourceOperatingSystem(DeviceOS::Linux);
    ipcPublishServerModifierMapping(true);
    ipcSendConnectionState(ConnectionState::Connected);
    QCoreApplication::sendPostedEvents(&server, QEvent::MetaCall);
    const auto query = [&] {
      server.processCommand(peer, QStringLiteral("deviceContextQuery"), {QStringLiteral("deviceContextQuery")});
      peer->flush();
    };
    query();
    QTRY_VERIFY(client.canReadLine());
    QCOMPARE(client.readLine(), QByteArray("sourceOperatingSystem=linux\n"));
    QTRY_VERIFY(client.canReadLine());
    QCOMPARE(client.readLine(), QByteArray("serverModifierMapping=active\n"));
    // Querying must not consume the cache; another late GUI needs the same
    // current metadata independently of the historical broadcast queue.
    query();
    QTRY_VERIFY(client.canReadLine());
    QCOMPARE(client.readLine(), QByteArray("sourceOperatingSystem=linux\n"));
    QTRY_VERIFY(client.canReadLine());
    QCOMPARE(client.readLine(), QByteArray("serverModifierMapping=active\n"));

    ipcSendConnectionState(ConnectionState::Disconnected);
    QCoreApplication::sendPostedEvents(&server, QEvent::MetaCall);
    query();
    QTRY_VERIFY(client.canReadLine());
    QCOMPARE(client.readLine(), QByteArray("sourceOperatingSystem=unknown\n"));
    QTRY_VERIFY(client.canReadLine());
    QCOMPARE(client.readLine(), QByteArray("serverModifierMapping=none\n"));
  }

  void sameUserCanExchangeCommandsAndReceiveBroadcasts()
  {
    const auto name = uniqueName();
    ProbeServer server(name, IpcServer::AccessPolicy::SameUser);
    server.listen();
    QLocalSocket client;
    client.connectToServer(name);
    QVERIFY(client.waitForConnected(2000));
    client.write("probe\n"); client.flush();
    QTRY_COMPARE(server.commands, 1);
    QVERIFY(server.nativeIdentityPassed);
    QCOMPARE(server.identityChecks, 1);
    QTRY_VERIFY(client.canReadLine());
    QCOMPARE(client.readLine(), QByteArray("probe=accepted\n"));
    server.broadcastCommand(QStringLiteral("status"), QStringLiteral("safe"));
    QTRY_VERIFY(client.canReadLine());
    QCOMPARE(client.readLine(), QByteArray("status=safe\n"));
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    const QFileInfo socketFile(client.fullServerName());
    QVERIFY(socketFile.exists());
    const auto publicPermissions = QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup |
                                   QFile::ReadOther | QFile::WriteOther | QFile::ExeOther;
    QVERIFY(!(socketFile.permissions() & publicPermissions));
#endif
  }

  void rejectedIdentityCannotSendCommandsOrReceiveBroadcasts()
  {
    const auto name = uniqueName();
    ProbeServer server(name, IpcServer::AccessPolicy::SameUser);
    server.denyPeer = true;
    server.listen();
    QLocalSocket client;
    client.connectToServer(name);
    QVERIFY(client.waitForConnected(2000));
    // Queue data before the server processes its accept event. The identity
    // gate must run before either buffered input or broadcasts can reach it.
    client.write("probe\n"); client.flush();
    QTRY_COMPARE(server.identityChecks, 1);
    QTRY_COMPARE(client.state(), QLocalSocket::UnconnectedState);
    server.broadcastCommand(QStringLiteral("status"), QStringLiteral("private"));
    QCoreApplication::processEvents();
    QCOMPARE(server.commands, 0);
    QCOMPARE(client.bytesAvailable(), 0);
  }

  void legacyDefaultDoesNotApplyTheDesktopIdentityGate()
  {
    const auto name = uniqueName();
    ProbeServer server(name);
    server.denyPeer = true;
    server.listen();
    QLocalSocket client;
    client.connectToServer(name);
    QVERIFY(client.waitForConnected(2000));
    client.write("probe\n"); client.flush();
    QTRY_COMPARE(server.commands, 1);
    QCOMPARE(server.identityChecks, 0);
    QTRY_VERIFY(client.canReadLine());
    QCOMPARE(client.readLine(), QByteArray("probe=accepted\n"));
  }

private:
  Log m_log;
};

QTEST_GUILESS_MAIN(IpcServerAccessTests)
#include "IpcServerAccessTests.moc"
