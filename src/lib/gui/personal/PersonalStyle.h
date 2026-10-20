/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#pragma once

#include <QColor>
#include <QIcon>
#include "common/ShortcutRules.h"
class QWidget;

namespace deskflow::gui::personal {
QColor personalAccentColor();
void applyPersonalStyle(QWidget *widget);
QIcon personalIcon(const QString &name, const QColor &color);
QIcon personalCloseIcon();
QIcon personalDisclosureIcon(bool expanded);
QString displayOperatingSystem(shortcuts::DeviceOS os);
} // namespace deskflow::gui::personal
