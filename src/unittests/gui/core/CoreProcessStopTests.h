/*
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <QObject>
#include <QTemporaryDir>

class CoreProcessStopTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void stopWithoutLiveProcess_data();
  void stopWithoutLiveProcess();
  void layoutServerStopRollsBackPendingApply();
  void topologyReconciliationUsesWholeGraphRevision();
  void layoutReconciliationRequiresFreshConfirmedQuery_data();
  void layoutReconciliationRequiresFreshConfirmedQuery();
  void sourceMetadataLogs();
  void sourceMetadataChunkBoundaries();
  void sourceMetadataResets_data();
  void sourceMetadataResets();

private:
  QTemporaryDir m_files;
};
