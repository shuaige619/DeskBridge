/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#pragma once
#include <QObject>
#include <QTemporaryDir>

class ShortcutApplyControllerTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void disconnectedApplyDoesNotStartSharing();
  void connectedApplyWaitsForReconnect();
  void retryPendingApplySavesAndResumesSharing();
  void startFailureRestoresPreviousRules();
  void connectionTimeoutRestoresPreviousRules();
  void failedWriteKeepsPreviousRulesAndRestarts();
  void duplicateApplyDoesNotStartSecondTransaction();
  void invalidRulesDoNotStopSharing();
private:
  QTemporaryDir m_files;
};
