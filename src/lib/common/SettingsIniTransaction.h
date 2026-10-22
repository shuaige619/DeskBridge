/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#pragma once

#include <QSet>
#include <QSettings>
#include <QStringList>
#include <QVariantMap>

namespace deskflow::settings_detail {
#ifdef Q_OS_WIN
inline constexpr auto iniKeyCaseSensitivity = Qt::CaseInsensitive;
#else
inline constexpr auto iniKeyCaseSensitivity = Qt::CaseSensitive;
#endif

inline QSet<QString> caseChangeWrites(
    const QVariantMap &values, Qt::CaseSensitivity sensitivity = iniKeyCaseSensitivity
)
{
  QSet<QString> result;
  if (sensitivity == Qt::CaseSensitive)
    return result;
  QSet<QString> removed;
  for (auto it = values.cbegin(); it != values.cend(); ++it)
    if (!it.value().isValid())
      removed.insert(it.key().toLower());
  for (auto it = values.cbegin(); it != values.cend(); ++it)
    if (it.value().isValid() && removed.contains(it.key().toLower()))
      result.insert(it.key());
  return result;
}

inline QVariantMap snapshotStoredValues(
    const QSettings &settings, const QStringList &keys, Qt::CaseSensitivity sensitivity = iniKeyCaseSensitivity
)
{
  QVariantMap result;
  QSet<QString> seen;
  QSet<QString> aliases;
  for (const auto &key : keys) {
    const auto folded = key.toLower();
    if (sensitivity == Qt::CaseInsensitive && seen.contains(folded))
      aliases.insert(folded);
    seen.insert(folded);
  }
  const auto storedKeys = settings.allKeys();
  for (const auto &key : keys) {
    // Only migration pairs need exact spelling: contains()/value() otherwise
    // make the not-yet-created destination look like the existing source.
    if (aliases.contains(key.toLower()) && !storedKeys.contains(key, Qt::CaseSensitive)) {
      result.insert(key, QVariant());
      continue;
    }
    result.insert(key, settings.value(key));
  }
  return result;
}

inline void applyIniChanges(
    QSettings &settings, const QVariantMap &values, Qt::CaseSensitivity sensitivity = iniKeyCaseSensitivity
)
{
  // Removing a key also removes differently-cased spellings on Windows. Do
  // every removal first, then recreate all destination keys with their values.
  if (sensitivity == Qt::CaseInsensitive) {
    for (auto it = values.cbegin(); it != values.cend(); ++it)
      if (!it.value().isValid())
        settings.remove(it.key());
    for (auto it = values.cbegin(); it != values.cend(); ++it)
      if (it.value().isValid())
        settings.setValue(it.key(), it.value());
    return;
  }
  for (auto it = values.cbegin(); it != values.cend(); ++it) {
    if (it.value().isValid())
      settings.setValue(it.key(), it.value());
    else
      settings.remove(it.key());
  }
}
} // namespace deskflow::settings_detail
