/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "IpcServer.h"
#include "common/ScreenLayout.h"
#include "common/ShortcutRules.h"

#include <QObject>
#include <QSet>

class QLocalSocket;
class IpcServerAccessTests;

namespace deskflow::core::ipc {

class CoreIpcServer : public IpcServer
{
  Q_OBJECT

public:
  explicit CoreIpcServer(QObject *parent);
  ~CoreIpcServer() override;

  static CoreIpcServer &instance();
  static CoreIpcServer *existingInstance();
  void setScreenLayout(const deskflow::layout::Snapshot &snapshot);
  void setSourceOperatingSystem(deskflow::shortcuts::DeviceOS os);
  void setServerModifierMapping(bool active);
  void refreshLayoutTarget();

private:
  friend class ::IpcServerAccessTests;
  deskflow::layout::Snapshot m_screenLayout;
  deskflow::shortcuts::DeviceOS m_sourceOperatingSystem = deskflow::shortcuts::DeviceOS::Unknown;
  bool m_serverModifierMapping = false;
  QSet<QLocalSocket *> m_layoutControllers;
  void processCommand(QLocalSocket *clientSocket, const QString &command, const QStringList &parts) override;
};

} // namespace deskflow::core::ipc
