/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2021 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AliasValidator.h"

#include "ComputerNameValidator.h"
#include "IpAddressValidator.h"
#include "SpacesValidator.h"
#include "ValidationError.h"

#include <QLineEdit>
#include <QRegularExpression>

namespace validators {

AliasValidator::AliasValidator(QLineEdit *parent, ValidationError *error) : LineEditValidator(parent, error)
{
  addValidator(std::make_unique<SpacesValidator>(tr("设备名称不能包含空格")));
  addValidator(std::make_unique<IpAddressValidator>(tr("别名不能使用 IP 地址")));
  addValidator(std::make_unique<ComputerNameValidator>(tr("名称含有无效字符，或超过 255 个字符")));
}

} // namespace validators
