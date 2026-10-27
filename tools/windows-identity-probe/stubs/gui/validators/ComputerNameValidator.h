// SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
// SPDX-License-Identifier: MIT
#pragma once

#include <QRegularExpression>
#include <QString>

namespace validators {
class ComputerNameValidator
{
public:
  explicit ComputerNameValidator(const QString &) {}
  bool validate(const QString &name) const
  {
    // The probe uses valid ASCII identities only; validator behavior is tested
    // by the full application suites, not by this QSettings mechanism probe.
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_.-]{1,255}$"));
    return valid.match(name).hasMatch();
  }
};
} // namespace validators
