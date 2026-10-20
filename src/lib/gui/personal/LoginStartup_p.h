/*
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <QByteArray>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <optional>

namespace deskflow::gui::personal::detail {

enum class LoginServiceState { NotRegistered, Enabled, RequiresApproval, NotFound, Unknown };
enum class LoginServiceAction { None, Register, Unregister, Reject };

struct LoginRegistration
{
  bool known = false;
  bool enabled = false;
  bool approvalRequired = false;
};

inline LoginRegistration serviceRegistration(LoginServiceState state)
{
  switch (state) {
  case LoginServiceState::Enabled: return {true, true, false};
  case LoginServiceState::RequiresApproval: return {true, true, true};
  // NotFound can also mean ServiceManagement has never seen this service.
  // It does not invalidate a verified legacy registration or forbid register().
  // Apple DTS: https://developer.apple.com/forums/thread/719862
  case LoginServiceState::NotRegistered:
  case LoginServiceState::NotFound: return {true, false, false};
  case LoginServiceState::Unknown: return {};
  }
  return {};
}

inline LoginRegistration effectiveLoginRegistration(
    LoginServiceState modern, std::optional<LoginRegistration> legacy = std::nullopt)
{
  const auto registration = serviceRegistration(modern);
  return !registration.known || registration.enabled ? registration : legacy.value_or(registration);
}

inline LoginServiceAction modernLoginAction(LoginServiceState state, bool enabled)
{
  const auto registration = serviceRegistration(state);
  if (!registration.known)
    return LoginServiceAction::Reject;
  if (enabled)
    return registration.enabled ? LoginServiceAction::None : LoginServiceAction::Register;
  return registration.enabled ? LoginServiceAction::Unregister : LoginServiceAction::None;
}

struct LegacyDisabledStatus
{
  bool known = false;
  bool disabled = false;
};

// launchctl changed its printed boolean tokens on newer macOS versions. Parse
// the complete dictionary conservatively and match the label as a full string.
// No matching entry in otherwise valid output means no disabled override.
inline LegacyDisabledStatus parseLegacyDisabledOutput(const QByteArray &output, const QString &label)
{
  if (output.size() > 1024 * 1024)
    return {};
  const auto lines = QString::fromUtf8(output).trimmed().split(QLatin1Char('\n'));
  if (lines.size() < 2 || lines.first().trimmed() != QStringLiteral("disabled services = {") ||
      lines.last().trimmed() != QStringLiteral("}"))
    return {};
  static const QRegularExpression entry(
      QStringLiteral("^\\s*\"((?:[^\"\\\\]|\\\\.)*)\"\\s*=>\\s*(true|false|enabled|disabled)\\s*$"));
  LegacyDisabledStatus result{true, false};
  QSet<QString> seen;
  for (int index = 1; index < lines.size() - 1; ++index) {
    if (lines[index].trimmed().isEmpty())
      continue;
    const auto match = entry.match(lines[index]);
    if (!match.hasMatch() || seen.contains(match.captured(1)))
      return {};
    seen.insert(match.captured(1));
    if (match.captured(1) == label) {
      const auto token = match.captured(2);
      result.disabled = token == QStringLiteral("true") || token == QStringLiteral("disabled");
    }
  }
  return result;
}

} // namespace deskflow::gui::personal::detail
