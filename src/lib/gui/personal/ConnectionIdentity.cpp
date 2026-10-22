/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#include "ConnectionIdentity.h"

#include "common/Settings.h"
#include "gui/validators/ComputerNameValidator.h"

#include <QRegularExpression>
#include <QSettings>

namespace deskflow::gui::personal {
QVariantMap expandedConnectionIdentityChanges(const QVariantMap &changes)
{
  const auto requested = changes.constFind(Settings::Core::ComputerName);
  if (requested == changes.cend() || !requested.value().isValid())
    return changes;

  QString newName = requested.value().toString();
  const validators::ComputerNameValidator validator({});
  if (newName.isEmpty() || !validator.validate(newName))
    return changes;
  // Settings normalizes valid names by trimming punctuation at either edge.
  // Use the same final identity for every reference in this transaction.
  while (newName.startsWith(QLatin1Char('.')) || newName.startsWith(QLatin1Char('_')) ||
         newName.startsWith(QLatin1Char('-')))
    newName.removeFirst();
  while (newName.endsWith(QLatin1Char('.')) || newName.endsWith(QLatin1Char('_')) || newName.endsWith(QLatin1Char('-')))
    newName.removeLast();
  const QString oldName = Settings::value(Settings::Core::ComputerName).toString();
  if (newName.isEmpty() || oldName.isEmpty() || newName == oldName)
    return changes;

  QVariantMap expanded = changes;
  const QString oldPrefix = QStringLiteral("computer_%1/").arg(oldName);
  const QString newPrefix = QStringLiteral("computer_%1/").arg(newName);
#ifdef Q_OS_WIN
  constexpr auto settingsCaseSensitivity = Qt::CaseInsensitive;
#else
  constexpr auto settingsCaseSensitivity = Qt::CaseSensitive;
#endif
  const bool sameGroup = oldPrefix.compare(newPrefix, settingsCaseSensitivity) == 0;
  const QRegularExpression screenName(QStringLiteral("^internalConfig/screens/[0-9]+/name$"));
  const QRegularExpression actionTarget(QStringLiteral(
      "^internalConfig/hotkeys/[0-9]+/actions/[0-9]+/"
      "(?:switchScreenName|typeScreenNames/[0-9]+/typeScreenName)$"
  ));
  // A separate read-only view avoids changing the shared proxy's group/array
  // cursor when this helper is used alongside the configuration editors.
  QSettings saved(Settings::settingsFile(), QSettings::IniFormat);
  for (const QString &key : saved.allKeys()) {
    const QVariant value = saved.value(key);
    if (key.startsWith(oldPrefix, settingsCaseSensitivity) && !sameGroup) {
      const QString target = newPrefix + key.mid(oldPrefix.size());
      if (!expanded.contains(target))
        expanded.insert(target, value);
      // The core discovers devices from every computer_* group, independently
      // of the GUI layout. Move the old identity rather than leaving a second
      // device behind; the caller snapshots these removals for rollback too.
      expanded.insert(key, QVariant());
    } else if ((screenName.match(key).hasMatch() || actionTarget.match(key).hasMatch()) &&
               changes.value(key, value).toString() == oldName) {
      expanded.insert(key, newName);
    }
  }
  expanded.insert(Settings::Computer::Name.arg(newName), newName);
  return expanded;
}
} // namespace deskflow::gui::personal
