/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "Settings.h"
#include "SettingsIniTransaction.h"

#include "LogLevel.h"
#include "NetworkProtocol.h"
#include "UrlConstants.h"
#include "common/KeyboardModifier.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileSystemWatcher>
#include <QRect>
#include <QRegularExpression>
#include <QStandardPaths>
#include <filesystem>
#include <system_error>

Settings *Settings::instance()
{
  static Settings m;
  return &m;
}

void Settings::setSettingsFile(const QString &settingsFile)
{
  if (Settings::settingsFile() == settingsFile) {
    qDebug("settings file already set, skipping");
    return;
  }

  if (instance()->m_settings)
    instance()->m_settings->deleteLater();

  instance()->m_settingsWatcher->removePath(instance()->m_settings->fileName());
  instance()->m_settings = new QSettings(settingsFile, QSettings::IniFormat, instance());
  instance()->m_settingsWatcher->addPath(settingsFile);
  instance()->m_settingsProxy->load(settingsFile);
  qInfo().noquote() << "settings file changed:" << instance()->m_settings->fileName();

  instance()->upgradeSettings();
  instance()->cleanSettings();
  instance()->cleanStateSettings();
  instance()->setupComputerName();
  instance()->checkIfSettingsWritableChange();
}

void Settings::setStateFile(const QString &stateFile)
{
  if (instance()->m_stateSettings->fileName() == stateFile) {
    qDebug("state file already set, skipping");
    return;
  }

  if (instance()->m_stateSettings)
    instance()->m_stateSettings->deleteLater();

  instance()->m_stateSettings = new QSettings(stateFile, QSettings::IniFormat, instance());
  qInfo().noquote() << "state file changed:" << instance()->m_stateSettings->fileName();
}

void Settings::clearState()
{
  auto *settings = instance();
  settings->m_stateSettings->clear();
  settings->m_stateSettings->sync();
  if (isPortableMode()) {
    // Portable mode stores the same state keys alongside preferences. Clear
    // those keys without deleting the user's connection or shortcut settings.
    for (const auto &key : m_stateKeys) settings->m_settings->remove(key);
    settings->m_settings->sync();
  }
}

Settings::Settings(QObject *parent) : QObject(parent), m_settingsWatcher{new QFileSystemWatcher(this)}
{
  QString fileToLoad;
  // Honor an explicitly isolated profile before consulting platform defaults.
  // In particular, Windows tests must not migrate a user's real configuration
  // while initializing the singleton before setSettingsFile() takes effect.
  if (const auto xdgConfigHome = qEnvironmentVariable("XDG_CONFIG_HOME"); !xdgConfigHome.isEmpty())
    fileToLoad = QStringLiteral("%1/%2/%2.conf").arg(xdgConfigHome, kAppName);
#ifdef Q_OS_WIN
  else if (const auto portableFile = portableSettingsFile(); QFile(portableFile).exists())
    fileToLoad = portableFile;
#endif
  else if (QFile(UserSettingFile).exists())
    fileToLoad = UserSettingFile;
  else if (QFile(SystemSettingFile).exists())
    fileToLoad = SystemSettingFile;
  else
    fileToLoad = UserSettingFile;

  m_settings = new QSettings(fileToLoad, QSettings::IniFormat, this);
  m_settingsWritable = m_settings->isWritable();
  m_settingsWatcher->addPath(m_settings->fileName());
  connect(m_settingsWatcher, &QFileSystemWatcher::fileChanged, this, &Settings::checkIfSettingsWritableChange);
  m_settingsProxy = std::make_shared<QSettingsProxy>();
  m_settingsProxy->load(fileToLoad);
  qInfo().noquote() << "initial settings file:" << m_settings->fileName();

  const auto xdgStateHome = qEnvironmentVariable("XDG_STATE_HOME");
  const auto stateBase = !xdgStateHome.isEmpty()
                             ? xdgStateHome
                             : QStandardPaths::standardLocations(QStandardPaths::GenericStateLocation).at(0);
  const auto stateFile = QStringLiteral("%1/%2.state").arg(stateBase, kAppName);

  m_stateSettings = new QSettings(stateFile, QSettings::IniFormat, this);

  upgradeSettings();
  cleanSettings();
  cleanStateSettings();
  setupComputerName();
}

void Settings::upgradeSettings()
{
  const auto existingKeys = m_settings->allKeys(); // Force lazy INI parsing before considering a migration.
  if (m_settings->status() != QSettings::NoError)
    return;
  const auto certificateName = QString::fromUtf8(kAppName).toLower();
  if (certificateName != QString::fromUtf8(kAppId).toLower() && !m_settings->contains(Security::Certificate)) {
    // Pin an older implicit identity once during migration. Defaults must not
    // change identity later just because a certificate file appears or vanishes.
    // This also runs in the constructor, so do not call instance()/tlsDir() here.
    auto certificateDirectory = QFileInfo(m_settings->fileName()).absolutePath();
#ifdef Q_OS_WIN
    if (!QFile(portableSettingsFile()).exists())
      certificateDirectory = SystemDir;
#endif
    certificateDirectory += QStringLiteral("/tls");
    const auto legacyPath = QStringLiteral("%1/%2.pem").arg(certificateDirectory, kAppId);
    const auto brandedPath = QStringLiteral("%1/%2.pem").arg(certificateDirectory, certificateName);
    std::error_code lookupError;
    const auto legacyStatus = std::filesystem::symlink_status(QFile(legacyPath).filesystemFileName(), lookupError);
    const bool keepLegacy = std::filesystem::exists(legacyStatus) ||
                            (lookupError && lookupError != std::errc::no_such_file_or_directory);
    m_settings->setValue(Security::Certificate, keepLegacy ? legacyPath : brandedPath);
    // The proxy uses the same QSettings backend and sees the complete persisted
    // selection without reloading or generating/replacing either identity file.
    m_settings->sync();
  }
  if (const auto logValue = m_settings->value(Settings::Log::Level).toString();
      !LogLevel::logLevelOptions().contains(logValue, Qt::CaseInsensitive))
    m_settings->setValue(Settings::Log::Level, defaultValue(Settings::Log::Level));
  const auto screenKeys = existingKeys.filter("screen_");
  for (const auto &key : screenKeys) {
    auto newKey = key;
    newKey.replace("screen_", "computer_");
    m_settings->setValue(newKey, m_settings->value(key));
  }
  for (const auto [oldKey, newKey] : m_upgradedMap.asKeyValueRange()) {
    if (m_settings->contains(newKey) || !m_settings->contains(oldKey))
      continue;
    if (oldKey == InternalConfig::Protocol) {
      m_settings->setValue(newKey, networkProtocolToOption(NetworkProtocol(m_settings->value(oldKey).toInt())));
    } else if (oldKey == InternalConfig::ClipboardSharingSize) {
      m_settings->setValue(newKey, m_settings->value(oldKey).toUInt() / 1024);
    } else {
      m_settings->setValue(newKey, m_settings->value(oldKey));
    }
  }

  // move to computers

  m_settings->beginGroup("internalConfig");
  const int screenArraySize = m_settings->beginReadArray("screens");
  m_settings->endArray();
  m_settings->endGroup();

  for (int i = 1; i <= screenArraySize; i++) {
    m_settings->beginGroup("internalConfig");
    m_settings->beginReadArray("screens");
    m_settings->setArrayIndex(i);
    const auto name = m_settings->value("name", {}).toString();

    if (name.isEmpty()) {
      m_settings->endArray();
      m_settings->endGroup();
      continue;
    }

    int switchCornerSize = 0;
    if (m_settings->contains("switchCornerSize"))
      switchCornerSize = m_settings->value("switchCornerSize", 0).toInt();

    QList<bool> fixTemp;
    if (m_settings->contains("fixArray/size")) {
      int fixArraySize = m_settings->beginReadArray("fixArray");
      for (int j = 0; j < fixArraySize; j++) {
        m_settings->setArrayIndex(j);
        fixTemp.append(m_settings->value("fix").toBool());
      }
      m_settings->endArray();
    }

    QList<bool> cornerTemp;
    if (m_settings->contains("switchCornerArray/size")) {
      int cornerArraySize = m_settings->beginReadArray("switchCornerArray");
      for (int j = 0; j < cornerArraySize; j++) {
        m_settings->setArrayIndex(j);
        cornerTemp.append(m_settings->value("switchCorner").toBool());
      }
      m_settings->endArray();
    }

    QList<QString> modifierTemp;
    if (m_settings->contains("modifierArray/size")) {
      int modifierArraySize = m_settings->beginReadArray("modifierArray");
      for (int j = 0; j < modifierArraySize; j++) {
        m_settings->setArrayIndex(j);
        auto modifier = m_settings->value("modifier").toInt();
        modifierTemp.append(
            modifier == -1 ? valueToKeyboardModifierOption(j) : valueToKeyboardModifierOption(modifier)
        );
      }
      m_settings->endArray();
    }

    QStringList aliases;
    if (m_settings->contains("aliasArray/size")) {
      int aliasList = m_settings->beginReadArray("aliasArray");
      for (int j = 0; j < aliasList; j++) {
        m_settings->setArrayIndex(j);
        aliases.append(m_settings->value("alias").toString());
      }
      m_settings->endArray();
      aliases.removeDuplicates();
    }
    m_settings->endArray();
    m_settings->endGroup();

    // Write Settings
    if (!m_settings->contains(Settings::Computer::Name.arg(name)))
      m_settings->setValue(Settings::Computer::Name.arg(name), name);
    if (!m_settings->contains(Settings::Computer::SwitchCornerSize.arg(name)))
      m_settings->setValue(Settings::Computer::SwitchCornerSize.arg(name), switchCornerSize);
    if (!m_settings->contains(Settings::Computer::HalfDuplexCapsLock.arg(name)))
      m_settings->setValue(Settings::Computer::HalfDuplexCapsLock.arg(name), fixTemp.at(0));
    if (!m_settings->contains(Settings::Computer::HalfDuplexNumLock.arg(name)))
      m_settings->setValue(Settings::Computer::HalfDuplexNumLock.arg(name), fixTemp.at(1));
    if (!m_settings->contains(Settings::Computer::HalfDuplexScrollLock.arg(name)))
      m_settings->setValue(Settings::Computer::HalfDuplexScrollLock.arg(name), fixTemp.at(2));
    if (!m_settings->contains(Settings::Computer::XtestIsXineramaUnaware.arg(name)))
      m_settings->setValue(Settings::Computer::XtestIsXineramaUnaware.arg(name), fixTemp.at(3));
    if (!m_settings->contains(Settings::Computer::SwitchCornerTopLeft.arg(name)))
      m_settings->setValue(Settings::Computer::SwitchCornerTopLeft.arg(name), cornerTemp.at(0));
    if (!m_settings->contains(Settings::Computer::SwitchCornerTopRight.arg(name)))
      m_settings->setValue(Settings::Computer::SwitchCornerTopRight.arg(name), cornerTemp.at(1));
    if (!m_settings->contains(Settings::Computer::SwitchCornerBottomLeft.arg(name)))
      m_settings->setValue(Settings::Computer::SwitchCornerBottomLeft.arg(name), cornerTemp.at(2));
    if (!m_settings->contains(Settings::Computer::SwitchCornerBottomRight.arg(name)))
      m_settings->setValue(Settings::Computer::SwitchCornerBottomRight.arg(name), cornerTemp.at(3));
    if (!m_settings->contains(Settings::Computer::ModifierShift.arg(name)))
      m_settings->setValue(Settings::Computer::ModifierShift.arg(name), modifierTemp.at(0));
    if (!m_settings->contains(Settings::Computer::ModifierCtrl.arg(name)))
      m_settings->setValue(Settings::Computer::ModifierCtrl.arg(name), modifierTemp.at(1));
    if (!m_settings->contains(Settings::Computer::ModifierAlt.arg(name)))
      m_settings->setValue(Settings::Computer::ModifierAlt.arg(name), modifierTemp.at(2));
    if (!m_settings->contains(Settings::Computer::ModifierMeta.arg(name)))
      m_settings->setValue(Settings::Computer::ModifierMeta.arg(name), modifierTemp.at(3));
    if (!m_settings->contains(Settings::Computer::ModifierSuper.arg(name)))
      m_settings->setValue(Settings::Computer::ModifierSuper.arg(name), modifierTemp.at(4));
    if (!m_settings->contains(Settings::Computer::ModifierAltGr.arg(name)))
      m_settings->setValue(Settings::Computer::ModifierAltGr.arg(name), modifierTemp.at(5));
    if (!m_settings->contains(Settings::Computer::Aliases.arg(name))) {
      m_settings->setValue(Settings::Computer::Aliases.arg(name), aliases);
    }
  }
}

void Settings::cleanSettings()
{
  if (m_settings->status() != QSettings::NoError)
    return;
  const QStringList keys = m_settings->allKeys();
  for (const QString &key : keys) {
    if (m_oldServerConfigKeys.contains(key) || key.startsWith("screen_"))
      m_settings->remove(key);
    if (key.startsWith(QStringLiteral("internalConfig")))
      continue;
    if (const auto group = key.mid(0, key.indexOf('/')); !m_validKeys.contains(key) && m_validGroup.contains(group))
      m_settings->remove(key);
    if (!m_settings->value(key).isValid())
      m_settings->remove(key);
  }
}

void Settings::cleanStateSettings()
{
  const QStringList keys = m_stateKeys;
  for (const QString &key : keys) {
    if (!m_stateKeys.contains(key))
      m_stateSettings->remove(key);
    if (m_stateSettings->value(key).toString().isEmpty() && !m_stateSettings->value(key).toRect().isValid())
      m_stateSettings->remove(key);
  }
}

void Settings::setupComputerName()
{
  if (m_settings->status() != QSettings::NoError)
    return;
  if (m_settings->value(Settings::Core::ComputerName).toString().isEmpty())
    m_settings->setValue(Settings::Core::ComputerName, cleanComputerName(QSysInfo::machineHostName()));
}

QString Settings::cleanComputerName(const QString &name)
{
  static const auto hyphen = QStringLiteral("-");
  static const auto space = QStringLiteral(" ");
  static const auto underscore = QStringLiteral("_");
  static const auto period = QStringLiteral(".");
  static const auto nameRegex = QRegularExpression(QStringLiteral("[^\\w\\-\\.]"));

  QString cleanName = name.simplified();
  cleanName.replace(space, underscore);
  cleanName.replace(nameRegex, {});
  while (cleanName.startsWith(hyphen) || cleanName.startsWith(underscore) || cleanName.startsWith(period))
    cleanName.removeFirst();
  while (cleanName.endsWith(hyphen) || cleanName.endsWith(underscore) || cleanName.endsWith(period))
    cleanName.removeLast();
  if (cleanName.length() > 255) {
    cleanName.truncate(255);
    cleanName = cleanComputerName(cleanName);
  }
  return cleanName;
}

void Settings::checkIfSettingsWritableChange()
{
  if (!instance()->m_settingsWatcher->files().contains(Settings::settingsFile()))
    m_settingsWatcher->addPath(Settings::settingsFile());

  bool writable = Settings::isWritable();
  if (writable == m_settingsWritable)
    return;
  qDebug() << QString("Setting are now %1").arg(writable ? "writable" : "readonly");
  instance()->m_settingsWritable = writable;
  Q_EMIT instance()->settingsWritableChanged(writable);
}

QVariant Settings::defaultValue(const QString &key)
{
  if (key == Client::ShortcutConfig)
    return QString();
  if (key.startsWith(QStringLiteral("computer_"))) {
    const auto computerKey = key.mid(key.indexOf('/') + 1);
    return computerDefaults(computerKey);
  }

  if (m_defaultFalseValues.contains(key))
    return false;

  if (key == Gui::AutoUpdateCheck && QString::fromUtf8(kRuntimeId) != QString::fromUtf8(kAppId))
    return false;

  if (m_defaultTrueValues.contains(key))
    return true;

  if (key == Client::ClipboardShortcutApps) {
#ifdef Q_OS_MACOS
    return QStringList{QStringLiteral("com.openai.codex"), QStringLiteral("com.electron.lark")};
#else
    return QStringList{};
#endif
  }

  if (key == Security::Certificate) {
    const auto certificateName = instance()->m_settings->status() == QSettings::NoError
                                     ? QString::fromUtf8(kAppName).toLower()
                                     : QString::fromUtf8(kAppId);
    return QStringLiteral("%1/%2.pem").arg(Settings::tlsDir(), certificateName);
  }

  if (key == Security::KeySize)
    return 2048;

  if (key == Log::File) {
    if (QString::fromUtf8(kRuntimeId) != QString::fromUtf8(kAppId))
      return QStringLiteral("%1/%2.log").arg(Settings::settingsPath(), QString::fromUtf8(kAppName).toLower());
#ifdef Q_OS_MACOS
    return QStringLiteral("%1/%2.log").arg(Settings::settingsPath(), QString::fromUtf8(kAppName).toLower());
#else
    return QStringLiteral("%1/%2.log").arg(QDir::homePath(), kAppId);
#endif
  }

  if (key == Log::Level)
    return QVariant::fromValue(LogLevel::Level::Info).toString();

  if (key == Daemon::Elevate)
    return !Settings::isPortableMode();

  if (key == Gui::UpdateCheckUrl)
    return kUrlUpdateCheck;

  if (key == Server::ExternalConfigFile)
    return QStringLiteral("%1/%2-server.conf").arg(Settings::settingsPath(), kAppId);

  if (key == Core::Port)
    return 24800;

  if (key == Core::ProcessMode) {
#ifdef Q_OS_WIN
    if (!Settings::isPortableMode())
      return Settings::ProcessMode::Service;
#endif
    return Settings::ProcessMode::Desktop;
  }

  if (key == Daemon::LogFile)
    return QStringLiteral("%1/%2-daemon.log").arg(Settings::settingsPath(), kAppId);

  if (key == Client::YScrollScale || key == Client::XScrollScale)
    return 1.0;

  if (key == Server::Protocol)
    return networkProtocolToOption(NetworkProtocol::Barrier);

  if (key == Server::GridWidth)
    return kServerGridWidth;

  if (key == Server::GridHeight)
    return kServerGridHeight;

  if (key == Server::Heartbeat)
    return 5000;

  if (key == Server::SwitchDelay || key == Server::SwitchDoubleTap)
    return 250;

  if (key == Server::ClipboardSize)
    return 3; // 3 MiB

  return QVariant();
}

QSettingsProxy &Settings::proxy()
{
  return *instance()->m_settingsProxy;
}

NetworkProtocol Settings::networkProtocol()
{
  return networkProtocolFromString(Settings::value(Server::Protocol).toString());
}

void Settings::save(bool emitSaving)
{
  if (!Settings::isWritable())
    qWarning().noquote() << "settings not saved, file is read-only:" << Settings::settingsFile();
  if (emitSaving)
    Q_EMIT instance()->serverSettingsChanged();
  instance()->m_settings->sync();
  instance()->m_stateSettings->sync();
}

QStringList Settings::validKeys()
{
  return Settings::m_validKeys;
}

QString Settings::serverConfigFile()
{
  bool useExt = value(Server::ExternalConfig).toBool();
  return useExt ? value(Server::ExternalConfigFile).toString() : defaultValue(Server::ExternalConfigFile).toString();
}

bool Settings::isServerConfigFileReadable()
{
  auto file = QFile(serverConfigFile());
  return file.open(QFile::ReadOnly);
}

bool Settings::isWritable()
{
  return instance()->m_settings->isWritable();
}

bool Settings::isPortableMode()
{
  // Enable portable mode only if the portable settings file exists in the expected location.
  return QFile(portableSettingsFile()).exists();
}

QString Settings::settingsFile()
{
  return QFileInfo(instance()->m_settings->fileName()).absoluteFilePath();
}

QString Settings::settingsPath()
{
#ifdef Q_OS_WIN
  if (!isPortableMode())
    return SystemDir;
#endif

  return QFileInfo(instance()->m_settings->fileName()).absolutePath();
}

QString Settings::tlsDir()
{
  return QStringLiteral("%1/tls").arg(instance()->settingsPath());
}

QString Settings::tlsTrustedServersDb()
{
  return QFileInfo(QStringLiteral("%1/trusted-servers").arg(instance()->tlsDir())).absoluteFilePath();
}

QString Settings::tlsTrustedClientsDb()
{
  return QFileInfo(QStringLiteral("%1/trusted-clients").arg(instance()->tlsDir())).absoluteFilePath();
}

QString Settings::logLevelText()
{
  return Settings::value(Log::Level).toString();
}

void Settings::setValue(const QString &key, const QVariant &value)
{
  const bool useState = Settings::m_stateKeys.contains(key) && !instance()->isPortableMode();
  auto settings = useState ? instance()->m_stateSettings : instance()->m_settings;

  if (settings->value(key) == value)
    return;

  if (!value.isValid())
    settings->remove(key);
  else {
    if (key == Settings::Core::ComputerName)
      settings->setValue(key, cleanComputerName(value.toString()));
    else
      settings->setValue(key, value);
  }

  settings->sync();
  Q_EMIT instance()->settingsChanged(key);
}

QVariant Settings::value(const QString &key)
{
  const bool useState = Settings::m_stateKeys.contains(key) && !instance()->isPortableMode();
  auto settings = useState ? instance()->m_stateSettings : instance()->m_settings;
  return settings->value(key, defaultValue(key));
}

bool Settings::trySetValue(const QString &key, const QVariant &value, QString *error)
{
  if (error)
    error->clear();
  const bool useState = Settings::m_stateKeys.contains(key) && !instance()->isPortableMode();
  auto *backend = useState ? instance()->m_stateSettings : instance()->m_settings;
  // QSettings retains its first error for its lifetime. A fresh transaction
  // allows a later retry after a failed write, without resetting global state.
  QSettings transaction(backend->fileName(), backend->format());
  auto *settings = &transaction;
  if (!settings->isWritable()) {
    if (error)
      *error = QStringLiteral("配置文件不可写，原配置已保留。");
    return false;
  }
  const bool existed = settings->contains(key);
  const QVariant previous = settings->value(key);
  if (previous == value)
    return true;

  // QSettings uses an atomic file replacement. Require it rather than allowing
  // a non-atomic fallback, and publish the settingsChanged signal only on success.
  settings->setAtomicSyncRequired(true);
  settings->setValue(key, value);
  settings->sync();
  if (settings->status() != QSettings::NoError) {
    if (existed)
      settings->setValue(key, previous);
    else
      settings->remove(key);
    settings->sync();
    if (error)
      *error = QStringLiteral("无法保存配置，已恢复原配置。");
    return false;
  }
  Q_EMIT instance()->settingsChanged(key);
  return true;
}

bool Settings::trySetValues(const QVariantMap &values, QString *error)
{
  if (error)
    error->clear();
  if (values.isEmpty())
    return true;

  const bool portable = isPortableMode();
  const bool useState = m_stateKeys.contains(values.firstKey()) && !portable;
  for (auto it = values.cbegin(); it != values.cend(); ++it) {
    if (it.key().isEmpty() || (m_stateKeys.contains(it.key()) && !portable) != useState) {
      if (error)
        *error = QStringLiteral("一次保存只能修改同一个配置文件，原配置已保留。");
      return false;
    }
  }

  auto *backend = useState ? instance()->m_stateSettings : instance()->m_settings;
  // A fresh QSettings avoids carrying a previous transaction's error status.
  QSettings transaction(backend->fileName(), backend->format());
  transaction.setAtomicSyncRequired(true);
  if (!transaction.isWritable()) {
    if (error)
      *error = QStringLiteral("配置文件不可写，原配置已保留。");
    return false;
  }
  transaction.sync();
  if (transaction.status() != QSettings::NoError) {
    if (error)
      *error = QStringLiteral("无法读取或同步配置，原配置已保留。");
    return false;
  }

  QVariantMap previous;
  QVariantMap requested;
  QStringList changedKeys;
  const auto stored = deskflow::settings_detail::snapshotStoredValues(transaction, values.keys());
  const auto caseChangeKeys = deskflow::settings_detail::caseChangeWrites(values);
  const auto identity = values.constFind(Core::ComputerName);
  const bool changingIdentity = identity != values.cend() && identity.value().isValid() &&
                                cleanComputerName(identity.value().toString()) !=
                                    transaction.value(Core::ComputerName, defaultValue(Core::ComputerName)).toString();
  for (auto it = values.cbegin(); it != values.cend(); ++it) {
    auto candidate = it.value();
    if (it.key() == Core::ComputerName && candidate.isValid())
      candidate = cleanComputerName(candidate.toString());
    const auto effective = candidate.isValid() ? candidate : defaultValue(it.key());
    const bool effectiveChanged = transaction.value(it.key(), defaultValue(it.key())) != effective;
    const bool existed = transaction.contains(it.key());
    // Identity rollback must restore explicitly stored device defaults too.
    // Other callers retain the existing implicit-default filtering behavior.
    const bool restoreComputerValue = changingIdentity && candidate.isValid() && !existed &&
                                      it.key().startsWith(QStringLiteral("computer_"));
    if (!effectiveChanged && !caseChangeKeys.contains(it.key()) &&
        !restoreComputerValue && (candidate.isValid() || !existed))
      continue;
    previous.insert(it.key(), stored.value(it.key()));
    requested.insert(it.key(), candidate);
    if (effectiveChanged)
      changedKeys.append(it.key());
  }
  if (requested.isEmpty())
    return true;

  deskflow::settings_detail::applyIniChanges(transaction, requested);
  // All requested keys reach the INI file through one atomic replacement.
  transaction.sync();
  if (transaction.status() != QSettings::NoError) {
    // QSettings instances for the same file share their in-memory cache. Restore
    // it as well as the pending write, including keys which did not exist before.
    deskflow::settings_detail::applyIniChanges(transaction, previous);
    transaction.sync();
    if (error)
      *error = QStringLiteral("无法保存配置，已保留全部原配置。");
    return false;
  }

  // Observers can read the complete new configuration from the very first signal.
  for (const auto &key : changedKeys)
    Q_EMIT instance()->settingsChanged(key);
  return true;
}

void Settings::restoreDefaultSettings()
{
  for (const auto &key : m_validKeys) {
    instance()->setValue(key, defaultValue(key));
  }
}

QString Settings::portableSettingsFile()
{
  static const auto filename =
      QStringLiteral("%1/settings/%2.conf").arg(QCoreApplication::applicationDirPath(), kAppName);
  return QFileInfo(filename).absoluteFilePath();
}

void Settings::removeUnknownComputers(const QStringList &knownComputers)
{
  const QStringList knownGroups = instance()->m_settings->childGroups();
  for (const auto &group : knownGroups) {
    if (m_validGroup.contains(group) || knownComputers.contains(group))
      continue;
    instance()->m_settings->remove(group);
  }
}

QVariant Settings::computerDefaults(const QString &key)
{
  if (key.contains(QStringLiteral("halfDuplex")) || key.contains("xtestIsXineramaUnaware") || key.contains("X11") ||
      key.contains("Left") || key.contains("Right"))
    return false;
  if (key.endsWith("switchCornerSize"))
    return 0;
  if (key == "modifierAlt")
    return keyboardModifierToOption(KeyboardModifier::Alt);
  if (key == "modifierAltGr")
    return keyboardModifierToOption(KeyboardModifier::AltGr);
  if (key == "modifierCtrl")
    return keyboardModifierToOption(KeyboardModifier::Ctrl);
  if (key == "modifierMeta")
    return keyboardModifierToOption(KeyboardModifier::Meta);
  if (key == "modifierShift")
    return keyboardModifierToOption(KeyboardModifier::Shift);
  if (key == "modifierSuper")
    return keyboardModifierToOption(KeyboardModifier::Super);
  return QVariant();
}

QStringList Settings::knownComputers()
{
  const QStringList knownGroups = instance()->m_settings->childGroups();
  QStringList computers;
  for (const auto &group : knownGroups) {
    if (group.startsWith("computer_")) {
      computers.append(Settings::value(QStringLiteral("%1/name").arg(group)).toString());
    }
  }
  return computers;
}
