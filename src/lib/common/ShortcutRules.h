/*
 * DeskBridge -- personal keyboard shortcut preferences
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "deskflow/KeyTypes.h"

#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <optional>

namespace deskflow::shortcuts {

enum class DeviceOS { Unknown = 0, MacOS = 1, Windows = 2, Linux = 3 };
enum class Habit { Custom = 0, Automatic = 1, Mac = 2, PC = 3 };

struct ModifierMapping
{
  KeyModifierMask source = 0;
  KeyModifierMask target = 0;
  bool operator==(const ModifierMapping &) const = default;
};

struct Chord
{
  KeyID key = kKeyNone;
  KeyModifierMask modifiers = 0;
  bool operator==(const Chord &) const = default;
};

struct Rule
{
  QString id;
  QString name;
  Chord input;
  Chord output;
  bool enabled = true;
  QStringList apps;
  bool operator==(const Rule &) const = default;
};

struct Configuration
{
  bool enabled = true;
  QStringList apps;
  QList<Rule> rules;
  Habit habit = Habit::Custom;
  DeviceOS sourceOS = DeviceOS::Unknown;
  bool fullShortcuts = false;
  bool allApplications = false;
  // Independent remote-input gesture. macOS translates an isolated Shift tap
  // to Ctrl+Space; other targets retain their native input-method behavior.
  bool shiftTapInputMethod = false;
  bool operator==(const Configuration &) const = default;
};

// Exact rules use only the intersection of global apps and rule.apps; an empty
// list never means all apps. Only the full-shortcut fallback can opt into
// allApplications. Rules retain apps temporarily removed from the boundary.
Configuration defaults();
QList<Rule> defaultRules(const QStringList &apps);
Configuration load(QString *error = nullptr);
bool save(const Configuration &config, QString *error = nullptr);
QString validate(const Configuration &config);
QString toJson(const Configuration &config);
std::optional<Configuration> fromJson(const QString &json, QString *error = nullptr);
DeviceOS localOS();
QString deviceOSName(DeviceOS os);
QString habitName(Habit habit);
DeviceOS effectiveSourceOS(const Configuration &config, DeviceOS detected);
std::optional<ModifierMapping> shortcutModifierMapping(
    const Configuration &config, DeviceOS detected = DeviceOS::Unknown, DeviceOS target = localOS());
Configuration effectiveConfiguration(
    const Configuration &config, DeviceOS detected = DeviceOS::Unknown, DeviceOS target = localOS());
void setHabit(Configuration &config, Habit habit);
QString habitDescription(
    const Configuration &config, DeviceOS detected = DeviceOS::Unknown, DeviceOS target = localOS());
QString chordText(const Chord &chord, DeviceOS os = localOS());
KeyID normalizedKey(KeyID key);
bool isReservedInput(const Chord &chord);

} // namespace deskflow::shortcuts

Q_DECLARE_METATYPE(deskflow::shortcuts::DeviceOS)
Q_DECLARE_METATYPE(deskflow::shortcuts::Habit)
