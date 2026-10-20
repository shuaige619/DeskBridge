/*
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "LoginStartup.h"
#include "LoginStartup_p.h"
#include "common/Constants.h"

#import <Foundation/Foundation.h>
#import <ServiceManagement/ServiceManagement.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QUuid>
#include <cerrno>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

namespace deskflow::gui::personal {
namespace {
constexpr auto kLegacyName = "local.shuai.deskbridge.login.plist";
constexpr auto kLegacyLabel = "local.shuai.deskbridge.login";

QString string(NSString *value)
{
  return value ? QString::fromUtf8(value.UTF8String) : QString{};
}

QString legacyDirectory()
{
  return QDir::homePath() + QStringLiteral("/Library/LaunchAgents");
}

QString installedBundle()
{
  if (QString::fromLatin1(kRuntimeId) != QStringLiteral("local.shuai.deskbridge"))
    return {};
  NSBundle *bundle = NSBundle.mainBundle;
  const auto path = QDir::cleanPath(string(bundle.bundleURL.path));
  const auto homeApp = QDir::homePath() + QStringLiteral("/Applications/DeskBridge.app");
  if (path != QStringLiteral("/Applications/DeskBridge.app") && path != homeApp)
    return {};
  if (string(bundle.bundleIdentifier) != QString::fromLatin1(kRuntimeId) ||
      QFileInfo(path).isSymLink() || QFileInfo(path).canonicalFilePath() != path ||
      string(bundle.executableURL.path) != path + QStringLiteral("/Contents/MacOS/DeskBridge"))
    return {};
  return path;
}

struct FileDescriptor
{
  int value = -1;
  explicit FileDescriptor(int fd) : value(fd) {}
  ~FileDescriptor() { if (value >= 0) ::close(value); }
};

struct Legacy
{
  bool present = false;
  bool trusted = false;
  QByteArray contents;
  struct stat identity = {};
  QString error;
};

QString unsafeLegacyMessage()
{
  return LoginStartup::tr("已有同名登录启动文件无法确认为桌桥创建；未更改该文件。");
}

detail::LoginServiceState serviceState(SMAppServiceStatus status)
{
  switch (status) {
  case SMAppServiceStatusNotRegistered: return detail::LoginServiceState::NotRegistered;
  case SMAppServiceStatusEnabled: return detail::LoginServiceState::Enabled;
  case SMAppServiceStatusRequiresApproval: return detail::LoginServiceState::RequiresApproval;
  case SMAppServiceStatusNotFound: return detail::LoginServiceState::NotFound;
  }
  return detail::LoginServiceState::Unknown;
}

bool trustedArguments(NSArray *arguments)
{
  if (![arguments isKindOfClass:NSArray.class])
    return false;
  for (id argument in arguments)
    if (![argument isKindOfClass:NSString.class])
      return false;
  const auto systemApp = QStringLiteral("/Applications/DeskBridge.app");
  const auto userApp = QDir::homePath() + QStringLiteral("/Applications/DeskBridge.app");
  QString candidate;
  if (arguments.count == 1) {
    const auto executable = string(arguments[0]);
    for (const auto &app : {systemApp, userApp})
      if (executable == app + QStringLiteral("/Contents/MacOS/DeskBridge"))
        candidate = app;
  } else if (arguments.count == 4 && string(arguments[0]) == QStringLiteral("/usr/bin/open") &&
             string(arguments[1]) == QStringLiteral("-g") && string(arguments[2]) == QStringLiteral("-a")) {
    candidate = string(arguments[3]);
    if (candidate != systemApp && candidate != userApp)
      return false;
  }
  if (candidate.isEmpty() || QFileInfo(candidate).isSymLink() ||
      QFileInfo(candidate).canonicalFilePath() != candidate)
    return false;
  const auto data = candidate.toUtf8();
  NSBundle *bundle = [NSBundle bundleWithPath:[NSString stringWithUTF8String:data.constData()]];
  return string(bundle.bundleIdentifier) == QString::fromLatin1(kRuntimeId) &&
         string(bundle.executableURL.path) == candidate + QStringLiteral("/Contents/MacOS/DeskBridge");
}

Legacy inspectLegacyAt(int directory, const char *filename)
{
  Legacy result;
  FileDescriptor file(::openat(directory, filename, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
  if (file.value < 0) {
    if (errno != ENOENT) {
      result.present = true;
      result.error = unsafeLegacyMessage();
    }
    return result;
  }
  result.present = true;
  if (::fstat(file.value, &result.identity) != 0 || !S_ISREG(result.identity.st_mode) ||
      result.identity.st_uid != ::getuid() || result.identity.st_nlink != 1 ||
      (result.identity.st_mode & (S_IWGRP | S_IWOTH)) ||
      result.identity.st_size <= 0 || result.identity.st_size > 65536) {
    result.error = unsafeLegacyMessage();
    return result;
  }
  QFile input;
  if (!input.open(file.value, QIODevice::ReadOnly, QFileDevice::DontCloseHandle)) {
    result.error = unsafeLegacyMessage();
    return result;
  }
  result.contents = input.read(65537);
  if (input.error() != QFileDevice::NoError || result.contents.size() != result.identity.st_size) {
    result.error = unsafeLegacyMessage();
    return result;
  }
  NSData *data = [NSData dataWithBytes:result.contents.constData() length:result.contents.size()];
  id plist = [NSPropertyListSerialization propertyListWithData:data options:NSPropertyListImmutable format:nil error:nil];
  if (![plist isKindOfClass:NSDictionary.class]) {
    result.error = unsafeLegacyMessage();
    return result;
  }
  NSDictionary *dictionary = plist;
  // Only the known, inert RunAtLoad template is ours. Extra launch triggers,
  // environment overrides, executable arguments or a different owner fail closed.
  if (dictionary.count != 6 || ![dictionary[@"Label"] isEqual:@"local.shuai.deskbridge.login"] ||
      ![dictionary[@"AssociatedBundleIdentifiers"] isEqual:@[@"local.shuai.deskbridge"]] ||
      ![dictionary[@"LimitLoadToSessionType"] isEqual:@"Aqua"] ||
      ![dictionary[@"ProcessType"] isEqual:@"Interactive"] || dictionary[@"RunAtLoad"] != (__bridge id)kCFBooleanTrue ||
      !trustedArguments(dictionary[@"ProgramArguments"])) {
    result.error = unsafeLegacyMessage();
    return result;
  }
  result.trusted = true;
  return result;
}

Legacy inspectLegacy()
{
  Legacy result;
  const auto directory = legacyDirectory();
  struct stat directoryState = {};
  if (::lstat(QFile::encodeName(directory).constData(), &directoryState) != 0) {
    if (errno != ENOENT)
      result.error = LoginStartup::tr("无法读取旧版登录启动目录。");
    return result;
  }
  const QFileInfo directoryInfo(directory);
  if (!S_ISDIR(directoryState.st_mode) || directoryInfo.isSymLink() || directoryInfo.canonicalFilePath() != directory) {
    result.present = true;
    result.error = unsafeLegacyMessage();
    return result;
  }
  FileDescriptor dir(::open(QFile::encodeName(directory).constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  if (dir.value < 0) {
    result.error = LoginStartup::tr("无法读取旧版登录启动目录。");
    return result;
  }
  return inspectLegacyAt(dir.value, kLegacyName);
}

detail::LegacyDisabledStatus legacyDisabled()
{
  QProcess process;
  process.setProgram(QStringLiteral("/bin/launchctl"));
  process.setArguments({QStringLiteral("print-disabled"), QStringLiteral("gui/%1").arg(::getuid())});
  process.start();
  if (!process.waitForStarted(1000) || !process.waitForFinished(1500)) {
    process.kill();
    process.waitForFinished(100);
    return {};
  }
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    return {};
  return detail::parseLegacyDisabledOutput(process.readAllStandardOutput(), QString::fromLatin1(kLegacyLabel));
}

QString removeLegacy()
{
  const auto before = inspectLegacy();
  if (!before.error.isEmpty())
    return before.error;
  if (!before.present)
    return {};
  if (!before.trusted)
    return unsafeLegacyMessage();
  FileDescriptor dir(::open(QFile::encodeName(legacyDirectory()).constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  if (dir.value < 0)
    return LoginStartup::tr("无法移除旧版登录启动文件。");
  const auto stagingName = (QStringLiteral(".deskbridge-login-") + QUuid::createUuid().toString(QUuid::WithoutBraces) +
                            QStringLiteral(".removing")).toUtf8();
  // Atomically move this directory entry out of the launchd plist namespace.
  // A concurrent replacement is inspected at the private staging name before
  // deletion, and is restored without overwriting another entry if it is foreign.
  if (::renameatx_np(dir.value, kLegacyName, dir.value, stagingName.constData(), RENAME_EXCL) != 0) {
    if (errno == ENOENT)
      return {};
    return LoginStartup::tr("无法准备移除旧版登录启动文件。");
  }
  const auto restore = [&] {
    if (::renameatx_np(dir.value, stagingName.constData(), dir.value, kLegacyName, RENAME_EXCL) == 0)
      return LoginStartup::tr("旧版登录启动文件在操作期间发生变化，已保留。请刷新后重试。");
    return LoginStartup::tr("旧版登录启动文件已保留为 %1。请刷新状态并检查该文件。")
        .arg(legacyDirectory() + QLatin1Char('/') + QString::fromUtf8(stagingName));
  };
  const auto staged = inspectLegacyAt(dir.value, stagingName.constData());
  if (!staged.trusted || staged.identity.st_dev != before.identity.st_dev || staged.identity.st_ino != before.identity.st_ino ||
      staged.contents != before.contents)
    return restore();
  if (::unlinkat(dir.value, stagingName.constData(), 0) != 0 && errno != ENOENT)
    return restore();
  return {};
}

LoginStartup::Status readNative()
{
  @autoreleasepool {
    LoginStartup::Status result;
    // Test executables and dist copies must return before reading user files
    // or asking ServiceManagement about the real installed application.
    if (installedBundle().isEmpty()) {
      result.message = LoginStartup::tr("请从“应用程序”中的桌桥使用登录启动。");
      return result;
    }
    if (@available(macOS 13.0, *)) {
      result.supported = true;
      const auto modern = serviceState(SMAppService.mainAppService.status);
      const auto modernRegistration = detail::effectiveLoginRegistration(modern);
      result.enabled = modernRegistration.enabled;
      result.approvalRequired = modernRegistration.approvalRequired;
      if (!modernRegistration.known)
        result.error = LoginStartup::tr("系统返回了未知的桌桥登录启动状态。");
      const auto legacy = inspectLegacy();
      if (!legacy.error.isEmpty())
        result.error = legacy.error;
      if (legacy.trusted && !modernRegistration.enabled) {
        const auto disabled = legacyDisabled();
        if (!disabled.known) {
          result.error = LoginStartup::tr("无法确认旧版登录启动是否已被系统停用。");
        } else if (!disabled.disabled) {
          const auto path = (legacyDirectory() + QLatin1Char('/') + QString::fromLatin1(kLegacyName)).toUtf8();
          const auto legacyState = [SMAppService statusForLegacyURL:
              [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.constData()]]];
          const auto registration = detail::effectiveLoginRegistration(
              modern, detail::serviceRegistration(serviceState(legacyState)));
          result.enabled = registration.enabled;
          result.approvalRequired = registration.approvalRequired;
          if (!registration.known || legacyState == SMAppServiceStatusNotRegistered || legacyState == SMAppServiceStatusNotFound)
            result.error = LoginStartup::tr("系统无法确认旧版登录启动登记。");
        }
      }
      if (result.approvalRequired)
        result.message = LoginStartup::tr("已登记，需在系统设置的“登录项与扩展”中允许桌桥。");
      else if (result.enabled)
        result.message = LoginStartup::tr("将在下次登录 Mac 时打开桌桥。");
      else
        result.message = LoginStartup::tr("登录 Mac 时不自动打开桌桥。");
      if (!result.error.isEmpty())
        result.message = result.error;
      return result;
    }
    result.message = LoginStartup::tr("登录启动需要 macOS 13 或更新版本。");
    return result;
  }
}

QString nativeError(NSError *error)
{
  if (!error)
    return LoginStartup::tr("系统未能修改桌桥登录启动。");
  return QStringLiteral("%1 (%2:%3)").arg(string(error.localizedDescription), string(error.domain)).arg(error.code);
}

LoginStartup::ChangeResult changeNative(bool enabled)
{
  @autoreleasepool {
    LoginStartup::ChangeResult result;
    result.status = readNative();
    if (!result.status.supported || !result.status.error.isEmpty()) {
      result.error = result.status.error.isEmpty() ? result.status.message : result.status.error;
      return result;
    }
    if (@available(macOS 13.0, *)) {
      SMAppService *service = SMAppService.mainAppService;
      const auto action = detail::modernLoginAction(serviceState(service.status), enabled);
      NSError *error = nil;
      BOOL success = YES;
      if (action == detail::LoginServiceAction::Register)
        success = [service registerAndReturnError:&error];
      else if (action == detail::LoginServiceAction::Unregister)
        success = [service unregisterAndReturnError:&error];
      else if (action == detail::LoginServiceAction::Reject) {
        result.error = LoginStartup::tr("系统返回了未知的桌桥登录启动状态，请刷新后重试。");
        success = NO;
      }
      if (!success) {
        if (result.error.isEmpty())
          result.error = nativeError(error);
      } else if (!enabled || service.status == SMAppServiceStatusEnabled) {
        // Removing the verified file affects subsequent logins. Unregistering
        // an agent/bootout would kill the currently running GUI, so never do it.
        result.error = removeLegacy();
      }
      result.status = readNative();
      if (result.error.isEmpty() && !result.status.error.isEmpty())
        result.error = result.status.error;
      const bool desired = enabled ? result.status.enabled : (!result.status.enabled && !result.status.approvalRequired);
      if (result.error.isEmpty() && !desired)
        result.error = LoginStartup::tr("系统状态尚未确认此更改，请刷新登录启动状态后重试。");
      result.success = success && result.error.isEmpty() && desired;
      return result;
    }
    result.error = result.status.message;
    return result;
  }
}
} // namespace

LoginStartup::Operations nativeLoginStartupOperations()
{
  return {
      [] { return readNative(); },
      [](bool enabled) { return changeNative(enabled); },
      [] {
        @autoreleasepool {
          if (!installedBundle().isEmpty())
            if (@available(macOS 13.0, *))
              [SMAppService openSystemSettingsLoginItems];
        }
      }
  };
}

} // namespace deskflow::gui::personal
