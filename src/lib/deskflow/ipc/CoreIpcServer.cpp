/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CoreIpcServer.h"

#include "base/Log.h"
#include "deskflow/ipc/CoreIpc.h"
#include "common/Constants.h"
#include "common/LogLevel.h"

#include <QLocalSocket>

namespace deskflow::core::ipc {

static CoreIpcServer *s_instance = nullptr;

CoreIpcServer::CoreIpcServer(QObject *parent)
    : IpcServer(parent, kCoreIpcName, QStringLiteral("core"),
#if DESKBRIDGE_PERSONAL_BUILD
                AccessPolicy::SameUser
#else
                AccessPolicy::SystemWide
#endif
      )
{
  assert(s_instance == nullptr);
  s_instance = this;
}

CoreIpcServer::~CoreIpcServer()
{
  s_instance = nullptr;
}

CoreIpcServer &CoreIpcServer::instance()
{
  assert(s_instance != nullptr);
  return *s_instance;
}

CoreIpcServer *CoreIpcServer::existingInstance()
{
  return s_instance;
}

void CoreIpcServer::refreshLayoutTarget()
{
  deskflow::layout::Command query;
  query.controllerAvailable = !m_layoutControllers.isEmpty();
  ipcPostLayoutCommand(query);
}

void CoreIpcServer::setScreenLayout(const deskflow::layout::Snapshot &snapshot)
{
  m_screenLayout = snapshot;
  broadcastCommand(QStringLiteral("screenLayout"), deskflow::layout::encodeIpc(deskflow::layout::toJson(snapshot)));
}

void CoreIpcServer::processCommand(QLocalSocket *clientSocket, const QString &command, const QStringList &parts)
{
  if (command == QStringLiteral("logLevel")) {
    if (parts.size() != 2 || !LogLevel::logLevelOptions().contains(parts.at(1), Qt::CaseInsensitive)) return;
    CLOG->setFilter(parts.at(1).toUpper());
    writeToClientSocket(clientSocket, QStringLiteral("ok"));
    return;
  }
  if (command == QStringLiteral("stop")) {
    LOG_DEBUG("core ipc server got stop message");
    writeToClientSocket(clientSocket, QStringLiteral("ok"));
    broadcastCommand(QStringLiteral("bye"));
    Q_EMIT stopProcessRequested();
    return;
  }
  using namespace deskflow::layout;
  if (command == QStringLiteral("layoutQuery")) {
    quint32 queryId = 0;
    if (parts.size() > 1) {
      if (parts.size() != 2) return;
      const auto query = decodeIpc(parts.at(1));
      const auto id = query ? requestIdFromJson(*query) : std::nullopt;
      if (!id) return;
      queryId = *id;
    }
    if (!m_layoutControllers.contains(clientSocket)) {
      m_layoutControllers.insert(clientSocket);
      const auto removeController = [this, clientSocket] {
        m_layoutControllers.remove(clientSocket);
        refreshLayoutTarget();
      };
      connect(clientSocket, &QLocalSocket::disconnected, this, removeController);
      connect(clientSocket, &QObject::destroyed, this, removeController);
    }
    writeToClientSocket(clientSocket, QStringLiteral("screenLayout=%1").arg(encodeIpc(toJson(m_screenLayout))));
    Command query;
    query.id = queryId;
    query.controllerAvailable = true;
    ipcPostLayoutCommand(query);
    return;
  }
  if (command == QStringLiteral("layoutRequest") || command == QStringLiteral("layoutMove") ||
      command == QStringLiteral("layoutComplete")) {
    if (parts.size() != 2 || !m_layoutControllers.contains(clientSocket))
      return;
    const auto object = decodeIpc(parts.at(1));
    if (!object)
      return;
    const auto id = requestIdFromJson(*object);
    if (!id)
      return;
    Command request;
    request.id = *id;
    if (command == QStringLiteral("layoutMove")) {
      if (!object->value("move").isObject()) return;
      const auto move = moveFromJson(object->value("move").toObject());
      if (!move) return;
      request.kind = Command::Kind::Move;
      request.move = *move;
    } else if (command == QStringLiteral("layoutRequest")) {
      const auto expected = object->value("expected").toInt(-1);
      const auto desired = object->value("desired").toInt(-1);
      if (!validRequest(*id, expected, desired) || object->value("expected").toDouble() != expected ||
          object->value("desired").toDouble() != desired)
        return;
      request.kind = Command::Kind::Request;
      request.expected = static_cast<Direction>(expected);
      request.desired = static_cast<Direction>(desired);
    } else {
      if (!object->value("success").isBool() || !object->value("error").isString() ||
          object->value("error").toString().size() > kMaxErrorLength)
        return;
      request.kind = Command::Kind::Complete;
      request.success = object->value("success").toBool();
      request.error = object->value("error").toString();
    }
    ipcPostLayoutCommand(request);
    return;
  }
  LOG_WARN("core ipc server got unknown command: %s", command.toUtf8().constData());
}

} // namespace deskflow::core::ipc
