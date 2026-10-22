// SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
// SPDX-License-Identifier: MIT
#pragma once

#include <QSettings>
#include <QString>
#include <QVariant>

// Only the production identity helper's read interface is replaced. Every
// read still uses the real platform QSettings INI implementation.
class Settings
{
public:
  struct Core
  {
    inline static const QString ComputerName = QStringLiteral("core/computerName");
  };
  struct Computer
  {
    inline static const QString Name = QStringLiteral("computer_%1/name");
  };

  static QVariant value(const QString &key)
  {
    QSettings saved(s_file, QSettings::IniFormat);
    return saved.value(key);
  }
  static QString settingsFile() { return s_file; }
  static void setSettingsFile(const QString &path) { s_file = path; }

private:
  inline static QString s_file;
};
