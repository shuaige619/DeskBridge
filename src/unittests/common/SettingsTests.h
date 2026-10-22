/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "common/Settings.h"

#include <QTest>
#include <QTemporaryDir>

class SettingsTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  // Test are run in order top to bottom
  void setSettingsFile();
  void setStateFile();
  void settingsFile();
  void settingsPath();
  void tlsDir();
  void certificatePathMigration_data();
  void certificatePathMigration();
  void certificateMigrationPreservesDanglingSymlink();
  void certificateMigrationRejectsUnreadableConfiguration();
  void tlsTrustedServersDb();
  void tlsTrustedClientsDb();
  void checkValidSettings();
  void checkClipboardShortcutPreferences();
  void checkCleanComputerName();
  void checkCleanComputerName_LongName();
  void checkClearState();
  void iniCaseMigrationKeepsEqualValuesAndRestoresKeySpelling();

private:
  QTemporaryDir m_initialProfile;
  inline static const QString m_settingsPathTemp = QStringLiteral("tmp/test");
  inline static const QString m_settingsFile = QStringLiteral("%1/Deskflow.conf").arg(m_settingsPathTemp);
  inline static const QString m_stateFile = QStringLiteral("%1/Deskflow.state").arg(m_settingsPathTemp);

};
