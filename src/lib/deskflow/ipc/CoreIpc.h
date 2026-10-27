/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "common/Enums.h"
#include "common/ScreenLayout.h"
#include "common/ShortcutRules.h"
#include "deskflow/ScreenLayoutProtocol.h"

#include <QString>

void ipcSendToClient(const QString &command, const QString &args = "");
void ipcSendConnectionState(deskflow::core::ConnectionState state);
void ipcPublishSourceOperatingSystem(deskflow::shortcuts::DeviceOS os);
void ipcPublishServerModifierMapping(bool active);

class IEventQueue;
void ipcSetLayoutTarget(IEventQueue *events, void *target);
void ipcClearLayoutTarget(void *target);
void ipcPostLayoutCommand(const deskflow::layout::Command &command);
void ipcPublishScreenLayout(const deskflow::layout::Snapshot &snapshot);
