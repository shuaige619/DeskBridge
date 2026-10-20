/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/Log.h"

#include <QObject>
#include <QTemporaryDir>

class ServerProxyTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void handleKeepAliveAlarm_timeout_queuesDisconnectRequest();
  void handleData_incompleteMessage_queuesDisconnectRequest();
  void parseHandshakeMessage_protocolError_queuesRefusalRequest();
  void sourceOptions_wireRoundTrip();
  void screenLayout_capabilityGuardsWireCommands();
  void screenLayout_v2TopologyRoundTripAndBounds();
  void screenLayout_acceptsNegotiatedSnapshot();
  void screenLayout_rejectsOversizedAndWrongIdentity();
  void screenLayout_validatesMetadataAndRequests();
  void mouseMoves_mixedModes_preserveWireOrder_data();
  void mouseMoves_mixedModes_preserveWireOrder();
  void mouseMoves_sameMode_coalesce_data();
  void mouseMoves_sameMode_coalesce();
  void mouseMoves_alternatingGroups_keepDeltasAndBoundaries();
  void mouseMoves_inputBoundaries_flushBeforeAction_data();
  void mouseMoves_inputBoundaries_flushBeforeAction();
  void mouseMoves_noop_keepsCoalescingAndReplies();
  void mouseMoves_infoChange_ignoresUntilAcknowledged();
  void mouseMoves_enter_discardsPreviousPosition();
  void clipboard_flushesMouseBeforeReadingPayload_data();
  void clipboard_flushesMouseBeforeReadingPayload();

private:
  Log m_log;
  QTemporaryDir m_settingsDirectory;
};
