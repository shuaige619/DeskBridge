// SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
#include "common/Settings.h"
#include "common/SettingsIniTransaction.h"
#include "gui/personal/ConnectionIdentity.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>
#include <stdexcept>

using deskflow::gui::personal::expandedConnectionIdentityChanges;

namespace {
void require(bool condition, const QString &message)
{
  if (!condition)
    throw std::runtime_error(message.toStdString());
}

void seed(const QString &path)
{
  QSettings saved(path, QSettings::IniFormat);
  saved.setAtomicSyncRequired(true);
  const QVariantMap values{
      {QStringLiteral("core/computerName"), QStringLiteral("source")},
      {QStringLiteral("computer_source/name"), QStringLiteral("source")},
      {QStringLiteral("computer_source/modifierCtrl"), QStringLiteral("meta")},
      {QStringLiteral("computer_source/modifierMeta"), QStringLiteral("ctrl")},
      {QStringLiteral("computer_source/aliases"), QStringList{QStringLiteral("source-alias")}},
      {QStringLiteral("computer_source/future/nestedOption"), QStringLiteral("keep")},
      {QStringLiteral("computer_target/name"), QStringLiteral("target")},
      {QStringLiteral("computer_target/modifierCtrl"), QStringLiteral("alt")},
      {QStringLiteral("computer_target/aliases"), QStringList{QStringLiteral("target-alias")}},
      {QStringLiteral("computer_sourceExtra/custom"), QStringLiteral("unrelated")},
      {QStringLiteral("internalConfig/screens/1/name"), QStringLiteral("source")},
      {QStringLiteral("internalConfig/screens/2/name"), QStringLiteral("target")},
      {QStringLiteral("internalConfig/hotkeys/1/actions/1/switchScreenName"), QStringLiteral("source")},
      {QStringLiteral("internalConfig/hotkeys/1/actions/2/typeScreenNames/1/typeScreenName"), QStringLiteral("source")},
      {QStringLiteral("internalConfig/hotkeys/1/actions/2/typeScreenNames/2/typeScreenName"), QStringLiteral("target")},
  };
  for (auto it = values.cbegin(); it != values.cend(); ++it)
    saved.setValue(it.key(), it.value());
  saved.sync();
  require(saved.status() == QSettings::NoError, QStringLiteral("Cannot seed synthetic INI"));
}

QVariantMap applyLikeProduction(const QVariantMap &values)
{
  QSettings transaction(Settings::settingsFile(), QSettings::IniFormat);
  transaction.setAtomicSyncRequired(true);
  require(transaction.isWritable(), QStringLiteral("Synthetic INI is not writable"));
  transaction.sync();
  require(transaction.status() == QSettings::NoError, QStringLiteral("INI initial sync failed"));
  QVariantMap requested;
  const auto caseChangeWrites = deskflow::settings_detail::caseChangeWrites(values);
  // Production trySetValues first filters values against a single unmodified
  // QSettings view, then applies the resulting QString-sorted QVariantMap.
  // Seeded non-default values avoid needing the full preference defaults here.
  for (auto it = values.cbegin(); it != values.cend(); ++it) {
    const QVariant candidate = it.value();
    const bool changed = transaction.value(it.key()) != candidate;
    const bool existed = transaction.contains(it.key());
    if (!changed && !caseChangeWrites.contains(it.key()) && (candidate.isValid() ? existed : !existed))
      continue;
    requested.insert(it.key(), candidate);
  }
  deskflow::settings_detail::applyIniChanges(transaction, requested);
  transaction.sync();
  require(transaction.status() == QSettings::NoError, QStringLiteral("INI transaction sync failed"));
  return requested;
}

QStringList namedDevices(QSettings &saved)
{
  QStringList names;
  for (const QString &group : saved.childGroups()) {
    if (!group.startsWith(QStringLiteral("computer_")))
      continue;
    const QString name = saved.value(group + QStringLiteral("/name")).toString();
    if (!name.isEmpty())
      names.append(name);
  }
  names.sort();
  return names;
}

void verify(QSettings &saved, const QString &name)
{
  QStringList expected{name, QStringLiteral("target")};
  expected.sort();
  const QStringList actual = namedDevices(saved);
  require(actual == expected,
          QStringLiteral("Named device count/values differ: expected [%1], actual [%2]")
              .arg(expected.join(QLatin1Char(',')), actual.join(QLatin1Char(','))));
  require(saved.value(QStringLiteral("core/computerName")).toString() == name,
          QStringLiteral("Core identity was not persisted"));
  const QString prefix = QStringLiteral("computer_%1/").arg(name);
  const QStringList storedKeys = saved.allKeys();
  for (const QString &suffix : {QStringLiteral("name"), QStringLiteral("modifierCtrl"),
                                QStringLiteral("modifierMeta"), QStringLiteral("aliases"),
                                QStringLiteral("future/nestedOption")})
    require(storedKeys.contains(prefix + suffix, Qt::CaseSensitive),
            QStringLiteral("Device keys retain an old or mixed group spelling: ") + prefix + suffix);
  require(saved.value(prefix + QStringLiteral("modifierCtrl")).toString() == QStringLiteral("meta"),
          QStringLiteral("Source Ctrl modifier lost"));
  require(saved.value(prefix + QStringLiteral("modifierMeta")).toString() == QStringLiteral("ctrl"),
          QStringLiteral("Source Meta modifier lost"));
  require(saved.value(prefix + QStringLiteral("aliases")).toStringList() == QStringList{QStringLiteral("source-alias")},
          QStringLiteral("Source aliases lost"));
  require(saved.value(prefix + QStringLiteral("future/nestedOption")).toString() == QStringLiteral("keep"),
          QStringLiteral("Nested unknown source setting lost"));
  require(saved.value(QStringLiteral("computer_target/modifierCtrl")).toString() == QStringLiteral("alt"),
          QStringLiteral("Target modifier changed"));
  require(saved.value(QStringLiteral("computer_target/aliases")).toStringList() == QStringList{QStringLiteral("target-alias")},
          QStringLiteral("Target aliases changed"));
  require(saved.value(QStringLiteral("computer_sourceExtra/custom")).toString() == QStringLiteral("unrelated"),
          QStringLiteral("Unrelated prefixed group changed"));
  for (const QString &key : {QStringLiteral("internalConfig/screens/1/name"),
                             QStringLiteral("internalConfig/hotkeys/1/actions/1/switchScreenName"),
                             QStringLiteral("internalConfig/hotkeys/1/actions/2/typeScreenNames/1/typeScreenName")})
    require(saved.value(key).toString() == name, QStringLiteral("Layout/hotkey reference lost: ") + key);
  require(saved.status() == QSettings::NoError, QStringLiteral("INI verification read failed"));
}

QJsonObject runRow(const QString &label, const QStringList &sequence, QTemporaryDir &temporary)
{
  QJsonObject result{{QStringLiteral("row"), label}, {QStringLiteral("sequence"), QJsonArray::fromStringList(sequence)}};
  try {
    QString path = temporary.filePath(label + QStringLiteral(".ini"));
    seed(path);
    Settings::setSettingsFile(path);
    QJsonArray steps;
    for (qsizetype index = 0; index < sequence.size(); ++index) {
      const QString name = sequence.at(index);
      QSettings before(path, QSettings::IniFormat);
      const QString oldName = before.value(QStringLiteral("core/computerName")).toString();
      const QStringList oldKeys = before.allKeys();
      const QVariantMap expanded = expandedConnectionIdentityChanges({{Settings::Core::ComputerName, name}});
      // Windows contains()/value() use CI lookup. Rollback must distinguish
      // genuinely stored spellings from new spellings that only compare equal.
      const QVariantMap previous = deskflow::settings_detail::snapshotStoredValues(before, expanded.keys());
      const QVariantMap applied = applyLikeProduction(expanded);
      QSettings warm(path, QSettings::IniFormat);
      result.insert(QStringLiteral("lastRequestedName"), name);
      result.insert(QStringLiteral("lastExpandedChanges"), QJsonObject::fromVariantMap(expanded));
      result.insert(QStringLiteral("lastAppliedChanges"), QJsonObject::fromVariantMap(applied));
      result.insert(QStringLiteral("lastWarmKeys"), QJsonArray::fromStringList(warm.allKeys()));
      result.insert(QStringLiteral("lastWarmDevices"), QJsonArray::fromStringList(namedDevices(warm)));
      QFile serialized(path);
      if (serialized.open(QIODevice::ReadOnly))
        result.insert(QStringLiteral("lastSerializedSyntheticIni"), QString::fromUtf8(serialized.readAll()));
      serialized.close();
      verify(warm, name);
      // A new filename bypasses QSettings' shared per-filename cache and tests
      // persisted group discovery rather than only the warm in-memory view.
      const QString copied = temporary.filePath(label + QStringLiteral("-cold-%1.ini").arg(index));
      require(QFile::copy(path, copied), QStringLiteral("Cannot copy synthetic INI for cold read"));
      QSettings cold(copied, QSettings::IniFormat);
      verify(cold, name);
      applyLikeProduction(previous);
      QSettings rollback(path, QSettings::IniFormat);
      verify(rollback, oldName);
      QStringList restoredKeys = rollback.allKeys();
      QStringList expectedKeys = oldKeys;
      restoredKeys.sort();
      expectedKeys.sort();
      require(restoredKeys == expectedKeys, QStringLiteral("Rollback did not restore original key spellings"));
      const QString rollbackCopy = temporary.filePath(label + QStringLiteral("-rollback-cold-%1.ini").arg(index));
      require(QFile::copy(path, rollbackCopy), QStringLiteral("Cannot copy rolled-back INI for cold read"));
      QSettings rollbackCold(rollbackCopy, QSettings::IniFormat);
      verify(rollbackCold, oldName);
      steps.append(QJsonObject{{QStringLiteral("name"), name},
                               {QStringLiteral("appliedKeys"), QJsonArray::fromStringList(applied.keys())},
                               {QStringLiteral("namedDevices"), QJsonArray::fromStringList(namedDevices(cold))},
                               {QStringLiteral("storedKeys"), QJsonArray::fromStringList(cold.allKeys())},
                               {QStringLiteral("rollbackPassed"), true}});
      path = copied;
      Settings::setSettingsFile(path);
    }
    result.insert(QStringLiteral("steps"), steps);
    result.insert(QStringLiteral("passed"), true);
  } catch (const std::exception &error) {
    result.insert(QStringLiteral("passed"), false);
    result.insert(QStringLiteral("error"), QString::fromUtf8(error.what()));
  }
  return result;
}
} // namespace

int main(int argc, char **argv)
{
  QCoreApplication application(argc, argv);
  QJsonObject report{{QStringLiteral("scope"), QStringLiteral("Actual ConnectionIdentity.cpp + shared SettingsIniTransaction helpers + native QtCore QSettings INI; minimal Settings/validator adapters, not full Settings integration")},
                     {QStringLiteral("sourceCommit"), qEnvironmentVariable("GITHUB_SHA")},
                     {QStringLiteral("compileQt"), QStringLiteral(QT_VERSION_STR)},
                     {QStringLiteral("runtimeQt"), QString::fromLatin1(qVersion())}};
  const QStringList arguments = application.arguments();
  if (arguments.size() > 2 && (arguments.at(2) != QStringLiteral(QT_VERSION_STR) || arguments.at(2) != QString::fromLatin1(qVersion()))) {
    QTextStream(stderr) << "Unexpected Qt compile/runtime version\n";
    return 2;
  }
  QTemporaryDir temporary;
  if (!temporary.isValid())
    return 2;
  const QList<QPair<QString, QStringList>> rows{
      {QStringLiteral("ordinary"), {QStringLiteral("renamed")}},
      {QStringLiteral("case-only"), {QStringLiteral("SOURCE")}},
      {QStringLiteral("case-then-ordinary"), {QStringLiteral("SOURCE"), QStringLiteral("renamed")}},
      {QStringLiteral("repeated-case-only"), {QStringLiteral("SOURCE"), QStringLiteral("sOurCe")}},
  };
  QJsonArray results;
  bool passed = true;
  for (const auto &row : rows) {
    const QJsonObject result = runRow(row.first, row.second, temporary);
    passed &= result.value(QStringLiteral("passed")).toBool();
    results.append(result);
  }
  report.insert(QStringLiteral("rows"), results);
  report.insert(QStringLiteral("passed"), passed);
  const QByteArray output = QJsonDocument(report).toJson(QJsonDocument::Indented);
  QTextStream(stdout) << output;
  if (arguments.size() > 1) {
    QFile file(arguments.at(1));
    if (!file.open(QIODevice::WriteOnly) || file.write(output) != output.size())
      return 2;
  }
  return passed ? 0 : 1;
}
