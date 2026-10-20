/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CoreIpc.h"

#include "CoreIpcServer.h"

#include <QMetaEnum>
#include <QMutexLocker>
#include "base/IEventQueue.h"

void ipcSendToClient(const QString &command, const QString &args)
{
  // Queued because callers may not be on the main thread,
  // and QLocalSocket can only be written to from its owning thread.
  auto &server = deskflow::core::ipc::CoreIpcServer::instance();
  QMetaObject::invokeMethod(
      &server, [command, args] { deskflow::core::ipc::CoreIpcServer::instance().broadcastCommand(command, args); },
      Qt::QueuedConnection
  );
}

void ipcSendConnectionState(deskflow::core::ConnectionState state)
{
  const auto metaEnum = QMetaEnum::fromType<deskflow::core::ConnectionState>();
  ipcSendToClient(QStringLiteral("connectionState"), metaEnum.valueToKey(static_cast<int>(state)));
}

namespace {
QMutex s_layoutMutex;
IEventQueue *s_layoutEvents = nullptr;
void *s_layoutTarget = nullptr;
}

void ipcSetLayoutTarget(IEventQueue *events, void *target)
{
  QMutexLocker lock(&s_layoutMutex);
  s_layoutEvents = events;
  s_layoutTarget = target;
  if (auto *server = deskflow::core::ipc::CoreIpcServer::existingInstance())
    QMetaObject::invokeMethod(server, [server] { server->refreshLayoutTarget(); }, Qt::QueuedConnection);
}

void ipcClearLayoutTarget(void *target)
{
  QMutexLocker lock(&s_layoutMutex);
  if (s_layoutTarget == target) {
    s_layoutEvents = nullptr;
    s_layoutTarget = nullptr;
  }
}

void ipcPostLayoutCommand(const deskflow::layout::Command &command)
{
  QMutexLocker lock(&s_layoutMutex);
  if (!s_layoutEvents || !s_layoutTarget)
    return;
  auto *copy = new deskflow::layout::Command;
  copy->kind = command.kind;
  copy->id = command.id;
  copy->expected = command.expected;
  copy->desired = command.desired;
  copy->move = command.move;
  copy->success = command.success;
  copy->controllerAvailable = command.controllerAvailable;
  copy->error = command.error;
  s_layoutEvents->addEvent(Event(EventTypes::ScreenLayoutCommand, s_layoutTarget, copy));
}

void ipcPublishScreenLayout(const deskflow::layout::Snapshot &snapshot)
{
  auto *server = deskflow::core::ipc::CoreIpcServer::existingInstance();
  if (!server)
    return;
  QMetaObject::invokeMethod(server, [server, snapshot] { server->setScreenLayout(snapshot); }, Qt::QueuedConnection);
}
