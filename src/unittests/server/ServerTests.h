/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include <QTest>
#include "base/Log.h"
#include "arch/Arch.h"
#include <QTemporaryDir>

class ServerTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void screenLayout_appliesOnlyAfterMatchingPersistedCompletion();
  void screenTopology_movesThreeAndFourNodeGraphs();
  void screenTopology_publishesCanonicalIdentityAndUpdatedGraphToAllPeers();
  void screenTopology_rejectsStaleCollisionAndDisconnectedMoves();
  void screenLayout_rejectsStaleMalformedAndUnavailableRequests();
  void screenLayout_timeoutAndControllerLossKeepRuntimeUnchanged();
  void screenLayout_invalidSavedConfigKeepsRoutingAndKeyHandlers();
  void SwitchToComputerInfo_alloc_computer();
  void KeyboardBroadcastInfo_alloc_stateAndComputers();
private:
  Arch m_arch;
  Log m_log;
  QTemporaryDir m_files;
};
