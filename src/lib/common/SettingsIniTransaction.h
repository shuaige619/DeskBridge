/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#pragma once

#include <QSettings>
#include <QStringList>
#include <QVariantMap>

namespace deskflow::settings_detail {
#ifdef Q_OS_WIN
inline constexpr auto iniKeyCaseSensitivity = Qt::CaseInsensitive;
#else
inline constexpr auto iniKeyCaseSensitivity = Qt::CaseSensitive;
#endif

inline QString canonicalComputerKey(
    const QSettings &settings, const QString &key, Qt::CaseSensitivity sensitivity = iniKeyCaseSensitivity
)
{
  const auto slash = key.indexOf(QLatin1Char('/'));
  if (sensitivity == Qt::CaseSensitive || slash < 0 || !key.startsWith(QStringLiteral("computer_")))
    return key;
  const auto requestedGroup = key.first(slash);
  QString existingGroup;
  for (const auto &stored : settings.allKeys()) {
    const auto storedSlash = stored.indexOf(QLatin1Char('/'));
    if (storedSlash < 0 || stored.first(storedSlash).compare(requestedGroup, Qt::CaseInsensitive) != 0)
      continue;
    if (existingGroup.isEmpty())
      existingGroup = stored.first(storedSlash);
    if (stored.mid(storedSlash + 1).compare(QStringLiteral("name"), Qt::CaseInsensitive) == 0) {
      existingGroup = stored.first(storedSlash);
      break;
    }
  }
  return existingGroup.isEmpty() ? key : existingGroup + key.mid(slash);
}

inline QVariantMap canonicalComputerChanges(
    const QSettings &settings, const QVariantMap &values, Qt::CaseSensitivity sensitivity = iniKeyCaseSensitivity
)
{
  QVariantMap result;
  for (auto it = values.cbegin(); it != values.cend(); ++it) {
    const auto key = canonicalComputerKey(settings, it.key(), sensitivity);
    // A write wins over a removal that aliases the same Windows storage key.
    if (it.value().isValid() || !result.contains(key))
      result.insert(key, it.value());
  }
  return result;
}

inline QVariantMap snapshotStoredValues(
    const QSettings &settings, const QStringList &keys, Qt::CaseSensitivity sensitivity = iniKeyCaseSensitivity
)
{
  QVariantMap result;
  for (const auto &key : keys)
    result.insert(key, settings.value(canonicalComputerKey(settings, key, sensitivity)));
  return result;
}

inline void applyIniChanges(
    QSettings &settings, const QVariantMap &values, Qt::CaseSensitivity sensitivity = iniKeyCaseSensitivity
)
{
  const auto stored = canonicalComputerChanges(settings, values, sensitivity);
  for (auto it = stored.cbegin(); it != stored.cend(); ++it) {
    if (it.value().isValid())
      settings.setValue(it.key(), it.value());
    else
      settings.remove(it.key());
  }
}
} // namespace deskflow::settings_detail
