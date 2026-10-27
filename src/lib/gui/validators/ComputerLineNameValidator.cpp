/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2021 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ComputerLineNameValidator.h"

#include "ComputerDuplicationsValidator.h"
#include "ComputerNameValidator.h"
#include "EmptyStringValidator.h"
#include "SpacesValidator.h"
#include "ValidationError.h"

#include "gui/config/ComputerList.h"

#include <QLineEdit>
#include <QRegularExpression>
#include <memory>

namespace validators {

ComputerLineNameValidator::ComputerLineNameValidator(
    QLineEdit *lineEdit, ValidationError *error, const ComputerList *pComputers
)
    : LineEditValidator(lineEdit, error)
{
  addValidator(std::make_unique<EmptyStringValidator>(tr("设备名称不能为空")));
  addValidator(std::make_unique<SpacesValidator>(tr("设备名称不能包含空格")));
  addValidator(std::make_unique<ComputerNameValidator>(tr("名称含有无效字符，或超过 255 个字符")));
  addValidator(
      std::make_unique<ComputerDuplicationsValidator>(
          tr("已有设备使用此名称或别名"), lineEdit ? lineEdit->text() : "", pComputers
      )
  );
}

} // namespace validators
