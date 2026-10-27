/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#include "PreferenceChanges.h"

#include "common/LogLevel.h"
#include "common/Settings.h"

#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QSet>
#include <cmath>

namespace deskflow::gui::personal {
namespace {
const QSet<QString> &booleanKeys()
{
  static const QSet<QString> keys{
      Settings::Gui::AutoStartCore,
      Settings::Gui::Autohide,
      Settings::Gui::CloseToTray,
      Settings::Gui::ShowVersionInTitle,
      Settings::Gui::SymbolicTrayIcon,
      Settings::Client::InvertXScroll,
      Settings::Client::InvertYScroll,
      Settings::Client::DynamicConnectionRetry,
      Settings::Client::LanguageSync,
      Settings::Core::PreventSleep,
      Settings::Core::EnableEnterCommand,
      Settings::Core::EnableExitCommand,
      Settings::Security::TlsEnabled,
      Settings::Security::CheckPeers,
      Settings::Log::GuiDebug,
      Settings::Log::ToFile,
#ifdef Q_OS_WIN
      Settings::Daemon::Elevate,
#endif
  };
  return keys;
}

const QSet<QString> &allowedKeys()
{
  static const QSet<QString> keys = [] {
    auto result = booleanKeys();
    result.unite(
        {Settings::Client::XScrollScale, Settings::Client::YScrollScale, Settings::Core::Port,
         Settings::Core::Interface, Settings::Core::ProcessMode, Settings::Core::ComputerEnterCommand,
         Settings::Core::ComputerExitCommand, Settings::Log::Level, Settings::Log::File}
    );
    return result;
  }();
  return keys;
}

QVariant mergedValue(const QVariantMap &changes, const QString &key)
{
  return changes.value(key, Settings::value(key));
}

bool changed(const QVariantMap &changes, const QString &key)
{
  return changes.contains(key) && changes.value(key) != Settings::value(key);
}

bool fail(QString *error, const QString &message)
{
  if (error)
    *error = message;
  return false;
}

bool integralValue(const QVariant &value)
{
  const auto type = value.metaType().id();
  return type == QMetaType::Int || type == QMetaType::UInt || type == QMetaType::LongLong ||
         type == QMetaType::ULongLong || type == qMetaTypeId<Settings::ProcessMode>();
}

bool writableLogPath(const QString &path)
{
  if (path.trimmed().isEmpty() || !QDir::isAbsolutePath(path))
    return false;
  QFileInfo target(path);
  if (target.exists())
    return target.isFile() && target.isWritable();
  // CoreProcess creates missing parent directories when logging starts. Check
  // their closest existing ancestor without creating a file while editing.
  QFileInfo parent(target.absolutePath());
  while (!parent.exists()) {
    const auto ancestor = parent.dir().absolutePath();
    if (ancestor == parent.absoluteFilePath())
      return false;
    parent.setFile(ancestor);
  }
  return parent.isDir() && parent.isWritable();
}
} // namespace

bool validatePreferenceChanges(const QVariantMap &changes, QString *error)
{
  if (error)
    error->clear();
  for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
    const auto &key = it.key();
    const auto &value = it.value();
    if (!allowedKeys().contains(key))
      return fail(error, QStringLiteral("此设置不属于常规偏好设置，无法保存：%1").arg(key));
    if (!value.isValid())
      return fail(error, QStringLiteral("设置值无效：%1").arg(key));
    if (booleanKeys().contains(key) && value.metaType().id() != QMetaType::Bool)
      return fail(error, QStringLiteral("开关设置必须为开启或关闭：%1").arg(key));
    // A retained legacy value must not prevent saving an unrelated preference.
    if (!changed(changes, key))
      continue;
    if (key == Settings::Client::XScrollScale || key == Settings::Client::YScrollScale) {
      const auto type = value.metaType().id();
      if ((!integralValue(value) && type != QMetaType::Double && type != QMetaType::Float) ||
          !std::isfinite(value.toDouble()) || value.toDouble() < 0.1 || value.toDouble() > 10.0)
        return fail(error, QStringLiteral("滚动速度必须在 0.1 到 10 之间。"));
    } else if (key == Settings::Core::Port) {
      bool ok = false;
      const auto port = value.toLongLong(&ok);
      if (!integralValue(value) || !ok || port < 1 || port > 65535)
        return fail(error, QStringLiteral("连接端口必须是 1 到 65535 之间的整数。"));
    } else if (key == Settings::Core::ProcessMode) {
      if (!integralValue(value) || (value.toInt() != Settings::Desktop && value.toInt() != Settings::Service))
        return fail(error, QStringLiteral("后台运行方式无效。"));
#ifndef Q_OS_WIN
      if (value.toInt() != Settings::Desktop)
        return fail(error, QStringLiteral("当前系统不支持 Windows 后台系统服务。"));
#endif
    } else if (key == Settings::Core::Interface) {
      QHostAddress address;
      if (value.metaType().id() != QMetaType::QString ||
          (!value.toString().isEmpty() && !address.setAddress(value.toString())))
        return fail(error, QStringLiteral("绑定地址应留空，或填写有效的 IPv4 / IPv6 地址。"));
    } else if (key == Settings::Log::Level) {
      if (value.metaType().id() != QMetaType::QString ||
          !LogLevel::logLevelOptions().contains(value.toString(), Qt::CaseInsensitive))
        return fail(error, QStringLiteral("日志级别无效。"));
    } else if (key == Settings::Core::ComputerEnterCommand || key == Settings::Core::ComputerExitCommand ||
               key == Settings::Log::File) {
      if (value.metaType().id() != QMetaType::QString)
        return fail(error, QStringLiteral("设置内容必须为文本：%1").arg(key));
    }
  }

  const auto checkCommand = [&](const QString &enabled, const QString &command) {
    return !(changed(changes, enabled) || changed(changes, command)) || !mergedValue(changes, enabled).toBool() ||
           !mergedValue(changes, command).toString().trimmed().isEmpty();
  };
  if (!checkCommand(Settings::Core::EnableEnterCommand, Settings::Core::ComputerEnterCommand) ||
      !checkCommand(Settings::Core::EnableExitCommand, Settings::Core::ComputerExitCommand))
    return fail(error, QStringLiteral("启用进入或离开时执行命令前，请填写命令内容。"));

  if ((changed(changes, Settings::Log::ToFile) || changed(changes, Settings::Log::File)) &&
      mergedValue(changes, Settings::Log::ToFile).toBool() &&
      !writableLogPath(mergedValue(changes, Settings::Log::File).toString()))
    return fail(error, QStringLiteral("请选择可写的日志文件绝对路径。"));
  return true;
}

bool preferencesRequireReconnect(const QVariantMap &changes)
{
  static const QSet<QString> reconnectKeys{
      Settings::Client::InvertXScroll,
      Settings::Client::InvertYScroll,
      Settings::Client::XScrollScale,
      Settings::Client::YScrollScale,
      Settings::Client::DynamicConnectionRetry,
      Settings::Client::LanguageSync,
      Settings::Core::PreventSleep,
      Settings::Core::Port,
      Settings::Core::Interface,
      Settings::Core::ProcessMode,
      Settings::Core::EnableEnterCommand,
      Settings::Core::EnableExitCommand,
      Settings::Security::TlsEnabled,
      Settings::Security::CheckPeers,
#ifdef Q_OS_WIN
      Settings::Daemon::Elevate,
#endif
  };
  for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
    if (!changed(changes, it.key()))
      continue;
    if (reconnectKeys.contains(it.key()) || it.key() == Settings::Log::ToFile)
      return true;
    if (it.key() == Settings::Log::File && mergedValue(changes, Settings::Log::ToFile).toBool())
      return true;
    if ((it.key() == Settings::Core::ComputerEnterCommand &&
         mergedValue(changes, Settings::Core::EnableEnterCommand).toBool()) ||
        (it.key() == Settings::Core::ComputerExitCommand &&
         mergedValue(changes, Settings::Core::EnableExitCommand).toBool()))
      return true;
  }
  return false;
}

bool commitPreferenceChanges(const QVariantMap &changes, QString *error)
{
  return validatePreferenceChanges(changes, error) && Settings::trySetValues(changes, error);
}

} // namespace deskflow::gui::personal
